
#include "pch.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

#include "include/ZeDrawer.h"
#include "include/ZeOverlayWindow.h"
#include "include/Zekit.h"

#include "ZeDrawUtil.h"
#include "ZeFrameCollector.h"

namespace Kit {

    namespace {
        constexpr float kFallbackPanelSize = 320.0f;
        constexpr float kFallbackTitleHeight = 44.0f;
        constexpr float kPanelPad = 16.0f;
        constexpr float kCloseSize = 22.0f;
        constexpr float kCloseMargin = 12.0f;
        constexpr float kCrossSize = 5.0f;
        constexpr float kCrossThickness = 1.6f;
    }

    Drawer::Drawer() {
        SetFillWindow(true);

        SetDirection(FlexDirection::Column);
        SetAlign(AlignItems::Stretch);

        Panel body;
        body.SetDirection(FlexDirection::Column);
        body.SetAlign(AlignItems::Stretch);
        Panel& placed = AddChild(std::move(body));
        body_ = &placed;

        ApplyTheme(Themes::Light());
        syncBodyPadding();

        SetVisible(false);

        SetFocusable(true);
        SetDefaultSize(kFallbackPanelSize, 0.0f);
    }

    Drawer::~Drawer() {
        unmountBody();
        if (overlay_) overlay_->Hide();
    }

    Drawer::Drawer(Drawer&&) noexcept = default;

    bool Drawer::nativePopup() const {
        return overlay_ && overlay_->isVisible();
    }

    void Drawer::mountBody() const {
        if (bodyMounted_ || !body_ || !overlay_) return;

        auto* self = const_cast<Drawer*>(this);
        auto owned = self->releaseChild(*self->body_);
        if (!owned) return;

        overlay_->content().adoptChild(std::move(owned));
        bodyMounted_ = true;
    }

    void Drawer::unmountBody() const {
        if (!bodyMounted_ || !body_ || !overlay_) return;

        auto* self = const_cast<Drawer*>(this);
        auto owned = overlay_->content().releaseChild(*self->body_);
        if (owned) {
            Widget& back = self->adoptChild(std::move(owned));
            self->body_ = static_cast<Panel*>(&back);
        }
        bodyMounted_ = false;
    }

    void Drawer::syncOverlay() const {
        if (!open_) {
            unmountBody();
            if (overlay_) overlay_->Hide();
            return;
        }

        Window* w = this->window();
        if (!w || w->IsClosed()) return;
        if (!OverlayWindow::SupportsNative(*w)) return;

        float px = 0.0f, py = 0.0f, pw = 0.0f, ph = 0.0f;
        panelRect(px, py, pw, ph);
        if (pw <= 0.0f || ph <= 0.0f) { if (overlay_) overlay_->Hide(); return; }

        if (!overlay_) {
            overlay_ = std::make_unique<OverlayWindow>(*w, OverlayLayer::Panel);
            auto* self = const_cast<Drawer*>(this);
            overlay_->SetOnTakenOver([self] { self->Close(); });

            OverlayContent content;
            content.paint = [this](FrameCollector& fc) {
                paintChrome(fc, true);
            };
            content.onPress = [self](MouseEvent& e) { return self->onPopupPress(e); };
            content.onRelease = [self](MouseEvent& e) { return self->onPopupRelease(e); };
            content.onMove = [self](MouseEvent& e) { return self->onPopupMove(e); };
            content.onLeave = [self] { self->onPopupLeave(); };
            overlay_->SetContent(std::move(content));
            overlay_->SetBackground(Color4f{ 0.0f, 0.0f, 0.0f, 0.0f });
            overlay_->SetOnHostGeometryChanged([this] { syncOverlay(); });
        }

        mountBody();

        const bool sameGeom = (px == overlayX_ && py == overlayY_
                               && pw == overlayW_ && ph == overlayH_);
        overlayX_ = px; overlayY_ = py; overlayW_ = pw; overlayH_ = ph;

        if (overlay_->isVisible() && sameGeom) overlay_->Repaint();
        else                                   overlay_->Show(px, py, pw, ph);
    }

    void Drawer::ApplyTheme(const Theme& t) {
        Panel::ApplyTheme(t);

        if (titleHeight_ <= 0.0f)
            titleHeight_ = (t.ctrlHL > 0.0f) ? t.ctrlHL : kFallbackTitleHeight;
        if (panelSize_ <= 0.0f)
            panelSize_ = kFallbackPanelSize;
        if (scrimColor_.a <= 0.0f)
            scrimColor_ = t.subwindow.scrimColor;

        shadowColor_ = t.subwindow.shadowColor;
        if (shadowColor_.a <= 0.0f) shadowColor_ = Color4f{ 0.0f, 0.0f, 0.0f, 0.20f };
        shadowOffset_ = (t.subwindow.shadowOffsetY > 0.0f) ? t.subwindow.shadowOffsetY : 4.0f;
        shadowSpread_ = (t.subwindow.shadowSpread >= 0.0f) ? t.subwindow.shadowSpread : 2.0f;

        syncBodyPadding();
        Invalidate();
    }

