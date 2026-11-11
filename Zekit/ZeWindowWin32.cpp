
#include "pch.h"

#include "ZeWindowWin32.h"

#include <Windows.h>
#include <Shlwapi.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <ole2.h>
#include <timeapi.h>

#include <SOIL2/SOIL2.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

	void EnableDPIAwareness() {
		using SetCtx_t = BOOL(WINAPI*)(HANDLE);
		using SetShcore_t = HRESULT(WINAPI*)(int);

		static bool done = false;
		if (done) return;
		done = true;

		if (HMODULE u32 = LoadLibraryW(L"user32.dll")) {
			SetCtx_t fn = reinterpret_cast<SetCtx_t>(
				GetProcAddress(u32, "SetProcessDpiAwarenessContext"));
			if (fn && fn(reinterpret_cast<HANDLE>(static_cast<intptr_t>(-4)))) return;
		}

		if (HMODULE sh = LoadLibraryW(L"shcore.dll")) {
			SetShcore_t fn = reinterpret_cast<SetShcore_t>(
				GetProcAddress(sh, "SetProcessDpiAwareness"));
			if (fn && SUCCEEDED(fn(2))) return;
		}

		SetProcessDPIAware();
	}

	bool ReadSystemPrefersDark() {
		DWORD value = 1;
		DWORD size = sizeof(value);
		const LSTATUS st = RegGetValueW(
			HKEY_CURRENT_USER,
			L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
			L"AppsUseLightTheme",
			RRF_RT_REG_DWORD, nullptr, &value, &size);
		if (st != ERROR_SUCCESS) return false;
		return value == 0;
	}

	void SetTitleBarDark(HWND hwnd, bool dark) {
		if (!hwnd) return;

		using DwmSetAttr_t = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
		static HMODULE dll = LoadLibraryW(L"dwmapi.dll");
		static DwmSetAttr_t fn = dll ? reinterpret_cast<DwmSetAttr_t>(
			GetProcAddress(dll, "DwmSetWindowAttribute")) : nullptr;
		using DwmFlush_t = HRESULT(WINAPI*)();
		static DwmFlush_t flush = dll ? reinterpret_cast<DwmFlush_t>(
			GetProcAddress(dll, "DwmFlush")) : nullptr;
		if (!fn) return;

		const BOOL v = dark ? TRUE : FALSE;
		if (FAILED(fn(hwnd, 20, &v, sizeof(v))))
			fn(hwnd, 19, &v, sizeof(v));

		const HWND fg = ::GetForegroundWindow();
		const BOOL selfFg = (fg == hwnd) ? TRUE : FALSE;
		const BOOL sameThread = (fg && ::GetWindowThreadProcessId(fg, nullptr) ==
			::GetWindowThreadProcessId(hwnd, nullptr)) ? TRUE : FALSE;
		const BOOL active = (selfFg || sameThread) ? TRUE : FALSE;

		::SendMessageW(hwnd, WM_NCACTIVATE, static_cast<WPARAM>(active ? FALSE : TRUE), 0);
		::SendMessageW(hwnd, WM_NCACTIVATE, static_cast<WPARAM>(active), 0);

		::PostMessageW(hwnd, WM_NCACTIVATE, static_cast<WPARAM>(active ? FALSE : TRUE), 0);
		::PostMessageW(hwnd, WM_NCACTIVATE, static_cast<WPARAM>(active), 0);

		SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
			SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
		RedrawWindow(hwnd, nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE);

		if (flush) flush();
	}

	void ApplyCornerPreference(HWND hwnd, UINT pref) {
		if (!hwnd) return;

		using DwmSetAttr_t = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
		static DwmSetAttr_t fn = []() -> DwmSetAttr_t {
			HMODULE dll = LoadLibraryW(L"dwmapi.dll");
			return dll ? reinterpret_cast<DwmSetAttr_t>(
				GetProcAddress(dll, "DwmSetWindowAttribute")) : nullptr;
			}();
		if (!fn) return;

		fn(hwnd, 33 , &pref, sizeof(pref));
	}

	void DisableSystemFrame(HWND hwnd) {
		if (!hwnd) return;

		using DwmSetAttr_t = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
		static DwmSetAttr_t fn = []() -> DwmSetAttr_t {
			HMODULE dll = LoadLibraryW(L"dwmapi.dll");
			return dll ? reinterpret_cast<DwmSetAttr_t>(
				GetProcAddress(dll, "DwmSetWindowAttribute")) : nullptr;
			}();
		if (!fn) return;

		const UINT kDoNotRound = 1;
		fn(hwnd, 33 , &kDoNotRound, sizeof(kDoNotRound));

		const DWORD kColorNone = 0xFFFFFFFE;
		fn(hwnd, 34 , &kColorNone, sizeof(kColorNone));

	}

	UINT QueryCornerPreference(HWND hwnd) {
		if (!hwnd) return 0;
		using DwmGetAttr_t = HRESULT(WINAPI*)(HWND, DWORD, PVOID, DWORD);
		static DwmGetAttr_t fn = []() -> DwmGetAttr_t {
			HMODULE dll = LoadLibraryW(L"dwmapi.dll");
			return dll ? reinterpret_cast<DwmGetAttr_t>(
				GetProcAddress(dll, "DwmGetWindowAttribute")) : nullptr;
			}();
		if (!fn) return 0;
		UINT pref = 0;
		if (FAILED(fn(hwnd, 33, &pref, sizeof(pref)))) return 0;
		return pref;
	}

	std::wstring Utf8ToWide(const std::string& utf8) {
		if (utf8.empty()) return std::wstring();
		const int n = ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
			static_cast<int>(utf8.size()), nullptr, 0);
		if (n <= 0) return std::wstring();
		std::wstring out(static_cast<size_t>(n), L'\0');
		::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
			static_cast<int>(utf8.size()), &out[0], n);
		return out;
	}

	std::string WideToUtf8(const std::wstring& w) {
		if (w.empty()) return std::string();
		const int n = ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(),
			static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
		if (n <= 0) return std::string();
		std::string out(static_cast<size_t>(n), '\0');
		::WideCharToMultiByte(CP_UTF8, 0, w.c_str(),
			static_cast<int>(w.size()), &out[0], n, nullptr, nullptr);
		return out;
	}

	void ReadHdrop(HDROP drop, std::vector<std::string>& out) {
		if (!drop) return;
		const UINT count = ::DragQueryFileW(drop, 0xFFFFFFFFu, nullptr, 0);
		for (UINT i = 0; i < count; ++i) {
			const UINT len = ::DragQueryFileW(drop, i, nullptr, 0);
			if (len == 0) continue;
			std::wstring buf(static_cast<size_t>(len) + 1, L'\0');
			const UINT got = ::DragQueryFileW(drop, i, &buf[0], len + 1);
			if (got == 0) continue;
			buf.resize(got);
			out.push_back(WideToUtf8(buf));
		}
	}

	std::wstring GetProjectName() {		wchar_t path[MAX_PATH] = {};
		GetModuleFileNameW(nullptr, path, MAX_PATH);
		const wchar_t* fileName = PathFindFileNameW(path);
		std::wstring name = fileName ? fileName : L"";
		const size_t dot = name.find_last_of(L'.');
		if (dot != std::wstring::npos) name.erase(dot);
		return name;
	}

	void UnpackMouseXY(LPARAM lParam, float& x, float& y) {
		x = static_cast<float>(static_cast<short>(LOWORD(lParam)));
		y = static_cast<float>(static_cast<short>(HIWORD(lParam)));
	}

	Plat::Modifiers CurrentModifiers() {
		Plat::Modifiers m = Plat::Modifiers::None;
		if (GetKeyState(VK_SHIFT) & 0x8000)   m = m | Plat::Modifiers::Shift;
		if (GetKeyState(VK_CONTROL) & 0x8000) m = m | Plat::Modifiers::Control;
		if (GetKeyState(VK_MENU) & 0x8000)    m = m | Plat::Modifiers::Alt;
		if ((GetKeyState(VK_LWIN) & 0x8000) || (GetKeyState(VK_RWIN) & 0x8000))
			m = m | Plat::Modifiers::Super;
		return m;
	}

	Plat::MouseButton MouseButtonFromMsg(UINT msg, WPARAM wParam) {
		switch (msg) {
		case WM_LBUTTONDOWN: case WM_LBUTTONUP: return Plat::MouseButton::Left;
		case WM_RBUTTONDOWN: case WM_RBUTTONUP: return Plat::MouseButton::Right;
		case WM_MBUTTONDOWN: case WM_MBUTTONUP: return Plat::MouseButton::Middle;
		case WM_XBUTTONDOWN: case WM_XBUTTONUP:
			return (HIWORD(wParam) == XBUTTON1) ? Plat::MouseButton::X1 : Plat::MouseButton::X2;
		default: return Plat::MouseButton::None;
		}
	}

	constexpr UINT_PTR kWakeTimerId = 0x5A01;

	constexpr UINT kWakePostedMsg = WM_APP + 0x5A;

}

namespace Plat {

	class Win32DropTarget;

