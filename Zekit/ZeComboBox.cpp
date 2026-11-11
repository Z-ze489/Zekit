
#include "pch.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "include/ZeComboBox.h"
#include "ZeDrawUtil.h"
#include "include/Zekit.h"
#include "include/ZeOverlayWindow.h"

#include "ZeFrameCollector.h"
#include "ZeAnchorVisible.h"
#include "ZePolygonShape.h"
#include "ZeRoundedRectShape.h"

namespace Kit {

    namespace {
        constexpr float kDefaultItemHeight = 28.0f;
        constexpr float kArrowFallback     = 10.0f;
        constexpr float kFallbackW         = 180.0f;
        constexpr float kFallbackH         = 32.0f;
        constexpr float kPopupMargin       = 4.0f;
        constexpr float kItemTextPad       = 8.0f;
        constexpr float kItemRadius        = 4.0f;
        constexpr float kShadowPad         = 8.0f;

        float BorderOf(const BoxStyle& box) {
            return std::max({ box.border.width[0], box.border.width[1],
                              box.border.width[2], box.border.width[3] });
        }
    }

    ComboBox::ComboBox(std::string placeholder) {
        ApplyTheme(Themes::Light());

        placeholder_ = std::move(placeholder);

        SetFocusable(true);
        SetCursor(Cursor::Pointer);
        SetDefaultSize(kFallbackW, kFallbackH);
    }

    ComboBox::~ComboBox() = default;
    ComboBox::ComboBox(ComboBox&&) noexcept = default;

    void ComboBox::SetItems(std::vector<std::string> items) {
        const std::string prev = selectedText();

        items_ = std::move(items);

        selected_ = -1;
        if (!prev.empty()) {
            for (std::size_t i = 0; i < items_.size(); ++i) {
                if (items_[i] == prev) { selected_ = static_cast<int>(i); break; }
            }
        }

        highlight_ = -1;
        hoverRow_ = -1;
        scroll_ = 0;
        if (items_.empty()) open_ = false;

        SetText(selectedText());
        notifyLayoutInputChanged();
        requestRepaint();
        RequestWindowRedraw();
    }

    void ComboBox::AddItem(std::string text) {
        items_.push_back(std::move(text));
        notifyLayoutInputChanged();
        requestRepaint();
    }

    void ComboBox::ClearItems() {
        items_.clear();
        selected_ = -1;
        highlight_ = -1;
        hoverRow_ = -1;
        scroll_ = 0;
        open_ = false;

        SetText(std::string());
        notifyLayoutInputChanged();
        requestRepaint();
    }

    const std::string& ComboBox::itemAt(std::size_t index) const {
        static const std::string kEmpty;
        return index < items_.size() ? items_[index] : kEmpty;
    }

    void ComboBox::SetSelectedIndex(int index) {
        if (index < 0 || static_cast<std::size_t>(index) >= items_.size())
            index = -1;

        if (selected_ == index) return;

        selected_ = index;
        if (highlight_ >= 0) highlight_ = index;

        SetText(selectedText());
        requestRepaint();
        RequestWindowRedraw();
    }

    std::string ComboBox::selectedText() const {
        return hasSelection() ? items_[static_cast<std::size_t>(selected_)]
                              : std::string();
    }

    void ComboBox::SetPlaceholder(std::string text) {
        placeholder_ = std::move(text);
        notifyLayoutInputChanged();
        requestRepaint();
        RequestWindowRedraw();
    }

    bool ComboBox::SetOpen(bool open) {
        if (open && items_.empty()) open = false;
        if (open_ == open) return false;

        open_ = open;

        if (open_) {
            highlight_ = hasSelection() ? selected_ : 0;
            hoverRow_ = -1;
            scroll_ = 0;
            ensureHighlightVisible();
            showPopupOverlay();
        }
        else {
            highlight_ = -1;
            hoverRow_ = -1;
            pressedInPopup_ = false;
            hidePopupOverlay();
        }

        requestRepaint();
        return true;
    }

    void ComboBox::Open()   { SetOpen(true); }
    void ComboBox::Close()  { SetOpen(false); }

    bool ComboBox::popupRect(float& x, float& y, float& w, float& h) const {
        const PopupGeom g = computePopup();
        if (g.w <= 0.0f || g.h <= 0.0f || g.visible <= 0) return false;
        x = g.x; y = g.y; w = g.w; h = g.h;
        return true;
    }

    void ComboBox::dismissPopup() {
        Close();
    }
    void ComboBox::Toggle() { SetOpen(!open_); }

    void ComboBox::SetOnSelectionChanged(SelectionChangedAction handler) {
        onChanged_ = std::move(handler);
    }

