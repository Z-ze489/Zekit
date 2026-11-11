// ZeSlider.h - Slider: drag a thumb along a track to pick a value from a range.
//
// Slider : BoxWidget, same family as ProgressBar (both draw "track + fill"),
// but Slider is interactive: mouse drag, arrow-key tuning, and a change
// callback. It manages:
//   1. Value: SetValue clamps into [min, max] (default 0~100); NaN snaps to
//      min. Thumb position = normalized value linearly mapped onto travel.
//   2. Drag: press + move on the track changes the value. Pressing off-thumb
//      makes the thumb smoothly jump over while keeping the grabbed relative
//      offset (same feel as the ScrollView scrollbar).
//   3. Keyboard: after SetFocusable(true), clicking focuses it; arrows adjust
//      by step (left/right and up/down both work; Home/End jump to the ends).
//   4. Callback: SetOnValueChanged fires only for user interaction (drag /
//      keys). Programmatic SetValue does NOT fire, avoiding self-feeding loops.
//
// ProgressBar is read-only display of completion; Slider is writable input.
//
// Must override hintWidth/hintHeight: the Widget base hint defaults to 0;
// without the override a VBoxLayout sums child heights to 0 and the widget
// becomes invisible with no error.

#pragma once
#include "ZeBoxWidget.h"

#include <functional>
#include <string>

namespace Kit {

    // Slider direction (track direction / value growth direction)
    enum class SliderOrientation {
        Horizontal,   // Grows left to right (default)
        Vertical      // Grows bottom to top (like ProgressBar, bottom = min)
    };

    // Slider: a track + a fill segment + a draggable thumb.
    // Inherits BoxWidget: the track is the box body (background, border,
    // radius, padding); the thumb and fill are drawn inside the content area.
    class Slider : public BoxWidget {
    public:
        // Value changed callback (sender + new value)
        using ValueChangedAction = std::function<void(Widget&, float)>;

        // Constructs a slider. Defaults: range 0~100, value 0, horizontal,
        // step 0 (continuous), no value text, no animation. Uses the built-in
        // light theme's slider section. Applied here rather than in the base
        // ctor because themeBox is virtual and would resolve to the base
        // version during base construction.
        Slider();

        // -------------------------------------------------------------------
        //  Value / range
        // -------------------------------------------------------------------

        // Sets the current value (clamped to [min, max]; NaN snaps to min).
        // Does NOT fire SetOnValueChanged (that is for user interaction).
        void SetValue(float value);
        // Current value
        float value() const { return value_; }

        // Sets the lower bound (swapped with max if greater)
        void SetMin(float min);
        // Sets the upper bound (swapped with min if less)
        void SetMax(float max);
        // Sets the range in one call (auto-sorted so min <= max)
        void SetRange(float min, float max);

        // Lower bound
        float min() const { return min_; }
        // Upper bound
        float max() const { return max_; }

        // Normalized position 0~1 (returns 0 when span is 0). This is the
        // thumb's current displayed position (interpolated while animating);
        // drag mapping and painting both derive from it. Use value() for the
        // target value.
        float progress() const;

        // Sets the value from [0,1] (equivalent to SetValue(min + k*(max-min)))
        void SetProgress(float k);

        // -------------------------------------------------------------------
        //  Step (snapping)
        // -------------------------------------------------------------------

        // Sets the step (default 0 = continuous, no snapping). Arrow keys
        // change the value by step; when > 0, ALL value changes (set/drag)
        // snap to the nearest step multiple (step=5 -> values are 0/5/10...).
        // Zero or negative disables snapping.
        void SetStep(float step);
        // Current step (0 = continuous)
        float step() const { return step_; }

        // -------------------------------------------------------------------
        //  Orientation
        // -------------------------------------------------------------------

        // Sets the direction (Horizontal: left to right / Vertical: bottom to top)
        void SetOrientation(SliderOrientation o);
        // Current direction
        SliderOrientation orientation() const { return orientation_; }
        // Whether vertical
        bool isVertical() const { return orientation_ == SliderOrientation::Vertical; }

        // -------------------------------------------------------------------
        //  Callback
        // -------------------------------------------------------------------

