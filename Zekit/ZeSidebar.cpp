
#include "pch.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "include/ZeSidebar.h"
#include "include/Zekit.h"

#include "ZeDrawUtil.h"
#include "ZeFrameCollector.h"

namespace Kit {

    namespace {
        constexpr float kDefaultRowHeight = 34.0f;
        constexpr float kDefaultHeadHeight = 36.0f;
        constexpr float kDefaultPad = 8.0f;
        constexpr float kRowTextPad = 12.0f;
        constexpr float kFallbackW = 180.0f;
        constexpr float kActiveRowAlpha = 0.14f;
    }

    Sidebar::Sidebar() {
        SetOrientation(StripOrientation::Vertical);

        ApplyTheme(Themes::Light());

        syncPadding();

        SetFocusable(true);

        SetDefaultSize(kFallbackW, 0.0f);
    }

    void Sidebar::ApplyTheme(const Theme& t) {
        ItemStripBase::ApplyTheme(t);

        bodyPad_ = (t.sidebar.padding > 0.0f) ? t.sidebar.padding : kDefaultPad;
        if (headerHeight_ <= 0.0f) headerHeight_ = (t.ctrlHM > 0.0f) ? t.ctrlHM : kDefaultHeadHeight;
        if (footerHeight_ <= 0.0f) footerHeight_ = (t.ctrlHM > 0.0f) ? t.ctrlHM : kDefaultHeadHeight;

        if (indicatorColor_.a <= 0.0f)
            indicatorColor_ = t.itemStrip.itemActiveColor.a > 0.0f
                ? t.itemStrip.itemActiveColor
                : t.primary;

        if (styleTheme_.itemActiveColor.a > 0.0f)
            styleTheme_.itemActiveColor.a =
                std::min(styleTheme_.itemActiveColor.a, kActiveRowAlpha);

        syncPadding();
        Invalidate();
    }

    void Sidebar::syncPadding() {
        const bool showHead = !collapsed_ && !header_.empty();
        const bool showFoot = !collapsed_ && !footer_.empty();

        const float top = bodyPad_ + (showHead ? headerHeight_ : 0.0f);
        const float bottom = bodyPad_ + (showFoot ? footerHeight_ : 0.0f);
        SetPadding(bodyPad_, top, bodyPad_, bottom);
    }

    int Sidebar::AddItem(std::string label, bool enabled) {
        return ItemStripBase::AddItem(std::move(label), enabled);
    }

    void Sidebar::SetItems(const std::vector<std::string>& labels) {
        ItemStripBase::SetItems(labels);
        RequestWindowRedraw();
    }

    void Sidebar::SetHeader(std::string text) {
        header_ = std::move(text);
        syncPadding();
        Invalidate();
        notifyLayoutInputChanged();
        RequestWindowRedraw();
    }

    void Sidebar::SetFooter(std::string text) {
        footer_ = std::move(text);
        syncPadding();
        Invalidate();
        notifyLayoutInputChanged();
        RequestWindowRedraw();
    }

    void Sidebar::SetHeaderHeight(float h) {
        headerHeight_ = (h > 0.0f) ? h : 0.0f;
        syncPadding();
        Invalidate();
        RequestWindowRedraw();
    }

    void Sidebar::SetFooterHeight(float h) {
        footerHeight_ = (h > 0.0f) ? h : 0.0f;
        syncPadding();
        Invalidate();
        RequestWindowRedraw();
    }

    void Sidebar::SetCollapsed(bool collapsed) {
        if (collapsed_ == collapsed) return;
        collapsed_ = collapsed;

        if (collapsed_) {
            const float w = width();
            expandedWidth_ = (w > collapsedWidth_) ? w : 0.0f;
            SetDefaultSize(collapsedWidth_, 0.0f);
            SetWidth(collapsedWidth_);
        }
        else {
            const float back = (expandedWidth_ > 0.0f) ? expandedWidth_ : kFallbackW;
            SetDefaultSize(kFallbackW, 0.0f);
            SetWidth(back);
        }

        syncPadding();

        Invalidate();
        notifyLayoutInputChanged();
        RequestWindowRedraw();
    }

    void Sidebar::SetCollapsedWidth(float w) {
        collapsedWidth_ = (w > 0.0f) ? w : 0.0f;
        if (collapsed_) {
            SetWidth(collapsedWidth_);
            Invalidate();
            notifyLayoutInputChanged();
        }
        RequestWindowRedraw();
    }

    void Sidebar::SetIndicatorWidth(float w) {
        indicatorWidth_ = (w > 0.0f) ? w : 0.0f;
        markStyled();
        RequestWindowRedraw();
    }

    void Sidebar::SetIndicatorColor(const Color4f& c) {
        indicatorColor_ = c;
        markStyled();
        RequestWindowRedraw();
    }

    float Sidebar::measureStripItem(int index) const {
        (void)index;
        if (styleTheme_.itemHeight > 0.0f) return styleTheme_.itemHeight;
        return kDefaultRowHeight;
    }

