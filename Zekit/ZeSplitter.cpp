
#include "pch.h"

#include <algorithm>
#include <cmath>

#include "include/ZeSplitter.h"
#include "ZeDrawUtil.h"
#include "include/Zekit.h"

#include "ZeFrameCollector.h"
#include "ZeRoundedRectShape.h"

namespace Kit {

    namespace {
        constexpr float kHitSlop = 2.0f;
    }

    Splitter::Splitter(bool horizontal) : horizontal_(horizontal) {
        SetDirection(horizontal ? FlexDirection::Row : FlexDirection::Column);

        SetGap(handleW_, handleW_);
        SetPadding(0.0f, 0.0f);

        ApplyTheme(Themes::Light());
    }

    void Splitter::SetHorizontal(bool on) {
        if (horizontal_ == on) return;
        horizontal_ = on;
        SetDirection(on ? FlexDirection::Row : FlexDirection::Column);
        lastAvail_ = -1.0f;
        Invalidate();
        RequestWindowRedraw();
    }

    void Splitter::SetHandleWidth(float px) {
        handleW_ = (px > 0.0f) ? px : 0.0f;
        SetGap(handleW_, handleW_);
        lastAvail_ = -1.0f;
        Invalidate();
        RequestWindowRedraw();
    }

    void Splitter::SetHandleColor(const Color4f& c) {
        handleColor_ = c;
        markStyled();
        RequestWindowRedraw();
    }

    void Splitter::SetHandleActiveColor(const Color4f& c) {
        handleActiveColor_ = c;
        markStyled();
        RequestWindowRedraw();
    }

    void Splitter::SetHandleRadius(float r) {
        handleRadius_ = (r > 0.0f) ? r : 0.0f;
        markStyled();
        RequestWindowRedraw();
    }

    void Splitter::SetMinFirst(float px) { minFirst_ = (px > 0.0f) ? px : 0.0f;   RequestWindowRedraw();}
    void Splitter::SetMinSecond(float px) { minSecond_ = (px > 0.0f) ? px : 0.0f;   RequestWindowRedraw();}

    void Splitter::ApplyTheme(const Theme& t) {
        handleColor_ = t.splitter.handleColor;
        handleActiveColor_ = t.splitter.handleActiveColor;
        handleRadius_ = (t.splitter.radius > 0.0f) ? t.splitter.radius : 0.0f;
    }

    void Splitter::SetFirstSize(float px) {
        applyFirstSize(px);
        RequestWindowRedraw();
    }

    void Splitter::SetFirstRatio(float r) {
        if (!first_) return;
        const float axis = horizontal_ ? width() : height();
        const float avail = std::max(0.0f, axis - handleW_);
        if (avail <= 0.0f) {
            firstRatio_ = std::clamp(r, 0.0f, 1.0f);
            firstSize_ = 0.0f;
            return;
        }
        applyFirstSize(std::clamp(r, 0.0f, 1.0f) * avail);
        RequestWindowRedraw();
    }

    void Splitter::applyFirstSize(float px, bool recordRatio) {
        if (!first_) return;

        const float axis = horizontal_ ? width() : height();
        const float avail = std::max(0.0f, axis - handleW_);

        float v = std::max(px, 0.0f);
        if (avail > 0.0f) {
            const float hi = std::max(minFirst_, avail - minSecond_);
            v = std::clamp(v, minFirst_, hi);
        }

        if (horizontal_) first_->SetWidth(v);
        else             first_->SetHeight(v);

        const float accepted = horizontal_ ? first_->width() : first_->height();
        firstSize_ = accepted;

        if (recordRatio && avail > 0.0f)
            firstRatio_ = accepted / avail;

    }

    void Splitter::DoLayout() {
        if (!secondVisible()) {
            collapsed_ = true;
            if (first_) {
                const float axis = horizontal_ ? width() : height();
                if (axis > 0.0f) {
                    if (horizontal_) first_->SetWidth(axis);
                    else             first_->SetHeight(axis);
                }
            }
            Panel::DoLayout();
            return;
        }

        const float axis = horizontal_ ? width() : height();
        const float avail = std::max(0.0f, axis - handleW_);

        if (first_ && avail > 0.0f) {
            const float curPref = horizontal_ ? first_->prefWidth() : first_->prefHeight();

            if (collapsed_) {
                collapsed_ = false;
                applyFirstSize((firstRatio_ > 0.0f ? firstRatio_ : 0.4f) * avail, false);
            }
            else if (curPref > 0.0f && std::fabs(curPref - firstSize_) > 0.5f) {
                applyFirstSize(curPref, true);
            }
            else if (std::fabs(avail - lastAvail_) > 0.5f) {
                if (firstRatio_ > 0.0f) {
                    applyFirstSize(firstRatio_ * avail, false);
                }
                else {
                    applyFirstSize(firstSize_ > 0.0f ? firstSize_ : 0.4f * avail, true);
                }
            }
        }

        Panel::DoLayout();

        lastAvail_ = avail;
    }

