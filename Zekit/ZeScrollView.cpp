#include "pch.h"
#include "include/ZeScrollView.h"
#include "include/Zekit.h"
#include "include/ZeTheme.h"

#include "ZeFrameCollector.h"

#include <algorithm>
#include <cmath>

namespace Kit {

    namespace {
        float ClampScroll(float value, float upper) {
            return std::min(std::max(value, 0.0f), std::max(upper, 0.0f));
        }

        constexpr float kWheelStep = 40.0f;

        constexpr float kBarThickness = 8.0f;

        constexpr float kMinThumb = 24.0f;

        constexpr float kPageOverlap = 0.9f;

        bool InRect(float rx, float ry, float rw, float rh, float px, float py) {
            return px >= rx && px < rx + rw && py >= ry && py < ry + rh;
        }
    }

    ScrollView::ScrollView() {
        style_.direction = FlexDirection::Column;
        syncSizePolicy();
    }

    void ScrollView::SetScrollDirection(ScrollDirection dir) {
        if (scrollDir_ == dir) return;
        scrollDir_ = dir;
        syncSizePolicy();
        notifyLayoutInputChanged();
        Invalidate();
        RequestWindowRedraw();
    }

    void ScrollView::syncSizePolicy() {
        switch (scrollDir_) {
        case ScrollDirection::Vertical:
            SetSizePolicy(SizePolicy::Preferred, SizePolicy::Expanding);
            break;
        case ScrollDirection::Horizontal:
            SetSizePolicy(SizePolicy::Expanding, SizePolicy::Preferred);
            break;
        case ScrollDirection::Both:
            SetSizePolicy(SizePolicy::Expanding, SizePolicy::Expanding);
            break;
        }
    }

    void ScrollView::SetScrollX(float x) {
        const float v = (x > 0.0f) ? x : 0.0f;
        if (scrollX_ == v) return;
        scrollX_ = v;
        anchorValid_ = false;
        Invalidate();
        notifyScrolled(scrollX_, scrollY_);
        RequestWindowRedraw();
    }

    void ScrollView::SetScrollY(float y) {
        const float v = (y > 0.0f) ? y : 0.0f;
        if (scrollY_ == v) return;
        scrollY_ = v;
        anchorValid_ = false;
        Invalidate();
        notifyScrolled(scrollX_, scrollY_);
        RequestWindowRedraw();
    }

    void ScrollView::notifyScrolled(float x, float y) {
        if (onScroll_)     onScroll_(x, y);
        if (selfOnScroll_) selfOnScroll_(*this, x, y);
    }

    bool ScrollView::OnMouseWheel(MouseEvent& e) {
        const float delta = static_cast<float>(e.wheelDelta) / 120.0f * kWheelStep;

        bool horizontal = false;
        switch (scrollDir_) {
        case ScrollDirection::Horizontal: horizontal = true;  break;
        case ScrollDirection::Vertical:   horizontal = false; break;
        case ScrollDirection::Both:
            if (hasModifier(e.modifiers, KeyboardModifiers::Shift))
                horizontal = true;
            else
                horizontal = (maxScrollY_ <= 0.0f && maxScrollX_ > 0.0f);
            break;
        }

        if (horizontal) {
            const float before = scrollX_;
            const float target = std::min(std::max(scrollX_ - delta, 0.0f), maxScrollX_);
            SetScrollX(target);
            if (scrollX_ == before) return false;
        }
        else {
            const float before = scrollY_;
            const float target = std::min(std::max(scrollY_ - delta, 0.0f), maxScrollY_);
            SetScrollY(target);
            if (scrollY_ == before) return false;
        }

        e.accept();
        return true;
    }

    float ScrollView::reservedRight() const {
        return (scrollDir_ == ScrollDirection::Vertical ||
                scrollDir_ == ScrollDirection::Both) ? kBarThickness : 0.0f;
    }

    float ScrollView::reservedBottom() const {
        return (scrollDir_ == ScrollDirection::Horizontal ||
                scrollDir_ == ScrollDirection::Both) ? kBarThickness : 0.0f;
    }

    bool ScrollView::clipChildrenRect(float& x, float& y, float& w, float& h) const {
        x = contentLeft();
        y = contentTop();
        w = contentAvailableW();
        h = contentAvailableH();
        return true;
    }

