
#include "pch.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "include/ZeSteps.h"
#include "include/Zekit.h"

#include "ZeDrawUtil.h"
#include "ZeFrameCollector.h"

namespace Kit {

    namespace {
        constexpr float kDefaultCircle = 24.0f;
        constexpr float kMinItemWidth = 56.0f;
        constexpr float kDigitFontRatio = 0.5f;
        constexpr float kCheckSizeRatio = 0.22f;
        constexpr float kCheckThickness = 1.8f;
        constexpr float kLineThickness = 2.0f;
        constexpr float kRingThickness = 1.5f;
    }

    Steps::Steps() {
        SetAlign(AlignItems::Center);
        SetFocusable(true);
        SetDefaultSize(360.0f, 48.0f);
    }

    int Steps::AddStep(std::string title) {
        return AddItem(std::move(title));
    }

    void Steps::SetSteps(const std::vector<std::string>& titles) {
        ItemStripBase::SetItems(titles);
        RequestWindowRedraw();
    }

    void Steps::SetStepTitle(int index, std::string title) {
        SetItemLabel(index, std::move(title));
        RequestWindowRedraw();
    }

    void Steps::SetCircleSize(float diameter) {
        circleSize_ = (diameter > 0.0f) ? diameter : 0.0f;
        Invalidate();
        RequestWindowRedraw();
    }

    void Steps::SetTitleGap(float gap) {
        titleGap_ = (gap > 0.0f) ? gap : 0.0f;
        RequestWindowRedraw();
    }

    float Steps::resolvedCircle() const {
        if (circleSize_ > 0.0f) return circleSize_;
        if (styleTheme_.itemHeight > 0.0f) return styleTheme_.itemHeight;
        return kDefaultCircle;
    }

    float Steps::measureStripItem(int index) const {
        (void)index;
        const int n = static_cast<int>(itemCount());
        if (n <= 0) return kMinItemWidth;

        const float avail = contentAvailableW();
        if (avail <= 0.0f) return kMinItemWidth;

        return std::max(kMinItemWidth, avail / static_cast<float>(n));
    }

    float Steps::separatorWidth() const {
        return resolvedCircle() * 0.5f;
    }

    void Steps::paintStripSeparator(FrameCollector& fc, const ItemRect& r) const {
        if (r.w <= 0.0f || r.h <= 0.0f) return;

        int leftIndex = -1;
        for (int i = 0; i < static_cast<int>(itemCount()); ++i) {
            ItemRect ir;
            if (!itemRect(i, ir)) continue;
            if (std::abs(ir.x + ir.w - r.x) < 0.5f) { leftIndex = i; break; }
        }

        const bool done = (leftIndex >= 0) && (leftIndex < currentIndex());

        Color4f col = done ? styleTheme_.itemActiveColor : styleTheme_.separatorColor;
        if (col.a <= 0.0f) {
            col = done ? styleTheme_.itemActive.colors.get(WidgetState::Normal).text
                       : styleTheme_.item.colors.get(WidgetState::Normal).text;
            col.a *= done ? 1.0f : 0.35f;
        }

        const float cy = r.y + r.h * 0.5f;
        drawutil::PaintHLine(fc, r.x, cy - kLineThickness * 0.5f, r.w,
            kLineThickness, col);
    }

    void Steps::circleRect(const ItemRect& r, float& cx, float& cy, float& d) const {
        d = resolvedCircle();
        cx = r.x + d * 0.5f;
        cy = r.y + r.h * 0.5f;
    }

