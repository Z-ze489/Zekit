// ZeCanvasView.h: a native canvas widget that participates in layout and can
// host an external renderer, or become its own standalone window.
//
// Default mode is exclusive rendering: Kit draws nothing into the child window
// (not even the backend is created) unless SetRenderer() is installed. With a
// renderer, Kit keeps layout / hit testing / theming / caching and only
// delegates the final GPU submission to your ICanvasRenderer implementation.
//
// This widget never creates GL / Vulkan / D3D12 contexts for you and provides
// no shaders or swap chains; CreateGLContext only builds a WGL / EGL context.
// SetOnFrame hands you the per-frame timing inside the library message loop.
//
// Key notes:
// - The child window is lazily created on first draw, so SetOnReady fires no
//   earlier than after the first frame; do not read nativeHandle() right after
//   AddWidget.
// - Clicking the child window gives it keyboard focus (native behavior); the
//   host window loses focus until the user clicks back.
// - Mobile has no native child window (fallback to self-drawn): isReady() stays
//   false and nativeHandle() stays nullptr; desktop only.
// - Switching window mode rebuilds the window, so external renderers must be
//   rebuilt in SetOnReady (see CanvasWindowMode).

#pragma once
#include "ZeWidget.h"
#include "ZeSelfAction.h"
#include "ZeCanvasRenderer.h"   // Kit::ICanvasRenderer, the SetRenderer parameter type
#include "color4f.h"

#include <chrono>
#include <memory>
#include <string>

namespace GL {
    // The canvas-provided context is one type per platform:
    // Windows uses GLContext (WGL, ZeGLContext.h), Linux / Android / HarmonyOS
    // use EglContext (EGL, ZeEglContext.h). Both expose the same interface
    // (IsValid / MakeCurrent / DoneCurrent / SwapBuffers / SetSwapInterval),
    // so the canvas layer needs no platform branching.
#if defined(_WIN32)
    class GLContext;   // Internal implementation (ZeGLContext.h): WGL context on Windows
    using CanvasContext = GLContext;
#else
    class EglContext;  // Internal implementation (ZeEglContext.h): EGL context (X11 / mobile)
    using CanvasContext = EglContext;
#endif
}

namespace Kit {

    class Window;

    // Native surface kind of the canvas window.
    // Child (WS_CHILD): real child window, clipped to the host client area; use
    // for OpenGL (WGL works fine on child windows).
    // OwnedWindow (WS_POPUP owned window): separate HWND, repositioned by the
    // canvas each frame. Required for D3D12 / Vulkan because swap chains cannot
    // attach to child windows (CreateSwapChainForHwnd rejects them). Cost: the
    // library must reposition it whenever the host moves.
    enum class CanvasSurface {
        Child,        // Real child window (GL default)
        OwnedWindow   // Owned top-level window (D3D12 / Vulkan default)
    };

    // How the canvas is presented: embedded in the host layout or standalone.
    // CanvasSurface is orthogonal: it only matters when the mode is Embedded.
    // Both standalone modes create a host-owned window: it closes and hides with
    // the host and inherits its theme; the layout only keeps a placeholder.
    // Switching modes destroys and recreates the window (nativeHandle() changes,
    // SetOnReady fires again), so external renderers must be rebuilt there.
    // Fullscreen (SetFullscreen) does not rebuild the window; it only changes
    // styles and position, at the cost of one onResized.
    enum class CanvasWindowMode {
        Embedded,   // Embedded in host layout (default), follows the layout
        Standard,   // Standalone standard Win32 window (title bar / resizable / taskbar)
        Borderless  // Standalone borderless window (custom title bar / fullscreen preview)
    };

    class CanvasView : public Widget {
    public:
        // Constructor (default 320x200; stretches per size policy in layout)
        CanvasView();

        // Destructor (defined in .cpp; unique_ptr<GL::GLContext> needs a complete type)
        ~CanvasView() override;

