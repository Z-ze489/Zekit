
#include "pch.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "include/ZeTooltip.h"
#include "include/Zekit.h"
#include "include/ZeOverlayWindow.h"

#include "ZePlatformWindow.h"
#include "ZeFrameCollector.h"
#include "ZePolygonShape.h"
#include "ZeRoundedRectShape.h"
#include "ZeDrawUtil.h"
#include "ZeValueAnim.h"

#include <cmath>
#include <iostream>

namespace Kit {

    namespace {
        constexpr float kShadowPad = 8.0f;
        constexpr float kFallbackLineH = 18.0f;

        constexpr float kNotchCut   = 14.0f;
        constexpr float kRibbonTail = 12.0f;
        constexpr float kCloudPadX  = 30.0f;
        constexpr float kCloudPadY  = 26.0f;
        constexpr float kCloudTopPad = 2.0f;
        constexpr float kNeonGlow   = 9.0f;
        constexpr float kNeonReserve = 12.0f;

        Color4f MixColor(const Color4f& a, const Color4f& b, float t) {
            return Color4f{ a.r + (b.r - a.r) * t,
                            a.g + (b.g - a.g) * t,
                            a.b + (b.b - a.b) * t,
                            a.a };
        }

        float BorderOf(const BoxStyle& box) {
            return std::max({ box.border.width[0], box.border.width[1],
                              box.border.width[2], box.border.width[3] });
        }

        void PaintTriangle(FrameCollector& fc, const Vec2& a, const Vec2& b,
            const Vec2& c, const Color4f& color) {
            if (color.a <= 0.0f) return;
            fc.AddShape(PolygonShape(std::vector<Vec2>{ a, b, c }, true), color);
        }

        std::vector<Vec2> StarVertices(float cx, float cy, float outerR, float innerR) {
            std::vector<Vec2> pts;
            pts.reserve(10);
            constexpr float kPi = 3.14159265f;
            for (int i = 0; i < 10; ++i) {
                const float ang = -kPi * 0.5f + static_cast<float>(i) * kPi * 0.2f;
                const float r = (i % 2 == 0) ? outerR : innerR;
                pts.push_back(Vec2{ cx + std::cos(ang) * r, cy + std::sin(ang) * r });
            }
            return pts;
        }

        std::vector<Vec2> NotchVertices(float x, float y, float w, float h, float cut) {
            if (cut > w * 0.45f) cut = w * 0.45f;
            if (cut > h * 0.65f) cut = h * 0.65f;
            if (cut <= 0.0f) cut = 1.0f;
            return {
                Vec2{ x,           y },
                Vec2{ x + w - cut, y },
                Vec2{ x + w,       y + cut },
                Vec2{ x + w,       y + h },
                Vec2{ x,           y + h }
            };
        }

        std::vector<Vec2> RibbonVertices(float x, float y, float w, float h, float tail) {
            if (tail > w * 0.22f) tail = w * 0.22f;
            if (tail <= 0.0f) tail = 1.0f;
            const float mid = y + h * 0.5f;
            return {
                Vec2{ x,            y },
                Vec2{ x + tail,     mid },
                Vec2{ x,            y + h },
                Vec2{ x + w,        y + h },
                Vec2{ x + w - tail, mid },
                Vec2{ x + w,        y }
            };
        }

        std::size_t Utf8Count(const std::string& s) {
            std::size_t n = 0;
            for (unsigned char ch : s)
                if ((ch & 0xC0) != 0x80) ++n;
            return n;
        }

        struct CloudGeom {
            float crownR = 0.0f;
            float bandTop = 0.0f;
            float bandH = 0.0f;
            float bandRadius = 0.0f;
            int   crowns = 3;
        };

        CloudGeom MakeCloudGeom(float w, float h) {
            CloudGeom g;
            if (w <= 2.0f || h <= 2.0f) return g;

            const int n = std::max(3, std::min(
                static_cast<int>(std::ceil(w / (0.648f * h))), 12));
            g.crowns = n;
            g.crownR = w / (2.0f * static_cast<float>(n)) * 1.06f;
            if (g.crownR > h * 0.45f) g.crownR = h * 0.45f;
            if (g.crownR < 4.0f) g.crownR = 4.0f;

            g.bandTop = g.crownR + kCloudTopPad;
            if (g.bandTop > h * 0.58f) g.bandTop = h * 0.58f;
            g.bandH = h - g.bandTop;
            g.bandRadius = std::min(g.bandH * 0.5f, h * 0.20f);
            return g;
        }

        void PaintCloudSilhouette(FrameCollector& fc, float x, float y, float w, float h,
            float grow, const Color4f& color) {
            if (w <= 0.0f || h <= 0.0f) return;
            const CloudGeom cg = MakeCloudGeom(w, h);
            if (cg.crownR <= 0.0f) return;

            drawutil::PaintRoundedRect(fc, x - grow, y + cg.bandTop,
                w + 2.0f * grow, cg.bandH + grow,
                cg.bandRadius + grow, color);

            const float r0 = cg.crownR + grow;
            const int n = cg.crowns;
            for (int i = 0; i < n; ++i) {
                const float t = (n == 1) ? 0.5f : static_cast<float>(i) / static_cast<float>(n - 1);
                const float cx = x + cg.crownR + (w - 2.0f * cg.crownR) * t;
                drawutil::PaintCircle(fc, cx, y + cg.bandTop, r0, color);
            }
        }

        void PaintNeonGlow(FrameCollector& fc, float x, float y, float w, float h,
            float radius, const Color4f& glow) {
            if (glow.a <= 0.0f) return;
            const int n = static_cast<int>(kNeonGlow);
            for (int i = n; i >= 1; --i) {
                const float k = static_cast<float>(i) / static_cast<float>(n);
                const float grow = static_cast<float>(i) * 1.2f;
                Color4f c = glow;
                c.a = glow.a * 0.20f * (1.0f - k) * (1.0f - k) + glow.a * 0.02f;
                if (c.a <= 0.0f) continue;
                drawutil::PaintRoundedRect(fc, x - grow, y - grow,
                    w + 2.0f * grow, h + 2.0f * grow, radius + grow, c);
            }
        }