    void ComboBox::SetOnSelectionChanged(SelfAction handler) {
        onChangedSelf_ = std::move(handler);
    }

    void ComboBox::notifySelectionChanged(int index) {
        if (onChanged_)     onChanged_(*this, index);
        if (onChangedSelf_) onChangedSelf_(*this);
    }

    void ComboBox::commitSelection(int index) {
        if (index < 0 || static_cast<std::size_t>(index) >= items_.size()) return;

        const bool changed = (selected_ != index);
        selected_ = index;
        SetText(selectedText());

        if (changed) notifySelectionChanged(index);
    }

    void ComboBox::SetMaxVisibleItems(int n) { maxVisibleItems_ = n; requestRepaint();   RequestWindowRedraw();}

    void ComboBox::SetItemHeight(float h) {
        itemHeightOverride_ = (h > 0.0f) ? h : 0.0f;
        requestRepaint();
        RequestWindowRedraw();
    }

    void ComboBox::SetArrowSize(float size) {
        arrowSize_ = (size > 0.0f) ? size : 0.0f;
        notifyLayoutInputChanged();
        requestRepaint();
        RequestWindowRedraw();
    }

    void ComboBox::SetGap(float gap) {
        gap_ = (gap < 0.0f) ? 0.0f : gap;
        notifyLayoutInputChanged();
        requestRepaint();
        RequestWindowRedraw();
    }

    void ComboBox::SetArrowColor(const Color4f& c) { arrowColor_ = c; markStyled(); requestRepaint();   RequestWindowRedraw();}
    void ComboBox::SetItemHoverColor(const Color4f& c) { itemHoverColor_ = c; markStyled(); requestRepaint();   RequestWindowRedraw();}
    void ComboBox::SetItemSelectedColor(const Color4f& c) { itemSelectedColor_ = c; markStyled(); requestRepaint();   RequestWindowRedraw();}

    float ComboBox::resolvedItemHeight() const {
        if (itemHeightOverride_ > 0.0f) return itemHeightOverride_;
        return themeItemHeight_ > 0.0f ? themeItemHeight_ : kDefaultItemHeight;
    }

    float ComboBox::resolvedArrowSize() const {
        if (arrowSize_ > 0.0f) return arrowSize_;

        if (const Window* w = window()) {
            const TextMetrics tm =
                const_cast<Window*>(w)->MeasureText(std::string_view("Ag"), textStyle_.font);
            if (tm.height > 0.0f) return tm.height * 0.5f;
        }
        return kArrowFallback;
    }

    float ComboBox::hintWidth() const {
        if (prefWidth() > 0.0f) return prefWidth();

        float dx = 0.0f, dy = 0.0f;
        drawutil::BoxInsets(box_, dx, dy);
        const float arrow = resolvedArrowSize();

        float textW = 0.0f;
        if (const Window* w = window()) {
            auto* mw = const_cast<Window*>(w);
            textW = mw->MeasureText(placeholder_, textStyle_.font).width;
            for (const std::string& it : items_)
                textW = std::max(textW, mw->MeasureText(it, textStyle_.font).width);
        }

        if (textW <= 0.0f) {
            const float fb = defaultWidth();
            return fb > 0.0f ? fb : (arrow + gap_ + dx);
        }
        return textW + gap_ + arrow + dx;
    }

    float ComboBox::hintHeight() const {
        if (prefHeight() > 0.0f) return prefHeight();

        float dx = 0.0f, dy = 0.0f;
        drawutil::BoxInsets(box_, dx, dy);

        float textH = 0.0f;
        if (const Window* w = window()) {
            const TextMetrics tm =
                const_cast<Window*>(w)->MeasureText(std::string_view("Ag"), textStyle_.font);
            textH = tm.height;
        }

        if (textH <= 0.0f) {
            const float fb = defaultHeight();
            return fb > 0.0f ? fb : (kArrowFallback + dy);
        }
        return std::max(textH, resolvedArrowSize()) + dy;
    }

