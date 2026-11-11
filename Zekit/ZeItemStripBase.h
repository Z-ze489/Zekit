/**
 * @file ZeItemStripBase.h
 * @brief Library-internal common base class for "a row of discrete items" widgets (NOT part of the delivery surface)
 *
 * @details
 * Breadcrumb (ZeBreadcrumb.h) / pagination (ZePagination.h) / steps (ZeSteps.h)
 * share the same skeleton:
 *
 *   - a row of equal-height items laid out horizontally, with separators between items (/ , ... , connecting lines);
 *   - each item can be hovered and clicked, and exactly one item is the "current item";
 *   - items are painted by the widget itself, not a pile of child widgets (the same decision as the TabView tab bar:
 *     item widths must be exact, and the tree should not gain a pile of buttons).
 *
 * The only difference is what an item looks like: breadcrumb is text + slash, pagination is square numbers, steps is circles + connecting lines.
 * So the common parts live in this class, and the appearance is left to the three paint hooks of the derived class.
 *
 * @par Only one copy of the geometry (shared by paint and hit test)
 * Same discipline as ComboBox::computePopup: item rectangles are computed once by ensureGeometry(),
 * and both paintContent and HitTest read it. The most common widget bug is "it looks like item 3,
 * but clicking selects item 2" -- exactly the consequence of computing paint and hit test separately.
 *
 * @par Why derive from Panel instead of BoxWidget
 * All three widgets may carry child items (the content area of each step, the jump button next to pagination).
 * Laying out child items is the LayoutBase branch -- Panel is the default answer of that branch.
 * Appearance goes through ItemStripStyle (pure value) and is painted in paintContent.
 *
 * @warning This header lives in Zekit/ (source root), NOT in include/ -- the same rule as
 *          ZeFrameCollector.h / ZeDrawUtil.h: it knows paint primitives and is a library-internal
 *          implementation detail. Users only see the public headers of the three derived widgets.
 *
 * @see ZeTheme.h  ItemStripStyle (the shared theme section of the three)
 */

#pragma once

#include "include/ZePanel.h"
#include "include/ZeSelfAction.h"
#include "include/ZeTheme.h"
#include "include/ZeTextStyle.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

// FrameCollector is the parameter type of Paint / MeasureText -- the base class only needs a forward declaration,
// but the derived class implementations need the full definition, so it is not included here (each .cpp includes it itself).
namespace Kit { class FrameCollector; }

namespace Kit {

    /**
     * @brief Common base class for "a row of discrete items" widgets
     *
     * @details
     * The derived class only has to answer four things (all have reasonable defaults):
     *   - measureStripItem(i)  -- how wide item i is (default: text width + padding on both sides);
     *   - paintStripItem(fc, i, r) -- paints item i (default: hover / current item background + centered text);
     *   - separatorWidth() / paintStripSeparator(fc, r) -- separator footprint and appearance;
     *   - optional paintStripBackground(fc) -- self-painted background of the whole strip.
     */
    class ItemStripBase : public Panel {
    public:
        /// @brief Rectangle of one item (client-area absolute coordinates)
        struct ItemRect {
            float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
        };

        /// @brief Layout direction
        /// @note Horizontal is the "tab / path / page number" family (breadcrumb, pagination, steps);
        ///       vertical is the "menu / navigation" family (sidebar). Both share the same item model and input handling,
        ///       and diverge only in geometry and paint direction -- exactly why they are collected in one base class.
        enum class StripOrientation {
            Horizontal,   /// < left to right (default)
            Vertical      /// < top to bottom
        };

        ItemStripBase();

        // -------------------------------------------------------------------
        //  Item data (data, not style -- changing the theme does not reset it)
        // -------------------------------------------------------------------

        /// @brief Appends an item
        /// @param label item text (pagination numbers and step titles both go here)
        /// @return index of the new item
        int AddItem(std::string label);

        /// @brief Appends an item and specifies whether it is clickable (disabled items paint gray and do not respond)
        int AddItem(std::string label, bool enabled);

        /// @brief Replaces all items at once (the current index is clamped into the new range)
        void SetItems(const std::vector<std::string>& labels);

        /// @brief Clears all items (the current index becomes -1)
        void ClearItems();

        /// @brief Number of items
        std::size_t itemCount() const { return items_.size(); }

        /// @brief Changes the text of one item (out of range is silently ignored)
        void SetItemLabel(int index, std::string label);
        /// @brief Returns the text of one item (out of range returns a static empty string)
        const std::string& itemLabel(int index) const;

        /// @brief Whether one item is clickable (out of range returns false)
        bool isItemEnabled(int index) const;
        /// @brief Sets whether one item is clickable (out of range is silently ignored)
        void SetItemEnabled(int index, bool enabled);