        void PaintNeonBody(FrameCollector& fc, float x, float y, float w, float h,
            float radius, float ringWidth, const Color4f& ring, const Color4f& body) {
            if (ringWidth > 0.0f && ring.a > 0.0f) {
                drawutil::PaintRoundedRect(fc, x, y, w, h, radius, ring);
                drawutil::PaintRoundedRect(fc, x + ringWidth, y + ringWidth,
                    w - 2.0f * ringWidth, h - 2.0f * ringWidth,
                    std::max(0.0f, radius - ringWidth), body);
            }
            else {
                drawutil::PaintRoundedRect(fc, x, y, w, h, radius, body);
            }
        }

        std::uint64_t NextTooltipId() {
            static std::uint64_t n = 0;
            return ++n;
        }
    }

    bool TooltipHoverFilter::onMouse(Window& win, MouseEvent& e) {
        if (name_.empty()) return false;
        if (Tooltip* tip = win.FindWidgetDeep<Tooltip>(name_))
            tip->NotifyHover(e.x, e.y);
        return false;
    }

    Tooltip::Tooltip() {
        ApplyTheme(Themes::Light());
    }

    void Tooltip::ShowAt(float x, float y, std::string text) {
        TooltipContent c;
        c.text = std::move(text);
        ShowAt(x, y, std::move(c));
    }

    void Tooltip::ShowAt(float x, float y, TooltipContent content) {
        if (content.empty()) { Hide(); return; }
        pendingShow_ = false;
        pendingTarget_ = nullptr;

        content_ = std::move(content);
        SetText(content_.text);
        arrowDir_ = Arrow::None;
        anchorX_ = x;
        anchorY_ = y;
        arrowTipX_ = x;
        arrowTipY_ = y;

        float cw = 0.0f, ch = 0.0f;
        measureContent(cw, ch);
        sizeBubble(cw, ch);

        visible_ = true;
        shownTarget_ = nullptr;
        shownAtMs_ = detail::NowMs();
        fade_ = (fadeMs_ > 0) ? 0.0f : 1.0f;
        showNativePopup();
        requestRepaint();
    }

    void Tooltip::ShowFor(Widget& target, std::string text) {
        TooltipContent c;
        c.text = std::move(text);
        ShowFor(target, std::move(c));
    }

    void Tooltip::ShowFor(Widget& target, TooltipContent content) {
        if (content.empty()) { Hide(); return; }
        pendingShow_ = false;
        applyShowFor(target, std::move(content));
    }

    void Tooltip::Hide() {
        const bool hadPending = pendingShow_;
        pendingShow_ = false;
        pendingTarget_ = nullptr;

        if (!visible_) {
            if (hadPending) requestRepaint();
            return;
        }

        visible_ = false;
        shownTarget_ = nullptr;
        fade_ = 1.0f;
        hideNativePopup();
        requestRepaint();
    }

    void Tooltip::RequestShowFor(Widget& target, TooltipContent content, int delayMs) {
        if (content.empty()) { Dismiss(); return; }

        if (visible_ && shownTarget_ == &target && content_ == content) return;

        const int d = (delayMs >= 0) ? delayMs : delayMs_;
        if (d <= 0) { ShowFor(target, std::move(content)); return; }

        if (visible_) {
            visible_ = false;
            shownTarget_ = nullptr;
            hideNativePopup();
        }
        pendingShow_ = true;
        pendingTarget_ = &target;
        pendingContent_ = std::move(content);
        pendingHasAnchor_ = true;
        pendingX_ = pendingY_ = 0.0f;
        pendingDelayMs_ = d;
        pendingSinceMs_ = detail::NowMs();

        requestRepaint();
        if (Window* w = window()) w->RequestFrameWake(d);
    }

    void Tooltip::RequestShowAt(float x, float y, TooltipContent content, int delayMs) {
        if (content.empty()) { Dismiss(); return; }
        const int d = (delayMs >= 0) ? delayMs : delayMs_;
        if (d <= 0) { ShowAt(x, y, std::move(content)); return; }

        if (visible_) {
            visible_ = false;
            shownTarget_ = nullptr;
            hideNativePopup();
        }
        pendingShow_ = true;
        pendingTarget_ = nullptr;
        pendingContent_ = std::move(content);
        pendingHasAnchor_ = false;
        pendingX_ = x;
        pendingY_ = y;
        pendingDelayMs_ = d;
        pendingSinceMs_ = detail::NowMs();

        requestRepaint();
        if (Window* w = window()) w->RequestFrameWake(d);
    }

    void Tooltip::Dismiss() {
        pendingShow_ = false;
        pendingTarget_ = nullptr;
        hoverTarget_ = nullptr;
        Hide();
    }

    void Tooltip::Attach(Widget& target, std::string text) {
        TooltipContent c;
        c.text = std::move(text);
        Attach(target, std::move(c));
    }

    void Tooltip::Attach(Widget& target, TooltipContent content) {
        for (AttachEntry& a : attach_) {
            if (a.target == &target) {
                a.content = std::move(content);
                return;
            }
        }
        attach_.push_back(AttachEntry{ &target, std::move(content) });

        if (ObjectName().empty())
            SetObjectName("__zekit_tooltip_" + std::to_string(NextTooltipId()));

        syncHoverFilter();
    }

    void Tooltip::Detach(Widget& target) {
        attach_.erase(
            std::remove_if(attach_.begin(), attach_.end(),
                [&target](const AttachEntry& a) { return a.target == &target; }),
            attach_.end());

        if (hoverTarget_ == &target) {
            hoverTarget_ = nullptr;
            Dismiss();
        }
        syncHoverFilter();
    }

    void Tooltip::DetachAll() {
        attach_.clear();
        hoverTarget_ = nullptr;
        Dismiss();
        syncHoverFilter();
    }

    void Tooltip::syncHoverFilter() {
        if (attach_.empty()) {
            if (hoverFilter_) {
                if (Window* w = window()) w->RemoveEventFilter(installedId_);
                hoverFilter_.reset();
                installedId_ = 0;
            }
            return;
        }

        if (!hoverFilter_) {
            hoverFilter_ = std::make_unique<TooltipHoverFilter>();
            installedId_ = 0;
        }
        hoverFilter_->name_ = ObjectName();

        if (Window* w = window(); w && installedId_ == 0)
            installedId_ = w->InstallEventFilter(*hoverFilter_);
    }

