/**
 * @file ZeNumberBox.h
 * @brief Number input box: an input box plus a pair of step buttons on the right, with range clamping and precision
 *
 * @details
 * `NumberBox : InputBox` -- not a single line of the editing core is rewritten. Typing / backspace / selection / drag-select /
 * `Ctrl+A/C/X/V` / caret blinking / IME input / click-to-caret are all inherited from `TextEditBase`;
 * this class adds only the three things that are specific to "numbers":
 *
 *  1. value <-> text: `SetValue` / `Value()` convert between float and string,
 *     and fix the precision according to `decimals`;
 *  2. range and stepping: `SetRange` / `SetStep`; the pair of `▲▼` buttons on the right and the `↑` / `↓` keys
 *     all go through the same `StepBy()` convergence point (one implementation, three triggers);
 *  3. input filtering: only digits / decimal point / sign are accepted -- letters and CJK never enter the box
 *     (otherwise `Value()` would silently become 0 and the user could not see why).
 *
 * @par Rules for the three numbers
 * | Rule | Description |
 * |---|---|
 * | Clamp on "commit" | Out-of-range values are allowed temporarily while typing (otherwise a half-typed `-` would be clamped to `0` and negative numbers could not be entered); clamping happens only in `SetValue` / stepping / Enter / focus loss. |
 * | Callback only on user paths | `SetValue` (programmatic) does not trigger `SetOnValueChanged`; only keys, step buttons and focus-loss commit trigger it -- otherwise a parent setting the value would fire the callback the parent itself set (loop). |
 * | Callback deduplication | Nothing is emitted if the value did not change (repeatedly pressing a step button at the boundary must not spam the callback). |
 *
 * @par Usage
 * @code
 * Kit::NumberBox qty(1.0f);
 * qty.SetRange(1.0f, 99.0f);
 * qty.SetStep(1.0f);
 * qty.SetDecimals(0);                          // integers only
 * qty.SetOnValueChanged([](Kit::Widget& self, float v) {
 *     std::printf("quantity = %g\n", v);
 * });
 * win.AddWidget(qty);
 * @endcode
 *
 * @note Unit text (`kg` / `%` / `px`) is given via `SetSuffix`; it affects display only
 *       and does not participate in parsing -- so a string like "1.5kg" cannot break parsing.
 *
 * @see ZeInputBox.h     base class (single-line editing)
 * @see ZeTextEditBase.h editing core
 * @see ZeSlider.h       also a "value + range + user callback" widget, with the same callback convention
 */

#pragma once
#include "ZeInputBox.h"
#include "ZeSelfAction.h"

#include <string>

namespace Kit {

    /**
     * @brief Number input box
     *
     * @details
     * Inheritance chain: `Widget -> BoxWidget -> TextWidget -> TextEditBase -> InputBox -> NumberBox`.
     * The theme uses the `numberBox` section (its `box` / `text` themselves derive from the `input` section).
     */
    class NumberBox : public InputBox {
    public:
        /// @brief Constructs a number input box
        /// @param value Initial value (clamped by the default range of `SetRange`, `[-1e9, 1e9]`)
        explicit NumberBox(float value = 0.0f);

        // -------------------------------------------------------------------
        //  Value
        // -------------------------------------------------------------------

        /// @brief Sets the numeric value (programmatic path: does not trigger the callback)
        /// @note Clamps to the range, formats by precision, and syncs the text and caret in the box.
        void SetValue(float value);

        /// @brief Current numeric value (parsed from the text in the box; returns the last valid value if parsing fails)
        float Value() const { return value_; }

        /// @brief Sets the text in the box directly (shadows `TextEditBase::SetText`, which is not virtual)
        /// @note The semantics are "use the text as a number": on successful parse the value is updated (and clamped); on failure the old value is kept.
        ///       To display arbitrary text use `InputBox::SetText` -- but then `Value()`
        ///       no longer follows the text in the box.
        void SetText(const std::string& text);

