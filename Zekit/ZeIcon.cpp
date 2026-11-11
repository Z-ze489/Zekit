
#include "pch.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include "include/ZeIcon.h"
#include "include/Zekit.h"
#include "ZeDrawUtil.h"

namespace Kit {

    namespace {

        void DrawGlyph(FrameCollector& fc, IconGlyph g, float cx, float cy,
            float s, float t, const Color4f& c) {
            if (g == IconGlyph::None || s <= 0.0f || c.a <= 0.0f) return;
            const float h = s * 0.5f;
            const float th = std::max(t, 1.0f);

            switch (g) {
            case IconGlyph::Check:
                drawutil::PaintThickSegment(fc, cx - h * 0.60f, cy + h * 0.02f, cx - h * 0.15f, cy + h * 0.46f, th, c);
                drawutil::PaintThickSegment(fc, cx - h * 0.15f, cy + h * 0.46f, cx + h * 0.62f, cy - h * 0.48f, th, c);
                break;

            case IconGlyph::Close:
                drawutil::PaintThickSegment(fc, cx - h * 0.5f, cy - h * 0.5f, cx + h * 0.5f, cy + h * 0.5f, th, c);
                drawutil::PaintThickSegment(fc, cx - h * 0.5f, cy + h * 0.5f, cx + h * 0.5f, cy - h * 0.5f, th, c);
                break;

            case IconGlyph::Plus:
                drawutil::PaintThickSegment(fc, cx - h * 0.62f, cy, cx + h * 0.62f, cy, th, c);
                drawutil::PaintThickSegment(fc, cx, cy - h * 0.62f, cx, cy + h * 0.62f, th, c);
                break;

            case IconGlyph::Minus:
                drawutil::PaintThickSegment(fc, cx - h * 0.62f, cy, cx + h * 0.62f, cy, th, c);
                break;

            case IconGlyph::ChevronUp:
                drawutil::PaintChevron(fc, cx, cy - h * 0.18f, h * 0.42f, th,
                    drawutil::ChevronDir::Up, c);
                break;
            case IconGlyph::ChevronDown:
                drawutil::PaintChevron(fc, cx, cy + h * 0.18f, h * 0.42f, th,
                    drawutil::ChevronDir::Down, c);
                break;
            case IconGlyph::ChevronLeft:
                drawutil::PaintChevron(fc, cx - h * 0.18f, cy, h * 0.42f, th,
                    drawutil::ChevronDir::Left, c);
                break;
            case IconGlyph::ChevronRight:
                drawutil::PaintChevron(fc, cx + h * 0.18f, cy, h * 0.42f, th,
                    drawutil::ChevronDir::Right, c);
                break;

            case IconGlyph::ArrowUp: {
                const std::vector<Vec2> tri{
                    Vec2{ cx, cy - h * 0.62f },
                    Vec2{ cx - h * 0.55f, cy + h * 0.52f },
                    Vec2{ cx + h * 0.55f, cy + h * 0.52f },
                };
                fc.AddShape(PolygonShape(tri, true), c, 0.f, 0.f, 0.f, 0.f, 0.f);
                break;
            }
            case IconGlyph::ArrowDown: {
                const std::vector<Vec2> tri{
                    Vec2{ cx, cy + h * 0.62f },
                    Vec2{ cx - h * 0.55f, cy - h * 0.52f },
                    Vec2{ cx + h * 0.55f, cy - h * 0.52f },
                };
                fc.AddShape(PolygonShape(tri, true), c, 0.f, 0.f, 0.f, 0.f, 0.f);
                break;
            }

            case IconGlyph::Search: {
                const float r = h * 0.52f;
                const float ox = cx - h * 0.12f;
                const float oy = cy - h * 0.12f;
                drawutil::PaintCircleOutline(fc, ox, oy, r, th, c);
                const float k = 0.7071f;
                drawutil::PaintThickSegment(fc, ox + r * k, oy + r * k, cx + h * 0.72f, cy + h * 0.72f, th, c);
                break;
            }

            case IconGlyph::More: {
                const float r = std::max(th * 0.8f, s * 0.075f);
                drawutil::PaintCircle(fc, cx - h * 0.52f, cy, r, c);
                drawutil::PaintCircle(fc, cx, cy, r, c);
                drawutil::PaintCircle(fc, cx + h * 0.52f, cy, r, c);
                break;
            }

            case IconGlyph::Info: {
                const float r = h * 0.62f;
                drawutil::PaintCircleOutline(fc, cx, cy, r, th, c);
                const float dot = std::max(th * 0.8f, s * 0.07f);
                drawutil::PaintCircle(fc, cx, cy - h * 0.26f, dot, c);
                drawutil::PaintThickSegment(fc, cx, cy - h * 0.04f, cx, cy + h * 0.30f, th, c);
                break;
            }

            case IconGlyph::Warning: {
                const float topY = cy - h * 0.62f;
                const float botY = cy + h * 0.52f;
                const float lx = cx - h * 0.62f;
                const float rx = cx + h * 0.62f;
                drawutil::PaintThickSegment(fc, cx, topY, rx, botY, th, c);
                drawutil::PaintThickSegment(fc, rx, botY, lx, botY, th, c);
                drawutil::PaintThickSegment(fc, lx, botY, cx, topY, th, c);
                const float dot = std::max(th * 0.8f, s * 0.07f);
                drawutil::PaintCircle(fc, cx, cy + h * 0.22f, dot, c);
                drawutil::PaintThickSegment(fc, cx, cy - h * 0.24f, cx, cy + h * 0.02f, th, c);
                break;
            }

            default:
                break;
            }
        }

    }

