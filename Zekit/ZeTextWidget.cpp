#include "pch.h"

#include <algorithm>
#include <vector>

#include "include/ZeTextWidget.h"
#include "ZeFrameCollector.h"
#include "ZeDrawUtil.h"

namespace Kit {

    TextWidget::TextWidget(std::string text)
        : text_(std::move(text)) {
    }

    void TextWidget::SetText(const std::string& text) {
        if (isMovedFrom()) diagnoseMovedFrom("SetText");
        if (text_ == text) return;
        text_ = text;

        notifyLayoutInputChanged();
        RequestWindowRedraw();
    }

    const std::string& TextWidget::Text() const {
        if (isMovedFrom()) diagnoseMovedFrom("Text");
        return text_;
    }

    void TextWidget::SetTextAlign(TextHAlign h, TextVAlign v) {
        markStyled();
        textStyle_.hAlign = h;
        textStyle_.vAlign = v;
        RequestWindowRedraw();
    }

    void TextWidget::SetHorizontalAlign(TextHAlign h) {
        markStyled();
        textStyle_.hAlign = h;
        RequestWindowRedraw();
    }

    void TextWidget::SetVerticalAlign(TextVAlign v) {
        markStyled();
        textStyle_.vAlign = v;
        RequestWindowRedraw();
    }

    void TextWidget::SetTextDecoration(TextDecoration d) {
        markStyled();
        textStyle_.decoration = d;
        RequestWindowRedraw();
    }

    void TextWidget::SetLineHeight(float lineHeight) {
        markStyled();
        textStyle_.lineHeight = (lineHeight > 0.0f) ? lineHeight : 0.0f;
        RequestWindowRedraw();
    }

    void TextWidget::SetLetterSpacing(float letterSpacing) {
        markStyled();
        textStyle_.letterSpacing = letterSpacing;
        RequestWindowRedraw();
    }

    void TextWidget::SetFontDesc(const FontDesc& font) {
        markStyled();
        textStyle_.font = font;
        RequestWindowRedraw();
    }

    void TextWidget::SetFontFile(std::string file) {
        markStyled();
        textStyle_.font.file = std::move(file);
        RequestWindowRedraw();
    }

    void TextWidget::SetFontSize(unsigned int pixelSize) {
        markStyled();
        textStyle_.font.pixelSize = (pixelSize > 0u) ? pixelSize : 1u;
        RequestWindowRedraw();
    }

    void TextWidget::SetFontWeight(unsigned short weight) {
        markStyled();
        textStyle_.font.weight = weight;
        RequestWindowRedraw();
    }

    void TextWidget::SetItalic(bool italic) {
        markStyled();
        textStyle_.font.italic = italic;
        RequestWindowRedraw();
    }

    void TextWidget::SetBold(bool bold) {
        markStyled();
        textStyle_.font.SetBold(bold);
        RequestWindowRedraw();
    }

    void TextWidget::SetTextColor(const Color4f& c) {
        markStyled();
        textStyle_.colors.get(WidgetState::Normal).text = c;
        RequestWindowRedraw();
    }
    void TextWidget::SetHoverTextColor(const Color4f& c) {
        markStyled();
        textStyle_.colors.get(WidgetState::Hover).text = c;
        RequestWindowRedraw();
    }
    void TextWidget::SetPressedTextColor(const Color4f& c) {
        markStyled();
        textStyle_.colors.get(WidgetState::Pressed).text = c;
        RequestWindowRedraw();
    }
    void TextWidget::SetDisabledTextColor(const Color4f& c) {
        markStyled();
        textStyle_.colors.get(WidgetState::Disabled).text = c;
        RequestWindowRedraw();
    }

    void TextWidget::SetDecorationColor(const Color4f& c) {
        markStyled();
        textStyle_.colors.get(WidgetState::Normal).decoration = c;
        RequestWindowRedraw();
    }
    void TextWidget::SetHoverDecorationColor(const Color4f& c) {
        markStyled();
        textStyle_.colors.get(WidgetState::Hover).decoration = c;
        RequestWindowRedraw();
    }
    void TextWidget::SetPressedDecorationColor(const Color4f& c) {
        markStyled();
        textStyle_.colors.get(WidgetState::Pressed).decoration = c;
        RequestWindowRedraw();
    }
    void TextWidget::SetDisabledDecorationColor(const Color4f& c) {
        markStyled();
        textStyle_.colors.get(WidgetState::Disabled).decoration = c;
        RequestWindowRedraw();
    }

