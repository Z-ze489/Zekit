#pragma once
#include "ZeLayoutBase.h"
#include "ZeSelfAction.h"    // Event callback protocol: self is injected by the library during dispatch

// Scroll layout: content may exceed the viewport, wheel scrolling, automatic clipping on overflow, and scroll bars are painted

namespace Kit {

    // Scroll direction.
    enum class ScrollDirection {
        Vertical,    // Vertical: single column downwards, Y scrolls when content is taller
        Horizontal,  // Horizontal: single row to the right, X scrolls when content is wider
        Both         // Both axes: wrapping flow, X and Y scroll together
    };

    class ScrollView : public LayoutBase {
    public:
        ScrollView();

        /// @brief Sets the scroll direction
        void SetScrollDirection(ScrollDirection dir);
        ScrollDirection scrollDirection() const { return scrollDir_; }

        /**
         * @brief Sets the horizontal scroll offset
         * @param x Target offset, **negative values are clamped to zero**; **the upper bound is clamped by the next layout**
         * @note "SetScroll first, then layout" is safe: the upper bound (`maxScrollX()`) is only clamped in the next
         *       layout, so scrolling to the bottom at initialization and scrolling to the bottom after content grows are both reliable.
         *       See the note on `scrollX()` for the cost.
         *
         * @note **Clamping does not rewrite the value set here**. Layout only computes "how much to use for this placement"
         *       into `effectiveScrollX()`; after the view temporarily grows (content fits, upper bound becomes 0) and
         *       then recovers, the position comes back by itself.
         *       WARNING: early versions wrote the clamped result back to the member, so "a loose size given during a
         *       collapse animation / measurement" would **permanently reset** the reading position to zero -- that was a bug, not clamping semantics.
         */
        void SetScrollX(float x);
        /// @brief Sets the vertical scroll offset (negative values are clamped to zero; the upper bound is clamped by the next layout and does not rewrite the set value)
        void SetScrollY(float y);

        /// @brief Current horizontal scroll offset (= **intent value**, not necessarily the one actually used on screen)
        /// @note Right after Set and before relayout, this may read a value **not yet clamped by the upper bound** --
        ///       this is intentional: only one source of truth is kept (what you set is what you read),
        ///       and the getter does not add another layer of clamping. To see the "actual placement value", use `effectiveScrollX()`.
        float scrollX() const { return scrollX_; }
        /// @brief Current vertical scroll offset (same as `scrollX()`: may read an unclamped value before layout)
        float scrollY() const { return scrollY_; }

        /// @brief Horizontal offset used by the last layout for **actual placement** = clamp(`scrollX()`, `maxScrollX()`)
        /// @details This is the truth of "where the screen is showing right now": when the intent exceeds the upper bound, the two differ.
        ///          Painting (scroll bar thumb), scroll notifications, and hit testing should all use this value.
        float effectiveScrollX() const { return effScrollX_; }
        /// @brief Vertical offset used by the last layout for actual placement (same as `effectiveScrollX()`)
        float effectiveScrollY() const { return effScrollY_; }

        /// @brief Current scroll upper bound (the amount by which content exceeds the viewport; written by the last layout)
        float maxScrollX() const { return maxScrollX_; }
        float maxScrollY() const { return maxScrollY_; }

        // -------------------------------------------------------------------
        //  Scroll notifications (outward events)
        // -------------------------------------------------------------------
        //  Fired each time the offset **actually changes** (wheel / dragging the scroll bar / clicking the track / programmatic
        //  SetScrollX/Y all count; no change in value means no fire -- same rule as SetVisible's "notify only on value change").
        //
        //  The sender is injected by the library during dispatch and always points to the entity in the tree -- so the following "use the sender
        //  in the scroll callback" pattern is safe:
        //      sv.SetOnScroll([](Kit::Widget& self, float x, float y) {
        //          auto& sv = Kit::As<Kit::ScrollView>(self);
        //      });
        //  See ZeSelfAction.h for details.
        //
        //  WARNING: the protocol type (SelfAction2<...>) is **the only copy in the whole library**, defined in ZeSelfAction.h.
        // -------------------------------------------------------------------

        /// @brief Scroll callback type: parameters = the (x, y) offset after scrolling
        using ScrollAction = std::function<void(float, float)>;

