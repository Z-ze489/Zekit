
#include "pch.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "include/ZeDateTimePicker.h"
#include "include/ZeOverlayWindow.h"
#include "include/Zekit.h"

#include "ZeDrawUtil.h"
#include "ZeFrameCollector.h"
#include "ZeAnchorVisible.h"
#include "include/ZeText.h"

namespace Kit {

    namespace {
        constexpr int kGridRows = 6;
        constexpr int kGridCols = 7;
        constexpr int kCellCount = kGridRows * kGridCols;

        constexpr float kFallbackGap = 4.0f;
        constexpr float kFallbackPad = 8.0f;
        constexpr float kFallbackCellW = 34.0f;
        constexpr float kFallbackCellH = 30.0f;
        constexpr float kFallbackHeadH = 34.0f;
        constexpr float kFallbackWeekH = 22.0f;
        constexpr float kFallbackTimeH = 30.0f;
        constexpr float kFallbackFooterH = 34.0f;
        constexpr float kWindowMargin = 4.0f;
        constexpr float kShadowPad = 8.0f;
        constexpr float kFallbackW = 200.0f;
        constexpr float kFallbackH = 34.0f;
        constexpr float kTimeBlockW = 26.0f;
        constexpr float kArrowSlotW = 30.0f;

        float BorderOf(const BoxStyle& box) {
            return std::max({ box.border.width[Border::Top], box.border.width[Border::Right],
                              box.border.width[Border::Bottom], box.border.width[Border::Left] });
        }
    }

    DateTimePicker::DateTimePicker() {
        ApplyTheme(Themes::Light());

        if (!NowLocal(value_)) value_ = DateTime{ 2026, 1, 1, 0, 0, 0 };
        value_.second = 0;
        draft_ = value_;
        shownYear_ = value_.year;
        shownMonth_ = value_.month;

        SetFocusable(true);
        SetCursor(Cursor::Pointer);
        SetDefaultSize(kFallbackW, kFallbackH);
    }

    const BoxStyle& DateTimePicker::themeBox(const Theme& t) const { return t.datePicker.box; }
    const TextStyle& DateTimePicker::themeText(const Theme& t) const { return t.datePicker.text; }

    void DateTimePicker::ApplyTheme(const Theme& t) {
        TextWidget::ApplyTheme(t);
        styleTheme_ = t.datePicker;
        markMeasureDirty();
    }

    void DateTimePicker::requestRepaint() {
        RequestWindowRedraw();
    }

    void DateTimePicker::markMeasureDirty() {
        notifyLayoutInputChanged();
        requestRepaint();
    }

    void DateTimePicker::SetValue(const DateTime& v) {
        value_ = v;
        ClampDateTime(value_);
        if (Less(value_, minValue_)) value_ = minValue_;
        if (Less(maxValue_, value_)) value_ = maxValue_;
        ClampDateTime(value_);

        draft_ = value_;
        shownYear_ = value_.year;
        shownMonth_ = value_.month;

        markMeasureDirty();

        notifyChanged();
        RequestWindowRedraw();
    }

    void DateTimePicker::SetRange(const DateTime& minValue, const DateTime& maxValue) {
        if (Less(maxValue, minValue)) {
            minValue_ = maxValue;
            maxValue_ = minValue;
        }
        else {
            minValue_ = minValue;
            maxValue_ = maxValue;
        }
        ClampDateTime(minValue_);
        ClampDateTime(maxValue_);

        if (Less(value_, minValue_)) value_ = minValue_;
        if (Less(maxValue_, value_)) value_ = maxValue_;
        ClampDateTime(value_);
        draft_ = value_;

        requestRepaint();
        RequestWindowRedraw();
    }

    bool DateTimePicker::Less(const DateTime& a, const DateTime& b) {
        if (a.year != b.year)     return a.year < b.year;
        if (a.month != b.month)   return a.month < b.month;
        if (a.day != b.day)       return a.day < b.day;
        if (a.hour != b.hour)     return a.hour < b.hour;
        if (a.minute != b.minute) return a.minute < b.minute;
        return a.second < b.second;
    }

    void DateTimePicker::SetShowTime(bool showTime, bool withSeconds) {
        showTime_ = showTime;
        showSeconds_ = showTime ? withSeconds : false;
        markMeasureDirty();
        RequestWindowRedraw();
    }

    void DateTimePicker::SetFormat(std::string fmt) {
        format_ = std::move(fmt);
        markMeasureDirty();
        RequestWindowRedraw();
    }

