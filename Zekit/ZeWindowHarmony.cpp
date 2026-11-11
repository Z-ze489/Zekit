
#include "pch.h"

#if defined(__OHOS__)

#include "ZeWindowHarmony.h"
#include "ZeMobileWindow.h"

#include <ace/xcomponent/native_interface_xcomponent.h>
#include <ace/xcomponent/native_xcomponent_key_event.h>
#include <native_window/external_window.h>
#include <napi/native_api.h>
#include <hilog/log.h>

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#include <pthread.h>
#include <unistd.h>

extern "C" int ZekitHarmonyAppMain();

namespace Plat {

	constexpr unsigned int kZekitLogDomain = 0x0201;

	namespace {
		void LogLine(const char* line) {
			if (!line || !*line) return;
			OH_LOG_Print(LOG_APP, LOG_ERROR, kZekitLogDomain, "ZekitStderr",
				"%{public}s", line);
		}
	}

	namespace {
		int  g_logPipe[2] = { -1, -1 };

		void* LogPump(void*) {
			char buf[512];
			ssize_t n;
			while ((n = ::read(g_logPipe[0], buf, sizeof(buf) - 1)) > 0) {
				buf[n] = '\0';
				char* p = buf;
				for (;;) {
					char* nl = std::strchr(p, '\n');
					if (nl) *nl = '\0';
					if (*p) LogLine(p);
					if (!nl) break;
					p = nl + 1;
				}
			}
			return nullptr;
		}

		void RedirectStdioToHilog() {
			if (g_logPipe[0] >= 0) return;
			if (::pipe(g_logPipe) != 0) return;
			::dup2(g_logPipe[1], STDOUT_FILENO);
			::dup2(g_logPipe[1], STDERR_FILENO);
			std::setvbuf(stdout, nullptr, _IOLBF, 0);
			std::setvbuf(stderr, nullptr, _IONBF, 0);
			pthread_t t;
			if (::pthread_create(&t, nullptr, LogPump, nullptr) == 0)
				::pthread_detach(t);
		}
	}

	struct HarmonySession {
		OH_NativeXComponent* xcomponent = nullptr;
		void* nativeWindow = nullptr;
		bool  surfaceAlive = false;

		static constexpr int kQueueCap = 64;
		MobileEvent queue[kQueueCap];
		int         qHead = 0, qTail = 0;
		std::mutex  qMutex;
		std::condition_variable qCv;

		int  clientVpW = 0, clientVpH = 0;

		int  surfPxW = 0, surfPxH = 0;

		int  screenPxW = 0, screenPxH = 0;

		int  winPxW = 0, winPxH = 0;

		float density = 1.0f;
		bool  densityKnown = false;

		float safeL = 0, safeT = 0, safeR = 0, safeB = 0;

		int keyboardHeightVp = 0;
		bool kbVisible = false;

		bool hasFocus = false;
		bool immersive = false;
		bool systemDark = false;

		bool mouseChannelAlive = false;

		int lastPhysW = 0, lastPhysH = 0;

		bool modShift = false, modCtrl = false, modAlt = false;
		bool modSuper = false;

		bool appStarted = false;

		napi_env  env = nullptr;
		napi_threadsafe_function jsCall = nullptr;

		bool JsReady() const { return jsCall != nullptr; }

		void Push(const MobileEvent& e) {
			{
				std::lock_guard<std::mutex> lk(qMutex);
				const int next = (qTail + 1) % kQueueCap;
				if (next == qHead) qHead = (qHead + 1) % kQueueCap;
				queue[qTail] = e;
				qTail = next;
			}
			qCv.notify_all();
		}
		bool Pop(MobileEvent& out) {
			std::lock_guard<std::mutex> lk(qMutex);
			if (qHead == qTail) return false;
			out = queue[qHead];
			qHead = (qHead + 1) % kQueueCap;
			return true;
		}
		bool Empty() {
			std::lock_guard<std::mutex> lk(qMutex);
			return qHead == qTail;
		}
	};

	HarmonySession* g_session = nullptr;

	HarmonySession* Session() { return g_session; }

	float Density() {
		HarmonySession* S = Session();
		if (!S) return 1.0f;
		if (S->density > 0.01f) return S->density;
		return 1.0f;
	}

	float ToLogical(float px) {
		const float d = Density();
		return (d > 0.01f) ? px / d : px;
	}

