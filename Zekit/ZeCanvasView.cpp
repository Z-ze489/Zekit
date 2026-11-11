
#include "pch.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>

#include "include/ZeCanvasView.h"

#if defined(_WIN32)
#  include "ZeGLContext.h"
#else
#  include <dlfcn.h>
#  include "ZeEglContext.h"
#  include "ZeWindowX11.h"
#endif
#include "include/Zekit.h"

#include "ZeFrameCollector.h"

namespace Kit {

    namespace {
        constexpr float kDefaultW = 320.0f;
        constexpr float kDefaultH = 200.0f;

        void FillRect(FrameCollector& fc, float x, float y, float w, float h,
                      const Color4f& c) {
            if (w <= 0.0f || h <= 0.0f || c.a <= 0.0f) return;
            fc.AddRect(x, y, w, h, c);
        }
    }

    struct CanvasView::EscHook : EventFilter {
        CanvasView* self = nullptr;
        Window* win = nullptr;
        Window::FilterId id = 0;

        void install(Window& w) {
            win = &w;
            id = w.InstallEventFilter(*this);
        }

        void detach() {
            if (win && id) win->RemoveEventFilter(id);
            win = nullptr;
            id = 0;
        }

        bool onKey(Window& w, KeyEvent& e) override {
            (void)w;
            if (!self) return false;
            if (e.type != EventType::KeyPress) return false;
            if (e.key != Key::Escape || e.isRepeat) return false;
            if (!self->escapeTogglesFullscreen()) return false;
            self->ToggleFullscreen();
            e.accept();
            return true;
        }
    };

    CanvasView::CanvasView() {
        markNativeSurface();

        ApplyTheme(Themes::Light());
    }

    void CanvasView::ApplyTheme(const Theme& t) {
        const CanvasStyle& s = t.canvas;
        if (s.background.a > 0.0f)  bg_ = s.background;
        if (s.borderColor.a > 0.0f) border_ = s.borderColor;
        if (s.borderWidth >= 0.0f)  borderW_ = s.borderWidth;
        RequestWindowRedraw();
    }

    float CanvasView::hintWidth() const {
        if (prefWidth() > 0.0f) return prefWidth();
        return kDefaultW;
    }

    float CanvasView::hintHeight() const {
        if (prefHeight() > 0.0f) return prefHeight();
        return kDefaultH;
    }

    void* CanvasView::nativeHandle() const {
        if (!win_ || win_->IsClosed()) return nullptr;
        return win_->hwnd();
    }

    int CanvasView::canvasWidth() const {
        if (win_ && !win_->IsClosed()) {
            int w = 0, h = 0;
            win_->GetClientSize(w, h);
            if (w > 0) return w;
        }
        return static_cast<int>(std::lround(width()));
    }

    int CanvasView::canvasHeight() const {
        if (win_ && !win_->IsClosed()) {
            int w = 0, h = 0;
            win_->GetClientSize(w, h);
            if (h > 0) return h;
        }
        return static_cast<int>(std::lround(height()));
    }

    void CanvasView::SetOnReady(SelfAction cb) {
        onReady_ = std::move(cb);
    }

    void CanvasView::SetOnResized(SelfAction cb) {
        onResized_ = std::move(cb);
    }

    void CanvasView::SetOnWindowModeChanged(SelfAction cb) {
        onWindowModeChanged_ = std::move(cb);
    }

    void CanvasView::SetOnFullscreenChanged(SelfAction cb) {
        onFullscreenChanged_ = std::move(cb);
    }

    void CanvasView::SetRenderer(std::shared_ptr<ICanvasRenderer> renderer) {
        if (renderer_ == renderer) return;
        renderer_ = std::move(renderer);

        if (!win_ || win_->IsClosed()) return;

        releaseCanvas();
        RequestWindowRedraw();
    }

    void CanvasView::SetBackgroundColor(const Color4f& c) {
        bg_ = c;
        RequestWindowRedraw();
    }

