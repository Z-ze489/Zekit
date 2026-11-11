#include "pch.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

#include "include/ZeFileDropZone.h"
#include "include/Zekit.h"
#include "include/ZeText.h"

#include "ZeDrawUtil.h"

namespace Kit {

    namespace {
        std::string LowerAscii(std::string s) {
            for (char& c : s)
                if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            return s;
        }

        std::string ExtensionOf(const std::string& path) {
            const std::size_t slash = path.find_last_of("/\\");
            const std::size_t dot = path.find_last_of('.');
            if (dot == std::string::npos) return std::string();
            if (slash != std::string::npos && dot < slash) return std::string();
            return LowerAscii(path.substr(dot + 1));
        }

        std::string NormalizeExt(std::string e) {
            while (!e.empty() && (e.front() == '*' || e.front() == '.')) e.erase(e.begin());
            return LowerAscii(e);
        }
    }

    FileDropZone::FileDropZone(std::string hint)
        : Label(std::string())
    {
        hint_ = hint.empty() ? Kit::UiText(TextId::DropHere) : std::move(hint);

        ApplyTheme(Themes::Light());

        SetDefaultSize(320.0f, 140.0f);

        SetAcceptFileDrop(true);

        RefreshText();
    }

    void FileDropZone::RefreshText() {
        if (dragActive_) {
            Label::SetText(Kit::UiText(TextId::DropRelease, dragCount_));
            return;
        }
        Label::SetText(hint_);
    }

    namespace {
        std::vector<std::string_view> SplitByNewline(std::string_view s) {
            std::vector<std::string_view> out;
            if (s.empty()) return out;
            std::size_t start = 0;
            while (true) {
                const std::size_t nl = s.find('\n', start);
                if (nl == std::string_view::npos) {
                    out.push_back(s.substr(start));
                    break;
                }
                out.push_back(s.substr(start, nl - start));
                start = nl + 1;
            }
            return out;
        }

        constexpr float kSubGap = 6.0f;

        float BlockHeight(Kit::FrameCollector& fc, const std::vector<std::string_view>& lines,
            const Kit::TextStyle& style, std::vector<Kit::TextMetrics>& out) {
            if (lines.empty()) return 0.0f;
            float lineH = style.lineHeight;
            for (std::string_view l : lines) {
                const Kit::TextMetrics tm = fc.MeasureText(l, style.font);
                out.push_back(tm);
                lineH = std::max(lineH, tm.height);
            }
            return lineH * static_cast<float>(lines.size());
        }
    }

    void FileDropZone::paintContent(FrameCollector& fc) const {
        const bool showSub = !dragActive_ && !subHint_.empty();
        if (text_.empty() && !showSub) return;

        float cx = 0.0f, cy = 0.0f, cw = 0.0f, ch = 0.0f;
        contentRect(cx, cy, cw, ch);
        if (cw <= 0.0f || ch <= 0.0f) return;

        const std::vector<std::string_view> mainLines = SplitByNewline(text_);
        const std::vector<std::string_view> subLines =
            showSub ? SplitByNewline(subHint_) : std::vector<std::string_view>{};

        std::vector<TextMetrics> mainM, subM;
        const float mainH = BlockHeight(fc, mainLines, textStyle_, mainM);
        const float subH = BlockHeight(fc, subLines, subStyle_, subM);
        const float gap = (mainH > 0.0f && subH > 0.0f) ? kSubGap : 0.0f;
        const float blockH = mainH + gap + subH;
        if (blockH <= 0.0f) return;

        const float blockTop = cy + alignOffsetY(textStyle_.vAlign, ch - blockH);

        const ColorSet mainCs = resolvedTextColors(currentState());
        ColorSet subCs = mainCs;
        subCs.text = subStyle_.colors.get(WidgetState::Normal).text;
        if (!isEnabled()) subCs.text = textStyle_.colors.get(WidgetState::Disabled).text;

        const auto paintBlock = [&](const std::vector<std::string_view>& lines,
            const std::vector<TextMetrics>& ms, const TextStyle& style,
            const ColorSet& cs, float top) {
                float lineH = style.lineHeight;
                for (const TextMetrics& tm : ms) lineH = std::max(lineH, tm.height);
                if (lineH <= 0.0f) return;
                for (std::size_t i = 0; i < lines.size(); ++i) {
                    std::string cut;
                    std::string_view draw = lines[i];
                    TextMetrics tm = ms[i];
                    if (tm.width > cw) {
                        cut = drawutil::Ellipsize(fc, draw, style.font, cw);
                        draw = cut;
                        tm = fc.MeasureText(draw, style.font);
                    }
                    const float tx = cx + alignOffsetX(style.hAlign, cw - tm.width);
                    const float ty = top + lineH * static_cast<float>(i);
                    if (ty >= cy + ch) break;
                    if (ty + lineH <= cy) continue;
                    paintTextAt(fc, draw, tx, ty, tm, cs);
                }
            };

        paintBlock(mainLines, mainM, textStyle_, mainCs, blockTop);
        if (subH > 0.0f)
            paintBlock(subLines, subM, subStyle_, subCs, blockTop + mainH + gap);
    }

