
#include "pch.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>

#include "include/ZeNumberBox.h"
#include "include/Zekit.h"

#include "ZeDrawUtil.h"

namespace Kit {

    namespace {
        constexpr float kFallbackW = 120.0f;
        constexpr float kFallbackH = 34.0f;
        constexpr float kMinWidthForButtons = 56.0f;
        constexpr float kGlyphSize = 4.0f;
        constexpr float kGlyphThickness = 1.4f;

        float ClampValue(float v, float lo, float hi) {
            if (lo > hi) std::swap(lo, hi);
            return std::clamp(v, lo, hi);
        }

        bool ParseNumber(const std::string& s, float& out) {
            std::size_t i = 0;
            const std::size_t n = s.size();

            while (i < n && (s[i] == ' ' || s[i] == '\t')) ++i;

            bool negative = false;
            if (i < n && (s[i] == '+' || s[i] == '-')) {
                negative = (s[i] == '-');
                ++i;
            }

            bool sawDigit = false;
            double intPart = 0.0;
            while (i < n && s[i] >= '0' && s[i] <= '9') {
                intPart = intPart * 10.0 + static_cast<double>(s[i] - '0');
                sawDigit = true;
                ++i;
            }

            double frac = 0.0;
            if (i < n && s[i] == '.') {
                ++i;
                double scale = 0.1;
                while (i < n && s[i] >= '0' && s[i] <= '9') {
                    frac += static_cast<double>(s[i] - '0') * scale;
                    scale *= 0.1;
                    sawDigit = true;
                    ++i;
                }
            }

            if (!sawDigit) return false;

            const double v = intPart + frac;
            out = static_cast<float>(negative ? -v : v);
            return true;
        }
    }

    NumberBox::NumberBox(float value) {
        ApplyTheme(Themes::Light());

        value_ = ClampValue(value, min_, max_);

        setTextQuietly(FormatValue(value_));

        SetDefaultSize(kFallbackW, kFallbackH);
    }

    void NumberBox::setTextQuietly(const std::string& text) {
        text_ = text;
        setCaret(text_.size(), false);
        markLayoutDirty();
    }

    std::string NumberBox::FormatValue(float v) const {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.*f", decimals_, static_cast<double>(v));
        return std::string(buf) + suffix_;
    }

    void NumberBox::SetValue(float value) {
        const float clamped = ClampValue(value, min_, max_);
        const bool changed = (clamped != value_);
        value_ = clamped;
        refreshTextFromValue();

        if (changed) {
            RequestWindowRedraw();
        }
        RequestWindowRedraw();
    }

    void NumberBox::SetText(const std::string& text) {
        setTextQuietly(text);

        float parsed = 0.0f;
        if (ParseNumber(text, parsed))
            value_ = ClampValue(parsed, min_, max_);
        RequestWindowRedraw();
    }

    void NumberBox::refreshTextFromValue() {
        setTextQuietly(FormatValue(value_));
    }

    bool NumberBox::parseTextIntoValue() {
        float parsed = 0.0f;
        if (!ParseNumber(Text(), parsed)) return false;
        value_ = ClampValue(parsed, min_, max_);
        return true;
    }

    void NumberBox::SetRange(float minValue, float maxValue) {
        if (minValue > maxValue) std::swap(minValue, maxValue);
        min_ = minValue;
        max_ = maxValue;

        value_ = ClampValue(value_, min_, max_);
        refreshTextFromValue();
        RequestWindowRedraw();
    }

    void NumberBox::SetRange(float minValue, float maxValue, float step) {
        SetRange(minValue, maxValue);
        SetStep(step);
        RequestWindowRedraw();
    }

    void NumberBox::SetStep(float step) {
        if (step <= 0.0f) return;
        step_ = step;
        RequestWindowRedraw();
    }

    void NumberBox::SetDecimals(int digits) {
        decimals_ = std::clamp(digits, 0, 6);
        refreshTextFromValue();
        RequestWindowRedraw();
    }

