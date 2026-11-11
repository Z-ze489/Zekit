/**
 * @file ZeProgressBar.h
 * @brief Progress bar: expresses "how much is done" as a filled track
 *
 * @details
 * `ProgressBar : BoxWidget` -- it paints only one rectangle, so it inherits only the box layer.
 *
 * It has only four things of its own:
 *  1. Value: `SetValue(v)` places the current progress within `[min, max]` (default `0 ~ 100`),
 *     fill ratio = `(value - min) / (max - min)`, clamped automatically, never paints outside the track;
 *  2. Orientation: `SetOrientation(Horizontal / Vertical)` -- a vertical progress bar
 *     fills from the bottom upward (`Vertical`);
 *  3. Text: optionally overlays a percentage / custom text on the bar (off by default);
 *  4. Drag bar (optional, off by default): after `SetDraggable(true)` it becomes a draggable
 *     "drag bar" -- pressing / dragging the mouse on the bar changes the value directly, with an optional round thumb
 *     (thumb), and a callback `SetOnValueChanged` when the value changes.
 *
 *  * By default it is read-only (identical word for word to before dragging was introduced): without
 *    `SetDraggable` it consumes no mouse events at all, it only "shows completion". To interact, turn it on
 *    explicitly -- this way "the progress bar moves by itself, the user cannot touch it" and "the user can
 *    drag" are two clearly distinct things.
 *
 * The track and the fill are two rounded rectangles: the track fills the whole content area (background
 * color), the fill covers it proportionally (foreground color). Both corner radii are clamped to half
 * their own short side -- so a "capsule progress bar" (height 12, radius 6) still has a round cap at
 * very small values, it does not become a sharp corner.
 *
 * @par Basic usage
 * @code
 * Kit::ProgressBar bar;
 * bar.SetRange(0.0f, 100.0f);
 * bar.SetValue(42.0f);
 * bar.SetGeometry(24, 24, 320, 12);
 * win.AddWidget(bar);                    // value is taken over, keep changing it via the returned reference
 *
 * // to show percentage text (you must give the bar height manually so the text fits)
 * bar.SetSize(320, 24);
 * bar.SetShowText(true);
 *
 * // to get a "silky loading process" (smooth transition on value change instead of an instant jump)
 * bar.SetAnimated(true);                 // every subsequent SetValue eases over
 * bar.SetValue(80.0f);                   // animates to 80% (about 220ms)
 *
 * // to get a "draggable progress bar" (drag bar / player progress)
 * bar.SetDraggable(true);                // mouse press + drag changes the value directly, a round thumb appears
 * bar.SetStep(1.0f);                     // optional: snap drag / keyboard to steps of 1
 * bar.SetOnValueChanged([](Kit::Widget& self, float v) {
 *     // only user interaction (drag / keyboard) calls back; programmatic SetValue does not
 * });
 * @endcode
 *
 * @par Size
 * Overrides `hintWidth` / `hintHeight`, natural size = fallback 160 x 20
 * (bar length x bar thickness, swapped when vertical). With an explicit `SetSize`, `pref` wins.
 *
 * > WARNING: Why the hint must be overridden: the `Widget` base class hint returns 0 by default. Without
 * > the override, after being placed in a `VBoxLayout`, `VBox::hintHeight()` = sum of child hintHeight = 0
 * > -> the parent container height collapses to 0 -> the widget is completely invisible with zero errors.
 * > Any custom leaf widget must override these two functions before going into `VBox`/`HBox`/`Grid`/`Flow`.
 *
 * @note The progress bar's fill ratio is data, not style -- changing the theme does not reset it;
 *       the theme only injects the track/fill/text/thumb colors (`Theme::progress` section).
 * @note By default it does not consume mouse events (read-only display). Only after `SetDraggable(true)` does it
 *       become an interactive widget: mouse press / drag changes the value, arrow keys fine-tune, Home/End jump to
 *       the ends. That is, "draggable slider" is its optional form, not another widget
 *       (difference from `Slider`: the progress bar fill is still `bar width x ratio`, no travel reserved).
 *
 * @see ZeBoxWidget.h box layer (background / border / corner radius / padding)
 * @see ZeSlider.h    pure input slider (thumb travel = bar length - thumb diameter)
 * @see ZeTheme.h     the progress section of the theme
 */

#pragma once
#include "ZeBoxWidget.h"

#include <functional>
#include <string>

namespace Kit {