    void Sidebar::paintStripItem(FrameCollector& fc, int index, const ItemRect& r) const {
        if (index < 0 || index >= static_cast<int>(itemCount())) return;
        if (r.w <= 0.0f || r.h <= 0.0f) return;

        const bool active = (index == currentIndex());
        const bool enabled = isItemEnabled(index);
        const bool hovered = enabled && (index == hoverIndex());

        Color4f bg{ 0.0f, 0.0f, 0.0f, 0.0f };
        if (active)          bg = styleTheme_.itemActiveColor;
        else if (hovered)    bg = styleTheme_.itemHoverColor;
        if (bg.a > 0.0f)
            drawutil::PaintRoundedRect(fc, r.x, r.y, r.w, r.h, styleTheme_.radius, bg);

        if (active && indicatorWidth_ > 0.0f && indicatorColor_.a > 0.0f) {
            const float barH = r.h * 0.62f;
            drawutil::PaintRoundedRect(fc, r.x, r.y + (r.h - barH) * 0.5f,
                indicatorWidth_, barH, indicatorWidth_ * 0.5f, indicatorColor_);
        }

        const TextStyle& ts = itemTextStyle(index);
        const FontDesc& font = ts.font;

        Color4f fg = ts.colors.get(WidgetState::Normal).text;
        if (!enabled && styleTheme_.disabledColor.a > 0.0f) fg = styleTheme_.disabledColor;

        const std::string& label = itemLabel(index);
        const float textX = r.x + kRowTextPad + (active ? indicatorWidth_ : 0.0f);
        const float textW = std::max(0.0f, r.x + r.w - textX - kRowTextPad);
        if (textW <= 0.0f) return;

        std::string shown = label;
        TextMetrics tm = fc.MeasureText(shown, font);
        if (tm.width > textW) {
            shown = drawutil::Ellipsize(fc, label, font, textW);
            tm = fc.MeasureText(shown, font);
        }
        if (tm.width <= 0.0f) return;

        const float ty = r.y + (r.h - tm.height) * 0.5f;
        fc.AddText(shown, font, textX, ty + tm.ascent, fg);
    }

    void Sidebar::OnCurrentChangedByUser(int index) {
        if (onActivated_)     onActivated_(*this, index);
        if (onActivatedSelf_) onActivatedSelf_(*this);
    }

    float Sidebar::hintWidth() const {
        if (prefWidth() > 0.0f) return prefWidth();
        if (collapsed_) return collapsedWidth_;

        float w = ItemStripBase::hintWidth();

        if (Window* win = window()) {
            if (!header_.empty())
                w = std::max(w, win->MeasureText(header_, styleTheme_.item.font).width + bodyPad_ * 2.0f);
            if (!footer_.empty())
                w = std::max(w, win->MeasureText(footer_, styleTheme_.item.font).width + bodyPad_ * 2.0f);
        }
        return w;
    }

    float Sidebar::hintHeight() const {
        if (prefHeight() > 0.0f) return prefHeight();

        return ItemStripBase::hintHeight();
    }

    void Sidebar::Paint(FrameCollector& fc) const {
        const float w = width();
        const float h = height();
        if (w <= 0.0f || h <= 0.0f) return;

        const Theme& t = fc.GetTheme();
        const SurfaceStyle& ss = t.sidebar;

        if (ss.background.a > 0.0f)
            drawutil::PaintRoundedRect(fc, x(), y(), w, h, 0.0f, ss.background);
        if (ss.borderWidth > 0.0f && ss.borderColor.a > 0.0f)
            drawutil::PaintBox(fc, x(), y(), w, h, 0.0f, ss.borderWidth, ss.borderColor,
                Color4f{ 0.0f, 0.0f, 0.0f, 0.0f });

        if (ss.dividerColor.a > 0.0f && !collapsed_) {
            const float dw = (ss.dividerWidth > 0.0f) ? ss.dividerWidth : 1.0f;
            drawutil::PaintVLine(fc, x() + w - dw, y(), h, dw, ss.dividerColor);
        }

        if (!header_.empty() && headerHeight_ > 0.0f && !collapsed_) {
            const TextStyle& ts = ss.title;
            const TextMetrics tm = fc.MeasureText(header_, ts.font);
            if (tm.width > 0.0f) {
                const float tx = x() + bodyPad_;
                const float ty = y() + bodyPad_ + (headerHeight_ - tm.height) * 0.5f;
                fc.AddText(header_, ts.font, tx, ty + tm.ascent,
                    ts.colors.get(WidgetState::Normal).text);
            }
        }

        ItemStripBase::Paint(fc);

        if (!footer_.empty() && footerHeight_ > 0.0f && !collapsed_) {
            const TextStyle& ts = ss.title;
            const TextMetrics tm = fc.MeasureText(footer_, ts.font);
            if (tm.width > 0.0f) {
                const float tx = std::max(x() + bodyPad_, x() + w - bodyPad_ - tm.width);
                const float fy = y() + h - bodyPad_ - footerHeight_;
                const float ty = fy + (footerHeight_ - tm.height) * 0.5f;
                fc.AddText(footer_, ts.font, tx, ty + tm.ascent,
                    ts.colors.get(WidgetState::Normal).text);
            }
        }
    }

}
