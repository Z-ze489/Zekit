#pragma once
// =====================================================================
//  ZeCapability.h - platform capability bits (public contract)
// =====================================================================
//
//  Why a separate header under public `include/`:
//   - Used in two places: public `Kit::Window::Supports()` and internal
//     `Plat::IWindow::Capabilities()`. One definition, one alias, so the
//     two tables cannot drift apart.
//   - No dependencies beyond the standard library, so keeping it public
//     does not leak internal implementation (same approach as `Rhi::Api`
//     in `ZeRenderBackend.h`).
//
//  Namespace is `Kit`, not `Plat`: apps write `Kit::Capability::...`, and
//  the internal Plat layer aliases it (see `ZePlatformWindow.h`).
//
//  When to add a capability bit:
//  only for something one platform has and another does not. Bits every
//  platform must have (e.g. `RequestRedraw`) do not belong here - this is
//  a difference list, not a feature switch list. Ask first: which
//  different code would a caller write? No answer, no bit.
// =====================================================================

#include <cstdint>

namespace Kit {

	// Platform capability bits (combinable with `|`).
	// Bit set: the capability is really delivered on this platform (calling
	// it has an effect). Bit clear: the interface is a degraded no-op (safe
	// to call, nothing happens, one note goes to stderr).
	// Query it, do not use `#ifdef`:
	//   if (win.Supports(Kit::Capability::WindowPosition)) win.SetPosition(...);
	enum class Capability : uint64_t {
		None = 0,

		// ---- Window geometry ----
		WindowPosition = 1ull << 0,   // Can place the window at a screen coordinate
		WindowResize   = 1ull << 1,   // Can change the client area size from code
		WindowCenter   = 1ull << 2,   // Can center in the work area (resize centers too)

		// ---- Window state ----
		WindowMinimize = 1ull << 3,
		WindowMaximize = 1ull << 4,
		WindowTopmost  = 1ull << 5,
		WindowOpacity  = 1ull << 6,   // Whole-window opacity (layered/composited)

		// ---- Decoration ----
		WindowTitle    = 1ull << 7,   // Has a visible title concept
		WindowIcon     = 1ull << 8,   // Has a window icon concept (taskbar / Alt-Tab)
		TitleBar       = 1ull << 9,   // System title bar show/hide and system buttons
		Borderless     = 1ull << 10,
		WindowDrag     = 1ull << 11,  // System-handled drag (for custom title bars)
		Fullscreen     = 1ull << 12,
		DarkTitleBar   = 1ull << 13,  // Title bar can follow light/dark mode

		// ---- Input: pointer ----
		InputMouse     = 1ull << 14,  // Has a mouse (including buttons)
		InputWheel     = 1ull << 15,  // Has a wheel / scroll delta
		InputHover     = 1ull << 16,  // Has hover (Move fires with a still pointer)
		InputPen       = 1ull << 17,  // Has a stylus

		// ---- Input: touch ----
		InputTouch     = 1ull << 18,  // Has touch (single finger)
		InputMultiTouch= 1ull << 19,  // Reports several fingers at once (with pointerId)

		// ---- Input: keyboard and IME ----
		InputKeyboard  = 1ull << 20,  // Has a physical keyboard (built-in or external)
		InputIme       = 1ull << 21,  // Has IME composition (preedit string / candidates)
		InputOnScreenKeyboard = 1ull << 22,  // Has an on-screen keyboard that can be raised/dismissed
		ImeSpot        = 1ull << 23,  // Can tell the IME where the caret is (candidate placement)

		// ---- Misc ----
		Clipboard      = 1ull << 24,
		SystemDarkMode = 1ull << 25,  // Can read the system light/dark preference
		AppLifecycle   = 1ull << 26,  // Receives foreground/background/low-memory/terminate notices
		SafeArea       = 1ull << 27,  // Has notch/rounded corners/gesture bar; needs insets

		// ---- Window structure ----
		// Native child window inside the parent's client area (desktop only).
		// This bit drives the automatic per-platform implementation of
		// `Kit::Window(parent, x, y, w, h, kind)`: a real OS window when set
		// (Win32/X11), a library-internal renderer surrogate when clear
		// (Android/Harmony). Both are the same interface, so business code
		// should not branch on it; only library dispatch and real functional
		// differences (e.g. dragging out of the main window) need it.
		ChildWindow    = 1ull << 28,

		// Borderless dependent window (popups: modal dialogs, drawers, message
		// boxes). Two levels: `ChildWindow` = dependent window with system
		// decoration, `PopupWindow` = dependent window without it, positioned
		// relative to the host and drawn by the library (mask, corners, shadow).
		// Win32 delivers it (`WS_POPUP` + `WS_EX_TOOLWINDOW` + `WS_EX_NOACTIVATE`);
		// X11 does not claim it yet (no-activate and notification type unwired);
		// mobile degrades to an in-canvas overlay, same path as `ChildWindow`.
		PopupWindow    = 1ull << 29,

