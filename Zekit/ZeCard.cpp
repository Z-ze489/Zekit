
#include "pch.h"

#include <algorithm>
#include <string>
#include <utility>

#include "include/ZeCard.h"
#include "include/Zekit.h"

#include "ZeDrawUtil.h"

namespace Kit {

    namespace {
        constexpr float kDefaultTitleHeight = 36.0f;

        float ResolveRadius(const BoxStyle& box, const Theme& t) {
            switch (box.radiusMode) {
            case RadiusMode::None:    return 0.0f;
            case RadiusMode::Small:   return t.radiusS;
            case RadiusMode::Normal:  return t.radiusM;
            case RadiusMode::Large:   return t.radiusL;
            case RadiusMode::Capsule: return 0.0f;
            case RadiusMode::Custom:  return box.customRadius;
            }
            return 0.0f;
        }

    }

    Card::Card(std::string title) {
        ApplyTheme(Themes::Light());

        title_ = std::move(title);
        syncPadding();

        SetDirection(FlexDirection::Column);
        SetAlign(AlignItems::Stretch);

        SetDefaultSize(220.0f, 120.0f);
    }

    void Card::ApplyTheme(const Theme& t) {
        const std::string     keepPath = cardBox_.imagePath;
        const BackgroundImage keepImg = cardBox_.image;
        const BgImageDrawMode keepMode = cardBox_.drawMode;
        const BgImageHAlign   keepHA = cardBox_.hAlign;
        const BgImageVAlign   keepVA = cardBox_.vAlign;
        const Color4f         keepTint = cardBox_.tint;

        cardBox_ = t.card.box;

        cardBox_.imagePath = keepPath;
        cardBox_.image = keepImg;
        cardBox_.drawMode = keepMode;
        cardBox_.hAlign = keepHA;
        cardBox_.vAlign = keepVA;
        cardBox_.tint = keepTint;

        titleStyle_ = t.card.title;
        titleHeight_ = (titleHeightOverride_ >= 0.0f)
            ? titleHeightOverride_
            : (t.card.titleHeight > 0.0f ? t.card.titleHeight : kDefaultTitleHeight);

        shadowEnabled_ = t.card.shadow;
        shadowColor_ = t.card.shadowColor;
        shadowOffsetY_ = t.card.shadowOffsetY;
        shadowSpread_ = t.card.shadowSpread;

        const Padding& p = cardBox_.padding;
        padLeft_ = p.left;
        padTop_ = p.top;
        padRight_ = p.right;
        padBottom_ = p.bottom;

        syncPadding();
    }

    void Card::SetTitle(std::string title) {
        title_ = std::move(title);
        syncPadding();
        Invalidate();
        RequestWindowRedraw();
    }

    void Card::SetTitleHeight(float h) {
        titleHeightOverride_ = (h > 0.0f) ? h : 0.0f;
        titleHeight_ = titleHeightOverride_;
        syncPadding();
        Invalidate();
        RequestWindowRedraw();
    }

    void Card::SetShadowEnabled(bool on) {
        shadowEnabled_ = on;
        markStyled();
        RequestWindowRedraw();
    }

    void Card::SetBackgroundImage(std::string path) {
        markStyled();
        cardBox_.imagePath = std::move(path);
        cardBox_.image = BackgroundImage{};
        RequestWindowRedraw();
    }

    void Card::ClearBackgroundImage() {
        markStyled();
        cardBox_.imagePath.clear();
        cardBox_.image = BackgroundImage{};
        RequestWindowRedraw();
    }

    void Card::SetBackgroundDrawMode(BgImageDrawMode mode) {
        markStyled();
        cardBox_.drawMode = mode;
        RequestWindowRedraw();
    }

    void Card::SetBackgroundAlign(BgImageHAlign hAlign, BgImageVAlign vAlign) {
        markStyled();
        cardBox_.hAlign = hAlign;
        cardBox_.vAlign = vAlign;
        RequestWindowRedraw();
    }

    void Card::SetBackgroundTint(const Color4f& c) {
        markStyled();
        cardBox_.tint = c;
        RequestWindowRedraw();
    }

