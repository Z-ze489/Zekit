/**
 * @file ZeWatermark.h
 * @brief Watermark: tiles a faint repeated text (or image) over a container
 *
 * @details
 * `Watermark : Panel` -- it is a container: add content to it (`AddContent`),
 * and the container itself tiles the watermark in `Paint`. Because "a container paints itself first, then its children" is the
 * fixed order of this library, the watermark naturally falls below the content -- exactly where a watermark belongs.
 *
 * @par WARNING: this library has no rotated text channel
 * `FrameCollector::AddText` only accepts "position + baseline", with no angle, so the common
 * "45-degree slanted watermark" cannot be produced. What is provided here is horizontal cell-by-cell tiling (`SetStagger(true)` makes odd rows offset
 * by half a cell, which looks closer to diagonal stripes, but it is not rotation).
 *
 * @par Usage
 * @code
 * Kit::Watermark wm("Internal");
 * wm.SetColor(Kit::Color4f::FromHex(0x64748B, 0.10f));   // alpha is the density
 * wm.SetGap(96.0f, 64.0f);
 * wm.SetStagger(true);
 * wm.AddContent(Kit::TableView());                       // content covers the watermark
 * win.AddWidget(wm);
 * @endcode
 *
 * @see ZeTheme.h `WatermarkStyle`
 */

#pragma once
#include "ZePanel.h"
#include "ZeTextStyle.h"

#include <string>
#include <utility>

namespace Kit {

    /**
     * @brief Watermark container
     */
    class Watermark : public Panel {
    public:
        /// @brief Constructs
        /// @param text Watermark text (empty string = no text tiled; `SetImage` can tile an image later)
        explicit Watermark(std::string text = std::string());

        // -------------------------------------------------------------------
        //  Text watermark
        // -------------------------------------------------------------------

        /// @brief Sets the watermark text (empty string = no text tiled)
        void SetText(std::string text);
        const std::string& Text() const { return text_; }
        bool hasText() const { return !text_.empty(); }

        /// @brief Watermark font size (pixels)
        void SetFontSize(unsigned int px);

        /// @brief Watermark color (alpha is the density)
        void SetColor(const Color4f& c);
        const Color4f& color() const { return color_; }

        /// @brief Gap between adjacent cells (pixels)
        void SetGap(float gapX, float gapY);
        float gapX() const { return gapX_; }
        float gapY() const { return gapY_; }

        /// @brief Offset of the first cell (moves the whole grid to the position you want)
        void SetOffset(float offsetX, float offsetY);
        float offsetX() const { return offsetX_; }
        float offsetY() const { return offsetY_; }

        /// @brief Offsets odd rows by half a cell (looks closer to diagonal stripes; not rotated text)
        void SetStagger(bool on);
        bool stagger() const { return stagger_; }

        /// @brief Watermark text style (font weight etc. can be changed; the color still follows `color()`)
        TextStyle& textStyle() { return textStyle_; }
        const TextStyle& textStyle() const { return textStyle_; }

        // -------------------------------------------------------------------
        //  Image watermark
        // -------------------------------------------------------------------

        /// @brief Sets an image watermark (tiled at natural size; empty string = clears)
        /// @note Only the path is stored; decoding / texture upload happen in the render layer (same as the icon of `Image` / `Button`).
        void SetImage(std::string path);
        void ClearImage();
        const std::string& imagePath() const { return imagePath_; }
        bool hasImage() const { return !imagePath_.empty(); }

        /// @brief Tint of the image watermark (white = as-is; lower alpha to make it faint)
        void SetImageTint(const Color4f& c);
        const Color4f& imageTint() const { return imageTint_; }

        // -------------------------------------------------------------------
        //  Content
        // -------------------------------------------------------------------

        /// @brief Adds a child to the container (value semantics) -- equivalent to `AddChild`
        /// @note The watermark is painted first, children after -> the watermark is below the content.
        template<class T>
        T& AddContent(T&& child) { return this->AddChild(std::forward<T>(child)); }

        // -------------------------------------------------------------------
        //  Measure / theme / paint
        // -------------------------------------------------------------------
        float hintWidth() const override;
        float hintHeight() const override;

        void ApplyTheme(const Theme& t) override;

        /// @brief Paints the container skeleton first (`Panel::Paint`), then tiles the watermark; children are painted recursively by the window
        void Paint(FrameCollector& fc) const override;

    private:
        /// @brief Tiles the text watermark once (called by `Paint`)
        void paintTextTiles(FrameCollector& fc) const;
        /// @brief Tiles the image watermark once (called by `Paint`)
        void paintImageTiles(FrameCollector& fc) const;

        std::string text_;
        std::string imagePath_;

        Color4f color_{ 0.0f, 0.0f, 0.0f, 0.12f };
        Color4f imageTint_{ 1.0f, 1.0f, 1.0f, 0.12f };
        float   gapX_ = 96.0f;
        float   gapY_ = 64.0f;
        float   offsetX_ = 0.0f;
        float   offsetY_ = 0.0f;
        bool    stagger_ = false;
        bool    fontSizeSet_ = false;
        unsigned int fontSize_ = 0;          /// < explicit font size (0 = follow theme)

        TextStyle textStyle_;
    };

} // namespace Kit