    const TextStyle& TextWidget::themeText(const Theme& t) const { return t.button.text; }

    void TextWidget::ApplyTheme(const Theme& t) {
        BoxWidget::ApplyTheme(t);
        textStyle_ = themeText(t);
    }

    void TextWidget::contentRect(float& ox, float& oy, float& ow, float& oh) const {
        ox = x() + box_.padding.left;
        oy = y() + box_.padding.top;
        ow = width() - box_.padding.left - box_.padding.right;
        oh = height() - box_.padding.top - box_.padding.bottom;
    }

    float TextWidget::alignOffsetX(TextHAlign a, float rest) {
        return drawutil::AlignOffsetX(a, rest);
    }

    float TextWidget::alignOffsetY(TextVAlign a, float rest) {
        return drawutil::AlignOffsetY(a, rest);
    }

    ColorSet TextWidget::resolvedTextColors(WidgetState s) const {
        ColorSet cs = textStyle_.colors.get(s);
        const float op = box_.opacity;
        if (op < 1.0f) {
            cs.text.a       *= op;
            cs.decoration.a *= op;
        }
        return cs;
    }

    void TextWidget::paintTextAt(FrameCollector& fc, std::string_view line,
        float x, float textTop, const TextMetrics& tm, const ColorSet& cs) const {
        if (line.empty()) return;
        if (tm.width <= 0.0f) return;

        const FontDesc& font = textStyle_.font;

        const float baseline = textTop + tm.ascent;
        fc.AddText(line, font, x, baseline, cs.text);

        const TextDecoration dec = textStyle_.decoration;
        if (dec != TextDecoration::None) {
            const float thick = std::max(1.0f, font.pixelSize * 0.08f);
            float ly = 0.f;
            switch (dec) {
            case TextDecoration::Underline:   ly = baseline + tm.descent * 0.25f; break;
            case TextDecoration::LineThrough: ly = baseline - tm.ascent * 0.45f; break;
            case TextDecoration::Overline:    ly = baseline - tm.ascent - thick;  break;
            default: break;
            }
            fc.AddRect(x, ly, tm.width, thick, cs.decoration);
        }
    }

    void TextWidget::paintTextAt(FrameCollector& fc, float x, float textTop,
        const TextMetrics& tm, const ColorSet& cs) const {
        paintTextAt(fc, text_, x, textTop, tm, cs);
    }

    std::vector<std::size_t> TextWidget::Utf8Boundaries(std::string_view s) {
        return drawutil::Utf8Boundaries(s);
    }

    std::string TextWidget::ellipsizeLine(FrameCollector& fc, std::string_view line,
        const FontDesc& font, float avail) const {
        if (line.empty() || avail <= 0.0f) return std::string();

        static const std::string kEllipsis = "\xE2\x80\xA6";
        const float ew = fc.MeasureText(kEllipsis, font).width;
        if (ew >= avail) return std::string();

        const std::vector<std::size_t> b = Utf8Boundaries(line);
        std::size_t lo = 0, hi = b.size() >= 2 ? b.size() - 2 : 0;

        while (lo < hi) {
            const std::size_t mid = lo + (hi - lo + 1) / 2;
            const float w = fc.MeasureText(line.substr(0, b[mid]), font).width + ew;
            if (w <= avail) lo = mid; else hi = mid - 1;
        }

        std::size_t n = b[lo];
        while (n > 0 && line[n - 1] == ' ') --n;

        std::string out(line.substr(0, n));
        out += kEllipsis;
        return out;
    }

    void TextWidget::paintContent(FrameCollector& fc) const {
        if (text_.empty()) return;

        const TextMetrics tm = fc.MeasureText(text_, textStyle_.font);
        if (tm.width <= 0.0f) return;

        float cx = 0.f, cy = 0.f, cw = 0.f, ch = 0.f;
        contentRect(cx, cy, cw, ch);
        if (cw <= 0.0f || ch <= 0.0f) return;

        const ColorSet cs = resolvedTextColors(currentState());
        const float tx = cx + alignOffsetX(textStyle_.hAlign, cw - tm.width);
        const float ty = cy + alignOffsetY(textStyle_.vAlign, ch - tm.height);
        paintTextAt(fc, tx, ty, tm, cs);
    }
}
