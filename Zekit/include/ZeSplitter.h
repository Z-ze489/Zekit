/**
 * @file ZeSplitter.h
 * @brief Kit::Splitter -- two panels plus a draggable splitter bar in between
 *
 * @details
 * Inherits `Kit::Panel`: arrangement, padding, gap and background all come for free. This class only does two
 * more things -- fixes the size relationship between the two panels, and turns the gap between them into a draggable splitter bar.
 *
 * @par How it reuses what exists (no new mechanism added)
 *  - Arrangement: the two children go to `LayoutBase::layoutLinear`, and the container's `gap` reserves
 *    the splitter bar's position -- so the splitter bar "naturally has a place in the layout" and no coordinates need to be computed;
 *  - Dragging: after `Window::dispatchMousePress` hits, subsequent Move / Release are
 *    always routed back to the pressed widget (pointer capture), so this class only has to handle "whether the press point is on
 *    the splitter bar" and does not need `SetCapture`;
 *  - Sizing: the first panel has a fixed main-axis size (`pref`, changed by dragging), the second has `flexGrow = 1`
 *    and takes the rest -- so "drag the left bigger, the right shrinks naturally" is computed by the layout itself, with no manual sync.
 *
 * @par Usage
 * @code
 * Kit::Splitter sp;                       // default left-right split
 * auto& left  = sp.SetFirst(Kit::Panel());
 * auto& right = sp.SetSecond(Kit::Panel());
 * sp.SetFirstSize(220.0f);                // left column 220px (defaults to 40% of the container main axis)
 * sp.SetMinFirst(120.0f);                 // left column minimum 120px (clamped while dragging)
 * win.AddWidget(sp);
 * @endcode
 *
 * @par How the splitter bar is painted (redone 2026-11-10)
 * The `gap` slot has no child widget covering it, so a mouse press hits the Splitter itself (not a child
 * panel) -- this class catches the drag on that basis.
 *
 * Painting has two states, and is not "fill the whole bar with color":
 * - Normal: a 1px thin line centered (`splitter.handleColor`). It reads as a divider line,
 *   the same weight as a card border, so it does not stand out in any UI;
 * - Hover / drag: becomes a capsule grip `handleThickness` (default 3px) thick,
 *   using `splitter.handleActiveColor` (primary color), inset `handleInset` (default 8px) at both ends --
 *   "a short grabable segment" looks more draggable than "a pillar from end to end".
 *
 * Previously the whole gap was filled with the normal color, which in a 6~8px gap is a highly saturated vertical bar,
 * and every divider in the UI was a colored bar -- it looked like a missing layer, and readers first took it for "misaligned blocks".
 *
 * @note The feel is unchanged: the hit area is still the `handleWidth` gap (default 6px), only painted thinner.
 *
 * @note Only two panels are supported. For three or more, nest: `Splitter(Splitter(a,b), c)`.
 * @note `Panel` does not receive theme injection (`Panel::ApplyTheme` is empty), so this class only takes
 *       the two colors from the `splitter` section and does not touch the base class.
 *
 * @see ZePanel.h   base class (source of arrangement / background / padding)
 * @see ZeSlider.h  another implementation of the "user drags" interaction line (continuous update + pointer capture)
 */

#pragma once
#include "ZePanel.h"

#include <algorithm>
#include <utility>

namespace Kit {

    /**
     * @brief Split panel: two panels plus a draggable splitter bar
     */
    class Splitter : public Panel {
    public:
        /**
         * @brief Constructs the split panel
         * @param horizontal true = left-right split (splitter bar vertical, first panel on the left);
         *                   false = top-bottom split (splitter bar horizontal, first panel on top)
         */
        explicit Splitter(bool horizontal = true);

        // -------------------------------------------------------------------
        //  Children
        // -------------------------------------------------------------------
        /**
         * @brief Sets the first panel (left / top)
         * @param w widget value (an lvalue is moved internally -- same semantics as Panel::AddChild)
         * @return reference to the copy in the tree (reference is stable)
         * @note The first panel is the one with a fixed main-axis size; dragging changes its size.
         */
        template<class T>
        T& SetFirst(T&& w) {
            T& ref = Panel::AddChild(std::forward<T>(w));
            first_ = &ref;
            ref.SetFlexGrow(0.0f);                 // fixed size, does not share the remainder
            if (second_) second_->SetFlexGrow(1.0f);
            Invalidate();
            return ref;
        }

