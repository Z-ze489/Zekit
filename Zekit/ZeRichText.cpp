#include "pch.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "include/ZeRichText.h"
#include "include/Zekit.h"
#include "ZeDrawUtil.h"
#include "include/ZeTheme.h"
#include "ZeDrawUtil.h"
#include "ZeFrameCollector.h"

namespace Kit {

    namespace {
        inline unsigned DecodeUtf8(std::string_view s, std::size_t i, std::size_t& len) {
            const unsigned char c = static_cast<unsigned char>(s[i]);
            if (c < 0x80u) { len = 1; return c; }
            if ((c & 0xE0u) == 0xC0u && i + 1 < s.size()) {
                len = 2;
                return ((c & 0x1Fu) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3Fu);
            }
            if ((c & 0xF0u) == 0xE0u && i + 2 < s.size()) {
                len = 3;
                return ((c & 0x0Fu) << 12)
                     | ((static_cast<unsigned char>(s[i + 1]) & 0x3Fu) << 6)
                     | (static_cast<unsigned char>(s[i + 2]) & 0x3Fu);
            }
            if ((c & 0xF8u) == 0xF0u && i + 3 < s.size()) {
                len = 4;
                return ((c & 0x07u) << 18)
                     | ((static_cast<unsigned char>(s[i + 1]) & 0x3Fu) << 12)
                     | ((static_cast<unsigned char>(s[i + 2]) & 0x3Fu) << 6)
                     | (static_cast<unsigned char>(s[i + 3]) & 0x3Fu);
            }
            len = 1; return c;
        }

        inline bool IsSpaceCp(unsigned cp) {
            return cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r'
                || cp == 0x000Bu || cp == 0x000Cu || cp == 0x3000u;
        }

        inline bool IsWideCp(unsigned cp) {
            return (cp >= 0x1100u && cp <= 0x115Fu)
                || (cp >= 0x2E80u && cp <= 0x303Eu)
                || (cp >= 0x3041u && cp <= 0x33FFu)
                || (cp >= 0x3400u && cp <= 0x4DBFu)
                || (cp >= 0x4E00u && cp <= 0x9FFFu)
                || (cp >= 0xA000u && cp <= 0xA4CFu)
                || (cp >= 0xAC00u && cp <= 0xD7A3u)
                || (cp >= 0xF900u && cp <= 0xFAFFu)
                || (cp >= 0xFE30u && cp <= 0xFE4Fu)
                || (cp >= 0xFF00u && cp <= 0xFF60u)
                || (cp >= 0xFFE0u && cp <= 0xFFE6u)
                || (cp >= 0x20000u && cp <= 0x3FFFBu);
        }

        inline bool IsWordByte(unsigned char b) {
            return (b >= '0' && b <= '9') || (b >= 'A' && b <= 'Z') || (b >= 'a' && b <= 'z');
        }

        inline float AlignOffset(TextHAlign a, float rest) {
            if (rest <= 0.0f) return 0.0f;
            return drawutil::AlignOffsetX(a, rest);
        }

        template <class Measure>
        float PrefixWidth(Measure measure, std::string_view text,
                          std::size_t n, const FontDesc& font) {
            if (text.empty() || n == 0) return 0.0f;
            if (n >= text.size()) return measure(text, font).width;
            return measure(text.substr(0, n), font).width;
        }

        template <class Measure>
        std::size_t BoundaryAtX(Measure measure, std::string_view text,
                                const FontDesc& font, float x) {
            if (text.empty() || x <= 0.0f) return 0;
            if (x >= measure(text, font).width) return text.size();

            const std::vector<std::size_t> b = drawutil::Utf8Boundaries(text);

            std::size_t lo = 0, hi = b.size() - 1;
            while (lo < hi) {
                const std::size_t mid = lo + (hi - lo + 1) / 2;
                if (PrefixWidth(measure, text, b[mid], font) <= x) lo = mid;
                else hi = mid - 1;
            }

            if (lo + 1 < b.size()) {
                const float wLo = PrefixWidth(measure, text, b[lo], font);
                const float wHi = PrefixWidth(measure, text, b[lo + 1], font);
                if ((wHi - x) < (x - wLo)) return b[lo + 1];
            }
            return b[lo];
        }
    }

    RichText::RichText() {
        ApplyTheme(Themes::Light());
        SetDefaultSize(240.0f, 24.0f);
        SetSelectable(true);
        SetCursor(Cursor::Text);
    }

    RichText::RichText(std::string text) : RichText() {
        if (!text.empty()) AddText(std::move(text));
    }