        // Move constructor: widgets are value-semantic and AddWidget relocates
        // them; the explicit destructor suppresses the implicit move, so this is
        // required or the whole widget tree fails to compile.
        CanvasView(CanvasView&&) noexcept;

        // -------------------------------------------------------------------
        //  Measurement protocol (lets VBox / HBox recognize the widget)
        // -------------------------------------------------------------------
        // Natural width (explicit SetSize value if set, otherwise 320)
        float hintWidth() const override;
        // Natural height (explicit SetSize value if set, otherwise 200)
        float hintHeight() const override;

        // -------------------------------------------------------------------
        //  GL context (create / make current / proc address / swap buffers)
        // -------------------------------------------------------------------
        //
        //  Not a renderer: it only builds the context and gives you the Present
        //  channel; shaders, buffers and clears are all on your side. With it,
        //  your code does not need <windows.h>; GL addresses come from
        //  procAddress(). Vulkan / D3D12 do not go through this: use
        //  nativeHandle() and build your own instance / device / swap chain.

        // Creates the GL context (idempotent). Returns false on failure
        // (non-Windows, unsupported driver, or window not ready). Prefers 3.3
        // Core, falls back to compatibility. The context is current on return.
        // Uses the library's internal WGL code, so no graphics library dependency
        // is added to your project.
        bool CreateGLContext();

        // Whether the GL context has been created
        bool isGLContextReady() const;

        // Makes the context current (wglMakeCurrent); required between canvases.
        // Returns false if no context exists.
        bool MakeCurrent();
        // Releases the current context binding (wglMakeCurrent(nullptr, nullptr))
        void DoneCurrent();

        // Presents a frame (SwapBuffers); what and when to draw is up to SetOnFrame.
        void SwapBuffers();

        // Gets a GL function address (wglGetProcAddress, falling back to
        // opengl32.dll for pre-1.1 entries). Usable as a glad / GLEW loader:
        // gladLoadGLLoader((GLADloadproc)procAddress). nullptr on failure.
        void* procAddress(const char* name) const;

        // Swap interval: 1 = vsync (default), 0 = uncapped. Via wglSwapIntervalEXT;
        // silently ignored if the driver does not support it.
        void SetSwapInterval(int interval);

        // -------------------------------------------------------------------
        //  Surface kind / size
        // -------------------------------------------------------------------

        // Sets the canvas window surface kind (default Child). Child works for
        // OpenGL; OwnedWindow is required for D3D12 / Vulkan because swap chains
        // cannot attach to WS_CHILD windows. Must be called before the first
        // draw (changing it later does not rebuild the window). Only meaningful
        // in Embedded mode: standalone windows are always ChildKind::Owned.
        // Clipping is handled by the library in both modes: the window is placed
        // at the intersection with ancestor clip regions and hidden when fully
        // out of view (native windows do not go through Kit's clip stack).
        void SetSurfaceKind(CanvasSurface kind);
        // Current surface kind
        CanvasSurface surfaceKind() const { return surface_; }

        // Repositions the canvas window from the current widget rect (internal
        // use). Only needed for OwnedWindow: child windows follow their parent,
        // but owned windows must be re-synced when the host moves, otherwise the
        // canvas stays pinned in place. Called by Window on host geometry changes.
        void SyncToHost();

        // -------------------------------------------------------------------
        //  Window mode (standalone: standard / borderless + fullscreen)
        // -------------------------------------------------------------------
        //
        //  Answers "where is the canvas": a widget inside the host, or its own
        //  regular (host-owned) window with only a placeholder left in the layout.

        // Switches mode: embedded / standard window / borderless. Rebuilds the
        // window (child <-> top-level cannot be a style change), so nativeHandle()
        // changes, SetOnReady fires again and your renderer must be rebuilt.
        // Switching back to Embedded also exits fullscreen; Standard <-> Borderless
        // keeps the fullscreen state. The standalone window's initial geometry is
        // the widget's current rect in host client coordinates; afterwards its
        // position / size belong to the user and the layout no longer positions it.
        // Before the widget is attached (or visible) the request is only recorded
        // and applied at first sync. Mobile has no native windows: standalone
        // mode degrades to a self-drawn rect and fullscreen is a no-op.
        void SetWindowMode(CanvasWindowMode mode);