        // Registers the value changed callback (fires only on user
        // interaction: drag / arrow keys). Only one callback is kept
        // (assignment replaces, same convention as Button::SetOnClick).
        // Pass nullptr to clear.
        void SetOnValueChanged(ValueChangedAction handler);
        // Current callback (may be empty)
        const ValueChangedAction& onValueChanged() const { return onValueChanged_; }

        // -------------------------------------------------------------------
        //  Value text (optional, shown above / beside the thumb)
        // -------------------------------------------------------------------

        // Whether to show the value text on the slider (default false)
        void SetShowText(bool show) { showText_ = show; markStyled(); RequestWindowRedraw();}
        // Whether value text is shown
        bool showText() const { return showText_; }

        // Value text format (default "{}", showing the integer of value).
        // "{}" is replaced with the integer value; if the string contains '%',
        // it is treated as a C-style format string (argument is the float
        // value). Use one style, not both. Without {} or %, shown as-is.
        void SetTextFormat(std::string fmt);
        // Current text format
        const std::string& textFormat() const { return textFormat_; }

        // Value text color (overrides theme)
        void SetTextColor(const Color4f& c);
        // Current value text color
        const Color4f& textColor() const { return textColor_; }

        // -------------------------------------------------------------------
        //  Smooth animation (transition for programmatic value setting;
        //  dragging always follows the pointer and is unaffected)
        // -------------------------------------------------------------------
        //  Off by default to keep "set means in place now" deterministic
        //  semantics (tests/sync need it). When on, SetValue sets the target
        //  and the displayed value eases toward it each frame. Dragging does
        //  not use it (dragging must be immediate).
        // -------------------------------------------------------------------

        // Enables/disables smooth animation (default off). If animation is in
        // progress when turned off, jumps to the target immediately.
        void SetAnimated(bool on);

        // Whether animation is enabled
        bool animated() const { return animated_; }

        // Sets the animation duration in ms (default 180)
        void SetAnimationDuration(int ms);
        // Current animation duration in ms
        int animationDuration() const { return animMs_; }

        // Jumps to the target value immediately (skips animation; does not
        // change the animated setting)
        void FinishAnimation();

        // Target value (final value after animation; equals value() when
        // animation is off)
        float targetValue() const { return target_; }

        // Whether an animation is in progress
        bool animating() const { return animating_; }

        // Whether this widget needs the next frame (true while animating).
        // Overrides Widget::wantsAnimationFrame: smooth transition is a
        // continuous animation; in DriveMode::OnDemand it tells the host to
        // keep scheduling frames at a uniform rate, then the host drops back
        // to zero CPU when done.
        bool wantsAnimationFrame() const override { return animating_; }

        // -------------------------------------------------------------------
        //  Self-reported natural size (measurement protocol) - required when
        //  placed in VBox / FlowLayout
        // -------------------------------------------------------------------

        // Natural width = user-set value if any, else 160 (default length) +
        // padding + border
        float hintWidth() const override;
        // Natural height = user-set value if any, else 24 (default thickness)
        // + padding + border. For a vertical slider the length lies on the
        // height axis, so vertical uses 160 height / 24 width.
        float hintHeight() const override;

        // -------------------------------------------------------------------
        //  Colors (track / fill / thumb; overrides the theme's slider section)
        // -------------------------------------------------------------------

        // Track background color (whole track)
        void SetTrackColor(const Color4f& c);
        // Current track background color
        const Color4f& trackColor() const { return trackColor_; }

        // Fill segment (min -> current value) color
        void SetFillColor(const Color4f& c);
        // Current fill color
        const Color4f& fillColor() const { return fillColor_; }

        // Thumb color
        void SetThumbColor(const Color4f& c);
        // Current thumb color
        const Color4f& thumbColor() const { return thumbColor_; }

        // Thumb diameter (default 0 = auto-derived from track thickness).
        // d = diameter in pixels; <= 0 restores auto.
        void SetThumbSize(float d);
        // Current thumb diameter (0 = auto)
        float thumbSize() const { return thumbSize_; }

        // -------------------------------------------------------------------
        //  Theme injection
        // -------------------------------------------------------------------

        // Re-injects styles from the theme (overwrites manually set colors)
        void ApplyTheme(const Theme& t) override;

    protected:
        // Default box style comes from the theme's slider section (the track)
        const BoxStyle& themeBox(const Theme& t) const override;

