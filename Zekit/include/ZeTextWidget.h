/**
 * @file ZeTextWidget.h
 * @brief Text base class -- the shared layer for widgets that have text in a box
 *
 * @details
 * Adds two things on top of BoxWidget that depend only on text + TextStyle + content rect:
 *
 *  1. Text style and interface: holds a TextStyle and provides all text-related setters
 *     (font / alignment / decoration / line height / letter spacing / four-state text colors).
 *  2. Single-line text layout: Paint inherits BoxWidget's template method,
 *     and overrides paintContent() -- measure -> place in content rect by hAlign/vAlign ->
 *     draw baseline -> decoration line. A widget like Label that is just one line of text
 *     therefore needs no paint code at all.
 *
 * Provides only primitives, does not dictate how content is composed:
 *  - contentRect() gives the content rect (widget rect minus padding);
 *  - alignOffsetX/Y() convert an alignment enum into an offset within the remaining space;
 *  - paintTextAt() draws one line of text at an arbitrary position (including decoration line).
 *  Button uses these three primitives to lay out its icon + spacing + text as a whole
 *  (see Button::paintContent); Input will use them for its cursor and selection.
 *
 * What goes here vs. in each widget (the library's criterion):
 *  - Depends only on rect + BoxStyle + state + TextStyle + text -> base class;
 *  - Once it needs callbacks / icon path / checked state / cursor position -> the widget's own class.
 *    For example the icon block (IconPosition + SetImage family) is currently consumed only by Button,
 *    so it stays in Button; when Label / CheckBox also need icons, promote it here per
 *    "define once library-wide".
 *
 * @note Theme injection: ApplyTheme overrides BoxWidget's version and additionally injects TextStyle;
 *       which section is taken is still answered by themeBox() / themeText() (Label overrides to the label section).
 * @note Value semantics same as BoxWidget: no destructor / copy / move declared, relies on implicit move to move member by member.
 *
 * @see ZeBoxWidget.h box layer
 * @see ZeTextStyle.h text style fields
 */

