// ZeLayoutBase.h
// Shared base for layout containers: axis alignment, spacing, padding, clip rect.
// Subclasses only configure FlexStyle and override DoLayout() / Paint().

#pragma once

#include "ZeWidget.h"
#include "ZeFlexStyle.h"
#include "ZeSelfAction.h"   // Event protocol: self is injected by the library

#include <algorithm>   // std::max (non-negative clamp in effectiveWidth/Height)
#include <unordered_map>   // measuredH_ (height cache for width-dependent children)

namespace Kit {

    // Padding on all four sides (content inset relative to the widget rect)
    struct LayoutPadding {
        float left = 0.0f, top = 0.0f, right = 0.0f, bottom = 0.0f;
    };

    // Common base class for layout containers.
    // Owns FlexStyle and implements axis alignment, spacing, padding and dirty-flag
    // relayout. Per-child properties (margin / flexGrow / alignSelf) live on Widget.

    class LayoutBase : public Widget {
    public:
        LayoutBase() = default;
        ~LayoutBase() override = default;

        LayoutBase(const LayoutBase&) = delete;
        LayoutBase& operator=(const LayoutBase&) = delete;

        // Move is required by value takeover (AddChild / AddWidget move the widget in).
        // All members are trivially movable, so the default move carries the whole tree.
        LayoutBase(LayoutBase&&) noexcept = default;
        LayoutBase& operator=(LayoutBase&&) = delete;   // Address is immutable once attached

        // -------------------------------------------------------------------
        //  Layout style (typed access to FlexStyle)
        // -------------------------------------------------------------------
        // Main axis direction (VBox = Column, HBox = Row)
        void SetDirection(FlexDirection d);
        FlexDirection direction() const { return style_.direction; }

        // Main axis alignment (how leftover space is distributed).
        // With flexGrow children the extra space goes to them first; justify only
        // handles what they cannot take because their min/max clamps them.
        void SetJustify(JustifyContent j);
        JustifyContent justify() const { return style_.justifyContent; }

        /// @brief Cross-axis alignment
        void SetAlign(AlignItems a);
        AlignItems align() const { return style_.alignItems; }

        /// @brief Spacing between children (pixels)
        void SetSpacing(float spacing);
        float spacing() const { return style_.columnGap; }

        /// @brief Row and column spacing (corresponds to CSS gap; a single value means both are equal)
        void SetGap(float rowGap, float columnGap);

        /// @brief Sets the padding on all four sides (pixels)
        void SetPadding(float left, float top, float right, float bottom);
        void SetPadding(float horizontal, float vertical);

        const LayoutPadding& padding() const { return padding_; }

        // -------------------------------------------------------------------
        //  Child management (AddChild / AddWidget = value takeover, reuses Widget's storage mechanism)
        // -------------------------------------------------------------------
        /**
         * @brief "Hands" a widget into this layout (tells the layout "what I look like"; the layout arranges it)
         * @param child Widget value (an lvalue is moved internally)
         * @return Reference to the new location (the one in the tree; the reference is stable)
         *
         * @details
         * Equivalent to Widget::AddWidget: the widget is moved into the layout's storage area, and the layout
         * takes over its lifetime. The layout writes its geometry during arrangement; painting is done by
         * Window recursively over the whole tree -- the developer only needs to call win.AddWidget(layout)
         * once on the layout at the end.
         *
         * @note **This container is exactly where children belong**, so this goes through the internal
         *       implementation (`addWidgetValue`) and does not trigger the "Widget does not arrange
         *       children" deprecation diagnostic from `Widget::AddWidget`.
         */
        template<class T>
        T& AddChild(T&& child) {
            return this->addWidgetValue(std::forward<T>(child));
        }

