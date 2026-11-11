
#include "pch.h"

#include <algorithm>
#include <cmath>

#include "include/ZeRadioButton.h"
#include "ZeDrawUtil.h"
#include "include/Zekit.h"

#include "ZeFrameCollector.h"
#include "ZeIndicatorPaint.h"
#include "ZeIndicatorMetric.h"

namespace Kit {

    namespace {
    }

    RadioButton::RadioButton(std::string text, bool selected) {
        ApplyTheme(Themes::Light());
        text_ = std::move(text);
        selected_ = selected;
        SetFocusable(true);
        SetDefaultSize(160.0f, 24.0f);
    }

    void RadioButton::SetSelected(bool on) {
        if (on) {
            uncheckSiblings(*this);
        }
        if (selected_ == on) return;
        selected_ = on;
        markStyled();
        RequestWindowRedraw();
    }

    void RadioButton::SetGroup(std::string name) {
        group_ = std::move(name);
        RequestWindowRedraw();
    }

    void RadioButton::uncheckSiblings(const Widget& self) {
        Widget* root = const_cast<Widget*>(&self);
        while (root->parent() != nullptr) root = root->parent();
        if (!root) return;

        root->VisitTree([&](Widget& node, int) {
            if (&node == &self) return;
            auto* rb = dynamic_cast<RadioButton*>(&node);
            if (!rb) return;
            if (rb->group() != group_) return;
            if (!rb->selected_) return;
            rb->selected_ = false;
            rb->markStyled();
            rb->RequestWindowRedraw();
        });
    }

    void RadioButton::Activate() {
        if (!isEnabled()) return;
        if (selected_) return;

        uncheckSiblings(*this);
        selected_ = true;
        markStyled();
        RequestWindowRedraw();

        if (onSelected_)     onSelected_(*this);
        if (onSelectedBool_) onSelectedBool_(*this, true);
    }

    void RadioButton::SetOnSelected(SelectedAction handler) {
        onSelected_ = std::move(handler);
    }

    void RadioButton::SetOnSelected(std::function<void(Widget&, bool)> handler) {
        onSelectedBool_ = std::move(handler);
    }

    void RadioButton::SetIndicatorSize(float size) {
        indicatorSize_ = (size < 0.0f) ? 0.0f : size;
        markStyled();
        RequestWindowRedraw();
    }

    void RadioButton::SetIndicatorShape(IndicatorShape shape) {
        if (shape_ == shape) return;
        shape_ = shape;
        markStyled();
        RequestWindowRedraw();
    }

    void RadioButton::SetMarkStyle(RadioMark mark) {
        if (mark_ == mark) return;
        mark_ = mark;
        markStyled();
        RequestWindowRedraw();
    }

    void RadioButton::SetSpacing(float gap) {
        spacing_ = (gap < 0.0f) ? 0.0f : gap;
        markStyled();
        RequestWindowRedraw();
    }

    void RadioButton::SetIndicatorColor(const Color4f& c) { indicatorColor_ = c; markStyled();   RequestWindowRedraw();}
    void RadioButton::SetIndicatorBorderColor(const Color4f& c) { indicatorBorderColor_ = c; markStyled();   RequestWindowRedraw();}
    void RadioButton::SetMarkColor(const Color4f& c) { markColor_ = c; markStyled();   RequestWindowRedraw();}

    float RadioButton::resolvedIndicatorSize() const {
        if (indicatorSize_ > 0.0f) return indicatorSize_;
        if (const Window* w = window()) {
            const TextMetrics tm = const_cast<Window*>(w)->MeasureText(text_, textStyle_.font);
            return detail::ResolveIndicatorSize(indicatorSize_, tm.height);
        }
        return detail::ResolveIndicatorSize(indicatorSize_, 0.0f);
    }

    float RadioButton::hintWidth() const {
        if (prefWidth() > 0.0f) return prefWidth();
        TextMetrics tm{};
        if (const Window* w = window())
            tm = const_cast<Window*>(w)->MeasureText(text_, textStyle_.font);
        return detail::IndicatorHints(box_, resolvedIndicatorSize(), spacing_, tm).width;
    }

    float RadioButton::hintHeight() const {
        if (prefHeight() > 0.0f) return prefHeight();
        TextMetrics tm{};
        if (const Window* w = window())
            tm = const_cast<Window*>(w)->MeasureText(text_, textStyle_.font);
        return detail::IndicatorHints(box_, resolvedIndicatorSize(), spacing_, tm).height;
    }

    void RadioButton::ApplyTheme(const Theme& t) {
        TextWidget::ApplyTheme(t);
        indicatorColor_       = t.checkbox.indicatorColor;
        indicatorBorderColor_ = t.checkbox.indicatorBorderColor;
        markColor_            = t.checkbox.markColor;
    }

    const BoxStyle& RadioButton::themeBox(const Theme& t) const {
        return t.checkbox.box;
    }

    const TextStyle& RadioButton::themeText(const Theme& t) const {
        return t.checkbox.text;
    }

    bool RadioButton::OnMousePress(MouseEvent& e) {
        return detail::IndicatorPress(*this, e, isEnabled(), pressed_);
    }

    bool RadioButton::OnMouseRelease(MouseEvent& e) {
        bool hit = false;
        if (!detail::IndicatorRelease(*this, e, isEnabled(), pressed_, hit)) return false;
        if (hit) Activate();
        return true;
    }

    bool RadioButton::OnKeyPress(KeyEvent& e) {
        if (!isEnabled()) return false;
        if (e.key != Key::Space && e.key != Key::Enter) return false;

        Activate();
        e.accept();
        return true;
    }

    void RadioButton::paintContent(FrameCollector& fc) const {
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

        const float bw = std::max(1.0f, ind * 0.10f);
        const Color4f fill = selected_ ? markColor_ : indicatorColor_;
        const Color4f border = selected_ ? Color4f{ 0.0f, 0.0f, 0.0f, 0.0f } : indicatorBorderColor_;
        indicator::PaintContainer(fc, shape_, ix, iy, ind, fill, border, bw);

        if (selected_) {
            indicator::PaintRadioMark(fc, mark_, shape_, ix, iy, ind,
                indicatorColor_, indicatorColor_);
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
