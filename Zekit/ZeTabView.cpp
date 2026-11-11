
#include "pch.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "include/ZeTabView.h"
#include "ZeDrawUtil.h"
#include "include/Zekit.h"

#include "ZeFrameCollector.h"
#include "ZeRoundedRectShape.h"

namespace Kit {

    namespace {
    }

    TabView::TabView() {
        SetDirection(FlexDirection::Column);
        SetAlign(AlignItems::Stretch);
        SetPadding(0.0f, barHeight_, 0.0f, 0.0f);
        SetFocusable(true);
        ApplyTheme(Themes::Light());
    }

    int TabView::AddTab(std::string title) {
        titles_.push_back(std::move(title));

        Panel page;
        page.SetAlign(AlignItems::Stretch);
        Panel& placed = AddChild(std::move(page));
        pages_.push_back(&placed);

        if (current_ < 0) {
            current_ = 0;
            placed.SetVisible(true);
        }
        else {
            placed.SetVisible(false);
        }

        Invalidate();
        requestRedraw();
        return static_cast<int>(titles_.size()) - 1;
    }

    Panel* TabView::tabContent(int index) {
        if (index < 0 || index >= static_cast<int>(pages_.size())) return nullptr;
        return pages_[static_cast<std::size_t>(index)];
    }

    void TabView::SetTabTitle(int index, std::string title) {
        if (index < 0 || index >= static_cast<int>(titles_.size())) return;
        titles_[static_cast<std::size_t>(index)] = std::move(title);
        requestRedraw();
        RequestWindowRedraw();
    }

    const std::string& TabView::tabTitle(int index) const {
        static const std::string kEmpty;
        if (index < 0 || index >= static_cast<int>(titles_.size())) return kEmpty;
        return titles_[static_cast<std::size_t>(index)];
    }

    void TabView::SetCurrentIndex(int index) {
        if (index < 0 || index >= static_cast<int>(titles_.size())) return;
        if (index == current_) return;

        current_ = index;
        for (std::size_t i = 0; i < pages_.size(); ++i)
            pages_[i]->SetVisible(static_cast<int>(i) == index);

        requestRedraw();
        RequestWindowRedraw();
    }

    void TabView::selectByUser(int index) {
        if (index < 0 || index >= static_cast<int>(titles_.size())) return;
        if (index == current_) return;

        SetCurrentIndex(index);
        notifyChanged(index);
    }

    void TabView::notifyChanged(int index) {
        if (onChanged_) onChanged_(*this, index);
        if (onChangedSelf_) onChangedSelf_(*this);
    }

    void TabView::SetTabBarHeight(float h) {
        if (h <= 0.0f) return;
        barHeight_ = h;
        SetPadding(0.0f, barHeight_, 0.0f, 0.0f);
        Invalidate();
        requestRedraw();
        RequestWindowRedraw();
    }

    void TabView::SetTabMinWidth(float w) {
        minTabWidth_ = (w > 0.0f) ? w : 0.0f;
        requestRedraw();
        RequestWindowRedraw();
    }

    void TabView::SetTabPaddingX(float px) {
        tabPadX_ = (px > 0.0f) ? px : 0.0f;
        requestRedraw();
        RequestWindowRedraw();
    }

    void TabView::SetIndicator(TabIndicator s) {
        indicator_ = s;
        requestRedraw();
        RequestWindowRedraw();
    }

    void TabView::ApplyTheme(const Theme& t) {
        barHeight_ = (t.tab.barHeight > 0.0f) ? t.tab.barHeight : 34.0f;
        minTabWidth_ = (t.tab.minTabWidth > 0.0f) ? t.tab.minTabWidth : 0.0f;
        tabPadX_ = (t.tab.tabPaddingX > 0.0f) ? t.tab.tabPaddingX : 0.0f;
        headTextStyle_ = t.tab.head;
        headActiveTextStyle_ = t.tab.headActive;
        barBackground_ = t.tab.barBackground;
        dividerColor_ = t.tab.dividerColor;
        hoverBackground_ = t.tab.hoverBackground;
        activeBackground_ = t.tab.activeBackground;
        indicatorColor_ = t.tab.indicatorColor;
        indicatorHeight_ = (t.tab.indicatorHeight > 0.0f) ? t.tab.indicatorHeight : 2.0f;
        tabRadius_ = (radiusOverride_ >= 0.0f) ? radiusOverride_ : t.tab.radius;

        SetPadding(0.0f, barHeight_, 0.0f, 0.0f);
        Invalidate();
    }

    float TabView::tabWidth(int index) const {
        if (index < 0 || index >= static_cast<int>(titles_.size())) return 0.0f;

        float textW = 0.0f;
        if (Window* w = window())
            textW = w->MeasureText(titles_[static_cast<std::size_t>(index)], headTextStyle_.font).width;

        return std::max(minTabWidth_, textW + tabPadX_ * 2.0f);
    }