        /**
         * @brief Same as AddChild -- this class "legitimizes" the deprecated `Widget::AddWidget` entry point
         * @param child Widget value (an lvalue is moved internally)
         * @return Reference to the new location (the one in the tree; the reference is stable)
         *
         * @details
         * Why this layer exists: `Widget::AddWidget` is marked `[[deprecated]]` -- on **leaf widgets** it
         * really is a trap (children get added but nobody arranges them). But layout containers are
         * **precisely** the kind that should hold children, and `layout.AddWidget(x)` is a perfectly valid
         * way to write it.
         *
         * C++ name hiding happens to help here: when a derived class declares a member with the same name,
         * it **hides** the entire set of same-named overloads in the base class. So:
         *   - `vbox.AddWidget(x)` / `panel.AddWidget(x)` -> hits this function, no diagnostic;
         *   - `someWidget.AddWidget(x)` (static type is `Widget`) -> hits the base-class one and gets the
         *     "Widget does not arrange children" compile-time hint -- exactly what we want.
         *
         * @note Two names for the same implementation as AddChild; which one you pick only affects
         *       readability: `AddChild` emphasizes "the layout's child", `AddWidget` matches the Window-side
         *       spelling.
         */
        template<class T>
        T& AddWidget(T&& child) {
            return this->addWidgetValue(std::forward<T>(child));
        }

        // -------------------------------------------------------------------
        //  Layout
        // -------------------------------------------------------------------
        // @brief Marks the layout as needing re-arrangement (performed on the next EnsureLayout)
        void Invalidate();

        // -------------------------------------------------------------------
        //  Container layout-input change notification (outward event)
        // -------------------------------------------------------------------
        //  Same signal as "a child changed itself" (`Widget::notifyLayoutInputChanged`),
        //  just exposed to the outside. Typical use: when container content changes, the outside wants to
        //  know "needs re-measure / re-scroll".
        //  The sender is injected by the library during dispatch and always points to the entity in the
        //  tree (see ZeSelfAction.h).
        //  WARNING: Do **not** change child layout properties inside the callback -- that re-triggers this
        //     notification and easily turns into layout self-excitation; if you must change something,
        //     change things unrelated to this container's arrangement (logging, external state sync).
        //
        //  WARNING: The protocol type (SelfAction) is the **single copy for the whole library**, defined in
        //     ZeSelfAction.h; this class **no longer** defines its own alias -- new widgets get this
        //     protocol for free.
        // -------------------------------------------------------------------

        /// @brief Layout-input change callback type (without sender)
        using LayoutChangedAction = std::function<void()>;

        /// @brief Sets the layout-input change callback (without sender)
        void SetOnLayoutChanged(LayoutChangedAction cb) { onLayoutChanged_ = std::move(cb); }
        /// @brief Sets the layout-input change callback (with-sender version)
        /// @param cb Callback; `self` is this container itself (use `Kit::As<...>` to get the concrete interface)
        /// @note Can be registered together with the no-sender version; during dispatch the **no-sender version runs first, then the sender version**.
        void SetOnLayoutChanged(SelfAction cb) { selfOnLayoutChanged_ = std::move(cb); }

        /// @brief If dirty, immediately re-arranges child geometry (idempotent; normally called by Window each frame)
        void EnsureLayout();

        /**
         * @brief The concrete arrangement algorithm for this direction (implemented by subclasses; writes child geometry + content size)
         *
         * @warning **The implementation must call `LayoutAccess::RestorePref` on all children at the start.**
         *          The layout writes back the "result", while `pref` is the "baseline" -- without restoring,
         *          the previous round's result pollutes this round's baseline, producing a one-way ratchet
         *          where "dragging the window larger keeps up, dragging it smaller never goes back".
         *          `layoutLinear` / `layoutFlow` already do this step (they get called twice in a row by
         *          `ScrollView`, and each pass must start over); do not forget it when writing your own
         *          `DoLayout`. `EnsureLayout` has an extra fallback, but it only covers the pass that is
         *          directly driven.
         */
        virtual void DoLayout() = 0;

        /// @brief Establishes a clip region for children: returns the content visible area (client-area absolute coordinates)
        /// @note Overrides Widget::clipChildrenRect -- Window automatically Push/Pop when recursively rendering children.
        bool clipChildrenRect(float& x, float& y, float& w, float& h) const override;

        /// @brief A layout container of course arranges its children -- so the base class's "wrong place" runtime diagnostic no longer triggers
        /// @note See `Widget::arrangesChildren` and `Widget::reportMisplacedChild`.
        bool arrangesChildren() const override { return true; }

