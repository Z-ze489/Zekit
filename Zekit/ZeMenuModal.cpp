
#include "pch.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "include/ZeMenuModal.h"
#include "ZeDrawUtil.h"
#include "include/Zekit.h"
#include "include/ZeOverlayWindow.h"

#include "ZeFrameCollector.h"
#include "ZeRoundedRectShape.h"

namespace Kit {

    namespace {
        constexpr float kMinMenuWidth  = 160.0f;
        constexpr float kFallbackRowH  = 30.0f;
        constexpr float kSepSlotH      = 9.0f;
        constexpr float kSepLineH      = 1.0f;
        constexpr float kSepInset      = 6.0f;
        constexpr float kMenuMargin    = 4.0f;
        constexpr float kItemTextPad   = 10.0f;
        constexpr float kShortcutGap   = 24.0f;
        constexpr float kItemRadius    = 4.0f;
        constexpr float kShadowPad     = 8.0f;

        float BorderOf(const BoxStyle& box) {
            return std::max({ box.border.width[0], box.border.width[1],
                              box.border.width[2], box.border.width[3] });
        }
    }

    MenuModal::MenuModal() {
        ApplyTheme(Themes::Light());

        SetFocusable(true);
        SetCursor(Cursor::Pointer);
    }

    MenuModal::~MenuModal() = default;
    MenuModal::MenuModal(MenuModal&&) noexcept = default;

    void MenuModal::SetItems(std::vector<MenuItem> items) {
        items_ = std::move(items);

        highlight_ = -1;
        hoverRow_ = -1;
        pressedItem_ = -1;
        if (items_.empty()) open_ = false;

        requestRepaint();
        RequestWindowRedraw();
    }

    int MenuModal::AddItem(std::string text, std::string shortcut,
        MenuAction action) {
        MenuItem it;
        it.text = std::move(text);
        it.shortcut = std::move(shortcut);
        it.action = std::move(action);
        items_.push_back(std::move(it));
        requestRepaint();
        return static_cast<int>(items_.size()) - 1;
    }

    int MenuModal::AddSeparator() {
        MenuItem it;
        it.separator = true;
        items_.push_back(std::move(it));
        requestRepaint();
        return static_cast<int>(items_.size()) - 1;
    }

    void MenuModal::Clear() {
        items_.clear();
        highlight_ = -1;
        hoverRow_ = -1;
        pressedItem_ = -1;
        open_ = false;
        requestRepaint();
    }

    const MenuItem& MenuModal::itemAt(std::size_t index) const {
        static const MenuItem kEmpty;
        return index < items_.size() ? items_[index] : kEmpty;
    }

    void MenuModal::SetItemEnabled(std::size_t index, bool enabled) {
        if (index >= items_.size() || items_[index].separator) return;
        items_[index].enabled = enabled;

        if (!enabled) {
            if (highlight_ == static_cast<int>(index)) highlight_ = -1;
            if (hoverRow_ == static_cast<int>(index)) hoverRow_ = -1;
        }
        requestRepaint();
        RequestWindowRedraw();
    }

    bool MenuModal::hasActivableItem() const {
        for (const MenuItem& it : items_)
            if (!it.separator) return true;
        return false;
    }

    void MenuModal::OpenAt(float x, float y) {
        if (open_ && anchorX_ == x && anchorY_ == y) return;
        if (!hasActivableItem()) return;

        open_ = true;
        anchorX_ = x;
        anchorY_ = y;
        highlight_ = -1;
        hoverRow_ = -1;
        pressedItem_ = -1;

        requestFocus();

        showNativePopup();

        requestRepaint();
    }

    void MenuModal::OpenAtCursor() {
        Window* w = window();
        if (!w) return;

        int mx = -1, my = -1;
        w->GetMousePosition(mx, my);
        if (mx < 0 || my < 0) return;

        OpenAt(static_cast<float>(mx), static_cast<float>(my));
    }

    void MenuModal::Close() {
        if (!open_) return;

        open_ = false;
        highlight_ = -1;
        hoverRow_ = -1;
        pressedItem_ = -1;

        hideNativePopup();

        clearFocus();

        requestRepaint();

        if (onClosed_) onClosed_(*this);
    }

    void MenuModal::SetOnItemSelected(ItemSelectedAction handler) {
        onSelected_ = std::move(handler);
    }