    ComboBox::PopupGeom ComboBox::computePopup() const {
        PopupGeom g;

        const int n = static_cast<int>(items_.size());
        if (n <= 0) return g;

        g.itemH = resolvedItemHeight();

        int vis = (maxVisibleItems_ <= 0) ? n : std::min(n, maxVisibleItems_);
        if (vis < 1) vis = 1;
        g.visible = vis;

        const float bt = BorderOf(popupBox_);
        g.w = width();
        g.h = g.itemH * static_cast<float>(vis)
            + popupBox_.padding.top + popupBox_.padding.bottom + bt * 2.0f;
        g.x = x();
        g.y = y() + height();
        g.listTop = g.y + popupBox_.padding.top + bt;

        if (const Window* w = window()) {
            const float winH = static_cast<float>(const_cast<Window*>(w)->height());
            if (winH > 0.0f) {
                if (g.y + g.h > winH - kPopupMargin && y() - g.h >= kPopupMargin) {
                    g.y = y() - g.h;
                    g.flipped = true;
                }
                else if (g.y + g.h > winH - kPopupMargin) {
                    g.y = winH - kPopupMargin - g.h;
                }
                if (g.y < kPopupMargin) g.y = kPopupMargin;
                g.listTop = g.y + popupBox_.padding.top + bt;
            }
        }
        return g;
    }

    int ComboBox::itemAtPoint(float px, float py) const {
        if (!popupActive()) return -1;

        const PopupGeom g = computePopup();
        if (g.w <= 0.0f || g.h <= 0.0f) return -1;

        if (px < g.x || px >= g.x + g.w) return -1;
        if (py < g.y || py >= g.y + g.h) return -1;

        const float local = py - g.listTop;
        if (local < 0.0f) return -1;

        const int row = static_cast<int>(local / g.itemH);
        if (row < 0 || row >= g.visible) return -1;

        const int idx = scroll_ + row;
        return (idx >= 0 && idx < static_cast<int>(items_.size())) ? idx : -1;
    }

    void ComboBox::clampScroll() {
        const int n = static_cast<int>(items_.size());
        if (n <= 0) { scroll_ = 0; return; }

        int vis = (maxVisibleItems_ <= 0) ? n : std::min(n, maxVisibleItems_);
        if (vis < 1) vis = 1;

        const int maxScroll = std::max(0, n - vis);
        scroll_ = std::min(std::max(scroll_, 0), maxScroll);
    }

    void ComboBox::ensureHighlightVisible() {
        const int n = static_cast<int>(items_.size());
        if (n <= 0 || highlight_ < 0) return;

        int vis = (maxVisibleItems_ <= 0) ? n : std::min(n, maxVisibleItems_);
        if (vis < 1) vis = 1;

        if (highlight_ < scroll_)                  scroll_ = highlight_;
        else if (highlight_ >= scroll_ + vis)      scroll_ = highlight_ - vis + 1;

        clampScroll();
    }

    void ComboBox::requestRepaint() {
        RequestWindowRedraw();
    }

    bool ComboBox::popupActive() const {
        return open_ && !items_.empty() && popupAnchorVisible();
    }

    bool ComboBox::popupAnchorVisible() const {
        return detail::AnchorVisibleInAncestors(*this);
    }

    bool ComboBox::paintsOnTop() const {
        return popupActive() && !popupOverlayVisible();
    }

    bool ComboBox::HitTest(float px, float py) const {
        if (Widget::HitTest(px, py)) return true;

        if (!paintsOnTop()) return false;

        const PopupGeom g = computePopup();
        if (g.w <= 0.0f || g.h <= 0.0f) return false;
        return px >= g.x && px < g.x + g.w && py >= g.y && py < g.y + g.h;
    }

    void ComboBox::ApplyTheme(const Theme& t) {
        TextWidget::ApplyTheme(t);

        popupBox_ = t.combo.popup;
        itemTextStyle_ = t.combo.item;
        themeItemHeight_ = t.combo.itemHeight;

        arrowColor_ = t.combo.arrowColor;
        itemHoverColor_ = t.combo.itemHoverColor;
        itemSelectedColor_ = t.combo.itemSelectedColor;

        if (OverlayWindow* o = popupOverlay()) o->SetTheme(t);
    }

    const BoxStyle& ComboBox::themeBox(const Theme& t) const {
        return t.combo.box;
    }

    const TextStyle& ComboBox::themeText(const Theme& t) const {
        return t.combo.text;
    }

    bool ComboBox::OnMousePress(MouseEvent& e) {
        if (!isEnabled()) { pressedInFace_ = pressedInPopup_ = false; return false; }
        if (e.button != MouseButton::Left) return false;

        if (open_ && onPopupPress(e)) return true;

        if (!Widget::HitTest(e.x, e.y)) return false;
        pressedInFace_ = true;
        e.accept();
        return true;
    }

    bool ComboBox::onPopupPress(MouseEvent& e) {
        const PopupGeom g = computePopup();
        const bool inPopup = (g.w > 0.0f &&
            e.x >= g.x && e.x < g.x + g.w && e.y >= g.y && e.y < g.y + g.h);
        if (!inPopup) return false;

        pressedInPopup_ = true;
        const int row = itemAtPoint(e.x, e.y);
        if (row != hoverRow_) hoverRow_ = row;
        requestRepaint();
        return true;
    }

