#include "pch.h"

#include <algorithm>
#include <cctype>
#include <vector>

#include "include/ZeCodeEditor.h"
#include "include/Zekit.h"
#include "ZeFrameCollector.h"

namespace Kit {

    namespace {

        const char* const kCppKeywords[] = {
            "alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand", "bitor",
            "bool", "break", "case", "catch", "char", "char8_t", "char16_t", "char32_t",
            "class", "compl", "concept", "const", "consteval", "constexpr", "constinit",
            "const_cast", "continue", "co_await", "co_return", "co_yield", "decltype",
            "default", "delete", "do", "double", "dynamic_cast", "else", "enum",
            "explicit", "export", "extern", "false", "float", "for", "friend", "goto",
            "if", "inline", "int", "long", "mutable", "namespace", "new", "noexcept",
            "not", "not_eq", "nullptr", "operator", "or", "or_eq", "private", "protected",
            "public", "register", "reinterpret_cast", "requires", "return", "short",
            "signed", "sizeof", "static", "static_assert", "static_cast", "struct",
            "switch", "template", "this", "thread_local", "throw", "true", "try",
            "typedef", "typeid", "typename", "union", "unsigned", "using", "virtual",
            "void", "volatile", "wchar_t", "while", "xor", "xor_eq",
            "include", "define", "ifdef", "ifndef", "endif", "elif", "pragma", "undef"
        };

        bool isKeyword(std::string_view w) {
            for (const char* kw : kCppKeywords) {
                if (w == std::string_view(kw)) return true;
            }
            return false;
        }

        bool isIdentStart(unsigned char c) {
            return std::isalpha(c) || c == '_';
        }
        bool isIdentChar(unsigned char c) {
            return std::isalnum(c) || c == '_';
        }
        bool isDigit(unsigned char c) {
            return std::isdigit(c);
        }

    }

    void CppHighlighter::highlight(std::string_view line,
        std::vector<SyntaxToken>& out) const {
        out.clear();

        const std::size_t n = line.size();
        std::size_t i = 0;

        {
            std::size_t j = 0;
            while (j < n && (line[j] == ' ' || line[j] == '\t')) ++j;
            if (j < n && line[j] == '#') {
                out.push_back({ 0, n, SyntaxTokenKind::Preprocessor });
                return;
            }
        }

        while (i < n) {
            const unsigned char c = static_cast<unsigned char>(line[i]);

            if (c == '/' && i + 1 < n && line[i + 1] == '/') {
                out.push_back({ i, n, SyntaxTokenKind::Comment });
                i = n;
                break;
            }

            if (c == '/' && i + 1 < n && line[i + 1] == '*') {
                std::size_t end = line.find("*/", i + 2);
                if (end == std::string_view::npos) end = n;
                else end += 2;
                out.push_back({ i, end, SyntaxTokenKind::Comment });
                i = end;
                continue;
            }

            if (c == '"' || c == '\'') {
                const char q = static_cast<char>(c);
                std::size_t j = i + 1;
                while (j < n) {
                    if (line[j] == '\\') { j += 2; continue; }
                    if (line[j] == q) { ++j; break; }
                    ++j;
                }
                out.push_back({ i, j, SyntaxTokenKind::String });
                i = j;
                continue;
            }

            if (isDigit(c)) {
                std::size_t j = i;
                while (j < n && (std::isalnum(static_cast<unsigned char>(line[j]))
                    || line[j] == '.' || line[j] == '_')) ++j;
                out.push_back({ i, j, SyntaxTokenKind::Number });
                i = j;
                continue;
            }

            if (isIdentStart(c)) {
                std::size_t j = i;
                while (j < n && isIdentChar(static_cast<unsigned char>(line[j]))) ++j;
                const bool kw = isKeyword(line.substr(i, j - i));
                out.push_back({ i, j, kw ? SyntaxTokenKind::Keyword
                                         : SyntaxTokenKind::Identifier });
                i = j;
                continue;
            }

            ++i;
        }

        std::vector<SyntaxToken> merged;
        merged.reserve(out.size() + 4);
        std::size_t cursor = 0;
        for (const SyntaxToken& t : out) {
            if (t.begin > cursor)
                merged.push_back({ cursor, t.begin, SyntaxTokenKind::Default });
            merged.push_back(t);
            cursor = t.end;
        }
        if (cursor < n)
            merged.push_back({ cursor, n, SyntaxTokenKind::Default });
        out.swap(merged);
    }