    void MenuModal::SetOnItemSelected(SelfAction handler) {
        onSelectedSelf_ = std::move(handler);
    }

    void MenuModal::SetOnClosed(SelfAction handler) {
        onClosed_ = std::move(handler);
    }

    void MenuModal::notifyItemSelected(int index) {
        if (onSelected_)      onSelected_(*this, index);
        if (onSelectedSelf_)  onSelectedSelf_(*this);
    }

    void MenuModal::activateItem(int index) {
        if (index < 0 || static_cast<std::size_t>(index) >= items_.size()) return;
        MenuItem& it = items_[static_cast<std::size_t>(index)];
        if (it.separator || !it.enabled) return;

        MenuAction action = it.action;

        Close();
        action.invoke(*this);
        notifyItemSelected(index);
    }

    void MenuModal::SetScrimEnabled(bool on) { scrimEnabled_ = on; requestRepaint();   RequestWindowRedraw();}
    void MenuModal::SetMenuWidth(float w)    { menuWidthOverride_ = (w > 0.0f) ? w : 0.0f; requestRepaint();   RequestWindowRedraw();}

    void MenuModal::SetItemHeight(float h) {
        itemHeightOverride_ = (h > 0.0f) ? h : 0.0f;
        requestRepaint();
        RequestWindowRedraw();
    }

    void MenuModal::SetItemHoverColor(const Color4f& c)  { itemHoverColor_ = c; requestRepaint();   RequestWindowRedraw();}
    void MenuModal::SetSeparatorColor(const Color4f& c)  { separatorColor_ = c; requestRepaint();   RequestWindowRedraw();}
    void MenuModal::SetScrimColor(const Color4f& c)      { scrimColor_ = c; requestRepaint();   RequestWindowRedraw();}

    float MenuModal::resolvedItemHeight() const {
        if (itemHeightOverride_ > 0.0f) return itemHeightOverride_;
        return themeItemHeight_ > 0.0f ? themeItemHeight_ : kFallbackRowH;
    }

    float MenuModal::resolvedMenuWidth() const {
        if (menuWidthOverride_ > 0.0f) return menuWidthOverride_;

        const float bt = BorderOf(panelBox_);
        float contentW = 0.0f;
        if (window()) {
            auto* mw = const_cast<Window*>(window());
            for (const MenuItem& it : items_) {
                if (it.separator) continue;
                float w = kItemTextPad * 2.0f;
                if (!it.text.empty())
                    w += mw->MeasureText(it.text, itemTextStyle_.font).width;
                if (!it.shortcut.empty())
                    w += kShortcutGap + mw->MeasureText(it.shortcut, shortcutTextStyle_.font).width;
                contentW = std::max(contentW, w);
            }
        }

        float w = contentW + panelBox_.padding.left + panelBox_.padding.right + bt * 2.0f;
        return std::max(w, kMinMenuWidth);
    }

    float MenuModal::hintWidth() const {
        if (prefWidth() > 0.0f) return prefWidth();
        return 0.0f;
    }

    float MenuModal::hintHeight() const {
        if (prefHeight() > 0.0f) return prefHeight();
        return 0.0f;
    }

    float MenuModal::rowHeight(std::size_t index) const {
        if (index >= items_.size()) return 0.0f;
        return items_[index].separator ? (kSepSlotH + kSepLineH) : resolvedItemHeight();
    }

    MenuModal::MenuGeom MenuModal::computeMenu() const {
        MenuGeom g;
        if (!open_ || items_.empty()) return g;

        g.rowH = resolvedItemHeight();
        g.w = resolvedMenuWidth();

        const float bt = BorderOf(panelBox_);
        g.h = panelBox_.padding.top + panelBox_.padding.bottom + bt * 2.0f;
        for (std::size_t i = 0; i < items_.size(); ++i) g.h += rowHeight(i);

        g.x = anchorX_;
        g.y = anchorY_;
        if (const Window* w = window()) {
            const float winW = static_cast<float>(const_cast<Window*>(w)->width());
            const float winH = static_cast<float>(const_cast<Window*>(w)->height());
            if (winW > 0.0f) {
                if (g.x + g.w > winW - kMenuMargin) g.x = winW - kMenuMargin - g.w;
                if (g.x < kMenuMargin) g.x = kMenuMargin;
            }
            if (winH > 0.0f) {
                if (g.y + g.h > winH - kMenuMargin) g.y = winH - kMenuMargin - g.h;
                if (g.y < kMenuMargin) g.y = kMenuMargin;
            }
        }

        g.listTop = g.y + panelBox_.padding.top + bt;
        g.valid = true;
        return g;
    }