	void ResolveSurfaceSize(OH_NativeXComponent* comp, void* window) {
		HarmonySession* S = Session();
		if (!S || !comp || !window) return;

		uint64_t rawW = 0, rawH = 0;
		if (OH_NativeXComponent_GetXComponentSize(comp, window, &rawW, &rawH)
			!= OH_NATIVEXCOMPONENT_RESULT_SUCCESS || rawW == 0 || rawH == 0) {
			return;
		}

		const float d = Density();
		int pxW = static_cast<int>(rawW);
		int pxH = static_cast<int>(rawH);
		bool judgedVp = false;
		const char* why = "（无标尺，按实测 px）";

		if (S->winPxW > 0 && S->winPxH > 0 && d > 0.01f) {
			const double aw = static_cast<double>(rawW);
			const double ah = static_cast<double>(rawH);
			const double bw = aw * static_cast<double>(d);
			const double bh = ah * static_cast<double>(d);
			const double winW = static_cast<double>(S->winPxW);
			const double winH = static_cast<double>(S->winPxH);

			const double errA = std::abs(aw - winW) / winW + std::abs(ah - winH) / winH;
			const double errB = std::abs(bw - winW) / winW + std::abs(bh - winH) / winH;
			judgedVp = (errB < errA);
			why = judgedVp ? "（更贴近窗口尺寸 ⇒ vp）" : "（更贴近窗口尺寸 ⇒ px）";
		}
		else if (S->screenPxW > 0 && S->screenPxH > 0 && d > 0.01f) {
			const double asVpW = static_cast<double>(rawW) * static_cast<double>(d);
			const double asVpH = static_cast<double>(rawH) * static_cast<double>(d);
			const bool vpWouldOverflow =
				(asVpW > static_cast<double>(S->screenPxW) + 1.0) ||
				(asVpH > static_cast<double>(S->screenPxH) + 1.0);

			if (vpWouldOverflow) {
				judgedVp = false;
				why = "（raw×d 超过整屏 ⇒ 必是 px）";
			} else {
				const double asPxArea = static_cast<double>(rawW) * static_cast<double>(rawH);
				const double asVpArea = asPxArea * static_cast<double>(d) * static_cast<double>(d);
				const double screenArea = static_cast<double>(S->screenPxW)
					* static_cast<double>(S->screenPxH);
				const double dPx = (asPxArea > screenArea) ? (asPxArea - screenArea)
					: (screenArea - asPxArea);
				const double dVp = (asVpArea > screenArea) ? (asVpArea - screenArea)
					: (screenArea - asVpArea);
				judgedVp = (dVp < dPx);
				why = judgedVp ? "（面积更近整屏 ⇒ vp）" : "（面积更近整屏 ⇒ px）";
			}
		}

		if (judgedVp) {
			pxW = static_cast<int>(static_cast<double>(rawW) * d + 0.5);
			pxH = static_cast<int>(static_cast<double>(rawH) * d + 0.5);
		}

		S->surfPxW = pxW;
		S->surfPxH = pxH;
		S->clientVpW = static_cast<int>(static_cast<double>(pxW) / d + 0.5);
		S->clientVpH = static_cast<int>(static_cast<double>(pxH) / d + 0.5);

		std::fprintf(stderr,
			"[Harmony] surface 尺寸：raw=%ux%u（判为 %s）%s→ 物理 %dx%d px，"
			"逻辑 %dx%d vp，密度 %.3f，整屏 %dx%d px\n",
			static_cast<unsigned>(rawW), static_cast<unsigned>(rawH),
			judgedVp ? "vp（按文档口径）" : "px（实测口径）", why,
			pxW, pxH, S->clientVpW, S->clientVpH,
			static_cast<double>(d), S->screenPxW, S->screenPxH);

		if (!S->densityKnown) {
			std::fprintf(stderr,
				"[Harmony] ⚠ 密度未知（ArkTS 的 display.densityPixels 没送到）——"
				"逻辑尺寸会按 1.0 算，界面将整体放大、多半只剩左上角可见\n");
		}
	}

	bool SurfacePhysicalSize(int& w, int& h) {
		w = h = 0;
		HarmonySession* S = Session();
		if (!S || !S->nativeWindow || S->surfPxW <= 0 || S->surfPxH <= 0)
			return false;
		w = S->surfPxW;
		h = S->surfPxH;
		return true;
	}

	namespace {
		struct JsRequest {
			std::string action;
			std::string arg;
			double      num = 0.0;
			std::string result;
			bool        done = false;
			bool        ownsItself = false;
			std::mutex              m;
			std::condition_variable cv;
		};

		void JsCallCb(napi_env env, napi_value jsCb, void* , void* data) {
			JsRequest* req = static_cast<JsRequest*>(data);
			if (!req) return;

			napi_value fn = jsCb;
			napi_value undef = nullptr;
			napi_get_undefined(env, &undef);

			napi_value args[3];
			napi_create_string_utf8(env, req->action.c_str(), NAPI_AUTO_LENGTH, &args[0]);
			napi_create_string_utf8(env, req->arg.c_str(), NAPI_AUTO_LENGTH, &args[1]);
			napi_create_double(env, req->num, &args[2]);

			napi_value ret = undef;
			const napi_status st = napi_call_function(env, undef, fn, 3, args, &ret);

			if (st == napi_ok && ret) {
				size_t len = 0;
				napi_valuetype t = napi_undefined;
				napi_typeof(env, ret, &t);
				if (t == napi_string) {
					napi_get_value_string_utf8(env, ret, nullptr, 0, &len);
					if (len > 0) {
						req->result.resize(len);
						size_t written = 0;
						napi_get_value_string_utf8(env, ret, req->result.data(),
							len + 1, &written);
						req->result.resize(written);
					}
				}
			}
			if (st != napi_ok) {
				bool pending = false;
				if (napi_is_exception_pending(env, &pending) == napi_ok && pending) {
					napi_value e = nullptr;
					napi_get_and_clear_last_exception(env, &e);
				}
			}

			if (req->ownsItself) {
				delete req;
				return;
			}

			{
				std::lock_guard<std::mutex> lk(req->m);
				req->done = true;
			}
			req->cv.notify_all();
		}

		std::string JsCallSync(const char* action, const char* arg = "", double num = 0.0) {
			HarmonySession* S = Session();
			if (!S || !S->jsCall) return std::string();

			JsRequest req;
			req.action = action;
			req.arg = arg ? arg : "";
			req.num = num;

			if (napi_call_threadsafe_function(S->jsCall, &req,
				napi_tsfn_blocking) != napi_ok) {
				return std::string();
			}
			std::unique_lock<std::mutex> lk(req.m);
			req.cv.wait(lk, [&req] { return req.done; });
			return req.result;
		}

		void JsCallAsync(const char* action, const char* arg = "", double num = 0.0) {
			HarmonySession* S = Session();
			if (!S || !S->jsCall) return;

			auto* req = new JsRequest();
			req->action = action;
			req->arg = arg ? arg : "";
			req->num = num;
			req->ownsItself = true;

			if (napi_call_threadsafe_function(S->jsCall, req,
				napi_tsfn_nonblocking) != napi_ok) {
				delete req;
			}
		}
	}

