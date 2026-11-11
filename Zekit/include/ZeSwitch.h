/**
 * @file ZeSwitch.h
 * @brief Switch: a pill-shaped track plus a sliding thumb (optionally animated)
 *
 * @details
 * `Switch : BoxWidget` -- a click-to-toggle widget, same family as `Checkbox` (press inside -> do not
 * leave -> toggle on release), but with two extra things:
 *
 *  1. The thumb movement is animated (ease-out cubic, on by default) -- the switch's "on / off" is a
 *     spatial displacement, and a hard cut looks jarring. The animation contract follows the
 *     `ProgressBar` scheme (`wantsAnimationFrame()` + a starting `RequestRedraw()` + per-frame
 *     `RequestFrameWake`);
 *  2. An optional line of label text on the right (`SetText`). Is the label "part of the widget" or
 *     "an external caption"? The former is chosen here -- because a switch is often used together
 *     with a phrase like "enable notifications", and splitting them into two widgets to align
 *     baselines is more annoying (`Checkbox`'s label follows the same judgment).
 *
 * @par Interaction
 * | Operation | Behavior |
 * |---|---|
 * | Left click on track | Toggle (press inside -> do not leave -> toggle on release) |
 * | `Space` / `Enter` | Toggle (when focused) |
 *
 * @par WARNING: the class name is `Switch`, but the theme field is called `toggleSwitch`
 * `switch` is a C++ keyword -- the class name is fine (different case), but a **member name cannot
 * be used**, so that section in `Theme` is called `toggleSwitch`.
 *
 * @code
 * Kit::Switch s(true);
 * s.SetText("Enable notifications");
 * s.SetOnToggled([](Kit::Widget& self, bool on) {
 *     auto& sw = Kit::As<Kit::Switch>(self);
 *     std::printf("%s -> %s\n", sw.Text().c_str(), on ? "on" : "off");
 * });
 * @endcode
 *
 * @see ZeCheckbox.h also click-to-toggle, but without displacement animation
 * @see ZeTheme.h    `SwitchStyle`
 */

#pragma once
#include "ZeBoxWidget.h"
#include "ZeSelfAction.h"
#include "ZeTextStyle.h"

#include <string>

namespace Kit {

    /**
     * @brief Switch widget
     */
    class Switch : public BoxWidget {
    public:
        /// @brief Constructs
        /// @param on Initial state (off by default)
        explicit Switch(bool on = false);

        // -------------------------------------------------------------------
        //  State (persistent data, not a transient state like pressed_)
        // -------------------------------------------------------------------

        /// @brief Sets the switch state (**programmatic path: does not fire callbacks**; animates smoothly if animation is on)
        void SetChecked(bool on);
        bool isChecked() const { return checked_; }

        /// @brief Toggles (**programmatic path: does not fire callbacks**)
        void Toggle() { SetChecked(!checked_); }

        /// @brief Override: clears the pressed state and hover preview when disabled
        void SetEnabled(bool enabled) override;

        // -------------------------------------------------------------------
        //  Label text
        // -------------------------------------------------------------------

        /// @brief Right-side label (empty string = takes no space)
        void SetText(std::string text);
        const std::string& Text() const { return text_; }
        bool hasText() const { return !text_.empty(); }

        /// @brief Label text style (color / font size can be changed)
        TextStyle& textStyle() { return textStyle_; }
        const TextStyle& textStyle() const { return textStyle_; }

        // -------------------------------------------------------------------
        //  Appearance (value interface)
        // -------------------------------------------------------------------

        /// @brief Track size (pixels); a side <= 0 follows the theme
        void SetTrackSize(float w, float h);
        float trackWidth() const { return trackW_; }
        float trackHeight() const { return trackH_; }

        /// @brief Sets the three key colors at once (on / off / thumb)
        void SetColors(const Color4f& onColor, const Color4f& offColor, const Color4f& knobColor);

        // -------------------------------------------------------------------
        //  Animation
        // -------------------------------------------------------------------
        //  On by default: the switch thumb displacement is a "spatial change", and a hard cut is jarring.
        //  Programmatic SetChecked also animates; to land directly at initialization, use FinishAnimation().

