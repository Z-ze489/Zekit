
#include "pch.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "include/ZeMenuBar.h"
#include "include/Zekit.h"

#include "ZeFrameCollector.h"
#include "ZeDrawUtil.h"

namespace Kit {

    MenuBar::MenuBar() {
        SetPadding(6.0f, 0.0f, 6.0f, 0.0f);
        SetGap(6.0f, 0.0f);

        menu_ = &AddChild(BarMenu());

        ApplyTheme(Themes::Light());
    }

    int MenuBar::AddMenu(std::string title) {
        MenuEntry m;
        m.title = std::move(title);
        menus_.push_back(std::move(m));
        requestRepaint();
        return static_cast<int>(menus_.size()) - 1;
    }

    void MenuBar::SetMenuTitle(int index, std::string title) {
        if (index < 0 || index >= static_cast<int>(menus_.size())) return;
        menus_[static_cast<std::size_t>(index)].title = std::move(title);
        requestRepaint();
        RequestWindowRedraw();
    }

    void MenuBar::SetMenuItems(int index, std::vector<MenuItem> items) {
        if (index < 0 || index >= static_cast<int>(menus_.size())) return;
        MenuEntry& m = menus_[static_cast<std::size_t>(index)];
        m.items = std::move(items);
        if (openIndex_ == index && menu_ && menu_->isOpen()) menu_->SetItems(m.items);
        requestRepaint();
        RequestWindowRedraw();
    }

    void MenuBar::SetMenuEnabled(int index, bool enabled) {
        if (index < 0 || index >= static_cast<int>(menus_.size())) return;
        menus_[static_cast<std::size_t>(index)].enabled = enabled;
        if (!enabled && openIndex_ == index) CloseMenu();
        requestRepaint();
        RequestWindowRedraw();
    }

    bool MenuBar::menuEnabled(int index) const {
        if (index < 0 || index >= static_cast<int>(menus_.size())) return false;
        return menus_[static_cast<std::size_t>(index)].enabled;
    }

    void MenuBar::RemoveMenu(int index) {
        if (index < 0 || index >= static_cast<int>(menus_.size())) return;
        if (openIndex_ == index) CloseMenu();
        menus_.erase(menus_.begin() + index);
        openIndex_ = -1;
        hoverIndex_ = -1;
        requestRepaint();
    }

    void MenuBar::ClearMenus() {
        CloseMenu();
        menus_.clear();
        hoverIndex_ = -1;
        requestRepaint();
    }

    const std::string& MenuBar::menuTitle(int index) const {
        static const std::string kEmpty;
        if (index < 0 || index >= static_cast<int>(menus_.size())) return kEmpty;
        return menus_[static_cast<std::size_t>(index)].title;
    }

    void MenuBar::CloseMenu() {
        if (menu_ && menu_->isOpen()) menu_->Close();
        openIndex_ = -1;
        requestRepaint();
    }

    void MenuBar::SetRightAligned(Widget& w, bool on) {
        if (!w.parent()) return;
        const auto it = std::find(rightZone_.begin(), rightZone_.end(), &w);
        if (on && it == rightZone_.end()) {
            rightZone_.push_back(&w);
        }
        else if (!on && it != rightZone_.end()) {
            rightZone_.erase(it);
        }
        requestRepaint();
        RequestWindowRedraw();
    }

    bool MenuBar::isRightAligned(const Widget& w) const {
        return std::find(rightZone_.begin(), rightZone_.end(), &w) != rightZone_.end();
    }

    void MenuBar::SetLeading(Widget& w, bool on) {
        if (!w.parent()) return;
        const auto it = std::find(leadingZone_.begin(), leadingZone_.end(), &w);
        if (on && it == leadingZone_.end()) {
            leadingZone_.push_back(&w);
        }
        else if (!on && it != leadingZone_.end()) {
            leadingZone_.erase(it);
        }
        requestRepaint();
        RequestWindowRedraw();
    }

    bool MenuBar::isLeading(const Widget& w) const {
        return std::find(leadingZone_.begin(), leadingZone_.end(), &w) != leadingZone_.end();
    }

    float MenuBar::resolvedBarHeight() const {
        if (barHeightOverride_ > 0.0f) return barHeightOverride_;
        return (themeBarHeight_ > 0.0f) ? themeBarHeight_ : 38.0f;
    }

    void MenuBar::requestRepaint() {
        RequestWindowRedraw();
    }

    float MenuBar::barHeight() const { return resolvedBarHeight(); }

    float MenuBar::hintHeight() const {
        if (prefHeight() > 0.0f) return prefHeight();
        return resolvedBarHeight() + padding().top + padding().bottom;
    }