	bool SyncSurfaceSize(MobileEvent& out) {
		HarmonySession* S = Session();
		if (!S || S->clientVpW <= 0 || S->clientVpH <= 0) return false;
		if (S->clientVpW == S->lastPhysW && S->clientVpH == S->lastPhysH)
			return false;
		S->lastPhysW = S->clientVpW;
		S->lastPhysH = S->clientVpH;

		out.kind = MobileEventKind::Resized;
		out.width  = S->clientVpW;
		out.height = S->clientVpH;
		return true;
	}

	void RefreshClientSize(OH_NativeXComponent* comp, void* window) {
		ResolveSurfaceSize(comp, window);
	}

	void OnSurfaceCreated(OH_NativeXComponent* component, void* window) {
		HarmonySession* S = Session();
		if (!S) return;

		S->xcomponent  = component;
		S->nativeWindow = window;
		S->surfaceAlive = (window != nullptr);
		S->hasFocus = true;

		RefreshClientSize(component, window);

		MobileEvent e;
		e.kind = MobileEventKind::SurfaceReady;
		e.width  = S->clientVpW;
		e.height = S->clientVpH;
		S->Push(e);

		std::fprintf(stderr,
			"[Harmony] OnSurfaceCreated: window=%p  逻辑 %dx%d vp  密度 %.3f\n",
			window, S->clientVpW, S->clientVpH, static_cast<double>(Density()));

		if (!S->appStarted) {
			S->appStarted = true;

			SetHarmonyHost(S);

			std::thread([] {
				const int code = ::ZekitHarmonyAppMain();
				std::fprintf(stderr,
					"[Harmony] 应用主函数已返回（exit=%d）—— 请求 Ability 收尾\n", code);
				JsCallAsync("app.quit");
			}).detach();
		}
	}

	void OnSurfaceChanged(OH_NativeXComponent* component, void* window) {
		HarmonySession* S = Session();
		if (!S) return;

		const void* prev = S->nativeWindow;

		S->xcomponent = component;
		S->nativeWindow = window;
		S->surfaceAlive = (window != nullptr);
		RefreshClientSize(component, window);

		MobileEvent e;
		if (window && prev && prev != window) {
			e.kind = MobileEventKind::SurfaceLost;
			S->Push(e);
			e = MobileEvent{};
			e.kind = MobileEventKind::SurfaceReady;
			e.width  = S->clientVpW;
			e.height = S->clientVpH;
			S->Push(e);
		} else {
			e.kind = MobileEventKind::Resized;
			e.width  = S->clientVpW;
			e.height = S->clientVpH;
			S->Push(e);
		}

		std::fprintf(stderr, "[Harmony] OnSurfaceChanged: window=%p  逻辑 %dx%d vp\n",
			window, S->clientVpW, S->clientVpH);
	}

	void OnSurfaceDestroyed(OH_NativeXComponent* , void* ) {
		HarmonySession* S = Session();
		if (!S) return;

		S->nativeWindow = nullptr;
		S->surfaceAlive = false;
		S->lastPhysW = S->lastPhysH = 0;

		MobileEvent e;
		e.kind = MobileEventKind::SurfaceLost;
		S->Push(e);

		std::fprintf(stderr,
			"[Harmony] OnSurfaceDestroyed —— 渲染后端将整条拆掉重建\n");
	}

	bool TouchPointIsMouse(OH_NativeXComponent* comp, int32_t pointId, uint32_t index) {
		if (!comp) return false;

		OH_NativeXComponent_EventSourceType st =
			OH_NATIVEXCOMPONENT_SOURCE_TYPE_UNKNOWN;
		if (OH_NativeXComponent_GetTouchEventSourceType(comp, pointId, &st)
		    == OH_NATIVEXCOMPONENT_RESULT_SUCCESS) {
			if (st == OH_NATIVEXCOMPONENT_SOURCE_TYPE_MOUSE) return true;
			if (st == OH_NATIVEXCOMPONENT_SOURCE_TYPE_TOUCHSCREEN) return false;
		}

		OH_NativeXComponent_TouchPointToolType tool =
			OH_NATIVEXCOMPONENT_TOOL_TYPE_UNKNOWN;
		if (OH_NativeXComponent_GetTouchPointToolType(comp, index, &tool)
		    == OH_NATIVEXCOMPONENT_RESULT_SUCCESS) {
			return tool == OH_NATIVEXCOMPONENT_TOOL_TYPE_MOUSE;
		}
		return false;
	}