    void RichText::SetRuns(std::vector<RichRun> runs) {
        runs_ = std::move(runs);
        invalidate();
        RequestWindowRedraw();
    }

    RichText& RichText::AddRun(RichRun r) {
        if (!r.text.empty()) runs_.push_back(std::move(r));
        invalidate();
        return *this;
    }

    RichText& RichText::AddText(std::string text, unsigned flags, std::string href) {
        return AddRun(RichRun{ std::move(text), flags, std::move(href) });
    }

    void RichText::Clear() {
        runs_.clear();
        hoverRun_ = -1;
        invalidate();
    }

    bool RichText::empty() const { return runs_.empty(); }

    void RichText::invalidate() {
        cacheDirty_ = true;
        flatDirty_ = true;
        rebuildFlat();
        caret_ = std::min(caret_, flat_.size());
        selAnchor_ = std::min(selAnchor_, flat_.size());
        notifyLayoutInputChanged();
    }

    void RichText::rebuildFlat() const {
        if (!flatDirty_) return;
        flat_.clear();
        runStart_.assign(runs_.size() + 1, 0);
        for (std::size_t i = 0; i < runs_.size(); ++i) {
            runStart_[i] = flat_.size();
            flat_ += runs_[i].text;
        }
        runStart_[runs_.size()] = flat_.size();
        flatDirty_ = false;
    }

    std::size_t RichText::prevCharPos(std::size_t flat) const {
        rebuildFlat();
        if (flat == 0 || flat_.empty()) return 0;
        if (flat > flat_.size()) flat = flat_.size();
        std::size_t i = flat - 1;
        while (i > 0 && (static_cast<unsigned char>(flat_[i]) & 0xC0u) == 0x80u) --i;
        return i;
    }

    std::size_t RichText::nextCharPos(std::size_t flat) const {
        rebuildFlat();
        if (flat >= flat_.size()) return flat_.size();
        std::size_t len = 1;
        DecodeUtf8(flat_, flat, len);
        return std::min(flat + len, flat_.size());
    }

    void RichText::SetLineSpacing(float gap) {
        lineSpacing_ = (gap > 0.0f) ? gap : 0.0f;
        invalidate();
        RequestWindowRedraw();
    }

    void RichText::SetHorizontalAlign(TextHAlign a) {
        if (hAlign_ == a) return;
        hAlign_ = a;
        RequestWindowRedraw();
        RequestWindowRedraw();
    }

    void RichText::SetBaseFontDesc(const FontDesc& f) {
        baseFont_ = f;
        hasBaseFont_ = true;
        invalidate();
        RequestWindowRedraw();
    }

    void RichText::SetBaseFontSize(unsigned px) {
        baseFont_.pixelSize = (px > 0u) ? px : 1u;
        hasBaseFont_ = true;
        invalidate();
        RequestWindowRedraw();
    }

    void RichText::SetMonoFontFile(std::string file) {
        monoFile_ = std::move(file);
        invalidate();
        RequestWindowRedraw();
    }

    void RichText::SetMonoFontScale(float scale) {
        monoScale_ = (scale > 0.0f) ? scale : 1.0f;
        invalidate();
        RequestWindowRedraw();
    }

    void RichText::SetTextColor(const Color4f& c) { hasText_ = true; textColor_ = c; RequestWindowRedraw(); }
    void RichText::SetCodeTextColor(const Color4f& c) { hasCodeText_ = true; codeTextColor_ = c; RequestWindowRedraw(); }
    void RichText::SetCodeBackgroundColor(const Color4f& c) { hasCodeBg_ = true; codeBgColor_ = c; RequestWindowRedraw(); }
    void RichText::SetLinkColor(const Color4f& c) { hasLink_ = true; linkColor_ = c; RequestWindowRedraw(); }
    void RichText::SetLinkHoverColor(const Color4f& c) { hasLinkHover_ = true; linkHoverColor_ = c; RequestWindowRedraw(); }

    void RichText::SetCodeBackgroundPadding(float padX) {
        codeBgPadX_ = (padX > 0.0f) ? padX : 0.0f;
        RequestWindowRedraw();
    }

