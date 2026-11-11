
#include "pch.h"

#include <algorithm>
#include <string>
#include <utility>

#include "include/ZeWatermark.h"
#include "include/Zekit.h"
#include "ZeDrawUtil.h"

namespace Kit {

    namespace {
        constexpr int kMaxTiles = 4000;
    }

    Watermark::Watermark(std::string text)
        : text_(std::move(text)) {
        ApplyTheme(Themes::Light());
        Panel::SetBackground(Color4f{ 0.0f, 0.0f, 0.0f, 0.0f });
        SetDefaultSize(240.0f, 160.0f);
    }

    void Watermark::SetText(std::string text) {
        text_ = std::move(text);
        RequestWindowRedraw();
    }

    void Watermark::SetFontSize(unsigned int px) {
        fontSizeSet_ = true;
        fontSize_ = px;
        textStyle_.font.pixelSize = px;
        markStyled();
        RequestWindowRedraw();
    }

    void Watermark::SetColor(const Color4f& c) {
        color_ = c;
        markStyled();
        RequestWindowRedraw();
    }

    void Watermark::SetGap(float gapX, float gapY) {
        gapX_ = (gapX > 0.0f) ? gapX : 0.0f;
        gapY_ = (gapY > 0.0f) ? gapY : 0.0f;
        markStyled();
        RequestWindowRedraw();
    }

    void Watermark::SetOffset(float offsetX, float offsetY) {
        offsetX_ = offsetX;
        offsetY_ = offsetY;
        markStyled();
        RequestWindowRedraw();
    }

    void Watermark::SetStagger(bool on) {
        stagger_ = on;
        markStyled();
        RequestWindowRedraw();
    }

    void Watermark::SetImage(std::string path) {
        imagePath_ = std::move(path);
        RequestWindowRedraw();
    }

    void Watermark::ClearImage() {
        imagePath_.clear();
        RequestWindowRedraw();
    }

    void Watermark::SetImageTint(const Color4f& c) {
        imageTint_ = c;
        markStyled();
        RequestWindowRedraw();
    }

    void Watermark::ApplyTheme(const Theme& t) {
        const WatermarkStyle& ws = t.watermark;

        textStyle_ = ws.text;
        if (fontSizeSet_) textStyle_.font.pixelSize = fontSize_;

        color_ = ws.color;
        imageTint_ = ws.imageTint;
        gapX_ = (ws.gapX > 0.0f) ? ws.gapX : 96.0f;
        gapY_ = (ws.gapY > 0.0f) ? ws.gapY : 64.0f;
        offsetX_ = ws.offsetX;
        offsetY_ = ws.offsetY;
    }

    float Watermark::hintWidth() const {
        if (prefWidth() > 0.0f) return prefWidth();
        return Panel::hintWidth();
    }

    float Watermark::hintHeight() const {
        if (prefHeight() > 0.0f) return prefHeight();
        return Panel::hintHeight();
    }

    void Watermark::Paint(FrameCollector& fc) const {
        const float w = width();
        const float h = height();
        if (w <= 0.0f || h <= 0.0f) return;

        Panel::Paint(fc);

        fc.PushClip(x(), y(), w, h);
        if (hasImage()) paintImageTiles(fc);
        else            paintTextTiles(fc);
        fc.PopClip();
    }

    void Watermark::paintTextTiles(FrameCollector& fc) const {
        if (text_.empty()) return;

        const TextMetrics tm = fc.MeasureText(text_, textStyle_.font);
        if (tm.width <= 0.0f || tm.height <= 0.0f) return;

        Color4f c = color_;
        if (c.a <= 0.0f) return;

        const float stepX = tm.width + gapX_;
        const float stepY = tm.height + gapY_;
        if (stepX <= 0.0f || stepY <= 0.0f) return;

        const float x0 = x() + offsetX_;
        const float y0 = y() + offsetY_;
        const float x1 = x() + width();
        const float y1 = y() + height();

        int tiles = 0;
        for (float ty = y0; ty < y1; ty += stepY) {
            const float rowShift = stagger_ ? (stepX * 0.5f) : 0.0f;
            for (float tx = x0 + rowShift; tx < x1; tx += stepX) {
                if (++tiles > kMaxTiles) return;
                if (tx + tm.width < x() || tx > x1) continue;
                fc.AddText(text_, textStyle_.font, tx, ty + tm.ascent, c);
            }
        }
    }

    void Watermark::paintImageTiles(FrameCollector& fc) const {
        Color4f tint = imageTint_;
        if (tint.a <= 0.0f) return;

        float iw = 0.0f, ih = 0.0f;
        if (!fc.ImageSize(imagePath_, iw, ih) || iw <= 0.0f || ih <= 0.0f) {
            iw = 64.0f;
            ih = 64.0f;
        }

        const float stepX = iw + gapX_;
        const float stepY = ih + gapY_;
        if (stepX <= 0.0f || stepY <= 0.0f) return;

        const float x0 = x() + offsetX_;
        const float y0 = y() + offsetY_;
        const float x1 = x() + width();
        const float y1 = y() + height();

        int tiles = 0;
        for (float ty = y0; ty < y1; ty += stepY) {
            for (float tx = x0; tx < x1; tx += stepX) {
                if (++tiles > kMaxTiles) return;
                fc.AddImage(imagePath_, tx, ty, iw, ih,
                    BgImageDrawMode::Origin, BgImageHAlign::Left, BgImageVAlign::Top, tint);
            }
        }
    }

}