        // Current mode (see SetWindowMode)
        CanvasWindowMode windowMode() const { return windowMode_; }

        // Whether in a standalone window mode (Standard / Borderless)
        bool isWindowMode() const { return windowMode_ != CanvasWindowMode::Embedded; }

        // Standalone window title (shown in the system title bar). Only visible
        // in standalone mode; in Embedded mode it is only recorded. An empty
        // string restores the system default. Can be changed after creation.
        void SetWindowTitle(const std::string& title);

        // Enters / exits borderless fullscreen (fills the monitor, covers the
        // taskbar). Via Window::SetFullscreen: the original frame and styles are
        // remembered and restored on exit. Only effective in standalone mode;
        // otherwise recorded and applied when switching later. Does not rebuild
        // the window, but triggers one onResized. While minimized the system
        // rejects fullscreen requests, so isFullscreen() still reads false
        // (it reads the real window state, not the recorded intent).
        void SetFullscreen(bool on);

        // Whether currently fullscreen (real window state once created; recorded
        // intent before that)
        bool isFullscreen() const;

        // Toggles fullscreen (equivalent to SetFullscreen(!isFullscreen()))
        void ToggleFullscreen();

        // Whether Esc toggles fullscreen (default on). The canvas window consumes
        // Esc key presses itself (KeyPress, no autorepeat) and calls
        // ToggleFullscreen(). Disable it when the host has input fields or
        // dialogs that need Esc. Only applies when the canvas window has focus;
        // Esc pressed in the host does not toggle the canvas.
        void SetEscapeTogglesFullscreen(bool on);
        // Whether Esc toggles fullscreen (pairs with SetEscapeTogglesFullscreen)
        bool escapeTogglesFullscreen() const { return escTogglesFullscreen_; }

        // -------------------------------------------------------------------
        //  External renderer (widgets inside the canvas drawn by you)
        // -------------------------------------------------------------------
        //
        //  Without a renderer (default) Kit draws nothing. With one, Kit still
        //  runs layout / hit testing / display-list collection and calls your
        //  ICanvasRenderer implementation for the final submission, so regular
        //  Kit widgets work inside the canvas window with full features (focus,
        //  hover, scrolling, IME, animation, theming). Library-side resource
        //  caches create textures through the existing ITextureSink hook,
        //  the same mechanism used by the Vulkan / D3D12 backends.

        // Installs your renderer implementation (shared_ptr shared by the window
        // and the canvas). nullptr restores the default exclusive mode. Attach is
        // called when the canvas window is created (you build the context / swap
        // chain) and DrawFrame receives the laid-out display list each frame.
        // Must be called before the canvas window is created; calling it after
        // creation rebuilds the canvas window (SetOnReady fires again, which is
        // the right place to rebuild content and renderer). Do not mix this with
        // drawing yourself in SetOnFrame: one is library-driven, the other yours.
        // SetOnFrame stays usable as a per-frame timing hook. All renderer
        // callbacks run on the UI thread.
        void SetRenderer(std::shared_ptr<ICanvasRenderer> renderer);

        // Currently installed renderer (nullptr if none)
        ICanvasRenderer* renderer() const { return renderer_.get(); }

        // Whether an external renderer is installed
        bool hasRenderer() const { return renderer_ != nullptr; }

        // -------------------------------------------------------------------
        //  Canvas readiness / handles
        // -------------------------------------------------------------------

        // Whether the child window has been created (true only after first frame)
        bool isReady() const { return win_ != nullptr; }

        // The internal child window (Kit::Window); nullptr until ready. Owned by
        // this widget: do not delete it or call exec() on it.
        Kit::Window* canvasWindow() const { return win_.get(); }

