
#include "pch.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "include/ZeCollapse.h"
#include "include/Zekit.h"

#include "ZeFrameCollector.h"
#include "ZeDrawUtil.h"

namespace Kit {

    namespace {
        constexpr float kDefaultHeadHeight = 36.0f;
        constexpr float kDefaultIndicatorSize = 5.0f;
        constexpr float kDefaultIndicatorThickness = 1.6f;
        constexpr float kHeadPadLeft = 12.0f;
        constexpr float kHeadPadRight = 14.0f;
        constexpr float kFallbackW = 280.0f;
        constexpr float kFallbackH = 120.0f;
    }

    Collapse::Collapse() {
        SetDirection(FlexDirection::Column);
        SetAlign(AlignItems::Stretch);

        SetPadding(0.0f, 0.0f, 0.0f, 0.0f);

        ApplyTheme(Themes::Light());

        SetFocusable(true);
        SetDefaultSize(kFallbackW, kFallbackH);
    }

    void Collapse::ApplyTheme(const Theme& t) {
        styleTheme_ = t.collapse;
        if (headHeight_ <= 0.0f) headHeight_ = kDefaultHeadHeight;
        if (indicatorSize_ <= 0.0f) indicatorSize_ = kDefaultIndicatorSize;
        if (indicatorThickness_ <= 0.0f) indicatorThickness_ = kDefaultIndicatorThickness;
        Invalidate();
    }

    void Collapse::SetHeadHeight(float h) {
        headHeight_ = (h > 0.0f) ? h : kDefaultHeadHeight;
        Invalidate();
        RequestWindowRedraw();
    }

    void Collapse::SetIndicator(float size, float thickness) {
        indicatorSize_ = (size > 0.0f) ? size : kDefaultIndicatorSize;
        indicatorThickness_ = (thickness > 0.0f) ? thickness : kDefaultIndicatorThickness;
        RequestWindowRedraw();
    }

    Panel* Collapse::AddItem(std::string title) {
        Item it;
        it.title = std::move(title);

        Panel content;
        content.SetDirection(FlexDirection::Column);
        content.SetAlign(AlignItems::Stretch);
        Panel& placed = AddChild(std::move(content));

        placed.SetVisible(false);
        it.content = &placed;

        items_.push_back(it);

        Invalidate();
        notifyLayoutInputChanged();
        requestRepaint();
        return &placed;
    }

    Panel* Collapse::itemContent(int index) {
        if (index < 0 || index >= static_cast<int>(items_.size())) return nullptr;
        return items_[static_cast<std::size_t>(index)].content;
    }

    void Collapse::SetItemTitle(int index, std::string title) {
        if (index < 0 || index >= static_cast<int>(items_.size())) return;
        items_[static_cast<std::size_t>(index)].title = std::move(title);
        requestRepaint();
        RequestWindowRedraw();
    }

    const std::string& Collapse::itemTitle(int index) const {
        static const std::string kEmpty;
        if (index < 0 || index >= static_cast<int>(items_.size())) return kEmpty;
        return items_[static_cast<std::size_t>(index)].title;
    }

    void Collapse::ClearItems() {
        items_.clear();
        hover_ = -1;
        pressed_ = -1;
        focus_ = -1;
        Invalidate();
        notifyLayoutInputChanged();
        requestRepaint();
    }

    void Collapse::SetItemEnabled(int index, bool enabled) {
        if (index < 0 || index >= static_cast<int>(items_.size())) return;
        Item& it = items_[static_cast<std::size_t>(index)];
        if (it.enabled == enabled) return;
        it.enabled = enabled;
        if (!enabled) it.expanded = false;
        if (it.content) it.content->SetVisible(it.expanded);
        Invalidate();
        requestRepaint();
        RequestWindowRedraw();
    }

    bool Collapse::isItemEnabled(int index) const {
        if (index < 0 || index >= static_cast<int>(items_.size())) return false;
        return items_[static_cast<std::size_t>(index)].enabled;
    }

    void Collapse::SetItemExpanded(int index, bool expanded) {
        if (index < 0 || index >= static_cast<int>(items_.size())) return;

        Item& it = items_[static_cast<std::size_t>(index)];
        if (!it.enabled && expanded) return;

        if (expanded && accordion_) {
            for (std::size_t i = 0; i < items_.size(); ++i) {
                if (static_cast<int>(i) == index) continue;
                items_[i].expanded = false;
                if (items_[i].content) items_[i].content->SetVisible(false);
            }
        }

        if (it.expanded == expanded) {
            if (it.content) it.content->SetVisible(it.expanded);
            return;
        }

        it.expanded = expanded;
        if (it.content) it.content->SetVisible(it.expanded);

        refreshAfterToggle();
        RequestWindowRedraw();
    }