        // -------------------------------------------------------------------
        //  Range / step / precision
        // -------------------------------------------------------------------

        /// @brief Sets the value range; swaps automatically when `min > max`
        /// @note Immediately clamps the current value into the range (and syncs the text).
        void SetRange(float minValue, float maxValue);

        /// @brief Minimum value (default `-1e9f`)
        float Min() const { return min_; }
        /// @brief Maximum value (default `1e9f`)
        float Max() const { return max_; }

        /// @brief Sets minimum / maximum / step at once
        void SetRange(float minValue, float maxValue, float step);

        /// @brief Step amount (how much the `▲▼` buttons and the `↑` / `↓` keys add or subtract each time, default 1)
        /// @note A value <= 0 is ignored (a step button with step 0 is meaningless and would get stuck).
        void SetStep(float step);
        /// @brief Current step amount
        float Step() const { return step_; }

        /// @brief Number of decimal places (0 = integer; default 2)
        /// @param digits Decimal places; values above 6 are treated as 6 (the practical limit of `%f`)
        void SetDecimals(int digits);
        /// @brief Current number of decimal places
        int Decimals() const { return decimals_; }

        /// @brief Unit suffix (affects display only, e.g. `" kg"` / `"%"`; does not participate in parsing)
        void SetSuffix(std::string suffix);
        /// @brief Current suffix
        const std::string& Suffix() const { return suffix_; }

        // -------------------------------------------------------------------
        //  Stepping (one implementation, three triggers: buttons / up-down keys / program)
        // -------------------------------------------------------------------

        /// @brief Steps by `step_` for `steps` times (`steps` may be negative)
        /// @return true = the value changed
        /// @note Does not trigger the callback -- it is "changing the value", not a "user action".
        ///       User paths (buttons / keys) call `commitUserEdit()` themselves after calling it.
        bool StepBy(int steps);

        // -------------------------------------------------------------------
        //  Callback (triggered only on user paths)
        // -------------------------------------------------------------------

        /// @brief Sets the value-changed callback (with sender + new value)
        /// @note Triggered only on user paths (typing / step buttons / up-down keys / focus-loss commit);
        ///       programmatic `SetValue` does not emit it (same rule as `Slider` / `ComboBox`).
        void SetOnValueChanged(SelfAction1<float> cb) { onValueChanged_ = std::move(cb); }
        /// @brief Sets the value-changed callback (form that only cares about the sender)
        void SetOnValueChanged(SelfAction cb) { selfOnValueChanged_ = std::move(cb); }

        // -------------------------------------------------------------------
        //  Measurement protocol (one step-button width more than the base class)
        // -------------------------------------------------------------------
        float hintWidth() const override;

        // -------------------------------------------------------------------
        //  Theme
        // -------------------------------------------------------------------
        // @brief Re-injects from the theme `numberBox` section (box / text / step button appearance)
        void ApplyTheme(const Theme& t) override;

        // -------------------------------------------------------------------
        //  Paint / events
        // -------------------------------------------------------------------
        // @brief Base class content (number text) -> then paints the step buttons on the right
        void paintContent(FrameCollector& fc) const override;

        /// @brief Starts a step if the point lands on a step button (holding it repeats, see implementation notes)
        bool OnMousePress(MouseEvent& e) override;
        /// @brief Maintains the button hover highlight
        bool OnMouseMove(MouseEvent& e) override;
        /// @brief Clears hover on leave
        void OnMouseLeave() override;

        /// @brief The step button area is a "clickable button" (arrows); the rest is the edit area (I-beam)
        /// @details
        /// The base class `TextEditBase` calls `SetCursor(Cursor::Text)` in its constructor, which declares the cursor for the whole widget;
        /// the button area is only a local strip on the right, and a static declaration cannot express "only when pointing here"
        /// -- same as the `Splitter` divider, it answers by coordinates via `cursorAt(x, y)`.
        /// The disabled state needs no handling: `Window` always falls back to the arrow for disabled widgets.
        Cursor cursorAt(float x, float y) const override;