    bool TabView::tabRect(int index, float& rx, float& rw) const {
        if (index < 0 || index >= static_cast<int>(titles_.size())) return false;

        float cx = x();
        for (int i = 0; i < index; ++i) cx += tabWidth(i);
        rx = cx;
        rw = tabWidth(index);
        return true;
    }

    int TabView::tabAtPoint(float px, float py) const {
        if (py < y() || py >= y() + barHeight_) return -1;
        if (px < x()) return -1;

        float cx = x();
        for (int i = 0; i < static_cast<int>(titles_.size()); ++i) {
            const float tw = tabWidth(i);
            if (px >= cx && px < cx + tw) return i;
            cx += tw;
        }
        return -1;
    }

    bool TabView::OnKeyPress(KeyEvent& e) {
        if (!isEnabled()) return false;
        const int n = static_cast<int>(titles_.size());
        if (n <= 0) return false;

        int target = current_;
        switch (e.key) {
        case Key::LeftArrow:  target = (current_ < 0) ? 0 : current_ - 1; break;
        case Key::RightArrow: target = (current_ < 0) ? 0 : current_ + 1; break;
        case Key::Home:       target = 0; break;
        case Key::End:        target = n - 1; break;
        default:
            return false;
        }

        selectByUser(std::clamp(target, 0, n - 1));
        e.accept();
        return true;
    }

    bool TabView::OnMousePress(MouseEvent& e) {
        if (!isEnabled()) return false;
        if (e.button != MouseButton::Left) return false;

        const int i = tabAtPoint(e.x, e.y);
        if (i < 0) return false;

        pressedTab_ = i;
        e.accept();
        return true;
    }

    bool TabView::OnMouseRelease(MouseEvent& e) {
        if (!isEnabled()) { pressedTab_ = -1; return false; }
        if (pressedTab_ < 0) return false;

        const int was = pressedTab_;
        pressedTab_ = -1;

        if (tabAtPoint(e.x, e.y) == was) selectByUser(was);
        e.accept();
        return true;
    }

    bool TabView::OnMouseMove(MouseEvent& e) {
        const int i = tabAtPoint(e.x, e.y);
        if (i == hoverTab_) return false;

        hoverTab_ = i;
        requestRedraw();
        return true;
    }

    void TabView::OnMouseLeave() {
        if (hoverTab_ == -1) return;
        hoverTab_ = -1;
        requestRedraw();
    }

    void TabView::Paint(FrameCollector& fc) const {
        Panel::Paint(fc);

        const float w = width();
        const float barH = barHeight_;
        if (w <= 0.0f || barH <= 0.0f) return;

        fc.PushClip(x(), y(), w, barH);

        drawutil::PaintRoundedRect(fc, x(), y(), w, barH, 0.0f, barBackground_);
        if (dividerColor_.a > 0.0f)
            drawutil::PaintRoundedRect(fc, x(), y() + barH - 1.0f, w, 1.0f, 0.0f, dividerColor_);

        const Color4f normalText = headTextStyle_.colors.get(WidgetState::Normal).text;
        const Color4f activeText = headActiveTextStyle_.colors.get(WidgetState::Normal).text;

        for (int i = 0; i < static_cast<int>(titles_.size()); ++i) {
            float rx = 0.0f, rw = 0.0f;
            if (!tabRect(i, rx, rw) || rw <= 0.0f) continue;

            const bool active = (i == current_);

            if (active) {
                if (indicator_ == TabIndicator::Fill) {
                    drawutil::PaintRoundedRect(fc, rx, y() + 4.0f, rw, barH - 8.0f, tabRadius_, activeBackground_);
                }
                else {
                    if (hoverBackground_.a > 0.0f) {  }
                    drawutil::PaintRoundedRect(fc, rx, y() + barH - indicatorHeight_,
                        rw, indicatorHeight_, indicatorHeight_ * 0.5f, indicatorColor_);
                }
            }
            else if (i == hoverTab_) {
                drawutil::PaintRoundedRect(fc, rx, y() + 4.0f, rw, barH - 8.0f, tabRadius_, hoverBackground_);
            }

            const std::string& title = titles_[static_cast<std::size_t>(i)];
            if (title.empty()) continue;

            const TextStyle& style = active ? headActiveTextStyle_ : headTextStyle_;
            const TextMetrics tm = fc.MeasureText(title, style.font);
            if (tm.width <= 0.0f) continue;

            const float tx = rx + (rw - tm.width) * 0.5f;
            const float ty = y() + (barH - tm.height) * 0.5f;
            fc.AddText(title, style.font, tx, ty + tm.ascent, active ? activeText : normalText);
        }

        fc.PopClip();
    }

    void TabView::requestRedraw() {
        RequestWindowRedraw();
    }
}
