/**
 * @file ZeTableView.h
 * @brief Kit::TableView - read-only data table with a header (whole-row selection + vertical scrolling)
 *
 * @details
 * A fixed header on top, a scrollable data area below; click a row to select it.
 *
 * @par Why Panel instead of inheriting ScrollView directly
 * The header must not scroll. If TableView were itself a ScrollView, the header could only be painted at
 * the top of the widget, and the scrolling rows are also children of this widget - children are painted after the container, so they
 * would cover the header. Therefore this class inherits Panel (a vertical container):
 *
 * ```
 * TableView (Panel, top padding = header height)
 *  ├─ header: painted by TableView itself, occupies that padding (no child widget covers it)
 *  └─ body_ (ScrollView, flexGrow=1)
 *       └─ several TableRow (self-painted multi-column)
 * ```
 *
 * Thus "the header neither scrolls nor gets covered" is a structural guarantee, not propped up by PushClip.
 *
 * @par Horizontal alignment of columns
 * The header and the rows both start from the same left edge (the left of the widget content area) and accumulate the same column widths -
 * so the columns always align. The right end differing by one scrollbar width only affects "how much can be shown", not column positions.
 *
 * @par Usage
 * @code
 * Kit::TableView tv;
 * tv.SetColumns({ {"Contract", 90}, {"Last", 0}, {"Change", 0} });   // width = 0 -> auto by content
 * tv.SetRows({ {"RM2701", "2418", "+12"}, {"MA2701", "2156", "-8"} });
 * tv.SetOnRowSelected([](Kit::Widget& self, int row) { ... });
 * win.AddWidget(tv);
 * @endcode
 *
 * @note Read-only table: cells are not editable (to edit, overlay an InputBox on the cell yourself; this widget does not do it).
 * @note "User selection" and "programmatic SetSelectedRow" are two paths: only the former triggers the callback.
 *
 * @see ZeListView.h the isomorphic "one-dimensional" version (rows + selection + scrolling)
 * @see ZePanel.h    base class (arrangement / source of top padding)
 */

#pragma once
#include "ZePanel.h"
#include "ZeScrollView.h"
#include "ZeSelfAction.h"
#include "ZeTextStyle.h"

#include <cstddef>
#include <string>
#include <vector>

namespace Kit {

    /// @brief One column of the table
    struct TableColumn {
        std::string title;        /// < Column header text
        float       width = 0.0f; /// < Column width (pixels); 0 = auto (computed from the widest content in that column)
    };

    /**
     * @brief Read-only data table with a header
     */
    class TableView : public Panel {
    public:
        TableView();

        // -------------------------------------------------------------------
        //  Data (data is not style - changing theme does not reset it)
        // -------------------------------------------------------------------

        /// @brief Replaces the column definitions entirely (does not trigger callbacks)
        /// @note When the column count changes, row data is not padded/truncated automatically - SetRows uses the current column count, padding short rows with empty strings.
        void SetColumns(std::vector<TableColumn> cols);

        /// @brief Replaces all rows entirely (does not trigger callbacks); clears selection if selectedRow is out of range
        void SetRows(std::vector<std::vector<std::string>> rows);

        /// @brief Clears everything (also clears selection)
        void Clear();

        /// @brief Row count / column count
        std::size_t rowCount() const { return cells_.size(); }
        std::size_t columnCount() const { return cols_.size(); }

        /// @brief Returns the data of row index (returns a static empty row if out of range)
        const std::vector<std::string>& rowAt(std::size_t index) const;

        /// @brief Sets the attached data of all rows (one-to-one with the rows of SetRows)
        ///
        /// @details
        /// Attached data is the hidden payload of a row: it does not participate in painting or column width computation,
        /// and is only retrieved by the callback when that row is selected - typical use is "what this row represents",
        /// such as an object id, a description string, or a file path.
        ///
        /// Why not let the caller keep a map: the callback is capture-less (under value semantics,
        /// capturing members yields the moved-from empty shell, see ZeSelfAction.h), so inside the callback the only
        /// things available are "sender + row index". With the payload attached to the table itself, the callback stays self-consistent:
        /// @code
        /// table.SetOnRowSelected([](Kit::Widget& self, int row) {
        ///     const std::string& target = Kit::As<Kit::TableView>(self).rowUserData(row);
        ///     // ...do something with target, without capturing a single reference
        /// });
        /// @endcode
        ///
        /// @param data One payload per row; the length need not equal the row count (shorter means no payload for the rest)
        /// @note SetRows / Clear clear the payloads - the meaning of row indices changes, so old payloads
        ///       would necessarily mismatch ("row 2" may already be something else). Therefore attach payloads
        ///       after setting the rows. Out-of-range reads always return an empty string and never crash.
        void SetRowUserData(std::vector<std::string> data);

