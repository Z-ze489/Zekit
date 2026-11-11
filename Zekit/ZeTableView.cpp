
#include "pch.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "include/ZeTableView.h"
#include "ZeDrawUtil.h"
#include "include/ZeBoxWidget.h"
#include "include/Zekit.h"

#include "ZeFrameCollector.h"
#include "ZeRoundedRectShape.h"

namespace Kit {

    namespace {
        constexpr float kMinAutoColW = 64.0f;

        constexpr float kCellPadY = 4.0f;

        std::size_t Utf8CodeLen(unsigned char c) {
            if (c < 0x80u) return 1;
            if ((c & 0xE0u) == 0xC0u) return 2;
            if ((c & 0xF0u) == 0xE0u) return 3;
            if ((c & 0xF8u) == 0xF0u) return 4;
            return 1;
        }

        std::vector<std::string> WrapByWidth(FrameCollector& fc, const std::string& text,
            const FontDesc& font, float avail) {
            std::vector<std::string> lines;
            if (text.empty()) { lines.push_back(std::string()); return lines; }
            if (avail <= 0.0f) { lines.push_back(text); return lines; }

            std::string cur;
            std::size_t lastSpace = std::string::npos;

            for (std::size_t i = 0; i < text.size(); ) {
                const unsigned char b = static_cast<unsigned char>(text[i]);
                std::size_t len = Utf8CodeLen(b);
                if (i + len > text.size()) len = 1;

                if (b == '\n') {
                    lines.push_back(cur);
                    cur.clear();
                    lastSpace = std::string::npos;
                    ++i;
                    continue;
                }

                const std::string piece = text.substr(i, len);
                if (!cur.empty() && fc.MeasureText(cur + piece, font).width > avail) {
                    if (lastSpace != std::string::npos) {
                        lines.push_back(cur.substr(0, lastSpace));
                        cur.erase(0, lastSpace + 1);
                    } else {
                        lines.push_back(cur);
                        cur.clear();
                    }
                    lastSpace = cur.find_last_of(' ');
                }
                if (b == ' ') lastSpace = cur.size();
                cur += piece;
                i += len;
            }
            lines.push_back(cur);
            return lines;
        }

        class TableRow : public BoxWidget {
        public:
            explicit TableRow(std::vector<std::string> cells) : cells_(std::move(cells)) {}