        // Native window handle (HWND on Windows, Window on X11); nullptr until
        // ready. This is the handle your renderer needs: build the swap chain /
        // context on it and Present yourself.
        void* nativeHandle() const;

        // Current canvas pixel size (falls back to widget size until ready)
        int canvasWidth() const;
        int canvasHeight() const;

        // -------------------------------------------------------------------
        //  Callbacks
        // -------------------------------------------------------------------

        // Fires once when the child window is created (initialize your renderer
        // here). Sender protocol: Kit::As<CanvasView>(self). Runs during
        // rendering (first frame); avoid long blocking work (a swap chain taking
        // tens of ms is fine; load resources on another thread).
        void SetOnReady(SelfAction cb);

        // Fires when the canvas size actually changes (position changes do not
        // fire). In standalone window mode this callback is driven by host
        // frames: nobody else watches the standalone window's client area, so
        // keep the host producing frames (SetOnFrame). With a static canvas and
        // window mode, resize events arrive only at the host's next redraw.
        void SetOnResized(SelfAction cb);

        // Fires when the mode changed (SetWindowMode applied, or the standalone
        // window was closed by the user and it fell back to Embedded). The
        // window has been rebuilt and nativeHandle() is new; SetOnReady follows
        // right after (that is the time to rebuild the renderer). Sender
        // protocol same as SetOnReady.
        void SetOnWindowModeChanged(SelfAction cb);

        // Fires when fullscreen state changed (Esc / SetFullscreen /
        // ToggleFullscreen). Size changes go through SetOnResized.
        void SetOnFullscreenChanged(SelfAction cb);

        // Per-frame callback: the library loop calls you once per frame (the
        // rendering-loop entry). cb signature void(Kit::Widget& self, double dt);
        // dt is seconds since the previous frame. Installing it starts
        // continuous frame requests (~60fps, subject to system timer granularity);
        // passing nullptr stops them (idle zero CPU). Runs during rendering:
        // real drawing + Present is fine, but long blocking work slows the whole
        // UI. dt is real elapsed time, so animations stay correct when frames
        // drop. To own the pacing yourself (audio/video/games) use
        // Window::PollEvents() with your own loop instead; do not use both.
        void SetOnFrame(SelfAction1<double> cb);

        // -------------------------------------------------------------------
        //  Appearance (mostly hidden by the child window, visible before first frame)
        // -------------------------------------------------------------------

        // Placeholder background color (area before the child window appears)
        void SetBackgroundColor(const Color4f& c);
        // Canvas border (width <= 0 disables)
        void SetBorder(float width, const Color4f& c);

        // -------------------------------------------------------------------
        //  Theme
        // -------------------------------------------------------------------
        // Injects placeholder color / border from the theme's canvas section
        void ApplyTheme(const Theme& t) override;

        // -------------------------------------------------------------------
        //  Painting / hit testing
        // -------------------------------------------------------------------
        // Paints the placeholder (background + border). Takes over completely:
        // no BoxStyle. Geometry sync is NOT done here (see syncNativeSurface):
        // Paint can be skipped by fragment caching / viewport culling /
        // zero-size culling, which would miss native window updates.
        void Paint(FrameCollector& fc) const override;

        // Syncs this widget's geometry to the native window (called by the host
        // each frame; see ZeWidget.h). Three idempotent steps:
        // 1. invisible / zero-sized / ancestor invisible -> hide the window;
        // 2. visible -> position it at the intersection of its rect with every
        //    ancestor's clipChildrenRect() (native windows escape the drawing
        //    order; this is the last protection the layout can give them);
        // 3. empty intersection (scrolled out) -> hide; show again when back.
        // Happens regardless of whether Paint runs this frame.
        void syncNativeSurface() override;

        // Own rect decides hit testing (input lands on the child window first)
        bool HitTest(float x, float y) const override;

        // True when SetOnFrame is installed (host schedules per-frame updates)
        bool wantsAnimationFrame() const override { return onFrame_ != nullptr; }

    private:
        // Converts the widget rect to host client-area absolute coordinates
        void absoluteRect(float& ax, float& ay) const;