    void Drawer::syncBodyPadding() {
        if (!body_) return;
        const float top = (!title_.empty() && titleHeight_ > 0.0f) ? titleHeight_ : 0.0f;
        body_->SetPadding(kPanelPad, top + kPanelPad, kPanelPad, kPanelPad);
    }

    void Drawer::Open() {
        if (open_) return;
        open_ = true;
        SetVisible(true);
        requestRepaint();

        if (onOpened_) onOpened_(*this);
    }

    void Drawer::Close() {
        if (!open_) return;
        open_ = false;
        SetVisible(false);
        hoverClose_ = false;
        pressedClose_ = false;

        syncOverlay();
        requestRepaint();

        if (onClosed_) onClosed_(*this);
    }

    void Drawer::requestCloseByUser() {
        if (onRequestClose_) onRequestClose_(*this);
        else Close();
    }

    void Drawer::requestRepaint() {
        RequestWindowRedraw();
    }

    void Drawer::DoLayout() {
        Panel* cp = body_;
        if (!cp) return;

        float px = 0.0f, py = 0.0f, pw = 0.0f, ph = 0.0f;
        panelRect(px, py, pw, ph);

        if (bodyMounted_) {
            contentW_ = std::max(0.0f, pw);
            contentH_ = std::max(0.0f, ph);
            return;
        }

        LayoutAccess::RestorePref(*cp);

        const float w = std::max(0.0f, pw);
        const float h = std::max(0.0f, ph);

        LayoutAccess::SetActual(*cp, w, h);
        cp->SetPosition(px, py);

        contentW_ = w;
        contentH_ = h;
    }

    void Drawer::SetSide(DrawerSide side) {
        if (side_ == side) return;
        side_ = side;

        Invalidate();
        RequestWindowRedraw();
    }

    void Drawer::SetPanelSize(float size) {
        panelSize_ = (size > 0.0f) ? size : kFallbackPanelSize;

        Invalidate();
        RequestWindowRedraw();
    }

    void Drawer::SetTitle(std::string title) {
        title_ = std::move(title);
        syncBodyPadding();
        Invalidate();
        requestRepaint();
        RequestWindowRedraw();
    }

    void Drawer::SetTitleHeight(float h) {
        titleHeight_ = (h > 0.0f) ? h : 0.0f;
        syncBodyPadding();
        Invalidate();
        requestRepaint();
        RequestWindowRedraw();
    }

    void Drawer::SetClosable(bool closable) {
        closable_ = closable;
        if (!closable_) { hoverClose_ = false; pressedClose_ = false; }
        requestRepaint();
        RequestWindowRedraw();
    }

    void Drawer::SetScrimVisible(bool visible) {
        scrimVisible_ = visible;
        requestRepaint();
        RequestWindowRedraw();
    }

    void Drawer::SetScrimColor(const Color4f& c) {
        scrimColor_ = c;
        markStyled();
        requestRepaint();
        RequestWindowRedraw();
    }

    void Drawer::panelRect(float& px, float& py, float& pw, float& ph) const {
        const float vx = x();
        const float vy = y();
        const float vw = width();
        const float vh = height();

        const float size = (panelSize_ > 0.0f) ? panelSize_ : kFallbackPanelSize;

        switch (side_) {
        case DrawerSide::Left:
            px = vx;                py = vy;
            pw = std::min(size, vw); ph = vh;
            break;
        case DrawerSide::Right:
            pw = std::min(size, vw); ph = vh;
            px = vx + vw - pw;      py = vy;
            break;
        case DrawerSide::Top:
            px = vx;                py = vy;
            pw = vw;                ph = std::min(size, vh);
            break;
        case DrawerSide::Bottom:
            pw = vw;                ph = std::min(size, vh);
            px = vx;                py = vy + vh - ph;
            break;
        }
    }

    bool Drawer::closeButtonRect(float& bx, float& by, float& bw, float& bh) const {
        if (!closable_ || title_.empty()) return false;

        float px = 0.0f, py = 0.0f, pw = 0.0f, ph = 0.0f;
        panelRect(px, py, pw, ph);
        if (pw <= 0.0f || ph <= 0.0f) return false;
        if (titleHeight_ < kCloseSize) return false;

        bw = kCloseSize;
        bh = kCloseSize;
        bx = px + pw - kCloseMargin - bw;
        by = py + (titleHeight_ - bh) * 0.5f;
        return true;
    }

