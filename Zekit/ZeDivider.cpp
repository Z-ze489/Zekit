
#include "pch.h"

#include <algorithm>
#include <string>
#include <utility>

#include "include/ZeDivider.h"
#include "include/Zekit.h"
#include "ZeDrawUtil.h"

namespace Kit {

    namespace {
        constexpr float kFallbackLength = 1.0f;
        constexpr float kDefaultLength = 120.0f;
    }

    Divider::Divider(DividerOrientation orientation)
        : orientation_(orientation) {
        ApplyTheme(Themes::Light());
        SetFocusable(false);
        SetDefaultSize(120.0f, 1.0f);
    }

    void Divider::SetOrientation(DividerOrientation o) {
        if (orientation_ == o) return;
        orientation_ = o;
        notifyLayoutInputChanged();
        RequestWindowRedraw();
    }

    void Divider::SetColor(const Color4f& c) {
        color_ = c;
        markStyled();
        RequestWindowRedraw();
    }

    void Divider::SetThickness(float px) {
        thickness_ = (px > 0.0f) ? px : 1.0f;
        markStyled();
        notifyLayoutInputChanged();
        RequestWindowRedraw();
    }

    void Divider::SetGap(float gap) {
        gap_ = (gap > 0.0f) ? gap : 0.0f;
        markStyled();
        RequestWindowRedraw();
    }

    void Divider::SetLabel(std::string text) {
        label_ = std::move(text);
        markStyled();
        notifyLayoutInputChanged();
        RequestWindowRedraw();
    }

    void Divider::ApplyTheme(const Theme& t) {
        const DividerStyle& ds = t.divider;
        color_ = ds.color;
        thickness_ = (ds.thickness > 0.0f) ? ds.thickness : 1.0f;
        gap_ = (ds.gap > 0.0f) ? ds.gap : 0.0f;
        labelStyle_ = ds.label;

        box_.colors = MultiStateColors{};
        box_.border = Border{};
        box_.radiusMode = RadiusMode::None;
        box_.customRadius = 0.0f;
        box_.padding = Padding{};
        box_.shadow.enabled = false;
        box_.outline.enabled = false;
        box_.opacity = 1.0f;
    }

    float Divider::hintWidth() const {
        if (prefWidth() > 0.0f) return prefWidth();
        if (!isHorizontal()) return thickness_;

        if (label_.empty()) return kDefaultLength;
        if (const Window* w = window()) {
            const TextMetrics tm =
                const_cast<Window*>(w)->MeasureText(label_, labelStyle_.font);
            if (tm.width > 0.0f) return tm.width + gap_ * 2.0f;
        }
        return kDefaultLength;
    }

    float Divider::hintHeight() const {
        if (prefHeight() > 0.0f) return prefHeight();
        if (!isHorizontal()) return kDefaultLength;

        if (label_.empty()) return std::max(thickness_, kFallbackLength);
        if (const Window* w = window()) {
            const TextMetrics tm =
                const_cast<Window*>(w)->MeasureText(label_, labelStyle_.font);
            if (tm.height > 0.0f) return std::max(thickness_, tm.height);
        }
        return std::max(thickness_, kFallbackLength);
    }

    void Divider::paintContent(FrameCollector& fc) const {
        const float w = width();
        const float h = height();
        if (w <= 0.0f || h <= 0.0f) return;

        const float th = std::max(thickness_, 1.0f);

        if (!isHorizontal()) {
            const float cx = x() + w * 0.5f - th * 0.5f;
            drawutil::PaintVLine(fc, cx, y(), h, th, color_);
            return;
        }

        const float cy = y() + h * 0.5f - th * 0.5f;

        if (label_.empty()) {
            drawutil::PaintHLine(fc, x(), cy, w, th, color_);
            return;
        }

        const TextMetrics tm = fc.MeasureText(label_, labelStyle_.font);
        if (tm.width <= 0.0f) {
            drawutil::PaintHLine(fc, x(), cy, w, th, color_);
            return;
        }

        const float textW = tm.width;
        const float blockW = textW + gap_ * 2.0f;
        const float startX = x() + (w - blockW) * 0.5f;
        const float leftW = std::max(0.0f, startX - x());
        const float rightX = startX + blockW;
        const float rightW = std::max(0.0f, (x() + w) - rightX);

        if (leftW > 0.0f) drawutil::PaintHLine(fc, x(), cy, leftW, th, color_);
        if (rightW > 0.0f) drawutil::PaintHLine(fc, rightX, cy, rightW, th, color_);

        const Color4f fg = labelStyle_.colors.get(WidgetState::Normal).text;
        drawutil::PaintTextCentered(fc, label_, labelStyle_.font,
            startX + gap_, y(), textW, h, fg);
    }

}