    RichText::PieceStyle RichText::resolveStyle(const RichRun& run) const {
        PieceStyle st;
        if (HasRichFlag(run.flags, RichFlag::Code)) {
            st.isCode = true;
            st.font = baseFont_;
            if (!monoFile_.empty()) st.font.file = monoFile_;
            st.font.italic = false;
            st.font.weight = FontWeight::Normal;
            st.font.pixelSize = static_cast<unsigned>(
                std::max(1.0f, static_cast<float>(baseFont_.pixelSize) * monoScale_ + 0.5f));
        }
        else {
            st.font = baseFont_;
            if (HasRichFlag(run.flags, RichFlag::Bold))   st.font.weight = FontWeight::Bold;
            if (HasRichFlag(run.flags, RichFlag::Italic)) st.font.italic = true;
        }

        if (HasRichFlag(run.flags, RichFlag::Link))   st.isLink = true;
        if (HasRichFlag(run.flags, RichFlag::Strike)) st.decoration = TextDecoration::LineThrough;
        else if (HasRichFlag(run.flags, RichFlag::Underline)) st.decoration = TextDecoration::Underline;
        if (st.isLink) st.decoration = TextDecoration::Underline;

        return st;
    }

    RichText::Palette RichText::effectivePalette() const {
        Palette p = themePalette_;
        if (hasText_)      p.text = textColor_;
        if (hasCodeText_)  p.codeText = codeTextColor_;
        if (hasCodeBg_)    p.codeBg = codeBgColor_;
        if (hasLink_)      p.link = linkColor_;
        if (hasLinkHover_) p.linkHover = linkHoverColor_;
        return p;
    }

    Color4f RichText::pieceColor(const PieceStyle& st, const Palette& pal, bool hovered) const {
        if (st.isCode) return pal.codeText;
        if (st.isLink) return hovered ? pal.linkHover : pal.link;
        return pal.text;
    }

    template <class Measure>
    void RichText::buildLines(Measure measure, const Palette& pal, float avail,
        std::vector<Line>& out) const {
        (void)pal;
        out.clear();
        if (runs_.empty() || avail <= 0.0f) return;

        Line cur;
        float curRight = 0.0f;
        float accTop = 0.0f;

        auto flush = [&]() {
            if (cur.pieces.empty()) return;
            float asc = 0.0f, hgt = 0.0f, right = 0.0f;
            for (const Piece& p : cur.pieces) {
                asc = std::max(asc, p.tm.ascent);
                hgt = std::max(hgt, p.tm.height);
                right = std::max(right, p.x + p.tm.width);
            }
            cur.ascent = asc;
            cur.height = hgt;
            cur.width = right;
            cur.top = accTop;
            accTop += hgt + lineSpacing_;
            out.push_back(std::move(cur));
            cur = Line{};
            curRight = 0.0f;
        };

        auto put = [&](const RichRun& run, const PieceStyle& st,
            std::string_view t, const TextMetrics& tm, std::size_t flatPos) {
            Piece p;
            p.run = &run;
            p.text.assign(t);
            p.font = st.font;
            p.tm = tm;
            p.x = curRight;
            p.flat = flatPos;
            curRight += tm.width;
            cur.pieces.push_back(std::move(p));
        };

        rebuildFlat();

        for (std::size_t ri = 0; ri < runs_.size(); ++ri) {
            const RichRun& run = runs_[ri];
            if (run.text.empty()) continue;
            const PieceStyle st = resolveStyle(run);
            const std::string& s = run.text;
            const std::size_t n = s.size();
            const std::string_view sv(s);
            const std::size_t runBase = runStart_[ri];

            std::size_t i = 0;
            while (i < n) {
                std::size_t len = 1;
                const unsigned cp = DecodeUtf8(sv, i, len);
                const std::size_t b = i;
                std::size_t e = i + len;
                const bool spaceAtom = IsSpaceCp(cp);

                if (cp == '\n') { i = e; flush(); continue; }

                if (!spaceAtom && !IsWideCp(cp)) {
                    std::size_t j = e;
                    while (j < n) {
                        std::size_t l2 = 1;
                        const unsigned c2 = DecodeUtf8(sv, j, l2);
                        if (IsSpaceCp(c2) || IsWideCp(c2)) break;
                        j += l2;
                    }
                    if (!IsWordByte(static_cast<unsigned char>(s[b]))) j = e;
                    e = j;
                }
                i = e;

                if (spaceAtom && cur.pieces.empty()) continue;

                const std::string_view t = sv.substr(b, e - b);
                const TextMetrics tm = measure(t, st.font);
                if (tm.width <= 0.0f) continue;

                if (!cur.pieces.empty() && curRight + tm.width > avail) {
                    if (spaceAtom) continue;
                    flush();
                }

                if (tm.width > avail) {
                    std::size_t cb = b;
                    while (cb < e) {
                        std::size_t cl = 1;
                        DecodeUtf8(sv, cb, cl);
                        const std::string_view ch = sv.substr(cb, cl);
                        const TextMetrics cm = measure(ch, st.font);
                        if (!cur.pieces.empty() && curRight + cm.width > avail) flush();
                        put(run, st, ch, cm, runBase + cb);
                        cb += cl;
                    }
                    continue;
                }

                put(run, st, t, tm, runBase + b);
            }
        }

        flush();
    }

