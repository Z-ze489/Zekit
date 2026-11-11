/**
 * @file ZeSidebar.h
 * @brief Sidebar: a vertical navigation list attached to one edge of a window (or panel)
 *
 * @details
 * `Sidebar : ItemStripBase` -- this class is the vertical usage of that base class
 * (`SetOrientation(StripOrientation::Vertical)`): items stacked vertically, whole row clickable,
 * mouse and arrow-key navigation, and "paint and hit test share one geometry" are all inherited.
 *
 * This class adds five sidebar-specific things on top of the base class:
 *
 *  1. Left-aligned full-row items: the horizontal base class centers items into a square block, but a sidebar wants
 *     "the whole row highlights, text left-aligned" -- override `paintStripItem`;
 *  2. An accent bar on the left of the current item: a clearer "you are on this page" signal than
 *     "the whole row changes background";
 *  3. Header / footer: `SetHeader` (group name, one notch quieter) / `SetFooter` (version number and the like),
 *     neither needs an extra child widget, both are painted directly (saves a tree layer);
 *  4. Collapsible: `SetCollapsed(true)` leaves only a narrow edge (driven by `AddToggleButton` or an external button);
 *  5. An edge-hugging skeleton: background color + three borders turned off (only the inner line of a sidebar is meaningful).
 *
 * @par Usage
 * @code
 * Kit::Sidebar side;
 * side.SetHeader("ZEKIT");
 * side.AddItem("Overview");
 * side.AddItem("Widgets");
 * side.AddItem("Settings");
 * side.SetCurrentItem(0);
 * side.SetOnItemActivated([](Kit::Widget& self, int i) {
 *     std::printf("switched to item %d\n", i);
 * });
 *
 * // Sidebar + content area: side by side in a horizontal layout
 * Kit::HBoxLayout& root = win.AddWidget(Kit::HBoxLayout());
 * side.SetDefaultSize(180.0f, 0.0f);
 * root.AddChild(side);
 * root.AddChild(contentPanel);
 * @endcode
 *
 * @note `SetCurrentItem` (programmatic) does not trigger the callback; only clicks / arrow keys do
 *       -- the same rule as `TabView` / `ComboBox` / `Pagination`.
 *
 * @see ZeItemStripBase.h  the library's common base class (item model / geometry / input; vertical is one of its orientations)
 * @see ZeDrawer.h         drawer (a floating sidebar; see that header for a comparison of the two)
 */

#pragma once
#include "../ZeItemStripBase.h"

#include <cstddef>
#include <string>
#include <vector>

namespace Kit {

    /**
     * @brief Sidebar navigation
     *
     * @details
     * The theme takes the `sidebar` section (`SurfaceStyle`): background color / inner separator line / group title text.
     * Item text still takes the `itemStrip` section -- so sidebar items look naturally consistent with breadcrumb / pagination items.
     */
    class Sidebar : public ItemStripBase {
    public:
        Sidebar();

        // -------------------------------------------------------------------
        //  Items (navigation items)
        // -------------------------------------------------------------------

        /// @brief Appends a navigation item
        /// @param label item text
        /// @param enabled whether it is clickable (clickable by default)
        /// @return index of the new item (0-based)
        int AddItem(std::string label, bool enabled = true);

        /// @brief Sets all navigation items at once
        void SetItems(const std::vector<std::string>& labels);

        /// @brief Returns the text of one item (returns a static empty string if out of range)
        const std::string& itemText(int index) const { return itemLabel(index); }

        // -------------------------------------------------------------------
        //  Current item
        // -------------------------------------------------------------------

        /// @brief Sets the current item programmatically (does not trigger the callback; silently ignored if out of range)
        void SetCurrentItem(int index) { SetCurrentIndex(index);  RequestWindowRedraw();}
        /// @brief Index of the current item (-1 when there are no items)
        int currentItem() const { return currentIndex(); }

        // -------------------------------------------------------------------
        //  Header / footer
        // -------------------------------------------------------------------

        /// @brief Sets the top title (empty string = takes no space)
        void SetHeader(std::string text);
        /// @brief Returns the current top title
        const std::string& header() const { return header_; }

        /// @brief Sets the bottom footer (empty string = takes no space)
        void SetFooter(std::string text);
        /// @brief Returns the current bottom footer
        const std::string& footer() const { return footer_; }

