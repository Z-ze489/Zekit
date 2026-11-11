
#include "ZeOverlayHost.h"
#include "ZeRectShape.h"
#include "ZeFrameCollector.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace Kit {

    bool AnyFrameActive();

    namespace {

        class SurfacePane : public Panel {
        public:
            explicit SurfacePane(OverlaySurface& owner) : owner_(owner) {}

            float hintWidth() const override { return 0.0f; }
            float hintHeight() const override { return 0.0f; }

            void DoLayout() override {}

            void Paint(FrameCollector& fc) const override { owner_.paintInto(fc); }
            bool HitTest(float x, float y) const override { return owner_.hitTestAt(x, y); }

            bool OnMousePress(MouseEvent& e) override { return owner_.dispatchPress(e); }
            bool OnMouseRelease(MouseEvent& e) override { return owner_.dispatchRelease(e); }
            bool OnMouseMove(MouseEvent& e) override { return owner_.dispatchMove(e); }
            bool OnMouseWheel(MouseEvent& e) override { return owner_.dispatchWheel(e); }
            void OnMouseLeave() override { owner_.dispatchLeave(); }

        private:
            OverlaySurface& owner_;
        };

        using LayerMap = std::map<int, std::weak_ptr<OverlaySurface>>;
        std::map<const Window*, LayerMap>& Registry() {
            static std::map<const Window*, LayerMap> table;
            return table;
        }

    }

    bool OverlayHost::Supports(const Window& host) {
        return !host.IsClosed()
            && host.Supports(Capability::ChildWindow)
            && host.Supports(Capability::PopupWindow)
            && host.Supports(Capability::PerPixelTransparent);
    }

    std::shared_ptr<OverlaySurface> OverlayHost::Acquire(Window& host, OverlayLayer layer) {
        if (!Supports(host)) return nullptr;

        LayerMap& layers = Registry()[&host];
        auto it = layers.find(static_cast<int>(layer));
        if (it != layers.end()) {
            if (auto alive = it->second.lock()) return alive;
        }

        auto surface = std::make_shared<OverlaySurface>(host, layer);
        layers[static_cast<int>(layer)] = surface;
        return surface;
    }

    void OverlayHost::NotifyGeometryChanged(Window& host) {
        auto hostIt = Registry().find(&host);
        if (hostIt == Registry().end()) return;

        std::vector<std::shared_ptr<OverlaySurface>> live;
        live.reserve(hostIt->second.size());
        for (auto& kv : hostIt->second) {
            if (auto s = kv.second.lock()) live.push_back(std::move(s));
        }
        for (auto& s : live) s->followHost();
    }

    void OverlayHost::NotifyHostPressed(Window& host, float x, float y) {
        auto hostIt = Registry().find(&host);
        if (hostIt == Registry().end()) return;

        std::vector<std::shared_ptr<OverlaySurface>> live;
        live.reserve(hostIt->second.size());
        for (auto& kv : hostIt->second) {
            if (auto s = kv.second.lock()) live.push_back(std::move(s));
        }
        for (auto& s : live) s->notifyHostPressed(x, y);
    }

    void OverlayHost::Dismiss(Window& host) {
        auto it = Registry().find(&host);
        if (it == Registry().end()) return;

        std::vector<std::shared_ptr<OverlaySurface>> live;
        live.reserve(it->second.size());
        for (auto& kv : it->second) {
            if (auto s = kv.second.lock()) live.push_back(std::move(s));
        }

        for (auto& s : live) s->Hide();
    }

    void OverlayHost::Forget(Window& host) {
        auto it = Registry().find(&host);
        if (it == Registry().end()) return;

        std::vector<std::shared_ptr<OverlaySurface>> live;
        for (auto& kv : it->second) {
            if (auto s = kv.second.lock()) live.push_back(std::move(s));
        }
        Registry().erase(it);

        for (auto& s : live) s->Hide();
    }

    OverlaySurface::OverlaySurface(Window& host, OverlayLayer layer)
        : host_(host), layer_(layer) {}

    OverlaySurface::~OverlaySurface() {
        if (popup_ && !popup_->IsClosed() && visible_) popup_->Hide();
    }

    void OverlaySurface::ensureWindow() {
        if (popup_ && !popup_->IsClosed()) return;
        if (host_.IsClosed()) return;
        if (!OverlayHost::Supports(host_)) return;

        auto w = std::make_shared<Window>(host_, 16, 16, 64, 64, ChildKind::Overlay);
        if (w->IsClosed()) return;

        w->Hide();

        w->SetTheme(host_.theme());

        w->SetClearAlpha(0.0f);

        w->SetOpacity(1.0f);

        auto& pane = w->AddWidget(SurfacePane(*this));
        pane.SetFillWindow(true);
        pane_ = &pane;

        auto& c = pane.AddChild(Panel());
        c.SetBackground(Color4f{ 0.0f, 0.0f, 0.0f, 0.0f });
        c.SetPosition(0.0f, 0.0f);
        c.SetSize(64.0f, 64.0f);
        contentHost_ = &c;

        popup_ = std::move(w);
    }

    bool OverlaySurface::ensureReady() {
        ensureWindow();
        return isReady();
    }

    bool OverlaySurface::isReady() const {
        return popup_ != nullptr && !popup_->IsClosed();
    }

    Window* OverlaySurface::nativeWindow() {
        return isReady() ? popup_.get() : nullptr;
    }

    void OverlaySurface::SetOwner(void* owner, std::function<void()> onEvict) {
        if (owner_ && owner_ != owner) {
            auto previous = std::move(onEvict_);
            onEvict_ = nullptr;
            owner_ = nullptr;
            Hide();
            if (previous) previous();
        }
        owner_ = owner;
        onEvict_ = std::move(onEvict);
    }

    void OverlaySurface::ReleaseOwner(void* owner) {
        if (owner_ != owner) return;
        owner_ = nullptr;
        onEvict_ = nullptr;
    }

    void OverlaySurface::SetContent(OverlayContent content) {
        content_ = std::move(content);
        Repaint();
    }

    Panel& OverlaySurface::content() {
        ensureWindow();
        if (!contentHost_) {
            static Panel kFallback;
            return kFallback;
        }
        return *contentHost_;
    }

    void OverlaySurface::Repaint() {
        if (!isReady()) return;
        popup_->RequestRedraw();
    }

    void OverlaySurface::Show(float hostX, float hostY, float width, float height) {
        ensureWindow();
        if (!isReady()) return;

        anchorX_ = hostX;
        anchorY_ = hostY;
        pendW_ = width;
        pendH_ = height;

        if (AnyFrameActive()) {
            showDeferred_ = true;
            if (!deferQueued_ && popup_) {
                deferQueued_ = true;
                popup_->SetTimeout(0, [wp = std::weak_ptr<OverlaySurface>(shared_from_this())] {
                    if (auto s = wp.lock()) s->flushDeferredShow();
                });
            }
            return;
        }

        showNow();
    }

    void OverlaySurface::flushDeferredShow() {
        deferQueued_ = false;
        if (!showDeferred_ || !isReady()) return;
        showDeferred_ = false;
        showNow();
    }

    void OverlaySurface::showNow() {
        if (!isReady()) return;

        const int iw = std::max(1, static_cast<int>(std::lround(pendW_)));
        const int ih = std::max(1, static_cast<int>(std::lround(pendH_)));

        if (contentHost_) {
            contentHost_->SetPosition(0.0f, 0.0f);
            contentHost_->SetSize(static_cast<float>(iw), static_cast<float>(ih));
        }

        popup_->SetSize(iw, ih);
        popup_->SetClickThrough(clickThrough_);

        reposition();
        if (!visible_) popup_->Show();
        visible_ = true;

        popup_->RequestRedraw();

        popup_->RequestFrameWake(16);
        const std::weak_ptr<OverlaySurface> selfWp = shared_from_this();
        popup_->SetTimeout(48, [selfWp] {
            if (auto s = selfWp.lock()) s->Repaint();
        });
    }

    void OverlaySurface::Hide() {
        showDeferred_ = false;
        if (popup_ && !popup_->IsClosed() && visible_) popup_->Hide();
        visible_ = false;
    }

    void OverlaySurface::reposition() {
        if (!isReady()) return;
        popup_->SetPositionRelativeToHost(static_cast<int>(std::lround(anchorX_)),
                                          static_cast<int>(std::lround(anchorY_)));
    }

    void OverlaySurface::followHost() {
        if (!followHost_ || !visible_ || !isReady()) return;

        if (onHostGeom_) onHostGeom_();

        if (visible_) reposition();
    }

    void OverlaySurface::SetBackground(const Color4f& bg) {
        background_ = bg;
        Repaint();
    }

    void OverlaySurface::SetClickThrough(bool on) {
        clickThrough_ = on;
        if (isReady()) popup_->SetClickThrough(on);
    }

    void OverlaySurface::SetTopmost(bool on) {
        if (isReady()) popup_->SetTopmost(on);
    }

    void OverlaySurface::SetTheme(const Theme& t) {
        if (!isReady()) return;
        popup_->SetTheme(t);
        Repaint();
    }

    namespace {
        void PaintRect(FrameCollector& fc, float x, float y, float w, float h,
                       const Color4f& color) {
            if (w <= 0.0f || h <= 0.0f || color.a <= 0.0f) return;
            fc.AddShape(RectShape(Vec2{ x, y }, Vec2{ w, h }), color);
        }
    }

    void OverlaySurface::paintInto(FrameCollector& fc) const {
        const float w = static_cast<float>(popup_ ? popup_->width() : 0);
        const float h = static_cast<float>(popup_ ? popup_->height() : 0);
        if (w <= 0.0f || h <= 0.0f) return;

        PaintRect(fc, 0.0f, 0.0f, w, h, background_);

        if (content_.paint) content_.paint(fc);
    }

    bool OverlaySurface::hitTestAt(float x, float y) const {
        (void)x; (void)y;
        return true;
    }

    bool OverlaySurface::dispatchPress(MouseEvent& e) {
        if (!content_.onPress) return false;
        MouseEvent t = e;
        t.x += anchorX_; t.y += anchorY_;
        const bool handled = content_.onPress(t);
        if (handled) { e.accept(); Repaint(); }
        return handled;
    }

    bool OverlaySurface::dispatchRelease(MouseEvent& e) {
        if (!content_.onRelease) return false;
        MouseEvent t = e;
        t.x += anchorX_; t.y += anchorY_;
        const bool handled = content_.onRelease(t);
        if (handled) { e.accept(); Repaint(); }
        return handled;
    }

    bool OverlaySurface::dispatchMove(MouseEvent& e) {
        if (!content_.onMove) return false;
        MouseEvent t = e;
        t.x += anchorX_; t.y += anchorY_;
        const bool handled = content_.onMove(t);
        if (handled) Repaint();
        return handled;
    }

    bool OverlaySurface::dispatchWheel(MouseEvent& e) {
        if (!content_.onWheel) return false;
        MouseEvent t = e;
        t.x += anchorX_; t.y += anchorY_;
        const bool handled = content_.onWheel(t);
        if (handled) { e.accept(); Repaint(); }
        return handled;
    }

    void OverlaySurface::notifyHostPressed(float x, float y) {
        if (!visible_ || !content_.onHostPress) return;
        content_.onHostPress(x, y);
    }

    void OverlaySurface::dispatchLeave() {
        if (content_.onLeave) content_.onLeave();
    }

}