    void NumberBox::SetSuffix(std::string suffix) {
        suffix_ = std::move(suffix);
        refreshTextFromValue();
        notifyLayoutInputChanged();
        RequestWindowRedraw();
    }

    bool NumberBox::StepBy(int steps) {
        if (steps == 0) return false;

        const float before = value_;
        value_ = ClampValue(value_ + step_ * static_cast<float>(steps), min_, max_);
        if (value_ == before) return false;

        refreshTextFromValue();
        RequestWindowRedraw();
        return true;
    }

    void NumberBox::commitUserEdit(bool reformat) {
        const float before = value_;
        parseTextIntoValue();
        if (reformat) refreshTextFromValue();
        if (value_ != before) notifyValueChanged();
    }

    void NumberBox::notifyValueChanged() {
        if (onValueChanged_)     onValueChanged_(*this, value_);
        if (selfOnValueChanged_) selfOnValueChanged_(*this);
    }

    void NumberBox::requestRepaint() {
        RequestWindowRedraw();
    }

    void NumberBox::ApplyTheme(const Theme& t) {
        InputBox::ApplyTheme(t);

        stepBox_ = t.numberBox.stepButton;
        stepColor_ = t.numberBox.stepColor;
        stepHoverColor_ = t.numberBox.stepHoverColor;
        stepPressedColor_ = t.numberBox.stepPressedColor;
        buttonWidth_ = (t.numberBox.buttonWidth > 0.0f) ? t.numberBox.buttonWidth : 22.0f;

        Padding p = box_.padding;
        p.right += buttonWidth_;
        box_.padding = p;
    }

    float NumberBox::hintWidth() const {
        if (prefWidth() > 0.0f) return prefWidth();
        return InputBox::hintWidth();
    }

    bool NumberBox::stepButtonsRect(float& bx, float& by, float& bw, float& bh) const {
        const float w = width();
        const float h = height();
        if (w < kMinWidthForButtons || h <= 0.0f) return false;

        bw = buttonWidth_;
        bw = std::min(bw, w * 0.5f);
        if (bw <= 0.0f) return false;

        bx = x() + w - bw;
        by = y();
        bh = h;
        return true;
    }

    void NumberBox::paintContent(FrameCollector& fc) const {
        InputBox::paintContent(fc);

        float bx = 0.0f, by = 0.0f, bw = 0.0f, bh = 0.0f;
        if (!stepButtonsRect(bx, by, bw, bh)) return;

        const float halfH = bh * 0.5f;
        const bool enabled = isEnabled();

        Color4f glyphIdle = stepColor_;
        if (glyphIdle.a <= 0.0f) glyphIdle = resolvedTextColors(WidgetState::Normal).text;

        for (int side = 0; side < 2; ++side) {
            const int which = (side == 0) ? 1 : -1;
            const float cellY = by + (side == 0 ? 0.0f : halfH);

            WidgetState st = WidgetState::Normal;
            if (!enabled)                              st = WidgetState::Disabled;
            else if (pressedButton_ == which)          st = WidgetState::Pressed;
            else if (hoverButton_ == which)            st = WidgetState::Hover;

            Color4f bg = stepBox_.colors.get(st).background;
            drawutil::PaintRoundedRect(fc, bx, cellY, bw, halfH, 0.0f, bg);

            Color4f fg = glyphIdle;
            if (!enabled)                        fg = resolvedTextColors(WidgetState::Disabled).text;
            else if (pressedButton_ == which)    fg = (stepPressedColor_.a > 0.0f) ? stepPressedColor_ : glyphIdle;
            else if (hoverButton_ == which)      fg = (stepHoverColor_.a > 0.0f) ? stepHoverColor_ : glyphIdle;

            const float cx = bx + bw * 0.5f;
            const float cy = cellY + halfH * 0.5f;

            drawutil::PaintHLine(fc, cx - kGlyphSize, cy - kGlyphThickness * 0.5f,
                kGlyphSize * 2.0f, kGlyphThickness, fg);
            if (which > 0) {
                drawutil::PaintVLine(fc, cx - kGlyphThickness * 0.5f, cy - kGlyphSize,
                    kGlyphSize * 2.0f, kGlyphThickness, fg);
            }

            if (side == 0) {
                Color4f sep = stepBox_.colors.get(WidgetState::Normal).border;
                if (sep.a <= 0.0f) {
                    sep = resolvedTextColors(WidgetState::Normal).text;
                    sep.a *= 0.25f;
                }
                drawutil::PaintHLine(fc, bx + 2.0f, by + halfH - 0.5f, bw - 4.0f, 1.0f, sep);
            }
        }
    }

