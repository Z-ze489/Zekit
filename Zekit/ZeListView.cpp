
#include "pch.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "include/ZeListView.h"
#include "ZeDrawUtil.h"
#include "include/ZeTextWidget.h"
#include "include/Zekit.h"

#include "ZeFrameCollector.h"
#include "ZeRoundedRectShape.h"

namespace Kit {

    namespace {
        constexpr float kRowPadX = 8.0f;

        class ListViewRow : public TextWidget {
        public:
            explicit ListViewRow(std::string text) : TextWidget(std::move(text)) {
                SetPadding(kRowPadX, 0.0f);

                ApplyTheme(Themes::Light());
            }

            void SetSelected(bool on) {
                if (selected_ == on) return;
                selected_ = on;
                markStyled();
                RequestWindowRedraw();
            }

            void SetRowHeight(float h) {
                if (rowH_ == h) return;
                rowH_ = h;
                markStyled();
                RequestWindowRedraw();
            }

            void SetRowColors(const Color4f& hover, const Color4f& sel) {
                hoverColor_ = hover;
                selColor_ = sel;
                markStyled();
                RequestWindowRedraw();
            }

            float hintWidth() const override {
                if (prefWidth() > 0.0f) return prefWidth();
                float w = 0.0f;
                if (Window* win = window())
                    w = win->MeasureText(Text(), textStyle_.font).width;
                return w + kRowPadX * 2.0f;
            }

            float hintHeight() const override {
                if (prefHeight() > 0.0f) return prefHeight();
                return rowH_;
            }

        protected:
            const BoxStyle& themeBox(const Theme&) const override {
                static const BoxStyle kEmpty{};
                return kEmpty;
            }
            const TextStyle& themeText(const Theme& t) const override {
                return t.list.item;
            }

            void paintContent(FrameCollector& fc) const override {
                const float w = width();
                const float h = height();
                if (w <= 0.0f || h <= 0.0f) return;

                const Color4f bg = selected_ ? selColor_
                                 : (isHovered() ? hoverColor_ : Color4f{ 0, 0, 0, 0 });
                if (bg.a > 0.0f)
                    drawutil::PaintRoundedRect(fc, x(), y(), w, h, 0.0f, bg);

                TextWidget::paintContent(fc);
            }

        private:
            bool    selected_ = false;
            float   rowH_ = 28.0f;
            Color4f hoverColor_{ 0, 0, 0, 0 };
            Color4f selColor_{ 0, 0, 0, 0 };
        };
    }

    ListView::ListView() {
        SetScrollDirection(ScrollDirection::Vertical);
        SetFocusable(true);
        SetAlign(AlignItems::Stretch);
        ApplyTheme(Themes::Light());
    }

    void ListView::SetItems(std::vector<std::string> items) {
        items_ = std::move(items);
        rebuildRows();
        RequestWindowRedraw();
    }

    int ListView::AddItem(std::string text) {
        items_.push_back(std::move(text));
        rebuildRows();
        return static_cast<int>(items_.size()) - 1;
    }

    void ListView::Clear() {
        items_.clear();
        selected_ = -1;
        rebuildRows();
    }

    const std::string& ListView::itemAt(std::size_t index) const {
        static const std::string kEmpty;
        return index < items_.size() ? items_[index] : kEmpty;
    }

    void ListView::rebuildRows() {
        ClearChildren();
        rows_.clear();
        pressedRow_ = -1;

        for (const std::string& s : items_) {
            ListViewRow row(s);
            row.SetRowHeight(itemHeight_);
            row.SetRowColors(itemHoverColor_, itemSelectedColor_);
            row.SetFontDesc(itemTextStyle_.font);
            row.SetTextColor(itemTextStyle_.colors.get(WidgetState::Normal).text);
            row.SetTextAlign(itemTextStyle_.hAlign, itemTextStyle_.vAlign);
            Widget& placed = AddChild(std::move(row));
            rows_.push_back(&placed);
        }

        if (selected_ >= static_cast<int>(items_.size())) selected_ = -1;
        syncRowSelection();
        Invalidate();

        RequestWindowRedraw();
    }

    void ListView::syncRowSelection() {
        for (std::size_t i = 0; i < rows_.size(); ++i)
            static_cast<ListViewRow*>(rows_[i])->SetSelected(static_cast<int>(i) == selected_);
    }

    void ListView::SetSelectedIndex(int index) {
        if (index < -1 || index >= static_cast<int>(items_.size())) index = -1;
        if (index == selected_) return;
        selected_ = index;
        syncRowSelection();
        if (index >= 0) ensureVisible(index);
        RequestWindowRedraw();
        RequestWindowRedraw();
    }

    const std::string& ListView::selectedText() const {
        static const std::string kEmpty;
        if (selected_ < 0 || selected_ >= static_cast<int>(items_.size())) return kEmpty;
        return items_[static_cast<std::size_t>(selected_)];
    }

    void ListView::selectByUser(int index) {
        if (index < 0 || index >= static_cast<int>(items_.size())) return;
        if (index == selected_) {
            ensureVisible(index);
            return;
        }
        selected_ = index;
        syncRowSelection();
        ensureVisible(index);
        RequestWindowRedraw();
        notifySelection(index);
    }

    void ListView::notifySelection(int index) {
        if (onSel_) onSel_(*this, index);
        if (onSelSelf_) onSelSelf_(*this);
    }

