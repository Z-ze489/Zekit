/**
 * @file ZeIndicator.h
 * @brief Appearance vocabulary for indicators: container shape / check mark / radio mark
 *
 * @details
 * `Checkbox` and `RadioButton` are indicator widgets of the same family: a small square/dot on the left,
 * a line of label on the right. Their interaction differs (multi-select vs mutually exclusive), but their appearance shares the same set of parts:
 *
 * ```
 *   ┌──────────┐
 *   │ container│   <- IndicatorShape: square / large rounded / circle / diamond
 *   │   mark   │   <- CheckMark (checkbox) / RadioMark (radio button)
 *   └──────────┘
 *        label text
 * ```
 *
 * This header holds only enums (pure data, no dependencies, no platform); the paint implementation lives in the library
 * `ZeIndicatorPaint.h` (not part of the delivery surface). Reasons for this:
 *
 *  - Users only need to "pick a style" and do not need to know any primitive types -- the delivery surface
 *    does not expose `FrameCollector` / `Shape` / `Vec2`;
 *  - Themes, .ui configuration and serialization can store these integer values directly;
 *  - Adding a style later (say a "triangle") touches only this enum plus one paint branch,
 *    and the interfaces of both widgets need no change at all.
 *
 * @par Defaults = legacy appearance
 * `Checkbox` defaults to `Square` + `Tick`, `RadioButton` defaults to `Circle` + `Dot` --
 * pixel-identical to before this enum set was added, so upgrading does not change the look of any existing UI.
 *
 * @par Basic usage
 * @code
 * Kit::Checkbox cb("Agree to terms");
 * cb.SetIndicatorShape(Kit::IndicatorShape::Rounded);
 * cb.SetMarkStyle(Kit::CheckMark::Cross);
 *
 * Kit::RadioButton rb("Pay monthly");
 * rb.SetMarkStyle(Kit::RadioMark::Tick);      // radio buttons can also show a tick
 * @endcode
 *
 * @see ZeCheckbox.h    checkbox (multi-select + tri-state)
 * @see ZeRadioButton.h radio button (mutually exclusive within a group)
 */

#pragma once

namespace Kit {

    /**
     * @brief Shape of the indicator container (shared by checkbox and radio button)
     *
     * @details
     * The container is the ring/box around the mark. The same mark drawn in different containers looks very different:
     *  - `Square` -- classic Windows style, straight edges, the checkbox default;
     *  - `Rounded` -- square with large corner radius, more modern (common in Material / Fluent);
     *  - `Circle` -- perfect circle, the radio button default; a checkbox using it looks softer;
     *  - `Diamond` -- rotated square, visually louder, suited to options that need to stand out (featured/recommended).
     *
     * @note The shape affects only the container; what is drawn inside is decided by `CheckMark` / `RadioMark`.
     */
    enum class IndicatorShape {
        Square,     /// < square (small corner radius, about 22% of the side length) -- checkbox default
        Rounded,    /// < square (large corner radius, about 34% of the side length)
        Circle,     /// < circle -- radio button default
        Diamond     /// < diamond (square rotated 45 degrees)
    };

    /**
     * @brief Mark drawn inside the container when a checkbox is checked
     *
     * @details
     * When unchecked the container is a hollow box and no mark is drawn; the mark chosen here is drawn only when checked.
     * In tri-state (`SetTriState(true)`) the indeterminate state always draws a horizontal bar -- that is the common visual
     * for "partially selected" and does not follow this enum (otherwise indeterminate and checked would be indistinguishable).
     *
     * The mark color is decided by the container: when the container is a solid accent color, the mark automatically uses the
     * indicator background color (that is, a white tick on a blue background); with a background-less style it uses the accent color.
     * This is handled in paint and users do not need to care.
     */
    enum class CheckMark {
        Tick,       /// < tick (two-segment polyline) -- default
        Cross,      /// < cross (two diagonals)
        Plus,       /// < plus (horizontal and vertical segments)
        Dot,        /// < solid dot
        Bar,        /// < a thick horizontal bar (the "switch is on" feel)
        Fill,       /// < no symbol, the whole container is filled with the accent color
        Star        /// < five-pointed star
    };

    /**
     * @brief Mark drawn inside the container when a radio button is selected
     *
     * @details
     * Defining this separately from `CheckMark` is intentional: the radio container is usually already filled with the accent color,
     * far fewer symbols make sense (`Dot` / `Fill` / `Ring` are meaningless in a square container),
     * and sharing one enum would force users to guess which combinations look right.
     *
     * When unselected the container is a hollow ring/box (stroke color); when selected it is filled with the accent color and the mark is drawn.
     */
    enum class RadioMark {
        Dot,        /// < solid dot at the center -- default (the classic donut plus core look)
        Fill,       /// < no symbol, the whole container is filled ("solid circle")
        Ring,       /// < thick ring (the center keeps the background color, forming a "hollow thick ring")
        Tick,       /// < tick (same line set as the checkbox)
        Bar         /// < a thick horizontal bar
    };

} // namespace Kit
