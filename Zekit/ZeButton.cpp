#include "pch.h"

#include <algorithm>

#include "include/ZeButton.h"
#include "include/Zekit.h"
#include "ZeFrameCollector.h"

namespace Kit {

    Button::Button(std::string text)
        : TextWidget(std::move(text)) {
        ApplyTheme(Themes::Light());

        SetDefaultSize(160.0f, 36.0f);
    }

    void Button::SetOnPress(Action handler) {
        onPress_ = std::move(handler);
    }

    void Button::SetOnPress(SelfAction handler) {
        selfOnPress_ = std::move(handler);
    }

    void Button::SetOnRelease(Action handler) {
        onRelease_ = std::move(handler);
    }

    void Button::SetOnRelease(SelfAction handler) {
        selfOnRelease_ = std::move(handler);
    }

    void Button::SetOnClick(Action handler) {
        onClick_ = std::move(handler);
    }

    void Button::SetOnClick(SelfAction handler) {
        selfOnClick_ = std::move(handler);
    }

    void Button::Activate() {
        if (!isEnabled()) return;
        if (onClick_)     onClick_();
        if (selfOnClick_) selfOnClick_(*this);
    }

    void Button::SetImage(std::string path) {
        markStyled();
        iconPath_ = std::move(path);
        RequestWindowRedraw();
    }

    void Button::SetIconPosition(IconPosition pos) {
        markStyled();
        iconPos_ = pos;
        RequestWindowRedraw();
    }

    void Button::SetIconSize(float w, float h) {
        markStyled();
        iconW_ = (w > 0.0f) ? w : 0.0f;
        iconH_ = (h > 0.0f) ? h : 0.0f;
        RequestWindowRedraw();
    }

    void Button::SetIconSpacing(float gap) {
        markStyled();
        iconGap_ = (gap > 0.0f) ? gap : 0.0f;
        RequestWindowRedraw();
    }

    void Button::SetIconDrawMode(BgImageDrawMode mode) {
        markStyled();
        iconMode_ = mode;
        RequestWindowRedraw();
    }

    void Button::SetIconTint(const Color4f& c) {
        markStyled();
        iconTint_ = c;
        RequestWindowRedraw();
    }

    bool Button::contentBlockSize(float& w, float& h) const {
        const FontDesc& font = textStyle_.font;

        bool hasText = !text_.empty();
        TextMetrics tm{};
        if (hasText) {
            if (Window* win = window()) {
                tm = win->MeasureText(text_, font);
                if (tm.width <= 0.0f && tm.height <= 0.0f) hasText = false;
            }
            else {
                hasText = false;
            }
        }

        bool hasIcon = !iconPath_.empty() && iconPos_ != IconPosition::None
            && iconW_ > 0.0f && iconH_ > 0.0f;

        if (!hasIcon && !hasText) { w = 0.0f; h = 0.0f; return false; }

        const bool vertical = hasIcon && hasText
            && (iconPos_ == IconPosition::Top || iconPos_ == IconPosition::Bottom);
        const float gap = (hasIcon && hasText) ? iconGap_ : 0.0f;

        const float mainIcon = vertical ? iconH_ : iconW_;
        const float mainText = vertical ? tm.height : tm.width;
        const float crossIcon = vertical ? iconW_ : iconH_;
        const float crossText = vertical ? tm.width : tm.height;

        const float mainTotal = (hasIcon && hasText) ? (mainIcon + gap + mainText)
            : (hasIcon ? mainIcon : mainText);
        const float crossTotal = std::max(crossIcon, crossText);

        w = vertical ? crossTotal : mainTotal;
        h = vertical ? mainTotal : crossTotal;
        return true;
    }

    float Button::hintWidth() const {
        if (prefWidth() > 0.0f) return prefWidth();

        float bw = 0.0f, bh = 0.0f;
        if (!contentBlockSize(bw, bh)) return Widget::hintWidth();

        return bw + box_.padding.left + box_.padding.right
                  + box_.border.width[1] + box_.border.width[3];
    }

    float Button::hintHeight() const {
        if (prefHeight() > 0.0f) return prefHeight();

        float bw = 0.0f, bh = 0.0f;
        if (!contentBlockSize(bw, bh)) return Widget::hintHeight();

        return bh + box_.padding.top + box_.padding.bottom
                  + box_.border.width[0] + box_.border.width[2];
    }