    void RichText::ensureLines(float avail) const {
        if (!cacheDirty_ && cachedAvail_ == avail) return;
        lines_.clear();
        cachedAvail_ = avail;
        cacheDirty_ = false;

        Window* win = window();
        if (!win || avail <= 0.0f) return;

        const Palette pal = effectivePalette();
        buildLines(
            [win](std::string_view s, const FontDesc& f) { return win->MeasureText(s, f); },
            pal, avail, lines_);
    }

    void RichText::contentBox(float& cx, float& cy, float& cw, float& ch) const {
        const LayoutPadding& p = padding();
        cx = x() + p.left;
        cy = y() + p.top;
        cw = width() - p.left - p.right;
        ch = height() - p.top - p.bottom;
    }

    float RichText::hintWidth() const {
        if (prefWidth() > 0.0f) return prefWidth();

        Window* win = window();
        if (!win) return defaultWidth();

        const LayoutPadding& p = padding();
        float w = 0.0f;
        for (const RichRun& r : runs_) {
            if (r.text.empty()) continue;
            w += win->MeasureText(r.text, resolveStyle(r).font).width;
        }
        if (w <= 0.0f) return defaultWidth();
        return w + p.left + p.right;
    }

    float RichText::hintHeight() const {
        if (prefHeight() > 0.0f) return prefHeight();

        Window* win = window();
        if (!win) return defaultHeight();

        const LayoutPadding& p = padding();
        float h = 0.0f;
        for (const RichRun& r : runs_) {
            if (r.text.empty()) continue;
            h = std::max(h, win->MeasureText(r.text, resolveStyle(r).font).height);
        }
        if (h <= 0.0f) return defaultHeight();
        return h + p.top + p.bottom;
    }

    float RichText::hintHeightFor(float availWidth) const {
        if (prefHeight() > 0.0f) return prefHeight();

        Window* win = window();
        if (!win || availWidth <= 0.0f) return defaultHeight();

        const LayoutPadding& p = padding();
        const Palette pal = effectivePalette();

        std::vector<Line> tmp;
        buildLines(
            [win](std::string_view s, const FontDesc& f) { return win->MeasureText(s, f); },
            pal, availWidth, tmp);
        if (tmp.empty()) return defaultHeight();

        return (tmp.back().top + tmp.back().height) + p.top + p.bottom;
    }

    void RichText::ApplyTheme(const Theme& t) {
        Panel::ApplyTheme(t);

        if (!hasBaseFont_)
            baseFont_ = t.label.text.font;

        themePalette_.text = t.text;
        themePalette_.codeText = t.text;
        themePalette_.codeBg = t.border.withAlpha(0.55f);
        themePalette_.link = t.primary;
        themePalette_.linkHover = t.primaryHover;
        selColor_ = t.primary.withAlpha(0.28f);

        invalidate();
    }

