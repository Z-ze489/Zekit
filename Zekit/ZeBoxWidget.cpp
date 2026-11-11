#include "pch.h"

#include <algorithm>
#include <utility>

#include "include/ZeBoxWidget.h"
#include "ZeFrameCollector.h"
#include "ZeRoundedRectShape.h"

namespace Kit {

    void BoxWidget::SetEnabled(bool enabled) {
        Widget::SetEnabled(enabled);
        if (!isEnabled()) {
            hovered_ = false;
            pressed_ = false;
        }
        RequestWindowRedraw();
    }

    void BoxWidget::OnMouseEnter() {
        if (isEnabled()) hovered_ = true;
        RequestWindowRedraw();
    }

    void BoxWidget::OnMouseLeave() {
        hovered_ = false;
        pressed_ = false;
        RequestWindowRedraw();
    }

    void BoxWidget::SetBodyColor(const Color4f& c) {
        markStyled();
        box_.colors.get(WidgetState::Normal).background = c;
        RequestWindowRedraw();
    }
    void BoxWidget::SetHoverBodyColor(const Color4f& c) {
        markStyled();
        box_.colors.get(WidgetState::Hover).background = c;
        RequestWindowRedraw();
    }
    void BoxWidget::SetPressedBodyColor(const Color4f& c) {
        markStyled();
        box_.colors.get(WidgetState::Pressed).background = c;
        RequestWindowRedraw();
    }
    void BoxWidget::SetDisabledBodyColor(const Color4f& c) {
        markStyled();
        box_.colors.get(WidgetState::Disabled).background = c;
        RequestWindowRedraw();
    }

    void BoxWidget::SetBorderColor(const Color4f& c) {
        markStyled();
        box_.colors.get(WidgetState::Normal).border = c;
        RequestWindowRedraw();
    }
    void BoxWidget::SetHoverBorderColor(const Color4f& c) {
        markStyled();
        box_.colors.get(WidgetState::Hover).border = c;
        RequestWindowRedraw();
    }
    void BoxWidget::SetPressedBorderColor(const Color4f& c) {
        markStyled();
        box_.colors.get(WidgetState::Pressed).border = c;
        RequestWindowRedraw();
    }
    void BoxWidget::SetDisabledBorderColor(const Color4f& c) {
        markStyled();
        box_.colors.get(WidgetState::Disabled).border = c;
        RequestWindowRedraw();
    }

    void BoxWidget::SetBackgroundImage(std::string path) {
        markStyled();
        box_.imagePath = std::move(path);
        box_.image = BackgroundImage{};
        RequestWindowRedraw();
    }

    void BoxWidget::ClearBackgroundImage() {
        markStyled();
        box_.imagePath.clear();
        box_.image = BackgroundImage{};
        RequestWindowRedraw();
    }

    void BoxWidget::SetBackgroundDrawMode(BgImageDrawMode mode) {
        markStyled();
        box_.drawMode = mode;
        RequestWindowRedraw();
    }

    void BoxWidget::SetBackgroundAlign(BgImageHAlign hAlign, BgImageVAlign vAlign) {
        markStyled();
        box_.hAlign = hAlign;
        box_.vAlign = vAlign;
        RequestWindowRedraw();
    }

    void BoxWidget::SetBackgroundTint(const Color4f& c) {
        markStyled();
        box_.tint = c;
        RequestWindowRedraw();
    }

    void BoxWidget::SetBorderWidth(float w) {
        markStyled();
        SetBorderWidth(w, w, w, w);
        RequestWindowRedraw();
    }

    void BoxWidget::SetBorderWidth(float top, float right, float bottom, float left) {
        markStyled();
        box_.border.width[Border::Top] = (top > 0.0f) ? top : 0.0f;
        box_.border.width[Border::Right] = (right > 0.0f) ? right : 0.0f;
        box_.border.width[Border::Bottom] = (bottom > 0.0f) ? bottom : 0.0f;
        box_.border.width[Border::Left] = (left > 0.0f) ? left : 0.0f;
        RequestWindowRedraw();
    }

    void BoxWidget::SetBorderStyle(BorderStyle style) {
        markStyled();
        box_.border.style = style;
        RequestWindowRedraw();
    }

    void BoxWidget::SetRadiusMode(RadiusMode mode) {
        markStyled();
        box_.radiusMode = mode;
        RequestWindowRedraw();
    }

    void BoxWidget::SetRadius(float radius) {
        markStyled();
        box_.radiusMode = RadiusMode::Custom;
        box_.customRadius = (radius > 0.0f) ? radius : 0.0f;
        RequestWindowRedraw();
    }

    void BoxWidget::SetCustomRadius(float radius) {
        markStyled();
        SetRadius(radius);
        RequestWindowRedraw();
    }

