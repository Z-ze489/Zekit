#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <cstdlib>   // std::getenv (TraceLifecycle reads environment variables)

// Capability bits are defined in the public header (single source: the public API
// and the internal Plat layer share one table). See ZeCapability.h's header note
// on "why it lives in the public header".
#include "include/ZeCapability.h"
#include "include/ZeCursorKind.h"   // Kit::Cursor (SetMouseCursor's parameter; leaf enum)
#include "include/ZeFileDialog.h"   // Kit::FileDialogDesc (file dialog request description; shared pure data)

// =====================================================================
//  Plat — platform window abstraction (the first cross-platform pillar)
// =====================================================================
//  Layering contract (do not merge back):
//    The Plat layer only owns "the native window object exists": create / show /
//    destroy / size-position / title / native message extraction. It knows
//    NOTHING about widgets, renderers, or themes.
//    The Kit layer (Window::Impl) owns all business logic: widget tree, render
//    driver, input dispatch, focus, themes, filters. It talks to windows only
//    through this file's interfaces.
//  With this, one IWindow implementation per platform (Linux / macOS / Android)
//  is enough, and the Kit layer changes not a word.
//
//  Platform-specific code always uses compile-time conditions
//  (#if defined(_WIN32) ...), never runtime ifs: compiling #include <windows.h>
//  on Linux is a COMPILE failure, not something a runtime branch can save.
// =====================================================================

namespace Plat {

	// =============================== Capability bits ===============================
	//  Kit::Capability is defined in the PUBLIC header include/ZeCapability.h;
	//  this is just an alias. Why not let Plat define its own copy: the public
	//  Kit::Window::Supports() and the internal Plat::IWindow::Capabilities()
	//  query the SAME table — two definitions inevitably drift apart (and
	//  silently). Single source.
	//  This is the only place in this layer that references the Kit namespace,
	//  and its nature is SHARED PURE DATA, not "Plat knows Kit's business
	//  objects" — the boundary (Plat does not include Widget / Window) is
	//  untouched.
	using Cap = Kit::Capability;

	// Bitwise helpers come from Kit directly (the enum lives in namespace Kit;
	// ADL finds them).
	// Desktop backends' capability set is also aliased so Win32 / X11 can write
	// kDesktopCapabilities.
	inline constexpr Cap kDesktopCapabilities = Kit::kDesktopCapabilities;

	// ============================ Display geometry ============================
	//  Why this group is NOT on IWindow: "how big is the screen" is a
	//  PROCESS-LEVEL fact, independent of whether some window exists. The moment
	//  apps need it most is exactly when no window exists yet ("open a window at
	//  90% of the screen"). Hanging it on a window would force creating a window
	//  first just to ask — backwards.
	//  (The instance-side IWindow::GetScreenWorkArea solves a different problem:
	//  "which monitor does THIS window sit on" — in multi-monitor setups it must
	//  follow the window; the two are not interchangeable.)

	// Geometry of one display (PHYSICAL pixels; same coordinate system as
	// SetSize / the client area).
	//
	// Two sets: `bounds` and `work`:
	//   - bounds — the whole screen, including the taskbar / Dock strip;
	//   - work   — the work area: the region that can REALLY hold windows after
	//     subtracting taskbar / Dock / resident panels.
	//
	// Use bounds to "fill the screen", work to "not cover the taskbar". Most
	// windows should use work — this library's consistent default (CenterOnScreen
	// also computes by work).
	struct DisplayGeometry {
		int  x = 0;              // Whole-screen top-left (virtual desktop coords; may be negative — a monitor left of the primary)
		int  y = 0;
		int  width = 0;          // Whole-screen size
		int  height = 0;

		int  workX = 0;          // Work-area top-left
		int  workY = 0;
		int  workWidth = 0;      // Work-area size (taskbar / Dock subtracted)
		int  workHeight = 0;

		bool primary = false;    // Is the primary monitor (the one with the taskbar)
	};

	// Queries the primary display's geometry.
	// out: result written back (unchanged on failure). Returns true on success;
	// false = the platform has not implemented this capability.
	// Implementations live per platform (ZeWindowWin32.cpp / ZeWindowX11.cpp),
	// dispatched at compile time by ZeWindow.cpp; unimplemented platforms return
	// false — the same "capability bit" convention as IWindow::GetScreenWorkArea:
	// callers fall back themselves instead of failing to compile.
	bool QueryPrimaryDisplay(DisplayGeometry& out);

	// Enumerates all displays.
	// out: result array (may be nullptr to only count); maxCount: array capacity.
	// Returns the number of displays in the system (may exceed maxCount, in which
	// case only the first maxCount entries are written).
	// Return 0 = the platform does not support enumeration. Single-monitor
	// platforms return 1 and write one entry.
	int QueryDisplays(DisplayGeometry* out, int maxCount);

	// =============================== Events ===============================
	//  Platform-neutral input events. The Plat layer translates native messages
	//  (WM_* / XEvent / NSEvent) into these structs; the Kit layer only knows
	//  these structs, never platform types.
	//
	//  EventKind's values deliberately do NOT overlap with Win32 message numbers,
	//  preventing anyone from lazily using platform constants as this enum (which
	//  would break cross-platform behavior instantly).
	//  Event::key is an exception — it is a semantic key sharing the Kit::Key
	//  numbering (see include/ZeInputEvents.h), filled in by the platform layer
	//  after translation.
	enum class EventKind : uint32_t {
		None = 0,
		Quit,            // Window requests close (user clicked ✕ / system demands)
		Resize,          // Client size changed
		Redraw,          // System requests a redraw
		MouseMove,
		MouseLeave,      // Cursor left the client area
		MouseDown,
		MouseUp,
		MouseWheel,
		KeyDown,
		KeyUp,
		CharInput,       // Character after IME/case processing (UTF-32 codepoint)
		FocusIn,
		FocusOut,
		ImeStart,        // Composition started
		ImeCompose,      // Composition string / candidates changed
		ImeEnd,          // Composition ended
		ImeChar,
		ThemeChanged,    // System dark/light mode or other personalization changed
		Close,           // System says "this window should close" (WM_CLOSE semantics)

		// User clicked a system button; "whether it may act" is arbitrated by Kit.
		// The platform layer only does SEMANTIC recognition (this is ✕ /
		// minimize / maximize); whether the button is enabled is business state.
		// Host callback returning true swallows the action.
		// Which button: see Event::systemButton.
		SystemButtonGate,

		// ---------------- Pointer (unified channel for touch / stylus / mouse) ----------------
		// Why a separate group instead of merging into MouseDown/Move/Up:
		//  - The Mouse names carry desktop semantics (left/middle/right buttons,
		//    hover, wheel, cursor leaving) that touch lacks; merging would squeeze
		//    out "which finger";
		//  - Mobile sends ONLY Pointer*; desktop keeps sending Mouse* (Win32's
		//    Enter/Leave/SetCapture semantics lose information when forced into
		//    Pointer);
		//  - The two groups CONVERGE in the Kit layer (Kit::Window synthesizes the
		//    primary pointer into mouse events and reuses existing widget
		//    dispatch) — so all existing widgets work on touch with zero changes,
		//    while multi-finger widgets use Kit::TouchEvent separately.
		//  One PointerDown/Up reports only the CHANGED pointer (pointerId), but
		//  Event also carries a snapshot of ALL currently pressed pointers (see
		//  pointers) so pinch-zoom widgets need not track finger counts themselves.
		PointerDown,
		PointerMove,     // Any pointer moved (including drag after press)
		PointerUp,
		PointerCancel,   // System stole the gesture (incoming call / back gesture / app backgrounded)

		// The hosting window's native surface was replaced (Android's
		// TERM_WINDOW -> INIT_WINDOW).
		// Receiving it means ALL GPU resources bound to that surface are invalid
		// (Vulkan swapchain/surface, EGL window surface). Kit tears down and
		// rebuilds the whole render backend — exactly the "must rebuild entirely"
		// stated in ZeRenderBackendAndroid.cpp's header.
		// Desktop platforms never send it (window handles never change).
		SurfaceChanged,

		// On-screen IME (soft keyboard) visibility / occupied-height change.
		// The soft keyboard EATS part of the client area; the UI must dodge (push
		// the input box up). Occupied height comes from Event::height (0 = closed).
		// Not applicable on desktop (physical keyboards take no screen); sending
		// it is the platform's decision.
		SoftKeyboard,

		// App-level lifecycle change (mobile only). Which one: Event::lifecycle.
		AppLifecycleEvent,

		// ---------------- File drag-and-drop (Capability::FileDrop) ----------------
		// External files dragged into the window / moving within / dragged out /
		// released.
		//
		// The four phases SHARE one data channel (Event::files + Event::x/y);
		// only the timing differs:
		//  - FileDragEnter — first enters the window with files (one of the only
		//    phases guaranteed to have non-empty files)
		//  - FileDragOver  — still moving within the window (HIGH FREQUENCY,
		//    deliberately not repeating the file list / the only phase that may
		//    lack paths: coordinates only, so the UI can move its highlight)
		//  - FileDragLeave — left the window (files empty, coordinates meaningless)
		//  - FileDrop      — user released (files non-empty)
		//
		// Coordinates are CLIENT pixels (converted by the platform layer; same
		// coordinate system as mouse events).
		//
		// Platforms reporting only the last event (degraded tier, see
		// Capability::FileDrop) send only FileDrop — UI must not rely on "Enter
		// always arrives first".
		FileDragEnter,
		FileDragOver,
		FileDragLeave,
		FileDrop,

