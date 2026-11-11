
#include "pch.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "include/ZeTreeView.h"
#include "ZeDrawUtil.h"
#include "include/ZeBoxWidget.h"
#include "include/Zekit.h"

#include "ZeFrameCollector.h"
#include "ZeRoundedRectShape.h"

namespace Kit {

    namespace {
        constexpr float kRowPadX = 4.0f;
        constexpr float kTextPadR = 6.0f;

        class TreeRow : public BoxWidget {
        public:
            explicit TreeRow(std::string text) : text_(std::move(text)) {}

            void SetNodeInfo(int nodeId, int depth, bool hasChildren) {
                nodeId_ = nodeId;
                depth_ = depth;
                hasChildren_ = hasChildren;
                markStyled();
                RequestWindowRedraw();
            }
            void SetNodeState(bool expanded, bool selected) {
                if (expanded_ == expanded && selected_ == selected) return;
                expanded_ = expanded;
                selected_ = selected;
                markStyled();
                RequestWindowRedraw();
            }
            void SetLayout(float indentW, float indicatorW) {
                indentW_ = indentW;
                indicatorW_ = indicatorW;
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
            void SetIndicatorColor(const Color4f& c) {
                indicatorColor_ = c;
                markStyled();
                RequestWindowRedraw();
            }
            void SetRowText(std::string text) {
                text_ = std::move(text);
                markStyled();
                RequestWindowRedraw();
            }
            void SetRowTextStyle(const FontDesc& font, const Color4f& color) {
                font_ = font;
                textColor_ = color;
                markStyled();
                RequestWindowRedraw();
            }

            float hintWidth() const override {
                if (prefWidth() > 0.0f) return prefWidth();
                float w = 0.0f;
                if (Window* win = window())
                    w = win->MeasureText(text_, font_).width;
                return kRowPadX + static_cast<float>(depth_) * indentW_ + indicatorW_ + w + kTextPadR;
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

            void paintContent(FrameCollector& fc) const override {
                const float w = width();
                const float h = height();
                if (w <= 0.0f || h <= 0.0f) return;

                const Color4f bg = selected_ ? selColor_
                                 : (isHovered() ? hoverColor_ : Color4f{ 0, 0, 0, 0 });
                if (bg.a > 0.0f)
                    drawutil::PaintRoundedRect(fc, x(), y(), w, h, 0.0f, bg);

                const float baseX = x() + kRowPadX + static_cast<float>(depth_) * indentW_;

                if (hasChildren_ && indicatorColor_.a > 0.0f) {
                    const float cx = baseX + indicatorW_ * 0.5f;
                    const float cy = y() + h * 0.5f;
                    const float s = 4.0f;
                    const float t = 1.0f;

                    fc.AddRect(cx - s, cy - t * 0.5f, s * 2.0f, t, indicatorColor_);
                    if (!expanded_)
                        fc.AddRect(cx - t * 0.5f, cy - s, t, s * 2.0f, indicatorColor_);
                }

                if (!text_.empty()) {
                    const float tx = baseX + indicatorW_;
                    const float avail = std::max(0.0f, (x() + w) - tx - kTextPadR);
                    TextMetrics tm = fc.MeasureText(text_, font_);
                    std::string shown = text_;
                    if (tm.width > avail) {
                        shown = drawutil::EllipsizeToWidth(fc, text_, font_, avail);
                        tm = fc.MeasureText(shown, font_);
                    }
                    if (tm.width > 0.0f) {
                        const float ty = y() + (h - tm.height) * 0.5f;
                        fc.AddText(shown, font_, tx, ty + tm.ascent, textColor_);
                    }
                }
            }

        private:
            std::string text_;
            int   nodeId_ = -1;
            int   depth_ = 0;
            bool  hasChildren_ = false;
            bool  expanded_ = true;
            bool  selected_ = false;
            float rowH_ = 28.0f;
            float indentW_ = 18.0f;
            float indicatorW_ = 20.0f;
            Color4f hoverColor_{ 0, 0, 0, 0 };
            Color4f selColor_{ 0, 0, 0, 0 };
            Color4f indicatorColor_{ 0, 0, 0, 0 };
            Color4f textColor_{ 0, 0, 0, 1 };
            FontDesc font_;
        };

        TreeRow* AsRow(Widget* w) { return static_cast<TreeRow*>(w); }
    }