    void ScrollView::DoLayout() {
        Widget* anchorChild = anchorValid_ ? anchorChild_ : nullptr;
        const float anchorTY = anchorTY_;
        const float anchorTX = anchorTX_;

        const float prevContentW = contentW_;
        const float prevContentH = contentH_;
        const bool  hadLayout    = (prevContentW > 0.0f || prevContentH > 0.0f);

        setScrollOffset(0.0f, 0.0f);

        switch (scrollDir_) {
        case ScrollDirection::Vertical:   layoutLinear(true);   break;
        case ScrollDirection::Horizontal: layoutLinear(false);  break;
        case ScrollDirection::Both:       layoutFlow();         break;
        }

        const float availW = contentAvailableW();
        const float availH = contentAvailableH();
        switch (scrollDir_) {
        case ScrollDirection::Vertical:
            maxScrollY_ = std::max(contentH_ - availH, 0.0f);
            maxScrollX_ = 0.0f;
            break;
        case ScrollDirection::Horizontal:
            maxScrollX_ = std::max(contentW_ - availW, 0.0f);
            maxScrollY_ = 0.0f;
            break;
        case ScrollDirection::Both:
            maxScrollX_ = std::max(contentW_ - availW, 0.0f);
            maxScrollY_ = std::max(contentH_ - availH, 0.0f);
            break;
        }

        const bool reflowed = hadLayout &&
            (contentW_ != prevContentW || contentH_ != prevContentH);
        if (reflowed && anchorChild) {
            const auto& kids = children();
            if (std::find(kids.begin(), kids.end(), anchorChild) != kids.end()) {
                if (scrollDir_ != ScrollDirection::Horizontal) {
                    const float base = anchorChild->y() - contentTop();
                    scrollY_ = ClampScroll(base + anchorTY * anchorChild->height(), maxScrollY_);
                }
                if (scrollDir_ != ScrollDirection::Vertical) {
                    const float base = anchorChild->x() - contentLeft();
                    scrollX_ = ClampScroll(base + anchorTX * anchorChild->width(), maxScrollX_);
                }
            }
        }

        const float prevEffX = effScrollX_;
        const float prevEffY = effScrollY_;
        effScrollX_ = ClampScroll(scrollX_, maxScrollX_);
        effScrollY_ = ClampScroll(scrollY_, maxScrollY_);

        setScrollOffset(effScrollX_, effScrollY_);
        switch (scrollDir_) {
        case ScrollDirection::Vertical:   layoutLinear(true);   break;
        case ScrollDirection::Horizontal: layoutLinear(false);  break;
        case ScrollDirection::Both:       layoutFlow();         break;
        }

        if (effScrollX_ != prevEffX || effScrollY_ != prevEffY)
            notifyScrolled(effScrollX_, effScrollY_);

        captureAnchor();
    }

    void ScrollView::captureAnchor() {
        anchorValid_ = false;
        anchorChild_ = nullptr;

        const float vTop   = contentTop();
        const float vLeft  = contentLeft();
        const float availW = contentAvailableW();
        const float availH = contentAvailableH();
        const float vBot   = vTop  + availH;
        const float vRight = vLeft + availW;

        const bool testY = availH > 0.5f;
        const bool testX = availW > 0.5f;

        Widget* best = nullptr;
        float bestY = 1e30f, bestX = 1e30f;
        for (Widget* c : visibleChildren()) {
            if (testY) {
                if (c->y() + c->height() <= vTop + 0.5f) continue;
                if (c->y() >= vBot - 0.5f) continue;
            }
            if (testX) {
                if (c->x() + c->width() <= vLeft + 0.5f) continue;
                if (c->x() >= vRight - 0.5f) continue;
            }
            const float rankY = testY ? c->y() : 0.0f;
            const float rankX = testX ? c->x() : 0.0f;
            const bool higher  = rankY < bestY - 0.5f;
            const bool sameRow = rankY <= bestY + 0.5f;
            if (higher || (sameRow && rankX < bestX)) {
                bestY = rankY; bestX = rankX; best = c;
            }
        }
        if (!best) return;

        anchorChild_ = best;
        anchorTY_ = (best->height() > 0.0f)
            ? std::min(std::max((vTop  - best->y()) / best->height(), 0.0f), 1.0f) : 0.0f;
        anchorTX_ = (best->width() > 0.0f)
            ? std::min(std::max((vLeft - best->x()) / best->width(),  0.0f), 1.0f) : 0.0f;
        anchorValid_ = true;
    }

    bool ScrollView::heightDependsOnWidth() const {
        return scrollDir_ == ScrollDirection::Vertical
            || scrollDir_ == ScrollDirection::Both;
    }

    float ScrollView::hintHeightFor(float availWidth) const {
        if (!heightDependsOnWidth())
            return LayoutBase::hintHeightFor(availWidth);

        const float innerW = std::max(
            availWidth - padding_.left - padding_.right - reservedRight(), 0.0f);

        const float inner = padding_.top + padding_.bottom;
        const std::vector<Widget*> items = visibleChildren();
        if (items.empty()) return inner;

        float sum = 0.0f;
        for (Widget* c : items) {
            const Margin& m = c->margin();
            float h = 0.0f;

            if (c->prefHeight() > 0.0f) {
                h = c->prefHeight();
            }
            else if (c->heightDependsOnWidth()) {
                h = c->hintHeightFor(innerW - m.left - m.right);
            }
            else {
                h = c->hintHeight();
            }

            sum += h + m.top + m.bottom;
        }
        sum += style_.rowGap * static_cast<float>(items.size() - 1);

        return sum + inner;
    }