		// A background thread asks to wake the UI thread (IWindow::PostWake's
		// landing). No data, just a "time to wake up and handle things" hint.
		// Kit drains the cross-thread delivery queue (Window::Post) first, then
		// paints a frame.
		// Mobile / unwired platforms may skip it; Post then degrades to "handled
		// at the next natural wake" (no data loss, only delay).
		Wake,
	};

	// Pointer device type (with EventKind::Pointer*).
	enum class PointerDevice : uint32_t {
		Touch = 0,   // Finger
		Pen,         // Stylus (usually pressure-capable; not yet)
		Mouse,       // Mouse (desktop backends may also use the Pointer channel)
	};

	// App lifecycle state (with EventKind::AppLifecycleEvent).
	// Only mobile can be "suspended/killed by the system at any moment"; desktop
	// never sends these.
	enum class AppLifecycle : uint32_t {
		None = 0,
		Resumed,      // Back to foreground, got the window
		Paused,       // Backgrounded (may be killed at any time — time to save)
		LowMemory,    // System memory tight; release everything dispensable
		Terminating,  // About to exit
	};

	// Snapshot of one pointer (an element of Event::pointers).
	struct PointerState {
		uint32_t id = 0;        // Platform pointer id (Android: finger index, reused)
		float    x = 0.0f;      // Client pixels (already converted to content coords)
		float    y = 0.0f;
	};

	// System button (used with EventKind::SystemButtonGate).
	enum class SystemButton : uint32_t {
		None = 0,
		Minimize,
		Maximize,
		Close,
	};

	// Platform-neutral mouse buttons.
	enum class MouseButton : uint32_t {
		None = 0, Left, Right, Middle, X1, X2
	};

	// Modifier key bitmask.
	enum class Modifiers : uint32_t {
		None = 0,
		Shift   = 1u << 0,
		Control = 1u << 1,
		Alt     = 1u << 2,
		Super   = 1u << 3,   // Windows key / Command / Super
	};
	inline Modifiers operator|(Modifiers a, Modifiers b) {
		return static_cast<Modifiers>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
	}
	inline bool Has(Modifiers v, Modifiers bit) {
		return (static_cast<uint32_t>(v) & static_cast<uint32_t>(bit)) != 0;
	}

	// One pending native event (the only Plat -> Kit data channel).
	struct Event {
		EventKind     kind = EventKind::None;

		// ---- Mouse / size (client pixels) ----
		float         x = 0.0f;
		float         y = 0.0f;
		int           width = 0;
		int           height = 0;

		// ---- Mouse buttons / wheel ----
		MouseButton   button = MouseButton::None;
		int           wheelDelta = 0;      // 120 per notch, matching system convention

		// ---- Keyboard ----
		// SEMANTIC key (the platform layer's translation result, not a raw code).
		// The platform layer unifies X11 keysym / macOS keyCode / Win32 VK_* into
		// this value before passing it up. Hosts should not touch virtualKey.
		// Character events (CharInput / ImeChar) carry no key: this is 0 (None).
		uint32_t      key = 0;
		// Platform raw virtual key code (escape hatch; only needed when key is 0).
		// Cross-platform values are not guaranteed consistent; prefer key.
		uint32_t      virtualKey = 0;
		uint32_t      scanCode = 0;
		uint32_t      codepoint = 0;       // Character of CharInput / ImeChar
		bool          isRepeat = false;    // Long-press auto-repeat
		Modifiers     mods = Modifiers::None;

		// ---- System button gating (SystemButtonGate only) ----
		SystemButton  systemButton = SystemButton::None;

		// ---- Pointer (PointerDown / PointerMove / PointerUp / PointerCancel only) ----
		// Id of the pointer that CHANGED in this event.
		// x / y are also filled with its coordinates — single-pointer widgets only
		// need x/y; multi-pointer handling looks in pointers[].
		uint32_t      pointerId = 0;
		PointerDevice device = PointerDevice::Touch;

		// Snapshot of ALL pressed pointers at the moment of the event.
		// Lets widgets avoid tracking "how many fingers, where" themselves.
		// Length 10 = Android's de-facto limit on simultaneous touch points;
		// desktop mouse has only 1.
		// Filled only for Pointer* events; pointerCount is 0 otherwise.
		static constexpr uint32_t kMaxPointers = 10;
		uint32_t      pointerCount = 0;
		PointerState  pointers[kMaxPointers]{};

		// ---- App lifecycle (AppLifecycleEvent only) ----
		AppLifecycle  lifecycle = AppLifecycle::None;

		// ---- File drag-and-drop (FileDragEnter / FileDrop guaranteed non-empty) ----
		// Absolute paths of the dragged files / directories (UTF-8).
		//
		// Why stored by value here instead of letting Kit query the platform:
		//  - The platform layer receives SHORT-LIVED system structures
		//    (IDataObject is valid only during the Drop call and released after),
		//    so it must copy into its own storage right here;
		//  - Events are "one-shot snapshot" semantics anyway (same contract as the
		//    pointers snapshot).
		//
		// Cost: one vector construction per drop event — dragging is a
		// HUMAN-HAND-SPEED low-frequency event (at most dozens per second), not
		// comparable to mouse movement; zero-allocation optimization is not worth
		// it.
		std::vector<std::string> files;

		// ---- Platform-private extra data ----
		// Only when some platform's translation needs extra info (e.g. Win32's IME
		// lParam, X11's keysym). The Kit layer must not interpret it; pass it
		// through verbatim to IME callbacks.
		// Do NOT stash "raw message numbers for Kit to decode" here — that leaks
		// platform constants into the Kit layer. Native operations needing
		// business arbitration must be translated into semantic events in the
		// platform layer (see SystemButtonGate).
		uintptr_t     nativeData = 0;
		uintptr_t     nativeData2 = 0;
	};

	// =============================== Host callbacks ===============================
	//  Plat does not know Kit, so it cannot call Kit::Window directly. A pure
	//  virtual interface sits in between, implemented on the Kit side
	//  (Kit::Window::Impl); Plat only holds a reference to it.
	//  Dependency direction: Plat -> IWindowHost (this file) <- Kit implements.
	//  Plat never includes Kit.
	class IWindowHost {
	public:
		virtual ~IWindowHost() = default;

		// Handles one translated platform event.
		// Returns true = consumed (the host handled it); false = Plat may
		// continue default processing.
		virtual bool OnPlatformEvent(const Event& e) = 0;

		// Whether external file drops are accepted right now at (x,y) (CLIENT
		// coordinates).
		// Drop has two modes differing only in "what counts as the drop point":
		//  - AREA drop — only widgets that declared acceptance (and did not reject
		//    this batch of files) count;
		//  - WINDOW drop — the whole window counts (misses go to the window-level
		//    callback).
		//
		// The platform layer uses it to answer IDropTarget::DragEnter/DragOver's
		// *effect: returning false => DROPEFFECT_NONE => the system shows the
		// "forbidden" cursor, so the user knows without releasing. Platform layers
		// that do not implement this callback can only unconditionally report
		// "droppable" — exactly the old bug where area-drop showed "copyable"
		// outside the area (fixed 2026-10-30).
		//
		// Timing: must be asked AFTER OnPlatformEvent(FileDragEnter/Over) — the
		// drop point is resolved while the host processes that event.
		// Default returns false (old platforms become "accept nowhere" unchanged).
		virtual bool AcceptsFileDropAt(float x, float y) { (void)x; (void)y; return false; }

		// Whether there is PAINTED content at (x,y) (CLIENT coordinates).
		//
		// Per-pixel transparent windows (desktop widgets / ghost overlays) are
		// still a RECTANGLE to the system: when the mouse enters that rectangle,
		// the messages belong to it — even over transparent spots. The platform
		// layer uses this callback to implement fine-grained pass-through:
		//  - true  -> HTCLIENT (content here; interaction belongs to this window);
		//  - false -> HTTRANSPARENT (transparent here; events pass to the window
		//    below).
		//
		// This is the only way a widget can be "draggable on the opaque character
		// itself, click-through to what's behind over transparent areas".
		// The system-level WS_EX_TRANSPARENT is a WHOLE-WINDOW semantic and cannot
		// do this.
		//
		// Default returns TRUE (= whole window counts as content): old platforms
		// keep "whole window hittable" unchanged, bit-for-bit as before this
		// callback existed.
		// Implementations must be CHEAP: asked on every WM_NCHITTEST (i.e. every
		// mouse move). Reading GPU pixels here is forbidden (one readback = one
		// frame). Kit's implementation asks the widget tree's HitTest — pure
		// geometry.
		virtual bool IsOpaqueAt(float x, float y) { (void)x; (void)y; return true; }

		// Asks the host to paint one frame in place (WM_PAINT / resize / timer
		// top-ups). Why a callback instead of Kit polling: during border drags the
		// system enters a modal loop and Kit's message loop stops; only in-place
		// painting keeps the canvas live.
		//
		// reason — the ORIGIN of this "paint a frame" request; it decides whether
		// the host may coalesce it:
		//  - System    — system/input demanded a redraw. Coalescing is ALLOWED
		//    when content and size are unchanged (while dragging the border the
		//    system sends extra WM_PAINTs each step; painting them produces a
		//    bit-identical frame while costing an extra Present — one source of
		//    "picture jitters while resizing");
		//  - Scheduled — a TIMERED top-up frame fulfilled on the host's behalf
		//    (RequestWakeup due). Such frames always have a purpose (rebuilding a
		//    deferred swapchain / advancing animation / caret blinking) and must
		//    NEVER be coalesced: coalescing means "the promised frame never
		//    arrives".
		enum class RedrawReason {
			System,
			Scheduled,
		};
		virtual void OnRequestRedraw(RedrawReason reason = RedrawReason::System) = 0;