    std::string DateTimePicker::formatValue(const DateTime& v) const {
        if (!format_.empty()) {
            std::string out;
            out.reserve(format_.size() + 8);
            for (std::size_t i = 0; i < format_.size(); ++i) {
                if (format_[i] != '%' || i + 1 >= format_.size()) {
                    out.push_back(format_[i]);
                    continue;
                }
                const char c = format_[++i];
                char buf[16];
                switch (c) {
                case 'Y': std::snprintf(buf, sizeof(buf), "%04d", v.year);   out += buf; break;
                case 'm': std::snprintf(buf, sizeof(buf), "%02d", v.month);  out += buf; break;
                case 'd': std::snprintf(buf, sizeof(buf), "%02d", v.day);    out += buf; break;
                case 'H': std::snprintf(buf, sizeof(buf), "%02d", v.hour);   out += buf; break;
                case 'M': std::snprintf(buf, sizeof(buf), "%02d", v.minute); out += buf; break;
                case 'S': std::snprintf(buf, sizeof(buf), "%02d", v.second); out += buf; break;
                case '%': out.push_back('%'); break;
                default:  out.push_back('%'); out.push_back(c); break;
                }
            }
            return out;
        }

        if (!showTime_)   return FormatDate(v);
        if (showSeconds_) return FormatDateTime(v, true);
        return FormatDateTime(v, false);
    }

    int DateTimePicker::wantHour() const { return showTime_ ? draft_.hour : 0; }
    int DateTimePicker::wantMinute() const { return showTime_ ? draft_.minute : 0; }
    int DateTimePicker::wantSecond() const { return (showTime_ && showSeconds_) ? draft_.second : 0; }

    void DateTimePicker::SelectToday() {
        DateTime now{};
        if (!NowLocal(now)) return;
        if (!showSeconds_) now.second = 0;

        if (Less(now, minValue_)) now = minValue_;
        if (Less(maxValue_, now)) now = maxValue_;
        ClampDateTime(now);

        draft_ = now;
        shownYear_ = now.year;
        shownMonth_ = now.month;
        requestRepaint();
    }

    DateTimePicker::~DateTimePicker() = default;
    DateTimePicker::DateTimePicker(DateTimePicker&&) noexcept = default;

    bool DateTimePicker::popupAnchorVisible() const {
        return detail::AnchorVisibleInAncestors(*this);
    }

    void DateTimePicker::openPopup() {
        if (open_) return;
        if (!popupAnchorVisible()) return;

        draft_ = value_;
        shownYear_ = value_.year;
        shownMonth_ = value_.month;
        hover_ = Hit{};
        open_ = true;
        showPopupOverlay();
        requestRepaint();
    }

    void DateTimePicker::closePopup() {
        if (!open_) return;
        open_ = false;
        popupPressedInFace_ = false;
        pressedInPopup_ = false;
        hover_ = Hit{};
        hidePopupOverlay();
        requestRepaint();
    }

    bool DateTimePicker::popupRect(float& x, float& y, float& w, float& h) const {
        const PopupGeom g = computePopup();
        if (g.w <= 0.0f || g.h <= 0.0f) return false;
        x = g.x; y = g.y; w = g.w; h = g.h;
        return true;
    }

    void DateTimePicker::dismissPopup() {
        closePopup();
    }

    bool DateTimePicker::SetOpen(bool open) {
        if (open) openPopup();
        else      closePopup();
        return open_;
    }

    DateTimePicker::Layout DateTimePicker::computeLayout() const {
        Layout L;

        const float pad = (styleTheme_.panel.padding.left > 0.0f)
            ? styleTheme_.panel.padding.left : kFallbackPad;
        const float cellW = (styleTheme_.dayCellWidth > 0.0f) ? styleTheme_.dayCellWidth : kFallbackCellW;
        const float cellH = (styleTheme_.dayCellHeight > 0.0f) ? styleTheme_.dayCellHeight : kFallbackCellH;
        const float headH = (styleTheme_.headerHeight > 0.0f) ? styleTheme_.headerHeight : kFallbackHeadH;
        const float timeH = showTime_ ? kFallbackTimeH : 0.0f;
        const float footerH = kFallbackFooterH;

        L.panel.w = cellW * kGridCols + pad * 2.0f;
        L.panel.h = pad + headH + kFallbackWeekH
            + cellH * kGridRows + timeH + footerH + pad;

        const float gap = (styleTheme_.gap > 0.0f) ? styleTheme_.gap : kFallbackGap;
        float px = x();
        float py = y() + height() + gap;

        float vw = 0.0f, vh = 0.0f;
        if (Window* w = window()) {
            vw = static_cast<float>(w->width());
            vh = static_cast<float>(w->height());
        }

        if (vw > 0.0f) {
            if (px + L.panel.w > vw - kWindowMargin)
                px = vw - kWindowMargin - L.panel.w;
            if (px < kWindowMargin) px = kWindowMargin;
        }
        if (vh > 0.0f && py + L.panel.h > vh - kWindowMargin) {
            const float above = y() - gap - L.panel.h;
            py = (above >= kWindowMargin)
                ? above
                : std::max(kWindowMargin, vh - kWindowMargin - L.panel.h);
        }

        L.panel.x = px;
        L.panel.y = py;

        float cy = py + pad;
        L.headY = cy;   L.headH = headH;          cy += headH;
        L.weekY = cy;   L.weekH = kFallbackWeekH; cy += kFallbackWeekH;
        L.gridX = px + pad;
        L.gridY = cy;
        L.cellW = cellW;
        L.cellH = cellH;
        cy += cellH * kGridRows;
        L.timeY = cy;   L.timeH = timeH;          cy += timeH;
        L.footerY = cy; L.footerH = footerH;

        return L;
    }

