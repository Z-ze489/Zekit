/**
 * @file ZeToolBar.h
 * @brief Toolbar: a horizontal action bar at the top of a window / panel (or any position)
 *
 * @details
 * `ToolBar : Panel` -- horizontal arrangement, spacing, padding and child clipping all come from `Panel`;
 * this class only adds the four things specific to a toolbar:
 *
 *  1. A skeleton background: base color + optional border + bottom divider line (`toolbar` theme section);
 *  2. Children are vertically centered by default: buttons should not be stretched to the full bar height, so the cross axis defaults to `Center`
 *     (to fill, use `SetAlign(AlignItems::Stretch)` -- that is an inherited interface);
 *  3. Separator: `AddSeparator()` inserts a vertical line -- it is a placeholder child widget rather than
 *     a coordinate recorded by this class, so it follows the layout automatically (no need to recompute positions on every layout);
 *  4. Flexible space: `AddSpacer()` inserts a `flexGrow` blank that pushes the following children to the other end
 *     (the most common toolbar need: "one group on the left, one group on the right").
 *
 * @par Usage
 * @code
 * Kit::ToolBar tb;
 * tb.AddChild(Kit::Button("New"));
 * tb.AddChild(Kit::Button("Open"));
 * tb.AddSeparator();                     // vertical line between groups
 * tb.AddChild(Kit::Button("Save"));
 * tb.AddSpacer();                        // flexible space: pushes the rest to the right end
 * tb.AddChild(Kit::Label("Ready"));
 * win.AddWidget(tb);
 * @endcode
 *
 * @note The toolbar does not automatically fill anything beyond its width: it is an ordinary container, and when placed in
 *       a `VBoxLayout` it occupies the content height and fills horizontally (`Stretch` is the default
 *       cross-axis alignment of a vertical container). To pin it to the top of the window, use a layout or call `SetGeometry` directly.
 *
 * @see ZePanel.h     base class (horizontal arrangement / padding / spacing)
 * @see ZeStatusBar.h the other end of the same family (bottom status bar)
 */

#pragma once
#include "ZePanel.h"
#include "ZeTheme.h"

#include <cstddef>
#include <vector>

namespace Kit {

    /**
     * @brief Toolbar
     *
     * @details
     * The theme uses the `toolbar` section: `background` base color, `borderColor` border,
     * `dividerColor` divider, `padding` padding, `body` text baseline.
     */
    class ToolBar : public Panel {
    public:
        ToolBar();

        // -------------------------------------------------------------------
        //  Content
        // -------------------------------------------------------------------

        /// @brief Inserts a vertical divider line (not hit-testable -- it should not be treated as clickable)
        /// @return the separator index (0-based, for use with `SetSeparatorWidth`)
        int AddSeparator();

        /// @brief Inserts a flexible blank: pushes the following children to the other end
        /// @param grow flex weight; default 1 (multiple blanks share the remaining space by weight)
        /// @return reference to the blank widget (usually not needed)
        Widget& AddSpacer(float grow = 1.0f);

        /// @brief Changes the line width of a separator (pixels)
        /// @param index the return value of `AddSeparator()`; out-of-range is silently ignored
        void SetSeparatorWidth(int index, float width);

        /// @brief Number of separators
        std::size_t separatorCount() const { return separators_.size(); }

        // -------------------------------------------------------------------
        //  Measurement protocol
        // -------------------------------------------------------------------
        //  The toolbar is a horizontal container, so width = sum of children + spacing + padding,
        //  height = tallest visible child + top and bottom padding. The default hint of `LayoutBase` is
        //  computed for the vertical direction (width = max, height = sum), which is exactly reversed for a toolbar, so both sides are overridden.
        float hintWidth() const override;
        float hintHeight() const override;

        // -------------------------------------------------------------------
        //  Theme
        // -------------------------------------------------------------------
        // @brief Re-injects from the `toolbar` theme section (base color / border / divider / padding)
        void ApplyTheme(const Theme& t) override;

        // -------------------------------------------------------------------
        //  Paint
        // -------------------------------------------------------------------
        // @brief Skeleton (border + base color + bottom divider line); children are painted recursively by the window
        void Paint(FrameCollector& fc) const override;

    private:
        /// @brief Placeholder child widget: only occupies a position in the layout and draws a line itself
        /// @note Derives from `Widget` and does not lay out children (it never holds children), so when it is
        ///       added internally it goes through `AddWidgetUnchecked` -- that is the way to say "I know this widget
        ///       does not lay out, layout is mine" (see "19 Custom Widgets" section 8.2).
        class Strip : public Widget {
        public:
            /// @param vertical true = vertical line (for toolbars), false = horizontal line (for status bars)
            explicit Strip(bool vertical) : vertical_(vertical) {}

            void Paint(FrameCollector& fc) const override;
            void ApplyTheme(const Theme& t) override;

            /// @param thickness line width (pixels)
            void SetThickness(float thickness);
            float thickness() const { return thickness_; }
            bool isVertical() const { return vertical_; }

        private:
            bool  vertical_ = true;
            float thickness_ = 1.0f;
            float ratio_ = 0.62f;      /// < ratio of line length to cross-axis length (blank at both ends)
        };

        /// @brief Flexible blank: draws nothing, only contributes `flexGrow`
        class Spacer : public Widget {
        public:
            void Paint(FrameCollector& fc) const override { (void)fc; }
            void ApplyTheme(const Theme&) override {}
        };

        std::vector<Strip*> separators_;   /// < separator child widgets (borrowed; addresses are stable)
        SurfaceStyle styleTheme_;          /// < skeleton style injected by the theme (plain value)
    };

} // namespace Kit