    Icon::Icon(IconGlyph glyph)
        : glyph_(glyph) {
        ApplyTheme(Themes::Light());
        SetFocusable(false);
        SetDefaultSize(16.0f, 16.0f);
    }

    void Icon::SetGlyph(IconGlyph glyph) {
        glyph_ = glyph;
        imagePath_.clear();
        imageResId_ = 0;
        RequestWindowRedraw();
    }

    void Icon::SetImage(std::string path) {
        imagePath_ = std::move(path);
        imageResId_ = 0;
        RequestWindowRedraw();
    }

    void Icon::SetImageFromResource(int resId) {
        imageResId_ = resId;
        imagePath_.clear();
        RequestWindowRedraw();
    }

    void Icon::ClearImage() {
        imagePath_.clear();
        imageResId_ = 0;
        RequestWindowRedraw();
    }

    void Icon::ClearImageResource() {
        imageResId_ = 0;
        RequestWindowRedraw();
    }

    void Icon::SetImageTint(const Color4f& c) {
        imageTint_ = c;
        RequestWindowRedraw();
    }

    void Icon::SetIconSize(float px) {
        sizeOverride_ = (px > 0.0f) ? px : 0.0f;
        markStyled();
        notifyLayoutInputChanged();
        RequestWindowRedraw();
    }

    void Icon::SetColor(const Color4f& c) {
        color_ = c;
        glyphSet_ = true;
        markStyled();
        RequestWindowRedraw();
    }

    void Icon::SetStroke(float px) {
        strokeOverride_ = (px > 0.0f) ? px : 0.0f;
        markStyled();
        RequestWindowRedraw();
    }

    void Icon::ApplyTheme(const Theme& t) {
        if (!glyphSet_) color_ = t.icon.color;
        themeSize_ = (t.icon.size > 0.0f) ? t.icon.size : 16.0f;
        themeStroke_ = (t.icon.stroke > 0.0f) ? t.icon.stroke : 1.6f;

        box_.colors = MultiStateColors{};
        box_.border = Border{};
        box_.padding = Padding{};
        box_.radiusMode = RadiusMode::None;
        box_.shadow.enabled = false;
        box_.outline.enabled = false;
        box_.opacity = 1.0f;
    }

    float Icon::resolvedSize() const {
        return sizeOverride_ > 0.0f ? sizeOverride_ : themeSize_;
    }

    float Icon::resolvedStroke() const {
        return strokeOverride_ > 0.0f ? strokeOverride_ : themeStroke_;
    }

    float Icon::hintWidth() const { return prefWidth() > 0.0f ? prefWidth() : resolvedSize(); }
    float Icon::hintHeight() const { return prefHeight() > 0.0f ? prefHeight() : resolvedSize(); }

    void Icon::paintContent(FrameCollector& fc) const {
        const float s = std::min(width(), height());
        if (s <= 0.0f) return;

        const float cx = x() + width() * 0.5f;
        const float cy = y() + height() * 0.5f;

        if (hasImage() || hasResourceImage()) {
            Color4f tint = imageTint_;
            tint.a *= box_.opacity;
            if (hasResourceImage())
                fc.AddImageResource(imageResId_, cx - s * 0.5f, cy - s * 0.5f, s, s,
                    BgImageDrawMode::Fit, BgImageHAlign::Center, BgImageVAlign::Middle, tint);
            else
                fc.AddImage(imagePath_, cx - s * 0.5f, cy - s * 0.5f, s, s,
                    BgImageDrawMode::Fit, BgImageHAlign::Center, BgImageVAlign::Middle, tint);
            return;
        }

        Color4f c = color_;
        c.a *= box_.opacity;
        DrawGlyph(fc, glyph_, cx, cy, s * 0.86f, resolvedStroke(), c);
    }

}