    void RichText::Paint(FrameCollector& fc) const {
        Panel::Paint(fc);

        float cx = 0.0f, cy = 0.0f, cw = 0.0f, ch = 0.0f;
        contentBox(cx, cy, cw, ch);
        if (runs_.empty() || cw <= 0.0f || ch <= 0.0f) return;

        ensureLines(cw);
        if (lines_.empty()) return;

        const Palette pal = effectivePalette();

        fc.PushClip(x(), y(), width(), height());

        for (const Line& ln : lines_) {
            if (ln.pieces.empty()) continue;

            const float lineTop = cy + ln.top;
            if (lineTop >= cy + ch) break;
            if (lineTop + ln.height <= cy) continue;

            const float offX = AlignOffset(hAlign_, cw - ln.width);
            const float baseline = lineTop + ln.ascent;

            if (hasFocus() && selAnchor_ != caret_) {
                const std::size_t selLo = std::min(selAnchor_, caret_);
                const std::size_t selHi = std::max(selAnchor_, caret_);
                const auto measure = [&fc](std::string_view s, const FontDesc& f) {
                    return fc.MeasureText(s, f);
                };
                for (const Piece& pc : ln.pieces) {
                    const std::size_t pLo = (selLo > pc.flat) ? selLo : pc.flat;
                    const std::size_t pEnd = pc.flat + pc.text.size();
                    const std::size_t pHi = (selHi < pEnd) ? selHi : pEnd;
                    if (pLo >= pHi) continue;

                    const float x0 = cx + offX + pc.x
                        + PrefixWidth(measure, pc.text, pLo - pc.flat, pc.font);
                    const float x1 = cx + offX + pc.x
                        + PrefixWidth(measure, pc.text, pHi - pc.flat, pc.font);
                    if (x1 > x0)
                        fc.AddRect(x0, lineTop, x1 - x0, ln.height, selColor_);
                }
            }

            for (const Piece& pc : ln.pieces) {
                const PieceStyle st = resolveStyle(*pc.run);
                const bool hovered = (hoverRun_ >= 0
                    && pc.run == &runs_[static_cast<std::size_t>(hoverRun_)]);
                const Color4f col = pieceColor(st, pal, hovered);
                const float px = cx + offX + pc.x;

                if (st.isCode && pal.codeBg.a > 0.0f && pc.tm.width > 0.0f) {
                    const float by = lineTop + (ln.ascent - pc.tm.ascent);
                    drawutil::PaintRoundedRect(fc,
                        px - codeBgPadX_, by,
                        pc.tm.width + codeBgPadX_ * 2.0f, pc.tm.height,
                        3.0f, pal.codeBg);
                }

                if (pc.tm.width > 0.0f)
                    fc.AddText(pc.text, st.font, px, baseline, col);

                if (st.decoration != TextDecoration::None && pc.tm.width > 0.0f) {
                    const float thick = std::max(1.0f, static_cast<float>(st.font.pixelSize) * 0.08f);
                    float ly = 0.0f;
                    switch (st.decoration) {
                    case TextDecoration::Underline:   ly = baseline + pc.tm.descent * 0.25f; break;
                    case TextDecoration::LineThrough: ly = baseline - pc.tm.ascent * 0.45f; break;
                    case TextDecoration::Overline:    ly = baseline - pc.tm.ascent - thick;  break;
                    default: break;
                    }
                    fc.AddRect(px, ly, pc.tm.width, thick, col);
                }
            }
        }

        fc.PopClip();
    }

    int RichText::hitTestLink(float lx, float ly) const {
        if (lines_.empty() || cachedAvail_ <= 0.0f) return -1;

        for (const Line& ln : lines_) {
            if (ly < ln.top || ly >= ln.top + ln.height) continue;

            const float offX = AlignOffset(hAlign_, cachedAvail_ - ln.width);
            for (const Piece& p : ln.pieces) {
                if (!p.run || !HasRichFlag(p.run->flags, RichFlag::Link)) continue;
                const float x0 = offX + p.x;
                const float x1 = x0 + p.tm.width;
                if (lx >= x0 && lx <= x1)
                    return static_cast<int>(p.run - runs_.data());
            }
        }
        return -1;
    }

    void RichText::SetSelectable(bool on) {
        selectable_ = on;
        SetFocusable(on);
        if (!on) selAnchor_ = caret_;
        RequestWindowRedraw();
    }

    std::string RichText::SelectedText() const {
        rebuildFlat();
        if (selAnchor_ == caret_ || flat_.empty()) return std::string();
        const std::size_t lo = std::min(selAnchor_, caret_);
        const std::size_t hi = std::max(selAnchor_, caret_);
        if (lo >= flat_.size()) return std::string();
        return flat_.substr(lo, std::min(hi, flat_.size()) - lo);
    }

    void RichText::SelectAll() {
        rebuildFlat();
        if (flat_.empty()) return;
        selAnchor_ = 0;
        caret_ = flat_.size();
        RequestWindowRedraw();
    }

    void RichText::ClearSelection() {
        if (selAnchor_ == caret_) return;
        selAnchor_ = caret_;
        RequestWindowRedraw();
    }

    bool RichText::Copy() const {
        rebuildFlat();
        if (flat_.empty()) return false;

        Window* w = window();
        if (!w) return false;

        if (selAnchor_ != caret_) {
            w->SetClipboardText(SelectedText());
        } else {
            w->SetClipboardText(flat_);
        }
        return true;
    }