            void SetColumnLayout(const std::vector<float>& colX) {
                colX_ = colX;
                wrapDirty_ = true;
                markStyled();
                RequestWindowRedraw();
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
            void SetCellWrap(bool on) {
                if (wrap_ == on) return;
                wrap_ = on;
                wrapDirty_ = true;
                markStyled();
                RequestWindowRedraw();
            }
            void SetRowColors(const Color4f& hover, const Color4f& sel) {
                hoverColor_ = hover;
                selColor_ = sel;
                markStyled();
                RequestWindowRedraw();
            }
            void SetGridColor(const Color4f& c) {
                gridColor_ = c;
                markStyled();
                RequestWindowRedraw();
            }
            void SetCellStyle(const FontDesc& font, const Color4f& color, float padX) {
                font_ = font;
                textColor_ = color;
                padX_ = padX;
                wrapDirty_ = true;
                markStyled();
                RequestWindowRedraw();
            }

            float hintWidth() const override {
                if (prefWidth() > 0.0f) return prefWidth();
                return colX_.empty() ? 0.0f : colX_.back();
            }
            float hintHeight() const override {
                if (prefHeight() > 0.0f) return prefHeight();
                return (wrap_ && wrappedH_ > 0.0f) ? wrappedH_ : rowH_;
            }

        protected:
            const BoxStyle& themeBox(const Theme&) const override {
                static const BoxStyle kEmpty{};
                return kEmpty;
            }

            void paintContent(FrameCollector& fc) const override {
                const float w = width();
                const float h = height();
                if (w <= 0.0f || h <= 0.0f) return;

                if (ReflowCells(fc)) {
                    const_cast<TableRow*>(this)->notifyLayoutInputChanged();
                    RequestWindowRedraw();
                }

                const Color4f bg = selected_ ? selColor_
                                 : (isHovered() ? hoverColor_ : Color4f{ 0, 0, 0, 0 });
                if (bg.a > 0.0f)
                    drawutil::PaintRoundedRect(fc, x(), y(), w, h, 0.0f, bg);

                const float lineH = fc.MeasureText("Ag", font_).height;

                for (std::size_t i = 0; i < cells_.size() && i + 1 < colX_.size(); ++i) {
                    const float cx = x() + colX_[i];

                    if (i < wrapped_.size() && !wrapped_[i].empty() && lineH > 0.0f) {
                        const std::vector<std::string>& lines = wrapped_[i];
                        float ty = y() + (h - static_cast<float>(lines.size()) * lineH) * 0.5f;
                        for (const std::string& ln : lines) {
                            if (!ln.empty()) {
                                const TextMetrics tm = fc.MeasureText(ln, font_);
                                fc.AddText(ln, font_, cx + padX_, ty + tm.ascent, textColor_);
                            }
                            ty += lineH;
                        }
                    }

                    if (i > 0 && gridColor_.a > 0.0f)
                        fc.AddRect(cx, y(), 1.0f, h, gridColor_);
                }

                if (gridColor_.a > 0.0f && !colX_.empty())
                    fc.AddRect(x() + colX_.back(), y(), 1.0f, h, gridColor_);
            }

            bool ReflowCells(FrameCollector& fc) const {
                if (!wrapDirty_) return false;
                wrapDirty_ = false;

                const float oldH = wrappedH_;
                wrapped_.assign(cells_.size(), std::vector<std::string>());

                const float lineH = fc.MeasureText("Ag", font_).height;
                std::size_t maxLines = 1;

                for (std::size_t i = 0; i < cells_.size() && i + 1 < colX_.size(); ++i) {
                    const std::string& s = cells_[i];
                    const float cw = colX_[i + 1] - colX_[i];
                    const float avail = std::max(0.0f, cw - padX_ * 2.0f);

                    std::vector<std::string> ls;
                    if (s.empty()) {
                        ls.push_back(std::string());
                    }
                    else if (wrap_) {
                        ls = WrapByWidth(fc, s, font_, avail);
                    }
                    else {
                        ls.push_back(fc.MeasureText(s, font_).width > avail
                            ? drawutil::EllipsizeToWidth(fc, s, font_, avail) : s);
                    }
                    maxLines = std::max(maxLines, ls.size());
                    wrapped_[i] = std::move(ls);
                }

                wrappedH_ = std::max(rowH_, static_cast<float>(maxLines) * lineH + kCellPadY * 2.0f);
                return wrappedH_ != oldH;
            }

        private:
            std::vector<std::string> cells_;
            std::vector<float>       colX_;
            bool    selected_ = false;
            float   rowH_ = 28.0f;
            float   padX_ = 8.0f;
            Color4f hoverColor_{ 0, 0, 0, 0 };
            Color4f selColor_{ 0, 0, 0, 0 };
            Color4f gridColor_{ 0, 0, 0, 0 };
            Color4f textColor_{ 0, 0, 0, 1 };
            FontDesc font_;

            bool    wrap_ = true;
            mutable bool  wrapDirty_ = true;
            mutable float wrappedH_ = 0.0f;
            mutable std::vector<std::vector<std::string>> wrapped_;
        };
    }

    TableView::TableView() {
        SetDirection(FlexDirection::Column);
        SetAlign(AlignItems::Stretch);
        SetPadding(0.0f, headerHeight_, 0.0f, 0.0f);
        SetFocusable(true);

        ScrollView body;
        body.SetScrollDirection(ScrollDirection::Vertical);
        body.SetAlign(AlignItems::Stretch);
        body.SetFlexGrow(1.0f);
        body_ = &AddChild(std::move(body));

        ApplyTheme(Themes::Light());
    }

    void TableView::SetColumns(std::vector<TableColumn> cols) {
        cols_ = std::move(cols);
        colWidthsDirty_ = true;
        rebuildRows();
        RequestWindowRedraw();
    }

    void TableView::SetRows(std::vector<std::vector<std::string>> rows) {
        cells_ = std::move(rows);
        rowUserData_.clear();
        colWidthsDirty_ = true;
        rebuildRows();
        RequestWindowRedraw();
    }

    void TableView::Clear() {
        cells_.clear();
        rowUserData_.clear();
        selected_ = -1;
        rebuildRows();
    }

    void TableView::SetRowUserData(std::vector<std::string> data) {
        rowUserData_ = std::move(data);
        RequestWindowRedraw();
    }

    const std::string& TableView::rowUserData(int row) const {
        static const std::string kEmpty;
        return (row >= 0 && row < static_cast<int>(rowUserData_.size()))
            ? rowUserData_[static_cast<std::size_t>(row)]
            : kEmpty;
    }