    DateTimePicker::PopupGeom DateTimePicker::computePopup() const {
        return computeLayout().panel;
    }

    void DateTimePicker::cellDate(int index, int& y, int& m, int& d, bool& outOfMonth) const {
        y = shownYear_;
        m = shownMonth_;
        outOfMonth = false;

        const int firstDow = DayOfWeek(shownYear_, shownMonth_, 1);
        const int dim = DaysInMonth(shownYear_, shownMonth_);

        int dayNum = index - firstDow + 1;
        if (dayNum < 1) {
            outOfMonth = true;
            m = shownMonth_ - 1;
            y = shownYear_;
            if (m < 1) { m = 12; y -= 1; }
            dayNum += DaysInMonth(y, m);
        }
        else if (dayNum > dim) {
            outOfMonth = true;
            m = shownMonth_ + 1;
            y = shownYear_;
            if (m > 12) { m = 1; y += 1; }
            dayNum -= dim;
        }
        d = dayNum;
    }

    DateTimePicker::Hit DateTimePicker::hitTestPopup(float px, float py) const {
        Hit h;
        if (!open_) return h;

        const Layout L = computeLayout();
        if (!drawutil::HitRect(px, py, L.panel.x, L.panel.y, L.panel.w, L.panel.h))
            return h;

        if (py >= L.headY && py < L.headY + L.headH) {
            const float left0 = L.panel.x + 8.0f;
            const float right1 = L.panel.x + L.panel.w - 8.0f;

            if (px >= left0 && px < left0 + kArrowSlotW) { h.zone = Zone::PrevYear;  return h; }
            if (px >= left0 + kArrowSlotW && px < left0 + kArrowSlotW * 2.0f) { h.zone = Zone::PrevMonth; return h; }
            if (px >= right1 - kArrowSlotW * 2.0f && px < right1 - kArrowSlotW) { h.zone = Zone::NextMonth; return h; }
            if (px >= right1 - kArrowSlotW && px < right1) { h.zone = Zone::NextYear;  return h; }
            return h;
        }

        if (px >= L.gridX && px < L.gridX + L.cellW * kGridCols
            && py >= L.gridY && py < L.gridY + L.cellH * kGridRows) {
            const int col = static_cast<int>((px - L.gridX) / L.cellW);
            const int row = static_cast<int>((py - L.gridY) / L.cellH);
            const int idx = row * kGridCols + col;
            if (idx >= 0 && idx < kCellCount) {
                int yy = 0, mm = 0, dd = 0;
                bool oom = false;
                cellDate(idx, yy, mm, dd, oom);
                h.zone = Zone::Day;
                h.year = yy;
                h.month = mm;
                h.day = dd;
            }
            return h;
        }

        if (showTime_ && py >= L.timeY && py < L.timeY + L.timeH) {
            const float x0 = L.panel.x + 10.0f;
            const float blockW = kTimeBlockW * 3.0f;
            const bool upper = (py < L.timeY + L.timeH * 0.5f);

            if (px >= x0 && px < x0 + blockW) {
                h.zone = upper ? Zone::HourUp : Zone::HourDown;
                return h;
            }
            if (px >= x0 + blockW + 6.0f && px < x0 + blockW * 2.0f + 6.0f) {
                h.zone = upper ? Zone::MinuteUp : Zone::MinuteDown;
                return h;
            }
            if (showSeconds_) {
                const float x2 = x0 + blockW * 2.0f + 12.0f;
                if (px >= x2 && px < x2 + blockW) {
                    h.zone = upper ? Zone::SecondUp : Zone::SecondDown;
                    return h;
                }
            }
            return h;
        }

        if (py >= L.footerY && py < L.footerY + L.footerH) {
            const float btnH = 24.0f;
            const float byy = L.footerY + (L.footerH - btnH) * 0.5f;
            const float todayW = 56.0f, cancelW = 56.0f, confirmW = 56.0f, gapB = 6.0f;

            const float confirmX = L.panel.x + L.panel.w - 10.0f - confirmW;
            const float cancelX = confirmX - gapB - cancelW;
            const float todayX = L.panel.x + 10.0f;

            if (drawutil::HitRect(px, py, todayX, byy, todayW, btnH)) { h.zone = Zone::Today;   return h; }
            if (drawutil::HitRect(px, py, cancelX, byy, cancelW, btnH)) { h.zone = Zone::Cancel;  return h; }
            if (drawutil::HitRect(px, py, confirmX, byy, confirmW, btnH)) { h.zone = Zone::Confirm; return h; }
            return h;
        }

        return h;
    }

