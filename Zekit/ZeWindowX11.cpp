
#include "pch.h"

#include "ZeWindowX11.h"
#include "include/ZeEmbeddedRes.h"

#if defined(__linux__) && !defined(__OHOS__)

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <X11/XF86keysym.h>
#include <X11/cursorfont.h>

namespace { enum { kXFocusIn = FocusIn, kXFocusOut = FocusOut }; }
#undef FocusIn
#undef FocusOut
#undef None

#include <SOIL2/SOIL2.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <climits>
#include <clocale>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <dlfcn.h>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include <sys/select.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

	struct X11State {
		Display* dpy = nullptr;
		int      refs = 0;

		bool     autoRepeatSupported = false;
		XIM      im = nullptr;
		bool     imTried = false;

		bool     quitPending = false;
		int      exitCode = 0;
		Plat::IWindowHost* quitHost = nullptr;

		std::unordered_map<::Window, void*> windows;

		Atom wmProtocols{}, wmDeleteWindow{}, iccWmState{}, utf8String{};
		Atom netWmName{}, netWmIcon{}, netWmPid{}, netWmWindowType{},
			netWmWindowTypeNormal{}, netWmState{}, netWmStateFullscreen{},
			netWmStateMaxHorz{}, netWmStateMaxVert{}, netWmStateAbove{},
			netWmStateHidden{}, netWmWindowOpacity{}, netWmWorkarea{},
			netWmStateSkipTaskbar{}, netWmStateSkipPager{}, netWmWindowTypePopupMenu{},
			netWmAllowedActions{}, netWmActionMove{}, netWmActionResize{},
			netWmActionMinimize{}, netWmActionMaxHorz{}, netWmActionMaxVert{},
			netWmActionClose{}, netWmMoveResize{}, netActiveWindow{},
			netSupportingWmCheck{}, motifWmHints{}, gtkThemeVariant{},
			netCurrentDesktop{}, netFrameExtents{};

		Atom clipboard{}, targetsAtom{}, textAtom{}, incrAtom{}, clipboardProp{};

		Atom xdndAware{},
		     xdndEnter{},
		     xdndPosition{},
		     xdndStatus{},
		     xdndLeave{},
		     xdndDrop{},
		     xdndFinished{},
		     xdndSelection{},
		     xdndActionCopy{},
		     textUriList{},
		     xdndProp{};

		Atom     xsSelectionAtom{};
		Atom     xsSettingsAtom{};
		::Window xsWindow = 0;
		Display* xsOnDpy = nullptr;

		std::deque<XEvent> deferred;
	};

	X11State& XS() { static X11State s; return s; }

	Atom Intern(Display* dpy, const char* name) {
		return XInternAtom(dpy, name, False);
	}

	namespace {
		using XsVerdict = int;

		std::string LowerAscii(std::string s) {
			for (char& c : s)
				if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
			return s;
		}

		bool WalkXSettings(const unsigned char* p, std::size_t n,
			XsVerdict& outVerdict, bool& outThemeDark, unsigned int& outCount) {
			outVerdict = 0;
			outThemeDark = false;
			outCount = 0;
			bool sawTheme = false;
			if (!p || n < 12) return false;

			const bool little = (p[0] == 0);
			const auto rd32 = [&](std::size_t off) {
				unsigned int v = 0;
				if (little) for (int i = 3; i >= 0; --i) v = (v << 8) | p[off + i];
				else        for (int i = 0; i < 4; ++i)  v = (v << 8) | p[off + i];
				return v;
				};
			const auto rd16 = [&](std::size_t off) {
				return little ? (static_cast<unsigned int>(p[off]) | (p[off + 1] << 8))
					: ((static_cast<unsigned int>(p[off]) << 8) | p[off + 1]);
				};

			const unsigned int count = rd32(8);
			std::size_t off = 12;
			for (unsigned int i = 0; i < count; ++i) {
				if (off + 4 > n) return false;
				const unsigned int type = p[off];
				const unsigned int nameLen = rd16(off + 2);
				off += 4;
				if (off + nameLen > n) return false;
				const std::string key = LowerAscii(
					std::string(reinterpret_cast<const char*>(p + off), nameLen));
				off += (nameLen + 3u) & ~std::size_t(3u);
				if (off + 4 > n) return false;
				off += 4;

				std::string sval;
				int ival = 0;
				bool gotInt = false, gotStr = false;
				if (type == 0) {
					if (off + 4 > n) return false;
					ival = static_cast<int>(rd32(off));
					gotInt = true;
					off += 4;
				} else if (type == 1) {
					if (off + 4 > n) return false;
					const unsigned int len = rd32(off);
					off += 4;
					if (off + len > n) return false;
					sval.assign(reinterpret_cast<const char*>(p + off), len);
					gotStr = true;
					off += (len + 3u) & ~std::size_t(3u);
				} else if (type == 2) {
					if (off + 8 > n) return false;
					off += 8;
				} else {
					return false;
				}
				++outCount;

				if (key == "gtk/colorscheme" && gotStr) {
					sawTheme = true;
					const std::string v = LowerAscii(sval);
					if (v.find("dark") != std::string::npos)       outVerdict = +1;
					else if (v.find("light") != std::string::npos) outVerdict = -1;
				} else if (key == "gtk/applicationpreferdarktheme" && gotInt) {
					sawTheme = true;
					outVerdict = (ival != 0) ? +1 : -1;
				}
				else if ((key == "net/themename" || key == "gtk/themename") && gotStr) {
					sawTheme = true;
					if (LowerAscii(sval).find("dark") != std::string::npos)
						outThemeDark = true;
				}
			}

			if (outVerdict == 0 && sawTheme && !outThemeDark)
				outVerdict = -1;
			return true;
		}

		::Window FindXSettingsWindow(Display* dpy) {
			X11State& s = XS();
			if (!dpy || !s.xsSelectionAtom) return 0;

			if (const ::Window owner = XGetSelectionOwner(dpy, s.xsSelectionAtom))
				return owner;

			Atom type = 0;
			int fmt = 0;
			unsigned long nitems = 0, after = 0;
			unsigned char* data = nullptr;
			const int rc = XGetWindowProperty(dpy, DefaultRootWindow(dpy),
				s.xsSelectionAtom, 0, 1, False, XA_WINDOW,
				&type, &fmt, &nitems, &after, &data);
			::Window mgr = 0;
			if (rc == Success && data && fmt == 32 && nitems >= 1)
				mgr = static_cast<::Window>(*reinterpret_cast<const unsigned long*>(data));
			if (data) XFree(data);
			return mgr;
		}

		XsVerdict ReadXSettingsDark(Display* dpy, ::Window mgr) {
			X11State& s = XS();
			if (!dpy) return 0;
			if (!mgr) mgr = FindXSettingsWindow(dpy);
			if (!mgr || !s.xsSettingsAtom) return 0;

			Atom type = 0;
			int fmt = 0;
			unsigned long nitems = 0, after = 0;
			unsigned char* data = nullptr;
			const int rc = XGetWindowProperty(dpy, mgr, s.xsSettingsAtom,
				0, LONG_MAX / 4, False, AnyPropertyType,
				&type, &fmt, &nitems, &after, &data);
			if (rc != Success || !data) {
				if (data) XFree(data);
				if (Plat::TraceTheme())
					std::cerr << "[Zekit/theme] XSETTINGS：窗口=0x" << std::hex << mgr
					<< std::dec << " 读不到属性（设置守护没在跑？）\n";
				return 0;
			}

			XsVerdict verdict = 0;
			bool themeDark = false;
			unsigned int count = 0;
			const bool ok = (fmt == 8) &&
				WalkXSettings(data, nitems, verdict, themeDark, count);
			const XsVerdict out = (verdict != 0) ? verdict : (themeDark ? +1 : 0);
			if (Plat::TraceTheme())
				std::cerr << "[Zekit/theme] XSETTINGS：窗口=0x" << std::hex << mgr << std::dec
				<< " 字节=" << nitems << " 条数=" << count
				<< (ok ? "" : "（形状不合法 → 整份丢弃）")
				<< " → " << (verdict > 0 ? "深色" : verdict < 0 ? "浅色"
					: themeDark ? "仅主题名带 dark（弱信号）→ 深色" : "没表态")
				<< "\n";
			XFree(data);
			return out;
		}

		bool WatchXSettings() {
			X11State& s = XS();
			if (!s.dpy || !s.xsSettingsAtom) return false;

			if (Plat::TraceTheme())
				std::cerr << "[Zekit/theme] WatchXSettings：dpy=" << static_cast<const void*>(s.dpy)
				<< " fd=" << XConnectionNumber(s.dpy)
				<< " root=0x" << std::hex << static_cast<unsigned long>(DefaultRootWindow(s.dpy))
				<< std::dec << " 缓存 xsWindow=0x" << std::hex << s.xsWindow << std::dec << "\n";

			XSelectInput(s.dpy, DefaultRootWindow(s.dpy), PropertyChangeMask);

			if (s.xsWindow && s.xsOnDpy == s.dpy) {
				if (Plat::TraceTheme())
					std::cerr << "[Zekit/theme] WatchXSettings：本连接已订阅过 0x"
					<< std::hex << s.xsWindow << std::dec << "，跳过重复订阅\n";
				return true;
			}
			if (s.xsWindow) {
				if (Plat::TraceTheme())
					std::cerr << "[Zekit/theme] WatchXSettings：缓存窗口 0x" << std::hex << s.xsWindow
					<< std::dec << " 是**上一条连接**上订的 → 在本连接上重新订阅\n";
				s.xsWindow = 0;
				s.xsOnDpy = nullptr;
			}
			const ::Window mgr = FindXSettingsWindow(s.dpy);
			if (!mgr) {
				if (Plat::TraceTheme())
					std::cerr << "[Zekit/theme] 没有 XSETTINGS 管理器"
					"（裸 X / 无桌面设置守护）→ 深浅判读只能靠 GTK_THEME / settings.ini\n";
				return false;
			}
			XSelectInput(s.dpy, mgr, PropertyChangeMask);
			s.xsWindow = mgr;
			s.xsOnDpy = s.dpy;
			if (Plat::TraceTheme()) {
				XSync(s.dpy, False);
				XWindowAttributes wa{};
				if (XGetWindowAttributes(s.dpy, mgr, &wa))
					std::cerr << "[Zekit/theme] 订阅校验：mgr=0x" << std::hex << mgr
					<< " your_event_mask=0x" << static_cast<unsigned long>(wa.your_event_mask)
					<< " 期望含 PropertyChangeMask(0x400000)="
					<< ((wa.your_event_mask & PropertyChangeMask) ? "有" : "**没有**")
					<< std::dec << " map_state=" << static_cast<int>(wa.map_state)
					<< " depth=" << wa.depth << "\n";
				else
					std::cerr << "[Zekit/theme] 订阅校验：XGetWindowAttributes(0x" << std::hex << mgr
					<< std::dec << ") 失败（窗口不存在？）\n";
			}
			if (Plat::TraceTheme())
				std::cerr << "[Zekit/theme] 已订阅 XSETTINGS 管理器窗口 0x"
				<< std::hex << mgr << std::dec
				<< "（PropertyChangeMask，等 _XSETTINGS_SETTINGS 变化）\n";
			return true;
		}

		bool HasEwmhWm(Display* dpy) {
			X11State& s = XS();
			if (!dpy || !s.netSupportingWmCheck) return false;
			Atom type = 0; int fmt = 0;
			unsigned long n = 0, after = 0;
			unsigned char* data = nullptr;
			bool ok = false;
			if (XGetWindowProperty(dpy, DefaultRootWindow(dpy), s.netSupportingWmCheck,
				0, 1, False, XA_WINDOW, &type, &fmt, &n, &after, &data) == Success && data)
				ok = (fmt == 32 && n >= 1 &&
					*reinterpret_cast<const unsigned long*>(data) != 0);
			if (data) XFree(data);
			return ok;
		}

		void WarnNoWindowManager(const char* what) {
			static bool warned = false;
			if (warned) return;
			warned = true;
			std::cerr << "[Zekit] " << what
				<< "：当前 X 服务上没有 EWMH 窗口管理器"
				"（根窗口 _NET_SUPPORTING_WM_CHECK 为空）。\n"
				"        X11 的最小化 / 最大化 / 置顶 / 尺寸约束都是**发给 WM 的请求**"
				"（WM_CHANGE_STATE / _NET_WM_STATE / WM_NORMAL_HINTS），"
				"没有 WM 就没人处理 —— 这不是库坏了。\n"
				"        常见于：裸 X / Xvfb / 只带合成器不带 WM 的容器（如 WSLg）；"
				"完整的桌面会话（GNOME / KDE / Xfce）不会有这条。\n";
		}
	}

	unsigned long long NowMs() {
		return static_cast<unsigned long long>(
			std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now().time_since_epoch()).count());
	}

	int OnXError(Display* dpy, XErrorEvent* e) {
		char text[256] = {};
		XGetErrorText(dpy, e->error_code, text, sizeof(text));
		std::cerr << "[Zekit] X11 error: " << text
			<< " (request " << static_cast<int>(e->request_code)
			<< ", minor " << static_cast<int>(e->minor_code) << ")\n";
		return 0;
	}

	bool OpenDisplay() {
		X11State& s = XS();
		if (s.dpy) return true;

		static const bool s_threadsInited = (XInitThreads() != 0);
		(void)s_threadsInited;

		Display* dpy = XOpenDisplay(nullptr);
		if (!dpy) {
			std::cerr << "[Zekit] cannot open X display"
				" (is $DISPLAY set / XWayland running?)\n";
			return false;
		}

		XSetErrorHandler(OnXError);

		{
			int supported = 0;
			XkbSetDetectableAutoRepeat(dpy, True, &supported);
			s.autoRepeatSupported = (supported != 0);
		}

		s.wmProtocols      = Intern(dpy, "WM_PROTOCOLS");
		s.wmDeleteWindow   = Intern(dpy, "WM_DELETE_WINDOW");
		s.iccWmState       = Intern(dpy, "WM_STATE");
		s.utf8String       = Intern(dpy, "UTF8_STRING");

		s.netWmName              = Intern(dpy, "_NET_WM_NAME");
		s.netWmIcon              = Intern(dpy, "_NET_WM_ICON");
		s.netWmPid               = Intern(dpy, "_NET_WM_PID");
		s.netWmWindowType        = Intern(dpy, "_NET_WM_WINDOW_TYPE");
		s.netWmWindowTypeNormal  = Intern(dpy, "_NET_WM_WINDOW_TYPE_NORMAL");
		s.netWmState             = Intern(dpy, "_NET_WM_STATE");
		s.netWmStateFullscreen   = Intern(dpy, "_NET_WM_STATE_FULLSCREEN");
		s.netWmStateMaxHorz      = Intern(dpy, "_NET_WM_STATE_MAXIMIZED_HORZ");
		s.netWmStateMaxVert      = Intern(dpy, "_NET_WM_STATE_MAXIMIZED_VERT");
		s.netWmStateAbove        = Intern(dpy, "_NET_WM_STATE_ABOVE");
		s.netWmStateHidden       = Intern(dpy, "_NET_WM_STATE_HIDDEN");
		s.netWmStateSkipTaskbar  = Intern(dpy, "_NET_WM_STATE_SKIP_TASKBAR");
		s.netWmStateSkipPager    = Intern(dpy, "_NET_WM_STATE_SKIP_PAGER");
		s.netWmWindowTypePopupMenu = Intern(dpy, "_NET_WM_WINDOW_TYPE_POPUP_MENU");
		s.netWmWindowOpacity     = Intern(dpy, "_NET_WM_WINDOW_OPACITY");
		s.netWmWorkarea          = Intern(dpy, "_NET_WORKAREA");
		s.netSupportingWmCheck   = Intern(dpy, "_NET_SUPPORTING_WM_CHECK");
		s.netWmAllowedActions    = Intern(dpy, "_NET_WM_ALLOWED_ACTIONS");
		s.netWmActionMove        = Intern(dpy, "_NET_WM_ACTION_MOVE");
		s.netWmActionResize      = Intern(dpy, "_NET_WM_ACTION_RESIZE");
		s.netWmActionMinimize    = Intern(dpy, "_NET_WM_ACTION_MINIMIZE");
		s.netWmActionMaxHorz     = Intern(dpy, "_NET_WM_ACTION_MAXIMIZE_HORZ");
		s.netWmActionMaxVert     = Intern(dpy, "_NET_WM_ACTION_MAXIMIZE_VERT");
		s.netWmActionClose       = Intern(dpy, "_NET_WM_ACTION_CLOSE");
		s.netWmMoveResize        = Intern(dpy, "_NET_WM_MOVERESIZE");
		s.netActiveWindow        = Intern(dpy, "_NET_ACTIVE_WINDOW");
		s.motifWmHints           = Intern(dpy, "_MOTIF_WM_HINTS");
		s.gtkThemeVariant        = Intern(dpy, "_GTK_THEME_VARIANT");

		s.clipboard     = Intern(dpy, "CLIPBOARD");
		s.targetsAtom   = Intern(dpy, "TARGETS");
		s.textAtom      = Intern(dpy, "TEXT");
		s.incrAtom      = Intern(dpy, "INCR");
		s.clipboardProp = Intern(dpy, "ZEKIT_SELECTION");

		s.xdndAware      = Intern(dpy, "XdndAware");
		s.xdndEnter      = Intern(dpy, "XdndEnter");
		s.xdndPosition   = Intern(dpy, "XdndPosition");
		s.xdndStatus     = Intern(dpy, "XdndStatus");
		s.xdndLeave      = Intern(dpy, "XdndLeave");
		s.xdndDrop       = Intern(dpy, "XdndDrop");
		s.xdndFinished   = Intern(dpy, "XdndFinished");
		s.xdndSelection  = Intern(dpy, "XdndSelection");
		s.xdndActionCopy = Intern(dpy, "XdndActionCopy");
		s.textUriList    = Intern(dpy, "text/uri-list");
		s.xdndProp       = Intern(dpy, "ZEKIT_XDND");

		s.xsSelectionAtom = Intern(dpy, "_XSETTINGS_S0");
		s.xsSettingsAtom  = Intern(dpy, "_XSETTINGS_SETTINGS");

		s.netCurrentDesktop = Intern(dpy, "_NET_CURRENT_DESKTOP");
		s.netFrameExtents   = Intern(dpy, "_NET_FRAME_EXTENTS");

		s.dpy = dpy;
		return true;
	}

	void CloseDisplayIfUnused() {
		X11State& s = XS();
		if (!s.dpy || s.refs > 0) return;
		s.windows.clear();
		s.xsWindow = 0;
		s.xsOnDpy = nullptr;
		XCloseDisplay(s.dpy);
		s.im = nullptr;
		s.imTried = false;
		s.dpy = nullptr;
	}

	XIM SharedInputMethod(Display* dpy) {
		X11State& s = XS();
		if (s.im || s.imTried) return s.im;
		s.imTried = true;

		if (const char* cur = std::setlocale(LC_CTYPE, nullptr)) {
			if (std::strcmp(cur, "C") == 0 || std::strcmp(cur, "POSIX") == 0) {
				if (!std::setlocale(LC_ALL, "")) {
					std::cerr << "[Zekit] setlocale(LC_ALL, \"\") failed; "
					             "the input method may not produce non-ASCII text.\n";
				}
			}
		}

		XSetLocaleModifiers("");
		const auto tIm = std::chrono::steady_clock::now();
		s.im = XOpenIM(dpy, nullptr, nullptr, nullptr);
		if (Plat::TraceLifecycle()) {
			std::cerr << "[Zekit/trace] XOpenIM " << (s.im ? "成功" : "失败")
				<< "，耗时 " << std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::steady_clock::now() - tIm).count() << "ms\n";
		}
		return s.im;
	}

	std::string ProgramName() {
		char buf[4096] = {};
		const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
		if (n <= 0) return "Zekit";
		buf[n] = '\0';
		std::string s(buf);
		const std::size_t slash = s.find_last_of('/');
		if (slash != std::string::npos) s.erase(0, slash + 1);
		return s.empty() ? std::string("Zekit") : s;
	}

	Plat::Modifiers ModsFromState(unsigned int state) {
		Plat::Modifiers m = static_cast<Plat::Modifiers>(0);
		if (state & ShiftMask)   m = m | Plat::Modifiers::Shift;
		if (state & ControlMask) m = m | Plat::Modifiers::Control;
		if (state & Mod1Mask)    m = m | Plat::Modifiers::Alt;
		if (state & Mod4Mask)    m = m | Plat::Modifiers::Super;
		return m;
	}

	Plat::MouseButton ButtonFromX(unsigned int button) {
		switch (button) {
		case Button1: return Plat::MouseButton::Left;
		case Button2: return Plat::MouseButton::Middle;
		case Button3: return Plat::MouseButton::Right;
		case 8:       return Plat::MouseButton::X1;
		case 9:       return Plat::MouseButton::X2;
		default:      return static_cast<Plat::MouseButton>(0);
		}
	}

	unsigned int XButtonMask(Plat::MouseButton b) {
		switch (b) {
		case Plat::MouseButton::Left:   return Button1Mask;
		case Plat::MouseButton::Right:  return Button3Mask;
		case Plat::MouseButton::Middle: return Button2Mask;
		default:                        return 0;
		}
	}

	int ScreenDpi(Display* dpy, int screen) {
		if (char* res = XResourceManagerString(dpy)) {
			if (const char* p = std::strstr(res, "Xft.dpi")) {
				p += 7;
				while (*p == ' ' || *p == '\t' || *p == ':') ++p;
				const int v = std::atoi(p);
				if (v > 0) return v;
			}
		}
		const int mm = DisplayWidthMM(dpy, screen);
		const int px = DisplayWidth(dpy, screen);
		if (mm > 0 && px > 0)
			return static_cast<int>(px * 25.4 / mm + 0.5);
		return 96;
	}

	void WorkArea(Display* dpy, int screen, int& x, int& y, int& w, int& h) {
		x = 0; y = 0;
		w = DisplayWidth(dpy, screen);
		h = DisplayHeight(dpy, screen);

		Atom type = 0L; int fmt = 0;
		unsigned long n = 0, after = 0;
		unsigned char* data = nullptr;
		if (XGetWindowProperty(dpy, RootWindow(dpy, screen), XS().netWmWorkarea,
			0, 4, False, XA_CARDINAL, &type, &fmt, &n, &after, &data) == Success && data) {
			if (n >= 4) {
				const long* v = reinterpret_cast<const long*>(data);
				x = static_cast<int>(v[0]);
				y = static_cast<int>(v[1]);
				w = static_cast<int>(v[2]);
				h = static_cast<int>(v[3]);
			}
			XFree(data);
		}
	}

	bool PixelTransparentCapable(Display* dpy, int screen) {
		static Display* probedDpy = nullptr;
		static bool ok = false;
		if (probedDpy == dpy) return ok;
		probedDpy = dpy;
		ok = false;

		int ev = 0, er = 0, maj = 0;
		if (!XQueryExtension(dpy, "Composite", &maj, &ev, &er)) return ok;

		char name[32];
		std::snprintf(name, sizeof name, "_NET_WM_CM_S%d", screen);
		const Atom a = XInternAtom(dpy, name, False);
		if (XGetSelectionOwner(dpy, a) == 0L) return ok;

		XVisualInfo tmpl{};
		tmpl.depth = 32;
		int n = 0;
		XVisualInfo* list = XGetVisualInfo(dpy, VisualDepthMask, &tmpl, &n);
		if (list) {
			for (int i = 0; i < n; ++i) {
				if (list[i].c_class == TrueColor) { ok = true; break; }
			}
			XFree(list);
		}
		return ok;
	}

	Visual* ArgbVisual(Display* dpy, int screen) {
		(void)screen;
		XVisualInfo tmpl{};
		tmpl.depth = 32;
		int n = 0;
		XVisualInfo* list = XGetVisualInfo(dpy, VisualDepthMask, &tmpl, &n);
		if (!list) return nullptr;
		Visual* out = nullptr;
		for (int i = 0; i < n; ++i) {
			if (list[i].c_class == TrueColor) { out = list[i].visual; break; }
		}
		XFree(list);
		return out;
	}

	struct XShapeShim {
		void (*CombineRectangles)(Display*, ::Window, int, int, int,
			XRectangle*, int, int, int) = nullptr;
		void (*CombineMask)(Display*, ::Window, int, int, int, Pixmap, int) = nullptr;
		bool ok = false;
		bool tried = false;
	};

	XShapeShim& Shape() {
		static XShapeShim s;
		if (s.tried) return s;
		s.tried = true;
		void* h = ::dlopen("libXext.so.6", RTLD_LAZY | RTLD_LOCAL);
		if (!h) h = ::dlopen("libXext.so", RTLD_LAZY | RTLD_LOCAL);
		if (!h) return s;
		s.CombineRectangles = reinterpret_cast<decltype(s.CombineRectangles)>(
			::dlsym(h, "XShapeCombineRectangles"));
		s.CombineMask = reinterpret_cast<decltype(s.CombineMask)>(
			::dlsym(h, "XShapeCombineMask"));
		s.ok = s.CombineRectangles != nullptr;
		return s;
	}

	constexpr int kShapeInput = 2;
	constexpr int kShapeSet = 0;
	constexpr int kUnSorted = 0;

	void AppendIconEntry(std::vector<unsigned long>& out,
		const unsigned char* rgba, int sw, int sh, int dw, int dh) {
		if (dw <= 0 || dh <= 0) return;
		out.push_back(static_cast<unsigned long>(dw));
		out.push_back(static_cast<unsigned long>(dh));
		for (int y = 0; y < dh; ++y) {
			const int sy0 = y * sh / dh;
			const int sy1 = std::max(sy0 + 1, (y + 1) * sh / dh);
			for (int x = 0; x < dw; ++x) {
				const int sx0 = x * sw / dw;
				const int sx1 = std::max(sx0 + 1, (x + 1) * sw / dw);
				unsigned long r = 0, g = 0, b = 0, cnt = 0, aw = 0;
				for (int sy = sy0; sy < sy1 && sy < sh; ++sy) {
					for (int sx = sx0; sx < sx1 && sx < sw; ++sx) {
						const unsigned char* p = rgba + (static_cast<std::size_t>(sy) * sw + sx) * 4;
						const unsigned long pa = p[3];
						r += static_cast<unsigned long>(p[0]) * pa;
						g += static_cast<unsigned long>(p[1]) * pa;
						b += static_cast<unsigned long>(p[2]) * pa;
						aw += pa;
						++cnt;
					}
				}
				if (!cnt) cnt = 1;
				const unsigned long outA = aw / cnt;
				const unsigned long outR = aw ? r / aw : 0;
				const unsigned long outG = aw ? g / aw : 0;
				const unsigned long outB = aw ? b / aw : 0;
				out.push_back((outA << 24) | (outR << 16) | (outG << 8) | outB);
			}
		}
	}

	void WriteNetWmIcon(Display* dpy, ::Window win, Atom iconAtom,
		const unsigned char* rgba, int w, int h, const std::string& label) {
		std::vector<unsigned long> data;
		data.reserve(static_cast<std::size_t>(w) * h * 2 + 8);

		if (w > 256 || h > 256) AppendIconEntry(data, rgba, w, h, 256, 256);
		else                    AppendIconEntry(data, rgba, w, h, w, h);
		if (w > 128 && h > 128) AppendIconEntry(data, rgba, w, h, 128, 128);
		if (w > 32 && h > 32)   AppendIconEntry(data, rgba, w, h, 32, 32);

		XChangeProperty(dpy, win, iconAtom, XA_CARDINAL, 32,
			PropModeReplace, reinterpret_cast<const unsigned char*>(data.data()),
			static_cast<int>(data.size()));
		XFlush(dpy);

		if (Plat::TraceLifecycle())
			std::cerr << "[Zekit] _NET_WM_ICON ← " << label << "  " << w << "x" << h
			<< "（已写入 " << data.size() << " 个 CARDINAL）\n";
	}

}