	void DispatchTouchEvent(OH_NativeXComponent* component, void* window) {
		HarmonySession* S = Session();
		if (!S || !component || !window) return;

		OH_NativeXComponent_TouchEvent te{};
		if (OH_NativeXComponent_GetTouchEvent(component, window, &te)
		    != OH_NATIVEXCOMPONENT_RESULT_SUCCESS) {
			return;
		}

		{
			OH_NativeXComponent_EventSourceType dbgSt =
				OH_NATIVEXCOMPONENT_SOURCE_TYPE_UNKNOWN;
			const int stOk = (OH_NativeXComponent_GetTouchEventSourceType(
				component, te.id, &dbgSt) == OH_NATIVEXCOMPONENT_RESULT_SUCCESS) ? 1 : 0;
			OH_NativeXComponent_TouchPointToolType dbgTool =
				OH_NATIVEXCOMPONENT_TOOL_TYPE_UNKNOWN;
			const int toolOk = (OH_NativeXComponent_GetTouchPointToolType(
				component, 0, &dbgTool) == OH_NATIVEXCOMPONENT_RESULT_SUCCESS) ? 1 : 0;
			std::fprintf(stderr,
				"[Harmony][输入] TOUCH type=%d id=%d x=%.1f y=%.1f "
				"screenX=%.1f screenY=%.1f n=%u | src=%d(ok=%d) tool=%d(ok=%d) "
				"mouseAlive=%d\n",
				static_cast<int>(te.type), static_cast<int>(te.id),
				te.x, te.y, te.screenX, te.screenY,
				static_cast<unsigned>(te.numPoints),
				static_cast<int>(dbgSt), stOk, static_cast<int>(dbgTool), toolOk,
				S->mouseChannelAlive ? 1 : 0);
		}

		MobileEvent e;
		switch (te.type) {
		case OH_NATIVEXCOMPONENT_DOWN:   e.kind = MobileEventKind::TouchBegin;  break;
		case OH_NATIVEXCOMPONENT_MOVE:   e.kind = MobileEventKind::TouchMove;   break;
		case OH_NATIVEXCOMPONENT_UP:     e.kind = MobileEventKind::TouchEnd;    break;
		case OH_NATIVEXCOMPONENT_CANCEL: e.kind = MobileEventKind::TouchCancel; break;
		default: return;
		}

		if (S->mouseChannelAlive &&
		    TouchPointIsMouse(component, te.id, 0)) {
			return;
		}

		uint32_t n = 0;
		const uint32_t srcCount = te.numPoints;
		for (uint32_t i = 0; i < srcCount && n < MobileEvent::kMaxPointers; ++i) {
			e.pointers[n].id = static_cast<uint32_t>(te.touchPoints[i].id);
			e.pointers[n].x  = ToLogical(te.touchPoints[i].x);
			e.pointers[n].y  = ToLogical(te.touchPoints[i].y);
			++n;
		}

		uint32_t ci = 0;
		bool found = false;
		for (uint32_t i = 0; i < n; ++i) {
			if (e.pointers[i].id == static_cast<uint32_t>(te.id)) {
				ci = i; found = true; break;
			}
		}
		if (!found) {
			if (n < MobileEvent::kMaxPointers) {
				e.pointers[n].id = static_cast<uint32_t>(te.id);
				e.pointers[n].x  = ToLogical(te.x);
				e.pointers[n].y  = ToLogical(te.y);
				ci = n;
				++n;
			}
		}
		if (n == 0) return;

		e.pointerCount = n;
		e.changedIndex = ci;
		S->Push(e);
	}

	void DispatchMouseEvent(OH_NativeXComponent* component, void* window) {
		HarmonySession* S = Session();
		if (!S || !component || !window) return;

		OH_NativeXComponent_MouseEvent me{};
		if (OH_NativeXComponent_GetMouseEvent(component, window, &me)
		    != OH_NATIVEXCOMPONENT_RESULT_SUCCESS) {
			return;
		}

		std::fprintf(stderr,
			"[Harmony][输入] MOUSE action=%d x=%.1f y=%.1f screenX=%.1f screenY=%.1f "
			"button=%d ts=%lld\n",
			static_cast<int>(me.action), me.x, me.y, me.screenX, me.screenY,
			static_cast<int>(me.button),
			static_cast<long long>(me.timestamp));

		S->mouseChannelAlive = true;

		MobileEvent e;
		switch (me.action) {
		case OH_NATIVEXCOMPONENT_MOUSE_PRESS:   e.kind = MobileEventKind::TouchBegin; break;
		case OH_NATIVEXCOMPONENT_MOUSE_MOVE:    e.kind = MobileEventKind::TouchMove;  break;
		case OH_NATIVEXCOMPONENT_MOUSE_RELEASE: e.kind = MobileEventKind::TouchEnd;   break;
		case OH_NATIVEXCOMPONENT_MOUSE_CANCEL:  e.kind = MobileEventKind::TouchCancel; break;
		default: return;
		}

		e.pointerCount = 1;
		e.changedIndex = 0;
		e.pointers[0].id = 0;
		e.pointers[0].x  = me.x;
		e.pointers[0].y  = me.y;
		S->Push(e);
	}

	void DispatchHoverEvent(OH_NativeXComponent* , bool ) {
	}

	uint32_t ToKey(OH_NativeXComponent_KeyCode code) {
		if (code >= KEY_A && code <= KEY_Z)
			return static_cast<uint32_t>('A' + (code - KEY_A));
		if (code >= KEY_0 && code <= KEY_9)
			return static_cast<uint32_t>('0' + (code - KEY_0));

		switch (code) {
		case KEY_ENTER:       return 0x0D;
		case KEY_TAB:         return 0x09;
		case KEY_SPACE:       return 0x20;
		case KEY_ESCAPE:      return 0x1B;
		case KEY_DEL:         return 0x08;
		case KEY_FORWARD_DEL: return 0x2E;
		case KEY_INSERT:      return 0x2D;

		case KEY_DPAD_LEFT:  return 0x25;
		case KEY_DPAD_UP:    return 0x26;
		case KEY_DPAD_RIGHT: return 0x27;
		case KEY_DPAD_DOWN:  return 0x28;
		case KEY_MOVE_HOME:  return 0x24;
		case KEY_MOVE_END:   return 0x23;
		case KEY_PAGE_UP:    return 0x21;
		case KEY_PAGE_DOWN:  return 0x22;

		case KEY_SHIFT_LEFT:
		case KEY_SHIFT_RIGHT: return 0x10;
		case KEY_CTRL_LEFT:
		case KEY_CTRL_RIGHT:  return 0x11;
		case KEY_ALT_LEFT:
		case KEY_ALT_RIGHT:   return 0x12;

		case KEY_SEMICOLON:    return 0xBA;
		case KEY_EQUALS:       return 0xBB;
		case KEY_COMMA:        return 0xBC;
		case KEY_MINUS:        return 0xBD;
		case KEY_PERIOD:       return 0xBE;
		case KEY_SLASH:        return 0xBF;
		case KEY_GRAVE:        return 0xC0;
		case KEY_LEFT_BRACKET: return 0xDB;
		case KEY_BACKSLASH:    return 0xDC;
		case KEY_RIGHT_BRACKET:return 0xDD;
		case KEY_APOSTROPHE:   return 0xDE;

		case KEY_VOLUME_MUTE: return 0xAD;
		case KEY_VOLUME_DOWN: return 0xAE;
		case KEY_VOLUME_UP:   return 0xAF;

		default: return 0;
		}
	}

