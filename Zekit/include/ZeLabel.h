/**
 * @file ZeLabel.h
 * @brief Label: read-only text (multi-line, optional overflow handling)
 *
 * @details
 * `Label : TextWidget` -- box and text styles, font/alignment/decoration setters are all inherited;
 * this class only adds the two things that "read-only text" needs:
 *
 *  1. Multi-line: breaks lines at explicit `\n` (`\r` is consumed automatically, CRLF compatible).
 *     Each line is aligned independently by `hAlign`; the whole block is positioned in the content area by `vAlign`;
 *     line spacing = `TextStyle::lineHeight` (0 = follow the height of the tallest line).
 *     WARNING: no automatic wrapping -- wrapping is a layout policy: it needs to know "how much width am I given" first to
 *     decide where to fold. The base class `hintHeightFor(availWidth)` already provides this interface;
 *     automatic wrapping is implemented once `Label` is wired to text measurement.
 *  2. Overflow handling: what to do when the text is wider than the content area -- see TextOverflow (default Ellipsis, adds an ellipsis).
 *
 * Theme: by default takes the theme **label section** (`t.label.box` / `t.label.text`),
 * so "no border, font size 14, left aligned, vertically centered" is the plain-text look.
 *
 * @note Size adaptation: this widget overrides `hintWidth()` / `hintHeight()` -- when `SetSize`
 *       is not called explicitly, the natural size = **text width of the longest line** x **line count x line height**, plus
 *       its own padding and border. So inside `FlowLayout` / `VBoxLayout` it expands naturally to the text;
 *       if `SetSize` was called explicitly, the explicit value still wins (measurement protocol: what the user sets always wins).
 *       Measurement goes through `Window::MeasureText()` (shares glyph metrics with painting, results are exactly consistent).
 *       Before `Show()`, the font is not ready, so it falls back to the base class hint (= pref) and does not misreport.
 * @note Label is downstream of the interactive base class, so it **also** has hover / disabled state color picking
 *       (to color a "link-style label", just call SetHoverTextColor).
 *
 * @par Basic usage
 * @code
 * Kit::Label tip("This text gets an ellipsis automatically when it does not fit");
 * tip.SetGeometry(20, 20, 160, 20);
 * tip.SetTextColor(Kit::Color4f::FromHex(0x334155));
 * win.AddWidget(tip);                        // value takes over, then keep modifying via the returned reference
 * @endcode
 *
 * @see ZeTextWidget.h text layer (font / alignment / decoration / layout primitives)
 * @see ZeBoxWidget.h box layer (background / border / rounded corners / padding)
 */

#pragma once
#include "ZeTextWidget.h"

#include <string>

namespace Kit {

    class Label : public TextWidget {
    public:
        /// @brief Constructs a label
        /// @param text Initial text (may contain '\n' line breaks)
        /// @note At construction, uses the label section of the built-in light theme as default, and gives a **120x24 fallback size**
        ///       (via `SetDefaultSize`, does not write `pref`). Once attached to a window and the text size can be computed,
        ///       the natural size takes priority; when it cannot be computed yet (not `Show()`), the fallback value is used so it stays visible.
        explicit Label(std::string text = std::string());

        /// @brief How to handle text that does not fit
        /// @param mode Default TextOverflow::Ellipsis (truncate + "...")
        void SetOverflow(TextOverflow mode) { overflow_ = mode;  RequestWindowRedraw();}
        /// @brief Current overflow handling mode
        TextOverflow overflow() const { return overflow_; }

        // -------------------------------------------------------------------
        //  Self-reported natural size (measurement protocol): measures "how big I originally want to be" from the text
        // -------------------------------------------------------------------
        // @brief Natural width = text width of the longest line + padding + border
        // @note Returns the explicit value when width was set explicitly via `SetSize` (user intent wins, consistent with the effective rule).
        //       For multi-line text containing `\n`, takes the maximum across lines, not the sum.
        float hintWidth() const override;

        /// @brief Natural height = line count x line height + padding + border
        /// @note Line height is taken from `lineHeight` (when 0, uses the line height measured from the font).
        ///       Line count is determined by the number of `\n` in the text (this widget does **no automatic wrapping**).
        float hintHeight() const override;

    protected:
        /// @brief Default box style takes the theme label section (no border, no background, no padding)
        const BoxStyle& themeBox(const Theme& t) const override;
        /// @brief Default text style takes the theme label section
        const TextStyle& themeText(const Theme& t) const override;

        /// @brief Paints: splits lines by '\n' + overflow handling + per-line alignment
        void paintContent(FrameCollector& fc) const override;

        // Does not override clipsContent(): the label's widget-level clipping is **always on** (inherits BoxWidget default true),
        // so the text must never paint outside its own outer border regardless of overflow mode -- that would cover neighboring widgets.
        // overflow only decides "how the content-area layer handles text that does not fit":
        //   Visible -> no truncation, no ellipsis (may exceed the content area), but still clipped within the widget's padding box;
        //   Clip    -> hard clip at the content area edge;
        //   Ellipsis-> truncate + "...".

    private:
        TextOverflow overflow_ = TextOverflow::Ellipsis;
    };

} // namespace Kit
