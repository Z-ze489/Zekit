#pragma once

// Platform availability gate - MUST be the first include.
// HarmonyOS / Apple are "slot reserved, implementation unverified": this header #errors
// out when compiling for those platforms (not a silent downgrade). Being first keeps the
// error pointing at the platform itself rather than a cryptic syntax error downstream.
// See Zekit/include/ZePlatformGate.h for details and how to unlock.
#include "ZePlatformGate.h"

#include <cstdint>
#include <climits>             // INT_MIN (GadgetDesc's "let the system place it" sentinel)
#include <functional>          // std::function (event filters / lifecycle callbacks)
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#include "ZeInputEvents.h"
#include "ZeWidget.h"
#include "FontDesc.h"          // TextMetrics / FontDesc (public contract of MeasureText)
#include "ZeTheme.h"
#include "ZeCapability.h"      // Kit::Capability - platform capability bits (used by Supports())
#include "ZeFileDialog.h"      // FileDialogDesc / FileDialog - native file dialogs
#include "ZeRenderBackend.h"   // Rhi::Api - render backend choice (enum values restricted per platform)
#include "ZeEmbeddedRes.h"     // Embedded resource registry - the only source of "fetch a built-in
                               //   image by ID" on non-Windows (Windows uses .rc and skips this
                               //   table). The header is light (<cstddef> only); keeping it in
                               //   the main header saves downstream an extra include just to
                               //   register a png.
#include "ZePanel.h"           // Kit::Panel - recommended base class for composite widgets/panels.
                               //   Deliberately in the main header: when writing a composite
                               //   widget this should be the first thing reached for, without
                               //   needing to know a separate header exists. Light dependencies
                               //   (only LayoutBase / FlexStyle / Color4f).

// Version string
extern "C" const char* zekit_version();

namespace Kit {

	// External renderer interface for canvases (defined in ZeCanvasRenderer.h).
	// Forward-declared only, deliberately not included: the main header is included by every
	// downstream project and most never use an external renderer. The signatures of
	// UseExternalRenderer / CanvasView::SetRenderer only need shared_ptr<ICanvasRenderer>,
	// for which an incomplete type suffices.
	class ICanvasRenderer;

	// Hands the host's platform handle to Zekit (required on mobile; ignored on desktop).
	//
	// On desktop "you create the window" and the library manages itself; on mobile it is
	// reversed - "the system hands the window to you", and that handle only exists in the
	// host's entry function:
	//   - Android: the android_main(android_app*) parameter;
	//   - iOS: the window from UIApplicationDelegate (later integration).
	//
	// Must therefore be called BEFORE constructing Kit::Window. With ZEKIT_APP_ENTRY (see
	// <ZekitApp.h>) there is no need to call it by hand - the macro does it for you.
	//
	// host: platform-private handle (Android = android_app*); nullptr = clear.
	// Process-level: one host per process (on Android, the single NativeActivity).
	// Desktop platforms: a no-op - nothing happens and nothing errors.
	void AttachPlatformHost(void* host);

	// ===============================================================
	//  Console - the other half of the application entry
	// ===============================================================
	//  On desktop Windows, "GUI apps show no console" is decided at LINK time (subsystem):
	//  with ZEKIT_APP_ENTRY (see <ZekitApp.h>) the library sets the Windows GUI subsystem for
	//  you and the process never gets a console at all.
	//
	//  Why these three functions still exist:
	//    - Toolchains the linker cannot reach (MinGW / clang only honor -mwindows) need a
	//      runtime fallback to honor the same "no console by default";
	//    - Debugging wants the console back - under the GUI subsystem, neither library
	//      diagnostics (OpenGL fallback, ZEKIT_TRACE_* traces) nor the app's std::cout have
	//      anywhere to go; ZEKIT_CONSOLE=1 provides one.
	//
	//  Same reason as AttachPlatformHost for living in this header (not <ZekitApp.h>):
	//  people hand-writing int main() should not need another include just for the console.
	//
	//  All three functions share one discipline: never TAKE AWAY what the caller already has -
	//  a shared terminal window is not hidden, output already redirected to files/pipes is
	//  not re-routed. "No console by default" is only the entry macro's default, never an
	//  override of the user's existing choice.
	// ===============================================================

	// Hides the console window owned EXCLUSIVELY by this process.
	// Returns true if really hidden; false = no console window / it is shared / unsupported.
	//
	// "Exclusive" is the only safety valve: launched from a terminal (cmd / Windows
	// Terminal), the console belongs to the PARENT process; GetConsoleWindow() still returns
	// a handle, and hiding it would hide the user's terminal. The test is "only one process
	// attached to this console" (GetConsoleProcessList == 1); silently does nothing when
	// shared.
	//
	// Only hides the window, never FreeConsole(): the process stays attached and stdout /
	// stderr keep working (output is just invisible). The goal is "one fewer window", not
	// "one fewer channel".
	// Non-Windows always returns false - there is no "console window owned by this process"
	// there; false is an honest answer, not a failure.
	bool HideConsole();

	// Ensures output has somewhere to go and the console window is visible; allocates a
	// console only when truly nothing exists.
	// Returns true when console output is now usable (already attached / inherited / newly
	// created all count).
	//
	// Four cases, each doing no more than necessary:
	//   1. Attached console WITH a window -> ShowWindow(SW_SHOW); reattach dangling standard
	//      streams (no destination) as a courtesy - with a destination, not one byte moves;
	//   2. Attached to a console WITHOUT a window (CREATE_NO_WINDOW launches) -> do nothing
	//      and return false: that form was explicitly requested by the launcher (see below);
	//   3. No console attached but standard handles valid -> return true, NO new window -
	//      GUI-subsystem apps launched from a terminal inherit handles from the parent and
	//      printf / std::cerr still print to that terminal (measured);
	//   4. Truly nowhere to go (double-click / debugger launch) -> AllocConsole, attach
	//      stdout / stderr / stdin, set the code page to UTF-8, and push the window to the
	//      bottom (never steals foreground). printf text really lands in this console -
	//      Zekit/tests/build_console.sh asserts it by reading back the screen buffer.
	//
	// In case 1 "with a destination, do not reattach" protects redirections like
	// app.exe > log.txt; "reattach when dangling" is best-effort (a real "window but no
	// streams" state is nearly impossible to construct).
	// Do NOT "replace" a window-less console from the library or app (FreeConsole +
	// AllocConsole): measured twice - (a) that form was explicitly requested, taking it is
	// overreach; (b) the CRT's stdout still holds the console handle that FreeConsole killed,
	// and a following freopen("CONOUT$") makes the CRT fastfail (child exits 0xC0000409 =
	// STATUS_STACK_BUFFER_OVERRUN). Want visible output? Launch without CREATE_NO_WINDOW or
	// from a terminal.
	// Non-Windows always returns true: output there runs in a terminal or goes to
	// journald / log files - it always has a destination.
	// See SetupEntryConsole, ZEKIT_APP_ENTRY_CONSOLE.
	bool ShowConsole();

	// The application entry's "console policy" - called automatically by ZEKIT_APP_ENTRY.
	//
	// Chooses by the ZEKIT_CONSOLE environment switch: non-empty and the first char is not
	// '0' (same test as ZEKIT_TRACE_* / ZEKIT_PREHEAT_*) -> ShowConsole; otherwise ->
	// HideConsole.
	//
	// To open a console for logs, no code change or relink is needed:
	//   ZEKIT_CONSOLE=1  myapp.exe        (cmd)
	//   $env:ZEKIT_CONSOLE=1; .\myapp.exe (PowerShell)
	//
	// The hide branch is a NO-OP on MSVC (link time already chose the window subsystem, so
	// the process has no console to hide); it covers toolchains where the link-time
	// directive fails (MinGW / clang, which only honor -mwindows).
	// Hand-written int main() may call it too, but need NOT: a plain main() keeps its
	// console by default and nothing needs doing.
	// See ZEKIT_APP_ENTRY, ZEKIT_APP_ENTRY_CONSOLE, ZEKIT_APP_CONSOLE.
	void SetupEntryConsole();

	// The nature of this window when created as a hosted (child) window (see Window's
	// parent-child constructors).
	//
	// All of these are "a window with a host window", but completely different in nature -
	// mixing them up is the most common mistake in this area (style bits, coordinates,
	// draggability and reference frames all differ):
	//
	//                | Embedded          | Owned             | Popup             | Free
	//  non-client    | none (WS_CHILD,   | all (title bar /  | none (WS_POPUP;   | all (bit-identical
	//                | not even a border)| system menu /     | draw your own)    | to Owned)
	//                |                   | min-max / border) |                   |
	//  clipped by    | yes               | no (may overhang) | no (may overhang) | no
	//  host client   |                   |                   |                   |
	//  caption/border| caller-driven     | system-provided   | caller-driven     | system-provided
	//  drag          |                   |                   |                   |
	//  taskbar /     | never             | per-platform      | never             | per-platform
	//  Alt-Tab       |                   | style notes       | (WS_EX_TOOLWINDOW)| (same as Owned now)
	//  focus stealing| no                | yes               | no                | yes
	//                |                   |                   | (WS_EX_NOACTIVATE)|
	//  covered by    | (part of host)    | NO (owner rule)   | NO                | YES
	//  main window   |                   |                   |                   |
	//  minimized     | yes (same canvas) | yes (system)      | yes               | NO (independent)
	//  with host     |                   |                   |                   |
	//  typical use   | an area in the    | child window /    | notification bar /| tool panel /
	//                | host client area  | dialog            | frameless popup   | modeless inspector
	//                | with its own HWND |                   |                   |
	//
	// Owned IS a normal window - decorations are system-given, neither stripped nor redrawn;
	// "child" only means it is OWNED by the host (follows it, dies with it, not in taskbar).
	// Popup is Owned's decoration-free version: follows the host and stays out of the taskbar
	// too, but has no system decorations, does not steal focus and is naturally topmost -
	// what a notification bar should be (Kit::ToastNotifier is its first user). It maps to
	// Capability::PopupWindow.
	//
	// Owned vs Free is ONE system rule, not a switch: an owned window is ALWAYS above its
	// owner - a hard Windows guarantee that SetWindowPos(HWND_BOTTOM) cannot defeat (the
	// system re-sorts it above the owner). So "panel sinks behind when the main window is
	// clicked" requires starting without an owner (ChildKind::Free). Do not expect z-order
	// APIs to push an Owned window under the host: it does not hold, in any formulation.
	//
	// Three consequences for Free (all intentional, not defects):
	//  - it does not minimize with the host (no owner; the system ignores it) and may remain
	//    visible when the host is covered. Owned is for "follow the host", Free for "z-order
	//    freedom"; they are mutually exclusive;
	//  - it enters the taskbar (same style bits as Owned): without a taskbar entry, once
	//    minimized there is NOWHERE to restore from (the system shrinks it to a title bar at
	//    the screen's bottom-left corner) - that pit was hit on Owned, hence "full styles +
	//    taskbar";
	//  - destruction still belongs to the library (managed table / explicit destroy at host
	//    destruction): only the OS-level owner is removed, not the Kit-level managed
	//    relationship - DestroyChildWindow, IsClosed(), theme inheritance and
	//    SetPositionRelativeToHost all keep working.
	//
	// Mobile (Android / Harmony) has no native windows: Free, like the others, degrades to a
	// renderer-drawn stand-in painted on the host's canvas with NO independent z-order - so
	// "sink behind the host" is desktop-only.
	//
	// When to actually use Embedded (the honest version): "no border" is NOT its selling
	// point - top-level SetBorderless(true) or Popup both do that. Its value is a REAL native
	// child window inside the host client area (own HWND, own swap chain, clipped by the
	// host, follows it), at the cost of NOT participating in layout.
	//   - Adding content to a UI (image / text / chart / list) -> Kit::Panel + layout;
	//   - Floating window / dialog (system decorations, draggable out, always on top) -> Owned;
	//   - Tool panel / modeless inspector (MAY be covered by the main window) -> Free;
	//   - Notification bar / decorated-free popup (no focus stealing, pass-through) -> Popup;
	//   - Just want "a borderless window" -> top-level SetBorderless(true) or Popup;
	//   - An area that MUST own its HWND and be clipped by the host -> Embedded.
	// Reality: the library itself never uses Embedded (DockPanel tear-off floaters = Owned,
	// ModalDialog = Owned, ToastNotifier = Popup). Its role today is an escape hatch:
	//  - it does not join Kit layout (Window is not a Widget): coordinates are absolute
	//    values relative to the host client area; host resizes must be synced by you;
	//  - it carries an extra context and font atlas (sharing not landed on D3D12/Vulkan);
	//  - it is more fragile than Panel: both pits we hit ("visible on creation" swallowed by
	//    the compositor, glyph textures pre-cached as 0) happen ONLY on child windows.
	//
	// Embedded's render backend is FORCED to inherit the host's (see Zekit.cpp: switching
	// APIs would destroy the sharing premise) and the public surface has NO custom render
	// hook - so "a second 3D viewport / mixing another graphics API" is this path's GOAL,
	// not a ready capability; to run your own engine on that HWND you must take over via
	// hwnd() yourself (unofficial, and it will fight Kit's per-frame Present).
	//
	// All kinds TRY to share render resources with the host (same share group) - but whether
	// sharing really holds depends on the backend: only OpenGL has it landed (see
	// IBackend::CanShareContext); on D3D12 / Vulkan child windows silently fall back to an
	// INDEPENDENT backend (same features, separate device). Either way, lifetime hangs off
	// the host. Mobile stand-ins apply to all kinds - a platform essence, not a design
	// choice (and it must not push back onto desktop forms).
	// See Window(Window&, int, int, int, int, ChildKind).
	enum class ChildKind {
		Embedded,   // Embedded child (WS_CHILD): no non-client area at all, clipped by the host client area
		Owned,      // Owned NORMAL window: all decorations system-given (title bar / min / max /
		            // close / resizable border); in the taskbar by default (WS_EX_APPWINDOW) so
		            // minimize has somewhere to restore from
		Popup,      // Owned popup without system decorations: no focus stealing, not in
		            // taskbar/Alt-Tab; notification bars use it
		Free,       // Ownerless normal window: looks EXACTLY like Owned but is NOT forever above
		            // the host - clicking the host sinks it behind. For tool panels /
		            // modeless inspectors

		// Overlay carrier: a decorated-free popup like Popup, but requiring PER-PIXEL
		// transparency.
		//
		// The only form of "content visible, window invisible" - on Windows it lands as
		// WS_EX_NOREDIRECTIONBITMAP (the window has NO redirection surface), so DWM can compose
		// only the backend's composition swap chain (D3D12 + CreateSwapChainForComposition +
		// DirectComposition visuals). No window rectangle exists on screen; only rendered
		// pixels.
		//
		// The ONLY difference from Popup is this bit - everything else (no focus stealing, no
		// taskbar/Alt-Tab, naturally topmost, dies with the host, WS_POPUP undecorated) is
		// bit-identical.
		// REQUIRES SetClearAlpha(0): without it the swap chain degrades to DXGI_ALPHA_MODE_IGNORE
		// + CreateSwapChainForHwnd (no DComp visual) while the window has no redirection
		// surface - the result displays NOTHING. Only OverlayHost (the shared carrier for
		// tooltips / menus / image previews) uses this form.
		// The GL backend does not support per-pixel transparency (the default framebuffer has
		// no alpha DWM can compose) and automatically falls back to Popup behavior (with a
		// redirection surface, opaque).
		Overlay,
	};

	class Window {

	public:
		Window();
		~Window();                                  // Destroys the window and GL context (must run on the creating thread)

