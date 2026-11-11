/**
 * @file ZeListView.h
 * @brief Kit::ListView -- scrollable single-select text list
 *
 * @details
 * A vertical list of rows: one line of text per row, highlight on mouse hover, select on click (single select),
 * when content exceeds height a scrollbar appears automatically, and wheel / thumb drag / touch all scroll.
 *
 * @par How it reuses existing code (not a single line of scroll logic is new)
 * Inherits `Kit::ScrollView` -- so all of this comes for free:
 *  - Scrolling: wheel, thumb drag, track click paging, touch drag;
 *  - Clipping: out-of-bounds rows are clipped and never painted outside the widget;
 *  - Scrollbar: track and thumb position / hit test / paint;
 *  - Layout: `layoutLinear` + `scrollOffset` -- rows only need to be added as child widgets,
 *    placement, scroll translation and upper-bound clamping are all provided.
 *
 * A row itself is an internal row widget (`TextWidget` derived, defined in `.cpp`, not exposed):
 * it only paints one row (full-row background for hover / selected + text) and handles no mouse events --
 * events bubble up to `ListView` itself, which maps coordinates back to a row index. Thus "which row was clicked"
 * has a single geometry source (the row's actual rect), with no second algorithm silently drifting.
 *
 * @par Usage
 * @code
 * Kit::ListView list;
 * list.SetItems({ "Beijing", "Shanghai", "Guangzhou", "Shenzhen" });
 * list.SetSelectedIndex(1);
 * list.SetOnSelectionChanged([](Kit::Widget& self, int i) {
 *     // self is the entity in the tree; the callback fires only on the user path (mouse / keyboard)
 * });
 * win.AddWidget(list);
 * @endcode
 *
 * @note Selection is persistent data (not a transient state like hover): changing the theme does not reset it.
 * @note "User selection" and programmatic `SetSelectedIndex` are two paths: only the former fires the callback
 *       -- same rule as `ComboBox` / `Checkbox`.
 * @note Keyboard: `Up` / `Down` move the selection and scroll it into view automatically, `Home` / `End` jump to first / last;
 *       the `ListView` constructor already calls `SetFocusable(true)`, so clicking the list once enables the arrow keys.
 *
 * @see ZeScrollView.h  the source of all scrolling / clipping / scrollbar behavior
 * @see ZeComboBox.h    another "rows + selection" implementation (popup version)
 */

#pragma once
#include "ZeScrollView.h"
#include "ZeSelfAction.h"
#include "ZeTextStyle.h"

#include <cstddef>
#include <string>
#include <vector>

namespace Kit {

    /**
     * @brief Single-select text list (scrollable)
     */
    class ListView : public ScrollView {
    public:
        ListView();

        // -------------------------------------------------------------------
        //  Data (data is not style -- changing the theme does not reset it)
        // -------------------------------------------------------------------

        /// @brief Replaces all list items (does not fire callbacks)
        /// @note Rebuilds all row widgets; `selectedIndex` is kept within the valid range of the new list (out of range clears the selection).
        void SetItems(std::vector<std::string> items);

        /// @brief Appends one item and returns its index (does not fire callbacks)
        int AddItem(std::string text);

        /// @brief Clears everything (also clears the selection; does not fire callbacks)
        void Clear();

        /// @brief Number of items
        std::size_t itemCount() const { return items_.size(); }

        /// @brief Returns the text of item index (out of range returns a static empty string, reference is stable)
        const std::string& itemAt(std::size_t index) const;

        // -------------------------------------------------------------------
        //  Selection (persistent data)
        // -------------------------------------------------------------------

        /// @brief Sets the selected item programmatically (does not fire callbacks; `-1` = clear selection; out of range silently clamps to -1)
        void SetSelectedIndex(int index);

        /// @brief Current selected index (-1 = no selection)
        int selectedIndex() const { return selected_; }

        /// @brief Text of the currently selected item (empty string when nothing is selected)
        const std::string& selectedText() const;

        // -------------------------------------------------------------------
        //  Callbacks (fired only on the user path)
        // -------------------------------------------------------------------

