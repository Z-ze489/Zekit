#include "pch.h"

#ifdef _WIN32
#include <Windows.h>
#include <imm.h>
#pragma comment(lib, "imm32.lib")
#endif

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "include/ZeTextEditBase.h"
#include "include/Zekit.h"
#include "ZeFrameCollector.h"
#include "ZeDrawUtil.h"

namespace {

    std::size_t Utf8CharCount(std::string_view s) {
        std::size_t n = 0;
        for (char ch : s) {
            if ((static_cast<unsigned char>(ch) & 0xC0) != 0x80) ++n;
        }
        return n;
    }

    std::size_t Utf8ByteLimit(std::string_view s, std::size_t maxChars) {
        if (maxChars == 0) return 0;
        std::size_t n = 0;
        for (std::size_t i = 0; i < s.size(); ++i) {
            if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) {
                if (n == maxChars) return i;
                ++n;
            }
        }
        return s.size();
    }
}

namespace Kit {

    namespace {
        constexpr long long kCaretHalfPeriodMs = 530;
        constexpr float     kCaretWidth = 1.5f;

        inline unsigned long long nowMs() {
            return static_cast<unsigned long long>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count());
        }

        std::string CodepointToUtf8(char32_t c) {
            const uint32_t cp = static_cast<uint32_t>(c);
            if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return {};
            std::string out;
            if (cp < 0x80) {
                out.push_back(static_cast<char>(cp));
            } else if (cp < 0x800) {
                out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            } else if (cp < 0x10000) {
                out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            } else {
                out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
            return out;
        }

#ifdef _WIN32
        std::string Utf16ToUtf8(const wchar_t* s, int len) {
            if (!s || len <= 0) return {};
            const int n = WideCharToMultiByte(CP_UTF8, 0, s, len, nullptr, 0, nullptr, nullptr);
            std::string out(n >= 0 ? n : 0, '\0');
            if (n > 0)
                WideCharToMultiByte(CP_UTF8, 0, s, len, out.data(), n, nullptr, nullptr);
            return out;
        }

        [[maybe_unused]]
        std::wstring Utf8ToUtf16(const std::string& s) {
            if (s.empty()) return {};
            const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
            std::wstring out(n > 0 ? n : 0, L'\0');
            if (n > 0)
                MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n);
            return out;
        }
#else
        std::string Utf16ToUtf8(const wchar_t* s, int len) {
            if (!s || len <= 0) return {};
            std::string out;
            out.reserve(static_cast<std::size_t>(len));
            for (int i = 0; i < len; ++i) {
                uint32_t cp = static_cast<uint32_t>(s[i]);
                if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < len) {
                    const uint32_t lo = static_cast<uint32_t>(s[i + 1]);
                    if (lo >= 0xDC00 && lo <= 0xDFFF) {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        ++i;
                    }
                }
                if (cp < 0x80) {
                    out.push_back(static_cast<char>(cp));
                } else if (cp < 0x800) {
                    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                } else if (cp < 0x10000) {
                    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                } else {
                    out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
                    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
                    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                }
            }
            return out;
        }

        [[maybe_unused]]
        std::wstring Utf8ToUtf16(const std::string& s) {
            std::wstring out;
            out.reserve(s.size());
            std::size_t i = 0, n = s.size();
            while (i < n) {
                const unsigned char c = static_cast<unsigned char>(s[i]);
                uint32_t cp = 0;
                std::size_t extra = 0;
                if (c < 0x80) { cp = c;            extra = 0; }
                else if ((c >> 5) == 0x6) { cp = c & 0x1F; extra = 1; }
                else if ((c >> 4) == 0xE) { cp = c & 0x0F; extra = 2; }
                else if ((c >> 3) == 0x1E) { cp = c & 0x07; extra = 3; }
                else { ++i; continue; }
                for (std::size_t k = 1; k <= extra; ++k) {
                    if (i + k >= n) { cp = 0; break; }
                    cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
                }
                i += extra + 1;
                if (cp == 0) continue;
                if (cp < 0x10000) {
                    out.push_back(static_cast<wchar_t>(cp));
                } else {
                    cp -= 0x10000;
                    out.push_back(static_cast<wchar_t>(0xD800 + (cp >> 10)));
                    out.push_back(static_cast<wchar_t>(0xDC00 + (cp & 0x3FF)));
                }
            }
            return out;
        }