    void Tooltip::NotifyHover(float px, float py) {
        if (attach_.empty()) return;

        Widget* hit = nullptr;
        const TooltipContent* hitContent = nullptr;
        for (AttachEntry& a : attach_) {
            if (a.target == nullptr || !a.target->isVisible()) continue;
            if (a.target->HitTest(px, py)) { hit = a.target; hitContent = &a.content; break; }
        }

        if (hit != nullptr) {
            if (hoverTarget_ != hit) {
                hoverTarget_ = hit;
                RequestShowFor(*hit, *hitContent, delayMs_);
            }
        }
        else if (hoverTarget_ != nullptr) {
            hoverTarget_ = nullptr;
            Dismiss();
        }
    }

    float Tooltip::hintWidth() const {
        if (prefWidth() > 0.0f) return prefWidth();
        return 0.0f;
    }

    float Tooltip::hintHeight() const {
        if (prefHeight() > 0.0f) return prefHeight();
        return 0.0f;
    }

    FontDesc Tooltip::titleFont() const {
        FontDesc f = textStyle_.font;
        f.weight = FontWeight::Bold;
        if (titlePixelSize_ > 0)
            f.pixelSize = titlePixelSize_;
        else if (f.pixelSize > 0)
            f.pixelSize = f.pixelSize + 2;
        return f;
    }

    float Tooltip::lineHeightFor(const FontDesc& font) const {
        if (Window* w = window()) {
            const TextMetrics probe = w->MeasureText("Hg", font);
            if (probe.height > 0.0f) return probe.height;
        }
        if (font.pixelSize > 0)
            return static_cast<float>(font.pixelSize) * 1.35f;
        return kFallbackLineH;
    }

    void Tooltip::measureContent(float& w, float& h) const {
        const FontDesc bodyFont = textStyle_.font;
        const FontDesc tFont = titleFont();
        const float bodyLH = lineHeightFor(bodyFont);
        const float titleLH = lineHeightFor(tFont);

        float titleW = 0.0f;
        float textW = 0.0f;
        int   textLines = 0;

        const Window* win = window();
        if (!content_.title.empty()) {
            titleW = win ? win->MeasureText(content_.title, tFont).width
                         : static_cast<float>(Utf8Count(content_.title))
                               * static_cast<float>(tFont.pixelSize) * 0.6f;
        }
        if (!content_.text.empty()) {
            const auto lines = drawutil::SplitLines(content_.text);
            textLines = static_cast<int>(lines.size());
            for (const std::string& line : lines) {
                if (line.empty()) continue;
                const float lw = win ? win->MeasureText(line, bodyFont).width
                                     : static_cast<float>(Utf8Count(line))
                                           * static_cast<float>(bodyFont.pixelSize) * 0.6f;
                textW = std::max(textW, lw);
            }
        }

        const bool hasIcon = (iconSize_ > 0.0f) && !content_.icon.empty();
        const float iconSpan = hasIcon ? iconSize_ + iconGap_ : 0.0f;

        const float colW = std::max(titleW, textW);
        const float colH = (content_.title.empty() ? 0.0f : titleLH)
                         + static_cast<float>(textLines) * bodyLH;

        const float decorInset = (shape_ == TooltipShape::Card) ? 8.0f : 0.0f;

        w = iconSpan + colW + decorInset;
        h = std::max(colH, hasIcon ? iconSize_ : 0.0f);
    }

    void Tooltip::anchorForTarget(const Widget& target, float bw, float bh,
                                  float& ax, float& ay, Arrow& arrow) const {
        const bool withArrow = arrow_ && shapeHasArrow();
        const float tipGap = withArrow ? arrowLength() : 0.0f;
        const bool vertical = (placement_ != Placement::Left &&
                               placement_ != Placement::Right);
        arrow = Arrow::None;

        float bx = 0.0f, by = 0.0f;
        float tipX = 0.0f, tipY = 0.0f;
        Window* w = window();
        const float winW = w ? static_cast<float>(w->width()) : 0.0f;
        const float winH = w ? static_cast<float>(w->height()) : 0.0f;

        if (vertical) {
            bx = target.x() + (target.width() - bw) * 0.5f;
            if (winW > 0.0f) {
                if (bx + bw > winW - winMargin_) bx = winW - winMargin_ - bw;
                if (bx < winMargin_) bx = winMargin_;
            }
            const bool fitsBelow = (winH <= 0.0f) ||
                (target.y() + target.height() + offset_ + tipGap + bh <= winH - winMargin_);
            const bool fitsAbove = (winH <= 0.0f) ||
                (target.y() - offset_ - tipGap - bh >= winMargin_);
            const bool wantBelow = (placement_ != Placement::Top);
            const bool below = wantBelow ? (fitsBelow || !fitsAbove)
                                         : (fitsAbove ? false : fitsBelow);
            if (below) {
                by = target.y() + target.height() + offset_ + tipGap;
                arrow = withArrow ? Arrow::Up : Arrow::None;
                tipY = target.y() + target.height() + offset_;
            }
            else {
                by = target.y() - offset_ - tipGap - bh;
                arrow = withArrow ? Arrow::Down : Arrow::None;
                tipY = target.y() - offset_;
            }
            tipX = target.x() + target.width() * 0.5f;
            if (winH > 0.0f) {
                if (by < winMargin_) by = winMargin_;
                if (by + bh > winH - winMargin_) by = winH - winMargin_ - bh;
            }
        }
        else {
            by = target.y() + (target.height() - bh) * 0.5f;
            if (winH > 0.0f) {
                if (by + bh > winH - winMargin_) by = winH - winMargin_ - bh;
                if (by < winMargin_) by = winMargin_;
            }
            const bool fitsRight = (winW <= 0.0f) ||
                (target.x() + target.width() + offset_ + tipGap + bw <= winW - winMargin_);
            const bool fitsLeft = (winW <= 0.0f) ||
                (target.x() - offset_ - tipGap - bw >= winMargin_);
            const bool wantRight = (placement_ == Placement::Right);
            const bool rightSide = wantRight ? (fitsRight || !fitsLeft)
                                             : (fitsLeft ? false : fitsRight);
            if (rightSide) {
                bx = target.x() + target.width() + offset_ + tipGap;
                arrow = withArrow ? Arrow::Left : Arrow::None;
                tipX = target.x() + target.width() + offset_;
            }
            else {
                bx = target.x() - offset_ - tipGap - bw;
                arrow = withArrow ? Arrow::Right : Arrow::None;
                tipX = target.x() - offset_;
            }
            tipY = target.y() + target.height() * 0.5f;
            if (winW > 0.0f) {
                if (bx < winMargin_) bx = winMargin_;
                if (bx + bw > winW - winMargin_) bx = winW - winMargin_ - bw;
            }
        }

        const float inset = std::max(10.0f, box_.padding.left + 2.0f);
        if (arrow == Arrow::Up || arrow == Arrow::Down) {
            const float lo = bx + inset;
            const float hi = bx + bw - inset;
            if (hi > lo) tipX = std::clamp(tipX, lo, hi);
        }
        else if (arrow == Arrow::Left || arrow == Arrow::Right) {
            const float lo = by + inset;
            const float hi = by + bh - inset;
            if (hi > lo) tipY = std::clamp(tipY, lo, hi);
        }

        ax = bx;
        ay = by;
        arrowTipX_ = tipX;
        arrowTipY_ = tipY;
    }