    bool Drawer::HitTest(float px, float py) const {
        if (!open_) return false;

        if (Widget::HitTest(px, py)) return true;

        float x0 = 0.0f, y0 = 0.0f, w0 = 0.0f, h0 = 0.0f;
        panelRect(x0, y0, w0, h0);
        return drawutil::HitRect(px, py, x0, y0, w0, h0);
    }

    bool Drawer::clipChildrenRect(float& cx, float& cy, float& cw, float& ch) const {
        (void)cx; (void)cy; (void)cw; (void)ch;
        return false;
    }

    void Drawer::Paint(FrameCollector& fc) const {
        if (!open_) return;

        const float vw = width();
        const float vh = height();
        if (vw <= 0.0f || vh <= 0.0f) return;

        syncOverlay();
        if (nativePopup()) {
            if (scrimVisible_ && scrimColor_.a > 0.0f) {
                fc.ResetClip();
                drawutil::PaintRoundedRect(fc, x(), y(), vw, vh, 0.0f, scrimColor_);
            }
            return;
        }

        fc.ResetClip();

        if (scrimVisible_ && scrimColor_.a > 0.0f)
            drawutil::PaintRoundedRect(fc, x(), y(), vw, vh, 0.0f, scrimColor_);

        paintChrome(fc, false);
    }

    void Drawer::paintChrome(FrameCollector& fc, bool local) const {
        const float vw = width();
        const float vh = height();
        if (vw <= 0.0f || vh <= 0.0f) return;

        float px = 0.0f, py = 0.0f, pw = 0.0f, ph = 0.0f;
        panelRect(px, py, pw, ph);
        if (pw <= 0.0f || ph <= 0.0f) return;

        const float dx = local ? px : 0.0f;
        const float dy = local ? py : 0.0f;
        px -= dx;  py -= dy;

        const Theme& t = fc.GetTheme();
        const SurfaceStyle& ss = t.drawer;

        if (ss.background.a > 0.0f || ss.borderWidth > 0.0f) {
            const float offX = (side_ == DrawerSide::Left) ? shadowOffset_
                : (side_ == DrawerSide::Right) ? -shadowOffset_ : 0.0f;
            const float offY = (side_ == DrawerSide::Top) ? shadowOffset_
                : (side_ == DrawerSide::Bottom) ? -shadowOffset_ : 0.0f;
            drawutil::PaintShadow(fc, px, py, pw, ph, 0.0f,
                shadowColor_, offX, offY, shadowSpread_);
        }

        drawutil::PaintBox(fc, px, py, pw, ph, 0.0f,
            ss.borderWidth, ss.borderColor, ss.background);

        if (!title_.empty() && titleHeight_ > 0.0f) {
            const TextStyle& ts = ss.title;
            const TextMetrics tm = fc.MeasureText(title_, ts.font);
            if (tm.width > 0.0f) {
                const float tx = px + kPanelPad;
                const float avail = std::max(0.0f, pw - kPanelPad * 2.0f
                    - (closable_ ? (kCloseSize + kCloseMargin) : 0.0f));
                std::string shown = title_;
                TextMetrics tmShown = tm;
                if (tm.width > avail && avail > 0.0f) {
                    shown = drawutil::Ellipsize(fc, title_, ts.font, avail);
                    tmShown = fc.MeasureText(shown, ts.font);
                }
                if (tmShown.width > 0.0f) {
                    const float ty = py + (titleHeight_ - tmShown.height) * 0.5f;
                    fc.AddText(shown, ts.font, tx, ty + tmShown.ascent,
                        ts.colors.get(WidgetState::Normal).text);
                }
            }

            if (ss.dividerColor.a > 0.0f) {
                const float dw = (ss.dividerWidth > 0.0f) ? ss.dividerWidth : 1.0f;
                drawutil::PaintHLine(fc, px, py + titleHeight_ - dw, pw, dw, ss.dividerColor);
            }
        }

        float bx = 0.0f, by = 0.0f, bw = 0.0f, bh = 0.0f;
        if (closeButtonRect(bx, by, bw, bh)) {
            bx -= dx;  by -= dy;
            if (hoverClose_ || pressedClose_) {
                Color4f hb{ 0.0f, 0.0f, 0.0f, 0.0f };
                if (pressedClose_)     hb = t.itemStrip.itemHoverColor;
                else if (hoverClose_)  hb = t.itemStrip.itemHoverColor;
                if (hb.a > 0.0f)
                    drawutil::PaintRoundedRect(fc, bx, by, bw, bh, bw * 0.25f, hb);
            }

            Color4f cc = t.drawer.title.colors.get(WidgetState::Normal).text;
            if (hoverClose_) cc = t.itemStrip.itemActive.colors.get(WidgetState::Normal).text;
            if (cc.a <= 0.0f) cc = t.text;

            const float cx = bx + bw * 0.5f;
            const float cy = by + bh * 0.5f;
            const auto stroke = [&](float dx, float dy) {
                const float len = std::sqrt(dx * dx + dy * dy);
                if (len <= 1e-5f) return;
                const float nx = -dy / len * kCrossThickness * 0.5f;
                const float ny = dx / len * kCrossThickness * 0.5f;
                const float ax = cx - dx, ay = cy - dy;
                const float bx2 = cx + dx, by2 = cy + dy;
                const std::vector<Vec2> q{
                    Vec2{ ax + nx, ay + ny },
                    Vec2{ bx2 + nx, by2 + ny },
                    Vec2{ bx2 - nx, by2 - ny },
                    Vec2{ ax - nx, ay - ny },
                };
                fc.AddShape(PolygonShape(q, true), cc, 0.f, 0.f, 0.f, 0.f, 0.f);
            };
            stroke(kCrossSize, kCrossSize);
            stroke(kCrossSize, -kCrossSize);
        }
    }

