
#include "pch.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>

#include "include/ZeSlider.h"
#include "ZeDrawUtil.h"
#include "ZeValueAnim.h"
#include "ZeValueText.h"
#include "include/Zekit.h"

#include "ZeFrameCollector.h"
#include "ZeRoundedRectShape.h"

namespace Kit {

    namespace {
        constexpr float kDefaultLength = 160.0f;
        constexpr float kDefaultThickness = 24.0f;
    }

    Slider::Slider() {
        ApplyTheme(Themes::Light());

        SetFocusable(true);

        SetDefaultSize(kDefaultLength, kDefaultThickness);

        target_ = value_;
    }

    float Slider::hintWidth() const {
        if (prefWidth() > 0.0f) return prefWidth();

        float dx = 0.0f, dy = 0.0f;
        drawutil::BoxInsets(box_, dx, dy);
        (void)dy;

        const float len = isVertical() ? kDefaultThickness : kDefaultLength;
        return len + dx;
    }

    float Slider::hintHeight() const {
        if (prefHeight() > 0.0f) return prefHeight();

        float dx = 0.0f, dy = 0.0f;
        drawutil::BoxInsets(box_, dx, dy);
        (void)dx;

        const float len = isVertical() ? kDefaultLength : kDefaultThickness;
        return len + dy;
    }

    void Slider::SetMin(float min) {
        min_ = min;
        if (max_ < min_) std::swap(min_, max_);
        value_ = std::clamp(value_, min_, max_);
        target_ = std::clamp(target_, min_, max_);
        RequestWindowRedraw();
    }

    void Slider::SetMax(float max) {
        max_ = max;
        if (min_ > max_) std::swap(min_, max_);
        value_ = std::clamp(value_, min_, max_);
        target_ = std::clamp(target_, min_, max_);
        RequestWindowRedraw();
    }

    void Slider::SetRange(float min, float max) {
        min_ = min;
        max_ = max;
        if (min_ > max_) std::swap(min_, max_);
        value_ = std::clamp(value_, min_, max_);
        target_ = std::clamp(target_, min_, max_);
        RequestWindowRedraw();
    }

    void Slider::SetValue(float value) {
        if (std::isnan(value)) value = min_;

        float v = snapToStep(value);
        target_ = std::clamp(v, min_, max_);

        const bool wasAnimating = animating_;
        if (!animated_) {
            value_ = target_;
            animating_ = false;
        } else if (value_ != target_) {
            animFrom_ = value_;
            animStartMs_ = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            animating_ = true;
        }
        markStyled();

        if (animating_ && !wasAnimating) {
            RequestWindowRedraw();
        }
        RequestWindowRedraw();
    }

    float Slider::progress() const {
        const float span = max_ - min_;
        if (span <= 0.0f) return 0.0f;
        return std::clamp((value_ - min_) / span, 0.0f, 1.0f);
    }

    void Slider::SetProgress(float k) {
        if (std::isnan(k)) k = 0.0f;
        k = std::clamp(k, 0.0f, 1.0f);
        SetValue(min_ + k * (max_ - min_));
        RequestWindowRedraw();
    }

    float Slider::percent() const { return progress() * 100.0f; }

    void Slider::SetStep(float step) {
        step_ = (step > 0.0f) ? step : 0.0f;
        RequestWindowRedraw();
    }

    float Slider::snapToStep(float v) const {
        if (step_ <= 0.0f) return v;
        const float k = std::round((v - min_) / step_);
        return min_ + k * step_;
    }

    void Slider::SetOrientation(SliderOrientation o) {
        orientation_ = o;
        markStyled();
        RequestWindowRedraw();
    }

    void Slider::SetOnValueChanged(ValueChangedAction handler) {
        onValueChanged_ = std::move(handler);
    }

    void Slider::notifyChanged() {
        if (onValueChanged_) onValueChanged_(*this, value_);
    }

    void Slider::SetTextColor(const Color4f& c) { textColor_ = c; markStyled();   RequestWindowRedraw();}
    void Slider::SetTextFormat(std::string fmt) { textFormat_ = std::move(fmt);   RequestWindowRedraw();}

    void Slider::SetAnimated(bool on) {
        animated_ = on;
        if (!on) {
            value_ = target_;
            animating_ = false;
        }
        markStyled();
        RequestWindowRedraw();
    }

    void Slider::SetAnimationDuration(int ms) {
        animMs_ = (ms < 1) ? 1 : ms;
        RequestWindowRedraw();
    }

    void Slider::FinishAnimation() {
        value_ = target_;
        animating_ = false;
        markStyled();
    }

