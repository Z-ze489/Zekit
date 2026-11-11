/**
 * @file ZeBreadcrumb.h
 * @brief Breadcrumb: a path navigation like `Home / Users / Details`
 *
 * @details
 * `Breadcrumb : ItemStripBase` -- item model, hover / click / keyboard navigation,
 * "paint and hit test share one geometry" are all inherited from the base class. This class adds only the three
 * breadcrumb-specific things:
 *
 *  1. Separator: the `/` between items (can be changed to `>` / `·`);
 *  2. Current item: the last item is "you are here", bold + primary color; the preceding items are plain text;
 *  3. Length control: when the path is too long, keep only head and tail and fold the middle into `…` (`SetMaxCrumbs`).
 *
 * @par Usage
 * @code
 * Kit::Breadcrumb bc;
 * bc.SetCrumbs({ "Home", "User Management", "Zhang San", "Edit" });   // the last item is the current item
 * bc.SetOnNavigate([](Kit::Widget& self, int index) {
 *     std::printf("jump to level %d\n", index);
 * });
 * win.AddWidget(bc);
 * @endcode
 *
 * @note Clicking the current item does NOT trigger the callback (the value did not change) -- consistent with the library-wide "notify only when the value changes".
 * @note `SetCurrentCrumb` (programmatic) does NOT trigger the callback; only clicks / arrow keys do
 *       -- the same rule as `TabView` / `ComboBox` / `ListView`.
 *
 * @see ZeItemStripBase.h  library-internal common base class (geometry / state / input)
 * @see ZePagination.h     another "row of discrete items" widget in the same family
 */

#pragma once
#include "../ZeItemStripBase.h"   // Library-internal base class (not part of the include/ delivery surface)

#include <cstddef>
#include <string>
#include <vector>

namespace Kit {

    /**
     * @brief Breadcrumb navigation
     *
     * @details
     * The theme takes the `itemStrip` section: `item` governs the normal layer, `itemActive` governs the current layer
     * (bold + primary color), `separatorColor` governs the slash.
     */
    class Breadcrumb : public ItemStripBase {
    public:
        Breadcrumb();

        // -------------------------------------------------------------------
        //  Path (data)
        // -------------------------------------------------------------------

        /// @brief Sets the whole path at once (the last item is the current layer)
        void SetCrumbs(const std::vector<std::string>& crumbs);

        /// @brief Returns the current path
        std::vector<std::string> crumbs() const;

        /// @brief Appends a layer (the newly appended layer becomes the current layer)
        int AddCrumb(std::string label);

        /// @brief Number of path layers (number of layers in the full path, independent of folding)
        /// @note Folding only changes the number of displayed items (`itemCount()`); this function answers
        ///       "how many layers this path has in total" -- that is path data and should not be affected by the display strategy.
        std::size_t crumbCount() const { return crumbs_.size(); }

        /// @brief Returns the text of a layer (full path; returns an empty string if out of range)
        /// @note Middle layers remain readable when folded -- folding only affects display, not data.
        const std::string& crumb(int index) const;

        // -------------------------------------------------------------------
        //  Current layer
        // -------------------------------------------------------------------

        /// @brief Sets the current layer programmatically (does NOT trigger the callback; silently ignored if out of range)
        /// @param index layer index in the full path
        void SetCurrentCrumb(int index);
        /// @brief Current layer index (index in the full path; -1 when there are no layers)
        /// @note Different from the "displayed item index": when folded, middle layers collapse into `…`, so the two differ by several positions.
        int currentCrumb() const { return currentCrumb_; }

        // -------------------------------------------------------------------
        //  Length control
        // -------------------------------------------------------------------

        /// @brief Maximum number of displayed layers (0 = unlimited)
        /// @note When exceeded, the first layer and the last two layers are kept, and the middle folds into a non-clickable `…`
        ///       (the same technique as the pagination ellipsis).
        ///       Folding only affects display; `crumbCount()` / `currentCrumb()` still reflect the full path.
        void SetMaxCrumbs(int maxCount);
        /// @brief Current display limit (0 = unlimited)
        int maxCrumbs() const { return maxCrumbs_; }

        // -------------------------------------------------------------------
        //  Appearance
        // -------------------------------------------------------------------

        /// @brief Separator text (default `"/"`; can be changed to `">"` / `"·"`)
        void SetSeparator(std::string sep);
        /// @brief Returns the current separator
        const std::string& separator() const { return separator_; }

        // -------------------------------------------------------------------
        //  Callbacks (triggered only by user paths)
        // -------------------------------------------------------------------

        /// @brief Sets the "user clicked a layer" callback (with sender + layer index)
        /// @note To navigate inside the callback: use `As<Breadcrumb>(self)` to get self and read the path,
        ///       or use the index directly with `FindWidget*` to find the target page.
        void SetOnNavigate(SelfAction1<int> cb) { onNavigate_ = std::move(cb); }
        /// @brief Sets the navigation callback (form that only cares about the sender)
        void SetOnNavigate(SelfAction cb) { onNavigateSelf_ = std::move(cb); }

    protected:
        // -------------------------------------------------------------------
        //  Base class hooks
        // -------------------------------------------------------------------

        /// @brief Separator width = text width of `/` + 4px padding on each side
        float separatorWidth() const override;

        /// @brief Paints the separator (slash centered in the separator area, using `separatorColor`)
        void paintStripSeparator(FrameCollector& fc, const ItemRect& r) const override;

        /// @brief User selected a layer -> forwards to the navigation callback
        void OnCurrentChangedByUser(int index) override;

    private:
        /// @brief Rebuilds displayed items after folding according to `maxCrumbs_`
        /// @note Folding only touches displayed items; `crumbs_` is always the full path.
        void rebuildItems();

        /// @brief The "middle ellipsis" placeholder inserted when folded (not clickable)
        static const char* GapLabel();

        /// @brief Maps the "current layer" back to the displayed items (must be reset once after `SetItems`)
        void syncCurrentItem();

        std::vector<std::string> crumbs_;   /// < Full path (displayed items may have been folded)
        int         currentCrumb_ = -1;     /// < ★ Current layer (full path index, decoupled from displayed items)
        std::string separator_ = "/";
        int         maxCrumbs_ = 0;         /// < 0 = unlimited
        bool        rebuilding_ = false;    /// < Reentrancy gate (see rebuildItems)

        // ---- Callbacks ----
        SelfAction1<int> onNavigate_;
        SelfAction       onNavigateSelf_;
    };

} // namespace Kit
