
#include "pch.h"

#include <algorithm>
#include <string>
#include <utility>

#include "ZeSubWindow.h"
#include "include/Zekit.h"

#include "ZeFrameCollector.h"
#include "ZeDrawUtil.h"
#include "ZeRoundedRectShape.h"

namespace Kit {

    namespace {
        constexpr float kShadowPad = 16.0f;
        constexpr float kTitlePadX = 10.0f;
        constexpr float kCloseMargin = 4.0f;
    }

    SubWindow::SubWindow(SubWindowDesc desc)
        : title_(std::move(desc.title)),
          closable_(desc.closable),
          movable_(desc.movable) {

        SetDirection(FlexDirection::Column);
        SetAlign(AlignItems::Stretch);

        SetPadding(0.0f, titleBarHeight_, 0.0f, 0.0f);

        SetSize(desc.width, desc.height);
        SetPosition(desc.x, desc.y);

        SetFocusable(true);
        ApplyTheme(Themes::Light());
    }

    void SubWindow::SetTitle(std::string title) {
        title_ = std::move(title);
        requestRepaint();
        RequestWindowRedraw();
    }

    void SubWindow::SetWindowPosition(float x, float y) {
        SetPosition(x, y);
        requestRepaint();
        RequestWindowRedraw();
    }

    void SubWindow::MoveBy(float dx, float dy) {
        SetPosition(x() + dx, y() + dy);
        requestRepaint();
    }

    void SubWindow::SetWindowSize(float w, float h) {
        if (w <= 0.0f || h <= 0.0f) return;
        SetSize(w, h);
        Invalidate();
        requestRepaint();
        RequestWindowRedraw();
    }

    void SubWindow::Open() {
        if (isVisible()) return;
        SetVisible(true);
        requestFocus();
        requestRepaint();
    }

    void SubWindow::Close() {
        if (!isVisible()) return;
        dragging_ = false;
        hoverClose_ = false;
        SetVisible(false);

        clearFocus();

        requestRepaint();
        if (onClosed_) onClosed_(*this);
    }

    void SubWindow::SetClosable(bool on) {
        closable_ = on;
        hoverClose_ = false;
        requestRepaint();
        RequestWindowRedraw();
    }

    void SubWindow::SetTitleBarHeight(float h) {
        if (h <= 0.0f) return;
        titleBarHeight_ = h;
        SetPadding(0.0f, titleBarHeight_, 0.0f, 0.0f);
        Invalidate();
        requestRepaint();
        RequestWindowRedraw();
    }

    void SubWindow::SetTitleBarBackground(const Color4f& c) { titleBarBg_ = c; requestRepaint();   RequestWindowRedraw();}
    void SubWindow::SetTitleColor(const Color4f& c) { titleColor_ = c; requestRepaint();   RequestWindowRedraw();}
    void SubWindow::SetCloseButtonColor(const Color4f& c) { closeColor_ = c; requestRepaint();   RequestWindowRedraw();}

    void SubWindow::SetContentPadding(float all) {
        const float p = (all > 0.0f) ? all : 0.0f;
        SetPadding(p, titleBarHeight_ + p, p, p);
        Invalidate();
        requestRepaint();
        RequestWindowRedraw();
    }

    void SubWindow::ApplyTheme(const Theme& t) {
        titleBarHeight_ = (t.subwindow.titleBarHeight > 0.0f) ? t.subwindow.titleBarHeight : 30.0f;
        radius_ = (t.subwindow.radius >= 0.0f) ? t.subwindow.radius : 0.0f;
        titleBarBg_ = t.subwindow.titleBarBackground;
        background_ = t.subwindow.background;
        borderColor_ = t.subwindow.borderColor;
        shadowColor_ = t.subwindow.shadowColor;
        shadowOffsetY_ = t.subwindow.shadowOffsetY;
        shadowSpread_ = t.subwindow.shadowSpread;
        closeColor_ = t.subwindow.closeColor;
        closeHoverColor_ = t.subwindow.closeHoverColor;
        titleTextStyle_ = t.subwindow.title;
        titleColor_ = t.subwindow.title.colors.get(WidgetState::Normal).text;

        SetPadding(0.0f, titleBarHeight_, 0.0f, 0.0f);
        Invalidate();
    }

    bool SubWindow::paintsOnTop() const {
        return isVisible();
    }

    bool SubWindow::HitTest(float px, float py) const {
        if (!isVisible()) return false;
        return px >= x() && px < x() + width()
            && py >= y() && py < y() + height();
    }

    SubWindow::Rect SubWindow::titleBarRect() const {
        return Rect{ x(), y(), width(), titleBarHeight_ };
    }

    SubWindow::Rect SubWindow::closeButtonRect() const {
        if (!closable_) return Rect{ 0.0f, 0.0f, 0.0f, 0.0f };
        const float s = closeButtonSize();
        return Rect{ x() + width() - s - kCloseMargin,
                     y() + (titleBarHeight_ - s) * 0.5f, s, s };
    }