		// Creates this window as a hosted window's dependent (desktop platforms only).
		//
		// parent: the host window. MUST outlive this object - a dependent window's native
		//   handle hangs off the host (Embedded hangs inside its client area); once the parent
		//   dies, the child has no reason to exist.
		// x,y: position relative to the HOST CLIENT AREA's top-left (pixels).
		// width,height: window size (pixels); 0 or negative = auto size (60% of the host
		//   client area, min 320x240, never larger than the host client area). To set only the
		//   position, use the (parent, x, y, kind) overload.
		// kind: window nature (see ChildKind). DEFAULT Owned - "a normal window with system
		//   decorations that can be dragged outside the host but is owned by it (follows it,
		//   not in the taskbar)", matching CreateChildWindow's default.
		// borderlessAtBirth: frameless from birth (default false). When true, SetBorderless(true)
		//   is applied BEFORE the first on-screen frame, so the user never sees the "normal
		//   window with title bar" frame.
		//   Why the parameter exists: SetBorderless is a post-creation switch, but the Owned
		//   form shows during construction (see the platform Show() at the end of the child
		//   constructor in Zekit.cpp). "Create -> show (framed) -> unframe" flashes the
		//   non-client area - the system paints it immediately, and a style change must wait
		//   for the next WM_NCCALCSIZE. Tear-off floaters (DockPanel) are this scenario.
		//   Only meaningful for Owned / Free (Popup has no decorations anyway, Embedded is
		//   WS_CHILD); true on other forms is a silent no-op.
		//
		// Default changed from Embedded to Owned on 2026-10-17. The old default was legacy
		// (before the kind parameter existed this constructor always built WS_CHILD), not a
		// recommendation: Embedded has no title bar and does not join Kit layout, so using it
		// as the default for "create a window" misleads ("why no title bar / why can't it
		// leave"). For the old embedded behavior, write ChildKind::Embedded explicitly.
		//
		// What is built is an INDEPENDENT native window, not an overlay on the host canvas. It
		// has its own:
		//   - render context - sharing device resources with the host (same share group:
		//     textures / VBOs / programs mutually visible), so the font atlas is not duplicated;
		//   - widget tree / focus / layout - exactly the same implementation as Kit::Window;
		//   - message handling - but it does NOT run its own message loop; the host's exec()
		//     drives it.
		//
		// This constructor vs CreateChildWindow:
		//   - window object held by the CALLER (a fixed panel in main's scope) -> this one;
		//   - windows that come and go (settings panel / dialog popped in a button callback)
		//     -> CreateChildWindow (library-managed lifetime);
		//   - Android / Harmony -> the same constructor; the library auto-degrades to a
		//     renderer-drawn stand-in.
		//
		// Choosing a ChildKind (why Embedded "has no scenario"):
		//   - add content to the UI (image / text / chart / list) -> Kit::Panel + layout (no
		//     child window needed at all);
		//   - float above the host, draggable out, want a system title bar (panel / dialog)
		//     -> ChildKind::Owned;
		//   - decorated-free popup (notification bar), no focus stealing, pass-through clicks
		//     -> ChildKind::Popup;
		//   - just want "a borderless window" -> top-level SetBorderless(true), or Popup;
		//   - need an area with its own HWND / swap chain, clipped by the host client area
		//     -> ChildKind::Embedded.
		// Embedded is an ESCAPE HATCH, not a common component. Three reasons:
		//  - it does not join Kit layout (Window is not a Widget): coordinates are absolute
		//    values relative to the host client area; host resizes must be synced manually;
		//    "follow the panel" is impossible;
		//  - it carries an extra context and font atlas (CanShareContext() is false on
		//    D3D12/Vulkan; sharing itself is not landed), costlier than Panel;
		//  - it is more fragile than Panel - both pits we hit ("visible on creation" swallowed
		//    by the compositor, glyph textures pre-cached as 0) happen ONLY on child windows;
		//    same-canvas Panels are immune.
		// Embedded's backend is FORCED to inherit the host's (m_api = parent.m_api; see
		// Zekit.cpp: switching APIs destroys the sharing premise), so "mixing another graphics
		// API" is not something it can do today, and the public surface has no custom render
		// hook - running your own engine on that HWND means taking over via hwnd() yourself
		// (unofficial; Kit still clears and Presents every frame, and the two will fight).
		// "A second 3D viewport" is this path's GOAL, not a ready capability.
		// The library uses it NOWHERE internally (DockPanel tear-off = Owned, ModalDialog =
		// Owned, ToastNotifier = Popup) - hence the default is now Owned (2026-10-17),
		// aligned with CreateChildWindow; Embedded must be written explicitly.
		//
		// Mobile: calling is SAFE but ineffective - IsClosed() is immediately true, no crash.
		// Use IsClosed() / IsValid() to learn whether it actually came up.
		// Child windows do NOT participate in the "last window closed exits the process" rule
		// by default. EXCEPTION: a child with SetInheritOnParentClose(true) that successfully
		// inherits when the host closes counts as a live window from that moment - that is the
		// mechanism behind "main window closed but the program keeps running". See
		// SetInheritOnParentClose and IsInherited.
		// Do not call exec() on a child - it is driven by the parent and just returns 0.
		//
		// How "visible once created" is fulfilled (no manual Show): the constructor only builds
		// the window; "on screen" happens automatically after the FIRST widget is attached
		// (AddWidget / SetWidget / AddChild) - measured, a child window's first presentation
		// must happen after it has content, or the compositor sticks to the empty surface from
		// creation (symptom: window there, all white inside; see primeChildDisplay's note).
		// So the natural usage does NOT need viewport.Show(). Conversely, for "build first,
		// fill in, show later" call Hide() first - after an explicit Hide the library will not
		// race to show on attach; you call Show() yourself.
		//
		// Text in child windows uses its OWN font atlas: glyphs requested before the backend's
		// texture sink is installed are deferred and reloaded next frame
		// (FontRenderer::GetOrLoad) - no "boxes drawn, text missing".
		//
		// See IsChildWindow, CreateChildWindow, ChildKind, primeChildDisplay.
		Window(Window& parent, int x, int y, int width, int height,
			ChildKind kind = ChildKind::Owned, bool borderlessAtBirth = false);

		// Same as above, additionally choosing whether to keep the system drop shadow
		// (false = completely clean outside the window).
		// For widgets that hand a whole rectangle to an external renderer (CanvasView's
		// OwnedWindow form, video layers, map layers): the Popup form carries a ring of
		// CS_DROPSHADOW glow by default, drawn OUTSIDE the window rect and unaffected by any
		// clipping - inside a scrolling view, scrolled halfway it overlays the content above,
		// looking like "not clipped cleanly" (user report 2026-11-03).
		// systemShadow: default semantics as in the previous constructor (true); false builds
		//   with the window class without CS_DROPSHADOW and also disables DWM rounded corners
		//   and border color. Only meaningful for Popup; silently a no-op otherwise.
		// Why a separate overload instead of a default argument: a default would CHANGE the
		// previous constructor's signature (one more bool in the mangled name), making every
		// already-compiled TU hit LNK2019. Measured 2026-11-03 - the library is a static lib;
		// a signature change demands a full rebuild.
		Window(Window& parent, int x, int y, int width, int height,
			ChildKind kind, bool borderlessAtBirth, bool systemShadow);

		// Creates a hosted dependent window, giving only the position; the library picks the size.
		// x,y: pixel position relative to the host client area's top-left.
		// kind: window nature (see ChildKind).
		// Size = 60% of the host client area (min 320x240, capped by the host client area);
		// resize afterwards with SetSize(w, h) as usual. See the geometry-less constructor below.
		Window(Window& parent, int x, int y, ChildKind kind = ChildKind::Owned);

		// Creates a hosted dependent window; ALL geometry is left to the library.
		// parent: host window (must outlive this object); kind: window nature (see ChildKind).
		//
		// The ONLY difference from the full-geometry constructor is that it takes no geometry:
		// size = 60% of the host client area (min 320x240, capped by the host client area),
		// position = the host client area's center. Afterwards arrange it exactly like a main
		// window: SetSize / SetPositionRelativeToHost / CenterInHost.
		//
		// Why it exists: the main window Window() never required geometry (default size +
		// later SetSize / SetPosition), while child windows used to demand all four numbers at
		// once - "create first, measure content, then size" required making up fake numbers.
		// Now both sides match: geometry is optional, not an entry barrier.
		//
		// Why kind has NO default here (while CreateChildWindow has one): a default would
		// compete with the DELETED copy constructor - in `Window b(a);` Window& matches better
		// than const Window&, so the compiler would resolve it as "create a child hosted by a"
		// instead of reporting "Window is not copyable". That silent meaning change is far
		// more dangerous than typing one extra word. The managed version (CreateChildWindow)
		// has no such concern - its name already says it creates a window.
		//
		// Everything else (platform allocation / shared render context / parent-driven
		// messages / theme inheritance / no live-count participation / never call exec) is
		// identical to the full-geometry constructor.
		Window(Window& parent, ChildKind kind);

		// Whether this window is a native child window (created by the constructor above or
		// CreateChildWindow).
		// Becomes false again after a successful host inheritance - at that point it is an
		// independent top-level window (see SetInheritOnParentClose and IsInherited).
		bool IsChildWindow() const;

		// ===============================================================
		//  Gadget form (ghost window) - desktop pets / floating balls / hover hints
		// ===============================================================
		// A decorated-free, PER-PIXEL TRANSPARENT top-level window - only rendered content is
		// visible on screen. The form desktop pets and tooltip bubbles need. On Windows it
		// does three things normal windows cannot, simultaneously:
		//   - window body: WS_POPUP + WS_EX_NOREDIRECTIONBITMAP - no redirection surface, the
		//     window rectangle does not exist on screen;
		//   - content presentation: D3D12 CreateSwapChainForComposition + DirectComposition
		//     visual + DXGI_ALPHA_MODE_PREMULTIPLIED - renderer output goes straight to DWM
		//     and desktop composition;
		//   - clicks: WM_NCHITTEST -> widget-tree HitTest -> HTCLIENT / HTTRANSPARENT - the
		//     character itself is interactive, transparent surroundings pass through to the
		//     windows below.
		//
		// Why "borderless + clear alpha 0" is not enough: normal windows (even with
		// SetBorderless(true) + SetClearAlpha(0)) always get a DWM redirection surface as
		// backing - the swap chain's per-pixel alpha only affects visual content, not that
		// backing, so a rectangle always remains on screen. True invisibility requires the
		// window to have NO backing (WS_EX_NOREDIRECTIONBITMAP), which can only be specified
		// at window creation - hence the gadget form must go through this constructor and
		// cannot be toggled afterwards.
		//
		// Do NOT give it a background: a gadget's transparency comes from "nothing drawn".
		// SetBackdropColor or a window-sized Panel + SetBackground instantly paints the window
		// rectangle.
		// Backend capability: only D3D12 uses the composition swap chain. When D3D12 init
		// fails and falls back to GL, the library restores the redirection surface - the
		// window degrades to "opaque but content visible" (never vanishes entirely).
		// Mobile has no such form (one Activity, one Surface): calls silently degrade to a
		// normal window. Query Capability::... rather than #if.
		// See IsGadget, SetClearAlpha, Tooltip.
		struct GadgetDesc {
			int  width = 260;      // Client width (pixels)
			int  height = 260;     // Client height (pixels)
			int  x = INT_MIN;      // Screen coordinates; INT_MIN = let the system place it (centered)
			int  y = INT_MIN;
			bool topmost = true;   // Stay above other windows (gadget default)

			// Whether this gadget may keep the process alive after the MAIN window closes
			// (default false).
			// Gadgets are "screen attachments" and by default do NOT hold the process: closing
			// the host window ends the process and the gadget goes with it - what most tools
			// and floating-ball apps expect (bug report 2026-11-03: "after launching the
			// gadget, closing the host window, the program kept running in the background";
			// the root cause was the gadget being treated as an independent window pinning the
			// process).
			// For the opposite semantics - "close the main UI, the pet / ball stays on the
			// desktop and keeps running" - enable it.
			// Only matters when a main window still exists in the process. Pure-gadget apps
			// (the pet IS the whole app, no main window) behave identically either way:
			// closing the last window still exits, per the old "no live windows" rule.
			// As long as ANY anchor window is alive in the process (a main window, or a gadget
			// with this switch on), closing other windows never ends the process - the test is
			// "all anchors closed".
			bool keepsAppAlive = false;
		};
		explicit Window(const GadgetDesc& desc);

		// Whether this window is in gadget form (decorated-free + per-pixel transparent + top-level).
		bool IsGadget() const;

		// Whether this window is ACTUALLY per-pixel transparent right now (the backend really
		// took the composition swap chain).
		//
		// An honest answer about whether the intent was fulfilled, not an echo of IsGadget():
		//  - a gadget REQUESTS per-pixel transparency -> IsGadget() is always true;
		//  - but D3D12 may fail to initialize and downgrade to GL (GL has no alpha DWM can
		//    compose), or dcomp.dll may be missing -> then the window gets its redirection
		//    surface back and this returns FALSE, showing as "opaque but content visible".
		// When debugging "why is the window still visible", check this first: false means the
		// problem is the render backend, not the window style.
		bool IsPerPixelTransparent() const;

		// ===============================================================
		//  Child window inheritance - surviving the host's closure
		// ===============================================================
		//  Requirement: when the user closes the main window with X, the program must NOT end
		//  with it - a designated child window takes over as "the last window" and lives on
		//  until it too is closed.
		//
		//  Default is NO inheritance (word-for-word identical to before this API existed: last
		//  window closes -> message loop ends -> process exits). Inheriting must be opted into
		//  explicitly - it is a semantics that changes process lifetime and must not be guessed.
		//
		//  All three of these must hold at once for a real inheritance (missing one makes it
		//  fake):
		//    1. OS level: detach the child's host ownership. Win32's hard rule is "host
		//       DestroyWindow also destroys the windows it owns" - without detaching the owner
		//       first, the child dies the instant the host disappears;
		//    2. Kit bookkeeping: it counts as a "live window" from then on, so the last-window
		//       rule works;
		//    3. Message pump: the host is gone but its exec() loop still runs - the loop keeps
		//       driving the heir (OnDemand's per-frame scheduling keeps scheduling for it too).
		//
		//  Owned / Free can inherit; Embedded (a block inside the host client area, physically
		//  part of the host), Popup (a focus-free notification bar; promoting it to a main
		//  window is self-contradictory) and mobile stand-ins CANNOT - IsInherited() stays
		//  false for those forms, other behavior unchanged.
		// ===============================================================

		// Requests "this window inherits when the host closes" (default off; child windows only).
		// on: true = enable inheritance; false = disable (default).
		//
		// When the host receives a close request (X / Alt+F4 / Close()) and it is ALLOWED, the
		// library promotes inheritance-enabled child windows to independent top-level windows
		// BEFORE the native window is destroyed: OS owner detached, counted into the live
		// window count, still driven by the host's exec().
		//
		// Typical use: the main window closes, the settings panel stays on screen.
		// See SetOnInherited for the callback that fires after promotion.
		//
		// Why "at allow time" and not "after closing": Win32 destroys owned windows in the
		// same instant as the host's DestroyWindow - one step late and only a dead handle
		// remains.
		// Multiple child windows may enable inheritance simultaneously - they survive TOGETHER
		// (each independent).
		// Inheritance is orthogonal to value semantics: the Window object itself still belongs
		// to its creator / host. For CreateChildWindow() windows the object lives in the
		// host's managed table - so the HOST Window object must live until exec() returns
		// (the normal `Kit::Window main; return main.exec();` satisfies this naturally). If
		// you destroy the host object while exec() is running, the heir is destroyed with it
		// (it has no independent owner; the library will not secretly new one).
		// A BLOCKED host close (SetOnCloseRequest returning false, or a disabled close button)
		// does not trigger inheritance - the host never disappears.
		// Enabling inheritance changes NO appearance: position / size / theme / render API all
		// stay; only "who owns it" changes.
		// See inheritOnParentClose, IsInherited, SetOnInherited, IsChildWindow.
		void SetInheritOnParentClose(bool on);

		// Whether "inherit on host close" is enabled (default false).
		bool inheritOnParentClose() const;

		// Whether this window has ALREADY inherited the host (host closed, it is still alive).
		// Once true it stays true (also after the window closes). It is the authoritative test
		// for "am I an independent top-level window now" - at that point IsChildWindow() has
		// turned false again and CenterOnScreen() is back to screen-work-area semantics.
		// Read-only query: always false when inheritance was never enabled or the platform
		// does not support it (Embedded / Popup / mobile stand-ins).
		bool IsInherited() const;

		// Sets the "I inherited the host" callback (fired once after the promotion completes).
		// cb: the callback; its parameter is THIS window. Empty clears it.
		//
		// Timing: after the host allows the close and this window finished promoting (OS
		// ownership detached, live count updated), BEFORE the host is destroyed. Safe here to:
		//  - query the real state via IsInherited() / IsChildWindow();
		//  - SetTitle / SetSize / CenterOnScreen to restyle itself as "the main window";
		//  - even Close() directly ("the heir dies too" - the process exits normally).
		//
		// Fired only once (one per inheritance). Never called when inheritance was not enabled.
		// Do NOT destroy this window object inside the callback (still on the host's close
		// callback stack).
		// See SetInheritOnParentClose, IsInherited.
		void SetOnInherited(std::function<void(Window&)> cb);

		// ===============================================================
		//  Library-managed child windows - the RECOMMENDED creation path
		// ===============================================================
		//  The only difference from the constructor above is WHO owns the Window object
		//  (even the kind default is the same Owned, aligned 2026-10-17).
		//
		//   - Constructor `Window child(host, ...)`: CALLER-owned - the caller must also handle
		//     three things: declare a named variable, place it in the right scope (must be
		//     destroyed before the host), and manage show/close lifetimes itself (unique_ptr /
		//     optional / new). The classic symptom: it can only live in main - written in a
		//     button callback, the object is destroyed when the callback returns and the panel
		//     vanishes as soon as it appears.
		//   - These APIs: LIBRARY-owned - the object goes into the host's managed table and is
		//     destroyed with the host. The caller takes a reference and uses it, never asks
		//     when it dies, never news one.
		//
		//  So "pop up a panel on demand" is shaped exactly like "add a widget".
		//
		//  Geometry is an OPTIONAL parameter (2026-10-19): the three overloads map to
		//  "all left to the library" / "position only" / "position + size", aligned with the
		//  main window's "create first, SetSize / SetPosition later" usage.
		// ===============================================================