    /// @brief Progress bar orientation (fill growth direction)
    enum class ProgressOrientation {
        Horizontal,   /// < grows from left to right (default)
        Vertical      /// < grows from bottom to top
    };

    /**
     * @brief Progress bar: track + proportionally filled foreground
     *
     * @details
     * Inherits `BoxWidget` rather than `TextWidget`: the body of a progress bar is that rectangle,
     * the text is an optional overlay (hidden by default), so text styling is only brought in as one theme
     * section and the whole set of `TextWidget` layout primitives is not needed.
     */
    class ProgressBar : public BoxWidget {
    public:
        /// @brief Value change callback (with sender + new value)
        /// @note Same shape as `Slider::ValueChangedAction`: fires only on user interaction.
        using ValueChangedAction = std::function<void(Widget&, float)>;

        /// @brief Constructs the progress bar
        /// @note Default range `0 ~ 100`, value `0`, horizontal, no text;
        ///       the constructor uses the progress section of the built-in light theme as default (it cannot go in the
        ///       base class constructor: `themeBox` is virtual, and a virtual call during base construction only hits
        ///       the base version).
        ProgressBar();

        // -------------------------------------------------------------------
        //  Value / range
        // -------------------------------------------------------------------

        /// @brief Sets the current value (clamped to `[min, max]`, invalid values treated as min)
        void SetValue(float value);
        /// @brief Current value
        float value() const { return value_; }

        /// @brief Sets the lower bound (swapped if > upper bound)
        void SetMin(float min);
        /// @brief Sets the upper bound (swapped if < lower bound)
        void SetMax(float max);
        /// @brief Sets the range at once (sorted automatically, guarantees min <= max)
        void SetRange(float min, float max);

        /// @brief Lower bound
        float min() const { return min_; }
        /// @brief Upper bound
        float max() const { return max_; }

        /// @brief Normalized progress `0 ~ 1` (returns 0 when the span is 0)
        /// @note This is the currently displayed fill ratio (during animation it is the interpolated value, not the
        ///       target). Painting and text formatting both derive from it -- so during animation the text also
        ///       moves smoothly. To get the "target value the user set" use `value()`; to get the target progress
        ///       use `targetProgress()`.
        float progress() const;

        /// @brief Sets progress directly as `[0,1]` (equivalent to `SetValue(min + k*(max-min))`)
        /// @param k normalized progress; values outside `[0,1]` are clamped
        void SetProgress(float k);

        // -------------------------------------------------------------------
        //  Drag bar (optional interaction; off by default -- when off it is identical word for word to a
        //  read-only progress bar)
        // -------------------------------------------------------------------
        //  * Why not a separate "DragBar": the appearance (track + proportional fill) and the semantics
        //    ("completion") are exactly the same, only "can it be dragged" differs. A switch is more honest than
        //    an extra class: same painting, same value range, same animation, the two cannot slowly drift apart.
        //
        //  * The drag mapping differs from `Slider` (intentionally): the progress bar fill is always
        //    `bar length x progress` -- no travel is reserved for the thumb. So "thumb center = fill edge",
        //    at 100% the bar is truly full (Slider at 100% stops the fill at the thumb center, leaving half a thumb).
        //    Cost: at 0% / 100% half the thumb lies outside the widget (clipped), looking like a
        //    round cap flush with the edge -- but that is exactly the visual expression of "dragged to the end".
        // -------------------------------------------------------------------

        /// @brief Turns dragging on / off (off by default)
        /// @param on true = becomes a draggable progress bar: consumes the mouse (press positions, drag changes the
        ///            value), enters the Tab chain, arrow keys fine-tune; false = reverts to read-only display
        /// @note When turned on it calls `SetFocusable(true)` (a prerequisite for keyboard fine-tuning) and sets the
        ///       cursor to a two-way arrow (SizeWE horizontal / SizeNS vertical); both are restored when turned off.
        /// @note When turned off while dragging, the drag ends immediately (no dangling capture).
        void SetDraggable(bool on);

        /// @brief Whether it is currently a draggable progress bar (default false)
        bool draggable() const { return draggable_; }

        /// @brief Whether it is currently being dragged (pressed and not released)
        bool dragging() const { return dragging_; }

        /// @brief Sets the step (default `0` = continuous, no snapping)
        /// @param step drag / keyboard results snap to the nearest integer multiple of the step relative to `min`;
        ///             `0` or negative = snapping off
        /// @note Same rules as `Slider::SetStep` (ticks counted relative to `min`);
        ///       programmatic `SetValue` also snaps -- otherwise after "set 7, step 5" one drag jumps by 5.
        void SetStep(float step);
        /// @brief Current step (0 = continuous)
        float step() const { return step_; }