    const std::vector<std::string>& TableView::rowAt(std::size_t index) const {
        static const std::vector<std::string> kEmpty;
        return index < cells_.size() ? cells_[index] : kEmpty;
    }

    void TableView::rebuildRows() {
        if (!body_) return;

        body_->ClearChildren();
        rows_.clear();
        pressedRow_ = -1;

        const Color4f cellColor = cellTextStyle_.colors.get(WidgetState::Normal).text;

        for (const std::vector<std::string>& data : cells_) {
            TableRow row(data);
            row.SetRowHeight(rowHeight_);
            row.SetCellWrap(wrap_);
            row.SetColumnLayout(colX_);
            row.SetRowColors(rowHoverColor_, rowSelectedColor_);
            row.SetGridColor(gridColor_);
            row.SetCellStyle(cellTextStyle_.font, cellColor, cellPadX_);
            Widget& placed = body_->AddChild(std::move(row));
            rows_.push_back(&placed);
        }

        if (selected_ >= static_cast<int>(cells_.size())) selected_ = -1;
        syncRowSelection();

        body_->Invalidate();
        Invalidate();
        requestRedraw();
    }

    void TableView::resolveColumnWidths() {
        const std::size_t n = cols_.size();
        colX_.assign(n + 1, 0.0f);
        if (n == 0) return;

        Window* win = window();

        std::vector<float> w(n, 0.0f);
        float total = 0.0f;
        for (std::size_t i = 0; i < n; ++i) {
            float cur = cols_[i].width;
            if (cur <= 0.0f) {
                float maxW = 0.0f;
                if (win) {
                    maxW = win->MeasureText(cols_[i].title, headerTextStyle_.font).width;
                    for (const std::vector<std::string>& row : cells_) {
                        if (i < row.size())
                            maxW = std::max(maxW, win->MeasureText(row[i], cellTextStyle_.font).width);
                    }
                }
                cur = maxW + cellPadX_ * 2.0f;
                if (cur < kMinAutoColW) cur = kMinAutoColW;
            }
            w[i] = cur;
            total += cur;
        }

        if (wrap_) {
            const float avail = width();
            if (avail > 0.0f && total > avail) {
                for (std::size_t i = 0; i < n; ++i)
                    w[i] = std::max(kMinAutoColW, w[i] * avail / total);

                float sum = 0.0f;
                for (float v : w) sum += v;
                float over = sum - avail;
                for (int guard = 0; guard < 16 && over > 1.0f; ++guard) {
                    std::size_t widest = 0;
                    for (std::size_t i = 1; i < n; ++i) if (w[i] > w[widest]) widest = i;
                    if (w[widest] <= kMinAutoColW) break;
                    const float cut = std::min(over, w[widest] - kMinAutoColW);
                    w[widest] -= cut;
                    over -= cut;
                }
            }
        }

        float cx = 0.0f;
        for (std::size_t i = 0; i < n; ++i) {
            colX_[i] = cx;
            cx += w[i];
        }
        colX_[n] = cx;
    }

    void TableView::pushColumnLayout() {
        for (Widget* r : rows_) static_cast<TableRow*>(r)->SetColumnLayout(colX_);
        if (body_) body_->Invalidate();
    }

    void TableView::syncRowSelection() {
        const bool show = hasFocus();
        for (std::size_t i = 0; i < rows_.size(); ++i)
            static_cast<TableRow*>(rows_[i])->SetSelected(show && static_cast<int>(i) == selected_);
    }

    void TableView::OnFocusIn()  { syncRowSelection(); }
    void TableView::OnFocusOut() { syncRowSelection(); }

    void TableView::SetSelectedRow(int index) {
        if (index < -1 || index >= static_cast<int>(cells_.size())) index = -1;
        if (index == selected_) return;
        selected_ = index;
        syncRowSelection();
        if (index >= 0) ensureVisible(index);
        requestRedraw();
        RequestWindowRedraw();
    }

    const std::vector<std::string>& TableView::selectedRowData() const {
        static const std::vector<std::string> kEmpty;
        if (selected_ < 0 || selected_ >= static_cast<int>(cells_.size())) return kEmpty;
        return cells_[static_cast<std::size_t>(selected_)];
    }

    void TableView::activateRow(int index) { selectByUser(index); }

    void TableView::selectByUser(int index) {
        if (index < 0 || index >= static_cast<int>(cells_.size())) return;
        if (index == selected_) {
            ensureVisible(index);
            return;
        }
        selected_ = index;
        syncRowSelection();
        ensureVisible(index);
        requestRedraw();
        notifyRowSelected(index);
    }