    bool Splitter::handleRect(float& rx, float& ry, float& rw, float& rh) const {
        if (!first_ || handleW_ <= 0.0f) return false;
        if (!secondVisible()) return false;

        if (horizontal_) {
            rx = first_->x() + first_->width();
            ry = y();
            rw = handleW_;
            rh = height();
        }
        else {
            rx = x();
            ry = first_->y() + first_->height();
            rw = width();
            rh = handleW_;
        }
        return rw > 0.0f && rh > 0.0f;
    }

    bool Splitter::hitHandle(float px, float py) const {
        float rx, ry, rw, rh;
        if (!handleRect(rx, ry, rw, rh)) return false;
        return px >= rx - kHitSlop && px < rx + rw + kHitSlop
            && py >= ry - kHitSlop && py < ry + rh + kHitSlop;
    }

    void Splitter::Paint(FrameCollector& fc) const {
        Panel::Paint(fc);

        float rx, ry, rw, rh;
        if (!handleRect(rx, ry, rw, rh)) return;

        const bool active = dragging_ || hoverHandle_;

        if (active && handleActiveColor_.a > 0.0f && handleThickness_ > 0.0f) {
            const float t = std::min(handleThickness_, std::min(rw, rh));
            const float along = horizontal_ ? rh : rw;
            const float inset = std::min(handleInset_, (along - t) * 0.5f);
            const float span = std::max(t, along - inset * 2.0f);
            const float r = std::min(handleRadius_ > 0.0f ? handleRadius_ : t * 0.5f, t * 0.5f);
            if (horizontal_) {
                const float x = rx + (rw - t) * 0.5f;
                drawutil::PaintRoundedRect(fc, x, ry + (rh - span) * 0.5f, t, span, r, handleActiveColor_);
            }
            else {
                const float y = ry + (rh - t) * 0.5f;
                drawutil::PaintRoundedRect(fc, rx + (rw - span) * 0.5f, y, span, t, r, handleActiveColor_);
            }
            return;
        }

        if (handleColor_.a <= 0.0f) return;
        if (horizontal_) {
            const float x = rx + (rw - 1.0f) * 0.5f;
            drawutil::PaintRoundedRect(fc, x, ry, 1.0f, rh, 0.0f, handleColor_);
        }
        else {
            const float y = ry + (rh - 1.0f) * 0.5f;
            drawutil::PaintRoundedRect(fc, rx, y, rw, 1.0f, 0.0f, handleColor_);
        }
    }

    void Splitter::SetHandleThickness(float px) {
        handleThickness_ = std::max(0.0f, px);
        RequestWindowRedraw();
    }

    void Splitter::SetHandleInset(float px) {
        handleInset_ = std::max(0.0f, px);
        RequestWindowRedraw();
    }

    bool Splitter::OnMousePress(MouseEvent& e) {
        if (e.button != MouseButton::Left) return false;
        if (!hitHandle(e.x, e.y)) return false;

        dragging_ = true;
        dragStartPos_ = horizontal_ ? e.x : e.y;
        dragStartSize_ = first_ ? (horizontal_ ? first_->width() : first_->height())
                                : firstSize_;
        RequestWindowRedraw();

        e.accept();
        return true;
    }

    bool Splitter::OnMouseMove(MouseEvent& e) {
        if (dragging_) {
            const float cur = horizontal_ ? e.x : e.y;
            applyFirstSize(dragStartSize_ + (cur - dragStartPos_));
            e.accept();
            return true;
        }

        const bool over = hitHandle(e.x, e.y);
        if (over != hoverHandle_) {
            hoverHandle_ = over;
            RequestWindowRedraw();
            return true;
        }
        return false;
    }

    bool Splitter::OnMouseRelease(MouseEvent& e) {
        if (!dragging_) return false;

        dragging_ = false;
        hoverHandle_ = hitHandle(e.x, e.y);
        RequestWindowRedraw();
        e.accept();
        return true;
    }

    void Splitter::OnMouseLeave() {
        if (hoverHandle_) {
            hoverHandle_ = false;
            RequestWindowRedraw();
        }
    }

    Cursor Splitter::cursorAt(float x, float y) const {
        if (!secondVisible()) return Cursor::Arrow;

        if (dragging_ || hitHandle(x, y))
            return horizontal_ ? Cursor::SizeWE : Cursor::SizeNS;

        return Cursor::Arrow;
    }
}
