
#include "pch.h"

#include <algorithm>
#include <cstddef>
#include <vector>

#include "include/ZeToolBar.h"
#include "include/Zekit.h"

#include "ZeDrawUtil.h"
#include "ZeFrameCollector.h"

namespace Kit {

    namespace {
        constexpr float kFallbackHeight = 40.0f;
        constexpr float kFallbackPadding = 6.0f;
        constexpr float kSeparatorMargin = 4.0f;
        constexpr float kSpacerBase = 8.0f;
    }

    void ToolBar::Strip::SetThickness(float thickness) {
        thickness_ = (thickness > 0.0f) ? thickness : 1.0f;
        if (vertical_) SetDefaultSize(thickness_, kSpacerBase);
        else           SetDefaultSize(kSpacerBase, thickness_);
        notifyLayoutInputChanged();
    }

    void ToolBar::Strip::ApplyTheme(const Theme& t) {
        SetThickness((t.toolbar.dividerWidth > 0.0f) ? t.toolbar.dividerWidth : 1.0f);

        if (vertical_) { SetDefaultSize(thickness_, kSpacerBase); SetAlignSelf(AlignItems::Center); }
        else           { SetDefaultSize(kSpacerBase, thickness_); SetAlignSelf(AlignItems::Center); }
    }

    void ToolBar::Strip::Paint(FrameCollector& fc) const {
        const float w = width();
        const float h = height();
        if (w <= 0.0f || h <= 0.0f) return;

        const Theme& t = fc.GetTheme();
        const Color4f col = t.toolbar.dividerColor;
        if (col.a <= 0.0f) return;

        if (vertical_) {
            const float len = h * ratio_;
            const float x0 = x() + (w - thickness_) * 0.5f;
            const float y0 = y() + (h - len) * 0.5f;
            drawutil::PaintVLine(fc, x0, y0, len, thickness_, col);
        }
        else {
            const float len = w * ratio_;
            const float x0 = x() + (w - len) * 0.5f;
            const float y0 = y() + (h - thickness_) * 0.5f;
            drawutil::PaintHLine(fc, x0, y0, len, thickness_, col);
        }
    }

    ToolBar::ToolBar() {
        SetDirection(FlexDirection::Row);
        SetAlign(AlignItems::Center);

        ApplyTheme(Themes::Light());

        SetDefaultSize(0.0f, kFallbackHeight);
    }

    int ToolBar::AddSeparator() {
        Strip strip(true);
        strip.SetMargin(kSeparatorMargin, 0.0f, kSeparatorMargin, 0.0f);
        strip.ApplyTheme(Themes::Light());
        Strip& placed = AddWidgetUnchecked(std::move(strip));
        separators_.push_back(&placed);

        Invalidate();
        notifyLayoutInputChanged();
        RequestWindowRedraw();
        return static_cast<int>(separators_.size()) - 1;
    }

    Widget& ToolBar::AddSpacer(float grow) {
        Spacer sp;
        sp.SetFlexGrow((grow > 0.0f) ? grow : 1.0f);
        sp.SetDefaultSize(kSpacerBase, 0.0f);
        Widget& placed = AddWidgetUnchecked(std::move(sp));

        Invalidate();
        notifyLayoutInputChanged();
        return placed;
    }

    void ToolBar::SetSeparatorWidth(int index, float width) {
        if (index < 0 || index >= static_cast<int>(separators_.size())) return;
        separators_[static_cast<std::size_t>(index)]->SetThickness(width);
        Invalidate();
        notifyLayoutInputChanged();
        RequestWindowRedraw();
    }

    void ToolBar::ApplyTheme(const Theme& t) {
        styleTheme_ = t.toolbar;

        const float p = (styleTheme_.padding > 0.0f) ? styleTheme_.padding : kFallbackPadding;
        SetPadding(p, p, p, p);

        for (Strip* s : separators_) {
            if (s) s->ApplyTheme(t);
        }

        Invalidate();
    }

    float ToolBar::hintWidth() const {
        if (prefWidth() > 0.0f) return prefWidth();
        return stripHintWidth(spacing());
    }

    float ToolBar::hintHeight() const {
        if (prefHeight() > 0.0f) return prefHeight();
        const float h = stripHintHeight();
        return (h > 0.0f) ? h : kFallbackHeight;
    }

    void ToolBar::Paint(FrameCollector& fc) const {
        const float w = width();
        const float h = height();
        if (w <= 0.0f || h <= 0.0f) return;

        const Color4f bg = styleTheme_.background;
        const Color4f bd = styleTheme_.borderColor;
        const float bw = styleTheme_.borderWidth;

        drawutil::PaintBox(fc, x(), y(), w, h, 0.0f, bw, bd, bg);

        if (styleTheme_.dividerColor.a > 0.0f) {
            const float dw = (styleTheme_.dividerWidth > 0.0f) ? styleTheme_.dividerWidth : 1.0f;
            drawutil::PaintHLine(fc, x(), y() + h - dw, w, dw, styleTheme_.dividerColor);
        }
    }

}