    void Tooltip::applyShowFor(Widget& target, TooltipContent content) {
        if (content.empty()) { Hide(); return; }

        content_ = std::move(content);
        SetText(content_.text);

        float cw = 0.0f, ch = 0.0f;
        measureContent(cw, ch);
        sizeBubble(cw, ch);

        float ax = 0.0f, ay = 0.0f;
        Arrow arrow = Arrow::None;
        anchorForTarget(target, bw_, bh_, ax, ay, arrow);

        anchorX_ = ax;
        anchorY_ = ay;
        arrowDir_ = arrow;
        visible_ = true;
        shownTarget_ = &target;
        shownAtMs_ = detail::NowMs();
        fade_ = (fadeMs_ > 0) ? 0.0f : 1.0f;
        showNativePopup();
        requestRepaint();
    }

    Tooltip::BubbleGeom Tooltip::computeBubble() const {
        BubbleGeom g;
        if (!visible_ || bw_ <= 0.0f || bh_ <= 0.0f) return g;
        g.x = anchorX_;
        g.y = anchorY_;
        g.w = bw_;
        g.h = bh_;
        g.arrow = arrowDir_;
        g.arrowX = arrowTipX_;
        g.arrowY = arrowTipY_;
        g.valid = true;
        return g;
    }

    void Tooltip::stepTimers() const {
        const long long now = detail::NowMs();

        if (pendingShow_) {
            const long long elapsed = now - pendingSinceMs_;
            if (elapsed >= pendingDelayMs_) {
                pendingShow_ = false;
                Widget* target = pendingTarget_;
                TooltipContent c = pendingContent_;
                const bool hasAnchor = pendingHasAnchor_;
                const float px = pendingX_;
                const float py = pendingY_;
                pendingTarget_ = nullptr;

                if (hasAnchor) {
                    if (target && target->window() != nullptr)
                        const_cast<Tooltip*>(this)->applyShowFor(*target, std::move(c));
                }
                else {
                    const_cast<Tooltip*>(this)->ShowAt(px, py, std::move(c));
                }
            }
            else if (Window* w = window()) {
                w->RequestFrameWake(static_cast<int>(pendingDelayMs_ - elapsed));
            }
        }

        if (visible_) {
            if (nativePopup()) {
                fade_ = 1.0f;
            }
            else if (fadeMs_ <= 0) {
                fade_ = 1.0f;
            }
            else {
                const float t = static_cast<float>(now - shownAtMs_) / static_cast<float>(fadeMs_);
                fade_ = std::clamp(t, 0.0f, 1.0f);
                if (Plat::TraceTooltip())
                    std::cerr << "[Zekit/tooltip] fade → " << fade_ << "\n";
                if (fade_ < 1.0f) {
                    if (Window* w = window()) w->RequestFrameWake(16);
                }
            }
        }
        else {
            fade_ = 1.0f;
        }

        if (customResize_ && visible_ && shape_ == TooltipShape::Custom && customW_ <= 0.0f) {
            customResize_ = false;
            float cw = 0.0f, ch = 0.0f;
            measureContent(cw, ch);
            const float oldW = bw_, oldH = bh_;
            const_cast<Tooltip*>(this)->sizeBubble(cw, ch);
            if (bw_ != oldW || bh_ != oldH) {
                const_cast<Tooltip*>(this)->showNativePopup();
                const_cast<Tooltip*>(this)->requestRepaint();
            }
        }
    }

    bool Tooltip::wantsAnimationFrame() const {
        if (pendingShow_) return true;
        if (visible_ && fade_ < 1.0f) return true;
        return false;
    }

    bool Tooltip::paintsOnTop() const {
        if (nativePopup()) return pendingShow_;
        return visible_ || pendingShow_;
    }

    bool Tooltip::HitTest(float , float ) const {
        return false;
    }

    const BoxStyle& Tooltip::themeBox(const Theme& t) const {
        return t.tooltip.box;
    }

    const TextStyle& Tooltip::themeText(const Theme& t) const {
        return t.tooltip.text;
    }

    void Tooltip::ApplyTheme(const Theme& t) {
        TextWidget::ApplyTheme(t);

        if (!shapeSet_) shape_ = t.tooltip.shape;
        glassAlpha_ = std::clamp(t.tooltip.glassAlpha, 0.05f, 1.0f);
        arrowBorder_ = t.tooltip.arrowBorder;
        accent_ = t.tooltip.accent;
        requestRepaint();
        RequestWindowRedraw();
    }

    void Tooltip::SetBackground(const Color4f& c) {
        box_.colors.get(WidgetState::Normal).background = c;
        markStyled();
        requestRepaint();
        RequestWindowRedraw();
    }

    void Tooltip::SetBorder(float width, const Color4f& c) {
        SetBorderWidth(width);
        SetBorderColor(c);
        requestRepaint();
        RequestWindowRedraw();
    }

