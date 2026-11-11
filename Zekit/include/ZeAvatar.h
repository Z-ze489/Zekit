/**
 * @file ZeAvatar.h
 * @brief Avatar: a square / circular base + (image or initials) + optional status dot
 *
 * @details
 * `Avatar : BoxWidget` -- a fixed-edge-length square (`SetSize(px)`) that paints itself:
 *  1. shape base (circle / rounded square / square) -- an image when present, otherwise "palette color picked by name + initials";
 *  2. outer ring stroke (to "cut" the avatar out of the background);
 *  3. status dot at the bottom-right (online / offline / busy / away).
 *
 * @par How the color is picked without an image
 * After `SetName("Zhang San")`: base color = the entry selected by name hash in the theme `avatar.palette`,
 * text = first character of the name (UTF-8 safe). Thus every person in the same roster gets a stable and distinct base color,
 * and no manual color assignment by the user is needed.
 *
 * @par WARNING: known limitation of circular avatar + image
 * The clip region of this library is rectangle-only (`FrameCollector::PushClip`); there is no circular mask channel.
 * So a photo in a circular avatar is filled proportionally as an inscribed square (corners stay inside the circle), not "clipped inside the circle".
 * For a true circular photo, provide a PNG that is itself circular (with transparent corners).
 *
 * @code
 * Kit::Avatar a("Zhang San");
 * a.SetSize(48.0f);
 * a.SetShape(Kit::AvatarShape::Circle);
 * a.SetStatus(Kit::AvatarStatus::Online);
 * a.SetImage("face.png");          // the image can also be added later
 * @endcode
 *
 * @see ZeTheme.h `AvatarStyle`
 */

#pragma once
#include "ZeBoxWidget.h"
#include "ZeTextStyle.h"

#include <string>

namespace Kit {

    /// @brief Avatar shape
    enum class AvatarShape {
        Circle,    /// < Circle (default)
        Rounded,   /// < Rounded square
        Square     /// < Square with right angles
    };

    /// @brief Status dot (bottom-right)
    enum class AvatarStatus {
        None,      /// < Not painted (default)
        Online,    /// < Online (green)
        Offline,   /// < Offline (gray)
        Busy,      /// < Busy (red)
        Away       /// < Away (yellow)
    };

    /**
     * @brief Avatar widget
     */
    class Avatar : public BoxWidget {
    public:
        /// @brief Constructs
        /// @param name Name (used for initials and automatic coloring; may be empty)
        explicit Avatar(std::string name = std::string());

        // -------------------------------------------------------------------
        //  Data
        // -------------------------------------------------------------------

        /// @brief Sets the name (also updates initials and automatic coloring)
        void SetName(std::string name);
        const std::string& name() const { return name_; }

        /// @brief Explicitly sets the text shown when there is no image (empty = use name initials; also empty = paint a default person shape)
        void SetFallbackText(std::string text);
        const std::string& fallbackText() const { return fallbackText_; }

        // -------------------------------------------------------------------
        //  Image
        // -------------------------------------------------------------------
        // @brief Sets the avatar image (empty string = clear, back to initials)
        // @note Only the path is stored; decoding / texture upload is done in the render layer (same as the `Button` icon).
        void SetImage(std::string path);
        void ClearImage();
        const std::string& imagePath() const { return imagePath_; }
        bool hasImage() const { return !imagePath_.empty(); }

        // -------------------------------------------------------------------
        //  Appearance (value interface)
        // -------------------------------------------------------------------

        /// @brief Sets the edge length (square, pixels); also serves as `hintWidth` / `hintHeight`
        /// @note Deliberately not named `SetSize` -- that would shadow `Widget::SetSize(w, h)` and make
        ///       existing code such as `avatar.SetSize(48, 48)` fail to compile.
        void SetSide(float side);
        /// @brief Current edge length
        float side() const { return side_; }

        void SetShape(AvatarShape shape);
        AvatarShape shape() const { return shape_; }

        /// @brief Overrides the base color (after override, color is no longer picked by name)
        void SetAvatarColor(const Color4f& c);
        /// @brief Clears the override, back to "color picked by name"
        void ClearAvatarColor();
        /// @brief Whether the base color has been explicitly overridden
        bool hasAvatarColor() const { return colorSet_; }

        /// @brief Sets the status dot
        void SetStatus(AvatarStatus s);
        AvatarStatus status() const { return status_; }

        /// @brief Outer ring stroke width (pixels; 0 = no stroke)
        /// @note Named `SetRingWidth` rather than `SetBorderWidth` -- the latter is the
        ///       `BoxWidget` box border, which is not the same as the "avatar outer ring"; do not shadow each other.
        void SetRingWidth(float px);

        // -------------------------------------------------------------------
        //  Measure protocol (square)
        // -------------------------------------------------------------------
        float hintWidth() const override;
        float hintHeight() const override;

        // -------------------------------------------------------------------
        //  Theme
        // -------------------------------------------------------------------
        void ApplyTheme(const Theme& t) override;

    protected:
        /// @brief Shape base -> image / initials -> stroke -> status dot
        void paintContent(FrameCollector& fc) const override;

    private:
        /// @brief Currently effective base color (override value first, otherwise palette picked by name hash)
        Color4f resolvedColor() const;
        /// @brief Text to show when there is no image (fallbackText_ first, otherwise first character of the name)
        std::string resolvedInitials() const;
        /// @brief Name hash -> palette index
        int paletteIndex() const;

        std::string name_;
        std::string fallbackText_;
        std::string imagePath_;

        float side_ = 40.0f;
        AvatarShape shape_ = AvatarShape::Circle;
        AvatarStatus status_ = AvatarStatus::None;

        bool    colorSet_ = false;
        Color4f colorOverride_{ 0.0f, 0.0f, 0.0f, 1.0f };

        bool    borderSet_ = false;
        float   borderWidthOverride_ = -1.0f;

        // Theme injection
        TextStyle initialsStyle_;
        float     themeBorderWidth_ = 2.0f;
        Color4f   themeBorderColor_{ 1.0f, 1.0f, 1.0f, 1.0f };
        Color4f   fallbackColor_{ 0.0f, 0.0f, 0.0f, 1.0f };
        float     statusSize_ = 10.0f;
        float     statusBorderWidth_ = 2.0f;
        Color4f   statusBorderColor_{ 1.0f, 1.0f, 1.0f, 1.0f };
        Color4f   statusOnline_{ 0.0f, 1.0f, 0.0f, 1.0f };
        Color4f   statusOffline_{ 0.5f, 0.5f, 0.5f, 1.0f };
        Color4f   statusBusy_{ 1.0f, 0.0f, 0.0f, 1.0f };
        Color4f   statusAway_{ 1.0f, 0.75f, 0.0f, 1.0f };
        Color4f   palette_[6]{};
    };

} // namespace Kit