    int MenuModal::itemAtPoint(float px, float py) const {
        if (!open_ || items_.empty()) return -1;

        const MenuGeom g = computeMenu();
        if (!g.valid) return -1;

        if (px < g.x || px >= g.x + g.w) return -1;
        if (py < g.y || py >= g.y + g.h) return -1;

        const float bt = BorderOf(panelBox_);
        const float ix = g.x + bt + panelBox_.padding.left;
        const float iw = g.w - 2.0f * bt - panelBox_.padding.left - panelBox_.padding.right;
        if (px < ix || px >= ix + iw) return -1;

        float y = g.listTop;
        for (std::size_t i = 0; i < items_.size(); ++i) {
            const float rh = rowHeight(i);
            if (py >= y && py < y + rh) {
                if (items_[i].separator) return -1;
                return static_cast<int>(i);
            }
            y += rh;
        }
        return -1;
    }

    int MenuModal::firstEnabled() const {
        for (std::size_t i = 0; i < items_.size(); ++i)
            if (!items_[i].separator && items_[i].enabled) return static_cast<int>(i);
        return -1;
    }

    int MenuModal::nextEnabled(int from, int dir) const {
        const int n = static_cast<int>(items_.size());
        if (n <= 0) return -1;

        int i = from;
        for (int step = 0; step < n; ++step) {
            i += dir;
            if (i < 0) i = n - 1;
            if (i >= n) i = 0;
            const MenuItem& it = items_[static_cast<std::size_t>(i)];
            if (!it.separator && it.enabled) return i;
        }
        return -1;
    }

    void MenuModal::requestRepaint() {
        RequestWindowRedraw();
    }

    bool MenuModal::nativePopup() const {
        return overlay_ && overlay_->isVisible();
    }

    void MenuModal::showNativePopup() {
        Window* w = window();
        if (!w || w->IsClosed()) return;
        if (!OverlayWindow::SupportsNative(*w)) return;

        if (!overlay_) {
            overlay_ = std::make_unique<OverlayWindow>(*w, OverlayLayer::Panel);
            overlay_->SetOnTakenOver([this] { Close(); });

            OverlayContent content;
            content.paint = [this](FrameCollector& fc) { paintPopupInto(fc, true); };
            content.onPress = [this](MouseEvent& e) { return onPopupPress(e); };
            content.onRelease = [this](MouseEvent& e) { return onPopupRelease(e); };
            content.onMove = [this](MouseEvent& e) { return onPopupMove(e); };
            content.onWheel = [this](MouseEvent& e) { return onPopupWheel(e); };
            content.onLeave = [this] { onPopupLeave(); };
            overlay_->SetContent(std::move(content));
            overlay_->SetBackground(Color4f{ 0.0f, 0.0f, 0.0f, 0.0f });
        }

        const MenuGeom g = computeMenu();
        if (!g.valid) return;
        if (!overlay_->isReady()) return;

        overlay_->Show(g.x, g.y, g.w, g.h);
    }

    void MenuModal::hideNativePopup() {
        if (overlay_) overlay_->Hide();
    }

    bool MenuModal::paintsOnTop() const {
        if (!open_) return false;
        if (nativePopup()) return scrimEnabled_;
        return !items_.empty();
    }

    bool MenuModal::HitTest(float px, float py) const {
        if (!open_) return false;

        if (const Window* w = window()) {
            return px >= 0.0f && py >= 0.0f
                && px < static_cast<float>(const_cast<Window*>(w)->width())
                && py < static_cast<float>(const_cast<Window*>(w)->height());
        }

        const MenuGeom g = computeMenu();
        if (!g.valid) return false;
        return px >= g.x && px < g.x + g.w && py >= g.y && py < g.y + g.h;
    }

    void MenuModal::ApplyTheme(const Theme& t) {
        BoxWidget::ApplyTheme(t);

        panelBox_ = t.menu.panel;

        itemTextStyle_ = t.menu.item;
        shortcutTextStyle_ = t.menu.shortcut;
        themeItemHeight_ = t.menu.itemHeight;

        itemHoverColor_ = t.menu.itemHoverColor;
        separatorColor_ = t.menu.separatorColor;
        scrimColor_ = t.menu.scrimColor;

        if (overlay_) overlay_->SetTheme(t);
    }

