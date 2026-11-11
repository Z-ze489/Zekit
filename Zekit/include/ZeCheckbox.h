/**
 * @file ZeCheckbox.h
 * @brief Checkbox: a square indicator + one line of label, click to toggle the checked state
 *
 * @details
 * `Checkbox : TextWidget` -- it **carries its own fixed label** (the checkbox text is part of the widget,
 * not a separate Label placed beside it), so it inherits the text layer and gets TextStyle / font / alignment / six-state text color for free.
 *
 * This class only adds three things:
 *  1. **Checked state**: `SetChecked(bool)` / `checked()` / `Toggle()` -- a **persistent state**
 *     independent of "pressed / hovered" (a click does not spring back; this is the fundamental difference from Button);
 *  2. **Tri-state (optional)**: after `SetTriState(true)`, `Indeterminate` is allowed (half-checked, draws a horizontal bar),
 *     used for a parent checkbox meaning "some children are selected"; clicking it cycles through unchecked -> checked -> indeterminate -> unchecked;
 *  3. **Toggle callback**: `SetOnToggled` / `SetOnToggled(SelfAction)`, **triggered only on the user path**
 *     (click / keyboard Space); programmatic `SetChecked` does not trigger it -- same convention as Slider, to prevent loops.
 *
 * Layout: the indicator is fixed at the left of the content area (square, side = natural line height), the label text is placed to its right.
 *
 * @par Basic usage
 * @code
 * Kit::Checkbox cb("Remember password");
 * cb.SetChecked(true);
 * cb.SetGeometry(24, 24, 160, 24);
 * cb.SetOnToggled([](Kit::Widget& self, bool on) {
 *     std::printf("Remember password = %d\n", on);
 * });
 * win.AddWidget(cb);                      // value is taken over; use the returned reference to keep modifying
 * @endcode
 *
 * @par Size
 * Overrides `hintWidth` / `hintHeight`; natural size = **indicator side + spacing + label text width**
 * x **one line of text height** (plus padding and border). Without an explicit `SetSize` it expands naturally to the text;
 * if explicitly set, the explicit value wins (measurement protocol: what the user sets always wins).
 *
 * > WARNING: **Why hint must be overridden**: the `Widget` base class hint returns 0 by default. Without the override,
 * > after being placed in a `VBoxLayout`, `VBox::hintHeight()` = sum of child hintHeight = **0**
 * > -> the parent container height collapses to 0 -> the widget **is completely invisible with zero errors**.
 * > Any custom leaf widget must override these two functions before being placed in `VBox`/`HBox`/`Grid`/`Flow`.
 *
 * @note Checked is **data**, not style -- changing the theme does not reset it; the theme only injects the colors of the indicator / tick /
 *       text (`Theme::checkbox` section).
 *
 * @see ZeRadioButton.h Radio button (same kind of interaction, but mutually exclusive single choice)
 * @see ZeSlider.h      The first interactive widget (drag + keyboard); the checkbox interaction is simpler
 */

#pragma once
#include "ZeTextWidget.h"
#include "ZeSelfAction.h"
#include "ZeIndicator.h"

#include <functional>
#include <string>

namespace Kit {

    /**
     * @brief Display state of the checkbox (tri-state)
     *
     * @details
     * By default the widget only uses the two states `Unchecked` / `Checked`; after enabling `SetTriState(true)`,
     * `Indeterminate` (half-checked) participates in the cycle. The indeterminate state draws a horizontal bar instead of a tick --
     * this is the common visual for "partially selected".
     */
    enum class CheckState {
        Unchecked,      /// < Unchecked (empty box)
        Checked,        /// < Checked (tick drawn inside the box)
        Indeterminate   /// < Indeterminate (horizontal bar drawn inside the box; reachable only in tri-state mode)
    };

    /**
     * @brief Checkbox: square indicator + label text
     *
     * @details
     * Inherits `TextWidget`: the checkbox label is its own text (`Text()` / `SetText()`),
     * not an externally attached Label. So font size / color / alignment all go through the existing text-layer interfaces.
     *
     * Painting only overrides `paintContent()`: first lay out the text as "indicator + spacing + text",
     * then draw the indicator box (background + border + rounded corners), and overlay a tick (two thick lines) when checked.
     */
    class Checkbox : public TextWidget {
    public:
        /// @brief Constructs a checkbox
        /// @param text  Label text
        /// @param checked Whether initially checked (default no)
        /// @note At construction the checkbox section of the built-in light theme is used as default (cannot be done in the base class constructor:
        ///       `themeBox` / `themeText` are virtual functions, and a virtual call during base class construction only hits the base version).
        explicit Checkbox(std::string text = std::string(), bool checked = false);

        // -------------------------------------------------------------------
        //  Checked state
        // -------------------------------------------------------------------
        //  * Fundamental difference from Button: Button's "pressed" is **transient** (springs back on release),
        //    the checkbox's "checked" is **persistent** (click toggles, stays). So it does not override
        //    BoxWidget's pressed_ semantics to express selection, but has a separate field checked_.
        // -------------------------------------------------------------------