        /// @brief Registers the value change callback (fires only on user interaction: drag / arrow keys)
        /// @note Only one callback is kept (assignment replaces, same convention as Slider / Button); pass
        ///       `nullptr` to clear. Programmatic `SetValue` does not fire -- avoids a "write back to itself" infinite loop.
        void SetOnValueChanged(ValueChangedAction handler);
        /// @brief Current callback (may be null)
        const ValueChangedAction& onValueChanged() const { return onValueChanged_; }

        /// @brief Whether to paint the round thumb (default true; painted only when `draggable()` is true)
        /// @note Turning it off = "a drag bar with no visible thumb" (the whole bar is draggable, just no handle).
        void SetShowThumb(bool show);
        /// @brief Whether the thumb is currently painted
        bool showThumb() const { return showThumb_; }

        /// @brief Thumb color (overrides the theme; defaults to thumbColor of the theme progress section)
        void SetThumbColor(const Color4f& c);
        /// @brief Current thumb color
        const Color4f& thumbColor() const { return thumbColor_; }

        /// @brief Thumb diameter (pixels; default 0 = derived automatically by the widget from the bar thickness)
        /// @param d `<= 0` restores automatic (automatic value = bar thickness - 4, minimum 8)
        void SetThumbSize(float d);
        /// @brief Current thumb diameter (0 = automatic)
        float thumbSize() const { return thumbSize_; }

        // -------------------------------------------------------------------
        //  Smooth animation ("silky loading process")
        // -------------------------------------------------------------------
        //  * Off by default -- keeps the deterministic semantics of "set and it is there immediately" (tests /
        //    synchronous scenarios need it). To animate, call SetAnimated(true) explicitly.
        //
        //  When on: SetValue sets the target value, and internally the widget eases the displayed value toward the
        //  target every frame; once reached it stops and schedules no more wakeups (no idle CPU burn under
        //  EventDriven). The driving method is the library's existing self-painted animation approach: in
        //  paintContent compute the next frame by time and RequestFrameWake (same as the ZeInputBox cursor blink,
        //  see the comments there).
        // -------------------------------------------------------------------

        /// @brief Turns smooth animation on/off (off by default)
        /// @note When turned off while animating, it jumps to the target value immediately (no intermediate state left).
        void SetAnimated(bool on);

        /// @brief Whether animation is currently on
        bool animated() const { return animated_; }

        /// @brief Sets the animation duration (milliseconds, default 220)
        /// @param ms total time to go from the current displayed value to the target value; too small is treated as 1ms
        void SetAnimationDuration(int ms);

        /// @brief Current animation duration (milliseconds)
        int animationDuration() const { return animMs_; }

        /// @brief Jumps to the target value immediately (skips animation; does not change the "animation on" setting)
        void FinishAnimation();

        /// @brief Target value (the value finally reached when the animation ends; equals value() when animation is off)
        float targetValue() const { return target_; }

        /// @brief Target normalized progress `0 ~ 1`
        float targetProgress() const;

        /// @brief Whether the animation is still running
        bool animating() const { return animating_; }

        /// @brief Whether this widget needs the next frame right now (true while animating)
        /// @note Overrides `Widget::wantsAnimationFrame()` -- the progress bar's smooth slide is a
        ///       continuous animation; under `DriveMode::OnDemand` it tells the host "still moving",
        ///       so the host schedules frames at a uniform frame rate; when the slide finishes it returns false and
        ///       the host automatically falls back to zero CPU. See the base class notes for the division of labor
        ///       with `RequestFrameWake` (discrete timers).
        bool wantsAnimationFrame() const override { return animating_; }

        // -------------------------------------------------------------------
        //  Orientation
        // -------------------------------------------------------------------

        /// @brief Sets the orientation (Horizontal left to right / Vertical bottom to top)
        void SetOrientation(ProgressOrientation o);
        /// @brief Current orientation
        ProgressOrientation orientation() const { return orientation_; }
        /// @brief Whether vertical
        bool isVertical() const { return orientation_ == ProgressOrientation::Vertical; }

        // -------------------------------------------------------------------
        //  Text (optional text overlaid on the bar)
        // -------------------------------------------------------------------