    void CanvasView::SetBorder(float width, const Color4f& c) {
        borderW_ = (width > 0.0f) ? width : 0.0f;
        border_ = c;
        RequestWindowRedraw();
    }

    void CanvasView::absoluteRect(float& ax, float& ay) const {
        ax = x();
        ay = y();
    }

    bool CanvasView::computeSurfaceRect(float& ox, float& oy,
                                        float& ow, float& oh) const {
        if (!isVisible() || width() <= 0.0f || height() <= 0.0f) return false;

        for (const Widget* p = parent(); p; p = p->parent())
            if (!p->isVisible()) return false;

        float x = 0.0f, y = 0.0f;
        absoluteRect(x, y);
        float w = width(), h = height();

        for (const Widget* p = parent(); p; p = p->parent()) {
            float rx = 0.0f, ry = 0.0f, rw = 0.0f, rh = 0.0f;
            if (!p->clipChildrenRect(rx, ry, rw, rh)) continue;
            const float x1 = std::max(x, rx);
            const float y1 = std::max(y, ry);
            const float x2 = std::min(x + w, rx + rw);
            const float y2 = std::min(y + h, ry + rh);
            if (x2 <= x1 || y2 <= y1) return false;
            x = x1; y = y1; w = x2 - x1; h = y2 - y1;
        }

        if (w <= 0.0f || h <= 0.0f) return false;
        ox = x; oy = y; ow = w; oh = h;
        return true;
    }

    void CanvasView::ensureCanvas(int ix, int iy, int iw, int ih) {
        if (win_) {
            if (!win_->IsClosed()) return;
            win_.reset();
        }

        Window* host = window();
        if (!host || host->IsClosed()) return;

        const bool detached = (windowMode_ != CanvasWindowMode::Embedded);
        ChildKind kind;
        bool borderlessBirth;
        bool systemShadow;
        if (detached) {
            kind = ChildKind::Owned;
            borderlessBirth = (windowMode_ == CanvasWindowMode::Borderless);
            systemShadow = !borderlessBirth;
        } else {
            kind = (surface_ == CanvasSurface::Child)
                ? ChildKind::Embedded
                : ChildKind::Popup;
            borderlessBirth = true;
            systemShadow = false;
        }

        auto child = std::make_shared<Window>(*host, ix, iy, iw, ih, kind,
                                              borderlessBirth, systemShadow);

        if (renderer_) {
            if (!child->UseExternalRenderer(renderer_)) {
                std::cerr << "[Zekit] CanvasView: UseExternalRenderer 被拒，"
                             "退回“整块交出去”模式。" << std::endl;
                child->SetRenderEnabled(false);
            }
        } else {
            child->SetRenderEnabled(false);
        }
        if (kind == ChildKind::Popup) {
            child->SetOpacity(1.0f);
        }
        child->Show();
        if (kind == ChildKind::Popup) {
            child->SetTopmost(false);
        }

        win_ = child;

        if (detached) {
            int cw = 0, ch = 0;
            child->GetClientSize(cw, ch);
            lastX_ = ix;
            lastY_ = iy;
            lastW_ = (cw > 0) ? cw : iw;
            lastH_ = (ch > 0) ? ch : ih;
        } else {
            lastX_ = ix;
            lastY_ = iy;
            lastW_ = iw;
            lastH_ = ih;
        }
        winHidden_ = false;

        if (detached) applyStandaloneProps();

        if (onReady_) onReady_(*this);
    }

    void CanvasView::releaseCanvas() {
        if (escHook_) escHook_->detach();
        gl_.reset();
        if (win_) win_.reset();
        winHidden_ = false;
        lastX_ = lastY_ = lastW_ = lastH_ = 0;
    }

