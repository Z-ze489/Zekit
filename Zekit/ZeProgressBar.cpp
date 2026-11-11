
#include "pch.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>

#include "include/ZeProgressBar.h"
#include "ZeDrawUtil.h"
#include "ZeValueAnim.h"
#include "ZeValueText.h"
#include "include/Zekit.h"

#include "ZeFrameCollector.h"
#include "ZeRoundedRectShape.h"

namespace Kit {

    namespace {
        constexpr float kDefaultBarLength = 160.0f;
        constexpr float kDefaultBarThickness = 20.0f;
    }

    ProgressBar::ProgressBar() {
        ApplyTheme(Themes::Light());

        SetDefaultSize(kDefaultBarLength, kDefaultBarThickness);

        target_ = value_;
    }

    float ProgressBar::hintWidth() const {
        if (prefWidth() > 0.0f) return prefWidth();

        float dx = 0.0f, dy = 0.0f;
        drawutil::BoxInsets(box_, dx, dy);
        (void)dy;

        const float bar = isVertical() ? kDefaultBarThickness : kDefaultBarLength;
        return bar + dx;
    }

    float ProgressBar::hintHeight() const {
        if (prefHeight() > 0.0f) return prefHeight();

        float dx = 0.0f, dy = 0.0f;
        drawutil::BoxInsets(box_, dx, dy);
        (void)dx;

        const float bar = isVertical() ? kDefaultBarLength : kDefaultBarThickness;
        return bar + dy;
    }

    void ProgressBar::SetMin(float min) {
        min_ = min;
        if (max_ < min_) std::swap(min_, max_);
        value_  = std::clamp(value_,  min_, max_);
        target_ = std::clamp(target_, min_, max_);
        RequestWindowRedraw();
    }

    void ProgressBar::SetMax(float max) {
        max_ = max;
        if (min_ > max_) std::swap(min_, max_);
        value_  = std::clamp(value_,  min_, max_);
        target_ = std::clamp(target_, min_, max_);
        RequestWindowRedraw();
    }

    void ProgressBar::SetRange(float min, float max) {
        min_ = min;
        max_ = max;
        if (min_ > max_) std::swap(min_, max_);
        value_  = std::clamp(value_,  min_, max_);
        target_ = std::clamp(target_, min_, max_);
        RequestWindowRedraw();
    }

    void ProgressBar::SetValue(float value) {
        if (std::isnan(value)) value = min_;
        target_ = std::clamp(snapToStep(value), min_, max_);

        const bool wasAnimating = animating_;
        if (!animated_) {
            value_    = target_;
            animating_ = false;
        }
        else if (value_ != target_) {
            animFrom_    = value_;
            animStartMs_ = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            animating_   = true;
        }
        markStyled();

        if (animating_ && !wasAnimating) {
            RequestWindowRedraw();
        }
        RequestWindowRedraw();
    }

    float ProgressBar::progress() const {
        const float span = max_ - min_;
        if (span <= 0.0f) return 0.0f;
        return std::clamp((value_ - min_) / span, 0.0f, 1.0f);
    }

    void ProgressBar::SetProgress(float k) {
        if (std::isnan(k)) k = 0.0f;
        k = std::clamp(k, 0.0f, 1.0f);
        SetValue(min_ + k * (max_ - min_));
        RequestWindowRedraw();
    }

    float ProgressBar::percent() const { return progress() * 100.0f; }

    void ProgressBar::SetDraggable(bool on) {
        if (draggable_ == on) return;
        draggable_ = on;
        if (!on) {
            dragging_ = false;
            pressed_ = false;
        }
        SetFocusable(on);
        applyDragCursor();
        markStyled();
        RequestWindowRedraw();
    }

    void ProgressBar::applyDragCursor() {
        if (!draggable_) { SetCursor(Cursor::Arrow); return; }
        SetCursor(isVertical() ? Cursor::SizeNS : Cursor::SizeWE);
    }

    void ProgressBar::SetStep(float step) {
        step_ = (step > 0.0f) ? step : 0.0f;
        RequestWindowRedraw();
    }

    float ProgressBar::snapToStep(float v) const {
        if (step_ <= 0.0f) return v;
        const float k = std::round((v - min_) / step_);
        return min_ + k * step_;
    }

    void ProgressBar::SetOnValueChanged(ValueChangedAction handler) {
        onValueChanged_ = std::move(handler);
    }