    bool DateTimePicker::onPopupPress(MouseEvent& e) {
        if (e.button != MouseButton::Left || !open_) return false;

        const Hit h = hitTestPopup(e.x, e.y);
        if (h.zone != Zone::None) {
            pressedInPopup_ = true;
            handleZone(h);
            e.accept();
            return true;
        }

        const PopupGeom g = computePopup();
        if (drawutil::HitRect(e.x, e.y, g.x, g.y, g.w, g.h)) {
            e.accept();
            return true;
        }
        return false;
    }

    bool DateTimePicker::onPopupRelease(MouseEvent& e) {
        if (!pressedInPopup_) return false;
        pressedInPopup_ = false;
        e.accept();
        return true;
    }

    bool DateTimePicker::onPopupMove(MouseEvent& e) {
        if (!open_) return false;
        const Hit h = hitTestPopup(e.x, e.y);
        if (h.zone == hover_.zone && h.year == hover_.year
            && h.month == hover_.month && h.day == hover_.day) return false;
        hover_ = h;
        requestRepaint();
        return true;
    }

    bool DateTimePicker::onPopupWheel(MouseEvent& e) {
        if (!open_) return false;
        if (e.wheelDelta > 0) {
            shownMonth_ -= 1;
            if (shownMonth_ < 1) { shownMonth_ = 12; shownYear_ -= 1; }
        }
        else {
            shownMonth_ += 1;
            if (shownMonth_ > 12) { shownMonth_ = 1; shownYear_ += 1; }
        }
        requestRepaint();
        e.accept();
        return true;
    }

    void DateTimePicker::onPopupLeave() {
        if (hover_.zone == Zone::None) return;
        hover_ = Hit{};
        requestRepaint();
    }

    bool DateTimePicker::paintsOnTop() const {
        return open_ && popupAnchorVisible() && !popupOverlayVisible();
    }

    bool DateTimePicker::HitTest(float px, float py) const {
        if (Widget::HitTest(px, py)) return true;
        if (!paintsOnTop()) return false;

        const PopupGeom g = computePopup();
        return drawutil::HitRect(px, py, g.x, g.y, g.w, g.h);
    }

    float DateTimePicker::hintWidth() const {
        if (prefWidth() > 0.0f) return prefWidth();

        Window* win = window();
        if (!win) return Widget::hintWidth();

        const float textW = win->MeasureText(formatValue(value_), textStyle_.font).width;
        const float iconW = (height() > 0.0f) ? height() * 0.62f : 20.0f;
        const Padding& p = box_.padding;
        return textW + iconW + 8.0f + p.left + p.right + BorderOf(box_) * 2.0f;
    }

    float DateTimePicker::hintHeight() const {
        if (prefHeight() > 0.0f) return prefHeight();

        Window* win = window();
        if (!win) return Widget::hintHeight();

        const TextMetrics tm = win->MeasureText("2026-03-14 09:30", textStyle_.font);
        const Padding& p = box_.padding;
        return tm.height + 10.0f + p.top + p.bottom + BorderOf(box_) * 2.0f;
    }

    void DateTimePicker::Paint(FrameCollector& fc) const {
        paintHostAndPopup(fc);
    }

    void DateTimePicker::paintPopupInto(FrameCollector& fc, bool localPopup) const {
        Layout L = computeLayout();
        const Theme& theme = fc.GetTheme();

        if (localPopup) {
            const float dx = L.panel.x;
            const float dy = L.panel.y;
            L.panel.x = 0.0f;  L.panel.y = 0.0f;
            L.headY -= dy;     L.weekY -= dy;
            L.gridX -= dx;     L.gridY -= dy;
            L.timeY -= dy;     L.footerY -= dy;
        }

        fc.ResetClip();

        fc.PushClip(L.panel.x - kShadowPad, L.panel.y - kShadowPad,
            L.panel.w + kShadowPad * 2.0f, L.panel.h + kShadowPad * 2.0f);
        const Shadow& sh = styleTheme_.panel.shadow;
        if (sh.enabled) {
            drawutil::PaintShadow(fc, L.panel.x, L.panel.y, L.panel.w, L.panel.h,
                BoxWidget::resolveRadius(styleTheme_.panel, theme),
                sh.color, sh.offsetX, sh.offsetY, sh.spread, styleTheme_.panel.opacity);
        }
        fc.PopClip();

        fc.PushClip(L.panel.x, L.panel.y, L.panel.w, L.panel.h);
        PaintPopup(fc, L, theme);
        fc.PopClip();
    }

