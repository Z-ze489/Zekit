
#include "pch.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "include/ZeDockPanel.h"
#include "include/Zekit.h"

#include "ZeDrawUtil.h"
#include "ZeFrameCollector.h"

namespace Kit {

    namespace {

        constexpr float kHitSlop = 3.0f;

        constexpr float kTitlePadX = 10.0f;
        constexpr float kClosePadX = 8.0f;
        constexpr float kTitleActiveAlpha = 0.18f;

        constexpr float kFallbackTitleH = 26.0f;
        constexpr float kFallbackMinW = 56.0f;
        constexpr float kFallbackMinH = 40.0f;

        const char* const kCross = "\xC3\x97";

    }

    DockPanel::DockPane::DockPane(std::string title)
        : title_(std::move(title)) {
        SetPadding(0.0f, titleBarHeight_, 0.0f, 0.0f);
        ApplyTheme(Themes::Light());
    }

    DockPanel* DockPanel::DockPane::ownerPane() const {
        return dynamic_cast<DockPanel*>(parent());
    }

    void DockPanel::DockPane::setStyle(const DockPanelStyle& s, float titleBarHeight) {
        background_ = s.paneBackground;
        titleBarColor_ = s.titleBarColor;
        titleDividerAlpha_ = std::clamp(s.titleBarDivider, 0.0f, 1.0f);
        titleStyle_ = s.title;
        closeColor_ = s.closeColor;
        closeHoverColor_ = s.closeHoverColor;
        accentColor_ = s.handleActiveColor;
        closeSize_ = (s.closeSize > 0.0f) ? s.closeSize : 14.0f;

        if (DockPanel* o = ownerPane()) {
            radius_ = o->paneRadius();
            borderWidth_ = o->paneBorderWidth();
            borderColor_ = o->paneBorderColor();
        } else {
            radius_ = s.paneRadius;
            borderWidth_ = s.paneBorderWidth;
            borderColor_ = s.paneBorderColor;
        }

        const float h = (titleBarHeight > 0.0f) ? titleBarHeight : 0.0f;
        const float padBottom = std::min(radius_, 12.0f);
        if (h != titleBarHeight_ || padBottom != padBottom_) {
            titleBarHeight_ = h;
            padBottom_ = padBottom;
            SetPadding(0.0f, titleBarHeight_, 0.0f, padBottom_);
            Invalidate();
        }
        RequestWindowRedraw();
    }

    void DockPanel::DockPane::setTitleActive(bool on) {
        if (titleActive_ == on) return;
        titleActive_ = on;
        RequestWindowRedraw();
    }

    void DockPanel::DockPane::ApplyTheme(const Theme& t) {
        DockPanel* o = ownerPane();
        setStyle(t.dockPanel, o ? o->titleBarHeight() : titleBarHeight_);
    }

    DockPanel::DockPane::Rect DockPanel::DockPane::titleBarRect() const {
        Rect r;
        r.x = x();
        r.y = y();
        r.w = width();
        r.h = std::min(titleBarHeight_, height());
        return r;
    }

    DockPanel::DockPane::Rect DockPanel::DockPane::closeRect() const {
        const Rect tb = titleBarRect();
        Rect r;
        if (closeSize_ <= 0.0f || tb.w < closeSize_ + kClosePadX * 2.0f || tb.h <= 0.0f)
            return r;
        r.w = closeSize_;
        r.h = closeSize_;
        r.x = tb.x + tb.w - kClosePadX - r.w;
        r.y = tb.y + (tb.h - r.h) * 0.5f;
        return r;
    }

    bool DockPanel::DockPane::hitTitleBar(float px, float py) const {
        const Rect tb = titleBarRect();
        if (tb.w <= 0.0f || tb.h <= 0.0f) return false;
        if (px < tb.x || px >= tb.x + tb.w || py < tb.y || py >= tb.y + tb.h) return false;
        return !hitClose(px, py);
    }

    bool DockPanel::DockPane::hitClose(float px, float py) const {
        const Rect cb = closeRect();
        if (cb.w <= 0.0f) return false;
        constexpr float kSlop = 2.0f;
        return px >= cb.x - kSlop && px < cb.x + cb.w + kSlop
            && py >= cb.y - kSlop && py < cb.y + cb.h + kSlop;
    }

    void DockPanel::DockPane::Paint(FrameCollector& fc) const {
        const float px = x(), py = y(), pw = width(), ph = height();
        if (pw <= 0.0f || ph <= 0.0f) return;

        const float r = std::min(radius_, std::min(pw, ph) * 0.5f);

        drawutil::PaintBox(fc, px, py, pw, ph, r, borderWidth_, borderColor_, background_);

        const Rect tb = titleBarRect();
        if (tb.h <= 0.0f) return;

        const float tr = std::min(r, tb.h);
        if (titleBarColor_.a > 0.0f) {
            drawutil::PaintRoundedRect(fc, tb.x, tb.y, tb.w, tb.h, tr, titleBarColor_);
            if (tb.h > tr)
                drawutil::PaintRoundedRect(fc, tb.x, tb.y + tb.h - tr, tb.w, tr, 0.0f,
                    titleBarColor_);
        }

        if (titleActive_ && accentColor_.a > 0.0f) {
            Color4f a = accentColor_;
            a.a *= kTitleActiveAlpha;
            drawutil::PaintRoundedRect(fc, tb.x, tb.y, tb.w, tb.h, tr, a);
            if (tb.h > tr)
                drawutil::PaintRoundedRect(fc, tb.x, tb.y + tb.h - tr, tb.w, tr, 0.0f, a);
        }

        if (borderColor_.a > 0.0f && titleDividerAlpha_ > 0.0f) {
            Color4f line = borderColor_;
            line.a *= titleDividerAlpha_;
            drawutil::PaintHLine(fc, tb.x, tb.y + tb.h - 1.0f, tb.w, 1.0f, line);
        }

        const Rect cb = closeRect();

        if (!title_.empty()) {
            const float avail = std::max(0.0f,
                tb.w - kTitlePadX - ((cb.w > 0.0f) ? (cb.w + kClosePadX) : 0.0f));
            if (avail > 0.0f) {
                TextMetrics tm = fc.MeasureText(title_, titleStyle_.font);
                std::string shown = title_;
                if (tm.width > avail) {
                    shown = drawutil::Ellipsize(fc, title_, titleStyle_.font, avail);
                    tm = fc.MeasureText(shown, titleStyle_.font);
                }
                if (tm.width > 0.0f) {
                    const Color4f fg = titleStyle_.colors.get(WidgetState::Normal).text;
                    const float ty = tb.y + (tb.h - tm.height) * 0.5f;
                    fc.AddText(shown, titleStyle_.font, tb.x + kTitlePadX, ty + tm.ascent, fg);
                }
            }
        }

        if (cb.w > 0.0f) {
            if (hoverClose_ || pressedClose_) {
                const float br = std::min(4.0f, cb.w * 0.3f);
                Color4f hl = pressedClose_ ? closeHoverColor_ : closeColor_;
                hl.a = 0.14f;
                drawutil::PaintRoundedRect(fc, cb.x, cb.y, cb.w, cb.h, br, hl);
            }
            const TextMetrics tm = fc.MeasureText(kCross, titleStyle_.font);
            if (tm.width > 0.0f) {
                const float cx = cb.x + (cb.w - tm.width) * 0.5f;
                const float cy = cb.y + (cb.h - tm.height) * 0.5f;
                const Color4f c = (hoverClose_ || pressedClose_) ? closeHoverColor_ : closeColor_;
                fc.AddText(kCross, titleStyle_.font, cx, cy + tm.ascent, c);
            }
        }
    }

