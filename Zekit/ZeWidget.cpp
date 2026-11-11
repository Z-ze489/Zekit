#include "pch.h"

#include <algorithm>
#include <cstdio>
#include <set>
#include <string>

#if defined(_WIN32)
#  include <windows.h>
#endif

#include "include/ZeWidget.h"
#include "include/ZeTooltip.h"
#include "include/Zekit.h"
#include "ZePaintCache.h"

namespace Kit {

    Widget::Widget(Widget&& other) noexcept
        : x_(other.x_), y_(other.y_), w_(other.w_), h_(other.h_)
        , prefW_(other.prefW_), prefH_(other.prefH_)
        , minW_(other.minW_), minH_(other.minH_)
        , maxW_(other.maxW_), maxH_(other.maxH_)
        , defaultW_(other.defaultW_)
        , defaultH_(other.defaultH_)
        , hPolicy_(other.hPolicy_)
        , vPolicy_(other.vPolicy_)
        , visible_(other.visible_)
        , fillWindow_(other.fillWindow_)
        , nativeSurface_(other.nativeSurface_)
        , cursor_(other.cursor_)
        , objectName_(std::move(other.objectName_))
        , tooltipText_(std::move(other.tooltipText_))
        , tooltipTitle_(std::move(other.tooltipTitle_))
        , tooltipIcon_(std::move(other.tooltipIcon_))
        , focusable_(other.focusable_)
        , disabled_(other.disabled_)
        , styled_(other.styled_)
        , margin_(other.margin_)
        , flexGrow_(other.flexGrow_)
        , alignSelf_(other.alignSelf_)
        , hasAlignSelf_(other.hasAlignSelf_)
        , nextFilterId_(other.nextFilterId_)
        , acceptFileDrop_(other.acceptFileDrop_)
        , onFileDrop_(std::move(other.onFileDrop_))
{
        children_ = std::move(other.children_);
        other.children_.clear();
        for (Widget* c : children_)
            c->parent_ = this;

        owned_ = std::move(other.owned_);
        other.owned_.clear();

        filters_ = std::move(other.filters_);
        other.filters_.clear();
        for (FilterSlot& s : filters_)
            if (s.filter) s.filter->relinkWidget(&other, this);
        other.nextFilterId_ = 1;

        other.parent_ = nullptr;
        other.owner_  = nullptr;

        focused_ = false;

        other.movedFrom_ = true;
    }

    void Widget::diagnoseMovedFrom(const char* what) const {
#if !defined(NDEBUG)
        static std::set<std::string> reported;
        std::string key = std::string(typeid(*this).name()) + "::" + what;
        if (!reported.insert(key).second) return;

        char buf[1024];
        std::snprintf(buf, sizeof(buf),
            "[Zekit] 访问了已移动的控件：%s::%s()\n"
            "        这个对象是**移动构造的源对象**（值已被搬进别处），它是个空壳。\n"
            "        最常见的成因：在控件挂载前捕获了它的引用 ——\n"
            "            a.SetOnClick([&a]{ a.Text(); });  // &a 会在 return a 时失效\n"
            "        正解：用统一协议的回调写法，self 由库在分发时注入、永远指向树里实体：\n"
            "            a.SetOnClick([](Kit::Widget& self){ Kit::As<Kit::Button>(self).Text(); });\n"
            "            // 发件人恒为 Widget&，具体接口用 Kit::As<T> 取回 —— 见 ZeSelfAction.h\n",
            typeid(*this).name(), what);

#if defined(_WIN32)
        OutputDebugStringA(buf);
#endif
        std::fputs(buf, stderr);
        std::fflush(stderr);
#else
        (void)what;
#endif
    }

    void Widget::SetTooltip(std::string text) {
        tooltipTitle_.clear();
        tooltipIcon_.clear();
        tooltipText_ = std::move(text);
        RequestWindowRedraw();
    }