        /// @brief Whether to show text on the bar (default false)
        /// @note The text is painted centered within the whole widget rectangle (it does not follow the fill), so
        ///       horizontally it stays steadily in the center of the bar; vertically it is centered too.
        void SetShowText(bool show) { showText_ = show;  RequestWindowRedraw();}
        /// @brief Whether text is currently shown
        bool showText() const { return showText_; }

        /// @brief Custom text template (default `"{}%"`, shows the rounded percentage)
        /// @param fmt template; `{}` is replaced by the percentage integer (e.g. `42`).
        ///            Without `{}`: if it contains `%` it is treated as a C-style format string (the argument is the
        ///            percentage as a float, e.g. `"%.1f%%"`); otherwise it is shown as-is (usable as a static label).
        /// @note WARNING: With C-style syntax the conversion specifier must be floating-point (`%f` / `%.1f` / `%g`).
        ///       Writing `%d` passes a double to an integer specifier -- undefined behavior (observed locally printing
        ///       `536870912%`). The implementation rewrites `%d` / `%i` to `%ld` as a fallback,
        ///       but for `%u` / `%x` and the like, write the correct type yourself.
        void SetTextFormat(std::string fmt);
        /// @brief Current text template
        const std::string& textFormat() const { return textFormat_; }

        /// @brief Text color (normal state; overrides the theme)
        void SetTextColor(const Color4f& c);
        /// @brief Current text color (normal state)
        const Color4f& textColor() const { return textColor_; }

        /// @brief Font of the overlaid text (only size / weight are used -- the progress bar does no text layout)
        void SetFontDesc(const FontDesc& font);
        /// @brief Current overlaid text font
        const FontDesc& fontDesc() const { return font_; }

        // -------------------------------------------------------------------
        //  Self-reported natural size (measurement protocol) -- required for VBox / FlowLayout
        // -------------------------------------------------------------------
        //  WARNING: This group cannot be omitted. The first version did not implement them ("progress bar length is
        //     decided by the caller"), and the result after placing it in a `VBoxLayout`: `VBox::hintHeight()` = sum of
        //     child heights = 0 -> the parent container height collapses to 0 -> the whole block takes no space.
        //     The symptom is "the progress bar does not show", but the root cause is a broken measurement protocol,
        //     not the layout.
        //
        //     Convention: hint gives "how big a default progress bar should be" (width 160 / height 20, same as
        //     `SetDefaultSize`), so dropping it into any layout first has a reasonable footprint;
        //     after the user calls `SetSize` explicitly the user value still wins (pref always wins).
        // -------------------------------------------------------------------

        /// @brief Natural width = user value if set, otherwise 160 (default bar length) + padding + border
        float hintWidth() const override;

        /// @brief Natural height = user value if set, otherwise 20 (default bar height) + padding + border
        /// @note A vertical progress bar's "length" is on the height axis, so when vertical the natural height is 160
        ///       and the natural width is 20 -- mirrored from horizontal, so it does not collapse in reverse in a layout.
        float hintHeight() const override;

        // -------------------------------------------------------------------
        //  Colors (track / fill; override the progress section of the theme)
        // -------------------------------------------------------------------

        /// @brief Track background color (the whole bar base)
        void SetTrackColor(const Color4f& c);
        /// @brief Current track background color
        const Color4f& trackColor() const { return trackColor_; }

        /// @brief Foreground color of the filled part
        void SetFillColor(const Color4f& c);
        /// @brief Current fill color
        const Color4f& fillColor() const { return fillColor_; }

        // -------------------------------------------------------------------
        //  Theme injection
        // -------------------------------------------------------------------

        /// @brief Re-injects style from the theme (overwrites colors the user changed manually)
        /// @note Overrides `BoxWidget::ApplyTheme`: the box is injected as usual, and additionally the track/fill/text
        ///       colors and font size of the progress section are taken over. After manual changes via
        ///       `SetXxxColor`, switching the theme overwrites them (consistent with the library-wide "inject = start over").
        void ApplyTheme(const Theme& t) override;

    protected:
        /// @brief Default box style takes the progress section of the theme (the track background color part)
        const BoxStyle& themeBox(const Theme& t) const override;

        /// @brief Paints: track (full bar) -> fill (proportional) -> thumb (when draggable) -> text (optional)
        /// @note The content is clipped uniformly to the padding box by `BoxWidget::Paint`, so no clipping is needed here.
        void paintContent(FrameCollector& fc) const override;

