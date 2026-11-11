/**
 * @file ZeIcon.h
 * @brief Icon: a set of built-in vector glyphs, or an image
 *
 * @details
 * This library does not depend on any icon font / icon pack, so `Icon` offers two paths:
 *
 *  1. Built-in glyphs (`IconGlyph`) -- vector mini-icons drawn on the spot with the library's
 *     drawing primitives (circle / rounded rect / polygon / thick line segments) (check / cross /
 *     plus / minus / arrows / magnifier / more / info / warning...). Benefits: zero resource
 *     dependency, crisp at any size, color follows the theme;
 *  2. Image icon (`SetImage`) -- stores only the path; decoding and texture upload happen in the
 *     render layer (same pattern as the `Button` icon); `SetColor` can tint the whole image;
 *  3. Resource icon (`SetImageFromResource`) -- the image is compiled into the executable, so no
 *     loose image files are needed at runtime (the "UI content" version of the same idea as
 *     `Window::SetWindowIconFromResource`). On Windows it reads RCDATA from `.rc`; on other
 *     platforms it reads the in-memory bytes registered via `Kit::RegisterEmbeddedResource`
 *     (see `ZeEmbeddedRes.h`).
 *
 * @par Size and color
 * Side length = `SetIconSize(px)` (defaults to theme `icon.size`); color = `SetColor` (defaults
 * to theme `icon.color`); stroke width = `SetStroke` (defaults to theme `icon.stroke`).
 * When none of the three is set, everything follows the theme, so light/dark theme switching
 * follows automatically.
 *
 * @code
 * Kit::Icon search(Kit::IconGlyph::Search);
 * search.SetIconSize(20.0f);
 * Kit::Icon logo;
 * logo.SetImage("logo.png");
 * @endcode
 *
 * @see ZeTheme.h `IconStyle`
 */

#pragma once
#include "ZeBoxWidget.h"

#include <string>

namespace Kit {

    /// @brief Built-in glyphs
    enum class IconGlyph {
        None,          /// < Draws no glyph (used together with `SetImage`)
        Check,         /// < Check
        Close,         /// < Cross
        Plus,          /// < Plus
        Minus,         /// < Minus
        ChevronUp,     /// < Chevron (up)
        ChevronDown,   /// < Chevron (down)
        ChevronLeft,   /// < Chevron (left)
        ChevronRight,  /// < Chevron (right)
        ArrowUp,       /// < Triangle arrow (up)
        ArrowDown,     /// < Triangle arrow (down)
        Search,        /// < Magnifier
        More,          /// < Three dots (more)
        Info,          /// < Info (circle + i)
        Warning        /// < Warning (triangle + !)
    };

    /**
     * @brief Icon widget
     */
    class Icon : public BoxWidget {
    public:
        /// @brief Constructs
        /// @param glyph Built-in glyph (default `None`; for an image icon leave it empty and call `SetImage`)
        explicit Icon(IconGlyph glyph = IconGlyph::None);

        // -------------------------------------------------------------------
        //  Glyph / image
        // -------------------------------------------------------------------

        /// @brief Switches the built-in glyph (clears the image icon -- the two are mutually exclusive, "the last one set wins")
        void SetGlyph(IconGlyph glyph);
        IconGlyph glyph() const { return glyph_; }

        /// @brief Sets the image icon (empty string = clear, back to the built-in glyph)
        void SetImage(std::string path);
        void ClearImage();
        const std::string& imagePath() const { return imagePath_; }
        bool hasImage() const { return !imagePath_.empty(); }

        /// @brief Sets the image icon from an embedded resource of the executable (cross-platform)
        /// @param resId Resource ID -- points to image bytes (embedded PNG/JPG etc.).
        ///              Note this is not a Windows ICON resource: SOIL2 cannot decode .ico
        ///              containers; for a title bar / taskbar icon from an icon resource use
        ///              Window::SetWindowIconFromResource.
        /// @details
        /// The image ships with the executable, so no loose image files are needed at runtime.
        /// The byte source has two paths by platform (caller code is the same):
        ///  - Windows: `FindResourceW(RT_RCDATA)` reads RCDATA from `.rc`;
        ///  - Other platforms: looks up the in-memory bytes registered via
        ///    `Kit::RegisterEmbeddedResource(id, bytes, size)` (compile the png into a C array and
        ///    register it once).
        /// After that it is always SOIL2 in-memory decode -> texture upload (cached under the pseudo
        /// path `":res:<id>"`, zero cost after the first decode).
        /// Mutually exclusive with SetImage / SetGlyph -- "the last one set wins".
        /// @note If the resource does not exist / the ID is not registered, logs one line and treats it as
        /// "load failed" (no image shown).
        /// @see RegisterEmbeddedResource
        void SetImageFromResource(int resId);
        void ClearImageResource();
        int  imageResource() const { return imageResId_; }
        bool hasResourceImage() const { return imageResId_ != 0; }

        /// @brief Tint of the image icon (white = as-is; used to tint a monochrome icon with the theme color)
        void SetImageTint(const Color4f& c);

        // -------------------------------------------------------------------
        //  Appearance (value interface)
        // -------------------------------------------------------------------

        /// @brief Side length (square, pixels); <= 0 = follow theme
        void SetIconSize(float px);
        float iconSize() const { return sizeOverride_; }

        /// @brief Glyph color
        void SetColor(const Color4f& c);
        const Color4f& color() const { return color_; }

        /// @brief Stroke width (pixels); <= 0 = follow theme
        void SetStroke(float px);
        float stroke() const { return strokeOverride_; }

        // -------------------------------------------------------------------
        //  Measure protocol
        // -------------------------------------------------------------------
        float hintWidth() const override;
        float hintHeight() const override;

        // -------------------------------------------------------------------
        //  Theme
        // -------------------------------------------------------------------
        void ApplyTheme(const Theme& t) override;

    protected:
        /// @brief Paints the glyph or image
        void paintContent(FrameCollector& fc) const override;

    private:
        /// @brief Current effective side length
        float resolvedSize() const;
        /// @brief Current effective stroke width
        float resolvedStroke() const;

        IconGlyph   glyph_ = IconGlyph::None;
        std::string imagePath_;
        int         imageResId_ = 0;   /// < RCDATA resource icon (mutually exclusive with imagePath_, "last one set wins")
        Color4f     imageTint_{ 1.0f, 1.0f, 1.0f, 1.0f };

        bool    glyphSet_ = false;      /// < User explicitly set the glyph color
        Color4f color_{ 0.0f, 0.0f, 0.0f, 1.0f };
        float   sizeOverride_ = 0.0f;
        float   strokeOverride_ = 0.0f;

        // Theme injection
        float   themeSize_ = 16.0f;
        float   themeStroke_ = 1.6f;
    };

} // namespace Kit