    void Widget::SetTooltip(const TooltipContent& content) {
        tooltipTitle_ = content.title;
        tooltipText_ = content.text;
        tooltipIcon_ = content.icon;
        RequestWindowRedraw();
    }

    void Widget::ClearTooltip() {
        tooltipText_.clear();
        tooltipTitle_.clear();
        tooltipIcon_.clear();
        RequestWindowRedraw();
    }

    void Widget::SetEnabled(bool enabled) {
        if (isMovedFrom()) diagnoseMovedFrom("SetEnabled");
        if (disabled_ == !enabled) return;
        disabled_ = !enabled;

        if (disabled_) clearFocus();

        RequestWindowRedraw();
    }

    Widget::~Widget() {
        delete paintCache_;
        paintCache_ = nullptr;
        if (parent_) parent_->RemoveWidget(*this);
        if (owner_)  owner_->RemoveWidget(*this);

        for (const FilterSlot& slot : filters_) {
            if (slot.filter) slot.filter->forgetInstall(this);
        }
        filters_.clear();
    }

    void Widget::setFocused(bool focused) {
        if (focused_ == focused) return;

        focused_ = focused;
        if (focused_) OnFocusIn();
        else          OnFocusOut();
    }

    Window* Widget::window() const {
        for (const Widget* w = this; w; w = w->parent_) {
            if (w->owner_) return w->owner_;
        }
        return nullptr;
    }

    void Widget::CloseWindow()     { if (Window* w = window()) w->Close(); }    void Widget::ShowWindow()      { if (Window* w = window()) w->Show(); }
    void Widget::HideWindow()      { if (Window* w = window()) w->Hide(); }
    void Widget::MinimizeWindow()  { if (Window* w = window()) w->Minimize(); }
    void Widget::MaximizeWindow()  { if (Window* w = window()) w->Maximize(); }
    void Widget::RestoreWindow()   { if (Window* w = window()) w->Restore(); }

    void Widget::SetWindowTitle(const std::string& title) {
        if (Window* w = window()) w->SetTitle(title);
    }
    void Widget::SetWindowIcon(const std::string& path) {
        if (Window* w = window()) w->SetWindowIcon(path);
    }
    void Widget::SetWindowIconFromResource(int resId) {
        if (Window* w = window()) w->SetWindowIconFromResource(resId);
    }

    void Widget::SetWindowSize(int w, int h) {
        if (Window* win = window()) win->SetSize(w, h);
    }
    void Widget::SetWindowPosition(int x, int y) {
        if (Window* win = window()) win->SetPosition(x, y);
    }
    void Widget::SetWindowMinimumSize(int w, int h) {
        if (Window* win = window()) win->SetMinimumSize(w, h);
    }
    void Widget::SetWindowMaximumSize(int w, int h) {
        if (Window* win = window()) win->SetMaximumSize(w, h);
    }
    void Widget::GetWindowSize(int& w, int& h) const {
        if (Window* win = window()) win->GetClientSize(w, h);
        else { w = 0; h = 0; }
    }
    int  Widget::GetWindowWidth() const  { return window() ? window()->width() : 0; }
    int  Widget::GetWindowHeight() const { return window() ? window()->height() : 0; }
    void Widget::AdjustWindowSize()      { if (Window* w = window()) w->adjustSize(); }
    void Widget::CenterWindowOnScreen()  { if (Window* w = window()) w->CenterOnScreen(); }