		// Notified before the window closes (the native handle is NOT destroyed yet).
		// Kit uses it to establish "handle about to become invalid" semantics
		// (IsClosed etc.) early.
		virtual void OnNativeClosed() = 0;

		// Reports the window's size constraints (Plat clamps during drags).
		// minW,minH,maxW,maxH are outputs; 0 = unlimited.
		virtual void QuerySizeLimits(int& minW, int& minH, int& maxW, int& maxH) const = 0;

		// System settings changed (theme / personalization).
		virtual void OnSystemSettingsChanged() = 0;

		// The host window's GEOMETRY changed (moved / resized / programmatic
		// SetWindowPos).
		// It is separate from OnRequestRedraw for one reason: it MUST be able to
		// arrive inside a modal loop.
		//
		// When Windows drags the title bar / border it enters the system's modal
		// loop (WM_ENTERSIZEMOVE ... WM_EXITSIZEMOVE) and Kit's own message loop
		// NEVER runs — so approaches like "Kit timer polls host position" fail
		// completely during that window, and the user sees "overlay pinned in
		// place while the window moved". But the system modal loop DOES keep
		// dispatching WM_WINDOWPOSCHANGED to the window procedure, so the
		// notification must come from the message side.
		//
		// Semantics:
		//  - Raised only when the FRAME position or CLIENT size actually changed
		//    (the platform layer debounces);
		//  - At raise time the window position is fully updated;
		//    GetClientOrigin() / GetClientSize() immediately return new values;
		//  - Unrelated to repainting — whether to paint goes through
		//    OnRequestRedraw.
		//
		// Default is a no-op: only windows that "have something following the host"
		// (overlay carrier hosts) need it; other platforms compile unchanged.
		virtual void OnHostGeometryChanged() {}
	};

	// =============================== Descriptors ===============================
	// The RELATIONSHIP between this window and the window pointed to by `parent`
	// (meaningful only when parent != nullptr).
	//
	// All three are "a window with a host handle", but their NATURES are entirely
	// different — mixing them up is the easiest mistake in this kind of code
	// (style bits, coordinate systems, dragging, centering reference frames all
	// differ):
	//
	// |                                          | Child                     | Owned                                | Popup                                  | Free                             |
	// |------------------------------------------|---------------------------|--------------------------------------|----------------------------------------|----------------------------------|
	// | Win32 style                              | WS_CHILD                  | WS_OVERLAPPEDWINDOW given in full    | WS_POPUP (NO system decoration)        | Bit-identical to Owned           |
	// | Title bar / sys menu / min-max / resize  | None at all (no NC area)  | All present (system-provided)        | None (self-paint if wanted)            | All present (system-provided)    |
	// | Clipped by host client area              | Yes                       | No (may cover beyond the host)       | No (may cover beyond the host)         | No                               |
	// | Drag title / border to move-resize       | Caller-driven only        | System-supported directly            | Caller-driven only                     | System-supported directly        |
	// | Taskbar                                  | Absent                    | See per-platform style comments      | Absent (WS_EX_TOOLWINDOW)              | See per-platform style comments  |
	// | Steals focus                             | No                        | Yes                                  | No (WS_EX_NOACTIVATE)                  | Yes                              |
	// | Can sink BELOW the host in z-order       | (a child window, no z)    | No (owner invariant, see below)      | No                                     | Yes                              |
	// | Lifetime                                 | Destroyed with host       | Same (owner semantics)               | Same (owner semantics)                 | Same (explicitly destroyed by the LIBRARY) |
	//
	// Child = "embedded", Owned = "a normal window following the host", Popup =
	// "an undecorated owned overlay", Free = "the same normal window but not
	// owned". The first suits "a second 3D viewport" embedded in the host UI (it
	// has no border at all); the second is what a child window / dialog should be
	// — a normal window with all decoration from the system; the third is what
	// notification bars / frameless overlays should be (decoration self-painted),
	// matching Capability::PopupWindow; the fourth is a floating window that
	// "wants to be covered by the main window" (tool panels / modeless inspectors).
	//
	// Free is NOT "a more liberal Owned" — it has different z-order semantics,
	// and the difference is the owner: Win32 has a hard rule "an owned window is
	// always above its owner", and SetWindowPos(HWND_BOTTOM) cannot sink it (the
	// system re-orders it above the owner). So "can it sink below the host" is
	// FIXED AT CREATION TIME; calling z-order APIs afterwards is ineffective — to
	// sink, there must be no owner from the start. Cost: without an owner there
	// is no "minimize together with the host" system behavior (see per-platform
	// comments).
	//
	// Never add WS_EX_TOOLWINDOW to Owned: that is the "tool window" style; it
	// removes min/max buttons and excludes Alt-Tab — exactly removing what a
	// "normal window" should have (burned once).
	enum class Relation {
		Child,   // Embedded child window: WS_CHILD, no title bar, clipped by the host client area
		Owned,   // Owned normal window: decoration from the system, ALWAYS above the host, destroyed with it
		Popup,   // Undecorated owned overlay: no focus stealing, not in Alt-Tab, destroyed with the host
		Free,    // Owner-less normal window: free z-order (may be covered by the host); coordinates still relative to the host client area
	};

	// Window creation parameters shared by all platforms.
	struct WindowDesc {
		std::string title;      // UTF-8 (unified across platforms; never wchar_t)
		std::string iconPath;   // UTF-8; empty = no icon
		int  width = 800;
		int  height = 600;
		bool resizable = true;
		bool centered = true;   // Centered by screen ratio (Zekit's existing behavior: 53% of screen width)

		// ---------------- Windows with a host (desktop platforms only) ----------------
		// Native handle of the host window; nullptr = top-level window.
		//
		// Non-null means this window is a DEPENDENT of the host window; its nature
		// follows `relation`:
		//   - Child — a block embedded in the host client area (frameless, no
		//     title bar); position from x/y (relative to the host client area's
		//     top-left), moves with the host / clipped by the host / not in the
		//     taskbar;
		//   - Owned — a NORMAL window (all decoration from the system); position
		//     from x/y (frame top-left, relative to the host client area),
		//     destroyed with the host, not in the taskbar;
		//   - Popup — an UNDECORATED dependent overlay (WS_POPUP: no title bar /
		//     border / system menu; self-paint decoration if wanted); position also
		//     relative to the host client area, destroyed with the host, not in the
		//     taskbar, and does NOT steal focus (showing or clicking never pulls
		//     foreground focus from the host);
		//   - Free — looks exactly like Owned but has NO owner: free z-order (when
		//     the host activates, it falls behind it); coordinates still given
		//     relative to the host client area — so `parent` here is a GEOMETRIC
		//     REFERENCE, not a z-order parent. Platform implementations must NOT
		//     set it as the OS-level owner / transient (that is exactly what it
		//     escapes).
		//
		// All four ignore `centered` (that is the top-level "center by screen
		// ratio" semantic).
		//
		// Mobile has no such concept (one Activity = one Surface on Android/iOS):
		// Kit::Window's parent-child construction degrades to a LIBRARY-INTERNAL
		// renderer stand-in (see Zekit/ZeSubWindow.h) — platform nature, not a
		// design choice.
		// The handle type is platform-private (Windows = HWND) — to keep
		// <windows.h> out of this header it travels as void* and each platform
		// implementation converts it.
		void* parent = nullptr;

		// Position relative to the parent's client area (meaningful only when
		// parent != nullptr).
		int  x = 0;
		int  y = 0;

		// Relationship with parent (see Relation).
		// Defaults to Child — bit-for-bit the behavior before this field existed
		// (then parent != nullptr already meant WS_CHILD), so existing callers are
		// unaffected.
		Relation relation = Relation::Child;

		// Request PER-PIXEL TRANSPARENCY at creation (no redirection surface).
		//
		// On Windows, WS_EX_NOREDIRECTIONBITMAP can ONLY be specified at
		// CreateWindowExW time — a later SetWindowLongPtr is not guaranteed to
		// work (the DWM window surface already exists). So this intent must enter
		// the creation flow with WindowDesc, not be patched after backend Init.
		//
		// The caller (Kit::Window's overlay carrier) sets true only when it KNOWS
		// the backend uses a composition swapchain — currently the criterion is
		// "the requested backend is D3D12" (CreateSwapChainForComposition +
		// DirectComposition is its exclusive path). GL has no composable alpha
		// channel; setting true would make the window vanish entirely.
		// Default false = no behavior change at all.
		bool perPixelTransparent = false;

		// Frameless top-level window (WS_POPUP + tool-window styles; desktop
		// widgets / floating balls).
		//
		// The ONLY difference from relation == Popup: it has NO HOST — it follows
		// no window, may sit anywhere on screen (including outside the target
		// window), and does not vanish when the host minimizes. Desktop pet widgets
		// are exactly this form — "part of the desktop", not an appendage of some
		// window.
		//
		// Styles (Win32): WS_POPUP | WS_CLIPCHILDREN +
		// WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE
		// (not in taskbar / not in Alt-Tab / no focus stealing — a widget should
		// have none of that presence).
		// Meaningful only for TOP-LEVEL windows (parent == nullptr); child windows
		// ignore it.
		bool frameless = false;