    void DateTimePicker::paintFace(FrameCollector& fc) const {
        float cx = 0.0f, cy = 0.0f, cw = 0.0f, ch = 0.0f;
        contentRect(cx, cy, cw, ch);
        if (cw <= 0.0f || ch <= 0.0f) return;

        const float iconW = std::min(ch * 0.62f, cw * 0.30f);

        const ColorSet cs = resolvedTextColors(currentState());

        const std::string shown = formatValue(value_);
        if (!shown.empty()) {
            const float availW = std::max(0.0f, cw - iconW - 8.0f);
            if (availW > 0.0f) {
                const TextMetrics full = fc.MeasureText(shown, textStyle_.font);
                if (full.width > 0.0f) {
                    const std::string line = (full.width <= availW)
                        ? shown
                        : ellipsizeLine(fc, shown, textStyle_.font, availW);
                    if (!line.empty()) {
                        const TextMetrics tm = (line == shown)
                            ? full
                            : fc.MeasureText(line, textStyle_.font);
                        if (tm.width > 0.0f) {
                            const float ty = cy + (ch - tm.height) * 0.5f;
                            fc.AddText(line, textStyle_.font, cx, ty + tm.ascent, cs.text);
                        }
                    }
                }
            }
        }

        if (iconW <= 5.0f) return;

        Color4f ic = cs.text;
        ic.a *= 0.7f;
        if (ic.a <= 0.0f) return;

        const float ix = cx + cw - iconW;
        const float iy = cy + (ch - iconW) * 0.5f;
        const float stroke = std::max(1.0f, iconW * 0.085f);

        drawutil::PaintBox(fc, ix, iy, iconW, iconW, 2.0f, stroke, ic,
            Color4f{ 0.0f, 0.0f, 0.0f, 0.0f });
        drawutil::PaintHLine(fc, ix + stroke, iy + iconW * 0.34f,
            iconW - stroke * 2.0f, stroke, ic);
        drawutil::PaintVLine(fc, ix + iconW * 0.30f, iy - iconW * 0.10f,
            iconW * 0.20f, stroke, ic);
        drawutil::PaintVLine(fc, ix + iconW * 0.62f, iy - iconW * 0.10f,
            iconW * 0.20f, stroke, ic);
    }