    void BoxWidget::SetPadding(float top, float right, float bottom, float left) {
        markStyled();
        box_.padding.top = (top > 0.0f) ? top : 0.0f;
        box_.padding.right = (right > 0.0f) ? right : 0.0f;
        box_.padding.bottom = (bottom > 0.0f) ? bottom : 0.0f;
        box_.padding.left = (left > 0.0f) ? left : 0.0f;
        RequestWindowRedraw();
    }

    void BoxWidget::SetPadding(float horizontal, float vertical) {
        markStyled();
        box_.padding.left = box_.padding.right = (horizontal > 0.0f) ? horizontal : 0.0f;
        box_.padding.top = box_.padding.bottom = (vertical > 0.0f) ? vertical : 0.0f;
        RequestWindowRedraw();
    }

    void BoxWidget::SetPadding(float all) {
        markStyled();
        SetPadding(all, all);
        RequestWindowRedraw();
    }

    void BoxWidget::SetShadow(bool enabled, const Color4f& color,
        float offsetX, float offsetY, float blur, float spread) {
        markStyled();
        box_.shadow.enabled = enabled;
        box_.shadow.color = color;
        box_.shadow.offsetX = offsetX;
        box_.shadow.offsetY = offsetY;
        box_.shadow.blur = (blur > 0.0f) ? blur : 0.0f;
        box_.shadow.spread = (spread > 0.0f) ? spread : 0.0f;
        RequestWindowRedraw();
    }

    void BoxWidget::SetShadowEnabled(bool enabled) {
        markStyled();
        box_.shadow.enabled = enabled;
        RequestWindowRedraw();
    }
    void BoxWidget::SetShadowColor(const Color4f& c) {
        markStyled();
        box_.shadow.color = c;
        RequestWindowRedraw();
    }

    void BoxWidget::SetShadowOffset(float offsetX, float offsetY) {
        markStyled();
        box_.shadow.offsetX = offsetX;
        box_.shadow.offsetY = offsetY;
        RequestWindowRedraw();
    }

    void BoxWidget::SetShadowBlur(float blur) {
        markStyled();
        box_.shadow.blur = (blur > 0.0f) ? blur : 0.0f;
        RequestWindowRedraw();
    }

    void BoxWidget::SetShadowSpread(float spread) {
        markStyled();
        box_.shadow.spread = (spread > 0.0f) ? spread : 0.0f;
        RequestWindowRedraw();
    }

    void BoxWidget::SetOutline(bool enabled, float width, const Color4f& color, float offset) {
        markStyled();
        box_.outline.enabled = enabled;
        box_.outline.width = (width > 0.0f) ? width : 0.0f;
        box_.outline.color = color;
        box_.outline.offset = (offset > 0.0f) ? offset : 0.0f;
        RequestWindowRedraw();
    }

    void BoxWidget::SetOutlineEnabled(bool enabled) {
        markStyled();
        box_.outline.enabled = enabled;
        RequestWindowRedraw();
    }

    void BoxWidget::SetOutlineWidth(float width) {
        markStyled();
        box_.outline.width = (width > 0.0f) ? width : 0.0f;
        RequestWindowRedraw();
    }

    void BoxWidget::SetOutlineColor(const Color4f& c) {
        markStyled();
        box_.outline.color = c;
        RequestWindowRedraw();
    }

    void BoxWidget::SetOutlineOffset(float offset) {
        markStyled();
        box_.outline.offset = (offset > 0.0f) ? offset : 0.0f;
        RequestWindowRedraw();
    }

    void BoxWidget::SetOutlineStyle(BorderStyle style) {
        markStyled();
        box_.outline.style = style;
        RequestWindowRedraw();
    }

    void BoxWidget::SetOpacity(float opacity) {
        markStyled();
        box_.opacity = (opacity < 0.0f) ? 0.0f : (opacity > 1.0f ? 1.0f : opacity);
        RequestWindowRedraw();
    }

    const BoxStyle& BoxWidget::themeBox(const Theme& t) const { return t.button.box; }

    void BoxWidget::ApplyTheme(const Theme& t) {
        const std::string     keepPath = box_.imagePath;
        const BackgroundImage keepImg = box_.image;
        const BgImageDrawMode keepMode = box_.drawMode;
        const BgImageHAlign   keepHA = box_.hAlign;
        const BgImageVAlign   keepVA = box_.vAlign;
        const Color4f         keepTint = box_.tint;

        box_ = themeBox(t);

        box_.imagePath = keepPath;
        box_.image = keepImg;
        box_.drawMode = keepMode;
        box_.hAlign = keepHA;
        box_.vAlign = keepVA;
        box_.tint = keepTint;
    }

    WidgetState BoxWidget::currentState() const {
        if (!isEnabled()) return WidgetState::Disabled;
        if (pressed_)  return WidgetState::Pressed;
        if (hasFocus()) return WidgetState::Focused;
        if (hovered_)  return WidgetState::Hover;
        return WidgetState::Normal;
    }

