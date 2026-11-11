
#include "pch.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include "include/ZeGroupPanel.h"
#include "include/Zekit.h"
#include "ZeDrawUtil.h"

namespace Kit {

    namespace {
    }

    GroupPanel::GroupPanel(std::string title)
        : title_(std::move(title)) {
        ApplyTheme(Themes::Light());

        SetDirection(FlexDirection::Column);
        SetAlign(AlignItems::Stretch);
        syncPadding();

        SetDefaultSize(240.0f, 120.0f);
    }

    void GroupPanel::SetTitle(std::string title) {
        title_ = std::move(title);
        syncPadding();
        Invalidate();
        RequestWindowRedraw();
    }

    void GroupPanel::SetTitleHeight(float h) {
        titleHeight_ = (h > 0.0f) ? h : 0.0f;
        syncPadding();
        Invalidate();
        RequestWindowRedraw();
    }

    void GroupPanel::SetFrameWidth(float px) {
        frameWidth_ = (px > 0.0f) ? px : 0.0f;
        markStyled();
        RequestWindowRedraw();
    }

    void GroupPanel::SetFrameColor(const Color4f& c) {
        frameColor_ = c;
        markStyled();
        RequestWindowRedraw();
    }

    void GroupPanel::SetTitleGap(float px) {
        titleGap_ = (px > 0.0f) ? px : 0.0f;
        markStyled();
        RequestWindowRedraw();
    }

    void GroupPanel::ApplyTheme(const Theme& t) {
        const GroupPanelStyle& gs = t.groupPanel;
        background_ = gs.background;
        frameColor_ = gs.borderColor;
        frameWidth_ = std::max(0.0f, gs.borderWidth);
        radius_ = std::max(0.0f, gs.radius);
        padLeft_ = padTop_ = padRight_ = padBottom_ = gs.padding;
        titleHeight_ = (gs.titleHeight > 0.0f) ? gs.titleHeight : 0.0f;
        titleGap_ = (gs.titlePaddingX >= 0.0f) ? gs.titlePaddingX : 0.0f;
        titleStyle_ = gs.title;

        Panel::SetBackground(Color4f{ 0.0f, 0.0f, 0.0f, 0.0f });
        Panel::SetBorder(0.0f, Color4f{ 0.0f, 0.0f, 0.0f, 0.0f });

        syncPadding();
    }

    void GroupPanel::syncPadding() {
        const float border = frameWidth_;
        const float top = border + padTop_ + (hasTitle() ? titleHeight_ : 0.0f);
        Panel::SetPadding(border + padLeft_, top, border + padRight_, border + padBottom_);
    }

    void GroupPanel::DoLayout() {
        syncPadding();
        Panel::DoLayout();
    }

    float GroupPanel::hintWidth() const {
        if (prefWidth() > 0.0f) return prefWidth();
        return Panel::hintWidth();
    }

    float GroupPanel::hintHeight() const {
        if (prefHeight() > 0.0f) return prefHeight();
        return Panel::hintHeight();
    }

    void GroupPanel::Paint(FrameCollector& fc) const {
        const float w = width();
        const float h = height();
        if (w <= 0.0f || h <= 0.0f) return;

        const float x0 = x(), y0 = y();
        const float bw = std::max(frameWidth_, 0.0f);
        const float r = std::min(radius_, std::min(w, h) * 0.5f);

        drawutil::PaintRoundedRect(fc, x0, y0, w, h, r, background_);

        if (bw <= 0.0f || frameColor_.a <= 0.0f) return;

        const float inset = bw * 0.5f;
        const float bx0 = x0 + inset;
        const float by0 = y0 + inset;
        const float bx1 = x0 + w - inset;
        const float by1 = y0 + h - inset;
        const float rr = std::max(0.0f, r - inset);

        Color4f bc = frameColor_;

        float gapStart = 0.0f, gapEnd = 0.0f;
        bool hasGap = false;
        const float tx0 = x0 + std::max(padLeft_, 10.0f) + bw;
        float textW = 0.0f;
        if (hasTitle() && titleHeight_ > 0.0f) {
            const TextMetrics tm = fc.MeasureText(title_, titleStyle_.font);
            textW = tm.width;
            if (textW > 0.0f) {
                gapStart = tx0 - titleGap_;
                gapEnd = tx0 + textW + titleGap_;
                hasGap = true;
            }
        }

        if (hasGap) {
            if (bx0 + rr < gapStart)
                drawutil::PaintThickSegment(fc, bx0 + rr, by0, gapStart, by0, bw, bc);
            if (gapEnd < bx1 - rr)
                drawutil::PaintThickSegment(fc, gapEnd, by0, bx1 - rr, by0, bw, bc);
        }
        else {
            drawutil::PaintThickSegment(fc, bx0 + rr, by0, bx1 - rr, by0, bw, bc);
        }

        drawutil::PaintThickSegment(fc, bx1, by0 + rr, bx1, by1 - rr, bw, bc);
        drawutil::PaintThickSegment(fc, bx0, by0 + rr, bx0, by1 - rr, bw, bc);
        drawutil::PaintThickSegment(fc, bx0 + rr, by1, bx1 - rr, by1, bw, bc);

        drawutil::PaintArc(fc, bx1 - rr, by0 + rr, rr, -drawutil::kPi * 0.5f, 0.0f, bw, bc);
        drawutil::PaintArc(fc, bx1 - rr, by1 - rr, rr, 0.0f, drawutil::kPi * 0.5f, bw, bc);
        drawutil::PaintArc(fc, bx0 + rr, by1 - rr, rr, drawutil::kPi * 0.5f, drawutil::kPi, bw, bc);
        drawutil::PaintArc(fc, bx0 + rr, by0 + rr, rr, drawutil::kPi, drawutil::kPi * 1.5f, bw, bc);

        if (!hasGap || textW <= 0.0f) return;

        Color4f fg = titleStyle_.colors.get(WidgetState::Normal).text;

        const TextMetrics tm = fc.MeasureText(title_, titleStyle_.font);
        const float ty = y0 + (titleHeight_ - tm.height) * 0.5f;
        if (ty + tm.height < y0) return;
        fc.AddText(title_, titleStyle_.font, tx0, ty + tm.ascent, fg);
    }

}