    void ProgressBar::notifyChanged() {
        if (onValueChanged_) onValueChanged_(*this, value_);
    }

    void ProgressBar::SetShowThumb(bool show) { showThumb_ = show; markStyled();   RequestWindowRedraw();}
    void ProgressBar::SetThumbColor(const Color4f& c) { thumbColor_ = c; markStyled();   RequestWindowRedraw();}
    void ProgressBar::SetThumbSize(float d) { thumbSize_ = (d > 0.0f) ? d : 0.0f; markStyled();   RequestWindowRedraw();}

    float ProgressBar::posToProgress(float local) const {
        const float cx = x() + box_.padding.left;
        const float cy = y() + box_.padding.top;
        const float cw = width() - box_.padding.left - box_.padding.right;
        const float ch = height() - box_.padding.top - box_.padding.bottom;
        if (cw <= 0.0f || ch <= 0.0f) return 0.0f;

        if (isVertical()) {
            const float rel = (cy + ch) - local;
            return std::clamp(rel / ch, 0.0f, 1.0f);
        }
        const float rel = local - cx;
        return std::clamp(rel / cw, 0.0f, 1.0f);
    }

    bool ProgressBar::setValueByUser(float v) {
        if (std::isnan(v)) return false;
        v = std::clamp(snapToStep(v), min_, max_);
        if (v == value_ && v == target_) return false;

        value_ = v;
        target_ = v;
        animating_ = false;
        markStyled();
        notifyChanged();
        return true;
    }

    bool ProgressBar::OnMousePress(MouseEvent& e) {
        if (!draggable_ || !isEnabled()) return false;
        if (e.button != MouseButton::Left) return false;

        const float cx = x() + box_.padding.left;
        const float cy = y() + box_.padding.top;
        const float cw = width() - box_.padding.left - box_.padding.right;
        const float ch = height() - box_.padding.top - box_.padding.bottom;
        if (cw <= 0.0f || ch <= 0.0f) return false;
        const bool inside = (e.x >= cx - 1.0f && e.x <= cx + cw + 1.0f &&
                             e.y >= cy - 1.0f && e.y <= cy + ch + 1.0f);
        if (!inside) return false;

        dragging_ = true;
        pressed_ = true;
        setValueByUser(min_ + posToProgress(isVertical() ? e.y : e.x) * (max_ - min_));
        markStyled();

        e.accept();
        return true;
    }

    bool ProgressBar::OnMouseMove(MouseEvent& e) {
        if (!isEnabled()) { dragging_ = false; return false; }
        if (!dragging_) return false;

        setValueByUser(min_ + posToProgress(isVertical() ? e.y : e.x) * (max_ - min_));
        e.accept();
        return true;
    }

    bool ProgressBar::OnMouseRelease(MouseEvent& e) {
        if (!dragging_) return false;
        if (e.button != MouseButton::Left) return false;
        dragging_ = false;
        pressed_ = false;
        markStyled();
        e.accept();
        return true;
    }