        /// @brief Sets the checked state (**program path**, does not trigger callbacks)
        /// @param checked true = checked; false = unchecked
        /// @note In tri-state mode, calling this **exits the indeterminate state** (indeterminate is an intermediate state of user interaction;
        ///       a programmatic explicit set should land on a definite binary choice).
        void SetChecked(bool checked);

        /// @brief Whether currently "checked" (returns false in indeterminate state -- use state() to distinguish)
        bool checked() const { return state_ == CheckState::Checked; }

        /// @brief Tri-state display state (unchecked / checked / indeterminate)
        CheckState state() const { return state_; }

        /// @brief Sets the tri-state directly (program path, does not trigger callbacks)
        /// @param s Target state; when tri-state is off, `Indeterminate` is downgraded to `Unchecked`
        void SetState(CheckState s);

        /// @brief Toggles the checked state (program path, does not trigger callbacks)
        /// @note Two states: unchecked <-> checked; tri-state: unchecked -> checked -> indeterminate -> unchecked.
        void Toggle();

        // -------------------------------------------------------------------
        //  Tri-state
        // -------------------------------------------------------------------

        /// @brief Enables/disables tri-state mode (default off)
        /// @param on When enabled, `Indeterminate` participates in the cycle and painting
        /// @note When disabled, the indeterminate state is immediately downgraded to unchecked (no "state that only exists in tri-state" is left).
        void SetTriState(bool on);

        /// @brief Whether currently in tri-state mode
        bool triState() const { return triState_; }

        // -------------------------------------------------------------------
        //  Toggle callback (outward notification -- triggered only on the user path)
        // -------------------------------------------------------------------
        //  Same SelfAction protocol as Button's SetOnClick (defined in ZeSelfAction.h).
        //  Carries one payload: the checked state after toggling (bool). false when turned off.
        //
        //  * The callback is **triggered only on the user path** -- mouse click, keyboard Space. Programmatic SetChecked does not trigger it.
        //    This is the same convention as Slider's SetOnValueChanged: the callback means "the user changed it",
        //    not "the value changed"; otherwise a parent's SetChecked would trigger its own callback -> loops / repeated refreshes.
        // -------------------------------------------------------------------

        /// @brief Toggle callback type (with sender + new checked state)
        using ToggledAction = std::function<void(Widget&, bool)>;

        /// @brief Sets the "toggle" callback (triggered after user click / keyboard Space)
        /// @param handler Callback; `on` is the checked state after toggling; pass empty to clear
        /// @note The indeterminate state appears in the callback as `on == false` (indeterminate is not "checked").
        ///       To distinguish indeterminate, use `SetOnStateChanged`.
        void SetOnToggled(ToggledAction handler);

        /// @brief Sets the "toggle" callback (form that only cares about the sender, not the checked state)
        /// @param handler Callback; `self` is the checkbox itself; pass empty to clear
        /// @note Can be registered together with the payload version; on dispatch the **payload version fires first, then the sender version**.
        void SetOnToggled(SelfAction handler);

        /// @brief Sets the "state changed" callback (with sender + full tri-state)
        /// @param handler Callback; `s` is the full state after toggling (can distinguish indeterminate); pass empty to clear
        void SetOnStateChanged(std::function<void(Widget&, CheckState)> handler);

        // -------------------------------------------------------------------
        //  Indicator appearance
        // -------------------------------------------------------------------

        /// @brief Indicator side length (pixels); 0 = automatic (matches one line of text height)
        void SetIndicatorSize(float size);
        /// @brief Current indicator side length (0 = automatic)
        float indicatorSize() const { return indicatorSize_; }

        /// @brief **Shape** of the indicator (square / large rounded / circle / diamond)
        /// @note Default `Square` -- pixel-identical to the appearance before this interface was added.
        ///       The shape only changes the "container"; what is drawn inside is decided by `SetMarkStyle`, and the two combine freely.
        /// @see ZeIndicator.h Appearance description of the four values
        void SetIndicatorShape(IndicatorShape shape);
        /// @brief Current indicator shape
        IndicatorShape indicatorShape() const { return shape_; }

        /// @brief **Mark** drawn inside the container when checked (tick / cross / plus / dot / bar / solid / star)
        /// @note Default `Tick`. The indeterminate state always draws a bar and does not follow this setting (otherwise it would be indistinguishable from "checked").
        ///       The mark color is taken automatically from the container (indicator background color on a solid container, accent color on a hollow container),
        ///       so a separate mark color is **not needed** and should not be set -- `SetMarkColor` controls the accent color itself.
        void SetMarkStyle(CheckMark mark);
        /// @brief Current check mark
        CheckMark markStyle() const { return mark_; }

        /// @brief Spacing between indicator and label text (pixels, default 8)
        void SetSpacing(float gap);
        /// @brief Current spacing between indicator and text
        float spacing() const { return spacing_; }