        /**
         * @brief Sets the second panel (right / bottom)
         * @param w widget value (an lvalue is moved internally)
         * @return reference to the copy in the tree (reference is stable)
         * @note The second panel has `flexGrow = 1` and takes all remaining space besides the first panel and the splitter bar.
         */
        template<class T>
        T& SetSecond(T&& w) {
            T& ref = Panel::AddChild(std::forward<T>(w));
            second_ = &ref;
            ref.SetFlexGrow(1.0f);                 // takes the remainder
            if (first_) first_->SetFlexGrow(0.0f);
            Invalidate();
            return ref;
        }

        /// @brief First panel (returns nullptr if not set)
        Widget* first() const { return first_; }
        /// @brief Second panel (returns nullptr if not set)
        Widget* second() const { return second_; }

        // -------------------------------------------------------------------
        //  Size
        // -------------------------------------------------------------------

        /// @brief Sets the first panel's main-axis size (pixels; clamped by the minimum size and the container size)
        /// @note This pixel value is converted to a ratio on the spot and stored: afterwards, on window resize / maximize restore,
        ///       the first panel is reallocated by the same ratio (see the note on `firstRatio()`).
        void SetFirstSize(float px);
        /// @brief The first panel's current main-axis size (pixels)
        /// @note Stores the target value we last wrote to it (already clamped by the minimum size / container,
        ///       and already including the first panel's own min/max clamping). In extreme cases layout may squeeze it further
        ///       (the container really does not fit), and this value is still the "intended value" -- precisely because of that,
        ///       the ratio can be restored exactly when the window is pulled back to its original size.
        float firstSize() const { return firstSize_; }

        /// @brief The first panel's share of the available main-axis length (0~1)
        ///
        /// @details The split panel stores a ratio, not pixels -- this is a pitfall we hit:
        ///  when storing pixels, after the window switches from normal size to maximized (or back), the first panel keeps that pixel value,
        ///  so the "ratio" drifts with the window size: a 6:4 dragged under maximized becomes 9:1 when restored to a small window.
        ///  The user put it precisely: "the split size should be allocated according to the current window size".
        ///
        ///  So: dragging / `SetFirstSize` change the ratio; when the container main-axis length changes (resize / maximize /
        ///  restore), pixels are recomputed from this ratio. `firstSize()` and it are always consistent with each other.
        /// @note 0 = not yet determined (the next layout starts at 40%).
        float firstRatio() const { return firstRatio_; }

        /// @brief Sets the ratio directly (0~1, clamped by the minimum size)
        void SetFirstRatio(float r);

        /// @brief Minimum main-axis size of the first panel (lower bound while dragging / setting; default 32)
        void SetMinFirst(float px);
        /// @brief Minimum main-axis size of the second panel (default 32)
        void SetMinSecond(float px);
        float minFirst() const { return minFirst_; }
        float minSecond() const { return minSecond_; }

        // -------------------------------------------------------------------
        //  Splitter bar appearance
        // -------------------------------------------------------------------

        /// @brief Thickness reserved for the splitter bar (also the gap between the two panels; default 6)
        void SetHandleWidth(float px);
        float handleWidth() const { return handleW_; }

        /// @brief Normal color of the splitter bar
        void SetHandleColor(const Color4f& c);
        /// @brief Hover / dragging color of the splitter bar
        void SetHandleActiveColor(const Color4f& c);
        /// @brief Corner radius of the splitter bar (pixels; applies only to the grip, not the normal thin line; 0 = square corners)
        void SetHandleRadius(float r);

        /// @brief Thickness of the grip while hovering / dragging (pixels, default 3; pass 0 = change color only, no thickening)
        /// @note The normal state is always a 1px thin line -- the splitter bar should not be a colored pillar.
        void SetHandleThickness(float px);
        float handleThickness() const { return handleThickness_; }

        /// @brief Inset at both ends of the grip (pixels, default 8) -- makes it look like a "grip" rather than "a whole bar"
        void SetHandleInset(float px);
        float handleInset() const { return handleInset_; }

        /// @brief Whether a drag is in progress right now
        bool isDragging() const { return dragging_; }

        // -------------------------------------------------------------------
        //  Orientation
        // -------------------------------------------------------------------

        /// @brief Switches the split orientation (true = left-right, false = top-bottom)
        /// @note After switching, the first panel size keeps the value from the previous orientation; `Invalidate()` triggers relayout.
        void SetHorizontal(bool on);
        /// @brief Whether the split is currently left-right
        bool isHorizontal() const { return horizontal_; }

        // -------------------------------------------------------------------
        //  Interaction
        // -------------------------------------------------------------------
        bool OnMousePress(MouseEvent& e) override;
        bool OnMouseMove(MouseEvent& e) override;
        bool OnMouseRelease(MouseEvent& e) override;
        void OnMouseLeave() override;