    TreeView::TreeView() {
        SetScrollDirection(ScrollDirection::Vertical);
        SetAlign(AlignItems::Stretch);
        SetFocusable(true);
        ApplyTheme(Themes::Light());
    }

    int TreeView::AddRoot(std::string text) { return AddNode(-1, std::move(text)); }

    int TreeView::AddNode(int parentId, std::string text) {
        if (parentId < 0 || parentId >= static_cast<int>(nodes_.size())) parentId = -1;

        Node n;
        n.text = std::move(text);
        n.parent = parentId;
        n.depth = (parentId >= 0) ? nodes_[static_cast<std::size_t>(parentId)].depth + 1 : 0;
        n.expanded = true;
        nodes_.push_back(std::move(n));

        const int id = static_cast<int>(nodes_.size()) - 1;
        if (parentId >= 0) nodes_[static_cast<std::size_t>(parentId)].hasChildren = true;

        TreeRow row(nodes_[static_cast<std::size_t>(id)].text);
        row.SetNodeInfo(id, nodes_[static_cast<std::size_t>(id)].depth, nodes_[static_cast<std::size_t>(id)].hasChildren);
        row.SetLayout(indentW_, indicatorW_);
        row.SetRowHeight(rowHeight_);
        row.SetRowColors(rowHoverColor_, rowSelectedColor_);
        row.SetIndicatorColor(indicatorColor_);
        row.SetRowTextStyle(itemTextStyle_.font,
            itemTextStyle_.colors.get(WidgetState::Normal).text);
        row.SetNodeState(true, false);

        Widget& placed = AddChild(std::move(row));
        rows_.push_back(&placed);

        if (parentId >= 0) syncRow(parentId);

        refreshVisibility();
        return id;
    }

    void TreeView::Clear() {
        nodes_.clear();
        selected_ = -1;
        pressedRow_ = -1;
        rebuildRows();
        Invalidate();
        requestRedraw();
    }

    const std::string& TreeView::nodeText(int id) const {
        static const std::string kEmpty;
        if (id < 0 || id >= static_cast<int>(nodes_.size())) return kEmpty;
        return nodes_[static_cast<std::size_t>(id)].text;
    }

    void TreeView::SetNodeText(int id, std::string text) {
        if (id < 0 || id >= static_cast<int>(nodes_.size())) return;
        nodes_[static_cast<std::size_t>(id)].text = std::move(text);

        AsRow(rows_[static_cast<std::size_t>(id)])->SetRowText(nodes_[static_cast<std::size_t>(id)].text);
        Invalidate();
        requestRedraw();
        RequestWindowRedraw();
    }

    int TreeView::parentOf(int id) const {
        if (id < 0 || id >= static_cast<int>(nodes_.size())) return -1;
        return nodes_[static_cast<std::size_t>(id)].parent;
    }

    int TreeView::childCount(int id) const {
        if (id < 0 || id >= static_cast<int>(nodes_.size())) return 0;
        int n = 0;
        for (const Node& node : nodes_)
            if (node.parent == id) ++n;
        return n;
    }

    int TreeView::depthOf(int id) const {
        if (id < 0 || id >= static_cast<int>(nodes_.size())) return -1;
        return nodes_[static_cast<std::size_t>(id)].depth;
    }

    void TreeView::SetExpanded(int id, bool on) {
        if (id < 0 || id >= static_cast<int>(nodes_.size())) return;
        if (nodes_[static_cast<std::size_t>(id)].expanded == on) return;
        nodes_[static_cast<std::size_t>(id)].expanded = on;
        refreshVisibility();
        requestRedraw();
        RequestWindowRedraw();
    }

    bool TreeView::isExpanded(int id) const {
        if (id < 0 || id >= static_cast<int>(nodes_.size())) return false;
        return nodes_[static_cast<std::size_t>(id)].expanded;
    }

    void TreeView::ExpandAll() {
        for (Node& n : nodes_) n.expanded = true;
        refreshVisibility();
        requestRedraw();
    }

    void TreeView::CollapseAll() {
        for (Node& n : nodes_) n.expanded = false;
        refreshVisibility();
        requestRedraw();
    }

    bool TreeView::nodeVisible(int id) const {
        for (int p = nodes_[static_cast<std::size_t>(id)].parent; p >= 0;
             p = nodes_[static_cast<std::size_t>(p)].parent) {
            if (!nodes_[static_cast<std::size_t>(p)].expanded) return false;
        }
        return true;
    }