		// Undecorated overlays WITHOUT the system drop shadow (canvas / video
		// layer regions handed to an external renderer).
		//
		// Relation::Popup on Windows uses a window class with CS_DROPSHADOW, so
		// there is always a soft shadow ring outside the window rect — right for a
		// NOTIFICATION BAR (it should "float"), disastrous for a CANVAS: the
		// canvas's content is the whole rect, and the shadow draws a dirty outline
		// around it; worse, it follows the CLIPPED window — inside a scroll view
		// half-scrolled, the shadow overlaps the content above, looking like "not
		// clipped cleanly".
		//
		// When true: create with the plain class WITHOUT CS_DROPSHADOW and also
		// call DisableSystemFrame (turning off DWM rounded corners + the 1-2px
		// border). Cost: this window has zero system decoration — draw your own
		// shadow if wanted (a canvas should not have one anyway).
		//
		// Same reasoning as perPixelTransparent: CS_DROPSHADOW is a WINDOW CLASS
		// style, choosable only at CreateWindowExW time, unchangeable afterwards —
		// so the intent must ride WindowDesc into the creation flow.
		// Default true = bit-for-bit the behavior before this field existed.
		bool systemShadow = true;
	};

	// =============================== File dialog ===============================
	//  The descriptor struct is defined in the PUBLIC header
	//  include/ZeFileDialog.h (Kit::FileDialogDesc); this is just an alias — same
	//  reason as Cap: the app side and platform side query the SAME field table;
	//  two definitions inevitably drift silently (a platform skipping one field
	//  produces no error anywhere).
	//  This is the second place in this layer referencing the Kit namespace, again
	//  SHARED PURE DATA, not "Plat knows Kit's business objects".
	using FileDialogDesc = Kit::FileDialogDesc;

	// =============================== Interface ===============================
	// Platform window interface — describes only "the native window object".
	// All methods must be called on the thread that created it (single UI thread
	// assumption).
	class IWindow {
	public:
		virtual ~IWindow() = default;

		// Creates the native window (call immediately after construction; false on
		// failure).
		virtual bool Create(const WindowDesc& desc, IWindowHost& host) = 0;

		// Which capabilities this platform ACTUALLY implemented (see Cap).
		//
		// This is the key to "one API, many platforms" being programmable: the
		// interface always exists and is always callable; differences live only in
		// this bitmask. Callers wanting to trim UI per platform query it — do NOT
		// use #if.
		//
		// Deliberately NOT given a default implementation: new platforms must
		// explicitly declare what they have; copying the desktop defaults to sneak
		// through is exactly the easiest mistake on mobile.
		// Unrelated to Create: capabilities are a PLATFORM property; asking before
		// Create works equally well.
		virtual Cap Capabilities() const = 0;

		virtual void Show() = 0;
		virtual void Hide() = 0;
		virtual void Destroy() = 0;

		// Pumps one pending native message, translates it, and calls the host.
		// Returns true = got one (the host may need to pump again); false = queue
		// empty. This is the message pump's ONLY entry; Kit's exec() loop calls
		// nothing else.
		virtual bool PollEvent() = 0;

		// Blocks until a message / timeout, then drains the queue.
		// timeoutMs: < 0 = wait forever; >= 0 = wait at most that long.
		// EventDriven mode passes -1 (sleep in the kernel, zero CPU); Immediate
		// mode passes the frame interval. Cross-platform equivalents:
		// Win32 = MsgWaitForMultipleObjects, X11 = poll, Cocoa =
		// nextEventMatchingMask.
		virtual void WaitEvents(int timeoutMs) = 0;

		// Exit code of the message loop when the window just closed (exec's return
		// value).
		virtual int  ExitCode() const = 0;

		// ---- Geometry ----
		virtual void GetClientSize(int& w, int& h) const = 0;
		virtual void SetClientSize(int w, int h) = 0;
		virtual void SetPosition(int x, int y) = 0;

		// Moves the window by HOST CLIENT AREA coordinates (Relation::Owned /
		// Relation::Popup only).
		//
		// Dependent window positions have two natural meanings, and they are TWO
		// COORDINATE SYSTEMS:
		//  - At creation (WindowDesc::x/y) — relative to the HOST CLIENT AREA's
		//    top-left. This is what callers think in ("pinned at the host's bottom
		//    right, inset 24px"), while the host client origin's screen position
		//    depends on title bar + border thickness, which Kit cannot compute;
		//  - SetPosition() — screen coordinates (frame top-left). Clear semantics
		//    for top-level windows, but applied to dependents it requires the
		//    caller to know where the host client area is on screen.
		//
		// Windows that "move with the host" (notification bars / follow panels)
		// need this one: the platform layer holds the host handle, one
		// ClientToScreen lands it (Win32: see Win32Window's implementation; X11's
		// dependent windows are already X child windows relative to the parent, so
		// both sides happen to share the semantics here).
		//
		// x,y: pixel position relative to the host CLIENT area's top-left (frame
		// top-left).
		// Default degrades to SetPosition(x, y) (screen coordinates) — for
		// platforms without a "host" concept (top-level / mobile stand-in) this is
		// the only reasonable fallback; no error.
		virtual void SetPositionRelativeToHost(int x, int y) { SetPosition(x, y); }

		virtual void CenterOnScreen() = 0;

		// Detaches the OS-level ownership from the host, promoting this window to
		// an independent top-level window.
		// Returns true = detached (this window no longer dies with the host);
		// false = this form cannot be detached.
		//
		// The requirement comes from "child window inherits the host"
		// (Kit::Window::SetInheritOnParentClose): when the host closes, windows it
		// owns are DESTROYED WITH IT — Win32's rule is "DestroyWindow also destroys
		// the windows it owns", and on X11 most WMs clean up transient windows too.
		// To let a child survive, that ownership must be stripped BEFORE the host
		// is destroyed.
		//
		// Per relation:
		//  - Owned — clear the OS-level owner (Win32: GWLP_HWNDPARENT = 0; X11:
		//    delete WM_TRANSIENT_FOR) and clear the "host handle" geometric
		//    reference;
		//  - Free — had no owner; just clear the geometric reference;
		//  - Popup / Child — return false. The former is the notification-bar form
		//    ("undecorated, no focus stealing"), contradictory as a main window;
		//    the latter is WS_CHILD / an X child window, PHYSICALLY part of the
		//    host, with no "independence" possible.
		//
		// On success the platform side must account for this window as a TOP-LEVEL
		// window: CenterOnScreen / GetScreenWorkArea no longer borrow the host's
		// handle, and programmatic resizes no longer apply the "dependents don't
		// center" guard (it is a top-level window now).
		//
		// Default returns false (capability-bit semantics): platforms without it
		// still compile, and the caller (Kit layer) honestly rejects the
		// inheritance, leaving no half-applied state.
		// Idempotent: already detached (or always top-level) returns false, no
		// error. See Kit::Window::SetInheritOnParentClose.
		virtual bool ReleaseFromHost() { return false; }

		// Returns the work area of the monitor this window is on (screen
		// coordinates, taskbar excluded).
		// x,y,w,h: work-area rect output (physical pixels). Returns true on
		// success; false = the platform lacks this capability (caller falls back).
		//
		// The requirement comes from screen-corner-anchored notification windows
		// (Kit::ToastNotifier): to land at "screen top-left / bottom-left /
		// top-right / bottom-right" it must know the work area. Before this, the
		// library's only "knows the screen" entry was CenterOnScreen(), which is an
		// ACTION not a QUERY — the notification window recomputes its position per
		// frame by its own size, and an action-type interface cannot serve that.
		//
		// Three contracts:
		//  - Query "the monitor this window is on", NOT the primary — in
		//    multi-monitor setups the notification should follow the host's
		//    monitor, so the implementation uses the host's HWND (using the
		//    dependent's own HWND picks the wrong monitor, because the overlay is
		//    often still at (0,0) before placement);
		//  - Returns the WORK AREA, not the full screen — a notification bar
		//    covering the taskbar is not a "notification", it is an occlusion;
		//  - Default returns false (capability-bit semantics): unimplemented
		//    platforms still compile; the caller (the notification window)
		//    degrades to "position relative to the host client area".
		virtual bool GetScreenWorkArea(int& x, int& y, int& w, int& h) const {
			(void)x; (void)y; (void)w; (void)h;
			return false;
		}

		// The client area origin's SCREEN coordinates (written to x / y).
		// The coordinate bridge for cross-window drags (dock panel tear-out): two
		// windows each have only their own client coordinates; pointing at the
		// same spot requires conversion through screen space.
		// Default is a no-op (x=y=0): on unimplemented platforms cross-window
		// dragging degrades to "drag within one window" — neither fails to compile
		// nor goes wrong.
		virtual void ClientOriginToScreen(int& x, int& y) const {
			(void)x; (void)y;
		}

		// Non-client area thickness (the ring the frame adds beyond the client
		// area). left,top,right,bottom: per-edge thickness in pixels.
		//
		// A dependent window's "(x, y)" is the FRAME's top-left, while actions
		// like "center" usually mean the CLIENT area (or the whole visible window).
		// The difference is exactly this ring — with a title bar (Relation::Owned)
		// top/bottom can differ by 30-40px; centering without conversion lands
		// visibly low.
		//
		// Users: Kit::Window::CenterInHost, and the "geometry handed to the
		// library" creation path (default lands at the host client area's center).
		// For WS_CHILD child windows this ring is always 0.
		//
		// Default writes 0 — platforms without a non-client concept (mobile
		// stand-in / frameless) SHOULD be 0; this is not "unimplemented".
		// Describes only SYSTEM-provided decoration, not DWM's invisible shadow
		// border (AdjustWindowRectEx's measure) — that is the compositor's visual
		// overflow and does not participate in window geometry.
		virtual void GetFrameInsets(int& left, int& top, int& right, int& bottom) const {
			left = top = right = bottom = 0;
		}

