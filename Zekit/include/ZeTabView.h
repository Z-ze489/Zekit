/**
 * @file ZeTabView.h
 * @brief Kit::TabView -- tab bar + content area, shows one page at a time
 *
 * @details
 * A tab bar on top, a content area below; click a tab to switch, only one page is visible at a time.
 * The tab bar is drawn by the widget itself (not a pile of Button child widgets), so tab width, indicator line,
 * and hover highlight are all precisely controllable, and the tree does not gain a pile of buttons.
 *
 * @par How to reuse existing pieces
 * Inherit Kit::Panel (vertical) -- arrangement, padding, spacing, background all come for free. The key trick is
 * to write the tab bar height as the container's top padding (SetPadding(0, barHeight, 0, 0)):
 * so "the content area starts below the tab bar" is computed by the layout itself, and this class does not need to
 * manually offset the pages.
 *
 * Each page is a Panel child widget; switching relies on SetVisible -- hidden children do not participate in
 * layout (LayoutBase::visibleChildren), so the visible page naturally fills the content area.
 *
 * @par Usage
 * @code
 * Kit::TabView tv;
 * auto* page0 = tv.tabContent(tv.AddTab("General"));
 * page0->AddWidget(Kit::Label("General settings..."));
 * auto* page1 = tv.tabContent(tv.AddTab("Advanced"));
 * page1->AddWidget(Kit::Label("Advanced options..."));
 * tv.SetOnTabChanged([](Kit::Widget& self, int i) { ... });
 * win.AddWidget(tv);
 * @endcode
 *
 * @note "User switches page" and "program calls SetCurrentIndex" are two paths: only the former triggers the callback
 *       -- same rule as ComboBox / ListView.
 * @note Keyboard: Left / Right switch pages (Home / End jump to first/last). The tab bar area has no child widget
 *       covering it, so a press on a tab necessarily hits TabView itself -- no event filter needed.
 *
 * @see ZePanel.h    base class (source of arrangement / padding / background)
 * @see ZeListView.h another "list-type" widget from the same batch
 */

#pragma once
#include "ZePanel.h"
#include "ZeSelfAction.h"
#include "ZeTextStyle.h"

#include <cstddef>
#include <string>
#include <vector>

namespace Kit {

    /// @brief How the selected tab is indicated
    enum class TabIndicator {
        Line,   /// < a line at the bottom (default)
        Fill    /// < a full rounded background block
    };

    /**
     * @brief Tab view: tab bar + content area
     */
    class TabView : public Panel {
    public:
        TabView();

        // -------------------------------------------------------------------
        //  Pages
        // -------------------------------------------------------------------

        /// @brief Appends a page, returns its index
        /// @param title tab text
        /// @note A newly added page is a blank Panel; add content to it with tabContent(index).
        int AddTab(std::string title);

        /// @brief Returns the content container of a page (returns nullptr if out of range)
        /// @note Returns the address of the one in the tree (heap object, stable).
        Panel* tabContent(int index);

        /// @brief Number of pages
        std::size_t tabCount() const { return titles_.size(); }

        /// @brief Sets the tab text of a page (silently ignored if out of range)
        void SetTabTitle(int index, std::string title);
        /// @brief Returns the tab text of a page (returns a static empty string if out of range)
        const std::string& tabTitle(int index) const;

        // -------------------------------------------------------------------
        //  Current page
        // -------------------------------------------------------------------

        /// @brief Programmatically switches the current page (does not trigger the callback; silently ignored if out of range)
        void SetCurrentIndex(int index);
        /// @brief Current page index (-1 when there are no pages)
        int currentIndex() const { return current_; }

        // -------------------------------------------------------------------
        //  Callbacks (triggered only on the user path)
        // -------------------------------------------------------------------

        /// @brief Sets the page-change callback (with sender + new index)
        /// @note Triggered only on the user path (clicking a tab / arrow keys); programmatic SetCurrentIndex does not fire it.
        void SetOnTabChanged(SelfAction1<int> cb) { onChanged_ = std::move(cb); }
        /// @brief Sets the page-change callback (form that only cares about the sender; dispatch calls the payload version first, then this one)
        void SetOnTabChanged(SelfAction cb) { onChangedSelf_ = std::move(cb); }