        /// @brief Background color of the indicator (unchecked / checked / indeterminate)
        void SetIndicatorColor(const Color4f& c);
        /// @brief Indicator border color
        void SetIndicatorBorderColor(const Color4f& c);
        /// @brief Color of the tick / bar
        void SetMarkColor(const Color4f& c);

        /// @brief Current indicator background color
        const Color4f& indicatorColor() const { return indicatorColor_; }
        /// @brief Current indicator border color
        const Color4f& indicatorBorderColor() const { return indicatorBorderColor_; }
        /// @brief Current tick / bar color
        const Color4f& markColor() const { return markColor_; }

        // -------------------------------------------------------------------
        //  Self-reported natural size (measurement protocol) -- required for VBox / FlowLayout
        // -------------------------------------------------------------------
        //  Rule consistent with the whole library: **if the user explicitly set it, use the user value**; only compute here when not set.
        //  Natural size = indicator side + spacing + label text width  x  one line of text height (+ padding + border).
        // -------------------------------------------------------------------

        /// @brief Natural width = indicator side + spacing + label text width + padding + border
        float hintWidth() const override;

        /// @brief Natural height = max(indicator side, one line of text height) + padding + border
        float hintHeight() const override;

        // -------------------------------------------------------------------
        //  Theme injection
        // -------------------------------------------------------------------

        /// @brief Re-injects style from the theme (**overwrites** colors manually changed by the user)
        /// @note Overrides `TextWidget::ApplyTheme`: text and box are injected as usual, and additionally the indicator / tick colors
        ///       are taken from the `checkbox` section.
        void ApplyTheme(const Theme& t) override;

        // -------------------------------------------------------------------
        //  Interaction events
        // -------------------------------------------------------------------
        //  Click detection: press established -> not moved out -> release. Same as Button (moving out cancels,
        //  BoxWidget::OnMouseLeave is responsible for clearing pressed_).
        //  Keyboard: in focus state, Space / Enter also toggle (shares the same exit as "clicked once").
        // -------------------------------------------------------------------

        bool OnMousePress(MouseEvent& e) override;
        bool OnMouseRelease(MouseEvent& e) override;
        bool OnKeyPress(KeyEvent& e) override;

        /// @brief Activates the checkbox -- equivalent to "clicked once": toggles state + triggers callbacks
        /// @note This is the **only toggle exit**: mouse click and keyboard Space both call it (share one path).
        ///       In disabled state it returns immediately, without toggling or triggering any callback.
        virtual void Activate();

    protected:
        /// @brief Default box style taken from the theme's checkbox section
        const BoxStyle& themeBox(const Theme& t) const override;
        /// @brief Default text style taken from the theme's checkbox section
        const TextStyle& themeText(const Theme& t) const override;

        /// @brief Paints: indicator box (background + border + rounded corners) + tick/bar + label text
        /// @note The content is clipped uniformly to the padding box by `BoxWidget::Paint`, so no clipping is needed here.
        void paintContent(FrameCollector& fc) const override;

        /// @brief The checked state also participates in "which state's color to take" -- when unchecked the indicator uses the normal color
        /// @note This widget does not change interaction state semantics (disabled > pressed > focus > hover > normal);
        ///       the checked state only affects what is drawn **inside the indicator**, so currentState() is not overridden.
        ///       (This note is kept to state: it is **deliberately not** overridden here.)

    private:
        /// @brief Computes the indicator side length (explicit value first, otherwise matches one line of text height, fallback 18)
        float resolvedIndicatorSize() const;

        /// @brief Advances the state cycle by one step and triggers callbacks (common exit of the user path)
        /// @return Whether a change actually occurred (false if disabled / unchanged, no notification sent)
        bool advanceState();

        /// @brief Triggers all registered callbacks
        void notify();

        CheckState  state_ = CheckState::Unchecked;  /// < Tri-state display state
        bool        triState_ = false;               /// < Whether in tri-state mode

        float       indicatorSize_ = 0.0f;           /// < Indicator side length (0 = automatic)
        float       spacing_ = 8.0f;                 /// < Spacing between indicator and text

        IndicatorShape shape_ = IndicatorShape::Square;  /// < Container shape (default square)
        CheckMark      mark_ = CheckMark::Tick;          /// < Check mark (default tick)

        Color4f     indicatorColor_{ 1.0f, 1.0f, 1.0f, 1.0f };   /// < Indicator background color
        Color4f     indicatorBorderColor_{ 0.0f, 0.0f, 0.0f, 1.0f }; /// < Indicator border color
        Color4f     markColor_{ 1.0f, 1.0f, 1.0f, 1.0f };        /// < Tick / bar color

        // Callbacks (std::function is movable and is moved along by AddWidget with this class's implicit move constructor).
        ToggledAction                    onToggled_;       /// < Toggle callback (with bool)
        SelfAction                       selfOnToggled_;   /// < Toggle callback (with self only)
        std::function<void(Widget&, CheckState)> onStateChanged_;  /// < State changed callback (with tri-state)
    };

} // namespace Kit