    void CanvasView::applyStandaloneProps() {
        if (!win_ || win_->IsClosed()) return;

        if (!title_.empty()) win_->SetTitle(title_);

        if (windowMode_ == CanvasWindowMode::Borderless) win_->SetBorderlessResizable(true);

        if (fullscreenPending_ && !win_->IsFullscreen()) {
            win_->SetFullscreen(true);
            fullscreenPending_ = win_->IsFullscreen();
        }

        if (escTogglesFullscreen_) {
            if (!escHook_) escHook_ = std::make_shared<EscHook>();
            escHook_->self = this;
            escHook_->install(*win_);
        }
    }

    void CanvasView::syncStandaloneSurface() {
        if (escHook_) escHook_->self = this;

        if (win_ && win_->IsClosed()) {
            escHook_ = nullptr;
            win_.reset();
            fullscreenPending_ = false;
            windowMode_ = CanvasWindowMode::Embedded;
            winHidden_ = false;
            lastX_ = lastY_ = lastW_ = lastH_ = 0;
            if (onWindowModeChanged_) onWindowModeChanged_(*this);
            RequestWindowRedraw();
            return;
        }

        if (!win_) {
            Window* host = window();
            if (!host || host->IsClosed()) return;
            const float w = (width() > 0.0f) ? width() : hintWidth();
            const float h = (height() > 0.0f) ? height() : hintHeight();
            ensureCanvas(static_cast<int>(std::lround(x())),
                         static_cast<int>(std::lround(y())),
                         std::max(1, static_cast<int>(std::lround(w))),
                         std::max(1, static_cast<int>(std::lround(h))));
            if (!win_) return;
            tickFrame();
            return;
        }

        bool visible = isVisible();
        for (const Widget* p = parent(); p && visible; p = p->parent()) {
            if (!p->isVisible()) visible = false;
        }
        if (!visible) {
            if (!winHidden_) { win_->Hide(); winHidden_ = true; }
            tickFrame();
            return;
        }
        if (winHidden_) { win_->Show(); winHidden_ = false; }

        int cw = 0, ch = 0;
        win_->GetClientSize(cw, ch);
        if (cw != lastW_ || ch != lastH_) {
            lastW_ = cw;
            lastH_ = ch;
            if (onResized_) onResized_(*this);
        }

        tickFrame();
    }

    void CanvasView::SetWindowMode(CanvasWindowMode mode) {
        if (windowMode_ == mode) return;

        windowMode_ = mode;
        if (windowMode_ == CanvasWindowMode::Embedded) fullscreenPending_ = false;

        releaseCanvas();

        if (onWindowModeChanged_) onWindowModeChanged_(*this);
        RequestWindowRedraw();
    }

    void CanvasView::SetWindowTitle(const std::string& title) {
        title_ = title;
        if (win_ && !win_->IsClosed() && isWindowMode()) win_->SetTitle(title_);
    }

    void CanvasView::SetFullscreen(bool on) {
        if (!isWindowMode()) { fullscreenPending_ = on; return; }
        if (!win_ || win_->IsClosed()) { fullscreenPending_ = on; return; }
        if (win_->IsFullscreen() == on) { fullscreenPending_ = on; return; }

        win_->SetFullscreen(on);
        const bool now = win_->IsFullscreen();
        fullscreenPending_ = now;
        if (now != on) return;

        if (onFullscreenChanged_) onFullscreenChanged_(*this);
        RequestWindowRedraw();
    }

    bool CanvasView::isFullscreen() const {
        if (win_ && !win_->IsClosed() && isWindowMode()) return win_->IsFullscreen();
        return fullscreenPending_;
    }

    void CanvasView::ToggleFullscreen() {
        SetFullscreen(!isFullscreen());
    }

    void CanvasView::SetEscapeTogglesFullscreen(bool on) {
        if (escTogglesFullscreen_ == on) return;
        escTogglesFullscreen_ = on;

        if (!win_ || win_->IsClosed()) return;
        if (on) {
            if (!escHook_) escHook_ = std::make_shared<EscHook>();
            escHook_->self = this;
            escHook_->install(*win_);
        } else if (escHook_) {
            escHook_->detach();
        }
    }

