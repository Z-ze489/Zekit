
#include "pch.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>
#include <utility>

#include "include/ZeLoading.h"
#include "include/Zekit.h"
#include "ZeDrawUtil.h"

namespace Kit {

    namespace {
        constexpr int kFrameMs = 16;

        float WrapTau(float a) {
            a = std::fmod(a, drawutil::kTau);
            if (a < 0.0f) a += drawutil::kTau;
            return a;
        }

        float TailIntensity(float lead) {
            lead = std::clamp(lead, 0.0f, 1.0f);
            return 1.0f - lead;
        }

        constexpr float kSkeletonBarH = 12.0f;
        constexpr float kSkeletonGapK = 0.7f;
        constexpr float kSkeletonDefaultW = 220.0f;
        constexpr int kSkeletonMaxRows = 8;
    }

    Loading::Loading(LoadingVariant variant)
        : variant_(variant) {
        ApplyTheme(Themes::Light());
        SetFocusable(false);
        SetDefaultSize(28.0f, 28.0f);
        lastTickMs_ = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    void Loading::SetVariant(LoadingVariant v) {
        variant_ = v;
        RequestWindowRedraw();
    }

    void Loading::SetRunning(bool on) {
        if (running_ == on) return;

        const bool wasAnimating = wantsAnimationFrame();

        running_ = on;
        if (running_) {
            lastTickMs_ = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            if (!wasAnimating) {
                RequestWindowRedraw();
            }
        }
        RequestWindowRedraw();
    }

    void Loading::SetPercent(float k) {
        if (std::isnan(k) || k < 0.0f) {
            percent_ = -1.0f;
        }
        else {
            percent_ = std::clamp(k, 0.0f, 1.0f);
        }
        RequestWindowRedraw();
    }

    void Loading::SetSize(float px) {
        size_ = (px > 0.0f) ? px : 0.0f;
        markStyled();
        notifyLayoutInputChanged();
        RequestWindowRedraw();
    }

    void Loading::SetThickness(float px) {
        thickness_ = (px > 0.0f) ? px : 0.0f;
        markStyled();
        RequestWindowRedraw();
    }

    void Loading::SetColor(const Color4f& c) {
        colorSet_ = true;
        color_ = c;
        markStyled();
        RequestWindowRedraw();
    }

    void Loading::SetTrackColor(const Color4f& c) {
        trackColorSet_ = true;
        trackColor_ = c;
        markStyled();
        RequestWindowRedraw();
    }

    void Loading::SetSpeed(int ms) {
        speedMs_ = (ms > 0) ? ms : 1;
        markStyled();
        RequestWindowRedraw();
    }

    void Loading::SetItemCount(int n) {
        itemCount_ = (n >= 3) ? n : 3;
        markStyled();
        RequestWindowRedraw();
    }

    void Loading::SetLastLineRatio(float k) {
        if (std::isnan(k)) return;
        lastLineRatio_ = std::clamp(k, 0.05f, 1.0f);
        RequestWindowRedraw();
    }

    void Loading::SetShimmer(bool on) {
        if (shimmer_ == on) return;
        shimmer_ = on;
        RequestWindowRedraw();
    }

    void Loading::SetText(std::string text) {
        text_ = std::move(text);
        notifyLayoutInputChanged();
        RequestWindowRedraw();
    }

    void Loading::SetPhase(float radians) {
        phase_ = WrapTau(radians);
        RequestWindowRedraw();
    }

    void Loading::advance() const {
        if (!wantsAnimationFrame()) return;

        const long long now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        if (lastTickMs_ <= 0) lastTickMs_ = now;

        long long dt = now - lastTickMs_;
        if (dt < 0) dt = 0;
        if (dt > 200) dt = 200;
        lastTickMs_ = now;

        const float speed = static_cast<float>((speedMs_ > 0) ? speedMs_ : themeSpeedMs_);
        phase_ = WrapTau(phase_ + drawutil::kTau * static_cast<float>(dt) / std::max(1.0f, speed));

        if (Window* w = window()) w->RequestFrameWake(kFrameMs);
    }

    bool Loading::wantsAnimationFrame() const {
        if (!running_ || !indeterminate()) return false;
        if (variant_ == LoadingVariant::Skeleton && !shimmer_) return false;
        return true;
    }

    void Loading::ApplyTheme(const Theme& t) {
        const LoadingStyle& ls = t.loading;
        if (!colorSet_)      color_ = ls.color;
        if (!trackColorSet_) trackColor_ = ls.trackColor;
        themeSize_ = (ls.size > 0.0f) ? ls.size : 28.0f;
        themeThickness_ = (ls.thickness > 0.0f) ? ls.thickness : 3.0f;
        themeTextGap_ = (ls.textGap >= 0.0f) ? ls.textGap : 0.0f;
        themeItemCount_ = (ls.itemCount >= 3) ? ls.itemCount : 3;
        themeSpeedMs_ = (ls.speedMs > 0) ? ls.speedMs : 900;

        textStyle_ = ls.text;

        box_.colors = MultiStateColors{};
        box_.border = Border{};
        box_.padding = Padding{};
        box_.radiusMode = RadiusMode::None;
        box_.shadow.enabled = false;
        box_.outline.enabled = false;
        box_.opacity = 1.0f;
    }

    float Loading::indicatorSide() const {
        return (size_ > 0.0f) ? size_ : themeSize_;
    }

    float Loading::resolvedThickness() const {
        const float s = indicatorSide();
        float th = (thickness_ > 0.0f) ? thickness_ : themeThickness_;
        return std::min(th, std::max(1.0f, s * 0.5f));
    }

    float Loading::hintWidth() const {
        if (prefWidth() > 0.0f) return prefWidth();

        if (variant_ == LoadingVariant::Skeleton) return kSkeletonDefaultW;

        float w = indicatorSide();
        if (hasText()) {
            if (const Window* win = window()) {
                const float tw = const_cast<Window*>(win)->MeasureText(text_, textStyle_.font).width;
                if (tw > 0.0f) w += themeTextGap_ + tw;
            }
        }
        return w;
    }

    float Loading::hintHeight() const {
        if (prefHeight() > 0.0f) return prefHeight();

        if (variant_ == LoadingVariant::Skeleton) {
            const int rows = std::clamp((itemCount_ > 0) ? itemCount_ : 3, 1, kSkeletonMaxRows);
            const float barH = (thickness_ > 0.0f) ? std::max(thickness_, 4.0f) : kSkeletonBarH;
            return static_cast<float>(rows) * barH
                + static_cast<float>(rows - 1) * barH * kSkeletonGapK;
        }

        float hh = indicatorSide();
        if (hasText()) {
            if (const Window* win = window()) {
                const float th = const_cast<Window*>(win)
                    ->MeasureText(std::string_view("Ag"), textStyle_.font).height;
                hh = std::max(hh, th);
            }
        }
        return hh;
    }

    void Loading::paintContent(FrameCollector& fc) const {
        const float w = width();
        const float h = height();
        if (w <= 0.0f || h <= 0.0f) return;

        advance();

        if (variant_ == LoadingVariant::Skeleton) {
            paintSkeleton(fc);
            return;
        }

        const float side = std::min(indicatorSide(), std::min(w, h));
        if (side <= 0.0f) return;

        const float ax = x();
        const float ay = y() + (h - side) * 0.5f;

        switch (variant_) {
        case LoadingVariant::Dots:    paintDots(fc, ax, ay, side);    break;
        case LoadingVariant::Bars:    paintBars(fc, ax, ay, side);    break;
        case LoadingVariant::Spinner: paintSpinner(fc, ax, ay, side); break;
        case LoadingVariant::Pulse:   paintPulse(fc, ax, ay, side);   break;
        case LoadingVariant::Grid:    paintGrid(fc, ax, ay, side);    break;
        case LoadingVariant::Bounce:  paintBounce(fc, ax, ay, side);  break;
        case LoadingVariant::Wave:    paintWave(fc, ax, ay, side);    break;
        case LoadingVariant::Orbit:   paintOrbit(fc, ax, ay, side);   break;
        case LoadingVariant::Skeleton: break;
        case LoadingVariant::Ring:
        default:                      paintRing(fc, ax, ay, side);    break;
        }

        if (!hasText()) return;

        const float textX = ax + side + themeTextGap_;
        const float avail = std::max(0.0f, (x() + w) - textX);
        if (avail <= 0.0f) return;

        const FontDesc& font = textStyle_.font;
        Color4f fg = textStyle_.colors.get(WidgetState::Normal).text;
        fg.a *= box_.opacity;

        std::string line = text_;
        TextMetrics tm = fc.MeasureText(line, font);
        if (tm.width > avail) {
            line = drawutil::Ellipsize(fc, text_, font, avail);
            tm = fc.MeasureText(line, font);
        }
        if (tm.width <= 0.0f) return;

        const float ly = y() + (h - tm.height) * 0.5f;
        fc.AddText(line, font, textX, ly + tm.ascent, fg);
    }

    void Loading::paintRing(FrameCollector& fc, float ax, float ay, float side) const {
        const float th = resolvedThickness();
        const float r = std::max(0.0f, side * 0.5f - th * 0.5f);
        if (r <= 0.0f) return;

        const float cx = ax + side * 0.5f;
        const float cy = ay + side * 0.5f;

        Color4f c = color_;
        c.a *= box_.opacity;
        if (c.a <= 0.0f) return;

        if (!indeterminate()) {
            Color4f track = trackColor_;
            track.a *= box_.opacity;
            if (track.a > 0.0f) {
                drawutil::PaintCircleOutline(fc, cx, cy, r, th, track);
            }
            if (percent_ <= 0.0f) return;

            const float start = -drawutil::kPi * 0.5f;
            const float sweep = drawutil::kTau * percent_;
            drawutil::PaintArc(fc, cx, cy, r, start, start + sweep, th, c);
            return;
        }

        const float sweep = drawutil::kPi * 1.5f;
        drawutil::PaintArc(fc, cx, cy, r, phase_, phase_ + sweep, th, c);
    }

    void Loading::paintDots(FrameCollector& fc, float ax, float ay, float side) const {
        const float th = resolvedThickness();
        const int n = (itemCount_ > 0) ? itemCount_ : themeItemCount_;
        const float dotR = std::max(0.5f, th * 0.5f);
        const float orbit = std::max(0.0f, side * 0.5f - dotR);
        if (orbit <= 0.0f) return;

        const float cx = ax + side * 0.5f;
        const float cy = ay + side * 0.5f;

        Color4f base = color_;
        base.a *= box_.opacity;
        if (base.a <= 0.0f) return;

        for (int i = 0; i < n; ++i) {
            const float ang = phase_ - drawutil::kTau * static_cast<float>(i) / static_cast<float>(n);
            const float lead = static_cast<float>(i) / static_cast<float>(n);

            Color4f c = base;
            c.a *= 0.34f + 0.66f * TailIntensity(lead);
            const float rr = dotR * (0.62f + 0.38f * TailIntensity(lead));
            drawutil::PaintCircle(fc, cx + orbit * std::cos(ang), cy + orbit * std::sin(ang), rr, c);
        }
    }

    void Loading::paintBars(FrameCollector& fc, float ax, float ay, float side) const {
        const int n = (itemCount_ > 0) ? itemCount_ : themeItemCount_;
        if (n < 3) return;

        const float gap = std::max(1.0f, side * 0.05f);
        float barW = (side - gap * static_cast<float>(n - 1)) / static_cast<float>(n);
        if (barW < 0.75f) return;
        barW = std::min(barW, std::max(1.0f, (thickness_ > 0.0f) ? thickness_ : themeThickness_));

        const float totalW = barW * static_cast<float>(n) + gap * static_cast<float>(n - 1);

        Color4f base = color_;
        base.a *= box_.opacity;
        if (base.a <= 0.0f) return;

        const float x0 = ax + (side - totalW) * 0.5f;
        const float cy = ay + side * 0.5f;
        const float maxH = side;

        for (int i = 0; i < n; ++i) {
            const float lead = static_cast<float>(i) / static_cast<float>(n);
            const float k = 0.30f + 0.70f * TailIntensity(lead);

            const float wave = 0.5f + 0.5f * std::sin(phase_ + drawutil::kTau * static_cast<float>(i)
                / static_cast<float>(n));
            const float hh = std::max(barW, maxH * k * (0.45f + 0.55f * wave));

            const float bx = x0 + (barW + gap) * static_cast<float>(i);
            drawutil::PaintRoundedRect(fc, bx, cy - hh * 0.5f, barW, hh, barW * 0.5f, base);
        }
    }

    void Loading::paintSpinner(FrameCollector& fc, float ax, float ay, float side) const {
        int n = (itemCount_ > 0) ? itemCount_ : themeItemCount_;
        n = std::clamp(n, 8, 10);

        const float th = std::max(1.0f, resolvedThickness() * 0.7f);
        const float outer = side * 0.5f - th * 0.5f;
        if (outer <= 0.0f) return;

        const float cx = ax + side * 0.5f;
        const float cy = ay + side * 0.5f;

        Color4f base = color_;
        base.a *= box_.opacity;
        if (base.a <= 0.0f) return;

        for (int i = 0; i < n; ++i) {
            const float lead = static_cast<float>(i) / static_cast<float>(n);
            const float ang = phase_ - drawutil::kTau * lead;
            const float k = TailIntensity(lead);

            Color4f c = base;
            c.a *= 0.06f + 0.94f * std::pow(k, 2.2f);
            if (c.a <= 0.004f) continue;

            const float r0 = outer * (0.42f + 0.42f * lead);

            drawutil::PaintThickSegment(fc,
                cx + r0 * std::cos(ang), cy + r0 * std::sin(ang),
                cx + outer * std::cos(ang), cy + outer * std::sin(ang),
                th, c);
        }
    }

    void Loading::paintPulse(FrameCollector& fc, float ax, float ay, float side) const {
        const float th = resolvedThickness() * 0.8f;
        const float maxR = side * 0.5f - th * 0.5f;
        if (maxR <= 0.0f) return;

        const float cx = ax + side * 0.5f;
        const float cy = ay + side * 0.5f;

        Color4f base = color_;
        base.a *= box_.opacity;
        if (base.a <= 0.0f) return;

        constexpr int kRings = 3;
        for (int k = 0; k < kRings; ++k) {
            float t = phase_ / drawutil::kTau + static_cast<float>(k) / kRings;
            t -= std::floor(t);
            const float r = maxR * (0.12f + 0.88f * t);
            Color4f c = base;
            c.a *= (1.0f - t) * 0.9f;
            if (c.a <= 0.004f) continue;
            drawutil::PaintCircleOutline(fc, cx, cy, r, th, c);
        }

        Color4f dot = base;
        dot.a *= 0.9f;
        drawutil::PaintCircle(fc, cx, cy, std::max(1.5f, th * 0.75f), dot);
    }

    void Loading::paintGrid(FrameCollector& fc, float ax, float ay, float side) const {
        constexpr int kDim = 3;
        const float cell = side / static_cast<float>(kDim);
        const float maxR = cell * 0.31f;
        if (maxR <= 0.5f) return;

        const float th = resolvedThickness();
        const float dotR0 = std::max(0.5f, std::min(maxR, th * 0.9f));

        Color4f base = color_;
        base.a *= box_.opacity;
        if (base.a <= 0.0f) return;

        const float x0 = ax + cell * 0.5f;
        const float y0 = ay + cell * 0.5f;

        for (int gy = 0; gy < kDim; ++gy) {
            for (int gx = 0; gx < kDim; ++gx) {
                const float k = 0.5f + 0.5f * std::sin(
                    phase_ - static_cast<float>(gx + gy) * 0.62f);

                Color4f c = base;
                c.a *= 0.28f + 0.72f * k;
                const float r = dotR0 * (0.68f + 0.32f * k);
                drawutil::PaintCircle(fc,
                    x0 + cell * static_cast<float>(gx),
                    y0 + cell * static_cast<float>(gy), r, c);
            }
        }
    }

    void Loading::paintBounce(FrameCollector& fc, float ax, float ay, float side) const {
        int n = (itemCount_ > 0) ? itemCount_ : themeItemCount_;
        n = std::clamp(n, 2, 5);

        const float th = resolvedThickness();
        const float ballR = std::clamp(std::max(th * 0.6f, side * 0.095f),
                                       2.0f, side * 0.16f);

        const float span = std::min(side, ballR * 2.2f * static_cast<float>(n));
        const float step = (n > 1) ? (span - ballR * 2.0f) / static_cast<float>(n - 1) : 0.0f;
        const float x0 = ax + (side - span) * 0.5f + ballR;

        const float groundY = ay + side - ballR;
        const float lift = side - ballR * 2.2f;

        Color4f base = color_;
        base.a *= box_.opacity;
        if (base.a <= 0.0f) return;

        for (int i = 0; i < n; ++i) {
            const float t = phase_ + drawutil::kTau * static_cast<float>(i) / static_cast<float>(n);
            const float s = std::fabs(std::sin(t));
            const float cy = groundY - lift * s;

            const float squash = 1.0f - s;
            const float sx = ballR * (1.0f + 0.22f * squash);
            const float sy = ballR * (1.0f - 0.30f * squash);

            Color4f c = base;
            c.a *= 0.65f + 0.35f * s;
            drawutil::PaintRoundedRect(fc, x0 + step * static_cast<float>(i) - sx,
                cy - sy, sx * 2.0f, sy * 2.0f, std::min(sx, sy), c);
        }
    }

    void Loading::paintWave(FrameCollector& fc, float ax, float ay, float side) const {
        const float th = std::max(1.0f, resolvedThickness() * 0.75f);
        const float amp = side * 0.16f;
        const float cy = ay + side * 0.5f;
        const float x0 = ax + th * 0.5f;
        const float x1 = ax + side - th * 0.5f;
        if (x1 <= x0) return;

        Color4f base = color_;
        base.a *= box_.opacity;
        if (base.a <= 0.0f) return;

        const int segs = std::max(8, static_cast<int>((x1 - x0) / 3.0f));
        const float k = drawutil::kTau * 1.6f / (x1 - x0);

        for (int layer = 0; layer < 2; ++layer) {
            Color4f c = base;
            c.a *= (layer == 0) ? 1.0f : 0.45f;
            const float ph = phase_ + (layer == 0 ? 0.0f : drawutil::kPi);
            const float ampL = amp * (layer == 0 ? 1.0f : 0.7f);

            float px = x0;
            float py = cy + ampL * std::sin(k * (px - x0) - ph);
            for (int i = 1; i <= segs; ++i) {
                const float x = x0 + (x1 - x0) * static_cast<float>(i) / static_cast<float>(segs);
                const float y = cy + ampL * std::sin(k * (x - x0) - ph);
                drawutil::PaintThickSegment(fc, px, py, x, y, th, c);
                px = x;
                py = y;
            }
        }
    }

    void Loading::paintOrbit(FrameCollector& fc, float ax, float ay, float side) const {
        const float th = resolvedThickness();
        const float ballR = std::clamp(std::max(th * 0.7f, side * 0.085f),
                                       2.0f, side * 0.14f);
        const float trackTh = std::max(1.0f, th * 0.30f);
        const float rOuter = std::max(0.0f, side * 0.5f - ballR);
        const float rInner = rOuter * 0.52f;
        if (rOuter <= 0.0f) return;

        const float cx = ax + side * 0.5f;
        const float cy = ay + side * 0.5f;

        Color4f base = color_;
        base.a *= box_.opacity;
        if (base.a <= 0.0f) return;

        Color4f track = trackColor_;
        track.a *= box_.opacity;
        if (track.a > 0.0f) {
            drawutil::PaintCircleOutline(fc, cx, cy, rOuter, trackTh, track);
            drawutil::PaintCircleOutline(fc, cx, cy, rInner, trackTh, track);
        }

        const float aOut = phase_;
        const float aIn = -phase_ * 1.5f;

        Color4f c1 = base;
        drawutil::PaintCircle(fc, cx + rOuter * std::cos(aOut), cy + rOuter * std::sin(aOut),
            ballR, c1);

        Color4f c2 = base;
        c2.a *= 0.75f;
        drawutil::PaintCircle(fc, cx + rInner * std::cos(aIn), cy + rInner * std::sin(aIn),
            ballR * 0.8f, c2);
    }

    void Loading::paintSkeleton(FrameCollector& fc) const {
        const float w = width();
        const float h = height();
        if (w <= 0.0f || h <= 0.0f) return;

        const int rows = std::clamp((itemCount_ > 0) ? itemCount_ : 3, 1, kSkeletonMaxRows);
        float barH = (thickness_ > 0.0f) ? std::max(thickness_, 4.0f) : kSkeletonBarH;

        float totalH = static_cast<float>(rows) * barH
            + static_cast<float>(rows - 1) * barH * kSkeletonGapK;
        if (totalH > h && totalH > 0.0f) {
            const float k = h / totalH;
            barH *= k;
            totalH = h;
        }
        const float gap = barH * kSkeletonGapK;

        Color4f base = color_;
        base.a *= box_.opacity;
        if (base.a <= 0.0f) return;

        Color4f bar = base;
        bar.a *= 0.18f;
        Color4f hi = base;
        hi.a *= 0.55f;

        const float x0 = x();
        const float y0 = y() + (h - totalH) * 0.5f;
        const float t = phase_ / drawutil::kTau;

        for (int i = 0; i < rows; ++i) {
            const bool last = (i == rows - 1);
            const float lineW = last ? w * lastLineRatio_ : w;
            if (lineW <= 0.5f) continue;

            const float ly = y0 + static_cast<float>(i) * (barH + gap);
            const float rr = barH * 0.5f;

            drawutil::PaintRoundedRect(fc, x0, ly, lineW, barH, rr, bar);

            if (!shimmer_) continue;

            const float hiW = std::max(barH * 2.0f, lineW * 0.32f);
            const float center = x0 + (lineW + hiW) * t - hiW * 0.5f;
            float hx = center - hiW * 0.5f;
            float hw = hiW;
            if (hx < x0) { hw -= (x0 - hx); hx = x0; }
            if (hx + hw > x0 + lineW) hw = (x0 + lineW) - hx;
            if (hw <= 0.5f) continue;

            drawutil::PaintRoundedRect(fc, hx, ly, hw, barH, rr, hi);
        }
    }

}