        /// @brief Arrangement result: content-area width (the maximum width occupied by children)
        float contentWidth() const { return contentW_; }
        /// @brief Arrangement result: content-area height (the maximum height occupied by children)
        float contentHeight() const { return contentH_; }

        // -------------------------------------------------------------------
        //  Measurement protocol: container self-reported size (aggregated children + own padding)
        // -------------------------------------------------------------------
        /**
         * @brief Container self-reported width (default = max of children's effective widths + left/right padding)
         *
         * @details
         * The container does not "want" a fixed width -- its natural width is stretched out by **the widest
         * child**. This is the container-level override of `Widget::hintWidth()` defaulting to `prefWidth()`.
         *
         * @note With 0 children, returns the sum of padding ("I must at least fit my padding").
         * @note The return value **already includes padding**; the parent container need not add it again --
         *       consistent with the hint semantics of leaf widgets.
         */
        float hintWidth() const override;

        /// @brief Container self-reported height (default = sum of children's effective heights + spacing + top/bottom padding)
        /// @note This is the algorithm when **the main axis is vertical**. `FlowLayout` overrides it with the "after wrapping by width" result.
        /// @see hintHeightFor
        float hintHeight() const override;

        /**
         * @brief The container's self-reported height under a **given available width**
         *
         * @details
         * Why this parameterized overload is needed: `FlowLayout`'s height **depends on how wide it is given**
         * -- different width -> different row count -> different height. The fixed signature `hintHeight()`
         * cannot answer this; the width must be known first (the row count of a flow layout varies with
         * width, a problem every GUI library must solve for itself).
         *
         * The default implementation ignores the parameter and forwards to the no-arg `hintHeight()` -- for
         * containers unaffected by width (VBox / HBox) this is exact; `FlowLayout` overrides it to do a real
         * wrap trial arrangement.
         *
         * @param availWidth The content width the parent container plans to give this container (**excluding this container's padding**)
         * @return The height this container naturally needs at this width (including top/bottom padding)
         */
        /// @note This protocol is declared by `Widget` (defaults to forwarding `hintHeight()`); the container branch is just
        ///       the kind of implementation that "really wraps"; see the notes on `Widget::hintHeightFor`.
        float hintHeightFor(float availWidth) const override;

        /**
         * @brief Whether this container's "height depends on width"
         * @return true = the parent container should fix the width first, then call `hintHeightFor(width)` to ask for the height
         *
         * @details
         * Criterion (unified for both directions): if any visible child's height depends on width, this container does too.
         *
         *  - **Vertical**: children stack downward; a wrapping child growing taller pushes the whole column longer;
         *  - **Horizontal**: this used to be hardcoded false ("children side by side, container height = tallest child"),
         *    which only holds when **nobody wraps** -- when a horizontal row embeds a child that wraps, narrowing
         *    the width makes it wrap into multiple rows and the whole row grows taller, so horizontal rows are
         *    also "height depends on width".
         *    The observed symptom of missing this: a list item built from `HBoxLayout[bullet + RichText]` --
         *    long entries **do not wrap and get clipped on the right**, with no error at all.
         *
         * @note Missing this produces a **silent wrong height**, and the symptom is subtle: the parent container will not ask
         *       `hintHeightFor(width)`, so it uses the natural height computed with "all children on **a single line**" --
         *       the container is shorter than its content, and the extra rows **overflow the container** (observed:
         *       the second line of text in a quote card pokes out past the card's bottom border and overlaps the title below).
         *       So **whenever "a container embeds a widget that wraps" (`RichText` / `FlowLayout` /
         *       a `CodeEditor` with auto height enabled), the container itself must acknowledge this rule**.
         * @note Lets the parent container (especially `ScrollView`) recognize this cross-dependency without guessing types.
         */
        bool heightDependsOnWidth() const override;

    protected:
        // -------------------------------------------------------------------
        //  Self-reported size of strip-style containers (shared by toolbar / status bar)
        // -------------------------------------------------------------------
        /**
         * @brief The **self-reported width** of a horizontal strip (Row direction): sum(effective width + both margins) + spacing + left/right padding
         * @param gap Spacing between children (normally pass `spacing()`; 0 = not included)
         * @return Natural total width (with no children, just the left/right padding)
         *
         * @details
         * `ToolBar::hintWidth` and `StatusBar::hintWidth` each used to have a **verbatim identical** loop.
         * The measurement convention for "a strip holding several widgets" must be consistent -- two widgets
         * side by side, one counting spacing and the other not, will visually be off by a few pixels.
         *
         * @note Only meaningful in **horizontal** semantics; for vertical containers use the `hintHeight()` family.
         * @see stripHintHeight
         */
        float stripHintWidth(float gap) const;