		// Mouse click-through for the whole window: clicks land on the window
		// below, which one-way hints (toasts, corner badges) need. Widget-level
		// `HitTest` cannot do this, because a native window still captures the
		// mouse inside its rect. Win32: `WS_EX_TRANSPARENT` with `WS_EX_LAYERED`.
		// Only Win32 claims it (X11 would need `XShape` input shapes).
		ClickThrough   = 1ull << 30,

		// Accepting files dragged in from outside the window (drop target).
		// In-window drags are pure library coordinate math; file drops must be
		// delivered by the OS (Win32 `IDropTarget`/`WM_DROPFILES`, X11 XDND,
		// mobile share/drop entry points). Two levels, both promised: full
		// (Enter/Over/Leave/Drop) and degraded (Drop only). Win32 gives full,
		// or degraded when OLE is unavailable; X11 does not claim it yet.
		FileDrop       = 1ull << 31,

		// Native system file open/save/directory dialogs. Without it,
		// `Kit::FileDialog::Show*()` returns false and logs one line to stderr
		// instead of failing silently, since "nothing happened" is the hardest
		// thing to debug. Not reimplemented in-library: most of the value is
		// OS integration, and that is the app's product choice.
		NativeFileDialog = 1ull << 32,

		// Per-pixel transparency (only drawn pixels are visible). This is the
		// dividing line for using native popup windows for tooltips, dropdowns,
		// drawers, menus and toasts; without it they fall back to painting on
		// the host, clipped by the window bounds and unable to cover others.
		// Windows: `WS_EX_NOREDIRECTIONBITMAP` + composition swap chain (D3D12
		// only). X11: 32-bit ARGB visual plus a running compositor - a window
		// without one paints black, so the bit must be 0 there.
		PerPixelTransparent = 1ull << 33,
	};

	inline constexpr Capability operator|(Capability a, Capability b) {
		return static_cast<Capability>(
			static_cast<uint64_t>(a) | static_cast<uint64_t>(b));
	}
	inline constexpr Capability operator&(Capability a, Capability b) {
		return static_cast<Capability>(
			static_cast<uint64_t>(a) & static_cast<uint64_t>(b));
	}
	inline constexpr Capability& operator|=(Capability& a, Capability b) { a = a | b; return a; }
	inline constexpr Capability& operator&=(Capability& a, Capability b) { a = a & b; return a; }

	// Capability query: `Has(caps, Capability::InputTouch)`.
	inline constexpr bool Has(Capability v, Capability bit) {
		return (static_cast<uint64_t>(v) & static_cast<uint64_t>(bit)) != 0;
	}

	// Whether `v` fully contains every bit of `bits` (`Supports(a|b)` semantics).
	inline constexpr bool HasAll(Capability v, Capability bits) {
		return (static_cast<uint64_t>(v) & static_cast<uint64_t>(bits))
			== static_cast<uint64_t>(bits);
	}

	// Clear the given bits: `Cleared(caps, Capability::SafeArea)`.
	// Named because `enum class` cannot be negated directly (`caps & ~Cap::X`
	// does not compile). Platform adapters use it to strip bits they cannot
	// deliver from a baseline set.
	inline constexpr Capability Cleared(Capability v, Capability bits) {
		return static_cast<Capability>(
			static_cast<uint64_t>(v) & ~static_cast<uint64_t>(bits));
	}

	// Full capability set for desktop platforms with real movable windows
	// (Win32 / X11 directly), including native child windows - mobile has no
	// such concept. One constant so both backends need no copy; a platform
	// with real exceptions clears bits in its own `Capabilities()`.
	inline constexpr Capability kDesktopCapabilities =
		Capability::WindowPosition | Capability::WindowResize | Capability::WindowCenter |
		Capability::WindowMinimize | Capability::WindowMaximize |
		Capability::WindowTopmost | Capability::WindowOpacity |
		Capability::WindowTitle | Capability::WindowIcon | Capability::TitleBar |
		Capability::Borderless | Capability::WindowDrag | Capability::Fullscreen |
		Capability::DarkTitleBar |
		Capability::InputMouse | Capability::InputWheel | Capability::InputHover |
		Capability::InputPen | Capability::InputTouch | Capability::InputMultiTouch |
		Capability::InputKeyboard | Capability::InputIme | Capability::ImeSpot |
		Capability::Clipboard | Capability::SystemDarkMode |
		Capability::ChildWindow;

} // namespace Kit