    ColorSet BoxWidget::resolvedColors(WidgetState s) const {
        ColorSet cs = box_.colors.get(s);
        const float op = box_.opacity;
        if (op < 1.0f) {
            cs.background.a *= op;
            cs.border.a     *= op;
            cs.text.a       *= op;
            cs.decoration.a *= op;
        }
        return cs;
    }

    float BoxWidget::resolveRadius(const BoxStyle& box, const Theme& theme) {
        switch (box.radiusMode) {
        case RadiusMode::None:    return 0.0f;
        case RadiusMode::Small:   return theme.radiusS;
        case RadiusMode::Normal:  return theme.radiusM;
        case RadiusMode::Large:   return theme.radiusL;
        case RadiusMode::Capsule: return 0.0f;
        case RadiusMode::Custom:  return box.customRadius;
        }
        return 0.0f;
    }

    void BoxWidget::Paint(FrameCollector& fc) const {
        if (width() <= 0.0f || height() <= 0.0f) return;

        paintShadow(fc);
        paintOutline(fc);
        paintBox(fc);

        const bool clip = clipsContent();
        if (clip) {
            const float bl = box_.border.width[Border::Left];
            const float bt = box_.border.width[Border::Top];
            const float br = box_.border.width[Border::Right];
            const float bb = box_.border.width[Border::Bottom];
            fc.PushClip(x() + bl, y() + bt, width() - bl - br, height() - bt - bb);
        }

        paintContent(fc);

        if (clip) fc.PopClip();
    }

    bool BoxWidget::clipsContent() const {
        return true;
    }

    void BoxWidget::paintContent(FrameCollector&) const {
    }

    void BoxWidget::paintShadow(FrameCollector& fc) const {
        const Shadow& sh = box_.shadow;
        if (!sh.enabled) return;

        const Vec2 lo{ x(), y() };
        const Vec2 sz{ width(), height() };

        float radius = resolveRadius(box_, fc.GetTheme());
        if (box_.radiusMode == RadiusMode::Capsule) radius = 0.5f * std::min(sz.x, sz.y);

        Color4f sc = sh.color;
        sc.a *= box_.opacity;
        if (sc.a <= 0.0f) return;

        const float ex = sh.spread;
        const float sx = lo.x + sh.offsetX - ex;
        const float sy = lo.y + sh.offsetY - ex;
        const float sw = sz.x + ex * 2.0f;
        const float sh2 = sz.y + ex * 2.0f;
        if (sw <= 0.0f || sh2 <= 0.0f) return;

        const float r = (radius > 0.0f) ? radius + ex : 0.0f;
        fc.AddShape(RoundedRectShape(Vec2{ sx, sy }, Vec2{ sw, sh2 }, r), sc,
            sx + sw * 0.5f, sy + sh2 * 0.5f, sw * 0.5f, sh2 * 0.5f, r);
    }

    void BoxWidget::paintOutline(FrameCollector& fc) const {
        const Outline& ol = box_.outline;
        if (!ol.enabled || ol.width <= 0.0f) return;

        const Vec2 lo{ x(), y() };
        const Vec2 sz{ width(), height() };

        float radius = resolveRadius(box_, fc.GetTheme());
        if (box_.radiusMode == RadiusMode::Capsule) radius = 0.5f * std::min(sz.x, sz.y);

        Color4f oc = ol.color;
        oc.a *= box_.opacity;
        if (oc.a <= 0.0f) return;

        const float grow = ol.offset + ol.width;
        const float ox = lo.x - grow;
        const float oy = lo.y - grow;
        const float ow = sz.x + grow * 2.0f;
        const float oh = sz.y + grow * 2.0f;
        const float r = (radius > 0.0f) ? radius + grow : 0.0f;

        fc.AddShape(RoundedRectShape(Vec2{ ox, oy }, Vec2{ ow, oh }, r), oc,
            ox + ow * 0.5f, oy + oh * 0.5f, ow * 0.5f, oh * 0.5f, r);
    }