    void Widget::SetWindowResizable(bool on) {
        if (Window* w = window()) w->SetResizable(on);
    }
    void Widget::SetWindowBorderless(bool on) {
        if (Window* w = window()) w->SetBorderless(on);
    }
    void Widget::SetWindowBorderlessResizable(bool on) {
        if (Window* w = window()) w->SetBorderlessResizable(on);
    }
    void Widget::SetWindowTitleBarVisible(bool visible) {
        if (Window* w = window()) w->SetTitleBarVisible(visible);
    }
    void Widget::SetWindowTopmost(bool on) {
        if (Window* w = window()) w->SetTopmost(on);
    }
    void Widget::SetWindowFullscreen(bool on) {
        if (Window* w = window()) w->SetFullscreen(on);
    }
    bool Widget::IsWindowFullscreen() const {
        return window() ? window()->IsFullscreen() : false;
    }
    void Widget::SetWindowRoundedCorners(bool on) {
        if (Window* w = window()) w->SetRoundedCorners(on);
    }
    bool Widget::IsWindowRoundedCorners() const {
        return window() ? window()->IsRoundedCorners() : false;
    }
    void Widget::StartWindowDrag() {
        if (Window* w = window()) w->StartWindowDrag();
    }

    void Widget::RequestWindowRedraw() const {
        paintDirty_ = true;
        if (Window* w = window()) w->requestFrameOnly(this);
    }

    void Widget::InvalidatePaint() const {
        paintDirty_ = true;
    }

    void Widget::noteHoverResync() const {
        if (Window* w = window()) w->requestHoverResync();
    }

    PaintFragment* PaintCacheAccess::Get(const Widget& w) {
        return w.paintCache_;
    }

    PaintFragment& PaintCacheAccess::Ensure(Widget& w) {
        if (!w.paintCache_) w.paintCache_ = new PaintFragment();
        return *w.paintCache_;
    }

    bool PaintCacheAccess::IsDirty(const Widget& w) { return w.paintDirty_; }
    void PaintCacheAccess::MarkDirty(const Widget& w) { w.paintDirty_ = true; }
    void PaintCacheAccess::ClearDirty(const Widget& w) { w.paintDirty_ = false; }
    bool PaintCacheAccess::Timed(const Widget& w) { return w.paintTimed_; }
    void PaintCacheAccess::SetTimed(const Widget& w, bool v) { w.paintTimed_ = v; }

    void Widget::requestFocus() {
        if (Window* w = window()) w->setFocusedWidget(this);
    }

    void Widget::clearFocus() {
        if (!focused_) return;
        if (Window* w = window()) w->setFocusedWidget(nullptr);
    }

    Widget::FilterId Widget::InstallEventFilter(EventFilter& filter) {
        for (const FilterSlot& slot : filters_) {
            if (slot.filter == &filter) return slot.id;
        }

        FilterId id = allocFilterId();
        filters_.push_back(FilterSlot{ &filter, id });
        filter.addInstall(this, id);
        return id;
    }

    void Widget::RemoveEventFilter(FilterId id) {
        if (id == 0) return;
        detachEventFilter(id);
    }

    void Widget::detachEventFilter(FilterId id) {
        auto& v = filters_;
        for (auto it = v.begin(); it != v.end(); ++it) {
            if (it->id != id) continue;

            if (it->filter) it->filter->forgetInstall(this);
            v.erase(it);
            return;
        }
    }

    bool Widget::runMouseFilters(Window& win, MouseEvent& e) {
        bool blocked = false;
        for (const FilterSlot& slot : filters_) {
            if (slot.filter && slot.filter->onMouse(win, e)) blocked = true;
        }
        return blocked;
    }

    bool Widget::runKeyFilters(Window& win, KeyEvent& e) {
        bool blocked = false;
        for (const FilterSlot& slot : filters_) {
            if (slot.filter && slot.filter->onKey(win, e)) blocked = true;
        }
        return blocked;
    }

    void Widget::RemoveWidget(Widget& child) {
        auto& v = children_;
        v.erase(std::remove(v.begin(), v.end(), &child), v.end());
        if (child.parent_ == this) child.parent_ = nullptr;
    }

    bool Widget::DestroyWidget(Widget& child) {
        bool ownedHere = false;
        for (const std::unique_ptr<Widget>& p : owned_) {
            if (p.get() == &child) { ownedHere = true; break; }
        }
        if (!ownedHere) return false;

        destroyChild(child);
        return true;
    }