    void CanvasView::syncNativeSurface() {
        if (windowMode_ != CanvasWindowMode::Embedded) {
            syncStandaloneSurface();
            return;
        }

        float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
        if (!computeSurfaceRect(x, y, w, h)) {
            if (win_ && !win_->IsClosed() && !winHidden_) {
                win_->Hide();
                winHidden_ = true;
            }
            return;
        }

        const int vx = static_cast<int>(std::lround(x));
        const int vy = static_cast<int>(std::lround(y));
        const int vw = std::max(1, static_cast<int>(std::lround(w)));
        const int vh = std::max(1, static_cast<int>(std::lround(h)));

        if (!win_ || win_->IsClosed()) {
            ensureCanvas(vx, vy, vw, vh);
            return;
        }

        const bool moved = (vx != lastX_ || vy != lastY_);
        const bool resized = (vw != lastW_ || vh != lastH_);

        if (winHidden_) {
            win_->Show();
            winHidden_ = false;
        }
        if (moved) win_->SetPositionRelativeToHost(vx, vy);
        if (resized) win_->SetSize(vw, vh);

        lastX_ = vx;
        lastY_ = vy;
        lastW_ = vw;
        lastH_ = vh;

        if (resized && onResized_) onResized_(*const_cast<CanvasView*>(this));
    }

    CanvasView::~CanvasView() = default;
    CanvasView::CanvasView(CanvasView&&) noexcept = default;

    bool CanvasView::CreateGLContext() {
        if (gl_ && gl_->IsValid()) return true;

        if (!win_ || win_->IsClosed()) {
            float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
            if (!computeSurfaceRect(x, y, w, h)) return false;
            ensureCanvas(static_cast<int>(std::lround(x)),
                         static_cast<int>(std::lround(y)),
                         std::max(1, static_cast<int>(std::lround(w))),
                         std::max(1, static_cast<int>(std::lround(h))));
        }
        if (!win_ || win_->IsClosed()) return false;

#if defined(_WIN32)
        auto gl = std::make_unique<GL::GLContext>();
        HWND hwnd = static_cast<HWND>(win_->hwnd());
        if (!hwnd) return false;

        if (!gl->Create(hwnd, 3, 3, true)
            && !gl->Create(hwnd, 2, 1, false)) {
            return false;
        }
        gl->MakeCurrent();
        gl->SetSwapInterval(swapInterval_);
        gl_ = std::move(gl);
        return true;
#elif defined(__linux__)
        void* dpy = Plat::X11DisplayHandle();
        unsigned long xid = Plat::X11WindowId(win_->hwnd());
        if (!dpy || xid == 0) {
            std::cerr << "[Zekit/Canvas] 建 GL 上下文失败：display="
                << (dpy ? "ok" : "空") << " xid=0x"
                << std::hex << xid << std::dec << "\n";
            return false;
        }

        auto gl = std::make_unique<GL::CanvasContext>();
        if (!gl->Create(dpy, xid, 3, 3, true)
            && !gl->Create(dpy, xid, 2, 1, false)) {
            std::cerr << "[Zekit/Canvas] EGL 上下文建不起来（xid=0x"
                << std::hex << xid << std::dec << "）→ 画布将没有内容\n";
            return false;
        }
        gl->MakeCurrent();
        gl->SetSwapInterval(swapInterval_);
        gl_ = std::move(gl);
        return true;
#else
        return false;
#endif
    }

    bool CanvasView::isGLContextReady() const {
        return gl_ && gl_->IsValid();
    }

    bool CanvasView::MakeCurrent() {
        if (!gl_ || !gl_->IsValid()) return false;
        gl_->MakeCurrent();
        return true;
    }

    void CanvasView::DoneCurrent() {
        if (gl_ && gl_->IsValid()) gl_->DoneCurrent();
    }

    void CanvasView::SwapBuffers() {
        if (!gl_ || !gl_->IsValid()) return;
        gl_->MakeCurrent();
        gl_->SwapBuffers();
    }