    void Slider::stepAnimation() const {
        if (!animating_) return;

        const detail::ValueAnimStep st = detail::StepValueAnim(animFrom_, target_, animStartMs_, animMs_);
        value_ = st.value;
        if (st.done) { animating_ = false; return; }

        if (Window* w = window()) w->RequestFrameWake(st.remainMs);
    }

    const BoxStyle& Slider::themeBox(const Theme& t) const {
        return t.slider.track;
    }

    void Slider::ApplyTheme(const Theme& t) {
        BoxWidget::ApplyTheme(t);

        trackColor_ = t.slider.trackColor;
        fillColor_ = t.slider.fillColor;
        thumbColor_ = t.slider.thumbColor;
        textColor_ = t.slider.text.colors.get(WidgetState::Normal).text;

        thumbBorderColors_ = t.slider.thumb.colors;
        thumbBorderWidth_ = std::max({ t.slider.thumb.border.width[Border::Top],
                                       t.slider.thumb.border.width[Border::Right],
                                       t.slider.thumb.border.width[Border::Bottom],
                                       t.slider.thumb.border.width[Border::Left] });

        font_ = t.slider.text.font;
    }

    void Slider::SetTrackColor(const Color4f& c) { trackColor_ = c; markStyled();   RequestWindowRedraw();}
    void Slider::SetFillColor(const Color4f& c) { fillColor_ = c; markStyled();   RequestWindowRedraw();}
    void Slider::SetThumbColor(const Color4f& c) { thumbColor_ = c; markStyled();   RequestWindowRedraw();}
    void Slider::SetThumbSize(float d) { thumbSize_ = (d > 0.0f) ? d : 0.0f; markStyled();   RequestWindowRedraw();}

    namespace {
        struct SliderGeom {
            float x = 0, y = 0, w = 0, h = 0;
            float thumbD = 0;
            float travel = 0;
            bool  vertical = false;
        };

        SliderGeom ComputeGeom(float cx, float cy, float cw, float ch, bool vertical,
                               float thumbSizeOverride) {
            SliderGeom g;
            g.x = cx; g.y = cy; g.w = cw; g.h = ch;
            g.vertical = vertical;

            const float thin = vertical ? cw : ch;
            float d = (thumbSizeOverride > 0.0f) ? thumbSizeOverride
                                                 : std::max(8.0f, thin - 4.0f);
            d = std::min(d, std::max(thin, 8.0f));
            g.thumbD = d;

            const float main = vertical ? ch : cw;
            g.travel = std::max(0.0f, main - d);
            return g;
        }

        float ThumbMainCenter(const SliderGeom& g, float k) {
            return g.thumbD * 0.5f + std::clamp(k, 0.0f, 1.0f) * g.travel;
        }
    }

    float Slider::posToProgress(float local) const {
        const float cx = x() + box_.padding.left;
        const float cy = y() + box_.padding.top;
        const float cw = width() - box_.padding.left - box_.padding.right;
        const float ch = height() - box_.padding.top - box_.padding.bottom;
        if (cw <= 0.0f || ch <= 0.0f) return 0.0f;

        const SliderGeom g = ComputeGeom(cx, cy, cw, ch, isVertical(), thumbSize_);
        if (g.travel <= 0.0f) return 0.0f;

        float rel;
        if (isVertical()) {
            const float bottom = cy + ch;
            rel = (bottom - local) - (g.thumbD * 0.5f);
        } else {
            rel = (local - cx) - (g.thumbD * 0.5f);
        }
        return std::clamp(rel / g.travel, 0.0f, 1.0f);
    }

    bool Slider::setValueByUser(float v) {
        if (std::isnan(v)) return false;
        v = snapToStep(v);
        v = std::clamp(v, min_, max_);
        if (v == value_ && v == target_) return false;

        value_ = v;
        target_ = v;
        animating_ = false;
        markStyled();
        notifyChanged();
        RequestWindowRedraw();
        return true;
    }

    bool Slider::OnMousePress(MouseEvent& e) {
        if (!isEnabled()) return false;
        if (e.button != MouseButton::Left) return false;

        const float cx = x() + box_.padding.left;
        const float cy = y() + box_.padding.top;
        const float cw = width() - box_.padding.left - box_.padding.right;
        const float ch = height() - box_.padding.top - box_.padding.bottom;
        if (cw <= 0.0f || ch <= 0.0f) return false;

        const SliderGeom g = ComputeGeom(cx, cy, cw, ch, isVertical(), thumbSize_);
        if (g.travel <= 0.0f) return false;

        const bool inside = (e.x >= cx - 1.0f && e.x <= cx + cw + 1.0f &&
                             e.y >= cy - 1.0f && e.y <= cy + ch + 1.0f);
        if (!inside) return false;

        dragging_ = true;
        dragGrab_ = g.thumbD * 0.5f;

        const float k = posToProgress(isVertical() ? e.y : e.x);
        const float raw = min_ + k * (max_ - min_);
        setValueByUser(raw);

        e.accept();
        return true;
    }

