
#include "pch.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "include/ZeTimePicker.h"
#include "include/ZeOverlayWindow.h"
#include "include/ZeDateTime.h"
#include "include/Zekit.h"
#include "ZeAnchorVisible.h"
#include "include/ZeText.h"
#include "ZeDrawUtil.h"

namespace Kit {

    namespace {
        constexpr float kShadowPad = 18.0f;
        constexpr float kPopupMargin = 4.0f;
        constexpr float kArrowFallback = 10.0f;
        constexpr float kItemRadius = 4.0f;

        float BorderOf(const BoxStyle& b) {
            return std::max({ b.border.width[Border::Top], b.border.width[Border::Right],
                              b.border.width[Border::Bottom], b.border.width[Border::Left] });
        }

        void BoxInset(const BoxStyle& box, float& dx, float& dy) {
            const float bx = std::max(box.border.width[Border::Left], box.border.width[Border::Right]);
            const float by = std::max(box.border.width[Border::Top], box.border.width[Border::Bottom]);
            dx = box.padding.left + box.padding.right + bx * 2.0f;
            dy = box.padding.top + box.padding.bottom + by * 2.0f;
        }

        std::string Pad2(int v) {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "%02d", v);
            return std::string(buf);
        }
    }

    TimePicker::TimePicker() {
        ApplyTheme(Themes::Light());
        use24Hour_ = CurrentDateFormat().use24Hour;
        SetFocusable(true);
        syncFaceText();
        SetDefaultSize(110.0f, 34.0f);
    }

    void TimePicker::SetTime(int hour, int minute, int second) {
        hour_ = std::clamp(hour, 0, 23);
        minute_ = std::clamp(minute, 0, 59);
        second_ = std::clamp(second, 0, 59);
        syncFaceText();
        RequestWindowRedraw();
    }

    bool TimePicker::SetNow() {
        DateTime now;
        if (!NowLocal(now)) return false;
        SetTime(now.hour, now.minute, withSeconds_ ? now.second : 0);
        return true;
    }

    void TimePicker::SetWithSeconds(bool on) {
        if (withSeconds_ == on) return;
        withSeconds_ = on;
        if (!withSeconds_) second_ = 0;
        syncFaceText();
        notifyLayoutInputChanged();
        RequestWindowRedraw();
    }

    void TimePicker::SetUse24Hour(bool on) {
        if (use24Hour_ == on) return;
        use24Hour_ = on;
        syncFaceText();
        notifyLayoutInputChanged();
        RequestWindowRedraw();
    }

    void TimePicker::SetMinuteStep(int step) {
        minuteStep_ = (step > 0) ? std::min(step, 60) : 1;
        RequestWindowRedraw();
    }

    void TimePicker::SetSecondStep(int step) {
        secondStep_ = (step > 0) ? std::min(step, 60) : 1;
        RequestWindowRedraw();
    }

    void TimePicker::syncFaceText() {
        std::string s;
        if (use24Hour_) {
            s = Pad2(hour_) + ":" + Pad2(minute_);
            if (withSeconds_) s += ":" + Pad2(second_);
        }
        else {
            int h12 = hour_ % 12;
            if (h12 == 0) h12 = 12;
            s = Pad2(h12) + ":" + Pad2(minute_);
            if (withSeconds_) s += ":" + Pad2(second_);
            s += (hour_ < 12) ? (" " + std::string(CurrentDateFormat().am))
                          : (" " + std::string(CurrentDateFormat().pm));
        }
        TextWidget::SetText(s);
    }

    TimePicker::~TimePicker() = default;
    TimePicker::TimePicker(TimePicker&&) noexcept = default;

    bool TimePicker::SetOpen(bool open) {
        if (open_ == open) return false;

        open_ = open;
        if (open_) {
            hoverCol_ = hoverRow_ = -1;
            hoverFooter_ = false;
            activeCol_ = Col::Hour;

            Col cols[4];
            const int n = fillCols(cols);

            const int vis = (maxVisibleItems_ > 0) ? maxVisibleItems_ : 7;
            for (int i = 0; i < n; ++i) {
                const int sel = selectedRowOf(cols[i]);
                scroll_[i] = 0;
                if (sel >= vis) scroll_[i] = sel - vis + 1;
                clampScroll(i);
            }
            showPopupOverlay();
        }
        else {
            hidePopupOverlay();
        }
        requestRepaint();
        return true;
    }

    void TimePicker::Open() { SetOpen(true); }
    void TimePicker::Close() { SetOpen(false); }

    bool TimePicker::popupRect(float& x, float& y, float& w, float& h) const {
        const PopupGeom g = computePopup();
        if (g.w <= 0.0f || g.h <= 0.0f) return false;
        x = g.x; y = g.y; w = g.w; h = g.h;
        return true;
    }

    void TimePicker::dismissPopup() {
        Close();
    }

    void TimePicker::Toggle() { SetOpen(!open_); }

    void TimePicker::SetOnTimeChanged(SelfAction handler) {
        onChanged_ = std::move(handler);
    }

    void TimePicker::SetOnTimeChanged(SelfAction1<int> handler) {
        onChangedSecs_ = std::move(handler);
    }

    void TimePicker::notifyTimeChanged() {
        if (onChanged_) onChanged_(*this);
        if (onChangedSecs_) onChangedSecs_(*this, hour_ * 3600 + minute_ * 60 + second_);
    }

    void TimePicker::SetMaxVisibleItems(int n) {
        maxVisibleItems_ = n;
        Col cols[4];
        const int cn = fillCols(cols);
        for (int i = 0; i < cn; ++i) clampScroll(i);
        requestRepaint();
    }

    void TimePicker::SetItemHeight(float px) {
        itemHeightOverride_ = (px > 0.0f) ? px : 0.0f;
        requestRepaint();
        RequestWindowRedraw();
    }

    void TimePicker::SetArrowColor(const Color4f& c) {
        arrowColor_ = c;
        markStyled();
        requestRepaint();
        RequestWindowRedraw();
    }

    float TimePicker::resolvedItemHeight() const {
        if (itemHeightOverride_ > 0.0f) return itemHeightOverride_;
        return themeItemHeight_ > 0.0f ? themeItemHeight_ : 26.0f;
    }

    float TimePicker::resolvedColWidth() const {
        return themeColWidth_ > 0.0f ? themeColWidth_ : 46.0f;
    }

    float TimePicker::resolvedArrowSize() const {
        if (const Window* w = window()) {
            const TextMetrics tm =
                const_cast<Window*>(w)->MeasureText(std::string_view("Ag"), textStyle_.font);
            if (tm.height > 0.0f) return tm.height * 0.5f;
        }
        return kArrowFallback;
    }

    int TimePicker::itemCountOf(Col c) const {
        switch (c) {
        case Col::Hour:     return use24Hour_ ? 24 : 12;
        case Col::Minute:   return std::max(1, 60 / std::max(1, minuteStep_));
        case Col::Second:   return std::max(1, 60 / std::max(1, secondStep_));
        case Col::Meridiem: return 2;
        }
        return 0;
    }

    std::string TimePicker::itemTextOf(Col c, int row) const {
        switch (c) {
        case Col::Hour:
            if (use24Hour_) return Pad2(row);
            return std::to_string(row + 1);
        case Col::Minute:   return Pad2(row * std::max(1, minuteStep_));
        case Col::Second:   return Pad2(row * std::max(1, secondStep_));
        case Col::Meridiem: return (row == 0) ? CurrentDateFormat().am : CurrentDateFormat().pm;
        }
        return std::string();
    }

    int TimePicker::selectedRowOf(Col c) const {
        switch (c) {
        case Col::Hour:
            if (use24Hour_) return hour_;
            {
                int h12 = hour_ % 12;
                if (h12 == 0) h12 = 12;
                return h12 - 1;
            }
        case Col::Minute:   return minute_ / std::max(1, minuteStep_);
        case Col::Second:   return second_ / std::max(1, secondStep_);
        case Col::Meridiem: return (hour_ < 12) ? 0 : 1;
        }
        return -1;
    }

    void TimePicker::applyRow(Col c, int row) {
        const int count = itemCountOf(c);
        if (row < 0 || row >= count) return;

        switch (c) {
        case Col::Hour:
            if (use24Hour_) {
                hour_ = row;
            }
            else {
                int h12 = row + 1;
                if (h12 == 12) h12 = 0;
                hour_ = (hour_ >= 12) ? h12 + 12 : h12;
            }
            break;
        case Col::Minute:   minute_ = row * std::max(1, minuteStep_); break;
        case Col::Second:   second_ = row * std::max(1, secondStep_); break;
        case Col::Meridiem: {
            const int h12 = hour_ % 12;
            hour_ = (row == 1) ? h12 + 12 : h12;
            break;
        }
        }
        syncFaceText();
    }

    std::string TimePicker::headerOf(Col c) const {
        switch (c) {
        case Col::Hour:     return Kit::UiText(TextId::Hour);
        case Col::Minute:   return Kit::UiText(TextId::Minute);
        case Col::Second:   return Kit::UiText(TextId::Second);
        case Col::Meridiem: return std::string();
        }
        return std::string();
    }

    TimePicker::PopupGeom TimePicker::computePopup() const {
        PopupGeom g;

        Col cols[4];
        const int n = fillCols(cols);
        g.colCount = n;
        if (n <= 0) return g;

        g.colW = resolvedColWidth();
        g.headerH = themeHeaderH_ > 0.0f ? themeHeaderH_ : 22.0f;
        g.itemH = resolvedItemHeight();
        g.footerH = themeFooterH_ > 0.0f ? themeFooterH_ : 0.0f;

        g.maxRows = 0;
        for (int i = 0; i < n; ++i)
            g.maxRows = std::max(g.maxRows, itemCountOf(cols[i]));

        g.rows = g.maxRows;
        if (maxVisibleItems_ > 0) g.rows = std::min(g.rows, maxVisibleItems_);
        if (g.rows < 1) g.rows = 1;

        const float bt = BorderOf(popupBox_);
        const float padX = popupBox_.padding.left + popupBox_.padding.right;
        const float padY = popupBox_.padding.top + popupBox_.padding.bottom;

        g.w = g.colW * static_cast<float>(n) + padX + bt * 2.0f;
        g.h = g.headerH + g.itemH * static_cast<float>(g.rows) + g.footerH + padY + bt * 2.0f;

        g.x = x();
        g.y = y() + height() + themeGap_;

        if (const Window* w = window()) {
            const float winW = static_cast<float>(w->width());
            const float winH = static_cast<float>(w->height());
            if (g.x + g.w > winW - kPopupMargin) g.x = winW - kPopupMargin - g.w;
            if (g.x < kPopupMargin) g.x = kPopupMargin;

            if (g.y + g.h > winH - kPopupMargin) {
                const float above = y() - themeGap_ - g.h;
                if (above >= kPopupMargin) {
                    g.y = above;
                    g.flipped = true;
                }
                else {
                    g.y = winH - kPopupMargin - g.h;
                }
            }
            if (g.y < kPopupMargin) g.y = kPopupMargin;
        }

        g.innerX = g.x + bt + popupBox_.padding.left;
        g.innerY = g.y + bt + popupBox_.padding.top;
        return g;
    }

    void TimePicker::hitPopup(float px, float py, int& col, int& row) const {
        col = -1;
        row = -1;
        if (!popupActive()) return;

        const PopupGeom g = computePopup();
        if (g.w <= 0.0f || g.h <= 0.0f) return;
        if (px < g.x || px >= g.x + g.w || py < g.y || py >= g.y + g.h) return;

        if (g.footerH > 0.0f) {
            const float fy = g.innerY + g.headerH + g.itemH * static_cast<float>(g.rows);
            if (py >= fy) return;
        }

        const float localX = px - g.innerX;
        if (localX < 0.0f) return;
        const int c = static_cast<int>(localX / g.colW);
        if (c < 0 || c >= g.colCount) return;

        const float localY = py - (g.innerY + g.headerH);
        if (localY < 0.0f) return;
        const int r = static_cast<int>(localY / g.itemH);
        if (r < 0 || r >= g.rows) return;

        col = c;
        row = scroll_[c] + r;
    }

    bool TimePicker::hitFooter(float px, float py) const {
        if (!popupActive()) return false;
        const PopupGeom g = computePopup();
        if (g.footerH <= 0.0f || g.w <= 0.0f) return false;
        if (px < g.x || px >= g.x + g.w || py < g.y || py >= g.y + g.h) return false;

        const float fy = g.innerY + g.headerH + g.itemH * static_cast<float>(g.rows);
        return py >= fy && py < fy + g.footerH;
    }

    void TimePicker::clampScroll(int idx) {
        Col cols[4];
        const int n = fillCols(cols);
        if (idx < 0 || idx >= n) return;
        const int count = itemCountOf(cols[idx]);
        const int vis = (maxVisibleItems_ <= 0) ? count : std::min(count, maxVisibleItems_);
        if (vis < 1) return;
        const int maxScroll = std::max(0, count - vis);
        scroll_[idx] = std::min(std::max(scroll_[idx], 0), maxScroll);
    }

    int TimePicker::fillCols(Col cols[4]) const {
        int n = 0;
        cols[n++] = Col::Hour;
        cols[n++] = Col::Minute;
        if (withSeconds_) cols[n++] = Col::Second;
        if (!use24Hour_)  cols[n++] = Col::Meridiem;
        return n;
    }

    void TimePicker::requestRepaint() {
        RequestWindowRedraw();
    }

    bool TimePicker::popupAnchorVisible() const {
        return detail::AnchorVisibleInAncestors(*this);
    }

    bool TimePicker::popupActive() const {
        return open_ && popupAnchorVisible();
    }

    bool TimePicker::paintsOnTop() const {
        return popupActive() && !popupOverlayVisible();
    }

    bool TimePicker::HitTest(float px, float py) const {
        if (Widget::HitTest(px, py)) return true;
        if (!popupActive() || popupOverlayVisible()) return false;

        const PopupGeom g = computePopup();
        if (g.w <= 0.0f || g.h <= 0.0f) return false;
        return px >= g.x && px < g.x + g.w && py >= g.y && py < g.y + g.h;
    }

    void TimePicker::ApplyTheme(const Theme& t) {
        TextWidget::ApplyTheme(t);

        const TimePickerStyle& ts = t.timePicker;
        popupBox_ = ts.panel;
        headStyle_ = ts.head;
        valueStyle_ = ts.value;
        actionStyle_ = ts.action;

        themeItemHeight_ = (ts.itemHeight > 0.0f) ? ts.itemHeight : 26.0f;
        themeColWidth_ = (ts.columnWidth > 0.0f) ? ts.columnWidth : 46.0f;
        themeHeaderH_ = (ts.headerHeight > 0.0f) ? ts.headerHeight : 22.0f;
        themeFooterH_ = (ts.footerHeight > 0.0f) ? ts.footerHeight : 0.0f;
        themeGap_ = ts.gap;
        themeRadius_ = ts.radius;

        itemHoverColor_ = ts.itemHoverColor;
        itemSelectedColor_ = ts.itemSelectedColor;
        selectedTextColor_ = ts.selectedTextColor;
        arrowColor_ = ts.arrowColor;
        dividerColor_ = ts.dividerColor;
    }

    const BoxStyle& TimePicker::themeBox(const Theme& t) const {
        return t.timePicker.box;
    }

    const TextStyle& TimePicker::themeText(const Theme& t) const {
        return t.timePicker.text;
    }

    float TimePicker::hintWidth() const {
        if (prefWidth() > 0.0f) return prefWidth();

        float dx = 0.0f, dy = 0.0f;
        BoxInset(box_, dx, dy);
        const float arrow = resolvedArrowSize();

        float textW = 0.0f;
        if (const Window* w = window()) {
            textW = const_cast<Window*>(w)->MeasureText(text_, textStyle_.font).width;
        }
        if (textW <= 0.0f) return (defaultWidth() > 0.0f) ? defaultWidth() : (arrow + 8.0f + dx);
        return textW + 8.0f + arrow + dx;
    }

    float TimePicker::hintHeight() const {
        if (prefHeight() > 0.0f) return prefHeight();

        float dx = 0.0f, dy = 0.0f;
        BoxInset(box_, dx, dy);

        float textH = 0.0f;
        if (const Window* w = window()) {
            textH = const_cast<Window*>(w)->MeasureText(std::string_view("Ag"), textStyle_.font).height;
        }
        if (textH <= 0.0f) return (defaultHeight() > 0.0f) ? defaultHeight() : (kArrowFallback + dy);
        return std::max(textH, resolvedArrowSize()) + dy;
    }

    void TimePicker::paintContent(FrameCollector& fc) const {
        float cx = 0.0f, cy = 0.0f, cw = 0.0f, ch = 0.0f;
        contentRect(cx, cy, cw, ch);

        if (cw > 0.0f && ch > 0.0f) {
            const float arrow = resolvedArrowSize();
            const float textAvail = std::max(0.0f, cw - arrow - 8.0f);

            if (!text_.empty() && textAvail > 0.0f) {
                std::string line = text_;
                TextMetrics tm = fc.MeasureText(line, textStyle_.font);
                if (tm.width > textAvail) {
                    line = ellipsizeLine(fc, text_, textStyle_.font, textAvail);
                    tm = fc.MeasureText(line, textStyle_.font);
                }
                if (tm.width > 0.0f) {
                    const ColorSet cs = resolvedTextColors(currentState());
                    const float ty = cy + (ch - tm.height) * 0.5f;
                    paintTextAt(fc, line, cx, ty, tm, cs);
                }
            }

            const float axc = cx + cw - arrow * 0.5f;
            const float ayc = cy + ch * 0.5f;
            const float hw = arrow * 0.34f;
            const float dir = open_ ? -1.0f : 1.0f;
            std::vector<Vec2> tri{
                Vec2{ axc - hw, ayc - arrow * 0.18f * dir },
                Vec2{ axc + hw, ayc - arrow * 0.18f * dir },
                Vec2{ axc,      ayc + arrow * 0.30f * dir },
            };
            fc.AddShape(PolygonShape(std::move(tri), true), arrowColor_);
        }

        if (!paintsOnTop()) return;
        paintPopupInto(fc, false);
    }

    void TimePicker::paintPopupInto(FrameCollector& fc, bool localPopup) const {
        PopupGeom g = computePopup();
        if (g.w <= 0.0f || g.h <= 0.0f || g.colCount <= 0) return;

        if (localPopup) {
            const float dx = g.x;
            const float dy = g.y;
            g.x = 0.0f;  g.y = 0.0f;
            g.innerX -= dx;  g.innerY -= dy;
        }

        fc.ResetClip();

        const float radius = resolveRadius(popupBox_, fc.GetTheme());
        const float bt = BorderOf(popupBox_);

        fc.PushClip(g.x - kShadowPad, g.y - kShadowPad,
            g.w + 2.0f * kShadowPad, g.h + 2.0f * kShadowPad);
        drawutil::PaintShadow(fc, g.x, g.y, g.w, g.h, radius,
            popupBox_.shadow.color, 0.0f, popupBox_.shadow.offsetY,
            popupBox_.shadow.spread, popupBox_.opacity);
        fc.PopClip();

        fc.PushClip(g.x, g.y, g.w, g.h);
        const ColorSet pcs = popupBox_.colors.get(WidgetState::Normal);
        drawutil::PaintBox(fc, g.x, g.y, g.w, g.h, radius, bt, pcs.border, pcs.background);

        Col cols[4];
        const int n = fillCols(cols);

        const Color4f headColor = headStyle_.colors.get(WidgetState::Normal).text;
        const Color4f valueColor = valueStyle_.colors.get(WidgetState::Normal).text;
        const float rowsH = g.itemH * static_cast<float>(g.rows);

        for (int c = 0; c < n; ++c) {
            const Col col = cols[c];
            const float colX = g.innerX + g.colW * static_cast<float>(c);

            const std::string head = headerOf(col);
            if (!head.empty()) {
                drawutil::PaintTextCentered(fc, head, headStyle_.font,
                    colX, g.innerY, g.colW, g.headerH, headColor);
            }

            const int count = itemCountOf(col);
            const int sel = selectedRowOf(col);
            for (int r = 0; r < g.rows; ++r) {
                const int idx = scroll_[c] + r;
                if (idx < 0 || idx >= count) break;

                const float rowY = g.innerY + g.headerH + g.itemH * static_cast<float>(r);
                const bool isSel = (idx == sel);
                const bool isHover = (c == hoverCol_ && idx == hoverRow_);

                Color4f bg{ 0.0f, 0.0f, 0.0f, 0.0f };
                if (isSel)        bg = itemSelectedColor_;
                else if (isHover) bg = itemHoverColor_;
                if (bg.a > 0.0f) {
                    drawutil::PaintRoundedRect(fc, colX + 1.0f, rowY, g.colW - 2.0f, g.itemH,
                        std::min(kItemRadius, g.itemH * 0.25f), bg);
                }

                const Color4f textColor = isSel ? selectedTextColor_ : valueColor;
                drawutil::PaintTextCentered(fc, itemTextOf(col, idx), valueStyle_.font,
                    colX, rowY, g.colW, g.itemH, textColor);
            }

            if (c + 1 < n && dividerColor_.a > 0.0f) {
                const float dxLine = colX + g.colW - 0.5f;
                drawutil::PaintVLine(fc, dxLine, g.innerY + g.headerH * 0.5f,
                    g.headerH * 0.5f + rowsH, 1.0f, dividerColor_);
            }
        }

        if (g.footerH > 0.0f) {
            const float fy = g.innerY + g.headerH + rowsH;
            if (dividerColor_.a > 0.0f) {
                drawutil::PaintHLine(fc, g.innerX, fy, g.colW * static_cast<float>(n),
                    1.0f, dividerColor_);
            }
            const Color4f ac = hoverFooter_
                ? actionStyle_.colors.get(WidgetState::Normal).text
                : actionStyle_.colors.get(WidgetState::Disabled).text;
            drawutil::PaintTextCentered(fc, Kit::UiText(TextId::Now), actionStyle_.font,
                g.innerX, fy, g.colW * static_cast<float>(n), g.footerH, ac);
        }

        fc.PopClip();
    }

    bool TimePicker::OnMousePress(MouseEvent& e) {
        if (!isEnabled()) { pressedInFace_ = pressedInPopup_ = pressedInFooter_ = false; return false; }
        if (e.button != MouseButton::Left) return false;

        if (open_) {
            if (onPopupPress(e)) return true;
            if (!popupOverlayVisible()) {
                Close();
                e.accept();
                return true;
            }
        }

        if (!Widget::HitTest(e.x, e.y)) return false;
        pressedInFace_ = true;
        e.accept();
        return true;
    }

    bool TimePicker::onPopupPress(MouseEvent& e) {
        if (!popupActive()) return false;

        if (hitFooter(e.x, e.y)) {
            pressedInFooter_ = true;
            e.accept();
            return true;
        }
        int c = -1, r = -1;
        hitPopup(e.x, e.y, c, r);
        if (c >= 0 && r >= 0) {
            pressedInPopup_ = true;
            Col cols[4];
            const int n = fillCols(cols);
            if (c < n) activeCol_ = cols[c];
            if (c != hoverCol_ || r != hoverRow_) {
                hoverCol_ = c;
                hoverRow_ = r;
                requestRepaint();
            }
            e.accept();
            return true;
        }
        return false;
    }

    bool TimePicker::OnMouseRelease(MouseEvent& e) {
        if (e.button != MouseButton::Left) return false;

        if (!popupOverlayVisible() && (pressedInFooter_ || pressedInPopup_))
            return onPopupRelease(e);

        if (pressedInFace_) {
            pressedInFace_ = false;
            if (!Widget::HitTest(e.x, e.y)) { e.accept(); return true; }
            Toggle();
            e.accept();
            return true;
        }
        return false;
    }

    bool TimePicker::onPopupRelease(MouseEvent& e) {
        if (e.button != MouseButton::Left) return false;

        if (pressedInFooter_) {
            pressedInFooter_ = false;
            if (hitFooter(e.x, e.y)) {
                if (SetNow()) notifyTimeChanged();
            }
            requestRepaint();
            e.accept();
            return true;
        }

        if (pressedInPopup_) {
            pressedInPopup_ = false;
            int c = -1, r = -1;
            hitPopup(e.x, e.y, c, r);
            if (c >= 0 && r >= 0) {
                Col cols[4];
                int n = 0;
                cols[n++] = Col::Hour;
                cols[n++] = Col::Minute;
                if (withSeconds_) cols[n++] = Col::Second;
                if (!use24Hour_)  cols[n++] = Col::Meridiem;

                if (c < n) {
                    const Col col = cols[c];
                    const int before = hour_ * 3600 + minute_ * 60 + second_;
                    applyRow(col, r);
                    if (hour_ * 3600 + minute_ * 60 + second_ != before)
                        notifyTimeChanged();
                }
            }
            requestRepaint();
            e.accept();
            return true;
        }
        return false;
    }

    bool TimePicker::OnMouseMove(MouseEvent& e) {
        if (popupOverlayVisible()) return false;
        return onPopupMove(e);
    }

    bool TimePicker::onPopupMove(MouseEvent& e) {
        if (!popupActive()) return false;

        int c = -1, r = -1;
        hitPopup(e.x, e.y, c, r);
        const bool onFooter = hitFooter(e.x, e.y);

        if (c == hoverCol_ && r == hoverRow_ && onFooter == hoverFooter_) return false;

        hoverCol_ = c;
        hoverRow_ = r;
        hoverFooter_ = onFooter;
        requestRepaint();
        return true;
    }

    bool TimePicker::OnMouseWheel(MouseEvent& e) {
        if (popupOverlayVisible()) return false;
        return onPopupWheel(e);
    }

    bool TimePicker::onPopupWheel(MouseEvent& e) {
        if (!popupActive()) return false;

        int c = -1, r = -1;
        hitPopup(e.x, e.y, c, r);
        if (c < 0) return false;

        scroll_[c] += (e.wheelDelta > 0) ? -1 : 1;
        hoverRow_ = -1;

        clampScroll(c);

        requestRepaint();
        e.accept();
        return true;
    }

    void TimePicker::OnMouseLeave() {
        TextWidget::OnMouseLeave();
        onPopupLeave();
    }

    void TimePicker::onPopupLeave() {
        if (hoverCol_ != -1 || hoverRow_ != -1 || hoverFooter_) {
            hoverCol_ = hoverRow_ = -1;
            hoverFooter_ = false;
            requestRepaint();
        }
    }

    bool TimePicker::OnKeyPress(KeyEvent& e) {
        if (!isEnabled()) return false;

        if (!open_) {
            switch (e.key) {
            case Key::Space:
            case Key::Enter:
            case Key::DownArrow:
            case Key::UpArrow:
            case Key::LeftArrow:
            case Key::RightArrow:
                Open();
                e.accept();
                return true;
            default:
                return false;
            }
        }

        Col cols[4];
        const int n = fillCols(cols);

        int active = 0;
        for (int i = 0; i < n; ++i) {
            if (cols[i] == activeCol_) { active = i; break; }
        }

        switch (e.key) {
        case Key::Escape:
        case Key::Enter:
        case Key::Space:
            Close();
            e.accept();
            return true;

        case Key::UpArrow:
        case Key::DownArrow: {
            const Col col = cols[active];
            const int cur = selectedRowOf(col);
            const int next = cur + (e.key == Key::UpArrow ? -1 : 1);
            const int count = itemCountOf(col);
            const int wrapped = ((next % count) + count) % count;
            const int before = hour_ * 3600 + minute_ * 60 + second_;
            applyRow(col, wrapped);
            if (hour_ * 3600 + minute_ * 60 + second_ != before) notifyTimeChanged();
            const int vis = (maxVisibleItems_ > 0) ? maxVisibleItems_ : 7;
            if (wrapped < scroll_[active]) scroll_[active] = wrapped;
            else if (wrapped >= scroll_[active] + vis) scroll_[active] = wrapped - vis + 1;
            clampScroll(active);
            requestRepaint();
            e.accept();
            return true;
        }

        case Key::LeftArrow:
        case Key::RightArrow: {
            const int dir = (e.key == Key::RightArrow) ? 1 : -1;
            int next = ((active + dir) % n + n) % n;
            activeCol_ = cols[next];
            requestRepaint();
            e.accept();
            return true;
        }

        default:
            return false;
        }
    }

    void TimePicker::OnFocusOut() {
        if (open_) Close();
    }

}