    const BoxStyle& MenuModal::themeBox(const Theme& t) const {
        return t.menu.panel;
    }

    bool MenuModal::OnMousePress(MouseEvent& e) {
        if (!open_) return false;

        if (onPopupPress(e)) { e.accept(); return true; }

        Close();
        e.accept();
        return true;
    }

    bool MenuModal::onPopupPress(MouseEvent& e) {
        const MenuGeom g = computeMenu();
        const bool inPanel = g.valid
            && e.x >= g.x && e.x < g.x + g.w && e.y >= g.y && e.y < g.y + g.h;
        if (!inPanel) return false;

        if (e.button == MouseButton::Left) {
            pressedItem_ = itemAtPoint(e.x, e.y);
            hoverRow_ = (pressedItem_ >= 0 && items_[static_cast<std::size_t>(pressedItem_)].enabled)
                ? pressedItem_ : -1;
            requestRepaint();
        }
        return true;
    }

    bool MenuModal::OnMouseRelease(MouseEvent& e) {
        if (e.button != MouseButton::Left) return false;

        if (pressedItem_ >= 0) {
            if (onPopupRelease(e)) { e.accept(); return true; }
        }
        return false;
    }

    bool MenuModal::onPopupRelease(MouseEvent& e) {
        if (pressedItem_ < 0) return false;

        const int idx = itemAtPoint(e.x, e.y);
        pressedItem_ = -1;
        if (idx >= 0) activateItem(idx);
        else requestRepaint();
        return true;
    }

    bool MenuModal::OnMouseMove(MouseEvent& e) {
        return onPopupMove(e);
    }

    bool MenuModal::onPopupMove(MouseEvent& e) {
        if (!open_) return false;

        const int row = itemAtPoint(e.x, e.y);
        const int pick = (row >= 0 && items_[static_cast<std::size_t>(row)].enabled) ? row : -1;
        if (pick == hoverRow_) return false;

        hoverRow_ = pick;
        requestRepaint();
        return true;
    }

    bool MenuModal::OnMouseWheel(MouseEvent& e) {
        return onPopupWheel(e);
    }

    bool MenuModal::onPopupWheel(MouseEvent& e) {
        if (!open_) return false;
        e.accept();
        return true;
    }

    void MenuModal::OnMouseLeave() {
        BoxWidget::OnMouseLeave();
        onPopupLeave();
    }

    void MenuModal::onPopupLeave() {
        if (hoverRow_ != -1) {
            hoverRow_ = -1;
            requestRepaint();
        }
    }

    bool MenuModal::OnKeyPress(KeyEvent& e) {
        if (!open_) return false;

        const int n = static_cast<int>(items_.size());

        switch (e.key) {
        case Key::Escape:
            Close();
            e.accept();
            return true;

        case Key::Enter:
        case Key::Space:
            if (highlight_ >= 0) activateItem(highlight_);
            e.accept();
            return true;

        case Key::DownArrow:
        case Key::UpArrow:
            highlight_ = nextEnabled(highlight_, (e.key == Key::DownArrow) ? 1 : -1);
            requestRepaint();
            e.accept();
            return true;

        case Key::Home:
            highlight_ = firstEnabled();
            requestRepaint();
            e.accept();
            return true;

        case Key::End:
            highlight_ = (n > 0) ? nextEnabled(n, -1) : -1;
            requestRepaint();
            e.accept();
            return true;

        default:
            return false;
        }
    }

    void MenuModal::OnFocusOut() {
        if (open_) Close();
    }

    void MenuModal::Paint(FrameCollector& fc) const {
        if (!paintsOnTop()) return;
        paintContent(fc);
    }

    void MenuModal::paintContent(FrameCollector& fc) const {
        fc.ResetClip();

        if (scrimEnabled_ && scrimColor_.a > 0.0f) {
            if (const Window* w = window()) {
                const float winW = static_cast<float>(const_cast<Window*>(w)->width());
                const float winH = static_cast<float>(const_cast<Window*>(w)->height());
                if (winW > 0.0f && winH > 0.0f)
                    drawutil::PaintRoundedRect(fc, 0.0f, 0.0f, winW, winH, 0.0f, scrimColor_);
            }
        }

        if (!nativePopup()) paintPopupInto(fc, false);
    }

