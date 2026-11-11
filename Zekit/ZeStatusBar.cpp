
#include "pch.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "include/ZeStatusBar.h"
#include "include/ZeLabel.h"
#include "include/Zekit.h"

#include "ZeDrawUtil.h"
#include "ZeFrameCollector.h"

namespace Kit {

    namespace {
        constexpr float kFallbackHeight = 26.0f;
        constexpr float kFallbackPadding = 6.0f;
        constexpr float kDividerMargin = 4.0f;
        constexpr float kStretchBase = 8.0f;
    }

    void StatusBar::Divider::SetThickness(float thickness) {
        thickness_ = (thickness > 0.0f) ? thickness : 1.0f;
        SetDefaultSize(thickness_, kFallbackHeight);
        notifyLayoutInputChanged();
    }

    void StatusBar::Divider::ApplyTheme(const Theme& t) {
        SetThickness((t.statusBar.dividerWidth > 0.0f) ? t.statusBar.dividerWidth : 1.0f);
        SetAlignSelf(AlignItems::Center);
    }

    void StatusBar::Divider::Paint(FrameCollector& fc) const {
        const float w = width();
        const float h = height();
        if (w <= 0.0f || h <= 0.0f) return;

        const Color4f col = fc.GetTheme().statusBar.dividerColor;
        if (col.a <= 0.0f) return;

        const float len = h * ratio_;
        const float x0 = x() + (w - thickness_) * 0.5f;
        const float y0 = y() + (h - len) * 0.5f;
        drawutil::PaintVLine(fc, x0, y0, len, thickness_, col);
    }

    StatusBar::StatusBar() {
        SetDirection(FlexDirection::Row);
        SetAlign(AlignItems::Center);

        ApplyTheme(Themes::Light());

        SetDefaultSize(0.0f, kFallbackHeight);
    }

    void StatusBar::ApplyTheme(const Theme& t) {
        styleTheme_ = t.statusBar;

        const float p = (styleTheme_.padding > 0.0f) ? styleTheme_.padding : kFallbackPadding;
        SetPadding(p, p, p, p);

        for (Divider* d : dividers_) {
            if (d) d->ApplyTheme(t);
        }

        for (Label* s : sections_) {
            if (!s) continue;
            s->ApplyTheme(t);
            s->SetTextColor(styleTheme_.body.colors.get(WidgetState::Normal).text);
            s->SetFontDesc(styleTheme_.body.font);
        }

        Invalidate();
    }

    void StatusBar::ensureLeadingDivider() {
        if (!showDividers_) return;
        if (sections_.empty()) return;

        Divider d;
        d.SetMargin(kDividerMargin, 0.0f, kDividerMargin, 0.0f);
        d.ApplyTheme(Themes::Light());
        Divider& placed = AddWidgetUnchecked(std::move(d));
        dividers_.push_back(&placed);
    }

    Label& StatusBar::AddSection(std::string text) {
        ensureLeadingDivider();

        Label lb(std::move(text));
        lb.SetTextColor(styleTheme_.body.colors.get(WidgetState::Normal).text);
        lb.SetFontDesc(styleTheme_.body.font);

        Label& placed = AddChild(std::move(lb));
        sections_.push_back(&placed);

        Invalidate();
        notifyLayoutInputChanged();
        RequestWindowRedraw();
        return placed;
    }

    void StatusBar::AddStretch(float grow) {
        Stretch st;
        st.SetFlexGrow((grow > 0.0f) ? grow : 1.0f);
        st.SetDefaultSize(kStretchBase, 0.0f);
        AddWidgetUnchecked(std::move(st));

        Invalidate();
        notifyLayoutInputChanged();
    }

    void StatusBar::SetSectionText(int index, const std::string& text) {
        if (index < 0 || index >= static_cast<int>(sections_.size())) return;
        Label* lb = sections_[static_cast<std::size_t>(index)];
        if (!lb) return;
        lb->SetText(text);
        Invalidate();
        notifyLayoutInputChanged();
        RequestWindowRedraw();
    }

    const std::string& StatusBar::sectionText(int index) const {
        static const std::string kEmpty;
        if (index < 0 || index >= static_cast<int>(sections_.size())) return kEmpty;
        const Label* lb = sections_[static_cast<std::size_t>(index)];
        return lb ? lb->Text() : kEmpty;
    }

    void StatusBar::ClearSections() {
        const std::vector<Label*>   sections = sections_;
        const std::vector<Divider*> dividers = dividers_;
        sections_.clear();
        dividers_.clear();

        for (Label* s : sections)   if (s) DestroyWidget(*s);
        for (Divider* d : dividers) if (d) DestroyWidget(*d);

        Invalidate();
        notifyLayoutInputChanged();
        RequestWindowRedraw();
    }

    void StatusBar::SetShowDividers(bool on) {
        if (showDividers_ == on) return;
        showDividers_ = on;
        for (Divider* d : dividers_) {
            if (d) d->SetVisible(on);
        }
        Invalidate();
        RequestWindowRedraw();
    }

    float StatusBar::hintWidth() const {
        if (prefWidth() > 0.0f) return prefWidth();
        return stripHintWidth(spacing());
    }

    float StatusBar::hintHeight() const {
        if (prefHeight() > 0.0f) return prefHeight();
        const float h = stripHintHeight();
        return (h > 0.0f) ? h : kFallbackHeight;
    }

    void StatusBar::Paint(FrameCollector& fc) const {
        const float w = width();
        const float h = height();
        if (w <= 0.0f || h <= 0.0f) return;

        drawutil::PaintBox(fc, x(), y(), w, h, 0.0f,
            styleTheme_.borderWidth, styleTheme_.borderColor, styleTheme_.background);

        if (styleTheme_.dividerColor.a > 0.0f) {
            const float dw = (styleTheme_.dividerWidth > 0.0f) ? styleTheme_.dividerWidth : 1.0f;
            drawutil::PaintHLine(fc, x(), y(), w, dw, styleTheme_.dividerColor);
        }
    }

}