    void Tooltip::SetTextColor(const Color4f& c) {
        textStyle_.colors.get(WidgetState::Normal).text = c;
        markStyled();
        requestRepaint();
        RequestWindowRedraw();
    }

    void Tooltip::SetFontSize(unsigned int pixelSize) {
        textStyle_.font.pixelSize = pixelSize;
        markStyled();
        requestRepaint();
        RequestWindowRedraw();
    }

    void Tooltip::SetTitleColor(const Color4f& c) {
        titleColor_ = c;
        titleColorSet_ = true;
        requestRepaint();
        RequestWindowRedraw();
    }

    void Tooltip::SetTitleFontSize(unsigned int pixelSize) {
        titlePixelSize_ = pixelSize;
        requestRepaint();
        RequestWindowRedraw();
    }

    void Tooltip::SetIconSize(float size) {
        iconSize_ = (size > 0.0f) ? size : 0.0f;
        requestRepaint();
        RequestWindowRedraw();
    }

    void Tooltip::SetRadius(float radius) {
        BoxWidget::SetRadius(radius);
        requestRepaint();
        RequestWindowRedraw();
    }

    void Tooltip::SetPadding(float all) {
        BoxWidget::SetPadding(all);
        requestRepaint();
        RequestWindowRedraw();
    }

    void Tooltip::SetOffset(float offset) {
        offset_ = (offset > 0.0f) ? offset : 0.0f;
        RequestWindowRedraw();
    }

    void Tooltip::SetWindowMargin(float margin) {
        winMargin_ = (margin > 0.0f) ? margin : 0.0f;
        RequestWindowRedraw();
    }

    void Tooltip::SetShowDelay(int ms) {
        delayMs_ = (ms > 0) ? ms : 0;
    }

    void Tooltip::SetFadeDuration(int ms) {
        fadeMs_ = (ms > 0) ? ms : 0;
    }

    void Tooltip::SetArrowEnabled(bool on) {
        arrow_ = on;
        requestRepaint();
        RequestWindowRedraw();
    }

    void Tooltip::SetArrowSize(float size) {
        arrowSize_ = (size > 0.0f) ? size : 0.0f;
        requestRepaint();
        RequestWindowRedraw();
    }

    void Tooltip::SetShape(TooltipShape shape) {
        shape_ = shape;
        shapeSet_ = true;
        requestRepaint();
        RequestWindowRedraw();
    }

    void Tooltip::SetBubbleImage(std::string path) {
        SetBackgroundImage(std::move(path));
        customImgW_ = customImgH_ = 0.0f;
        SetShape(TooltipShape::Custom);
    }

    void Tooltip::SetContentOffset(float dx, float dy) {
        contentDX_ = dx;
        contentDY_ = dy;
        requestRepaint();
        RequestWindowRedraw();
    }

    void Tooltip::SetBubbleSize(float w, float h) {
        customW_ = (w > 0.0f) ? w : 0.0f;
        customH_ = (h > 0.0f) ? h : 0.0f;
        if (visible_) {
            float cw = 0.0f, ch = 0.0f;
            measureContent(cw, ch);
            sizeBubble(cw, ch);
            showNativePopup();
        }
        requestRepaint();
        RequestWindowRedraw();
    }

    void Tooltip::SetPlacement(Placement p) {
        placement_ = p;
        requestRepaint();
        RequestWindowRedraw();
    }

    bool Tooltip::shapeHasArrow() const {
        switch (shape_) {
        case TooltipShape::Rounded:
        case TooltipShape::Balloon:
        case TooltipShape::Info:
            return true;
        default:
            return false;
        }
    }

    bool Tooltip::centerContent() const {
        switch (shape_) {
        case TooltipShape::Circle:
        case TooltipShape::Star:
        case TooltipShape::Info:
        case TooltipShape::Cloud:
        case TooltipShape::Ribbon:
        case TooltipShape::Custom:
            return true;
        default:
            return false;
        }
    }

    float Tooltip::arrowLength() const {
        return (shape_ == TooltipShape::Balloon) ? arrowSize_ * 2.2f : arrowSize_;
    }

    float Tooltip::bubbleRadius(float themeRadius) const {
        switch (shape_) {
        case TooltipShape::Square:      return 0.0f;
        case TooltipShape::Notch:       return 0.0f;
        case TooltipShape::Ribbon:      return 0.0f;
        case TooltipShape::Cloud:       return bh_ * 0.34f;
        case TooltipShape::Pill:        return bh_ * 0.5f;
        case TooltipShape::Circle:      return bh_ * 0.5f;
        case TooltipShape::Info:        return bh_ * 0.5f;
        case TooltipShape::Neon:        return themeRadius;
        case TooltipShape::Custom:      return 0.0f;
        default:                        return themeRadius;
        }
    }

    void Tooltip::sizeBubble(float cw, float ch) {
        const float bt = BorderOf(box_);
        const float padX = box_.padding.left + box_.padding.right + bt * 2.0f;
        const float padY = box_.padding.top + box_.padding.bottom + bt * 2.0f;

        switch (shape_) {
        case TooltipShape::Circle:
        case TooltipShape::Info: {
            const float side = std::max(cw, ch) + std::max(padX, padY) + 8.0f;
            bw_ = side; bh_ = side;
            break;
        }
        case TooltipShape::Star: {
            const float side = std::max(cw, ch) * 2.4f + std::max(padX, padY);
            bw_ = side; bh_ = side;
            break;
        }
        case TooltipShape::Notch: {
            bw_ = cw + padX + kNotchCut;
            bh_ = ch + padY;
            break;
        }
        case TooltipShape::Ribbon: {
            bw_ = cw + padX + 2.0f * kRibbonTail;
            bh_ = ch + padY;
            break;
        }
        case TooltipShape::Cloud: {
            bw_ = cw + padX + kCloudPadX;
            bh_ = ch + padY + kCloudPadY;
            break;
        }
        case TooltipShape::Neon: {
            bw_ = cw + padX + kNeonReserve;
            bh_ = ch + padY + kNeonReserve;
            break;
        }
        case TooltipShape::Custom: {
            const float wantW = (customW_ > 0.0f) ? customW_ : customImgW_;
            const float wantH = (customH_ > 0.0f) ? customH_ : customImgH_;
            bw_ = std::max(cw + padX, wantW);
            bh_ = std::max(ch + padY, wantH);
            break;
        }
        default:
            bw_ = cw + padX;
            bh_ = ch + padY;
            break;
        }
    }