        // -------------------------------------------------------------------
        //  Appearance
        // -------------------------------------------------------------------

        /// @brief Tab bar height (pixels); 0 = follow theme tab.barHeight
        void SetTabBarHeight(float h);
        /// @brief Current tab bar height
        float tabBarHeight() const { return barHeight_; }

        /// @brief Minimum width of a single tab (pixels)
        void SetTabMinWidth(float w);
        /// @brief Left/right padding of the text inside a tab (pixels)
        void SetTabPaddingX(float px);
        /// @brief Selection indication style (Line = bottom indicator line / Fill = full background block)
        void SetIndicator(TabIndicator s);
        /// @brief Current indication style
        TabIndicator indicator() const { return indicator_; }

        // -------------------------------------------------------------------
        //  Interaction
        // -------------------------------------------------------------------
        // @brief Left / Right / Home / End switch pages; other keys return false
        bool OnKeyPress(KeyEvent& e) override;

        /// @brief Left button press: only starts when it lands on a tab (other positions are not consumed)
        bool OnMousePress(MouseEvent& e) override;
        /// @brief Left button release: commits the switch only when it lands on the tab that was pressed
        bool OnMouseRelease(MouseEvent& e) override;
        /// @brief Move: maintains the tab hover highlight
        bool OnMouseMove(MouseEvent& e) override;
        /// @brief Leave: clears the hover highlight
        void OnMouseLeave() override;

        // -------------------------------------------------------------------
        //  Paint / theme
        // -------------------------------------------------------------------
        // @brief Paints the base class background first, then the tab bar (background / separator line / each tab / indicator)
        void Paint(FrameCollector& fc) const override;

        /// @brief Re-injects tab styles from the theme tab section
        void ApplyTheme(const Theme& t) override;

    protected:
        /// @brief Horizontal extent of the index-th tab (rx = left edge, rw = width); returns false if out of range
        bool tabRect(int index, float& rx, float& rw) const;
        /// @brief Width of the index-th tab (text width + padding on both sides, and not less than the minimum width)
        float tabWidth(int index) const;
        /// @brief Which tab a point lands on (-1 = not in the tab bar / not on any tab)
        int  tabAtPoint(float px, float py) const;

    private:
        /// @brief Page-change exit for the user path (shared by click / keyboard): sets the value + notifies
        void selectByUser(int index);
        /// @brief Fires the registered page-change callbacks (payload version first, then sender version)
        void notifyChanged(int index);
        /// @brief Requests a repaint (silently skipped when there is no window)
        void requestRedraw();

        // ---- Pages ----
        std::vector<std::string> titles_;   /// < tab text
        std::vector<Panel*>      pages_;    /// < content container of each page (borrowed; heap object address is stable)
        int current_ = -1;

        // ---- Interaction transient state ----
        int pressedTab_ = -1;
        int hoverTab_ = -1;

        // ---- Appearance ----
        float barHeight_ = 34.0f;
        float minTabWidth_ = 64.0f;
        float tabPadX_ = 14.0f;
        TabIndicator indicator_ = TabIndicator::Line;
        TextStyle headTextStyle_;
        TextStyle headActiveTextStyle_;
        Color4f barBackground_{ 0, 0, 0, 0 };
        Color4f dividerColor_{ 0, 0, 0, 0 };
        Color4f hoverBackground_{ 0, 0, 0, 0 };
        Color4f activeBackground_{ 0, 0, 0, 0 };
        Color4f indicatorColor_{ 0, 0, 0, 0 };
        float   indicatorHeight_ = 2.0f;
        float   tabRadius_ = 6.0f;
        float   radiusOverride_ = -1.0f;   /// < when >= 0, overrides the theme's tab corner radius

        // ---- Callbacks ----
        SelfAction1<int> onChanged_;
        SelfAction       onChangedSelf_;
    };

} // namespace Kit
