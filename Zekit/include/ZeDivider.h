/**
 * @file ZeDivider.h
 * @brief Divider: a thin line (horizontal / vertical), optionally with a line of text in the middle
 *
 * @details
 * `Divider : BoxWidget` -- but it does NOT use the box appearance of `BoxWidget` (background / border fully
 * transparent, padding 0), only its "paint pipeline + content clipping". The line itself is drawn by
 * `paintContent` using the library's paint helpers (`drawutil::PaintHLine` / `PaintVLine`) -- that is, the
 * library-wide "thin lines are rounded rectangles" convention, which naturally gives antialiased edges.
 *
 * @par Two orientations
 *  - `DividerOrientation::Horizontal` -- horizontal line. In a `VBox` the width is stretched to fill by the
 *    cross axis, height = `thickness`;
 *  - `DividerOrientation::Vertical` -- vertical divider (the one in toolbars / sidebars).
 *    In an `HBox` the height fills, width = `thickness`.
 *
 * @par Divider with text
 * `SetLabel("or")` draws `-------- or --------` (text centered, `gap` on both sides).
 * Only HORIZONTAL dividers support text -- horizontal text on a vertical line is neither readable nor does it fit.
 *
 * @par Size
 * Overrides `hintWidth` / `hintHeight`: the natural size along the main axis is `thickness` (one line),
 * the cross axis gets a fallback length (computed from the text width when there is text). NOT overriding
 * them would silently collapse to 0.
 *
 * @code
 * Kit::Divider line;                       // horizontal
 * line.SetLabel("or");
 * Kit::Divider vline(Kit::DividerOrientation::Vertical);
 * @endcode
 *
 * @see ZeTheme.h `DividerStyle`
 */

#pragma once
#include "ZeBoxWidget.h"
#include "ZeTextStyle.h"

#include <string>

namespace Kit {

    /// @brief Divider orientation
    enum class DividerOrientation {
        Horizontal,   /// < Horizontal (default): width fills, height = line width
        Vertical      /// < Vertical: height fills, width = line width
    };

    /**
     * @brief Divider widget
     */
    class Divider : public BoxWidget {
    public:
        /// @brief Constructs
        /// @param orientation Orientation (default horizontal)
        explicit Divider(DividerOrientation orientation = DividerOrientation::Horizontal);

        // -------------------------------------------------------------------
        //  Orientation
        // -------------------------------------------------------------------

        /// @brief Sets the orientation (requests relayout: the meaning of main / cross axis is swapped)
        void SetOrientation(DividerOrientation o);
        DividerOrientation orientation() const { return orientation_; }

        /// @brief Whether horizontal
        bool isHorizontal() const { return orientation_ == DividerOrientation::Horizontal; }

        // -------------------------------------------------------------------
        //  Appearance (value interface)
        // -------------------------------------------------------------------

        /// @brief Line color
        void SetColor(const Color4f& c);
        const Color4f& color() const { return color_; }

        /// @brief Line width (pixels); <= 0 is treated as 1
        void SetThickness(float px);
        float thickness() const { return thickness_; }

        /// @brief With text, the gap on both sides of the text (horizontal) / above and below (meaningless for vertical lines)
        void SetGap(float gap);
        float gap() const { return gap_; }

        // -------------------------------------------------------------------
        //  Text (horizontal dividers only)
        // -------------------------------------------------------------------

        /// @brief Sets the line of text in the middle (empty string = a plain line)
        /// @note Vertical dividers ignore text.
        void SetLabel(std::string text);
        const std::string& label() const { return label_; }

        /// @brief Text style (color / font size / weight can be changed)
        TextStyle& labelStyle() { return labelStyle_; }
        const TextStyle& labelStyle() const { return labelStyle_; }

        // -------------------------------------------------------------------
        //  Measurement protocol (required for leaf widgets, otherwise they silently collapse in layouts)
        // -------------------------------------------------------------------
        float hintWidth() const override;
        float hintHeight() const override;

        // -------------------------------------------------------------------
        //  Theme
        // -------------------------------------------------------------------
        // @brief Injects from the theme `divider` section (line color / line width / gap / text style)
        void ApplyTheme(const Theme& t) override;

    protected:
        /// @brief Paints the line (with a gap for the middle text)
        void paintContent(FrameCollector& fc) const override;

    private:
        DividerOrientation orientation_ = DividerOrientation::Horizontal;
        Color4f color_{ 0.0f, 0.0f, 0.0f, 1.0f };
        float   thickness_ = 1.0f;
        float   gap_ = 8.0f;
        std::string label_;
        TextStyle   labelStyle_;
    };

} // namespace Kit