    CodeEditor::CodeEditor(std::string placeholder)
        : TextEditBase(std::move(placeholder)) {
        SetDefaultSize(480.0f, 240.0f);

        textStyle_.hAlign = TextHAlign::Left;
        textStyle_.vAlign = TextVAlign::Top;

        tokenColor_[static_cast<int>(SyntaxTokenKind::Default)]      = Color4f::FromHex(0x0F172A);
        tokenColor_[static_cast<int>(SyntaxTokenKind::Keyword)]      = Color4f::FromHex(0x7C3AED);
        tokenColor_[static_cast<int>(SyntaxTokenKind::String)]       = Color4f::FromHex(0x16A34A);
        tokenColor_[static_cast<int>(SyntaxTokenKind::Number)]       = Color4f::FromHex(0xEA580C);
        tokenColor_[static_cast<int>(SyntaxTokenKind::Comment)]      = Color4f::FromHex(0x64748B);
        tokenColor_[static_cast<int>(SyntaxTokenKind::Preprocessor)] = Color4f::FromHex(0x2563EB);
        tokenColor_[static_cast<int>(SyntaxTokenKind::Identifier)]   = Color4f::FromHex(0x0F172A);
    }

    void CodeEditor::SetHighlighter(std::shared_ptr<SyntaxHighlighter> h) {
        highlighter_ = std::move(h);
        RequestWindowRedraw();
    }

    void CodeEditor::SetTokenColor(SyntaxTokenKind kind, const Color4f& c) {
        const int i = static_cast<int>(kind);
        if (i < 0 || i >= static_cast<int>(SyntaxTokenKind::Count)) return;
        tokenColor_[i] = c;
        tokenCustom_[i] = true;
        RequestWindowRedraw();
    }

    void CodeEditor::SetWordWrap(bool on) {
        if (wordWrap_ == on) return;
        wordWrap_ = on;
        markLayoutDirty();
        notifyLayoutInputChanged();
        RequestWindowRedraw();
    }

    void CodeEditor::SetTabWidth(int spaces) {
        tabWidth_ = std::max(1, spaces);
        RequestWindowRedraw();
    }

    void CodeEditor::SetVisibleLines(int lines) {
        visibleLines_ = std::max(1, lines);
        notifyLayoutInputChanged();
        RequestWindowRedraw();
    }

    void CodeEditor::SetAutoHeight(bool on) {
        if (autoHeight_ == on) return;
        autoHeight_ = on;
        notifyLayoutInputChanged();
        RequestWindowRedraw();
    }

    void CodeEditor::ApplyTheme(const Theme& t) {
        TextEditBase::ApplyTheme(t);
        textStyle_.hAlign = TextHAlign::Left;
        textStyle_.vAlign = TextVAlign::Top;

        const Color4f* src[static_cast<int>(SyntaxTokenKind::Count)] = {
            &t.code.tokenDefault,
            &t.code.tokenKeyword,
            &t.code.tokenString,
            &t.code.tokenNumber,
            &t.code.tokenComment,
            &t.code.tokenPreprocessor,
            &t.code.tokenIdentifier,
        };
        for (int i = 0; i < static_cast<int>(SyntaxTokenKind::Count); ++i)
            if (!tokenCustom_[i]) tokenColor_[i] = *src[i];

        memoW_ = -1.0f;
    }

    const BoxStyle& CodeEditor::themeBox(const Theme& t) const {
        return t.code.box;
    }

    const TextStyle& CodeEditor::themeText(const Theme& t) const {
        return t.code.text;
    }

    void CodeEditor::buildLayout(FrameCollector& fc, EditLayout& out) const {
        wrapAll([&fc, this](std::string_view s) {
            return fc.MeasureText(s, textStyle_.font).width; }, out.areaW, out);
    }

    void CodeEditor::wrapAll(const WidthFn& measure, float availW, EditLayout& out) const {
        const std::string_view all(text_);
        const float avail = std::max(0.0f, availW);
        const std::size_t n = all.size();

        std::size_t paraBegin = 0;
        for (;;) {
            const std::size_t nl = all.find('\n', paraBegin);
            const bool hard = (nl != std::string_view::npos);
            const std::size_t paraEnd = hard ? nl : n;
            wrapParagraph(measure, all, paraBegin, paraEnd, hard, avail, out);
            if (!hard) break;
            paraBegin = paraEnd + 1;
            if (paraBegin > n) break;
        }
    }