        // -------------------------------------------------------------------
        //  Current item
        // -------------------------------------------------------------------

        /// @brief Sets the current item programmatically (does NOT fire callbacks; out of range is silently ignored)
        void SetCurrentIndex(int index);
        /// @brief Current item index (-1 when there are no items)
        int currentIndex() const { return current_; }

        /// @brief Writes the "current item index" directly (internal use: no clamping, no notification, no repaint)
        /// @param index new index; -1 means "no current item"
        /// @note Why this exists: derived classes (pagination / breadcrumb) need to put the
        ///       "current item" back at some position after rebuilding the item table, and the rebuilt index is often
        ///       NOT a value SetCurrentIndex accepts (in pagination, "page 10" corresponds to display item index 3; passing 9
        ///       directly would be silently ignored as out of range). This gives the derived class an entry point for
        ///       "I have already computed it myself".
        void SetCurrentIndexInternal(int index) { current_ = index;  RequestWindowRedraw();}

        // -------------------------------------------------------------------
        //  Callbacks (fired only on the user path)
        // -------------------------------------------------------------------

        /// @brief Sets the current item change callback (with sender + new index)
        /// @note Fired only on the user path (click / arrow keys); programmatic SetCurrentIndex does not fire
        ///       -- the same rule as TabView / ComboBox / ListView.
        void SetOnCurrentChanged(SelfAction1<int> cb) { onChanged_ = std::move(cb); }
        /// @brief Sets the current item change callback (form that only cares about the sender)
        void SetOnCurrentChanged(SelfAction cb) { onChangedSelf_ = std::move(cb); }

        // -------------------------------------------------------------------
        //  Measurement protocol
        // -------------------------------------------------------------------
        float hintWidth() const override;
        float hintHeight() const override;

        // -------------------------------------------------------------------
        //  Theme
        // -------------------------------------------------------------------
        // @brief Re-injects from the theme itemStrip section (item text / item background / separator)
        void ApplyTheme(const Theme& t) override;

        // -------------------------------------------------------------------
        //  Paint / events
        // -------------------------------------------------------------------
        // @brief Separators -> items (separators are painted first so item backgrounds can cover their ends)
        // @note Overrides Paint rather than paintContent -- the Panel branch
        //       (the LayoutBase half) has no BoxWidget template method, and the
        //       paintContent hook does not exist on Widget at all.
        void Paint(FrameCollector& fc) const override;

        /// @brief Nothing outside the item area is hittable (so the whole strip rectangle does not swallow clicks on blank space)
        bool HitTest(float x, float y) const override;

        /// @brief Left button press: consumes only when it lands on a clickable item
        bool OnMousePress(MouseEvent& e) override;
        /// @brief Left button release: commits only when it lands on the item that was pressed (dragging away = cancel)
        bool OnMouseRelease(MouseEvent& e) override;
        /// @brief Maintains the hovered item
        bool OnMouseMove(MouseEvent& e) override;
        /// @brief Clears hover on leave
        void OnMouseLeave() override;

        /// @brief Left / Right / Home / End move the current item; other keys return false
        bool OnKeyPress(KeyEvent& e) override;

    protected:
        // ===================================================================
        //  Geometry (readable by derived classes)
        // ===================================================================

        /// @brief Rectangle of item i; returns false when out of range or not yet computed
        bool itemRect(int index, ItemRect& out) const;

        /// @brief Which item a point currently falls on (-1 = not on any item)
        int itemAtPoint(float px, float py) const;

        /// @brief Usable content area of this strip (client-area absolute coordinates, padding already subtracted)
        void stripArea(float& x, float& y, float& w, float& h) const;

        /// @brief Total width of the strip content (items + separators + spacing) -- used by derived classes to compute natural width
        float measuredStripWidth() const;

        // ===================================================================
        //  Derived classes answer "what an item looks like"
        // ===================================================================

        /// @brief How wide item i is (must be implemented)
        /// @note Default implementation: text width + padding on both sides (ItemStripStyle::itemPaddingX),
        ///       and not less than itemHeight (so squares / circles stay square).
        ///       Measure text with MeasureStripText() -- it paints and measures from the same source.
        virtual float measureStripItem(int index) const;

        /// @brief Paints item i (default: hover / current item background + centered item text)
        virtual void paintStripItem(FrameCollector& fc, int index, const ItemRect& r) const;

        /// @brief Separator width (default: half the text width, at least 4px; returning 0 = no separator)
        virtual float separatorWidth() const;

        /// @brief Paints the separator (r is the rectangle given to the separator; default paints nothing)
        virtual void paintStripSeparator(FrameCollector& fc, const ItemRect& r) const;

        /// @brief Self-painted background of the whole strip (default paints nothing)
        virtual void paintStripBackground(FrameCollector& fc) const;