    std::size_t RichText::hitTestFlat(float lx, float ly) const {
        rebuildFlat();
        if (lines_.empty() || cachedAvail_ <= 0.0f) return 0;

        Window* win = window();
        if (!win) return 0;
        const auto measure = [win](std::string_view s, const FontDesc& f) {
            return win->MeasureText(s, f);
        };

        if (ly < lines_.front().top) return 0;
        const Line& last = lines_.back();
        if (ly >= last.top + last.height) return flat_.size();

        for (const Line& ln : lines_) {
            if (ly < ln.top || ly >= ln.top + ln.height) continue;
            if (ln.pieces.empty()) return 0;

            const float offX = AlignOffset(hAlign_, cachedAvail_ - ln.width);
            const float lx2 = lx - offX;

            if (lx2 <= ln.pieces.front().x) return ln.pieces.front().flat;

            for (const Piece& p : ln.pieces) {
                const float x1 = p.x + p.tm.width;
                if (lx2 < x1 || &p == &ln.pieces.back()) {
                    const std::size_t n = BoundaryAtX(measure, p.text, p.font, lx2 - p.x);
                    return p.flat + n;
                }
            }
            return ln.pieces.back().flat + ln.pieces.back().text.size();
        }
        return flat_.size();
    }

    bool RichText::OnMouseMove(MouseEvent& e) {
        if (dragging_ && selectable_ && isEnabled()) {
            float cx = 0.0f, cy = 0.0f, cw = 0.0f, ch = 0.0f;
            contentBox(cx, cy, cw, ch);
            if (cw > 0.0f && ch > 0.0f) {
                ensureLines(cw);
                caret_ = hitTestFlat(e.x - cx, e.y - cy);
                RequestWindowRedraw();
            }
            return true;
        }

        float cx = 0.0f, cy = 0.0f, cw = 0.0f, ch = 0.0f;
        contentBox(cx, cy, cw, ch);
        if (cw <= 0.0f || ch <= 0.0f) return false;

        ensureLines(cw);
        const int hit = hitTestLink(e.x - cx, e.y - cy);
        if (hit != hoverRun_) {
            hoverRun_ = hit;
            RequestWindowRedraw();
        }
        return false;
    }

    bool RichText::OnMousePress(MouseEvent& e) {
        if (e.button != MouseButton::Left || !isEnabled()) return false;

        float cx = 0.0f, cy = 0.0f, cw = 0.0f, ch = 0.0f;
        contentBox(cx, cy, cw, ch);
        if (cw <= 0.0f || ch <= 0.0f) return false;

        ensureLines(cw);

        const int link = hitTestLink(e.x - cx, e.y - cy);
        if (link >= 0) {
            if (onLinkClick_) {
                const std::string href = runs_[static_cast<std::size_t>(link)].href;
                onLinkClick_(*this, href);
            }
            pressConsumed_ = true;
            return true;
        }

        if (!selectable_) { pressConsumed_ = false; return false; }
        const std::size_t flat = hitTestFlat(e.x - cx, e.y - cy);
        caret_ = flat;
        selAnchor_ = flat;
        dragging_ = true;
        pressConsumed_ = true;
        RequestWindowRedraw();
        return true;
    }

    bool RichText::OnMouseRelease(MouseEvent& e) {
        (void)e;
        const bool consumed = pressConsumed_;
        pressConsumed_ = false;
        dragging_ = false;
        return consumed;
    }

    void RichText::OnMouseLeave() {
        if (hoverRun_ != -1) {
            hoverRun_ = -1;
            RequestWindowRedraw();
        }
    }

    void RichText::OnFocusOut() {
        ClearSelection();
        if (hoverRun_ != -1) hoverRun_ = -1;
    }

    bool RichText::OnKeyPress(KeyEvent& e) {
        if (!selectable_ || !isEnabled() || !hasFocus()) return false;
        rebuildFlat();

        const bool ctrl  = hasModifier(e.modifiers, KeyboardModifiers::Control);
        const bool shift = hasModifier(e.modifiers, KeyboardModifiers::Shift);

        if (ctrl && !e.isRepeat) {
            switch (e.key) {
            case Key::C: Copy();      return true;
            case Key::A: SelectAll(); return true;
            default: break;
            }
        }

        if (e.key == Key::LeftArrow || e.key == Key::RightArrow) {
            const std::size_t pos = (e.key == Key::LeftArrow) ? prevCharPos(caret_)
                                                             : nextCharPos(caret_);
            caret_ = pos;
            if (!shift) selAnchor_ = pos;
            RequestWindowRedraw();
            return true;
        }
        if (e.key == Key::Escape) {
            if (selAnchor_ != caret_) { ClearSelection(); return true; }
            return false;
        }
        return false;
    }
}