    void TableView::notifyRowSelected(int index) {
        if (onRow_) onRow_(*this, index);
        if (onRowSelf_) onRowSelf_(*this);
    }

    void TableView::SetRowHeight(float h) {
        if (h <= 0.0f) return;
        rowHeight_ = h;
        for (Widget* r : rows_) static_cast<TableRow*>(r)->SetRowHeight(h);
        if (body_) body_->Invalidate();
        requestRedraw();
        RequestWindowRedraw();
    }

    void TableView::SetCellWrap(bool on) {
        if (wrap_ == on) return;
        wrap_ = on;
        for (Widget* r : rows_) static_cast<TableRow*>(r)->SetCellWrap(on);
        colWidthsDirty_ = true;
        if (body_) body_->Invalidate();
        Invalidate();
        requestRedraw();
        RequestWindowRedraw();
    }

    void TableView::SetHeaderHeight(float h) {
        if (h <= 0.0f) return;
        headerHeight_ = h;
        SetPadding(0.0f, headerHeight_, 0.0f, 0.0f);
        Invalidate();
        requestRedraw();
        RequestWindowRedraw();
    }

    void TableView::SetCellPaddingX(float px) {
        cellPadX_ = (px > 0.0f) ? px : 0.0f;
        colWidthsDirty_ = true;
        rebuildRows();
        RequestWindowRedraw();
    }

    void TableView::SetRowHoverColor(const Color4f& c) {
        rowHoverColor_ = c;
        for (Widget* r : rows_)
            static_cast<TableRow*>(r)->SetRowColors(rowHoverColor_, rowSelectedColor_);
        requestRedraw();
        RequestWindowRedraw();
    }

    void TableView::SetRowSelectedColor(const Color4f& c) {
        rowSelectedColor_ = c;
        for (Widget* r : rows_)
            static_cast<TableRow*>(r)->SetRowColors(rowHoverColor_, rowSelectedColor_);
        requestRedraw();
        RequestWindowRedraw();
    }

    void TableView::SetGridColor(const Color4f& c) {
        gridColor_ = c;
        for (Widget* r : rows_) static_cast<TableRow*>(r)->SetGridColor(gridColor_);
        requestRedraw();
        RequestWindowRedraw();
    }

    void TableView::SetHeaderBackground(const Color4f& c) {
        headerBackground_ = c;
        requestRedraw();
        RequestWindowRedraw();
    }

    void TableView::SetBackground(const Color4f& c) {
        background_ = c;
        Panel::SetBackground(c);
        requestRedraw();
        RequestWindowRedraw();
    }

    void TableView::SetBorder(float width, const Color4f& c) {
        borderWidth_ = (width > 0.0f) ? width : 0.0f;
        borderColor_ = c;
        Panel::SetBorder(borderWidth_, borderColor_);
        requestRedraw();
        RequestWindowRedraw();
    }

    void TableView::ApplyTheme(const Theme& t) {
        rowHeight_ = (t.table.rowHeight > 0.0f) ? t.table.rowHeight : 28.0f;
        headerHeight_ = (t.table.headerHeight > 0.0f) ? t.table.headerHeight : 32.0f;
        cellPadX_ = (t.table.cellPaddingX > 0.0f) ? t.table.cellPaddingX : 8.0f;
        rowHoverColor_ = t.table.rowHoverColor;
        rowSelectedColor_ = t.table.rowSelectedColor;
        gridColor_ = t.table.gridColor;
        headerBackground_ = t.table.headerBackground;
        background_ = t.table.background;
        borderColor_ = t.table.borderColor;
        borderWidth_ = (t.table.borderWidth > 0.0f) ? t.table.borderWidth : 0.0f;
        headerTextStyle_ = t.table.header;
        cellTextStyle_ = t.table.cell;

        SetPadding(0.0f, headerHeight_, 0.0f, 0.0f);
        Panel::SetBackground(background_);
        Panel::SetBorder(borderWidth_, borderColor_);

        colWidthsDirty_ = true;

        const Color4f cellColor = cellTextStyle_.colors.get(WidgetState::Normal).text;
        for (Widget* r : rows_) {
            auto* row = static_cast<TableRow*>(r);
            row->SetRowHeight(rowHeight_);
            row->SetCellWrap(wrap_);
            row->SetColumnLayout(colX_);
            row->SetRowColors(rowHoverColor_, rowSelectedColor_);
            row->SetGridColor(gridColor_);
            row->SetCellStyle(cellTextStyle_.font, cellColor, cellPadX_);
        }

        if (body_) body_->Invalidate();
        Invalidate();
    }