    void ListView::SetItemHeight(float h) {
        itemHeight_ = (h > 0.0f) ? h : 1.0f;
        for (Widget* r : rows_) static_cast<ListViewRow*>(r)->SetRowHeight(itemHeight_);
        Invalidate();
        RequestWindowRedraw();
    }

    void ListView::SetItemHoverColor(const Color4f& c) {
        itemHoverColor_ = c;
        for (Widget* r : rows_)
            static_cast<ListViewRow*>(r)->SetRowColors(itemHoverColor_, itemSelectedColor_);
        RequestWindowRedraw();
    }

    void ListView::SetItemSelectedColor(const Color4f& c) {
        itemSelectedColor_ = c;
        for (Widget* r : rows_)
            static_cast<ListViewRow*>(r)->SetRowColors(itemHoverColor_, itemSelectedColor_);
        RequestWindowRedraw();
    }

    void ListView::SetBackground(const Color4f& c) {
        background_ = c;
        markStyled();
        RequestWindowRedraw();
    }

    void ListView::SetBorder(float width, const Color4f& c) {
        borderWidth_ = (width > 0.0f) ? width : 0.0f;
        borderColor_ = c;
        SetPadding(borderWidth_, borderWidth_);
        markStyled();
        RequestWindowRedraw();
    }

    void ListView::SetRadius(float r) {
        radius_ = (r > 0.0f) ? r : 0.0f;
        markStyled();
        RequestWindowRedraw();
    }

    void ListView::ApplyTheme(const Theme& t) {
        background_ = t.list.background;
        borderColor_ = t.list.borderColor;
        borderWidth_ = (t.list.borderWidth > 0.0f) ? t.list.borderWidth : 0.0f;
        radius_ = (t.list.radius > 0.0f) ? t.list.radius : 0.0f;
        itemHeight_ = (t.list.itemHeight > 0.0f) ? t.list.itemHeight : 28.0f;
        itemHoverColor_ = t.list.itemHoverColor;
        itemSelectedColor_ = t.list.itemSelectedColor;
        itemTextStyle_ = t.list.item;

        SetPadding(borderWidth_, borderWidth_);

        for (Widget* r : rows_) {
            auto* row = static_cast<ListViewRow*>(r);
            row->SetRowHeight(itemHeight_);
            row->SetRowColors(itemHoverColor_, itemSelectedColor_);
            row->SetTextColor(itemTextStyle_.colors.get(WidgetState::Normal).text);
            row->SetFontDesc(itemTextStyle_.font);
            row->SetTextAlign(itemTextStyle_.hAlign, itemTextStyle_.vAlign);
        }
    }

    bool ListView::OnKeyPress(KeyEvent& e) {
        if (!isEnabled()) return false;
        const int n = static_cast<int>(items_.size());
        if (n <= 0) return false;

        int target = selected_;
        switch (e.key) {
        case Key::DownArrow: target = (target < 0) ? 0 : target + 1; break;
        case Key::UpArrow:   target = (target < 0) ? n - 1 : target - 1; break;
        case Key::Home:      target = 0; break;
        case Key::End:       target = n - 1; break;
        default:
            return false;
        }

        selectByUser(std::clamp(target, 0, n - 1));
        e.accept();
        return true;
    }

    bool ListView::OnMousePress(MouseEvent& e) {
        if (!isEnabled()) return false;
        if (ScrollView::OnMousePress(e)) return true;

        if (e.button != MouseButton::Left) return false;
        const int i = indexAtPoint(e.x, e.y);
        if (i < 0) return false;

        pressedRow_ = i;
        e.accept();
        return true;
    }

    bool ListView::OnMouseRelease(MouseEvent& e) {
        if (!isEnabled()) { pressedRow_ = -1; return ScrollView::OnMouseRelease(e); }
        if (pressedRow_ < 0) return ScrollView::OnMouseRelease(e);

        const int was = pressedRow_;
        pressedRow_ = -1;

        if (indexAtPoint(e.x, e.y) == was) selectByUser(was);

        e.accept();
        return true;
    }

    int ListView::indexAtPoint(float px, float py) const {
        for (std::size_t i = 0; i < rows_.size(); ++i) {
            const Widget* r = rows_[i];
            const float w = r->width(), h = r->height();
            if (w <= 0.0f || h <= 0.0f) continue;
            if (px >= r->x() && px < r->x() + w && py >= r->y() && py < r->y() + h)
                return static_cast<int>(i);
        }
        return -1;
    }

    void ListView::ensureVisible(int index) {
        if (index < 0 || index >= static_cast<int>(rows_.size())) return;

        const Widget* r = rows_[index];
        const float rh = r->height();
        if (rh <= 0.0f) return;

        const float viewTop = y() + padding().top;
        const float viewH = contentAvailableH();
        if (viewH <= 0.0f) return;

        const float top = r->y();
        const float bottom = top + rh;

        if (top < viewTop)
            SetScrollY(scrollY() - (viewTop - top));
        else if (bottom > viewTop + viewH)
            SetScrollY(scrollY() + (bottom - (viewTop + viewH)));
    }

    void ListView::Paint(FrameCollector& fc) const {
        drawutil::PaintBox(fc, x(), y(), width(), height(), radius_,
                           borderWidth_, borderColor_, background_);

        ScrollView::Paint(fc);
    }
}
