
#include "pch.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>
#include <utility>

#include "include/ZeSwitch.h"
#include "include/Zekit.h"
#include "ZeDrawUtil.h"

namespace Kit {

    Switch::Switch(bool on)
        : checked_(on) {
        ApplyTheme(Themes::Light());
        SetFocusable(true);
        knob_ = checked_ ? 1.0f : 0.0f;
        SetDefaultSize(44.0f, 24.0f);
    }

    void Switch::SetChecked(bool on) {
        if (checked_ == on) return;
        checked_ = on;
        startAnim();
        requestRepaint();
        RequestWindowRedraw();
    }

    void Switch::SetEnabled(bool enabled) {
        BoxWidget::SetEnabled(enabled);
        if (!enabled) {
            pressed_ = false;
            animating_ = false;
        }
        requestRepaint();
        RequestWindowRedraw();
    }

    void Switch::SetText(std::string text) {
        text_ = std::move(text);
        notifyLayoutInputChanged();
        requestRepaint();
        RequestWindowRedraw();
    }

    void Switch::SetTrackSize(float w, float h) {
        trackW_ = (w > 0.0f) ? w : 0.0f;
        trackH_ = (h > 0.0f) ? h : 0.0f;
        markStyled();
        notifyLayoutInputChanged();
        requestRepaint();
        RequestWindowRedraw();
    }

    void Switch::SetColors(const Color4f& onColor, const Color4f& offColor, const Color4f& knobColor) {
        colorsSet_ = true;
        onColor_ = onColor;
        offColor_ = offColor;
        knobColor_ = knobColor;
        markStyled();
        requestRepaint();
        RequestWindowRedraw();
    }

    void Switch::SetAnimated(bool on) {
        animated_ = on;
        if (!on) FinishAnimation();
        RequestWindowRedraw();
    }

    void Switch::SetAnimationDuration(int ms) {
        animMs_ = (ms < 1) ? 1 : ms;
        RequestWindowRedraw();
    }

    void Switch::FinishAnimation() {
        knob_ = checked_ ? 1.0f : 0.0f;
        animating_ = false;
        requestRepaint();
    }

    void Switch::startAnim() {
        const float target = checked_ ? 1.0f : 0.0f;

        const bool wasAnimating = animating_;
        if (!animated_) {
            knob_ = target;
            animating_ = false;
        }
        else if (knob_ != target) {
            animFrom_ = knob_;
            animStartMs_ = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            animating_ = true;
        }

        if (animating_ && !wasAnimating) {
            RequestWindowRedraw();
        }
    }

    void Switch::stepAnimation() const {
        if (!animating_) return;

        const float target = checked_ ? 1.0f : 0.0f;
        const long long nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        const float dur = static_cast<float>(animMs_);
        float t = (dur <= 0.0f) ? 1.0f
            : static_cast<float>(nowMs - animStartMs_) / dur;
        if (t < 0.0f) t = 0.0f;
        if (t >= 1.0f) {
            knob_ = target;
            animating_ = false;
            return;
        }

        const float e = 1.0f - std::pow(1.0f - t, 3.0f);
        knob_ = animFrom_ + (target - animFrom_) * e;

        if (Window* w = window()) {
            const int remain = static_cast<int>(dur - static_cast<float>(nowMs - animStartMs_));
            w->RequestFrameWake(remain > 0 ? remain : 1);
        }
    }

    void Switch::SetOnToggled(SelfAction1<bool> handler) { onToggled_ = std::move(handler); }
    void Switch::SetOnToggled(SelfAction handler) { onToggledSelf_ = std::move(handler); }

    void Switch::notify(bool on) {
        if (onToggled_)     onToggled_(*this, on);
        if (onToggledSelf_) onToggledSelf_(*this);
    }

    void Switch::toggleByUser() {
        checked_ = !checked_;
        startAnim();
        notify(checked_);
        requestRepaint();
    }

    void Switch::requestRepaint() {
        RequestWindowRedraw();
    }

