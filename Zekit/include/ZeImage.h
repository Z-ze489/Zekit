/**
 * @file ZeImage.h
 * @brief Image: places an image into a rectangle according to the specified mode
 *
 * @details
 * `Image : BoxWidget` -- the box layer (background color / border / radius / padding) uses the theme `image.box`
 * (the same `BoxWidget` pipeline as the existing widgets); the image itself is handed to
 * `FrameCollector::AddImage` in `paintContent`.
 *
 * @par Scale mode
 * Reuses the single enum `BgImageDrawMode` from the whole library (`image_types.h`):
 * `Fit` (contain, default) / `Fill` (cover) / `Stretch` / `Origin` / `Tiled`.
 * No separate enum -- that would inevitably drift from the original enum.
 *
 * @par Stores only the path, does not load
 * `SetSource` only stores the string; decoding and texture upload happen in the render layer (`ImageStore`),
 * so "configure the image before adding it to the window" is legal (same pattern as the `Button` icon).
 *
 * @par Size
 * Without an explicit `SetSize`, the size is derived from `SetAspectRatio(w/h)`; if neither is given, falls back to a 160x120 default box.
 * Overrides `hintWidth` / `hintHeight` (mandatory for leaf widgets, otherwise they silently collapse to 0 in a layout).
 *
 * @par WARNING: radius applies only to the **outer frame**
 * The clipping domain of this library is rectangle-only; there is no rounded / circular mask channel -- so `SetRadius` rounds
 * the background box and stroke, while the image itself is drawn as a rectangle (the corners are not "rounded off"). For rounded photos, let the image carry its own rounded corners.
 *
 * @code
 * Kit::Image cover;
 * cover.SetSource("cover.jpg");
 * cover.SetDrawMode(Kit::BgImageDrawMode::Fill);   // cover
 * cover.SetRadius(8.0f);
 * @endcode
 *
 * @see ZeTheme.h `ImageStyle`
 * @see image_types.h `BgImageDrawMode`
 */

#pragma once
#include "ZeBoxWidget.h"

#include <string>

namespace Kit {

    /**
     * @brief Image widget
     */
    class Image : public BoxWidget {
    public:
        /// @brief Constructs
        /// @param path Image path (may be empty, call `SetSource` later)
        explicit Image(std::string path = std::string());

        // -------------------------------------------------------------------
        //  Data
        // -------------------------------------------------------------------

        /// @brief Sets the image path (empty string = clear, back to placeholder state)
        void SetSource(std::string path);
        const std::string& source() const { return path_; }
        bool hasSource() const { return !path_.empty(); }

        // -------------------------------------------------------------------
        //  Appearance (value interface)
        // -------------------------------------------------------------------

        /// @brief Scale mode (default `Fit`)
        void SetDrawMode(BgImageDrawMode mode);
        BgImageDrawMode drawMode() const { return drawMode_; }

        /// @brief Alignment (**only effective in `Origin` mode** -- consistent with the convention in `image_types.h`)
        void SetImageAlign(BgImageHAlign h, BgImageVAlign v);

        /// @brief Tint (multiplied component-wise with the texture; white = output as-is)
        void SetTint(const Color4f& c);
        const Color4f& tint() const { return tint_; }

        /// @brief Aspect ratio of the source image (w/h), used to derive the other side when "only one side is set"; <= 0 = unknown
        void SetAspectRatio(float ratio);
        float aspectRatio() const { return aspectRatio_; }

        /// @brief Radius (pixels) -- applies to the outer frame, not to the image itself (see the file header note)
        void SetCornerRadius(float radius);

        /// @brief Placeholder background color when there is no image
        void SetPlaceholderColor(const Color4f& c);

        // -------------------------------------------------------------------
        //  Measurement protocol
        // -------------------------------------------------------------------
        float hintWidth() const override;
        float hintHeight() const override;

        // -------------------------------------------------------------------
        //  Theme
        // -------------------------------------------------------------------
        void ApplyTheme(const Theme& t) override;

    protected:
        /// @brief Default box style taken from the theme `image` section
        const BoxStyle& themeBox(const Theme& t) const override;

        /// @brief Paints the image (or the placeholder)
        void paintContent(FrameCollector& fc) const override;

    private:
        std::string     path_;
        BgImageDrawMode drawMode_ = BgImageDrawMode::Fit;
        BgImageHAlign   hAlign_ = BgImageHAlign::Center;
        BgImageVAlign   vAlign_ = BgImageVAlign::Middle;
        Color4f         tint_{ 1.0f, 1.0f, 1.0f, 1.0f };
        float           aspectRatio_ = 0.0f;

        bool    placeholderSet_ = false;
        Color4f placeholderColor_{ 0.0f, 0.0f, 0.0f, 0.0f };
        Color4f placeholderGlyph_{ 0.0f, 0.0f, 0.0f, 1.0f };
    };

} // namespace Kit