		// DPI scale factor (96 DPI baseline: 1.0 / 1.25 / 1.5 ...).
		virtual float ScaleFactor() const = 0;
		virtual int   Dpi() const = 0;

		// RENDER SCALE: how many PHYSICAL pixels one "Kit logical unit" equals.
		//
		// Not the same thing as ScaleFactor() above; neither can substitute the
		// other:
		//  - ScaleFactor() is the DPI ratio (96 baseline) — 1.25 / 1.5 on desktop
		//    high-DPI screens;
		//  - RenderScale() is the render pipeline's true "logical coords ->
		//    physical pixels" multiplier.
		//
		// Desktop (Win32 / X11): the client area is already measured in physical
		// pixels and the orthographic projection fills it, so logical 1 unit == 1
		// physical pixel => always 1.0 (even when ScaleFactor is 1.5).
		// Mobile (Android / iOS): UI laid out in LOGICAL pixels (dp), rendering in
		// physical pixels, so logical 1 unit == density physical pixels => equals
		// density (3.5 on a real 560dpi device). Exactly inverse to GetClientSize()'s
		// conversion (physical / density).
		//
		// User: GLYPH RASTERIZATION. Glyph bitmaps must be generated at the
		// physical size "logical font size x RenderScale" to land 1:1 on screen;
		// generating at the logical size and letting the projection magnify them
		// density-fold blurs every stroke — the root cause of "text looks slightly
		// blurry" on Android devices. Glyph quad geometry is simultaneously divided
		// by the multiplier back to logical units, so LAYOUT AND MEASUREMENT ARE
		// UNAFFECTED; only the texture resolution changes.
		//
		// Default 1.0 — desktop backends (including unimplemented-platform
		// fallbacks) need not override; only platforms where "logical coords !=
		// physical pixels" (mobile) must.
		virtual float RenderScale() const { return 1.0f; }

		// Whether this platform's windows are born fullscreen / immersive (system
		// bars hidden by default).
		//
		// Currently false on ALL platforms:
		//  - Desktop — windows have borders, do not fill the screen; the user
		//    clicks to go fullscreen.
		//  - Mobile — the window fills the screen by system, but SYSTEM BARS ARE
		//    VISIBLE BY DEFAULT (matching Android app convention: only games /
		//    video players immerse at startup). To immerse, the app calls
		//    SetFullscreen(true) (mobile = hide system bars).
		//
		// Why it exists: Kit records this state in m_fullscreen, which must match
		// the platform's INITIAL value — otherwise the first "toggle fullscreen"
		// click looks dead (the platform is already fullscreen while Kit thinks
		// windowed, so that call becomes an idempotent no-op). The hook is kept so
		// a future "born fullscreen" platform aligns without touching Kit.
		virtual bool StartsImmersive() const { return false; }

		// Pushes "size constraints" down to the platform layer (Kit's
		// SetMinimumSize / SetMaximumSize). minW,minH,maxW,maxH: client pixels;
		// 0 = unconstrained on that axis.
		//
		// Why this (PUSH) exists besides IWindowHost::QuerySizeLimits (PULL): the
		// two platforms' directions are opposite, and missing either leaks:
		//  - Win32 is PULL: constraints go into WM_GETMINMAXINFO, a message the
		//    SYSTEM initiates; the platform layer can only ask the host in
		//    response. Win32 therefore ignores this function (default empty).
		//  - X11 is PUSH: constraints are properties the CLIENT writes into
		//    WM_NORMAL_HINTS itself; there is no "system asks me" moment — with
		//    only the pull interface, X11's m_minW/m_maxW would stay 0 forever and
		//    ApplySizeHints()'s PMinSize/PMaxSize would never go out (verified:
		//    SetMinimumSize / SetMaximumSize were completely inert on Linux).
		//
		// Default is a no-op — platforms not caring about size constraints (and
		// Win32) need not override.
		virtual void SetSizeLimits(int minW, int minH, int maxW, int maxH) {
			(void)minW; (void)minH; (void)maxW; (void)maxH;
		}

		// Whether a programmatic resize (SetClientSize) also re-centers the window
		// within its work area at the new size.
		// on: default true (preserves existing behavior); false = resize in place.
		// Why the switch: the default "resize + recenter" on CONTINUOUS resizes
		// hauls the whole window around — the user sees not "the window grew" but
		// "the whole window jumping/jittering". Worse, there is no alternative
		// wording: SetSize followed by SetPosition corrects the position but that
		// is already a SECOND move (jump to center, then jump back), and the middle
		// jump is visible. "Resize in place" is only possible through this switch.
		virtual void SetCenterOnResize(bool on) { (void)on; }

		// ---- Window state (minimize / maximize / restore / topmost) ----
		virtual void Minimize() = 0;
		virtual void Maximize() = 0;
		virtual void Restore() = 0;
		virtual bool IsMinimized() const = 0;
		virtual bool IsMaximized() const = 0;
		virtual void SetTopmost(bool on) = 0;

		// ---- Appearance ----
		// Resizable via draggable borders? (translated into platform style bits)
		virtual void SetResizable(bool on) = 0;
		// Fullscreen (fills the current monitor; border and title bar removed).
		virtual void SetFullscreen(bool on) = 0;
		// Frameless (keeps size/position; removes only the system border).
		virtual void SetBorderless(bool on) = 0;
		// Allows edge/corner drag-resizing in frameless mode (default =
		// unsupported, no-op).
		// The switch matters only when frameless (SetBorderless(true)) — normal
		// windows already have a system border. A default implementation is
		// DELIBERATE: on mobile "frameless" is the norm and edge-drag-resize has no
		// semantics, so new platforms need not implement it explicitly (same
		// convention as GetScreenWorkArea defaulting false).
		// Windows implementation = WM_NCHITTEST 8px hit bands on the four edges /
		// four corners reporting HT* — resizing stays fully native (smooth,
		// clamped, arrow cursors).
		virtual void SetBorderlessResizable(bool on) { (void)on; }
		// Whether frameless resizing is currently enabled (query paired with
		// SetBorderlessResizable).
		virtual bool IsBorderlessResizable() const { return false; }
		// Sets whether the window's four corners are rounded (SYSTEM-level
		// rounding, Win11+ DWM; older systems / self-painted stand-ins silently
		// no-op).
		// Since Win11, DWM offers a per-window rounding property
		// (DWMWA_WINDOW_CORNER_PREFERENCE): the corners are clipped by the system,
		// the system shadow follows the rounding, and the edges are anti-aliased.
		// Normal bordered top-level windows are round by default on Win11;
		// FRAMELESS (WS_POPUP) windows default to SQUARE — self-painted title bars /
		// frameless apps / notification bars need this interface to turn it on.
		// Win10 and below lack the property -> give up silently (stay square, look
		// reverts to the old style, no error).
		// Default = no-op when unsupported (same convention as
		// SetBorderlessResizable): mobile "stand-in windows" have no native window
		// concept and X11 rounding needs WM + shape extension cooperation — both
		// take the default no-op; developer calls never crash.
		virtual void SetRoundedCorners(bool on) { (void)on; }
		// Whether window rounding is on (default false; unimplemented platforms
		// always false).
		virtual bool IsRoundedCorners() const { return false; }
		// Title bar visibility + system button switches (sent in one call; each
		// platform translates into its style bits).
		// titleBarVisible: title bar (with title text) shown or not.
		// minBtn,maxBtn,closeBtn: whether the three system buttons are enabled.
		virtual void ApplyTitleBar(bool titleBarVisible,
			bool minBtn, bool maxBtn, bool closeBtn) = 0;
		// Sets the window icon (from an image file at a UTF-8 path; empty path =
		// clear).
		virtual void SetIconFromFile(const std::string& utf8Path) = 0;
		// Sets the window icon (from the executable's icon resource id; only
		// Windows has resources; other platforms may ignore or map to their own
		// resource mechanism).
		virtual void SetIconFromResource(int resId) = 0;
		// Sets the window icon (native icon handle directly; nullptr = restore
		// default).
		// Escape hatch with no cross-platform semantics — prefer SetIconFromFile.
		virtual void SetIconFromHandle(void* hIcon) = 0;
		// Sets window opacity (0.0 fully transparent ~ 1.0 opaque).
		virtual void SetOpacity(float alpha) = 0;

		// PER-PIXEL TRANSPARENCY: the handed-over window gets no redirection
		// surface (the window body itself is invisible).
		//
		// The ONLY way to get "overlay shows content, not window", and a different
		// thing from SetOpacity (uniform whole-window alpha):
		//
		// |              | Mechanism                                       | Look                                        |
		// |--------------|-------------------------------------------------|---------------------------------------------|
		// | SetOpacity   | WS_EX_LAYERED + LWA_ALPHA                       | The whole rect REMAINS, just faded overall   |
		// | This         | WS_EX_NOREDIRECTIONBITMAP + composition swapchain (DComp) | Window body DOES NOT EXIST; only renderer pixels |
		//
		// Only allowed when "the backend is using a composition swapchain" (D3D12:
		// CreateSwapChainForComposition + DXGI_ALPHA_MODE_PREMULTIPLIED). The upper
		// layer must first confirm via IBackend::IsPerPixelTransparent() — without
		// that condition, removing the redirection surface shows NOTHING (GL's
		// default framebuffer has no composable alpha channel; swaps have nowhere
		// to land).
		// Mutually exclusive with WS_EX_LAYERED: the layered window's composition
		// input is its redirection surface; with both bits present DWM falls back
		// to the layered path and per-pixel alpha is ignored — exactly where
		// "content visible, window also visible" comes from.
		// Default = no-op (mobile / X11 use their own compositors; the switch is
		// unneeded).
		virtual void SetPerPixelTransparent(bool on) { (void)on; }