        /// @brief Returns the attached data of a row (out of range / never set -> empty string)
        const std::string& rowUserData(int row) const;

        // -------------------------------------------------------------------
        //  Selection
        // -------------------------------------------------------------------

        /// @brief Programmatically sets the selected row (does not trigger callbacks; -1 = clear; out of range treated as -1)
        void SetSelectedRow(int index);
        /// @brief Current selected row (-1 = none)
        int selectedRow() const { return selected_; }
        /// @brief Data of the current selected row (empty row when nothing is selected)
        const std::vector<std::string>& selectedRowData() const;

        // -------------------------------------------------------------------
        //  Callbacks (triggered only on the user path)
        // -------------------------------------------------------------------

        /// @brief Sets the row-selected callback (with sender + new row index)
        /// @note Triggered only on the user path (mouse click / arrow keys); not fired if the value is unchanged.
        void SetOnRowSelected(SelfAction1<int> cb) { onRow_ = std::move(cb); }
        /// @brief Sets the row-selected callback (form that only cares about the sender; the payload version is called first, then this one)
        void SetOnRowSelected(SelfAction cb) { onRowSelf_ = std::move(cb); }

        // -------------------------------------------------------------------
        //  Appearance
        // -------------------------------------------------------------------

        /// @brief Row height (pixels, must be > 0)
        /// @note When cell wrapping is enabled (SetCellWrap(true), on by default), it is the minimum row height
        ///       - if content wraps taller, the content height wins; when wrapping is off it is the fixed row height.
        void SetRowHeight(float h);
        float rowHeight() const { return rowHeight_; }

        /// @brief Whether cells wrap automatically (default true)
        ///
        /// @details
        /// When enabled, the table does three cooperating things:
        ///   - column widths are compressed proportionally - when the sum of natural widths exceeds the available width, they shrink proportionally to fit
        ///     (otherwise long-content columns would push the whole table beyond the viewport and get clipped);
        ///   - wrapping inside cells - line breaks at the column width (CJK per character, Latin preferably at spaces);
        ///   - adaptive row height - row height = max(SetRowHeight value, height after content wrapping).
        ///
        /// Disabling it = back to the old behavior of "single line + ellipsis on overflow" (column widths take natural content width,
        /// the table may be wider than the container). For data tables (many columns, short content) both modes look the same.
        ///
        /// @note Wrapping requires measuring text, and text measurement is only accurate during rendering (MeasureText
        ///       always returns 0 during configuration). So row heights are only accurate on the first painted frame, which triggers one relayout
        ///       - row heights may "jump" once when the window first appears, then stabilize.
        void SetCellWrap(bool on);
        bool cellWrap() const { return wrap_; }

        /// @brief Header height (pixels); also resets the container's top padding
        void SetHeaderHeight(float h);
        float headerHeight() const { return headerHeight_; }

        /// @brief Left/right padding of text inside a cell (included in auto column width)
        void SetCellPaddingX(float px);

        void SetRowHoverColor(const Color4f& c);
        void SetRowSelectedColor(const Color4f& c);
        /// @brief Color of the vertical cell lines (alpha = 0 means not painted)
        void SetGridColor(const Color4f& c);
        void SetHeaderBackground(const Color4f& c);
        void SetBackground(const Color4f& c);
        /// @brief Table outer border (line width <= 0 = none)
        void SetBorder(float width, const Color4f& c);

        /// @brief Column header text style - can be modified directly
        TextStyle& headerTextStyle() { return headerTextStyle_; }
        const TextStyle& headerTextStyle() const { return headerTextStyle_; }
        /// @brief Cell text style - can be modified directly (after modifying, call SetRows or ApplyTheme again to sync existing rows)
        TextStyle& cellTextStyle() { return cellTextStyle_; }
        const TextStyle& cellTextStyle() const { return cellTextStyle_; }

        // -------------------------------------------------------------------
        //  Keyboard / mouse
        // -------------------------------------------------------------------
        // @brief Up / Down / Home / End move the selection (auto-scrolls into view)
        bool OnKeyPress(KeyEvent& e) override;

