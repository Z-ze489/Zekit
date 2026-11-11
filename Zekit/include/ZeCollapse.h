/**
 * @file ZeCollapse.h
 * @brief Collapse panel: a list of expandable / collapsible header bars, each with its own content area below
 *
 * @details
 * `Collapse : Panel` -- it is a container, so it has `DoLayout`; but its layout rules
 * differ from `layoutLinear`: the content area must sit directly below its own header bar,
 * and the header bar is painted by this widget itself (it is not a child widget). So this class
 * implements `DoLayout` itself:
 *
 * @verbatim
 *   +--------------------------+  <- header bar 1 (self-painted, headHeight tall)
 *   | > General settings       |
 *   +--------------------------+
 *   |  content 1 (takes space only when expanded) |  <- contentPanel(0)
 *   +--------------------------+
 *   | v Advanced settings      |  <- header bar 2
 *   +--------------------------+
 *   |  content 2               |  <- contentPanel(1)
 *   +--------------------------+
 * @endverbatim
 *
 * @par Three key decisions
 *  1. The content area is a child container: `AddItem` creates a `Panel` as the content area, and the user adds widgets into it
 *     -- so "anything can go inside a collapse panel" is guaranteed by structure, and each kind of content does not need to be written separately.
 *  2. Collapsed = hide the child: via `SetVisible(false)`; hidden children do not participate in layout
 *     (`LayoutBase::visibleChildren`) and are not painted. No "set height to 0" scheme is needed.
 *  3. Hand-written `DoLayout`: does not use `layoutLinear` -- that only stacks children evenly,
 *     whereas here each content area must first leave room for a header bar, and the header bar height is fixed.
 *     WARNING: still follows base class discipline: call `LayoutAccess::RestorePref` on all children at the start
 *     (not resetting the baseline makes layout ratchet one way: it keeps up when the window grows but cannot go back when it shrinks).
 *
 * @par Accordion mode
 * `SetAccordion(true)` -> only one item may be expanded at a time (expanding one collapses the others).
 *
 * @par Usage
 * @code
 * Kit::Collapse acc;
 * acc.SetAccordion(true);                       // accordion: only one open at a time
 * Kit::Panel* p0 = acc.AddItem("General settings");
 * p0->AddChild(Kit::Label("Content of general settings"));
 * Kit::Panel* p1 = acc.AddItem("Advanced settings");
 * p1->AddChild(Kit::Label("Content of advanced settings"));
 * acc.SetItemExpanded(0, true);                 // expand item 1 by default
 * acc.SetOnItemToggled([](Kit::Widget& self, int i, bool open) {
 *     std::printf("item %d %s\n", i, open ? "expanded" : "collapsed");
 * });
 * win.AddWidget(acc);
 * @endcode
 *
 * @note "User clicks the header bar" and "program calls `SetItemExpanded`" are two paths: only the former triggers the callback
 *       -- same rule as `TabView` / `ComboBox` / `Pagination`.
 *
 * @see ZePanel.h   base class (it is itself the branch that "can hold children")
 * @see ZeCard.h    same family of "container with a title", but a card cannot collapse
 */

#pragma once
#include "ZePanel.h"
#include "ZeSelfAction.h"
#include "ZeTheme.h"

#include <cstddef>
#include <string>
#include <vector>

namespace Kit {

    /**
     * @brief Collapse panel (accordion)
     *
     * @details
     * Theme section `collapse`: `panel` controls the overall frame, `head` / `headActive` /
     * `headDisabled` control the three-state text of the header bar, `headColor` / `headHoverColor` /
     * `headActiveColor` control the three-state background of the header bar, `indicatorColor` controls the expand arrow.
     */
    class Collapse : public Panel {
    public:
        Collapse();

        // -------------------------------------------------------------------
        //  Items (data)
        // -------------------------------------------------------------------

        /**
         * @brief Appends a collapse item
         * @param title Header text
         * @return Address of the content area container (stable; call `AddChild` on it to add content)
         * @note The returned `Panel*` points to the entity in the tree (the heap object pointed to by `unique_ptr`),
         *       and does not become invalid when this widget is moved.
         */
        Panel* AddItem(std::string title);

        /// @brief Number of items
        std::size_t itemCount() const { return items_.size(); }

        /// @brief Returns the content area container of an item (`nullptr` if out of range)
        Panel* itemContent(int index);

        /// @brief Sets the title of an item (silently ignored if out of range)
        void SetItemTitle(int index, std::string title);
        /// @brief Returns the title of an item (returns a static empty string if out of range)
        const std::string& itemTitle(int index) const;

        /// @brief Removes all items (content areas are destroyed as well)
        void ClearItems();

        // -------------------------------------------------------------------
        //  Expanded state
        // -------------------------------------------------------------------

        /// @brief Sets whether an item is expanded from code (does not trigger the callback; silently ignored if out of range)
        void SetItemExpanded(int index, bool expanded);

        /// @brief Whether an item is expanded (returns false if out of range)
        bool isItemExpanded(int index) const;

        /// @brief Toggles the expanded state of an item (program path: does not trigger the callback)
        void ToggleItem(int index) { SetItemExpanded(index, !isItemExpanded(index)); }

        /// @brief List of indices of currently expanded items (ascending by index)
        std::vector<int> expandedItems() const;

        /// @brief Whether an item is interactive (a disabled item has a grayed title, cannot be clicked, and does not expand)
        void SetItemEnabled(int index, bool enabled);
        /// @brief Whether an item is interactive (returns false if out of range)
        bool isItemEnabled(int index) const;