		// MOUSE PASS-THROUGH: this window stops receiving mouse; clicks land on
		// the window beneath it (0 = off).
		//
		// What one-way indicators (notification bars / screen badges) want: they
		// sit above the UI yet must not cost the widgets below a single click. The
		// widget-level HitTest-false only governs "how events are split INSIDE the
		// window"; it cannot change "who the system thinks this window is" — a
		// native window owns the messages whenever the mouse enters its rectangle,
		// regardless of its widgets.
		//
		// Default is a NO-OP — unsupported platforms (mobile stand-in / unwired
		// backends) keep their behavior; callers query Capability::ClickThrough
		// instead of #if.
		// One REAL coupling with SetOpacity on Win32: pass-through needs
		// WS_EX_LAYERED to be reliable, and SetOpacity(1.0f) used to strip that bit
		// ("opaque means no need for layering") — so while pass-through is on, the
		// layered bit is held BY pass-through itself and SetOpacity no longer
		// strips it (see Win32Window::SetOpacity).
		virtual void SetClickThrough(bool on) { (void)on; }

		// Title bar follows dark / light mode (platforms without system support
		// may ignore).
		virtual void SetDarkTitleBar(bool dark) = 0;

		// ENABLES / DISABLES the whole window (disabled = the system stops
		// dispatching mouse / keyboard to it).
		//
		// The landing mechanism for TRUE modality: when a modal dialog opens, the
		// HOST window is disabled (Win32 = EnableWindow(host, FALSE)) — the whole
		// host grays out, cannot be clicked, and accepts no keyboard — guaranteed
		// by the OS, no library-side event interception needed. The dialog itself
		// is owned (Relation::Owned), is NOT affected by the host's disabled state,
		// and keeps receiving input.
		//
		// on: false = disable, true = re-enable.
		// Default is a NO-OP — platforms without a "window enabled state" concept
		// (mobile self-painted stand-ins) keep their behavior; on those platforms
		// modality is guaranteed by other means (geometry claiming), so not
		// wiring this is not an error.
		// Idempotent: setting the same value repeatedly is harmless.
		virtual void SetEnabled(bool on) { (void)on; }

		// Sets the window's BACKDROP FILL COLOR (0xRRGGBB).
		//
		// It answers one question: "BEFORE the renderer paints the first frame,
		// who owns those pixels in the window?"
		//
		// If nobody owns them, the window background is UNDEFINED content — not
		// "blank", but whatever the system left there. On X11 the window shows THE
		// PIECE OF SCREEN THAT WAS THERE (as if the window photocopied what was
		// behind it); on Win32 a random garbage block. To the user: "the window
		// appears as a picture first, corrects itself later".
		//
		// So the platform layer must have a definite value to fill. The color
		// belongs to the THEME, not the platform, hence Kit pushes it down
		// (window construction + every theme change) and the platform just lands it.
		//
		// rgb: packed 0xRRGGBB (no alpha; the window background is a solid color).
		// Default is a no-op — platforms not caring (or not yet wired) keep their
		// behavior, but platforms with window-owned backgrounds (X11 / Win32)
		// SHOULD implement it.
		virtual void SetBackdropColor(unsigned int rgb) { (void)rgb; }

		// Tells the platform layer WHERE THE TEXT CURSOR IS (client logical
		// pixels).
		//
		// When the IME paints its candidate box / pinyin bar it does not guess the
		// position — the client must tell it. On X11 this channel is the XIC's
		// XNSpotLocation: fcitx / ibus read it to place the candidate window under
		// ALL preedit styles (the sole source under XIMPreeditPosition). If NEVER
		// set, they do not error out — they fall back to "the client area's bottom
		// left", which looks like "the candidate box at the very bottom of the
		// window, not under the cursor". That is why this method exists.
		//
		// Coordinate conventions (aligned with Win32's ImmSetCompositionWindow /
		// ImmSetCandidateWindow):
		//  - x          — the cursor's left end
		//  - lineBottom — the BOTTOM edge of the cursor's line. The IME places the
		//    candidate box BELOW this line and does not know the line height, so
		//    the bottom edge must be given (the top edge would make the box cover
		//    the text). Same as what GTK / Qt pass to XIM.
		//  - lineTop    — the cursor line's top edge (for IMEs wanting "exclude
		//    rectangles", like Win32's CFS_EXCLUDE)
		//
		// Platforms with real implementations: Win32 (IMM), X11 (XIM). Others use
		// the default empty implementation.
		// Call frequency can be high (the cursor may move every frame);
		// implementations MUST cache the last value themselves and skip the
		// delivery when unchanged — X11's XSetICValues is a SYNCHRONOUS round
		// trip; sending every frame turns the IME into a bottleneck.
		virtual void SetImeSpot(float x, float lineTop, float lineBottom) {
			(void)x; (void)lineTop; (void)lineBottom;
		}

		// Shows / hides the ON-SCREEN IME (soft keyboard).
		//
		// Not applicable on desktop (physical keyboards take no screen); the
		// default is a no-op and Capabilities() does not include
		// Cap::InputOnScreenKeyboard.
		//
		// Mobile semantics: the soft keyboard EATS part of the client area (on
		// Android adjustResize is the default — the window's content area really
		// shrinks). The platform layer must:
		//  - report the occupied height via EventKind::SoftKeyboard (Event::height)
		//    so the UI dodges and pushes the input box above the keyboard;
		//  - the keyboard showing/hiding also causes one size change, which is a
		//    separate EventKind::Resize.
		//
		// Who calls it: NOT the app (apps should not care). Kit calls
		// ShowSoftKeyboard(true) automatically when an InputBox gains focus and
		// false on blur — provided the capability bit includes
		// Cap::InputOnScreenKeyboard. Manual control (self-painted input, password
		// keyboard takeover) is when the app calls it itself.
		//
		// on: true = show; false = hide.
		// Default is a no-op (desktop / unimplemented platforms).
		virtual void ShowSoftKeyboard(bool on) { (void)on; }

		// Whether the on-screen IME is visible right now.
		// Default false. Do NOT infer it from "I called ShowSoftKeyboard(true)" —
		// the system may refuse (no window focus, user gesture dismissed, another
		// IME took over), so this state must be ASKED of the platform.
		virtual bool IsSoftKeyboardVisible() const { return false; }

		// Client area margins occupied by system UI (notch / corners / gesture bar
		// / status bar).
		//
		// Mobile only. Content fills the WHOLE screen (unlike desktop windows with
		// borders), so the top notch and bottom gesture bar really cover the
		// picture — the UI must dodge itself, or "the title gets a bite taken out"
		// / "buttons can't be clicked".
		//
		// left,top,right,bottom: outputs, how many LOGICAL pixels to yield per
		// edge. Default all 0 (desktop has no such concept); meaningful only when
		// Capabilities() includes Cap::SafeArea.
		virtual void GetSafeAreaInsets(float& left, float& top,
			float& right, float& bottom) const {
			left = top = right = bottom = 0.0f;
		}

		// Reads PLAIN TEXT from the system clipboard (UTF-8).
		//
		// Why in the PLATFORM layer, not the widget layer: the clipboard is an OS
		// concept (X11: the Selection protocol; Windows: CF_UNICODETEXT); the
		// widget layer should not know them. Contained here, InputBox's
		// Ctrl+C/V/X is ONE platform-neutral code path — no second clipboard
		// implementation per platform.
		//
		// SYNCHRONOUS semantics (on return, the text is either obtained or known
		// unobtainable). The X11 cost: when another process owns the content, an
		// XConvertSelection round trip is needed, and implementations must wait
		// with a BOUNDED limit (never wait forever), returning an empty string on
		// timeout. Not getting content is NOT an error: empty clipboard, unresponsive
		// owner, an image instead of text — all go through the normal
		// "return empty string" path.
		//
		// Returns the clipboard text (UTF-8); no text / no platform implementation
		// -> empty string.
		// Default returns an empty string — unwired platforms behave as "paste
		// yields nothing"; other editing features unaffected (matching the old
		// degraded behavior).
		virtual std::string GetClipboardText() { return std::string(); }

		// Writes plain text (UTF-8) into the system clipboard (replacing the old
		// content).
		//
		// X11 "writing" only TAKES OWNERSHIP of the Selection and remembers the
		// text — real data is delivered on demand when someone asks
		// (SelectionRequest), so:
		//  - Implementations must answer SelectionRequest inside the event loop
		//    (or the other side times out: "paste doesn't work in other programs");
		//  - When another program takes ownership (SelectionClear), drop the local
		//    copy, or you would serve STALE content you are no longer responsible
		//    for;
		//  - Content vanishes when the process exits (X11 has no system-level
		//    clipboard storage, unless a clipboard manager grabbed a copy before
		//    exit) — a platform fact, not an implementation flaw.
		//
		// utf8: the text to write (UTF-8). Empty string = clear the clipboard.
		// Default is a no-op.
		virtual void SetClipboardText(const std::string& utf8) { (void)utf8; }