    bool Collapse::isItemExpanded(int index) const {
        if (index < 0 || index >= static_cast<int>(items_.size())) return false;
        return items_[static_cast<std::size_t>(index)].expanded;
    }

    std::vector<int> Collapse::expandedItems() const {
        std::vector<int> out;
        for (std::size_t i = 0; i < items_.size(); ++i)
            if (items_[i].expanded) out.push_back(static_cast<int>(i));
        return out;
    }

    void Collapse::SetAccordion(bool on) {
        if (accordion_ == on) return;
        accordion_ = on;

        if (accordion_) {
            bool kept = false;
            for (Item& it : items_) {
                if (!it.expanded) continue;
                if (!kept) { kept = true; continue; }
                it.expanded = false;
                if (it.content) it.content->SetVisible(false);
            }
            refreshAfterToggle();
        }
        RequestWindowRedraw();
    }

    void Collapse::refreshAfterToggle() {
        Invalidate();
        notifyLayoutInputChanged();
        requestRepaint();
    }

    void Collapse::requestRepaint() {
        RequestWindowRedraw();
    }

    void Collapse::bodyRect(float& bx, float& by, float& bw, float& bh) const {
        const float border = std::max({ styleTheme_.panel.border.width[Border::Top],
                                        styleTheme_.panel.border.width[Border::Right],
                                        styleTheme_.panel.border.width[Border::Bottom],
                                        styleTheme_.panel.border.width[Border::Left] });
        bx = x() + border;
        by = y() + border;
        bw = std::max(0.0f, width() - border * 2.0f);
        bh = std::max(0.0f, height() - border * 2.0f);
    }

    bool Collapse::headRect(int index, float& hx, float& hy, float& hw, float& hh) const {
        if (index < 0 || index >= static_cast<int>(items_.size())) return false;

        float bx = 0.0f, by = 0.0f, bw = 0.0f, bh = 0.0f;
        bodyRect(bx, by, bw, bh);
        if (bw <= 0.0f) return false;

        const float cpPad = (styleTheme_.contentPadding > 0.0f) ? styleTheme_.contentPadding : 0.0f;

        float cy = by;
        for (int i = 0; i < index; ++i) {
            cy += headHeight_ + styleTheme_.itemGap;
            const float ch = contentHeight(i);
            if (ch <= 0.0f) continue;
            cy += ch + cpPad * 2.0f;
        }

        hx = bx;
        hy = cy;
        hw = bw;
        hh = headHeight_;
        return true;
    }

    int Collapse::headAtPoint(float px, float py) const {
        for (int i = 0; i < static_cast<int>(items_.size()); ++i) {
            float hx = 0.0f, hy = 0.0f, hw = 0.0f, hh = 0.0f;
            if (!headRect(i, hx, hy, hw, hh)) continue;
            if (drawutil::HitRect(px, py, hx, hy, hw, hh)) return i;
        }
        return -1;
    }

    float Collapse::contentHeight(int index) const {
        if (index < 0 || index >= static_cast<int>(contentHeights_.size())) return 0.0f;
        return contentHeights_[static_cast<std::size_t>(index)];
    }

    void Collapse::DoLayout() {
        const std::vector<Widget*> kids = children();

        for (Widget* c : kids) LayoutAccess::RestorePref(*c);

        float bx = 0.0f, by = 0.0f, bw = 0.0f, bh = 0.0f;
        bodyRect(bx, by, bw, bh);

        const float cpPad = (styleTheme_.contentPadding > 0.0f) ? styleTheme_.contentPadding : 0.0f;

        const float cx = bx + cpPad;
        float cy = by;
        float maxW = 0.0f;

        const float contentW = std::max(0.0f, bw - cpPad * 2.0f);

        contentHeights_.assign(items_.size(), 0.0f);

        for (std::size_t k = 0; k < items_.size(); ++k) {
            Item& it = items_[k];

            cy += headHeight_ + styleTheme_.itemGap;

            Panel* cp = it.content;
            if (!cp) continue;

            if (cp->isVisible()) {
                if (cpPad > 0.0f) cy += cpPad;

                const float ch = std::max(0.0f, cp->hintHeightFor(contentW));
                contentHeights_[k] = ch;

                LayoutAccess::SetActual(*cp, contentW, ch);
                cp->SetPosition(cx, cy);
                cy += ch;
                if (cpPad > 0.0f) cy += cpPad;
                maxW = std::max(maxW, contentW);
            }
            else {
                contentHeights_[k] = 0.0f;
                LayoutAccess::SetActual(*cp, 0.0f, 0.0f);
                cp->SetPosition(cx, cy);
            }
        }

        contentW_ = std::max(maxW, contentW);
        contentH_ = std::max(0.0f, cy - by);
    }