		// Creates a child window MANAGED by this window and returns its reference.
		//
		// x,y: position relative to THIS window's client-area top-left (pixels).
		// width,height: child client size (pixels); 0 or negative = auto size (60% of the host
		//   client area, min 320x240, capped by the host client area).
		// kind: form (see ChildKind). DEFAULT Owned - what "pop up a panel / dialog" wants
		//   (decorations from the system). Matches the constructor above's default (since
		//   2026-10-17; the constructor used to default to Embedded, an inconsistency that
		//   invited mistakes).
		// borderlessAtBirth: frameless from birth (default false; same-named parameter of the
		//   Window constructor). Tear-off floaters / panels with drawn title bars should use
		//   this, NOT "create then SetBorderless" - the latter flashes a framed window for a
		//   frame.
		// Returns the child reference. Its lifetime belongs to THIS window: destroyed with it;
		// while this window lives, the reference stays valid.
		//
		// Internally it is new Window(*this, x, y, width, height, kind) placed into the host's
		// managed table, so everything except lifetime (platform allocation / shared render
		// context / message driving / theme inheritance / never call exec) is identical to the
		// constructor above.
		//
		// Creation failure (platform unsupported and the stand-in also impossible) never
		// returns an empty reference - like stack creation, it returns an object with
		// IsClosed() == true whose later calls are all safe no-ops. Use IsClosed() to test
		// whether it really came up.
		// CLOSING != DESTROYING: clicking X / Close() only closes (IsClosed() becomes true);
		// the object stays in the managed table. To reclaim objects use DestroyChildWindow;
		// for open-close patterns, batch-clean with DestroyClosedChildWindows.
		// Do NOT destroy a child window from inside its own callback - the callback stack is
		// still standing on its members. If needed, defer the destruction to the host's
		// callback (or the next interaction).
		//
		// See DestroyChildWindow, DestroyClosedChildWindows, ChildKind.
		Window& CreateChildWindow(int x, int y, int width, int height,
			ChildKind kind = ChildKind::Owned, bool borderlessAtBirth = false);

		// Creates a managed child window, giving only the position; the library picks the size.
		// x,y: position relative to this window's client-area top-left (pixels).
		// kind: form (default Owned).
		// Size = 60% of the host client area (min 320x240, capped by the host client area);
		// change it later with SetSize(w, h) as usual. Same as Window(parent, x, y, kind).
		Window& CreateChildWindow(int x, int y, ChildKind kind = ChildKind::Owned);

		// Creates a managed child window with ALL geometry left to the library (recommended).
		// kind: form (default Owned).
		// Same shape as the main window Window(): create first, then set size / position
		// separately. Default geometry = 60% of the host client area (min 320x240, capped),
		// centered in the host client area.
		// See Window(Window&, ChildKind), SetSize, SetPositionRelativeToHost, CenterInHost.
		Window& CreateChildWindow(ChildKind kind = ChildKind::Owned);

		// Destroys a managed child window created by CreateChildWindow.
		// child: the target; silently ignored (idempotent) when it is NOT managed by this window.
		// Destroys immediately, no dangling: the reference is invalid afterwards; do not touch it.
		// Passing a child created elsewhere (stack object / managed by another host) does
		// NOTHING - the library never destroys objects it does not own.
		void DestroyChildWindow(Window& child);

		// Destroys ALL closed managed child windows at once.
		// Returns how many were destroyed.
		// Cleanup for open-close usage: after the user closes a panel with X, the object itself
		// lingers (closing is not destroying) and accumulates memory. Call once from the HOST's
		// callback.
		// The test is IsClosed(), so it never touches child windows that are still open.
		int DestroyClosedChildWindows();

		// Number of currently managed child windows (including closed but not destroyed).
		int childWindowCount() const;

		// Copy and move are deleted (window identity is unique; the PImpl holds HWND/GL resources)
		Window(const Window&) = delete;             // No copy construction
		Window& operator=(const Window&) = delete;  // No copy assignment
		Window(Window&&) = delete;                  // No move construction
		Window& operator=(Window&&) = delete;       // No move assignment

		// Shows the window and enters the message loop.
		// Returns the window exit code (0 = normal close).
		// The loop renders ALL shown and alive windows every frame (with multiple windows
		// coexisting, calling exec() on the main window drives the child windows' rendering and
		// input too); returns when THIS window is closed (PostQuitMessage ends the process when
		// ALL main windows are closed - desktop gadgets and other attachment windows do not
		// hold the process; see GadgetDesc::keepsAppAlive).
		//
		// CHILD WINDOW INHERITANCE changes when the loop ends: when the host closes but a child
		// with SetInheritOnParentClose inherited successfully, this loop CONTINUES and keeps
		// driving the heir (OnDemand's per-frame scheduling also keeps scheduling for it) -
		// returning only when the last live window (possibly the heir itself) closes.
		int exec();

		// ===============================================================
		//  Single-step driving: you own the loop (the library does not take the thread)
		// ===============================================================
		//
		//  exec() is BLOCKING: it holds the calling thread in its message loop until all
		//  windows close. Ordinary GUIs should be exactly that (sleep in the kernel when idle,
		//  zero CPU), but one class of programs is not: audio/video decoding, physics
		//  simulation, capture-card callbacks, game loops, and canvases Presenting every frame
		//  through an external renderer. Their common trait is "the thread already has its own
		//  while loop" - once exec() is entered, there is no way out.
		//
		//  That is what PollEvents() is for: pump one round and return; the thread stays yours.
		//
		//  Do NOT use exec() and PollEvents() on the SAME window - that is two loops fighting
		//  over one thread queue (same rule as "child windows never run their own loop").
		//
		//  PollEvents(0) sleeps 0 ms: with no messages it returns immediately, so the loop must
		//  pace itself (vsync blocking in Present, or RunRealtime / your own sleep), otherwise
		//  it pins a core at 100%.

		// Single-step drive: processes a batch of messages + paints a frame if needed, then
		// RETURNS immediately.
		//
		// waitMs: maximum wait in milliseconds:
		//   - 0 (default): non-blocking - process messages if any, return immediately if not
		//     (PeekMessage semantics);
		//   - -1: sleep until there is something to do (MsgWaitForMultipleObjects blocking
		//     wait, zero CPU when idle);
		//   - n > 0: wait at most n ms.
		// Returns true = the window is alive, keep looping; false = time to exit (window
		// closed / quit requested) - the caller should end its own loop immediately.
		//
		// Semantics are IDENTICAL to exec()'s main loop - the same implementation
		// (Impl::pumpOnce), so timers (SetInterval / SetTimeout), cross-thread posting, input
		// dispatch, Immediate / OnDemand per-frame scheduling and RequestFrameWake all work as
		// usual. The only difference: it does NOT take over the thread; it returns after this
		// round.
		//
		// The first call lazily completes initialization (font faces -> render backend -> first
		// frame), so it may be slower than later calls (equivalent to the head of exec()).
		//
		// When the window is already closed, returns false immediately; never hangs.
		// See ShouldQuit, RunRealtime, RenderNow.
		bool PollEvents(int waitMs = 0);

		// Whether the caller's loop should end (window closed / quit request received).
		// Simply the negation of PollEvents()'s return value, for `while (!win.ShouldQuit())`.
		bool ShouldQuit() const;

		// Draws one frame immediately (no dirty flags, no pacing).
		//
		// For "I changed a bunch of widgets in my own loop and want to see it NOW". In normal
		// event-driven mode RequestRedraw() is enough (the next PollEvents catches up); only
		// needed when a single PollEvents(0) changed many things and you control commit timing.
		//
		// No-op on windows whose rendering was handed away (SetRenderEnabled(false)) - that
		// window's content is not Kit's to draw (canvases).
		void RenderNow();

		// Real-time frame loop: same thread, the library does not block; calls back every frame.
		//
		// onFrame: per-frame callback, signature void(double dt) (dt = seconds since the last
		//   frame).
		// fps: target frame rate. 0 (default) = unthrottled (the pace is set by your own
		//   Present / vsync); > 0 = throttled to that rate (internally PollEvents(remaining ms)
		//   waits for the next beat; messages are still pumped during the wait).
		// Returns the exit code (always 0; the real "exit reason" is the window's).
		//
		// Each round does three things: callback -> pump messages clean -> sleep to the next
		// beat. To leave early, call win.Close() from your own callback.
		//
		// The throttle "sleep" is a PURE sleep, not "sleep until a message" - the latter never
		// reaches the full interval when messages keep arriving (measured: a 60fps target ran
		// at 80fps). Matches vsync behavior: no pumping during the interval; one clean batch at
		// the start of the next round.
		// Millisecond sleep precision is required, so fps > 0 requests the system's 1ms timer
		// resolution (timeBeginPeriod(1), returned at window destruction).
		//
		// It is STILL a blocking call (the loop runs until the window closes) - but this time
		// it is the loop YOU asked for, not one the library secretly runs. For full control of
		// every round, write `while (win.PollEvents(0)) { ... }` directly.
		int RunRealtime(const std::function<void(double)>& onFrame, int fps = 0);

		// Shows the window.
		// Re-shows a hidden window; no effect when already shown.
		void Show();

		// Closes the window and exits the message loop.
		// Equivalent to clicking the title bar's close button; the window object still exists
		// but no longer processes events.
		void Close();

		// Hides the window (does not exit the message loop).
		// The window stays in memory and can be re-shown via Show(); for temporary hiding.
		void Hide();

		// Enables / disables THIS window's own rendering (default on).
		//
		// When off, this window draws NOTHING: no clear, no frame collection, no Present - the
		// render backend (GL context / Vulkan device) is not even initialized - so the HWND's
		// presentation right is entirely yours. This is the switch for "hand a native window
		// over to an external renderer as a canvas"; Kit::CanvasView works exactly through it.
		//
		// What remains when off: the window itself, the message loop, input dispatch, geometry
		// APIs - just no drawing. So "draw with an external renderer" and "draw widgets with
		// Kit" are mutually exclusive usages; one window can only pick one.
		//
		// on: true = Kit renders again (default); false = hand it away.
		// Must be called BEFORE the first draw to matter: once the backend exists (drawing
		// after the first Show()), the graphics context is bound to that HWND. Turning it off
		// after drawing only stops drawing; the context is not torn down.
		// RequestRedraw() and animation hooks still exist but produce no drawing - canvas
		// content is yours to refresh (Present yourself).
		// Mobile stand-ins have no independent context: this switch is a silent no-op there.
		//
		// Typical usage (simpler with CanvasView): create the child window, SetRenderEnabled(false)
		// FIRST, then Show() (on screen but Kit does not draw), then take hwnd() to build your
		// own D3D12 / Vulkan swap chain.
		//
		// See isRenderEnabled, Kit::CanvasView.
		void SetRenderEnabled(bool on);

		// Whether Kit renders this window (default true; see SetRenderEnabled).
		bool isRenderEnabled() const;

		// ===============================================================
		//  Lifecycle state (IsClosed is the gatekeeper of all window APIs)
		// ===============================================================
		// Whether the window is closed.
		// Both paths make it true: the user clicked X / Close() was called (WM_CLOSE ->
		// DestroyWindow -> WM_DESTROY; the native handle is destroyed at that point), or the
		// Window object was destructed.
		// Calling Show() / hwnd() / geometry APIs after close is SAFE - the gatekeeper turns
		// them all into silent no-ops or empty returns; no dead-handle access.
		bool IsClosed() const;

		// Semantic alias for !IsClosed().
		bool IsValid() const { return !IsClosed(); }

		// ===============================================================
		//  Window geometry (queries are always client-area pixels; SetSize takes client size)
		// ===============================================================
		// Client-area size (the reference for widget layout / texture sizes / DPI conversion).
		// w,h: out params; written as 0, 0 when the window is closed.
		// Returns PHYSICAL pixels (the library declares Per-Monitor V2 DPI awareness) - the
		// same coordinate system as glViewport and widget coordinates; use directly, no
		// conversion.
		void GetClientSize(int& w, int& h) const;

		// Client-area width (single-value convenience for GetClientSize; 0 when closed).
		int width() const;

		// Client-area height (0 when closed).
		int height() const;

		// Sets the client-area size.
		// w,h: target CLIENT-AREA size (clamped to a minimum of 1) - UI authors think in
		// "how big is the canvas"; the frame / title-bar thickness is added automatically
		// from the current window style.
		// WM_SIZE paints a frame in place, so the canvas follows the new size immediately.
		// After resizing, the window re-centers within the CURRENT MONITOR's work area
		// (outside the taskbar): growing no longer drifts down-right, shrinking no longer
		// drifts up-left, no monitor jumping on multi-monitor. Minimized / maximized: resize
		// only, position untouched.
		// To resize without centering: SetSize then SetPosition(x, y) to override.
		// CHILD WINDOWS (incl. Embedded) may call this anytime: it only changes size, NEVER
		// position (screen-work-area centering is top-level semantics and would move the child
		// out of the host). Child windows re-center via CenterInHost.
		void SetSize(int w, int h);

		// Moves the window to screen coordinates (x, y) (outer-frame top-left, size unchanged).
		// A child window's coordinate system is NOT the screen: for Embedded (WS_CHILD) this
		// takes HOST CLIENT-AREA coordinates; Owned / Popup are real top-level windows so this
		// is screen coordinates - but for "relative to the host client area" use
		// SetPositionRelativeToHost (that is the right way to move together with the host).
		void SetPosition(int x, int y);

		// Shrinks the client area to "just fits all top-level widgets" (adaptive size).
		//
		// For each top-level widget: an explicit SetSize wins; otherwise its self-reported
		// NATURAL size (hintWidth() / hintHeight(), see the measurement protocol). The client
		// size is the UNION of all widgets' right/bottom edges.
		//
		// SetFillWindow() widgets are SKIPPED - they stretch with the window; counting them in
		// "how big should the window be" is self-referential.
		// Top-level widgets stack at the top-left by default, hence union not sum. For vertical
		// stacking, put them into a VBox / FlowLayout first and adjust that container.
		// Does NOT change the window size when there are no participating widgets (or none
		// visible).
		// Call after Show() - client size only means something for a real window.
		void adjustSize();

		// Measures the visual size of a text run (for widgets to self-report natural size at
		// LAYOUT time).
		//
		// Measurement and drawing share the same glyph metrics (bearingX / advanceX), so sizes
		// computed from it match the drawn text EXACTLY - no "box slightly narrower / wider
		// than the text".
		//
		// Why this entry exists: text metrics were only available at draw time (FrameCollector
		// is a parameter of Paint), while "widget self-reports natural size"
		// (hintWidth() / hintHeight()) happens at LAYOUT time - when no FrameCollector exists.
		// This method lends out the window's font renderer so widgets can ask how big the text
		// is at layout time.
		//
		// utf8: text to measure (UTF-8); font: font description (Label / Button / InputBox
		// font attributes construct it directly).
		// Returns visual width / line height / ascent / descent; ALL ZEROS before Show().
		TextMetrics MeasureText(std::string_view utf8, const FontDesc& font) const;

		// Centers the window horizontally and vertically in its monitor's work area
		// (outside the taskbar).
		// No effect while minimized / maximized.
		// Child windows are automatically redirected to CenterInHost (reference frame is the
		// host client area) - screen-centering a child only throws it outside the host, which
		// no caller wants. Both names work; each has clear semantics: screen is screen, host
		// is host.
		void CenterOnScreen();

		// Centers within the HOST CLIENT AREA (child windows only).
		//
		// Dependent windows (Owned / Popup / Embedded) speak "relative to the host client
		// area", so "centered" has exactly one sensible meaning for them: land at the host's
		// center. CenterOnScreen works on the monitor work area - applied to a dialog it would
		// throw it to the middle of the SCREEN, decoupled from the host (glaring on
		// multi-monitor).
		//
		// Size comes from GetClientSize() (client area); position is delivered via
		// SetPositionRelativeToHost - so "SetSize first, CenterInHost after" is the standard
		// "resize then re-center".
		//
		// On TOP-LEVEL windows it degrades to CenterOnScreen (no host; the screen is the
		// reference).
		// Unaffected by SetCenterOnResize: that is the "also center when resizing" switch;
		// this method is an explicit action.
		void CenterInHost();

		// Returns the work area of the monitor THIS WINDOW is on (screen coordinates, taskbar
		// excluded).
		// x,y,w,h: work-area rect (physical pixels). Returns true = got it; false = the
		// platform lacks this capability.
		//
		// The query entry for windows ANCHORED TO SCREEN CORNERS (first user: Kit::ToastNotifier
		// - notification windows sit on the screen's corners and resize every frame, so they
		// need a REPEATABLY queryable interface; CenterOnScreen() is an action, not a query,
		// and does not serve).
		//
		// Returns the work area of the monitor the window is ON (follows the window on
		// multi-monitor), not the primary monitor and not the full screen (a notification bar
		// covering the taskbar is occlusion, not a notification).
		//
		// On failure returns false and leaves the out params UNCHANGED - callers (notification
		// windows) fall back to "position relative to the host client area", so platforms
		// without this capability are not rendered unusable.
		bool GetScreenWorkArea(int& x, int& y, int& w, int& h) const;