namespace Plat {

	namespace {
		uint32_t ToKey(KeySym ks) {
			if (ks >= XK_a && ks <= XK_z)
				return static_cast<uint32_t>('A' + (ks - XK_a));
			if (ks >= XK_A && ks <= XK_Z)
				return static_cast<uint32_t>(ks);
			if (ks >= XK_0 && ks <= XK_9)
				return static_cast<uint32_t>(ks);

			if (ks >= XK_F1 && ks <= XK_F24)
				return static_cast<uint32_t>(0x70 + (ks - XK_F1));

			if (ks >= XK_KP_0 && ks <= XK_KP_9)
				return static_cast<uint32_t>(0x60 + (ks - XK_KP_0));

			switch (ks) {
			case XK_semicolon: case XK_colon:      return 0xBA;
			case XK_equal:     case XK_plus:       return 0xBB;
			case XK_comma:     case XK_less:       return 0xBC;
			case XK_minus:     case XK_underscore: return 0xBD;
			case XK_period:    case XK_greater:    return 0xBE;
			case XK_slash:     case XK_question:   return 0xBF;
			case XK_grave:     case XK_asciitilde: return 0xC0;
			case XK_bracketleft:  case XK_braceleft:  return 0xDB;
			case XK_backslash:    case XK_bar:        return 0xDC;
			case XK_bracketright: case XK_braceright: return 0xDD;
			case XK_apostrophe:   case XK_quotedbl:   return 0xDE;

			case XK_Escape:      return 0x1B;
			case XK_Tab:         return 0x09;
			case XK_Caps_Lock:   return 0x14;
			case XK_Shift_L: case XK_Shift_R:     return 0x10;
			case XK_Control_L: case XK_Control_R: return 0x11;
			case XK_Alt_L: case XK_Alt_R:         return 0x12;
			case XK_space:       return 0x20;
			case XK_Return: case XK_KP_Enter:     return 0x0D;
			case XK_BackSpace:   return 0x08;

			case XK_Insert:      return 0x2D;
			case XK_Delete:      return 0x2E;
			case XK_Home:        return 0x24;
			case XK_End:         return 0x23;
			case XK_Prior:       return 0x21;
			case XK_Next:        return 0x22;

			case XK_Left:        return 0x25;
			case XK_Up:          return 0x26;
			case XK_Right:       return 0x27;
			case XK_Down:        return 0x28;

			case XK_KP_Multiply: return 0x6A;
			case XK_KP_Add:      return 0x6B;
			case XK_KP_Subtract: return 0x6D;
			case XK_KP_Decimal:  return 0x6E;
			case XK_KP_Divide:   return 0x6F;

			case XK_Super_L:     return 0x5B;
			case XK_Super_R:     return 0x5C;
			case XK_Menu:        return 0x5D;
			case XK_Print:       return 0x2C;
			case XK_Scroll_Lock: return 0x91;
			case XK_Pause:       return 0x13;
			case XK_Num_Lock:    return 0x90;
#ifdef XK_Sleep
			case XK_Sleep:       return 0x5F;
#endif

			default: break;
			}

#ifdef XF86XK_AudioMute
			switch (ks) {
			case XF86XK_AudioMute:        return 0xAD;
			case XF86XK_AudioLowerVolume: return 0xAE;
			case XF86XK_AudioRaiseVolume: return 0xAF;
			case XF86XK_AudioNext:        return 0xB0;
			case XF86XK_AudioPrev:        return 0xB1;
			case XF86XK_AudioStop:        return 0xB2;
			case XF86XK_AudioPlay:        return 0xB3;
			case XF86XK_Mail:             return 0xB4;
			case XF86XK_AudioMedia:       return 0xB5;
			case XF86XK_Launch0:          return 0xB6;
			case XF86XK_Launch1:          return 0xB7;
			default: break;
			}
#endif
			return 0;
		}
	}

	struct X11Window::Impl {
		Display*     m_dpy = nullptr;
		::Window     m_win = 0;
		int          m_screen = 0;
		XIC          m_ic = nullptr;
		IWindowHost* m_host = nullptr;

		std::string  m_clipboardText;
		bool         m_clipboardOwns = false;

		int m_w = 0, m_h = 0;