    bool ProgressBar::OnKeyPress(KeyEvent& e) {
        if (!draggable_ || !isEnabled()) return false;

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

    void ProgressBar::SetAnimated(bool on) {
        animated_ = on;
        if (!on) {
            value_     = target_;
            animating_ = false;
        }
        markStyled();
        RequestWindowRedraw();
    }

    void ProgressBar::SetAnimationDuration(int ms) {
        animMs_ = (ms < 1) ? 1 : ms;
        RequestWindowRedraw();
    }

    void ProgressBar::FinishAnimation() {
        value_     = target_;
        animating_ = false;
        markStyled();
    }

    float ProgressBar::targetProgress() const {
        const float span = max_ - min_;
        if (span <= 0.0f) return 0.0f;
        return std::clamp((target_ - min_) / span, 0.0f, 1.0f);
    }

    void ProgressBar::stepAnimation() const {
        if (!animating_) return;

        const detail::ValueAnimStep st = detail::StepValueAnim(animFrom_, target_, animStartMs_, animMs_);
        value_ = st.value;
        if (st.done) { animating_ = false; return; }

        if (Window* w = window()) w->RequestFrameWake(st.remainMs);
    }

    void ProgressBar::SetOrientation(ProgressOrientation o) {
        orientation_ = o;
        applyDragCursor();
        markStyled();
        RequestWindowRedraw();
    }

    void ProgressBar::SetTrackColor(const Color4f& c) { trackColor_ = c; markStyled();   RequestWindowRedraw();}
    void ProgressBar::SetFillColor(const Color4f& c) { fillColor_ = c; markStyled();   RequestWindowRedraw();}
    void ProgressBar::SetTextColor(const Color4f& c) { textColor_ = c; markStyled();   RequestWindowRedraw();}
    void ProgressBar::SetFontDesc(const FontDesc& font) { font_ = font;   RequestWindowRedraw();}
    void ProgressBar::SetTextFormat(std::string fmt) { textFormat_ = std::move(fmt);   RequestWindowRedraw();}

    const BoxStyle& ProgressBar::themeBox(const Theme& t) const {
        return t.progress.track;
    }

    void ProgressBar::ApplyTheme(const Theme& t) {
        BoxWidget::ApplyTheme(t);

        trackColor_ = t.progress.trackColor;
        fillColor_ = t.progress.fillColor;
        textColor_ = t.progress.text.colors.get(WidgetState::Normal).text;

        thumbColor_ = t.progress.thumbColor;
        thumbBorderColors_ = t.progress.thumb.colors;
        thumbBorderWidth_ = std::max({ t.progress.thumb.border.width[Border::Top],
                                       t.progress.thumb.border.width[Border::Right],
                                       t.progress.thumb.border.width[Border::Bottom],
                                       t.progress.thumb.border.width[Border::Left] });

        font_ = t.progress.text.font;
    }

    void ProgressBar::paintContent(FrameCollector& fc) const {
        stepAnimation();

        const float x = this->x() + box_.padding.left;
        const float y = this->y() + box_.padding.top;
        const float w = width() - box_.padding.left - box_.padding.right;
        const float h = height() - box_.padding.top - box_.padding.bottom;
        if (w <= 0.0f || h <= 0.0f) return;

        const float k = progress();

        if (!isVertical()) {
            drawutil::PaintRoundedRect(fc, x, y, w, h, drawutil::CapsuleRadius(w, h), trackColor_);

            const float fw = w * k;
            drawutil::PaintRoundedRect(fc, x, y, fw, h, drawutil::CapsuleRadius(fw, h), fillColor_);
        } else {
            drawutil::PaintRoundedRect(fc, x, y, w, h, drawutil::CapsuleRadius(w, h), trackColor_);

            const float fh = h * k;
            drawutil::PaintRoundedRect(fc, x, y + (h - fh), w, fh, drawutil::CapsuleRadius(w, fh), fillColor_);
        }

        if (draggable_ && showThumb_) {
            const float thin = isVertical() ? w : h;
            float d = (thumbSize_ > 0.0f) ? thumbSize_
                                          : std::max(8.0f, thin - 4.0f);
            d = std::min(d, std::max(thin, 8.0f));
            if (d > 0.0f) {
                const float r = d * 0.5f;
                float tx, ty;
                if (isVertical()) {
                    tx = x + (w - d) * 0.5f;
                    ty = y + h - h * k - r;
                } else {
                    tx = x + w * k - r;
                    ty = y + (h - d) * 0.5f;
                }

                const WidgetState st = dragging_ ? WidgetState::Pressed : currentState();
                const Color4f borderColor = thumbBorderColors_.get(st).border;
                const float   bw = thumbBorderWidth_;

                if (bw > 0.0f && borderColor.a > 0.0f) {
                    drawutil::PaintRoundedRect(fc, tx, ty, d, d, r, borderColor);
                    const float dInner = d - 2.0f * bw;
                    if (dInner > 0.0f) {
                        drawutil::PaintRoundedRect(fc, tx + bw, ty + bw, dInner, dInner,
                                         dInner * 0.5f, thumbColor_);
                    }
                } else {
                    drawutil::PaintRoundedRect(fc, tx, ty, d, d, r, thumbColor_);
                }
            }
        }

        if (!showText_ || textFormat_.empty()) return;

        const FontDesc& font = font_;

        const std::string text = detail::FormatValueText(textFormat_, percent());
        if (text.empty()) return;

        const TextMetrics tm = fc.MeasureText(text, font);
        if (tm.width <= 0.0f && tm.height <= 0.0f) return;

        const float tx = x + (w - tm.width) * 0.5f;
        const float top = y + (h - tm.height) * 0.5f;
        fc.AddText(text, font, tx, top + tm.ascent, textColor_);
    }
}
