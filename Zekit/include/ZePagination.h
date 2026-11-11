/**
 * @file ZePagination.h
 * @brief Pagination: page number blocks + prev/next arrows + long-range folding (`‹ 1 … 4 5 6 … 20 ›`)
 *
 * @details
 * `Pagination : ItemStripBase` -- item model, hover / click / keyboard navigation,
 * "paint and hit test share one geometry" are all inherited from the base class. This class adds only the four
 * pagination-specific things:
 *
 *  1. Page number window folding: when there are too many pages, the middle folds into one `…` (not clickable),
 *     the first and last pages and the pages near the current page are always visible -- `SetMaxButtons`;
 *  2. Prev/next arrows: the item text is an internal sentinel string, translated into `‹` / `›` arrows when painting;
 *     automatically grayed out at the boundary (`SetItemEnabled` -> disabled items in the base class are not selectable);
 *  3. Page number semantics: externally 1-based (`currentPage()` returns 1...N), internally 0-based
 *     (that is the base class item index) -- the conversion happens only in this class, do not leak it outside;
 *  4. Page change callback: `SetOnPageChanged`, triggered only on the user path.
 *
 * @par Usage
 * @code
 * Kit::Pagination pg;
 * pg.SetPageCount(20);
 * pg.SetMaxButtons(7);                       // at most 7 page number slots (including the ellipsis)
 * pg.SetOnPageChanged([](Kit::Widget& self, int page) {
 *     std::printf("page %d\n", page);        // 1-based
 * });
 * win.AddWidget(pg);
 * @endcode
 *
 * @note "User page change" and "programmatic `SetCurrentPage`" are two paths: only the former triggers the callback
 *       -- the same rule as `TabView` / `ComboBox` / `ListView`.
 * @note When `totalPages` is 0, no items are shown at all (better empty than showing a non-clickable "1").
 *
 * @see ZeItemStripBase.h  common base class in the library (geometry / state / input)
 * @see ZeBreadcrumb.h     another "row of discrete items" widget in the same family
 */

#pragma once
#include "../ZeItemStripBase.h"

#include <cstddef>

namespace Kit {

    /**
     * @brief Pagination widget
     *
     * @details
     * Theme section `itemStrip`: the current page uses a solid `itemActiveColor` block + `itemActive`
     * text; normal pages use `item` text; hovered pages use `itemHoverColor`; arrows use `item` text color.
     */
    class Pagination : public ItemStripBase {
    public:
        Pagination();

        // -------------------------------------------------------------------
        //  Pages (externally 1-based)
        // -------------------------------------------------------------------

        /// @brief Total page count (0 = show no items)
        void SetPageCount(int totalPages);
        /// @brief Total page count
        int pageCount() const { return totalPages_; }

        /// @brief Programmatically jumps to a page (does not trigger the callback; out-of-range values are silently ignored)
        /// @param page Page number, 1-based
        void SetCurrentPage(int page);
        /// @brief Current page (1-based; 0 when there are no pages)
        /// @note The current page is this class's own state, not equal to the base class's "current item index" --
        ///       when folded, the item list contains only a few page number slots, so page 10 may land on index 3.
        int currentPage() const { return (totalPages_ > 0) ? page_ : 0; }

        // -------------------------------------------------------------------
        //  Folding / arrows
        // -------------------------------------------------------------------

        /// @brief Maximum number of "page number slots" to show (including `…` placeholders and the first/last pages)
        /// @param count Upper limit; values < 5 are treated as 5 (smaller cannot express "first/last + current + ellipsis" at once)
        void SetMaxButtons(int count);
        /// @brief Current upper limit
        int maxButtons() const { return maxButtons_; }

        /// @brief Whether to show first/last page buttons (default true, i.e. always provide `1` and `N`)
        void SetShowEdges(bool on);
        /// @brief Whether to show first/last page buttons
        bool showEdges() const { return showEdges_; }

