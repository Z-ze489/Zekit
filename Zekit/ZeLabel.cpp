#include "pch.h"

#include <algorithm>
#include <vector>

#include "include/ZeLabel.h"
#include "ZeDrawUtil.h"
#include "include/Zekit.h"
#include "ZeFrameCollector.h"

namespace Kit {

    namespace {
        std::vector<std::string_view> SplitLines(std::string_view s) {
            std::vector<std::string_view> out;
            std::size_t start = 0;
            for (;;) {
                const std::size_t nl = s.find('\n', start);
                std::string_view line = (nl == std::string_view::npos)
                    ? s.substr(start)
                    : s.substr(start, nl - start);
                if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
                out.push_back(line);
                if (nl == std::string_view::npos) break;
                start = nl + 1;
            }
            return out;
        }
    }

    Label::Label(std::string text)
        : TextWidget(std::move(text)) {
        ApplyTheme(Themes::Light());

        SetDefaultSize(120.0f, 24.0f);
    }

    const BoxStyle& Label::themeBox(const Theme& t) const { return t.label.box; }
    const TextStyle& Label::themeText(const Theme& t) const { return t.label.text; }

    namespace {
    }

    float Label::hintWidth() const {
        if (prefWidth() > 0.0f) return prefWidth();

        Window* win = window();
        if (!win) return Widget::hintWidth();

        float dx = 0.0f, dy = 0.0f;
        drawutil::BoxInsets(box_, dx, dy);
        (void)dy;

        const FontDesc& font = textStyle_.font;
        float widest = 0.0f;
        for (std::string_view line : SplitLines(text_))
            widest = std::max(widest, win->MeasureText(line, font).width);

        return widest + dx;
    }

    float Label::hintHeight() const {
        if (prefHeight() > 0.0f) return prefHeight();

        Window* win = window();
        if (!win) return Widget::hintHeight();

        float dx = 0.0f, dy = 0.0f;
        drawutil::BoxInsets(box_, dx, dy);
        (void)dx;

        const FontDesc& font = textStyle_.font;
        const std::vector<std::string_view> lines = SplitLines(text_);

        float lineH = textStyle_.lineHeight;
        if (lineH <= 0.0f) {
            for (std::string_view l : lines) {
                lineH = std::max(lineH, win->MeasureText(l, font).height);
            }
        }

        return lineH * static_cast<float>(lines.size()) + dy;
    }

    void Label::paintContent(FrameCollector& fc) const {
        if (text_.empty()) return;

        float cx = 0.0f, cy = 0.0f, cw = 0.0f, ch = 0.0f;
        contentRect(cx, cy, cw, ch);
        if (cw <= 0.0f || ch <= 0.0f) return;

        const FontDesc& font = textStyle_.font;
        const ColorSet cs = resolvedTextColors(currentState());

        const std::vector<std::string_view> lines = SplitLines(text_);
        std::vector<TextMetrics> m;
        m.reserve(lines.size());
        float maxLineH = 0.0f;
        for (std::string_view l : lines) {
            const TextMetrics tm = fc.MeasureText(l, font);
            maxLineH = std::max(maxLineH, tm.height);
            m.push_back(tm);
        }

        const float lineH = (textStyle_.lineHeight > 0.0f) ? textStyle_.lineHeight : maxLineH;
        if (lineH <= 0.0f) return;

        const float blockH = lineH * static_cast<float>(lines.size());
        const float blockY = cy + alignOffsetY(textStyle_.vAlign, ch - blockH);

        const bool needClip = (overflow_ != TextOverflow::Visible);
        if (needClip) fc.PushClip(cx, cy, cw, ch);

        for (std::size_t i = 0; i < lines.size(); ++i) {
            const float ty = blockY + lineH * static_cast<float>(i);
            if (ty >= cy + ch) break;
            if (ty + lineH <= cy) continue;

            std::string cut;
            std::string_view draw = lines[i];
            TextMetrics tm = m[i];

            if (overflow_ == TextOverflow::Ellipsis && tm.width > cw) {
                cut = ellipsizeLine(fc, draw, font, cw);
                draw = cut;
                tm = fc.MeasureText(draw, font);
            }

            const float tx = cx + alignOffsetX(textStyle_.hAlign, cw - tm.width);
            paintTextAt(fc, draw, tx, ty, tm, cs);
        }

        if (needClip) fc.PopClip();
    }
}