    void Steps::paintStripItem(FrameCollector& fc, int index, const ItemRect& r) const {
        if (index < 0 || index >= static_cast<int>(itemCount())) return;
        if (r.w <= 0.0f || r.h <= 0.0f) return;

        const int cur = currentIndex();
        const bool done = (index < cur);
        const bool active = (index == cur);
        const bool enabled = isItemEnabled(index);

        float cx = 0.0f, cy = 0.0f, d = 0.0f;
        circleRect(r, cx, cy, d);
        const float radius = d * 0.5f;

        Color4f accent = styleTheme_.itemActiveColor;
        if (accent.a <= 0.0f) accent = styleTheme_.itemActive.colors.get(WidgetState::Normal).text;

        Color4f normalText = styleTheme_.item.colors.get(WidgetState::Normal).text;
        Color4f muted = (styleTheme_.separatorColor.a > 0.0f) ? styleTheme_.separatorColor : normalText;

        Color4f circleFill{ 0.0f, 0.0f, 0.0f, 0.0f };
        Color4f circleRing{ 0.0f, 0.0f, 0.0f, 0.0f };
        Color4f markColor = accent;

        if (!enabled) {
            circleRing = muted;
            markColor = muted;
        }
        else if (done) {
            circleFill = accent;
            markColor = Color4f::White();
        }
        else if (active) {
            circleFill = accent;
            markColor = Color4f::White();
        }
        else {
            circleRing = muted;
        }

        if (circleRing.a > 0.0f)
            drawutil::PaintCircle(fc, cx, cy, radius, circleRing);
        if (circleFill.a > 0.0f) {
            const float inner = (circleRing.a > 0.0f) ? std::max(0.0f, radius - kRingThickness) : radius;
            drawutil::PaintCircle(fc, cx, cy, inner, circleFill);
        }

        if (done) {
            const float s = d * kCheckSizeRatio;
            const float x0 = cx - s, y0 = cy;
            const float x1 = cx - s * 0.15f, y1 = cy + s * 0.75f;
            const float x2 = cx + s, y2 = cy - s * 0.7f;

            const auto stroke = [&](float ax, float ay, float bx, float by) {
                const float dx = bx - ax, dy = by - ay;
                const float len = std::sqrt(dx * dx + dy * dy);
                if (len <= 0.0f) return;
                const float nx = -dy / len * kCheckThickness * 0.5f;
                const float ny = dx / len * kCheckThickness * 0.5f;
                const std::vector<Vec2> q{
                    Vec2{ ax + nx, ay + ny },
                    Vec2{ bx + nx, by + ny },
                    Vec2{ bx - nx, by - ny },
                    Vec2{ ax - nx, ay - ny },
                };
                fc.AddShape(PolygonShape(q, true), markColor, 0.f, 0.f, 0.f, 0.f, 0.f);
            };
            stroke(x0, y0, x1, y1);
            stroke(x1, y1, x2, y2);
        }
        else {
            const std::string num = std::to_string(index + 1);
            FontDesc font = styleTheme_.item.font;
            font.pixelSize = static_cast<unsigned>(std::max(9.0f, d * kDigitFontRatio));
            const TextMetrics tm = fc.MeasureText(num, font);
            if (tm.width > 0.0f) {
                const float tx = cx - tm.width * 0.5f;
                const float ty = cy - tm.height * 0.5f;
                fc.AddText(num, font, tx, ty + tm.ascent, markColor);
            }
        }

        const std::string& title = itemLabel(index);
        if (title.empty()) return;

        const FontDesc& font = itemTextStyle(index).font;
        const float textX = r.x + d + titleGap_;
        const float textW = std::max(0.0f, r.x + r.w - textX - 2.0f);
        if (textW <= 0.0f) return;

        Color4f fg = normalText;
        if (!enabled)                        fg = muted;
        else if (active)                     fg = styleTheme_.itemActive.colors.get(WidgetState::Normal).text;
        if (fg.a <= 0.0f)                    fg = normalText;

        std::string shown = title;
        TextMetrics tm = fc.MeasureText(shown, font);
        if (tm.width > textW) {
            shown = drawutil::Ellipsize(fc, title, font, textW);
            tm = fc.MeasureText(shown, font);
        }
        if (tm.width <= 0.0f) return;

        const float ty = r.y + (r.h - tm.height) * 0.5f;
        fc.AddText(shown, font, textX, ty + tm.ascent, fg);
    }

    void Steps::OnCurrentChangedByUser(int index) {
        if (index < 0 || index >= static_cast<int>(itemCount())) return;

        if (onStepChanged_)     onStepChanged_(*this, index);
        if (onStepChangedSelf_) onStepChangedSelf_(*this);
    }

}