        /**
         * @brief The self-reported height of a strip: the **tallest** visible child (including top/bottom margins) + top/bottom padding
         * @return Natural total height (with no children, just the top/bottom padding -- widgets needing a fallback height check for 0 themselves)
         *
         * @note The cross axis takes the maximum rather than the sum: summing is the vertical-container algorithm; copying it for a strip
         *       would make the height grow linearly with the number of children ("added a button, the toolbar got taller").
         */
        float stripHintHeight() const;

        // -------------------------------------------------------------------
        //  Re-arrangement triggers
        // -------------------------------------------------------------------
        /**
         * @brief Overrides Widget's layout-input notification: when a child's layout properties change, marks this layout for re-arrangement
         *
         * @details
         * With this, after `panel.AddChild(btn)`, subsequent `btn.SetSize(...)` / `SetMargin(...)` /
         * `SetFlexGrow(...)` / `SetVisible(false)` all take effect immediately -- the caller does not need
         * to add its own `panel.Invalidate()`.
         *
         * @note **Geometry written back during arrangement does not trigger re-arrangement**: the arrangement algorithm itself calls
         *       children's `SetSize` to stretch/expand them (Stretch, flexGrow); that is "writing the result", not
         *       "an external change". Without filtering it out, every frame would mark itself dirty and never converge --
         *       the filter is the `laying_` gate.
         */
        void OnLayoutInputChanged() override;
        // -------------------------------------------------------------------
        //  Arrangement helpers (reused by subclass DoLayout)
        // -------------------------------------------------------------------
        // @brief Top-left corner of the content box (client-area absolute coordinates, padding already included)
        float contentLeft() const { return x() + padding_.left; }
        float contentTop() const { return y() + padding_.top; }

        /// @brief Available width and height of the content box (= own width/height - left/right/top/bottom padding - reserved space, always >= 0)
        float contentAvailableW() const;
        float contentAvailableH() const;

        /**
         * @brief Width **reserved at the right edge** of the content area (scrollbar space; always 0 for ordinary layouts)
         * @note Deducted by contentAvailableW(), so both arrangement and the child clip region avoid this strip --
         *       a widget's own Paint runs **before its children** (container below, children above),
         *       so without reserving, the scrollbar would be covered by opaque children.
         * @see contentAvailableW, ScrollView
         */
        virtual float reservedRight() const { return 0.0f; }
        /// @brief Height reserved at the bottom edge of the content area (horizontal scrollbar space; always 0 for ordinary layouts)
        virtual float reservedBottom() const { return 0.0f; }

        /// @brief List of visible child widgets (arrangement skips hidden items)
        std::vector<Widget*> visibleChildren() const;

        // -------------------------------------------------------------------
        //  Measurement protocol helpers: uniformly decide "how big this child wants to be"
        // -------------------------------------------------------------------
        /**
         * @brief The child's **effective width**: use pref if the user set it, otherwise ask hint
         * @param c Child widget (non-null)
         * @return `c->prefWidth() > 0 ? prefWidth() : c->hintWidth()`, clamped to non-negative
         *
         * @details
         * This is the **single value-fetch entry point** of the measurement protocol -- all arrangement
         * algorithms go through it and do not read `prefWidth()` directly. There is only one rule: **what the
         * user explicitly set always wins; only when unset do we ask the widget**.
         *
         * Benefit: the protocol can be **adopted widget by widget**. Widgets that do not override `hintWidth()`
         * default to returning `prefWidth()`, so "pref>0 uses pref, pref==0 uses hint(=pref=0)" is **bit-for-bit
         * identical** to today's behavior -- introducing the protocol changes no existing arrangement result.
         *
         * @note **Virtual function**: ordinary layouts use the base-class implementation directly; `ScrollView` overrides it to inject
         *       "the height re-measured once the width is known" (see `ScrollView::measuredHeight`).
         *       Why "compute at query time" rather than "write into the child": `layoutLinear` calls `RestorePref` on all
         *       children at the start, wiping out written values; only a query-style override survives.
         */
        virtual float effectiveWidth(const Widget* c) const {
            const float p = c->prefWidth();
            return p > 0.0f ? p : std::max(c->hintWidth(), 0.0f);
        }

