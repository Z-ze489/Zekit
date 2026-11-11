
#include "include/ZeOverlayWindow.h"
#include "include/ZePopupHost.h"
#include "include/Zekit.h"
#include "ZeOverlayHost.h"
#include "ZeFrameCollector.h"

namespace Kit {

    bool OverlayWindow::SupportsNative(const Window& host) {
        return OverlayHost::Supports(host);
    }

    OverlayWindow::OverlayWindow(Window& host, OverlayLayer layer)
        : surface_(OverlayHost::Acquire(host, layer)) {}

    OverlayWindow::~OverlayWindow() {
        if (!surface_) return;
        if (surface_->ownedBy(this)) surface_->Hide();
        surface_->ReleaseOwner(this);
        surface_.reset();
    }

    void OverlayWindow::attachOwner() {
        if (!surface_) return;
        surface_->SetOwner(this, [this] {
            if (onTakenOver_) onTakenOver_();
        });
    }

    void OverlayWindow::SetOnTakenOver(std::function<void()> cb) {
        onTakenOver_ = std::move(cb);
    }

    void OverlayWindow::SetContent(OverlayContent content) {
        if (!surface_) return;
        ownContent_ = content;
        attachOwner();
        surface_->SetContent(std::move(content));
    }

    Panel& OverlayWindow::content() {
        if (!surface_) {
            static Panel kFallback;
            return kFallback;
        }
        attachOwner();
        return surface_->content();
    }

    bool OverlayWindow::isReady() const {
        return surface_ && surface_->ensureReady();
    }

    bool OverlayWindow::isVisible() const {
        return surface_ && surface_->isVisible() && surface_->ownedBy(this);
    }

    Window* OverlayWindow::nativeWindow() {
        return surface_ ? surface_->nativeWindow() : nullptr;
    }

    void OverlayWindow::Show(float hostX, float hostY, float width, float height) {
        if (!surface_) return;
        attachOwner();
        if (ownContent_) surface_->SetContent(*ownContent_);
        surface_->Show(hostX, hostY, width, height);
    }

    void OverlayWindow::Hide() {
        if (!surface_) return;
        if (surface_->ownedBy(this)) surface_->Hide();
    }

    void OverlayWindow::Repaint() {
        if (surface_ && surface_->ownedBy(this)) surface_->Repaint();
    }

    void OverlayWindow::SetBackground(const Color4f& bg) {
        if (surface_) surface_->SetBackground(bg);
    }

    void OverlayWindow::SetFollowHost(bool on) {
        if (surface_) surface_->SetFollowHost(on);
    }

    void OverlayWindow::SetOpacity(float alpha) {
        if (surface_ && surface_->nativeWindow()) surface_->nativeWindow()->SetOpacity(alpha);
    }

    void OverlayWindow::SetClickThrough(bool on) {
        if (surface_) surface_->SetClickThrough(on);
    }

    void OverlayWindow::SetTopmost(bool on) {
        if (surface_) surface_->SetTopmost(on);
    }

    void OverlayWindow::SetTheme(const Theme& theme) {
        if (surface_) surface_->SetTheme(theme);
    }

    void OverlayWindow::SetOnHostGeometryChanged(std::function<void()> cb) {
        if (surface_) surface_->SetOnHostGeometryChanged(std::move(cb));
    }

    PopupHostWidget::PopupHostWidget() = default;

    PopupHostWidget::~PopupHostWidget() = default;
    PopupHostWidget::PopupHostWidget(PopupHostWidget&&) noexcept = default;

    bool PopupHostWidget::popupOverlayVisible() const {
        return popupOverlay_ && popupOverlay_->isVisible();
    }

    void PopupHostWidget::showPopupOverlay() {
        Window* w = window();
        if (!w || w->IsClosed()) return;
        if (!OverlayWindow::SupportsNative(*w)) return;

        if (!popupOverlay_) {
            popupOverlay_ = std::make_unique<OverlayWindow>(*w, OverlayLayer::Panel);

            popupOverlay_->SetOnTakenOver([this] { dismissPopup(); });

            OverlayContent content;
            content.paint = [this](FrameCollector& fc) {
                paintPopupInto(fc, true);
            };
            content.onPress = [this](MouseEvent& e) { return onPopupPress(e); };
            content.onRelease = [this](MouseEvent& e) { return onPopupRelease(e); };
            content.onMove = [this](MouseEvent& e) { return onPopupMove(e); };
            content.onWheel = [this](MouseEvent& e) { return onPopupWheel(e); };
            content.onLeave = [this] { onPopupLeave(); };
            content.onHostPress = [this](float hx, float hy) {
                if (!open_) return;
                if (Widget::HitTest(hx, hy)) return;
                dismissPopup();
                RequestWindowRedraw();
            };
            popupOverlay_->SetContent(std::move(content));
            popupOverlay_->SetBackground(Color4f{ 0.0f, 0.0f, 0.0f, 0.0f });
        }

        float x = 0.0f, y = 0.0f, w2 = 0.0f, h2 = 0.0f;
        if (!popupRect(x, y, w2, h2)) return;

        if (!popupOverlay_->isReady()) return;
        popupOverlay_->Show(x, y, w2, h2);
    }

    void PopupHostWidget::paintHostAndPopup(FrameCollector& fc) const {
        if (width() <= 0.0f || height() <= 0.0f) return;

        TextWidget::Paint(fc);
        paintFace(fc);

        if (!paintsOnTop()) return;
        paintPopupInto(fc, false);
    }

    bool PopupHostWidget::popupFacePress(MouseEvent& e) {
        popupPressedInFace_ = true;
        e.accept();
        return true;
    }

    bool PopupHostWidget::OnMousePress(MouseEvent& e) {
        if (e.button != MouseButton::Left) return false;
        if (!isEnabled()) return false;

        if (open_) {
            if (onPopupPress(e)) return true;

            dismissPopup();
            e.accept();
            return true;
        }

        return popupFacePress(e);
    }

    void PopupHostWidget::hidePopupOverlay() {
        if (popupOverlay_) popupOverlay_->Hide();
    }

}