        /// @brief Whether to show `‹` / `›` prev/next arrows (default true)
        void SetShowArrows(bool on);
        /// @brief Whether to show prev/next arrows
        bool showArrows() const { return showArrows_; }

        // -------------------------------------------------------------------
        //  Callbacks (triggered only on the user path)
        // -------------------------------------------------------------------

        /// @brief Sets the "user page change" callback (with sender + new page number, 1-based)
        void SetOnPageChanged(SelfAction1<int> cb) { onPageChanged_ = std::move(cb); }
        /// @brief Sets the "user page change" callback (form that only cares about the sender)
        void SetOnPageChanged(SelfAction cb) { onPageChangedSelf_ = std::move(cb); }

        // -------------------------------------------------------------------
        //  Theme
        // -------------------------------------------------------------------

        /// @brief Injects from the theme (`itemStrip` section + "text color on solid block" `onPrimary`)
        /// @note public: the base class `Widget::ApplyTheme` is public, and the same-named
        ///       override in `Sidebar` is also public -- the three visibilities must match, otherwise callers of "manual theme switching"
        ///       (every `Preview_*.cpp` in Guide does this) are rejected at compile time.
        void ApplyTheme(const Theme& t) override;

    protected:
        // -------------------------------------------------------------------
        //  Base class hooks
        // -------------------------------------------------------------------

        /// @brief Item width: page number / ellipsis -> block; arrow -> slightly narrower block
        float measureStripItem(int index) const override;

        /// @brief Paints one item: page number text / ellipsis / arrow, background determined by current page and hover
        void paintStripItem(FrameCollector& fc, int index, const ItemRect& r) const override;

        /// @brief User page change -> converted to page change callback (page number converted to 1-based)
        void OnCurrentChangedByUser(int index) override;

    private:
        // -------------------------------------------------------------------
        //  Internal
        // -------------------------------------------------------------------

        /// @brief Rebuilds the displayed items from page count / current page / upper limit (including folding and arrows)
        void rebuildItems();

        /// @brief Whether an item is an "ellipsis placeholder"
        static bool IsGap(const std::string& label);
        /// @brief Whether an item is the "previous page" arrow
        static bool IsPrev(const std::string& label);
        /// @brief Whether an item is the "next page" arrow
        static bool IsNext(const std::string& label);

        /// @brief Page number text (`"1"` / `"20"`)
        static std::string PageLabel(int page1Based);
        /// @brief Resolves the page number from the item text (arrows are computed as "current page +/- 1"); returns 0 = not a page number item
        /// @param label Item text
        /// @param curPage Current page (1-based) -- arrow items are converted using it
        static int PageFromItem(const std::string& label, int curPage);

        /// @brief Marks the item in the item list that corresponds to the "current page" (used for highlight painting)
        void syncCurrentItem();

        /// @brief Item height (fallback when the theme does not provide it) -- also used as the block width
        float cellSize() const;

        int  totalPages_ = 0;
        int  page_ = 1;            /// < Current page (1-based) -- this class's own state, decoupled from the item index
        int  maxButtons_ = 7;      /// < Includes ellipsis placeholders and first/last pages
        bool showEdges_ = true;
        bool showArrows_ = true;
        bool rebuilding_ = false;  /// < Reentrancy gate (see rebuildItems)

        /// @brief Text color of the current page (= theme `onPrimary`)
        /// @details The current page is a solid primary color block, so the text must be inverted to be visible. The
        ///          `itemStrip.itemActive` text color in the theme is for "selected items on a normal background"
        ///          (breadcrumb / step title); using it for this cell paints same color on same color -- the page number
        ///          would vanish (phenomenon confirmed by testing on 2026-10-27).
        Color4f activeInk_{ 1.0f, 1.0f, 1.0f, 1.0f };

        // ---- Callbacks ----
        SelfAction1<int> onPageChanged_;
        SelfAction       onPageChangedSelf_;
    };

} // namespace Kit
