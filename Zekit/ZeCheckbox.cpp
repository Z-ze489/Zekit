
#include "pch.h"

#include <algorithm>
#include <cmath>

#include "include/ZeCheckbox.h"
#include "ZeDrawUtil.h"
#include "include/Zekit.h"

#include "ZeFrameCollector.h"
#include "ZeIndicatorPaint.h"
#include "ZeIndicatorMetric.h"

namespace Kit {

    namespace {
    }

    Checkbox::Checkbox(std::string text, bool checked) {
        ApplyTheme(Themes::Light());

        text_ = std::move(text);
        state_ = checked ? CheckState::Checked : CheckState::Unchecked;

        SetFocusable(true);

        SetDefaultSize(160.0f, 24.0f);
    }

    void Checkbox::SetState(CheckState s) {
        if (s == CheckState::Indeterminate && !triState_) s = CheckState::Unchecked;
        if (state_ == s) return;
        state_ = s;
        markStyled();
        RequestWindowRedraw();
    }

    void Checkbox::SetChecked(bool checked) {
        SetState(checked ? CheckState::Checked : CheckState::Unchecked);
        RequestWindowRedraw();
    }

    void Checkbox::SetTriState(bool on) {
        triState_ = on;
        if (!on && state_ == CheckState::Indeterminate) state_ = CheckState::Unchecked;
        markStyled();
        RequestWindowRedraw();
    }

    void Checkbox::Toggle() {
        switch (state_) {
        case CheckState::Unchecked:     SetState(CheckState::Checked); break;
        case CheckState::Checked:       SetState(triState_ ? CheckState::Indeterminate
                                                           : CheckState::Unchecked); break;
        case CheckState::Indeterminate: SetState(CheckState::Unchecked); break;
        }
    }

    bool Checkbox::advanceState() {
        if (!isEnabled()) return false;

        const CheckState before = state_;
        switch (state_) {
        case CheckState::Unchecked:     SetState(CheckState::Checked); break;
        case CheckState::Checked:       SetState(triState_ ? CheckState::Indeterminate
                                                           : CheckState::Unchecked); break;
        case CheckState::Indeterminate: SetState(CheckState::Unchecked); break;
        }
        if (state_ == before) return false;

        notify();
        return true;
    }

    void Checkbox::Activate() {
        advanceState();
    }

    void Checkbox::SetOnToggled(ToggledAction handler) {
        onToggled_ = std::move(handler);
    }

    void Checkbox::SetOnToggled(SelfAction handler) {
        selfOnToggled_ = std::move(handler);
    }

    void Checkbox::SetOnStateChanged(std::function<void(Widget&, CheckState)> handler) {
        onStateChanged_ = std::move(handler);
    }

    void Checkbox::notify() {
        const bool on = (state_ == CheckState::Checked);
        if (onToggled_)       onToggled_(*this, on);
        if (selfOnToggled_)   selfOnToggled_(*this);
        if (onStateChanged_)  onStateChanged_(*this, state_);
    }

    void Checkbox::SetIndicatorSize(float size) {
        indicatorSize_ = (size < 0.0f) ? 0.0f : size;
        markStyled();
        RequestWindowRedraw();
    }

    void Checkbox::SetIndicatorShape(IndicatorShape shape) {
        if (shape_ == shape) return;
        shape_ = shape;
        markStyled();
        RequestWindowRedraw();
    }

    void Checkbox::SetMarkStyle(CheckMark mark) {
        if (mark_ == mark) return;
        mark_ = mark;
        markStyled();
        RequestWindowRedraw();
    }

    void Checkbox::SetSpacing(float gap) {
        spacing_ = (gap < 0.0f) ? 0.0f : gap;
        markStyled();
        RequestWindowRedraw();
    }

    void Checkbox::SetIndicatorColor(const Color4f& c) { indicatorColor_ = c; markStyled();   RequestWindowRedraw();}
    void Checkbox::SetIndicatorBorderColor(const Color4f& c) { indicatorBorderColor_ = c; markStyled();   RequestWindowRedraw();}
    void Checkbox::SetMarkColor(const Color4f& c) { markColor_ = c; markStyled();   RequestWindowRedraw();}