		bool m_active = false;
		bool m_mapped = false;
		bool m_disabled = false;
		bool m_alphaVisual = false;
		bool m_minimized = false;
		bool m_maximized = false;
		bool m_fullscreen = false;
		bool m_borderless = false;
		bool m_dark = false;
		int  m_xsDark = 0;
		unsigned int m_backdrop = 0;

		float m_spotX = 0.0f;
		float m_spotTop = 0.0f;
		float m_spotBottom = 0.0f;
		bool  m_spotValid = false;

		int  m_minW = 0, m_minH = 0, m_maxW = 0, m_maxH = 0;

		bool m_centerOnResize = true;
		bool m_resizable = true;

		bool m_isChild = false;
		bool m_isOwned = false;
		bool m_popup = false;
		::Window m_parent = 0;

		::Window m_hostXid = 0;

		int  m_posX = 0, m_posY = 0;
		bool m_hasExplicitPos = false;

		void NotePosition(int x, int y) {
			m_posX = x; m_posY = y; m_hasExplicitPos = true;
		}

		bool m_titleBarVisible = true;
		bool m_minBtnEnabled = true;
		bool m_maxBtnEnabled = true;
		bool m_closeBtnEnabled = true;

		struct Geom { int x = 0, y = 0, w = 0, h = 0; } m_savedGeom{};

		unsigned long long m_wakeDeadlineMs = 0;
		bool m_wakeExpired = false;

		bool m_redrawPending = false;

		std::vector<KeyCode> m_keysDown;
		bool m_synthRepeat = false;

		int m_lastMouseX = -1;
		int m_lastMouseY = -1;

		int m_exitCode = 0;

		bool Alive() const { return m_win != 0; }

		bool Emit(EventKind kind) {
			Event e;
			e.kind = kind;
			return m_host ? m_host->OnPlatformEvent(e) : false;
		}
		bool Emit(const Event& e) {
			return m_host ? m_host->OnPlatformEvent(e) : false;
		}

		void SetUtf8Property(Atom prop, const std::string& utf8) {
			if (!Alive() || utf8.empty()) return;
			XChangeProperty(m_dpy, m_win, prop, XS().utf8String, 8, PropModeReplace,
				reinterpret_cast<const unsigned char*>(utf8.data()),
				static_cast<int>(utf8.size()));
		}

		void SendWmState(Atom a1, Atom a2, long action) {
			if (!Alive()) return;
			XEvent e{};
			e.xclient.type = ClientMessage;
			e.xclient.window = m_win;
			e.xclient.message_type = XS().netWmState;
			e.xclient.format = 32;
			e.xclient.data.l[0] = action;
			e.xclient.data.l[1] = static_cast<long>(a1);
			e.xclient.data.l[2] = static_cast<long>(a2);
			e.xclient.data.l[3] = 1;
			XSendEvent(m_dpy, RootWindow(m_dpy, m_screen), False,
				SubstructureNotifyMask | SubstructureRedirectMask, &e);
			XFlush(m_dpy);
		}

		void SendActiveWindow() {
			if (!Alive()) return;
			XEvent e{};
			e.xclient.type = ClientMessage;
			e.xclient.window = m_win;
			e.xclient.message_type = XS().netActiveWindow;
			e.xclient.format = 32;
			e.xclient.data.l[0] = 1;
			e.xclient.data.l[1] = CurrentTime;
			e.xclient.data.l[2] = 0;
			e.xclient.data.l[3] = 0;
			e.xclient.data.l[4] = 0;
			XSendEvent(m_dpy, RootWindow(m_dpy, m_screen), False,
				SubstructureNotifyMask | SubstructureRedirectMask, &e);
			XFlush(m_dpy);
		}

		bool ReadAtomList(Atom prop, std::vector<Atom>& out) const {
			out.clear();
			if (!Alive()) return false;
			Atom type = 0L; int fmt = 0;
			unsigned long n = 0, after = 0;
			unsigned char* data = nullptr;
			if (XGetWindowProperty(m_dpy, m_win, prop, 0, 64, False, XA_ATOM,
				&type, &fmt, &n, &after, &data) != Success || !data)
				return false;
			const Atom* v = reinterpret_cast<const Atom*>(data);
			out.assign(v, v + n);
			XFree(data);
			return true;
		}

		void RefreshMaximized() {
			std::vector<Atom> st;
			if (!ReadAtomList(XS().netWmState, st)) return;
			const bool h = std::find(st.begin(), st.end(), XS().netWmStateMaxHorz) != st.end();
			const bool v = std::find(st.begin(), st.end(), XS().netWmStateMaxVert) != st.end();
			m_maximized = (h && v);
		}

		void RefreshMinimized() {
			std::vector<Atom> st;
			if (ReadAtomList(XS().netWmState, st)) {
				if (std::find(st.begin(), st.end(), XS().netWmStateHidden) != st.end()) {
					m_minimized = true;
					return;
				}
			}
			Atom type = 0L; int fmt = 0;
			unsigned long n = 0, after = 0;
			unsigned char* data = nullptr;
			if (!Alive()) return;
			if (XGetWindowProperty(m_dpy, m_win, XS().iccWmState, 0, 2, False, XS().iccWmState,
				&type, &fmt, &n, &after, &data) == Success && data) {
				const long* v = reinterpret_cast<const long*>(data);
				m_minimized = (n >= 1 && v[0] == IconicState);
				XFree(data);
			}
		}

		void ApplySizeHints() {
			if (!Alive()) return;
			XSizeHints h{};
			h.flags = PWinGravity;
			h.win_gravity = StaticGravity;

			const bool relaxLimits = m_fullscreen;

			if (m_hasExplicitPos) {
				h.flags |= PPosition | USPosition;
				h.x = m_posX;
				h.y = m_posY;
			}

			int minW = m_minW, minH = m_minH, maxW = m_maxW, maxH = m_maxH;
			if (!m_resizable && !relaxLimits) {
				minW = maxW = m_w;
				minH = maxH = m_h;
			}
			if (!relaxLimits && (minW > 0 || minH > 0)) {
				h.flags |= PMinSize;
				h.min_width  = minW > 0 ? minW : 1;
				h.min_height = minH > 0 ? minH : 1;
			}
			if (!relaxLimits && (maxW > 0 || maxH > 0)) {
				h.flags |= PMaxSize;
				h.max_width  = maxW > 0 ? maxW : INT_MAX;
				h.max_height = maxH > 0 ? maxH : INT_MAX;
			}
			XSetWMNormalHints(m_dpy, m_win, &h);
			XFlush(m_dpy);
		}

		void ApplyMotifHints() {
			if (!Alive()) return;

			constexpr unsigned long MWM_HINTS_FUNCTIONS   = 1UL << 0;
			constexpr unsigned long MWM_HINTS_DECORATIONS = 1UL << 1;
			constexpr unsigned long MWM_FUNC_RESIZE   = 1UL << 1;
			constexpr unsigned long MWM_FUNC_MOVE     = 1UL << 2;
			constexpr unsigned long MWM_FUNC_MINIMIZE = 1UL << 3;
			constexpr unsigned long MWM_FUNC_MAXIMIZE = 1UL << 4;
			constexpr unsigned long MWM_FUNC_CLOSE    = 1UL << 5;
			constexpr unsigned long MWM_DECOR_BORDER   = 1UL << 1;
			constexpr unsigned long MWM_DECOR_RESIZEH  = 1UL << 2;
			constexpr unsigned long MWM_DECOR_TITLE    = 1UL << 3;
			constexpr unsigned long MWM_DECOR_MENU     = 1UL << 4;
			constexpr unsigned long MWM_DECOR_MINIMIZE = 1UL << 5;
			constexpr unsigned long MWM_DECOR_MAXIMIZE = 1UL << 6;

			unsigned long deco = 0;
			if (m_borderless) {
				deco = 0;
			} else if (m_titleBarVisible) {
				deco = MWM_DECOR_TITLE | MWM_DECOR_BORDER | MWM_DECOR_RESIZEH | MWM_DECOR_MENU;
				if (m_minBtnEnabled) deco |= MWM_DECOR_MINIMIZE;
				if (m_maxBtnEnabled) deco |= MWM_DECOR_MAXIMIZE;
			} else {
				deco = MWM_DECOR_BORDER | MWM_DECOR_RESIZEH;
			}
			if (!m_resizable) deco &= ~MWM_DECOR_RESIZEH;

			unsigned long funcs = MWM_FUNC_MOVE;
			if (m_resizable)     funcs |= MWM_FUNC_RESIZE;
			if (m_minBtnEnabled) funcs |= MWM_FUNC_MINIMIZE;
			if (m_maxBtnEnabled) funcs |= MWM_FUNC_MAXIMIZE;
			if (m_closeBtnEnabled) funcs |= MWM_FUNC_CLOSE;

			const unsigned long data[5] = {
				MWM_HINTS_FUNCTIONS | MWM_HINTS_DECORATIONS,
				funcs,
				deco,
				0,
				0
			};
			XChangeProperty(m_dpy, m_win, XS().motifWmHints, XS().motifWmHints,
				32, PropModeReplace, reinterpret_cast<const unsigned char*>(data), 5);
			XFlush(m_dpy);
		}

		void ApplyAllowedActions() {
			if (!Alive()) return;
			Atom acts[8];
			int n = 0;
			acts[n++] = XS().netWmActionMove;
			if (m_resizable)      acts[n++] = XS().netWmActionResize;
			if (m_minBtnEnabled)  acts[n++] = XS().netWmActionMinimize;
			if (m_maxBtnEnabled) {
				acts[n++] = XS().netWmActionMaxHorz;
				acts[n++] = XS().netWmActionMaxVert;
			}
			if (m_closeBtnEnabled) acts[n++] = XS().netWmActionClose;
			XChangeProperty(m_dpy, m_win, XS().netWmAllowedActions, XA_ATOM, 32,
				PropModeReplace, reinterpret_cast<const unsigned char*>(acts), n);
			XFlush(m_dpy);
		}

		void ApplyAllHints() {
			ApplySizeHints();
			ApplyMotifHints();
			ApplyAllowedActions();
		}

		void NativeGone(::Window xid) {
			if (xid) {
				XS().windows.erase(xid);
				if (XS().refs > 0) --XS().refs;
			}
			if (m_ic) { XDestroyIC(m_ic); m_ic = nullptr; }

			if (m_win == xid) m_win = 0;

			IWindowHost* host = m_host;
			m_host = nullptr;

			XS().quitHost = host;

			if (TraceLifecycle())
				std::cerr << "[Zekit/trace] NativeGone：原生窗口 " << xid
				<< " 销毁（refs 剩 " << XS().refs << "）→ 回调宿主\n";
			if (host) host->OnNativeClosed();
			if (TraceLifecycle())
				std::cerr << "[Zekit/trace] NativeGone：宿主已回调，quitPending="
				<< XS().quitPending << "\n";
			CloseDisplayIfUnused();
		}

		void DestroyNative() {
			if (!Alive()) return;
			const ::Window xid = m_win;
			m_win = 0;
			XS().windows.erase(xid);
			XDestroyWindow(m_dpy, xid);
			NativeGone(xid);
		}

		void HandleEvent(XEvent& ev);

		void TranslateKey(XKeyEvent& ke, bool press);

		void AnswerSelectionRequest(const XSelectionRequestEvent& req);

		bool     m_acceptDrop = false;
		bool     m_dndActive = false;
		bool     m_dndEnterSent = false;
		::Window m_dndSource = 0;
		float    m_dndX = 0.0f;
		float    m_dndY = 0.0f;
		std::vector<std::string> m_dndFiles;

		void EmitFileEvent(EventKind kind, float x, float y,
			std::vector<std::string> files = {}) {
			if (!m_host) return;
			Event e;
			e.kind = kind;
			e.x = x;
			e.y = y;
			e.files = std::move(files);
			m_host->OnPlatformEvent(e);
		}

		void SendXdndStatus(bool accept);

		void RootToClient(int rx, int ry, float& cx, float& cy) const;

		void HandleXdndEnter(const XClientMessageEvent& c);
		void HandleXdndPosition(const XClientMessageEvent& c);
		void HandleXdndLeave();
		void HandleXdndDrop();
		void HandleXdndSelection(const XSelectionEvent& s);

		void OnXSettingsChanged();
	};

	void X11Window::Impl::OnXSettingsChanged() {
		X11State& s = XS();

		if (Plat::TraceTheme())
			std::cerr << "[Zekit/theme] ← 收到 XSETTINGS 变化通知，重新判读\n";

		const ::Window mgr = FindXSettingsWindow(m_dpy);
		if (mgr != s.xsWindow) {
			s.xsWindow = mgr;
			s.xsOnDpy = mgr ? m_dpy : nullptr;
			if (mgr) XSelectInput(m_dpy, mgr, PropertyChangeMask);
			if (Plat::TraceTheme())
				std::cerr << "[Zekit/theme] XSETTINGS 管理器换人了：0x"
				<< std::hex << mgr << std::dec << "\n";
		}

		const XsVerdict v = ReadXSettingsDark(m_dpy, mgr);
		if (v == 0) return;
		if (v == m_xsDark) return;
		m_xsDark = v;
		Emit(EventKind::ThemeChanged);
	}