    void Card::syncPadding() {
        const float top = padTop_ + (hasTitle() ? titleHeight_ : 0.0f);
        Panel::SetPadding(padLeft_, top, padRight_, padBottom_);
    }

    void Card::contentArea(float& x, float& y, float& w, float& h) const {
        const float border = std::max({ cardBox_.border.width[Border::Top],
                                        cardBox_.border.width[Border::Right],
                                        cardBox_.border.width[Border::Bottom],
                                        cardBox_.border.width[Border::Left] });
        const float insetL = padLeft_ + border;
        const float insetT = padTop_ + border + (hasTitle() ? titleHeight_ : 0.0f);
        const float insetR = padRight_ + border;
        const float insetB = padBottom_ + border;

        x = this->x() + insetL;
        y = this->y() + insetT;
        w = std::max(0.0f, width() - insetL - insetR);
        h = std::max(0.0f, height() - insetT - insetB);
    }

    void Card::DoLayout() {
        syncPadding();
        Panel::DoLayout();
    }

    float Card::hintWidth() const {
        if (prefWidth() > 0.0f) return prefWidth();
        return Panel::hintWidth();
    }

    float Card::hintHeight() const {
        if (prefHeight() > 0.0f) return prefHeight();
        return Panel::hintHeight();
    }

    void Card::Paint(FrameCollector& fc) const {
        const float w = width();
        const float h = height();
        if (w <= 0.0f || h <= 0.0f) return;

        const float radius = ResolveRadius(cardBox_, fc.GetTheme());
        const ColorSet cs = cardBox_.colors.get(WidgetState::Normal);

        if (shadowEnabled_) {
            drawutil::PaintShadow(fc, x(), y(), w, h, radius,
                shadowColor_, 0.0f, shadowOffsetY_, shadowSpread_, cardBox_.opacity);
        }

        const float border = std::max({ cardBox_.border.width[Border::Top],
                                        cardBox_.border.width[Border::Right],
                                        cardBox_.border.width[Border::Bottom],
                                        cardBox_.border.width[Border::Left] });
        Color4f bg = cs.background;
        Color4f bd = cs.border;
        bg.a *= cardBox_.opacity;
        bd.a *= cardBox_.opacity;
        drawutil::PaintBox(fc, x(), y(), w, h, radius, border, bd, bg);

        {
            float ix = 0.0f, iy = 0.0f, iw = 0.0f, ih = 0.0f;
            drawutil::InscribedRect(x() + border, y() + border,
                w - border * 2.0f, h - border * 2.0f, radius, ix, iy, iw, ih);
            if (iw > 0.0f && ih > 0.0f) {
                Color4f tint = cardBox_.tint;
                tint.a *= cardBox_.opacity;
                if (!cardBox_.imagePath.empty()) {
                    fc.AddImage(cardBox_.imagePath, ix, iy, iw, ih,
                        cardBox_.drawMode, cardBox_.hAlign, cardBox_.vAlign, tint);
                }
                else if (cardBox_.image.handle != 0) {
                    fc.AddImageHandle(cardBox_.image.handle, ix, iy, iw, ih,
                        cardBox_.drawMode, cardBox_.hAlign, cardBox_.vAlign, tint);
                }
            }
        }

        if (!hasTitle() || titleHeight_ <= 0.0f) return;

        const float innerL = border + padLeft_;
        const float titleX = x() + innerL;
        const float titleW = std::max(0.0f, w - innerL - border - padRight_);
        const float titleY = y() + border + padTop_;
        if (titleW <= 0.0f || titleHeight_ <= 0.0f) return;

        const FontDesc& font = titleStyle_.font;
        Color4f fg = titleStyle_.colors.get(WidgetState::Normal).text;
        fg.a *= cardBox_.opacity;

        std::string shown = title_;
        TextMetrics tm = fc.MeasureText(shown, font);
        if (tm.width > titleW) {
            shown = drawutil::Ellipsize(fc, title_, font, titleW);
            tm = fc.MeasureText(shown, font);
        }
        if (tm.width <= 0.0f) return;

        const float ty = titleY + (titleHeight_ - tm.height) * 0.5f;
        fc.AddText(shown, font, titleX, ty + tm.ascent, fg);
    }

}