    void TreeView::refreshVisibility() {
        visible_.clear();
        for (std::size_t i = 0; i < nodes_.size(); ++i) {
            const bool vis = nodeVisible(static_cast<int>(i));
            AsRow(rows_[i])->SetNodeState(nodes_[i].expanded, static_cast<int>(i) == selected_);
            rows_[i]->SetVisible(vis);
            if (vis) visible_.push_back(static_cast<int>(i));
        }
        Invalidate();
    }

    void TreeView::syncRow(int id) {
        if (id < 0 || id >= static_cast<int>(rows_.size())) return;
        const Node& n = nodes_[static_cast<std::size_t>(id)];
        TreeRow* row = AsRow(rows_[static_cast<std::size_t>(id)]);
        row->SetNodeInfo(id, n.depth, n.hasChildren);
        row->SetNodeState(n.expanded, id == selected_);
    }

    void TreeView::SetSelectedNode(int id) {
        if (id < -1 || id >= static_cast<int>(nodes_.size())) id = -1;
        if (id == selected_) return;

        if (id >= 0) {
            for (int p = nodes_[static_cast<std::size_t>(id)].parent; p >= 0;
                 p = nodes_[static_cast<std::size_t>(p)].parent)
                nodes_[static_cast<std::size_t>(p)].expanded = true;
            refreshVisibility();
        }

        selected_ = id;
        for (std::size_t i = 0; i < rows_.size(); ++i) syncRow(static_cast<int>(i));
        if (id >= 0) ensureVisible(id);
        requestRedraw();
        RequestWindowRedraw();
    }

    const std::string& TreeView::selectedText() const {
        static const std::string kEmpty;
        if (selected_ < 0 || selected_ >= static_cast<int>(nodes_.size())) return kEmpty;
        return nodes_[static_cast<std::size_t>(selected_)].text;
    }

    void TreeView::selectByUser(int id) {
        if (id < 0 || id >= static_cast<int>(nodes_.size())) return;
        if (id == selected_) {
            ensureVisible(id);
            return;
        }
        selected_ = id;
        for (std::size_t i = 0; i < rows_.size(); ++i) syncRow(static_cast<int>(i));
        ensureVisible(id);
        requestRedraw();

        if (onSel_) onSel_(*this, id);
        if (onSelSelf_) onSelSelf_(*this);
    }

    void TreeView::toggleByUser(int id) {
        if (id < 0 || id >= static_cast<int>(nodes_.size())) return;
        Node& n = nodes_[static_cast<std::size_t>(id)];
        if (!n.hasChildren) return;

        n.expanded = !n.expanded;
        refreshVisibility();
        requestRedraw();

        if (onTog_) onTog_(*this, id);
        if (onTogSelf_) onTogSelf_(*this);
    }

    int TreeView::nodeAtPoint(float px, float py) const {
        for (std::size_t i = 0; i < rows_.size(); ++i) {
            const Widget* r = rows_[i];
            if (!r->isVisible()) continue;
            const float w = r->width(), h = r->height();
            if (w <= 0.0f || h <= 0.0f) continue;
            if (px >= r->x() && px < r->x() + w && py >= r->y() && py < r->y() + h)
                return static_cast<int>(i);
        }
        return -1;
    }

    bool TreeView::hitIndicator(float px, float py, int id) const {
        if (id < 0 || id >= static_cast<int>(rows_.size())) return false;
        const Widget* r = rows_[static_cast<std::size_t>(id)];
        if (!r->isVisible()) return false;

        const float baseX = r->x() + kRowPadX
            + static_cast<float>(nodes_[static_cast<std::size_t>(id)].depth) * indentW_;
        return px >= baseX && px < baseX + indicatorW_
            && py >= r->y() && py < r->y() + r->height();
    }