	void X11Window::Impl::HandleEvent(XEvent& ev) {
		if (m_disabled) {
			switch (ev.type) {
			case MotionNotify: case ButtonPress: case ButtonRelease:
			case EnterNotify: case LeaveNotify:
			case KeyPress: case KeyRelease:
				return;
			default:
				break;
			}
		}

		switch (ev.type) {

		case ConfigureNotify: {
			if (ev.xconfigure.window != m_win) break;
			const int nw = ev.xconfigure.width;
			const int nh = ev.xconfigure.height;
			if (nw == m_w && nh == m_h) break;
			m_w = nw; m_h = nh;
			Emit(EventKind::Resize);
			break;
		}

		case Expose:
			if (ev.xexpose.count == 0) Emit(EventKind::Redraw);
			break;

		case MotionNotify: {
			if (ev.xmotion.window != m_win) break;
			m_lastMouseX = ev.xmotion.x;
			m_lastMouseY = ev.xmotion.y;

			Event e;
			e.kind = EventKind::MouseMove;
			e.x = static_cast<float>(ev.xmotion.x);
			e.y = static_cast<float>(ev.xmotion.y);
			e.mods = ModsFromState(ev.xmotion.state);
			Emit(e);
			break;
		}

		case EnterNotify: {
			if (ev.xcrossing.window != m_win) break;
			if (ev.xcrossing.detail == NotifyInferior) break;
			if (ev.xcrossing.mode == NotifyGrab || ev.xcrossing.mode == NotifyUngrab) break;

			m_lastMouseX = ev.xcrossing.x;
			m_lastMouseY = ev.xcrossing.y;
			Event e;
			e.kind = EventKind::MouseMove;
			e.x = static_cast<float>(ev.xcrossing.x);
			e.y = static_cast<float>(ev.xcrossing.y);
			e.mods = ModsFromState(ev.xcrossing.state);
			Emit(e);
			break;
		}

		case LeaveNotify: {
			if (ev.xcrossing.window != m_win) break;
			if (ev.xcrossing.detail == NotifyInferior) break;
			if (ev.xcrossing.mode == NotifyGrab || ev.xcrossing.mode == NotifyUngrab) break;
			m_lastMouseX = -1;
			m_lastMouseY = -1;
			Emit(EventKind::MouseLeave);
			break;
		}

		case ButtonPress: {
			const unsigned int b = ev.xbutton.button;

			if (b == 4 || b == 5 || b == 6 || b == 7) {
				Event e;
				e.kind = EventKind::MouseWheel;
				e.x = static_cast<float>(ev.xbutton.x);
				e.y = static_cast<float>(ev.xbutton.y);
				e.wheelDelta = (b == 4 || b == 6) ? 120 : -120;
				e.mods = ModsFromState(ev.xbutton.state);
				Emit(e);
				break;
			}

			Event e;
			e.kind = EventKind::MouseDown;
			e.x = static_cast<float>(ev.xbutton.x);
			e.y = static_cast<float>(ev.xbutton.y);
			e.button = ButtonFromX(b);
			e.mods = ModsFromState(ev.xbutton.state);
			Emit(e);
			break;
		}

		case ButtonRelease: {
			const unsigned int b = ev.xbutton.button;
			if (b == 4 || b == 5 || b == 6 || b == 7) break;
			Event e;
			e.kind = EventKind::MouseUp;
			e.x = static_cast<float>(ev.xbutton.x);
			e.y = static_cast<float>(ev.xbutton.y);
			e.button = ButtonFromX(b);
			e.mods = ModsFromState(ev.xbutton.state);
			Emit(e);
			break;
		}

		case KeyPress:
			TranslateKey(ev.xkey, true);
			break;
		case KeyRelease:
			TranslateKey(ev.xkey, false);
			break;
		case MappingNotify:
			XRefreshKeyboardMapping(&ev.xmapping);
			break;

		case kXFocusIn:
			if (ev.xfocus.detail == NotifyInferior) break;
			if (ev.xfocus.mode == NotifyGrab || ev.xfocus.mode == NotifyUngrab) break;
			m_active = true;
			if (m_ic) XSetICFocus(m_ic);
			Emit(EventKind::FocusIn);
			break;

		case kXFocusOut:
			if (ev.xfocus.detail == NotifyInferior) break;
			if (ev.xfocus.mode == NotifyGrab || ev.xfocus.mode == NotifyUngrab) break;
			m_active = false;
			if (m_ic) XUnsetICFocus(m_ic);
			Emit(EventKind::FocusOut);
			break;

		case MapNotify:
			if (ev.xmap.window == m_win) { m_mapped = true; m_minimized = false; }
			break;

		case UnmapNotify:
			if (ev.xunmap.window == m_win) { m_mapped = false; RefreshMinimized(); }
			break;

		case PropertyNotify:
			if (ev.xproperty.window == m_win &&
				ev.xproperty.atom == XS().netWmState) {
				RefreshMaximized();
				RefreshMinimized();
			} else if (ev.xproperty.window == m_win &&
				ev.xproperty.atom == XS().iccWmState) {
				RefreshMinimized();
			}
			else if (ev.xproperty.atom == XS().xsSettingsAtom &&
				ev.xproperty.window == XS().xsWindow) {
				OnXSettingsChanged();
			}
			else if (ev.xproperty.atom == XS().xsSelectionAtom &&
				ev.xproperty.window == RootWindow(m_dpy, m_screen)) {
				OnXSettingsChanged();
			}
			break;

		case ClientMessage:
			if (TraceLifecycle())
				std::cerr << "[Zekit/trace] 收到 ClientMessage type=" << (unsigned long)ev.xclient.message_type
				<< " data0=" << (unsigned long)ev.xclient.data.l[0]
				<< "（WM_PROTOCOLS=" << (unsigned long)XS().wmProtocols
				<< " WM_DELETE_WINDOW=" << (unsigned long)XS().wmDeleteWindow << "）\n";
			if (ev.xclient.message_type == XS().wmProtocols &&
				static_cast<Atom>(ev.xclient.data.l[0]) == XS().wmDeleteWindow) {
				Event e;
				e.kind = EventKind::Close;
				const bool blocked = Emit(e);
				if (TraceLifecycle())
					std::cerr << "[Zekit/trace] Close 事件：Kit "
					<< (blocked ? "拦下（关闭按钮不可用）" : "放行 → 销毁原生窗口") << "\n";
				if (blocked) break;
				DestroyNative();
			}
			else if (ev.xclient.message_type == XS().xdndEnter) {
				HandleXdndEnter(ev.xclient);
			}
			else if (ev.xclient.message_type == XS().xdndPosition) {
				HandleXdndPosition(ev.xclient);
			}
			else if (ev.xclient.message_type == XS().xdndLeave) {
				HandleXdndLeave();
			}
			else if (ev.xclient.message_type == XS().xdndDrop) {
				HandleXdndDrop();
			}
			break;

		case SelectionNotify:
			HandleXdndSelection(ev.xselection);
			break;

		case DestroyNotify:
			if (TraceLifecycle())
				std::cerr << "[Zekit/trace] 收到 DestroyNotify xid=" << ev.xdestroywindow.window << "\n";
			if (ev.xdestroywindow.window) NativeGone(ev.xdestroywindow.window);
			break;

		case SelectionRequest:
			AnswerSelectionRequest(ev.xselectionrequest);
			break;

		case SelectionClear:
			if (TraceClipboard())
				std::cerr << "[Zekit/clip] 失去 CLIPBOARD 所有权（另一个程序接手）\n";
			m_clipboardOwns = false;
			m_clipboardText.clear();
			break;

		default:
			break;
		}
	}

	namespace {
		std::string Utf8ToLatin1(const std::string& utf8) {
			std::string out;
			out.reserve(utf8.size());
			const auto* u = reinterpret_cast<const unsigned char*>(utf8.data());
			const std::size_t n = utf8.size();
			std::size_t i = 0;
			while (i < n) {
				const unsigned char c0 = u[i];
				uint32_t cp = 0;
				std::size_t extra = 0;
				if (c0 < 0x80)                 { cp = c0;        extra = 0; }
				else if ((c0 & 0xE0) == 0xC0)  { cp = c0 & 0x1F; extra = 1; }
				else if ((c0 & 0xF0) == 0xE0)  { cp = c0 & 0x0F; extra = 2; }
				else if ((c0 & 0xF8) == 0xF0)  { cp = c0 & 0x07; extra = 3; }
				else { ++i; out.push_back('?'); continue; }

				if (i + extra >= n) { out.push_back('?'); break; }
				bool ok = true;
				for (std::size_t k = 1; k <= extra; ++k) {
					if ((u[i + k] & 0xC0) != 0x80) { ok = false; break; }
					cp = (cp << 6) | (u[i + k] & 0x3F);
				}
				if (!ok) { ++i; out.push_back('?'); continue; }
				i += extra + 1;
				out.push_back(cp < 0x100 ? static_cast<char>(cp) : '?');
			}
			return out;
		}
	}

	void X11Window::Impl::AnswerSelectionRequest(const XSelectionRequestEvent& req) {
		const bool trace = TraceClipboard();

		XSelectionEvent resp{};
		resp.type = SelectionNotify;
		resp.display = req.display ? req.display : m_dpy;
		resp.requestor = req.requestor;
		resp.selection = req.selection;
		resp.target = req.target;
		resp.time = req.time;
		resp.property = 0L;

		const Atom prop = req.property ? req.property : req.target;

		if (m_clipboardOwns && prop) {
			const X11State& s = XS();
			if (req.target == s.targetsAtom) {
				const Atom list[3] = { s.utf8String, XA_STRING, s.textAtom };
				XChangeProperty(m_dpy, req.requestor, prop, XA_ATOM, 32,
					PropModeReplace, reinterpret_cast<const unsigned char*>(list), 3);
				resp.property = prop;
			}
			else if (req.target == s.utf8String) {
				XChangeProperty(m_dpy, req.requestor, prop, s.utf8String, 8,
					PropModeReplace,
					reinterpret_cast<const unsigned char*>(m_clipboardText.data()),
					static_cast<int>(m_clipboardText.size()));
				resp.property = prop;
			}
			else if (req.target == XA_STRING || req.target == s.textAtom) {
				const std::string latin1 = Utf8ToLatin1(m_clipboardText);
				XChangeProperty(m_dpy, req.requestor, prop,
					req.target == s.textAtom ? s.textAtom : XA_STRING, 8,
					PropModeReplace,
					reinterpret_cast<const unsigned char*>(latin1.data()),
					static_cast<int>(latin1.size()));
				resp.property = prop;
			}
		}

		if (trace)
			std::cerr << "[Zekit/clip] 应答 SelectionRequest：target="
			<< (unsigned long)req.target
			<< (resp.property ? " → 已给出数据" : " → 拒绝（不支持该格式/已失去所有权）")
			<< "\n";

		resp.display = m_dpy;
		XSendEvent(m_dpy, req.requestor, False, 0, reinterpret_cast<XEvent*>(&resp));
		XFlush(m_dpy);
	}

	namespace {
		std::vector<std::string> ParseUriList(const std::string& text) {
			std::vector<std::string> out;
			std::size_t pos = 0;
			while (pos <= text.size()) {
				std::size_t end = text.find('\n', pos);
				if (end == std::string::npos) end = text.size();
				std::string line = text.substr(pos, end - pos);
				if (!line.empty() && line.back() == '\r') line.pop_back();
				pos = end + 1;

				if (line.empty() || line[0] == '#') continue;
				if (line.compare(0, 7, "file://") != 0) continue;

				std::string path = line.substr(7);
				const std::size_t slash = path.find('/');
				if (slash == std::string::npos) continue;
				path = path.substr(slash);

				std::string decoded;
				decoded.reserve(path.size());
				for (std::size_t i = 0; i < path.size(); ++i) {
					if (path[i] == '%' && i + 2 < path.size()) {
						const auto hex = [](char ch) -> int {
							if (ch >= '0' && ch <= '9') return ch - '0';
							if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
							if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
							return -1;
							};
						const int hi = hex(path[i + 1]);
						const int lo = hex(path[i + 2]);
						if (hi >= 0 && lo >= 0) {
							decoded.push_back(static_cast<char>(hi * 16 + lo));
							i += 2;
							continue;
						}
					}
					decoded.push_back(path[i]);
				}
				if (!decoded.empty()) out.push_back(std::move(decoded));
			}
			return out;
		}
	}

	void X11Window::Impl::RootToClient(int rx, int ry, float& cx, float& cy) const {
		cx = static_cast<float>(rx);
		cy = static_cast<float>(ry);
		Display* dpy = XS().dpy;
		if (!dpy || !m_win) return;
		int x = rx, y = ry;
		::Window child = 0;
		XTranslateCoordinates(dpy, DefaultRootWindow(dpy), m_win, rx, ry, &x, &y, &child);
		cx = static_cast<float>(x);
		cy = static_cast<float>(y);
	}

	void X11Window::Impl::SendXdndStatus(bool accept) {
		Display* dpy = XS().dpy;
		if (!dpy || !m_dndSource) return;

		int ax = 0, ay = 0;
		{
			int dx = 0, dy = 0;
			::Window child = 0;
			XTranslateCoordinates(dpy, m_win, DefaultRootWindow(dpy), 0, 0, &dx, &dy, &child);
			ax = dx; ay = dy;
		}

		XEvent e{};
		e.xclient.type = ClientMessage;
		e.xclient.display = dpy;
		e.xclient.window = m_dndSource;
		e.xclient.message_type = XS().xdndStatus;
		e.xclient.format = 32;
		e.xclient.data.l[0] = static_cast<long>(m_win);
		e.xclient.data.l[1] = accept ? 1 : 0;
		e.xclient.data.l[2] = static_cast<long>((ax & 0xffff) | ((ay & 0xffff) << 16));
		e.xclient.data.l[3] = static_cast<long>((m_w & 0xffff) | ((m_h & 0xffff) << 16));
		e.xclient.data.l[4] = accept ? static_cast<long>(XS().xdndActionCopy) : 0;
		XSendEvent(dpy, m_dndSource, False, 0, &e);
		XFlush(dpy);
	}

	void X11Window::Impl::HandleXdndEnter(const XClientMessageEvent& c) {
		Display* dpy = XS().dpy;
		if (!dpy) return;

		if (!m_acceptDrop) { m_dndSource = static_cast<::Window>(c.data.l[0]); SendXdndStatus(false); m_dndSource = 0; return; }

		m_dndSource = static_cast<::Window>(c.data.l[0]);
		m_dndActive = true;
		m_dndEnterSent = false;
		m_dndFiles.clear();

		bool mayHaveUri = (c.data.l[1] & 1) != 0;
		for (int i = 2; i <= 4 && !mayHaveUri; ++i)
			if (static_cast<Atom>(c.data.l[i]) == XS().textUriList) mayHaveUri = true;

		if (!mayHaveUri) {
			SendXdndStatus(false);
			m_dndActive = false;
			m_dndSource = 0;
			if (TraceLifecycle())
				std::cerr << "[Zekit/trace] XDND Enter：源不带 text/uri-list，拒收\n";
			return;
		}
		if (TraceLifecycle())
			std::cerr << "[Zekit/trace] XDND Enter：已挂 XdndAware，正在换 text/uri-list\n";

		XDeleteProperty(dpy, m_win, XS().xdndProp);
		XConvertSelection(dpy, XS().xdndSelection, XS().textUriList,
			XS().xdndProp, m_win, CurrentTime);
		SendXdndStatus(true);
		XFlush(dpy);
	}

	void X11Window::Impl::HandleXdndPosition(const XClientMessageEvent& c) {
		if (!m_dndActive) return;
		const int rx = static_cast<int>(c.data.l[2] >> 16);
		const int ry = static_cast<int>((c.data.l[2] & 0xffff));
		RootToClient(rx, ry, m_dndX, m_dndY);

		if (!m_dndEnterSent) {
			m_dndEnterSent = true;
			EmitFileEvent(EventKind::FileDragEnter, m_dndX, m_dndY, m_dndFiles);
		}
		else {
			EmitFileEvent(EventKind::FileDragOver, m_dndX, m_dndY);
		}
		SendXdndStatus(true);
	}

	void X11Window::Impl::HandleXdndLeave() {
		if (!m_dndActive) return;
		if (TraceLifecycle())
			std::cerr << "[Zekit/trace] XDND Leave\n";
		if (m_dndEnterSent) EmitFileEvent(EventKind::FileDragLeave, m_dndX, m_dndY);
		m_dndActive = false;
		m_dndEnterSent = false;
		m_dndSource = 0;
		m_dndFiles.clear();
	}

	void X11Window::Impl::HandleXdndDrop() {
		Display* dpy = XS().dpy;
		if (!m_dndActive) return;

		if (TraceLifecycle())
			std::cerr << "[Zekit/trace] XDND Drop：文件数=" << m_dndFiles.size() << "\n";
		if (!m_dndFiles.empty())
			EmitFileEvent(EventKind::FileDrop, m_dndX, m_dndY, m_dndFiles);
		else if (m_dndEnterSent)
			EmitFileEvent(EventKind::FileDragLeave, m_dndX, m_dndY);

		if (m_dndSource && dpy) {
			XEvent e{};
			e.xclient.type = ClientMessage;
			e.xclient.display = dpy;
			e.xclient.window = m_dndSource;
			e.xclient.message_type = XS().xdndFinished;
			e.xclient.format = 32;
			e.xclient.data.l[0] = static_cast<long>(m_win);
			e.xclient.data.l[1] = m_dndFiles.empty() ? 0 : 1;
			e.xclient.data.l[2] = m_dndFiles.empty() ? 0
				: static_cast<long>(XS().xdndActionCopy);
			XSendEvent(dpy, m_dndSource, False, 0, &e);
		}

		m_dndActive = false;
		m_dndEnterSent = false;
		m_dndSource = 0;
		m_dndFiles.clear();
		if (dpy) XFlush(dpy);
	}

	void X11Window::Impl::HandleXdndSelection(const XSelectionEvent& s) {
		Display* dpy = XS().dpy;
		if (!dpy || s.property != XS().xdndProp) return;

		Atom type = 0L; int fmt = 0;
		unsigned long n = 0, after = 0;
		unsigned char* data = nullptr;
		std::vector<std::string> files;
		if (XGetWindowProperty(dpy, m_win, XS().xdndProp, 0, LONG_MAX / 4, True,
				AnyPropertyType, &type, &fmt, &n, &after, &data) == Success && data) {
			if (fmt == 8 && n > 0) {
				files = ParseUriList(std::string(reinterpret_cast<const char*>(data),
					static_cast<std::size_t>(n)));
			}
			XFree(data);
		}

		if (!m_dndActive) return;
		if (TraceLifecycle())
			std::cerr << "[Zekit/trace] XDND Selection：换到 " << files.size() << " 个文件\n";
		m_dndFiles = std::move(files);

		if (m_dndEnterSent) EmitFileEvent(EventKind::FileDragEnter, m_dndX, m_dndY, m_dndFiles);
	}

	namespace {
		constexpr int kClipboardWaitMs = 400;
		constexpr int kClipboardIncrMs = 3000;