        // -------------------------------------------------------------------
        //  Accordion mode
        // -------------------------------------------------------------------

        /// @brief Accordion mode: only one item may be expanded at a time (expanding one collapses the others)
        void SetAccordion(bool on);
        /// @brief Whether accordion mode is currently on
        bool isAccordion() const { return accordion_; }

        // -------------------------------------------------------------------
        //  Appearance
        // -------------------------------------------------------------------

        /// @brief Header bar height (pixels; 0 = follow theme `collapse.headHeight`)
        void SetHeadHeight(float h);
        /// @brief Current header bar height
        float headHeight() const { return headHeight_; }

        /// @brief Size and line width of the expand arrow
        void SetIndicator(float size, float thickness);

        // -------------------------------------------------------------------
        //  Callbacks (triggered only on the user path)
        // -------------------------------------------------------------------

        /// @brief Sets the "user expanded / collapsed an item" callback (with sender + index + new state)
        void SetOnItemToggled(SelfAction2<int, bool> cb) { onToggled_ = std::move(cb); }
        /// @brief Sets the "user expanded / collapsed an item" callback (with sender + index only)
        void SetOnItemToggled(SelfAction1<int> cb) { onToggledIndex_ = std::move(cb); }
        /// @brief Sets the "user expanded / collapsed an item" callback (sender only)
        void SetOnItemToggled(SelfAction cb) { onToggledSelf_ = std::move(cb); }

        // -------------------------------------------------------------------
        //  Measurement protocol
        // -------------------------------------------------------------------
        float hintWidth() const override;
        float hintHeight() const override;

        // -------------------------------------------------------------------
        //  Theme
        // -------------------------------------------------------------------
        // @brief Re-injects from theme section `collapse` (panel box + three header states + arrow)
        void ApplyTheme(const Theme& t) override;

        // -------------------------------------------------------------------
        //  Layout / paint / events
        // -------------------------------------------------------------------

        /// @brief Hand-written layout: walks down item by item "header bar -> content area" (see class comment)
        void DoLayout() override;

        /// @brief Paints the frame + each header bar (content is painted recursively by the window)
        void Paint(FrameCollector& fc) const override;

        /// @brief Left button press: consumed only when it lands on a header bar
        bool OnMousePress(MouseEvent& e) override;
        /// @brief Left button release: commits the toggle only when it lands on the header bar that was pressed
        bool OnMouseRelease(MouseEvent& e) override;
        /// @brief Maintains header bar hover highlight
        bool OnMouseMove(MouseEvent& e) override;
        /// @brief Clears hover on leave
        void OnMouseLeave() override;

        /// @brief `Up` / `Down` move focus between header bars, `Space` / `Enter` toggles the current bar
        bool OnKeyPress(KeyEvent& e) override;

    private:
        /// @brief One collapse item
        struct Item {
            std::string title;
            Panel* content = nullptr;   /// < Content area container (borrowed; heap object address is stable)
            bool expanded = false;
            bool enabled = true;
        };

        /// @brief Rectangle of header bar i (shared by paint and hit test)
        bool headRect(int index, float& hx, float& hy, float& hw, float& hh) const;

        /// @brief Which header bar a point lands on (-1 = not on any header bar)
        int headAtPoint(float px, float py) const;

        /// @brief Overall content box (widget rectangle minus the frame border)
        void bodyRect(float& bx, float& by, float& bw, float& bh) const;

        /// @brief Toggle exit for the user path (shared by click / keyboard): set value + callback
        void toggleByUser(int index);

        /// @brief Fires the registered callbacks (payload version first, then down to sender-only version)
        void notifyToggled(int index, bool expanded);

        /// @brief Relayout + repaint (single exit after the expanded state changes)
        void refreshAfterToggle();

        /// @brief Requests a repaint (silently skipped when there is no window)
        void requestRepaint();

        /// @brief Returns the content area height by index (0 when collapsed)
        /// @note Reads the measured result of the previous layout pass (`contentHeights_`).
        ///       Why not have `headRect` measure it again: paint and hit test must use the same geometry
        ///       (same discipline as `ComboBox::computePopup`) -- computing it separately will eventually misalign.
        float contentHeight(int index) const;

        // ---- Data ----
        std::vector<Item> items_;
        /// @brief Measured height of each content area (written by `DoLayout`, read by `headRect` / `Paint` / `hintHeight`)
        /// @note Storing it is deliberate: geometry must have a single source, otherwise header bar position (hit test)
        ///       and content area position (layout) are computed separately and misalign.
        std::vector<float> contentHeights_;
        int hover_ = -1;         /// < Index of the hovered header bar
        int pressed_ = -1;       /// < Index of the pressed header bar
        int focus_ = -1;         /// < Header bar holding keyboard focus (the "current bar" outside Tab)
        bool accordion_ = false;

        // ---- Appearance (theme injection) ----
        CollapseStyle styleTheme_;
        float headHeight_ = 0.0f;            /// < 0 = follow theme
        float indicatorSize_ = 0.0f;         /// < 0 = follow theme
        float indicatorThickness_ = 0.0f;    /// < 0 = follow theme

        // ---- Callbacks ----
        SelfAction2<int, bool> onToggled_;
        SelfAction1<int>       onToggledIndex_;
        SelfAction             onToggledSelf_;
    };

} // namespace Kit