    float Collapse::hintWidth() const {
        if (prefWidth() > 0.0f) return prefWidth();

        float widest = 0.0f;
        for (const Item& it : items_) {
            if (it.content && it.content->isVisible())
                widest = std::max(widest, it.content->hintWidth());
        }

        float bx = 0.0f, by = 0.0f, bw = 0.0f, bh = 0.0f;
        bodyRect(bx, by, bw, bh);
        const float border = std::max({ styleTheme_.panel.border.width[Border::Top],
                                        styleTheme_.panel.border.width[Border::Right],
                                        styleTheme_.panel.border.width[Border::Bottom],
                                        styleTheme_.panel.border.width[Border::Left] });

        if (Window* w = window()) {
            for (const Item& it : items_) {
                const float tw = w->MeasureText(it.title, styleTheme_.head.font).width
                    + kHeadPadLeft + kHeadPadRight;
                widest = std::max(widest, tw);
            }
        }

        return widest + border * 2.0f;
    }

    float Collapse::hintHeight() const {
        if (prefHeight() > 0.0f) return prefHeight();

        float total = 0.0f;
        for (std::size_t i = 0; i < items_.size(); ++i) {
            total += headHeight_ + styleTheme_.itemGap;
            float ch = contentHeight(static_cast<int>(i));
            if (ch <= 0.0f && items_[i].expanded && items_[i].content
                && items_[i].content->isVisible()) {
                ch = std::max(0.0f, items_[i].content->hintHeightFor(contentAvailableW()));
            }
            if (ch > 0.0f) {
                const float cpPad = (styleTheme_.contentPadding > 0.0f) ? styleTheme_.contentPadding : 0.0f;
                ch += cpPad * 2.0f;
            }
            total += ch;
        }

        const float border = std::max({ styleTheme_.panel.border.width[Border::Top],
                                        styleTheme_.panel.border.width[Border::Right],
                                        styleTheme_.panel.border.width[Border::Bottom],
                                        styleTheme_.panel.border.width[Border::Left] });
        return total + border * 2.0f;
    }

    void Collapse::Paint(FrameCollector& fc) const {
        const float w = width();
        const float h = height();
        if (w <= 0.0f || h <= 0.0f) return;

        const ColorSet boxCs = styleTheme_.panel.colors.get(WidgetState::Normal);
        const float border = std::max({ styleTheme_.panel.border.width[Border::Top],
                                        styleTheme_.panel.border.width[Border::Right],
                                        styleTheme_.panel.border.width[Border::Bottom],
                                        styleTheme_.panel.border.width[Border::Left] });
        Color4f bg = boxCs.background;
        Color4f bd = boxCs.border;
        bg.a *= styleTheme_.panel.opacity;
        bd.a *= styleTheme_.panel.opacity;
        drawutil::PaintBox(fc, x(), y(), w, h, styleTheme_.radius, border, bd, bg);

        if (items_.empty()) return;

        const Color4f normalText = styleTheme_.head.colors.get(WidgetState::Normal).text;
        const Color4f activeText = styleTheme_.headActive.colors.get(WidgetState::Normal).text;
        const Color4f disabledText = styleTheme_.headDisabled.colors.get(WidgetState::Normal).text;

        for (int i = 0; i < static_cast<int>(items_.size()); ++i) {
            float hx = 0.0f, hy = 0.0f, hw = 0.0f, hh = 0.0f;
            if (!headRect(i, hx, hy, hw, hh)) continue;
            if (hw <= 0.0f || hh <= 0.0f) continue;

            const Item& it = items_[static_cast<std::size_t>(i)];
            const bool enabled = it.enabled;
            const bool expanded = it.expanded;
            const bool hovered = enabled && (i == hover_);

            Color4f headBg{ 0.0f, 0.0f, 0.0f, 0.0f };
            if (expanded)                              headBg = styleTheme_.headActiveColor;
            else if (hovered)                          headBg = styleTheme_.headHoverColor;
            else                                       headBg = styleTheme_.headColor;
            if (headBg.a > 0.0f)
                drawutil::PaintRoundedRect(fc, hx, hy, hw, hh, 0.0f, headBg);

            const bool lastItem = (i + 1 == static_cast<int>(items_.size()));
            if (styleTheme_.dividerColor.a > 0.0f && !(lastItem && !expanded)) {
                drawutil::PaintHLine(fc, hx, hy + hh - styleTheme_.dividerColor.a * 0.0f - 1.0f,
                    hw, 1.0f, styleTheme_.dividerColor);
            }

            const drawutil::ChevronDir dir = expanded
                ? drawutil::ChevronDir::Down
                : drawutil::ChevronDir::Right;
            Color4f arrow = styleTheme_.indicatorColor;
            if (!enabled) arrow.a *= 0.5f;
            drawutil::PaintChevron(fc, hx + kHeadPadLeft * 0.5f + indicatorSize_,
                hy + hh * 0.5f, indicatorSize_, indicatorThickness_, dir, arrow);

            const TextStyle& ts = !enabled ? styleTheme_.headDisabled
                : (expanded ? styleTheme_.headActive : styleTheme_.head);
            const FontDesc& font = ts.font;

            Color4f fg = normalText;
            if (!enabled)      fg = disabledText;
            else if (expanded) fg = activeText;

            const float textX = hx + kHeadPadLeft + indicatorSize_ * 2.0f + 6.0f;
            const float textW = std::max(0.0f, hx + hw - textX - kHeadPadRight);
            if (textW <= 0.0f) continue;

            std::string shown = it.title;
            TextMetrics tm = fc.MeasureText(shown, font);
            if (tm.width > textW) {
                shown = drawutil::Ellipsize(fc, it.title, font, textW);
                tm = fc.MeasureText(shown, font);
            }
            if (tm.width <= 0.0f) continue;

            const float ty = hy + (hh - tm.height) * 0.5f;
            fc.AddText(shown, font, textX, ty + tm.ascent, fg);
        }
    }

