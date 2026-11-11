
#include "pch.h"

#if defined(__ANDROID__)

#include "ZeWindowAndroid.h"
#include "ZeMobileWindow.h"

#include <android/configuration.h>
#include <android/input.h>
#include <android/keycodes.h>
#include <android/log.h>
#include <android/looper.h>
#include <android/native_activity.h>
#include <android/native_window.h>
#include <android/window.h>
#include <jni.h>
#include <mutex>
#include <string>
#include <android_native_app_glue.h>

#include <cstdio>
#include <cstring>
#include <unistd.h>

namespace Plat {

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
					if (*p) __android_log_print(ANDROID_LOG_ERROR, "ZekitStderr", "%s", p);
					if (!nl) break;
					p = nl + 1;
				}
			}
			return nullptr;
		}

		void RedirectStdioToLogcat() {
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

	namespace {
	struct AndroidSession {
		android_app* app = nullptr;

		static constexpr int kQueueCap = 32;
		MobileEvent queue[kQueueCap];
		int         qHead = 0, qTail = 0;

		int lastFullHeight = 0;
		bool kbRequested = false;
		bool kbVisible = false;
		bool hasFocus = false;
		bool immersive = false;

		int lastSurfaceW = 0, lastSurfaceH = 0;

		float cachedDensity = 1.0f;

		int contentOffsetX = 0, contentOffsetY = 0;

		std::mutex qMutex;

		JavaVM*  vm = nullptr;
		jobject  activity = nullptr;
		jclass   bridge = nullptr;
		jmethodID mShowSoftInput = nullptr;
		jmethodID mHideSoftInput = nullptr;
		jmethodID mIsSoftInputVisible = nullptr;
		jmethodID mSetImmersive = nullptr;
		jmethodID mSetSystemBarAppearance = nullptr;
		jmethodID mGetClipboard = nullptr;
		jmethodID mSetClipboard = nullptr;
		jmethodID mGetSafeArea = nullptr;
		jmethodID mSetWindowBackground = nullptr;
		jmethodID mGetContentOffset = nullptr;
		jmethodID mIsSystemDark = nullptr;
		jmethodID mMoveTaskToBack = nullptr;
		jmethodID mSetTitle = nullptr;
		jmethodID mSetWindowAlpha = nullptr;

		bool JniReady() const { return vm && activity && bridge; }
		JNIEnv* Env();

		void Push(const MobileEvent& e) {
			std::lock_guard<std::mutex> lk(qMutex);
			const int next = (qTail + 1) % kQueueCap;
			if (next == qHead) qHead = (qHead + 1) % kQueueCap;
			queue[qTail] = e;
			qTail = next;
		}
		bool Pop(MobileEvent& out) {
			std::lock_guard<std::mutex> lk(qMutex);
			if (qHead == qTail) return false;
			out = queue[qHead];
			qHead = (qHead + 1) % kQueueCap;
			return true;
		}
	};

	AndroidSession* SessionOf(android_app* app) {
		return app ? static_cast<AndroidSession*>(app->userData) : nullptr;
	}

	android_app* g_app = nullptr;

	bool SessionSystemPrefersDark(AndroidSession* S) {
		if (!S) return false;
		if (S->JniReady() && S->mIsSystemDark) {
			if (JNIEnv* env = S->Env()) {
				const jboolean dark = env->CallStaticBooleanMethod(
					S->bridge, S->mIsSystemDark, S->activity);
				if (env->ExceptionCheck()) env->ExceptionClear();
				return dark == JNI_TRUE;
			}
		}
		if (!S->app || !S->app->config) return false;
		return AConfiguration_getUiModeNight(S->app->config)
		       == ACONFIGURATION_UI_MODE_NIGHT_YES;
	}

	JNIEnv* AndroidSession::Env() {
		if (!vm) return nullptr;
		JNIEnv* env = nullptr;
		if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_OK)
			return env;
		if (vm->AttachCurrentThread(&env, nullptr) == JNI_OK) return env;
		return nullptr;
	}

		void PushCommittedText(AndroidSession* S, const std::string& utf8) {
			if (utf8 == "\b") {
				MobileEvent e;
				e.kind = MobileEventKind::KeyDown;
				e.key = 0x08;
				S->Push(e);
				return;
			}
			if (utf8 == "\n") {
				MobileEvent e;
				e.kind = MobileEventKind::KeyDown;
				e.key = 0x0D;
				S->Push(e);
				return;
			}

			std::size_t i = 0;
			while (i < utf8.size()) {
				const unsigned char c = static_cast<unsigned char>(utf8[i]);
				uint32_t cp = 0;
				int extra = 0;
				if (c < 0x80)                { cp = c;           extra = 0; }
				else if ((c & 0xE0) == 0xC0) { cp = c & 0x1Fu;   extra = 1; }
				else if ((c & 0xF0) == 0xE0) { cp = c & 0x0Fu;   extra = 2; }
				else if ((c & 0xF8) == 0xF0) { cp = c & 0x07u;   extra = 3; }
				else { ++i; continue; }
				++i;
				for (int k = 0; k < extra && i < utf8.size(); ++k, ++i)
					cp = (cp << 6) | (static_cast<unsigned char>(utf8[i]) & 0x3Fu);

				MobileEvent e;
				e.kind = MobileEventKind::TextInput;
				e.unicodeChar = cp;
				e.key = cp;
				S->Push(e);
			}
		}

		bool JstringToUtf8(JNIEnv* env, jstring s, std::string& out) {
			if (!env || !s) return false;
			const char* p = env->GetStringUTFChars(s, nullptr);
			if (!p) return false;
			out.assign(p);
			env->ReleaseStringUTFChars(s, p);
			return true;
		}

		extern "C" JNIEXPORT void JNICALL
		Java_com_zekit_runtime_ZekitBridge_nativeCommitText(JNIEnv* env, jclass, jstring text) {
			std::string s;
			AndroidSession* S = SessionOf(g_app);
			if (!S) return;
			if (!JstringToUtf8(env, text, s) || s.empty()) return;
			PushCommittedText(S, s);
			if (S->app && S->app->looper) ALooper_wake(S->app->looper);
		}

		extern "C" JNIEXPORT void JNICALL
		Java_com_zekit_runtime_ZekitBridge_nativeComposingText(JNIEnv* env, jclass, jstring text) {
			(void)env; (void)text;
		}

		void QuerySize(AndroidSession* S, int& w, int& h) {
			w = h = 0;
			if (!S->app || !S->app->window) return;
			w = ANativeWindow_getWidth(S->app->window);
			h = ANativeWindow_getHeight(S->app->window);
		}

		uint32_t ToKey(int32_t akey) {
			if (akey >= AKEYCODE_A && akey <= AKEYCODE_Z)
				return static_cast<uint32_t>('A' + (akey - AKEYCODE_A));
			if (akey >= AKEYCODE_0 && akey <= AKEYCODE_9)
				return static_cast<uint32_t>('0' + (akey - AKEYCODE_0));

			switch (akey) {
			case AKEYCODE_ENTER:       return 0x0D;
			case AKEYCODE_TAB:         return 0x09;
			case AKEYCODE_SPACE:       return 0x20;
			case AKEYCODE_ESCAPE:      return 0x1B;
			case AKEYCODE_DEL:         return 0x08;
			case AKEYCODE_FORWARD_DEL: return 0x2E;
			case AKEYCODE_INSERT:      return 0x2D;

			case AKEYCODE_DPAD_LEFT:   return 0x25;
			case AKEYCODE_DPAD_UP:     return 0x26;
			case AKEYCODE_DPAD_RIGHT:  return 0x27;
			case AKEYCODE_DPAD_DOWN:   return 0x28;
			case AKEYCODE_MOVE_HOME:   return 0x24;
			case AKEYCODE_MOVE_END:    return 0x23;
			case AKEYCODE_PAGE_UP:     return 0x21;
			case AKEYCODE_PAGE_DOWN:   return 0x22;

			case AKEYCODE_SHIFT_LEFT:
			case AKEYCODE_SHIFT_RIGHT: return 0x10;
			case AKEYCODE_CTRL_LEFT:
			case AKEYCODE_CTRL_RIGHT:  return 0x11;
			case AKEYCODE_ALT_LEFT:
			case AKEYCODE_ALT_RIGHT:   return 0x12;

			case AKEYCODE_SEMICOLON:    return 0xBA;
			case AKEYCODE_EQUALS:       return 0xBB;
			case AKEYCODE_COMMA:        return 0xBC;
			case AKEYCODE_MINUS:        return 0xBD;
			case AKEYCODE_PERIOD:       return 0xBE;
			case AKEYCODE_SLASH:        return 0xBF;
			case AKEYCODE_GRAVE:        return 0xC0;
			case AKEYCODE_LEFT_BRACKET: return 0xDB;
			case AKEYCODE_BACKSLASH:    return 0xDC;
			case AKEYCODE_RIGHT_BRACKET:return 0xDD;
			case AKEYCODE_APOSTROPHE:   return 0xDE;

			case AKEYCODE_VOLUME_MUTE: return 0xAD;
			case AKEYCODE_VOLUME_DOWN: return 0xAE;
			case AKEYCODE_VOLUME_UP:   return 0xAF;

			default:                   return 0;
			}
		}

		Modifiers ToMods(int32_t meta) {
			Modifiers m = Modifiers::None;
			if (meta & AMETA_SHIFT_ON) m = m | Modifiers::Shift;
			if (meta & AMETA_CTRL_ON)  m = m | Modifiers::Control;
			if (meta & AMETA_ALT_ON)   m = m | Modifiers::Alt;
			if (meta & AMETA_META_ON)  m = m | Modifiers::Super;
			return m;
		}

		uint32_t DeriveChar(int32_t akey, int32_t meta) {
			const bool shift = (meta & AMETA_SHIFT_ON) != 0;

			if (akey >= AKEYCODE_A && akey <= AKEYCODE_Z)
				return static_cast<uint32_t>('a' + (akey - AKEYCODE_A)) - (shift ? 32u : 0u);

			static const char kShiftDigits[] = ")!@#$%^&*(";
			if (akey >= AKEYCODE_0 && akey <= AKEYCODE_9) {
				const int d = akey - AKEYCODE_0;
				return static_cast<uint32_t>(
					shift ? kShiftDigits[d] : static_cast<char>('0' + d));
			}

			switch (akey) {
			case AKEYCODE_SPACE:       return ' ';
			case AKEYCODE_ENTER:       return '\r';
			case AKEYCODE_TAB:         return '\t';
			case AKEYCODE_COMMA:       return shift ? '<' : ',';
			case AKEYCODE_PERIOD:      return shift ? '>' : '.';
			case AKEYCODE_SEMICOLON:   return shift ? ':' : ';';
			case AKEYCODE_APOSTROPHE:  return shift ? '"' : '\'';
			case AKEYCODE_SLASH:       return shift ? '?' : '/';
			case AKEYCODE_BACKSLASH:   return shift ? '|' : '\\';
			case AKEYCODE_GRAVE:       return shift ? '~' : '`';
			case AKEYCODE_MINUS:       return shift ? '_' : '-';
			case AKEYCODE_EQUALS:      return shift ? '+' : '=';
			case AKEYCODE_LEFT_BRACKET:  return shift ? '{' : '[';
			case AKEYCODE_RIGHT_BRACKET: return shift ? '}' : ']';
			default:                   return 0;
			}
		}

		float DensityScale(AndroidSession* S) {
			if (!S->app || !S->app->config) return S->cachedDensity;
			const int32_t dpi = AConfiguration_getDensity(S->app->config);
			if (dpi > 0) S->cachedDensity = static_cast<float>(dpi) / 160.0f;
			return S->cachedDensity;
		}

		float ToLogical(AndroidSession* S, float px) {
			const float d = DensityScale(S);
			return (d > 0.0f) ? px / d : px;
		}

		void RefreshContentOffset(AndroidSession* S) {
			if (!S->JniReady() || !S->mGetContentOffset) return;
			JNIEnv* env = S->Env();
			if (!env) return;

			jintArray arr = static_cast<jintArray>(
				env->CallStaticObjectMethod(S->bridge, S->mGetContentOffset, S->activity));
			if (env->ExceptionCheck() || !arr) { env->ExceptionClear(); return; }

			jint buf[2] = { 0, 0 };
			env->GetIntArrayRegion(arr, 0, 2, buf);
			env->DeleteLocalRef(arr);
			if (env->ExceptionCheck()) { env->ExceptionClear(); return; }

			S->contentOffsetX = buf[0];
			S->contentOffsetY = buf[1];
		}

		bool SyncSurfaceSize(AndroidSession* S, MobileEvent& out) {
			if (!S->app || !S->app->window) return false;
			const int w = ANativeWindow_getWidth(S->app->window);
			const int h = ANativeWindow_getHeight(S->app->window);
			if (w <= 0 || h <= 0) return false;
			if (w == S->lastSurfaceW && h == S->lastSurfaceH) return false;
			S->lastSurfaceW = w;
			S->lastSurfaceH = h;

			out.kind = MobileEventKind::Resized;
			out.width  = static_cast<int>(ToLogical(S, static_cast<float>(w)) + 0.5f);
			out.height = static_cast<int>(ToLogical(S, static_cast<float>(h)) + 0.5f);
			return true;
		}

		void OnAppCmd(android_app* app, int32_t cmd) {
			AndroidSession* S = SessionOf(app);
			if (!S) return;
			MobileEvent e;
			switch (cmd) {
			case APP_CMD_INIT_WINDOW: {
				QuerySize(S, e.width, e.height);
				e.width = static_cast<int>(ToLogical(S, static_cast<float>(e.width)) + 0.5f);
				e.height = static_cast<int>(ToLogical(S, static_cast<float>(e.height)) + 0.5f);
				S->lastFullHeight = e.height;
				e.kind = MobileEventKind::SurfaceReady;
				S->Push(e);
				break;
			}
			case APP_CMD_TERM_WINDOW:
				e.kind = MobileEventKind::SurfaceLost;
				S->Push(e);
				break;

			case APP_CMD_WINDOW_RESIZED:
			case APP_CMD_CONTENT_RECT_CHANGED: {
				QuerySize(S, e.width, e.height);
				e.width = static_cast<int>(ToLogical(S, static_cast<float>(e.width)) + 0.5f);
				e.height = static_cast<int>(ToLogical(S, static_cast<float>(e.height)) + 0.5f);
				e.kind = MobileEventKind::Resized;
				S->Push(e);
				break;
			}

			case APP_CMD_CONFIG_CHANGED: {
				e.kind = MobileEventKind::ConfigChanged;
				S->Push(e);
				break;
			}

			case APP_CMD_GAINED_FOCUS:
				S->hasFocus = true;
				e.kind = MobileEventKind::FocusGained;
				S->Push(e);
				break;			case APP_CMD_LOST_FOCUS:
				S->hasFocus = false;
				e.kind = MobileEventKind::FocusLost;
				S->Push(e);
				break;

			case APP_CMD_RESUME:
				e.kind = MobileEventKind::AppResumed;     S->Push(e); break;
			case APP_CMD_PAUSE:
				e.kind = MobileEventKind::AppPaused;      S->Push(e); break;
			case APP_CMD_LOW_MEMORY:
				e.kind = MobileEventKind::AppLowMemory;   S->Push(e); break;
			case APP_CMD_DESTROY:
				e.kind = MobileEventKind::AppTerminating; S->Push(e);
				e.kind = MobileEventKind::QuitRequested;  S->Push(e);
				break;

			case APP_CMD_START:
			case APP_CMD_STOP:
				break;

			default:
				break;
			}
			(void)app;
		}

		int32_t OnInputEvent(android_app* app, AInputEvent* ev) {
			AndroidSession* S = SessionOf(app);
			if (!S) return 0;
			const int32_t type = AInputEvent_getType(ev);
			if (type == AINPUT_EVENT_TYPE_MOTION) {
				const int32_t action = AMotionEvent_getAction(ev);
				const int32_t masked = action & AMOTION_EVENT_ACTION_MASK;

				MobileEvent e;
				switch (masked) {
				case AMOTION_EVENT_ACTION_DOWN:
				case AMOTION_EVENT_ACTION_POINTER_DOWN:
					e.kind = MobileEventKind::TouchBegin; break;
				case AMOTION_EVENT_ACTION_MOVE:
					e.kind = MobileEventKind::TouchMove;  break;
				case AMOTION_EVENT_ACTION_UP:
				case AMOTION_EVENT_ACTION_POINTER_UP:
					e.kind = MobileEventKind::TouchEnd;   break;
				case AMOTION_EVENT_ACTION_CANCEL:
					e.kind = MobileEventKind::TouchCancel; break;
				default:
					return 1;
				}

				RefreshContentOffset(S);
				const float offX = static_cast<float>(S->contentOffsetX);
				const float offY = static_cast<float>(S->contentOffsetY);

				const size_t n = AMotionEvent_getPointerCount(ev);
				e.pointerCount = (n > MobileEvent::kMaxPointers)
					? MobileEvent::kMaxPointers : static_cast<uint32_t>(n);
				for (uint32_t i = 0; i < e.pointerCount; ++i) {
					e.pointers[i].id = static_cast<uint32_t>(
						AMotionEvent_getPointerId(ev, i));
					e.pointers[i].x = ToLogical(S, AMotionEvent_getX(ev, i) + offX);
					e.pointers[i].y = ToLogical(S, AMotionEvent_getY(ev, i) + offY);
				}

				if (masked == AMOTION_EVENT_ACTION_POINTER_DOWN ||
					masked == AMOTION_EVENT_ACTION_POINTER_UP) {
					e.changedIndex = static_cast<uint32_t>(
						(action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK)
						>> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
				}
				if (e.changedIndex >= e.pointerCount) e.changedIndex = 0;

				S->Push(e);
				return 1;
			}

			if (type == AINPUT_EVENT_TYPE_KEY) {
				const int32_t akey = AKeyEvent_getKeyCode(ev);
				const int32_t aaction = AKeyEvent_getAction(ev);
				const int32_t meta = AKeyEvent_getMetaState(ev);

				MobileEvent e;
				e.rawKeyCode = static_cast<uint32_t>(akey);
				e.key = ToKey(akey);
				e.mods = ToMods(meta);

				if (aaction == AKEY_EVENT_ACTION_DOWN) {
					e.kind = MobileEventKind::KeyDown;
					e.isRepeat = (AKeyEvent_getRepeatCount(ev) > 0);
					e.unicodeChar = DeriveChar(akey, meta);
					S->Push(e);

					return 1;
				}
				if (aaction == AKEY_EVENT_ACTION_UP) {
					e.kind = MobileEventKind::KeyUp;
					S->Push(e);
					return 1;
				}
				if (aaction == AKEY_EVENT_ACTION_MULTIPLE) {
					e.kind = MobileEventKind::KeyDown;
					e.isRepeat = true;
					e.unicodeChar = DeriveChar(akey, meta);
					const int32_t n = AKeyEvent_getRepeatCount(ev);
					for (int32_t i = 0; i < n && i < 16; ++i) S->Push(e);
					return 1;
				}
				return 1;
			}

			return 0;
		}

		void InstallCallbacks(android_app* app) {
			app->onAppCmd = OnAppCmd;
			app->onInputEvent = OnInputEvent;
		}

	}

	namespace {
		class AndroidMobileBackend final : public IMobileBackend {
		public:
			AndroidMobileBackend() {
				S = SessionOf(g_app);
				if (!S) {
					static AndroidSession s_orphan;
					S = &s_orphan;
				}
				if (!g_app) {
					std::fprintf(stderr,
						"[Zekit] Android 后端在宿主句柄之前被创建 —— 窗口永远不会出现。\n"
						"        请用 ZEKIT_APP_ENTRY 定义入口（见 <ZekitApp.h>），\n"
						"        或在构造 Kit::Window 之前先调 Kit::AttachPlatformHost(app)。\n");
				}
			}

			Cap MobileCapabilities() const override {
				Cap c = kMobileBaseCapabilities;

				if (S->JniReady()) {
					c |= Cap::Clipboard;
					c |= Cap::SafeArea;
				} else {
					c = Cleared(c, Cap::InputOnScreenKeyboard);
				}

				return c;
			}

			bool QuerySurface(void*& nativeSurface, int& widthPx, int& heightPx) override {
				nativeSurface = nullptr;
				widthPx = heightPx = 0;
				if (!S->app || !S->app->window) return false;
				nativeSurface = static_cast<void*>(S->app->window);
				widthPx = ANativeWindow_getWidth(S->app->window);
				heightPx = ANativeWindow_getHeight(S->app->window);
				return widthPx > 0 && heightPx > 0;
			}

			bool PollMobileEvent(MobileEvent& out) override {
				if (SyncSurfaceSize(S, out)) return true;

				if (S->Pop(out)) return true;
				if (!S->app) return false;

				for (;;) {
					int events = 0;
					android_poll_source* source = nullptr;
					const int ident = ALooper_pollOnce(
						0, nullptr, &events, reinterpret_cast<void**>(&source));

					if (ident == ALOOPER_POLL_ERROR) return false;
					if (suppressWake) { suppressWake = false; return false; }
					if (ident == ALOOPER_POLL_TIMEOUT) return false;
					if (source) source->process(S->app, source);

					if (S->Pop(out)) return true;
				}
			}

			void WaitMobileEvents(int timeoutMs) override {
				if (!S->app) return;
				int events = 0;
				android_poll_source* source = nullptr;
				const int ident = ALooper_pollOnce(
					timeoutMs, nullptr, &events, reinterpret_cast<void**>(&source));
				if (ident == ALOOPER_POLL_ERROR) return;
				if (ident == ALOOPER_POLL_WAKE) {
					suppressWake = true;
					return;
				}
				if (source) source->process(S->app, source);
			}

			void WakeEventLoop() override {
				if (S->app && S->app->looper) ALooper_wake(S->app->looper);
			}

			void ShowSoftKeyboard(bool on) override {
				if (!S->app) return;
				S->kbRequested = on;

				if (S->JniReady()) {
					JNIEnv* env = S->Env();
					if (env) {
						jmethodID mid = on ? S->mShowSoftInput : S->mHideSoftInput;
						if (mid) env->CallStaticVoidMethod(S->bridge, mid, S->activity);
						if (env->ExceptionCheck()) env->ExceptionClear();
					}
				} else if (S->app->activity) {
					if (on) ANativeActivity_showSoftInput(S->app->activity,
						ANATIVEACTIVITY_SHOW_SOFT_INPUT_IMPLICIT);
					else    ANativeActivity_hideSoftInput(S->app->activity, 0);
				}

				S->kbVisible = on;

				MobileEvent e;
				e.kind = on ? MobileEventKind::SoftKeyboardShown
					: MobileEventKind::SoftKeyboardHidden;
				e.keyboardHeight = 0;
				S->Push(e);
				WakeEventLoop();
			}

			bool IsSoftKeyboardVisible() const override {
				if (S->JniReady() && S->mIsSoftInputVisible) {
					JNIEnv* env = S->Env();
					if (env) {
						const jboolean r = env->CallStaticBooleanMethod(
							S->bridge, S->mIsSoftInputVisible, S->activity);
						if (env->ExceptionCheck()) { env->ExceptionClear(); return S->kbVisible; }
						return r == JNI_TRUE;
					}
				}
				return S->kbVisible;
			}

			float Density() const override {
				return DensityScale(S);
			}

			void GetSafeAreaInsets(float& l, float& t, float& r, float& b) const override {
				l = t = r = b = 0.0f;
				if (!S->JniReady() || !S->mGetSafeArea) return;

				JNIEnv* env = S->Env();
				if (!env) return;

				jintArray arr = static_cast<jintArray>(
					env->CallStaticObjectMethod(S->bridge, S->mGetSafeArea, S->activity));
				if (env->ExceptionCheck() || !arr) { env->ExceptionClear(); return; }

				jint buf[4] = { 0, 0, 0, 0 };
				env->GetIntArrayRegion(arr, 0, 4, buf);
				env->DeleteLocalRef(arr);
				if (env->ExceptionCheck()) { env->ExceptionClear(); return; }

				const float d = Density();
				const float k = (d > 0.0f) ? d : 1.0f;
				l = static_cast<float>(buf[0]) / k;
				t = static_cast<float>(buf[1]) / k;
				r = static_cast<float>(buf[2]) / k;
				b = static_cast<float>(buf[3]) / k;
			}

			void SetWindowBackground(unsigned int rgb) override {
				if (!S->JniReady() || !S->mSetWindowBackground) return;
				JNIEnv* env = S->Env();
				if (!env) return;
				const jint argb = static_cast<jint>(0xFF000000u | (rgb & 0x00FFFFFFu));
				env->CallStaticVoidMethod(S->bridge, S->mSetWindowBackground, S->activity, argb);
				if (env->ExceptionCheck()) env->ExceptionClear();
			}

			std::string GetClipboardText() override {
				if (!S->JniReady() || !S->mGetClipboard) return std::string();
				JNIEnv* env = S->Env();
				if (!env) return std::string();

				jstring s = static_cast<jstring>(
					env->CallStaticObjectMethod(S->bridge, S->mGetClipboard, S->activity));
				if (env->ExceptionCheck() || !s) { env->ExceptionClear(); return std::string(); }

				std::string out;
				JstringToUtf8(env, s, out);
				env->DeleteLocalRef(s);
				return out;
			}

			void SetClipboardText(const std::string& utf8) override {
				if (!S->JniReady() || !S->mSetClipboard) return;
				JNIEnv* env = S->Env();
				if (!env) return;

				jstring s = env->NewStringUTF(utf8.c_str());
				if (!s) { env->ExceptionClear(); return; }
				env->CallStaticVoidMethod(S->bridge, S->mSetClipboard, S->activity, s);
				env->DeleteLocalRef(s);
				if (env->ExceptionCheck()) env->ExceptionClear();
			}

			bool HasFocus() const override { return S->hasFocus; }

			void RequestAppQuit() override {
				if (S->app && S->app->activity)
					ANativeActivity_finish(S->app->activity);
			}

			bool SystemPrefersDark() const override {
				return SessionSystemPrefersDark(S);
			}

			void SetImmersive(bool on) override {
				S->immersive = on;

				if (S->JniReady() && S->mSetImmersive) {
					JNIEnv* env = S->Env();
					if (env) {
						env->CallStaticVoidMethod(S->bridge, S->mSetImmersive, S->activity,
							on ? JNI_TRUE : JNI_FALSE);
						if (env->ExceptionCheck()) env->ExceptionClear();
						return;
					}
				}

				if (S->app && S->app->activity) {
					ANativeActivity_setWindowFlags(S->app->activity,
						on ? AWINDOW_FLAG_FULLSCREEN : 0,
						on ? 0 : AWINDOW_FLAG_FULLSCREEN);
				}
			}
			bool IsImmersive() const override { return S->immersive; }

			void SetDarkSystemBar(bool dark) override {
				if (!S->JniReady() || !S->mSetSystemBarAppearance) return;
				JNIEnv* env = S->Env();
				if (!env) return;
				env->CallStaticVoidMethod(S->bridge, S->mSetSystemBarAppearance, S->activity,
					dark ? JNI_TRUE : JNI_FALSE);
				if (env->ExceptionCheck()) env->ExceptionClear();
			}

			void RequestAppMoveToBack() override {
				if (!S->JniReady() || !S->mMoveTaskToBack) return;
				JNIEnv* env = S->Env();
				if (!env) return;
				env->CallStaticBooleanMethod(S->bridge, S->mMoveTaskToBack, S->activity);
				if (env->ExceptionCheck()) env->ExceptionClear();
			}

			void SetActivityTitle(const std::string& utf8) override {
				if (!S->JniReady() || !S->mSetTitle) return;
				JNIEnv* env = S->Env();
				if (!env) return;
				jstring js = env->NewStringUTF(utf8.c_str());
				if (!js) { env->ExceptionClear(); return; }
				env->CallStaticVoidMethod(S->bridge, S->mSetTitle, S->activity, js);
				if (env->ExceptionCheck()) env->ExceptionClear();
				env->DeleteLocalRef(js);
			}

			void SetWindowAlpha(float alpha) override {
				if (!S->JniReady() || !S->mSetWindowAlpha) return;
				JNIEnv* env = S->Env();
				if (!env) return;
				env->CallStaticVoidMethod(S->bridge, S->mSetWindowAlpha, S->activity,
					static_cast<jfloat>(alpha));
				if (env->ExceptionCheck()) env->ExceptionClear();
			}

		private:
			AndroidSession* S = nullptr;

			bool suppressWake = false;
		};
	}

	IMobileBackend* CreateAndroidMobileBackend() {
		return new AndroidMobileBackend();
	}

	void InitJniBridge(android_app* app) {
		AndroidSession* S = SessionOf(app);
		if (!S || !app || !app->activity || !app->activity->vm) return;

		S->vm = app->activity->vm;
		JNIEnv* env = S->Env();
		if (!env) return;

		if (S->activity) { env->DeleteGlobalRef(S->activity); S->activity = nullptr; }
		if (S->bridge)   { env->DeleteGlobalRef(S->bridge);   S->bridge = nullptr; }

		S->activity = env->NewGlobalRef(app->activity->clazz);
		if (!S->activity || env->ExceptionCheck()) {
			env->ExceptionClear(); S->activity = nullptr; S->vm = nullptr; return;
		}

		jclass bridgeCls = nullptr;
		{
			jclass activityCls = env->GetObjectClass(S->activity);
			jmethodID midGetLoader = activityCls
				? env->GetMethodID(activityCls, "getClassLoader", "()Ljava/lang/ClassLoader;")
				: nullptr;
			if (midGetLoader) {
				jobject loader = env->CallObjectMethod(S->activity, midGetLoader);
				jclass loaderCls = env->FindClass("java/lang/ClassLoader");
				jmethodID midLoad = loaderCls
					? env->GetMethodID(loaderCls, "loadClass",
						"(Ljava/lang/String;)Ljava/lang/Class;")
					: nullptr;
				if (loader && midLoad) {
					jstring name = env->NewStringUTF("com.zekit.runtime.ZekitBridge");
					bridgeCls = static_cast<jclass>(
						env->CallObjectMethod(loader, midLoad, name));
					env->DeleteLocalRef(name);
				}
				if (loader)    env->DeleteLocalRef(loader);
				if (loaderCls) env->DeleteLocalRef(loaderCls);
			}
			if (activityCls) env->DeleteLocalRef(activityCls);
		}
		if (!bridgeCls || env->ExceptionCheck()) {
			env->ExceptionClear();
			if (bridgeCls) env->DeleteLocalRef(bridgeCls);
			env->DeleteGlobalRef(S->activity); S->activity = nullptr; S->vm = nullptr;
			return;
		}
		S->bridge = static_cast<jclass>(env->NewGlobalRef(bridgeCls));
		env->DeleteLocalRef(bridgeCls);

		S->mShowSoftInput       = env->GetStaticMethodID(S->bridge, "showSoftInput",
		                             "(Landroid/app/Activity;)V");
		S->mHideSoftInput       = env->GetStaticMethodID(S->bridge, "hideSoftInput",
		                             "(Landroid/app/Activity;)V");
		S->mIsSoftInputVisible  = env->GetStaticMethodID(S->bridge, "isSoftInputVisible",
		                             "(Landroid/app/Activity;)Z");
		S->mSetImmersive        = env->GetStaticMethodID(S->bridge, "setImmersive",
		                             "(Landroid/app/Activity;Z)V");
		S->mSetSystemBarAppearance = env->GetStaticMethodID(S->bridge,
		                             "setSystemBarAppearance", "(Landroid/app/Activity;Z)V");
		S->mGetClipboard        = env->GetStaticMethodID(S->bridge, "getClipboard",
		                             "(Landroid/app/Activity;)Ljava/lang/String;");
		S->mSetClipboard        = env->GetStaticMethodID(S->bridge, "setClipboard",
		                             "(Landroid/app/Activity;Ljava/lang/String;)V");
		S->mGetSafeArea         = env->GetStaticMethodID(S->bridge, "getSafeArea",
		                             "(Landroid/app/Activity;)[I");
		S->mSetWindowBackground = env->GetStaticMethodID(S->bridge, "setWindowBackground",
		                             "(Landroid/app/Activity;I)V");
		S->mGetContentOffset    = env->GetStaticMethodID(S->bridge, "getContentOffset",
		                             "(Landroid/app/Activity;)[I");
		S->mIsSystemDark        = env->GetStaticMethodID(S->bridge, "isSystemDark",
		                             "(Landroid/app/Activity;)Z");
		S->mMoveTaskToBack      = env->GetStaticMethodID(S->bridge, "moveTaskToBack",
		                             "(Landroid/app/Activity;)Z");
		S->mSetTitle            = env->GetStaticMethodID(S->bridge, "setTitle",
		                             "(Landroid/app/Activity;Ljava/lang/String;)V");
		S->mSetWindowAlpha      = env->GetStaticMethodID(S->bridge, "setWindowAlpha",
		                             "(Landroid/app/Activity;F)V");

		if (env->ExceptionCheck()) {
			env->ExceptionClear();
			env->DeleteGlobalRef(S->bridge);   S->bridge = nullptr;
			env->DeleteGlobalRef(S->activity); S->activity = nullptr;
			S->vm = nullptr;
			S->mShowSoftInput = nullptr;
			return;
		}

		JNINativeMethod methods[] = {
			{ const_cast<char*>("nativeCommitText"),
			  const_cast<char*>("(Ljava/lang/String;)V"),
			  reinterpret_cast<void*>(
				  Java_com_zekit_runtime_ZekitBridge_nativeCommitText) },
			{ const_cast<char*>("nativeComposingText"),
			  const_cast<char*>("(Ljava/lang/String;)V"),
			  reinterpret_cast<void*>(
				  Java_com_zekit_runtime_ZekitBridge_nativeComposingText) },
		};
		env->RegisterNatives(S->bridge, methods, 2);
		env->ExceptionClear();
	}

	void SetAndroidHost(void* androidApp) {
		RedirectStdioToLogcat();

		android_app* app = static_cast<android_app*>(androidApp);

		if (app) {
			if (!app->userData) app->userData = new AndroidSession();
			AndroidSession* S = static_cast<AndroidSession*>(app->userData);
			S->app = app;

			g_app = app;

			InitJniBridge(app);
			InstallCallbacks(app);
		} else {
			g_app = nullptr;
		}
	}

	bool SystemPrefersDark() {
		AndroidSession* S = SessionOf(g_app);
		if (S) return SessionSystemPrefersDark(S);
		if (!g_app || !g_app->config) return false;
		return AConfiguration_getUiModeNight(g_app->config)
		       == ACONFIGURATION_UI_MODE_NIGHT_YES;
	}

}

#endif