    void Widget::ClearChildren() {
        if (children_.empty() && owned_.empty()) return;

        if (Window* win = window()) {
            for (Widget* c : children_) win->RemoveWidget(*c);
        }

        children_.clear();
        owned_.clear();

        notifyLayoutInputChanged();
        RequestWindowRedraw();
    }

    std::unique_ptr<Widget> Widget::releaseChild(Widget& child) {
        auto& owned = owned_;
        for (auto it = owned.begin(); it != owned.end(); ++it) {
            if (it->get() != &child) continue;

            if (Window* win = window()) {
                for (Widget* f = win->focusedWidget(); f; f = f->parent()) {
                    if (f == &child) { win->setFocusedWidget(nullptr); break; }
                }
            }
            children_.erase(std::remove(children_.begin(), children_.end(), &child),
                children_.end());
            if (child.parent_ == this) child.parent_ = nullptr;
            std::unique_ptr<Widget> p = std::move(*it);
            owned.erase(it);
            return p;
        }
        return nullptr;
    }

    Widget& Widget::adoptChild(std::unique_ptr<Widget> p) {
        Widget& ref = *p;
        ref.parent_ = this;
        children_.push_back(&ref);
        owned_.push_back(std::move(p));
        return ref;
    }

    Widget* Widget::findChildByName(const std::string& name) {
        if (name.empty()) return nullptr;
        for (Widget* c : children_) {
            if (c && c->hasObjectName(name)) return c;
        }
        return nullptr;
    }

    void Widget::findAllByName(const std::string& name, std::vector<Widget*>& out) {
        if (name.empty()) return;
        for (Widget* c : children_) {
            if (!c) continue;
            if (c->hasObjectName(name)) out.push_back(c);
            c->findAllByName(name, out);
        }
    }

    Widget* Widget::findDeepByName(const std::string& name) {
        if (name.empty()) return nullptr;
        for (Widget* c : children_) {
            if (!c) continue;
            if (c->hasObjectName(name)) return c;
            if (Widget* hit = c->findDeepByName(name)) return hit;
        }
        return nullptr;
    }

    void Widget::visitTreeRaw(void (*fn)(Widget&, int, void*), void* ctx, int depth) {
        fn(*this, depth, ctx);
        for (Widget* c : children_) {
            if (!c) continue;
            c->visitTreeRaw(fn, ctx, depth + 1);
        }
    }

    void Widget::reportMisplacedChild(const char* hostType, const char* childType) {
        static std::set<std::string> reported;
        const std::string key = std::string(hostType ? hostType : "?") + " <- " +
                                std::string(childType ? childType : "?");
        if (!reported.insert(key).second) return;

        std::string msg = "[Zekit] 布局警告：";
        msg += (hostType ? hostType : "?");
        msg += " 不是布局容器（Widget 派生），却装入了子控件 ";
        msg += (childType ? childType : "?");
        msg += "\n"
               "         → 该子项不会被排版：几何停在 0×0，界面上看不到它（编译器不会报错）。\n"
               "         → 基类请换成 Kit::Panel（推荐），或 LayoutBase 一支的布局容器：\n"
               "           Kit::VBoxLayout / Kit::HBoxLayout / Kit::FlowLayout / Kit::GridLayout / Kit::ScrollView。\n"
               "         → 若本控件确实自己管排版，这条提示可忽略（用 AddWidgetUnchecked 表达该意图）。\n";

#if defined(_WIN32)
        OutputDebugStringA(msg.c_str());
#endif
        std::fputs(msg.c_str(), stderr);
        std::fflush(stderr);
    }

    void Widget::destroyChild(Widget& child) {
        RemoveWidget(child);

        OnLayoutInputChanged();
        RequestWindowRedraw();

        if (Window* win = window()) win->RemoveWidget(child);

        auto& owned = owned_;
        for (auto it = owned.begin(); it != owned.end(); ++it) {
            if (it->get() == &child) {
                owned.erase(it);
                return;
            }
        }
    }