    void Switch::ApplyTheme(const Theme& t) {
        const SwitchStyle& ss = t.toggleSwitch;

        if (!colorsSet_) {
            onColor_ = ss.onColor;
            offColor_ = ss.offColor;
            knobColor_ = ss.knobColor;
        }
        onDisabledColor_ = ss.onDisabledColor;
        offDisabledColor_ = ss.offDisabledColor;
        knobDisabledColor_ = ss.knobDisabledColor;

        themeTrackW_ = (ss.trackWidth > 0.0f) ? ss.trackWidth : 44.0f;
        themeTrackH_ = (ss.trackHeight > 0.0f) ? ss.trackHeight : 24.0f;
        knobInset_ = (ss.knobInset >= 0.0f) ? ss.knobInset : 0.0f;
        textGap_ = (ss.textGap >= 0.0f) ? ss.textGap : 0.0f;
        textStyle_ = ss.text;

        box_.colors = MultiStateColors{};
        box_.border = Border{};
        box_.padding = Padding{};
        box_.radiusMode = RadiusMode::None;
        box_.shadow.enabled = false;
        box_.outline.enabled = false;
        box_.opacity = 1.0f;
    }

    float Switch::resolvedTrackW() const { return trackW_ > 0.0f ? trackW_ : themeTrackW_; }
    float Switch::resolvedTrackH() const { return trackH_ > 0.0f ? trackH_ : themeTrackH_; }

    float Switch::hintWidth() const {
        if (prefWidth() > 0.0f) return prefWidth();

        float w = resolvedTrackW();
        if (hasText()) {
            if (const Window* win = window()) {
                const float tw = const_cast<Window*>(win)->MeasureText(text_, textStyle_.font).width;
                if (tw > 0.0f) w += textGap_ + tw;
            }
        }
        return w;
    }

    float Switch::hintHeight() const {
        if (prefHeight() > 0.0f) return prefHeight();

        float hh = resolvedTrackH();
        if (hasText()) {
            if (const Window* win = window()) {
                const float th = const_cast<Window*>(win)
                    ->MeasureText(std::string_view("Ag"), textStyle_.font).height;
                hh = std::max(hh, th);
            }
        }
        return hh;
    }

    bool Switch::OnMousePress(MouseEvent& e) {
        if (!isEnabled()) { pressed_ = false; return false; }
        if (e.button != MouseButton::Left) return false;
        if (!HitTest(e.x, e.y)) return false;

        pressed_ = true;
        e.accept();
        return true;
    }

    bool Switch::OnMouseRelease(MouseEvent& e) {
        if (e.button != MouseButton::Left) return false;

        const bool was = pressed_;
        pressed_ = false;
        if (!was || !HitTest(e.x, e.y)) return false;

        toggleByUser();
        e.accept();
        return true;
    }

    bool Switch::OnKeyPress(KeyEvent& e) {
        if (!isEnabled()) return false;
        if (e.key == Key::Space || e.key == Key::Enter) {
            toggleByUser();
            e.accept();
            return true;
        }
        return false;
    }

    void Switch::paintContent(FrameCollector& fc) const {
        const float w = width();
        const float h = height();
        if (w <= 0.0f || h <= 0.0f) return;

        stepAnimation();

        const float tw = std::min(resolvedTrackW(), w);
        const float th = resolvedTrackH();
        if (tw <= 0.0f || th <= 0.0f) return;

        const float ty = y() + (h - th) * 0.5f;
        const float inset = std::min(knobInset_, th * 0.5f);
        const float knobD = std::max(0.0f, th - inset * 2.0f);

        const bool on = isEnabled();
        Color4f track = checked_ ? (on ? onColor_ : onDisabledColor_)
                                 : (on ? offColor_ : offDisabledColor_);
        Color4f knob = on ? knobColor_ : knobDisabledColor_;

        track.a *= box_.opacity;
        knob.a *= box_.opacity;

        drawutil::PaintRoundedRect(fc, x(), ty, tw, th, th * 0.5f, track);

        const float travel = std::max(0.0f, tw - knobD - inset * 2.0f);
        const float kx = x() + inset + travel * std::clamp(knob_, 0.0f, 1.0f);
        drawutil::PaintCircle(fc, kx + knobD * 0.5f, ty + th * 0.5f, knobD * 0.5f, knob);

        if (!hasText()) return;

        const float textX = x() + tw + textGap_;
        const float avail = std::max(0.0f, (x() + w) - textX);
        if (avail <= 0.0f) return;

        const FontDesc& font = textStyle_.font;
        Color4f fg = textStyle_.colors.get(isEnabled() ? WidgetState::Normal : WidgetState::Disabled).text;
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

}