#pragma once
#include "ZeBoxWidget.h"
#include "ZeTextStyle.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Kit {

    /**
     * @brief What to do when text does not fit the content rect
     *
     * @details
     * Lives in the text layer (not private to Label): Input's placeholder text and TextArea's lines use the same semantics.
     * It only describes what to do when it does not fit, and does not involve automatic wrapping -- wrapping is a layout policy
     * that waits for the measurement protocol (a widget must first be able to answer "how big do I want to be", see 问题.txt),
     * at which point a separate interface will be opened.
     *
     * WARNING: All three modes never draw outside the widget itself: widget-level clipping (to the padding box) is done
     * uniformly by BoxWidget::Paint and is always on, independent of this enum. This enum only governs what this layer does
     * when exceeding the content rect width.
     */
    enum class TextOverflow {
        Visible,   /// < No ellipsis, no hard clip: text may exceed the content rect (keeps drawing up to the padding box, still within the widget border)
        Clip,      /// < Clips the part outside the content rect (hard clip, may cut a glyph in half)
        Ellipsis   /// < Truncates and appends "..." at the end (default)
    };

    /**
     * @brief Text base class: box + single-line text layout capability
     *
     * @details
     * Inheritance chain: Widget -> BoxWidget -> TextWidget -> concrete widget.
     * Button / Label inherit this class directly; Input also inherits this class but lays out its content rect itself
     *  (overrides paintContent or Paint, because the cursor / selection / horizontal scroll positions are not "centered").
     */
    class TextWidget : public BoxWidget {
    public:
        explicit TextWidget(std::string text = std::string());

        // -------------------------------------------------------------------
        //  Text content (data, not style)
        // -------------------------------------------------------------------

        void SetText(const std::string& text);
        const std::string& Text() const;

        // -------------------------------------------------------------------
        //  Style access
        // -------------------------------------------------------------------

        TextStyle& textStyle() { return textStyle_; }
        const TextStyle& textStyle() const { return textStyle_; }

        // -------------------------------------------------------------------
        //  Text layout (alignment / decoration / line height / letter spacing)
        // -------------------------------------------------------------------

        /// @brief Sets horizontal + vertical alignment at once (most common)
        void SetTextAlign(TextHAlign h, TextVAlign v = TextVAlign::Middle);
        /// @brief Sets horizontal alignment only (left / center / right)
        void SetHorizontalAlign(TextHAlign h);
        /// @brief Sets vertical alignment only (top / middle / bottom)
        void SetVerticalAlign(TextVAlign v);
        /// @brief Sets decoration line (underline / strikethrough / overline / none)
        void SetTextDecoration(TextDecoration d);
        /// @brief Sets line height (0 = follow font size automatically)
        void SetLineHeight(float lineHeight);
        /// @brief Sets letter spacing (0 = default; negative values tighten)
        void SetLetterSpacing(float letterSpacing);

        // -------------------------------------------------------------------
        //  Font (one interface per FontDesc field)
        // -------------------------------------------------------------------
        //  FontDesc is the font cache key: changing any field fetches/creates another font resource.
        // -------------------------------------------------------------------

        /// @brief Replaces the whole font description (sets all four fields at once)
        void SetFontDesc(const FontDesc& font);
        /// @brief Sets the font file path (empty = use built-in default font)
        void SetFontFile(std::string file);
        /// @brief Sets the font size (pixels)
        void SetFontSize(unsigned int pixelSize);
        /// @brief Sets the font weight (100~900, see FontWeight constants)
        void SetFontWeight(unsigned short weight);
        /// @brief Sets italic
        void SetItalic(bool italic);
        /// @brief Sets bold (equivalent to SetFontWeight(bold ? 700 : 400))
        void SetBold(bool bold);

        // -------------------------------------------------------------------
        //  Colors (four states x two slots: text / decoration line)
        // -------------------------------------------------------------------
        //  The box's two slots (background / border) are in BoxWidget.
        // -------------------------------------------------------------------

        void SetTextColor(const Color4f& c);            /// < Text color (normal)
        void SetHoverTextColor(const Color4f& c);       /// < Text color (hover)
        void SetPressedTextColor(const Color4f& c);     /// < Text color (pressed)
        void SetDisabledTextColor(const Color4f& c);    /// < Text color (disabled)

        void SetDecorationColor(const Color4f& c);          /// < Decoration line color (normal)
        void SetHoverDecorationColor(const Color4f& c);     /// < Decoration line color (hover)
        void SetPressedDecorationColor(const Color4f& c);   /// < Decoration line color (pressed)
        void SetDisabledDecorationColor(const Color4f& c);  /// < Decoration line color (disabled)

        // -------------------------------------------------------------------
        //  Theme injection
        // -------------------------------------------------------------------

        /// @brief Re-injects box + text style from Theme (overwrites fields manually changed by the user)
        void ApplyTheme(const Theme& t) override;

    protected:
        /// @brief Default content paint: draws one line of text in the content rect (aligned by hAlign/vAlign, including decoration line)
        /// @note Label uses this directly and needs no paint code;
        ///       to lay out combinations like icon + text, override it (see Button::paintContent).
        void paintContent(FrameCollector& fc) const override;

        /// @brief Which section of the theme this widget's default text style comes from
        /// @note Default is t.button.text; Label overrides to t.label.text.
        virtual const TextStyle& themeText(const Theme& t) const;

        // -------------------------------------------------------------------
        //  Layout primitives (used by derived classes to compose content)
        // -------------------------------------------------------------------

        /// @brief Content rect = widget rect minus padding
        /// @param x, y, w, h Output content rect (w/h may be <= 0, caller must check)
        void contentRect(float& x, float& y, float& w, float& h) const;

        /// @brief Returns the text color for a state (text / decoration line slots), with SetOpacity already folded in
        ///
        /// @details
        /// Colors live in two places: the box's two slots (background / border) are in box_.colors, given by
        /// BoxWidget::resolvedColors; the text's two slots (text / decoration line) are in
        /// textStyle_.colors, which is this function.
        /// WARNING: Painting text must use this function -- previously resolvedColors() (the box's copy) was used by mistake,
        /// and ColorSet::text defaults to "opaque black", so SetTextColor / SetHoverTextColor / SetDecorationColor all became dead
        /// interfaces (written but never read), and text was always painted black. Buttons only "looked normal" because the theme
        /// wrote the text color into the box's copy.
        ColorSet resolvedTextColors(WidgetState s) const;

        /// @brief Horizontal alignment conversion: within remaining space rest, Left = 0 / Center = rest/2 / Right = rest
        static float alignOffsetX(TextHAlign a, float rest);
        /// @brief Vertical alignment conversion: within remaining space rest, Top = 0 / Middle = rest/2 / Bottom = rest
        static float alignOffsetY(TextVAlign a, float rest);

        /// @brief Draws one line of text + decoration line at (x, textTop)
        /// @param x Left edge of text; textTop top edge of the text block (baseline = textTop + tm.ascent)
        /// @param tm Text size (given by the caller: Button already measures when laying out icon+text,
        ///           Label measures each line itself, so it should not be recomputed here)
        /// @param cs Color set with opacity already folded (takes .text / .decoration slots)
        /// @note Single line only: no wrapping -- multi-line callers call this per line.
        void paintTextAt(FrameCollector& fc, float x, float textTop,
            const TextMetrics& tm, const ColorSet& cs) const;

        /// @brief Draws one line of the specified text (not member text_) -- used after multi-line layout / ellipsis truncation
        /// @param line Content to draw (caller guarantees a complete UTF-8 string)
        /// @param tm   Size of line (not the size of member text_)
        void paintTextAt(FrameCollector& fc, std::string_view line,
            float x, float textTop, const TextMetrics& tm, const ColorSet& cs) const;

        /// @brief Collects UTF-8 character boundary indices: 0, start of each character..., size()
        /// @note Per-character operations (ellipsis truncation / InputBox cursor movement and deletion) rely on it --
        ///       splitting by byte would cut Chinese into half a character (render layer gets an invalid sequence = tofu blocks).
        static std::vector<std::size_t> Utf8Boundaries(std::string_view s);

        /// @brief Truncates a line to the available width and appends "..." (U+2026) at the end
        /// @param line Original full line (UTF-8)
        /// @param avail Available width (content rect width)
        /// @return Truncated string (including ellipsis); returns empty string if even the ellipsis does not fit
        /// @note Truncates only at character boundaries -- Chinese/emoji must never be cut mid-byte;
        ///       trailing spaces are eaten (to avoid "abc ...").
        std::string ellipsizeLine(FrameCollector& fc, std::string_view line,
            const FontDesc& font, float avail) const;

        std::string text_;        /// < Text content
        TextStyle   textStyle_;   /// < Text style
    };

} // namespace Kit