    bool DockPanel::DockPane::OnMousePress(MouseEvent& e) {
        if (e.button != MouseButton::Left) return false;
        if (titleBarHeight_ <= 0.0f) return false;

        if (hitClose(e.x, e.y)) {
            pressedClose_ = true;
            RequestWindowRedraw();
            e.accept();
            return true;
        }

        if (hitTitleBar(e.x, e.y)) {
            titleActive_ = true;
            if (DockPanel* o = ownerPane()) o->beginPaneDrag(*this, e.x, e.y);
            RequestWindowRedraw();
            e.accept();
            return true;
        }
        return false;
    }

    bool DockPanel::DockPane::OnMouseMove(MouseEvent& e) {
        const bool over = hitClose(e.x, e.y);
        if (over != hoverClose_) {
            hoverClose_ = over;
            RequestWindowRedraw();
        }

        if (titleActive_) {
            if (DockPanel* o = ownerPane()) o->updatePaneDrag(e.x, e.y);
            e.accept();
            return true;
        }
        return hoverClose_;
    }

    bool DockPanel::DockPane::OnMouseRelease(MouseEvent& e) {
        if (pressedClose_) {
            const bool onClose = hitClose(e.x, e.y);
            pressedClose_ = false;
            RequestWindowRedraw();
            if (onClose) {
                if (DockPanel* o = ownerPane()) o->requestPaneClose(*this);
                e.accept();
                return true;
            }
        }

        if (titleActive_) {
            titleActive_ = false;
            if (DockPanel* o = ownerPane()) o->finishPaneDrag();
            RequestWindowRedraw();
            e.accept();
            return true;
        }
        return false;
    }

    void DockPanel::DockPane::OnMouseLeave() {
        if (!hoverClose_) return;
        hoverClose_ = false;
        RequestWindowRedraw();
    }

    bool DockPanel::DropIndicator::paintsOnTop() const {
        DockPanel* o = dynamic_cast<DockPanel*>(parent());
        if (!o) return false;
        float x = 0, y = 0, w = 0, h = 0;
        return o->dropRect(x, y, w, h);
    }

    void DockPanel::DropIndicator::Paint(FrameCollector& fc) const {
        DockPanel* o = dynamic_cast<DockPanel*>(parent());
        if (!o) return;
        float x = 0, y = 0, w = 0, h = 0;
        if (!o->dropRect(x, y, w, h)) return;

        fc.ResetClip();

        Color4f edge = dropColor_;
        edge.a = std::min(1.0f, dropColor_.a * 2.0f);
        drawutil::PaintBox(fc, x, y, w, h, 0.0f, dropBorderWidth_, edge, dropColor_);
    }

    void DockPanel::DropIndicator::ApplyTheme(const Theme& t) {
        dropColor_ = t.dockPanel.dropColor;
        dropBorderWidth_ = t.dockPanel.dropBorderWidth;
    }

    DockPanel::DockPanel() {
        AddChild(DropIndicator());

        ApplyTheme(Themes::Light());
    }

    DockPanel::~DockPanel() {
        if (hostDock_) hostDock_->floatDock_ = nullptr;
        if (floatDock_) floatDock_->hostDock_ = nullptr;
    }

    DockPanel::DockPane* DockPanel::paneOf(const Widget* content) const {
        if (!content) return nullptr;
        for (DockPane* p : panes_) {
            if (p && p->content() == content) return p;
        }
        return nullptr;
    }

    Widget* DockPanel::paneAt(std::size_t index) const {
        if (index >= panes_.size()) return nullptr;
        return panes_[index] ? panes_[index]->content() : nullptr;
    }

    const std::string& DockPanel::paneTitleAt(std::size_t index) const {
        static const std::string kEmpty;
        if (index >= panes_.size() || !panes_[index]) return kEmpty;
        return panes_[index]->title();
    }

    void DockPanel::SetPaneTitle(Widget& content, std::string title) {
        if (DockPane* p = paneOf(&content)) {
            p->SetTitle(std::move(title));
            RequestWindowRedraw();
        }
        RequestWindowRedraw();
    }

    const std::string& DockPanel::paneTitle(Widget& content) const {
        static const std::string kEmpty;
        if (DockPane* p = paneOf(&content)) return p->title();
        return kEmpty;
    }

    int DockPanel::indexOf(const Widget& content) const {
        for (std::size_t i = 0; i < panes_.size(); ++i) {
            if (panes_[i] && panes_[i]->content() == &content) return static_cast<int>(i);
        }
        return -1;
    }