    bool Collapse::OnMousePress(MouseEvent& e) {
        if (e.button != MouseButton::Left) return false;

        const int i = headAtPoint(e.x, e.y);
        if (i < 0) return false;
        if (!isItemEnabled(i)) { e.accept(); return true; }

        pressed_ = i;
        focus_ = i;
        e.accept();
        return true;
    }

    bool Collapse::OnMouseRelease(MouseEvent& e) {
        if (pressed_ < 0) return false;

        const int was = pressed_;
        pressed_ = -1;

        if (headAtPoint(e.x, e.y) == was) toggleByUser(was);

        e.accept();
        return true;
    }

    bool Collapse::OnMouseMove(MouseEvent& e) {
        const int i = headAtPoint(e.x, e.y);
        if (i == hover_) return false;

        hover_ = i;
        requestRepaint();
        return true;
    }

    void Collapse::OnMouseLeave() {
        if (hover_ == -1) return;
        hover_ = -1;
        requestRepaint();
    }

    bool Collapse::OnKeyPress(KeyEvent& e) {
        const int n = static_cast<int>(items_.size());
        if (n <= 0) return false;

        switch (e.key) {
        case Key::UpArrow:
        case Key::DownArrow: {
            int cur = (focus_ >= 0) ? focus_ : 0;
            const int dir = (e.key == Key::DownArrow) ? 1 : -1;
            for (int k = 0; k < n; ++k) {
                cur = (cur + dir + n) % n;
                if (isItemEnabled(cur)) { focus_ = cur; requestRepaint(); break; }
            }
            e.accept();
            return true;
        }

        case Key::Space:
        case Key::Enter: {
            const int idx = (focus_ >= 0) ? focus_ : 0;
            if (isItemEnabled(idx)) toggleByUser(idx);
            e.accept();
            return true;
        }

        default:
            return false;
        }
    }

    void Collapse::toggleByUser(int index) {
        if (index < 0 || index >= static_cast<int>(items_.size())) return;
        if (!isItemEnabled(index)) return;

        Item& it = items_[static_cast<std::size_t>(index)];
        const bool next = !it.expanded;

        if (next && accordion_) {
            for (std::size_t i = 0; i < items_.size(); ++i) {
                if (static_cast<int>(i) == index) continue;
                items_[i].expanded = false;
                if (items_[i].content) items_[i].content->SetVisible(false);
            }
        }

        it.expanded = next;
        if (it.content) it.content->SetVisible(it.expanded);

        refreshAfterToggle();
        notifyToggled(index, it.expanded);
    }

    void Collapse::notifyToggled(int index, bool expanded) {
        if (onToggled_)      onToggled_(*this, index, expanded);
        if (onToggledIndex_) onToggledIndex_(*this, index);
        if (onToggledSelf_)  onToggledSelf_(*this);
    }

}