    bool SubWindow::OnMousePress(MouseEvent& e) {
        if (!isVisible()) return false;
        if (e.button != MouseButton::Left) return false;

        if (closable_) {
            const Rect cb = closeButtonRect();
            if (e.x >= cb.x && e.x < cb.x + cb.w && e.y >= cb.y && e.y < cb.y + cb.h) {
                Close();
                e.accept();
                return true;
            }
        }

        if (movable_) {
            const Rect tb = titleBarRect();
            if (e.x >= tb.x && e.x < tb.x + tb.w && e.y >= tb.y && e.y < tb.y + tb.h) {
                dragging_ = true;
                dragOffsetX_ = e.x - x();
                dragOffsetY_ = e.y - y();
                e.accept();
                return true;
            }
        }

        return false;
    }

    bool SubWindow::OnMouseMove(MouseEvent& e) {
        if (!isVisible()) return false;

        if (dragging_) {
            SetWindowPosition(e.x - dragOffsetX_, e.y - dragOffsetY_);
            e.accept();
            return true;
        }

        bool overClose = false;
        if (closable_) {
            const Rect cb = closeButtonRect();
            overClose = (e.x >= cb.x && e.x < cb.x + cb.w
                      && e.y >= cb.y && e.y < cb.y + cb.h);
        }
        if (overClose != hoverClose_) {
            hoverClose_ = overClose;
            requestRepaint();
            return true;
        }
        return false;
    }

    bool SubWindow::OnMouseRelease(MouseEvent& e) {
        if (!dragging_) return false;
        dragging_ = false;
        e.accept();
        return true;
    }

    void SubWindow::OnMouseLeave() {
        if (!hoverClose_) return;
        hoverClose_ = false;
        requestRepaint();
    }

    void SubWindow::Paint(FrameCollector& fc) const {
        if (!paintsOnTop()) return;

        const float w = width();
        const float h = height();
        if (w <= 0.0f || h <= 0.0f) return;

        fc.ResetClip();

        const float r = std::min(radius_, std::min(w, h) * 0.5f);
        const float tbH = std::min(titleBarHeight_, h);

        if (shadowColor_.a > 0.0f) {
            fc.PushClip(x() - kShadowPad, y() - kShadowPad,
                        w + 2.0f * kShadowPad, h + 2.0f * kShadowPad);
            drawutil::PaintRoundedRect(fc, x() - shadowSpread_, y() + shadowOffsetY_ - shadowSpread_,
                w + 2.0f * shadowSpread_, h + 2.0f * shadowSpread_, r, shadowColor_);
            fc.PopClip();
        }

        fc.PushClip(x() - 1.0f, y() - 1.0f, w + 2.0f, h + 2.0f);
        if (borderColor_.a > 0.0f) {
            drawutil::PaintRoundedRect(fc, x(), y(), w, h, r, borderColor_);
            drawutil::PaintRoundedRect(fc, x() + 1.0f, y() + 1.0f, w - 2.0f, h - 2.0f,
                std::max(0.0f, r - 1.0f), background_);
        }
        else {
            drawutil::PaintRoundedRect(fc, x(), y(), w, h, r, background_);
        }

        if (tbH > 0.0f && titleBarBg_.a > 0.0f) {
            drawutil::PaintRoundedRect(fc, x(), y(), w, tbH, r, titleBarBg_);
            if (tbH > r)
                drawutil::PaintRoundedRect(fc, x(), y() + r, w, tbH - r, 0.0f, titleBarBg_);
        }
        fc.PopClip();

        if (!title_.empty() && tbH > 0.0f) {
            const float cbW = closable_ ? (closeButtonSize() + kCloseMargin * 2.0f) : 0.0f;
            const float avail = std::max(0.0f, w - kTitlePadX - cbW);
            if (avail > 0.0f) {
                TextMetrics tm = fc.MeasureText(title_, titleTextStyle_.font);
                std::string shown = title_;
                if (tm.width > avail) {
                    shown = drawutil::EllipsizeToWidth(fc, title_, titleTextStyle_.font, avail);
                    tm = fc.MeasureText(shown, titleTextStyle_.font);
                }
                if (tm.width > 0.0f) {
                    const float ty = y() + (tbH - tm.height) * 0.5f;
                    fc.AddText(shown, titleTextStyle_.font, x() + kTitlePadX,
                        ty + tm.ascent, titleColor_);
                }
            }
        }

        if (closable_ && tbH > 0.0f) {
            const Rect cb = closeButtonRect();

            if (hoverClose_) {
                const float br = std::min(4.0f, cb.w * 0.3f);
                drawutil::PaintRoundedRect(fc, cb.x, cb.y, cb.w, cb.h, br,
                    Color4f{ closeHoverColor_.r, closeHoverColor_.g, closeHoverColor_.b, 0.14f });
            }

            static const std::string kCross = "\xC3\x97";
            const TextMetrics tm = fc.MeasureText(kCross, titleTextStyle_.font);
            if (tm.width > 0.0f) {
                const float cx = cb.x + (cb.w - tm.width) * 0.5f;
                const float cy = cb.y + (cb.h - tm.height) * 0.5f;
                fc.AddText(kCross, titleTextStyle_.font, cx, cy + tm.ascent,
                    hoverClose_ ? closeHoverColor_ : closeColor_);
            }
        }
    }

    void SubWindow::requestRepaint() {
        RequestWindowRedraw();
    }
}