    bool Slider::OnMouseMove(MouseEvent& e) {
        if (!isEnabled()) { dragging_ = false; return false; }
        if (!dragging_) return false;

        const float k = posToProgress(isVertical() ? e.y : e.x);
        const float raw = min_ + k * (max_ - min_);
        setValueByUser(raw);

        e.accept();
        return true;
    }

    bool Slider::OnMouseRelease(MouseEvent& e) {
        if (!dragging_) return false;
        if (e.button != MouseButton::Left) return false;
        dragging_ = false;
        e.accept();
        return true;
    }

    bool Slider::OnKeyPress(KeyEvent& e) {
        if (!isEnabled()) return false;

        const float step = (step_ > 0.0f) ? step_ : ((max_ - min_) * 0.01f);

        float v = value_;
        switch (e.key) {
        case Key::LeftArrow:
        case Key::DownArrow:
            v -= step;
            break;
        case Key::RightArrow:
        case Key::UpArrow:
            v += step;
            break;
        case Key::Home:
            v = min_;
            break;
        case Key::End:
            v = max_;
            break;
        default:
            return false;
        }

        setValueByUser(v);
        e.accept();
        return true;
    }

    void Slider::paintContent(FrameCollector& fc) const {
        stepAnimation();

        const float cx = x() + box_.padding.left;
        const float cy = y() + box_.padding.top;
        const float cw = width() - box_.padding.left - box_.padding.right;
        const float ch = height() - box_.padding.top - box_.padding.bottom;
        if (cw <= 0.0f || ch <= 0.0f) return;

        const SliderGeom g = ComputeGeom(cx, cy, cw, ch, isVertical(), thumbSize_);
        const float k = progress();
        const bool vert = g.vertical;

        const float mainC = ThumbMainCenter(g, k);

        drawutil::PaintRoundedRect(fc, cx, cy, cw, ch, drawutil::CapsuleRadius(cw, ch), trackColor_);

        if (vert) {
            const float fh = std::clamp(mainC, 0.0f, ch);
            drawutil::PaintRoundedRect(fc, cx, cy + ch - fh, cw, fh,
                             drawutil::CapsuleRadius(cw, fh), fillColor_);
        } else {
            const float fw = std::clamp(mainC, 0.0f, cw);
            drawutil::PaintRoundedRect(fc, cx, cy, fw, ch,
                             drawutil::CapsuleRadius(fw, ch), fillColor_);
        }

        const float d = g.thumbD;
        const float r = d * 0.5f;
        float tx, ty;
        if (vert) {
            tx = cx + (cw - d) * 0.5f;
            ty = cy + ch - mainC - r;
        } else {
            tx = cx + mainC - r;
            ty = cy + (ch - d) * 0.5f;
        }

        const WidgetState st = currentState();
        const Color4f bwColor = thumbBorderColors_.get(st).border;
        const float   bw = thumbBorderWidth_;

        if (bw > 0.0f && bwColor.a > 0.0f) {
            drawutil::PaintRoundedRect(fc, tx, ty, d, d, r, bwColor);
            const float dInner = d - 2.0f * bw;
            if (dInner > 0.0f) {
                drawutil::PaintRoundedRect(fc, tx + bw, ty + bw, dInner, dInner,
                                 dInner * 0.5f, thumbColor_);
            }
        } else {
            drawutil::PaintRoundedRect(fc, tx, ty, d, d, r, thumbColor_);
        }

        if (!showText_ || textFormat_.empty()) return;

        const FontDesc& font = font_;
        const std::string text = detail::FormatValueText(textFormat_, static_cast<double>(value_));
        if (text.empty()) return;

        const TextMetrics tm = fc.MeasureText(text, font);
        if (tm.width <= 0.0f && tm.height <= 0.0f) return;

        float textX, textTop;
        if (vert) {
            textX = cx + cw + 4.0f;
            textTop = ty + (d - tm.height) * 0.5f;
        } else {
            textX = cx + (cw - tm.width) * 0.5f;
            textTop = cy - tm.height - 2.0f;
            if (textTop < cy) textTop = cy;
        }
        fc.AddText(text, font, textX, textTop + tm.ascent, textColor_);
    }
}
