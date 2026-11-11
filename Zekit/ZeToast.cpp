
#include "pch.h"

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")
#else
#include <unistd.h>
#include <sys/wait.h>
#endif

#include "include/ZeToast.h"
#include "include/ZeOverlayWindow.h"
#include "include/Zekit.h"

#include "ZeFrameCollector.h"
#include "ZeDrawUtil.h"
#include "ZeValueAnim.h"
#include "ZeRoundedRectShape.h"

namespace Kit {

    namespace {
        constexpr float kShadowPad = 8.0f;
        constexpr float kFallbackLineH = 20.0f;
        constexpr float kCloseSize = 18.0f;
        constexpr float kCloseGap = 8.0f;

        float BorderOf(const BoxStyle& box) {
            return std::max({ box.border.width[0], box.border.width[1],
                              box.border.width[2], box.border.width[3] });
        }

        void PaintGlyph(FrameCollector& fc, ToastLevel level, float cx, float cy,
            float d, const Color4f& color) {
            const float t = d * 0.105f;
            switch (level) {
            case ToastLevel::Success: {
                drawutil::PaintThickSegment(fc,
                    cx - d * 0.24f, cy + d * 0.01f, cx - d * 0.06f, cy + d * 0.19f, t, color);
                drawutil::PaintThickSegment(fc,
                    cx - d * 0.06f, cy + d * 0.19f, cx + d * 0.26f, cy - d * 0.17f, t, color);
                break;
            }
            case ToastLevel::Error: {
                drawutil::PaintThickSegment(fc,
                    cx - d * 0.17f, cy - d * 0.17f, cx + d * 0.17f, cy + d * 0.17f, t, color);
                drawutil::PaintThickSegment(fc,
                    cx + d * 0.17f, cy - d * 0.17f, cx - d * 0.17f, cy + d * 0.17f, t, color);
                break;
            }
            case ToastLevel::Warning: {
                drawutil::PaintThickSegment(fc,
                    cx, cy - d * 0.24f, cx, cy + d * 0.06f, t, color);
                drawutil::PaintCircle(fc, cx, cy + d * 0.24f, d * 0.062f, color);
                break;
            }
            case ToastLevel::Info:
            default: {
                drawutil::PaintCircle(fc, cx, cy - d * 0.20f, d * 0.068f, color);
                drawutil::PaintThickSegment(fc,
                    cx, cy - d * 0.02f, cx, cy + d * 0.24f, t, color);
                break;
            }
            }
        }
    }

    Toast::Toast() {
        ApplyTheme(Themes::Light());
    }

    void Toast::Show(std::string text, ToastLevel level, int durationMs) {
        Item it;
        it.text = std::move(text);
        it.level = level;
        it.bornMs = detail::NowMs();
        it.durationMs = durationMs;
        items_.push_back(std::move(it));

        playSound(level);

        if (maxVisible_ > 0) {
            while (static_cast<int>(items_.size()) > maxVisible_)
                items_.erase(items_.begin());
        }

        requestRepaint();
    }

    void Toast::ShowInfo(std::string text, int durationMs) {
        Show(std::move(text), ToastLevel::Info, durationMs);
    }
    void Toast::ShowSuccess(std::string text, int durationMs) {
        Show(std::move(text), ToastLevel::Success, durationMs);
    }
    void Toast::ShowWarning(std::string text, int durationMs) {
        Show(std::move(text), ToastLevel::Warning, durationMs);
    }
    void Toast::ShowError(std::string text, int durationMs) {
        Show(std::move(text), ToastLevel::Error, durationMs);
    }

    void Toast::Clear() {
        if (items_.empty()) return;
        items_.clear();
        syncOverlay();
        requestRepaint();
    }

    void Toast::SetPlace(ToastPlace p) { place_ = p; requestRepaint();   RequestWindowRedraw();}

    void Toast::SetMaxVisible(int n) {
        maxVisible_ = n;
        if (maxVisible_ > 0) {
            while (static_cast<int>(items_.size()) > maxVisible_)
                items_.erase(items_.begin());
        }
        requestRepaint();
        RequestWindowRedraw();
    }