        /// @brief Sets the selection-changed callback (sender + new index)
        /// @note Fires only on the user path (mouse click / keyboard arrows); programmatic `SetSelectedIndex` does not fire it.
        void SetOnSelectionChanged(SelfAction1<int> cb) { onSel_ = std::move(cb); }
        /// @brief Sets the selection-changed callback (sender-only form; dispatch calls the payload version first, then this one)
        void SetOnSelectionChanged(SelfAction cb) { onSelSelf_ = std::move(cb); }

        // -------------------------------------------------------------------
        //  Appearance
        // -------------------------------------------------------------------

        /// @brief Single row height (pixels); 0 = follow theme `list.itemHeight`
        void SetItemHeight(float h);
        /// @brief Currently effective row height
        float itemHeight() const { return itemHeight_; }

        /// @brief Full-row background of the hovered row (default theme `list.itemHoverColor`)
        void SetItemHoverColor(const Color4f& c);
        /// @brief Full-row background of the selected row (default theme `list.itemSelectedColor`)
        void SetItemSelectedColor(const Color4f& c);

        /// @brief Row text style -- can be modified directly
        TextStyle& itemTextStyle() { return itemTextStyle_; }
        const TextStyle& itemTextStyle() const { return itemTextStyle_; }

        /// @brief List background color
        void SetBackground(const Color4f& c);
        /// @brief List border (line width <= 0 = no border)
        void SetBorder(float width, const Color4f& c);
        /// @brief List corner radius (pixels)
        void SetRadius(float r);

        // -------------------------------------------------------------------
        //  Keyboard / mouse
        // -------------------------------------------------------------------
        // @brief `Up` / `Down` / `Home` / `End` move the selection (scrolls it into view); other keys return false
        bool OnKeyPress(KeyEvent& e) override;

        /// @brief Left button press: scrollbar first (thumb drag / paging), then row hit test if not on the bar
        bool OnMousePress(MouseEvent& e) override;

        /// @brief Left button release: commits the selection only if released on the row pressed (dragging to another row cancels, same rhythm as ComboBox)
        bool OnMouseRelease(MouseEvent& e) override;

        // -------------------------------------------------------------------
        //  Paint / theme
        // -------------------------------------------------------------------
        // @brief Paints the list skeleton first (background + border), then lets the base class paint the scrollbar
        void Paint(FrameCollector& fc) const override;

        /// @brief Re-injects skeleton / row height / row backgrounds / text style from the theme `list` section
        void ApplyTheme(const Theme& t) override;

    private:
        /// @brief Rebuilds all row widgets from items_
        void rebuildRows();
        /// @brief Syncs the current selection state to the row widgets
        void syncRowSelection();
        /// @brief Returns the row index a point falls on (-1 = no row); resolved via the row's actual rect
        int  indexAtPoint(float px, float py) const;
        /// @brief Makes row index fully visible in the viewport (adjusts the scroll offset if needed)
        void ensureVisible(int index);
        /// @brief Selection exit for the user path (shared by mouse / keyboard): set value + notify
        void selectByUser(int index);
        /// @brief Fires the registered selection callbacks (payload version first, then sender version)
        void notifySelection(int index);

        // ---- Data ----
        std::vector<std::string> items_;   /// < item text
        std::vector<Widget*>     rows_;    /// < row widgets (borrowed; heap object addresses are stable)

        // ---- Selection state ----
        int selected_ = -1;
        int pressedRow_ = -1;   /// < which row the press landed on (selection is committed on release)

        // ---- Appearance ----
        float   itemHeight_ = 28.0f;
        Color4f itemHoverColor_{ 0, 0, 0, 0 };
        Color4f itemSelectedColor_{ 0, 0, 0, 0 };
        TextStyle itemTextStyle_;
        Color4f background_{ 0, 0, 0, 0 };
        Color4f borderColor_{ 0, 0, 0, 0 };
        float   borderWidth_ = 0.0f;
        float   radius_ = 0.0f;

        // ---- Callbacks ----
        SelfAction1<int> onSel_;
        SelfAction       onSelSelf_;
    };

} // namespace Kit