        /// @brief Left button press: only starts when landing on a row (header / blank area not consumed)
        bool OnMousePress(MouseEvent& e) override;

        /// @brief Left button release: commits the selection only when landing on the row that was pressed
        bool OnMouseRelease(MouseEvent& e) override;

        /// @brief Gains focus: paints the highlight of the current selection
        void OnFocusIn() override;
        /// @brief Loses focus: removes the selection highlight (see the note on syncRowSelection)
        void OnFocusOut() override;

        // -------------------------------------------------------------------
        //  Painting / theme
        // -------------------------------------------------------------------
        // @brief Paints the base class background first, then the header (background + column titles + vertical lines)
        void Paint(FrameCollector& fc) const override;

        /// @brief Re-injects column widths / row height / colors / text styles from the theme's table section
        void ApplyTheme(const Theme& t) override;

        /// @brief Called by row widgets (library internal): sets row index as selected and notifies
        void activateRow(int index);

    private:
        /// @brief Rebuilds all row widgets
        void rebuildRows();
        /// @brief Computes the left edge of each column (colX_): user-specified widths used directly, 0 computed from widest content
        void resolveColumnWidths();
        /// @brief Syncs the current selection state to each row widget
        void syncRowSelection();
        /// @brief Pushes the column layout down to each row widget (used when column widths/positions change, without rebuilding rows)
        void pushColumnLayout();
        /// @brief Which row a point falls on (-1 = not on any row); looked up via the actual row rectangles
        int  rowAtPoint(float px, float py) const;
        /// @brief Scrolls row index fully into the viewport
        void ensureVisible(int index);
        /// @brief Selection exit on the user path: sets the value + notifies
        void selectByUser(int index);
        /// @brief Fires the registered selection callback (payload version first, then sender version)
        void notifyRowSelected(int index);
        /// @brief Requests a repaint (silently skipped when there is no window)
        void requestRedraw();

        // ---- Data ----
        std::vector<TableColumn>             cols_;
        std::vector<std::vector<std::string>> cells_;
        /// Row attached data (hidden payload, see SetRowUserData) - parallel to cells_, maintained independently
        std::vector<std::string>             rowUserData_;
        std::vector<float>                   colX_;   /// < Left edge of each column (relative to the left of the content area)
        std::vector<Widget*>                 rows_;   /// < Row widgets (borrowed; heap object addresses are stable)

        /**
         * @brief Column widths not yet computed accurately (must wait for the font renderer to be ready)
         *
         * @details
         * During configuration (before exec()) the font renderer is not initialized, and Window::MeasureText
         * always returns 0 - computing auto column widths then would degrade all of them to the minimum width (symptom: every column shows
         * as 2...), without reporting any error. So column widths are deferred to the first Paint:
         * by then the renderer is ready and measurement is accurate.
         *
         * mutable because Paint is const, yet it needs to clear this flag.
         */
        mutable bool colWidthsDirty_ = true;

        /// @brief Widget width at the last column width computation
        /// @note In wrap mode column widths are distributed proportionally by widget width, so any width change requires recomputation
        ///       (detail pane collapse/expand, splitter drag, window resize all change it). Checking only colWidthsDirty_
        ///       would leave the table stuck at the old width - symptom: "the rightmost columns are clipped and invisible".
        mutable float colResolveWidth_ = -1.0f;

        /// @brief Data area (scroll container hosting the row widgets)
        /// @note Borrowed pointer - address is stable after AddChild (heap object).
        ScrollView* body_ = nullptr;

        // ---- Selection state ----
        int selected_ = -1;
        int pressedRow_ = -1;

        // ---- Appearance ----
        bool    wrap_ = true;         /// < Whether cells wrap automatically (see SetCellWrap)
        float   rowHeight_ = 28.0f;
        float   headerHeight_ = 32.0f;
        float   cellPadX_ = 8.0f;
        Color4f rowHoverColor_{ 0, 0, 0, 0 };
        Color4f rowSelectedColor_{ 0, 0, 0, 0 };
        Color4f gridColor_{ 0, 0, 0, 0 };
        Color4f headerBackground_{ 0, 0, 0, 0 };
        Color4f background_{ 0, 0, 0, 0 };
        Color4f borderColor_{ 0, 0, 0, 0 };
        float   borderWidth_ = 0.0f;

        TextStyle headerTextStyle_;
        TextStyle cellTextStyle_;

        // ---- Callbacks ----
        SelfAction1<int> onRow_;
        SelfAction       onRowSelf_;
    };

} // namespace Kit