		bool ReadStringProperty(Display* dpy, ::Window win, Atom prop,
			std::string& out, Atom& typeOut) {
			Atom type = 0L; int fmt = 0;
			unsigned long n = 0, after = 0;
			unsigned char* data = nullptr;
			if (XGetWindowProperty(dpy, win, prop, 0, LONG_MAX / 4, True,
				AnyPropertyType, &type, &fmt, &n, &after, &data) != Success)
				return false;
			typeOut = type;
			if (data) {
				if (fmt == 8 && n > 0)
					out.assign(reinterpret_cast<const char*>(data), static_cast<std::size_t>(n));
				XFree(data);
			}
			return true;
		}

		bool WaitForPropertyNotify(Display* dpy, ::Window win, Atom prop,
			std::deque<XEvent>& deferred,
			std::chrono::steady_clock::time_point deadline) {
			while (std::chrono::steady_clock::now() < deadline) {
				while (XPending(dpy) > 0) {
					XEvent ev{};
					XNextEvent(dpy, &ev);
					if (ev.type == PropertyNotify && ev.xproperty.window == win &&
						ev.xproperty.atom == prop && ev.xproperty.state == PropertyNewValue)
						return true;
					deferred.push_back(ev);
				}
				struct timeval nap{};
				nap.tv_usec = 1000;
				::select(0, nullptr, nullptr, nullptr, &nap);
				XFlush(dpy);
			}
			return false;
		}
	}

	void X11Window::SetClipboardText(const std::string& utf8) {
		if (!m_impl->Alive()) return;
		Display* dpy = m_impl->m_dpy;
		if (!dpy) return;

		X11State& s = XS();

		XSetSelectionOwner(dpy, s.clipboard, m_impl->m_win, CurrentTime);

		m_impl->m_clipboardText = utf8;
		m_impl->m_clipboardOwns = (XGetSelectionOwner(dpy, s.clipboard) == m_impl->m_win);
		XFlush(dpy);

		if (TraceClipboard())
			std::cerr << "[Zekit/clip] 写入剪贴板 " << m_impl->m_clipboardText.size()
			<< " 字节，"
			<< (m_impl->m_clipboardOwns ? "已取得 CLIPBOARD 所有权" : "**未取得所有权**（被拒）")
			<< "\n";
	}

	std::string X11Window::GetClipboardText() {
		if (!m_impl->Alive()) return std::string();
		Display* dpy = m_impl->m_dpy;
		if (!dpy) return std::string();

		X11State& s = XS();
		const bool trace = TraceClipboard();

		if (m_impl->m_clipboardOwns && XGetSelectionOwner(dpy, s.clipboard) == m_impl->m_win)
			return m_impl->m_clipboardText;

		XDeleteProperty(dpy, m_impl->m_win, s.clipboardProp);
		XConvertSelection(dpy, s.clipboard, s.utf8String,
			s.clipboardProp, m_impl->m_win, CurrentTime);
		XFlush(dpy);

		const auto deadline = std::chrono::steady_clock::now() +
			std::chrono::milliseconds(kClipboardWaitMs);
		bool gotNotify = false;
		while (std::chrono::steady_clock::now() < deadline) {
			while (XPending(dpy) > 0) {
				XEvent ev{};
				XNextEvent(dpy, &ev);

				if (ev.type == SelectionNotify &&
					ev.xselection.requestor == m_impl->m_win &&
					ev.xselection.selection == s.clipboard) {
					gotNotify = true;

					if (ev.xselection.property == 0L) {
						if (trace) std::cerr << "[Zekit/clip] 所有者拒绝提供文本\n";
						return std::string();
					}

					std::string out;
					Atom type = 0L;
					if (!ReadStringProperty(dpy, m_impl->m_win, ev.xselection.property, out, type))
						return std::string();

					if (type == s.incrAtom) {
						out.clear();
						const auto incrDeadline = std::chrono::steady_clock::now() +
							std::chrono::milliseconds(kClipboardIncrMs);
						while (std::chrono::steady_clock::now() < incrDeadline) {
							if (!WaitForPropertyNotify(dpy, m_impl->m_win,
								ev.xselection.property, s.deferred, incrDeadline)) {
								break;
							}
							std::string chunk;
							Atom ct = 0L;
							if (!ReadStringProperty(dpy, m_impl->m_win,
								ev.xselection.property, chunk, ct))
								break;
							if (chunk.empty()) break;
							out += chunk;
						}
					}

					if (trace)
						std::cerr << "[Zekit/clip] 从所有者取到 " << out.size() << " 字节\n";
					return out;
				}

				if (ev.type == SelectionRequest) {
					m_impl->AnswerSelectionRequest(ev.xselectionrequest);
					continue;
				}
				if (ev.type == SelectionClear) {
					m_impl->m_clipboardOwns = false;
					m_impl->m_clipboardText.clear();
					continue;
				}

				s.deferred.push_back(ev);
			}

			if (gotNotify) break;
			struct timeval nap{};
			nap.tv_usec = 1000;
			::select(0, nullptr, nullptr, nullptr, &nap);
			XFlush(dpy);
		}

		if (trace && !gotNotify)
			std::cerr << "[Zekit/clip] 等待所有者回应超时（" << kClipboardWaitMs
			<< "ms，对端未响应）\n";
		return std::string();
	}

	void X11Window::Impl::TranslateKey(XKeyEvent& ke, bool press) {
		bool isRepeat = false;

		if (press) {
			isRepeat = XS().autoRepeatSupported
				? (std::find(m_keysDown.begin(), m_keysDown.end(), ke.keycode) != m_keysDown.end())
				: m_synthRepeat;
			m_synthRepeat = false;
			if (std::find(m_keysDown.begin(), m_keysDown.end(), ke.keycode) == m_keysDown.end())
				m_keysDown.push_back(ke.keycode);
		} else {
			if (!XS().autoRepeatSupported && XPending(m_dpy) > 0) {
				XEvent next{};
				XPeekEvent(m_dpy, &next);
				if (next.type == KeyPress &&
					next.xkey.keycode == ke.keycode &&
					next.xkey.time == ke.time) {
					m_synthRepeat = true;
					return;
				}
			}
			const auto it = std::find(m_keysDown.begin(), m_keysDown.end(), ke.keycode);
			if (it != m_keysDown.end()) m_keysDown.erase(it);
		}

		KeySym ks = XkbKeycodeToKeysym(m_dpy, ke.keycode, 0, 0);
		if (ks == NoSymbol) ks = XLookupKeysym(&ke, 0);

		Event e;
		e.kind = press ? EventKind::KeyDown : EventKind::KeyUp;
		e.key = ToKey(ks);
		e.virtualKey = static_cast<uint32_t>(ks);
		e.scanCode = static_cast<uint32_t>(ke.keycode);
		e.isRepeat = isRepeat;
		e.mods = ModsFromState(ke.state);
		Emit(e);

		if (!press) return;

		Status status = 0;
		std::string text;
		if (m_ic) {
			char buf[512] = {};
			int len = Xutf8LookupString(m_ic, &ke, buf, static_cast<int>(sizeof(buf) - 1),
				nullptr, &status);
			if (status == XBufferOverflow && len > 0) {
				std::string big(static_cast<std::size_t>(len) + 1, '\0');
				len = Xutf8LookupString(m_ic, &ke, big.data(),
					static_cast<int>(big.size()), nullptr, &status);
				if (len > 0) text.assign(big.data(), static_cast<std::size_t>(len));
			} else if (len > 0) {
				text.assign(buf, static_cast<std::size_t>(len));
			}
		} else {
			char buf[512] = {};
			KeySym dummy = NoSymbol;
			const int len = XLookupString(&ke, buf, static_cast<int>(sizeof(buf) - 1),
				&dummy, nullptr);
			if (len > 0) text.assign(buf, static_cast<std::size_t>(len));
			status = XLookupChars;
		}
		if (text.empty()) return;
		if (status != XLookupChars && status != XLookupBoth) return;

		if ((ke.state & ControlMask) || (ke.state & Mod1Mask)) return;

		const auto* u = reinterpret_cast<const unsigned char*>(text.data());
		const int n = static_cast<int>(text.size());
		int i = 0;
		while (i < n) {
			const unsigned char c0 = u[i];
			uint32_t cp = 0;
			int extra = 0;
			if (c0 < 0x80)                 { cp = c0;        extra = 0; }
			else if ((c0 & 0xE0) == 0xC0)  { cp = c0 & 0x1F; extra = 1; }
			else if ((c0 & 0xF0) == 0xE0)  { cp = c0 & 0x0F; extra = 2; }
			else if ((c0 & 0xF8) == 0xF0)  { cp = c0 & 0x07; extra = 3; }
			else { ++i; continue; }

			if (i + extra >= n) break;
			bool ok = true;
			for (int k = 1; k <= extra; ++k) {
				if ((u[i + k] & 0xC0) != 0x80) { ok = false; break; }
				cp = (cp << 6) | (u[i + k] & 0x3F);
			}
			if (!ok) { ++i; continue; }
			i += extra + 1;

			Event ce;
			ce.kind = EventKind::CharInput;
			ce.codepoint = cp;
			ce.mods = ModsFromState(ke.state);
			Emit(ce);
		}
	}

	X11Window::X11Window() : m_impl(std::make_unique<Impl>()) {}
	X11Window::~X11Window() { Destroy(); }

	bool X11Window::Create(const WindowDesc& desc, IWindowHost& host) {
		const auto tCreate = std::chrono::steady_clock::now();
		if (!OpenDisplay()) return false;

		Display* dpy = XS().dpy;

		m_impl->m_dpy = dpy;
		m_impl->m_host = &host;
		m_impl->m_screen = DefaultScreen(dpy);

		const ::Window root = RootWindow(dpy, m_impl->m_screen);

		const bool isPopup = (desc.parent != nullptr) && (desc.relation == Relation::Popup);
		const bool isOwned = (desc.parent != nullptr) && (desc.relation == Relation::Owned);
		const bool isFree = (desc.parent != nullptr) && (desc.relation == Relation::Free);
		const bool isChild = (desc.parent != nullptr) && !isOwned && !isPopup && !isFree;
		m_impl->m_isOwned = isOwned || isPopup || isFree;
		m_impl->m_popup = isPopup;
		if (isChild) {
			m_impl->m_isChild = true;
			m_impl->m_parent = static_cast<::Window>(reinterpret_cast<uintptr_t>(desc.parent));

			const int cw = std::max(desc.width, 1);
			const int ch = std::max(desc.height, 1);

			XSetWindowAttributes cswa{};
			cswa.background_pixel = 0;
			cswa.border_pixel = 0;
			cswa.colormap = DefaultColormap(m_impl->m_dpy, m_impl->m_screen);
			cswa.bit_gravity = NorthWestGravity;
			cswa.event_mask =
				StructureNotifyMask |
				ExposureMask |
				KeyPressMask | KeyReleaseMask |
				ButtonPressMask | ButtonReleaseMask |
				PointerMotionMask |
				EnterWindowMask | LeaveWindowMask |
				FocusChangeMask |
				PropertyChangeMask |
				VisibilityChangeMask;

			constexpr unsigned long childMask =
				CWBackPixel | CWBorderPixel | CWColormap | CWBitGravity | CWEventMask;

			::Window cwin = XCreateWindow(m_impl->m_dpy, m_impl->m_parent,
				desc.x, desc.y,
				static_cast<unsigned int>(cw), static_cast<unsigned int>(ch),
				0, CopyFromParent, InputOutput, CopyFromParent, childMask, &cswa);
			if (!cwin) {
				std::cerr << "[Zekit] XCreateWindow(child) failed\n";
				CloseDisplayIfUnused();
				return false;
			}

			m_impl->m_win = cwin;
			m_impl->m_w = cw;
			m_impl->m_h = ch;
			m_impl->m_resizable = false;

			XS().windows[cwin] = m_impl.get();
			++XS().refs;

			XMapWindow(m_impl->m_dpy, cwin);
			XFlush(m_impl->m_dpy);
			return true;
		}

		int waX = 0, waY = 0, waW = 0, waH = 0;
		WorkArea(dpy, m_impl->m_screen, waX, waY, waW, waH);

		int w = desc.width;
		int h = desc.height;
		int x = 0;
		int y = 0;
		if (isOwned || isPopup || isFree) {
			const ::Window host = static_cast<::Window>(reinterpret_cast<uintptr_t>(desc.parent));
			::Window unused = 0;
			int hx = 0, hy = 0;
			if (XTranslateCoordinates(dpy, host, DefaultRootWindow(dpy), 0, 0, &hx, &hy, &unused)) {
				x = hx + desc.x;
				y = hy + desc.y;
			}
		}
		else {
			if (desc.centered) {
				w = static_cast<int>(waW * 0.53);
				h = static_cast<int>(waH * 0.53);
			}
			x = waX + (waW - w) / 2;
			y = waY + (waH - h) / 2;
		}
		w = std::max(w, 1);
		h = std::max(h, 1);

		std::string title = desc.title;
		if (title.empty()) title = ProgramName();

		XSetWindowAttributes swa{};
		swa.background_pixel = 0;
		swa.border_pixel = 0;
		swa.colormap = DefaultColormap(dpy, m_impl->m_screen);
		swa.bit_gravity = NorthWestGravity;
		swa.event_mask =
			StructureNotifyMask |
			ExposureMask |
			KeyPressMask | KeyReleaseMask |
			ButtonPressMask | ButtonReleaseMask |
			PointerMotionMask |
			EnterWindowMask | LeaveWindowMask |
			FocusChangeMask |
			PropertyChangeMask |
			VisibilityChangeMask;

		bool alphaVisual = false;
		Visual* vis = CopyFromParent;
		int depth = CopyFromParent;
		const bool wantsAlpha = desc.perPixelTransparent &&
			(isPopup || (desc.frameless && desc.parent == nullptr));
		if (wantsAlpha && PixelTransparentCapable(dpy, m_impl->m_screen)) {
			if (Visual* av = ArgbVisual(dpy, m_impl->m_screen)) {
				vis = av;
				depth = 32;
				swa.colormap = XCreateColormap(dpy, root, av, AllocNone);
				alphaVisual = true;
				swa.background_pixmap = 0L;
			}
		}

		unsigned long mask =
			CWBorderPixel | CWColormap | CWBitGravity | CWEventMask;
		mask |= alphaVisual ? CWBackPixmap : CWBackPixel;

		::Window win = XCreateWindow(dpy, root, x, y,
			static_cast<unsigned int>(w), static_cast<unsigned int>(h),
			0, depth, InputOutput, vis, mask, &swa);
		if (!win) {
			std::cerr << "[Zekit] XCreateWindow failed\n";
			CloseDisplayIfUnused();
			return false;
		}
		m_impl->m_alphaVisual = alphaVisual;

		m_impl->m_win = win;
		m_impl->m_w = w;
		m_impl->m_h = h;

		m_impl->NotePosition(x, y);
		XS().windows[win] = m_impl.get();
		++XS().refs;

		if (isFree && desc.parent) {
			m_impl->m_hostXid = static_cast<::Window>(reinterpret_cast<uintptr_t>(desc.parent));
		}
		if ((isOwned || isPopup) && desc.parent) {
			m_impl->m_hostXid = static_cast<::Window>(reinterpret_cast<uintptr_t>(desc.parent));
			XSetTransientForHint(dpy, win,
				static_cast<::Window>(reinterpret_cast<uintptr_t>(desc.parent)));
		}

		if (isPopup) {
			SetBorderless(true);
			SetTopmost(true);

			XWMHints* hints = XGetWMHints(dpy, win);
			if (!hints) hints = XAllocWMHints();
			if (hints) {
				hints->flags |= InputHint;
				hints->input = False;
				XSetWMHints(dpy, win, hints);
				XFree(hints);
			}

			{
				const Atom st[3] = {
					XS().netWmStateAbove,
					XS().netWmStateSkipTaskbar,
					XS().netWmStateSkipPager
				};
				XChangeProperty(dpy, win, XS().netWmState, XA_ATOM, 32, PropModeReplace,
					reinterpret_cast<const unsigned char*>(st), 3);
			}
		}

		Atom protocols[1] = { XS().wmDeleteWindow };
		XSetWMProtocols(dpy, win, protocols, 1);

		XClassHint cls{};
		std::string pname = ProgramName();
		cls.res_name = const_cast<char*>(pname.c_str());
		cls.res_class = const_cast<char*>(pname.c_str());
		XSetClassHint(dpy, win, &cls);

		{
			const long pid = static_cast<long>(::getpid());
			XChangeProperty(dpy, win, XS().netWmPid, XA_CARDINAL, 32, PropModeReplace,
				reinterpret_cast<const unsigned char*>(&pid), 1);
		}
		{
			const Atom t = isPopup ? XS().netWmWindowTypePopupMenu
			                       : XS().netWmWindowTypeNormal;
			XChangeProperty(dpy, win, XS().netWmWindowType, XA_ATOM, 32, PropModeReplace,
				reinterpret_cast<const unsigned char*>(&t), 1);
		}

		m_impl->SetUtf8Property(XS().netWmName, title);
		XStoreName(dpy, win, title.c_str());

		if (desc.frameless && desc.parent == nullptr) {
			m_impl->m_borderless = true;

			{
				const Atom st[3] = {
					XS().netWmStateAbove,
					XS().netWmStateSkipTaskbar,
					XS().netWmStateSkipPager
				};
				XChangeProperty(dpy, win, XS().netWmState, XA_ATOM, 32, PropModeReplace,
					reinterpret_cast<const unsigned char*>(st), 3);
			}
			if (XWMHints* hints = XGetWMHints(dpy, win)) {
				hints->flags |= InputHint;
				hints->input = False;
				XSetWMHints(dpy, win, hints);
				XFree(hints);
			}
			else if (XWMHints* fresh = XAllocWMHints()) {
				fresh->flags |= InputHint;
				fresh->input = False;
				XSetWMHints(dpy, win, fresh);
				XFree(fresh);
			}
		}

		if (!desc.resizable) m_impl->m_resizable = false;
		m_impl->ApplyAllHints();

		m_impl->m_dark = SystemPrefersDark();
		SetDarkTitleBar(m_impl->m_dark);

		WatchXSettings();

		if (XIM im = SharedInputMethod(dpy)) {
			m_impl->m_ic = XCreateIC(im,
				XNInputStyle, static_cast<XIMStyle>(XIMPreeditNothing | XIMStatusNothing),
				XNClientWindow, win,
				XNFocusWindow, win,
				NULL);
		}

		if (!desc.iconPath.empty()) SetIconFromFile(desc.iconPath);

		if (Plat::TraceLifecycle()) {
			std::cerr << "[Zekit/trace] ⓪ 原生窗口建立（含 XIC）耗时 "
				<< std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::steady_clock::now() - tCreate).count() << "ms\n";
		}