    void DateTimePicker::PaintPopup(FrameCollector& fc, const Layout& L, const Theme& theme) const {
        const float radius = BoxWidget::resolveRadius(styleTheme_.panel, theme);
        const ColorSet panelCs = styleTheme_.panel.colors.get(WidgetState::Normal);
        Color4f pbg = panelCs.background;
        Color4f pbd = panelCs.border;
        pbg.a *= styleTheme_.panel.opacity;
        pbd.a *= styleTheme_.panel.opacity;
        drawutil::PaintBox(fc, L.panel.x, L.panel.y, L.panel.w, L.panel.h,
            radius, BorderOf(styleTheme_.panel), pbd, pbg);

        const Color4f arrowIdle = styleTheme_.arrowColor;
        const Color4f arrowHot = (styleTheme_.arrowHoverColor.a > 0.0f)
            ? styleTheme_.arrowHoverColor : theme.primary;

        {
            const float left0 = L.panel.x + 8.0f;
            const float right1 = L.panel.x + L.panel.w - 8.0f;
            const float cyHead = L.headY + L.headH * 0.5f;
            const float cs = 4.0f, ct = 1.6f;

            drawutil::PaintChevron(fc, left0 + kArrowSlotW * 0.5f, cyHead, cs, ct,
                drawutil::ChevronDir::Left,
                (hover_.zone == Zone::PrevYear) ? arrowHot : arrowIdle);
            drawutil::PaintChevron(fc, left0 + kArrowSlotW * 1.5f, cyHead, cs, ct,
                drawutil::ChevronDir::Left,
                (hover_.zone == Zone::PrevMonth) ? arrowHot : arrowIdle);
            drawutil::PaintChevron(fc, right1 - kArrowSlotW * 1.5f, cyHead, cs, ct,
                drawutil::ChevronDir::Right,
                (hover_.zone == Zone::NextMonth) ? arrowHot : arrowIdle);
            drawutil::PaintChevron(fc, right1 - kArrowSlotW * 0.5f, cyHead, cs, ct,
                drawutil::ChevronDir::Right,
                (hover_.zone == Zone::NextYear) ? arrowHot : arrowIdle);

            const std::string ym = FormatYearMonth(shownYear_, shownMonth_);
            const TextStyle& hs = styleTheme_.head;
            const TextMetrics tm = fc.MeasureText(ym, hs.font);
            if (tm.width > 0.0f) {
                const float tx = L.panel.x + (L.panel.w - tm.width) * 0.5f;
                const float ty = L.headY + (L.headH - tm.height) * 0.5f;
                fc.AddText(ym, hs.font, tx, ty + tm.ascent,
                    hs.colors.get(WidgetState::Normal).text);
            }
        }

        {
            const TextStyle& ws = styleTheme_.weekday;
            for (int c = 0; c < kGridCols; ++c) {
                const float cx = L.gridX + L.cellW * static_cast<float>(c);
                drawutil::PaintTextCentered(fc, CurrentDateFormat().weekdays[c], ws.font,
                    cx, L.weekY, L.cellW, L.weekH,
                    ws.colors.get(WidgetState::Normal).text);
            }
        }

        {
            const TextStyle& ds = styleTheme_.day;
            const Color4f normal = ds.colors.get(WidgetState::Normal).text;
            const Color4f muted = (styleTheme_.dayMutedColor.a > 0.0f)
                ? styleTheme_.dayMutedColor : ds.colors.get(WidgetState::Disabled).text;
            const Color4f selBg = styleTheme_.daySelectedColor;
            const Color4f hovBg = styleTheme_.dayHoverColor;
            const Color4f todayCol = styleTheme_.dayTodayColor;

            DateTime today{};
            const bool haveToday = NowLocal(today);

            for (int i = 0; i < kCellCount; ++i) {
                const int row = i / kGridCols;
                const int col = i % kGridCols;
                const float cx = L.gridX + L.cellW * static_cast<float>(col);
                const float cyy = L.gridY + L.cellH * static_cast<float>(row);

                int yy = 0, mm = 0, dd = 0;
                bool oom = false;
                cellDate(i, yy, mm, dd, oom);

                DateTime cell{ yy, mm, dd, wantHour(), wantMinute(), wantSecond() };
                ClampDateTime(cell);

                const bool outOfRange = Less(cell, minValue_) || Less(maxValue_, cell);
                const bool selected = (yy == draft_.year && mm == draft_.month && dd == draft_.day);
                const bool isToday = haveToday
                    && (yy == today.year && mm == today.month && dd == today.day);
                const bool hovered = (hover_.zone == Zone::Day)
                    && (hover_.year == yy) && (hover_.month == mm) && (hover_.day == dd);

                const float pad2 = 2.0f;
                const float bx = cx + pad2, by = cyy + pad2;
                const float bw = L.cellW - pad2 * 2.0f, bh = L.cellH - pad2 * 2.0f;

                if (selected && !outOfRange)
                    drawutil::PaintRoundedRect(fc, bx, by, bw, bh, 4.0f, selBg);
                else if (hovered && !outOfRange)
                    drawutil::PaintRoundedRect(fc, bx, by, bw, bh, 4.0f, hovBg);

                Color4f fg = normal;
                if (selected)                          fg = Color4f::White();
                else if (outOfRange || oom)            fg = muted;
                else if (isToday && todayCol.a > 0.0f) fg = todayCol;

                char buf[8];
                std::snprintf(buf, sizeof(buf), "%d", dd);
                drawutil::PaintTextCentered(fc, buf, ds.font, cx, cyy, L.cellW, L.cellH, fg);

                if (isToday && !selected && todayCol.a > 0.0f) {
                    drawutil::PaintBox(fc, bx, by, bw, bh, 4.0f, 1.0f, todayCol,
                        Color4f{ 0.0f, 0.0f, 0.0f, 0.0f });
                }
            }
        }

        if (showTime_ && L.timeH > 0.0f) {
            const TextStyle& vs = styleTheme_.value;
            const FontDesc& font = vs.font;
            Color4f fg = vs.colors.get(WidgetState::Normal).text;
            if (fg.a <= 0.0f) fg = theme.text;

            const float x0 = L.panel.x + 10.0f;
            const float blockW = kTimeBlockW * 3.0f;

            const auto timeBlock = [&](float bx, int value, Zone up, Zone down) {
                const float ccx = bx + kTimeBlockW;
                drawutil::PaintChevron(fc, ccx, L.timeY + L.timeH * 0.25f, 3.2f, 1.4f,
                    drawutil::ChevronDir::Up,
                    (hover_.zone == up) ? arrowHot : arrowIdle);
                drawutil::PaintChevron(fc, ccx, L.timeY + L.timeH * 0.75f, 3.2f, 1.4f,
                    drawutil::ChevronDir::Down,
                    (hover_.zone == down) ? arrowHot : arrowIdle);

                char buf[8];
                std::snprintf(buf, sizeof(buf), "%02d", value);
                drawutil::PaintTextCentered(fc, buf, font,
                    bx + kTimeBlockW * 1.4f, L.timeY, kTimeBlockW * 1.2f, L.timeH, fg);
            };

            timeBlock(x0, draft_.hour, Zone::HourUp, Zone::HourDown);
            drawutil::PaintTextCentered(fc, ":", font,
                x0 + kTimeBlockW * 2.8f, L.timeY, 8.0f, L.timeH, fg);

            const float x1 = x0 + blockW + 6.0f;
            timeBlock(x1, draft_.minute, Zone::MinuteUp, Zone::MinuteDown);

            if (showSeconds_) {
                drawutil::PaintTextCentered(fc, ":", font,
                    x0 + blockW * 2.0f + 6.0f, L.timeY, 8.0f, L.timeH, fg);
                const float x2 = x0 + blockW * 2.0f + 12.0f;
                timeBlock(x2, draft_.second, Zone::SecondUp, Zone::SecondDown);
            }
        }

        {
            const float btnH = 24.0f;
            const float byy = L.footerY + (L.footerH - btnH) * 0.5f;
            const float todayW = 56.0f, cancelW = 56.0f, confirmW = 56.0f, gapB = 6.0f;

            const float confirmX = L.panel.x + L.panel.w - 10.0f - confirmW;
            const float cancelX = confirmX - gapB - cancelW;
            const float todayX = L.panel.x + 10.0f;

            const Color4f hoverBg = (styleTheme_.dayHoverColor.a > 0.0f)
                ? styleTheme_.dayHoverColor : theme.bg;
            Color4f btnText = styleTheme_.value.colors.get(WidgetState::Normal).text;
            if (btnText.a <= 0.0f) btnText = theme.text;

            if (hover_.zone == Zone::Today)
                drawutil::PaintRoundedRect(fc, todayX, byy, todayW, btnH, 4.0f, hoverBg);
            drawutil::PaintTextCentered(fc, Kit::UiText(TextId::Today), styleTheme_.value.font,
                todayX, byy, todayW, btnH, btnText);

            if (hover_.zone == Zone::Cancel)
                drawutil::PaintRoundedRect(fc, cancelX, byy, cancelW, btnH, 4.0f, hoverBg);
            drawutil::PaintTextCentered(fc, Kit::UiText(TextId::Cancel), styleTheme_.value.font,
                cancelX, byy, cancelW, btnH, btnText);

            drawutil::PaintRoundedRect(fc, confirmX, byy, confirmW, btnH, 4.0f, theme.primary);
            drawutil::PaintTextCentered(fc, Kit::UiText(TextId::Ok), styleTheme_.value.font,
                confirmX, byy, confirmW, btnH, theme.onPrimary);
        }
    }