    float FileDropZone::hintHeight() const {
        if (prefHeight() > 0.0f) return prefHeight();

        Window* win = window();
        if (!win) return Widget::hintHeight();

        float dx = 0.0f, dy = 0.0f;
        drawutil::BoxInsets(box_, dx, dy);
        (void)dx;

        const auto blockH = [&](const std::string& s, const TextStyle& style) -> float {
            if (s.empty()) return 0.0f;
            const std::vector<std::string_view> lines = SplitByNewline(s);
            float lineH = style.lineHeight;
            for (std::string_view l : lines)
                lineH = std::max(lineH, win->MeasureText(l, style.font).height);
            return lineH * static_cast<float>(lines.size());
            };
        const float mainH = blockH(text_, textStyle_);
        const float subH = (!dragActive_ && !subHint_.empty()) ? blockH(subHint_, subStyle_) : 0.0f;
        const float gap = (mainH > 0.0f && subH > 0.0f) ? kSubGap : 0.0f;
        return mainH + gap + subH + dy;
    }

    float FileDropZone::hintWidth() const {
        if (prefWidth() > 0.0f) return prefWidth();

        Window* win = window();
        if (!win) return Widget::hintWidth();

        float dx = 0.0f, dy = 0.0f;
        drawutil::BoxInsets(box_, dx, dy);
        (void)dy;

        const auto blockW = [&](const std::string& s, const TextStyle& style) -> float {
            float widest = 0.0f;
            for (std::string_view l : SplitByNewline(s))
                widest = std::max(widest, win->MeasureText(l, style.font).width);
            return widest;
            };
        const float mainW = blockW(text_, textStyle_);
        const float subW = (!dragActive_ && !subHint_.empty()) ? blockW(subHint_, subStyle_) : 0.0f;
        return std::max(mainW, subW) + dx;
    }

    void FileDropZone::SetHint(const std::string& hint) {
        if (hint_ == hint) return;
        hint_ = hint;
        RefreshText();
    }

    void FileDropZone::SetSubHint(const std::string& sub) {
        if (subHint_ == sub) return;
        subHint_ = sub;
        RefreshText();
        notifyLayoutInputChanged();
        RequestWindowRedraw();
    }

    void FileDropZone::SetOnFilesDropped(std::function<void(const std::vector<std::string>&)> cb) {
        onDropped_ = std::move(cb);
    }

    void FileDropZone::SetMultiple(bool on) {
        if (multiple_ == on) return;
        multiple_ = on;
        RequestWindowRedraw();
    }

    void FileDropZone::SetExtensions(std::vector<std::string> exts) {
        for (std::string& e : exts) e = NormalizeExt(std::move(e));
        exts.erase(std::remove(exts.begin(), exts.end(), std::string()), exts.end());
        if (extensions_ == exts) return;
        extensions_ = std::move(exts);
        if (!extensions_.empty() && subHint_.empty()) {
            std::string joined;
            for (const std::string& e : extensions_) {
                if (!joined.empty()) joined += " / ";
                joined += "*." + e;
            }
            subHint_ = Kit::UiText(TextId::Supports, joined);
        }
        RefreshText();
        RequestWindowRedraw();
    }