	uint32_t DeriveChar(OH_NativeXComponent_KeyCode code, bool shift) {
		if (code >= KEY_A && code <= KEY_Z)
			return static_cast<uint32_t>('a' + (code - KEY_A)) - (shift ? 32u : 0u);

		static const char kShiftDigits[] = ")!@#$%^&*(";
		if (code >= KEY_0 && code <= KEY_9) {
			const int d = static_cast<int>(code - KEY_0);
			return static_cast<uint32_t>(
				shift ? kShiftDigits[d] : static_cast<char>('0' + d));
		}

		switch (code) {
		case KEY_SPACE:      return ' ';
		case KEY_ENTER:      return '\r';
		case KEY_TAB:        return '\t';
		case KEY_COMMA:      return shift ? '<' : ',';
		case KEY_PERIOD:     return shift ? '>' : '.';
		case KEY_SEMICOLON:  return shift ? ':' : ';';
		case KEY_APOSTROPHE: return shift ? '"' : '\'';
		case KEY_SLASH:      return shift ? '?' : '/';
		case KEY_BACKSLASH:  return shift ? '|' : '\\';
		case KEY_GRAVE:      return shift ? '~' : '`';
		case KEY_MINUS:      return shift ? '_' : '-';
		case KEY_EQUALS:     return shift ? '+' : '=';
		case KEY_LEFT_BRACKET:  return shift ? '{' : '[';
		case KEY_RIGHT_BRACKET: return shift ? '}' : ']';
		default:             return 0;
		}
	}

	Modifiers CurrentMods(const HarmonySession* S) {
		Modifiers m = Modifiers::None;
		if (S->modShift) m = m | Modifiers::Shift;
		if (S->modCtrl)  m = m | Modifiers::Control;
		if (S->modAlt)   m = m | Modifiers::Alt;
		if (S->modSuper) m = m | Modifiers::Super;
		return m;
	}

	void DispatchKeyEvent(OH_NativeXComponent* component, void* ) {
		HarmonySession* S = Session();
		if (!S || !component) return;

		OH_NativeXComponent_KeyEvent* ke = nullptr;
		if (OH_NativeXComponent_GetKeyEvent(component, &ke)
		    != OH_NATIVEXCOMPONENT_RESULT_SUCCESS || !ke) {
			return;
		}

		OH_NativeXComponent_KeyCode code = KEY_UNKNOWN;
		OH_NativeXComponent_KeyAction action = OH_NATIVEXCOMPONENT_KEY_ACTION_UNKNOWN;
		if (OH_NativeXComponent_GetKeyEventCode(ke, &code)
		    != OH_NATIVEXCOMPONENT_RESULT_SUCCESS) return;
		if (OH_NativeXComponent_GetKeyEventAction(ke, &action)
		    != OH_NATIVEXCOMPONENT_RESULT_SUCCESS) return;
		if (action == OH_NATIVEXCOMPONENT_KEY_ACTION_UNKNOWN) return;

		const bool isMod = (code == KEY_SHIFT_LEFT || code == KEY_SHIFT_RIGHT
			|| code == KEY_CTRL_LEFT || code == KEY_CTRL_RIGHT
			|| code == KEY_ALT_LEFT || code == KEY_ALT_RIGHT);
		const bool down = (action == OH_NATIVEXCOMPONENT_KEY_ACTION_DOWN);
		if (isMod) {
			const bool on = down;
			if (code == KEY_SHIFT_LEFT || code == KEY_SHIFT_RIGHT) S->modShift = on;
			else if (code == KEY_CTRL_LEFT || code == KEY_CTRL_RIGHT) S->modCtrl = on;
			else S->modAlt = on;
		}

		MobileEvent e;
		e.kind = down ? MobileEventKind::KeyDown : MobileEventKind::KeyUp;
		e.key = ToKey(code);
		e.rawKeyCode = static_cast<uint32_t>(code);
		e.mods = CurrentMods(S);
		e.unicodeChar = down ? DeriveChar(code, S->modShift) : 0;
		S->Push(e);
	}

	void DispatchFocusEvent(OH_NativeXComponent* , void* , bool hasFocus) {
		HarmonySession* S = Session();
		if (!S) return;
		if (S->hasFocus == hasFocus) return;
		S->hasFocus = hasFocus;
		MobileEvent e;
		e.kind = hasFocus ? MobileEventKind::FocusGained
			: MobileEventKind::FocusLost;
		S->Push(e);
	}

	void OnKeyEventThunk(OH_NativeXComponent* c, void* w) { DispatchKeyEvent(c, w); }
	void OnFocusGainedThunk(OH_NativeXComponent* c, void* w) { DispatchFocusEvent(c, w, true); }
	void OnFocusLostThunk(OH_NativeXComponent* c, void* w) { DispatchFocusEvent(c, w, false); }

	namespace {

		double ArgNum(napi_env env, napi_value* argv, size_t argc,
			size_t i, double def = 0.0) {
			if (i >= argc || !argv[i]) return def;
			double v = def;
			if (napi_get_value_double(env, argv[i], &v) != napi_ok) {
				int32_t iv = static_cast<int32_t>(def);
				if (napi_get_value_int32(env, argv[i], &iv) == napi_ok)
					v = static_cast<double>(iv);
			}
			return v;
		}