		// Sets the mouse cursor shape (desktop capability; mobile has no cursor).
		//
		// The division of labor mirrors the clipboard: "which cursor to show" is
		// decided by the WIDGET layer (Widget::SetCursor, with Window forwarding it
		// here after the hit test on mouse moves); the platform layer only turns
		// that shape into a system call (Win32: SetCursor(LoadCursorW(...)); X11:
		// XDefineCursor; macOS: NSCursor).
		//
		// c: Kit::Cursor (the widget and platform layers SHARE one value set — no
		// secondary mapping; the mapping table itself is a drift source, same rule
		// as Kit::Capability / Kit::Key).
		// Default is a no-op — unwired platforms keep "cursor always an arrow",
		// same as before, breaking nothing else.
		virtual void SetMouseCursor(Kit::Cursor c) { (void)c; }

		// Makes this window ACCEPT external file drops (on / off).
		//
		// When off, the system never delivers drop events to this window at all —
		// so this is the "become a drop target" switch, not a "accept or not"
		// filter. Only windows with it on are eligible to receive
		// EventKind::FileDrag* / FileDrop.
		//
		// Implementation notes (Win32): RegisterDragDrop needs a live IDropTarget,
		// which depends on OLE initialization — the platform implementation does it
		// at Create; callers need not care. Without full drop capability, fall back
		// to DragAcceptFiles (reports Drop only), matching Capability::FileDrop's
		// degraded tier.
		//
		// on: true = accept; false = unregister (idempotent).
		// Default is a no-op — platforms without this capability still compile.
		virtual void SetAcceptFileDrop(bool on) { (void)on; }

		// Pops the SYSTEM-NATIVE file dialog (synchronous, blocks until the user
		// confirms / cancels).
		//
		// Synchronous is DELIBERATE: this is a modal conversation and the return
		// value IS "what the user picked"; making it async would shred one straight
		// line of logic into callback hell. It blocks the UI thread — exactly what
		// a modal dialog should do (the host is disabled via SetEnabled(false)
		// meanwhile).
		//
		// desc: the request (see FileDialogDesc).
		// out: selected absolute paths (UTF-8); multi-select follows the user's
		// picking order.
		// Returns true = confirmed (out has at least one entry); false = canceled /
		// failed / unsupported.
		// Default returns false (capability-bit semantics) — callers query
		// Capability::NativeFileDialog, not #if.
		virtual bool ShowFileDialog(const FileDialogDesc& desc, std::vector<std::string>& out) {
			(void)desc; (void)out;
			return false;
		}

		// Whether this platform can build the render backend BEFORE the window is
		// shown (GL/EGL context, Vulkan surface+swapchain, D3D12 swapchain).
		// This decides how long the "empty window period" at startup is: between
		// the window being visible and the first frame, the screen shows only the
		// backdrop color (see SetBackdropColor). If backend init can be squeezed
		// onto the "window not yet shown" side, the user never sees the wait — the
		// window appears already painted.
		//
		// Per-platform conclusions:
		//  - Win32: YES. Creating the swapchain / GL context after CreateWindow and
		//    before ShowWindow is standard practice; the window handle existing is
		//    enough (visibility not required).
		//  - X11: implemented as NO for now (map + XSync first, then the backend).
		//    This is a CONSERVATIVE value, NOT yet validated — a "build early"
		//    variant was tried in an isolated sandbox, but that environment could
		//    not present at all (MIT-SHM / DRI3 cross-namespace failures); neither
		//    variant painted anything, so that experiment cannot count as evidence.
		//    To validate once in a session with a real GPU: run
		//    ZEKIT_PREHEAT_BACKEND=1 — if the picture still appears, X11 can also
		//    build early and the startup blank period can be removed; if the window
		//    shows only the backdrop color, this platform really must map first.
		//
		// Addendum (2026-10-06): this "may we" question is about the WHOLE backend
		// building before map — the most aggressive split. Actually not needed at
		// that granularity: only "swapchain / EGL window surface" in backend init
		// truly depends on mapped (see ZeWindowX11::Show), and the most expensive,
		// window-independent part — loading GPU drivers — has been split out, done
		// by Rhi::IBackend::Preheat before the platform layer shows the window
		// (Kit::Window::Show calls it first, regardless of this function's return).
		// So this function's semantics narrow to: "build the swapchain early too?" —
		// still no on X11.
		//
		// Returns true = may build early (Show runs ensureBackend before showing).
		virtual bool CanPrepareRendererBeforeShow() const {
#if defined(_WIN32)
			return true;
#else
			// On-device validation switch: unset = conservative branch (behavior
			// identical to not setting the variable).
			if (const char* v = std::getenv("ZEKIT_PREHEAT_BACKEND"))
				return *v != '\0' && *v != '0';
			return false;
#endif
		}

		// The render backend cannot be built NOW but may be LATER — do not treat
		// this as "this machine has no GPU".
		//
		// Desktop is "you create the window": when Create() returns, the native
		// handle definitely exists; a failed backend means this machine truly has
		// no usable GPU. Kit then degrades to "run the message loop, paint nothing"
		// and reports one error — that is correct.
		//
		// Mobile is the reverse: THE SYSTEM DELIVERS THE WINDOW. At Show() time the
		// ANativeWindow* often has not arrived yet (waiting for
		// APP_CMD_INIT_WINDOW); failing to build the backend is NORMAL TIMING, not
		// a fault. If this also degraded to "paint nothing", that device would
		// NEVER paint again — once the surface arrives nothing pulls rendering back
		// (verified symptom: black window, "no usable render backend" in logcat,
		// then nothing).
		//
		// So when this returns true, Kit keeps running the main loop: when the
		// surface is ready the platform sends EventKind::SurfaceChanged, at which
		// ensureBackend() succeeds and rendering starts automatically (the app does
		// nothing).
		//
		// Returns true = failing now is temporary (mobile); false = truly absent
		// (desktop, default).
		virtual bool RenderBackendPending() const { return false; }

		// ---- State ----
		virtual void SetTitle(const std::string& utf8) = 0;
		virtual bool IsClosed() const = 0;
		virtual bool IsActive() const = 0;
		virtual void RequestRedraw() = 0;

		// Schedules a "delayed message-loop wake" (one-shot; for caret blinking and
		// similar timered frame top-ups).
		// afterMs: milliseconds from now to post the wake; <= 0 = as soon as
		// possible (wakes next round).
		// Semantics: a SYSTEM TIMER posts one empty message; it expires after
		// firing — no continuous CPU burn. Multiple schedules KEEP THE EARLIEST:
		// while a schedule is pending, a new one that is not earlier is ignored and
		// an earlier one advances the timer (i.e. "the earliest of all pending
		// schedules fires", not last-writer-wins). The host paints a frame upon the
		// wake and schedules again if it must keep animating.
		virtual void RequestWakeup(int afterMs) = 0;

		// Cancels a pending wake that has not fired (no schedule = no-op).
		virtual void CancelWakeup() = 0;

		// Wakes this window's message loop from ANY thread (thread-safe).
		//
		// RequestWakeup / RequestRedraw may only be called on the UI thread (they
		// touch window state directly). "Background data arrived, the UI should
		// update" is the MOST COMMON cross-thread need — without this channel, apps
		// would have to roll their own PostThreadMessage or poll.
		//
		// Semantics are deliberately the WEAKEST tier: it only guarantees "the UI
		// thread wakes once" — not immediately, not exactly once, and with no data
		// (callers put data in their own thread-safe container; Kit fetches it
		// after waking).
		//
		// Win32 = PostMessageW(hwnd, WM_APP+n, 0, 0) (message queues are
		// naturally thread-safe).
		// Default is a no-op: on those platforms background pushes degrade to
		// "handled at the next natural wake" — no data lost, only delayed —
		// consistent with the "capability degradation never errors" convention.
		virtual void PostWake() {}

		// Acquires / returns the "high-resolution system timer" (required for
		// Immediate mode's frame pacing).
		// on: true = raise to millisecond precision; false = return it.
		// Why: Windows' default scheduler tick is ~15.6ms, so
		// MsgWaitForMultipleObjects(16) actually sleeps 15.6 or 31.2ms — sleeping
		// exactly to the next 16.7ms frame boundary is impossible; measured jitter
		// ~20%. Kit acquires on entering the Immediate main loop and returns on
		// exit (RAII).
		//
		// The PLATFORM decides how, and never errors (this library's convention):
		//  - Win32: timeBeginPeriod(1) / timeEndPeriod(1) (process-global
		//    counter);
		//  - X11 / mobile: the wait itself is a high-precision select / kernel
		//    timer — silently no-op.
		//
		// Acquisition is PROCESS-GLOBAL: calls must be paired (the library uses
		// RAII in exactly one place).
		virtual void SetTimerPrecision(bool on) = 0;

		virtual void PostClose() = 0;

		// Requests quitting the current thread's message loop.
		// exitCode: process exit code (IWindow::ExitCode() returns it).
		// Corresponds to Win32's PostQuitMessage. Called by the host when all
		// windows have closed, realizing the "quit only when the last window
		// closes" semantics.
		virtual void RequestQuit(int exitCode) = 0;

		// Lets the system take over window dragging (for self-painted title bars).
		virtual void StartWindowDrag() = 0;

		// Asynchronously queries whether a mouse button is physically down.
		// Independent of the message stream — true answers even when the message
		// loop is stuck / the cursor left the window.
		virtual bool IsMouseButtonDown(MouseButton b) const = 0;

		// Escape hatch: for when a real HWND / NSView is unavoidable; the normal
		// path must not touch it.
		virtual void* NativeHandle() const = 0;
	};