        // ---- Mouse / keyboard interaction (meaningful only after `SetDraggable(true)`) ----
        //  Drag continuity relies on the Window's pointer capture: on press it remembers this widget, and subsequent
        //  Move / Release are always routed back here (even if the pointer leaves the widget or the window).
        bool OnMousePress(MouseEvent& e) override;
        bool OnMouseMove(MouseEvent& e) override;
        bool OnMouseRelease(MouseEvent& e) override;
        /// @brief Arrow keys +/- step, Home/End jump to the ends (requires focus first: focusable automatically when draggable)
        bool OnKeyPress(KeyEvent& e) override;

    private:
        /// @brief Converts the current value to a percentage `0 ~ 100` (for text formatting)
        float percent() const;

        /// @brief Advances the animation during rendering (eases the displayed value toward the target by time)
        /// @note Called every frame by paintContent; if still animating after returning, RequestFrameWake continues frames.
        void stepAnimation() const;

        /// @brief Snaps to the nearest step (returns as-is when step_ <= 0; ticks counted relative to `min`)
        float snapToStep(float v) const;

        /// @brief Converts "client-area coordinates" to "normalized progress 0~1" (the only source of the drag mapping)
        /// @param local offset on that axis relative to the origin of the window client area (same system as MouseEvent::x)
        /// @note The progress bar reserves no travel for the thumb: local maps directly to the full content area,
        ///       so "thumb center = fill edge", and at 100% the bar is full (see the header notes).
        float posToProgress(float local) const;

        /// @brief Value-setting entry for user interaction: snap -> clamp -> update target/display -> callback
        /// @return whether the value actually changed (only then callback / repaint)
        bool setValueByUser(float v);

        /// @brief Notifies externally that "the value changed" (fires onValueChanged_)
        void notifyChanged();

        /// @brief Sets the cursor to the "draggable" look for the current orientation (back to arrow when dragging is off)
        void applyDragCursor();

        float min_ = 0.0f;                            /// < lower bound
        float max_ = 100.0f;                          /// < upper bound
        /// Displayed value (during animation = interpolated). Marked mutable: the animation advances by time during
        /// rendering (inside the const paintContent) -- this is the library's existing form of "self-painted animation"
        /// (same as the ZeInputBox cursor blink: compute the next phase while painting and continue frames), not an
        /// external state change.
        mutable float value_ = 0.0f;
        float target_ = 0.0f;                         /// < target value (what SetValue sets)
        float step_ = 0.0f;                           /// < step (0 = continuous, no snapping)

        ProgressOrientation orientation_ = ProgressOrientation::Horizontal;

        // ---- Drag state ----
        bool draggable_ = false;                      /// < whether draggable (off by default: read-only display)
        bool dragging_ = false;                       /// < whether currently dragging
        bool showThumb_ = true;                       /// < whether to paint the thumb (effective only when draggable)
        float thumbSize_ = 0.0f;                      /// < thumb diameter (0 = automatic)

        bool        showText_ = false;                /// < whether to show text
        std::string textFormat_ = "{}%";              /// < text template

        // ---- Animation (mutable: advanced inside the const paintContent) ----
        bool  animated_ = false;                      /// < whether animation is on (off by default, keeps determinism)
        int   animMs_   = 220;                        /// < total animation duration (milliseconds)
        mutable bool animating_ = false;              /// < whether currently animating (rewritten during rendering)
        /// @brief Start value of the current animation + start time (steady_clock milliseconds)
        mutable float           animFrom_ = 0.0f;
        mutable long long       animStartMs_ = 0;

        Color4f trackColor_{ 0.0f, 0.0f, 0.0f, 0.0f };   /// < track background color
        Color4f fillColor_{ 0.0f, 0.0f, 0.0f, 0.0f };    /// < fill foreground color
        Color4f textColor_{ 0.0f, 0.0f, 0.0f, 1.0f };    /// < text color
        Color4f thumbColor_{ 1.0f, 1.0f, 1.0f, 1.0f };   /// < thumb background color
        FontDesc font_;                                   /// < overlaid text font (size / weight)

        /// Six-state colors of the thumb outline (brighter on hover / press -- the thumb's interaction feedback).
        /// Captured by ApplyTheme from `Theme::progress.thumb.colors`.
        MultiStateColors thumbBorderColors_;
        /// Thumb outline width (the maximum width of the four sides of the theme thumb). 0 = no outline (solid circle).
        float thumbBorderWidth_ = 0.0f;

        ValueChangedAction onValueChanged_;           /// < value change callback (fires only on user interaction)
    };

} // namespace Kit