		std::string ArgStr(napi_env env, napi_value* argv, size_t argc, size_t i) {
			std::string out;
			if (i >= argc || !argv[i]) return out;
			napi_valuetype t = napi_undefined;
			if (napi_typeof(env, argv[i], &t) != napi_ok || t != napi_string)
				return out;
			size_t len = 0;
			if (napi_get_value_string_utf8(env, argv[i], nullptr, 0, &len) != napi_ok)
				return out;
			out.resize(len);
			size_t written = 0;
			napi_get_value_string_utf8(env, argv[i], out.data(), len + 1, &written);
			out.resize(written);
			return out;
		}

		napi_value NapiAttachBridge(napi_env env, napi_callback_info info) {
			HarmonySession* S = Session();
			size_t argc = 1;
			napi_value argv[1] = { nullptr };
			napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);

			napi_value undef = nullptr;
			napi_get_undefined(env, &undef);
			if (!S || argc < 1) return undef;

			napi_valuetype t = napi_undefined;
			if (napi_typeof(env, argv[0], &t) != napi_ok || t != napi_function)
				return undef;

			if (S->jsCall) {
				napi_release_threadsafe_function(S->jsCall, napi_tsfn_release);
				S->jsCall = nullptr;
			}

			napi_value resName = nullptr;
			napi_create_string_utf8(env, "zekit.bridge", NAPI_AUTO_LENGTH, &resName);

			if (napi_create_threadsafe_function(env, argv[0], nullptr, resName,
				0, 1, nullptr, nullptr, nullptr,
				JsCallCb, &S->jsCall) != napi_ok) {
				S->jsCall = nullptr;
				std::fprintf(stderr, "[Harmony] 建立 ArkTS 桥失败 —— "
					"软键盘/系统栏/剪贴板/安全区将按\"未实现\"降级\n");
			}
			return undef;
		}

		napi_value NapiNotifyState(napi_env env, napi_callback_info info) {
			size_t argc = 2;
			napi_value argv[2] = { nullptr, nullptr };
			napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);
			napi_value undef = nullptr;
			napi_get_undefined(env, &undef);

			HarmonySession* S = Session();
			if (!S) return undef;

			const std::string name = ArgStr(env, argv, argc, 0);
			const double v = ArgNum(env, argv, argc, 1);