    void MenuBar::ApplyTheme(const Theme& t) {
        barBox_          = t.menubar.bar;
        titleStyle_      = t.menubar.title;
        titleHover_      = t.menubar.titleHover;
        titlePadX_       = (t.menubar.titlePadX   > 0.0f) ? t.menubar.titlePadX   : 10.0f;
        titleRadius_     = (t.menubar.titleRadius > 0.0f) ? t.menubar.titleRadius : 5.0f;
        themeBarHeight_  = (t.menubar.barHeight   > 0.0f) ? t.menubar.barHeight   : 38.0f;
        requestRepaint();
    }

    void MenuBar::DoLayout() {
        const std::vector<Widget*> items = visibleChildren();

        for (Widget* it : items) LayoutAccess::RestorePref(*it);

        const float cx = contentLeft();
        const float cy = contentTop();
        const float availW = contentAvailableW();
        float H = contentAvailableH();
        if (H <= 0.0f) H = resolvedBarHeight();
        const float colGap = style_.columnGap;

        auto inItems = [&](const Widget* w) {
            return std::find(items.begin(), items.end(), w) != items.end();
        };
        auto isRight = [&](const Widget* w) {
            return std::find(rightZone_.begin(), rightZone_.end(), w) != rightZone_.end();
        };
        auto isLead = [&](const Widget* w) {
            return std::find(leadingZone_.begin(), leadingZone_.end(), w) != leadingZone_.end();
        };
        auto centerY = [&](const Widget& it) {
            const Margin& mg = it.margin();
            return cy + mg.top
                + std::max(0.0f, (H - mg.top - mg.bottom - it.height()) * 0.5f);
        };

        float cursor = cx;
        for (Widget* it : leadingZone_) {
            if (!inItems(it)) continue;
            const Margin& mg = it->margin();
            const float iw = std::max(effectiveWidth(it) + mg.left + mg.right, 0.0f);
            const float w = effectiveWidth(it);
            const float h = effectiveHeight(it);
            if (w != it->width())  LayoutAccess::SetActual(*it, w, it->height());
            if (h != it->height()) LayoutAccess::SetActual(*it, it->width(), h);
            it->SetPosition(cursor + mg.left, centerY(*it));
            cursor += iw + colGap;
        }

        if (!menus_.empty()) cursor += colGap;
        for (MenuEntry& m : menus_) {
            m.x = cursor;
            m.w = titleTextWidth(m) + titlePadX_ * 2.0f;
            cursor += m.w;
        }

        if (!menus_.empty()) cursor += colGap;
        for (Widget* it : items) {
            if (it == menu_) continue;
            if (isRight(it)) continue;
            if (isLead(it)) continue;
            const Margin& mg = it->margin();
            const float iw = std::max(effectiveWidth(it) + mg.left + mg.right, 0.0f);
            const float w = effectiveWidth(it);
            const float h = effectiveHeight(it);
            if (w != it->width())  LayoutAccess::SetActual(*it, w, it->height());
            if (h != it->height()) LayoutAccess::SetActual(*it, it->width(), h);
            it->SetPosition(cursor + mg.left, centerY(*it));
            cursor += iw + colGap;
        }

        float redge = cx + availW;
        for (auto rit = rightZone_.rbegin(); rit != rightZone_.rend(); ++rit) {
            Widget* it = *rit;
            if (!inItems(it)) continue;
            const Margin& mg = it->margin();
            const float w = effectiveWidth(it);
            const float h = effectiveHeight(it);
            redge -= w + mg.right;
            if (w != it->width())  LayoutAccess::SetActual(*it, w, it->height());
            if (h != it->height()) LayoutAccess::SetActual(*it, it->width(), h);
            it->SetPosition(redge, centerY(*it));
            redge -= mg.left + colGap;
        }

        contentW_ = std::max(cursor - cx, availW);
        contentH_ = std::max(H - padding_.top - padding_.bottom, 0.0f);
    }

    float MenuBar::titleTextWidth(const MenuEntry& m) const {
        if (Window* w = window()) {
            const TextMetrics tm = w->MeasureText(m.title, titleStyle_.font);
            if (tm.width > 0.0f) return tm.width;
        }
        const float px = static_cast<float>(titleStyle_.font.pixelSize > 0
            ? titleStyle_.font.pixelSize : 14);
        return static_cast<float>(m.title.size()) * px * 0.75f;
    }

    int MenuBar::titleAtPoint(float px, float py) const {
        if (width() <= 0.0f || height() <= 0.0f) return -1;
        if (py < y() || py > y() + height()) return -1;
        for (std::size_t i = 0; i < menus_.size(); ++i) {
            const MenuEntry& m = menus_[i];
            if (m.w <= 0.0f) continue;
            if (px >= m.x && px <= m.x + m.w) return static_cast<int>(i);
        }
        return -1;
    }