    void DateTimePicker::handleZone(const Hit& h) {
        switch (h.zone) {
        case Zone::PrevYear:  shownYear_ -= 1; break;
        case Zone::NextYear:  shownYear_ += 1; break;

        case Zone::PrevMonth:
            shownMonth_ -= 1;
            if (shownMonth_ < 1) { shownMonth_ = 12; shownYear_ -= 1; }
            break;
        case Zone::NextMonth:
            shownMonth_ += 1;
            if (shownMonth_ > 12) { shownMonth_ = 1; shownYear_ += 1; }
            break;

        case Zone::Day: {
            if (h.year > 0 && h.month > 0) {
                draft_.year  = h.year;
                draft_.month = h.month;
                shownYear_   = h.year;
                shownMonth_  = h.month;
            }
            else {
                draft_.year  = shownYear_;
                draft_.month = shownMonth_;
            }
            draft_.day = h.day;
            break;
        }

        case Zone::HourUp:     draft_.hour = (draft_.hour + 1) % 24; break;
        case Zone::HourDown:   draft_.hour = (draft_.hour + 23) % 24; break;
        case Zone::MinuteUp:   draft_.minute = (draft_.minute + 1) % 60; break;
        case Zone::MinuteDown: draft_.minute = (draft_.minute + 59) % 60; break;
        case Zone::SecondUp:   draft_.second = (draft_.second + 1) % 60; break;
        case Zone::SecondDown: draft_.second = (draft_.second + 59) % 60; break;

        case Zone::Today:
            SelectToday();
            return;

        case Zone::Confirm:
            commitDraft();
            return;

        case Zone::Cancel:
            cancelDraft();
            return;

        case Zone::None:
        default:
            return;
        }

        ClampDateTime(draft_);
        requestRepaint();
    }

    void DateTimePicker::notifyChanged() {
        if (onChanged_) onChanged_(*this, value_);
    }

    void DateTimePicker::commitDraft() {
        DateTime v = draft_;
        ClampDateTime(v);
        if (Less(v, minValue_)) v = minValue_;
        if (Less(maxValue_, v)) v = maxValue_;
        ClampDateTime(v);

        value_ = v;
        draft_ = v;
        shownYear_ = v.year;
        shownMonth_ = v.month;

        closePopup();
        markMeasureDirty();
        notifyChanged();

        if (onCommitted_)     onCommitted_(*this, value_);
        if (onCommittedSelf_) onCommittedSelf_(*this);
    }