    void BoxWidget::paintBox(FrameCollector& fc) const {
        const ColorSet cs = resolvedColors(currentState());
        const Color4f  body = cs.background;
        const Color4f  border = cs.border;

        const float bwTop = box_.border.width[Border::Top];
        const float bwRight = box_.border.width[Border::Right];
        const float bwBottom = box_.border.width[Border::Bottom];
        const float bwLeft = box_.border.width[Border::Left];
        const bool  hasBorder = (bwTop > 0.f || bwRight > 0.f || bwBottom > 0.f || bwLeft > 0.f);

        const Vec2 lo{ x(), y() };
        const Vec2 sz{ width(), height() };

        float radius = resolveRadius(box_, fc.GetTheme());
        if (box_.radiusMode == RadiusMode::Capsule) {
            radius = 0.5f * std::min(sz.x, sz.y);
        }

        const BorderStyle bstyle = box_.border.style;
        const bool wantsBorder = hasBorder && (bstyle != BorderStyle::None);
        const bool dashedBorder = wantsBorder
            && (bstyle == BorderStyle::Dashed || bstyle == BorderStyle::Dotted);

        if (dashedBorder) {
            const float bw = std::max(std::max(bwTop, bwRight), std::max(bwBottom, bwLeft));

            const float ix = lo.x + bw, iy = lo.y + bw;
            const float iw = sz.x - bw * 2.0f, ih = sz.y - bw * 2.0f;
            if (iw > 0.f && ih > 0.f && body.a > 0.0f) {
                const float ri = std::max(0.f, radius - bw);
                fc.AddShape(RoundedRectShape(Vec2{ ix, iy }, Vec2{ iw, ih }, ri), body,
                    ix + iw * 0.5f, iy + ih * 0.5f, iw * 0.5f, ih * 0.5f, ri);
            }

            if (border.a > 0.0f) {
                const bool dotted = (bstyle == BorderStyle::Dotted);
                const float dashLen = dotted ? bw * 0.9f : bw * 3.0f;
                const float gapLen = dotted ? bw * 1.1f : bw * 2.0f;
                fc.AddDashedRect(lo.x, lo.y, sz.x, sz.y, radius, bw, dashLen, gapLen, border);
            }
        }
        else if (wantsBorder && body.a <= 0.0f && border.a > 0.0f) {
            if (sz.x > 0.0f && sz.y > 0.0f) {
                const float midH = sz.y - bwTop - bwBottom;
                auto edge = [&](float ex, float ey, float ew, float eh) {
                    if (ew <= 0.0f || eh <= 0.0f) return;
                    fc.AddShape(RoundedRectShape(Vec2{ ex, ey }, Vec2{ ew, eh }, 0.0f), border,
                        ex + ew * 0.5f, ey + eh * 0.5f, ew * 0.5f, eh * 0.5f, 0.0f);
                };
                edge(lo.x, lo.y, sz.x, bwTop);
                edge(lo.x, lo.y + sz.y - bwBottom, sz.x, bwBottom);
                edge(lo.x, lo.y + bwTop, bwLeft, midH);
                edge(lo.x + sz.x - bwRight, lo.y + bwTop, bwRight, midH);
            }
        }
        else if (wantsBorder) {
            if (border.a > 0.0f)
                fc.AddShape(RoundedRectShape(lo, sz, radius), border,
                    lo.x + sz.x * 0.5f, lo.y + sz.y * 0.5f,
                    sz.x * 0.5f, sz.y * 0.5f, radius);

            const float ix = lo.x + bwLeft;
            const float iy = lo.y + bwTop;
            const float iw = sz.x - bwLeft - bwRight;
            const float ih = sz.y - bwTop - bwBottom;
            if (iw > 0.f && ih > 0.f && body.a > 0.0f) {
                const float rTL = std::max(0.f, radius - std::max(bwTop, bwLeft));
                const float rTR = std::max(0.f, radius - std::max(bwTop, bwRight));
                const float rBR = std::max(0.f, radius - std::max(bwBottom, bwRight));
                const float rBL = std::max(0.f, radius - std::max(bwBottom, bwLeft));
                const float ri = std::min(std::min(rTL, rTR), std::min(rBR, rBL));
                fc.AddShape(RoundedRectShape(Vec2{ ix, iy }, Vec2{ iw, ih }, rTL, rTR, rBR, rBL),
                    body, ix + iw * 0.5f, iy + ih * 0.5f, iw * 0.5f, ih * 0.5f, ri);
            }
        }
        else {
            if (body.a > 0.0f)
                fc.AddShape(RoundedRectShape(lo, sz, radius), body,
                    lo.x + sz.x * 0.5f, lo.y + sz.y * 0.5f,
                    sz.x * 0.5f, sz.y * 0.5f, radius);
        }

        const float ix = lo.x + bwLeft;
        const float iy = lo.y + bwTop;
        const float iw = sz.x - bwLeft - bwRight;
        const float ih = sz.y - bwTop - bwBottom;
        if (iw > 0.0f && ih > 0.0f) {
            if (!box_.imagePath.empty()) {
                fc.AddImage(box_.imagePath, ix, iy, iw, ih,
                    box_.drawMode, box_.hAlign, box_.vAlign, box_.tint);
            }
            else if (box_.image.handle != 0) {
                fc.AddImageHandle(box_.image.handle, ix, iy, iw, ih,
                    box_.drawMode, box_.hAlign, box_.vAlign, box_.tint);
            }
        }
    }

}