    bool MenuBar::OnMousePress(MouseEvent& e) {
        if (e.button != MouseButton::Left) return false;

        if (openIndex_ >= 0 && menu_ && !menu_->isOpen()) openIndex_ = -1;

        const int idx = titleAtPoint(e.x, e.y);
        if (idx >= 0) {
            if (openIndex_ == idx) CloseMenu();
            else OpenDropdown(idx);
            return true;
        }

        if (dragEnabled_) {
            if (Window* w = window()) {
                w->StartWindowDrag();
                return true;
            }
        }
        return false;
    }

    bool MenuBar::OnMouseMove(MouseEvent& e) {
        const int idx = titleAtPoint(e.x, e.y);
        if (idx != hoverIndex_) {
            hoverIndex_ = idx;
            requestRepaint();
        }
        return false;
    }

    void MenuBar::OnMouseLeave() {
        if (hoverIndex_ < 0) return;
        hoverIndex_ = -1;
        requestRepaint();
    }

    void MenuBar::OpenDropdown(int index) {
        if (!menu_) return;
        if (index < 0 || index >= static_cast<int>(menus_.size())) return;
        if (!menus_[static_cast<std::size_t>(index)].enabled) return;

        if (menu_->isOpen()) {
            menu_->Close();
            openIndex_ = -1;
        }
        menu_->SetItems(menus_[static_cast<std::size_t>(index)].items);
        const float bottom = (height() > 0.0f) ? y() + height() : resolvedBarHeight();
        menu_->OpenAt(menus_[static_cast<std::size_t>(index)].x, bottom);
        openIndex_ = menu_->isOpen() ? index : -1;
        requestRepaint();
    }

    void MenuBar::menuClosedByOutsidePress(float mx, float my) {
        const int wasOpen = openIndex_;
        openIndex_ = -1;

        const int idx = titleAtPoint(mx, my);
        if (idx >= 0 && idx != wasOpen) OpenDropdown(idx);
        requestRepaint();
    }

    bool MenuBar::BarMenu::OnMousePress(MouseEvent& e) {
        const bool wasOpen = isOpen();
        const bool consumed = MenuModal::OnMousePress(e);
        if (wasOpen && !isOpen()) {
            for (Widget* p = parent(); p; p = p->parent()) {
                if (auto* bar = dynamic_cast<MenuBar*>(p)) {
                    bar->menuClosedByOutsidePress(e.x, e.y);
                    break;
                }
            }
        }
        return consumed;
    }

    void MenuBar::Paint(FrameCollector& fc) const {
        const ColorSet cs = barBox_.colors.get(WidgetState::Normal);

        if (cs.background.a > 0.0f && width() > 0.0f && height() > 0.0f)
            drawutil::PaintRoundedRect(fc, x(), y(), width(), height(), 0.0f,
                cs.background);

        const float bw = barBox_.border.width[Border::Bottom];
        if (bw > 0.0f && cs.border.a > 0.0f && width() > 0.0f && height() > 0.0f)
            drawutil::PaintRoundedRect(fc, x(), y() + height() - bw, width(), bw,
                0.0f, cs.border);

        if (width() <= 0.0f || height() <= 0.0f || menus_.empty()) return;

        const Color4f tcol = titleStyle_.colors.get(WidgetState::Normal).text;
        const Color4f tdis = titleStyle_.colors.get(WidgetState::Disabled).text;

        const float contentTopY = contentTop();
        float contentH = contentAvailableH();
        if (contentH <= 0.0f) contentH = resolvedBarHeight();

        for (std::size_t i = 0; i < menus_.size(); ++i) {
            const MenuEntry& m = menus_[i];
            if (m.w <= 0.0f) continue;

            const bool open = (openIndex_ == static_cast<int>(i)) && menu_
                && menu_->isOpen();
            const bool hov = (hoverIndex_ == static_cast<int>(i));
            if ((open || hov) && m.enabled) {
                drawutil::PaintRoundedRect(fc, m.x, contentTopY, m.w, contentH,
                    titleRadius_, titleHover_);
            }

            TextMetrics tm = fc.MeasureText(m.title, titleStyle_.font);
            if (tm.width <= 0.0f) continue;
            const float tx = m.x + titlePadX_;
            const float ty = contentTopY + (contentH - tm.height) * 0.5f;
            fc.AddText(m.title, titleStyle_.font, tx, ty + tm.ascent,
                m.enabled ? tcol : tdis);
        }
    }

}