			MobileEvent e;
			if (name == "density") {
				if (v > 0.1 && v < 20.0) {
					S->density = static_cast<float>(v);
					S->densityKnown = true;
				}
			} else if (name == "screenPxW") {
				if (v > 0.0 && v < 100000.0) S->screenPxW = static_cast<int>(v + 0.5);
			} else if (name == "screenPxH") {
				if (v > 0.0 && v < 100000.0) S->screenPxH = static_cast<int>(v + 0.5);
			} else if (name == "winPxW") {
				if (v > 0.0 && v < 100000.0) S->winPxW = static_cast<int>(v + 0.5);
			} else if (name == "winPxH") {
				if (v > 0.0 && v < 100000.0) S->winPxH = static_cast<int>(v + 0.5);
			} else if (name == "darkmode") {
				const bool dark = (v != 0.0);
				if (dark != S->systemDark) {
					S->systemDark = dark;
					e.kind = MobileEventKind::ConfigChanged;
					S->Push(e);
				}
			} else if (name == "keyboardHeight") {
				S->keyboardHeightVp = static_cast<int>(v + 0.5);
			} else if (name == "focus") {
				const bool f = (v != 0.0);
				if (f != S->hasFocus) {
					S->hasFocus = f;
					e.kind = f ? MobileEventKind::FocusGained
						: MobileEventKind::FocusLost;
					S->Push(e);
				}
			} else if (name == "immersive") {
				S->immersive = (v != 0.0);
			} else if (name == "lifecycle") {
				switch (static_cast<int>(v + 0.5)) {
				case 0: e.kind = MobileEventKind::AppResumed;     break;
				case 1: e.kind = MobileEventKind::AppPaused;      break;
				case 2: e.kind = MobileEventKind::AppLowMemory;   break;
				default: e.kind = MobileEventKind::AppTerminating; break;
				}
				S->Push(e);
			} else if (name == "quit") {
				e.kind = MobileEventKind::QuitRequested;
				S->Push(e);
			}
			return undef;
		}

		napi_value NapiNotifySafeArea(napi_env env, napi_callback_info info) {
			size_t argc = 4;
			napi_value argv[4] = {};
			napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);
			napi_value undef = nullptr;
			napi_get_undefined(env, &undef);

			HarmonySession* S = Session();
			if (!S) return undef;

			S->safeL = static_cast<float>(ArgNum(env, argv, argc, 0));
			S->safeT = static_cast<float>(ArgNum(env, argv, argc, 1));
			S->safeR = static_cast<float>(ArgNum(env, argv, argc, 2));
			S->safeB = static_cast<float>(ArgNum(env, argv, argc, 3));
			return undef;
		}

		napi_value NapiNotifyText(napi_env env, napi_callback_info info) {
			size_t argc = 1;
			napi_value argv[1] = { nullptr };
			napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);
			napi_value undef = nullptr;
			napi_get_undefined(env, &undef);

			HarmonySession* S = Session();
			if (!S) return undef;

			const std::string s = ArgStr(env, argv, argc, 0);
			if (s.empty()) return undef;

			MobileEvent e;
			if (s == "\b") {
				e.kind = MobileEventKind::KeyDown;
				e.key = 0x08;
				S->Push(e);
				return undef;
			}
			if (s == "\n") {
				e.kind = MobileEventKind::KeyDown;
				e.key = 0x0D;
				S->Push(e);
				return undef;
			}

			std::size_t i = 0;
			while (i < s.size()) {
				const unsigned char c = static_cast<unsigned char>(s[i]);
				uint32_t cp = 0;
				int extra = 0;
				if (c < 0x80)                { cp = c;         extra = 0; }
				else if ((c & 0xE0) == 0xC0) { cp = c & 0x1Fu; extra = 1; }
				else if ((c & 0xF0) == 0xE0) { cp = c & 0x0Fu; extra = 2; }
				else if ((c & 0xF8) == 0xF0) { cp = c & 0x07u; extra = 3; }
				else { ++i; continue; }
				++i;
				for (int k = 0; k < extra && i < s.size(); ++k, ++i)
					cp = (cp << 6) | (static_cast<unsigned char>(s[i]) & 0x3Fu);

				MobileEvent c2;
				c2.kind = MobileEventKind::TextInput;
				c2.unicodeChar = cp;
				c2.key = cp;
				S->Push(c2);
			}
			return undef;
		}

		napi_value NapiNotifyComposing(napi_env env, napi_callback_info info) {
			(void)env; (void)info;
			napi_value undef = nullptr;
			napi_get_undefined(env, &undef);
			return undef;
		}

		napi_property_descriptor kProps[] = {
			{ "attachBridge",   nullptr, NapiAttachBridge,    nullptr, nullptr, nullptr, napi_default, nullptr },
			{ "notifyState",    nullptr, NapiNotifyState,     nullptr, nullptr, nullptr, napi_default, nullptr },
			{ "notifySafeArea", nullptr, NapiNotifySafeArea,  nullptr, nullptr, nullptr, napi_default, nullptr },
			{ "notifyText",     nullptr, NapiNotifyText,      nullptr, nullptr, nullptr, napi_default, nullptr },
			{ "notifyComposing",nullptr, NapiNotifyComposing, nullptr, nullptr, nullptr, napi_default, nullptr },
		};

		napi_value Init(napi_env env, napi_value exports) {
			RedirectStdioToHilog();

			if (!g_session) g_session = new HarmonySession();
			g_session->env = env;

			napi_define_properties(env, exports,
				sizeof(kProps) / sizeof(kProps[0]), kProps);

			napi_value exportInstance = nullptr;
			if (napi_get_named_property(env, exports, OH_NATIVE_XCOMPONENT_OBJ,
				&exportInstance) != napi_ok || !exportInstance) {
				std::fprintf(stderr,
					"[Harmony] 模块导出里没有 %s —— XComponent 绑定失败\n"
					"        （检查 ArkTS 侧 XComponent 的 libraryname 是否等于本模块名）\n",
					OH_NATIVE_XCOMPONENT_OBJ);
				return exports;
			}

			OH_NativeXComponent* comp = nullptr;
			if (napi_unwrap(env, exportInstance,
				reinterpret_cast<void**>(&comp)) != napi_ok || !comp) {
				std::fprintf(stderr, "[Harmony] napi_unwrap 没拿到 "
					"OH_NativeXComponent —— 漏了这一步就会是空指针\n");
				return exports;
			}

			g_session->xcomponent = comp;

			static OH_NativeXComponent_Callback cb{};
			cb.OnSurfaceCreated   = OnSurfaceCreated;
			cb.OnSurfaceChanged   = OnSurfaceChanged;
			cb.OnSurfaceDestroyed = OnSurfaceDestroyed;
			cb.DispatchTouchEvent = DispatchTouchEvent;
			OH_NativeXComponent_RegisterCallback(comp, &cb);

			static OH_NativeXComponent_MouseEvent_Callback mcb{};
			mcb.DispatchMouseEvent = DispatchMouseEvent;
			mcb.DispatchHoverEvent = DispatchHoverEvent;
			if (OH_NativeXComponent_RegisterMouseEventCallback(comp, &mcb)
			    != OH_NATIVEXCOMPONENT_RESULT_SUCCESS) {
				std::fprintf(stderr,
					"[Harmony] 鼠标回调注册失败 —— 鼠标事件将按触摸通道处理\n");
			} else {
				std::fprintf(stderr,
					"[Harmony] 鼠标回调注册**成功** —— 鼠标事件应走 DispatchMouseEvent\n");
			}

			if (OH_NativeXComponent_RegisterKeyEventCallback(comp, OnKeyEventThunk)
			    != OH_NATIVEXCOMPONENT_RESULT_SUCCESS) {
				std::fprintf(stderr,
					"[Harmony] 物理键盘回调注册失败 —— 外接键盘将不可用\n");
			}

			OH_NativeXComponent_RegisterFocusEventCallback(comp, OnFocusGainedThunk);
			OH_NativeXComponent_RegisterBlurEventCallback(comp, OnFocusLostThunk);

			char id[OH_XCOMPONENT_ID_LEN_MAX + 1] = {};
			uint64_t idLen = sizeof(id);
			if (OH_NativeXComponent_GetXComponentId(comp, id, &idLen)
			    == OH_NATIVEXCOMPONENT_RESULT_SUCCESS) {
				std::fprintf(stderr,
					"[Harmony] NAPI 就绪：XComponent id=\"%s\" —— 等 OnSurfaceCreated\n", id);
			}
			return exports;
		}

		napi_module g_module = {
			NAPI_MODULE_VERSION,
			0,
			__FILE__,
			Init,
			"zekit",
			nullptr,
			{ nullptr, nullptr, nullptr, nullptr },
		};

	}

	namespace {
		class HarmonyMobileBackend final : public IMobileBackend {
		public:
			HarmonyMobileBackend() {
				S = Session();
				if (!S) {
					static HarmonySession s_orphan;
					S = &s_orphan;
				}
				if (!g_session) {
					std::fprintf(stderr,
						"[Zekit] 鸿蒙后端在宿主之前被创建 —— 窗口永远不会出现。\n"
						"        请用 ZEKIT_APP_ENTRY 定义入口（见 <ZekitApp.h>）；\n"
						"        它会在 XComponent 的 surface 就绪后由库在独立线程上调。\n");
				}
			}

			Cap MobileCapabilities() const override {
				Cap c = kMobileBaseCapabilities;

				if (!S->JsReady()) {
					c = Cleared(c, Cap::Clipboard);
					c = Cleared(c, Cap::SafeArea);
					c = Cleared(c, Cap::InputOnScreenKeyboard);
				}
				return c;
			}

			bool QuerySurface(void*& nativeSurface, int& widthPx, int& heightPx) override {
				nativeSurface = nullptr;
				widthPx = heightPx = 0;
				if (!S->nativeWindow || !S->surfaceAlive) return false;
				nativeSurface = S->nativeWindow;
				return SurfacePhysicalSize(widthPx, heightPx);
			}

			bool PollMobileEvent(MobileEvent& out) override {
				if (SyncSurfaceSize(out)) return true;
				return S->Pop(out);
			}

			void WaitMobileEvents(int timeoutMs) override {
				std::unique_lock<std::mutex> lk(S->qMutex);
				if (S->qHead == S->qTail) {
					if (timeoutMs < 0) {
						S->qCv.wait(lk, [this] { return S->qHead != S->qTail; });
					} else {
						S->qCv.wait_for(lk,
							std::chrono::milliseconds(timeoutMs),
							[this] { return S->qHead != S->qTail; });
					}
				}
			}

			void WakeEventLoop() override { S->qCv.notify_all(); }

			void ShowSoftKeyboard(bool on) override {
				S->kbVisible = on;
				MobileEvent e;
				e.kind = on ? MobileEventKind::SoftKeyboardShown
					: MobileEventKind::SoftKeyboardHidden;
				e.keyboardHeight = on ? S->keyboardHeightVp : 0;
				S->Push(e);
				JsCallAsync(on ? "keyboard.show" : "keyboard.hide");
			}

			bool IsSoftKeyboardVisible() const override {
				if (S->JsReady()) {
					const std::string r = JsCallSync("keyboard.isVisible");
					if (!r.empty()) return r == "1";
				}
				return S->kbVisible;
			}

			float Density() const override { return Plat::Density(); }

			void GetSafeAreaInsets(float& l, float& t, float& r, float& b) const override {
				const float d = Plat::Density();
				const float k = (d > 0.01f) ? d : 1.0f;
				l = S->safeL / k;
				t = S->safeT / k;
				r = S->safeR / k;
				b = S->safeB / k;
			}

			void SetWindowBackground(unsigned int rgb) override {
				if (!S->JsReady()) return;
				if (rgb == m_lastBg) return;
				m_lastBg = rgb;
				JsCallAsync("window.background", "", static_cast<double>(rgb & 0x00FFFFFFu));
			}

			std::string GetClipboardText() override {
				if (!S->JsReady()) return std::string();
				return JsCallSync("clipboard.get");
			}
			void SetClipboardText(const std::string& utf8) override {
				if (!S->JsReady()) return;
				JsCallAsync("clipboard.set", utf8.c_str());
			}

			bool HasFocus() const override { return S->hasFocus; }

			void RequestAppQuit() override {
				if (S->JsReady()) JsCallAsync("app.quit");
			}

			bool SystemPrefersDark() const override { return S->systemDark; }

			void SetImmersive(bool on) override {
				S->immersive = on;
				JsCallAsync(on ? "immersive.on" : "immersive.off");
			}
			bool IsImmersive() const override { return S->immersive; }

			void SetDarkSystemBar(bool dark) override {
				JsCallAsync("systembar.dark", "", dark ? 1.0 : 0.0);
			}

			void SetKeepScreenOn(bool on) override {
				JsCallAsync("screen.keepOn", "", on ? 1.0 : 0.0);
			}

			void RequestAppMoveToBack() override { JsCallAsync("task.moveToBack"); }
			void SetActivityTitle(const std::string& utf8) override {
				JsCallAsync("title.set", utf8.c_str());
			}
			void SetWindowAlpha(float alpha) override {
				JsCallAsync("window.alpha", "", static_cast<double>(alpha));
			}

		private:
			HarmonySession* S = nullptr;
			unsigned int m_lastBg = 0xFFFFFFFFu;
		};
	}

	IMobileBackend* CreateHarmonyMobileBackend() {
		return new HarmonyMobileBackend();
	}

	void SetHarmonyHost(void* host) {
		if (host) g_session = static_cast<HarmonySession*>(host);
	}

	void* HarmonyHostHandle() { return g_session; }

	bool SystemPrefersDark() {
		HarmonySession* S = Session();
		return S ? S->systemDark : false;
	}

	bool HarmonySurfaceSize(int& widthPx, int& heightPx) {
		return SurfacePhysicalSize(widthPx, heightPx);
	}

	__attribute__((constructor))
	static void ZekitHarmonyRegisterModule() {
		napi_module_register(&g_module);
	}

}

extern "C" __attribute__((weak)) int ZekitHarmonyAppMain() {
	std::fprintf(stderr,
		"[Zekit] 没有找到应用入口 ZekitHarmonyAppMain —— 页面会停在"
		"\"surface 已就绪但什么都没画\"。\n"
		"        请在应用里用 ZEKIT_APP_ENTRY(名字){ ... } 定义入口"
		"（见 <ZekitApp.h>）。\n");
	return 1;
}

#endif