        /// @brief The child's **effective height**: use pref if the user set it, otherwise ask hint
        /// @note Query order is `pref > measured value > hint` -- the "measured value" is obtained before vertical arrangement via
        ///       `hintHeightFor(available width)` (see `measureWidthDependentHeights`).
        ///       `ScrollView` overrides this function to do the same (it has its own cache).
        /// @see effectiveWidth, measureWidthDependentHeights
        virtual float effectiveHeight(const Widget* c) const {
            const float p = c->prefHeight();
            if (p > 0.0f) return p;

            auto it = measuredH_.find(c);
            if (it != measuredH_.end()) return std::max(it->second, 0.0f);

            return std::max(c->hintHeight(), 0.0f);
        }

        /// @brief Gets the child's "desired main-axis length" by main-axis direction (including both margins)
        /// @param vertical true = main axis is Y
        /// @note Reused by `layoutLinear` / `layoutFlow` to avoid writing the ternary expression twice.
        float effectiveMain(const Widget* c, bool vertical) const {
            const Margin& m = c->margin();
            return vertical ? (effectiveHeight(c) + m.top + m.bottom)
                            : (effectiveWidth(c) + m.left + m.right);
        }

        /**
         * @brief Measures children whose "height depends on width": asks for the height they really need under a given available width
         *
         * @details
         * When the axis is vertical (`VBoxLayout` / `ScrollView.Vertical`), the main-axis size should be reported by the child
         * itself. But `FlowLayout`'s height **depends on how wide it is given** -- different width, different row count,
         * different height. The no-arg `hintHeight()` cannot answer this (it can only answer "how tall is one row").
         *
         * So before vertical arrangement, do this pass first: give **this container's available width** to each
         * child whose `heightDependsOnWidth()` is true, get back the real height via `hintHeightFor(width)`,
         * store it in `measuredH_`, and have `effectiveHeight()` inject it at query time.
         *
         * Why "inject at query time" rather than "write into the child": `layoutLinear` calls `RestorePref` on each child
         * at the start, wiping the written actual size back to `effective` -- the measured row count would be lost.
         * Only a stored table + overridden query survives.
         *
         * Children that do not depend on width (ordinary Button / Label / HBox) are skipped automatically, and behavior is
         * exactly the same as before this protocol was introduced.
         *
         * @note Called by `layoutLinear(true)` after restoring sizes and before computing the total main-axis occupancy.
         * @see hintHeightFor, heightDependsOnWidth
         */
        void measureWidthDependentHeights();

        /// @brief Child -> height measured this round under the available width (only for vertical, "height depends on width", unset-height items)
        /// @see measureWidthDependentHeights
        mutable std::unordered_map<const Widget*, float> measuredH_;

        /// @brief Main-axis distribution: converts freeLen remaining space by justify into "first-item offset + extra inter-item spacing"
        /// @param[in]  freeLen  Main-axis remaining space (>=0)
        /// @param[in]  n        Number of children
        /// @param[out] lead     Offset of the first item's start
        /// @param[out] gap      Extra spacing between adjacent items (applied on top of spacing)
        void mainAxisDistribution(float freeLen, size_t n, float& lead, float& gap) const;

        /// @brief Cross-axis alignment: computes the child's position/size on the cross axis given crossLen available length
        /// @param[in]  align    The effective alignment (the caller first computes it with effectiveAlign:
        ///                      use the child's explicit alignSelf if set, otherwise the container's alignItems)
        /// @param[in]  crossLen Available length on the cross axis
        /// @param[in]  childCrossSize The child's own cross-axis size
        /// @param[out] pos     Cross-axis start coordinate (relative to the content box start)
        /// @param[out] size    Cross-axis size: when **not Stretch** = the child's own cross-axis size;
        ///                     **when Stretch this return value is unused** -- this function returns `crossLen`
        ///                     without margins, while the caller actually writes back
        ///                     `crossLen - both margins` and accounts by the child's real size
        void alignCrossAxis(AlignItems align, float crossLen, float childCrossSize,
                            float& pos, float& size) const;