    bool Drawer::onPopupPress(MouseEvent& e) {
        if (!open_ || e.button != MouseButton::Left) return false;

        float bx = 0.0f, by = 0.0f, bw = 0.0f, bh = 0.0f;
        if (closeButtonRect(bx, by, bw, bh)
            && drawutil::HitRect(e.x, e.y, bx, by, bw, bh)) {
            pressedClose_ = true;
            e.accept();
            return true;
        }
        e.accept();
        return true;
    }

    bool Drawer::onPopupRelease(MouseEvent& e) {
        if (!open_ || e.button != MouseButton::Left) return false;
        if (!pressedClose_) { e.accept(); return true; }

        pressedClose_ = false;
        float bx = 0.0f, by = 0.0f, bw = 0.0f, bh = 0.0f;
        const bool stillOn = closeButtonRect(bx, by, bw, bh)
            && drawutil::HitRect(e.x, e.y, bx, by, bw, bh);
        if (stillOn) requestCloseByUser();
        requestRepaint();
        e.accept();
        return true;
    }

    bool Drawer::onPopupMove(MouseEvent& e) {
        if (!open_) return false;

        float bx = 0.0f, by = 0.0f, bw = 0.0f, bh = 0.0f;
        const bool over = closeButtonRect(bx, by, bw, bh)
            && drawutil::HitRect(e.x, e.y, bx, by, bw, bh);
        if (over != hoverClose_) {
            hoverClose_ = over;
            requestRepaint();
        }
        e.accept();
        return true;
    }

    void Drawer::onPopupLeave() {
        if (!hoverClose_) return;
        hoverClose_ = false;
        requestRepaint();
    }

    bool Drawer::OnMousePress(MouseEvent& e) {
        if (!open_) return false;
        if (e.button != MouseButton::Left) return false;

        float bx = 0.0f, by = 0.0f, bw = 0.0f, bh = 0.0f;
        if (closeButtonRect(bx, by, bw, bh)
            && drawutil::HitRect(e.x, e.y, bx, by, bw, bh)) {
            pressedClose_ = true;
            e.accept();
            return true;
        }

        float px = 0.0f, py = 0.0f, pw = 0.0f, ph = 0.0f;
        panelRect(px, py, pw, ph);
        const bool insidePanel = drawutil::HitRect(e.x, e.y, px, py, pw, ph);

        if (!insidePanel && scrimVisible_ && closeOnScrimClick_) {
            requestCloseByUser();
            e.accept();
            return true;
        }

        e.accept();
        return true;
    }

    bool Drawer::OnMouseRelease(MouseEvent& e) {
        if (!open_) return false;
        if (e.button != MouseButton::Left) return false;

        if (pressedClose_) {
            pressedClose_ = false;

            float bx = 0.0f, by = 0.0f, bw = 0.0f, bh = 0.0f;
            const bool stillOn = closeButtonRect(bx, by, bw, bh)
                && drawutil::HitRect(e.x, e.y, bx, by, bw, bh);
            if (stillOn) requestCloseByUser();

            requestRepaint();
            e.accept();
            return true;
        }

        e.accept();
        return true;
    }

    bool Drawer::OnMouseMove(MouseEvent& e) {
        if (!open_) return false;

        float bx = 0.0f, by = 0.0f, bw = 0.0f, bh = 0.0f;
        const bool over = closeButtonRect(bx, by, bw, bh)
            && drawutil::HitRect(e.x, e.y, bx, by, bw, bh);

        if (over != hoverClose_) {
            hoverClose_ = over;
            requestRepaint();
            return true;
        }
        e.accept();
        return true;
    }

    void Drawer::OnMouseLeave() {
        if (!hoverClose_) return;
        hoverClose_ = false;
        requestRepaint();
    }

    bool Drawer::OnKeyPress(KeyEvent& e) {
        if (!open_) return false;

        if (e.key == Key::Escape && closeOnEscape_) {
            requestCloseByUser();
            e.accept();
            return true;
        }
        return false;
    }

}