    bool NumberBox::OnMousePress(MouseEvent& e) {
        if (e.button != MouseButton::Left) return InputBox::OnMousePress(e);
        if (!isEnabled()) return false;

        float bx = 0.0f, by = 0.0f, bw = 0.0f, bh = 0.0f;
        if (stepButtonsRect(bx, by, bw, bh) && drawutil::HitRect(e.x, e.y, bx, by, bw, bh)) {
            const int which = (e.y < by + bh * 0.5f) ? 1 : -1;
            pressedButton_ = which;
            if (StepBy(which)) notifyValueChanged();
            requestRepaint();
            e.accept();
            return true;
        }

        pressedButton_ = 0;
        return InputBox::OnMousePress(e);
    }

    bool NumberBox::OnMouseMove(MouseEvent& e) {
        float bx = 0.0f, by = 0.0f, bw = 0.0f, bh = 0.0f;
        int which = 0;
        if (stepButtonsRect(bx, by, bw, bh) && drawutil::HitRect(e.x, e.y, bx, by, bw, bh))
            which = (e.y < by + bh * 0.5f) ? 1 : -1;

        if (which != hoverButton_) {
            hoverButton_ = which;
            requestRepaint();
        }
        return InputBox::OnMouseMove(e);
    }

    void NumberBox::OnMouseLeave() {
        InputBox::OnMouseLeave();
        pressedButton_ = 0;
        if (hoverButton_ != 0) {
            hoverButton_ = 0;
            requestRepaint();
        }
    }

    Cursor NumberBox::cursorAt(float x, float y) const {
        float bx = 0.0f, by = 0.0f, bw = 0.0f, bh = 0.0f;
        if (stepButtonsRect(bx, by, bw, bh) && drawutil::HitRect(x, y, bx, by, bw, bh))
            return Cursor::Arrow;
        return InputBox::cursorAt(x, y);
    }

    bool NumberBox::OnKeyPress(KeyEvent& e) {
        if (!isEnabled()) return false;

        switch (e.key) {
        case Key::UpArrow:
            if (StepBy(hasModifier(e.modifiers, KeyboardModifiers::Shift) ? 10 : 1))
                notifyValueChanged();
            e.accept();
            return true;

        case Key::DownArrow:
            if (StepBy(hasModifier(e.modifiers, KeyboardModifiers::Shift) ? -10 : -1))
                notifyValueChanged();
            e.accept();
            return true;

        case Key::Enter:
            commitUserEdit(true);
            e.accept();
            return true;

        default:
            return InputBox::OnKeyPress(e);
        }
    }

    bool NumberBox::OnCharInput(KeyEvent& e) {
        if (!isEnabled() || isReadOnly()) return false;

        const char32_t c = e.character;
        const bool isDigit = (c >= U'0' && c <= U'9');
        const bool isDot = (c == U'.' || c == U',');
        const bool isSign = (c == U'-' || c == U'+');

        if (!isDigit && !isDot && !isSign) {
            e.accept();
            return true;
        }

        return InputBox::OnCharInput(e);
    }

    void NumberBox::OnFocusOut() {
        const float before = value_;
        parseTextIntoValue();
        refreshTextFromValue();
        if (value_ != before) notifyValueChanged();
        InputBox::OnFocusOut();
    }

}