		// Client-area origin in SCREEN coordinates (the bridge for cross-window coordinate math).
		// Dock panels "tearing off into a floater / docking back" must refer to the same point
		// across two windows' client coordinate systems:
		// screen = A.origin + pointInA = B.origin + pointInB.
		// Platforms without a screen concept (drawn stand-ins) always return (0,0).
		void GetClientOrigin(int& x, int& y) const;

		// Whether programmatic resizes (SetSize) also move the window to the work-area center.
		// on: true = center (DEFAULT, matches existing behavior); false = resize only, position
		// untouched.
		//
		// The default re-centering exists for the look of a SINGLE resize (no down-right
		// drift), but during CONTINUOUS resizes it is a disaster: every resize moves the whole
		// window, and what the user sees is not "the window growing" but "the entire window
		// jumping / shaking".
		//
		// And there is no alternative formulation - SetSize followed by SetPosition can correct
		// the position, but that is already a SECOND move (jump to center, jump back), and the
		// jump in between is still visible. In-place resizing is only possible via this switch.
		//
		// When off, SetSize and CenterOnScreen are fully decoupled: call the latter explicitly
		// to center. Minimized / maximized states never move position anyway - unrelated to
		// this switch.
		// See SetSize, SetPosition, CenterOnScreen.
		void SetCenterOnResize(bool on);

		// Whether programmatic resizes also center (default true).
		bool centerOnResize() const;

		// DPI scale factor (96 DPI basis: 1.0 / 1.25 / 1.5 / 2.0 ...).
		// Returns 1.0f when the window is closed.
		// The library declares Per-Monitor V2 awareness: the client area is already physical
		// pixels; use this for DPI-aware layout (1px strokes, pixel alignment), e.g.
		// int pen = static_cast<int>(1 * win.GetScaleFactor());
		// Internally GetDpiForWindow (runtime-loaded, Win10 1607+); older systems fall back to
		// the system DPI.
		float GetScaleFactor() const;

		// The window's current DPI (96 = 100% scaling, 150% = 144); returns 0 when closed.
		int GetDpi() const;

		// ===============================================================
		//  Window state / title / redraw
		// ===============================================================
		// Sets the title bar text (still changeable anytime after construction).
		// title is UTF-8 (unified across platforms; Windows converts to UTF-16 internally,
		// other platforms use it directly).
		void SetTitle(const std::string& title);

		// Requests one redraw (the on-demand redraw entry).
		// Only posts the redraw request into the window queue (InvalidateRect); actual drawing
		// still happens in the UI thread's WM_PAINT - safe to call cross-thread (it only
		// enqueues, never touches GL).
		// The current rendering strategy is "paint a frame when the message queue is empty";
		// this API serves self-managed message loops and is the reserved entry for a future
		// switch to fully on-demand redraw (CPU savings).
		void RequestRedraw();

		// Present throttling (swap interval): -1 adaptive / 0 never wait for vblank / 1 always.
		//
		// Default -1 (adaptive): input seen within the last 300ms -> do not wait, otherwise
		// wait once.
		//
		// Why the default does not always wait: waiting makes SwapBuffers / Present BLOCK the
		// UI thread for up to 16.7ms. During drags the mouse sends every 8ms while the UI
		// thread is stuck ~14.7ms per frame - input can be processed at best every 16.7ms;
		// that is the direct cause of "rendering cannot keep up with the mouse" (visible with
		// ZEKIT_TRACE_FRAME=1: paint 0.07ms, draw 14.7ms). No amount of draw-cost optimization
		// can fix that block.
		//
		// Tearing: in windowed mode (flip model + DWM composition) not waiting only means "not
		// blocking"; presentation is still composited by DWM - the same reasoning as "swap
		// interval 0 while dragging the border". Exclusive fullscreen / compositor-less X11 can
		// genuinely tear; pass 1 explicitly there.
		//
		// mode: -1 = adaptive (recommended); 0 = never wait (lowest latency); 1 = always wait
		// (most conservative).
		// No conflict with SetDriveMode: Immediate mode schedules frames by rate; this only
		// controls "wait for vblank at the end of each frame or not".
		void SetSwapInterval(int mode);

		// Schedules a "timed wakeup repaint" (for content that animates by itself).
		// after: how long from now until the wakeup (ms; 0 / negative = wake immediately on
		// the next loop round).
		// In EventDriven mode the main loop usually sleeps in the kernel (zero CPU), but
		// SELF-ANIMATING content needs to wake on a timer to paint the next frame - the
		// classic case is the input box's caret blink (half-period on/off). This hands a timer
		// to the platform layer: at the deadline the system sends a wake message, the main
		// loop paints one frame and SLEEPS AGAIN - no Immediate-style constant CPU burn.
		//
		// Semantics are ONE-SHOT: it expires after waking and painting one frame; to keep
		// moving, request again each frame (widgets renew on demand while drawing).
		//
		// MULTIPLE REQUESTS = KEEP EARLIEST: with a request pending, a new one that is not
		// earlier is ignored, an earlier one moves the timer up. That is "among all unexpired
		// requests, wake at the earliest" rather than "later requests override earlier ones".
		// Multiple widgets can each request at their own tempo (animation every 16ms, input
		// caret every 530ms) without stepping on each other - wakeups follow the earliest and
		// each side renews after its turn.
		//
		// Like RequestRedraw this is "enqueue" semantics, thread-safe (no GPU touched).
		// Immediate mode paints by frame rate anyway and OnDemand scheduling is handled by the
		// host via Widget::wantsAnimationFrame() - in those modes this API is meaningless but
		// harmless (only EventDriven needs it for DISCRETE timed catch-up frames).
		// See DriveMode, Widget::wantsAnimationFrame.
		void RequestFrameWake(int afterMs);

		// Cancels a timed wakeup that has not expired (the undo of RequestFrameWake; no-op when
		// none pending).
		void CancelFrameWake();

		// =================================================================
		//  Library-driven timers - "keep updating" without writing your own loop
		// =================================================================
		//  Why this group must exist: the most common app need is "refresh quotes every 200ms /
		//  update a clock every 1s". In other frameworks that becomes
		//  while (running) { update(); sleep(200); } - and on the GUI thread such a loop
		//  STRANGLES the message loop (window stops responding, painting stops, buttons dead).
		//  A background polling thread hits "touching widgets cross-thread". The only correct
		//  shape: hand the beat to the library, which executes in the message loop's idle time
		//  slices - callbacks always on the UI thread, never blocking.
		//
		//  Relation to the other "request" APIs of this class:
		//   - RequestFrameWake - one-shot catch-up frames (caret-blink style half-period flips);
		//   - this group - PERIODIC tasks whose callback runs when due (mutating widgets in the
		//     callback -> setters mark dirty -> the next frame paints naturally; no manual
		//     RequestRedraw needed).
		//
		//  Precision: millisecond-level, NO frame catching up. When a callback overruns its
		//  interval, the next one starts timing from "now" (no backlog of stale ticks) - UI
		//  refresh wants the LATEST state; replaying a pile of expired ticks only makes the
		//  interface drag further behind.
		// =================================================================

		// Timer task handle (ClearTimer uses it to cancel; 0 = invalid).
		using TimerId = uint64_t;

		// Registers a PERIODIC task: runs on the UI thread every intervalMs milliseconds.
		// intervalMs: interval (ms, minimum treated as 1).
		// fn: callback (runs on the UI thread; safe to mutate widgets).
		// Returns the handle (pass it to ClearTimer); 0 = window closed / empty callback, not
		// registered.
		//
		// The general foundation of "data-driven updates". Typical usage is one line:
		// win.SetInterval(200, [&label]{ label.SetText(fetchQuote()); })
		// No threads, no sleep, no blocked message loop - the library schedules it into the
		// main loop's time slices; it runs when due and a frame paints naturally afterwards.
		//
		// Changing the SAME task's interval from inside its callback is NOT supported:
		// ClearTimer and re-register instead.
		// Callbacks may safely ClearTimer (including cancelling themselves) - iteration uses a
		// snapshot.
		// All tasks are voided automatically when the window closes; no manual cleanup.
		// See SetTimeout, ClearTimer, Kit::LiveText.
		TimerId SetInterval(int intervalMs, std::function<void()> fn);

		// Registers a ONE-SHOT task: runs on the UI thread once after delayMs milliseconds.
		// delayMs: delay (ms); fn: callback (UI thread).
		// Returns the handle (ClearTimer can cancel before it fires); 0 = not registered.
		// For finishers like "collapse the toast after 300ms" - far cleaner than a sleeping
		// thread.
		TimerId SetTimeout(int delayMs, std::function<void()> fn);

		// Cancels a timer task that has not fired yet (periodic tasks stop running once cancelled).
		// id: the handle returned by SetInterval / SetTimeout.
		// Idempotent: unknown ids / already-fired / 0 all fine, no error.
		// May be called from the task's OWN callback (cancelling itself); iteration uses a
		// snapshot.
		void ClearTimer(TimerId id);

		// Posts a function from ANY thread onto the UI thread.
		// fn: what to do on the UI thread (validity of captured data is the caller's job).
		//
		// After a background thread fetches data, this is the only safe way onto the screen:
		// do the slow work in the background, then win.Post([&]{ table.SetRows(rows); }) to
		// hop back to the UI thread.
		//
		// Semantics: guaranteed to run exactly once EVENTUALLY, not immediately (the platform
		// wake message nudges the loop; in EventDriven that is typically one kernel-wakeup
		// delay). Posts made after the window closed are dropped.
		//
		// Do NOT capture soon-to-dangle references in the callback (a background thread
		// outliving UI objects is the caller's responsibility). The library does not manage
		// lifetimes for you - standard C++.
		// This function IS thread-safe; SetInterval / SetTimeout / ClearTimer are NOT (they
		// are UI-thread interfaces, same tier as widget setters).
		void Post(std::function<void()> fn);

		// =================================================================
		//  File drag-drop (drag files from the explorer into the window)
		// =================================================================
		//  TWO MODES, differing only in "what counts as the drop point":
		//
		//  | Mode         | How to enable                    | Drop point |
		//  | Zone drop    | Widget::SetAcceptFileDrop()      | Only those widgets; elsewhere the
		//  |              |                                  | forbidden cursor shows and releasing
		//  |              |                                  | does nothing |
		//  | Window drop  | this class's SetOnFileDrop       | The whole window (blank areas too) |
		//
		//  Both can be used together; priority is "innermost widget -> window callback": over
		//  a drop zone it is a zone drop, elsewhere a window drop.
		//
		//  "Does the cursor show droppable" is decided in exactly ONE place (not so before
		//  2026-10-30): Kit resolves the drop point and hands the verdict to the platform layer
		//  (Plat::IWindowHost::AcceptsFileDropAt), which answers DROPEFFECT_NONE /
		//  DROPEFFECT_COPY. So "copy cursor outside the zone but nothing happens on release" -
		//  that lying state cannot recur.
		//  Widgets may also reject by file CONTENT: OnFileDragEnter returning false suffices
		//  (FileDropZone's extension whitelist does this); Kit then asks along the parent chain.
		// =================================================================

		// Opens this window's drag-drop channel (default off; idempotent).
		// on: true = open; false = close.
		// While closed, the system will not deliver drops to this window (saves a pointless
		// COM round trip), so it MUST be open to receive events. Widget-level drops do not
		// need it - declaring SetAcceptFileDrop() on a widget opens the window channel
		// automatically.
		// This switch only decides "channel open or not", NOT "what counts as a drop point":
		// whole-window acceptance comes from SetOnFileDrop (see the table above).
		// Silently ineffective on unsupported platforms (check Supports(Capability::FileDrop)).
		void SetAcceptFileDrop(bool on = true);
		// Whether this window's file drag-drop channel is open.
		bool acceptsFileDrop() const;

		// Window-level drop callback - registering it switches the window to "window drop" mode.
		// cb: receives the dragged absolute paths (UTF-8), at least one.
		// Drop-point priority: the innermost drop widget -> this callback. So the same window
		// can run zone logic inside the drop zone and whole-window logic elsewhere.
		// Registration auto-opens the channel (SetAcceptFileDrop); conversely, an open channel
		// without a registered callback accepts NOWHERE (the system cursor honestly shows the
		// forbidden sign).
		// This callback has NO Enter / Over / Leave phases - for drag-in highlighting use a
		// widget-level drop zone (Kit::FileDropZone), which receives all four phases.
		// Widget drop callbacks: see Widget::OnFileDrop / Kit::FileDropZone.
		void SetOnFileDrop(std::function<void(const std::vector<std::string>&)> cb);

		// =================================================================
		//  Native file dialogs
		// =================================================================

		// Pops the system's native file dialog (synchronous / modal).
		// desc: the request description (see ZeFileDialog.h).
		// out: the chosen absolute paths (UTF-8).
		// Returns true = user confirmed (out has at least one); false = cancelled / platform
		// unsupported.
		//
		// For daily use go straight to Kit::FileDialog (chained config + result wrapper); this
		// function is its ground-floor API, for when a FileDialogDesc already exists (config
		// from a file / network / command line).
		//
		// Unsupported platforms return false and print one stderr line - checking
		// Supports(Capability::NativeFileDialog) first avoids that noise.
		// The dialog's owner internally resolves to the TOP-LEVEL window (passing a child is
		// also correct).
		bool ShowFileDialog(const FileDialogDesc& desc, std::vector<std::string>& out);

		// Minimizes to the taskbar.
		void Minimize();

		// Maximizes (restores first when minimized).
		void Maximize();

		// Restores from maximized / minimized to normal size.
		void Restore();

		// Topmost (true) / cancel topmost (false).
		// A hard requirement for tool panels / floating windows; toggling does not steal focus
		// (NOACTIVATE).
		void SetTopmost(bool on);

		// ===============================================================
		//  Size limits / appearance / mouse queries
		// ===============================================================
		// Limits the window's minimum size (CLIENT-AREA pixels, same coordinate system as SetSize).
		// w,h: minimum size; (0, 0) removes the limit.
		// Binds both border dragging (WM_GETMINMAXINFO clamping) and SetSize (clamped when set);
		// no effect on minimized / maximized states.
		void SetMinimumSize(int w, int h);

		// Limits the window's maximum size (client-area pixels); (0, 0) removes it.
		// Only constrains USER border dragging and programmatic SetSize; the system "maximize"
		// button and frameless fullscreen (SetFullscreen) are NOT limited
		// (use SetResizable(false) to disable maximize as well).
		// Implemented in WM_SIZING (interactive dragging) rather than WM_GETMINMAXINFO's
		// ptMaxTrackSize - the latter Windows also applies to dragging, maximizing and
		// SetWindowPos, so writing there makes "maximize and fullscreen stop working once a
		// max size is set". Known limit: Aero Snap (Win+arrows, edge snapping) goes through
		// WM_WINDOWPOSCHANGING and is not constrained by this limit.
		void SetMaximumSize(int w, int h);

		// Allows (true) / forbids (false) the user resizing by dragging borders.
		// Toggles WS_THICKFRAME + WS_MAXIMIZEBOX; programmatic SetSize is unaffected.
		// When the maximize button was disabled via SetMaximizeButtonEnabled(false),
		// SetResizable(true) only adds WS_THICKFRAME and does not revive that button.
		void SetResizable(bool on);

		// ===============================================================
		//  Title bar / system buttons (works for drawn and system title bars alike)
		// ===============================================================

		// Shows / hides the system title bar (title text + button area as a whole).
		// visible: true = show (default); false = hide (the window stays border-resizable).
		// Bit-wise WS_CAPTION | WS_SYSMENU add/remove, keeping WS_THICKFRAME - hide the title
		// bar and combine with StartWindowDrag() for a drawn title bar that drags.
		// While borderless (SetBorderless(true)) / fullscreen, only state is recorded; it takes
		// effect automatically on exit.
		void SetTitleBarVisible(bool visible);

		// Enables / disables the minimize button.
		// Removing WS_MINIMIZEBOX hides the button and grays out SC_MINIMIZE in the system
		// menu; WndProc interception of SC_MINIMIZE covers paths that bypass the button
		// (Win+Down / system menu / programmatic SendMessage(WM_SYSCOMMAND)).
		//
		// The style bit only controls "is the button drawn"; "is it allowed" is a separate
		// gate - the two must agree, otherwise "no button but still minimizes" (exactly how
		// Owned child windows minimized to the screen's bottom-left corner).
		// ChildKind::Owned child windows default to ENABLED (matching main windows). Do not
		// want it in the taskbar? That is ChildKind::Popup, or just use Panel - this API only
		// touches the button and the gate, never taskbar membership.
		void SetMinimizeButtonEnabled(bool enabled);