    bool Widget::HitTest(float x, float y) const {
        return x >= x_ && x < x_ + w_ && y >= y_ && y < y_ + h_;
    }

    void Widget::SetFillWindow(bool on) {
        fillWindow_ = on;
    }

    void Widget::notifyLayoutInputChanged() {
        if (parent_) parent_->OnLayoutInputChanged();
    }

    void Widget::SetPosition(float x, float y) {
        const float ox = x_, oy = y_;
        x_ = x;
        y_ = y;
        if (x_ == ox && y_ == oy) return;
        notifyLayoutInputChanged();
    }

    void Widget::SetSize(float width, float height) {
        const float ow = w_, oh = h_;
        w_ = width;
        h_ = height;
        clampSize();
        prefW_ = w_;
        prefH_ = h_;
        if (w_ == ow && h_ == oh) return;
        notifyLayoutInputChanged();
    }

    void Widget::SetGeometry(float x, float y, float width, float height) {
        x_ = x;
        y_ = y;
        SetSize(width, height);
    }

    void Widget::SetX(float x) {
        if (x_ == x) return;
        x_ = x;
        notifyLayoutInputChanged();
    }

    void Widget::SetY(float y) {
        if (y_ == y) return;
        y_ = y;
        notifyLayoutInputChanged();
    }

    void Widget::SetWidth(float width) {
        const float ow = w_;
        w_ = width;
        clampSize();
        prefW_ = w_;
        if (w_ == ow) return;
        notifyLayoutInputChanged();
    }

    void Widget::SetHeight(float height) {
        const float oh = h_;
        h_ = height;
        clampSize();
        prefH_ = h_;
        if (h_ == oh) return;
        notifyLayoutInputChanged();
    }

    float Widget::x() const { return x_; }
    float Widget::y() const { return y_; }
    float Widget::width() const { return w_; }
    float Widget::height() const { return h_; }

    float Widget::prefWidth() const {
        float v = prefW_;
        if (v < minW_) v = minW_;
        if (v > maxW_) v = maxW_;
        return v;
    }

    float Widget::prefHeight() const {
        float v = prefH_;
        if (v < minH_) v = minH_;
        if (v > maxH_) v = maxH_;
        return v;
    }

    bool Widget::AutoSize() {
        if (prefW_ > 0.0f && prefH_ > 0.0f) return false;

        const float ow = w_, oh = h_;

        if (prefW_ > 0.0f) {
            w_ = prefW_;
        } else {
            const float hw = hintWidth();
            w_ = (hw > 0.0f) ? hw : defaultW_;
        }
        if (prefH_ > 0.0f) {
            h_ = prefH_;
        } else {
            const float hh = hintHeight();
            h_ = (hh > 0.0f) ? hh : defaultH_;
        }
        clampSize();

        if (w_ == ow && h_ == oh) return false;

        notifyLayoutInputChanged();
        RequestWindowRedraw();
        return true;
    }

    void Widget::clampSize() {
        if (w_ < minW_) w_ = minW_;
        if (w_ > maxW_) w_ = maxW_;
        if (h_ < minH_) h_ = minH_;
        if (h_ > maxH_) h_ = maxH_;
    }

    void Widget::restorePrefSize() {
        const float pw = prefW_ > 0.0f ? prefW_
            : (hintWidth()  > 0.0f ? hintWidth()  : defaultW_);
        const float ph = prefH_ > 0.0f ? prefH_
            : (hintHeight() > 0.0f ? hintHeight() : defaultH_);
        w_ = pw;
        h_ = ph;
        clampSize();
    }

    void Widget::setActualSize(float w, float h) {
        w_ = w;
        h_ = h;
        clampSize();
    }