#endif

        [[maybe_unused]]
        std::u32string Utf16ToCodepoints(const wchar_t* s, int len) {
            std::u32string out;
            if (!s || len <= 0) return out;
            out.reserve(static_cast<std::size_t>(len));
            for (int i = 0; i < len; ++i) {
                uint32_t cp = static_cast<uint32_t>(s[i]);
                if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < len) {
                    const uint32_t lo = static_cast<uint32_t>(s[i + 1]);
                    if (lo >= 0xDC00 && lo <= 0xDFFF) {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        ++i;
                    }
                }
                if (cp == 0) continue;
                out.push_back(static_cast<char32_t>(cp));
            }
            return out;
        }

        std::size_t snapToBoundary(const std::vector<std::size_t>& b, std::size_t byte) {
            std::size_t best = 0;
            for (std::size_t i = 0; i < b.size(); ++i) {
                if (b[i] <= byte) best = b[i];
                else break;
            }
            return best;
        }
    }

    TextEditBase::TextEditBase(std::string placeholder) : TextWidget(std::string()) {
        ApplyTheme(Themes::Light());
        placeholder_ = std::move(placeholder);
        SetFocusable(true);
        SetCursor(Cursor::Text);
        caretAnchor_ = std::chrono::steady_clock::now();
    }

    const BoxStyle& TextEditBase::themeBox(const Theme& t) const { return t.input.box; }
    const TextStyle& TextEditBase::themeText(const Theme& t) const { return t.input.text; }

    void TextEditBase::SetText(const std::string& text) {
        if (maxLength_ > 0) {
            const std::size_t keep = Utf8ByteLimit(text, maxLength_);
            if (keep < text.size()) {
                TextWidget::SetText(text.substr(0, keep));
            }
            else {
                TextWidget::SetText(text);
            }
        }
        else {
            TextWidget::SetText(text);
        }
        caret_ = text_.size();
        selAnchor_ = caret_;
        cancelVerticalPending();
        resetBlink();
        if (hasFocus()) {
            requestCaretVisible();
        } else {
            scrollX_ = 0.0f;
            scrollY_ = 0.0f;
            ensureCaretVisible_ = false;
        }
        changed();
        RequestWindowRedraw();
    }

    void TextEditBase::changed() {
        layoutDirty_ = true;
        if (onTextChanged_)     onTextChanged_(text_);
        if (selfOnTextChanged_) selfOnTextChanged_(*this, text_);

        RequestWindowRedraw();
    }

    void TextEditBase::Submit() {
        if (onReturn_)     onReturn_(text_);
        if (selfOnReturn_) selfOnReturn_(*this, text_);
    }

    std::size_t TextEditBase::TextLength() const {
        return Utf8CharCount(text_);
    }

    void TextEditBase::insertText(const std::string& utf8) {
        if (readOnly_) return;
        if (utf8.empty()) return;
        cancelVerticalPending();
        deleteSelection();

        const bool keepNewline = acceptsNewline();
        const int  spaces = std::max(1, tabSpaces());
        std::string clean;
        clean.reserve(utf8.size());
        for (std::size_t i = 0; i < utf8.size(); ++i) {
            const unsigned char c = static_cast<unsigned char>(utf8[i]);
            if (c == '\r') {
                if (!keepNewline) { clean.push_back(' '); continue; }
                clean.push_back('\n');
                if (i + 1 < utf8.size() && utf8[i + 1] == '\n') ++i;
                continue;
            }
            if (c == '\n') { clean.push_back(keepNewline ? '\n' : ' '); continue; }
            if (c == '\t') {
                if (!keepNewline) { clean.push_back(' '); continue; }
                clean.append(static_cast<std::size_t>(spaces), ' ');
                continue;
            }
            clean.push_back((c >= 0x20 && c != 0x7F) ? static_cast<char>(c) : ' ');
        }

        if (maxLength_ > 0) {
            const std::size_t cur = Utf8CharCount(text_);
            if (cur >= maxLength_) return;
            const std::size_t room = maxLength_ - cur;
            if (Utf8CharCount(clean) > room) {
                clean.resize(Utf8ByteLimit(clean, room));
                if (clean.empty()) return;
            }
        }

        text_.insert(caret_, clean);
        caret_ += clean.size();
        selAnchor_ = caret_;
        resetBlink();
        requestCaretVisible();
        changed();
    }

    void TextEditBase::deleteSelection() {
        if (readOnly_) return;
        if (!hasSelection()) return;
        const std::size_t lo = std::min(selAnchor_, caret_);
        const std::size_t hi = std::max(selAnchor_, caret_);
        text_.erase(lo, hi - lo);
        caret_ = lo;
        selAnchor_ = lo;
        resetBlink();
        requestCaretVisible();
        changed();
    }

    void TextEditBase::eraseChars(bool before, std::size_t count) {
        if (readOnly_) return;
        if (text_.empty() || count == 0) return;
        if (hasSelection()) { deleteSelection(); return; }
        cancelVerticalPending();
        const std::vector<std::size_t> b = Utf8Boundaries(text_);

        std::size_t pos = 0;
        while (pos < b.size() && b[pos] != caret_) ++pos;
        if (pos >= b.size()) { caret_ = text_.size(); return; }

        if (before) {
            if (pos == 0) return;
            if (count > pos) count = pos;
            const std::size_t from = b[pos - count];
            text_.erase(from, caret_ - from);
            caret_ = from;
        }
        else {
            if (pos + count >= b.size()) count = b.size() - 1 - pos;
            if (count == 0) return;
            text_.erase(caret_, b[pos + count] - caret_);
        }
        selAnchor_ = caret_;
        resetBlink();
        requestCaretVisible();
        changed();
    }

    void TextEditBase::setCaret(std::size_t byte, bool select) {
        cancelVerticalPending();
        const std::vector<std::size_t> b = Utf8Boundaries(text_);
        caret_ = snapToBoundary(b, std::min(byte, text_.size()));
        if (!select) selAnchor_ = caret_;
        resetBlink();
        requestCaretVisible();
    }

    void TextEditBase::SelectAll() {
        if (text_.empty()) return;
        cancelVerticalPending();
        selAnchor_ = 0;
        caret_ = text_.size();
        resetBlink();
        requestCaretVisible();
        RequestWindowRedraw();
    }

    void TextEditBase::ClearSelection() {
        if (!hasSelection()) return;
        selAnchor_ = caret_;
        RequestWindowRedraw();
    }

    std::string TextEditBase::SelectedText() const {
        if (!hasSelection()) return {};
        const std::size_t lo = std::min(selAnchor_, caret_);
        const std::size_t hi = std::max(selAnchor_, caret_);
        return text_.substr(lo, hi - lo);
    }

    bool TextEditBase::Copy() const {
        if (text_.empty()) return false;
        Kit::Window* w = window();
        if (!w) return false;

        if (hasSelection()) {
            w->SetClipboardText(SelectedText());
        } else {
            w->SetClipboardText(text_);
        }
        return true;
    }

    bool TextEditBase::Cut() {
        if (readOnly_) return false;
        if (!hasSelection()) return false;
        if (!Copy()) return false;
        deleteSelection();
        return true;
    }

    bool TextEditBase::Paste() {
        if (readOnly_) return false;
        Kit::Window* w = window();
        if (!w) return false;

        const std::string clip = w->GetClipboardText();
        if (clip.empty()) return false;

        insertText(clip);
        return true;
    }

    float TextEditBase::linePrefixWidth(FrameCollector& fc, const EditLine& line,
        std::size_t byteIdx) const {
        if (byteIdx <= line.begin || text_.empty()) return 0.0f;
        const std::size_t hi = std::min(byteIdx, line.end);
        if (hi <= line.begin) return 0.0f;
        return fc.MeasureText(
            std::string_view(text_).substr(line.begin, hi - line.begin), textStyle_.font).width;
    }

    std::size_t TextEditBase::byteAtXInLine(FrameCollector& fc, const EditLine& line,
        float x) const {
        if (line.end <= line.begin) return line.begin;
        const std::string_view whole(text_);
        const std::string_view body = whole.substr(line.begin, line.end - line.begin);
        const std::vector<std::size_t> b = Utf8Boundaries(body);
        if (b.size() < 2) return line.begin;

        std::size_t lo = 0, hi = b.size() - 1;
        while (lo < hi) {
            const std::size_t mid = lo + (hi - lo + 1) / 2;
            const float w = fc.MeasureText(whole.substr(line.begin, b[mid]), textStyle_.font).width;
            if (w <= x) lo = mid; else hi = mid - 1;
        }
        if (lo + 1 < b.size()) {
            const float wLo = fc.MeasureText(whole.substr(line.begin, b[lo]), textStyle_.font).width;
            const float wHi = fc.MeasureText(whole.substr(line.begin, b[lo + 1]), textStyle_.font).width;
            if (std::fabs(wHi - x) < std::fabs(wLo - x)) ++lo;
        }
        return line.begin + b[lo];
    }

    std::size_t TextEditBase::caretLineIndex() const {
        const auto& L = layout_.lines;
        if (L.empty()) return 0;
        for (std::size_t i = 0; i < L.size(); ++i) {
            if (caret_ < L[i].end) return i;
            if (caret_ == L[i].end) {
                if (i + 1 < L.size() && L[i + 1].begin == L[i].end) return i + 1;
                return i;
            }
        }
        return L.size() - 1;
    }

    std::size_t TextEditBase::pointToCaret(FrameCollector& fc, float localX, float localY) const {
        const auto& L = layout_.lines;
        if (L.empty()) return text_.size();
        float offX = 0.0f, offY = 0.0f;
        blockOffsets(offX, offY);
        (void)offX;

        int idx = 0;
        if (layout_.lineHeight > 0.0f)
            idx = static_cast<int>(std::floor((localY - offY) / layout_.lineHeight));
        if (idx < 0) idx = 0;
        if (idx >= static_cast<int>(L.size())) idx = static_cast<int>(L.size()) - 1;

        {
            const EditLine& ln = L[static_cast<std::size_t>(idx)];
            const float lineOff = drawutil::LineAlignX(textStyle_.hAlign, layout_.areaW, ln.width);
            return byteAtXInLine(fc, ln, localX - lineOff);
        }
    }

    float TextEditBase::linesHintHeight(int visibleLines) const {
        Window* win = window();
        if (!win) return 0.0f;

        const TextMetrics tm = win->MeasureText(std::string_view("Ag"), textStyle_.font);
        const float lineH = (textStyle_.lineHeight > 0.0f) ? textStyle_.lineHeight : tm.height;

        return lineH * static_cast<float>(std::max(1, visibleLines))
            + box_.padding.top + box_.padding.bottom
            + box_.border.width[0] + box_.border.width[2];
    }

    void TextEditBase::blockOffsets(float& offsetX, float& offsetY) const {
        const float restX = layout_.areaW - layout_.maxWidth;
        const float restY = layout_.areaH - layout_.totalH;
        offsetX = (restX > 0.0f) ? alignOffsetX(textStyle_.hAlign, restX) : 0.0f;
        offsetY = (restY > 0.0f) ? alignOffsetY(textStyle_.vAlign, restY) : 0.0f;
    }

    void TextEditBase::ensureLayout(FrameCollector& fc, float cx, float cy,
        float cw, float ch) const {
        const TextMetrics probe = fc.MeasureText(std::string_view("Ag"), textStyle_.font);
        const float effLineH = (textStyle_.lineHeight > 0.0f)
            ? textStyle_.lineHeight : probe.height;

        if (!layoutDirty_ && layout_.valid &&
            layout_.areaW == cw && layout_.areaH == ch &&
            layout_.lineHeight == effLineH) {
            layout_.areaX = cx;
            layout_.areaY = cy;
            return;
        }

        layout_.lines.clear();
        layout_.areaX = cx;
        layout_.areaY = cy;
        layout_.areaW = cw;
        layout_.areaH = ch;
        layout_.lineHeight = effLineH;
        buildLayout(fc, layout_);

        if (layout_.lines.empty())
            layout_.lines.push_back(EditLine{ 0, text_.size(), true, 0.0f, 0.0f });

        float top = 0.0f;
        layout_.maxWidth = 0.0f;
        for (EditLine& ln : layout_.lines) {
            ln.begin = std::min(ln.begin, text_.size());
            ln.end = std::min(std::max(ln.end, ln.begin), text_.size());
            ln.top = top;
            top += layout_.lineHeight;
            layout_.maxWidth = std::max(layout_.maxWidth, ln.width);
        }
        layout_.totalH = top;
        layout_.caretLine = caretLineIndex();
        layout_.valid = true;
        layoutDirty_ = false;
        if (hasFocus()) ensureCaretVisible_ = true;
    }

    void TextEditBase::applyPendingInputs(FrameCollector& fc) const {
        if (hasPress_) {
            hasPress_ = false;
            const std::size_t p = pointToCaret(fc, pressX_, pressY_);
            caret_ = p;
            selAnchor_ = p;
            ensureCaretVisible_ = true;
        }
        if (hasDrag_) {
            hasDrag_ = false;
            caret_ = pointToCaret(fc, dragX_, dragY_);
            ensureCaretVisible_ = true;
        }
        applyPendingVertical(fc);
    }

    void TextEditBase::applyPendingVertical(FrameCollector& fc) const {
        if (pendingVertDelta_ == 0) return;
        const int  delta = pendingVertDelta_;
        const bool select = pendingVertSelect_;
        pendingVertDelta_ = 0;

        const auto& L = layout_.lines;
        if (L.empty()) return;

        float offX = 0.0f, offY = 0.0f;
        blockOffsets(offX, offY);
        (void)offX;

        const int cur = static_cast<int>(caretLineIndex());
        const int last = static_cast<int>(L.size()) - 1;
        int target = cur + delta;
        if (target < 0) target = 0;
        if (target > last) target = last;

        if (target == cur) {
            const EditLine& ln = L[static_cast<std::size_t>(cur)];
            caret_ = (delta < 0) ? ln.begin : ln.end;
        } else {
            const EditLine& ln = L[static_cast<std::size_t>(target)];
            const float lineStartX = layout_.areaX
                + drawutil::LineAlignX(textStyle_.hAlign, layout_.areaW, ln.width) - scrollX_;
            caret_ = byteAtXInLine(fc, ln, vertGoalX_ - lineStartX);
        }
        if (!select) selAnchor_ = caret_;
        ensureCaretVisible_ = true;
    }

    void TextEditBase::clampScroll(FrameCollector& fc) const {
        float offX = 0.0f, offY = 0.0f;
        blockOffsets(offX, offY);
        const float cw = layout_.areaW;
        const float ch = layout_.areaH;

        const float maxY = std::max(0.0f, layout_.totalH + offY - ch);
        const float maxX = std::max(0.0f, layout_.maxWidth + offX - cw);

        if (ensureCaretVisible_) {
            ensureCaretVisible_ = false;
            const std::size_t cl = caretLineIndex();
            const EditLine& ln = layout_.lines[cl];
            const float caretX = linePrefixWidth(fc, ln, caret_);

            if (offY + ln.top - scrollY_ < 0.0f) scrollY_ = offY + ln.top;
            if (offY + ln.top + layout_.lineHeight - scrollY_ > ch)
                scrollY_ = offY + ln.top + layout_.lineHeight - ch;

            if (offX + caretX - scrollX_ < 0.0f) scrollX_ = offX + caretX;
            if (offX + caretX - scrollX_ > cw) scrollX_ = offX + caretX - cw;
        }
        scrollY_ = std::min(std::max(scrollY_, 0.0f), maxY);
        scrollX_ = std::min(std::max(scrollX_, 0.0f), maxX);
    }

    void TextEditBase::paintSelection(FrameCollector& fc,
        float offX, float offY) const {
        (void)offX;
        if (!hasFocus()) return;
        if (!hasSelection() || text_.empty()) return;

        const std::size_t lo = std::min(selAnchor_, caret_);
        const std::size_t hi = std::max(selAnchor_, caret_);
        const float lh = layout_.lineHeight;
        const float baseY = layout_.areaY + offY - scrollY_;
        const Color4f hl(0.15f, 0.45f, 0.95f, 0.24f);

        for (const EditLine& ln : layout_.lines) {
            const float ly = baseY + ln.top;
            if (ly + lh < layout_.areaY || ly > layout_.areaY + layout_.areaH) continue;

            const std::size_t a = std::max(lo, ln.begin);
            const std::size_t b = std::min(hi, ln.end);
            if (b < a) continue;
            const float baseX = layout_.areaX
                + drawutil::LineAlignX(textStyle_.hAlign, layout_.areaW, ln.width) - scrollX_;
            float sx = baseX + linePrefixWidth(fc, ln, a);
            float sw = linePrefixWidth(fc, ln, b) - linePrefixWidth(fc, ln, a);
            if (sw <= 0.0f) {
                if (!(lo <= ln.begin && hi >= ln.end)) continue;
                sw = lh * 0.4f;
            }
            fc.AddRect(sx, ly, sw, lh, hl);
        }
    }

    void TextEditBase::paintLines(FrameCollector& fc, float offX, float offY) const {
        const FontDesc& font = textStyle_.font;
        const Theme& theme = fc.GetTheme();
        (void)offX;
        const float baseY = layout_.areaY + offY - scrollY_;

        const bool showingComposition = imeComposing_ && !imeCompText_.empty();
        if (text_.empty() && !showingComposition) {
            if (placeholder_.empty()) return;
            ColorSet pcs = resolvedTextColors(currentState());
            pcs.text = placeholderColorSet_ ? placeholderColor_ : theme.muted;
            const TextMetrics tm = fc.MeasureText(placeholder_, font);
            const float px = layout_.areaX
                + drawutil::LineAlignX(textStyle_.hAlign, layout_.areaW, tm.width) - scrollX_;
            paintTextAt(fc, placeholder_, px, baseY, tm, pcs);
            return;
        }

        const ColorSet cs = resolvedTextColors(currentState());
        const float viewTop = layout_.areaY;
        const float viewBottom = layout_.areaY + layout_.areaH;
        for (const EditLine& ln : layout_.lines) {
            if (ln.end <= ln.begin) continue;
            const float ly = baseY + ln.top;
            if (ly + layout_.lineHeight < viewTop) continue;
            if (ly > viewBottom) continue;
            const std::string_view body = std::string_view(text_).substr(ln.begin, ln.end - ln.begin);
            const TextMetrics tm = fc.MeasureText(body, font);
            const float lx = layout_.areaX
                + drawutil::LineAlignX(textStyle_.hAlign, layout_.areaW, ln.width) - scrollX_;
            paintLineToken(fc, body, lx, ly, tm, cs);
        }
    }

    void TextEditBase::paintLineToken(FrameCollector& fc, std::string_view body,
        float lx, float ly, const TextMetrics& tm, const ColorSet& cs) const {
        paintTextAt(fc, body, lx, ly, tm, cs);
    }

    void TextEditBase::paintComposition(FrameCollector& fc, float offX, float offY) const {
        (void)offX; (void)offY;
        if (!imeComposing_ || imeCompText_.empty()) return;

        const std::string comp8 = Utf16ToUtf8(imeCompText_.data(), (int)imeCompText_.size());
        if (comp8.empty()) return;
        const TextMetrics cm = fc.MeasureText(comp8, textStyle_.font);
        if (cm.width <= 0.0f) return;

        const ColorSet cs = resolvedTextColors(currentState());
        paintTextAt(fc, comp8, caretPixelX_, caretPixelY_, cm, cs);
        const float baseline = caretPixelY_ + cm.ascent;
        fc.AddRect(caretPixelX_, baseline + cm.descent * 0.25f, cm.width,
            std::max(1.0f, textStyle_.font.pixelSize * 0.08f), cs.text);
    }

    void TextEditBase::paintCaret(FrameCollector& fc, float offX, float offY) const {
        (void)offX; (void)offY;
        const auto now = std::chrono::steady_clock::now();
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - caretAnchor_).count();

        if (Window* w = window()) {
            const long long remain = kCaretHalfPeriodMs - (ms % kCaretHalfPeriodMs);
            w->RequestFrameWake(static_cast<int>(remain));
        }

        const long long phase = ms / kCaretHalfPeriodMs;
        if (phase % 2 != 0) return;

        const Color4f cc = caretColorSet_ ? caretColor_ : fc.GetTheme().primary;
        fc.AddRect(caretPixelX_ - kCaretWidth * 0.5f, caretPixelY_, kCaretWidth,
            std::max(caretLineH_, 1.0f), cc);
    }

    void TextEditBase::paintContent(FrameCollector& fc) const {
        float cx = 0.f, cy = 0.f, cw = 0.f, ch = 0.f;
        contentRect(cx, cy, cw, ch);
        if (cw <= 0.0f || ch <= 0.0f) return;

        ensureLayout(fc, cx, cy, cw, ch);
        applyPendingInputs(fc);
        clampScroll(fc);

        float offX = 0.f, offY = 0.f;
        blockOffsets(offX, offY);

        const EditLine& caretLine = layout_.lines[caretLineIndex()];
        caretPixelX_ = layout_.areaX
            + drawutil::LineAlignX(textStyle_.hAlign, layout_.areaW, caretLine.width)
            + linePrefixWidth(fc, caretLine, caret_) - scrollX_;
        caretPixelY_ = layout_.areaY + offY + caretLine.top - scrollY_;
        caretLineH_ = layout_.lineHeight;

        paintSelection(fc, offX, offY);
        paintLines(fc, offX, offY);
        paintComposition(fc, offX, offY);

        if (!hasFocus()) return;
        updateImeCaretRect();

        if (imeComposing_) return;
        paintCaret(fc, offX, offY);
    }

    bool TextEditBase::OnMousePress(MouseEvent& e) {
        if (!isEnabled()) return false;
        if (e.button != MouseButton::Left) return false;
        float cx = 0.f, cy = 0.f, cw = 0.f, ch = 0.f;
        contentRect(cx, cy, cw, ch);
        pressX_ = e.x - cx + scrollX_;
        pressY_ = e.y - cy + scrollY_;
        hasPress_ = true;
        dragging_ = true;
        cancelVerticalPending();
        return true;
    }

    bool TextEditBase::OnMouseMove(MouseEvent& e) {
        if (!isEnabled() || !dragging_) return false;
        float cx = 0.f, cy = 0.f, cw = 0.f, ch = 0.f;
        contentRect(cx, cy, cw, ch);
        dragX_ = e.x - cx + scrollX_;
        dragY_ = e.y - cy + scrollY_;
        hasDrag_ = true;
        return true;
    }

    bool TextEditBase::OnMouseRelease(MouseEvent& e) {
        (void)e;
        dragging_ = false;
        return true;
    }

    bool TextEditBase::OnMouseWheel(MouseEvent& e) {
        if (!isEnabled()) return false;
        const float maxY = std::max(0.0f, layout_.totalH - layout_.areaH);
        const float maxX = std::max(0.0f, layout_.maxWidth - layout_.areaW);
        if (maxY <= 0.0f && maxX <= 0.0f) return false;

        const float steps = static_cast<float>(e.wheelDelta) / 120.0f;

        if (maxY > 0.0f) {
            const float before = scrollY_;
            const float dy = steps * layout_.lineHeight * 2.0f;
            scrollY_ = std::min(std::max(scrollY_ - dy, 0.0f), maxY);
            if (scrollY_ == before) return false;
        } else if (hasModifier(e.modifiers, KeyboardModifiers::Shift)) {
            const float before = scrollX_;
            scrollX_ = std::min(std::max(scrollX_ - steps * 40.0f, 0.0f), maxX);
            if (scrollX_ == before) return false;
        } else {
            return false;
        }

        ensureCaretVisible_ = false;
        e.accept();
        return true;
    }

    bool TextEditBase::OnKeyPress(KeyEvent& e) {
        if (!hasFocus()) return false;
        if (!isEnabled()) return false;

        if (imeComposing_) {
            if (e.key == Key::Escape) {
                cancelComposition();
                return true;
            }
            return true;
        }

        const bool ctrl = hasModifier(e.modifiers, KeyboardModifiers::Control);
        const bool shift = hasModifier(e.modifiers, KeyboardModifiers::Shift);

        if (ctrl && !e.isRepeat) {
            switch (e.key) {
            case Key::V: Paste(); return true;
            case Key::C: Copy();  return true;
            case Key::X: Cut();   return true;
            case Key::A: SelectAll(); return true;
            default: break;
            }
        }

        switch (e.key) {
        case Key::Backspace:
            if (!readOnly_) eraseChars(true, 1);
            return true;
        case Key::Delete:
            if (!readOnly_) eraseChars(false, 1);
            return true;

        case Key::LeftArrow:
        case Key::RightArrow: {
            const auto b = Utf8Boundaries(text_);
            std::size_t pos = 0;
            while (pos < b.size() && b[pos] != caret_) ++pos;
            if (pos >= b.size()) return true;
            if (e.key == Key::LeftArrow) { if (pos > 0) setCaret(b[pos - 1], shift); }
            else { if (pos + 1 < b.size()) setCaret(b[pos + 1], shift); }
            return true;
        }
        case Key::Home:
        case Key::End: {
            if (layout_.lines.empty()) {
                setCaret(e.key == Key::Home ? 0 : text_.size(), shift);
            } else {
                const EditLine& ln = layout_.lines[caretLineIndex()];
                setCaret(e.key == Key::Home ? ln.begin : ln.end, shift);
            }
            return true;
        }
        case Key::UpArrow:
            return keyVerticalMove(-1, shift);
        case Key::DownArrow:
            return keyVerticalMove(+1, shift);

        case Key::Tab:
            if (ctrl || !acceptsTab()) return false;
            if (readOnly_) return false;
            if (shift) outdentLine(); else insertTabIndent();
            return true;

        case Key::Enter: {
            const bool newline = acceptsNewline() && !ctrl;
            if (newline) {
                if (!readOnly_) insertText("\n");
                return true;
            }
            Submit();
            return true;
        }
        default:
            return false;
        }
    }

    bool TextEditBase::OnCharInput(KeyEvent& e) {
        if (!hasFocus() || !isEnabled() || readOnly_) return false;
        if (imeComposing_) return true;
        const char32_t ch = e.character;
        if (ch < 0x20 || ch == 0x7F) return true;

        if (consumePendingImeChar(ch)) return true;

        const std::string utf8 = CodepointToUtf8(ch);
        if (utf8.empty()) return true;
        insertText(utf8);
        return true;
    }

    void TextEditBase::OnFocusIn() {
        resetBlink();
        requestCaretVisible();
        if (!readOnly_) activateIme();
    }

    void TextEditBase::OnFocusOut() {
        cancelComposition();
    }

    void TextEditBase::cancelVerticalPending() {
        pendingVertDelta_ = 0;
        vertRun_ = false;
    }

    void TextEditBase::insertTabIndent() {
        const int n = std::max(1, tabSpaces());
        insertText(std::string(static_cast<std::size_t>(n), ' '));
    }

    void TextEditBase::outdentLine() {
        const std::size_t nl = (caret_ == 0) ? std::string::npos
            : text_.rfind('\n', caret_ - 1);
        const std::size_t lineBegin = (nl == std::string::npos) ? 0 : nl + 1;
        const std::size_t maxN = static_cast<std::size_t>(std::max(1, tabSpaces()));
        std::size_t remove = 0;
        while (remove < maxN && lineBegin + remove < caret_ &&
            text_[lineBegin + remove] == ' ')
            ++remove;
        if (remove == 0) return;
        text_.erase(lineBegin, remove);
        caret_ -= remove;
        selAnchor_ = caret_;
        resetBlink();
        requestCaretVisible();
        changed();
    }

    bool TextEditBase::beginVerticalMove(int delta, bool select) {
        if (!vertRun_) {
            vertGoalX_ = caretPixelX_;
            vertRun_ = true;
        }
        pendingVertDelta_ += delta;
        pendingVertSelect_ = select;
        return true;
    }

    void TextEditBase::activateIme() {
#ifdef _WIN32
        Window* win = window();
        HWND h = win ? static_cast<HWND>(win->hwnd()) : nullptr;
        if (!h) return;
        HIMC himc = ImmGetContext(h);
        if (himc) {
            ImmAssociateContext(h, himc);
            ImmReleaseContext(h, himc);
        }
#endif
    }

    void TextEditBase::cancelComposition() {
#ifdef _WIN32
        Window* win = window();
        HWND h = win ? static_cast<HWND>(win->hwnd()) : nullptr;
        if (h) {
            HIMC himc = ImmGetContext(h);
            if (himc) {
                ImmNotifyIME(himc, NI_COMPOSITIONSTR, CPS_CANCEL, 0);
                ImmReleaseContext(h, himc);
            }
        }
#endif
        imeComposing_ = false;
        imeCompText_.clear();
        imePending_.clear();
        imePendingMs_ = 0;
        RequestWindowRedraw();
    }

    bool TextEditBase::OnImeStartComposition() {
        if (!hasFocus() || !isEnabled() || readOnly_) return false;
        imeComposing_ = true;
        imeCompText_.clear();
        if (hasSelection()) deleteSelection();
        RequestWindowRedraw();
        return true;
    }

    bool TextEditBase::OnImeComposition(uint32_t flags, uintptr_t lParam) {
        (void)lParam;
        if (!hasFocus() || !isEnabled() || readOnly_) return true;
#ifdef _WIN32
        Window* win = window();
        HWND h = win ? static_cast<HWND>(win->hwnd()) : nullptr;
        HIMC himc = ImmGetContext(h);
        if (!himc) return true;

        if (flags & GCS_COMPSTR) {
            const LONG len = ImmGetCompositionStringW(himc, GCS_COMPSTR, nullptr, 0);
            if (len > 0) {
                imeCompText_.resize(static_cast<size_t>(len) / sizeof(wchar_t));
                ImmGetCompositionStringW(himc, GCS_COMPSTR, &imeCompText_[0], len);
            } else {
                imeCompText_.clear();
            }
            while (!imeCompText_.empty() && imeCompText_.back() == L'\0')
                imeCompText_.pop_back();
        }

        if (flags & GCS_RESULTSTR) {
            const LONG len = ImmGetCompositionStringW(himc, GCS_RESULTSTR, nullptr, 0);
            if (len > 0) {
                std::wstring result(static_cast<size_t>(len) / sizeof(wchar_t), L'\0');
                ImmGetCompositionStringW(himc, GCS_RESULTSTR, &result[0], len);
                while (!result.empty() && result.back() == L'\0')
                    result.pop_back();

                if (!result.empty()) {
                    const unsigned long long t = nowMs();
                    if (!(result == imeLastCommit_ && t - imeLastCommitMs_ < 50)) {
                        imeLastCommit_ = result;
                        imeLastCommitMs_ = t;
                        imePending_ = Utf16ToCodepoints(result.data(), (int)result.size());
                        imePendingMs_ = t;
                        insertText(Utf16ToUtf8(result.data(), (int)result.size()));
                    }
                    imeCompText_.clear();
                }
            }
        }

        ImmReleaseContext(h, himc);
#else
        (void)flags;
#endif
        RequestWindowRedraw();
        return true;
    }

    bool TextEditBase::OnImeEndComposition() {
        imeComposing_ = false;
        imeCompText_.clear();
        RequestWindowRedraw();
        return true;
    }

    bool TextEditBase::OnImeChar(wchar_t ch) {
        if (!hasFocus() || !isEnabled() || readOnly_) return false;
        if (imeComposing_) return true;

        if (consumePendingImeChar(static_cast<char32_t>(ch))) return true;

        insertText(Utf16ToUtf8(&ch, 1));
        return true;
    }

    bool TextEditBase::consumePendingImeChar(char32_t ch) {
        if (imePending_.empty()) return false;
        if (nowMs() - imePendingMs_ >= 500) {
            imePending_.clear();
            return false;
        }
        if (imePending_.front() != ch) {
            imePending_.clear();
            return false;
        }
        imePending_.erase(0, 1);
        if (imePending_.empty()) imePendingMs_ = 0;
        return true;
    }

    void TextEditBase::updateImeCaretRect() const {
#ifdef _WIN32
        Window* win = window();
        if (!win || !win->isWindowActive()) return;
        HWND h = static_cast<HWND>(win->hwnd());
        if (!h) return;
        HIMC himc = ImmGetContext(h);
        if (!himc) return;

        const LONG cxp = static_cast<LONG>(std::llround(caretPixelX_));
        const LONG topY = static_cast<LONG>(std::llround(caretPixelY_));
        const LONG botY = topY + static_cast<LONG>(std::max(caretLineH_, 1.0f));

        COMPOSITIONFORM cf{};
        cf.dwStyle = CFS_POINT;
        cf.ptCurrentPos.x = cxp;
        cf.ptCurrentPos.y = botY;
        ImmSetCompositionWindow(himc, &cf);

        RECT caretRC{ cxp, topY, cxp + 2, botY };
        CANDIDATEFORM cand{};
        cand.dwIndex = 0;
        cand.dwStyle = CFS_EXCLUDE;
        cand.ptCurrentPos.x = cxp;
        cand.ptCurrentPos.y = botY;
        cand.rcArea = caretRC;
        ImmSetCandidateWindow(himc, &cand);

        ImmReleaseContext(h, himc);
#else
        Window* win = window();
        if (!win || !win->isWindowActive()) return;
        win->reportImeSpot(caretPixelX_, caretPixelY_,
            caretPixelY_ + std::max(caretLineH_, 1.0f));
#endif
    }
}