		// Enables / disables the maximize button.
		// Shares the WS_MAXIMIZEBOX bit with SetResizable: once this switch turned it off,
		// SetResizable(true) does NOT grow the button back (it checks this switch first).
		// ChildKind::Owned child windows default to enabled (matching main windows).
		void SetMaximizeButtonEnabled(bool enabled);

		// Enables / disables the close button (X / Alt+F4 / Close() all become ineffective).
		// Win32's title-bar X has no "grayed out" style bit - the recipe is: gray out SC_CLOSE
		// in the system menu + WndProc interception (HTCLOSE press, SC_CLOSE, WM_CLOSE) - three
		// gates.
		// Close()'s semantics are "simulate the user clicking X" (also PostMessage(WM_CLOSE)),
		// so after disabling, programmatic Close() is ineffective too - a feature, not a bug;
		// to force destruction, destruct the Window object directly.
		void SetCloseButtonEnabled(bool enabled);

		// Reads back the enable state of the three title bar buttons (modal takeover / restore
		// needs it).
		bool MinimizeButtonEnabled() const;
		bool MaximizeButtonEnabled() const;
		bool CloseButtonEnabled() const;

		// ===============================================================
		//  Enable / disable (the ground truth of real modality)
		// ===============================================================

		// Enables / disables this window.
		//
		// enabled: false = disable, true = restore.
		//
		// While disabled, the system stops dispatching mouse / keyboard to this window
		// (Win32 = EnableWindow(host, FALSE)): the whole window grays out, clicks dead,
		// keyboard blocked. This is the CORRECT way to realize modal dialogs - the operating
		// system guarantees input never leaks to the host; the library need not filter events
		// itself.
		//
		// Child windows OWNED by this window (ChildKind::Owned) are NOT affected - they keep
		// receiving input, so the modal dialog itself stays clickable.
		// Platforms without a window enable state (mobile stand-ins): a no-op; modality there
		// is guaranteed by other means (geometry claiming).
		// Idempotent. On restore, the foreground activation is handed back to this window
		// (otherwise the user sees the title bar stay gray forever).
		// See isEnabled, ModalDialog.
		void SetEnabled(bool enabled);

		// Whether this window is currently enabled (default true).
		// Platforms without SetEnabled support always return true (that is the fact "always
		// usable", not an error).
		bool isEnabled() const;

		// Sets the "window closed" callback (fired once when the native window is really gone).
		//
		// cb: the callback; its parameter is THIS window (the sender). Empty clears it.
		//
		// The trigger point is single: the platform layer destroyed the native window and
		// IWindowHost::OnNativeClosed ran - title-bar X, Alt+F4, programmatic Close(), host
		// destruction, ANY close path reaches it. IsClosed() is already true when it fires.
		//
		// This is a LIFECYCLE notification, not a "value change" - programmatic closes fire
		// too.
		// Do NOT destroy this window object inside the callback (still on the platform layer's
		// callback stack). To reclaim managed children use DestroyClosedChildWindows.
		// See ModalDialog (it uses this callback to restore the host when the dialog is closed
		// via X).
		void SetOnClosed(std::function<void(Window&)> cb);

		// Sets the "close request" callback - asked BEFORE the window actually closes.
		//
		// cb: the callback; its parameter is this window. Return true = allow the close,
		// false = block it. Empty clears it (= always allow).
		//
		// Trigger: X / Alt+F4 / Close() all land on the platform's Close event, which consults
		// this BEFORE letting the system destroy the window. That gives "unsaved changes,
		// confirm first" a home - pop a confirmation dialog in the callback and return false
		// to block when the user declines.
		//
		// Division of labor with SetCloseButtonEnabled: that one makes the window's X dead
		// entirely - a STATIC switch; this callback is a DYNAMIC question (decide by current
		// state).
		// To close after blocking, call Close() as usual (the callback then returns true).
		// A disabled window (SetEnabled(false)) receives no close requests - the system does
		// not dispatch them.
		void SetOnCloseRequest(std::function<bool(Window&)> cb);

		// Reads back the current close-request hook (never installed = empty function).
		// Lets library components that need to temporarily take over close confirmation
		// (e.g. modal sessions) chain: remember the old one -> install yours -> restore on
		// exit. Without it, taking over would silently wipe the app's "confirm unsaved
		// changes" hook.
		std::function<bool(Window&)> OnCloseRequest() const;

		// Starts one window drag (the standard companion of a drawn title bar).
		// Sends the current cursor position as a system caption press (WM_NCLBUTTONDOWN +
		// HTCAPTION); the system then takes over whole-window dragging - call it from
		// OnMousePress in your drawn "title bar" area.
		// No effect while fullscreen / minimized / maximized.
		void StartWindowDrag() const;

		// Sets the window icon from a NATIVE icon handle (platform-specific API).
		// hIcon: the platform icon handle (Windows = HICON, passed as void* to avoid dragging
		//   windows.h into this header); the CALLER owns and destroys it - the library does
		//   not take over; nullptr restores the default icon.
		// Cross-platform semantics do not hold (no HICON on other platforms); kept only for
		// existing code. New code: SetWindowIcon(path).
		void SetIcon(void* hIcon);

		// Sets the window icon from a FILE (taskbar / title bar / Alt-Tab).
		// path: icon file path, UTF-8 (.ico / .png / .jpg / .bmp; / separators auto-converted
		// to \).
		// .ico loads natively (multi-size ICOs take the big/small tiers separately - clearest
		// title bar); bitmap formats decode to RGBA -> premultiplied alpha -> native icon (no
		// whitened translucent edges). Icon handles are created and destroyed by THIS LIBRARY
		// (unlike SetIcon's caller-owns rule); a replacement automatically destroys the old
		// icon, and window destruction cleans up.
		// On failure (missing file / unsupported format) silently returns with one
		// OutputDebugString line; the current icon stays unchanged.
		void SetWindowIcon(const std::string& path);

		// Sets the window icon from the EXECUTABLE'S icon resources (taskbar / title bar / Alt-Tab).
		// resId: resource ID (shares the numbering with the .rc file).
		// The byte source differs per platform; caller code is identical:
		//  - Windows: .rc ICON / RCDATA resources; big/small tiers loaded from this process
		//    module at the system's standard sizes (the title bar uses the built-in 16x16 tier,
		//    no scaling);
		//  - Other platforms (Linux/X11 etc.): no resource section; instead reads bytes
		//    registered via Kit::RegisterEmbeddedResource(id, bytes, size), decodes and writes
		//    _NET_WM_ICON (with automatic 256/128/32 downsampled tiers). See ZeEmbeddedRes.h -
		//    register a png-encoded array once; the output directory needs no companion icon
		//    files.
		// Icon handles are created and destroyed by this library; replacement / window
		// destruction cleans up automatically.
		// Missing resource / unregistered ID: one log line, then a silent return; the current
		// icon stays unchanged.
		// See RegisterEmbeddedResource.
		void SetWindowIconFromResource(int resId);

		// Whole-window opacity (0.0 fully transparent ~ 1.0 fully opaque).
		// Setting back 1.0 REMOVES the layered attribute underneath - an opaque window should
		// not keep paying composition cost. For fades, call this every frame (pair with
		// RequestRedraw under EventDriven).
		// Platform differences: Windows uses layered windows; other platforms use their own
		// mechanism (or ignore).
		void SetOpacity(float alpha);

		// Mouse click-through: this window stops receiving the mouse; clicks land on the
		// windows below (true = pass through).
		//
		// The essential switch for one-way notifications like notification bars / screen
		// badges: they float above the UI yet must not cost the controls below a single click.
		// The widget layer cannot do this - Widget::HitTest returning false only governs how
		// events split INSIDE the window; once the mouse enters a native window's rectangle,
		// the system attributes messages to it regardless of its widgets.
		//
		// Real coupling with SetOpacity: on Win32 pass-through depends on layered windows
		// (WS_EX_LAYERED), while SetOpacity(1.0f) means "keep no useless layered bit". The
		// platform layer arbitrates: WHILE pass-through is on, the layered bit belongs to it
		// and SetOpacity(1.0f) will not strip it (otherwise "set opaque once -> pass-through
		// silently dies").
		// Check Supports(Capability::ClickThrough) to see whether the platform really lands it;
		// unlanded platforms are no-ops (the window keeps receiving the mouse) - no crash, no
		// error.
		// A pass-through window NEVER receives the mouse, so "click to dismiss early" is
		// impossible by design - same discipline as Kit::Toast's HitTest returning false.
		void SetClickThrough(bool on);

		// Moves the window by HOST CLIENT-AREA coordinates (dependent windows only).
		//
		// SetPosition(x, y) takes SCREEN coordinates (clear for top-level windows), but
		// dependent windows (ChildKind::Owned / ChildKind::Popup) naturally speak "relative to
		// the host client area" - where that origin sits on screen depends on title bar and
		// border thickness, unknowable at the Kit layer. Windows that follow the host
		// (notification bars, follower panels) need this.
		//
		// x,y: position relative to the HOST CLIENT AREA's top-left (pixels).
		// All three child kinds work:
		//   - Embedded - WS_CHILD native coordinates are already the parent client area; the
		//     platform does NO screen conversion (it would double-offset outside the host);
		//   - Owned / Popup - real top-level windows; the platform does one
		//     ClientToScreen(host) to fold client coordinates into screen coordinates.
		// One semantic for all three paths; callers need not know which kind they built.
		// On TOP-LEVEL windows it equals SetPosition (no host; the screen is the reference).
		// After INHERITING the host (IsInherited()) it also equals SetPosition: the host
		// client-area reference frame no longer exists. To center, use CenterOnScreen()
		// (CenterInHost() reroutes automatically).
		// Same coordinate system as the x/y parameters of Window(Window&, x, y, ...): use the
		// constructor args at creation, this method afterwards - no manual conversion.
		void SetPositionRelativeToHost(int x, int y);

		// Borderless mode (drawn-title-bar scenarios).
		// Removes the title bar and the system frame (switches to WS_POPUP); restoring puts
		// back the complete pre-entry style. Does not record SetFullscreen's style snapshot -
		// when mixing both, exit fullscreen before leaving borderless.
		void SetBorderless(bool on);

		// Enables drag-edge / drag-corner RESIZING in borderless mode (default off).
		// The switch only means something while borderless (SetBorderless(true)) - normal
		// windows already have the system frame and do not need it. When on, the platform
		// layer keeps an 8px hit band on all four edges / corners and reports them to the
		// system (WM_NCHITTEST -> HTLEFT / HTBOTTOMRIGHT ...): edge dragging, diagonal
		// dragging, auto resize cursors and min / max clamping (SetMinimumSize still applies)
		// are ALL system-native - the same feel as framed windows.
		// Harmless when left on for normal windows; auto-inactive while maximized / fullscreen.
		// The top 8px belongs to the resize band (priority above the client area) - menu bar
		// titles are vertically centered, so clicking text is unaffected; an industry-standard
		// tradeoff (system notification areas / VS do the same).
		// Aero Snap (drag to the screen edge for half-split / edge maximize) does not work in
		// the borderless form - that is WS_POPUP's nature, not this switch being lazy.
		// Unsupported platforms (mobile / other desktops): a silent no-op.
		void SetBorderlessResizable(bool on);
		// Whether borderless resizing is currently enabled (query paired with
		// SetBorderlessResizable).
		bool IsBorderlessResizable() const;

		// Frameless fullscreen (fills the window's monitor, covering the taskbar too).
		// Remembers the original outer-frame position and style on entry, restores them
		// verbatim on exit; re-entering / exiting is idempotent; no effect while minimized.
		void SetFullscreen(bool on);

		// Whether currently in fullscreen (query paired with SetFullscreen).
		// Reads Kit's OWN semantic bit (the one SetFullscreen writes), without a second
		// platform query - mobile windows are "born immersive" (StartsImmersive()) and sync
		// this bit to true at creation, so it agrees with the real screen state from the very
		// beginning.
		// Apps should NOT pair SetFullscreen with their own bool: mobile is born fullscreen,
		// and external bookkeeping is guaranteed to be one step off from Kit (the first call
		// gets swallowed by the idempotence check, looking like "no reaction").
		bool IsFullscreen() const;

		// Sets whether the window's four corners are rounded (SYSTEM-level corners, Win11+ DWM).
		// on: true = force rounded; false = force square.
		//
		// Since Win11, DWM provides a per-window corner attribute
		// (DWMWA_WINDOW_CORNER_PREFERENCE): the system clips the corners antialiased and the
		// system shadow follows the shape - noticeably better than drawn / region-clipped
		// corners; the right answer for borderless / drawn-title-bar windows.
		//
		// Framed top-level windows are rounded BY DEFAULT on Win11; borderless
		// (SetBorderless(true) -> WS_POPUP) windows default to square - the latter needs this
		// API explicitly (call SetBorderless(true) then SetRoundedCorners(true)).
		//
		// The real radius is theme-decided (not settable per pixel). Win10 and below lack the
		// attribute -> silent no-op (stays square, no error). Mobile "drawn stand-ins" have no
		// native window -> silent no-op.
		// The attribute hangs on the HWND and survives frame-style switches - one set is
		// persistent.
		// See SetBorderless, IsRoundedCorners.
		void SetRoundedCorners(bool on);

		// Whether the window currently shows rounded corners (truth: DWM's stored preference;
		// always false on Win10 / unimplemented).
		bool IsRoundedCorners() const;

		// Client-area coordinates of the last mouse event ((-1, -1) if no mouse message ever).
		// Under event-driven mode this reads the cached "last WM_MOUSEMOVE" - after a drag
		// takes the cursor out of the window it stays at the edge, not a live global position
		// (use IsMouseButtonDown for live checks).
		void GetMousePosition(int& x, int& y) const;

		// Whether a given mouse button is currently pressed (for out-of-window drag checks /
		// hold detection).
		// Internally GetAsyncKeyState queries the PHYSICAL key state asynchronously,
		// independent of the message stream - the true value even when the loop stalls
		// mid-press.
		// Mobile has no mouse buttons, but for MouseButton::Left it returns "is a finger
		// touching right now" - the closest semantic to what drag logic wants.
		bool IsMouseButtonDown(MouseButton b) const;

		// ===============================================================
		//  Auto hover tooltips (the unified rhythm behind Widget::SetTooltip)
		// ===============================================================
		//  Widgets only store text (Widget::SetTooltip); the bubble and its rhythm belong to
		//  the WINDOW: at the next position-state recompute the window walks the parent chain
		//  for the innermost widget carrying a tip and hands it to the internal auto bubble
		//  (Tooltip, lazily created on first need, fixed name __zekit_auto_tooltip). UIs full
		//  of tips need no hand-new'd widgets, no AddWidget, no attaching a pile of targets.
		//
		//  Coexists with manual Tooltip::Attach without interference: that one is your own
		//  bubble, this one is the window's.
		// ===============================================================

		// How long to hover before the tooltip pops (ms; default 400, <= 0 = immediate).
		// Only affects AUTO tips (Widget::SetTooltip); manual Tooltip::ShowFor always shows
		// immediately, and Tooltip::SetShowDelay governs the Attach path.
		void SetTooltipDelay(int ms);
		int  tooltipDelay() const;

		// Auto-tooltip master switch (default on).
		// Turning it off also dismisses a currently shown bubble; manual Tooltips unaffected.
		void SetTooltipEnabled(bool on);
		bool isTooltipEnabled() const;

		// ===============================================================
		//  Platform capability queries (the key to one API across platforms)
		// ===============================================================
		// Which window capabilities this platform REALLY implemented.
		//
		// This is where "one codebase for desktop + mobile" becomes programmable. The library
		// design: interfaces ALWAYS exist and are ALWAYS callable; unsupported ones degrade
		// semantically (no-op, no crash); the difference shows only in the bitmask queried
		// here.
		//
		// To trim the UI per platform (e.g. no maximize button on mobile), query this - and
		// NEVER #if defined(__ANDROID__): the latter needs an edit for every new platform,
		// the former needs none.
		//
		// c: the capability bits to check (combinable with |; a combination requires ALL).
		// Returns true = the capability is live on this platform.
		// Calling an unsupported capability makes the platform layer print ONE stderr line
		// (once per interface) - degradation is discoverable even without querying.
		bool Supports(Capability c) const;

		// All capabilities of this platform (for one-shot dumps / logs).
		Capability capabilities() const;

		// ===============================================================
		//  On-screen input method (soft keyboard) - mobile
		// ===============================================================
		// Shows / hides the on-screen input method (soft keyboard).
		//
		// Apps usually never need this: InputBox summons it on focus and dismisses on blur by
		// itself (given this platform supports Capability::OnScreenKeyboard). Call it only for
		// manual takeover (custom input, password keyboards).
		//
		// Desktop platforms do not support it (physical keyboards do not occupy the screen);
		// calling is a safe no-op. Ask Supports(Capability::OnScreenKeyboard) to check first.
		//
		// on: true = show; false = hide.
		void ShowSoftKeyboard(bool on);