    ScrollView::BarGeometry ScrollView::verticalBarGeometry() const {
        BarGeometry b;
        if (scrollDir_ != ScrollDirection::Vertical && scrollDir_ != ScrollDirection::Both) return b;
        if (maxScrollY_ <= 0.0f) return b;

        b.visible = true;
        b.trackX = x() + width() - kBarThickness;
        b.trackY = y();
        b.trackW = kBarThickness;
        b.trackH = height();

        const float viewH = contentAvailableH();
        const float totalH = viewH + maxScrollY_;
        b.thumbW = kBarThickness;
        b.thumbH = std::min(b.trackH, std::max(b.trackH * (viewH / totalH), kMinThumb));

        b.maxScroll = maxScrollY_;
        b.scroll = std::min(std::max(scrollY_, 0.0f), maxScrollY_);
        const float t = b.scroll / maxScrollY_;

        b.thumbX = b.trackX;
        b.thumbY = b.trackY + (b.trackH - b.thumbH) * t;
        return b;
    }

    ScrollView::BarGeometry ScrollView::horizontalBarGeometry() const {
        BarGeometry b;
        if (scrollDir_ != ScrollDirection::Horizontal && scrollDir_ != ScrollDirection::Both) return b;
        if (maxScrollX_ <= 0.0f) return b;

        b.visible = true;
        b.trackX = x();
        b.trackY = y() + height() - kBarThickness;
        b.trackW = width();
        b.trackH = kBarThickness;

        const float viewW = contentAvailableW();
        const float totalW = viewW + maxScrollX_;
        b.thumbH = kBarThickness;
        b.thumbW = std::min(b.trackW, std::max(b.trackW * (viewW / totalW), kMinThumb));

        b.maxScroll = maxScrollX_;
        b.scroll = std::min(std::max(scrollX_, 0.0f), maxScrollX_);
        const float t = b.scroll / maxScrollX_;

        b.thumbX = b.trackX + (b.trackW - b.thumbW) * t;
        b.thumbY = b.trackY;
        return b;
    }

    int ScrollView::scrollBarAxisAt(float x, float y) const {
        const BarGeometry hb = horizontalBarGeometry();
        if (hb.visible && InRect(hb.trackX, hb.trackY, hb.trackW, hb.trackH, x, y))
            return 1;

        const BarGeometry vb = verticalBarGeometry();
        if (vb.visible && InRect(vb.trackX, vb.trackY, vb.trackW, vb.trackH, x, y))
            return 2;

        return 0;
    }

    void ScrollView::Paint(FrameCollector& fc) const {
        const Theme& th = fc.GetTheme();

        const BarGeometry vb = verticalBarGeometry();
        if (vb.visible) {
            fc.AddRect(vb.trackX, vb.trackY, vb.trackW, vb.trackH, th.scrollbarTrack);
            fc.AddRect(vb.thumbX, vb.thumbY, vb.thumbW, vb.thumbH, th.scrollbarThumb);
        }

        const BarGeometry hb = horizontalBarGeometry();
        if (hb.visible) {
            fc.AddRect(hb.trackX, hb.trackY, hb.trackW, hb.trackH, th.scrollbarTrack);
            fc.AddRect(hb.thumbX, hb.thumbY, hb.thumbW, hb.thumbH, th.scrollbarThumb);
        }
    }

    bool ScrollView::OnMousePress(MouseEvent& e) {
        if (e.button != MouseButton::Left) return false;
        if (!isEnabled()) return false;

        const int axis = scrollBarAxisAt(e.x, e.y);
        if (axis == 0) return false;

        const BarGeometry bar = (axis == 1) ? horizontalBarGeometry()
                                            : verticalBarGeometry();
        const bool onThumb = InRect(bar.thumbX, bar.thumbY, bar.thumbW, bar.thumbH, e.x, e.y);

        if (onThumb) {
            dragGrab_ = (axis == 1) ? (e.x - bar.thumbX) : (e.y - bar.thumbY);
        } else {
            const float cur        = bar.scroll;
            const float thumbStart = (axis == 1) ? bar.thumbX : bar.thumbY;
            const float thumbLen   = (axis == 1) ? bar.thumbW : bar.thumbH;
            const float click      = (axis == 1) ? e.x : e.y;
            const float viewLen    = (axis == 1) ? contentAvailableW() : contentAvailableH();
            const float page       = std::max(viewLen * kPageOverlap, kMinThumb);

            float target = (click < thumbStart) ? (cur - page) : (cur + page);
            target = std::min(std::max(target, 0.0f), bar.maxScroll);
            if (axis == 1) SetScrollX(target);
            else           SetScrollY(target);

            const float travel = (axis == 1) ? (bar.trackW - thumbLen) : (bar.trackH - thumbLen);
            if (travel > 0.0f && bar.maxScroll > 0.0f) {
                const float trackStart2 = (axis == 1) ? bar.trackX : bar.trackY;
                const float newThumbStart = trackStart2 + (target / bar.maxScroll) * travel;
                dragGrab_ = std::min(std::max(click - newThumbStart, 0.0f), thumbLen);
            }
            else {
                dragGrab_ = thumbLen * 0.5f;
            }
        }

        dragAxis_ = axis;
        e.accept();
        return true;
    }