    float Checkbox::resolvedIndicatorSize() const {
        if (indicatorSize_ > 0.0f) return indicatorSize_;
        if (const Window* w = window()) {
            const TextMetrics tm = const_cast<Window*>(w)->MeasureText(text_, textStyle_.font);
            return detail::ResolveIndicatorSize(indicatorSize_, tm.height);
        }
        return detail::ResolveIndicatorSize(indicatorSize_, 0.0f);
    }

    float Checkbox::hintWidth() const {
        if (prefWidth() > 0.0f) return prefWidth();
        TextMetrics tm{};
        if (const Window* w = window())
            tm = const_cast<Window*>(w)->MeasureText(text_, textStyle_.font);
        return detail::IndicatorHints(box_, resolvedIndicatorSize(), spacing_, tm).width;
    }

    float Checkbox::hintHeight() const {
        if (prefHeight() > 0.0f) return prefHeight();
        TextMetrics tm{};
        if (const Window* w = window())
            tm = const_cast<Window*>(w)->MeasureText(text_, textStyle_.font);
        return detail::IndicatorHints(box_, resolvedIndicatorSize(), spacing_, tm).height;
    }

    void Checkbox::ApplyTheme(const Theme& t) {
        TextWidget::ApplyTheme(t);

        indicatorColor_       = t.checkbox.indicatorColor;
        indicatorBorderColor_ = t.checkbox.indicatorBorderColor;
        markColor_            = t.checkbox.markColor;
    }

    const BoxStyle& Checkbox::themeBox(const Theme& t) const {
        return t.checkbox.box;
    }

    const TextStyle& Checkbox::themeText(const Theme& t) const {
        return t.checkbox.text;
    }

    bool Checkbox::OnMousePress(MouseEvent& e) {
        return detail::IndicatorPress(*this, e, isEnabled(), pressed_);
    }

    bool Checkbox::OnMouseRelease(MouseEvent& e) {
        bool hit = false;
        if (!detail::IndicatorRelease(*this, e, isEnabled(), pressed_, hit)) return false;
        if (hit) Activate();
        return true;
    }

    bool Checkbox::OnKeyPress(KeyEvent& e) {
        if (!isEnabled()) return false;
        if (e.key != Key::Space && e.key != Key::Enter) return false;

        Activate();
        e.accept();
        return true;
    }

    void Checkbox::paintContent(FrameCollector& fc) const {
        float cx = 0.0f, cy = 0.0f, cw = 0.0f, ch = 0.0f;
        contentRect(cx, cy, cw, ch);
        if (cw <= 0.0f || ch <= 0.0f) return;

        const float ind = resolvedIndicatorSize();
        const float textH = [&] {
            if (text_.empty()) return 0.0f;
            const TextMetrics tm = fc.MeasureText(text_, textStyle_.font);
            return tm.height;
            }();

        const float rowH = std::max(ind, textH);
        const float rowTop = cy + (ch - rowH) * 0.5f;

        const float ix = cx;
        const float iy = rowTop + (rowH - ind) * 0.5f;

        const bool filled = (state_ != CheckState::Unchecked);
        const float bw = std::max(1.0f, ind * 0.10f);
        const Color4f fill = filled ? markColor_ : indicatorColor_;
        const Color4f border = filled ? Color4f{ 0.0f, 0.0f, 0.0f, 0.0f } : indicatorBorderColor_;
        indicator::PaintContainer(fc, shape_, ix, iy, ind, fill, border, bw);

        const Color4f glyph = filled ? indicatorColor_ : markColor_;
        if (state_ == CheckState::Checked) {
            indicator::PaintCheckMark(fc, mark_, ix, iy, ind, glyph);
        }
        else if (state_ == CheckState::Indeterminate) {
            indicator::PaintBar(fc, ix, iy, ind, glyph);
        }

        if (text_.empty()) return;

        const TextMetrics tm = fc.MeasureText(text_, textStyle_.font);
        if (tm.width <= 0.0f && tm.height <= 0.0f) return;

        const float textX = ix + ind + spacing_;
        const float textTop = rowTop + (rowH - tm.height) * 0.5f;

        const WidgetState st = isEnabled() ? WidgetState::Normal : WidgetState::Disabled;
        const ColorSet cs = resolvedTextColors(st);
        fc.AddText(text_, textStyle_.font, textX, textTop + tm.ascent, cs.text);
    }
}