    bool TableView::OnKeyPress(KeyEvent& e) {
        if (!isEnabled()) return false;
        const int n = static_cast<int>(cells_.size());
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

    bool TableView::OnMousePress(MouseEvent& e) {
        if (!isEnabled()) return false;
        if (e.button != MouseButton::Left) return false;
        const int i = rowAtPoint(e.x, e.y);
        if (i < 0) return false;
        pressedRow_ = i;
        e.accept();
        return true;
    }

    bool TableView::OnMouseRelease(MouseEvent& e) {
        if (!isEnabled()) { pressedRow_ = -1; return false; }
        if (pressedRow_ < 0) return false;

        const int was = pressedRow_;
        pressedRow_ = -1;

        if (rowAtPoint(e.x, e.y) == was) selectByUser(was);
        e.accept();
        return true;
    }

    int TableView::rowAtPoint(float px, float py) const {
        float viewTop = 0.0f, viewH = 0.0f;
        if (body_) {
            viewTop = body_->y();
            viewH = body_->height();
            if (viewH <= 0.0f) return -1;
            if (py < viewTop || py >= viewTop + viewH) return -1;
        }

        for (std::size_t i = 0; i < rows_.size(); ++i) {
            const Widget* r = rows_[i];
            const float w = r->width(), h = r->height();
            if (w <= 0.0f || h <= 0.0f) continue;
            if (px >= r->x() && px < r->x() + w && py >= r->y() && py < r->y() + h)
                return static_cast<int>(i);
        }
        return -1;
    }

    void TableView::ensureVisible(int index) {
        if (!body_ || index < 0 || index >= static_cast<int>(rows_.size())) return;

        const Widget* r = rows_[index];
        const float rh = r->height();
        if (rh <= 0.0f) return;

        const float viewTop = body_->y();
        const float viewH = body_->height();
        if (viewH <= 0.0f) return;

        const float top = r->y();
        const float bottom = top + rh;

        if (top < viewTop)
            body_->SetScrollY(body_->scrollY() - (viewTop - top));
        else if (bottom > viewTop + viewH)
            body_->SetScrollY(body_->scrollY() + (bottom - (viewTop + viewH)));
    }

    void TableView::Paint(FrameCollector& fc) const {
        if (colWidthsDirty_ || colResolveWidth_ != width()) {
            auto* self = const_cast<TableView*>(this);
            self->resolveColumnWidths();
            self->pushColumnLayout();
            self->colWidthsDirty_ = false;
            self->colResolveWidth_ = width();
        }

        Panel::Paint(fc);

        const float w = width();
        const float hh = headerHeight_;
        if (w <= 0.0f || hh <= 0.0f) return;

        fc.PushClip(x(), y(), w, hh);

        drawutil::PaintRoundedRect(fc, x(), y(), w, hh, 0.0f, headerBackground_);

        const Color4f titleColor = headerTextStyle_.colors.get(WidgetState::Normal).text;
        for (std::size_t i = 0; i < cols_.size() && i + 1 < colX_.size(); ++i) {
            const float cx = x() + colX_[i];
            const float cw = colX_[i + 1] - colX_[i];

            const std::string& title = cols_[i].title;
            if (!title.empty()) {
                TextMetrics tm = fc.MeasureText(title, headerTextStyle_.font);
                const float avail = std::max(0.0f, cw - cellPadX_ * 2.0f);
                std::string shown = title;
                if (tm.width > avail) {
                    shown = drawutil::EllipsizeToWidth(fc, title, headerTextStyle_.font, avail);
                    tm = fc.MeasureText(shown, headerTextStyle_.font);
                }
                if (tm.width > 0.0f) {
                    const float ty = y() + (hh - tm.height) * 0.5f;
                    fc.AddText(shown, headerTextStyle_.font, cx + cellPadX_, ty + tm.ascent, titleColor);
                }
            }

            if (i > 0 && gridColor_.a > 0.0f)
                fc.AddRect(cx, y(), 1.0f, hh, gridColor_);
        }

        fc.PopClip();

        if (gridColor_.a > 0.0f)
            fc.AddRect(x(), y() + hh - 1.0f, w, 1.0f, gridColor_);
    }

    void TableView::requestRedraw() {
        RequestWindowRedraw();
    }
}