    void Widget::SetMinSize(float minWidth, float minHeight) {
        const float nw = (minWidth  > 0.0f) ? minWidth  : 0.0f;
        const float nh = (minHeight > 0.0f) ? minHeight : 0.0f;
        if (minW_ == nw && minH_ == nh) return;

        minW_ = nw;
        minH_ = nh;

        if (maxW_ < minW_) maxW_ = minW_;
        if (maxH_ < minH_) maxH_ = minH_;

        clampSize();
        notifyLayoutInputChanged();
    }

    void Widget::SetMaxSize(float maxWidth, float maxHeight) {
        const float nw = (maxWidth  > 0.0f) ? maxWidth  : FLT_MAX;
        const float nh = (maxHeight > 0.0f) ? maxHeight : FLT_MAX;
        if (maxW_ == nw && maxH_ == nh) return;

        maxW_ = nw;
        maxH_ = nh;

        if (minW_ > maxW_) minW_ = maxW_;
        if (minH_ > maxH_) minH_ = maxH_;

        clampSize();
        notifyLayoutInputChanged();
    }

    void Widget::SetMinWidth(float w) {
        const float v = (w > 0.0f) ? w : 0.0f;
        if (minW_ == v) return;
        minW_ = v;
        if (maxW_ < minW_) maxW_ = minW_;
        clampSize();
        notifyLayoutInputChanged();
    }

    void Widget::SetMinHeight(float h) {
        const float v = (h > 0.0f) ? h : 0.0f;
        if (minH_ == v) return;
        minH_ = v;
        if (maxH_ < minH_) maxH_ = minH_;
        clampSize();
        notifyLayoutInputChanged();
    }

    void Widget::SetMaxWidth(float w) {
        const float v = (w > 0.0f) ? w : FLT_MAX;
        if (maxW_ == v) return;
        maxW_ = v;
        if (minW_ > maxW_) minW_ = maxW_;
        clampSize();
        notifyLayoutInputChanged();
    }

    void Widget::SetMaxHeight(float h) {
        const float v = (h > 0.0f) ? h : FLT_MAX;
        if (maxH_ == v) return;
        maxH_ = v;
        if (minH_ > maxH_) minH_ = maxH_;
        clampSize();
        notifyLayoutInputChanged();
    }

    float Widget::minWidth() const { return minW_; }
    float Widget::maxWidth() const { return maxW_; }
    float Widget::minHeight() const { return minH_; }
    float Widget::maxHeight() const { return maxH_; }

    void Widget::SetMargin(float left, float top, float right, float bottom) {
        if (margin_.left == left && margin_.top == top &&
            margin_.right == right && margin_.bottom == bottom) return;
        margin_ = { left, top, right, bottom };
        notifyLayoutInputChanged();
    }

    void Widget::SetMargin(float horizontal, float vertical) {
        if (margin_.left == horizontal && margin_.top == vertical &&
            margin_.right == horizontal && margin_.bottom == vertical) return;
        margin_ = { horizontal, vertical, horizontal, vertical };
        notifyLayoutInputChanged();
    }

    void Widget::SetFlexGrow(float grow) {
        const float v = (grow > 0.0f) ? grow : 0.0f;
        if (flexGrow_ == v) return;
        flexGrow_ = v;
        notifyLayoutInputChanged();
    }

    void Widget::SetAlignSelf(AlignItems a) {
        if (hasAlignSelf_ && alignSelf_ == a) return;
        alignSelf_ = a;
        hasAlignSelf_ = true;
        notifyLayoutInputChanged();
    }

    void Widget::ClearAlignSelf() {
        if (!hasAlignSelf_) return;

        alignSelf_ = AlignItems::Stretch;
        hasAlignSelf_ = false;
        notifyLayoutInputChanged();
    }

    void Widget::SetAcceptFileDrop(bool on) {
        if (acceptFileDrop_ == on) return;
        acceptFileDrop_ = on;
        if (on) {
            if (Window* w = window()) w->SetAcceptFileDrop(true);
        }
        RequestWindowRedraw();
    }

}
