
#include "pch.h"

#include <algorithm>

#include "include/ZePanel.h"

#include "ZeFrameCollector.h"
#include "ZeRoundedRectShape.h"

namespace Kit {

    void Panel::DoLayout() {
        layoutLinear(direction() == FlexDirection::Column);
    }

    void Panel::SetBackground(const Color4f& c) {
        background_ = c;
        markStyled();
        RequestWindowRedraw();
    }

    void Panel::SetRadius(float radius) {
        radius_ = (radius > 0.0f) ? radius : 0.0f;
        markStyled();
        RequestWindowRedraw();
    }

    void Panel::SetBorder(float width, const Color4f& color) {
        borderWidth_ = (width > 0.0f) ? width : 0.0f;
        borderColor_ = color;
        markStyled();
        RequestWindowRedraw();
    }

    void Panel::SetBorderWidth(float width) {
        borderWidth_ = (width > 0.0f) ? width : 0.0f;
        markStyled();
        RequestWindowRedraw();
    }

    void Panel::SetBorderColor(const Color4f& color) {
        borderColor_ = color;
        markStyled();
        RequestWindowRedraw();
    }

    void Panel::Paint(FrameCollector& fc) const {
        const float w = width();
        const float h = height();
        if (w <= 0.0f || h <= 0.0f) return;

        const Vec2 lo{ x(), y() };
        const Vec2 sz{ w, h };

        const auto paintRounded = [&fc](const Vec2& pos, const Vec2& size, float r, const Color4f& c) {
            fc.AddShape(RoundedRectShape(pos, size, r), c,
                pos.x + size.x * 0.5f, pos.y + size.y * 0.5f,
                size.x * 0.5f, size.y * 0.5f, r);
        };

        const bool hasBorder = (borderWidth_ > 0.0f && borderColor_.a > 0.0f);

        if (hasBorder) {
            paintRounded(lo, sz, radius_, borderColor_);

            const float iw = w - borderWidth_ * 2.0f;
            const float ih = h - borderWidth_ * 2.0f;
            if (iw > 0.0f && ih > 0.0f && background_.a > 0.0f) {
                const float ri = std::max(0.0f, radius_ - borderWidth_);
                paintRounded(Vec2{ lo.x + borderWidth_, lo.y + borderWidth_ },
                             Vec2{ iw, ih }, ri, background_);
            }
            return;
        }

        if (background_.a > 0.0f)
            paintRounded(lo, sz, radius_, background_);
    }

}
