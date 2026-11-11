#include "pch.h"

#include <algorithm>
#include <vector>

#include "include/ZeTextArea.h"
#include "include/Zekit.h"
#include "ZeFrameCollector.h"

namespace Kit {

    TextArea::TextArea(std::string placeholder)
        : TextEditBase(std::move(placeholder)) {
        SetDefaultSize(260.0f, 120.0f);

        SetTextAlign(TextHAlign::Left, TextVAlign::Top);
    }

    void TextArea::ApplyTheme(const Theme& t) {
        TextEditBase::ApplyTheme(t);
        SetTextAlign(TextHAlign::Left, TextVAlign::Top);
    }

    void TextArea::SetWordWrap(bool on) {
        if (wordWrap_ == on) return;
        wordWrap_ = on;
        markLayoutDirty();
        RequestWindowRedraw();
    }

    void TextArea::SetTabWidth(int spaces) {
        tabWidth_ = std::max(1, spaces);
        RequestWindowRedraw();
    }

    void TextArea::SetVisibleLines(int lines) {
        visibleLines_ = std::max(1, lines);
        RequestWindowRedraw();
    }

    void TextArea::buildLayout(FrameCollector& fc, EditLayout& out) const {
        const std::string_view all(text_);
        const float avail = std::max(0.0f, out.areaW);
        const std::size_t n = all.size();

        std::size_t paraBegin = 0;
        for (;;) {
            const std::size_t nl = all.find('\n', paraBegin);
            const bool hard = (nl != std::string_view::npos);
            const std::size_t paraEnd = hard ? nl : n;
            wrapParagraph(fc, all, paraBegin, paraEnd, hard, avail, out);
            if (!hard) break;
            paraBegin = paraEnd + 1;
            if (paraBegin > n) break;
        }
    }

    void TextArea::wrapParagraph(FrameCollector& fc, std::string_view all,
        std::size_t begin, std::size_t end, bool hard,
        float avail, EditLayout& out) const {
        const FontDesc& font = textStyle_.font;
        const std::string_view body = all.substr(begin, end - begin);

        std::size_t lineStart = 0;
        bool pushed = false;

        if (body.empty() || !wordWrap_ || avail <= 0.0f) {
            EditLine ln;
            ln.begin = begin;
            ln.end = end;
            ln.hard = hard;
            ln.width = body.empty() ? 0.0f : fc.MeasureText(body, font).width;
            out.lines.push_back(ln);
            return;
        }

        const std::vector<std::size_t> b = Utf8Boundaries(body);
        const std::size_t count = b.size() - 1;
        std::vector<float> cw(count, 0.0f);
        for (std::size_t i = 0; i < count; ++i)
            cw[i] = fc.MeasureText(body.substr(b[i], b[i + 1] - b[i]), font).width;

        auto isSpace = [&](std::size_t i) {
            return (b[i + 1] - b[i] == 1) && body[b[i]] == ' ';
        };

        auto pushLine = [&](std::size_t charEnd, bool isLast) {
            EditLine ln;
            ln.begin = begin + b[lineStart];
            ln.end   = begin + b[charEnd];
            ln.hard  = isLast ? hard : false;
            ln.width = fc.MeasureText(all.substr(ln.begin, ln.end - ln.begin), font).width;
            out.lines.push_back(ln);
            pushed = true;
        };

        constexpr std::size_t kNone = static_cast<std::size_t>(-1);
        std::size_t lastSpace = kNone;
        float w = 0.0f;

        for (std::size_t i = 0; i < count; ++i) {
            const bool space = isSpace(i);
            if (i > lineStart && w + cw[i] > avail) {
                std::size_t cut = i;
                if (!space && lastSpace != kNone && lastSpace > lineStart)
                    cut = lastSpace + 1;
                if (cut <= lineStart) cut = i;
                pushLine(cut, false);
                lineStart = cut;
                w = 0.0f;
                lastSpace = kNone;
                while (lineStart < count && isSpace(lineStart)) ++lineStart;
                i = (lineStart == 0) ? 0 : lineStart - 1;
                continue;
            }
            w += cw[i];
            if (space) lastSpace = i;
        }

        if (lineStart < count || !pushed) pushLine(count, true);
    }

    float TextArea::hintWidth() const {
        if (prefWidth() > 0.0f) return prefWidth();
        constexpr float kDefaultW = 260.0f;
        return kDefaultW
             + box_.padding.left + box_.padding.right
             + box_.border.width[1] + box_.border.width[3];
    }

    float TextArea::hintHeight() const {
        if (prefHeight() > 0.0f) return prefHeight();
        const float h = linesHintHeight(visibleLines_);
        return (h > 0.0f) ? h : Widget::hintHeight();
    }

}