    void FileDropZone::SetAcceptDirectories(bool on) {
        if (acceptDirs_ == on) return;
        acceptDirs_ = on;
        RequestWindowRedraw();
    }

    void FileDropZone::SetActiveTextColor(const Color4f& c) {
        overrideActiveText_ = true;
        activeTextOverride_ = c;
        activeText_ = c;
        RequestWindowRedraw();
    }

    void FileDropZone::SetActiveBackColor(const Color4f& c) {
        overrideActiveBack_ = true;
        activeBackOverride_ = c;
        activeBox_.colors.get(WidgetState::Normal).background = c;
        RequestWindowRedraw();
    }

    void FileDropZone::SetActiveBorderColor(const Color4f& c) {
        overrideActiveBorder_ = true;
        activeBorderOverride_ = c;
        activeBox_.colors.get(WidgetState::Normal).border = c;
        RequestWindowRedraw();
    }

    const BoxStyle& FileDropZone::themeBox(const Theme& t) const { return t.fileDropZone.box; }
    const TextStyle& FileDropZone::themeText(const Theme& t) const { return t.fileDropZone.text; }

    void FileDropZone::ApplyTheme(const Theme& t) {
        TextWidget::ApplyTheme(t);

        savedBox_ = box_;

        activeBox_ = t.fileDropZone.activeBox;
        activeText_ = t.fileDropZone.activeText;
        subStyle_ = t.fileDropZone.subText;

        if (overrideActiveBack_)
            activeBox_.colors.get(WidgetState::Normal).background = activeBackOverride_;
        if (overrideActiveBorder_)
            activeBox_.colors.get(WidgetState::Normal).border = activeBorderOverride_;
        if (overrideActiveText_)
            activeText_ = activeTextOverride_;

        if (dragActive_) box_ = activeBox_;
    }

    bool FileDropZone::AcceptsThese(const std::vector<std::string>& files) const {
        if (files.empty()) return false;
        if (extensions_.empty()) return true;
        for (const std::string& f : files) {
            const std::string ext = ExtensionOf(f);
            if (ext.empty()) {
                return true;
            }
            if (std::find(extensions_.begin(), extensions_.end(), ext) != extensions_.end())
                return true;
        }
        return false;
    }

    std::vector<std::string> FileDropZone::Filter(const std::vector<std::string>& files) const {
        std::vector<std::string> out;
        for (const std::string& f : files) {
            if (!extensions_.empty()) {
                const std::string ext = ExtensionOf(f);
                if (!ext.empty() && std::find(extensions_.begin(), extensions_.end(), ext) == extensions_.end())
                    continue;
                if (ext.empty() && !acceptDirs_) {
                }
            }
            out.push_back(f);
            if (!multiple_) break;
        }
        return out;
    }

    void FileDropZone::SetDragActive(bool on, int count) {
        if (dragActive_ == on && dragCount_ == count) return;
        if (on) savedBox_ = box_;
        dragActive_ = on;
        dragCount_ = count;
        box_ = on ? activeBox_ : savedBox_;
        RefreshText();
        RequestWindowRedraw();
    }

    bool FileDropZone::OnFileDragEnter(float, float, const std::vector<std::string>& files) {
        if (!AcceptsThese(files)) {
            SetDragActive(false, 0);
            return false;
        }
        SetDragActive(true, static_cast<int>(Filter(files).size()));
        return true;
    }

    bool FileDropZone::OnFileDragOver(float, float) {
        return dragActive_;
    }

    void FileDropZone::OnFileDragLeave() {
        SetDragActive(false, 0);
    }

    bool FileDropZone::OnFileDrop(float, float, const std::vector<std::string>& files) {
        SetDragActive(false, 0);
        std::vector<std::string> accepted = Filter(files);
        if (accepted.empty()) return false;
        if (onDropped_) onDropped_(accepted);
        return true;
    }

}