    void Button::paintContent(FrameCollector& fc) const {
        const ColorSet cs = resolvedTextColors(currentState());
        const FontDesc& font = textStyle_.font;

        float cx = 0.0f, cy = 0.0f, cw = 0.0f, ch = 0.0f;
        contentRect(cx, cy, cw, ch);
        if (cw <= 0.0f || ch <= 0.0f) return;

        bool hasIcon = !iconPath_.empty() && iconPos_ != IconPosition::None;
        float iw = 0.0f, ih = 0.0f;
        if (hasIcon) {
            if (fc.ImageSize(iconPath_, iw, ih)) {
                if (iconW_ > 0.0f && iconH_ > 0.0f) {
                    iw = iconW_;
                    ih = iconH_;
                }
            }
            else {
                hasIcon = false;
            }
        }

        bool hasText = !text_.empty();
        TextMetrics tm{};
        if (hasText) {
            tm = fc.MeasureText(text_, font);
            if (tm.width <= 0.0f && tm.height <= 0.0f) hasText = false;
        }

        if (!hasIcon && !hasText) return;

        const bool  vertical = hasIcon && hasText
            && (iconPos_ == IconPosition::Top || iconPos_ == IconPosition::Bottom);
        const float gap = (hasIcon && hasText) ? iconGap_ : 0.0f;

        const float mainIcon = vertical ? ih : iw;
        const float mainText = vertical ? tm.height : tm.width;
        const float crossIcon = vertical ? iw : ih;
        const float crossText = vertical ? tm.width : tm.height;

        const float mainTotal = (hasIcon && hasText) ? (mainIcon + gap + mainText)
            : (hasIcon ? mainIcon : mainText);
        const float crossTotal = std::max(crossIcon, crossText);

        float blockX = 0.0f, blockY = 0.0f;
        if (vertical) {
            blockX = cx + alignOffsetX(textStyle_.hAlign, cw - crossTotal);
            blockY = cy + alignOffsetY(textStyle_.vAlign, ch - mainTotal);
        }
        else {
            blockX = cx + alignOffsetX(textStyle_.hAlign, cw - mainTotal);
            blockY = cy + alignOffsetY(textStyle_.vAlign, ch - crossTotal);
        }

        float iconX = 0.0f, iconY = 0.0f, textX = 0.0f, textTop = 0.0f;
        if (hasIcon && hasText) {
            switch (iconPos_) {
            case IconPosition::Left:
                iconX = blockX;
                iconY = blockY + (crossTotal - ih) * 0.5f;
                textX = blockX + iw + gap;
                textTop = blockY + (crossTotal - tm.height) * 0.5f;
                break;
            case IconPosition::Right:
                textX = blockX;
                textTop = blockY + (crossTotal - tm.height) * 0.5f;
                iconX = blockX + tm.width + gap;
                iconY = blockY + (crossTotal - ih) * 0.5f;
                break;
            case IconPosition::Top:
                iconX = blockX + (crossTotal - iw) * 0.5f;
                iconY = blockY;
                textX = blockX + (crossTotal - tm.width) * 0.5f;
                textTop = blockY + ih + gap;
                break;
            case IconPosition::Bottom:
                textX = blockX + (crossTotal - tm.width) * 0.5f;
                textTop = blockY;
                iconX = blockX + (crossTotal - iw) * 0.5f;
                iconY = blockY + tm.height + gap;
                break;
            case IconPosition::None:
                break;
            }
        }
        else if (hasIcon) {
            iconX = blockX;
            iconY = blockY;
        }
        else {
            textX = blockX;
            textTop = blockY;
        }

        if (hasIcon) {
            Color4f tint = iconTint_;
            tint.a *= box_.opacity;
            fc.AddImage(iconPath_, iconX, iconY, iw, ih, iconMode_,
                BgImageHAlign::Center, BgImageVAlign::Middle, tint);
        }

        if (hasText)
            paintTextAt(fc, textX, textTop, tm, cs);
    }

    bool Button::OnMousePress(MouseEvent& e) {
        if (!isEnabled()) return false;
        if (e.button != MouseButton::Left) return false;
        pressed_ = true;
        if (onPress_)     onPress_();
        if (selfOnPress_) selfOnPress_(*this);
        return true;
    }

    bool Button::OnMouseRelease(MouseEvent& e) {
        if (e.button != MouseButton::Left) return false;
        const bool wasPressed = pressed_;
        pressed_ = false;

        if (!wasPressed) return false;

        if (onRelease_)     onRelease_();
        if (selfOnRelease_) selfOnRelease_(*this);
        Activate();
        return true;
    }

}