    void MenuModal::paintPopupInto(FrameCollector& fc, bool localPopup) const {
        const MenuGeom g0 = computeMenu();
        if (!g0.valid) return;

        const float baseX = localPopup ? g0.x : 0.0f;
        const float baseY = localPopup ? g0.y : 0.0f;
        const float gx = g0.x - baseX;
        const float gy = g0.y - baseY;
        const float listTop = g0.listTop - baseY;

        fc.ResetClip();

        const float bt = BorderOf(panelBox_);
        const float radius = resolveRadius(panelBox_, fc.GetTheme());

        fc.PushClip(gx - kShadowPad, gy - kShadowPad,
                    g0.w + 2.0f * kShadowPad, g0.h + 2.0f * kShadowPad);
        drawutil::PaintBoxShadow(fc, panelBox_, gx, gy, g0.w, g0.h, radius);
        fc.PopClip();

        fc.PushClip(gx, gy, g0.w, g0.h);

        const ColorSet pcs = panelBox_.colors.get(WidgetState::Normal);
        if (bt > 0.0f) {
            drawutil::PaintRoundedRect(fc, gx, gy, g0.w, g0.h, radius, pcs.border);
            drawutil::PaintRoundedRect(fc, gx + bt, gy + bt,
                g0.w - 2.0f * bt, g0.h - 2.0f * bt,
                std::max(0.0f, radius - bt), pcs.background);
        }
        else {
            drawutil::PaintRoundedRect(fc, gx, gy, g0.w, g0.h, radius, pcs.background);
        }

        const float ix = gx + bt + panelBox_.padding.left;
        const float iw = std::max(0.0f,
            g0.w - 2.0f * bt - panelBox_.padding.left - panelBox_.padding.right);
        const int pick = (hoverRow_ >= 0) ? hoverRow_ : highlight_;
        const Color4f itemColor = itemTextStyle_.colors.get(WidgetState::Normal).text;
        const Color4f itemDisabled = itemTextStyle_.colors.get(WidgetState::Disabled).text;
        const Color4f shortcutColor = shortcutTextStyle_.colors.get(WidgetState::Normal).text;

        float y = listTop;
        for (std::size_t i = 0; i < items_.size(); ++i) {
            const MenuItem& it = items_[i];
            const float rh = rowHeight(i);

            if (!it.separator) {
                if (iw > 0.0f && static_cast<int>(i) == pick && it.enabled) {
                    drawutil::PaintRoundedRect(fc, ix, y, iw, rh,
                        std::min(kItemRadius, rh * 0.25f), itemHoverColor_);
                }

                if (iw > 0.0f) {
                    if (!it.text.empty()) {
                        std::string line = it.text;
                        TextMetrics tm = fc.MeasureText(line, itemTextStyle_.font);
                        const float avail = std::max(0.0f, iw - kItemTextPad * 2.0f);
                        if (tm.width > avail) {
                            line = drawutil::EllipsizeToWidth(fc, it.text, itemTextStyle_.font, avail);
                            tm = fc.MeasureText(line, itemTextStyle_.font);
                        }
                        if (tm.width > 0.0f) {
                            const float ty = y + (rh - tm.height) * 0.5f;
                            fc.AddText(line, itemTextStyle_.font,
                                ix + kItemTextPad, ty + tm.ascent,
                                it.enabled ? itemColor : itemDisabled);
                        }
                    }

                    if (!it.shortcut.empty()) {
                        TextMetrics sm = fc.MeasureText(it.shortcut, shortcutTextStyle_.font);
                        const float sx = ix + iw - kItemTextPad - sm.width;
                        if (sm.width > 0.0f && sx > ix + kItemTextPad) {
                            const float ty = y + (rh - sm.height) * 0.5f;
                            fc.AddText(it.shortcut, shortcutTextStyle_.font,
                                sx, ty + sm.ascent, shortcutColor);
                        }
                    }
                }
            }
            else {
                const float lineY = y + kSepSlotH * 0.5f + kSepLineH * 0.5f;
                drawutil::PaintRoundedRect(fc, ix + kSepInset, lineY - kSepLineH * 0.5f,
                    std::max(0.0f, iw - kSepInset * 2.0f), kSepLineH,
                    0.0f, separatorColor_);
            }

            y += rh;
        }

        fc.PopClip();
    }
}