    bool DockPanel::RemovePane(Widget& content) {
        DockPane* pane = paneOf(&content);
        if (!pane) return false;

        const int leaf = findLeaf(pane);
        if (leaf >= 0) {
            nodes_[leaf].pane = nullptr;
            compact();
        }
        for (std::size_t i = 0; i < panes_.size(); ++i) {
            if (panes_[i] == pane) { panes_.erase(panes_.begin() + static_cast<std::ptrdiff_t>(i)); break; }
        }
        DestroyWidget(*pane);

        Invalidate();
        notifyDockChanged();
        return true;
    }

    void DockPanel::ClearPanes() {
        for (DockPane* p : panes_) {
            if (p) DestroyWidget(*p);
        }
        panes_.clear();
        nodes_.clear();
        root_ = -1;
        Invalidate();
        notifyDockChanged();
    }

    void DockPanel::HidePane(Widget& content) {
        DockPane* p = paneOf(&content);
        if (!p || !p->isVisible()) return;
        p->SetVisible(false);
        Invalidate();
    }

    void DockPanel::ShowPane(Widget& content) {
        DockPane* p = paneOf(&content);
        if (!p || p->isVisible()) return;
        p->SetVisible(true);
        Invalidate();
    }

    bool DockPanel::isPaneVisible(const Widget& content) const {
        const DockPane* p = paneOf(&content);
        return p && p->isVisible();
    }

    int DockPanel::newLeaf(DockPane* pane) {
        Node n;
        n.leaf = true;
        n.pane = pane;
        nodes_.push_back(n);
        return static_cast<int>(nodes_.size()) - 1;
    }

    int DockPanel::newSplit(bool horizontal, int first, int second, float ratio) {
        Node n;
        n.leaf = false;
        n.horizontal = horizontal;
        n.first = first;
        n.second = second;
        n.ratio = ratio;
        nodes_.push_back(n);
        return static_cast<int>(nodes_.size()) - 1;
    }

    int DockPanel::findLeaf(const DockPane* pane) const {
        for (std::size_t i = 0; i < nodes_.size(); ++i) {
            if (nodes_[i].leaf && nodes_[i].pane == pane) return static_cast<int>(i);
        }
        return -1;
    }

    bool DockPanel::subtreeVisible(int idx) const {
        if (idx < 0 || idx >= static_cast<int>(nodes_.size())) return false;
        const Node& n = nodes_[idx];
        if (n.leaf) return n.pane && n.pane->isVisible();
        return subtreeVisible(n.first) || subtreeVisible(n.second);
    }

    void DockPanel::compact() {
        std::vector<Node> out;
        out.reserve(nodes_.size());

        std::function<int(int)> visit = [&](int idx) -> int {
            if (idx < 0 || idx >= static_cast<int>(nodes_.size())) return -1;
            Node n = nodes_[idx];
            if (n.leaf) {
                if (!n.pane) return -1;
                n.first = n.second = -1;
                out.push_back(n);
                return static_cast<int>(out.size()) - 1;
            }
            const int a = visit(n.first);
            const int b = visit(n.second);
            if (a < 0 && b < 0) return -1;
            if (a < 0) return b;
            if (b < 0) return a;
            n.first = a;
            n.second = b;
            out.push_back(n);
            return static_cast<int>(out.size()) - 1;
        };

        root_ = visit(root_);
        nodes_.swap(out);
    }

    void DockPanel::attachPane(DockPane* pane, DockSide side) {
        if (!pane) return;
        const bool empty = (root_ < 0);
        if (!empty && side == DockSide::Center) side = DockSide::Right;

        const int leaf = newLeaf(pane);
        if (empty) {
            root_ = leaf;
            Invalidate();
            notifyDockChanged();
            return;
        }

        const bool horizontal = (side == DockSide::Left || side == DockSide::Right);
        const bool newFirst = (side == DockSide::Left || side == DockSide::Top);
        const int idx = newSplit(horizontal,
            newFirst ? leaf : root_,
            newFirst ? root_ : leaf,
            0.5f);

        nodes_[idx].pending = true;
        nodes_[idx].pendingIsFirst = newFirst;
        nodes_[idx].pendingPx = defaultDockSize_;

        root_ = idx;
        Invalidate();
        notifyDockChanged();
    }

    void DockPanel::notifyDockChanged() {
        if (onDockChanged_) onDockChanged_(*this);
    }

    bool DockPanel::Dock(Widget& content, DockSide side) {
        DockPane* pane = paneOf(&content);
        if (!pane) return false;
        if (panes_.size() <= 1) return false;

        const int leaf = findLeaf(pane);
        if (leaf >= 0) {
            nodes_[leaf].pane = nullptr;
            compact();
        }
        pane->SetVisible(true);
        attachPane(pane, side);
        return true;
    }

    bool DockPanel::Dock(Widget& content, Widget& target, DockSide side) {
        DockPane* a = paneOf(&content);
        DockPane* b = paneOf(&target);
        if (!a || !b || a == b) return false;
        return restructure(a, b, side);
    }

    bool DockPanel::restructure(DockPane* dragged, DockPane* target, DockSide side) {
        const int aLeaf = findLeaf(dragged);
        const int bLeaf = findLeaf(target);
        if (aLeaf < 0 || bLeaf < 0) return false;

        dragged->SetVisible(true);

        if (side == DockSide::Center) {
            std::swap(nodes_[aLeaf].pane, nodes_[bLeaf].pane);
            Invalidate();
            notifyDockChanged();
            return true;
        }

        nodes_[aLeaf].pane = nullptr;
        compact();

        const int ti = findLeaf(target);
        if (ti < 0) {
            attachPane(dragged, DockSide::Center);
            return true;
        }

        const bool horizontal = (side == DockSide::Left || side == DockSide::Right);
        const bool draggedFirst = (side == DockSide::Left || side == DockSide::Top);

        DockPane* targetPane = nodes_[ti].pane;
        const int targetLeaf = newLeaf(targetPane);
        const int draggedLeaf = newLeaf(dragged);

        Node split;
        split.leaf = false;
        split.horizontal = horizontal;
        split.ratio = 0.5f;
        split.first = draggedFirst ? draggedLeaf : targetLeaf;
        split.second = draggedFirst ? targetLeaf : draggedLeaf;
        nodes_[ti] = split;

        Invalidate();
        notifyDockChanged();
        return true;
    }