	// Creates the current platform's window implementation (compile-time
	// dispatch; see ZeWindow.cpp).
	// Never returns nullptr; on unsupported platforms returns an implementation
	// whose Create never succeeds.
	IWindow* CreateNativeWindow();

	// Hands the HOST HANDLE to the platform layer (required on mobile; ignored on
	// desktop).
	//
	// Desktop is "you create the window" (CreateWindowEx), so
	// CreateNativeWindow() suffices. Mobile is the reverse — "the system hands the
	// window to you":
	//  - Android: the parameter of android_main(android_app*); the window appears
	//    through it only at APP_CMD_INIT_WINDOW;
	//  - iOS: UIApplication / UIWindow (later integration).
	// The handle exists only inside the HOST's entry function; the library cannot
	// fetch it itself, so the host must hand it in.
	//
	// host: the platform-private handle (Android = android_app*; iOS =
	// UIApplication*).
	// Must be called BEFORE constructing Kit::Window — Kit::Window's constructor
	// calls CreateNativeWindow().
	// Desktop platforms: a no-op (windows are created by Zekit itself; there is no
	// "host handle").
	// Process level: one process has one host (on Android, that one
	// NativeActivity).
	void AttachPlatformHost(void* host);

	// =============================== Console ===============================
	//  Only the APP ENTRY uses this (see include/ZekitApp.h's ZEKIT_APP_ENTRY):
	//  on desktop Windows, "a windowed program shows no console" is decided at
	//  LINK time (the subsystem), and two things need a runtime landing: the
	//  toolchain where the link-time directive does not apply (MinGW / clang only
	//  honor -mwindows), and "getting the console back while debugging".
	//  Implementation in ZeConsole.cpp (fully compile-time dispatched; non-Windows
	//  is an honest empty implementation).
	//
	//  One discipline shared by all three functions: NEVER TAKE AWAY what the
	//  caller already has. A shared terminal window is not hidden; output already
	//  redirected to a file / pipe is not re-routed.
	// =====================================================================

	// Hides the console window owned EXCLUSIVELY by this process.
	// Returns true = really hidden; false = no console window / it is shared (a
	// terminal) / the platform has no such concept.
	// Only hides; no FreeConsole() — output is merely invisible, the pipe remains
	// (reason in the implementation).
	// When launched from a terminal, the console belongs to the PARENT process;
	// this deliberately does nothing there.
	bool HideConsole();

	// Ensures output has a destination and the console window is visible (allocates
	// one only when truly nowhere to go).
	// Returns true = console output is now available (existing / inherited / newly
	// allocated all count); false = attached to a WINDOW-LESS console (the
	// launcher's chosen form; this function does not steal it).
	// Already attached to a console WITH a window: only ShowWindow(SW_SHOW); the
	// standard streams are re-attached ONLY when dangling — with a destination,
	// not a byte is touched, so redirections like `app.exe > log.txt` survive.
	// No console attached but the standard handles are valid (inherited when a
	// windowed subsystem binary is started from a terminal): also returns true and
	// does NOT open a window — output already has a destination; no need to
	// bother the user.
	// Only when truly nowhere to go: allocate (AllocConsole + hook streams + UTF-8
	// codepage + push to the bottom).
	// Do NOT "swap out" a window-less console (FreeConsole + AllocConsole): that
	// is the launcher's explicit choice, and in testing it makes the CRT fastfail
	// (0xC0000409).
	bool ShowConsole();

	// The entry macro's "console policy": hide or show per the environment switch.
	// ZEKIT_CONSOLE non-empty and first char not '0' (same criterion as the
	// library's other switches) -> ShowConsole(); otherwise -> HideConsole().
	//
	// The environment-switch branch is the main door for DEBUGGING a
	// windowed-subsystem build: such a process has no console at all — library
	// diagnostics (GL fallback, ZEKIT_TRACE_* traces) and the app's own std::cout
	// have nowhere to go; ZEKIT_CONSOLE=1 gives them a home instantly.
	// The hide branch is a NO-OP on MSVC (the link already chose the windowed
	// subsystem; the process has no console); it covers toolchains where the
	// link-time directive does not apply (MinGW etc.).
	void SetupEntryConsole();

	// Reads the system preference once: whether the system uses dark app mode.
	// Returns true = dark; false = light / the platform has no such concept.
	// Process-level query, independent of any window instance
	// (Kit::Window::SystemPrefersDarkMode forwards to it).
	// Where each platform reads (none of these is guessable; check this before
	// changing the criteria):
	//   - Win32 — registry Personalize\AppsUseLightTheme.
	//   - X11   — (1) GTK_THEME env var (2) XSETTINGS Gtk/ColorScheme
	//             (3) prefer-dark-theme in gtk-N.0/settings.ini (fallback).
	//             (2) is the only channel GNOME 42+ still writes: since then the
	//             dark switch lives in gsettings color-scheme, broadcast by the
	//             settings daemon via XSETTINGS; settings.ini is NO LONGER
	//             WRITTEN — implementations reading only it always judge "light"
	//             on Ubuntu 24.04 (the canvas never follows the system).
	bool SystemPrefersDark();

	// Lifecycle trace switch (OFF by default; set ZEKIT_TRACE_LIFECYCLE=1 to
	// enable).
	//
	// When on, every step of the "window closed -> message loop exits" chain
	// (which native event arrived, when the native window was destroyed, the
	// live-window count, whether a quit was requested, when it was posted) prints
	// to stderr with a [Zekit/trace] prefix.
	//
	// Why keep this: problems in that chain are INTERMITTENT (wrong one time in
	// ten), not deducible from code — only instrumentation helps, and the chain
	// has six or seven links, each a possible break. A switch beats re-editing and
	// rebuilding every time.
	//
	// When off: one static init + one bool check, ZERO output (matching this
	// library's "nothing but errors is printed" convention).
	inline bool TraceLifecycle() {
		static const bool on = [] {
			const char* v = std::getenv("ZEKIT_TRACE_LIFECYCLE");
			return v && *v != '\0' && *v != '0';
			}();
		return on;
	}

	// Clipboard trace switch (ZEKIT_TRACE_CLIPBOARD=1; off by default).
	// Why a separate switch instead of riding TraceLifecycle: clipboard problems
	// are CROSS-PROCESS ("copied here, can't paste there"); when debugging one
	// usually wants just these few lines, while the lifecycle output is far larger
	// and would wash the clues away. The print content is deliberately "three
	// things the other side can understand": who is asking, for which format, and
	// whether we answered.
	inline bool TraceClipboard() {
		static const bool on = [] {
			const char* v = std::getenv("ZEKIT_TRACE_CLIPBOARD");
			return v && *v != '\0' && *v != '0';
			}();
		return on;
	}

	// Dark/light-mode detection trace switch (ZEKIT_TRACE_THEME=1; off by
	// default).
	// Why its own switch: "a wrong verdict produces NO error at all". The system
	// is dark, we cannot read it -> we silently run light, the canvas is all
	// white, and nobody knows which step failed. When on, every verdict prints
	// "which source produced which conclusion" — instantly telling "not read"
	// from "read but inverted".
	inline bool TraceTheme() {
		static const bool on = [] {
			const char* v = std::getenv("ZEKIT_TRACE_THEME");
			return v && *v != '\0' && *v != '0';
			}();
		return on;
	}

	// Frame-time trace switch (ZEKIT_TRACE_FRAME=1; off by default).
	// Why its own switch: "slowness" has no error message; only measurement
	// helps. When on, RenderFrame prints one line of per-section timing per frame
	// (auto-size / fill / frame collection / backend submit) plus widget and
	// vertex counts — instantly showing whether the bottleneck is layout, frame
	// collection (triangulation / text measurement), or the backend. High-frequency
	// redraw problems like dragging a slider are located with this.
	// One line PER FRAME, high volume: enable only while reproducing, do not leave
	// it on.
	inline bool TraceFrame() {
		static const bool on = [] {
			const char* v = std::getenv("ZEKIT_TRACE_FRAME");
			return v && *v != '\0' && *v != '0';
			}();
		return on;
	}

	// Per-widget redraw source tracing (ZEKIT_TRACE_REDRAW=1).
	// Answers "WHO demands a redraw each frame" — layout self-feedback (a widget
	// written geometry each frame -> requests redraw -> written again next frame)
	// shows as "continuous jitter / idle frame spinning", invisible in frame times
	// and vertex counts; you must see "which widget the request came from".
	inline bool TraceRedraw() {
		static const bool on = [] {
			const char* v = std::getenv("ZEKIT_TRACE_REDRAW");
			return v && *v != '\0' && *v != '0';
			}();
		return on;
	}

	// Tooltip display pipeline trace (ZEKIT_TRACE_TOOLTIP=1; off by default).
	// Tooltip display spans TWO windows (host fallback self-paint <-> native
	// popup carrier), and the fade-in alpha is mutable state advanced during
	// rendering — for problems like "carrier but no content / text grayed", you
	// must see each step of "who paints at which alpha". When on:
	//  - showNativePopup: one line when the carrier is shown (including nested
	//    frame deferral) (fade / geometry);
	//  - paintPopupInto: one line per actual paint (this frame's alpha / geometry
	//    / whether carrier path).
	// One line PER PAINT, high volume: enable only while reproducing.
	inline bool TraceTooltip() {
		static const bool on = [] {
			const char* v = std::getenv("ZEKIT_TRACE_TOOLTIP");
			return v && *v != '\0' && *v != '0';
			}();
		return on;
	}

} // namespace Plat