        /// @brief Shows the double arrow only over the splitter bar gap (with 2px tolerance)
        /// @note Uses `Widget::cursorAt` (with coordinates) -- the static `SetCursor` cannot express
        ///       "only when moved here". Left-right split gives `SizeWE`, top-bottom gives `SizeNS`;
        ///       during a drag it is always the double arrow (the pointer may already have left that 10px).
        Cursor cursorAt(float x, float y) const override;

        // -------------------------------------------------------------------
        //  Layout / paint / theme
        // -------------------------------------------------------------------
        // @brief First falls back on the first panel size (starts at 40% of the container main axis if never set), then runs Panel's linear arrangement
        void DoLayout() override;

        /// @brief Paints the background (base class) + the splitter bar
        void Paint(FrameCollector& fc) const override;

        /// @brief Takes the two splitter colors + corner radius from the theme (Panel does not receive the theme, so the base class is not called)
        void ApplyTheme(const Theme& t) override;

    protected:
        /// @brief Whether the second panel participates in the split (present and visible)
        /// @note After the app calls `SetVisible(false)` on a collapsible panel such as a "details pane", there is no split
        ///       any more: the first panel fills the main axis, and the splitter bar is neither painted nor clickable. This is decided
        ///       per frame (computed in `DoLayout` / `handleRect`), so it stays consistent on window resize --
        ///       the caller does not have to compute "how much width is left" and manually change the first panel size.
        bool secondVisible() const { return second_ != nullptr && second_->isVisible(); }

        /// @brief Splitter bar rectangle (shared by painting and hit testing -- prevents "looks here but cannot be clicked")
        /// @return false = not yet two panels / thickness is 0 / second panel invisible / degenerate geometry
        bool handleRect(float& x, float& y, float& w, float& h) const;

        /// @brief Whether (px, py) falls on the splitter bar
        bool hitHandle(float px, float py) const;

        /// @brief Sets the first panel's main-axis size to px (written back after clamping by the minimum size and the container size)
        /// @param recordRatio whether to record this clamp result as the "user intent ratio"
        /// @details
        ///  * This parameter is the root fix for "shrink then enlarge, the share drifts"; do not delete it casually:
        ///    callers of this function fall into two kinds --
        ///      1. user intent (`SetFirstSize` / `SetFirstRatio` / dragging the splitter bar):
        ///         pass true. Wherever the user drags or whatever they set becomes the new ratio baseline.
        ///      2. conversion after the container size changed (recomputed by ratio in `DoLayout`):
        ///         pass false. Here `px = ratio x available length`, and it only needs to land on the geometry;
        ///         once it is clamped by the minimum size here (window shrunk very narrow),
        ///         that clamped value only reflects "it does not fit at this moment", not the user's intent --
        ///         recording it into `firstRatio_` permanently pollutes the ratio:
        ///         measured (`_probe_out/splitter_ratio_probe.cpp`, minFirst=220,
        ///         window 1000 -> 300 -> 1000) the left column goes 397.6 -> 743.8,
        ///         and what the user sees is "shrink then enlarge, the split share visibly shifts".
        void applyFirstSize(float px, bool recordRatio = true);

    private:
        bool  horizontal_ = true;    /// < left-right split?
        float handleW_ = 6.0f;       /// < splitter bar thickness = gap between the two panels
        float handleRadius_ = 2.0f;  /// < splitter bar corner radius
        float minFirst_ = 32.0f;
        float minSecond_ = 32.0f;
        float firstSize_ = 0.0f;     /// < first panel's current main-axis size (0 = not yet determined)
        float firstRatio_ = 0.0f;    /// < first panel's share of the available main-axis length (0 = not yet determined)
        float lastAvail_ = -1.0f;    /// < available main-axis length of the previous layout (detects container change; -1 = first round)
        bool  collapsed_ = false;    /// < second panel was invisible (collapsed) in the previous round

        Widget* first_ = nullptr;    /// < first panel (borrowed; heap object address is stable, does not become invalid when the value moves)
        Widget* second_ = nullptr;   /// < second panel (borrowed)

        bool  dragging_ = false;
        float dragStartPos_ = 0.0f;  /// < main-axis coordinate at press
        float dragStartSize_ = 0.0f; /// < first panel size at press
        bool  hoverHandle_ = false;  /// < whether the pointer is hovering over the splitter bar

        float   handleThickness_ = 3.0f;           /// < grip thickness while hovering / dragging
        float   handleInset_ = 8.0f;               /// < inset at both ends of the grip
        Color4f handleColor_{ 0, 0, 0, 0 };        /// < theme injection: normal
        Color4f handleActiveColor_{ 0, 0, 0, 0 };  /// < theme injection: hover / dragging
    };

} // namespace Kit