		// Whether the on-screen input method is currently visible.
		// Always false on desktop. Mobile is BEST-EFFORT: without JNI the system truth is
		// unavailable and this returns "the last requested state" (see
		// Plat::IWindow::IsSoftKeyboardVisible). Do not use it for critical decisions.
		bool IsSoftKeyboardVisible() const;

		// Safe-area insets (notch / rounded corners / gesture bar), logical pixels.
		// Mobile-only; always 0 on desktop. Meaningful only when
		// Supports(Capability::SafeArea) - the Android side reads
		// WindowInsets(systemBars | displayCutout) via JNI.
		// It reflects the system bars' PHYSICAL footprint, unrelated to whether they are
		// CURRENTLY visible: in immersive mode with bars hidden this still reports values -
		// exactly the "content avoidance" semantics.
		// Mobile system bars start VISIBLE (see Plat::IWindow::StartsImmersive), so content
		// should reserve insets from the start. The system dispatches insets ASYNC - values
		// read at construction are often 0, so pair with SetOnSafeAreaChanged.
		void GetSafeAreaInsets(float& left, float& top,
			float& right, float& bottom) const;

		// Sets the "safe area changed" callback (mobile).
		//
		// The safe area changes with ROTATION / SPLIT SCREEN / FOLDING, and Android dispatches
		// WindowInsets ASYNC - values read right after construction are often 0. So "read once
		// at construction" is not safe; the right posture: read once at construction for an
		// initial value, attach this callback, and re-apply margins when it fires.
		//
		// Desktop never fires (safe area always 0) - one codebase correct on both ends.
		// Called on the UI thread inside the message loop - keep it light.
		// fn: called on notification; nullptr clears.
		void SetOnSafeAreaChanged(std::function<void(float, float, float, float)> fn);

		// Client-area height currently occupied by the on-screen input method (logical pixels);
		// 0 = none.
		// The UI uses it for avoidance (lifting the input box above the keyboard). Always 0 on
		// desktop. Android currently has no JNI path to the exact height, so it is 0 - but
		// SIZE avoidance still works: adjustResize really shrinks the client area, arriving
		// via the Resize event.
		float softKeyboardHeight() const;

		// ===============================================================
		//  App lifecycle (mobile) - the only reliable "save now" signal
		// ===============================================================
		// Sets the lifecycle callback.
		//
		// Mandatory on mobile: persist everything that needs persisting in
		// AppLifecycle::Paused. Do not count on destructors - when the system kills the
		// process in the background, destructors never run.
		//
		// Desktop never fires (Supports(Capability::AppLifecycle) is false), so setting it is
		// harmless - one codebase correct on both ends.
		//
		// fn: called on notification; nullptr clears.
		// Called on the UI thread inside the message loop - keep it light, and do not do
		// anything that needs to re-enter the message loop.
		void SetOnAppLifecycle(std::function<void(AppLifecycle)> fn);

		// ===============================================================
		//  Render drive mode (who pushes the picture's rhythm)
		// ===============================================================
		// Window drive mode.
		// All three modes share one render pipeline (RenderFrame); only WHEN to draw differs:
		// - EventDriven (default): draw only when something happens - input (hover / press /
		//   key and other visual-state changes), size changes (WM_SIZE), redraw requests
		//   (RequestRedraw / WM_PAINT). The main loop blocks on messages when idle: ZERO CPU.
		//   For static UIs / forms / tool panels.
		// - OnDemand: event-driven + ON-DEMAND ANIMATION. Identical to EventDriven when idle
		//   (zero CPU); the moment any widget declares "I am continuously animating"
		//   (Widget::wantsAnimationFrame() returns true) it rises to SetFrameRate's rate and
		//   schedules frames continuously, dropping back to zero CPU the instant animation
		//   stops. The recommended mode for self-animating UIs - smooth without burning.
		// - Immediate: unconditionally repaints at SetFrameRate, even when idle. For games /
		//   real-time waveforms - "the picture never stops".
		//
		// How to choose: static UI -> EventDriven; ordinary app with some animation ->
		// OnDemand; continuous full-rate rendering (games) -> Immediate.
		enum class DriveMode {
			EventDriven,   // Event-driven (default): draw only on events, zero CPU when idle
			OnDemand,      // On-demand animation: event-driven + frame-rate scheduling while animating
			Immediate      // Immediate: continuous repaint at frameRate
		};

		// Switches the drive mode.
		// mode: EventDriven (default) / OnDemand / Immediate.
		// The mode is frozen when exec() enters the main loop - call BEFORE exec(); changes
		// during a run only take effect at the next exec().
		// See SetFrameRate.
		void SetDriveMode(DriveMode mode);

		// Current drive mode (default EventDriven).
		DriveMode driveMode() const;

		// Sets the frame rate (Immediate's constant rate / OnDemand's animation rate).
		// fps: frames per second (default 60, clamped to a minimum of 1); no effect under
		// EventDriven (no error; takes effect when switching back).
		// The target rate is genuinely honored: the main loop schedules frames on an ALIGNED
		// timeline (frame N at t0 + N/fps) and requests the high-precision system timer while
		// in the frame-paced loop (Win32 = timeBeginPeriod(1)), returning it on exit. Measured
		// 60fps frame-interval stddev ~1.6ms.
		//
		// Two PHYSICAL ceilings (not the library's fault):
		//  - SwapBuffers is vsynced (swapInterval=1) -> the real rate never exceeds the
		//    monitor's refresh; 120/144fps requests on a 60Hz screen land at ~60.
		//  - When a frame takes longer than its interval, frames naturally drop beats (no
		//    catch-up, no accumulating lag).
		// See SetDriveMode.
		void SetFrameRate(int fps);

		// Currently set target frame rate (default 60).
		int frameRate() const;

		// Moves a widget ONTO the window (the window takes over its storage and lifetime).
		// w: the widget value (lvalues are moved internally - equivalent to writing std::move).
		// Returns a reference to the placed copy (the one in the tree; references are stable).
		//
		// Lifetime is handled by the library: once handed in, the widget lives in the window's
		// storage and the whole tree is released at window destruction. LOCAL widgets keep
		// displaying long-term - no member variables, no delete, no std::move needed.
		//
		// The subtree moves intact: children and filters migrate with the widget.
		// Only use on widgets not yet attached (newly built ones are exactly that).
		// After handing over, the original variable is moved-from; keep operating via the
		// returned reference: auto& placed = win.AddWidget(std::move(btn));
		// Without SetSize it will NOT be 0x0: at attach (and before every frame's render) the
		// window gives top-level widgets without a layouter their NATURAL size - priority:
		// user's SetSize > hintWidth()/hintHeight() > SetDefaultSize(). So
		// win.AddWidget(Kit::Label("hi")) is visible immediately, no need to measure first.
		// See Widget::AutoSize.
		template<class T>
		T& AddWidget(T&& w) {
			static_assert(std::is_base_of_v<Widget, std::decay_t<T>>,
				"AddWidget requires a Kit::Widget-derived control");
			static_assert(!std::is_abstract_v<std::decay_t<T>>,
				"AddWidget cannot instantiate an abstract control (did you forget to implement Paint?)");
			using Plain = std::decay_t<T>;                // Strip references: T is T& for lvalue args
			auto p = std::make_unique<Plain>(std::move(w));   // The library moves internally; callers need not write it
			Plain& ref = *p;
			adoptTop(std::move(p));
			return ref;
		}

		// Detaches a widget from the window (its storage stays with the window until window
		// destruction).
		// Called automatically at widget destruction; manual calls usually unnecessary.
		void RemoveWidget(Widget& widget);

		// ===============================================================
		//  Find / replace by identity (the right path to "update existing UI in event callbacks")
		// ===============================================================
		//  Under value semantics a widget's address changes at AddWidget, so pointers cannot be
		//  held long-term. To modify an already-attached widget, give it an objectName, then:
		//    - FindWidget<T>(name) - find it and mutate properties via reference (no ownership);
		//    - SetWidget<T>(name, w) - a same-name widget is REPLACED in place (old destroyed),
		//      otherwise ADDED. The safest "update UI" pattern, eliminating stacked duplicates
		//      from repeated AddWidget.
		//  Neither API lets developers touch memory / lifetime - life and death stay with the
		//  window.
		// ===============================================================

		// Finds a top-level widget by identity name.
		// name: the target widget's objectName (an empty string matches nothing).
		// Returns the first top-level widget with objectName == name; nullptr when missing.
		//
		// Only the TOP-LEVEL table is searched (the widgets attached directly via AddWidget).
		// To search inside a container, recurse via the reference the container's AddWidget
		// returned, or give the target a top-level name.
		//
		// Found -> mutate it; no ownership, no lifetime worries.
		// See SetWidget, Widget::SetObjectName.
		template<class T>
		T* FindWidget(const std::string& name) {
			Widget* w = findTopByName(name);        // Non-template bridge: Impl is incomplete in this header
			return dynamic_cast<T*>(w);             // Type mismatch -> nullptr
		}

		// Replaces-or-adds a top-level widget by identity name.
		// name: identity name (empty = no identity, degrades to a plain AddWidget add).
		// w: the widget value (lvalues moved internally, same as writing std::move).
		// Returns the in-tree reference (stable).
		//
		// The recommended "update UI from event callbacks" pattern:
		//   - FIRST time: no widget named name in the window -> equals AddWidget (added);
		//   - AFTERWARDS: a same-name widget exists -> the old one destroyed first, the new one
		//     attached - NEVER stacked.
		//
		// The old widget is destroyed (released from storage; destruction auto-unregisters
		// focus / filters / the parent chain) and the new widget inherits its place and
		// identity. Life and death always belong to the window; developers never touch delete.
		//
		// Names are case-sensitive and type-sensitive: same name but different type does not
		// replace (an old Label and a new Button coexist); enforce strict uniqueness yourself.
		// See FindWidget, AddWidget, Widget::SetObjectName.
		template<class T>
		T& SetWidget(const std::string& name, T&& w) {
			static_assert(std::is_base_of_v<Widget, std::decay_t<T>>,
				"SetWidget requires a Kit::Widget-derived control");
			static_assert(!std::is_abstract_v<std::decay_t<T>>,
				"SetWidget cannot instantiate an abstract control (did you forget to implement Paint?)");
			using Plain = std::decay_t<T>;

			// Contract: name is not only the key for finding the old widget; it is also stamped
			// onto the new widget automatically. Otherwise the second SetWidget's findTopByName
			// never finds the previously attached one and degrades to "always add, always stack"
			// (exactly the duplication bug developers hit).
			w.SetObjectName(name);

			// A same-name same-type old widget -> destroy it first, then attach the new one
			// (replace semantics, never stack).
			if (Widget* victim = dynamic_cast<Plain*>(findTopByName(name))) {
				destroyTop(*victim);                    // Old widget and its subtree destroyed (lifetime still the window's)
				return AddWidget(std::move(w));         // The new widget inherits the identity
			}

			// No same-name match (or same name, different type) -> degrade to a plain add
			return AddWidget(std::move(w));
		}

		// ===============================================================
		//  Addressable (window level): recursive search / traversal of the whole widget tree
		// ===============================================================
		//  Companions of the same-named Widget APIs; only the STARTING POINT differs:
		//    - Widget versions search one widget's subtree downward;
		//    - These versions search the whole forest from all TOP-LEVEL widgets.
		//  For "grab any-depth widgets by name" (mutate a grandchild label from a button
		//  callback, save all input boxes, dump the whole tree's geometry...), use these - no
		//  hand-written layer-by-layer paths.
		// ===============================================================

		// Recursively finds the FIRST widget matching an identity name in the whole tree
		// (pre-order).
		// name: the target widget's objectName (an empty string matches nothing).
		// Returns the first matching widget; nullptr when missing.
		//
		// No need to know how many containers wrap that Label.
		//
		// Isomorphic with Widget::FindWidgetDeep, but covering all top-level widgets and their
		// subtrees.
		// See FindAll, FindWidget, Widget::FindWidgetDeep.
		template<class T>
		T* FindWidgetDeep(const std::string& name) {
			if (name.empty()) return nullptr;
			std::vector<Widget*> hits;
			forEachTop([&](Widget& top) { top.findDeepCollect(name, hits); });
			for (Widget* w : hits)
				if (T* t = dynamic_cast<T*>(w)) return t;
			return nullptr;
		}

		// Recursively finds ALL widgets matching an identity name in the whole tree.
		// name: the target widget's objectName (an empty string returns an empty list).
		// Returns every widget matching name AND type (pre-order: top-levels in add order,
		// each subtree pre-order).
		// See FindWidgetDeep.
		template<class T>
		std::vector<T*> FindAll(const std::string& name) {
			std::vector<T*> out;
			if (name.empty()) return out;
			std::vector<Widget*> hits;
			forEachTop([&](Widget& top) { top.findDeepCollect(name, hits); });
			for (Widget* w : hits)
				if (T* t = dynamic_cast<T*>(w)) out.push_back(t);
			return out;
		}

		// Pre-order traverses ALL widgets in the window (top-levels and their whole subtrees),
		// calling f once per widget.
		// f: callable with signature void(Widget&, int depth); top-level widgets have depth 0.
		//
		// Dump the whole UI's identity + geometry (persistence / debugging / picking all use it):
		// iterate with std::printf("%*s%s (%.0f,%.0f %.0fx%.0f)\n", depth * 2, "", ...).
		//
		// Order: top-level widgets in AddWidget order, each top-level internally pre-order
		// (parents before children).
		// Do NOT add / remove widgets inside f (it invalidates the traversal); collect first,
		// mutate after - see FindAll's usage.
		// See Widget::VisitTree, FindAll.
		template<class F>
		void VisitTree(F&& f) {
			forEachTop([&](Widget& top) { top.VisitTree([&](Widget& w, int d) { f(w, d); }); });
		}

		// Replaces this window's entire color scheme (the single source of styling).
		// theme: the new theme (usually Themes::Light() with a few fields changed).
		// Immediately injects the new theme into the whole widget tree (recursive
		// Widget::ApplyTheme) - widgets are constructed with the built-in light theme, so
		// without injection a theme change would show nothing. Injection is "start over"
		// semantics: it OVERWRITES styles the user manually Set (intentional).
		//
		// It takes over the COLOR SCHEME, not the dark/light polarity (changed 2026-11-02):
		// when the system switches to dark, the window switches to "THE DARK VERSION OF THIS
		// SCHEME" (Themes::Recolor: neutral backgrounds swap to the dark set, accents keep
		// their hue bumped one step), not back to the built-in dark blue - your Ocean survives
		// a system dark switch. Polarity itself belongs to the system (while following is on)
		// or SetDarkMode (manual).
		// isFollowingSystemDarkMode() is NOT affected by this function (older versions reset it
		// - which was exactly the root cause of "title bar went dark, canvas unmoved"). To
		// fully detach from the system: SetFollowSystemDarkMode(false).
		// The theme's own polarity (bg luminance) syncs to isDarkMode() and the title bar.
		// See SetDarkMode, SetFollowSystemDarkMode, Themes::Recolor.
		void SetTheme(const Theme& theme);

		// Switches the theme by NAME (five built-ins + custom themes registered via Register()).
		// name: theme name, see Themes::Names() ("light" / "dark" / "ocean" / "nord" / "warm" ...).
		// Returns success (false = the name is not registered; the theme stays as is).
		// Semantics identical to SetTheme(const Theme&) (takes over the scheme / reinjects the
		// whole tree), just fetching the Theme from the registry - fits settings pages listing
		// themes for the user to pick: build menu items from Themes::Names().
		// See SetTheme, Themes::Get, Themes::Register.
		bool SetThemeByName(const std::string& name);

		// The currently active theme (widgets / drawing read it only).
		const Theme& theme() const;

		// ===============================================================
		//  Dark mode (follows the system by default)
		// ===============================================================
		//  Three paths together guarantee "system goes dark, the app follows":
		//   1. read the system preference once at startup;
		//   2. on a runtime "system appearance changed" notification -> re-evaluate immediately;
		//   3. the change hits three places simultaneously: the CANVAS (per-frame clear uses
		//      theme bg), the window TITLE BAR, and WIDGET STYLES (whole-tree theme reinjection).
		//
		//  Steps 1-2 are the platform's job, and the mechanisms differ wildly - do not carry
		//  one platform's experience to another:
		//   - Win32: 1 reads registry Personalize\AppsUseLightTheme;
		//            2 relies on WM_SETTINGCHANGE("ImmersiveColorSet").
		//   - X11:   1 reads GTK_THEME / XSETTINGS Gtk/ColorScheme / settings.ini (three tiers,
		//            see ZePlatformWindow.h);
		//            2 relies on XSETTINGS property changes surfaced as ThemeChanged events.
		//            GNOME 42+'s dark toggle goes ONLY through the XSETTINGS path -
		//            gtk-3.0/settings.ini does not exist on modern GNOME at all.
		//            The window-frame decorations are painted by the WM (mutter under GNOME)
		//            from system settings, independent of our detection; therefore "frame went
		//            dark but canvas did not" ONLY means we failed to detect dark mode (check
		//            ZEKIT_TRACE_THEME).
		//
		//  SetTheme takes over the COLOR SCHEME; dark/light polarity still follows the system
		//  (while following is on): on a system dark switch the window applies "the dark version
		//  of this scheme", not the built-in dark blue. To detach polarity too:
		//  SetFollowSystemDarkMode(false).
		// ===============================================================