		XFlush(dpy);
		return true;
	}

	void X11Window::Show() {
		if (!m_impl->Alive()) return;
		XMapWindow(m_impl->m_dpy, m_impl->m_win);
		XSync(m_impl->m_dpy, False);
	}

	void X11Window::Hide() {
		if (!m_impl->Alive()) return;
		XUnmapWindow(m_impl->m_dpy, m_impl->m_win);
		XFlush(m_impl->m_dpy);
	}

	void X11Window::SetBackdropColor(unsigned int rgb) {
		if (!m_impl->Alive()) return;

		m_impl->m_backdrop = rgb;

		XSetWindowBackground(m_impl->m_dpy, m_impl->m_win,
			static_cast<unsigned long>(rgb));

		if (m_impl->m_mapped)
			XClearWindow(m_impl->m_dpy, m_impl->m_win);

		XFlush(m_impl->m_dpy);
	}

	void X11Window::SetImeSpot(float x, float lineTop, float lineBottom) {
		if (!m_impl->Alive() || !m_impl->m_ic) return;

		if (m_impl->m_spotValid &&
			std::fabs(m_impl->m_spotX - x) < 0.5f &&
			std::fabs(m_impl->m_spotTop - lineTop) < 0.5f &&
			std::fabs(m_impl->m_spotBottom - lineBottom) < 0.5f) {
			return;
		}
		m_impl->m_spotX = x;
		m_impl->m_spotTop = lineTop;
		m_impl->m_spotBottom = lineBottom;
		m_impl->m_spotValid = true;

		XPoint spot = { static_cast<short>(std::lround(x)),
		                static_cast<short>(std::lround(lineBottom)) };

		if (Plat::TraceLifecycle()) {
			std::cerr << "[Zekit/trace] 输入法锚点 -> (" << spot.x << "," << spot.y
				<< ")（行顶 " << static_cast<int>(std::lround(lineTop)) << "）\n";
		}

		XVaNestedList preedit = XVaCreateNestedList(0, XNSpotLocation, &spot, NULL);
		if (!preedit) return;

		char* failed = XSetICValues(m_impl->m_ic, XNPreeditAttributes, preedit, NULL);
		XFree(preedit);

		if (failed) {
			static bool warned = false;
			if (!warned) {
				warned = true;
				std::cerr << "[Zekit] XSetICValues(XNSpotLocation) failed: "
					<< failed << "; the IME candidate window may not follow the caret.\n";
			}
			XFree(failed);
			return;
		}
		XFlush(m_impl->m_dpy);
	}

	void X11Window::Destroy() {
		if (!m_impl) return;
		m_impl->DestroyNative();
		m_impl->m_host = nullptr;
		CloseDisplayIfUnused();
	}

	bool X11Window::DeliverDueWakeup() {
		for (auto& kv : XS().windows) {
			auto* im = static_cast<Impl*>(kv.second);
			if (!im || !im->m_wakeExpired) continue;

			im->m_wakeExpired = false;
			im->m_wakeDeadlineMs = 0;

			Event e;
			e.kind = EventKind::Redraw;
			if (im->m_host) im->m_host->OnPlatformEvent(e);
			return true;
		}
		return false;
	}

	bool X11Window::DeliverPendingRedraw() {
		for (auto& kv : XS().windows) {
			auto* im = static_cast<Impl*>(kv.second);
			if (!im || !im->m_redrawPending) continue;

			im->m_redrawPending = false;
			Event e;
			e.kind = EventKind::Redraw;
			if (im->m_host) im->m_host->OnPlatformEvent(e);
			return true;
		}
		return false;
	}

	int X11Window::ComputeWaitTimeoutMs(int requestedMs) {
		if (requestedMs >= 0) return requestedMs;

		bool synth = false;
		unsigned long long earliest = 0;
		for (auto& kv : XS().windows) {
			auto* im = static_cast<Impl*>(kv.second);
			if (!im) continue;
			if (im->m_redrawPending || im->m_wakeExpired) { synth = true; break; }
			if (im->m_wakeDeadlineMs != 0 &&
				(earliest == 0 || im->m_wakeDeadlineMs < earliest))
				earliest = im->m_wakeDeadlineMs;
		}

		if (synth) return 0;
		if (earliest == 0) return -1;

		const unsigned long long now = NowMs();
		if (earliest <= now) return 0;
		return static_cast<int>(std::min<unsigned long long>(earliest - now, INT_MAX));
	}

	void X11Window::MarkExpiredWakeups() {
		const unsigned long long now = NowMs();
		for (auto& kv : XS().windows) {
			auto* im = static_cast<Impl*>(kv.second);
			if (!im || im->m_wakeDeadlineMs == 0) continue;
			if (im->m_wakeDeadlineMs <= now) im->m_wakeExpired = true;
		}
	}

	bool X11Window::PollEvent() {
		if (XS().quitPending) {
			XS().quitPending = false;
			m_impl->m_exitCode = XS().exitCode;
			Event e;
			e.kind = EventKind::Quit;
			if (TraceLifecycle())
				std::cerr << "[Zekit/trace] PollEvent：投递 Quit（exitCode=" << m_impl->m_exitCode
				<< "）→ " << (XS().quitHost ? "宿主" : "**没有宿主，事件将被丢弃**") << "\n";
			IWindowHost* target = XS().quitHost;
			XS().quitHost = nullptr;
			if (target) target->OnPlatformEvent(e);
			return true;
		}

		if (DeliverDueWakeup()) return true;

		Display* dpy = XS().dpy;
		if (!dpy) return false;

		if (!XS().deferred.empty()) {
			XEvent d{};
			d = XS().deferred.front();
			XS().deferred.pop_front();
			auto dit = XS().windows.find(d.xany.window);
			if (dit != XS().windows.end()) {
				if (auto* dim = static_cast<Impl*>(dit->second))
					dim->HandleEvent(d);
			}
			return true;
		}

		if (XPending(dpy) <= 0)
			return DeliverPendingRedraw();

		XEvent ev{};
		XNextEvent(dpy, &ev);

		if (Plat::TraceTheme() &&
			ev.xany.window != 0 &&
			ev.xany.window != DefaultRootWindow(dpy) &&
			XS().windows.find(ev.xany.window) == XS().windows.end())
			std::cerr << "[Zekit/theme] ★外来事件 type=" << ev.type
			<< " window=0x" << std::hex << ev.xany.window << std::dec
			<< "（XFilterEvent 之前）\n";

		if (XFilterEvent(&ev, ev.xany.window)) {
			if (Plat::TraceTheme())
				std::cerr << "[Zekit/theme] ✗ XFilterEvent 吃掉了一条 type=" << ev.type
				<< " window=0x" << std::hex << ev.xany.window << std::dec << "\n";
			return true;
		}

		if (Plat::TraceTheme() && ev.type == PropertyNotify)
			std::cerr << "[Zekit/theme] PollEvent 收到 PropertyNotify window=0x"
			<< std::hex << ev.xproperty.window << std::dec
			<< " atom=" << static_cast<unsigned long>(ev.xproperty.atom)
			<< "  (xsWindow=0x" << std::hex << XS().xsWindow << std::dec
			<< " xsSettings=" << static_cast<unsigned long>(XS().xsSettingsAtom)
			<< " root=0x" << std::hex << static_cast<unsigned long>(DefaultRootWindow(dpy))
			<< std::dec << ")\n";

		auto it = XS().windows.find(ev.xany.window);
		if (it != XS().windows.end()) {
			if (auto* im = static_cast<Impl*>(it->second))
				im->HandleEvent(ev);
		}
		else if (ev.type == PropertyNotify &&
			((ev.xany.window == DefaultRootWindow(dpy) &&
			  ev.xproperty.atom == XS().xsSelectionAtom) ||
			 (XS().xsWindow != 0 &&
			  ev.xany.window == XS().xsWindow &&
			  ev.xproperty.atom == XS().xsSettingsAtom))) {
			std::vector<void*> all;
			all.reserve(XS().windows.size());
			for (const auto& kv : XS().windows) all.push_back(kv.second);
			for (void* p : all) {
				auto* im = static_cast<Impl*>(p);
				if (!im || XS().windows.find(im->m_win) == XS().windows.end()) continue;
				im->HandleEvent(ev);
			}
		}
		return true;
	}

	void X11Window::WaitEvents(int timeoutMs) {
		Display* dpy = XS().dpy;
		if (!dpy) {
			if (timeoutMs < 0 || timeoutMs > 1) {
				struct timeval nap{};
				nap.tv_usec = 1000;
				::select(0, nullptr, nullptr, nullptr, &nap);
			}
			return;
		}

		XFlush(dpy);

		if (XPending(dpy) > 0) return;

		const int effective = ComputeWaitTimeoutMs(timeoutMs);

		const int fd = XConnectionNumber(dpy);
		fd_set rfds;
		FD_ZERO(&rfds);
		FD_SET(fd, &rfds);

		struct timeval tv{};
		struct timeval* ptv = nullptr;
		if (effective >= 0) {
			tv.tv_sec = effective / 1000;
			tv.tv_usec = (effective % 1000) * 1000;
			ptv = &tv;
		}

		const int r = ::select(fd + 1, &rfds, nullptr, nullptr, ptv);
		if (r < 0 && errno != EINTR) {
			std::cerr << "[Zekit] select on X connection failed: "
				<< std::strerror(errno) << "\n";
			return;
		}

		if (r > 0) {
			XPending(dpy);
			return;
		}

		if (r == 0) MarkExpiredWakeups();
	}

	int X11Window::ExitCode() const { return m_impl->m_exitCode; }

	void X11Window::GetClientSize(int& w, int& h) const {
		w = 0; h = 0;
		if (!m_impl->Alive()) return;
		XWindowAttributes wa{};
		if (!XGetWindowAttributes(m_impl->m_dpy, m_impl->m_win, &wa)) return;
		w = wa.width;
		h = wa.height;
	}

	void X11Window::SetClientSize(int w, int h) {
		if (!m_impl->Alive()) return;
		w = std::max(w, 1);
		h = std::max(h, 1);

		if (m_impl->m_minW > 0) w = std::max(w, m_impl->m_minW);
		if (m_impl->m_minH > 0) h = std::max(h, m_impl->m_minH);
		if (m_impl->m_maxW > 0) w = std::min(w, m_impl->m_maxW);
		if (m_impl->m_maxH > 0) h = std::min(h, m_impl->m_maxH);

		const bool noPosition = (m_impl->m_isChild || m_impl->m_isOwned)
			|| !m_impl->m_centerOnResize
			|| m_impl->m_minimized || m_impl->m_maximized || m_impl->m_fullscreen;
		int waX = 0, waY = 0, waW = 0, waH = 0;
		WorkArea(m_impl->m_dpy, m_impl->m_screen, waX, waY, waW, waH);

		const int x = waX + (waW - w) / 2;
		const int y = waY + (waH - h) / 2;

		if (noPosition)
			XResizeWindow(m_impl->m_dpy, m_impl->m_win,
				static_cast<unsigned int>(w), static_cast<unsigned int>(h));
		else {
			XMoveResizeWindow(m_impl->m_dpy, m_impl->m_win, x, y,
				static_cast<unsigned int>(w), static_cast<unsigned int>(h));
			m_impl->NotePosition(x, y);
		}

		m_impl->m_w = w;
		m_impl->m_h = h;
		m_impl->ApplySizeHints();
		XFlush(m_impl->m_dpy);
	}

	void X11Window::SetCenterOnResize(bool on) { m_impl->m_centerOnResize = on; }

	void X11Window::SetPosition(int x, int y) {
		if (!m_impl->Alive()) return;

		if (m_impl->m_isChild) {
			XMoveWindow(m_impl->m_dpy, m_impl->m_win, x, y);
			XFlush(m_impl->m_dpy);
			return;
		}

		m_impl->NotePosition(x, y);
		XMoveWindow(m_impl->m_dpy, m_impl->m_win, x, y);
		m_impl->ApplySizeHints();
		XFlush(m_impl->m_dpy);
	}

	void X11Window::SetPositionRelativeToHost(int x, int y) {
		if (!m_impl->Alive()) return;

		if (m_impl->m_hostXid) {
			::Window unused = 0;
			int hx = 0, hy = 0;
			if (XTranslateCoordinates(m_impl->m_dpy, m_impl->m_hostXid,
				DefaultRootWindow(m_impl->m_dpy), 0, 0, &hx, &hy, &unused)) {
				x = hx + x;
				y = hy + y;
			}
		}
		SetPosition(x, y);
	}

	bool X11Window::ReleaseFromHost() {
		if (!m_impl->m_dpy || !m_impl->m_win) return false;

		if (m_impl->m_isChild) return false;
		if (m_impl->m_popup) return false;
		if (!m_impl->m_hostXid) return false;

		XDeleteProperty(m_impl->m_dpy, m_impl->m_win, XA_WM_TRANSIENT_FOR);

		m_impl->m_hostXid = 0;
		m_impl->m_isOwned = false;

		XMapRaised(m_impl->m_dpy, m_impl->m_win);
		m_impl->SendActiveWindow();
		XFlush(m_impl->m_dpy);
		return true;
	}

	void X11Window::CenterOnScreen() {
		if (!m_impl->Alive()) return;
		if (m_impl->m_minimized || m_impl->m_maximized || m_impl->m_fullscreen) return;

		if (m_impl->m_isChild) {
			XWindowAttributes pa{};
			XWindowAttributes ca{};
			if (!XGetWindowAttributes(m_impl->m_dpy, m_impl->m_parent, &pa)) return;
			if (!XGetWindowAttributes(m_impl->m_dpy, m_impl->m_win, &ca)) return;
			const int cx = std::max(0, (pa.width  - ca.width)  / 2);
			const int cy = std::max(0, (pa.height - ca.height) / 2);
			XMoveWindow(m_impl->m_dpy, m_impl->m_win, cx, cy);
			XFlush(m_impl->m_dpy);
			return;
		}

		int waX = 0, waY = 0, waW = 0, waH = 0;
		WorkArea(m_impl->m_dpy, m_impl->m_screen, waX, waY, waW, waH);

		XWindowAttributes wa{};
		if (!XGetWindowAttributes(m_impl->m_dpy, m_impl->m_win, &wa)) return;

		const int x = waX + (waW - wa.width) / 2;
		const int y = waY + (waH - wa.height) / 2;
		m_impl->NotePosition(x, y);
		XMoveWindow(m_impl->m_dpy, m_impl->m_win, x, y);
		m_impl->ApplySizeHints();
		XFlush(m_impl->m_dpy);
	}

	int X11Window::Dpi() const {
		if (!m_impl->Alive()) return 0;
		return ScreenDpi(m_impl->m_dpy, m_impl->m_screen);
	}

	float X11Window::ScaleFactor() const {
		const int dpi = Dpi();
		return dpi > 0 ? static_cast<float>(dpi) / 96.0f : 1.0f;
	}

	void X11Window::SetTitle(const std::string& utf8) {
		if (!m_impl->Alive() || utf8.empty()) return;
		m_impl->SetUtf8Property(XS().netWmName, utf8);
		XStoreName(m_impl->m_dpy, m_impl->m_win, utf8.c_str());
		XFlush(m_impl->m_dpy);
	}

	bool X11Window::IsClosed() const { return m_impl->m_win == 0; }

	bool X11Window::IsActive() const {
		return m_impl->m_active;
	}

	void X11Window::RequestRedraw() {
		m_impl->m_redrawPending = true;
	}

	void X11Window::RequestWakeup(int afterMs) {
		if (!m_impl->Alive()) return;

		const unsigned long long now = NowMs();
		const unsigned long long delay = afterMs > 0
			? static_cast<unsigned long long>(afterMs) : 1ULL;
		const unsigned long long deadline = now + delay;

		if (m_impl->m_wakeDeadlineMs != 0 && deadline >= m_impl->m_wakeDeadlineMs)
			return;

		m_impl->m_wakeDeadlineMs = deadline;
		m_impl->m_wakeExpired = false;

	}

	void X11Window::CancelWakeup() {
		m_impl->m_wakeDeadlineMs = 0;
		m_impl->m_wakeExpired = false;
	}

	void X11Window::SetTimerPrecision(bool ) {
	}

	void X11Window::PostClose() {
		if (!m_impl->Alive()) return;

		XEvent e{};
		e.xclient.type = ClientMessage;
		e.xclient.window = m_impl->m_win;
		e.xclient.message_type = XS().wmProtocols;
		e.xclient.format = 32;
		e.xclient.data.l[0] = static_cast<long>(XS().wmDeleteWindow);
		e.xclient.data.l[1] = CurrentTime;
		XSendEvent(m_impl->m_dpy, m_impl->m_win, False, NoEventMask, &e);
		XFlush(m_impl->m_dpy);
	}

	void X11Window::RequestQuit(int exitCode) {
		if (TraceLifecycle())
			std::cerr << "[Zekit/trace] RequestQuit(exitCode=" << exitCode
			<< ") → 置 quitPending，等 PollEvent 投递\n";
		XS().exitCode = exitCode;
		XS().quitPending = true;
	}

	void X11Window::StartWindowDrag() {
		if (!m_impl->Alive()) return;
		if (m_impl->m_minimized || m_impl->m_maximized || m_impl->m_fullscreen) return;
		if (m_impl->m_isChild) return;

		::Window root = RootWindow(m_impl->m_dpy, m_impl->m_screen);
		::Window child = 0;
		int rx = 0, ry = 0, wx = 0, wy = 0;
		unsigned int mask = 0;
		if (!XQueryPointer(m_impl->m_dpy, m_impl->m_win, &root, &child,
			&rx, &ry, &wx, &wy, &mask))
			return;

		XUngrabPointer(m_impl->m_dpy, CurrentTime);

		XEvent e{};
		e.xclient.type = ClientMessage;
		e.xclient.window = m_impl->m_win;
		e.xclient.message_type = XS().netWmMoveResize;
		e.xclient.format = 32;
		e.xclient.data.l[0] = rx;
		e.xclient.data.l[1] = ry;
		e.xclient.data.l[2] = 8;
		e.xclient.data.l[3] = 1;
		e.xclient.data.l[4] = 1;
		XSendEvent(m_impl->m_dpy, root, False,
			SubstructureNotifyMask | SubstructureRedirectMask, &e);
		XFlush(m_impl->m_dpy);

		if (TraceLifecycle())
			std::cerr << "[Zekit/trace] StartWindowDrag：已向 WM 发 _NET_WM_MOVERESIZE"
			"（root=" << rx << "," << ry << "，本客户端指针抓取已放开）\n";
	}

	bool X11Window::IsMouseButtonDown(MouseButton b) const {
		if (!m_impl->Alive()) return false;
		const unsigned int want = XButtonMask(b);
		if (!want) return false;

		::Window root = 0, child = 0;
		int rx = 0, ry = 0, wx = 0, wy = 0;
		unsigned int mask = 0;
		if (!XQueryPointer(m_impl->m_dpy, m_impl->m_win, &root, &child,
			&rx, &ry, &wx, &wy, &mask))
			return false;
		return (mask & want) != 0;
	}

	void* X11Window::NativeHandle() const {
		return reinterpret_cast<void*>(static_cast<uintptr_t>(m_impl->m_win));
	}

	bool SystemPrefersDark() {
		if (const char* theme = std::getenv("GTK_THEME")) {
			if (std::strstr(theme, "dark") || std::strstr(theme, "Dark")) {
				if (Plat::TraceTheme())
					std::cerr << "[Zekit/theme] 判据 ① GTK_THEME=" << theme << " → 深色\n";
				return true;
			}
		}

		if (const XsVerdict v = ReadXSettingsDark(XS().dpy, XS().xsWindow)) {
			if (Plat::TraceTheme())
				std::cerr << "[Zekit/theme] 判据 ② XSETTINGS → "
				<< (v > 0 ? "深色" : "浅色") << "\n";
			return v > 0;
		}

		const char* home = std::getenv("HOME");
		if (!home) {
			if (Plat::TraceTheme())
				std::cerr << "[Zekit/theme] 判据 ③ 没有 HOME，settings.ini 无从读起\n";
			return false;
		}

		const char* rel[] = {
			"/.config/gtk-4.0/settings.ini",
			"/.config/gtk-3.0/settings.ini",
		};
		for (const char* r : rel) {
			const std::string path = std::string(home) + r;
			std::FILE* f = std::fopen(path.c_str(), "rb");
			if (!f) continue;
			char line[512];
			while (std::fgets(line, sizeof(line), f)) {
				if (!std::strstr(line, "gtk-application-prefer-dark-theme")) continue;
				const char* eq = std::strchr(line, '=');
				if (!eq) continue;
				++eq;
				while (*eq == ' ' || *eq == '\t') ++eq;
				std::fclose(f);
				const bool dark = (*eq == '1' || *eq == 't' || *eq == 'T' ||
					*eq == 'y' || *eq == 'Y');
				if (Plat::TraceTheme())
					std::cerr << "[Zekit/theme] 判据 ③ " << path << " → "
					<< (dark ? "深色" : "浅色") << "\n";
				return dark;
			}
			std::fclose(f);
		}
		if (Plat::TraceTheme())
			std::cerr << "[Zekit/theme] 三级判据都没表态 → 按浅色。"
			"（无 XSETTINGS 守护 / 无 settings.ini / 无 GTK_THEME 时是常态；"
			"可用 GTK_THEME=Adwaita:dark 强制深色来单独验证换肤链路）\n";
		return false;
	}

	void X11Window::Minimize() {
		if (!m_impl->Alive()) return;
		if (!HasEwmhWm(m_impl->m_dpy)) WarnNoWindowManager("Minimize()");
		XIconifyWindow(m_impl->m_dpy, m_impl->m_win, m_impl->m_screen);
		XFlush(m_impl->m_dpy);
	}

	void X11Window::Maximize() {
		if (!m_impl->Alive()) return;
		if (!HasEwmhWm(m_impl->m_dpy)) WarnNoWindowManager("Maximize()");
		if (m_impl->m_fullscreen) SetFullscreen(false);
		if (m_impl->m_minimized) {
			m_impl->SendActiveWindow();
			m_impl->m_minimized = false;
		}
		m_impl->SendWmState(XS().netWmStateMaxHorz, XS().netWmStateMaxVert, 1);
		m_impl->m_maximized = true;
	}

	void X11Window::Restore() {
		if (!m_impl->Alive()) return;
		if (m_impl->m_fullscreen) { SetFullscreen(false); return; }

		if (m_impl->m_minimized) {
			m_impl->SendActiveWindow();
			m_impl->m_minimized = false;
		}
		if (m_impl->m_maximized) {
			m_impl->SendWmState(XS().netWmStateMaxHorz, XS().netWmStateMaxVert, 0);
			m_impl->m_maximized = false;
		}
		XFlush(m_impl->m_dpy);
	}

	bool X11Window::IsMinimized() const {
		if (!m_impl->Alive()) return false;
		m_impl->RefreshMinimized();
		return m_impl->m_minimized;
	}

	bool X11Window::IsMaximized() const {
		if (!m_impl->Alive()) return false;
		m_impl->RefreshMaximized();
		return m_impl->m_maximized;
	}

	void X11Window::SetTopmost(bool on) {
		if (!m_impl->Alive()) return;
		m_impl->SendWmState(XS().netWmStateAbove, 0, on ? 1 : 0);
	}

	void X11Window::SetResizable(bool on) {
		if (!m_impl->Alive()) return;
		if (m_impl->m_resizable == on) return;
		m_impl->m_resizable = on;
		m_impl->ApplySizeHints();
		m_impl->ApplyMotifHints();
		m_impl->ApplyAllowedActions();
	}

	void X11Window::SetSizeLimits(int minW, int minH, int maxW, int maxH) {
		if (!m_impl->Alive()) return;
		m_impl->m_minW = std::max(minW, 0);
		m_impl->m_minH = std::max(minH, 0);
		m_impl->m_maxW = std::max(maxW, 0);
		m_impl->m_maxH = std::max(maxH, 0);
		m_impl->ApplySizeHints();
	}

	void X11Window::SetBorderless(bool on) {
		if (!m_impl->Alive()) return;
		if (on == m_impl->m_borderless) return;
		m_impl->m_borderless = on;

		m_impl->ApplyMotifHints();
		m_impl->ApplyAllowedActions();
		XFlush(m_impl->m_dpy);
	}

	void X11Window::SetFullscreen(bool on) {
		if (!m_impl->Alive()) return;
		if (m_impl->m_minimized) return;
		if (on == m_impl->m_fullscreen) return;

		if (on) {
			XWindowAttributes wa{};
			int rx = 0, ry = 0;
			::Window child = 0;
			if (XGetWindowAttributes(m_impl->m_dpy, m_impl->m_win, &wa)) {
				m_impl->m_savedGeom.w = wa.width;
				m_impl->m_savedGeom.h = wa.height;
			}
			if (XTranslateCoordinates(m_impl->m_dpy, m_impl->m_win,
				RootWindow(m_impl->m_dpy, m_impl->m_screen), 0, 0, &rx, &ry, &child)) {
				m_impl->m_savedGeom.x = rx;
				m_impl->m_savedGeom.y = ry;
			}
			m_impl->m_fullscreen = true;
			m_impl->ApplySizeHints();
			m_impl->SendWmState(XS().netWmStateFullscreen, 0, 1);
		} else {
			m_impl->m_fullscreen = false;
			m_impl->SendWmState(XS().netWmStateFullscreen, 0, 0);
			const auto& g = m_impl->m_savedGeom;
			if (g.w > 0 && g.h > 0) {
				XMoveResizeWindow(m_impl->m_dpy, m_impl->m_win, g.x, g.y,
					static_cast<unsigned int>(g.w), static_cast<unsigned int>(g.h));
				m_impl->NotePosition(g.x, g.y);
			}
			m_impl->ApplyAllHints();
		}
		XFlush(m_impl->m_dpy);
	}

	void X11Window::ApplyTitleBar(bool titleBarVisible,
		bool minBtn, bool maxBtn, bool closeBtn) {
		m_impl->m_titleBarVisible = titleBarVisible;
		m_impl->m_minBtnEnabled = minBtn;
		m_impl->m_maxBtnEnabled = maxBtn;
		m_impl->m_closeBtnEnabled = closeBtn;
		m_impl->ApplyMotifHints();
		m_impl->ApplyAllowedActions();
	}

	void X11Window::SetOpacity(float alpha) {
		if (!m_impl->Alive()) return;
		alpha = std::clamp(alpha, 0.0f, 1.0f);

		if (alpha >= 1.0f) {
			XDeleteProperty(m_impl->m_dpy, m_impl->m_win, XS().netWmWindowOpacity);
			XFlush(m_impl->m_dpy);
			return;
		}
		const unsigned long v =
			static_cast<unsigned long>(alpha * 4294967295.0f + 0.5f);
		XChangeProperty(m_impl->m_dpy, m_impl->m_win, XS().netWmWindowOpacity,
			XA_CARDINAL, 32, PropModeReplace,
			reinterpret_cast<const unsigned char*>(&v), 1);
		XFlush(m_impl->m_dpy);
	}

	void X11Window::SetMouseCursor(Kit::Cursor c) {
		if (!m_impl->Alive()) return;
		Display* dpy = m_impl->m_dpy;

		struct Cache {
			Display* dpy = nullptr;
			::Cursor cur[9] = {};
		};
		static Cache cache;

		static const unsigned int glyph[9] = {
			XC_left_ptr,
			XC_hand2,
			XC_xterm,
			XC_X_cursor,
			XC_watch,
			XC_hand1,
			XC_fleur,
			XC_sb_h_double_arrow,
			XC_sb_v_double_arrow,
		};

		const int idx = static_cast<int>(c);
		if (idx < 0 || idx >= 9) return;

		if (cache.dpy != dpy) {
			cache = Cache{};
			cache.dpy = dpy;
		}
		if (!cache.cur[idx]) {
			cache.cur[idx] = XCreateFontCursor(dpy, glyph[idx]);
		}

		if (c == Kit::Cursor::Arrow) XUndefineCursor(dpy, m_impl->m_win);
		else                        XDefineCursor(dpy, m_impl->m_win, cache.cur[idx]);
		XFlush(dpy);
	}

	namespace {
		bool HasExternalFileDialog() {
			static const bool ok = [] {
				const char* pathEnv = ::getenv("PATH");
				if (!pathEnv) return false;
				for (const char* exe : { "zenity", "kdialog" }) {
					for (const char* p = pathEnv; *p; ) {
						const char* sep = std::strchr(p, ':');
						const std::size_t len = sep ? static_cast<std::size_t>(sep - p)
							: std::strlen(p);
						if (len > 0) {
							std::string cand(p, len);
							cand += '/';
							cand += exe;
							if (::access(cand.c_str(), X_OK) == 0) return true;
						}
						if (!sep) break;
						p = sep + 1;
					}
				}
				return false;
				}();
			return ok;
		}
	}

	Cap X11Window::Capabilities() const {
		Cap c = kDesktopCapabilities | Cap::PopupWindow;

		c |= Cap::FileDrop;

		if (HasExternalFileDialog()) c |= Cap::NativeFileDialog;

		c = Cleared(c, Cap::DarkTitleBar);

		if (Shape().ok) c |= Cap::ClickThrough;

		if (m_impl && m_impl->m_dpy) {
			if (PixelTransparentCapable(m_impl->m_dpy, m_impl->m_screen))
				c |= Cap::PerPixelTransparent;
		}
		return c;
	}

	void X11Window::SetPerPixelTransparent(bool on) {
		if (!m_impl->Alive()) return;
		if (on && !m_impl->m_alphaVisual) {
			static bool warned = false;
			if (!warned) {
				warned = true;
				std::cerr << "[Zekit] 逐像素透明没生效：这扇窗口不是用 32 位 ARGB visual 建的"
				             "（多半是本机没有合成器，或窗口在建窗时没有声明 perPixelTransparent）。"
				             "浮层会退回宿主自绘 —— 功能正常，只是压不住别的窗口。\n";
			}
		}
	}

	void X11Window::SetClickThrough(bool on) {
		if (!m_impl->Alive()) return;
		XShapeShim& sh = Shape();
		if (!sh.ok) return;

		Display* dpy = m_impl->m_dpy;
		if (on) {
			XRectangle none[1] = { {0, 0, 0, 0} };
			sh.CombineRectangles(dpy, m_impl->m_win, kShapeInput, 0, 0,
				none, 0, kShapeSet, kUnSorted);
		} else {
			sh.CombineMask(dpy, m_impl->m_win, kShapeInput, 0, 0, 0L, kShapeSet);
		}
		XFlush(dpy);
	}

	void X11Window::SetAcceptFileDrop(bool on) {
		if (!m_impl->Alive()) return;
		if (m_impl->m_acceptDrop == on) return;
		m_impl->m_acceptDrop = on;
		Display* dpy = m_impl->m_dpy;

		if (on) {
			const long version = 5;
			XChangeProperty(dpy, m_impl->m_win, XS().xdndAware, XA_ATOM, 32,
				PropModeReplace,
				reinterpret_cast<const unsigned char*>(&version), 1);
		}
		else {
			XDeleteProperty(dpy, m_impl->m_win, XS().xdndAware);
		}
		XFlush(dpy);
	}

	bool X11Window::ShowFileDialog(const FileDialogDesc& desc, std::vector<std::string>& out) {
		out.clear();
		if (!m_impl || !m_impl->Alive()) return false;
		Display* dpy = m_impl->m_dpy;
		if (!dpy) return false;

		const auto which = [](const char* exe) -> std::string {
			const char* pathEnv = ::getenv("PATH");
			if (!pathEnv) return {};
			for (const char* p = pathEnv; *p; ) {
				const char* sep = std::strchr(p, ':');
				const std::size_t len = sep ? static_cast<std::size_t>(sep - p)
					: std::strlen(p);
				if (len > 0) {
					std::string cand(p, len);
					cand += '/';
					cand += exe;
					if (::access(cand.c_str(), X_OK) == 0) return cand;
				}
				if (!sep) break;
				p = sep + 1;
			}
			return {};
			};

		const std::string zenity = which("zenity");
		const std::string kdialog = which("kdialog");
		if (zenity.empty() && kdialog.empty()) {
			std::cerr << "[Zekit] 文件对话框不可用：系统里没有 zenity / kdialog\n";
			return false;
		}

		std::vector<std::string> args;
		const bool isSave = (desc.mode == FileDialogDesc::Mode::Save);
		const bool isFolder = (desc.mode == FileDialogDesc::Mode::PickFolder);

		std::string start;
		if (!desc.initialDir.empty()) {
			start = desc.initialDir;
			if (!start.empty() && start.back() != '/') start += '/';
		}
		if (isSave && !desc.defaultName.empty()) start += desc.defaultName;

		if (!zenity.empty()) {
			args.push_back(zenity);
			args.push_back("--file-selection");
			if (isSave) {
				args.push_back("--save");
				if (desc.overwritePrompt) args.push_back("--confirm-overwrite");
			}
			if (isFolder) args.push_back("--directory");
			if (desc.allowMultiple && !isSave && !isFolder) args.push_back("--multiple");
			if (!start.empty()) { args.push_back("--filename"); args.push_back(start); }
			if (!desc.title.empty()) { args.push_back("--title"); args.push_back(desc.title); }
			for (std::size_t i = 0; i < desc.filterLabels.size()
				&& i < desc.filterPatterns.size(); ++i) {
				std::string f = desc.filterLabels[i] + " | ";
				const std::string& pats = desc.filterPatterns[i];
				for (std::size_t k = 0; k < pats.size(); ++k)
					f.push_back(pats[k] == ';' ? ' ' : pats[k]);
				args.push_back("--file-filter=" + f);
			}
			if (!desc.filterLabels.empty())
				args.push_back("--file-filter=所有文件 | *");
		}
		else {
			args.push_back(kdialog);
			if (!desc.title.empty()) { args.push_back("--title"); args.push_back(desc.title); }
			std::string filter;
			for (std::size_t i = 0; i < desc.filterLabels.size()
				&& i < desc.filterPatterns.size(); ++i) {
				std::string pats = desc.filterPatterns[i];
				for (std::size_t k = 0; k < pats.size(); ++k)
					if (pats[k] == ';') pats[k] = ' ';
				if (!filter.empty()) filter += "\\n";
				filter += pats + "|" + desc.filterLabels[i];
			}
			std::string startArg = start.empty() ? std::string(".") : start;
			if (isFolder) {
				args.push_back("--getexistingdirectory");
				args.push_back(startArg);
			}
			else if (isSave) {
				args.push_back("--getsavefilename");
				args.push_back(startArg);
				if (!filter.empty()) args.push_back(filter);
			}
			else {
				args.push_back("--getopenfilename");
				args.push_back(startArg);
				if (!filter.empty()) args.push_back(filter);
				if (desc.allowMultiple) {
					args.push_back("--multiple");
					args.push_back("--separate-output");
				}
			}
		}

		int fds[2] = { 0, 0 };
		if (::pipe(fds) != 0) return false;

		std::vector<char*> argv;
		argv.reserve(args.size() + 1);
		for (const std::string& a : args) argv.push_back(const_cast<char*>(a.c_str()));
		argv.push_back(nullptr);

		const pid_t pid = ::fork();
		if (pid < 0) {
			::close(fds[0]); ::close(fds[1]);
			return false;
		}
		if (pid == 0) {
			::close(fds[0]);
			::dup2(fds[1], STDOUT_FILENO);
			::close(fds[1]);
			::execvp(argv[0], argv.data());
			::_exit(127);
		}
		::close(fds[1]);

		std::string result;
		char buf[4096];
		for (;;) {
			const ssize_t n = ::read(fds[0], buf, sizeof buf);
			if (n <= 0) break;
			result.append(buf, static_cast<std::size_t>(n));
		}
		::close(fds[0]);

		int status = 0;
		::waitpid(pid, &status, 0);
		const bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0 && !result.empty();
		if (!ok) return false;

		std::size_t pos = 0;
		while (pos <= result.size()) {
			std::size_t end = result.find('\n', pos);
			if (end == std::string::npos) end = result.size();
			std::string line = result.substr(pos, end - pos);
			if (!line.empty() && line.back() == '\r') line.pop_back();
			if (!line.empty()) out.push_back(std::move(line));
			if (end == result.size()) break;
			pos = end + 1;
		}

		if (isSave && !out.empty() && !desc.defaultExt.empty()
			&& out[0].find('.') == std::string::npos) {
			out[0] += '.';
			out[0] += desc.defaultExt;
		}

		(void)dpy;
		return !out.empty();
	}

	namespace {
		bool ReadEwmhWorkArea(Display* dpy, int screen, int& x, int& y, int& w, int& h) {
			x = y = 0;
			w = h = 0;
			if (!dpy) return false;

			const ::Window root = RootWindow(dpy, screen);
			Atom type = 0L; int fmt = 0;
			unsigned long n = 0, after = 0;
			unsigned char* data = nullptr;
			bool ok = false;
			if (XGetWindowProperty(dpy, root, XS().netWmWorkarea, 0, 4L * 64L, False,
					XA_CARDINAL, &type, &fmt, &n, &after, &data) == Success
				&& data && type == XA_CARDINAL && fmt == 32 && n >= 4) {
				const unsigned long* v = reinterpret_cast<const unsigned long*>(data);
				const unsigned long desks = n / 4;

				unsigned long cur = 0;
				Atom t2 = 0L; int f2 = 0;
				unsigned long n2 = 0, a2 = 0;
				unsigned char* d2 = nullptr;
				if (XGetWindowProperty(dpy, root, XS().netCurrentDesktop, 0, 1, False,
						XA_CARDINAL, &t2, &f2, &n2, &a2, &d2) == Success && d2) {
					if (t2 == XA_CARDINAL && f2 == 32 && n2 >= 1)
						cur = reinterpret_cast<const unsigned long*>(d2)[0];
					XFree(d2);
				}
				if (cur >= desks) cur = 0;

				x = static_cast<int>(v[cur * 4 + 0]);
				y = static_cast<int>(v[cur * 4 + 1]);
				w = static_cast<int>(v[cur * 4 + 2]);
				h = static_cast<int>(v[cur * 4 + 3]);
				ok = (w > 0 && h > 0);
			}
			if (data) XFree(data);
			return ok;
		}
	}

	bool X11Window::GetScreenWorkArea(int& x, int& y, int& w, int& h) const {
		x = y = 0;
		w = h = 0;
		if (!m_impl) return false;
		Display* dpy = m_impl->m_dpy;
		if (!dpy) return false;

		if (ReadEwmhWorkArea(dpy, m_impl->m_screen, x, y, w, h)) return true;

		x = 0; y = 0;
		w = DisplayWidth(dpy, m_impl->m_screen);
		h = DisplayHeight(dpy, m_impl->m_screen);
		return false;
	}

	void X11Window::ClientOriginToScreen(int& x, int& y) const {
		if (!m_impl || !m_impl->Alive()) return;
		Display* dpy = m_impl->m_dpy;
		::Window child = 0;
		int rx = 0, ry = 0;
		if (XTranslateCoordinates(dpy, m_impl->m_win,
				RootWindow(dpy, m_impl->m_screen), 0, 0, &rx, &ry, &child)) {
			x = rx;
			y = ry;
		}
	}

	void X11Window::GetFrameInsets(int& left, int& top, int& right, int& bottom) const {
		left = top = right = bottom = 0;
		if (!m_impl || !m_impl->Alive()) return;
		Display* dpy = m_impl->m_dpy;

		Atom type = 0L; int fmt = 0;
		unsigned long n = 0, after = 0;
		unsigned char* data = nullptr;
		if (XGetWindowProperty(dpy, m_impl->m_win, XS().netFrameExtents, 0, 4, False,
				XA_CARDINAL, &type, &fmt, &n, &after, &data) == Success
			&& data && type == XA_CARDINAL && fmt == 32 && n >= 4) {
			const unsigned long* v = reinterpret_cast<const unsigned long*>(data);
			left   = static_cast<int>(v[0]);
			right  = static_cast<int>(v[1]);
			top    = static_cast<int>(v[2]);
			bottom = static_cast<int>(v[3]);
		}
		if (data) XFree(data);
	}

	void X11Window::SetEnabled(bool on) {
		if (!m_impl->Alive()) return;
		Display* dpy = m_impl->m_dpy;
		m_impl->m_disabled = !on;

		XWMHints* hints = XGetWMHints(dpy, m_impl->m_win);
		if (!hints) hints = XAllocWMHints();
		if (hints) {
			hints->flags |= InputHint;
			hints->input = on ? True : False;
			XSetWMHints(dpy, m_impl->m_win, hints);
			XFree(hints);
		}

		XFlush(dpy);
	}

	void X11Window::SetDarkTitleBar(bool dark) {

		if (!dark) {
			XDeleteProperty(m_impl->m_dpy, m_impl->m_win, XS().gtkThemeVariant);
		} else {
			const char* v = "dark";
			XChangeProperty(m_impl->m_dpy, m_impl->m_win, XS().gtkThemeVariant,
				XS().utf8String, 8, PropModeReplace,
				reinterpret_cast<const unsigned char*>(v), 4);
		}
		XFlush(m_impl->m_dpy);
	}

	void X11Window::SetIconFromFile(const std::string& utf8Path) {
		if (!m_impl->Alive() || utf8Path.empty()) return;

		int w = 0, h = 0, ch = 0;
		unsigned char* px = SOIL_load_image(utf8Path.c_str(), &w, &h, &ch, SOIL_LOAD_RGBA);
		if (!px || w <= 0 || h <= 0) {
			const char* why = SOIL_last_result();
			std::cerr << "[Zekit] SetWindowIcon failed: " << utf8Path
				<< " (" << (why ? why : "unknown") << ")\n";
			if (px) SOIL_free_image_data(px);
			return;
		}

		WriteNetWmIcon(m_impl->m_dpy, m_impl->m_win, XS().netWmIcon, px, w, h, utf8Path);
		SOIL_free_image_data(px);
	}

	void X11Window::SetIconFromResource(int resId) {
		if (!m_impl->Alive() || resId <= 0) return;

		const Kit::EmbeddedResource res = Kit::FindEmbeddedResource(resId);
		if (!res) {
			std::cerr << "[Zekit] SetWindowIconFromResource(" << resId << ") 失败："
				<< "该 ID 未登记（非 Windows 平台请先 Kit::RegisterEmbeddedResource）\n";
			return;
		}

		int w = 0, h = 0, ch = 0;
		unsigned char* px = SOIL_load_image_from_memory(
			static_cast<const unsigned char*>(res.data), static_cast<int>(res.size),
			&w, &h, &ch, SOIL_LOAD_RGBA);
		if (!px || w <= 0 || h <= 0) {
			const char* why = SOIL_last_result();
			std::cerr << "[Zekit] SetWindowIconFromResource(" << resId << ") 解码失败"
				<< " (" << (why ? why : "unknown") << ")\n";
			if (px) SOIL_free_image_data(px);
			return;
		}

		WriteNetWmIcon(m_impl->m_dpy, m_impl->m_win, XS().netWmIcon, px, w, h,
			"res:" + std::to_string(resId));
		SOIL_free_image_data(px);
	}

	void X11Window::SetIconFromHandle(void* hIcon) {
		(void)hIcon;
	}

	void* X11DisplayHandle() {
		return XS().dpy;
	}

	unsigned long X11WindowId(void* nativeHandle) {
		return static_cast<unsigned long>(reinterpret_cast<uintptr_t>(nativeHandle));
	}

	unsigned long X11VisualId(void* nativeHandle) {
		X11State& s = XS();
		if (!s.dpy || !nativeHandle) return 0;
		const ::Window win = static_cast<::Window>(reinterpret_cast<uintptr_t>(nativeHandle));
		XWindowAttributes wa{};
		if (!XGetWindowAttributes(s.dpy, win, &wa) || !wa.visual) return 0;

		if (wa.depth != 32) return 0;

		XVisualInfo tmpl{};
		tmpl.visualid = XVisualIDFromVisual(wa.visual);
		int n = 0;
		XVisualInfo* vi = XGetVisualInfo(s.dpy, VisualIDMask, &tmpl, &n);
		unsigned long id = 0;
		if (vi && n > 0) id = static_cast<unsigned long>(vi[0].visualid);
		if (vi) XFree(vi);
		return id;
	}

	namespace {

		bool ReadRootGeometry(DisplayGeometry& out) {
			Display* dpy = XS().dpy;
			if (!dpy) return false;

			const ::Window root = DefaultRootWindow(dpy);
			if (!root) return false;

			XWindowAttributes wa{};
			if (!XGetWindowAttributes(dpy, root, &wa)) return false;

			out.x = 0;
			out.y = 0;
			out.width  = wa.width;
			out.height = wa.height;

			int wx = 0, wy = 0, ww = 0, wh = 0;
			if (ReadEwmhWorkArea(dpy, DefaultScreen(dpy), wx, wy, ww, wh)) {
				out.workX = wx;
				out.workY = wy;
				out.workWidth  = ww;
				out.workHeight = wh;
			}
			else {
				out.workX = out.x;
				out.workY = out.y;
				out.workWidth  = out.width;
				out.workHeight = out.height;
			}

			out.primary = true;
			return out.width > 0 && out.height > 0;
		}

	}

	int QueryDisplays(DisplayGeometry* out, int maxCount) {
		DisplayGeometry g;
		if (!ReadRootGeometry(g)) return 0;

		if (out && maxCount > 0) out[0] = g;
		return 1;
	}

	bool QueryPrimaryDisplay(DisplayGeometry& out) {
		return ReadRootGeometry(out);
	}

}

#endif