        /// @brief Hook for when the user changed the current item (click / arrow keys, value already written)
        /// @param index new current item index
        /// @note Default is empty. Derived classes override it to emit their own semantic callbacks
        ///       (breadcrumb -> "navigate to level N", pagination -> "jump to page N").
        ///       WARNING: programmatic SetCurrentIndex does NOT reach here -- that is "setting a value",
        ///       not a "user action" (the library-wide loop-prevention convention).
        virtual void OnCurrentChangedByUser(int index) { (void)index; }

        /// @brief Current item exit on the user path (shared by click / keyboard): set value + notify
        /// @param index target item index (out of range / same / disabled are all silently ignored)
        /// @note Derived classes use it when adding new keys (e.g. Up / Down for vertical navigation) --
        ///       this way the convention "programmatic set does not fire callbacks, only user actions do" has a single implementation.
        void selectByUser(int index);

        // ===================================================================
        //  Utilities available to derived classes
        // ===================================================================

        /// @brief Measures the width of a piece of text
        /// @note During paint, measures with fc (same source as paint, exactly consistent); outside paint (hintWidth /
        ///       layout) falls back to Window::MeasureText; returns 0 when neither is available.
        float MeasureStripText(FrameCollector* fc, std::string_view text,
            const FontDesc& font) const;

        /// @brief Current hovered item index (-1 = none)
        /// @note Item backgrounds pick colors by hover state, and hover state is a private transient of this class --
        ///       derived classes need to read it for self-painted items, so this read-only accessor is exposed.
        int hoverIndex() const { return hover_; }

        /// @brief Layout direction (generally not changed after construction; call Invalidate() if changed)
        void SetOrientation(StripOrientation o);
        /// @brief Current layout direction
        StripOrientation orientation() const { return orientation_; }
        /// @brief Whether the layout is vertical
        bool isVertical() const { return orientation_ == StripOrientation::Vertical; }

        /// @brief Whether the selected item is currently "pressed" (-1 = none)
        int pressedIndex() const { return pressed_; }

        /// @brief Currently effective item text style (current item -> itemActive, others -> item)
        const TextStyle& itemTextStyle(int index) const;

        // ---- Styles injected from the theme (read directly by derived classes) ----
        ItemStripStyle styleTheme_;   /// < theme itemStrip section

    private:
        // -------------------------------------------------------------------
        //  Internal
        // -------------------------------------------------------------------

        /// @brief Recomputes / reuses item geometry (recomputes only when dirty) -- the single source shared by paint and hit test
        void ensureGeometry() const;

        /// @brief Marks dirty: item data or size changed
        void invalidateGeometry() const { geomDirty_ = true; }

        /// @brief Marks dirty when the content area changed (geometry depends on available width + content area top-left)
        /// @details
        /// Why the position is watched too: this strip does not scroll itself, but it is often placed in a ScrollView
        /// -- when scrolling, the parent only translates the child as a whole (relayout writes a new y()), and the available
        /// width does not change at all. Watching only the width would never invalidate the cache, so circles / text / connecting lines
        /// would stay at their pre-scroll coordinates and the whole widget would look like it did not scroll (observed 2026-11-07).
        /// The cost is two extra float comparisons per geometry access, which is negligible.
        void syncStripArea() const;

        /// @brief Fires the registered callbacks (payload version first, then sender version)
        void notifyCurrentChanged(int index);

        /// @brief Requests a repaint (silently skipped when there is no window)
        void requestRepaint();

        // ---- Item data ----
        struct Item {
            std::string label;
            bool        enabled = true;
        };
        std::vector<Item> items_;
        int current_ = -1;
        StripOrientation orientation_ = StripOrientation::Horizontal;

        // ---- Interaction transients (do not change data, no markStyled needed) ----
        int hover_ = -1;
        int pressed_ = -1;

        // ---- Geometry cache (mutable: const Paint must also recompute on demand) ----
        //  Same discipline as ComboBox computing popup geometry once and sharing it between paint and hit test.
        mutable std::vector<ItemRect> geom_;
        mutable float geomUsed_ = 0.0f;      /// < width actually occupied by this strip (no trailing separator)
        mutable bool  geomDirty_ = true;
        mutable float cachedWidth_ = -1.0f;  /// < available width at the last geometry computation (recompute on mismatch)
        mutable float cachedHeight_ = -1.0f; /// < available height at the last geometry computation (item height / vertical centering depend on it)
        mutable float cachedLeft_  = -1.0f;  /// < content area top-left at the last geometry computation
        mutable float cachedTop_   = -1.0f;  /// < (scroll translation changes them while the width stays the same)

        // ---- Callbacks ----
        SelfAction1<int> onChanged_;
        SelfAction       onChangedSelf_;
    };

} // namespace Kit