    bool ScrollView::OnMouseMove(MouseEvent& e) {
        if (!isEnabled()) { dragAxis_ = 0; return false; }
        if (dragAxis_ == 0) return false;

        const BarGeometry bar = (dragAxis_ == 1) ? horizontalBarGeometry()
                                                : verticalBarGeometry();
        if (!bar.visible || bar.maxScroll <= 0.0f) {
            dragAxis_ = 0;
            return false;
        }

        const float trackStart = (dragAxis_ == 1) ? bar.trackX : bar.trackY;
        const float trackLen   = (dragAxis_ == 1) ? bar.trackW : bar.trackH;
        const float thumbLen   = (dragAxis_ == 1) ? bar.thumbW : bar.thumbH;
        const float cur        = (dragAxis_ == 1) ? e.x : e.y;

        const float travel = trackLen - thumbLen;
        if (travel <= 0.0f) return false;

        float t = (cur - trackStart - dragGrab_) / travel;
        t = std::min(std::max(t, 0.0f), 1.0f);
        const float target = t * bar.maxScroll;

        if (dragAxis_ == 1) SetScrollX(target);
        else                SetScrollY(target);

        e.accept();
        return true;
    }

    bool ScrollView::OnMouseRelease(MouseEvent& e) {
        if (dragAxis_ == 0) return false;
        if (e.button != MouseButton::Left) {
            dragAxis_ = 0;
            dragGrab_ = 0.0f;
            return false;
        }

        dragAxis_ = 0;
        dragGrab_ = 0.0f;
        e.accept();
        return true;
    }

    bool ScrollView::OnTouchBegin(TouchEvent& e) {
        if (scrollBarAxisAt(e.x, e.y) != 0) {
            touchActive_ = false;
            touchDragging_ = false;
            touchAxis_ = 0;
            return false;
        }

        touchActive_ = true;
        touchDragging_ = false;
        touchAxis_ = 0;
        touchStartX_ = e.x;
        touchStartY_ = e.y;
        touchStartScrollX_ = scrollX_;
        touchStartScrollY_ = scrollY_;

        return false;
    }

    bool ScrollView::OnTouchUpdate(TouchEvent& e) {
        if (!touchActive_) return false;

        const float dx = e.x - touchStartX_;
        const float dy = e.y - touchStartY_;

        if (!touchDragging_) {
            if (std::fabs(dx) < kTouchSlop && std::fabs(dy) < kTouchSlop)
                return false;

            const bool canH = maxScrollX() > 0.0f;
            const bool canV = maxScrollY() > 0.0f;
            if (canH && (!canV || std::fabs(dx) > std::fabs(dy))) touchAxis_ = 1;
            else if (canV)                                        touchAxis_ = 2;
            else                                                  return false;

            touchDragging_ = true;

            if (Window* w = window()) WindowAccess::CancelPointerCapture(*w);
        }

        const float delta = (touchAxis_ == 1) ? dx : dy;
        const float start = (touchAxis_ == 1) ? touchStartScrollX_ : touchStartScrollY_;
        const float maxS  = (touchAxis_ == 1) ? maxScrollX() : maxScrollY();

        float target = start - delta;
        target = std::min(std::max(target, 0.0f), maxS);

        if (touchAxis_ == 1) SetScrollX(target);
        else                 SetScrollY(target);

        e.accept();
        return true;
    }

    bool ScrollView::OnTouchEnd(TouchEvent& e) {
        const bool wasDragging = touchDragging_;
        touchActive_ = false;
        touchDragging_ = false;
        touchAxis_ = 0;

        if (!wasDragging) return false;

        e.accept();
        return true;
    }

    bool ScrollView::OnTouchCancel(TouchEvent& e) {
        const bool wasDragging = touchDragging_;
        touchActive_ = false;
        touchDragging_ = false;
        touchAxis_ = 0;
        if (wasDragging) e.accept();
        return wasDragging;
    }

}