    float DockPanel::hintNode(int idx, bool alongWidth) const {
        if (idx < 0 || idx >= static_cast<int>(nodes_.size())) return 0.0f;
        const Node& n = nodes_[idx];
        if (n.leaf) {
            if (!n.pane || !n.pane->isVisible()) return 0.0f;
            return alongWidth ? n.pane->hintWidth() : n.pane->hintHeight();
        }
        const float a = hintNode(n.first, alongWidth);
        const float b = hintNode(n.second, alongWidth);
        if (n.horizontal == alongWidth) return a + splitterW_ + b;
        return std::max(a, b);
    }

    float DockPanel::hintWidth() const {
        if (prefWidth() > 0.0f) return prefWidth();
        if (root_ < 0) return 0.0f;
        return hintNode(root_, true) + padding().left + padding().right;
    }

    float DockPanel::hintHeight() const {
        if (prefHeight() > 0.0f) return prefHeight();
        if (root_ < 0) return 0.0f;
        return hintNode(root_, false) + padding().top + padding().bottom;
    }

    void DockPanel::layoutNode(int idx, float x, float y, float w, float h) {
        if (idx < 0 || idx >= static_cast<int>(nodes_.size())) return;
        Node& n = nodes_[idx];

        n.aw = w;
        n.ah = h;

        if (n.leaf) {
            n.cx = x; n.cy = y; n.cw = w; n.ch = h;
            if (n.pane) {
                n.pane->SetPosition(x, y);
                LayoutAccess::SetActual(*n.pane, std::max(0.0f, w), std::max(0.0f, h));
            }
            return;
        }

        const bool visA = subtreeVisible(n.first);
        const bool visB = subtreeVisible(n.second);

        if (!visA || !visB) {
            n.hx = n.hy = n.hw = n.hh = 0.0f;
            layoutNode(visA ? n.first : n.second, x, y, w, h);
            return;
        }

        const float axisLen = n.horizontal ? w : h;
        const float avail = std::max(0.0f, axisLen - splitterW_);

        if (n.pending) {
            n.pending = false;
            const float minAxis = n.horizontal ? minPaneW_ : minPaneH_;
            const float px = std::clamp(n.pendingPx, 0.0f,
                std::max(0.0f, avail - minAxis));
            const float firstLen = n.pendingIsFirst ? px : (avail - px);
            n.ratio = (avail > 0.0f) ? (firstLen / avail) : 0.5f;
        }

        const float minAxis = n.horizontal ? minPaneW_ : minPaneH_;
        const float lo = std::min(minAxis, avail);
        const float hi = std::max(lo, avail - minAxis);
        const float firstLen = std::clamp(n.ratio * avail, lo, hi);
        const float secondLen = std::max(0.0f, avail - firstLen);

        if (n.horizontal) {
            n.hx = x + firstLen; n.hy = y; n.hw = splitterW_; n.hh = h;
            layoutNode(n.first, x, y, firstLen, h);
            layoutNode(n.second, x + firstLen + splitterW_, y, secondLen, h);
        }
        else {
            n.hx = x; n.hy = y + firstLen; n.hw = w; n.hh = splitterW_;
            layoutNode(n.first, x, y, w, firstLen);
            layoutNode(n.second, x, y + firstLen + splitterW_, w, secondLen);
        }
    }

    void DockPanel::DoLayout() {
        const std::vector<Widget*> kids = children();
        for (Widget* c : kids) {
            if (c) LayoutAccess::RestorePref(*c);
        }

        const float cx = contentLeft();
        const float cy = contentTop();
        const float cw = contentAvailableW();
        const float ch = contentAvailableH();

        if (root_ < 0) {
            for (DockPane* p : panes_) {
                if (!p) continue;
                p->SetPosition(cx, cy);
                LayoutAccess::SetActual(*p, 0.0f, 0.0f);
            }
            return;
        }
        layoutNode(root_, cx, cy, cw, ch);
    }

    void DockPanel::Paint(FrameCollector& fc) const {
        const float px = x(), py = y(), pw = width(), ph = height();
        if (pw <= 0.0f || ph <= 0.0f) return;

        drawutil::PaintBox(fc, px, py, pw, ph, containerRadius_,
            containerBorderW_, containerBorder_, containerBg_);

        const float autoThick = std::clamp(splitterW_ * 0.45f, 2.0f, 4.0f);
        for (std::size_t i = 0; i < nodes_.size(); ++i) {
            const Node& n = nodes_[i];
            if (n.leaf || n.hw <= 0.0f || n.hh <= 0.0f) continue;

            const bool active = (splitDragging_ && splitNode_ == static_cast<int>(i));
            const bool hover = (!splitDragging_ && hoverSplitNode_ == static_cast<int>(i));
            if (!active && !hover && !handleAlwaysVisible_) continue;

            Color4f c = (active || hover) ? handleActiveColor_ : handleColor_;
            if (!active && hover) c.a *= 0.55f;
            if (c.a <= 0.0f) continue;

            const float base = (handleThickness_ > 0.0f) ? handleThickness_ : autoThick;
            const float thick = std::max(1.0f,
                std::min(base * (active ? 1.25f : 1.0f), std::min(n.hw, n.hh)));
            const float rr = std::min(handleRadius_, thick * 0.5f);

            if (n.horizontal) {
                const float inset = std::min(n.hh * 0.5f - thick, 8.0f);
                const float gx = n.hx + (n.hw - thick) * 0.5f;
                const float gh = std::max(thick, n.hh - std::max(0.0f, inset) * 2.0f);
                drawutil::PaintRoundedRect(fc, gx, n.hy + std::max(0.0f, inset),
                    thick, gh, rr, c);
            } else {
                const float inset = std::min(n.hw * 0.5f - thick, 8.0f);
                const float gy = n.hy + (n.hh - thick) * 0.5f;
                const float gw = std::max(thick, n.hw - std::max(0.0f, inset) * 2.0f);
                drawutil::PaintRoundedRect(fc, n.hx + std::max(0.0f, inset),
                    gy, gw, thick, rr, c);
            }
        }

        if (floatRoot_) {
            constexpr float kGrip = 13.0f;
            const float gx = px + pw - 4.5f;
            const float gy = py + ph - 4.5f;
            Color4f gc = handleActiveColor_;
            if (gc.a <= 0.0f) gc = handleColor_;
            gc.a *= 1.5f;
            for (int i = 0; i < 3; ++i) {
                const float o = 4.5f * static_cast<float>(i);
                drawutil::PaintThickSegment(fc,
                    gx - kGrip + o, gy, gx, gy - kGrip + o, 1.6f, gc);
            }
        }
    }