        /// @brief Sets the scroll callback (without sender)
        /// @param cb Callback; pass empty to clear
        void SetOnScroll(ScrollAction cb) { onScroll_ = std::move(cb); }
        /// @brief Sets the scroll callback (with sender) -- register this to use the sender during scrolling
        /// @param cb Callback; `self` is the widget itself (use `Kit::As<ScrollView>` to get the concrete interface)
        /// @note Can be registered together with the version without sender; during dispatch the **no-sender version runs first, then the sender version**.
        void SetOnScroll(SelfAction2<float, float> cb) { selfOnScroll_ = std::move(cb); }

        // ===================================================================
        //  Events: wheel scrolling + scroll bar dragging
        // ===================================================================
        //  Wheel: `Vertical` scrolls Y / `Horizontal` scrolls X / `Both` scrolls Y by default, **hold Shift to scroll X**
        //        (under `Both`, if Y has no scrollable space at all, it automatically scrolls X instead -- see .cpp)
        //  Mouse: hold and drag the thumb = scroll; click the track = page one screen in that direction.
        //
        //  All three share the same convention: **consume only if scrolling actually happened / the scroll bar was actually grabbed**
        //  (return false to let the event bubble to the parent container) -- otherwise "eating the event even at the top" would
        //  deadlock the outer layer of nested containers (a scroll area inside a scroll area).
        // ===================================================================

        /// @brief Wheel scrolling (consumption rule: returns true only if the offset actually changed)
        bool OnMouseWheel(MouseEvent& e) override;

        /// @brief Left button press: on the thumb -> start dragging; on the track -> page one screen and take over dragging
        /// @note The scroll bar is **painted by this widget itself** (not a child widget), so its clicks do not hit any
        ///       child item and must be handled here. Returning true means "the press point belongs to the scroll bar",
        ///       and bubbling stops (avoiding being treated as a click on empty space by the outer layer).
        bool OnMousePress(MouseEvent& e) override;

        /// @brief During drag: linearly maps back to the scroll offset by "the point where the thumb was grabbed"
        /// @note During dragging, `Window` sends all Move events back to the pressed widget, so moving the mouse off
        ///       the scroll bar or even out of the window does not break it.
        bool OnMouseMove(MouseEvent& e) override;

        /// @brief Left button release: ends dragging (`dragAxis_` must be cleared, otherwise the next move continues dragging)
        bool OnMouseRelease(MouseEvent& e) override;

        // -------------------------------------------------------------------
        //  Touch drag scrolling (mobile)
        // -------------------------------------------------------------------
        //  Why this is needed in addition to the scroll bar: on desktop "wheel + drag the scroll bar" is enough, but
        //  **a finger on a phone simply cannot hit that 8-pixel-wide scroll bar**. The mobile convention is
        //  "dragging the content scrolls", so raw touch events are taken over here and handled directly.
        //
        //  * Works **in parallel** with the mouse path: Kit synthesizes the primary pointer into a left mouse button, so child widgets
        //    (buttons etc.) still work normally; this group of callbacks only handles "finger dragging in the content area".
        //    "Content area" is **literal** -- a gesture whose start point lands on the scroll bar yields the whole
        //    gesture to the mouse path's "drag the scroll bar" (criterion in `scrollBarAxisAt`).
        //    Without yielding they fight: the same gesture is driven by both paths at once, and their target values
        //    point in opposite directions (touch is "content follows the finger": `start offset - displacement`; the scroll bar is
        //    "where the thumb should be": proportional to scroll progress) -- the offset is written twice per frame, the picture oscillates;
        //    and the touch path, which runs later, always wins: swiping the finger down => the target keeps decreasing => on release it stops at the origin.
        //  * Bubbling protocol: when judged as "not dragging", returns false and the event keeps bubbling to the parent --
        //    this is the future foothold for nested scrolling (inner layer reaches the end, outer layer takes over).
        bool OnTouchBegin(TouchEvent& e) override;
        bool OnTouchUpdate(TouchEvent& e) override;
        bool OnTouchEnd(TouchEvent& e) override;
        bool OnTouchCancel(TouchEvent& e) override;

        // ---- Clip region: clip with the "visible area" rather than the "content area"; out-of-bounds children are clipped ----
        bool clipChildrenRect(float& x, float& y, float& w, float& h) const override;

        void DoLayout() override;

        /// @brief Describes the scroll bar (track + thumb); position is computed from `scroll*_ / maxScroll*_`
        /// @note The scroll bar hugs the widget **edge** and spans the full height/width, **not constrained by padding** --
        ///       so it is longer than the content area by one padding length. This is intentional
        ///       (the scroll bar belongs to the widget border, not the content area).
        /// @note In Both mode the two bars overlap at the bottom-right 8x8 corner (the later-painted horizontal bar wins),
        ///       visually an L-shaped intersection, which is also a common practice.
        void Paint(FrameCollector& fc) const override;