    void* CanvasView::procAddress(const char* name) const {
        if (!name || !isGLContextReady()) return nullptr;
#if defined(_WIN32)
        void* p = reinterpret_cast<void*>(::wglGetProcAddress(name));
        if (p && p != reinterpret_cast<void*>(1) && p != reinterpret_cast<void*>(2)
            && p != reinterpret_cast<void*>(3) && p != reinterpret_cast<void*>(-1)) {
            return p;
        }
        static HMODULE mod = ::LoadLibraryW(L"opengl32.dll");
        return mod ? reinterpret_cast<void*>(::GetProcAddress(mod, name)) : nullptr;
#elif defined(__linux__)
        using EglGetProcAddressFn = void* (*)(const char*);
        static EglGetProcAddressFn eglGetProc = []() -> EglGetProcAddressFn {
            void* h = ::dlopen("libEGL.so.1", RTLD_LAZY | RTLD_LOCAL);
            if (!h) h = ::dlopen("libEGL.so", RTLD_LAZY | RTLD_LOCAL);
            return h ? reinterpret_cast<EglGetProcAddressFn>(::dlsym(h, "eglGetProcAddress"))
                     : nullptr;
            }();
        if (eglGetProc) {
            if (void* p = eglGetProc(name)) return p;
        }
        static void* glLib = []() -> void* {
            void* h = ::dlopen("libGL.so.1", RTLD_LAZY | RTLD_LOCAL);
            if (!h) h = ::dlopen("libGL.so", RTLD_LAZY | RTLD_LOCAL);
            return h;
            }();
        return glLib ? ::dlsym(glLib, name) : nullptr;
#else
        return nullptr;
#endif
    }

    void CanvasView::SetSwapInterval(int interval) {
        swapInterval_ = (interval <= 0) ? 0 : 1;
        if (gl_ && gl_->IsValid()) gl_->SetSwapInterval(swapInterval_);
    }

    void CanvasView::SyncToHost() {
        syncNativeSurface();
    }

    void CanvasView::SetSurfaceKind(CanvasSurface kind) {
        surface_ = kind;
    }

    void CanvasView::SetOnFrame(SelfAction1<double> cb) {
        onFrame_ = std::move(cb);

        if (onFrame_) RequestWindowRedraw();
    }

    void CanvasView::Paint(FrameCollector& fc) const {
        const float w = width(), h = height();
        if (w <= 0.0f || h <= 0.0f) return;

        FillRect(fc, x(), y(), w, h, bg_);

        if (borderW_ > 0.0f && border_.a > 0.0f) {
            const float bw = std::min(borderW_, std::min(w, h) * 0.5f);
            FillRect(fc, x(), y(), w, bw, border_);
            FillRect(fc, x(), y() + h - bw, w, bw, border_);
            FillRect(fc, x(), y() + bw, bw, h - bw * 2.0f, border_);
            FillRect(fc, x() + w - bw, y() + bw, bw, h - bw * 2.0f, border_);
        }

        if (windowMode_ == CanvasWindowMode::Embedded) tickFrame();
    }

    void CanvasView::tickFrame() const {
        if (!onFrame_) return;

        const auto now = std::chrono::steady_clock::now();
        double dt = 1.0 / 60.0;
        if (frameTickValid_) {
            dt = std::chrono::duration<double>(now - lastFrameAt_).count();
            if (dt < 0.0) dt = 0.0;
            if (dt > 0.25) dt = 0.25;
        }
        frameTickValid_ = true;
        lastFrameAt_ = now;

        onFrame_(*const_cast<CanvasView*>(this), dt);

        if (onFrame_) {
            if (Kit::Window* host = window()) host->RequestFrameWake(8);
        }
    }

    bool CanvasView::HitTest(float x_, float y_) const {
        return isVisible() && x_ >= x() && x_ < x() + width()
            && y_ >= y() && y_ < y() + height();
    }

}
