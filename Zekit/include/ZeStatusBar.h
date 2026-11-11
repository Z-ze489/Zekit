/**
 * @file ZeStatusBar.h
 * @brief Status bar: an information strip at the bottom of a window / panel (`Ready  |  Ln 3, Col 12`)
 *
 * @details
 * `StatusBar : Panel` -- horizontal arrangement, spacing, padding and child clipping all come from `Panel`.
 * This class and `ToolBar` are the two ends of the same family (one on top, one on the bottom), with an isomorphic skeleton:
 *
 *  | | `ToolBar` | `StatusBar` |
 *  |---|---|---|
 *  | Background | `surface` (the toolbar is the main action surface) | `bg` (status is auxiliary information, one notch quieter) |
 *  | Divider position | bottom edge (what is below it is the content) | top edge (what is above it is the content) |
 *  | Cross-axis alignment | `Center` (buttons do not fill) | `Center` (text does not fill) |
 *  | Common children | buttons / dividers / stretch | text sections / dividers / stretch |
 *
 * @par Sections (the most common usage)
 * `AddSection` directly gives you a left-aligned `Label` and also handles inserting the divider:
 *
 * @code
 * Kit::StatusBar sb;
 * sb.AddSection("Ready");
 * sb.AddStretch();                       // stretch: subsequent sections are pushed to the right end
 * sb.AddSection("Ln 3, Col 12");
 * sb.AddSection("UTF-8");
 * sb.AddChild(Kit::ProgressBar());        // to place other widgets, just use AddChild
 * win.AddWidget(sb);
 * @endcode
 *
 * @note The automatically inserted dividers are only added between sections (there is no line before the first section) -- this is
 *       a status bar, not a game menu; an extra vertical line at the start only looks dirty.
 *
 * @see ZePanel.h     base class (horizontal arrangement / padding / spacing)
 * @see ZeToolBar.h   the other end of the same family (top action bar)
 */

#pragma once
#include "ZePanel.h"
#include "ZeTheme.h"

#include <cstddef>
#include <string>
#include <vector>

namespace Kit {

    class Label;   /// < `AddSection` returns a reference to it (a forward declaration suffices)

    /**
     * @brief Status bar
     *
     * @details
     * The theme reads the `statusBar` section: `background` background color, `dividerColor` top-edge divider,
     * `padding` padding, `body` section text (muted by default -- status should not compete with content).
     */
    class StatusBar : public Panel {
    public:
        StatusBar();

        // -------------------------------------------------------------------
        //  Sections
        // -------------------------------------------------------------------

        /// @brief Appends a text section (automatically inserts a divider before it)
        /// @param text section text
        /// @return reference to the `Label` that holds it (use it to change text / style)
        Label& AddSection(std::string text);

        /// @brief Appends a stretch: pushes the following sections to the other end
        /// @param grow stretch weight; default 1
        void AddStretch(float grow = 1.0f);

        /// @brief Sets the text of the index-th section (out of range is silently ignored)
        /// @note The index is the section ordinal (excluding automatically inserted dividers) and corresponds to `sectionCount()`.
        void SetSectionText(int index, const std::string& text);

        /// @brief Returns the text of the index-th section (returns a static empty string if out of range)
        const std::string& sectionText(int index) const;

        /// @brief Number of sections
        std::size_t sectionCount() const { return sections_.size(); }

        /// @brief Clears all sections (together with the automatically inserted dividers)
        void ClearSections();

        // -------------------------------------------------------------------
        //  Appearance
        // -------------------------------------------------------------------

        /// @brief Whether automatically inserted dividers are enabled (default true)
        void SetShowDividers(bool on);

        // -------------------------------------------------------------------
        //  Measurement protocol
        // -------------------------------------------------------------------
        //  The status bar is a horizontal container: width = sum of children, height = tallest child + top and bottom padding.
        float hintWidth() const override;
        float hintHeight() const override;

        // -------------------------------------------------------------------
        //  Theme
        // -------------------------------------------------------------------
        // @brief Re-injects from the theme `statusBar` section (background / divider / padding)
        void ApplyTheme(const Theme& t) override;

        // -------------------------------------------------------------------
        //  Paint
        // -------------------------------------------------------------------
        // @brief Skeleton (border + background + top-edge divider)
        void Paint(FrameCollector& fc) const override;

    private:
        /// @brief Placeholder child widget: a vertical divider (same technique as `ToolBar::Strip`)
        /// @note Each widget keeps its own copy instead of hoisting it into a common header -- it is only about ten lines,
        ///       and the theme sections differ on each side (the toolbar uses `toolbar.dividerColor`,
        ///       the status bar uses `statusBar.dividerColor`). Sharing it would require an extra "which section" parameter.
        class Divider : public Widget {
        public:
            void Paint(FrameCollector& fc) const override;
            void ApplyTheme(const Theme& t) override;
            void SetThickness(float thickness);
            float thickness() const { return thickness_; }

        private:
            float thickness_ = 1.0f;
            float ratio_ = 0.62f;
        };

        /// @brief Stretch (paints nothing, only contributes `flexGrow`)
        class Stretch : public Widget {
        public:
            void Paint(FrameCollector& fc) const override { (void)fc; }
            void ApplyTheme(const Theme&) override {}
        };

        /// @brief Prepares a divider for the next section (not added before the first section)
        void ensureLeadingDivider();

        std::vector<Divider*> dividers_;   /// < automatically inserted dividers (borrowed; addresses stable)
        std::vector<Label*>   sections_;   /// < sections (borrowed; addresses stable)
        bool showDividers_ = true;
        SurfaceStyle styleTheme_;          /// < skeleton style injected from the theme (plain value)
    };

} // namespace Kit