        // -------------------------------------------------------------------
        //  Measurement protocol passthrough: pass "height depends on width" further up
        // -------------------------------------------------------------------
        //  The chain is usually VBox -> ScrollView -> FlowLayout. Only ScrollView itself knows the real height of
        //  the inner FlowLayout at that width, so it must both:
        //    - acknowledge "my height also depends on width" (otherwise the outer VBox will not ask);
        //    - in hintHeightFor(width), forward the width to the content and answer the height after wrapping.
        //  Without this link, the outer VBox falls back to this class's hintHeight() -- which is the wrong value of
        //  "children one line high", showing as "the scroll area only displays the first line".
        // -------------------------------------------------------------------
        // @brief When scrolling vertically, this view's height depends on the width given to it (the inner flow layout wraps)
        bool heightDependsOnWidth() const override;

        /// @brief The height this view (vertical scrolling) needs at the given available width
        /// @param availWidth The width the outer layer intends to give this view
        /// @return The height of the content after wrapping at that width + top and bottom padding; falls back to the base class when not scrolling vertically
        /// @note This is a **passthrough** of `hintHeightFor`: the width first has its own padding and scroll bar reservation subtracted,
        ///       then is handed to the inner children for measurement.
        float hintHeightFor(float availWidth) const override;

    protected:
        /// @brief Reserves space for the right vertical scroll bar (see LayoutBase::reservedRight)
        float reservedRight() const override;
        /// @brief Reserves space for the bottom horizontal scroll bar
        float reservedBottom() const override;

    private:
        /// @brief Synchronizes the size policy with the current scroll direction (scroll axis = Expanding, see .cpp notes)
        /// @note Called once in the constructor and once in `SetScrollDirection`; both must share the same source.
        void syncSizePolicy();

        /// @brief Fires the scroll notification after the offset actually changes (no-arg version first, self version after)
        /// @note Single funnel: both SetScrollX/Y and DoLayout's clamping must notify, avoiding writing it twice.
        /// @param x,y The **actually visible** offset (SetScroll* passes the intent value, DoLayout passes the effective value)
        void notifyScrolled(float x, float y);

        // -------------------------------------------------------------------
        //  Scroll bar geometry: **single source of truth**
        // -------------------------------------------------------------------
        //  Paint and mouse hit testing (drag / page) must use the same set of numbers -- otherwise "the thumb you see"
        //  and "the thumb you can grab" are misaligned (symptom: clicking the center of the thumb is treated as a track page).
        //  So the track / thumb rectangles + upper bound + current offset are packed into one value, and both sides read from it.
        //  `visible=false` covers three cases: the direction does not include that axis / that axis has no scrollable space / size is 0.
        // -------------------------------------------------------------------
        struct BarGeometry {
            bool  visible   = false;
            float trackX = 0.0f, trackY = 0.0f, trackW = 0.0f, trackH = 0.0f;  /// < Track (hugs the widget edge, spans the full length)
            float thumbX = 0.0f, thumbY = 0.0f, thumbW = 0.0f, thumbH = 0.0f;  /// < Thumb
            float maxScroll = 0.0f;   /// < Scroll upper bound for that axis (a bar exists only if > 0)
            float scroll    = 0.0f;   /// < Current offset for that axis (already clamped within the upper bound)
        };

        /// @brief Geometry of the right vertical scroll bar (`visible=false` when the direction excludes Y / no overflow)
        BarGeometry verticalBarGeometry() const;
        /// @brief Geometry of the bottom horizontal scroll bar (`visible=false` when the direction excludes X / no overflow)
        BarGeometry horizontalBarGeometry() const;

        /// @brief Which **available** scroll bar (x,y) falls on: `0` = neither, `1` = horizontal bar, `2` = vertical bar        /// @note The return value uses the same encoding as `dragAxis_`; hit priority matches `Paint`'s drawing order
        ///       (in Both mode the two overlap at the bottom-right corner and the horizontal bar is painted later on top, so the horizontal bar is tested first).
        /// @note **Three places share this one criterion**: painting (Paint's visibility), mouse press (drag / page),
        ///       touch takeover (whether the finger start point is in the content area) -- if any one computes it separately,
        ///       "the bar you see" and "the bar that yields" become misaligned (the old path of two paths fighting again).
        int scrollBarAxisAt(float x, float y) const;

