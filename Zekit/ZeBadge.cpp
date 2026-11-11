
#include "pch.h"

#include <algorithm>
#include <string>
#include <utility>

#include "include/ZeBadge.h"
#include "include/Zekit.h"
#include "ZeDrawUtil.h"

namespace Kit {

    Badge::Badge(std::string text)
        : TextWidget(std::move(text)) {
        ApplyTheme(Themes::Light());
        SetFocusable(false);
        SetTextAlign(TextHAlign::Center, TextVAlign::Middle);
        SetDefaultSize(18.0f, 18.0f);
    }

    void Badge::SetDot(bool dot) {
        if (dot_ == dot) return;
        dot_ = dot;
        notifyLayoutInputChanged();
        RequestWindowRedraw();
    }

    void Badge::SetDotSize(float px) {
        dotSizeOverride_ = (px > 0.0f) ? px : 0.0f;
        markStyled();
        notifyLayoutInputChanged();
        RequestWindowRedraw();
    }

    void Badge::SetTone(BadgeTone tone) {
        if (tone_ == tone) return;
        tone_ = tone;
        syncTextColor();
        RequestWindowRedraw();
    }

    void Badge::SetCount(int n) {
        std::string s;
        if (n < 0) {
            s = "0";
        }
        else if (maxDisplay_ > 0 && n > maxDisplay_) {
            s = std::to_string(maxDisplay_) + "+";
        }
        else {
            s = std::to_string(n);
        }
        SetText(s);
        RequestWindowRedraw();
    }

    void Badge::SetMaxDisplay(int maxDisplay) {
        maxDisplay_ = maxDisplay;
        RequestWindowRedraw();
    }

    void Badge::SetBadgeColor(const Color4f& c) {
        bodyColorSet_ = true;
        bodyOverride_ = c;
        markStyled();
        RequestWindowRedraw();
    }

    void Badge::SetLabelColor(const Color4f& c) {
        textColorSet_ = true;
        textOverride_ = c;
        syncTextColor();
        markStyled();
        RequestWindowRedraw();
    }

    const Color4f& Badge::toneBackground() const {
        if (bodyColorSet_) return bodyOverride_;
        switch (tone_) {
        case BadgeTone::Danger:  return bgDanger_;
        case BadgeTone::Success: return bgSuccess_;
        case BadgeTone::Warning: return bgWarning_;
        case BadgeTone::Neutral: return bgNeutral_;
        case BadgeTone::Primary:
        default:                 return bgPrimary_;
        }
    }

    const Color4f& Badge::toneForeground() const {
        if (textColorSet_) return textOverride_;
        switch (tone_) {
        case BadgeTone::Danger:  return fgDanger_;
        case BadgeTone::Success: return fgSuccess_;
        case BadgeTone::Warning: return fgWarning_;
        case BadgeTone::Neutral: return fgNeutral_;
        case BadgeTone::Primary:
        default:                 return fgPrimary_;
        }
    }

    void Badge::syncTextColor() {
        textStyle_.colors.get(WidgetState::Normal).text = toneForeground();
    }

    void Badge::ApplyTheme(const Theme& t) {
        const BadgeStyle& bs = t.badge;

        textStyle_ = bs.text;
        themeDotSize_ = (bs.dotSize > 0.0f) ? bs.dotSize : 8.0f;
        minWidth_ = bs.minWidth;
        fixedHeight_ = bs.height;
        paddingX_ = bs.paddingX;
        radius_ = bs.radius;

        bgPrimary_ = bs.primaryBackground;
        bgDanger_ = bs.dangerBackground;
        bgSuccess_ = bs.successBackground;
        bgWarning_ = bs.warningBackground;
        bgNeutral_ = bs.neutralBackground;
        fgPrimary_ = bs.primaryText;
        fgDanger_ = bs.dangerText;
        fgSuccess_ = bs.successText;
        fgWarning_ = bs.warningText;
        fgNeutral_ = bs.neutralText;

        box_.colors = MultiStateColors{};
        box_.border = Border{};
        box_.padding = Padding{};
        box_.radiusMode = RadiusMode::None;
        box_.shadow.enabled = false;
        box_.outline.enabled = false;
        box_.opacity = 1.0f;

        syncTextColor();
    }

    float Badge::hintWidth() const {
        if (prefWidth() > 0.0f) return prefWidth();

        if (dot_) {
            return (dotSizeOverride_ > 0.0f) ? dotSizeOverride_ : themeDotSize_;
        }

        float textW = 0.0f;
        if (const Window* w = window()) {
            textW = const_cast<Window*>(w)->MeasureText(text_, textStyle_.font).width;
        }
        return std::max(minWidth_, textW + paddingX_ * 2.0f);
    }

    float Badge::hintHeight() const {
        if (prefHeight() > 0.0f) return prefHeight();
        if (dot_) return (dotSizeOverride_ > 0.0f) ? dotSizeOverride_ : themeDotSize_;

        float textH = 0.0f;
        if (const Window* w = window()) {
            textH = const_cast<Window*>(w)->MeasureText(std::string_view("Ag"), textStyle_.font).height;
        }
        return std::max(fixedHeight_, textH);
    }

    void Badge::paintContent(FrameCollector& fc) const {
        const float w = width();
        const float h = height();
        if (w <= 0.0f || h <= 0.0f) return;

        Color4f bg = toneBackground();
        bg.a *= box_.opacity;

        if (dot_) {
            const float d = std::min(w, h);
            drawutil::PaintCircle(fc, x() + w * 0.5f, y() + h * 0.5f, d * 0.5f, bg);
            return;
        }

        drawutil::PaintRoundedRect(fc, x(), y(), w, h, radius_, bg);

        TextWidget::paintContent(fc);
    }

}