    bool ComboBox::OnMouseRelease(MouseEvent& e) {
        if (e.button != MouseButton::Left) return false;

        if (pressedInPopup_ && onPopupRelease(e)) { e.accept(); return true; }

        if (pressedInFace_) {
            pressedInFace_ = false;
            if (!Widget::HitTest(e.x, e.y)) { e.accept(); return true; }
            Toggle();
            e.accept();
            return true;
        }
        return false;
    }

    bool ComboBox::onPopupRelease(MouseEvent& e) {
        if (!pressedInPopup_) return false;
        pressedInPopup_ = false;

        const int idx = itemAtPoint(e.x, e.y);
        if (idx >= 0) {
            commitSelection(idx);
            SetOpen(false);
        }
        requestRepaint();
        return true;
    }

    bool ComboBox::OnMouseMove(MouseEvent& e) {
        return onPopupMove(e);
    }

    bool ComboBox::onPopupMove(MouseEvent& e) {
        if (!open_) return false;

        const int row = itemAtPoint(e.x, e.y);
        if (row == hoverRow_) return false;

        hoverRow_ = row;
        requestRepaint();
        return true;
    }

    bool ComboBox::OnMouseWheel(MouseEvent& e) {
        if (popupOverlayVisible()) return false;
        return onPopupWheel(e);
    }

    bool ComboBox::onPopupWheel(MouseEvent& e) {
        if (!open_) return false;

        const int n = static_cast<int>(items_.size());
        int vis = (maxVisibleItems_ <= 0) ? n : std::min(n, maxVisibleItems_);
        if (vis < 1) vis = 1;
        if (n <= vis) return false;

        scroll_ += (e.wheelDelta > 0) ? -1 : 1;
        clampScroll();
        hoverRow_ = -1;
        requestRepaint();
        e.accept();
        return true;
    }

    void ComboBox::OnMouseLeave() {
        BoxWidget::OnMouseLeave();
        onPopupLeave();
    }

    void ComboBox::onPopupLeave() {
        if (hoverRow_ != -1) {
            hoverRow_ = -1;
            requestRepaint();
        }
    }

    bool ComboBox::OnKeyPress(KeyEvent& e) {
        if (!isEnabled()) return false;

        if (!open_) {
            switch (e.key) {
            case Key::Space:
            case Key::Enter:
            case Key::DownArrow:
            case Key::UpArrow:
                if (items_.empty()) return false;
                Open();
                e.accept();
                return true;
            default:
                return false;
            }
        }

        const int n = static_cast<int>(items_.size());

        switch (e.key) {
        case Key::Escape:
            Close();
            e.accept();
            return true;

        case Key::Enter:
        case Key::Space:
            if (highlight_ >= 0 && highlight_ < n) {
                commitSelection(highlight_);
                Close();
            }
            e.accept();
            return true;

        case Key::DownArrow:
        case Key::UpArrow: {
            if (n <= 0) { e.accept(); return true; }

            int h = (highlight_ >= 0) ? highlight_ : (hasSelection() ? selected_ : 0);
            h += (e.key == Key::DownArrow) ? 1 : -1;
            if (h < 0) h = n - 1;
            if (h >= n) h = 0;

            highlight_ = h;
            ensureHighlightVisible();
            requestRepaint();
            e.accept();
            return true;
        }

        case Key::Home:
        case Key::End:
            if (n <= 0) { e.accept(); return true; }
            highlight_ = (e.key == Key::Home) ? 0 : (n - 1);
            ensureHighlightVisible();
            requestRepaint();
            e.accept();
            return true;

        default:
            return false;
        }
    }

    void ComboBox::OnFocusOut() {
        if (open_) Close();
    }