    void DockPanel::ApplyTheme(const Theme& t) {
        style_ = t.dockPanel;
        const DockPanelStyle& s = style_;

        containerBg_ = s.background;
        containerBorder_ = s.borderColor;
        containerBorderW_ = s.borderWidth;
        containerRadius_ = s.radius;
        handleColor_ = s.handleColor;
        handleActiveColor_ = s.handleActiveColor;
        handleRadius_ = s.handleRadius;

        if (!titleBarSet_)
            titleBarHeight_ = (s.titleBarHeight > 0.0f) ? s.titleBarHeight : kFallbackTitleH;
        if (!minPaneSet_) {
            minPaneW_ = (s.minPaneWidth > 0.0f) ? s.minPaneWidth : kFallbackMinW;
            minPaneH_ = (s.minPaneHeight > 0.0f) ? s.minPaneHeight : kFallbackMinH;
        }

        if (!paneRadiusSet_) paneRadius_ = std::max(0.0f, s.paneRadius);
        if (!paneBorderSet_) {
            paneBorderW_ = std::max(0.0f, s.paneBorderWidth);
            paneBorderC_ = s.paneBorderColor;
        }
        if (!handleThicknessSet_) handleThickness_ = std::max(0.0f, s.handleThickness);
        if (!handleVisibleSet_) handleAlwaysVisible_ = s.handleAlwaysVisible;

        const std::vector<Widget*> kids = children();
        for (Widget* c : kids) {
            if (c) c->ApplyTheme(t);
        }

        Invalidate();
    }

    void DockPanel::SetTitleBarHeight(float h) {
        titleBarSet_ = true;
        titleBarHeight_ = std::max(0.0f, h);
        for (DockPane* p : panes_) {
            if (p) p->setStyle(style_, titleBarHeight_);
        }
        Invalidate();
        RequestWindowRedraw();
    }

    void DockPanel::SetSplitterWidth(float w) {
        splitterWSet_ = true;
        splitterW_ = std::max(0.0f, w);
        Invalidate();
        RequestWindowRedraw();
    }

    void DockPanel::SetPaneMinSize(float minW, float minH) {
        minPaneSet_ = true;
        minPaneW_ = std::max(0.0f, minW);
        minPaneH_ = std::max(0.0f, minH);
        Invalidate();
        RequestWindowRedraw();
    }

    void DockPanel::SetDefaultDockSize(float px) {
        dockSizeSet_ = true;
        defaultDockSize_ = std::max(0.0f, px);
        RequestWindowRedraw();
    }

    void DockPanel::SetPaneRadius(float r) {
        paneRadiusSet_ = true;
        paneRadius_ = std::max(0.0f, r);
        for (DockPane* p : panes_) {
            if (p) p->setStyle(style_, titleBarHeight_);
        }
        RequestWindowRedraw();
    }

    void DockPanel::SetPaneBorder(float width, const Color4f& color) {
        paneBorderSet_ = true;
        paneBorderW_ = std::max(0.0f, width);
        paneBorderC_ = color;
        for (DockPane* p : panes_) {
            if (p) p->setStyle(style_, titleBarHeight_);
        }
        RequestWindowRedraw();
    }

    void DockPanel::SetHandleAlwaysVisible(bool on) {
        handleVisibleSet_ = true;
        handleAlwaysVisible_ = on;
        RequestWindowRedraw();
    }

    void DockPanel::SetHandleThickness(float px) {
        handleThicknessSet_ = true;
        handleThickness_ = std::max(0.0f, px);
        RequestWindowRedraw();
    }