        // Computes the rect the native window should occupy (client-area
        // absolute coordinates). Returns false when it must not appear
        // (invisible, zero-sized, or fully scrolled out of view). Visibility is
        // checked against every ancestor's clip region.
        bool computeSurfaceRect(float& x, float& y, float& w, float& h) const;

        // Lazily creates the child window (idempotent; retried on failure). The
        // rect is the clipped intersection, not the widget rect. Non-const:
        // props that can only be sent at creation time (title / fullscreen /
        // Esc hook) are applied right after (applyStandaloneProps).
        void ensureCanvas(int ix, int iy, int iw, int ih);

        // Destroys the canvas window (mode switch: child <-> top-level cannot be
        // a style change). Just releases win_; next syncNativeSurface recreates
        // it for the new mode.
        void releaseCanvas();

        // Per-frame sync for standalone mode: create window (one reposition) and
        // watch client size; does not reposition
        void syncStandaloneSurface();

        // Applies props that can only be sent at window creation (title / fullscreen)
        void applyStandaloneProps();

        // Advances the per-frame callback (shared by Paint and syncStandaloneSurface)
        void tickFrame() const;

        // Esc hook: event filter installed on the standalone window (defined in
        // .cpp). Held by shared_ptr instead of a member because a filter's
        // address must stay stable once installed (it unregisters by address),
        // while CanvasView is value-semantic and gets relocated by AddWidget.
        // The heap slot moves with the shared_ptr; its self pointer is refreshed
        // each frame (see syncStandaloneSurface) so it self-corrects after moves.
        struct EscHook;

        // Child window (shared_ptr: follows value-semantic moves, auto-released)
        mutable std::shared_ptr<Window> win_;

        // Whether this widget hid the child window (hidden while widget invisible)
        mutable bool winHidden_ = false;

        // External renderer (see SetRenderer); null = default exclusive mode.
        // shared_ptr: one instance can serve multiple canvases and be kept
        // externally for queries.
        std::shared_ptr<ICanvasRenderer> renderer_;

        // Last geometry pushed to the child window (debounce: compare only)
        mutable int lastX_ = 0;
        mutable int lastY_ = 0;
        mutable int lastW_ = 0;
        mutable int lastH_ = 0;

        // Canvas window surface kind (see CanvasSurface)
        CanvasSurface surface_ = CanvasSurface::Child;

        // Window mode (see CanvasWindowMode)
        CanvasWindowMode windowMode_ = CanvasWindowMode::Embedded;
        // Fullscreen intent recorded before the window exists, applied at creation
        bool fullscreenPending_ = false;
        // Whether Esc toggles fullscreen (default on)
        bool escTogglesFullscreen_ = true;
        // Standalone window title (empty = system default)
        std::string title_;
        // Esc hook (standalone mode only; replaced on window rebuild)
        std::shared_ptr<EscHook> escHook_;

        // GL context (non-null only after CreateGLContext). Declared after win_:
        // reverse destruction order releases the context before the window.
        std::unique_ptr<GL::CanvasContext> gl_;
        // Requested swap interval (applied when the context is created)
        int swapInterval_ = 1;

        // Appearance
        Color4f bg_{ 0.0f, 0.0f, 0.0f, 0.0f };
        Color4f border_{ 0.0f, 0.0f, 0.0f, 0.0f };
        float   borderW_ = 1.0f;

        // Callbacks
        SelfAction onReady_;
        SelfAction onResized_;
        // Mode changed (including standalone window closed by the user)
        SelfAction onWindowModeChanged_;
        // Fullscreen state changed
        SelfAction onFullscreenChanged_;
        // Per-frame callback (non-null = requesting continuous frames)
        SelfAction1<double> onFrame_;
        // Last frame timestamp for dt (mutable: frame callback runs in const Paint)
        mutable bool   frameTickValid_ = false;
        mutable std::chrono::steady_clock::time_point lastFrameAt_{};
    };

} // namespace Kit