		// Sets whether to follow the system dark mode.
		// follow: true = follow (DEFAULT); false = SetDarkMode decides manually from now on.
		// When turned on, it refreshes once immediately from current system settings
		// (reinjects even when the polarity did not change, covering "resume from suspend");
		// turning it off does not change the current mode.
		//
		// No longer mutually exclusive with SetTheme (changed 2026-11-02): the scheme stays
		// taken over while polarity follows the system (switching to that scheme's dark /
		// light version). To return to built-in colors: SetTheme(Themes::Light()).
		void SetFollowSystemDarkMode(bool follow);

		// Whether the system dark mode is currently followed (default true; SetTheme does not
		// change it).
		bool isFollowingSystemDarkMode() const;

		// Manually specifies dark / light mode.
		// dark: true = dark.
		// DISABLES "follow system" (you explicitly chose; further system changes must not
		// override you).
		// Applies to canvas / title bar / widget styles. If SetTheme took over the scheme,
		// this switches "THAT scheme's dark/light version" (Themes::Recolor) - polarity yours,
		// scheme yours, no more fighting (older versions left a "dark title bar + light canvas"
		// half-state).
		void SetDarkMode(bool dark);

		// Whether dark mode is currently active (the canvas's and widgets' actual mode).
		// Semantics: THIS WINDOW's current mode. After a manual SetDarkMode it decouples from
		// the system and no longer reflects the system's current preference - query the system
		// via SystemPrefersDarkMode().
		// SetTheme aligns this to the theme's own polarity (bg luminance): a dark theme makes
		// the window dark, title bar included.
		bool isDarkMode() const;

		// One-shot read of system settings: does the system currently prefer dark.
		// Read-only; does not change this window's state. For custom UI (e.g. drawing your own
		// toggle switch).
		static bool SystemPrefersDarkMode();

		// ===============================================================
		//  Native window handle / window active state (IME positioning, native graphics APIs)
		// ===============================================================
		// Returns this window's NATIVE handle (platform-specific).
		// Returns nullptr when the window is not yet created or already CLOSED.
		// Platform semantics:
		//  - Windows: HWND (for widgets' native calls like ImmSetCompositionWindow);
		//  - Linux/X11: Window (an XID integer passed out as an opaque pointer). The connection
		//    is PROCESS-LEVEL shared; render backends use it together with
		//    Plat::X11DisplayHandle() / Plat::X11WindowId() to build GLX contexts / Vulkan
		//    surfaces.
		// The library treats it as an OPAQUE pointer, only checking "is it nullptr"; real
		// platform API calls belong to the Rhi layer's backend implementations.
		void* hwnd() const;

		// Whether this window is currently the foreground-active window (holds system keyboard
		// focus).
		// Input boxes use it to decide caret drawing / IME activation (stop after Alt+Tab
		// away).
		bool isWindowActive() const;

		// ===============================================================
		//  IME caret anchor
		// ===============================================================
		// Reports the TEXT CARET position for the platform IME to place its composition /
		// candidate windows.
		// x: the caret's left end (client-area logical pixels).
		// lineTop / lineBottom: the caret line's top / bottom edges - the IME places the
		// candidate window BELOW this line.
		// The IME does not guess; it waits for the client to tell it. The channels differ:
		//  - Windows: IMM's ImmSetCompositionWindow / ImmSetCandidateWindow (InputBox already
		//    calls these directly internally, bypassing this method);
		//  - Linux/X11: XIC's XNSpotLocation - fcitx / ibus read it in every preedit style.
		//    Never setting it makes them fall back to "the client area's bottom-left corner",
		//    showing as "the candidate window lands at the window's very bottom, not under the
		//    caret".
		// The line's BOTTOM edge is given, not the top: the server places the window below the
		// anchor without knowing the line height; giving the top would let the candidate
		// window cover the text being edited.
		// Implementations cache and debounce (X11's XSetICValues is a synchronous round trip),
		// so callers may call it blindly every frame.
		// Custom widgets (self-drawn input boxes) should call this after caret moves, or the
		// candidate window will not follow the caret.
		void reportImeSpot(float x, float lineTop, float lineBottom);

		// ===============================================================
		//  System clipboard (plain text)
		// ===============================================================
		// Reads the system clipboard's plain text (UTF-8).
		// Returns the clipboard text; empty string when there is no text content / no
		// implementation (NOT an error).
		// The clipboard is an operating-system concept, so the implementation lives in the
		// platform layer (CF_UNICODETEXT on Windows; the Selection protocol on X11). Widget
		// code (like InputBox's Ctrl+V) only knows this interface, so the editing logic is ONE
		// platform-independent piece of code.
		//
		// SYNCHRONOUS semantics: by return, the content is either obtained or known absent.
		// On X11, if another process holds the content, this performs one XConvertSelection
		// round trip - the implementation has a wait cap and returns an empty string on
		// timeout (never hangs the UI).
		// Closed window / no platform object: returns an empty string (safe).
		std::string GetClipboardText() const;

		// Writes plain text (UTF-8) into the system clipboard (replacing the previous content).
		// utf8: the text to write; an empty string = clear the clipboard.
		// On X11 "writing" means TAKING SELECTION OWNERSHIP and storing the text; the actual
		// data is delivered only when someone asks - so the platform layer must answer
		// SelectionRequest in its event loop, or other programs' paste waits until timeout
		// (showing as "copied but cannot paste").
		// On X11 the content vanishes at process exit (no system-level clipboard store; a
		// clipboard manager would take a copy before exit). A platform fact, not an
		// implementation flaw.
		// Closed window / no platform object: a no-op (safe).
		void SetClipboardText(const std::string& utf8);

		// ===============================================================
		//  Event filters
		// ===============================================================
		// Filter handle - returned by InstallEventFilter, used with RemoveEventFilter.
		// 0 means invalid (0 is never a legal id, handy as an "not installed" sentinel).
		using FilterId = std::uint64_t;

		// Installs an event filter that intercepts events BEFORE they reach widgets.
		// filter: the filter itself (NON-OWNING; the window stores only its address).
		// Returns the filter's handle for later RemoveEventFilter; 0 on failure.
		//
		// Events entering from Win32 pass through all filters in INSTALL ORDER before hit
		// testing. Any filter returning true from onMouse / onKey means "intercepted": the
		// event is dropped and widgets never see it.
		//
		// Typical uses: global shortcuts, drag coordination, event logging, modal overlay
		// blocking.
		//
		// Idempotent: installing the same filter twice does not take effect twice (returns the
		// existing handle).
		// The window does NOT own the filter; the filter may be destroyed before the window
		// (it auto-unregisters on destruction), but its ADDRESS must never change once
		// installed - copying / moving the filter object is forbidden.
		// See EventFilter, RemoveEventFilter.
		FilterId InstallEventFilter(EventFilter& filter);

		// Uninstalls an event filter.
		// id: the handle returned by InstallEventFilter.
		// Invalid handles (0 or already removed) have no side effects.
		// Filters auto-uninstall on destruction; manual calls usually unnecessary.
		// See InstallEventFilter.
		void RemoveEventFilter(FilterId id);

		// Selects this window's render backend (each window holds its own; independent of others).
		// api: the desired renderer. Choices come from Rhi::Api restricted per platform at
		// compile time - OpenGL / Vulkan / D3D12 on Windows, Metal / OpenGL on macOS,
		// OpenGL / Vulkan on Linux / Android. Every platform additionally has Software
		// (software rasterization, no graphics driver needed). Values this platform lacks are
		// NOT in the enum at all - writing one is a compile error - which surfaces problems
		// earlier than "compiles, fails at runtime".
		// Returns true = switched to that backend; false = initialization failed, AUTO FALLBACK
		// (OpenGL first, then Software - the window stays usable); the reason goes to stderr
		// and OutputDebugString.
		//
		// Must be called BEFORE exec(); calling later does not affect a running frame loop.
		// Idempotent: re-passing the current backend returns true immediately.
		// Calling no Use* means the platform's recommended backend (Rhi::DefaultApi()):
		// D3D12 on Windows, Metal on macOS, Vulkan on Linux / Android.
		//
		// Rhi::Api::Software: the last road when there is no GPU driver. Pixels are computed on
		// the CPU (rounded-corner SDFs / 1px edge transitions / premultiplied alpha / bilinear
		// glyphs, aligned feature-by-feature with the GPU backends), then presented by the
		// window system. It is NEVER auto-selected as the default; it steps in only when
		// explicitly requested or when all GPU backends failed. Two uses:
		//  - a fallback - servers / cloud desktops / remote sessions / containers where GPU
		//    backends all die: the UI stays viewable and clickable;
		//  - bisection - specify it explicitly; if the picture is fine, the problem is the GPU
		//    driver / backend, not layout or widgets.
		// Two honest limits: per-pixel transparency only works on X11 32-bit ARGB windows (the
		// Windows GDI path reports unsupported, so gadget windows become opaque); and SPEED is
		// millisecond-level (full-window repaint) - fine as a fallback, not as a regular
		// backend.
		//
		// Software is the ONLY backend that does not claim the window (X11 XPutImage, Windows
		// GDI BitBlt - neither touches pixel formats / swap chains), which makes it the safest
		// step when GPU backends already failed and you want to try another backend. The
		// calling-order constraint below targets GL / D3D12 / Vulkan.
		//
		// HARD CALLING-ORDER CONSTRAINT: UseRenderer must be the FIRST call after window
		// construction, before any SetSize / SetPosition / Show. Reason: those interfaces let
		// the DEFAULT backend claim the window first (on Windows: D3D12 builds a swap chain and
		// exclusively owns the HWND; on the GL path it is WGL's SetPixelFormat + MakeCurrent).
		// Claiming is irreversible - building another backend's swap chain on the same window
		// afterwards necessarily fails (E_ACCESSDENIED /
		// VK_ERROR_NATIVE_WINDOW_IN_USE_KHR), so UseRenderer returns false and silently
		// falls back.
		//
		// Example: Kit::Window win; win.UseRenderer(Rhi::Api::OpenGL); // first call, before
		// SetSize / Show - then SetTitle / SetSize / Show / exec.
		// See Rhi::Api, Rhi::DefaultApi.
		bool UseRenderer(Rhi::Api api);

		// Swaps this window's renderer for YOUR OWN implementation (none by default).
		//
		// This is the "library handles layout and collection; you draw the triangles" path.
		// Once installed:
		//  - Kit keeps working as usual - layout, hit testing, focus, hover, scrolling, IME,
		//    animation, theming, fragment caching, all of it;
		//  - only the last step "submit this frame to the GPU" becomes your
		//    Kit::ICanvasRenderer::DrawFrame;
		//  - glyph (ZeFontRenderer) and image (ZeImageStore) texture creation also route to you
		//    (the same ITextureSink mechanism the Vulkan / D3D12 backends use).
		//
		// One-line contrast: SetRenderEnabled(false) hands the WHOLE thing away (Kit draws not
		// one pixel); this function COOPERATES - Kit still draws, the brush is just in your
		// hand.
		//
		// renderer: your renderer (shared_ptr: window and canvas share the same instance).
		//   nullptr = unbind (from the next frame, back to the platform's default backend).
		// Returns true = recorded; false = too late (the backend is already built) or the
		// window is closed.
		//
		// Must be called BEFORE the first draw - the same hard constraint as UseRenderer:
		// context / swap chain bindings on that HWND are irreversible; swapping after the
		// backend exists requires rebuilding the window.
		// Mutually exclusive with UseRenderer: with an external renderer installed, UseRenderer
		// values are ignored (rendererApi() reports Rhi::Api::External).
		// NO fallback on failure: when your Attach returns false, the frame is skipped and
		// retried next frame - the library will not quietly build a GL context beside you
		// (that HWND's context is yours).
		// Callback threading: all interfaces are called on the UI thread (including glyph
		// uploads); no locking needed.
		//
		// Example: Kit::Window win; win.UseExternalRenderer(myRenderer); // first call - then
		// SetSize / AddWidget / Show; the UI is drawn by that renderer.
		// See Kit::ICanvasRenderer (ZeCanvasRenderer.h), Kit::CanvasView::SetRenderer.
		bool UseExternalRenderer(std::shared_ptr<Kit::ICanvasRenderer> renderer);

		// Queries the render backend this window ACTUALLY uses.
		// Returns the current backend (an Rhi::Api value); after a failed-init fallback, the
		// FALLBACK value.
		// Returns OpenGL (the default) when the window is closed.
		Rhi::Api rendererApi() const;

		// ===============================================================
		//  Render device (GPU) selection
		// ===============================================================
		// Chooses which GPU renders this window.
		// gpu: device policy, see Rhi::Gpu:
		//  - Auto (DEFAULT): discrete GPU preferred; integrated only when no discrete exists;
		//  - Discrete: discrete only; failing to get one is an INIT FAILURE (no silent
		//    downgrade to integrated);
		//  - Integrated: integrated preferred (power saving).
		// Returns true = the policy is recorded (NOT necessarily in effect); false when the
		// window is closed.
		//
		// The render API (UseRenderer) and "which GPU" are TWO INDEPENDENT DIMENSIONS. On
		// dual-GPU machines the driver hands the window to the integrated GPU by default
		// (power first), while this library defaults to performance.
		//
		// The two backends land it differently, transparently to users:
		//  - Vulkan: picks the physical device by VkPhysicalDeviceType priority. Pure library
		//    behavior, no env vars or driver config - hence RELIABLY achievable;
		//  - OpenGL (Linux/EGL): EGL device enumeration first; if no window surface can be
		//    created, falls back to the X11 platform, setting
		//    __NV_PRIME_RENDER_OFFLOAD=1 (NVIDIA's official switch) before doing so. Measured
		//    (Intel RPL-S + RTX 4060) Auto does land on the discrete GPU, but that is
		//    ultimately a PRIME-configuration outcome, not an API capability - hence Discrete
		//    verifies afterwards and fails on mismatch.
		//
		// Must be called BEFORE render backend initialization: after window construction,
		// before the first Show() / exec() / draw - device selection happens inside the
		// backend's Init(). Same "configure before the backend exists" tier as UseRenderer.
		// Idempotent. Switching the policy tears down the built backend and rebuilds on the
		// next draw.
		// Runtime GPU-policy switching within one process is only reliably bounded on the GL
		// backend: it relies on setenv, and EGL's vendor library (glvnd) parses environment
		// variables only once per display - "built a GL context first, then changed the
		// policy" may not take effect. For runtime GPU switching use the Vulkan backend (it
		// re-enumerates devices on every rebuild) or restart the process. Calling after
		// construction but before the first draw ALWAYS works.
		//
		// Example: win.UseGpu(Rhi::Gpu::Auto) (the default; writing it is just a statement),
		// win.UseRenderer(Rhi::Api::Vulkan), then Show / exec; confirm the device via
		// deviceInfo() (valid && isDiscrete).
		// See Rhi::Gpu, deviceInfo, UseRenderer.
		bool UseGpu(Rhi::Gpu gpu);

		// Sets this window's canvas to a TRANSPARENT background (clear alpha = 0).
		// transparent: true = clear transparent (content primitives still draw with their own
		//   alpha); false = opaque (default).
		//
		// The switch behind "transparent child windows": for per-pixel transparent overlay
		// carriers like ChildKind::Overlay - clear transparent + content alpha 1, DWM composes
		// per-pixel alpha, so pixels with content are visible and hittable while transparent
		// areas pass through naturally (rounded corners / bubble arrows / free-form shapes all
		// work without a window rectangle). Kit::OverlayWindow uses this internally; ordinary
		// app code rarely calls it directly.
		//
		// Only TOGETHER with ChildKind::Overlay does the window truly become invisible: that
		// form carries WS_EX_NOREDIRECTIONBITMAP on Win32 (the window has NO redirection
		// surface), so only swap-chain-composed pixels are visible on screen. With
		// ChildKind::Popup the window still has a rectangle (DWM's window surface) and content
		// floats on it - "content visible, window visible".
		// TIMING IS HARD: transparent alpha determines the swap chain's composition mode, fixed
		// at the moment the swap chain is created (ResizeBuffers cannot change it). The child
		// constructor ends with primeChildDisplay() presenting - that step may already create
		// the swap chain. So calling this AFTERWARDS on ChildKind::Overlay / ChildKind::Popup
		// is likely too late: the call succeeds without error, but the window stays opaque and
		// unpainted areas show BLACK (the 2026-11-02 arrow black-band bug). For transparency
		// use ChildKind::Overlay (it fixes alpha to 0 in its constructor); do not count on
		// setting it later. Diagnostic switch: ZEKIT_TRACE_SWAPCHAIN=1.
		// All three backends implement it (IBackend::SetClearAlpha): GL (alpha bit in the pixel
		// format), D3D12 (DXGI_ALPHA_MODE_PREMULTIPLIED swap chain + DirectComposition),
		// Vulkan (compositeAlpha POST_MULTIPLIED). Transparent alpha decides the swap chain's
		// composition mode, so it MUST be set before the backend builds the swap chain (before
		// Show); calling after the backend exists only affects later frames' clear-alpha
		// component and does not rebuild the swap chain (for a full switch, set it right
		// after window creation).
		// If ChildKind::Overlay fails to reach a composition swap chain (D3D12 init failure
		// downgraded to GL, or the system lacks dcomp.dll), the library restores the
		// redirection surface - degrading to "opaque but content visible", not vanishing
		// entirely.
		// Mobile (Android / HarmonyOS) overlays do not take this path: silently a no-op.
		// See SetRenderEnabled, Kit::OverlayWindow, ChildKind.
		void SetClearAlpha(float alpha);