    void Tooltip::Paint(FrameCollector& fc) const {
        stepTimers();
        if (!visible_) return;
        if (nativePopup()) return;
        paintContent(fc);
    }

    void Tooltip::paintContent(FrameCollector& fc) const {
        paintPopupInto(fc, false, true);
    }

    void Tooltip::paintPopupInto(FrameCollector& fc, bool localPopup, bool drawArrow) const {
        const BubbleGeom g0 = computeBubble();
        if (!g0.valid) return;

        const float baseX = localPopup ? popupOriginX_ : 0.0f;
        const float baseY = localPopup ? popupOriginY_ : 0.0f;
        const float gx = g0.x - baseX;
        const float gy = g0.y - baseY;

        if (Plat::TraceTooltip())
            std::cerr << "[Zekit/tooltip] paint  " << (localPopup ? "popup" : "host  ")
            << " fade=" << fade_ << " visible=" << visible_
            << " bubble=" << g0.w << "x" << g0.h
            << " origin=(" << baseX << "," << baseY << ")"
            << " local=(" << gx << "," << gy << ")"
            << " arrow=(" << g0.arrowX << "," << g0.arrowY << ")"
            << " carrier=" << (overlay_ && overlay_->nativeWindow()
                    ? std::to_string(overlay_->nativeWindow()->width()) + "x"
                      + std::to_string(overlay_->nativeWindow()->height())
                    : std::string("?"))
            << "\n";

        fc.ResetClip();

        const float bt = BorderOf(box_);
        const float fade = std::clamp(fade_, 0.0f, 1.0f);
        if (fade <= 0.0f) return;

        const float radius = bubbleRadius(resolveRadius(box_, fc.GetTheme()));
        const ColorSet cs = box_.colors.get(WidgetState::Normal);

        Color4f bodyColor = cs.background;
        bool    outlined = false;
        bool    accentBar = false;
        const bool starShape = (shape_ == TooltipShape::Star);
        const bool cloudShape = (shape_ == TooltipShape::Cloud);
        const bool neonShape = (shape_ == TooltipShape::Neon);
        const bool customShape = (shape_ == TooltipShape::Custom);
        const bool polyShape = (shape_ == TooltipShape::Notch ||
                                shape_ == TooltipShape::Ribbon);
        if (shape_ == TooltipShape::Glass) {
            bodyColor.a *= glassAlpha_;
            outlined = true;
        }
        else if (shape_ == TooltipShape::Card) {
            accentBar = true;
        }
        else if (customShape) {
            bodyColor.a = 0.0f;
        }
        if (bt > 0.0f && !customShape) outlined = true;

        Color4f outlineCol = cs.border;
        if (outlined && outlineCol.a <= 0.0f) outlineCol = arrowBorder_;
        if (outlined && outlineCol.a <= 0.0f) outlined = false;

        Color4f bodyFaded = bodyColor; bodyFaded.a *= fade;
        Color4f outlineFaded = outlineCol; outlineFaded.a *= fade;

        Color4f accentGlow = (accent_.a > 0.0f) ? accent_ : fc.GetTheme().primary;
        accentGlow.a *= fade;

        if (neonShape) {
            fc.PushClip(gx - kNeonReserve, gy - kNeonReserve,
                        g0.w + 2.0f * kNeonReserve, g0.h + 2.0f * kNeonReserve);
            PaintNeonGlow(fc, gx, gy, g0.w, g0.h, radius, accentGlow);
            fc.PopClip();
        }

        if (fade >= 1.0f && !starShape && !cloudShape && !polyShape && !neonShape && !customShape) {
            fc.PushClip(gx - kShadowPad, gy - kShadowPad,
                        g0.w + 2.0f * kShadowPad, g0.h + 2.0f * kShadowPad);
            drawutil::PaintBoxShadow(fc, box_, gx, gy, g0.w, g0.h, radius);
            fc.PopClip();
        }

        if (drawArrow && arrow_ && shapeHasArrow() &&
            g0.arrow != Arrow::None && arrowSize_ > 0.0f) {
            const bool balloon = (shape_ == TooltipShape::Balloon);
            const float len = arrowLength();
            const float halfW = balloon ? arrowSize_ * 0.8f : arrowSize_;
            const float tipX = g0.arrowX - baseX;
            const float tipY = g0.arrowY - baseY;
            const bool verticalArrow = (g0.arrow == Arrow::Up || g0.arrow == Arrow::Down);
            const bool pointsBack = (g0.arrow == Arrow::Down || g0.arrow == Arrow::Right);
            const float baseC = pointsBack ? (verticalArrow ? tipY - len : tipX - len)
                                           : (verticalArrow ? tipY + len : tipX + len);

            if (bt > 0.0f && outlineFaded.a > 0.0f) {
                const float baseC2 = pointsBack ? baseC - bt : baseC + bt;
                const float halfW2 = halfW + bt;
                if (verticalArrow)
                    PaintTriangle(fc, Vec2{ tipX, tipY },
                        Vec2{ tipX - halfW2, baseC2 }, Vec2{ tipX + halfW2, baseC2 }, outlineFaded);
                else
                    PaintTriangle(fc, Vec2{ tipX, tipY },
                        Vec2{ baseC2, tipY - halfW2 }, Vec2{ baseC2, tipY + halfW2 }, outlineFaded);
            }
            if (verticalArrow)
                PaintTriangle(fc, Vec2{ tipX, tipY },
                    Vec2{ tipX - halfW, baseC }, Vec2{ tipX + halfW, baseC }, bodyFaded);
            else
                PaintTriangle(fc, Vec2{ tipX, tipY },
                    Vec2{ baseC, tipY - halfW }, Vec2{ baseC, tipY + halfW }, bodyFaded);
        }

        fc.PushClip(gx, gy, g0.w, g0.h);
        if (customShape) {
        }
        else if (starShape) {
            const float R = std::min(g0.w, g0.h) * 0.5f;
            const float cx = gx + g0.w * 0.5f;
            const float cy = gy + g0.h * 0.5f;
            if (outlined && outlineFaded.a > 0.0f)
                fc.AddShape(PolygonShape(StarVertices(cx, cy, R + bt, (R + bt) * 0.42f), true),
                    outlineFaded);
            fc.AddShape(PolygonShape(StarVertices(cx, cy, R, R * 0.42f), true), bodyFaded);
        }
        else if (shape_ == TooltipShape::Cloud) {
            if (outlined && outlineFaded.a > 0.0f)
                PaintCloudSilhouette(fc, gx, gy, g0.w, g0.h, bt, outlineFaded);
            PaintCloudSilhouette(fc, gx, gy, g0.w, g0.h, 0.0f, bodyFaded);
        }
        else if (shape_ == TooltipShape::Neon) {
            Color4f ring = accentGlow;
            if (ring.a <= 0.0f) ring = outlineFaded;
            const Color4f neonBody = MixColor(bodyFaded, ring, 0.16f);
            PaintNeonBody(fc, gx, gy, g0.w, g0.h, radius, std::max(2.0f, bt), ring, neonBody);
        }
        else if (shape_ == TooltipShape::Notch || shape_ == TooltipShape::Ribbon) {
            auto polyOf = [&](float ix, float iy, float iw, float ih) {
                return (shape_ == TooltipShape::Notch)
                    ? NotchVertices(ix, iy, iw, ih, kNotchCut)
                    : RibbonVertices(ix, iy, iw, ih, kRibbonTail);
            };
            if (outlined && outlineFaded.a > 0.0f) {
                fc.AddShape(PolygonShape(polyOf(gx, gy, g0.w, g0.h), true), outlineFaded);
                fc.AddShape(PolygonShape(polyOf(gx + bt, gy + bt, g0.w - 2.0f * bt,
                                                g0.h - 2.0f * bt), true), bodyFaded);
            }
            else {
                fc.AddShape(PolygonShape(polyOf(gx, gy, g0.w, g0.h), true), bodyFaded);
            }

            if (shape_ == TooltipShape::Notch && fade > 0.0f) {
                const float cut = std::min(kNotchCut, std::min(g0.w * 0.45f, g0.h * 0.65f));
                Color4f fold = MixColor(bodyFaded, cs.text, 0.22f);
                fold.a = bodyFaded.a;
                if (fold.a > 0.0f) {
                    PaintTriangle(fc, Vec2{ gx + g0.w - cut, gy },
                        Vec2{ gx + g0.w - cut, gy + cut },
                        Vec2{ gx + g0.w,       gy + cut }, fold);
                }
            }
        }
        else if (outlined && outlineFaded.a > 0.0f) {
            drawutil::PaintRoundedRect(fc, gx, gy, g0.w, g0.h, radius, outlineFaded);
            drawutil::PaintRoundedRect(fc, gx + bt, gy + bt,
                g0.w - 2.0f * bt, g0.h - 2.0f * bt,
                std::max(0.0f, radius - bt), bodyFaded);
        }
        else {
            drawutil::PaintRoundedRect(fc, gx, gy, g0.w, g0.h, radius, bodyFaded);
        }

        if (accentBar) {
            Color4f barCol = (accent_.a > 0.0f) ? accent_ : Color4f{ 0.0f, 0.0f, 0.0f, 0.0f };
            const Theme* th = &fc.GetTheme();
            if (barCol.a <= 0.0f && th) barCol = th->primary;
            barCol.a *= fade;
            if (barCol.a > 0.0f)
                drawutil::PaintRoundedRect(fc, gx + 3.0f, gy + 4.0f, 3.0f, g0.h - 8.0f,
                    1.5f, barCol);
        }

        if (!box_.imagePath.empty() || box_.image.handle != 0) {
            if (customShape && customW_ <= 0.0f && !box_.imagePath.empty()) {
                float iwPx = 0.0f, ihPx = 0.0f;
                if (fc.ImageSize(box_.imagePath, iwPx, ihPx) && iwPx > 0.0f && ihPx > 0.0f &&
                    (customImgW_ != iwPx || customImgH_ != ihPx)) {
                    customImgW_ = iwPx;
                    customImgH_ = ihPx;
                    customResize_ = true;
                    if (Window* w = window()) w->RequestFrameWake(0);
                }
            }

            const float bx0 = gx + bt;
            const float by0 = gy + bt;
            const float bw0 = g0.w - 2.0f * bt;
            const float bh0 = g0.h - 2.0f * bt;

            float ix = 0.0f, iy = 0.0f, iw = 0.0f, ih = 0.0f;
            if (customShape) {
                ix = gx; iy = gy; iw = g0.w; ih = g0.h;
            }
            else if (shape_ == TooltipShape::Circle || shape_ == TooltipShape::Info || starShape) {
                const float half = std::min(bw0, bh0) * 0.5f;
                const float inner = starShape ? half * 0.42f : half;
                drawutil::InscribedSquare(bx0 + bw0 * 0.5f, by0 + bh0 * 0.5f, inner,
                    ix, iy, iw);
                ih = iw;
            }
            else {
                float padL = 0.0f, padT = 0.0f, padR = 0.0f, padB = 0.0f;
                if (polyShape) {
                    if (shape_ == TooltipShape::Notch) padR = kNotchCut;
                    else { padL = kRibbonTail; padR = kRibbonTail; }
                }
                else if (cloudShape) {
                    const CloudGeom cg = MakeCloudGeom(g0.w, g0.h);
                    padL = padR = kCloudPadX * 0.45f;
                    padT = cg.bandTop;
                }
                drawutil::InscribedRect(bx0 + padL, by0 + padT,
                    bw0 - padL - padR, bh0 - padT - padB, radius, ix, iy, iw, ih);
            }

            if (iw > 0.0f && ih > 0.0f) {
                Color4f tint = box_.tint;
                tint.a *= fade;
                if (shape_ == TooltipShape::Glass) tint.a *= glassAlpha_;

                if (!box_.imagePath.empty())
                    fc.AddImage(box_.imagePath, ix, iy, iw, ih,
                        box_.drawMode, box_.hAlign, box_.vAlign, tint);
                else
                    fc.AddImageHandle(box_.image.handle, ix, iy, iw, ih,
                        box_.drawMode, box_.hAlign, box_.vAlign, tint);
            }
        }

        const FontDesc bodyFont = textStyle_.font;
        const FontDesc tFont = titleFont();
        Color4f textColor = textStyle_.colors.get(WidgetState::Normal).text;
        textColor.a *= fade;
        Color4f titleColor = titleColorSet_ ? titleColor_ : textColor;
        titleColor.a *= fade;

        const float contentL = gx + bt + box_.padding.left + (accentBar ? 8.0f : 0.0f);
        const float contentT = gy + bt + box_.padding.top;

        if (centerContent()) {
            struct Row { std::string text; FontDesc font; Color4f color; };
            std::vector<Row> rows;
            if (!content_.title.empty())
                rows.push_back({ content_.title, tFont, titleColor });
            if (!content_.text.empty())
                for (const std::string& line : drawutil::SplitLines(content_.text))
                    if (!line.empty()) rows.push_back({ line, bodyFont, textColor });

            float blockH = 0.0f;
            for (const Row& r : rows) blockH += lineHeightFor(r.font);

            float areaX = gx + bt + box_.padding.left;
            float areaW = g0.w - 2.0f * bt - box_.padding.left - box_.padding.right;
            float areaY = gy + bt + box_.padding.top;
            float areaH = g0.h - 2.0f * bt - box_.padding.top - box_.padding.bottom;
            if (shape_ == TooltipShape::Cloud) {
                const CloudGeom cg = MakeCloudGeom(g0.w, g0.h);
                const float bandTop = gy + cg.bandTop;
                if (bandTop > areaY) {
                    areaH -= bandTop - areaY;
                    areaY = bandTop;
                }
            }
            if (areaW < 1.0f) areaW = 1.0f;
            if (areaH < 1.0f) areaH = 1.0f;

            float ry = areaY + (areaH - blockH) * 0.5f + contentDY_;

            for (const Row& r : rows) {
                const TextMetrics tm = fc.MeasureText(r.text, r.font);
                const float rx = areaX + (areaW - tm.width) * 0.5f + contentDX_;
                fc.AddText(r.text, r.font, rx, ry + tm.ascent, r.color);
                ry += lineHeightFor(r.font);
            }
        }
        else {
            const bool hasIcon = (iconSize_ > 0.0f) && !content_.icon.empty();
            const float iconSpan = hasIcon ? iconSize_ + iconGap_ : 0.0f;
            if (hasIcon) {
                const float ih = iconSize_;
                const float contentH = g0.h - 2.0f * bt - box_.padding.top - box_.padding.bottom;
                const float iy = contentT + std::max(0.0f, (contentH - ih) * 0.5f);
                fc.AddImage(content_.icon, contentL, iy, iconSize_, ih,
                    BgImageDrawMode::Fit, BgImageHAlign::Center, BgImageVAlign::Middle,
                    Color4f{ 1.0f, 1.0f, 1.0f, fade });
            }

            const float textL = contentL + iconSpan;
            float ty = contentT;

            if (!content_.title.empty()) {
                const TextMetrics tm = fc.MeasureText(content_.title, tFont);
                fc.AddText(content_.title, tFont, textL, ty + tm.ascent, titleColor);
                ty += lineHeightFor(tFont);
            }

            const float bodyLH = lineHeightFor(bodyFont);
            if (!content_.text.empty()) {
                for (const std::string& line : drawutil::SplitLines(content_.text)) {
                    if (!line.empty()) {
                        const TextMetrics tm = fc.MeasureText(line, bodyFont);
                        fc.AddText(line, bodyFont, textL, ty + tm.ascent, textColor);
                    }
                    ty += bodyLH;
                }
            }
        }

        fc.PopClip();
    }

