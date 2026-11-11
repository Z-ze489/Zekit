
#include "pch.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "include/ZeImage.h"
#include "include/Zekit.h"
#include "ZeDrawUtil.h"

namespace Kit {

    namespace {
        constexpr float kDefaultW = 160.0f;
        constexpr float kDefaultH = 120.0f;
    }

    Image::Image(std::string path)
        : path_(std::move(path)) {
        ApplyTheme(Themes::Light());
        SetFocusable(false);
        SetDefaultSize(kDefaultW, kDefaultH);
    }

    void Image::SetSource(std::string path) {
        path_ = std::move(path);
        RequestWindowRedraw();
    }

    void Image::SetDrawMode(BgImageDrawMode mode) {
        drawMode_ = mode;
        RequestWindowRedraw();
    }

    void Image::SetImageAlign(BgImageHAlign h, BgImageVAlign v) {
        hAlign_ = h;
        vAlign_ = v;
        RequestWindowRedraw();
    }

    void Image::SetTint(const Color4f& c) {
        tint_ = c;
        RequestWindowRedraw();
    }

    void Image::SetAspectRatio(float ratio) {
        aspectRatio_ = (ratio > 0.0f) ? ratio : 0.0f;
        notifyLayoutInputChanged();
        RequestWindowRedraw();
    }

    void Image::SetCornerRadius(float radius) {
        box_.radiusMode = RadiusMode::Custom;
        box_.customRadius = (radius > 0.0f) ? radius : 0.0f;
        markStyled();
        RequestWindowRedraw();
    }

    void Image::SetPlaceholderColor(const Color4f& c) {
        placeholderSet_ = true;
        placeholderColor_ = c;
        markStyled();
        RequestWindowRedraw();
    }

    const BoxStyle& Image::themeBox(const Theme& t) const {
        return t.image.box;
    }

    void Image::ApplyTheme(const Theme& t) {
        BoxWidget::ApplyTheme(t);
        if (!placeholderSet_) placeholderColor_ = t.image.placeholderColor;
        placeholderGlyph_ = t.image.placeholderGlyph;
    }

    float Image::hintWidth() const {
        if (prefWidth() > 0.0f) return prefWidth();
        if (aspectRatio_ > 0.0f && prefHeight() > 0.0f) return prefHeight() * aspectRatio_;
        return kDefaultW;
    }

    float Image::hintHeight() const {
        if (prefHeight() > 0.0f) return prefHeight();
        if (aspectRatio_ > 0.0f && prefWidth() > 0.0f) return prefWidth() / aspectRatio_;
        return kDefaultH;
    }

    void Image::paintContent(FrameCollector& fc) const {
        const float w = width();
        const float h = height();
        if (w <= 0.0f || h <= 0.0f) return;

        const float bl = box_.border.width[Border::Left];
        const float bt = box_.border.width[Border::Top];
        const float br = box_.border.width[Border::Right];
        const float bb = box_.border.width[Border::Bottom];
        const float cx = x() + bl + box_.padding.left;
        const float cy = y() + bt + box_.padding.top;
        const float cw = std::max(0.0f, w - bl - br - box_.padding.left - box_.padding.right);
        const float ch = std::max(0.0f, h - bt - bb - box_.padding.top - box_.padding.bottom);
        if (cw <= 0.0f || ch <= 0.0f) return;

        if (hasSource()) {
            Color4f tint = tint_;
            tint.a *= box_.opacity;
            fc.AddImage(path_, cx, cy, cw, ch, drawMode_, hAlign_, vAlign_, tint);
            return;
        }

        Color4f ph = placeholderColor_;
        ph.a *= box_.opacity;
        drawutil::PaintRoundedRect(fc, cx, cy, cw, ch, 0.0f, ph);

        Color4f g = placeholderGlyph_;
        g.a *= box_.opacity;
        if (g.a > 0.0f) {
            const float s = std::min(cw, ch) * 0.5f;
            const float midX = cx + cw * 0.5f;
            const float baseY = cy + ch * 0.5f + s * 0.42f;

            const std::vector<Vec2> leftTri{
                Vec2{ midX - s * 0.62f, baseY },
                Vec2{ midX - s * 0.16f, baseY - s * 0.80f },
                Vec2{ midX + s * 0.22f, baseY },
            };
            const std::vector<Vec2> rightTri{
                Vec2{ midX - s * 0.06f, baseY },
                Vec2{ midX + s * 0.42f, baseY - s * 1.05f },
                Vec2{ midX + s * 0.96f, baseY },
            };
            fc.AddShape(PolygonShape(leftTri, true), g, 0.f, 0.f, 0.f, 0.f, 0.f);
            fc.AddShape(PolygonShape(rightTri, true), g, 0.f, 0.f, 0.f, 0.f, 0.f);
            drawutil::PaintCircle(fc, midX + s * 0.52f, baseY - s * 0.92f, s * 0.15f, g);
        }
    }

}