        void SetAnimated(bool on);
        bool animated() const { return animated_; }

        /// @brief Immediately converges to the target position (skips animation; use after setting an initial value to land directly)
        void FinishAnimation();

        /// @brief Whether animation is in progress (also the basis for `wantsAnimationFrame`)
        bool animating() const { return animating_; }

        /// @brief Current thumb position (0 = off, 1 = on) -- an intermediate value during animation
        /// @note Same semantics as `ProgressBar::value()`: to query state programmatically use `isChecked()`
        ///       (this is the **display value**).
        float knobProgress() const { return knob_; }

        /// @brief Animation duration (milliseconds)
        void SetAnimationDuration(int ms);
        int animationDuration() const { return animMs_; }

        /// @brief Overrides `Widget::wantsAnimationFrame()` -- under OnDemand driving, this keeps frames coming
        bool wantsAnimationFrame() const override { return animating_; }

        // -------------------------------------------------------------------
        //  Callbacks (fired only on the user path)
        // -------------------------------------------------------------------

        /// @brief "Toggled" callback (with the new state)
        void SetOnToggled(SelfAction1<bool> handler);
        /// @brief "Toggled" callback (sender only)
        void SetOnToggled(SelfAction handler);

        // -------------------------------------------------------------------
        //  Measurement protocol
        // -------------------------------------------------------------------
        float hintWidth() const override;
        float hintHeight() const override;

        // -------------------------------------------------------------------
        //  Theme
        // -------------------------------------------------------------------
        void ApplyTheme(const Theme& t) override;

        // -------------------------------------------------------------------
        //  Interaction events
        // -------------------------------------------------------------------
        bool OnMousePress(MouseEvent& e) override;
        bool OnMouseRelease(MouseEvent& e) override;
        bool OnKeyPress(KeyEvent& e) override;

    protected:
        /// @brief Track + thumb + label
        void paintContent(FrameCollector& fc) const override;

    private:
        /// @brief The single exit of the user path: change state + start animation + fire callbacks
        void toggleByUser();
        /// @brief Starts one "slide to the current target" animation (internal)
        void startAnim();
        /// @brief Advances the animation (called during paint; on finish lands exactly and stops requesting frames)
        void stepAnimation() const;
        /// @brief Fires the callbacks
        void notify(bool on);
        /// @brief Requests a repaint
        void requestRepaint();

        /// @brief Currently effective track width / height
        float resolvedTrackW() const;
        float resolvedTrackH() const;

        bool  checked_ = false;
        std::string text_;

        bool  animated_ = true;
        int   animMs_ = 160;

        // Animation state: rewritten during animation by stepAnimation (const, during paint) -> mutable
        mutable float knob_ = 0.0f;          /// < Display position (0..1)
        mutable bool  animating_ = false;
        mutable float animFrom_ = 0.0f;
        mutable long long animStartMs_ = 0;

        float trackW_ = 0.0f;                /// < Explicit track width (0 = follow theme)
        float trackH_ = 0.0f;                /// < Explicit track height (0 = follow theme)

        bool    colorsSet_ = false;          /// < User explicitly set colors -> no longer overridden by the theme

        // Theme injection
        float   themeTrackW_ = 44.0f;
        float   themeTrackH_ = 24.0f;
        float   knobInset_ = 2.0f;
        float   textGap_ = 8.0f;
        Color4f onColor_{ 0.0f, 0.0f, 0.0f, 1.0f };
        Color4f offColor_{ 0.0f, 0.0f, 0.0f, 1.0f };
        Color4f onDisabledColor_{ 0.0f, 0.0f, 0.0f, 1.0f };
        Color4f offDisabledColor_{ 0.0f, 0.0f, 0.0f, 1.0f };
        Color4f knobColor_{ 1.0f, 1.0f, 1.0f, 1.0f };
        Color4f knobDisabledColor_{ 1.0f, 1.0f, 1.0f, 1.0f };
        TextStyle textStyle_;

        SelfAction1<bool> onToggled_;
        SelfAction        onToggledSelf_;
    };

} // namespace Kit