    bool Tooltip::nativePopup() const {
        return overlay_ && overlay_->isVisible();
    }

    void Tooltip::showNativePopup() {
        Window* w = window();
        if (!w || w->IsClosed()) return;
        if (!OverlayWindow::SupportsNative(*w)) return;

        if (!overlay_) {
            overlay_ = std::make_shared<OverlayWindow>(*w, OverlayLayer::Tooltip);
            overlay_->SetOnTakenOver([this] { Hide(); });

            OverlayContent content;
            content.paint = [this](FrameCollector& fc) {
                paintPopupInto(fc, true, true);
            };
            overlay_->SetContent(std::move(content));
            overlay_->SetClickThrough(true);
        }

        const BubbleGeom g = computeBubble();
        if (!g.valid) return;
        if (!overlay_->isReady()) return;

        float px = g.x, py = g.y, pw = g.w, ph = g.h;
        const bool nativeArrow = arrow_ && shapeHasArrow() && g.arrow != Arrow::None;
        if (nativeArrow) {
            switch (g.arrow) {
            case Arrow::Up:
                py = g.arrowY;
                ph = (g.y + g.h) - g.arrowY;
                break;
            case Arrow::Down:
                ph = g.arrowY - g.y;
                break;
            case Arrow::Left:
                px = g.arrowX;
                pw = (g.x + g.w) - g.arrowX;
                break;
            case Arrow::Right:
                pw = g.arrowX - g.x;
                break;
            default:
                break;
            }
        }
        const float margin = (shape_ == TooltipShape::Neon) ? kNeonReserve : kShadowPad;
        px -= margin;
        py -= margin;
        pw += 2.0f * margin;
        ph += 2.0f * margin;

        popupOriginX_ = px;
        popupOriginY_ = py;

        fade_ = 1.0f;
        shownAtMs_ = detail::NowMs() - fadeMs_;
        overlay_->SetClickThrough(true);
        if (Plat::TraceTooltip())
            std::cerr << "[Zekit/tooltip] t=" << detail::NowMs()
            << " showPopup fade=" << fade_
            << " popup=" << pw << "x" << ph << "@" << px << "," << py
            << " perPixelTransparent="
            << ((overlay_->nativeWindow() && overlay_->nativeWindow()->IsPerPixelTransparent()) ? 1 : 0)
            << "\n";
        overlay_->Show(px, py, pw, ph);
    }

    void Tooltip::hideNativePopup() {
        if (overlay_) overlay_->Hide();
    }

    void Tooltip::requestRepaint() {
        RequestWindowRedraw();
    }
}