        // Paints: track -> fill segment -> thumb -> optional value text.
        // Content is already clipped to the padding box by BoxWidget::Paint,
        // so no manual clipping is needed here.
        void paintContent(FrameCollector& fc) const override;

        // ---- Mouse interaction (dragging) ----
        //  Pressing anywhere on the track starts dragging (the thumb need not
        //  be hit exactly - it jumps over, more intuitive than requiring a
        //  precise 10-pixel hit). Continuity relies on Window pointer
        //  capture: on press the Window remembers this widget and routes
        //  Move/Release back here even if the pointer leaves the widget or
        //  the window.
        bool OnMousePress(MouseEvent& e) override;
        bool OnMouseMove(MouseEvent& e) override;
        bool OnMouseRelease(MouseEvent& e) override;

        // ---- Keyboard (after SetFocusable(true)) ----
        bool OnKeyPress(KeyEvent& e) override;

    private:
        // Converts the current value to a percentage 0~100 (for text format)
        float percent() const;

        // Advances the animation during rendering (eases displayed value
        // toward the target by time)
        void stepAnimation() const;

        // Snaps to the nearest step multiple (returned as-is if step_ <= 0)
        float snapToStep(float v) const;

        // Converts a client-area coordinate to a normalized position 0~1.
        // local = offset from the widget's outer origin on the axis.
        // Usable travel = content length - thumb diameter; the thumb CENTER
        // maps onto the position.
        float posToProgress(float local) const;

        // User-interaction entry point: snap -> clamp -> update target/display
        // -> notify. Returns whether the value actually changed (only then
        // notify/repaint).
        bool setValueByUser(float v);

        // Notifies that the value changed (fires onValueChanged_)
        void notifyChanged();

        // ---- Value ----
        float min_ = 0.0f;                            // Lower bound
        float max_ = 100.0f;                          // Upper bound
        // Displayed value (interpolated while animating). mutable because the
        // animation advances during rendering (const paintContent) - the
        // library's established self-drawn animation pattern.
        mutable float value_ = 0.0f;
        float target_ = 0.0f;                         // Target value (what SetValue/drag sets)
        float step_ = 0.0f;                           // Step (0 = continuous, no snapping)

        SliderOrientation orientation_ = SliderOrientation::Horizontal;

        // ---- Drag state ----
        bool  dragging_ = false;                      // Whether currently dragging
        // Pointer offset from the thumb center while grabbing (keeps the
        // grabbed relative offset for a continuous feel). Pressing the track
        // (not the thumb) sets it to the thumb radius, so the thumb center
        // chases under the pointer and further dragging starts from center.
        float dragGrab_ = 0.0f;

        // ---- Value text ----
        bool        showText_ = false;                // Whether to show value text
        std::string textFormat_ = "{}";               // Text format
        Color4f     textColor_{ 0.0f, 0.0f, 0.0f, 1.0f };  // Value text color

        // ---- Animation (mutable: advanced in const paintContent) ----
        bool  animated_ = false;                      // Whether animation is on (default off, deterministic)
        int   animMs_ = 180;                          // Total animation duration (ms)
        mutable bool animating_ = false;              // Whether animating (written during render)
        mutable float     animFrom_ = 0.0f;           // Start value of the current animation
        mutable long long animStartMs_ = 0;           // Start time of the current animation (steady_clock ms)

        // ---- Colors ----
        Color4f trackColor_{ 0.0f, 0.0f, 0.0f, 0.0f };   // Track background color
        Color4f fillColor_{ 0.0f, 0.0f, 0.0f, 0.0f };    // Fill segment color
        Color4f thumbColor_{ 0.0f, 0.0f, 0.0f, 1.0f };   // Thumb color
        float   thumbSize_ = 0.0f;                        // Thumb diameter (0 = auto)

        // Six-state colors for the thumb border (hover/press brighter - the
        // thumb's interaction feedback). Captured from
        // Theme::slider.thumb.colors by ApplyTheme.
        MultiStateColors thumbBorderColors_;
        // Thumb border width (max of the theme thumb's four sides).
        // 0 = no border (solid circle).
        float thumbBorderWidth_ = 0.0f;

        // Value text font (size/weight; captured from slider.text by ApplyTheme)
        FontDesc font_;

        ValueChangedAction onValueChanged_;           // Value changed callback (user interaction only)
    };

} // namespace Kit