    int DockPanel::hitSplitter(float px, float py, float* rx, float* ry,
        float* rw, float* rh) const {
        for (std::size_t i = 0; i < nodes_.size(); ++i) {
            const Node& n = nodes_[i];
            if (n.leaf || n.hw <= 0.0f || n.hh <= 0.0f) continue;
            if (px >= n.hx - kHitSlop && px < n.hx + n.hw + kHitSlop &&
                py >= n.hy - kHitSlop && py < n.hy + n.hh + kHitSlop) {
                if (rx) *rx = n.hx;
                if (ry) *ry = n.hy;
                if (rw) *rw = n.hw;
                if (rh) *rh = n.hh;
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    void DockPanel::applySplitterDrag(float px, float py) {
        if (splitNode_ < 0 || splitNode_ >= static_cast<int>(nodes_.size())) return;
        Node& n = nodes_[splitNode_];
        if (n.leaf) return;

        const float cur = n.horizontal ? px : py;
        const float ownLen = n.horizontal ? n.aw : n.ah;
        const float axisLen = (ownLen > 0.0f) ? ownLen : (n.horizontal ? width() : height());
        const float avail = std::max(1.0f, axisLen - splitterW_);

        float ratio = splitStartRatio_ + (cur - splitStartPos_) / avail;
        n.ratio = std::clamp(ratio, 0.0f, 1.0f);
        Invalidate();
    }

    void DockPanel::beginPaneDrag(DockPane& pane, float px, float py) {
        if (!dragEnabled_) return;

        if (floatRoot_) {
            floatDrag_ = true;
            dragGrabX_ = px;
            dragGrabY_ = py;
            int sx = 0, sy = 0;
            if (floatWin_) floatWin_->GetClientOrigin(sx, sy);
            floatLastScreenX_ = static_cast<float>(sx) + px;
            floatLastScreenY_ = static_cast<float>(sy) + py;
            return;
        }

        dragging_ = true;
        dragPane_ = &pane;
        dragStartX_ = px;
        dragStartY_ = py;
        dropNode_ = -1;
        dropTargetContent_ = nullptr;
        dropSide_ = DockSide::Center;
        RequestWindowRedraw();
    }

    bool DockPanel::computeDropTarget(float px, float py, int& nodeIdx, DockSide& side) const {
        nodeIdx = -1;
        side = DockSide::Center;

        if (px < x() || px >= x() + width() || py < y() || py >= y() + height()) return false;

        int found = -1;
        for (std::size_t i = 0; i < nodes_.size(); ++i) {
            const Node& n = nodes_[i];
            if (!n.leaf || !n.pane || n.cw <= 0.0f || n.ch <= 0.0f) continue;
            if (n.pane == dragPane_) continue;
            if (px >= n.cx && px < n.cx + n.cw && py >= n.cy && py < n.cy + n.ch) {
                found = static_cast<int>(i);
                break;
            }
        }
        if (found < 0) return false;

        const Node& n = nodes_[found];
        const float u = (n.cw > 0.0f) ? ((px - n.cx) / n.cw) : 0.5f;
        const float v = (n.ch > 0.0f) ? ((py - n.cy) / n.ch) : 0.5f;

        if (u > 0.25f && u < 0.75f && v > 0.25f && v < 0.75f) {
            nodeIdx = found;
            side = DockSide::Center;
            return true;
        }

        const float dl = u, dr = 1.0f - u, dt = v, db = 1.0f - v;
        float best = dl;
        DockSide s = DockSide::Left;
        if (dr < best) { best = dr; s = DockSide::Right; }
        if (dt < best) { best = dt; s = DockSide::Top; }
        if (db < best) { best = db; s = DockSide::Bottom; }

        nodeIdx = found;
        side = s;
        return true;
    }

    void DockPanel::dropRectOf(int nodeIdx, DockSide side,
        float& x, float& y, float& w, float& h) const {
        x = y = w = h = 0.0f;
        if (nodeIdx < 0 || nodeIdx >= static_cast<int>(nodes_.size())) return;
        const Node& n = nodes_[nodeIdx];
        if (!n.leaf) return;

        switch (side) {
        case DockSide::Left:   x = n.cx;                y = n.cy;                w = n.cw * 0.5f; h = n.ch; break;
        case DockSide::Right:  x = n.cx + n.cw * 0.5f;  y = n.cy;                w = n.cw * 0.5f; h = n.ch; break;
        case DockSide::Top:    x = n.cx;                y = n.cy;                w = n.cw;        h = n.ch * 0.5f; break;
        case DockSide::Bottom: x = n.cx;                y = n.cy + n.ch * 0.5f;  w = n.cw;        h = n.ch * 0.5f; break;
        case DockSide::Center:
        default:               x = n.cx;                y = n.cy;                w = n.cw;        h = n.ch; break;
        }
    }

    bool DockPanel::dropRect(float& x, float& y, float& w, float& h) const {
        if (extPreviewOn_ && extNode_ >= 0) {
            dropRectOf(extNode_, extSide_, x, y, w, h);
            return w > 0.0f && h > 0.0f;
        }
        if (!dragging_ || dropNode_ < 0) return false;
        dropRectOf(dropNode_, dropSide_, x, y, w, h);
        return w > 0.0f && h > 0.0f;
    }

    void DockPanel::updatePaneDrag(float px, float py) {
        if (floatRoot_) { floatTitleDragMove(px, py); return; }
        if (!dragging_ || !dragPane_) return;
        if (tornFollow_) { floatFollowTo(px, py); return; }

        int idx = -1;
        DockSide side = DockSide::Center;
        const bool ok = computeDropTarget(px, py, idx, side);

        if (!ok && floatEnabled_ && outsideHostWindow(px, py, 8.0f)) {
            tryTearOut(px, py);
            return;
        }

        const int newIdx = ok ? idx : -1;
        if (newIdx == dropNode_ && (!ok || side == dropSide_)) return;

        dropNode_ = newIdx;
        dropSide_ = ok ? side : DockSide::Center;
        dropTargetContent_ = (ok && nodes_[idx].pane) ? nodes_[idx].pane->content() : nullptr;
        RequestWindowRedraw();
    }

    void DockPanel::finishPaneDrag() {
        if (floatRoot_) { finishFloatDrag(); return; }

        if (!dragging_) return;
        dragging_ = false;

        if (tornFollow_) {
            tornFollow_ = false;
            clearExternalPreview();
            if (tornPane_) {
                for (std::size_t i = 0; i < panes_.size(); ++i) {
                    if (panes_[i] == tornPane_) {
                        panes_.erase(panes_.begin() + static_cast<std::ptrdiff_t>(i));
                        break;
                    }
                }
                DestroyWidget(*tornPane_);
                tornPane_ = nullptr;
                Invalidate();
                notifyDockChanged();
            }
            if (floatDock_) {
                DockPanel* fd = floatDock_;
                floatDock_ = nullptr;
                fd->finishFloatDrag();
            }
            RequestWindowRedraw();
            return;
        }

        DockPane* dragged = dragPane_;
        const int targetIdx = dropNode_;
        const DockSide side = dropSide_;
        dragPane_ = nullptr;
        dropNode_ = -1;
        dropTargetContent_ = nullptr;
        dropSide_ = DockSide::Center;

        if (!dragged) return;
        dragged->setTitleActive(false);
        RequestWindowRedraw();

        if (targetIdx < 0 || targetIdx >= static_cast<int>(nodes_.size())) return;
        DockPane* target = nodes_[targetIdx].pane;
        if (!target || target == dragged) return;

        restructure(dragged, target, side);
    }

    bool DockPanel::outsideHostWindow(float px, float py, float margin) const {
        Window* w = window();
        if (!w) return false;
        return px < -margin || py < -margin ||
            px > w->width() + margin || py > w->height() + margin;
    }

    void DockPanel::detachPaneFrame(DockPane* pane) {
        const int leaf = findLeaf(pane);
        if (leaf >= 0) {
            nodes_[leaf].pane = nullptr;
            compact();
        }
        for (std::size_t i = 0; i < panes_.size(); ++i) {
            if (panes_[i] == pane) {
                panes_.erase(panes_.begin() + static_cast<std::ptrdiff_t>(i));
                break;
            }
        }
        Invalidate();
    }

    void DockPanel::tryTearOut(float px, float py) {
        Window* host = window();
        DockPane* pane = dragPane_;
        if (!host || !pane || !pane->content_) return;
        if (!host->Supports(Capability::ChildWindow)) return;

        float pw = 300.0f, ph = 200.0f;
        if (const int leaf = findLeaf(pane); leaf >= 0) {
            pw = std::max(pw, nodes_[leaf].cw);
            ph = std::max(ph, nodes_[leaf].ch);
        }
        const std::string title = pane->title_;

        std::unique_ptr<Widget> p = pane->releaseChild(*pane->content_);
        if (!p) return;
        pane->content_ = nullptr;

        detachPaneFrame(pane);
        pane->SetVisible(false);
        tornPane_ = pane;
        dragPane_ = nullptr;

        const int fww = static_cast<int>(pw);
        const int fhh = static_cast<int>(ph);
        const int fx = static_cast<int>(px - pw * 0.35f);
        const int fy = static_cast<int>(py - titleBarHeight_ * 0.5f);
        Window& fw = host->CreateChildWindow(fx, fy, fww, fhh, ChildKind::Owned,
            true);
        if (fw.IsClosed()) {
            DockPane& np = AddChild(DockPane(title));
            Widget& ref = np.adoptChild(std::move(p));
            np.setContent(ref);
            panes_.push_back(&np);
            attachPane(&np, DockSide::Center);
            tornPane_ = nullptr;
            Invalidate();
            notifyDockChanged();
            return;
        }
        DockPanel& fd = fw.AddWidget(DockPanel());
        fd.floatRoot_ = true;
        fd.hostDock_ = this;
        fd.floatWin_ = &fw;

        DockPane& np = fd.AddChild(DockPane(title));
        Widget& ref = np.adoptChild(std::move(p));
        np.setContent(ref);
        fd.panes_.push_back(&np);
        fd.attachPane(&np, DockSide::Center);
        fw.SetTheme(host->theme());
        fw.Show();

        floatDock_ = &fd;
        tornFollow_ = true;
        lastHostX_ = px;
        lastHostY_ = py;
        followDx_ = pw * 0.35f;
        followDy_ = titleBarHeight_ * 0.5f;
        RequestWindowRedraw();
    }

    void DockPanel::floatFollowTo(float px, float py) {
        lastHostX_ = px;
        lastHostY_ = py;
        if (!floatWin_) return;
        if (!outsideHostWindow(px, py, 0.0f))
            previewExternalDrop(px, py);
        else
            clearExternalPreview();
        floatWin_->SetPositionRelativeToHost(
            static_cast<int>(px - followDx_), static_cast<int>(py - followDy_));
    }

    void DockPanel::previewExternalDrop(float px, float py) {
        int idx = -1;
        DockSide side = DockSide::Center;
        const bool ok = computeDropTarget(px, py, idx, side);
        if (!ok && !extPreviewOn_) return;
        if (ok && extPreviewOn_ && idx == extNode_ && side == extSide_) return;
        extPreviewOn_ = ok;
        extNode_ = ok ? idx : -1;
        extSide_ = side;
        RequestWindowRedraw();
    }

    void DockPanel::clearExternalPreview() {
        if (!extPreviewOn_) return;
        extPreviewOn_ = false;
        extNode_ = -1;
        RequestWindowRedraw();
    }

    bool DockPanel::adoptTornPane(const std::string& title, std::unique_ptr<Widget> content,
        float px, float py) {
        if (!content) return false;

        int idx = -1;
        DockSide side = DockSide::Center;
        computeDropTarget(px, py, idx, side);

        DockPane& pane = AddChild(DockPane(title));
        Widget& ref = pane.adoptChild(std::move(content));
        pane.setContent(ref);
        panes_.push_back(&pane);
        attachPane(&pane, DockSide::Center);
        if (idx >= 0 && idx < static_cast<int>(nodes_.size()) &&
            nodes_[idx].pane && nodes_[idx].pane != &pane)
            restructure(&pane, nodes_[idx].pane, side);

        Invalidate();
        notifyDockChanged();
        return true;
    }

    void DockPanel::floatTitleDragMove(float px, float py) {
        if (!floatDrag_) return;
        if (!floatWin_) return;

        int ox = 0, oy = 0;
        floatWin_->GetClientOrigin(ox, oy);
        const float screenX = static_cast<float>(ox) + px;
        const float screenY = static_cast<float>(oy) + py;
        floatLastScreenX_ = screenX;
        floatLastScreenY_ = screenY;

        const int nx = static_cast<int>(screenX - dragGrabX_);
        const int ny = static_cast<int>(screenY - dragGrabY_);
        floatWin_->SetPosition(nx, ny);

        if (hostDock_ && hostDock_->window()) {
            int hox = 0, hoy = 0;
            hostDock_->window()->GetClientOrigin(hox, hoy);
            hostDock_->previewExternalDrop(screenX - static_cast<float>(hox),
                screenY - static_cast<float>(hoy));
        }
    }

    void DockPanel::finishFloatDrag() {
        const bool fromFloatTitle = floatDrag_;
        floatDrag_ = false;
        if (!hostDock_ || !floatWin_ || panes_.empty()) return;
        Window* hw = hostDock_->window();
        if (!hw) return;

        float hx = 0.0f, hy = 0.0f;
        if (fromFloatTitle) {
            int hox = 0, hoy = 0;
            hw->GetClientOrigin(hox, hoy);
            hx = floatLastScreenX_ - static_cast<float>(hox);
            hy = floatLastScreenY_ - static_cast<float>(hoy);
        }
        else {
            hx = hostDock_->lastHostX_;
            hy = hostDock_->lastHostY_;
        }

        hostDock_->clearExternalPreview();

        if (hx < 0.0f || hy < 0.0f ||
            hx >= static_cast<float>(hw->width()) || hy >= static_cast<float>(hw->height()))
            return;

        DockPane* pane = panes_.front();
        if (!pane || !pane->content_) return;
        const std::string title = pane->title_;
        std::unique_ptr<Widget> p = pane->releaseChild(*pane->content_);
        pane->content_ = nullptr;
        if (!p) return;

        hostDock_->adoptTornPane(title, std::move(p), hx, hy);
        hostDock_->floatDock_ = nullptr;
        floatWin_->Close();
    }

    bool DockPanel::resizeZoneAt(float px, float py, unsigned& mask) const {
        mask = 0;
        if (!floatRoot_) return false;
        constexpr float kEdge = 6.0f;
        if (px > width() - kEdge) mask |= 1u << 1;
        else if (px < kEdge) mask |= 1u << 0;
        if (py > height() - kEdge) mask |= 1u << 2;
        return mask != 0;
    }

    void DockPanel::applyResizeDrag(float px, float py) {
        if (!resizing_ || !floatWin_) return;

        int ox = 0, oy = 0;
        floatWin_->GetClientOrigin(ox, oy);
        const float dx = (static_cast<float>(ox) + px) - rsGrabScreenX_;
        const float dy = (static_cast<float>(oy) + py) - rsGrabScreenY_;
        constexpr float kMinW = 220.0f, kMinH = 140.0f;

        float nx = rsWinX_, ny = rsWinY_, nw = rsWinW_, nh = rsWinH_;
        if (resizeMask_ & (1u << 1)) nw = std::max(kMinW, rsWinW_ + dx);
        if (resizeMask_ & (1u << 0)) {
            nw = std::max(kMinW, rsWinW_ - dx);
            nx = rsWinX_ + (rsWinW_ - nw);
        }
        if (resizeMask_ & (1u << 2)) nh = std::max(kMinH, rsWinH_ + dy);
        floatWin_->SetPosition(static_cast<int>(nx), static_cast<int>(ny));
        floatWin_->SetSize(static_cast<int>(nw), static_cast<int>(nh));
    }

    void DockPanel::requestPaneClose(DockPane& pane) {
        Widget* content = pane.content();

        if (floatRoot_ && floatWin_) {
            if (hostDock_) hostDock_->floatDock_ = nullptr;
            floatWin_->Close();
            return;
        }

        if (onPaneCloseRequest_) {
            onPaneCloseRequest_(*this, content);
            return;
        }
        if (content) HidePane(*content);
    }

    bool DockPanel::OnMousePress(MouseEvent& e) {
        if (e.button != MouseButton::Left) return false;

        if (floatRoot_ && resizeZoneAt(e.x, e.y, resizeMask_)) {
            resizing_ = true;
            if (floatWin_) {
                int sx = 0, sy = 0;
                floatWin_->GetClientOrigin(sx, sy);
                rsWinX_ = static_cast<float>(sx);
                rsWinY_ = static_cast<float>(sy);
                rsWinW_ = static_cast<float>(floatWin_->width());
                rsWinH_ = static_cast<float>(floatWin_->height());
                rsGrabScreenX_ = rsWinX_ + e.x;
                rsGrabScreenY_ = rsWinY_ + e.y;
            }
            e.accept();
            return true;
        }

        const int idx = hitSplitter(e.x, e.y);
        if (idx >= 0) {
            splitDragging_ = true;
            splitNode_ = idx;
            splitStartPos_ = nodes_[idx].horizontal ? e.x : e.y;
            splitStartRatio_ = nodes_[idx].ratio;
            RequestWindowRedraw();
            e.accept();
            return true;
        }
        return false;
    }

    bool DockPanel::OnMouseMove(MouseEvent& e) {
        if (resizing_) {
            applyResizeDrag(e.x, e.y);
            e.accept();
            return true;
        }
        if (splitDragging_) {
            applySplitterDrag(e.x, e.y);
            e.accept();
            return true;
        }
        if (dragging_) {
            updatePaneDrag(e.x, e.y);
            e.accept();
            return true;
        }
        const int idx = hitSplitter(e.x, e.y);
        if (idx != hoverSplitNode_) {
            hoverSplitNode_ = idx;
            RequestWindowRedraw();
            return idx >= 0;
        }
        return false;
    }

    bool DockPanel::OnMouseRelease(MouseEvent& e) {
        if (resizing_) {
            resizing_ = false;
            resizeMask_ = 0;
            e.accept();
            return true;
        }
        if (splitDragging_) {
            splitDragging_ = false;
            splitNode_ = -1;
            hoverSplitNode_ = hitSplitter(e.x, e.y);
            RequestWindowRedraw();
            e.accept();
            return true;
        }
        return false;
    }

    void DockPanel::OnMouseLeave() {
        if (hoverSplitNode_ < 0) return;
        hoverSplitNode_ = -1;
        RequestWindowRedraw();
    }

    Cursor DockPanel::cursorAt(float x, float y) const {
        if (splitDragging_ && splitNode_ >= 0
            && splitNode_ < static_cast<int>(nodes_.size())) {
            const Node& n = nodes_[splitNode_];
            if (!n.leaf) return n.horizontal ? Cursor::SizeWE : Cursor::SizeNS;
        }
        if (resizing_) {
            const bool lr = (resizeMask_ & 0x3u) != 0u;
            return lr ? Cursor::SizeWE : Cursor::SizeNS;
        }
        const int idx = hitSplitter(x, y);
        if (idx >= 0 && !nodes_[idx].leaf)
            return nodes_[idx].horizontal ? Cursor::SizeWE : Cursor::SizeNS;
        return Cursor::Arrow;
    }

}