        /// @brief Height of the header / footer area (pixels; 0 = follow the theme's `ctrlHM`)
        void SetHeaderHeight(float h);
        void SetFooterHeight(float h);

        // -------------------------------------------------------------------
        //  Collapse
        // -------------------------------------------------------------------

        /// @brief Collapses to a narrow edge (only the accent bar and a short label of the current item remain)
        void SetCollapsed(bool collapsed);
        /// @brief Whether it is collapsed
        bool isCollapsed() const { return collapsed_; }

        /// @brief Width in the collapsed state (pixels, default 48)
        void SetCollapsedWidth(float w);
        /// @brief Returns the current collapsed width
        float collapsedWidth() const { return collapsedWidth_; }

        // -------------------------------------------------------------------
        //  Appearance
        // -------------------------------------------------------------------

        /// @brief Width of the accent bar on the left of the current item (pixels; 0 = no accent bar)
        void SetIndicatorWidth(float w);
        /// @brief Returns the current accent bar width
        float indicatorWidth() const { return indicatorWidth_; }

        /// @brief Accent bar color (defaults to the theme's item accent color)
        void SetIndicatorColor(const Color4f& c);
        /// @brief Returns the current accent bar color
        const Color4f& indicatorColor() const { return indicatorColor_; }

        // -------------------------------------------------------------------
        //  Callbacks (triggered only on the user path)
        // -------------------------------------------------------------------

        /// @brief Sets the "user clicked an item" callback (with sender + item index)
        void SetOnItemActivated(SelfAction1<int> cb) { onActivated_ = std::move(cb); }
        /// @brief Sets the "user clicked an item" callback (form that only cares about the sender)
        void SetOnItemActivated(SelfAction cb) { onActivatedSelf_ = std::move(cb); }

        // -------------------------------------------------------------------
        //  Measurement protocol
        // -------------------------------------------------------------------
        float hintWidth() const override;
        float hintHeight() const override;

        // -------------------------------------------------------------------
        //  Theme
        // -------------------------------------------------------------------
        // @brief Re-injects from the theme's `sidebar` section (skeleton) + `itemStrip` section (item text)
        void ApplyTheme(const Theme& t) override;

        // -------------------------------------------------------------------
        //  Paint
        // -------------------------------------------------------------------
        // @brief Skeleton (background color + inner line) -> header -> items -> footer
        void Paint(FrameCollector& fc) const override;

    protected:
        // -------------------------------------------------------------------
        //  Base class hooks
        // -------------------------------------------------------------------

        /// @brief Main-axis size of an item: vertical means this is the row height (theme itemHeight or fallback)
        float measureStripItem(int index) const override;

        /// @brief Paints one item: full-row background (current / hover) + left accent bar + left-aligned text
        void paintStripItem(FrameCollector& fc, int index, const ItemRect& r) const override;

        /// @brief User clicked an item -> forwards to the activation callback
        void OnCurrentChangedByUser(int index) override;

    private:
        /// @brief Item area (the available rect after subtracting header / footer) -- item geometry lands here
        /// @note The base class's `stripArea` uses the whole content area, but a sidebar must give up top and bottom space to header and footer.
        ///       Approach: write the header / footer heights into the layout padding (`SetPadding`),
        ///       so the base class's `stripArea` is naturally the item area -- the same trick as `TabView` writing the tab bar height
        ///       as top padding; this class does not need to override geometry.
        void syncPadding();

        std::string header_;
        std::string footer_;
        float headerHeight_ = 0.0f;      /// < 0 = follow the theme
        float footerHeight_ = 0.0f;      /// < 0 = follow the theme

        bool  collapsed_ = false;
        float collapsedWidth_ = 48.0f;
        float expandedWidth_ = 0.0f;     /// < width before collapsing (used to restore on expand)

        float   indicatorWidth_ = 3.0f;
        Color4f indicatorColor_{ 0.0f, 0.0f, 0.0f, 0.0f };

        float bodyPad_ = 8.0f;           /// < theme padding (saved for reuse by syncPadding)

        // ---- Callbacks ----
        SelfAction1<int> onActivated_;
        SelfAction       onActivatedSelf_;
    };

} // namespace Kit