    void CodeEditor::wrapParagraph(const WidthFn& measure, std::string_view all,
        std::size_t begin, std::size_t end, bool hard,
        float avail, EditLayout& out) const {
        const std::string_view body = all.substr(begin, end - begin);

        std::size_t lineStart = 0;
        bool pushed = false;

        if (body.empty() || !wordWrap_ || avail <= 0.0f) {
            EditLine ln;
            ln.begin = begin;
            ln.end = end;
            ln.hard = hard;
            ln.width = body.empty() ? 0.0f : measure(body);
            out.lines.push_back(ln);
            return;
        }

        const float wholeW = measure(body);
        if (wholeW <= avail) {
            EditLine ln;
            ln.begin = begin;
            ln.end = end;
            ln.hard = hard;
            ln.width = wholeW;
            out.lines.push_back(ln);
            return;
        }

        const std::vector<std::size_t> b = Utf8Boundaries(body);
        const std::size_t count = b.size() - 1;
        std::vector<float> cw(count, 0.0f);
        for (std::size_t i = 0; i < count; ++i)
            cw[i] = measure(body.substr(b[i], b[i + 1] - b[i]));

        auto isSpace = [&](std::size_t i) {
            return (b[i + 1] - b[i] == 1) && body[b[i]] == ' ';
        };

        auto pushLine = [&](std::size_t charEnd, bool isLast) {
            EditLine ln;
            ln.begin = begin + b[lineStart];
            ln.end   = begin + b[charEnd];
            ln.hard  = isLast ? hard : false;
            ln.width = measure(all.substr(ln.begin, ln.end - ln.begin));
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

    float CodeEditor::hintWidth() const {
        if (prefWidth() > 0.0f) return prefWidth();
        constexpr float kDefaultW = 480.0f;
        return kDefaultW
             + box_.padding.left + box_.padding.right
             + box_.border.width[1] + box_.border.width[3];
    }

    float CodeEditor::hintHeight() const {
        if (prefHeight() > 0.0f) return prefHeight();
        const float h = linesHintHeight(visibleLines_);
        return (h > 0.0f) ? h : Widget::hintHeight();
    }

    std::size_t CodeEditor::textHash() const {
        std::size_t h = 1469598103934665603ull;
        for (unsigned char c : text_) {
            h ^= static_cast<std::size_t>(c);
            h *= 1099511628211ull;
        }
        return h ^ text_.size();
    }

    float CodeEditor::hintHeightFor(float availWidth) const {
        if (prefHeight() > 0.0f) return prefHeight();

        Window* win = window();
        if (!win || !autoHeight_ || !wordWrap_ || availWidth <= 0.0f)
            return hintHeight();

        const std::size_t h = textHash();
        const unsigned    px = textStyle_.font.pixelSize;
        const float       lh = textStyle_.lineHeight;
        if (memoW_ == availWidth && memoHash_ == h &&
            memoFontPx_ == px && memoLineH_ == lh)
            return memoH_;

        const float innerW = std::max(0.0f,
            availWidth - box_.padding.left - box_.padding.right
                      - box_.border.width[Border::Left] - box_.border.width[Border::Right]);

        EditLayout tmp;
        wrapAll([win, this](std::string_view s) {
            return win->MeasureText(s, textStyle_.font).width; }, innerW, tmp);

        const TextMetrics probe = win->MeasureText(std::string_view("Ag"), textStyle_.font);
        const float lineH = (textStyle_.lineHeight > 0.0f)
            ? textStyle_.lineHeight : probe.height;

        const float rows = static_cast<float>(std::max<std::size_t>(tmp.lines.size(), 1));
        const float h2 = lineH * rows
             + box_.padding.top + box_.padding.bottom
             + box_.border.width[Border::Top] + box_.border.width[Border::Bottom];

        memoW_ = availWidth; memoHash_ = h; memoFontPx_ = px;
        memoLineH_ = lh; memoH_ = h2;
        return h2;
    }

    void CodeEditor::paintLineToken(FrameCollector& fc, std::string_view body,
        float lx, float ly, const TextMetrics& tm, const ColorSet& cs) const {
        if (!highlighter_) {
            paintTextAt(fc, body, lx, ly, tm, cs);
            return;
        }

        thread_local std::vector<SyntaxToken> tokens;
        highlighter_->highlight(body, tokens);

        float x = lx;
        for (const SyntaxToken& tk : tokens) {
            const std::size_t len = tk.end - tk.begin;
            if (len == 0) continue;
            const std::string_view seg = body.substr(tk.begin, len);

            ColorSet tcs = cs;
            tcs.text = tokenColor_[static_cast<int>(tk.kind)];

            const TextMetrics segTm = fc.MeasureText(seg, textStyle_.font);
            paintTextAt(fc, seg, x, ly, segTm, tcs);
            x += segTm.width;
        }
    }

}
