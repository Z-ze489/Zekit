/**
 * @file ZeRating.h
 * @brief Rating widget: a row of stars, clickable / draggable / keyboard-driven, supports half stars
 *
 * @details
 * `Rating : BoxWidget` -- it paints the stars itself (polygons), so it needs no text layer; but it is **interactive and
 * focusable**, so it has an extra set of input handling compared to the purely display `Badge` / `Icon` (same family as `Slider`).
 *
 * @par The two "current values"
 * Like the slider, rating also has two values:
 *  - `value()`   -- **result value** (only counts once the user clicks);
 *  - `hoverValue()` -- **preview value** ("what score would result if clicked" while the mouse hovers on top).
 * When painting, use `display value = hoverValue >= 0 ? hoverValue : value`; callbacks report only `value()`.
 *
 * @par How half stars are painted
 * The clip region of this library is rectangular -- which is exactly enough: first paint an **unselected** star, then `PushClip` to
 * the rectangle of the left half of this star (or the whole star), paint a **selected** star at the same position, then `PopClip`.
 * Thus a "half star" is two pieces stacked, requiring no mask channel at all.
 *
 * @par Interaction
 * | Operation | Behavior |
 * |---|---|
 * | Left-click a star | Sets value (in half-star mode, left / right half sets x.5 / x) |
 * | Press and drag | Changes value continuously (pointer capture is handled by `Window`, the widget does not care) |
 * | Click the **same value** again | Clears to zero (when `SetAllowClear(true)`, on by default) |
 * | `<-` / `->` | +/-1 (+/-0.5 in half-star mode) |
 * | `Home` / `End` | Min / max |
 * | Mouse leaves | Clears the preview, returns to `value()` |
 *
 * @code
 * Kit::Rating r(5);
 * r.SetAllowHalf(true);
 * r.SetValue(3.5f);
 * r.SetOnValueChanged([](Kit::Widget& self, float v) {
 *     std::printf("rate = %.1f\n", v);
 * });
 * @endcode
 *
 * @see ZeSlider.h   another "two values + drag" widget (for comparison)
 * @see ZeTheme.h    `RatingStyle`
 */

#pragma once
#include "ZeBoxWidget.h"
#include "ZeSelfAction.h"

namespace Kit {

    /**
     * @brief Rating widget
     */
    class Rating : public BoxWidget {
    public:
        /// @brief Constructs
        /// @param maxStars total number of stars (default 5)
        explicit Rating(int maxStars = 5);

        // -------------------------------------------------------------------
        //  Data
        // -------------------------------------------------------------------

        /// @brief Total number of stars (>= 1; out of range is treated as 1)
        void SetMaxStars(int n);
        int maxStars() const { return maxStars_; }

        /// @brief Sets the rating (**programmatic path: does not fire callbacks**)
        /// @param v score; clamped to `[0, maxStars]` (rounded to 0.5 in half-star mode)
        void SetValue(float v);
        float value() const { return value_; }

        /// @brief Whether half stars are allowed (default false)
        void SetAllowHalf(bool on);
        bool allowHalf() const { return allowHalf_; }

        /// @brief Whether clicking the same value again clears to zero (default true)
        void SetAllowClear(bool on);
        bool allowClear() const { return allowClear_; }

        /// @brief Read-only (display only, accepts no mouse / keyboard)
        void SetReadOnly(bool ro);
        bool isReadOnly() const { return readOnly_; }

        /// @brief Disabled (difference from read-only: disabled also lowers opacity and consumes no events at all)
        /// @note Overriding `Widget::SetEnabled` only to also clear the hover preview.
        void SetEnabled(bool enabled) override;

        // -------------------------------------------------------------------
        //  Appearance (value interface)
        // -------------------------------------------------------------------

        /// @brief Side length of the bounding box of a single star (pixels); <= 0 = follow theme
        void SetStarSize(float px);
        float starSize() const { return starSize_; }

        /// @brief Spacing between stars (pixels); < 0 is treated as 0
        void SetStarGap(float px);
        float starGap() const { return gap_; }

        void SetActiveColor(const Color4f& c);
        void SetInactiveColor(const Color4f& c);
        void SetHoverColor(const Color4f& c);

        /// @brief Whether "Hover preview" is enabled (default true)
        void SetHoverPreview(bool on);
        bool hoverPreview() const { return hoverPreview_; }

        /// @brief Current preview value (-1 = no preview)
        float hoverValue() const { return hoverValue_; }

        // -------------------------------------------------------------------
        //  Callbacks (fired only on the user path)
        // -------------------------------------------------------------------

        /// @brief "Rating changed" callback (with the new score)
        void SetOnValueChanged(SelfAction1<float> handler);
        /// @brief "Rating changed" callback (sender only)
        void SetOnValueChanged(SelfAction handler);

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
        bool OnMouseMove(MouseEvent& e) override;
        bool OnMouseRelease(MouseEvent& e) override;
        void OnMouseLeave() override;
        bool OnKeyPress(KeyEvent& e) override;

    protected:
        /// @brief Paints the row of stars (half stars are stacked via rectangular clipping)
        void paintContent(FrameCollector& fc) const override;

    private:
        /// @brief Step of a single star (including spacing)
        float step() const;
        /// @brief Converts cursor x to a score (clamped to the valid range)
        float valueAtX(float px) const;
        /// @brief Unified exit of the user path: writes the value + fires callbacks (not fired if the value is unchanged)
        void setByUser(float v);
        /// @brief Normalizes the value to a valid grid point (half star -> multiple of 0.5)
        float snap(float v) const;
        /// @brief Fires the registered callbacks
        void notify(float v);
        /// @brief Requests a repaint
        void requestRepaint();

        int   maxStars_ = 5;
        float value_ = 0.0f;

        bool  allowHalf_ = false;
        bool  allowClear_ = true;
        bool  readOnly_ = false;
        bool  hoverPreview_ = true;

        float hoverValue_ = -1.0f;      /// < Preview value (-1 = none)
        bool  dragging_ = false;        /// < Whether dragging is in progress
        // * 2026-11-07: removed `pressValue_` -- it was only ever written, never read.
        //   The "click again to clear" check uses `v == value_` **at press time** (see OnMousePress),
        //   so no extra copy is needed; keeping it only makes people think there is a drag-time guard.

        float starSize_ = 0.0f;         /// < Explicit star size (0 = follow theme)
        float gap_ = -1.0f;             /// < Explicit spacing (< 0 = follow theme)

        Color4f activeColor_{ 0.0f, 0.0f, 0.0f, 1.0f };
        Color4f inactiveColor_{ 0.0f, 0.0f, 0.0f, 1.0f };
        Color4f hoverColor_{ 0.0f, 0.0f, 0.0f, 1.0f };
        float   themeStarSize_ = 22.0f;
        float   themeGap_ = 2.0f;
        float   hoverScale_ = 1.06f;

        SelfAction1<float> onChanged_;
        SelfAction         onChangedSelf_;
    };

} // namespace Kit