        /// @brief `↑` / `↓` step; `Enter` commits (clamp + callback); other keys go back to the base class
        bool OnKeyPress(KeyEvent& e) override;
        /// @brief Accepts only digits / decimal point / sign; other characters are swallowed and never enter the box
        bool OnCharInput(KeyEvent& e) override;

        /// @brief On focus loss, parses, clamps and formats the current text ("auto-correct after input")
        void OnFocusOut() override;

    private:
        // -------------------------------------------------------------------
        //  Internal
        // -------------------------------------------------------------------

        /// @brief Formats `value_` into the text in the box (does not trigger the `SetText` parse path)
        void refreshTextFromValue();

        /**
         * @brief Writes the text only + moves the caret to the end, emits no callback and does not touch parsing
         *
         * @details
         * Why it is needed: `TextEditBase::SetText` goes through the full editing core
         * (`cancelVerticalPending` / `resetBlink` / `requestCaretVisible` / `changed()`),
         * and `changed()` calls back out. Calling it in the constructor = emitting the
         * callback to the outside before the object is fully constructed -- in practice this makes virtual calls / member accesses
         * during derived-class construction hit uninitialized state (access violation). The constructor body of this class therefore takes this "quiet" path.
         *
         * @note Semantically it is a "programmatic set" (no notification), so it need not emit `onTextChanged_` either.
         */
        void setTextQuietly(const std::string& text);

        /// @brief Parses the value from the current text; on success writes `value_` (clamped), on failure returns false
        bool parseTextIntoValue();

        /// @brief Unified exit for user paths: parse -> clamp -> callback + repaint if changed
        /// @param reformat true = also reformat the text (do not reformat while typing,
        ///        otherwise the caret is pushed to the end and a just-typed decimal point is eaten)
        void commitUserEdit(bool reformat);

        /// @brief Fires the registered callbacks (payload version first, then sender version)
        void notifyValueChanged();

        /// @brief Requests a repaint (silently skipped when there is no window)
        void requestRepaint();

        /// @brief Formats `v` into text using the current precision and suffix
        /// @note Extracted separately because it has three callers (constructor / `SetValue` / `SetDecimals`),
        ///       and the formatting details (the `snprintf` format string) should not be scattered in three copies.
        std::string FormatValue(float v) const;

        /// @brief Geometry of the step button area on the right (top-left corner + width of one side), shared by paint and hit test
        /// @return false = the widget is too narrow to fit the buttons (then neither painted nor hit-tested)
        bool stepButtonsRect(float& bx, float& by, float& bw, float& bh) const;

        /// @brief Width of one button (theme-adjustable)
        float buttonWidth() const { return buttonWidth_; }

        // ---- Value domain ----
        float value_ = 0.0f;
        float min_ = -1.0e9f;
        float max_ = 1.0e9f;
        float step_ = 1.0f;
        int   decimals_ = 2;
        std::string suffix_;

        // ---- Appearance ----
        float    buttonWidth_ = 22.0f;
        BoxStyle stepBox_;                  /// < step button box (background / border)
        Color4f  stepColor_{ 0.0f, 0.0f, 0.0f, 0.0f };
        Color4f  stepHoverColor_{ 0.0f, 0.0f, 0.0f, 0.0f };
        Color4f  stepPressedColor_{ 0.0f, 0.0f, 0.0f, 0.0f };

        // ---- Interaction transients (do not change data, no markStyled needed) ----
        int hoverButton_ = 0;      /// < -1 = step down, 0 = none, +1 = step up
        int pressedButton_ = 0;

        // ---- Callbacks ----
        SelfAction1<float> onValueChanged_;
        SelfAction         selfOnValueChanged_;
    };

} // namespace Kit