        /// @brief The child's "effective alignment" on the cross axis: explicit alignSelf overrides the container's alignItems
        AlignItems effectiveAlign(const Widget* child) const {
            return child->hasAlignSelf() ? child->alignSelf() : style_.alignItems;
        }

        // -------------------------------------------------------------------
        //  Arrangement algorithms (shared by VBox/HBox/Flow and ScrollView to avoid multiple copies)
        // -------------------------------------------------------------------
        //  When placing, uniformly add the scroll offset scrollOffsetX_/scrollOffsetY_ (always 0 for ordinary layouts;
        //  only ScrollView sets a non-zero value via SetScrollOffset). "Linear stacking" and "wrapping flow"
        //  each have one implementation; scrolling is just "shifting the start point".
        // -------------------------------------------------------------------

        /// @brief Linear stacking (single main axis): shared by VBox(vertical=true) / HBox(vertical=false) /
        ///        ScrollView-Vertical / ScrollView-Horizontal
        void layoutLinear(bool vertical);
        /// @brief Horizontal wrapping flow (shared by Flow / ScrollView-Both)
        void layoutFlow();

        /// @brief Sets the scroll offset (only used by ScrollView; always 0 for ordinary layouts)
        void setScrollOffset(float sx, float sy);

        FlexStyle     style_;                 /// < Layout style (direction/alignment/spacing)
        LayoutPadding padding_;               /// < Padding

        // Arrangement result (written by DoLayout)
        float contentW_ = 0.0f;
        float contentH_ = 0.0f;

        // Scroll offset (added to child start positions when placing; always 0 for ordinary layouts, used by ScrollView)
        float scrollOffsetX_ = 0.0f;
        float scrollOffsetY_ = 0.0f;

        // Dirty flag
        bool  dirty_ = true;
        /// @brief "Currently executing DoLayout" gate (see OnLayoutInputChanged)
        /// @note The move constructor moves it along (members are moved by default). Only a **not-yet-mounted**
        ///       new widget can be moved (the AddChild case), and then it must be false, so this is safe.
        bool  laying_ = false;

        /// @brief "Currently propagating layout-input change upward" gate (see OnLayoutInputChanged)
        /// @note When a child changes, this container **continues notifying its own parent** (all the way to the root), so every ancestor
        ///       re-measures. This gate ensures the same input is processed only once per node along the chain.
        ///       Same as `laying_`: only a not-yet-mounted widget can be moved, and then it must be false.
        bool  propagating_ = false;

        /// @brief RAII guard for the gate: sets on construction, **restores to the value before entry** on destruction
        /// @note If you hand-write `laying_ = true; DoLayout(); laying_ = false;`, a mid-way return in
        ///       `DoLayout` (or a future exception) skips the reset -- afterwards `laying_`
        ///       stays true forever, and **all child property changes get swallowed** (symptom: "changed it, no effect").
        ///       Restoring to the value before entry (rather than hard-setting false) is also safe for nested calls.
        struct LayoutScope {
            bool&      flag;
            const bool prev;
            explicit LayoutScope(bool& f) : flag(f), prev(f) { flag = true; }
            ~LayoutScope() { flag = prev; }
            LayoutScope(const LayoutScope&) = delete;
            LayoutScope& operator=(const LayoutScope&) = delete;
        };

        /// @brief The same guard for upward propagation (see `propagating_`)
        using PropagatingGuard = LayoutScope;

        float cacheX_ = 0.0f, cacheY_ = 0.0f, cacheW_ = 0.0f, cacheH_ = 0.0f;
        size_t lastChildCount_ = 0;

        // Layout-input change callbacks (std::function is movable and is moved along with the implicit move constructor).
        // The two signatures are stored separately: the no-sender version and the sender version can be registered together (both are called during dispatch).
        LayoutChangedAction onLayoutChanged_;      /// < Without sender
        SelfAction          selfOnLayoutChanged_;  /// < With sender
    };

} // namespace Kit