    void ComboBox::paintContent(FrameCollector& fc) const {
        float cx = 0.0f, cy = 0.0f, cw = 0.0f, ch = 0.0f;
        contentRect(cx, cy, cw, ch);

        if (cw > 0.0f && ch > 0.0f) {
            const float arrow = resolvedArrowSize();
            const float textAvail = std::max(0.0f, cw - arrow - gap_);
            const bool hasSel = hasSelection();
            const std::string& shown =
                hasSel ? items_[static_cast<std::size_t>(selected_)] : placeholder_;

            if (drawsOwnFaceText() && !shown.empty() && textAvail > 0.0f) {
                std::string line = shown;
                TextMetrics tm = fc.MeasureText(line, textStyle_.font);
                if (tm.width > textAvail) {
                    line = ellipsizeLine(fc, shown, textStyle_.font, textAvail);
                    tm = fc.MeasureText(line, textStyle_.font);
                }

                if (tm.width > 0.0f) {
                    ColorSet cs = resolvedTextColors(currentState());
                    if (!hasSel) {
                        cs.text = textStyle_.colors.get(WidgetState::Disabled).text;
                    }
                    const float ty = cy + (ch - tm.height) * 0.5f;
                    paintTextAt(fc, line, cx, ty, tm, cs);
                }
            }

            const float axc = cx + cw - arrow * 0.5f;
            const float ayc = cy + ch * 0.5f;
            const float hw = arrow * 0.34f;
            const float dir = open_ ? -1.0f : 1.0f;
            const float tipOff = arrow * 0.30f * dir;
            const float baseOff = -arrow * 0.18f * dir;

            std::vector<Vec2> tri{
                Vec2{ axc - hw, ayc + baseOff },
                Vec2{ axc + hw, ayc + baseOff },
                Vec2{ axc,      ayc + tipOff  },
            };
            fc.AddShape(PolygonShape(std::move(tri), true), arrowColor_);
        }

        if (!popupActive() || popupOverlayVisible()) return;
        paintPopupInto(fc, false);
    }

    void ComboBox::paintPopupInto(FrameCollector& fc, bool localPopup) const {
        const PopupGeom g0 = computePopup();
        if (g0.w <= 0.0f || g0.h <= 0.0f || g0.visible <= 0) return;

        const float baseX = localPopup ? g0.x : 0.0f;
        const float baseY = localPopup ? g0.y : 0.0f;
        const float gx = g0.x - baseX;
        const float gy = g0.y - baseY;
        const float listTop = g0.listTop - baseY;

        fc.ResetClip();

        const float radius = resolveRadius(popupBox_, fc.GetTheme());

        fc.PushClip(gx - kShadowPad, gy - kShadowPad,
                    g0.w + 2.0f * kShadowPad, g0.h + 2.0f * kShadowPad);
        drawutil::PaintBoxShadow(fc, popupBox_, gx, gy, g0.w, g0.h, radius);
        fc.PopClip();

        fc.PushClip(gx, gy, g0.w, g0.h);

        const float bt = BorderOf(popupBox_);
        const ColorSet pcs = popupBox_.colors.get(WidgetState::Normal);
        if (bt > 0.0f) {
            drawutil::PaintRoundedRect(fc, gx, gy, g0.w, g0.h, radius, pcs.border);
            drawutil::PaintRoundedRect(fc, gx + bt, gy + bt,
                g0.w - 2.0f * bt, g0.h - 2.0f * bt,
                std::max(0.0f, radius - bt), pcs.background);
        }
        else {
            drawutil::PaintRoundedRect(fc, gx, gy, g0.w, g0.h, radius, pcs.background);
        }

        const float ix = gx + bt + popupBox_.padding.left;
        const float iw = g0.w - 2.0f * bt - popupBox_.padding.left - popupBox_.padding.right;
        const int pick = (hoverRow_ >= 0) ? hoverRow_ : highlight_;
        const Color4f itemTextColor = itemTextStyle_.colors.get(WidgetState::Normal).text;

        for (int row = 0; row < g0.visible; ++row) {
            const int idx = scroll_ + row;
            if (idx < 0 || idx >= static_cast<int>(items_.size())) break;

            const float iy = listTop + g0.itemH * static_cast<float>(row);

            Color4f bg{ 0.0f, 0.0f, 0.0f, 0.0f };
            if (idx == selected_)      bg = itemSelectedColor_;
            else if (idx == pick)      bg = itemHoverColor_;

            if (bg.a > 0.0f && iw > 0.0f) {
                drawutil::PaintRoundedRect(fc, ix, iy, iw, g0.itemH,
                    std::min(kItemRadius, g0.itemH * 0.25f), bg);
            }

            const std::string& text = items_[static_cast<std::size_t>(idx)];
            if (text.empty() || iw <= 0.0f) continue;

            const float avail = std::max(0.0f, iw - kItemTextPad * 2.0f);
            if (avail <= 0.0f) continue;

            std::string line = text;
            TextMetrics tm = fc.MeasureText(line, itemTextStyle_.font);
            if (tm.width > avail) {
                line = ellipsizeLine(fc, text, itemTextStyle_.font, avail);
                tm = fc.MeasureText(line, itemTextStyle_.font);
            }
            if (tm.width <= 0.0f) continue;

            const float ty = iy + (g0.itemH - tm.height) * 0.5f;
            fc.AddText(line, itemTextStyle_.font, ix + kItemTextPad, ty + tm.ascent, itemTextColor);
        }

        fc.PopClip();
    }
}