        // -------------------------------------------------------------------
        //  Anchor: after relayout, put "the content originally being viewed" back in place
        // -------------------------------------------------------------------
        //  Why needed: the scroll offset is in **pixels**, and after content relayout (width changes -> wrapping changes)
        //  the same pixel points to **different text**. What the user sees is "the content moved by itself".
        //  So what must be recorded is not the offset, but "at what percentage of which child item the viewport top edge falls".
        //
        //  * Why record it **at the end of the previous layout** (rather than capturing it at the start of this one):
        //    before entering DoLayout, `LayoutBase::EnsureLayout` first calls `RestorePref` on all children, and container children
        //    are immediately squashed back to "one line high" -- the position is still there but the height is no longer
        //    trustworthy, so "at what percentage it is cut" cannot be computed at all (measured: h=30 at capture time,
        //    the real height after the same layout is 300). Only the geometry at the **end** of layout is the final value.
        // -------------------------------------------------------------------
        // @brief Records "at what percentage of which child item the viewport top edge falls" (called at the end of each layout)
        void captureAnchor();

        /// @brief Anchor child item (weak reference: checked against `children()` before use)
        /// @note Used only for pointer comparison, never dereferenced, so a deleted child item does not cause a dangling access.
        Widget* anchorChild_ = nullptr;
        float   anchorTY_ = 0.0f;      /// < Vertical ratio of the viewport top within the anchor child item (0 = top edge, 1 = bottom edge)
        float   anchorTX_ = 0.0f;      /// < Horizontal ratio of the viewport left within the anchor child item
        bool    anchorValid_ = false;

        ScrollDirection scrollDir_ = ScrollDirection::Vertical;

        // -------------------------------------------------------------------
        //  Two identities of the offset: **intent** and **effective value**
        // -------------------------------------------------------------------
        //  scroll*_  = intent (set by the user / program, only kept non-negative)
        //  effScroll*_ = used for actual placement in this layout (= clamp(intent, upper bound))
        //
        //  * Why they must be separate: when merged, a layout where "the upper bound becomes 0" (viewport taller than content,
        //    an intermediate frame of a collapse animation, a loose size given during measurement) writes the intent as 0 -- after the viewport recovers
        //    the position cannot come back either, and the user's reading position is **permanently** eaten.
        //    Once separated, clamping only means "where it is displayed this time", not "where you want to go".
        // -------------------------------------------------------------------
        float scrollX_ = 0.0f;
        float scrollY_ = 0.0f;
        float effScrollX_ = 0.0f;
        float effScrollY_ = 0.0f;
        float maxScrollX_ = 0.0f;
        float maxScrollY_ = 0.0f;

        // ---- Touch drag scrolling state ----
        // @brief Drag threshold (logical pixels): the finger must move beyond it to count as "dragging" rather than "clicking".
        // @note Same order of magnitude as Android's ViewConfiguration.getTouchSlop() (8dp).
        //       Too small -> a slight hand tremor during a click is judged as a drag and buttons cannot be pressed;
        //       too large -> the first part of the drag is eaten, feeling "sticky".
        static constexpr float kTouchSlop = 8.0f;

        /// @brief Gesture start point and the content offset at that time (the whole drag is computed relative to the start point, no incremental accumulation)
        /// @note Using the relative start point instead of per-frame increments **avoids error accumulation** and also lets
        ///       edge cases like "content grows/shrinks during the drag" converge naturally (clamping applies only to the target value).
        bool  touchActive_ = false;    /// < Finger is down (not yet judged as a drag)
        bool  touchDragging_ = false;  /// < Threshold exceeded, dragging the content
        int   touchAxis_ = 0;          /// < 1 = horizontal, 2 = vertical, 0 = undetermined
        float touchStartX_ = 0.0f;
        float touchStartY_ = 0.0f;
        float touchStartScrollX_ = 0.0f;
        float touchStartScrollY_ = 0.0f;

        /// @brief Which scroll bar is being dragged: 0 = none, 1 = horizontal bar, 2 = vertical bar
        int   dragAxis_ = 0;
        /// @brief The "relative position within the thumb" grabbed at press time (pixels). Kept constant during the drag,
        ///        so the thumb does not jump directly under the mouse at the moment of pressing.
        float dragGrab_ = 0.0f;

        // Scroll callbacks (std::function is movable and is moved along with this class's implicit move constructor by AddChild).
        // The two signatures are stored separately: the no-sender version and the sender version do not affect each other and may be registered together (both are called during dispatch).
        ScrollAction              onScroll_;      /// < Scroll callback (without sender)
        SelfAction2<float, float> selfOnScroll_;  /// < Scroll callback (with sender)
    };
}