		// Queries this window's REQUESTED GPU policy.
		// Returns the last value passed to UseGpu (default Rhi::DefaultGpu() = Auto).
		// For the GPU actually in use, use deviceInfo() - the policy is intent, not fact.
		Rhi::Gpu gpuPreference() const;

		// Queries the render device this window ACTUALLY uses.
		// Returns a device description (name / driver / API / discrete flag); valid == false
		// when the backend is not ready, the query is unsupported, or the window is closed.
		//
		// The sole reason this interface exists is OBSERVABILITY: device policy can fail
		// ("I asked for discrete, I am on integrated") and the program must be able to detect
		// that itself instead of eyeballing.
		//
		// Strings are fixed-size char[] with no heap allocation - safe to return across the
		// library boundary.
		//
		// Does NOT trigger backend initialization (that belongs to the draw path). But if you
		// explicitly called UseRenderer() - which builds the backend IN PLACE - the query
		// returns values right away. Without UseRenderer, the first completed frame is needed.
		// Backends without this query return valid == false (only the GL/Vk backends support
		// it, and isDiscrete only has discriminating power on multi-GPU machines).
		Rhi::DeviceInfo deviceInfo() const;

	private:
		friend struct WindowAccess;   // Lets Zekit.cpp's Impl call Window's private dispatch functions
		friend class EventFilter;     // Lets ~EventFilter unregister itself in reverse
		friend class Widget;          // Lets Widget::requestFocus/clearFocus go through focus
		                              // arbitration (the legal channel for modal widgets - ordinary
		                              // widgets never see those two doors)

		// Schedules exactly one frame WITHOUT whole-tree invalidation (widget-level redraw
		// requests take this path).
		// Division with Window::RequestRedraw() (2026-10-27, retained-mode fragment cache):
		//  - a widget knows IT changed -> Widget::RequestWindowRedraw() -> this function (only
		//    its fragment invalidates; other widgets go through memcpy);
		//  - the app changed something but cannot say which -> Window::RequestRedraw()
		//    (whole-tree invalidation, conservative but correct - the only safe fallback under
		//    retained mode).
		// Made private so "which entry invalidates everything" has exactly one place to check.
		// from: the initiating widget (null = app / platform side). DIAGNOSTICS ONLY (printed
		// by ZEKIT_TRACE_REDRAW=1 as "who requested a redraw within this frame"); no logic
		// depends on it.
		void requestFrameOnly(const Widget* from = nullptr);

		// Declares "widget positions changed; recompute position state (hover / cursor shape)
		// once next frame".
		// Hover is a function of POINTER POSITION, not of mouse events - but historically it
		// was computed only in dispatchMouseMove, so the whole class of "pointer still, content
		// moved" scenarios broke:
		//   scrolling a list -> rows move past under the pointer while the highlight stays on
		//   the old row (floating with the content); dragging a scrollbar / splitter / resizing
		//   the window - same.
		// The fix is not "re-hit-test in every scrolling site" (each content-moving site would
		// need its own patch) but moving the trigger into the frame: layout reflowed => position
		// state may be stale (see LayoutBase::EnsureLayout's declaration points). This function
		// only sets a flag; the real recompute happens in RenderFrame - positioned after ALL
		// layout settles and before frame collection, so the same scroll paints correctly in
		// the SAME frame (no one-frame stale highlight).
		// Cost: frames without reflow do not traverse once more (the flag unset means no work).
		// Only library geometry paths should call it; apps never need it (content changes
		// naturally reflow).
		void requestHoverResync();

		// Recomputes the hovered widget and cursor shape from "the most recent known pointer
		// position" (idempotent).
		// Shares the same semantics as the edge detection in dispatchMouseMove (old hover
		// Leaves first, new hover Enters after, both marked dirty); only the coordinate source
		// differs: the cached pointer position here, not event parameters.
		void resyncHover();

		// Invalidates the whole tree's fragment cache (next frame re-runs Paint everywhere).
		// Theme change / backend change / DPI change / app-side RequestRedraw all go through
		// it. The implementation is one O(node count) traversal, called only in low-frequency
		// events.
		void invalidateAllPaint();

		// Takes over the widget handed to AddWidget(T&&): attach to table + setOwner + store
		// (implementation in Zekit.cpp; the storage lives in Impl - an incomplete type in this
		// header cannot reach it).
		void adoptTop(std::unique_ptr<Widget> p);

		// The child window's "show on first attach" patch: Show once on the user's behalf after
		// the first widget is attached (implementation in Zekit.cpp; why it must happen and
		// what breaks otherwise - see the long note there).
		// Only effective for CHILD windows, and skipped when the user explicitly Hide()d.
		void primeChildDisplay();

		// Actually destroys a top-level widget (with its subtree) and frees its storage
		// (implementation in Zekit.cpp; called by SetWidget's "replace" semantics).
		// vs RemoveWidget: RemoveWidget only detaches from the tree (not destroyed, storage
		// still holds it); this one frees the storage too - replacement semantics need real
		// destruction.
		void destroyTop(Widget& w);

		// Finds the first top-level widget by identity name (type-agnostic)
		// (implementation in Zekit.cpp; called by the FindWidget / SetWidget templates).
		// Returns the first top-level widget with objectName == name; nullptr when missing
		// (or name empty).
		Widget* findTopByName(const std::string& name);

		// Calls fn for each top-level widget in order (non-template bridge; implementation in
		// Zekit.cpp).
		// Window's top-level table lives in Impl (invisible to this header), so an in-module
		// "walk the table" door is needed - FindWidgetDeep / FindAll / VisitTree all rely on
		// it. The template layer only converts the received Widget& into the user's type; the
		// loop body compiles once.
		void forEachTopImpl(void (*fn)(Widget&, void*), void* ctx);

		// Template wrapper: makes forEachTop(callable) feel like VisitTree.
		template<class F>
		void forEachTop(F&& f) {
			struct Bridge {
				static void Thunk(Widget& w, void* ctx) { (*static_cast<F*>(ctx))(w); }
			};
			forEachTopImpl(&Bridge::Thunk, &f);
		}

		// Translates title bar / min-max button toggles into WS_* style bits bit-wise
		// (implementation in Zekit.cpp; shared by SetTitleBarVisible & co and the borderless /
		// fullscreen exit paths; bit-wise add/remove, never touches SetResizable /
		// SetBorderless's style bits).
		void applyTitleBarStyle();

		// ===============================================================
		//  Theme injection ("conservative" at attach, "full" on theme change - different semantics)
		// ===============================================================
		// Full injection: recursively applies the current theme to the whole subtree (theme
		// change / dark-mode switch).
		void applyThemeToAll(Widget& w);

		// Conservative injection: themes only widgets the user never SetStyle'd (attach time).
		// Otherwise the "style first, AddWidget later" pattern would be washed out at attach.
		void applyThemeToPristine(Widget& w);

		// Refreshes dark mode from the current system settings (follow switch on, or on
		// WM_SETTINGCHANGE).
		// If the user took over the scheme via SetTheme, only the title bar updates - not the
		// theme.
		void refreshDarkModeFromSystem();

		// Applies dark / light to the window title bar (DWM immersive dark; silent failure on
		// old systems).
		void applyTitleBarDarkMode();

		struct Impl;
		std::unique_ptr<Impl> m_impl;

		// ===============================================================
		//  Input dispatch (all triggered via WndProc; users never call these directly)
		// ===============================================================
		//  Unified flow: build a value-semantic event -> hit test finds the target -> bubble
		//  up the parent chain until some widget returns true (consumed) or the top is reached
		//  (dropped).
		// ===============================================================

		// Moves the mouse position, dispatching Enter/Leave when needed, then OnMouseMove.
		void dispatchMouseMove(float x, float y, KeyboardModifiers mods);

		// Dispatches a mouse press (hit test + bubbling).
		void dispatchMousePress(float x, float y, MouseButton button, KeyboardModifiers mods);

		// Dispatches a mouse release (hit test + bubbling; also clears the pressed record).
		void dispatchMouseRelease(float x, float y, MouseButton button, KeyboardModifiers mods);

		// Dispatches the mouse wheel.
		void dispatchMouseWheel(float x, float y, int wheelDelta, KeyboardModifiers mods);

		// -------------------------------------------------------------------
		//  Mobile pointers (touch / stylus) - the confluence with the mouse
		// -------------------------------------------------------------------
		//  The platform layer only sends Plat::EventKind::Pointer* on mobile (multi-point
		//  events carrying pointerId). The confluence has two halves:
		//   - the STATE MACHINE (who is the primary pointer, when to synthesize MouseDown/Up)
		//     lives in Window::Impl (inside Zekit.cpp, which can see Plat::Event) - the public
		//     header must not expose Plat types;
		//   - DISPATCH lands here (knows only the public TouchEvent).
		// -------------------------------------------------------------------

		// Dispatches one touch event (hit/capture + bubbling); coordinates already in client space.
		void dispatchTouch(TouchEvent& e);

		// Cancels the current pointer capture.
		// Must NOT go through dispatchMouseRelease - that would make buttons read "pressed then
		// released = a click" (see ZeButton.cpp's click rule). Cancel means "this gesture does
		// not count": remove the pressed state AND give widgets an OnMouseLeave to clear their
		// pressed visuals.
		void cancelPointerCapture();

		// Dispatches key press / release / character input.
		// character uses char32_t instead of wchar_t - the latter is only 16 bits on Windows
		// and cannot hold code points beyond the BMP (emoji etc.); platform layers passing
		// full code points would be truncated.
		void dispatchKey(EventType type, Key key, uint32_t virtualKey,
			uint32_t scanCode, bool isRepeat, KeyboardModifiers mods, char32_t ch);

		// -------------------------------------------------------------------
		//  Focus arbitration (Window's job; widgets never grab focus by themselves)
		// -------------------------------------------------------------------
		//  Widget already has focusable_ / focused_ / OnFocusIn-Out / focused state colors;
		//  this wires up the Window side:
		//   - a click's hit chain containing an isFocusable widget gives it focus (clicking
		//     blank = blur);
		//   - the keyboard goes only to the focused widget's chain (no more broadcasting);
		//     falls back to broadcast when nothing has focus (legacy behavior);
		//   - Tab cycles to the next focusable widget within the focus chain.
		// -------------------------------------------------------------------

		// Gives w focus (w == nullptr = clear focus); edge-notifies OnFocusIn/Out.
		void setFocusedWidget(Widget* w);

		// The current focused widget (with a liveness check: it may have been detached from
		// the tree).
		Widget* focusedWidget() const;

		// Hit-test convenience entry: finds the topmost widget hit at this point among the
		// window's top-level widgets.
		Widget* hitTestTopmost(float x, float y) const;

		// Clears hover / pressed state when the mouse leaves the window's client area.
		void clearMouseState();

		// ===============================================================
		//  Filter chain
		// ===============================================================
		// Runs mouse events through the filters in order; returns true = intercepted by some
		// filter.
		// ALL filters run (no short-circuit) so observers can coexist.
		bool runMouseFilters(MouseEvent& e);

		// Runs keyboard events through the filters in order; returns true = intercepted by
		// some filter.
		bool runKeyFilters(KeyEvent& e);

		// Removes a filter by id (no visibility / logging; called back by ~EventFilter).
		void detachEventFilter(FilterId id);
	};

	// Bridge to Window's private dispatch functions (for Zekit.cpp's internal WndProc only).
	//
	// WndProc is a C-style callback and cannot reach Window's private members. An empty
	// friend struct funnels "window -> dispatch" into one place instead of making all 8
	// dispatch functions public - cleaner. User code should not use this struct.
	struct WindowAccess {
		static void MouseMove(Window& w, float x, float y, KeyboardModifiers m) {
			w.dispatchMouseMove(x, y, m);
		}
		static void MousePress(Window& w, float x, float y, MouseButton b, KeyboardModifiers m) {
			w.dispatchMousePress(x, y, b, m);
		}
		static void MouseRelease(Window& w, float x, float y, MouseButton b, KeyboardModifiers m) {
			w.dispatchMouseRelease(x, y, b, m);
		}
		static void MouseWheel(Window& w, float x, float y, int d, KeyboardModifiers m) {
			w.dispatchMouseWheel(x, y, d, m);
		}
		static void Key(Window& w, EventType t, Key k, uint32_t vk,
			uint32_t sc, bool rep, KeyboardModifiers m, char32_t ch) {
			w.dispatchKey(t, k, vk, sc, rep, m, ch);
		}
		static void ClearMouse(Window& w) { w.clearMouseState(); }

		// Recomputes hover / cursor shape from the cached pointer position (called in frames;
		// see requestHoverResync).
		static void ResyncHover(Window& w) { w.resyncHover(); }
		static void Touch(Window& w, TouchEvent& e) { w.dispatchTouch(e); }
		static void CancelPointerCapture(Window& w) { w.cancelPointerCapture(); }
	};

	// =====================================================================
	//  Kit::Screen - monitor geometry (NO window instance needed)
	// =====================================================================
	//
	//  Why this exists (instead of Window::GetScreenWorkArea): "how big is the screen" is a
	//  PROCESS-LEVEL fact, independent of whether some window exists. The moment apps need
	//  it most is exactly when NO window exists yet - the typical use is "open a window at
	//  90% of the screen":
	//
	//  Kit::Screen::Display d;
	//  if (Kit::Screen::Primary(d)) {
	//      win.SetSize(static_cast<int>(d.workWidth  * 0.9f),
	//                  static_cast<int>(d.workHeight * 0.9f));
	//  }
	//
	//  After a window exists, asking "which screen is it on" still uses
	//  Window::GetScreenWorkArea() - on multi-monitor that one FOLLOWS THE WINDOW, while this
	//  is "the machine's whole screen inventory"; neither replaces the other.
	//
	//  Coordinates share the same system as SetSize / client areas (physical pixels), so
	//  SetSize(d.workWidth, d.workHeight) fills the work area pixel-exact.
	//
	//  Platform support status (unimplemented platforms return false / 0; NEVER a compile
	//  error):
	//    Windows: Primary yes, enumeration yes, work area (minus taskbar) accurate.
	//    Linux/X11: Primary works (a window must have been created first); enumeration
	//      reports 1 item (the whole virtual desktop); work area equals the full screen.
	//    Android / HarmonyOS / Apple: Primary returns false, enumeration 0.
	//  The X11 limits are inherent to the protocol (Xlib without Xinerama sees only one
	//  screen; the taskbar belongs to the window manager with no standard API reporting its
	//  size) - not unfinished work. Callers should reserve margins from bounds themselves,
	//  e.g. take 90% instead of 100%.
	namespace Screen {

		// One monitor's geometry (physical pixels).
		struct Display {
			int x = 0;              // Monitor top-left (virtual-desktop coordinates; secondary
			                        // screens left of the primary are negative)
			int y = 0;
			int width = 0;          // Full monitor size (including the strip the taskbar occupies)
			int height = 0;

			int workX = 0;          // Work-area top-left
			int workY = 0;
			int workWidth = 0;      // Work-area size (taskbar / Dock EXCLUDED - where windows can live)
			int workHeight = 0;

			bool primary = false;   // Whether this is the primary monitor (the one with the taskbar)
		};

		// The primary monitor's geometry.
		// out: the result written back; left UNCHANGED on failure.
		// Returns true = got it; false = not implemented on this platform (or on X11 no window
		// has been created yet).
		bool Primary(Display& out);

		// The number of all monitors.
		// Returns the count; 0 = enumeration unsupported on this platform.
		// Single-screen platforms (like the X11 implementation) return 1.
		int Count();

		// Enumerates all monitors.
		// out: the result array (may be nullptr, which only counts); maxCount: array capacity.
		// Returns the TRUE total (may be greater than maxCount; only the first maxCount
		// entries were written in that case).
		int List(Display* out, int maxCount);

		// Shortcut for the primary monitor's WORK AREA (the most common query).
		// x,y,w,h: the work-area rect.
		// Returns true = got it.
		// The only difference from Primary() is "one struct less to write"; the implementation
		// is fully shared.
		bool PrimaryWorkArea(int& x, int& y, int& w, int& h);

	} // namespace Screen
}