    void Toast::SetMaxWidth(float w) { maxWidth_ = (w > 0.0f) ? w : 0.0f; requestRepaint();   RequestWindowRedraw();}
    void Toast::SetMargin(float m) { margin_ = (m > 0.0f) ? m : 0.0f; requestRepaint();   RequestWindowRedraw();}
    void Toast::SetItemGap(float g) { itemGap_ = (g > 0.0f) ? g : 0.0f; requestRepaint();   RequestWindowRedraw();}
    void Toast::SetFadeDuration(int ms) { fadeMs_ = (ms < 1) ? 1 : ms; requestRepaint();   RequestWindowRedraw();}

    void Toast::SetBackground(const Color4f& c) {
        box_.colors.get(WidgetState::Normal).background = c;
        markStyled();
        requestRepaint();
        RequestWindowRedraw();
    }
    void Toast::SetTextColor(const Color4f& c) {
        textStyle_.colors.get(WidgetState::Normal).text = c;
        markStyled();
        requestRepaint();
        RequestWindowRedraw();
    }
    void Toast::SetFontSize(unsigned int pixelSize) {
        textStyle_.font.pixelSize = pixelSize;
        markStyled();
        requestRepaint();
        RequestWindowRedraw();
    }

    void Toast::SetInteractive(bool on) {
        if (interactive_ == on) return;
        interactive_ = on;
        hoverRow_ = -1;
        hoverClose_ = false;
        pressedCloseRow_ = -1;
        pressedBody_ = false;
        requestRepaint();
        RequestWindowRedraw();
    }

    void Toast::SetClickToClose(bool on) {
        clickToClose_ = on;
        RequestWindowRedraw();
    }

    void Toast::SetSound(ToastSound mode) {
        sound_ = mode;
        RequestWindowRedraw();
    }

    void Toast::SetSoundFile(const std::string& path) {
        soundFile_ = path;
        RequestWindowRedraw();
    }

    void Toast::SetCustomSound(std::function<void()> fn) {
        customSound_ = std::move(fn);
        RequestWindowRedraw();
    }

    void Toast::playSound(ToastLevel level) {
#ifdef _WIN32
        switch (sound_) {
        case ToastSound::Off:
            return;
        case ToastSound::Custom: {
            if (!soundFile_.empty()) {
                const int n = MultiByteToWideChar(CP_UTF8, 0, soundFile_.c_str(),
                    static_cast<int>(soundFile_.size()), nullptr, 0);
                std::wstring w(static_cast<size_t>(n > 0 ? n : 0), L'\0');
                if (n > 0) {
                    MultiByteToWideChar(CP_UTF8, 0, soundFile_.c_str(),
                        static_cast<int>(soundFile_.size()), &w[0], n);
                    if (PlaySoundW(w.c_str(), nullptr,
                            SND_FILENAME | SND_ASYNC | SND_NODEFAULT))
                        return;
                }
                if (customSound_) customSound_();
                return;
            }
            if (customSound_) { customSound_(); return; }
            return;
        }
        case ToastSound::System:
        default: {
            UINT id = MB_ICONASTERISK;
            if (level == ToastLevel::Error)   id = MB_ICONHAND;
            else if (level == ToastLevel::Warning) id = MB_ICONEXCLAMATION;
            MessageBeep(id);
            return;
        }
        }
#else
        {
            std::string file;
            const char* themeEvent = nullptr;
            if (sound_ == ToastSound::Custom) {
                if (!soundFile_.empty()) file = soundFile_;
                else { if (customSound_) customSound_(); return; }
            }
            else if (sound_ == ToastSound::Off) {
                return;
            }
            else {
                themeEvent = "message";
                if (level == ToastLevel::Error)        themeEvent = "dialog-error";
                else if (level == ToastLevel::Warning) themeEvent = "dialog-warning";
            }

            const pid_t mid = ::fork();
            if (mid == 0) {
                const pid_t pid = ::fork();
                if (pid == 0) {
                    if (!file.empty()) {
                        ::execlp("paplay", "paplay", file.c_str(), (char*)nullptr);
                        ::execlp("canberra-gtk-play", "canberra-gtk-play",
                                 "-f", file.c_str(), (char*)nullptr);
                    } else {
                        ::execlp("paplay", "paplay",
                                 std::string("/usr/share/sounds/freedesktop/stereo/")
                                     + themeEvent + ".oga", (char*)nullptr);
                        ::execlp("canberra-gtk-play", "canberra-gtk-play",
                                 "-i", themeEvent, (char*)nullptr);
                    }
                    ::_exit(127);
                }
                ::_exit(0);
            }
            if (mid > 0) {
                int st = 0;
                ::waitpid(mid, &st, 0);
            }
        }
#endif
    }