    void DateTimePicker::cancelDraft() {
        draft_ = value_;
        shownYear_ = value_.year;
        shownMonth_ = value_.month;
        closePopup();
    }

    bool DateTimePicker::OnMouseRelease(MouseEvent& e) {
        if (popupPressedInFace_) {
            popupPressedInFace_ = false;
            if (!Widget::HitTest(e.x, e.y)) { e.accept(); return true; }
            openPopup();
            e.accept();
            return true;
        }
        if (pressedInPopup_) return onPopupRelease(e);
        return false;
    }

    bool DateTimePicker::OnMouseMove(MouseEvent& e) {
        if (popupOverlayVisible()) return false;
        return onPopupMove(e);
    }

    bool DateTimePicker::OnMouseWheel(MouseEvent& e) {
        if (popupOverlayVisible()) return false;
        return onPopupWheel(e);
    }

    void DateTimePicker::OnMouseLeave() {
        onPopupLeave();
    }

    bool DateTimePicker::OnKeyPress(KeyEvent& e) {
        if (!isEnabled()) return false;

        if (!open_) {
            switch (e.key) {
            case Key::Enter:
            case Key::Space:
            case Key::DownArrow:
                openPopup();
                e.accept();
                return true;
            default:
                return false;
            }
        }

        switch (e.key) {
        case Key::Escape:
            cancelDraft();
            e.accept();
            return true;

        case Key::Enter:
            commitDraft();
            e.accept();
            return true;

        case Key::LeftArrow: {
            draft_.day -= 1;
            if (draft_.day < 1) {
                shownMonth_ -= 1;
                if (shownMonth_ < 1) { shownMonth_ = 12; shownYear_ -= 1; }
                draft_.year = shownYear_;
                draft_.month = shownMonth_;
                draft_.day = DaysInMonth(shownYear_, shownMonth_);
            }
            ClampDateTime(draft_);
            requestRepaint();
            e.accept();
            return true;
        }

        case Key::RightArrow: {
            draft_.day += 1;
            const int dim = DaysInMonth(draft_.year, draft_.month);
            if (draft_.day > dim) {
                shownMonth_ += 1;
                if (shownMonth_ > 12) { shownMonth_ = 1; shownYear_ += 1; }
                draft_.year = shownYear_;
                draft_.month = shownMonth_;
                draft_.day = 1;
            }
            ClampDateTime(draft_);
            requestRepaint();
            e.accept();
            return true;
        }

        case Key::UpArrow:
            if (hasModifier(e.modifiers, KeyboardModifiers::Shift)) {
                draft_.year += 1;
                shownYear_ = draft_.year;
            }
            else {
                int remain = 7;
                while (remain > 0) {
                    const int dim = DaysInMonth(draft_.year, draft_.month);
                    const int room = dim - draft_.day;
                    if (remain <= room) { draft_.day += remain; remain = 0; }
                    else {
                        remain -= room + 1;
                        draft_.day = 1;
                        draft_.month += 1;
                        if (draft_.month > 12) { draft_.month = 1; draft_.year += 1; }
                    }
                }
                shownYear_  = draft_.year;
                shownMonth_ = draft_.month;
            }
            ClampDateTime(draft_);
            requestRepaint();
            e.accept();
            return true;

        case Key::DownArrow:
            if (hasModifier(e.modifiers, KeyboardModifiers::Shift)) {
                draft_.year -= 1;
                shownYear_ = draft_.year;
            }
            else {
                int remain = 7;
                while (remain > 0) {
                    if (draft_.day > remain) { draft_.day -= remain; remain = 0; }
                    else {
                        remain -= draft_.day;
                        draft_.month -= 1;
                        if (draft_.month < 1) { draft_.month = 12; draft_.year -= 1; }
                        draft_.day = DaysInMonth(draft_.year, draft_.month);
                    }
                }
                shownYear_  = draft_.year;
                shownMonth_ = draft_.month;
            }
            ClampDateTime(draft_);
            requestRepaint();
            e.accept();
            return true;

        case Key::PageUp:
            shownMonth_ -= 1;
            if (shownMonth_ < 1) { shownMonth_ = 12; shownYear_ -= 1; }
            requestRepaint();
            e.accept();
            return true;

        case Key::PageDown:
            shownMonth_ += 1;
            if (shownMonth_ > 12) { shownMonth_ = 1; shownYear_ += 1; }
            requestRepaint();
            e.accept();
            return true;

        case Key::Home:
            SelectToday();
            e.accept();
            return true;

        default:
            return false;
        }
    }

    void DateTimePicker::OnFocusOut() {
        if (open_) cancelDraft();
    }

}