	namespace {
		inline uint32_t ToKey(UINT vk) {
			switch (vk) {
			case 'A': case 'B': case 'C': case 'D': case 'E': case 'F': case 'G':
			case 'H': case 'I': case 'J': case 'K': case 'L': case 'M': case 'N':
			case 'O': case 'P': case 'Q': case 'R': case 'S': case 'T': case 'U':
			case 'V': case 'W': case 'X': case 'Y': case 'Z':
			case '0': case '1': case '2': case '3': case '4':
			case '5': case '6': case '7': case '8': case '9':
				return static_cast<uint32_t>(vk);

			case VK_F1: case VK_F2: case VK_F3: case VK_F4:  case VK_F5:  case VK_F6:
			case VK_F7: case VK_F8: case VK_F9: case VK_F10: case VK_F11: case VK_F12:
			case VK_F13: case VK_F14: case VK_F15: case VK_F16: case VK_F17:
			case VK_F18: case VK_F19: case VK_F20: case VK_F21: case VK_F22:
			case VK_F23: case VK_F24:
			case VK_NUMPAD0: case VK_NUMPAD1: case VK_NUMPAD2: case VK_NUMPAD3:
			case VK_NUMPAD4: case VK_NUMPAD5: case VK_NUMPAD6: case VK_NUMPAD7:
			case VK_NUMPAD8: case VK_NUMPAD9:
			case VK_MULTIPLY: case VK_ADD: case VK_SUBTRACT:
			case VK_DECIMAL:  case VK_DIVIDE:
			case VK_OEM_1: case VK_OEM_PLUS: case VK_OEM_COMMA: case VK_OEM_MINUS:
			case VK_OEM_PERIOD: case VK_OEM_2: case VK_OEM_3: case VK_OEM_4:
			case VK_OEM_5: case VK_OEM_6: case VK_OEM_7:
			case VK_ESCAPE: case VK_TAB:    case VK_CAPITAL:
			case VK_SHIFT:  case VK_CONTROL: case VK_MENU:
			case VK_SPACE:  case VK_RETURN: case VK_BACK:
			case VK_INSERT: case VK_DELETE: case VK_HOME: case VK_END:
			case VK_PRIOR:  case VK_NEXT:
			case VK_LEFT:   case VK_UP:     case VK_RIGHT: case VK_DOWN:
			case VK_VOLUME_MUTE: case VK_VOLUME_DOWN: case VK_VOLUME_UP:
			case VK_MEDIA_NEXT_TRACK: case VK_MEDIA_PREV_TRACK:
			case VK_MEDIA_STOP: case VK_MEDIA_PLAY_PAUSE:
			case VK_LAUNCH_MAIL: case VK_LAUNCH_MEDIA_SELECT:
			case VK_LAUNCH_APP1: case VK_LAUNCH_APP2:
			case VK_LWIN: case VK_RWIN: case VK_APPS:
			case VK_SNAPSHOT: case VK_SCROLL: case VK_PAUSE: case VK_NUMLOCK: case VK_SLEEP:
				switch (vk) {
				case VK_SHIFT:    return 0x10;
				case VK_CONTROL:  return 0x11;
				case VK_MENU:     return 0x12;
				case VK_PRIOR:    return 0x21;
				case VK_NEXT:     return 0x22;
				case VK_MULTIPLY: return 0x6A;
				case VK_ADD:      return 0x6B;
				case VK_SUBTRACT: return 0x6D;
				case VK_DECIMAL:  return 0x6E;
				case VK_DIVIDE:   return 0x6F;
				case VK_OEM_1:      return 0xBA;
				case VK_OEM_PLUS:   return 0xBB;
				case VK_OEM_COMMA:  return 0xBC;
				case VK_OEM_MINUS:  return 0xBD;
				case VK_OEM_PERIOD: return 0xBE;
				case VK_OEM_2:      return 0xBF;
				case VK_OEM_3:      return 0xC0;
				case VK_OEM_4:      return 0xDB;
				case VK_OEM_5:      return 0xDC;
				case VK_OEM_6:      return 0xDD;
				case VK_OEM_7:      return 0xDE;
				case VK_LWIN:       return 0x5B;
				case VK_RWIN:       return 0x5C;
				default:
					return static_cast<uint32_t>(vk);
				}
			default:
				return 0;
			}
		}
	}

	struct OwnedScan { HWND owner; HWND exclude; bool found; };
	BOOL CALLBACK CountOtherOwnedProc(HWND h, LPARAM p) {
		auto* c = reinterpret_cast<OwnedScan*>(p);
		if (h != c->exclude && ::GetWindow(h, GW_OWNER) == c->owner && ::IsWindowVisible(h))
			c->found = true;
		return TRUE;
	}

	struct Win32Window::Impl {
		HWND        m_hwnd = nullptr;
		HINSTANCE   m_hInstance = nullptr;
		IWindowHost* m_host = nullptr;

		bool        m_isChild = false;

		bool        m_isOwned = false;

		bool        m_noActivate = false;

		void HandBackForegroundToOwner() {
			if (!m_isOwned || !m_hostHwnd || !m_hwnd || m_hostHwnd == m_hwnd) return;
			if (::GetForegroundWindow() != m_hwnd) return;
			OwnedScan scan{ m_hostHwnd, m_hwnd, false };
			::EnumThreadWindows(::GetCurrentThreadId(), &CountOtherOwnedProc,
				reinterpret_cast<LPARAM>(&scan));
			if (scan.found) return;
			if (::IsWindowVisible(m_hostHwnd) && ::IsWindowEnabled(m_hostHwnd))
				::SetForegroundWindow(m_hostHwnd);
		}

		HWND        m_hostHwnd = nullptr;

		bool m_trackingLeave = false;

		HCURSOR m_cursor = nullptr;

		bool m_active = false;

		bool m_dark = false;

		int m_exitCode = 0;

		unsigned long long m_wakeDeadlineMs = 0;

		bool m_acceptDrop = false;
		bool m_oleReady = false;
		Win32DropTarget* m_dropTarget = nullptr;
		bool m_dropViaHdrop = false;

		HICON m_hIconBig = nullptr;
		HICON m_hIconSmall = nullptr;

		bool m_inFrame = false;

		bool m_centerOnResize = true;

		bool m_borderless = false;
		DWORD m_borderlessSavedStyle = 0;
		DWORD m_borderlessSavedExStyle = 0;
		bool m_borderlessResize = false;
		bool m_fullscreen = false;
		DWORD m_fullscreenSavedStyle = 0;
		RECT  m_fullscreenSavedRect{};

		bool m_popup = false;
		bool m_clickThrough = false;
		float m_winAlpha = 1.0f;
		bool m_noRedirection = false;

		bool m_titleBarVisible = true;
		bool m_minBtnEnabled = true;
		bool m_maxBtnEnabled = true;
		bool m_closeBtnEnabled = true;

		void ApplyTitleBarImpl();

		void SetIconsCore(HICON hBig, HICON hSmall);

		static Impl* From(HWND hwnd) {
			return reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
		}

		void EnsureMouseLeaveTracking(HWND hwnd) {
			if (m_trackingLeave) return;
			TRACKMOUSEEVENT tme{};
			tme.cbSize = sizeof(tme);
			tme.dwFlags = TME_LEAVE;
			tme.hwndTrack = hwnd;
			if (TrackMouseEvent(&tme)) m_trackingLeave = true;
		}

		void RequestRedraw(bool scheduled = false) {
			if (!m_host) return;
			m_host->OnRequestRedraw(scheduled
				? IWindowHost::RedrawReason::Scheduled
				: IWindowHost::RedrawReason::System);
		}

		void ApplyTitleBarDark(bool dark) {
			m_dark = dark;
			SetTitleBarDark(m_hwnd, dark);
		}

		void ApplyDropTarget();

		void EmitFileEvent(EventKind kind, POINTL pt, std::vector<std::string> files) {
			if (!m_host) return;
			Event e;
			e.kind = kind;
			POINT p{ pt.x, pt.y };
			if (m_hwnd) ::ScreenToClient(m_hwnd, &p);
			e.x = static_cast<float>(p.x);
			e.y = static_cast<float>(p.y);
			e.files = std::move(files);
			m_host->OnPlatformEvent(e);
		}

		bool HostAcceptsDropAt(POINTL pt) const {
			if (!m_host) return false;
			POINT p{ pt.x, pt.y };
			if (m_hwnd) ::ScreenToClient(m_hwnd, &p);
			return m_host->AcceptsFileDropAt(static_cast<float>(p.x), static_cast<float>(p.y));
		}

		static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
	};

	class Win32DropTarget final : public IDropTarget {
	public:
		explicit Win32DropTarget(Win32Window::Impl* impl) : m_impl(impl) {}
		~Win32DropTarget() = default;

		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
			if (!ppv) return E_POINTER;
			if (riid == IID_IUnknown || riid == IID_IDropTarget) {
				*ppv = static_cast<IDropTarget*>(this);
				AddRef();
				return S_OK;
			}
			*ppv = nullptr;
			return E_NOINTERFACE;
		}
		ULONG STDMETHODCALLTYPE AddRef() override { return static_cast<ULONG>(::InterlockedIncrement(&m_refs)); }
		ULONG STDMETHODCALLTYPE Release() override {
			const LONG left = ::InterlockedDecrement(&m_refs);
			if (left == 0) delete this;
			return static_cast<ULONG>(left);
		}

		HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* data, DWORD, POINTL pt, DWORD* effect) override {
			std::vector<std::string> files;
			const bool ok = ReadFileList(data, files);
			if (ok) {
				m_dragging = true;
				m_impl->EmitFileEvent(EventKind::FileDragEnter, pt, std::move(files));
			}
			if (effect) *effect = ok && m_impl->HostAcceptsDropAt(pt) ? DROPEFFECT_COPY : DROPEFFECT_NONE;
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE DragOver(DWORD, POINTL pt, DWORD* effect) override {
			if (m_dragging) m_impl->EmitFileEvent(EventKind::FileDragOver, pt, {});
			if (effect) *effect = m_dragging && m_impl->HostAcceptsDropAt(pt) ? DROPEFFECT_COPY : DROPEFFECT_NONE;
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE DragLeave() override {
			if (m_dragging) {
				m_dragging = false;
				POINTL none{ 0, 0 };
				m_impl->EmitFileEvent(EventKind::FileDragLeave, none, {});
			}
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE Drop(IDataObject* data, DWORD, POINTL pt, DWORD* effect) override {
			std::vector<std::string> files;
			const bool ok = ReadFileList(data, files);
			m_dragging = false;
			if (ok) m_impl->EmitFileEvent(EventKind::FileDrop, pt, std::move(files));
			if (effect) *effect = ok ? DROPEFFECT_COPY : DROPEFFECT_NONE;
			return S_OK;
		}

	private:
		static bool ReadFileList(IDataObject* data, std::vector<std::string>& out) {
			if (!data) return false;
			FORMATETC fmt{};
			fmt.cfFormat = CF_HDROP;
			fmt.dwAspect = DVASPECT_CONTENT;
			fmt.lindex = -1;
			fmt.tymed = TYMED_HGLOBAL;

			STGMEDIUM stm{};
			if (FAILED(data->GetData(&fmt, &stm))) return false;

			bool ok = false;
			if (stm.tymed == TYMED_HGLOBAL && stm.hGlobal) {
				if (HDROP drop = static_cast<HDROP>(::GlobalLock(stm.hGlobal))) {
					ReadHdrop(drop, out);
					::GlobalUnlock(stm.hGlobal);
					ok = !out.empty();
				}
			}
			::ReleaseStgMedium(&stm);
			return ok;
		}

		Win32Window::Impl* m_impl = nullptr;
		LONG  m_refs = 1;
		bool  m_dragging = false;
	};

	void Win32Window::Impl::ApplyDropTarget() {
		const bool alive = (m_hwnd != nullptr);

		if (alive && m_dropViaHdrop) {
			::DragAcceptFiles(m_hwnd, FALSE);
		}
		m_dropViaHdrop = false;
		if (m_dropTarget) {
			if (alive) ::RevokeDragDrop(m_hwnd);
			m_dropTarget->Release();
			m_dropTarget = nullptr;
		}
		if (!alive || !m_acceptDrop) return;

		if (m_oleReady) {
			auto* target = new Win32DropTarget(this);
			const HRESULT hr = ::RegisterDragDrop(m_hwnd, static_cast<IDropTarget*>(target));
			if (SUCCEEDED(hr)) {
				m_dropTarget = target;
				return;
			}
			std::cerr << "[Zekit/drop] RegisterDragDrop 失败 HRESULT=0x"
				<< std::hex << static_cast<unsigned long>(hr) << std::dec
				<< "（退回 WM_DROPFILES：只有 Drop、没有 Enter/Over）\n";
			target->Release();
		}
		else {
			std::cerr << "[Zekit/drop] OLE 未初始化，只能用 WM_DROPFILES（只有 Drop）\n";
		}

		::DragAcceptFiles(m_hwnd, TRUE);
		m_dropViaHdrop = true;
	}

	LRESULT CALLBACK Win32Window::Impl::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		if (msg == WM_NCCREATE) {
			auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
			auto* self = static_cast<Impl*>(cs->lpCreateParams);
			SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
			self->m_hwnd = hwnd;
			return DefWindowProcW(hwnd, msg, wParam, lParam);
		}

		Impl* self = Impl::From(hwnd);
		if (!self || !self->m_host)
			return DefWindowProcW(hwnd, msg, wParam, lParam);

		IWindowHost& host = *self->m_host;

		switch (msg) {
		case WM_NCHITTEST: {
			if (self->m_clickThrough) return HTTRANSPARENT;

			if (self->m_noRedirection && self->m_host) {
				const POINT pt{ static_cast<short>(LOWORD(lParam)),
				                static_cast<short>(HIWORD(lParam)) };
				POINT local = pt;
				if (ScreenToClient(hwnd, &local)) {
					if (!self->m_host->IsOpaqueAt(static_cast<float>(local.x),
					                              static_cast<float>(local.y)))
						return HTTRANSPARENT;
				}
			}

			if (!(self->m_borderless && self->m_borderlessResize)
				|| IsZoomed(hwnd) || self->m_fullscreen)
				break;
			const POINT pt{ static_cast<short>(LOWORD(lParam)),
			                static_cast<short>(HIWORD(lParam)) };
			RECT rc{};
			if (!GetWindowRect(hwnd, &rc)) break;
			const int x = pt.x - rc.left;
			const int y = pt.y - rc.top;
			const int w = rc.right - rc.left;
			const int h = rc.bottom - rc.top;
			constexpr int EDGE = 8;
			const bool left   = x < EDGE;
			const bool right  = x >= w - EDGE;
			const bool top    = y < EDGE;
			const bool bottom = y >= h - EDGE;
			if (left && top)     return HTTOPLEFT;
			if (right && top)    return HTTOPRIGHT;
			if (left && bottom)  return HTBOTTOMLEFT;
			if (right && bottom) return HTBOTTOMRIGHT;
			if (left)   return HTLEFT;
			if (right)  return HTRIGHT;
			if (top)    return HTTOP;
			if (bottom) return HTBOTTOM;
			break;
		}
		case WM_GETMINMAXINFO: {
			auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);

			DefWindowProcW(hwnd, msg, wParam, lParam);

			const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE));
			const DWORD ex = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE));

			int minW = 0, minH = 0, maxW = 0, maxH = 0;
			host.QuerySizeLimits(minW, minH, maxW, maxH);
			(void)maxW; (void)maxH;

			if (minW > 0) {
				RECT rc{ 0, 0, minW, 0 };
				AdjustWindowRectEx(&rc, style, FALSE, ex);
				mmi->ptMinTrackSize.x = std::max(mmi->ptMinTrackSize.x, rc.right - rc.left);
			}
			if (minH > 0) {
				RECT rc{ 0, 0, 0, minH };
				AdjustWindowRectEx(&rc, style, FALSE, ex);
				mmi->ptMinTrackSize.y = std::max(mmi->ptMinTrackSize.y, rc.bottom - rc.top);
			}

			const HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
			MONITORINFO mi{ sizeof(mi) };
			if (GetMonitorInfoW(mon, &mi)) {
				const int bx = GetSystemMetrics(SM_CXSIZEFRAME)
					+ GetSystemMetrics(SM_CXPADDEDBORDER);
				const int by = GetSystemMetrics(SM_CYSIZEFRAME)
					+ GetSystemMetrics(SM_CXPADDEDBORDER);
				mmi->ptMaxSize.x = (mi.rcWork.right - mi.rcWork.left) + 2 * bx;
				mmi->ptMaxSize.y = (mi.rcWork.bottom - mi.rcWork.top) + 2 * by;
				mmi->ptMaxPosition.x = mi.rcWork.left - bx;
				mmi->ptMaxPosition.y = mi.rcWork.top - by;
			}
			return 0;
		}

		case WM_SIZE:
			if (Plat::TraceFrame())
				std::cerr << "[win32] WM_SIZE 触发重绘 " << LOWORD(lParam)
				<< "x" << HIWORD(lParam) << "\n";
			self->RequestRedraw();
			return 0;

		case WM_WINDOWPOSCHANGED: {
			const auto* wp = reinterpret_cast<const WINDOWPOS*>(lParam);
			const bool moved = wp && !(wp->flags & SWP_NOMOVE);
			const bool sized = wp && !(wp->flags & SWP_NOSIZE);
			if (!wp || moved || sized)
				host.OnHostGeometryChanged();
			break;
		}

		case WM_SIZING: {
			int minW = 0, minH = 0, maxW = 0, maxH = 0;
			host.QuerySizeLimits(minW, minH, maxW, maxH);
			(void)minW; (void)minH;

			if (maxW <= 0 && maxH <= 0) break;
			RECT* rc = reinterpret_cast<RECT*>(lParam);
			if (!rc) break;

			const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE));
			const DWORD ex = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE));
			RECT box{ 0, 0, 0, 0 };
			AdjustWindowRectEx(&box, style, FALSE, ex);
			const int frameW = box.right - box.left;
			const int frameH = box.bottom - box.top;

			const int maxOuterW = (maxW > 0) ? maxW + frameW : (1 << 30);
			const int maxOuterH = (maxH > 0) ? maxH + frameH : (1 << 30);

			switch (wParam) {
			case WMSZ_RIGHT: case WMSZ_TOPRIGHT: case WMSZ_BOTTOMRIGHT:
				rc->right = std::min(rc->right, rc->left + maxOuterW);
				break;
			case WMSZ_LEFT: case WMSZ_TOPLEFT: case WMSZ_BOTTOMLEFT:
				rc->left = std::max(rc->left, rc->right - maxOuterW);
				break;
			default:
				break;
			}
			switch (wParam) {
			case WMSZ_BOTTOM: case WMSZ_BOTTOMLEFT: case WMSZ_BOTTOMRIGHT:
				rc->bottom = std::min(rc->bottom, rc->top + maxOuterH);
				break;
			case WMSZ_TOP: case WMSZ_TOPLEFT: case WMSZ_TOPRIGHT:
				rc->top = std::max(rc->top, rc->bottom - maxOuterH);
				break;
			default:
				break;
			}
			return TRUE;
		}

		case WM_PAINT: {
			if (Plat::TraceFrame())
				std::cerr << "[win32] WM_PAINT 触发重绘\n";
			self->RequestRedraw();
			PAINTSTRUCT ps{};
			BeginPaint(hwnd, &ps);
			EndPaint(hwnd, &ps);
			return 0;
		}

		case WM_TIMER:
			if (wParam == kWakeTimerId) {
				KillTimer(hwnd, kWakeTimerId);
				self->m_wakeDeadlineMs = 0;
				self->RequestRedraw(true);
				return 0;
			}
			break;

		case WM_DROPFILES: {
			HDROP drop = reinterpret_cast<HDROP>(wParam);
			Event e;
			e.kind = EventKind::FileDrop;
			POINT p{};
			if (::DragQueryPoint(drop, &p)) {
				e.x = static_cast<float>(p.x);
				e.y = static_cast<float>(p.y);
			}
			ReadHdrop(drop, e.files);
			::DragFinish(drop);
			if (!e.files.empty()) host.OnPlatformEvent(e);
			return 0;
		}

		case kWakePostedMsg: {
			Event e;
			e.kind = EventKind::Wake;
			host.OnPlatformEvent(e);
			return 0;
		}

		case WM_ERASEBKGND:			return 1;

		case WM_EXITSIZEMOVE:
			self->RequestRedraw();
			ValidateRect(hwnd, nullptr);
			return 0;

		case WM_SETCURSOR:
			if (LOWORD(lParam) == HTCLIENT && self->m_cursor) {
				::SetCursor(self->m_cursor);
				return TRUE;
			}
			break;

		case WM_MOUSEMOVE: {
			self->EnsureMouseLeaveTracking(hwnd);
			Event e;
			e.kind = EventKind::MouseMove;
			UnpackMouseXY(lParam, e.x, e.y);
			e.mods = CurrentModifiers();
			host.OnPlatformEvent(e);
			return 0;
		}

		case WM_MOUSELEAVE:
			self->m_trackingLeave = false;
			{
				Event e;
				e.kind = EventKind::MouseLeave;
				host.OnPlatformEvent(e);
			}
			return 0;

		case WM_LBUTTONDOWN: case WM_RBUTTONDOWN:
		case WM_MBUTTONDOWN: case WM_XBUTTONDOWN: {
			if (!self->m_noActivate && ::GetFocus() != hwnd) ::SetFocus(hwnd);

			SetCapture(hwnd);
			Event e;
			e.kind = EventKind::MouseDown;
			UnpackMouseXY(lParam, e.x, e.y);
			e.button = MouseButtonFromMsg(msg, wParam);
			e.mods = CurrentModifiers();
			host.OnPlatformEvent(e);
			return 0;
		}

		case WM_LBUTTONUP: case WM_RBUTTONUP:
		case WM_MBUTTONUP: case WM_XBUTTONUP: {
			if (GetCapture() == hwnd) ReleaseCapture();
			Event e;
			e.kind = EventKind::MouseUp;
			UnpackMouseXY(lParam, e.x, e.y);
			e.button = MouseButtonFromMsg(msg, wParam);
			e.mods = CurrentModifiers();
			host.OnPlatformEvent(e);
			return 0;
		}

		case WM_MOUSEWHEEL: {
			POINT pt{ static_cast<LONG>(static_cast<short>(LOWORD(lParam))),
					  static_cast<LONG>(static_cast<short>(HIWORD(lParam))) };
			ScreenToClient(hwnd, &pt);

			Event e;
			e.kind = EventKind::MouseWheel;
			e.x = static_cast<float>(pt.x);
			e.y = static_cast<float>(pt.y);
			e.wheelDelta = GET_WHEEL_DELTA_WPARAM(wParam);
			e.mods = CurrentModifiers();
			host.OnPlatformEvent(e);
			return 0;
		}

		case WM_KEYDOWN:
		case WM_SYSKEYDOWN: {
			Event e;
			e.kind = EventKind::KeyDown;
			e.key = ToKey(static_cast<UINT>(wParam));
			e.virtualKey = static_cast<uint32_t>(wParam);
			e.scanCode = static_cast<uint32_t>((lParam >> 16) & 0xFFu);
			e.isRepeat = (lParam & (1u << 30)) != 0;
			e.mods = CurrentModifiers();
			host.OnPlatformEvent(e);
			return 0;
		}

		case WM_KEYUP:
		case WM_SYSKEYUP: {
			Event e;
			e.kind = EventKind::KeyUp;
			e.key = ToKey(static_cast<UINT>(wParam));
			e.virtualKey = static_cast<uint32_t>(wParam);
			e.scanCode = static_cast<uint32_t>((lParam >> 16) & 0xFFu);
			e.mods = CurrentModifiers();
			host.OnPlatformEvent(e);
			return 0;
		}

		case WM_CHAR: {
			Event e;
			e.kind = EventKind::CharInput;
			e.codepoint = static_cast<uint32_t>(wParam);
			e.mods = CurrentModifiers();
			host.OnPlatformEvent(e);
			return 0;
		}

		case WM_KILLFOCUS:
			self->m_active = false;
			self->m_trackingLeave = false;
			{
				Event e;
				e.kind = EventKind::FocusOut;
				host.OnPlatformEvent(e);
			}
			return 0;

		case WM_SETFOCUS:
			self->m_active = true;
			{
				Event e;
				e.kind = EventKind::FocusIn;
				host.OnPlatformEvent(e);
			}
			return 0;

		case WM_ACTIVATE:
			self->m_active = (LOWORD(wParam) != WA_INACTIVE);
			return 0;

		case WM_IME_SETCONTEXT: {
			if (wParam && lParam) {
				lParam &= ~ISC_SHOWUICOMPOSITIONWINDOW;
			}
			break;
		}
		case WM_IME_STARTCOMPOSITION: {
			Event e;
			e.kind = EventKind::ImeStart;
			if (host.OnPlatformEvent(e)) return 0;
			break;
		}
		case WM_IME_COMPOSITION: {
			Event e;
			e.kind = EventKind::ImeCompose;
			e.nativeData = static_cast<uintptr_t>(lParam);
			e.nativeData2 = static_cast<uintptr_t>(wParam);
			if (host.OnPlatformEvent(e)) return 0;
			break;
		}
		case WM_IME_ENDCOMPOSITION: {
			Event e;
			e.kind = EventKind::ImeEnd;
			if (host.OnPlatformEvent(e)) return 0;
			break;
		}
		case WM_IME_CHAR: {
			Event e;
			e.kind = EventKind::ImeChar;
			e.codepoint = static_cast<uint32_t>(wParam);
			if (host.OnPlatformEvent(e)) return 0;
			break;
		}
		case WM_INPUTLANGCHANGE:
			break;

		case WM_SETTINGCHANGE:
			if (!lParam || std::wcscmp(reinterpret_cast<LPCWSTR>(lParam), L"ImmersiveColorSet") == 0) {
				self->m_dark = ReadSystemPrefersDark();
				SetTitleBarDark(hwnd, self->m_dark);
				host.OnSystemSettingsChanged();
			}
			return 0;

		case WM_SYSCOMMAND:
			{
				Event e;
				e.kind = EventKind::SystemButtonGate;
				switch (static_cast<unsigned>(wParam) & 0xFFF0u) {
				case SC_CLOSE:    e.systemButton = SystemButton::Close;    break;
				case SC_MINIMIZE: e.systemButton = SystemButton::Minimize; break;
				case SC_MAXIMIZE: e.systemButton = SystemButton::Maximize; break;
				default: break;
				}
				if (e.systemButton != SystemButton::None && host.OnPlatformEvent(e))
					return 0;
			}
			break;

		case WM_NCLBUTTONDOWN:
		case WM_NCLBUTTONDBLCLK:
			{
				Event e;
				e.kind = EventKind::SystemButtonGate;
				switch (static_cast<unsigned>(wParam)) {
				case HTMINBUTTON: e.systemButton = SystemButton::Minimize; break;
				case HTMAXBUTTON: e.systemButton = SystemButton::Maximize; break;
				case HTCLOSE:     e.systemButton = SystemButton::Close;    break;
				default: break;
				}
				if (e.systemButton != SystemButton::None && host.OnPlatformEvent(e))
					return 0;
			}
			break;

		case WM_CLOSE:
			{
				Event e;
				e.kind = EventKind::Close;
				if (!host.OnPlatformEvent(e)) {
					self->HandBackForegroundToOwner();
					break;
				}
				return 0;
			}

		case WM_DESTROY:
			SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
			self->m_hwnd = nullptr;
			host.OnNativeClosed();
			return 0;

		default:
			break;
		}
		return DefWindowProcW(hwnd, msg, wParam, lParam);
	}

	Win32Window::Win32Window() : m_impl(std::make_unique<Impl>()) {}
	Win32Window::~Win32Window() {
		Destroy();
	}

	bool Win32Window::Create(const WindowDesc& desc, IWindowHost& host) {
		EnableDPIAwareness();

		m_impl->m_host = &host;
		m_impl->m_hInstance = GetModuleHandleW(nullptr);

		const wchar_t CLASS_NAME[] = L"ZekitWindowClass";
		const wchar_t POPUP_CLASS_NAME[] = L"ZekitPopupClass";

		{
			WNDCLASSEXW wc = {};
			wc.cbSize = sizeof(wc);
			wc.style = CS_OWNDC;
			wc.lpfnWndProc = m_impl->WndProc;
			wc.hInstance = m_impl->m_hInstance;
			wc.lpszClassName = CLASS_NAME;
			wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
			wc.hbrBackground = nullptr;

			if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
				MessageBoxW(nullptr, L"Failed to register window class", L"Error", MB_ICONERROR);
				return false;
			}

			WNDCLASSEXW pc = wc;
			pc.lpszClassName = POPUP_CLASS_NAME;
			pc.style |= CS_DROPSHADOW;
			RegisterClassExW(&pc);
		}

		const bool isChild = (desc.parent != nullptr);
		const bool isOwned = isChild && (desc.relation == Relation::Owned);
		const bool isPopup = isChild && (desc.relation == Relation::Popup);
		const bool isFree = isChild && (desc.relation == Relation::Free);
		m_impl->m_isChild = isChild && !isOwned && !isPopup && !isFree;
		m_impl->m_isOwned = isOwned || isPopup || isFree;
		m_impl->m_popup = isPopup;
		m_impl->m_hostHwnd = static_cast<HWND>(desc.parent);

		int w = desc.width;
		int h = desc.height;
		int x = 0;
		int y = 0;
		if (isChild) {
			x = desc.x;
			y = desc.y;
			if (isOwned || isPopup || isFree) {
				if (const HWND owner = static_cast<HWND>(desc.parent)) {
					POINT pt{ x, y };
					ClientToScreen(owner, &pt);
					x = pt.x;
					y = pt.y;
				}
			}
		} else {
			const int screenW = GetSystemMetrics(SM_CXSCREEN);
			const int screenH = GetSystemMetrics(SM_CYSCREEN);
			if (desc.frameless) {
				x = desc.x;
				y = desc.y;
				if (x == INT_MIN || y == INT_MIN) {
					x = (screenW - w) / 2;
					y = (screenH - h) / 2;
				}
			}
			else {
				if (desc.centered) {
					w = static_cast<int>(screenW * 0.53);
					h = static_cast<int>(screenH * 0.53);
				}
				x = (screenW - w) / 2;
				y = (screenH - h) / 2;
			}
		}

		std::wstring title = Utf8ToWide(desc.title);
		if (title.empty()) title = GetProjectName();
		if (title.empty()) title = L"Zekit";

		DWORD style = 0;
		DWORD exStyle = 0;
		if (isPopup) {
			style = WS_POPUP | WS_CLIPCHILDREN;
			exStyle = WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST;
			m_impl->m_noActivate = true;
			if (desc.perPixelTransparent) {
				exStyle |= WS_EX_NOREDIRECTIONBITMAP;
				m_impl->m_noRedirection = true;
			}
		}
		else if (isOwned || isFree) {
			style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
			exStyle = WS_EX_APPWINDOW;
		}
		else if (isChild) {
			style = WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
		}
		else if (desc.frameless) {
			style = WS_POPUP | WS_CLIPCHILDREN;
			exStyle = WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE;
			m_impl->m_noActivate = true;
			if (desc.perPixelTransparent) {
				exStyle |= WS_EX_NOREDIRECTIONBITMAP;
				m_impl->m_noRedirection = true;
			}
		}
		else {
			style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
			if (!desc.resizable) style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX | WS_MINIMIZEBOX);
		}

		if (isOwned || isPopup || isFree) {
			RECT r{ 0, 0, w, h };
			if (AdjustWindowRectEx(&r, style, FALSE, exStyle)) {
				w = r.right - r.left;
				h = r.bottom - r.top;
			}
		}

		m_impl->m_hwnd = CreateWindowExW(
			exStyle,
			(isPopup && !m_impl->m_noRedirection && desc.systemShadow)
				? POPUP_CLASS_NAME : CLASS_NAME,
			title.c_str(),
			style,
			x, y, w, h,
			isFree ? nullptr : static_cast<HWND>(desc.parent),
			nullptr, m_impl->m_hInstance,
			m_impl.get()
		);
		if (!m_impl->m_hwnd) {
			MessageBoxW(nullptr, L"Failed to create window", L"Error", MB_ICONERROR);
			return false;
		}

		if ((!isChild || isOwned || isFree) && !isPopup) {
			m_impl->m_dark = ReadSystemPrefersDark();
			SetTitleBarDark(m_impl->m_hwnd, m_impl->m_dark);
		}

		if (m_impl->m_noRedirection
			|| (isPopup && !desc.systemShadow))   DisableSystemFrame(m_impl->m_hwnd);
		else if (isPopup)                    ApplyCornerPreference(m_impl->m_hwnd, 2 );

		{
			const HRESULT hr = ::OleInitialize(nullptr);
			if (SUCCEEDED(hr)) m_impl->m_oleReady = true;
		}

		m_impl->ApplyDropTarget();

		return true;
	}

	void Win32Window::Show() {
		if (!m_impl->m_hwnd) return;
		ShowWindow(m_impl->m_hwnd, m_impl->m_popup ? SW_SHOWNOACTIVATE : SW_SHOWDEFAULT);
		UpdateWindow(m_impl->m_hwnd);
	}

	void Win32Window::Hide() {
		if (m_impl->m_hwnd)
			ShowWindow(m_impl->m_hwnd, SW_HIDE);
	}

	void Win32Window::Destroy() {
		if (!m_impl) return;

		m_impl->m_acceptDrop = false;
		m_impl->ApplyDropTarget();
		if (m_impl->m_oleReady) {
			::OleUninitialize();
			m_impl->m_oleReady = false;
		}

		if (m_impl->m_hIconBig) { DestroyIcon(m_impl->m_hIconBig);   m_impl->m_hIconBig = nullptr; }
		if (m_impl->m_hIconSmall) { DestroyIcon(m_impl->m_hIconSmall); m_impl->m_hIconSmall = nullptr; }

		if (m_impl->m_hwnd) {
			HWND h = m_impl->m_hwnd;
			m_impl->HandBackForegroundToOwner();
			m_impl->m_hwnd = nullptr;
			DestroyWindow(h);
		}

		m_impl->m_host = nullptr;
	}

	bool Win32Window::PollEvent() {
		MSG msg{};
		if (!PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) return false;

		if (msg.message == WM_QUIT) {
			m_impl->m_exitCode = static_cast<int>(msg.wParam);
			Event e;
			e.kind = EventKind::Quit;
			if (m_impl->m_host) m_impl->m_host->OnPlatformEvent(e);
			return true;
		}

		TranslateMessage(&msg);
		DispatchMessageW(&msg);
		return true;
	}

	void Win32Window::WaitEvents(int timeoutMs) {
		const DWORD t = (timeoutMs < 0) ? INFINITE : static_cast<DWORD>(timeoutMs);
		MsgWaitForMultipleObjects(0, nullptr, FALSE, t, QS_ALLINPUT);
	}

	int Win32Window::ExitCode() const { return m_impl->m_exitCode; }

	void Win32Window::GetClientSize(int& w, int& h) const {
		w = 0; h = 0;
		if (!m_impl->m_hwnd) return;
		RECT rc{};
		if (!GetClientRect(m_impl->m_hwnd, &rc)) return;
		w = rc.right - rc.left;
		h = rc.bottom - rc.top;
	}

	void Win32Window::SetClientSize(int w, int h) {
		if (!m_impl->m_hwnd) return;
		w = std::max(w, 1);
		h = std::max(h, 1);
		RECT rc{ 0, 0, w, h };
		AdjustWindowRectEx(&rc,
			static_cast<DWORD>(GetWindowLongPtrW(m_impl->m_hwnd, GWL_STYLE)),
			FALSE,
			static_cast<DWORD>(GetWindowLongPtrW(m_impl->m_hwnd, GWL_EXSTYLE)));
		const int frameW = rc.right - rc.left;
		const int frameH = rc.bottom - rc.top;

		const bool noPosition = m_impl->m_isChild || m_impl->m_isOwned
			|| !m_impl->m_centerOnResize
			|| IsIconic(m_impl->m_hwnd) || IsZoomed(m_impl->m_hwnd);
		const HMONITOR mon = MonitorFromWindow(m_impl->m_hwnd, MONITOR_DEFAULTTONEAREST);
		MONITORINFO mi{ sizeof(mi) };
		if (!GetMonitorInfoW(mon, &mi)) {
			SetWindowPos(m_impl->m_hwnd, nullptr, 0, 0, frameW, frameH,
				SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
			return;
		}

		const int workW = mi.rcWork.right - mi.rcWork.left;
		const int workH = mi.rcWork.bottom - mi.rcWork.top;
		if (noPosition) {
			SetWindowPos(m_impl->m_hwnd, nullptr, 0, 0, frameW, frameH,
				SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
			return;
		}
		const int x = mi.rcWork.left + (workW - frameW) / 2;
		const int y = mi.rcWork.top + (workH - frameH) / 2;
		SetWindowPos(m_impl->m_hwnd, nullptr, x, y, frameW, frameH,
			SWP_NOZORDER | SWP_NOACTIVATE);
	}

	void Win32Window::SetCenterOnResize(bool on) { m_impl->m_centerOnResize = on; }

	bool Win32Window::GetScreenWorkArea(int& x, int& y, int& w, int& h) const {
		if (!m_impl->m_hwnd) return false;

		const HWND ref = m_impl->m_hostHwnd ? m_impl->m_hostHwnd : m_impl->m_hwnd;
		const HMONITOR mon = MonitorFromWindow(ref, MONITOR_DEFAULTTONEAREST);
		MONITORINFO mi{ sizeof(mi) };
		if (!GetMonitorInfoW(mon, &mi)) return false;

		x = mi.rcWork.left;
		y = mi.rcWork.top;
		w = mi.rcWork.right - mi.rcWork.left;
		h = mi.rcWork.bottom - mi.rcWork.top;
		return (w > 0 && h > 0);
	}

	void Win32Window::ClientOriginToScreen(int& x, int& y) const {
		if (!m_impl->m_hwnd) return;
		POINT pt{ 0, 0 };
		if (ClientToScreen(m_impl->m_hwnd, &pt)) { x = pt.x; y = pt.y; }
	}

	void Win32Window::GetFrameInsets(int& left, int& top, int& right, int& bottom) const {
		left = top = right = bottom = 0;
		if (!m_impl->m_hwnd) return;
		RECT rc{ 0, 0, 0, 0 };
		if (!AdjustWindowRectEx(&rc,
			static_cast<DWORD>(GetWindowLongPtrW(m_impl->m_hwnd, GWL_STYLE)),
			FALSE,
			static_cast<DWORD>(GetWindowLongPtrW(m_impl->m_hwnd, GWL_EXSTYLE))))
			return;
		left   = -rc.left;
		top    = -rc.top;
		right  = rc.right;
		bottom = rc.bottom;
	}

	void Win32Window::SetPosition(int x, int y) {
		if (!m_impl->m_hwnd) return;
		SetWindowPos(m_impl->m_hwnd, nullptr, x, y, 0, 0,
			SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
	}

	void Win32Window::CenterOnScreen() {
		if (!m_impl->m_hwnd) return;

		if (m_impl->m_isChild) {
			const HWND parent = m_impl->m_hostHwnd;
			if (!parent) return;
			RECT pr{};
			RECT wr{};
			if (!GetClientRect(parent, &pr) || !GetWindowRect(m_impl->m_hwnd, &wr)) return;
			const int cw = pr.right - pr.left;
			const int ch = pr.bottom - pr.top;
			const int w  = wr.right - wr.left;
			const int h  = wr.bottom - wr.top;
			const int x = std::max(0, (cw - w) / 2);
			const int y = std::max(0, (ch - h) / 2);
			SetWindowPos(m_impl->m_hwnd, nullptr, x, y, 0, 0,
				SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
			return;
		}

		if (m_impl->m_isOwned) {
			const HWND owner = m_impl->m_hostHwnd;
			if (!owner) return;

			RECT oc{}, mc{}, wr{};
			if (!GetClientRect(owner, &oc) || !GetClientRect(m_impl->m_hwnd, &mc)
				|| !GetWindowRect(m_impl->m_hwnd, &wr)) return;

			const int ow = oc.right - oc.left;
			const int oh = oc.bottom - oc.top;
			const int cw = mc.right - mc.left;
			const int chh = mc.bottom - mc.top;

			POINT pt{ std::max(0, (ow - cw) / 2), std::max(0, (oh - chh) / 2) };
			ClientToScreen(owner, &pt);

			POINT cur{ 0, 0 };
			ClientToScreen(m_impl->m_hwnd, &cur);
			const int frameX = cur.x - wr.left;
			const int frameY = cur.y - wr.top;

			if (IsIconic(m_impl->m_hwnd) || IsZoomed(m_impl->m_hwnd)) return;
			SetWindowPos(m_impl->m_hwnd, nullptr, pt.x - frameX, pt.y - frameY, 0, 0,
				SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
			return;
		}

		if (IsIconic(m_impl->m_hwnd) || IsZoomed(m_impl->m_hwnd)) return;

		const HMONITOR mon = MonitorFromWindow(m_impl->m_hwnd, MONITOR_DEFAULTTONEAREST);
		MONITORINFO mi{ sizeof(mi) };
		RECT wr{};
		if (!GetMonitorInfoW(mon, &mi) || !GetWindowRect(m_impl->m_hwnd, &wr)) return;

		const int w = wr.right - wr.left;
		const int h = wr.bottom - wr.top;
		const int x = mi.rcWork.left + ((mi.rcWork.right - mi.rcWork.left) - w) / 2;
		const int y = mi.rcWork.top + ((mi.rcWork.bottom - mi.rcWork.top) - h) / 2;
		SetWindowPos(m_impl->m_hwnd, nullptr, x, y, 0, 0,
			SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
	}

	int Win32Window::Dpi() const {
		if (!m_impl->m_hwnd) return 0;
		using GetDpiForWindow_t = UINT(WINAPI*)(HWND);
		static GetDpiForWindow_t fn = []() -> GetDpiForWindow_t {
			if (HMODULE u32 = LoadLibraryW(L"user32.dll"))
				return reinterpret_cast<GetDpiForWindow_t>(
					GetProcAddress(u32, "GetDpiForWindow"));
			return nullptr;
			}();
		if (fn) return static_cast<int>(fn(m_impl->m_hwnd));

		const HDC dc = GetDC(m_impl->m_hwnd);
		if (!dc) return 96;
		const int dpi = GetDeviceCaps(dc, LOGPIXELSY);
		ReleaseDC(m_impl->m_hwnd, dc);
		return dpi > 0 ? dpi : 96;
	}

	float Win32Window::ScaleFactor() const {
		const int dpi = Dpi();
		return dpi > 0 ? static_cast<float>(dpi) / 96.0f : 1.0f;
	}

	void Win32Window::SetTitle(const std::string& utf8) {
		if (!m_impl->m_hwnd) return;
		const std::wstring w = Utf8ToWide(utf8);
		SetWindowTextW(m_impl->m_hwnd, w.c_str());
	}

	bool Win32Window::IsClosed() const { return m_impl->m_hwnd == nullptr; }
	bool Win32Window::IsActive() const { return m_impl->m_active; }

	void Win32Window::RequestRedraw() {
		if (!m_impl->m_hwnd) return;
		InvalidateRect(m_impl->m_hwnd, nullptr, FALSE);
	}

	void Win32Window::RequestWakeup(int afterMs) {
		if (!m_impl->m_hwnd) return;

		const unsigned long long now = static_cast<unsigned long long>(
			std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now().time_since_epoch()).count());
		const unsigned long long delay = afterMs > 0 ? static_cast<unsigned long long>(afterMs) : 1;
		const unsigned long long deadline = now + delay;

		if (m_impl->m_wakeDeadlineMs != 0 && deadline >= m_impl->m_wakeDeadlineMs)
			return;

		m_impl->m_wakeDeadlineMs = deadline;
		SetTimer(m_impl->m_hwnd, kWakeTimerId, static_cast<UINT>(delay), nullptr);
	}

	void Win32Window::CancelWakeup() {
		if (!m_impl->m_hwnd) return;
		m_impl->m_wakeDeadlineMs = 0;
		KillTimer(m_impl->m_hwnd, kWakeTimerId);
	}

	void Win32Window::SetTimerPrecision(bool on) {
		if (on) timeBeginPeriod(1);
		else    timeEndPeriod(1);
	}

	void Win32Window::PostClose() {
		if (m_impl->m_hwnd)
			PostMessageW(m_impl->m_hwnd, WM_CLOSE, 0, 0);
	}

	void Win32Window::RequestQuit(int exitCode) {
		PostQuitMessage(exitCode);
	}

	void Win32Window::StartWindowDrag() {
		const HWND hwnd = m_impl->m_hwnd;
		if (!hwnd) return;
		if (IsIconic(hwnd) || IsZoomed(hwnd)) return;
		if (m_impl->m_isChild) return;
		if (GetCapture() == hwnd) ReleaseCapture();

		POINT pt{};
		if (!GetCursorPos(&pt)) return;
		SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, MAKELPARAM(pt.x, pt.y));
	}

	bool Win32Window::IsMouseButtonDown(MouseButton b) const {
		int vk = 0;
		switch (b) {
		case MouseButton::Left:   vk = VK_LBUTTON;  break;
		case MouseButton::Right:  vk = VK_RBUTTON;  break;
		case MouseButton::Middle: vk = VK_MBUTTON;  break;
		case MouseButton::X1:     vk = VK_XBUTTON1; break;
		case MouseButton::X2:     vk = VK_XBUTTON2; break;
		default: return false;
		}
		return (GetAsyncKeyState(vk) & 0x8000) != 0;
	}

	void Win32Window::SetAcceptFileDrop(bool on) {
		if (!m_impl) return;
		if (m_impl->m_acceptDrop == on) return;
		m_impl->m_acceptDrop = on;
		m_impl->ApplyDropTarget();
	}

	void Win32Window::PostWake() {
		if (m_impl && m_impl->m_hwnd)
			::PostMessageW(m_impl->m_hwnd, kWakePostedMsg, 0, 0);
	}

	bool Win32Window::ShowFileDialog(const FileDialogDesc& desc, std::vector<std::string>& out) {
		out.clear();
		if (!m_impl) return false;

		bool scopedOle = false;
		if (!m_impl->m_oleReady) {
			const HRESULT hr = ::OleInitialize(nullptr);
			if (FAILED(hr)) {
				std::cerr << "[Zekit/win32] 文件对话框不可用：OLE 初始化失败 HRESULT=0x"
					<< std::hex << static_cast<unsigned long>(hr) << std::dec << "\n";
				return false;
			}
			scopedOle = true;
		}
		struct OleScope {
			bool own;
			~OleScope() { if (own) ::OleUninitialize(); }
		} oleScope{ scopedOle };

		const bool saving = desc.mode == FileDialogDesc::Mode::Save;
		IFileDialog* dlg = nullptr;
		HRESULT hr = ::CoCreateInstance(saving ? CLSID_FileSaveDialog : CLSID_FileOpenDialog,
			nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg));
		if (FAILED(hr) || !dlg) {
			std::cerr << "[Zekit/win32] 文件对话框不可用：CoCreateInstance 失败 HRESULT=0x"
				<< std::hex << static_cast<unsigned long>(hr) << std::dec << "\n";
			return false;
		}
		struct ComRelease {
			IUnknown* p;
			~ComRelease() { if (p) p->Release(); }
		} dlgGuard{ dlg };

		DWORD opts = 0;
		if (SUCCEEDED(dlg->GetOptions(&opts))) {
			opts |= FOS_FORCEFILESYSTEM;
			switch (desc.mode) {
			case FileDialogDesc::Mode::Open:
				opts |= FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST;
				if (desc.allowMultiple) opts |= FOS_ALLOWMULTISELECT;
				break;
			case FileDialogDesc::Mode::Save:
				opts |= FOS_PATHMUSTEXIST | FOS_NOREADONLYRETURN;
				if (desc.overwritePrompt) opts |= FOS_OVERWRITEPROMPT;
				else                      opts &= ~FOS_OVERWRITEPROMPT;
				break;
			case FileDialogDesc::Mode::PickFolder:
				opts |= FOS_PICKFOLDERS | FOS_PATHMUSTEXIST;
				opts &= ~FOS_FILEMUSTEXIST;
				break;
			}
			dlg->SetOptions(opts);
		}

		if (!desc.title.empty()) dlg->SetTitle(Utf8ToWide(desc.title).c_str());

		std::vector<std::wstring> labelStore;
		std::vector<std::wstring> patternStore;
		std::vector<COMDLG_FILTERSPEC> specs;
		const size_t n = std::min(desc.filterLabels.size(), desc.filterPatterns.size());
		labelStore.reserve(n + 1);
		patternStore.reserve(n + 1);
		specs.reserve(n + 1);
		for (size_t i = 0; i < n; ++i) {
			if (desc.filterPatterns[i].empty()) continue;
			labelStore.push_back(Utf8ToWide(desc.filterLabels[i]));
			patternStore.push_back(Utf8ToWide(desc.filterPatterns[i]));
		}
		if (!labelStore.empty()) {
			labelStore.push_back(L"所有文件 (*.*)");
			patternStore.push_back(L"*.*");
			for (size_t i = 0; i < labelStore.size(); ++i)
				specs.push_back(COMDLG_FILTERSPEC{ labelStore[i].c_str(), patternStore[i].c_str() });
			dlg->SetFileTypes(static_cast<UINT>(specs.size()), specs.data());
			if (desc.filterIndex > 0 && static_cast<size_t>(desc.filterIndex) < specs.size())
				dlg->SetFileTypeIndex(static_cast<UINT>(desc.filterIndex) + 1);
		}

		if (!desc.initialDir.empty()) {
			IShellItem* dir = nullptr;
			if (SUCCEEDED(::SHCreateItemFromParsingName(
				Utf8ToWide(desc.initialDir).c_str(), nullptr, IID_PPV_ARGS(&dir))) && dir) {
				dlg->SetFolder(dir);
				dir->Release();
			}
		}
		if (!desc.defaultName.empty())
			dlg->SetFileName(Utf8ToWide(desc.defaultName).c_str());
		if (saving) {
			IFileSaveDialog* sd = nullptr;
			if (SUCCEEDED(dlg->QueryInterface(IID_PPV_ARGS(&sd))) && sd) {
				if (!desc.defaultExt.empty())
					sd->SetDefaultExtension(Utf8ToWide(desc.defaultExt).c_str());
				sd->Release();
			}
		}

		HWND owner = m_impl->m_hwnd;
		if (owner) {
			if (HWND root = ::GetAncestor(owner, GA_ROOTOWNER)) owner = root;
		}
		const HRESULT shown = dlg->Show(owner);
		if (FAILED(shown)) return false;

		auto append = [](IShellItem* item, std::vector<std::string>& dst) {
			if (!item) return;
			PWSTR raw = nullptr;
			if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &raw)) && raw) {
				dst.push_back(WideToUtf8(raw));
				::CoTaskMemFree(raw);
			}
		};

		if (desc.allowMultiple && desc.mode == FileDialogDesc::Mode::Open) {
			IFileOpenDialog* open = nullptr;
			if (SUCCEEDED(dlg->QueryInterface(IID_PPV_ARGS(&open))) && open) {
				IShellItemArray* arr = nullptr;
				if (SUCCEEDED(open->GetResults(&arr)) && arr) {
					DWORD count = 0;
					if (SUCCEEDED(arr->GetCount(&count))) {
						for (DWORD i = 0; i < count; ++i) {
							IShellItem* item = nullptr;
							if (SUCCEEDED(arr->GetItemAt(i, &item)) && item) {
								append(item, out);
								item->Release();
							}
						}
					}
					arr->Release();
				}
				open->Release();
			}
		} else {
			IShellItem* item = nullptr;
			if (SUCCEEDED(dlg->GetResult(&item)) && item) {
				append(item, out);
				item->Release();
			}
		}

		return !out.empty();
	}

	void* Win32Window::NativeHandle() const { return m_impl->m_hwnd; }

	bool SystemPrefersDark() { return ReadSystemPrefersDark(); }

	void Win32Window::Minimize() {
		if (!m_impl->m_hwnd) return;
		ShowWindow(m_impl->m_hwnd, SW_MINIMIZE);
	}

	void Win32Window::Maximize() {
		if (!m_impl->m_hwnd) return;
		ShowWindow(m_impl->m_hwnd, SW_MAXIMIZE);
	}

	void Win32Window::Restore() {
		if (!m_impl->m_hwnd) return;
		ShowWindow(m_impl->m_hwnd, SW_RESTORE);
	}

	bool Win32Window::IsMinimized() const {
		return m_impl->m_hwnd && IsIconic(m_impl->m_hwnd);
	}

	bool Win32Window::IsMaximized() const {
		return m_impl->m_hwnd && IsZoomed(m_impl->m_hwnd);
	}

	void Win32Window::SetTopmost(bool on) {
		if (!m_impl->m_hwnd) return;
		SetWindowPos(m_impl->m_hwnd, on ? HWND_TOPMOST : HWND_NOTOPMOST,
			0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	}

	void Win32Window::SetResizable(bool on) {
		if (!m_impl->m_hwnd) return;
		const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(m_impl->m_hwnd, GWL_STYLE));
		const DWORD maxBit = m_impl->m_maxBtnEnabled ? WS_MAXIMIZEBOX : 0u;
		const DWORD want = on ? (style | WS_THICKFRAME | maxBit)
			: (style & ~(WS_THICKFRAME | WS_MAXIMIZEBOX));
		if (want == style) return;
		SetWindowLongPtrW(m_impl->m_hwnd, GWL_STYLE, static_cast<LONG_PTR>(want));
		SetWindowPos(m_impl->m_hwnd, nullptr, 0, 0, 0, 0,
			SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
	}

	void Win32Window::SetBorderless(bool on) {
		if (!m_impl->m_hwnd) return;
		if (on == m_impl->m_borderless) return;
		if (on) {
			m_impl->m_borderlessSavedStyle =
				static_cast<DWORD>(GetWindowLongPtrW(m_impl->m_hwnd, GWL_STYLE));
			m_impl->m_borderlessSavedExStyle =
				static_cast<DWORD>(GetWindowLongPtrW(m_impl->m_hwnd, GWL_EXSTYLE));
			SetWindowLongPtrW(m_impl->m_hwnd, GWL_STYLE,
				static_cast<LONG_PTR>((m_impl->m_borderlessSavedStyle & ~WS_OVERLAPPEDWINDOW)
					| WS_POPUP));
			const DWORD addAppWindow = m_impl->m_popup ? 0u : WS_EX_APPWINDOW;
			SetWindowLongPtrW(m_impl->m_hwnd, GWL_EXSTYLE,
				static_cast<LONG_PTR>(m_impl->m_borderlessSavedExStyle | addAppWindow));
		} else {
			SetWindowLongPtrW(m_impl->m_hwnd, GWL_STYLE,
				static_cast<LONG_PTR>(m_impl->m_borderlessSavedStyle));
			SetWindowLongPtrW(m_impl->m_hwnd, GWL_EXSTYLE,
				static_cast<LONG_PTR>(m_impl->m_borderlessSavedExStyle));
		}
		m_impl->m_borderless = on;
		SetWindowPos(m_impl->m_hwnd, nullptr, 0, 0, 0, 0,
			SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
		if (!on) m_impl->ApplyTitleBarImpl();
	}

	void Win32Window::SetBorderlessResizable(bool on) {
		if (!m_impl->m_hwnd) return;
		m_impl->m_borderlessResize = on;
	}

	bool Win32Window::IsBorderlessResizable() const {
		return m_impl->m_borderlessResize;
	}

	void Win32Window::SetRoundedCorners(bool on) {
		if (!m_impl->m_hwnd) return;
		ApplyCornerPreference(m_impl->m_hwnd, on ? 2 : 1);
	}

	bool Win32Window::IsRoundedCorners() const {
		return QueryCornerPreference(m_impl->m_hwnd) == 2;
	}

	void Win32Window::SetFullscreen(bool on) {
		if (!m_impl->m_hwnd) return;
		if (IsIconic(m_impl->m_hwnd)) return;
		if (on == m_impl->m_fullscreen) return;

		if (on) {
			const HMONITOR mon = MonitorFromWindow(m_impl->m_hwnd, MONITOR_DEFAULTTONEAREST);
			MONITORINFO mi{ sizeof(mi) };
			if (!mon || !GetMonitorInfoW(mon, &mi)) return;

			GetWindowRect(m_impl->m_hwnd, &m_impl->m_fullscreenSavedRect);
			m_impl->m_fullscreenSavedStyle =
				static_cast<DWORD>(GetWindowLongPtrW(m_impl->m_hwnd, GWL_STYLE));

			SetWindowLongPtrW(m_impl->m_hwnd, GWL_STYLE,
				static_cast<LONG_PTR>(m_impl->m_fullscreenSavedStyle & ~WS_OVERLAPPEDWINDOW));
			SetWindowPos(m_impl->m_hwnd, HWND_TOP,
				mi.rcMonitor.left, mi.rcMonitor.top,
				mi.rcMonitor.right - mi.rcMonitor.left,
				mi.rcMonitor.bottom - mi.rcMonitor.top,
				SWP_NOZORDER | SWP_FRAMECHANGED | SWP_NOACTIVATE);
		} else {
			SetWindowLongPtrW(m_impl->m_hwnd, GWL_STYLE,
				static_cast<LONG_PTR>(m_impl->m_fullscreenSavedStyle));
			const RECT& r = m_impl->m_fullscreenSavedRect;
			SetWindowPos(m_impl->m_hwnd, nullptr,
				r.left, r.top, r.right - r.left, r.bottom - r.top,
				SWP_NOZORDER | SWP_FRAMECHANGED | SWP_NOACTIVATE);
		}
		m_impl->m_fullscreen = on;
		if (!on) m_impl->ApplyTitleBarImpl();
	}

	void Win32Window::ApplyTitleBar(bool titleBarVisible,
		bool minBtn, bool maxBtn, bool closeBtn) {
		m_impl->m_titleBarVisible = titleBarVisible;
		m_impl->m_minBtnEnabled = minBtn;
		m_impl->m_maxBtnEnabled = maxBtn;
		m_impl->m_closeBtnEnabled = closeBtn;
		m_impl->ApplyTitleBarImpl();
	}

	void Win32Window::Impl::ApplyTitleBarImpl() {
		if (!m_hwnd) return;
		if (m_borderless || m_fullscreen) return;

		const LONG_PTR cur = GetWindowLongPtrW(m_hwnd, GWL_STYLE);
		LONG_PTR want = cur;

		if (m_titleBarVisible) want |= (WS_CAPTION | WS_SYSMENU);
		else                   want &= ~(WS_CAPTION | WS_SYSMENU);

		if (m_titleBarVisible && m_minBtnEnabled) want |= WS_MINIMIZEBOX;
		else                                      want &= ~WS_MINIMIZEBOX;
		if (m_titleBarVisible && m_maxBtnEnabled) want |= WS_MAXIMIZEBOX;
		else                                      want &= ~WS_MAXIMIZEBOX;

		if (want != cur) {
			SetWindowLongPtrW(m_hwnd, GWL_STYLE, want);
			SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
				SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
		}
	}

	void Win32Window::SetOpacity(float alpha) {
		if (!m_impl->m_hwnd) return;
		alpha = std::clamp(alpha, 0.0f, 1.0f);
		m_impl->m_winAlpha = alpha;

		if (m_impl->m_noRedirection) return;

		const auto a = static_cast<BYTE>(alpha * 255.0f + 0.5f);
		const DWORD ex = static_cast<DWORD>(GetWindowLongPtrW(m_impl->m_hwnd, GWL_EXSTYLE));
		const bool layeredNeeded = m_impl->m_clickThrough;
		if (a >= 255 && !layeredNeeded) {
			if (ex & WS_EX_LAYERED)
				SetWindowLongPtrW(m_impl->m_hwnd, GWL_EXSTYLE,
					static_cast<LONG_PTR>(ex & ~WS_EX_LAYERED));
			return;
		}
		SetWindowLongPtrW(m_impl->m_hwnd, GWL_EXSTYLE,
			static_cast<LONG_PTR>(ex | WS_EX_LAYERED));
		SetLayeredWindowAttributes(m_impl->m_hwnd, 0, a, LWA_ALPHA);
	}

	void Win32Window::SetPerPixelTransparent(bool on) {
		if (!m_impl->m_hwnd) return;
		if (on == m_impl->m_noRedirection) return;

		DWORD ex = static_cast<DWORD>(GetWindowLongPtrW(m_impl->m_hwnd, GWL_EXSTYLE));
		if (on) {
			ex &= ~WS_EX_LAYERED;
			ex |= WS_EX_NOREDIRECTIONBITMAP;
		}
		else {
			ex &= ~WS_EX_NOREDIRECTIONBITMAP;
			if (m_impl->m_clickThrough || m_impl->m_winAlpha < 1.0f)
				ex |= WS_EX_LAYERED;
		}
		SetWindowLongPtrW(m_impl->m_hwnd, GWL_EXSTYLE, static_cast<LONG_PTR>(ex));
		m_impl->m_noRedirection = on;

		if (on) {
			DisableSystemFrame(m_impl->m_hwnd);
		}
		else {
			ApplyCornerPreference(m_impl->m_hwnd, 2 );
			if (ex & WS_EX_LAYERED)
				SetLayeredWindowAttributes(m_impl->m_hwnd, 0,
					static_cast<BYTE>(m_impl->m_winAlpha * 255.0f + 0.5f), LWA_ALPHA);
		}

		SetWindowPos(m_impl->m_hwnd, nullptr, 0, 0, 0, 0,
			SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
	}

	void Win32Window::SetClickThrough(bool on) {
		if (!m_impl->m_hwnd) return;
		if (on == m_impl->m_clickThrough) return;
		m_impl->m_clickThrough = on;

		DWORD ex = static_cast<DWORD>(GetWindowLongPtrW(m_impl->m_hwnd, GWL_EXSTYLE));
		if (on) {
			if (m_impl->m_noRedirection) {
				ex |= WS_EX_TRANSPARENT;
				SetWindowLongPtrW(m_impl->m_hwnd, GWL_EXSTYLE, static_cast<LONG_PTR>(ex));
			}
			else {
				ex |= (WS_EX_LAYERED | WS_EX_TRANSPARENT);
				SetWindowLongPtrW(m_impl->m_hwnd, GWL_EXSTYLE, static_cast<LONG_PTR>(ex));
				SetLayeredWindowAttributes(m_impl->m_hwnd, 0,
					static_cast<BYTE>(m_impl->m_winAlpha * 255.0f + 0.5f), LWA_ALPHA);
			}
		} else {
			ex &= ~WS_EX_TRANSPARENT;
			SetWindowLongPtrW(m_impl->m_hwnd, GWL_EXSTYLE, static_cast<LONG_PTR>(ex));
			SetOpacity(m_impl->m_winAlpha);
		}
		SetWindowPos(m_impl->m_hwnd, nullptr, 0, 0, 0, 0,
			SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
	}

	void Win32Window::SetPositionRelativeToHost(int x, int y) {
		if (!m_impl->m_hwnd) return;
		if (m_impl->m_hostHwnd && !m_impl->m_isChild) {
			POINT pt{ x, y };
			ClientToScreen(m_impl->m_hostHwnd, &pt);
			x = pt.x;
			y = pt.y;
		}
		SetWindowPos(m_impl->m_hwnd, nullptr, x, y, 0, 0,
			SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
	}

	bool Win32Window::ReleaseFromHost() {
		if (!m_impl->m_hwnd) return false;

		if (m_impl->m_isChild) return false;
		if (m_impl->m_popup) return false;
		if (!m_impl->m_hostHwnd) return false;

		const HWND oldOwner = m_impl->m_hostHwnd;

		if (::GetWindowLongPtrW(m_impl->m_hwnd, GWLP_HWNDPARENT)
				== reinterpret_cast<LONG_PTR>(oldOwner)) {
			::SetWindowLongPtrW(m_impl->m_hwnd, GWLP_HWNDPARENT, 0);
		}

		m_impl->m_hostHwnd = nullptr;
		m_impl->m_isOwned = false;

		::ShowWindow(m_impl->m_hwnd, SW_SHOWDEFAULT);
		::SetForegroundWindow(m_impl->m_hwnd);
		return true;
	}

	void Win32Window::SetDarkTitleBar(bool dark) {
		if (!m_impl->m_hwnd) return;
		m_impl->m_dark = dark;
		SetTitleBarDark(m_impl->m_hwnd, dark);
	}

	void Win32Window::SetEnabled(bool on) {
		if (!m_impl->m_hwnd) return;

		const BOOL want = on ? TRUE : FALSE;
		if (::IsWindowEnabled(m_impl->m_hwnd) == want) return;

		::EnableWindow(m_impl->m_hwnd, want);

		if (on && ::IsWindowVisible(m_impl->m_hwnd) && ::GetActiveWindow() != m_impl->m_hwnd)
			::SetActiveWindow(m_impl->m_hwnd);
	}

	namespace {
		bool EndsWithIgnoreCase(const std::wstring& s, const wchar_t* suffix) {
			const std::wstring_view suf(suffix);
			if (s.size() < suf.size()) return false;
			return std::equal(suf.rbegin(), suf.rend(), s.rbegin(),
				[](wchar_t a, wchar_t b) { return towlower(a) == towlower(b); });
		}

		HICON CreateIconFromRGBA(const unsigned char* rgba, int w, int h) {
			if (!rgba || w <= 0 || h <= 0) return nullptr;

			BITMAPV5HEADER bi{};
			bi.bV5Size = sizeof(bi);
			bi.bV5Width = w;
			bi.bV5Height = -h;
			bi.bV5Planes = 1;
			bi.bV5BitCount = 32;
			bi.bV5Compression = BI_BITFIELDS;
			bi.bV5RedMask = 0x00FF0000u;
			bi.bV5GreenMask = 0x0000FF00u;
			bi.bV5BlueMask = 0x000000FFu;
			bi.bV5AlphaMask = 0xFF000000u;

			void* bits = nullptr;
			HDC hdc = GetDC(nullptr);
			HBITMAP color = CreateDIBSection(hdc, reinterpret_cast<const BITMAPINFO*>(&bi),
				DIB_RGB_COLORS, &bits, nullptr, 0);
			ReleaseDC(nullptr, hdc);
			if (!color || !bits) {
				if (color) DeleteObject(color);
				return nullptr;
			}

			auto* dst = static_cast<unsigned char*>(bits);
			for (int i = 0; i < w * h; ++i) {
				const unsigned char a = rgba[i * 4 + 3];
				dst[i * 4 + 0] = static_cast<unsigned char>(rgba[i * 4 + 2] * a / 255);
				dst[i * 4 + 1] = static_cast<unsigned char>(rgba[i * 4 + 1] * a / 255);
				dst[i * 4 + 2] = static_cast<unsigned char>(rgba[i * 4 + 0] * a / 255);
				dst[i * 4 + 3] = a;
			}

			const int maskRowBytes = ((w + 15) / 16) * 2;
			std::vector<unsigned char> maskBits(static_cast<size_t>(maskRowBytes) * h, 0);
			HBITMAP mask = CreateBitmap(w, h, 1, 1, maskBits.data());
			if (!mask) { DeleteObject(color); return nullptr; }

			ICONINFO ii{};
			ii.fIcon = TRUE;
			ii.hbmMask = mask;
			ii.hbmColor = color;
			HICON icon = CreateIconIndirect(&ii);
			DeleteObject(color);
			DeleteObject(mask);
			return icon;
		}
	}

	void Win32Window::SetIconFromFile(const std::string& utf8Path) {
		if (!m_impl->m_hwnd || utf8Path.empty()) return;

		std::wstring path = Utf8ToWide(utf8Path);
		std::replace(path.begin(), path.end(), L'/', L'\\');

		HICON hBig = nullptr;
		HICON hSmall = nullptr;

		const bool isIco = EndsWithIgnoreCase(path, L".ico");
		if (isIco) {
			hBig = static_cast<HICON>(LoadImageW(nullptr, path.c_str(), IMAGE_ICON,
				GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_LOADFROMFILE));
			hSmall = static_cast<HICON>(LoadImageW(nullptr, path.c_str(), IMAGE_ICON,
				GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_LOADFROMFILE));
			if (!hBig && hSmall) hBig = hSmall;
			if (hBig && !hSmall) hSmall = hBig;
			if (!hBig) {
				OutputDebugStringW(L"[Zekit] SetIconFromFile: .ico 加载失败，已放弃\n");
				return;
			}
		}

		if (!hBig) {
			const std::string pathA = utf8Path;
			int w = 0, h = 0, ch = 0;
			unsigned char* data = SOIL_load_image(pathA.c_str(), &w, &h, &ch, SOIL_LOAD_RGBA);
			if (data) {
				hBig = CreateIconFromRGBA(data, w, h);
				SOIL_free_image_data(data);
			}
			if (hBig) hSmall = hBig;
		}

		if (!hBig) {
			OutputDebugStringA("[Zekit] SetWindowIcon failed: unsupported file or missing\n");
			return;
		}
		m_impl->SetIconsCore(hBig, hSmall);
	}

	void Win32Window::SetIconFromHandle(void* hIcon) {
		if (!m_impl->m_hwnd) return;
		const HICON icon = static_cast<HICON>(hIcon);
		SendMessageW(m_impl->m_hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon));
		SendMessageW(m_impl->m_hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon));
	}

	void Win32Window::SetIconFromResource(int resId) {
		if (!m_impl->m_hwnd) return;
		const HINSTANCE hInst = GetModuleHandleW(nullptr);
		HICON hBig = static_cast<HICON>(LoadImageW(hInst, MAKEINTRESOURCEW(resId),
			IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0));
		HICON hSmall = static_cast<HICON>(LoadImageW(hInst, MAKEINTRESOURCEW(resId),
			IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));
		if (!hBig && !hSmall) {
			OutputDebugStringA("[Zekit] SetWindowIconFromResource failed: resource not found\n");
			return;
		}
		if (!hBig)   hBig = hSmall;
		if (!hSmall) hSmall = hBig;
		m_impl->SetIconsCore(hBig, hSmall);
	}

	void Win32Window::Impl::SetIconsCore(HICON hBig, HICON hSmall) {
		HWND hwnd = m_hwnd;
		if (!hwnd) return;

		if (hSmall == hBig) {
			hSmall = nullptr;
			if (m_hIconSmall && m_hIconSmall != m_hIconBig) {
				DestroyIcon(m_hIconSmall);
				m_hIconSmall = nullptr;
			}
		}
		if (hBig) {
			if (m_hIconBig) DestroyIcon(m_hIconBig);
			m_hIconBig = hBig;
			SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(hBig));
		}
		if (hSmall) {
			if (m_hIconSmall && m_hIconSmall != m_hIconBig)
				DestroyIcon(m_hIconSmall);
			m_hIconSmall = hSmall;
			SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(hSmall));
		}
	}

	namespace {
		bool OpenClipboardRetry() {
			for (int i = 0; i < 8; ++i) {
				if (OpenClipboard(nullptr)) return true;
				Sleep(1);
			}
			return false;
		}

		std::string Utf16ToUtf8(const wchar_t* s, int len) {
			if (!s || len <= 0) return std::string();
			const int n = WideCharToMultiByte(CP_UTF8, 0, s, len, nullptr, 0, nullptr, nullptr);
			std::string out(n > 0 ? static_cast<std::size_t>(n) : 0u, '\0');
			if (n > 0)
				WideCharToMultiByte(CP_UTF8, 0, s, len, out.data(), n, nullptr, nullptr);
			return out;
		}
	}

	void Win32Window::SetMouseCursor(Kit::Cursor c) {
		LPCWSTR id = IDC_ARROW;
		switch (c) {
		case Kit::Cursor::Arrow:      id = IDC_ARROW;   break;
		case Kit::Cursor::Pointer:    id = IDC_HAND;    break;
		case Kit::Cursor::Text:       id = IDC_IBEAM;   break;
		case Kit::Cursor::NotAllowed: id = IDC_NO;      break;
		case Kit::Cursor::Wait:       id = IDC_WAIT;    break;
		case Kit::Cursor::Grab:       id = IDC_SIZEALL; break;
		case Kit::Cursor::Grabbing:   id = IDC_SIZEALL; break;
		case Kit::Cursor::SizeWE:     id = IDC_SIZEWE;  break;
		case Kit::Cursor::SizeNS:     id = IDC_SIZENS;  break;
		}
		HCURSOR hc = ::LoadCursorW(nullptr, id);
		if (m_impl) m_impl->m_cursor = hc;
		::SetCursor(hc);
	}

	std::string Win32Window::GetClipboardText() {
		if (!OpenClipboardRetry()) return std::string();

		std::string out;
		if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
			if (const wchar_t* p = static_cast<const wchar_t*>(GlobalLock(h))) {
				out = Utf16ToUtf8(p, lstrlenW(p));
				GlobalUnlock(h);
			}
		}
		CloseClipboard();
		return out;
	}

	void Win32Window::SetClipboardText(const std::string& utf8) {
		if (!OpenClipboardRetry()) return;

		EmptyClipboard();

		const std::wstring w = Utf8ToWide(utf8);
		if (HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (w.size() + 1) * sizeof(wchar_t))) {
			if (void* p = GlobalLock(h)) {
				memcpy(p, w.c_str(), (w.size() + 1) * sizeof(wchar_t));
				GlobalUnlock(h);
				if (!SetClipboardData(CF_UNICODETEXT, h)) {
					GlobalFree(h);
					if (Plat::TraceClipboard())
						std::cerr << "[Zekit/clip] SetClipboardData 失败（剪贴板已被别的程序占住？）"
						<< "错误码 " << GetLastError() << "\n";
				}
			} else {
				GlobalFree(h);
			}
		}
		CloseClipboard();
	}

	namespace {

		void FillGeometry(const MONITORINFO& mi, bool primary, DisplayGeometry& g) {
			g.x      = mi.rcMonitor.left;
			g.y      = mi.rcMonitor.top;
			g.width  = mi.rcMonitor.right - mi.rcMonitor.left;
			g.height = mi.rcMonitor.bottom - mi.rcMonitor.top;

			g.workX      = mi.rcWork.left;
			g.workY      = mi.rcWork.top;
			g.workWidth  = mi.rcWork.right - mi.rcWork.left;
			g.workHeight = mi.rcWork.bottom - mi.rcWork.top;

			g.primary = primary;
		}

		struct MonitorSink {
			DisplayGeometry* out;
			int maxCount;
			int count;
		};

		BOOL CALLBACK CollectMonitorProc(HMONITOR mon, HDC, LPRECT, LPARAM data) {
			auto* sink = reinterpret_cast<MonitorSink*>(data);
			if (!sink) return FALSE;

			if (sink->out && sink->count < sink->maxCount) {
				MONITORINFO mi{ sizeof(mi) };
				if (GetMonitorInfoW(mon, &mi)) {
					FillGeometry(mi, (mi.dwFlags & MONITORINFOF_PRIMARY) != 0,
						sink->out[sink->count]);
				}
			}
			++sink->count;
			return TRUE;
		}

	}

	int QueryDisplays(DisplayGeometry* out, int maxCount) {
		MonitorSink sink{ out, (out ? maxCount : 0), 0 };
		if (!EnumDisplayMonitors(nullptr, nullptr, &CollectMonitorProc,
			reinterpret_cast<LPARAM>(&sink))) {
			return 0;
		}
		return sink.count;
	}

	bool QueryPrimaryDisplay(DisplayGeometry& out) {
		const POINT origin{ 0, 0 };
		const HMONITOR mon = MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
		if (!mon) return false;

		MONITORINFO mi{ sizeof(mi) };
		if (!GetMonitorInfoW(mon, &mi)) return false;

		FillGeometry(mi, true, out);
		return out.workWidth > 0 && out.workHeight > 0;
	}

}