    int Toast::rowAtPoint(float px, float py) const {
        if (!interactive_ || items_.empty()) return -1;
        const StackGeom g = computeStack();
        if (!g.valid) return -1;
        const std::size_t n = std::min(items_.size(), g.rows.size());
        for (std::size_t i = 0; i < n; ++i) {
            const RowRect& r = g.rows[i];
            if (px >= r.x && px <= r.x + r.w && py >= r.y && py <= r.y + r.h)
                return static_cast<int>(i);
        }
        return -1;
    }

    void Toast::closeRectOf(const RowRect& r, float& cx, float& cy, float& size) const {
        const float bt = BorderOf(box_);
        size = kCloseSize;
        cx = r.x + r.w - bt - box_.padding.right - size * 0.5f;
        cy = r.y + r.h * 0.5f;
    }

    bool Toast::hitCloseOf(const RowRect& r, float px, float py) const {
        float cx = 0.0f, cy = 0.0f, s = 0.0f;
        closeRectOf(r, cx, cy, s);
        const float h = s * 0.5f + 3.0f;
        return px >= cx - h && px <= cx + h && py >= cy - h && py <= cy + h;
    }

    void Toast::removeAt(std::size_t idx) {
        if (idx >= items_.size()) return;
        items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(idx));
        hoverRow_ = -1;
        hoverClose_ = false;
        pressedCloseRow_ = -1;
        pressedBody_ = false;
        requestRepaint();
    }

    const BoxStyle& Toast::themeBox(const Theme& t) const {
        return t.toast.box;
    }

    void Toast::ApplyTheme(const Theme& t) {
        BoxWidget::ApplyTheme(t);
        textStyle_ = t.toast.text;
        accentWidth_ = t.toast.accentWidth;
        accentInfo_ = t.toast.accentInfo;
        accentSuccess_ = t.toast.accentSuccess;
        accentWarning_ = t.toast.accentWarning;
        accentError_ = t.toast.accentError;
        iconSize_ = (t.toast.iconSize > 0.0f) ? t.toast.iconSize : 0.0f;
        iconGap_ = (t.toast.iconGap > 0.0f) ? t.toast.iconGap : 0.0f;
        iconGlyph_ = t.toast.iconGlyph;
        minWidth_ = (t.toast.minWidth > 0.0f) ? t.toast.minWidth : 0.0f;
        closeGlyph_ = t.toast.closeGlyph;
        closeHoverBg_ = t.toast.closeHoverBg;
    }

    Color4f Toast::accentOf(ToastLevel level) const {
        switch (level) {
        case ToastLevel::Success: return accentSuccess_;
        case ToastLevel::Warning: return accentWarning_;
        case ToastLevel::Error:   return accentError_;
        case ToastLevel::Info:
        default:                  return accentInfo_;
        }
    }

    float Toast::hintWidth() const {
        if (prefWidth() > 0.0f) return prefWidth();
        return 0.0f;
    }
    float Toast::hintHeight() const {
        if (prefHeight() > 0.0f) return prefHeight();
        return 0.0f;
    }

    float Toast::lineHeight() const {
        if (Window* w = window()) {
            const TextMetrics tm = w->MeasureText("Hg", textStyle_.font);
            if (tm.height > 0.0f) return tm.height;
        }
        if (textStyle_.font.pixelSize > 0)
            return static_cast<float>(textStyle_.font.pixelSize) * 1.35f;
        return kFallbackLineH;
    }

    float Toast::itemHeight(const Item& it) const {
        const float bt = BorderOf(box_);
        const std::size_t n = drawutil::SplitLines(it.text).size();
        const float textH = lineHeight() * static_cast<float>(n)
            + box_.padding.top + box_.padding.bottom + bt * 2.0f;
        if (iconSize_ > 0.0f) {
            const float iconH = iconSize_ + box_.padding.top + box_.padding.bottom + bt * 2.0f;
            return std::max(textH, iconH);
        }
        return textH;
    }

    float Toast::itemWidth(const Item& it) const {
        const float bt = BorderOf(box_);
        float contentW = 0.0f;
        if (Window* w = window()) {
            for (const std::string& line : drawutil::SplitLines(it.text))
                contentW = std::max(contentW, w->MeasureText(line, textStyle_.font).width);
        }
        const float closeSpan = interactive_ ? (kCloseSize + kCloseGap) : 0.0f;
        float w = contentW + accentWidth_ + iconSpan() + closeSpan
            + box_.padding.left + box_.padding.right + bt * 2.0f;
        if (minWidth_ > 0.0f) w = std::max(w, minWidth_);
        return w;
    }

    bool Toast::measureStack(float& width, float& height) const {
        width = 0.0f;
        height = 0.0f;
        if (items_.empty()) return false;

        float widest = 0.0f;
        float totalH = 0.0f;
        for (const Item& it : items_) {
            widest = std::max(widest, itemWidth(it));
            totalH += itemHeight(it);
        }
        totalH += itemGap_ * static_cast<float>(items_.size() - 1);

        if (maxWidth_ > 0.0f) widest = std::min(widest, maxWidth_);

        width = std::max(widest, 1.0f);
        height = std::max(totalH, 1.0f);
        return true;
    }

    Toast::StackGeom Toast::computeStack() const {
        StackGeom g;
        if (items_.empty()) return g;

        float widest = 0.0f;
        std::vector<float> hs;
        hs.reserve(items_.size());
        for (const Item& it : items_) {
            widest = std::max(widest, itemWidth(it));
            hs.push_back(itemHeight(it));
        }

        float w = widest;
        if (maxWidth_ > 0.0f) w = std::min(w, maxWidth_);
        float winW = 0.0f, winH = 0.0f;
        if (Window* win = window()) {
            winW = static_cast<float>(win->width());
            winH = static_cast<float>(win->height());
        }
        if (winW > 0.0f) w = std::min(w, std::max(1.0f, winW - margin_ * 2.0f));
        w = std::max(w, 1.0f);

        float totalH = 0.0f;
        for (float h : hs) totalH += h;
        totalH += itemGap_ * static_cast<float>(hs.size() - 1);

        float x = 0.0f, y = 0.0f;
        switch (place_) {
        case ToastPlace::TopCenter:    x = (winW - w) * 0.5f; y = margin_; break;
        case ToastPlace::TopLeft:      x = margin_;           y = margin_; break;
        case ToastPlace::TopRight:     x = winW - margin_ - w; y = margin_; break;
        case ToastPlace::BottomCenter: x = (winW - w) * 0.5f; y = winH - margin_ - totalH; break;
        case ToastPlace::BottomLeft:   x = margin_;           y = winH - margin_ - totalH; break;
        case ToastPlace::BottomRight:  x = winW - margin_ - w; y = winH - margin_ - totalH; break;
        case ToastPlace::Center:       x = (winW - w) * 0.5f; y = (winH - totalH) * 0.5f; break;
        }
        if (winW <= 0.0f) x = 0.0f;
        if (winH <= 0.0f) y = 0.0f;
        if (x < 0.0f) x = 0.0f;
        if (y < 0.0f) y = 0.0f;

        g.x = x; g.y = y; g.w = w; g.h = totalH;

        float cy = y;
        for (float h : hs) {
            g.rows.push_back(RowRect{ x, cy, w, h });
            cy += h + itemGap_;
        }
        g.valid = true;
        return g;
    }

    float Toast::alphaOf(const Item& it, long long nowMs) const {
        const float fade = static_cast<float>(fadeMs_ > 0 ? fadeMs_ : 1);
        const long long age = nowMs - it.bornMs;
        if (age < 0) return 0.0f;

        if (age < fadeMs_)
            return std::clamp(static_cast<float>(age) / fade, 0.0f, 1.0f);

        if (it.durationMs <= 0) return 1.0f;

        const long long remain = static_cast<long long>(it.durationMs) - age;
        if (remain < fadeMs_)
            return std::clamp(static_cast<float>(remain) / fade, 0.0f, 1.0f);
        return 1.0f;
    }

    bool Toast::needNextFrame(long long nowMs) const {
        for (const Item& it : items_) {
            if (it.durationMs > 0) return true;
            if (nowMs - it.bornMs < fadeMs_) return true;
        }
        return false;
    }

    void Toast::stepAnimation() const {
        const long long now = detail::NowMs();

        items_.erase(
            std::remove_if(items_.begin(), items_.end(),
                [now](const Item& it) {
                    return it.durationMs > 0 && (now - it.bornMs) >= it.durationMs;
                }),
            items_.end());

        if (needNextFrame(now)) {
            if (Window* w = window()) w->RequestFrameWake(16);
        }
    }

    bool Toast::wantsAnimationFrame() const {
        if (items_.empty()) return false;
        return needNextFrame(detail::NowMs());
    }

    Toast::~Toast() = default;
    Toast::Toast(Toast&&) noexcept = default;

    bool Toast::nativePopup() const {
        return overlay_ && overlay_->isVisible();
    }

    void Toast::syncOverlay() const {
        if (interactive_) return;
        if (items_.empty()) {
            if (overlay_) overlay_->Hide();
            return;
        }

        Window* w = this->window();
        if (!w || w->IsClosed()) return;
        if (!OverlayWindow::SupportsNative(*w)) return;

        if (!overlay_) {
            overlay_ = std::make_unique<OverlayWindow>(*w, OverlayLayer::Toast);
            auto* self = const_cast<Toast*>(this);
            overlay_->SetOnTakenOver([self] { self->Clear(); });
            OverlayContent content;
            content.paint = [this](FrameCollector& fc) { paintStack(fc, true); };
            overlay_->SetContent(std::move(content));
            overlay_->SetBackground(Color4f{ 0.0f, 0.0f, 0.0f, 0.0f });
            overlay_->SetOnHostGeometryChanged([this] { syncOverlay(); });
            overlay_->SetClickThrough(true);
        }

        const StackGeom g = computeStack();
        if (!g.valid) { overlay_->Hide(); return; }

        const bool sameGeom = (g.x == overlayX_ && g.y == overlayY_
                               && g.w == overlayW_ && g.h == overlayH_);
        overlayX_ = g.x; overlayY_ = g.y; overlayW_ = g.w; overlayH_ = g.h;

        if (overlay_->isVisible() && sameGeom) overlay_->Repaint();
        else                                    overlay_->Show(g.x, g.y, g.w, g.h);
    }

    bool Toast::paintsOnTop() const {
        return !items_.empty() && !nativePopup();
    }

    bool Toast::HitTest(float x, float y) const {
        if (!interactive_) return false;
        return rowAtPoint(x, y) >= 0;
    }

    bool Toast::OnMousePress(MouseEvent& e) {
        if (!interactive_ || items_.empty()) return false;
        const StackGeom g = computeStack();
        if (!g.valid) return false;

        const int row = rowAtPoint(e.x, e.y);
        if (row < 0 || row >= static_cast<int>(g.rows.size())) return false;

        if (hitCloseOf(g.rows[static_cast<std::size_t>(row)], e.x, e.y)) {
            pressedCloseRow_ = row;
        }
        else {
            pressedBody_ = clickToClose_;
        }
        e.accept();
        requestRepaint();
        return true;
    }

    bool Toast::OnMouseMove(MouseEvent& e) {
        if (!interactive_ || items_.empty()) return false;

        const int row = rowAtPoint(e.x, e.y);
        const bool onClose = (row >= 0) &&
            hitCloseOf(computeStack().rows[static_cast<std::size_t>(row)], e.x, e.y);

        const bool changed = (row != hoverRow_) || (onClose != hoverClose_) || !hoverAny_;
        hoverAny_ = true;
        hoverRow_ = row;
        hoverClose_ = onClose;
        if (changed) requestRepaint();
        return false;
    }

    bool Toast::OnMouseRelease(MouseEvent& e) {
        if (!interactive_) { pressedCloseRow_ = -1; pressedBody_ = false; return false; }

        const bool wasClose = (pressedCloseRow_ >= 0);
        const bool wantClose = wasClose || pressedBody_;
        pressedCloseRow_ = -1;
        pressedBody_ = false;
        if (!wantClose || items_.empty()) { requestRepaint(); return false; }

        const int row = rowAtPoint(e.x, e.y);
        if (row < 0) { requestRepaint(); return false; }

        const StackGeom g = computeStack();
        if (row >= static_cast<int>(g.rows.size())) { requestRepaint(); return false; }

        const RowRect& r = g.rows[static_cast<std::size_t>(row)];
        if (wasClose) {
            if (hitCloseOf(r, e.x, e.y)) removeAt(static_cast<std::size_t>(row));
        }
        else if (clickToClose_) {
            removeAt(static_cast<std::size_t>(row));
        }
        requestRepaint();
        return true;
    }

    void Toast::OnMouseLeave() {
        if (!hoverAny_ && hoverRow_ < 0 && !hoverClose_) return;
        hoverAny_ = false;
        hoverRow_ = -1;
        hoverClose_ = false;
        pressedBody_ = false;
        requestRepaint();
    }

    void Toast::Paint(FrameCollector& fc) const {
        stepAnimation();
        syncOverlay();

        if (items_.empty()) return;
        if (!paintsOnTop()) return;
        paintContent(fc);
    }

    void Toast::paintContent(FrameCollector& fc) const {
        paintStack(fc, false);
    }

    void Toast::paintStack(FrameCollector& fc, bool local) const {
        stepAnimation();
        if (items_.empty()) return;

        StackGeom g = computeStack();
        if (!g.valid) return;

        if (local) {
            const float dx = g.x, dy = g.y;
            g.x -= dx; g.y -= dy;
            for (RowRect& r : g.rows) { r.x -= dx; r.y -= dy; }
        }

        fc.ResetClip();

        const float bt = BorderOf(box_);
        const float radius = resolveRadius(box_, fc.GetTheme());
        const ColorSet cs = box_.colors.get(WidgetState::Normal);
        const Color4f baseText = textStyle_.colors.get(WidgetState::Normal).text;
        const float lineH = lineHeight();
        const long long now = detail::NowMs();

        const std::size_t n = std::min(items_.size(), g.rows.size());
        for (std::size_t i = 0; i < n; ++i) {
            const Item& it = items_[i];
            const RowRect& r = g.rows[i];

            const float a = alphaOf(it, now);
            if (a <= 0.001f) continue;

            if (box_.shadow.enabled) {
                Color4f sc = box_.shadow.color;
                sc.a *= a * box_.opacity;
                if (sc.a > 0.0f) {
                    const float ex = box_.shadow.spread;
                    fc.PushClip(r.x - kShadowPad, r.y - kShadowPad,
                                r.w + 2.0f * kShadowPad, r.h + 2.0f * kShadowPad);
                    drawutil::PaintRoundedRect(fc, r.x + box_.shadow.offsetX - ex,
                        r.y + box_.shadow.offsetY - ex,
                        r.w + 2.0f * ex, r.h + 2.0f * ex, radius, sc);
                    fc.PopClip();
                }
            }

            fc.PushClip(r.x, r.y, r.w, r.h);

            Color4f bg = cs.background;
            bg.a *= a;
            drawutil::PaintRoundedRect(fc, r.x, r.y, r.w, r.h, radius, bg);

            if (accentWidth_ > 0.0f) {
                Color4f acc = accentOf(it.level);
                acc.a *= a;
                const float ar = std::min(radius, accentWidth_ * 0.5f);
                drawutil::PaintRoundedRect(fc, r.x, r.y, accentWidth_, r.h, ar, acc);
            }

            float circleCx = 0.0f;
            if (iconSize_ > 0.0f) {
                Color4f acc = accentOf(it.level);
                acc.a *= a;
                Color4f glyph = iconGlyph_;
                glyph.a *= a;

                const float bx = r.x + bt + box_.padding.left;
                circleCx = bx + iconSize_ * 0.5f;
                const float circleCy = r.y + r.h * 0.5f;

                drawutil::PaintCircle(fc, circleCx, circleCy, iconSize_ * 0.5f, acc);
                PaintGlyph(fc, it.level, circleCx, circleCy, iconSize_, glyph);
            }

            const auto lines = drawutil::SplitLines(it.text);
            const float textLeft = r.x + accentWidth_ + box_.padding.left
                + iconSpan();
            const float closeSpan = interactive_ ? (kCloseSize + kCloseGap) : 0.0f;
            const float availW = std::max(0.0f,
                r.w - accentWidth_ - iconSpan() - closeSpan
                - box_.padding.left - box_.padding.right - bt * 2.0f);

            float ty = r.y + bt + box_.padding.top;
            for (const std::string& line : lines) {
                if (!line.empty() && availW > 0.0f) {
                    std::string shown = line;
                    TextMetrics tm = fc.MeasureText(shown, textStyle_.font);
                    if (tm.width > availW) {
                        shown = drawutil::EllipsizeToWidth(fc, line, textStyle_.font, availW);
                        tm = fc.MeasureText(shown, textStyle_.font);
                    }
                    if (tm.width > 0.0f) {
                        Color4f fg = baseText;
                        fg.a *= a;
                        fc.AddText(shown, textStyle_.font, textLeft, ty + tm.ascent, fg);
                    }
                }
                ty += lineH;
            }

            if (interactive_) {
                float ccx = 0.0f, ccy = 0.0f, csz = 0.0f;
                closeRectOf(r, ccx, ccy, csz);
                const float h = csz * 0.5f;

                const bool hovered = (hoverRow_ == static_cast<int>(i)) && hoverClose_;
                const bool pressed = (pressedCloseRow_ == static_cast<int>(i));
                if (hovered || pressed) {
                    Color4f bg = closeHoverBg_;
                    if (pressed) bg.a = std::min(1.0f, bg.a * 2.0f);
                    bg.a *= a;
                    if (bg.a > 0.0f)
                        drawutil::PaintCircle(fc, ccx, ccy, h, bg);
                }

                Color4f gl = (closeGlyph_.a > 0.0f) ? closeGlyph_ : baseText;
                if (closeGlyph_.a <= 0.0f) gl.a *= 0.55f;
                if (hovered) gl.a = std::min(1.0f, gl.a * 1.6f);
                gl.a *= a;

                const float arm = csz * 0.28f;
                const float th = std::max(1.5f, csz * 0.11f);
                drawutil::PaintThickSegment(fc, ccx - arm, ccy - arm, ccx + arm, ccy + arm, th, gl);
                drawutil::PaintThickSegment(fc, ccx + arm, ccy - arm, ccx - arm, ccy + arm, th, gl);
            }

            fc.PopClip();
        }
    }

    void Toast::requestRepaint() {
        RequestWindowRedraw();
    }
}