    void TreeView::ensureVisible(int id) {
        if (id < 0 || id >= static_cast<int>(rows_.size())) return;
        const Widget* r = rows_[static_cast<std::size_t>(id)];
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

    int TreeView::nextVisible(int id, int dir) const {
        const int n = static_cast<int>(visible_.size());
        if (n <= 0) return -1;

        int pos = -1;
        for (int i = 0; i < n; ++i)
            if (visible_[static_cast<std::size_t>(i)] == id) { pos = i; break; }

        if (pos < 0) return visible_[0];
        pos += dir;
        if (pos < 0) pos = 0;
        if (pos >= n) pos = n - 1;
        return visible_[static_cast<std::size_t>(pos)];
    }

    bool TreeView::OnKeyPress(KeyEvent& e) {
        if (!isEnabled()) return false;
        if (visible_.empty()) return false;

        const int cur = (selected_ >= 0) ? selected_ : visible_.front();
        switch (e.key) {
        case Key::DownArrow: selectByUser(nextVisible(cur, 1)); break;
        case Key::UpArrow:   selectByUser(nextVisible(cur, -1)); break;
        case Key::Home:      selectByUser(visible_.front()); break;
        case Key::End:       selectByUser(visible_.back()); break;

        case Key::RightArrow:
            if (nodes_[static_cast<std::size_t>(cur)].hasChildren
                && !nodes_[static_cast<std::size_t>(cur)].expanded)
                toggleByUser(cur);
            else
                selectByUser(nextVisible(cur, 1));
            break;

        case Key::LeftArrow:
            if (nodes_[static_cast<std::size_t>(cur)].hasChildren
                && nodes_[static_cast<std::size_t>(cur)].expanded)
                toggleByUser(cur);
            else if (const int p = nodes_[static_cast<std::size_t>(cur)].parent; p >= 0)
                selectByUser(p);
            break;

        default:
            return false;
        }

        e.accept();
        return true;
    }

    bool TreeView::OnMousePress(MouseEvent& e) {
        if (!isEnabled()) return false;
        if (ScrollView::OnMousePress(e)) return true;

        if (e.button != MouseButton::Left) return false;

        const int id = nodeAtPoint(e.x, e.y);
        if (id < 0) return false;

        if (nodes_[static_cast<std::size_t>(id)].hasChildren
            && hitIndicator(e.x, e.y, id)) {
            toggleByUser(id);
            e.accept();
            return true;
        }

        pressedRow_ = id;
        e.accept();
        return true;
    }

    bool TreeView::OnMouseRelease(MouseEvent& e) {
        if (!isEnabled()) { pressedRow_ = -1; return ScrollView::OnMouseRelease(e); }
        if (pressedRow_ < 0) return ScrollView::OnMouseRelease(e);

        const int was = pressedRow_;
        pressedRow_ = -1;

        if (nodeAtPoint(e.x, e.y) == was) selectByUser(was);
        e.accept();
        return true;
    }

    void TreeView::Paint(FrameCollector& fc) const {
        drawutil::PaintBox(fc, x(), y(), width(), height(), radius_,
                           borderWidth_, borderColor_, background_);

        ScrollView::Paint(fc);
    }

    void TreeView::ApplyTheme(const Theme& t) {
        rowHeight_ = (t.tree.rowHeight > 0.0f) ? t.tree.rowHeight : 28.0f;
        indentW_ = (t.tree.indentWidth > 0.0f) ? t.tree.indentWidth : 18.0f;
        indicatorW_ = (t.tree.indicatorWidth > 0.0f) ? t.tree.indicatorWidth : 20.0f;
        rowHoverColor_ = t.tree.rowHoverColor;
        rowSelectedColor_ = t.tree.rowSelectedColor;
        indicatorColor_ = t.tree.indicatorColor;
        itemTextStyle_ = t.tree.item;
        background_ = t.tree.background;
        borderColor_ = t.tree.borderColor;
        borderWidth_ = (t.tree.borderWidth > 0.0f) ? t.tree.borderWidth : 0.0f;
        radius_ = (t.tree.radius > 0.0f) ? t.tree.radius : 0.0f;

        SetPadding(borderWidth_, borderWidth_);

        const Color4f textColor = itemTextStyle_.colors.get(WidgetState::Normal).text;
        for (std::size_t i = 0; i < rows_.size(); ++i) {
            auto* row = AsRow(rows_[i]);
            row->SetLayout(indentW_, indicatorW_);
            row->SetRowHeight(rowHeight_);
            row->SetRowColors(rowHoverColor_, rowSelectedColor_);
            row->SetIndicatorColor(indicatorColor_);
            row->SetRowTextStyle(itemTextStyle_.font, textColor);
        }
        Invalidate();
    }

    void TreeView::rebuildRows() {
        ClearChildren();
        rows_.clear();
        visible_.clear();
    }

    void TreeView::requestRedraw() {
        RequestWindowRedraw();
    }
}
