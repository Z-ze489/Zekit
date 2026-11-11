#pragma once

#include <cassert>      // Type check for Kit::As<T>() in debug builds
#include <cfloat>
#include <cstdint>
#include <functional>   // std::function (file-drop callbacks / widget On* callbacks)
#include <memory>
#include <string>
#include <type_traits>
#include <typeinfo>     // typeid(...) - reports real host/child class names on misplacement
#include <utility>
#include <vector>

#include "color4f.h"        // Core::Color4f: plain value type for widget colors (no glm)
#include "ZeInputEvents.h"  // MouseEvent / KeyEvent (parameter types of event virtuals)
#include "ZeFlexStyle.h"    // AlignItems and other enums (for the child alignSelf property)
#include "ZeCursorKind.h"   // Kit::Cursor (mouse cursor declaration, see SetCursor below)

namespace Kit {
    class Window;
    class FrameCollector;
    struct Theme;   // Theme (ZeTheme.h) - only appears in the ApplyTheme signature; forward declaration suffices
    struct PaintFragment;   // Frame paint fragment cache (ZePaintCache.h, internal)

    // Four-side outer margin (how far a child wants to be from its siblings)
    struct Margin {
        float left = 0.0f, top = 0.0f, right = 0.0f, bottom = 0.0f;

        // Sets one value per axis (equivalent to left=right=h, top=bottom=v)
        static Margin Sym(float horizontal, float vertical) {
            return { horizontal, vertical, horizontal, vertical };
        }
    };

    // Size policy: how a widget "wants to be treated" along an axis.
    // A hint alone is ambiguous: a Label reporting 200 wide means "at most 200,
    // do not stretch me"; an HBoxLayout reporting 200 means "at least 200, give
    // me surplus space". SizePolicy adds that meaning so containers know
    // whether to stretch the child. Defined at namespace level so callers can
    // write Kit::SizePolicy::Expanding directly; Widget::SizePolicy is an
    // in-class alias and both spellings work.
    enum class SizePolicy {
        Fixed,     // Pinned to the hint, never stretched (cards, icons)
        Preferred, // Wants the hint, but may be stretched when space allows (default)
        Expanding  // Takes as much remaining space as possible (container content area, scroll view axis)
    };

    // =======================================================================
    //  Widget base class
    // =======================================================================

    struct TooltipContent;   // Tooltip content (ZeTooltip.h) - forward declaration only,
                             // avoids the ZeWidget -> ZeTooltip -> ZeTextWidget -> ZeWidget cycle

    class Widget {
    public:
        Widget() = default;

        // Unregisters itself from its parent widget and owning window on
        // destruction. Defined in ZeWidget.cpp (which sees the full Window /
        // Widget definitions). This lets widgets outlive windows in any
        // destruction order.
        virtual ~Widget();

        // Move constructor: moves in a NOT-YET-ATTACHED widget (with its whole
        // subtree and filters). Implemented in ZeWidget.cpp; fixes child
        // parent_ pointers and filter bookkeeping so the tree is intact at
        // the new location. Only use on widgets not yet attached to a
        // window/parent (exactly what AddWidget does internally).
        Widget(Widget&& other) noexcept;

        // Whether this object has been moved away from ("empty shell" state).
        // Move construction marks the source movedFrom_ = true. Purely a
        // diagnostic: it is the only runtime clue when a developer captures a
        // reference to a widget that then gets moved (the callback then talks
        // to an empty shell). Accessors that detect a shell report a named
        // diagnostic in debug builds via diagnoseMovedFrom. No checks in
        // release (NDEBUG) - zero cost. This is a safety net, not the
        // intended path; the intended path is the SelfAction protocol (see
        // ZeSelfAction.h).
        bool isMovedFrom() const { return movedFrom_; }

        // Copy disabled: widgets are movable values but not copyable (tree
        // relations cannot be shallow-copied). Assignment disabled too: the
        // address is fixed once attached, and assignment would break that.
        Widget(const Widget&) = delete;
        Widget& operator=(const Widget&) = delete;
        Widget& operator=(Widget&&) = delete;

        // -------------------------------------------------------------------
        //  Painting
        // -------------------------------------------------------------------
        // Appends "what I look like" to this frame's collector (the one
        // interface every subclass must implement). fc = per-frame collector,
        // created and passed in by Window each frame. Must be const and must
        // not touch GL: widgets only describe; Window handles vertices,
        // textures, VAOs.
        virtual void Paint(FrameCollector& fc) const = 0;

        // Whether this widget wants a clip region for its children (layout
        // containers return true). x/y/w/h = clip rect in absolute client
        // coordinates (meaningless when returning false). Returning true makes
        // Window Push/PopClip around the children during recursive rendering.
        // Containers override it with their visible content area so scrolled /
        // out-of-bounds children are clipped; ordinary widgets return false.
        // This is key to "overlay escape": Window pushes clip regions on
        // demand during recursive rendering, and an overlay widget calls
        // fc.ResetClip() before describing its popup panel to leave the clip.
        virtual bool clipChildrenRect(float& x, float& y, float& w, float& h) const {
            (void)x; (void)y; (void)w; (void)h;
            return false;
        }

        // Whether this widget arranges its children (layout containers true,
        // leaf widgets false). True means added children get geometry written
        // (the LayoutBase branch). It answers "does anyone manage children
        // once added", not "can children be stored" - Widget can already store
        // children and paint them recursively; only DoLayout is missing.
        // Used for runtime diagnostics: addWidgetValue reports a one-shot hint
        // when a non-arranging widget holds children (reportMisplacedChild).
        // The compile-time interception is AddWidget's [[deprecated]]; the two
        // complement each other (the compiler cannot catch AddWidgetUnchecked
        // or prebuilt external binaries). LayoutBase overrides to true; when
        // adding a new layout container, override it too (or derive from
        // LayoutBase, which already does).
        virtual bool arrangesChildren() const { return false; }

        // Whether this widget still needs the "next frame" (single source of
        // truth for continuous self-drawn animation: progress slides, fades,
        // live waveforms...). Kit::Window asks the whole tree once per frame
        // in DriveMode::OnDemand: any true keeps scheduling frames at a
        // uniform rate; all false sleeps back to the kernel at zero CPU.
        // Division of labor with Window::RequestFrameWake (both coexist):
        // continuous animation overrides this; discrete timers (e.g. flipping
        // the cursor every 530ms) still use RequestFrameWake. Default false:
        // not overriding means permanently static at zero cost. Only answer
        // for yourself; the host recurses over children (asking children here
        // would become O(n^2)). Must be const (queried in the render phase,
        // same phase as Paint).
        virtual bool wantsAnimationFrame() const { return false; }

        // Whether this widget paints in the "topmost overlay phase" (overlay
        // widgets return true). True defers this widget until after all
        // ordinary widgets this frame; false paints in the normal order.
        // Solves the z-order of overlays (dropdown panels, tooltip bubbles,
        // context menus): rendering is a single pre-order + topmost-list
        // pass, so a widget nested in a layout can only paint at its own
        // position - later siblings cover it, and it cannot paint a popup
        // above them. Window therefore renders in two passes: first skips
        // (and records) widgets whose paintsOnTop() is true, then paints each
        // recorded widget's whole subtree. Overlays always end up above all
        // ordinary widgets; UIs without overlays are unaffected (empty second
        // pass).
        //
        // Must be paired with hit testing (Kit::HitTest is overlay-first: it
        // searches overlay subtrees first). Overriding only this function and
        // not HitTest is a common mistake: the panel is drawn but clicks hit
        // the widget underneath. Only answer for yourself; a returning-true
        // widget need not be top-level (dropdowns are deeply nested - the
        // second pass paints their whole subtree). Typical pattern is
        // on-demand: return open_; when closed it is an ordinary widget and
        // costs nothing. Must be const (queried in the render phase, same
        // phase as Paint). See Kit::HitTest::Topmost and ZeComboBox.h.
        virtual bool paintsOnTop() const { return false; }

        // Whether this widget holds a native surface (a real HWND outside the
        // Kit rendering pipeline; canvas / video layer / map layer). True
        // means the widget manages that window's lifetime and geometry
        // itself. Default false. Kit::CanvasView calls markNativeSurface() in
        // its constructor, so the library knows it draws "someone else's
        // window", not primitives.
        //
        // Why this bit is needed: native windows do not participate in the
        // Kit pipeline - they have their own z-order and bypass PushClip /
        // PaintCache / viewport culling. So "where is it, how big, should it
        // show" cannot be computed in Paint(): Paint is skipped by fragment
        // cache hits, viewport culling, and zero-size early-outs. Each skip
        // is correct for drawing but means "never synced again" for a native
        // window (symptoms seen in practice: canvas pinned on screen after
        // scrolling out, window alive at size 0, shadow stuck). The library
        // therefore calls syncNativeSurface at the very front of paintTree
        // (before visibility, culling, and caching) for hasNativeSurface()
        // nodes - that is geometry sync, not drawing, independent of whether
        // the frame paints it.
        //
        // A plain bool read, no virtual call - no extra indirect jump per
        // node even at ten-thousand-widget scale (same idea as the
        // children().empty() guard).
        bool hasNativeSurface() const { return nativeSurface_; }

        // Syncs this widget's geometry to its native surface (library
        // internal; called once per frame by paintTree). Only widgets with
        // hasNativeSurface() receive it. Implementations should do three
        // things idempotently: hide the native window when invisible or
        // zero-size; when visible, intersect ancestor clip regions along
        // parent() and position the window at the intersection; hide it when
        // the intersection is empty (scrolled out of view). Division of labor
        // with Paint: Paint only draws the placeholder "before the window
        // appears"; all geometry sync happens here - doing both would
        // desync if Paint is skipped ("placeholder scrolled, window did
        // not"). Called every frame, so implementations must debounce: do
        // not touch platform APIs when position/size are unchanged.
        virtual void syncNativeSurface() {}


        // ===================================================================
        //  File drag-and-drop (widget-level drop target)
        // ===================================================================
        //  Division of labor with window-level Window::SetOnFileDrop:
        //    - Widget level: only widgets that declared acceptance receive
        //      it, with enter/over/leave phases so a "light up while dragging"
        //      highlight can be drawn. This is how drop zones should be built.
        //    - Window level: fallback when the drop lands on empty window
        //      space (no drop-accepting widget was hit).
        //
        //  Hit testing picks the innermost accepting widget, so in nested drop
        //  zones the inner one wins (consistent with overlay-first hit rules).
        //  When the platform lacks support (Capability::FileDrop false) these
        //  callbacks never fire, but registering is not an error - consistent
        //  with the library's other capability degradations.
        // ===================================================================

        // Declares that this widget accepts external file drops.
        // on = true becomes a drop target; false leaves (idempotent).
        // Opening also opens the owning window's drop channel (the platform
        // layer needs it to attach IDropTarget), so business code needs only
        // this call, not win.SetAcceptFileDrop(true). The widget's geometry is
        // used for drop hit testing, so attach it to the tree first (if not
        // attached, only the state is recorded; the window re-opens the
        // channel once attached).
        void SetAcceptFileDrop(bool on = true);
        // Whether this widget accepts external file drops
        bool acceptsFileDrop() const { return acceptFileDrop_; }

        // Files dragged into this widget (first enter).
        // x,y = client coordinates (drop pointer position); files = absolute
        // paths (UTF-8, at least one). Return whether this batch of files is
        // accepted (false = rejected: the system shows the forbidden cursor
        // and no Over/Drop is dispatched). Typical uses: set the drag-over
        // state and RequestWindowRedraw() for a highlight; filter by
        // extension/count by returning false (how FileDropZone whitelists).
        // Defaults to true: widgets that called SetAcceptFileDrop() but do not
        // implement Enter still receive files (long-standing behavior).
        // Drops do not bubble and the target is unique: on rejection Kit asks
        // the next acceptor along the parent chain, falling back to the
        // window-level callback only if none accepts.
        virtual bool OnFileDragEnter(float x, float y, const std::vector<std::string>& files) {
            (void)x; (void)y; (void)files;
            return true;
        }

        // Files moving over this widget (high frequency: once per pointer
        // move). Do not do heavy work here; to move a highlight box, update
        // coordinates and request a redraw. This phase carries no file list
        // (the platform layer deliberately avoids re-copying) - use the list
        // from Enter.
        virtual bool OnFileDragOver(float x, float y) { (void)x; (void)y; return false; }

        // Files dragged out of this widget (or the drag was cancelled).
        // Must clear any visual state set in Enter, or the highlight stays on.
        virtual void OnFileDragLeave() {}

        // Files dropped onto this widget (user released).
        // x,y = client coordinates; files = absolute paths (UTF-8, at least
        // one). Returns true if handled. OnFileDragLeave always fires first
        // (drop-target state wraps up there), so clearing drag-over visuals
        // here is unnecessary but harmless.
        virtual bool OnFileDrop(float x, float y, const std::vector<std::string>& files) {
            (void)x; (void)y;
            if (onFileDrop_) { onFileDrop_(files); return true; }
            return false;
        }

        // Derivation-free drop callback (same mental model as
        // Button::SetOnClick). cb receives the dropped file paths; empty clears.
        // Only used when the subclass does NOT override OnFileDrop (the
        // default implementation calls it). For enter/leave visual feedback,
        // override the virtuals (or use FileDropZone).
        void SetOnFileDrop(std::function<void(const std::vector<std::string>&)> cb) {
            onFileDrop_ = std::move(cb);
        }

        // ===================================================================
        //  Mouse cursor (widget-level declaration - Window pushes it to the
        //  platform layer when the hit target changes)
        // ===================================================================
        //  Division of labor: the widget only DECLARES "what cursor to show
        //  here" and never touches platform APIs; the real SetCursor
        //  (LoadCursor...) is done by the platform layer - Window translates
        //  the hit widget's cursor to Plat::IWindow::SetMouseCursor after hit
        //  testing on mouse move (see dispatchMouseMove in Zekit.cpp), so the
        //  widget layer stays cross-platform.
        //
        //  Why it lives on Widget (not BoxWidget): the cursor is widget-level
        //  semantics, unrelated to how a box looks. Text widgets want an
        //  I-beam, splitters want resize arrows - and they do not all derive
        //  from BoxWidget (Panel and the LayoutBase branch need it too).
        //  Historical note: it used to live on BoxStyle::cursor, a field
        //  nobody read, making SetCursor a silently dead API.
        // ===================================================================

        // Declares "what cursor to show while the mouse hovers this widget"
        // (default Cursor::Arrow). Declaration only: the cursor actually
        // changes after the next mouse-move hit test. When the widget is
        // disabled (SetEnabled(false)), Window falls back to the default
        // arrow - disabled widgets should not suggest interaction.
        void SetCursor(Cursor c) { cursor_ = c;  RequestWindowRedraw();}
        // The cursor this widget declares
        Cursor cursor() const { return cursor_; }

        // What cursor to show when the mouse is at this client-area point
        // (default = cursor()). SetCursor declares one cursor for the whole
        // widget, but some widgets are interactive only locally: Splitter's
        // ~10px bar should be a double arrow while the I-beam over the panels
        // reverts to an arrow; future table column borders and scrollbar
        // edges are the same. So Window calls this function (with
        // coordinates) after hit testing instead of reading cursor(). The
        // default ignores coordinates and returns cursor(), so existing
        // widgets are unchanged. x,y = absolute client coordinates (same set
        // as MouseEvent::x/y). During a drag, Window uses the pressed widget
        // (pointer capture) with the current pointer position, so "the cursor
        // does not revert when dragging past the splitter" holds for free.
        virtual Cursor cursorAt(float x, float y) const { (void)x; (void)y; return cursor_; }

        // ===================================================================
        //  Hover tooltip (attach a hint in one line - the bubble is hosted by
        //  Window)
        // ===================================================================
        //  Division of labor (coexists with the manual Tooltip usage):
        //   - Widgets only remember text (this group of APIs);
        //   - Window, on pointer-position state changes, walks the parent
        //     chain for the nearest widget with a tooltip and hands it to its
        //     automatic bubble (delay/fade/edge-flip handled there).
        //
        //  Why on Widget instead of per-widget copies: a tooltip is
        //  widget-level semantics (same level as SetCursor); any widget,
        //  including third-party custom ones, should be able to attach one.
        //  Difference from Tooltip::Attach: that requires new-ing a Tooltip
        //  widget, AddWidget-ing it, and attaching targets yourself; this API
        //  is one line, suited to UIs with tooltips everywhere. Both can be
        //  used together (each manages its own bubbles).
        // ===================================================================

        // Sets the hover tooltip text ('\n' for line breaks; empty clears).
        // Pops only while the pointer rests on this widget; Window hosts the
        // bubble - no AddWidget needed. Rich content (icon/title) uses the
        // overload below.
        void SetTooltip(std::string text);

        // Sets the hover tooltip (icon + title + body).
        // content = tooltip content (see ZeTooltip.h). The parameter is a
        // reference to an incomplete type: callers need to include
        // <ZeTooltip.h>; this header does not include it back (it would form
        // a cycle with ZeTextWidget).
        void SetTooltip(const TooltipContent& content);

        // Clears the tooltip
        void ClearTooltip();

        // Current tooltip body (empty = none attached)
        const std::string& tooltipText() const { return tooltipText_; }

        // Current tooltip title (empty = no title line)
        const std::string& tooltipTitle() const { return tooltipTitle_; }

        // Current tooltip icon path (empty = no icon)
        const std::string& tooltipIcon() const { return tooltipIcon_; }

        // Whether a tooltip is attached (body/title/icon any non-empty)
        bool hasTooltip() const {
            return !tooltipText_.empty() || !tooltipTitle_.empty() || !tooltipIcon_.empty();
        }

        // ===================================================================
        //  AddWidget - value takeover: hands a widget into this widget/window
        //  (the library handles the lifetime)
        // ===================================================================
        //  Developer usage (no std::move needed; the library moves internally):
        //      void makeUi() {
        //          Kit::Button btn("Cancel");
        //          btn.SetGeometry(200, 150, 100, 50);
        //          AddWidget(btn);            // value moved into the parent; still shown after return
        //      }
        //
        //  Semantics (same mental model as push_back(std::move(x))):
        //   - The widget's value (geometry, style, subtree, filters) moves into
        //     the parent's storage;
        //   - Its lifetime belongs to the parent - the whole tree is released
        //     when the parent is destroyed;
        //   - The source variable is moved-from after handing over; do not use
        //     it again - catch the returned reference to keep working:
        //     auto& placed = AddWidget(std::move(btn));
        //   - The returned reference is stable: containers hold unique_ptr, so
        //     reallocation does not move widget addresses.
        //
        //  This entry point is [[deprecated]]: Widget does not arrange children
        //  (see the long comment below). Use Kit::Panel / layout containers to
        //  add children (they shadow this entry as a normal one); this class
        //  internally always uses addWidgetValue.
        // ===================================================================

        // Moves a widget in as a child of this widget (this widget takes over
        // its storage). w = widget value (lvalues are moved internally -
        // equivalent to writing std::move). Returns a stable reference to the
        // copy in the tree. Lifetime is library-managed: once handed over the
        // widget lives in the parent's storage until the parent dies - no
        // member variable or delete needed; function-local widgets keep
        // showing. The subtree moves wholesale (children/filters migrate with
        // it). Only use on not-yet-attached widgets (freshly built locals/
        // temporaries).
        //
        // Do NOT call this on a Widget-derived class. "Fits in" does not mean
        // "gets laid out": DoLayout() is a Kit::LayoutBase capability and
        // Widget has no such concept. In a Widget-derived "container" nothing
        // ever writes the children's geometry - they stay at their last value
        // (0x0 for new widgets), so nothing is visible and the compiler is
        // silent (an empty Paint() is legal and required). Preferred fixes:
        //   1. Base your composite control on Kit::Panel (recommended base for
        //      composites/panels: vertical stacking, auto-fill at top level,
        //      background/radius/border accessors);
        //   2. Or use a concrete layout container: Kit::VBoxLayout /
        //      Kit::HBoxLayout / Kit::FlowLayout / Kit::GridLayout /
        //      Kit::ScrollView (all on the LayoutBase branch);
        //   3. If a Widget-derived class really must hold children and lays
        //      them out itself, use AddWidgetUnchecked to state "I know what
        //      I am bypassing".
        // The diagnostic rides on [[deprecated]]: a warning (C4996) in lax
        // configs, a hard error with /sdl or /WX. The library and VS projects
        // use /sdl, so this pit stops at compile time instead of becoming a
        // blank window at runtime. See AddWidgetUnchecked, LayoutBase::AddChild,
        // Kit::Panel.
        template<class T>
        [[deprecated(
            "Kit::Widget 不排版子项：DoLayout() 是 Kit::LayoutBase 的能力，"
            "派生自 Widget 的容器里子项不会有几何（停在 0x0 → 界面一片空白，编译器不会报错）。"
            "写复合控件/容器请把基类换成 Kit::Panel（推荐）或 LayoutBase 一支的布局容器"
            "(Kit::VBoxLayout / Kit::HBoxLayout / Kit::FlowLayout / Kit::GridLayout / Kit::ScrollView)；"
            "确知自己在自管排版时才用 AddWidgetUnchecked。")]]
        T& AddWidget(T&& w) {
            return addWidgetValue(std::forward<T>(w));
        }

        // Escape hatch: this widget does not arrange children but the caller
        // explicitly wants it added (at their own risk). Same implementation
        // as AddWidget, only without the deprecation diagnostic. Gives the
        // few "Widget-derived + self-laid-out" cases an honest path: writing
        // this line declares "I know this widget is not a layout container;
        // layout is on me". Still prints a one-shot runtime diagnostic when
        // arrangesChildren() is false (prints your class name and a hint); if
        // that is not intended, switch to Kit::Panel. See AddWidget.
        template<class T>
        T& AddWidgetUnchecked(T&& w) {
            return addWidgetValue(std::forward<T>(w));
        }

        // Detaches a child widget from this widget without destroying it.
        // child = the widget itself. Called automatically on child
        // destruction; usually no need to call manually. See AddWidget,
        // children.
        void RemoveWidget(Widget& child);

        // Destroys a direct child (with its whole subtree) and releases its
        // storage immediately. child must be the copy this widget received via
        // AddWidget. Returns true if destroyed; false if not a child of this
        // widget (nothing happens). Completely different from RemoveWidget:
        // RemoveWidget only removes from the tree (not rendered, not hit
        // tested, object alive, memory held until this widget dies);
        // DestroyWidget truly destroys (storage released, subtree destructed,
        // memory freed, destructor unregisters filters as usual). To "replace
        // by name" use SetWidget (it goes through this path). Only accepts
        // children this widget owns; other widgets return false. See
        // ClearChildren, RemoveWidget, SetWidget.
        bool DestroyWidget(Widget& child);

        // Destroys ALL direct children (with their subtrees) and releases
        // storage. For "reset/rebuild container content": after clearing, new
        // widgets can be added. Repeated clear+add does not accumulate memory
        // - with RemoveWidget only, removed widgets keep their memory until
        // this widget is destroyed. Implementation clears the child list
        // first, then releases storage; the other order would make each child
        // destructor callback RemoveWidget do an O(n) list removal, degrading
        // to O(n^2). See DestroyWidget, RemoveWidget.
        void ClearChildren();

        // Takes ownership of a direct child OUT of this widget's storage
        // (transfers it to the caller). child must be a direct child added via
        // AddWidget/AddChild. Returns the handed-out subtree (caller owns its
        // fate); nullptr if not a child of this widget. Unlike RemoveWidget
        // (removed from the tree but storage kept here), this hands the
        // storage over too - the subtree can live in another container/window
        // (how a dock panel tears off into a floating window). The subtree's
        // window() resolves along the parent chain and points to the new
        // window after adoption. See adoptChild, RemoveWidget.
        std::unique_ptr<Widget> releaseChild(Widget& child);

        // Takes over an already-constructed widget subtree (attach to this
        // widget's children_ / owned_). Returns the stable in-tree reference.
        // Pairs with releaseChild: adoptChild(other.releaseChild(w)) = moving
        // across containers. See releaseChild.
        Widget& adoptChild(std::unique_ptr<Widget> p);

        // ===================================================================
        //  Addressable: find / traverse the whole subtree by objectName
        // ===================================================================
        //  Pairs with FindWidget (direct children only):
        //    - FindWidget<T>(name)     - searches this level only (fast, clear);
        //    - FindWidgetDeep<T>(name) - recurses the whole subtree (slower, but
        //      "get a widget by name" is the real developer need: a button
        //      callback wanting a grandchild label should not hand-write paths);
        //    - FindAll<T>(name)        - recursively collects ALL matches (for
        //      duplicate names).
        //
        //  Why recursive find/traverse is built into the library (instead of
        //  making developers write recursion): this lands "every widget is
        //  referenceable". The library provides the capability; what to do
        //  with a found widget (read text, dump geometry, save/restore) is up
        //  to the developer - but the library must make any widget reachable.
        // ===================================================================

        // Finds the first widget matching the objectName, recursively, in this
        // widget's subtree (depth-first, pre-order). name = target
        // objectName (empty never matches). Returns the first match's address;
        // nullptr if not found. Types are validated with dynamic_cast: a name
        // match with a wrong type keeps searching (not a total failure).
        // Search order: this level in add order, drilling into containers
        // first (pre-order) - same order as VisitTree. Includes this widget
        // itself (consistent with the Window version covering all top-level
        // widgets and their subtrees; otherwise a top-level widget directly on
        // the window could never be found). Names need not be unique; on
        // duplicates returns the first encountered - use FindAll for all.
        template<class T>
        T* FindWidgetDeep(const std::string& name) {
            if (name.empty()) return nullptr;
            return findDeep<T>(name);
        }

        // Finds ALL widgets matching the objectName, recursively (pre-order).
        // name = target objectName (empty returns an empty list). Returns
        // every widget matching both name and type (in pre-order). Duplicate
        // names are allowed (the library does not enforce uniqueness), so
        // "find all matches" is safer for batch processing - e.g. gray out
        // every widget named "row", save every input named "field".
        // FindWidgetDeep returns only the first, which can mislead. Type
        // mismatches are skipped and the search continues. See
        // FindWidgetDeep, VisitTree.
        template<class T>
        std::vector<T*> FindAll(const std::string& name) {
            std::vector<T*> out;
            if (name.empty()) return out;
            findAllInto(name, out);
            return out;
        }

        // Traverses this widget and its whole subtree in pre-order, calling f
        // once per widget. f is callable with signature void(Widget&, int
        // depth); depth = depth relative to this widget (this widget 0, direct
        // children 1, ...). Pre-order (parents before children, siblings in
        // add order) makes depth a natural indent level: a larger depth than
        // the previous item means "entering a child level", smaller means
        // "back up". Combined with ObjectName() this yields a restorable flat
        // snapshot (dump identity + geometry for saving, picking, debugging).
        // Do not add/remove widgets inside f (mutating children_ during
        // traversal invalidates iterators); collect into a vector first if
        // needed (FindAll is that pattern). f's return value is ignored; to
        // stop early use your own flag or FindWidgetDeep. See
        // FindWidgetDeep, FindAll.
        template<class F>
        void VisitTree(F&& f) {
            visitTreeImpl(f, 0);
        }

        // ===================================================================
        //  Find / replace by objectName (this container's own level)
        // ===================================================================
        //  Same shape as Window::FindWidget / Window::SetWidget, different
        //  scope: the Window versions search the top-level widget table
        //  (widgets AddWidget-ed directly onto the window); these search this
        //  widget's direct children (children_) - i.e. this container's level.
        //  Nested containers chain calls: panel.FindWidget<Label>("x") finds
        //  panel's child; to search deeper, call FindWidget on that child.
        //  For a single whole-tree search use FindWidgetDeep / FindAll /
        //  VisitTree above.
        // ===================================================================

        // Finds a direct child of this widget by objectName. name = target
        // objectName (empty never matches). Returns the first direct child
        // whose objectName == name; nullptr if not found. Type validated with
        // dynamic_cast: name match with wrong type returns nullptr. Searches
        // direct children only, no recursion into grandchildren (same shape as
        // the Window version searching only the top-level table). Names need
        // not be unique; on duplicates returns the first (in add order). See
        // SetWidget, Widget::SetObjectName.
        template<class T>
        T* FindWidget(const std::string& name) {
            Widget* w = findChildByName(name);      // Non-template bridge: loop compiled once, not per T
            return dynamic_cast<T*>(w);             // Type mismatch -> nullptr
        }

        // Replaces or adds a direct child by objectName. name = target
        // objectName (empty means unnamed, degrading to a pure AddWidget);
        // w = widget value (lvalues moved internally, like std::move). Returns
        // the stable in-tree reference. The recommended way to update container
        // content from event callbacks: the first call finds no child named
        // name and behaves like AddWidget; afterwards an existing child with
        // the same name AND type is destroyed first and the new one takes its
        // place - never stacking. The old widget (and its subtree) is released
        // from storage; its destructor unregisters filters; the new widget
        // inherits the identity. Lifetime stays with this container - no
        // delete in user code. Names are case-sensitive and type-sensitive:
        // same name with a different type does not replace (an old Label and
        // a new Button coexist). See FindWidget, AddWidget,
        // Widget::SetObjectName.
        template<class T>
        T& SetWidget(const std::string& name, T&& w) {
            static_assert(std::is_base_of_v<Widget, std::decay_t<T>>,
                "SetWidget requires a Kit::Widget-derived control");
            static_assert(!std::is_abstract_v<std::decay_t<T>>,
                "SetWidget cannot instantiate an abstract control (did you forget to implement Paint?)");
            using Plain = std::decay_t<T>;

            // Contract: name is not only the key to find the old widget, it is
            // also stamped onto the new widget. Otherwise the second
            // SetWidget's findChildByName never finds the previous one,
            // degrading to "always add, always stack" (the stacking bug users
            // hit).
            w.SetObjectName(name);

            // Same-name same-type old child -> destroy it, then add the new
            // one (replace semantics, never stacking)
            if (Widget* victim = dynamic_cast<Plain*>(findChildByName(name))) {
                // Remember its original index. Replace = "swap another widget
                // into the SAME position", never append: in a VBox/HBox
                // appending makes the replacement jump to the end (users see
                // "content moved away", not "content updated").
                std::size_t at = children_.size();
                for (std::size_t i = 0; i < children_.size(); ++i) {
                    if (children_[i] == victim) { at = i; break; }
                }

                destroyChild(*victim);                  // Old widget destroyed with its subtree (lifetime stays with this container)
                Plain& ref = addWidgetValue(std::move(w));   // New widget takes over the identity (internal entry, no deprecation)

                // addWidgetValue always push_backs; move it back into place.
                // Only children_ is reordered (paint/hit order); owned_ order
                // does not matter.
                if (at + 1 < children_.size()) {
                    children_.pop_back();
                    children_.insert(children_.begin() + static_cast<std::ptrdiff_t>(at), &ref);
                }
                return ref;
            }

            // No same-name (or same-name different-type) child -> pure add
            return addWidgetValue(std::move(w));
        }

        // This widget's parent (nullptr for top-level widgets)
        Widget* parent() const { return parent_; }

        // All children of this widget (in add order; usable for recursive
        // painting/hit testing)
        const std::vector<Widget*>& children() const { return children_; }

        // Whether this widget is top-level (attached directly to a window,
        // no parent)
        bool isTopLevel() const { return parent_ == nullptr; }

        // ===================================================================
        //  Identity (objectName - lets developers "find and update" mounted
        //  widgets by name)
        // ===================================================================
        //  Under value semantics a widget's address changes after AddWidget,
        //  so pointers cannot be held long-term. This field gives a widget a
        //  STABLE logical identity; combine with Widget::FindWidget /
        //  Widget::SetWidget (containers find their direct children) or
        //  Window::FindWidget / Window::SetWidget (windows find top-level
        //  widgets) to locate widgets in event callbacks. This avoids:
        //   - storing widgets as member variables (breaks the "no lifetime
        //     worries" promise);
        //   - re-AddWidget-ing every frame (stacking widgets, runaway memory).
        //
        //  Names need not be unique (FindWidget returns the first match); the
        //  convention is "the same logical widget always uses the same name
        //  for Set/Find".
        // ===================================================================

        // Sets this widget's objectName (for FindWidget / SetWidget lookup).
        // Empty name = no identity (same as unset). The name travels with the
        // value through AddWidget - findable after being handed over.
        void SetObjectName(std::string name) { objectName_ = std::move(name);  RequestWindowRedraw();}

        // This widget's objectName (empty = unset)
        const std::string& ObjectName() const { return objectName_; }

        // Whether this widget's objectName is exactly name (empty name never
        // matches, avoiding false hits)
        bool hasObjectName(const std::string& name) const {
            return !name.empty() && objectName_ == name;
        }

        // ===================================================================
        //  Owning window (self-sufficient - modules need no external Window)
        // ===================================================================
        // Returns the window this widget belongs to; nullptr while not
        // attached to any window. From itself: a top-level widget (no parent)
        // returns the window it was attached to at construction; non-top-level
        // widgets walk up the parent() chain to an ancestor's window.
        //
        // This is a key piece of "module self-sufficiency": when a widget
        // subclass needs the window in its own member functions (redraw
        // requests, size queries, global shortcuts), just call window() -
        // no extra Window& parameter per member function, no cached copy.
        //
        // The walk is O(tree depth), but depth is usually a few levels; called
        // only when needed, never cached.
        //
        // WARNING: always nullptr during construction - the widget is not yet
        // in the tree (parent_ / owner_ unset). So calling window() inside a
        // constructor is a null dereference (observed as 0xC0000005 reading
        // 0x0000000000000000, because Window's first member m_impl sits at
        // offset 0). For window-dependent initialization, either pass the host
        // as a constructor parameter (Preview(Kit::Window& host, ...)) or move
        // the work to after attachment (event callback / first paint). Also,
        // an object used as an ad-hoc action target that is never AddWidget-ed
        // can never get a window - use the sender (self.window() in the
        // callback) or pass it explicitly. See owner(), InstallEventFilter.
        Window* window() const;

        // ===================================================================
        //  Window convenience APIs (Close / Show / Hide / geometry / icon /
        //  title / decorations)
        // ===================================================================
        //  A "direct channel to window()": the window actions most common in
        //  widget callbacks are brought onto Widget itself - as reachable as
        //  AddChild / AddWidget, no need to know "how to get the Window" and
        //  no need to pass Window& through constructor parameters.
        //
        //  Why this group must exist (a real pitfall, 2026-10-15): a developer
        //  wanted a menu item "Exit" to close the window from inside a panel
        //  constructor, could not get the window, and wrote a local
        //  Kit::Window win; then [&]{ win.Close(); } - once the constructor
        //  returned, the local win was destroyed and the callback held a dead
        //  object, crashing on click (0xC0000005, read at 0x28). Root cause:
        //  callbacks could not reach "the live window", so developers built
        //  their own. This API group plus the sender protocol (self injected
        //  by the library when dispatching) close that path:
        //    { .text = "Exit", .action = [](Kit::Widget& self) { self.CloseWindow(); } }
        //
        //  Contract (same as the library-wide "unsupported = silent no-op"):
        //   - When not yet attached (window() == nullptr): action APIs are
        //     silent no-ops, query APIs return 0/false - constructor-time
        //     calls neither crash, throw, nor warn;
        //   - Actions forward to the owning window with semantics identical
        //     to the Window APIs of the same name (SetWindowSize =
        //     Window::SetSize, SetWindowPosition = Window::SetPosition, ...);
        //   - The action applies to the window of whoever the sender is -
        //     child widgets resolve the window along the parent chain and
        //     always get the currently attached live window, no matter how
        //     many times it was re-homed;
        //   - Not to be confused with the widget's own geometry APIs
        //     (SetSize/SetPosition act on the WIDGET); the Window-suffixed
        //     ones are window-level.
        // ===================================================================

        // ---- Window control ----
        // Closes this widget's window and exits the message loop (= user
        // clicking X; Window::Close)
        void CloseWindow();
        // Shows the window (no effect if already shown; Window::Show)
        void ShowWindow();
        // Hides the window (message loop keeps running; Window::Hide)
        void HideWindow();
        // Minimizes to the taskbar (Window::Minimize)
        void MinimizeWindow();
        // Maximizes (Window::Maximize)
        void MaximizeWindow();
        // Restores from maximized/minimized to normal size (Window::Restore)
        void RestoreWindow();

        // ---- Title / icon ----
        // Sets the window title bar text (UTF-8; Window::SetTitle)
        void SetWindowTitle(const std::string& title);
        // Window icon from an image file path (Window::SetWindowIcon)
        void SetWindowIcon(const std::string& path);
        // Window icon from a resource ID (Window::SetWindowIconFromResource)
        void SetWindowIconFromResource(int resId);

        // ---- Geometry ----
        // Sets the window CLIENT AREA size (Window::SetSize)
        void SetWindowSize(int w, int h);
        // Moves the window to screen coordinates (x, y) (Window::SetPosition)
        void SetWindowPosition(int x, int y);
        // Window minimum size constraint (Window::SetMinimumSize)
        void SetWindowMinimumSize(int w, int h);
        // Window maximum size constraint (Window::SetMaximumSize)
        void SetWindowMaximumSize(int w, int h);
        // Queries the client area size (writes 0, 0 when unattached;
        // Window::GetClientSize)
        void GetWindowSize(int& w, int& h) const;
        // Client area width (0 when unattached)
        int  GetWindowWidth() const;
        // Client area height (0 when unattached)
        int  GetWindowHeight() const;
        // Shrinks the client area to exactly fit all top-level widgets
        // (Window::adjustSize)
        void AdjustWindowSize();
        // Centers on the working area of the monitor it is on
        // (Window::CenterOnScreen)
        void CenterWindowOnScreen();

        // ---- Decorations / state ----
        // Allows / forbids resizing by dragging borders (Window::SetResizable)
        void SetWindowResizable(bool on);
        // Borderless mode switch (Window::SetBorderless)
        void SetWindowBorderless(bool on);
        // Edge-drag resizing while borderless (Window::SetBorderlessResizable)
        void SetWindowBorderlessResizable(bool on);
        // Shows / hides the system title bar (Window::SetTitleBarVisible)
        void SetWindowTitleBarVisible(bool visible);
        // Always-on-top switch (Window::SetTopmost)
        void SetWindowTopmost(bool on);
        // Fullscreen switch (Window::SetFullscreen)
        void SetWindowFullscreen(bool on);
        // Whether currently fullscreen (false when unattached)
        bool IsWindowFullscreen() const;
        // Rounded window corners switch (Window::SetRoundedCorners;
        // Win11+ system-level rounding)
        void SetWindowRoundedCorners(bool on);
        // Whether the window is currently rounded (false when unattached)
        bool IsWindowRoundedCorners() const;
        // Hands "drag the whole window" to the system (the right way for
        // custom title bars / menu-bar dragging; Window::StartWindowDrag)
        void StartWindowDrag();

        // ---- Repainting ----
        // Requests a repaint of THIS widget (marks itself dirty + schedules a
        // frame). Since 2026-10-27 the semantics are precise: it used to be an
        // alias of window()->RequestRedraw() ("repaint the whole tree"); now
        // it first marks itself dirty, then schedules a frame - with the
        // retained-mode fragment cache only this widget's Paint re-runs and
        // the rest is memcpy'd. Any visible state change in a widget (color,
        // text, selection, hover, values...) MUST call this. Without it,
        // immediate mode could still get by on other widgets' repaints, but
        // with the paint cache that no longer happens - a real bug, not a
        // performance issue. Thread-safe (sets a flag + enqueues, no GPU
        // touching). If you actually changed ANOTHER widget (rare), call that
        // widget's RequestWindowRedraw(), or Window::RequestRedraw() for the
        // conservative whole-tree invalidation. const: requesting a repaint
        // does not change observable state, just "remember to look at me next
        // frame" - Paint is const, and "noticed a repaint is needed while
        // painting" (e.g. wrap-cache invalidation) is a legitimate scenario;
        // without const every such call site would need const_cast.
        void RequestWindowRedraw() const;

        // Only marks this widget dirty, does NOT schedule a frame (the next
        // frame comes from another source). For changing many widgets in one
        // frame while scheduling only once; generally use the one above.
        void InvalidatePaint() const;

        // ===================================================================
        //  Event filters (widget-level - applies to this widget and its
        //  subtree only)
        // ===================================================================
        //  Same shape as Window::InstallEventFilter: attaching a filter to
        //  this widget means events targeting this widget or any descendant
        //  pass through the filter before dispatch. Key to modular
        //  development - modules can carry their own filtering without
        //  knowing Window:
        //   - a panel intercepting Esc to close itself attaches it to itself;
        //   - a table blocking drags in its own area attaches it to itself.
        //
        //  Timing: after the hit target is found, walk UP the parent chain
        //  from the target; each level runs its own filters first; any level
        //  intercepting drops the event. A filter installed on a parent
        //  naturally covers the whole subtree.
        // ===================================================================

        // Filter handle - returned by InstallEventFilter, used by
        // RemoveEventFilter. 0 means invalid (0 is never a valid id, serving
        // as the "not installed" sentinel).
        using FilterId = std::uint64_t;

        // Installs an event filter on this widget (non-owning). filter = the
        // filter itself; this widget stores only its address and does not
        // manage its lifetime. Returns the filter's handle for later
        // RemoveEventFilter; 0 on failure. A filter installed here applies
        // only to events hitting this widget or its descendants - events
        // hitting other widgets never pass through it; the interception scope
        // is exactly this subtree. Idempotent: installing the same filter
        // twice on the same widget does not double-apply (returns the
        // existing handle). The host does NOT own the filter; the filter may
        // be destroyed before the host (it unregisters itself on
        // destruction), but its address must never change once installed -
        // copying/moving the filter object is forbidden. See EventFilter,
        // RemoveEventFilter, Window::InstallEventFilter.
        FilterId InstallEventFilter(EventFilter& filter);

        // Uninstalls an event filter from this widget. id = the handle
        // returned by InstallEventFilter. Passing an invalid handle (0 or
        // already removed) has no side effects. Filters uninstall themselves
        // on destruction, so manual calls are rarely needed. See
        // InstallEventFilter.
        void RemoveEventFilter(FilterId id);

        // ===================================================================
        //  Visibility
        // ===================================================================
        // Sets visibility. visible = false means not rendered and excluded
        // from hit testing. Hiding affects only this level and does NOT
        // cascade to children, but hit testing skips an invisible widget
        // entirely (with its subtree). Visibility is also layout input: layout
        // only arranges visible children, so hiding a child removes it from
        // the layout sequence and this function notifies the parent to
        // relayout. Setting the current value returns immediately - an
        // unchanged value is not layout input and does not disturb the parent.
        void SetVisible(bool visible) {
            if (visible_ == visible) return;
            visible_ = visible;
            // Hidden = invisible AND must not keep keyboard focus. If focus
            // stayed on it, Tab would land on an invisible box before all
            // visible widgets and input would keep flowing into it. The
            // window-side RemoveWidget only handles "removed from the tree",
            // not "still in the tree, just hidden".
            if (!visible_) clearFocus();
            notifyLayoutInputChanged();
            RequestWindowRedraw();
        }

        // Whether visible
        bool isVisible() const { return visible_; }

        // Sets whether enabled (disabled widgets: no interaction, no focus,
        // Disabled-state colors). virtual: derived classes holding extra
        // interaction state (e.g. BoxWidget hover/pressed) override it - call
        // the base version first, then clear their own transients.
        virtual void SetEnabled(bool enabled);
        // Whether enabled
        bool isEnabled() const { return !disabled_; }

        // ===================================================================
        //  Keyboard focus (desktop only)
        // ===================================================================
        //  Division of labor: focus ARBITRATION lives on the Window side (who
        //  holds focus, Tab order, focus-ring painting); this side only
        //  provides "eligibility declaration + state + notification". So:
        //   - SetFocusable(true) declares "I belong in the Tab chain and can
        //     take keyboard input";
        //   - when focus is actually gained/lost, Window calls setFocused()
        //     and the widget is notified via OnFocusIn / OnFocusOut;
        //   - for focused-state colors use WidgetState::Focused (six-state
        //     table is ready); the focus ring uses BoxStyle::Outline.
        //
        //  Why not self-sufficient like SetVisible: focus is MUTUALLY
        //  EXCLUSIVE global state (only one widget can hold it at a time) and
        //  needs an arbiter - a widget cannot unilaterally acquire it.
        // ===================================================================

        // Sets whether this widget can take keyboard focus. focusable = true
        // makes it a Tab-cycling candidate. Default false - pure display
        // widgets (Label, decorative containers) should not enter the Tab
        // chain. This is an eligibility declaration, not current ownership;
        // query state with hasFocus().
        void SetFocusable(bool focusable) { focusable_ = focusable;  RequestWindowRedraw();}

        // Whether this widget is focusable (default false)
        bool isFocusable() const { return focusable_; }

        // Whether this widget can ACTUALLY take focus right now (queried by
        // focus arbitration / Tab traversal). Default = focusable AND not
        // disabled - "eligible but currently not". Click-to-focus and Tab
        // traversal live on the Window side (dispatchMousePress / dispatchKey
        // in Zekit.cpp); widgets must not grab focus via setFocused themselves.
        virtual bool isFocusTarget() const { return focusable_ && !disabled_; }

        // Whether this widget currently holds focus
        bool hasFocus() const { return focused_; }

        // Called when focus is gained (override to highlight, select all
        // text, open a candidate list, etc.)
        virtual void OnFocusIn() {}

        // Called when focus is lost (override to commit edits, clear pressed
        // state, close overlays, etc.)
        virtual void OnFocusOut() {}

        // ===================================================================
        //  Theme injection (the host pours the current theme into widgets)
        // ===================================================================
        //  At construction widgets get a built-in light theme as default (so
        //  a standalone new-ed widget still looks right); the real theme is
        //  injected by Window. Two paths with different semantics:
        //   - On attach (AddWidget): inject only into widgets the user has
        //     not SetStyle-d - otherwise "configure style first, then hand
        //     over" would be washed away at attach time;
        //   - On theme switch / dark-mode change: full injection (a theme
        //     switch is "start over", see ZeButton.h).
        //  Widgets without visual styles (pure containers, layouts) need not
        //  override - the default does nothing.
        // ===================================================================

        // Re-injects styles from the current theme. theme = the active theme.
        // Default empty implementation. Styled widgets (Button etc.) override
        // it; when overriding do NOT call markStyled() - injection is not
        // "user customization".
        virtual void ApplyTheme(const Theme& theme) { (void)theme; }

        // Whether this widget's style was set manually by the user. true =
        // a style setter was called (theme injection at attach time skips it).
        // See markStyled.
        bool hasCustomStyle() const { return styled_; }

        // ===================================================================
        //  Hit testing
        // ===================================================================
        // Whether a client-area point falls inside this widget's hittable
        // region. x = client X (pixels, Y down); y = client Y. Returns true on
        // hit. The default is a rectangle test (x/y/w/h contains the point),
        // good enough for most widgets. Circular, ring-shaped, or partially
        // drawn widgets should override it so blank areas do not count as
        // hits. Does NOT consider visible_ or parent-child hierarchy - it
        // answers "geometrically, yes or no"; hierarchy/visibility filtering
        // is Kit::HitTest::Topmost's job. Coordinates are absolute client
        // coordinates; if widget-relative (parent-offset) coordinates are
        // introduced later, the default must subtract the x()/y() offset.
        virtual bool HitTest(float x, float y) const;

        // ===================================================================
        //  Input events (default NOT consumed; returning false bubbles the
        //  event to the parent widget)
        // ===================================================================
        //  Convention:
        //   - To handle: do your logic, set e.accepted (or call e.accept()),
        //     and return true - bubbling stops immediately;
        //   - Not handling: return false and the event goes to the parent;
        //   - If even the top-level widget does not handle it, the event is
        //     dropped (system default behavior unaffected).
        // ===================================================================

        // Mouse pressed (received only when this widget was hit)
        virtual bool OnMousePress(MouseEvent& e) { (void)e; return false; }

        // Mouse released
        virtual bool OnMouseRelease(MouseEvent& e) { (void)e; return false; }

        // Mouse moving over the widget (also received with no button down)
        virtual bool OnMouseMove(MouseEvent& e) { (void)e; return false; }

        // Mouse wheel
        virtual bool OnMouseWheel(MouseEvent& e) { (void)e; return false; }

        // Mouse entered this widget (only one hover widget at a time)
        virtual void OnMouseEnter() {}

        // Mouse left this widget
        virtual void OnMouseLeave() {}

        // Whether taking focus requires an on-screen (soft) keyboard.
        // Mobile only. Default false (ordinary widgets need no keyboard).
        // Editable widgets (InputBox) override to true - the keyboard pops up
        // or retracts automatically on focus change, no manual
        // Window::ShowSoftKeyboard calls. On desktop the whole chain is a
        // no-op (Capability::OnScreenKeyboard false), so overriding has no
        // desktop effect - one codebase, right on both. Read once per focus
        // CHANGE (see Window::setFocusedWidget), not polled every frame.
        virtual bool WantsSoftKeyboard() const { return false; }

        // -------------------------------------------------------------------
        //  Touch events (multi-touch) - mobile
        // -------------------------------------------------------------------
        //  Override these ONLY for multi-finger gestures. Single-finger
        //  actions like "tap a button" are already synthesized into mouse
        //  events by the platform layer (see TouchEvent in ZeInputEvents.h) -
        //  overriding the mouse callbacks is enough. Override these for
        //  two-finger pinch/rotate or multi-finger swipes.
        //
        //  Bubbling rules match the mouse: return true (or set e.accepted)
        //  stops bubbling. Coordinates are client pixels, the same coordinate
        //  system as mouse events, freely mixable.
        // -------------------------------------------------------------------

        // A finger pressed on this widget (once per finger for multi-touch).
        // Default false, so touches keep bubbling - and that is correct: a
        // mouse-only widget (the vast majority) must not eat the event just
        // because it did not override touch callbacks, or parents (e.g.
        // ScrollView) would never see it.
        virtual bool OnTouchBegin(TouchEvent& e) { (void)e; return false; }

        // A finger moving (including OTHER fingers moving - this widget still
        // receives it, because one report carries a snapshot of all fingers;
        // claim yours via e.pointerId)
        virtual bool OnTouchUpdate(TouchEvent& e) { (void)e; return false; }

        // A finger lifted
        virtual bool OnTouchEnd(TouchEvent& e) { (void)e; return false; }

        // The whole gesture was stolen by the system (incoming call, system
        // back gesture, app backgrounded). Must unconditionally abort any
        // in-progress drag/pinch - there will be no End.
        virtual bool OnTouchCancel(TouchEvent& e) { (void)e; return false; }

        // Key pressed (for widgets that need focus mechanics; currently
        // broadcast by Window to top-level widgets)
        virtual bool OnKeyPress(KeyEvent& e) { (void)e; return false; }

        // Key released
        virtual bool OnKeyRelease(KeyEvent& e) { (void)e; return false; }

        // Character input (IME and case handling already done; for text entry)
        virtual bool OnCharInput(KeyEvent& e) { (void)e; return false; }

        // -------------------------------------------------------------------
        //  IME (IMM32) message hooks - only InputBox overrides; other widgets
        //  ignore by default
        // -------------------------------------------------------------------
        //  Design: IME messages are captured by Window's WndProc and sent
        //  directly to the focused widget. Virtual functions instead of
        //  Window knowing about InputBox keeps Window decoupled from any
        //  specific widget. All processing logic lives inside InputBox
        //  (self-drawn composition string, candidate window positioning,
        //  result de-duplication), matching "the IME belongs to the input
        //  box" responsibility boundary.
        // -------------------------------------------------------------------

        // IME started composition (WM_IME_STARTCOMPOSITION). true = handled
        // (focused widget is an input box); false = not handled, back to
        // DefWindowProc
        virtual bool OnImeStartComposition() { return false; }

        // IME composition update (WM_IME_COMPOSITION). flags = GCS_* bit
        // flags (GCS_COMPSTR / GCS_RESULTSTR ...); lParam = native lParam
        // (some bits need the raw value)
        virtual bool OnImeComposition(uint32_t flags, uintptr_t lParam) {
            (void)flags; (void)lParam; return false;
        }

        // IME ended composition (WM_IME_ENDCOMPOSITION)
        virtual bool OnImeEndComposition() { return false; }

        // IME character commit (WM_IME_CHAR; used by IMEs without a
        // composition flow). ch = UTF-16 code unit
        virtual bool OnImeChar(wchar_t ch) { (void)ch; return false; }

        // -------------------------------------------------------------------
        //  Geometry
        // -------------------------------------------------------------------
        // Makes this widget auto-fill the window client area (top-level
        // widgets only). on = true enables following (default), false disables.
        // When enabled, before each frame's rendering the window syncs this
        // widget's geometry to position (0, 0) + size = client width/height in
        // pixels. Any window resize (border drag, SetSize, maximize) relayouts
        // this widget and its whole layout tree within the SAME frame - no
        // resize hook needed (Window offers none), and no "window changed but
        // layout did not". Typical usage: hand the single root container to
        // the window and let it fill the client area (root.SetFillWindow()).
        // Meaningful only for widgets attached DIRECTLY to the window; child
        // positions are decided by the parent container and opening this
        // switch gets overwritten by the parent's layout. Multiple widgets may
        // enable it: they overlap on the same client area, layered in add
        // order (z-order). For "switch whole pages", Window::SetWidget by name
        // is clearer. Uses physical client pixels (same source as
        // Window::GetClientSize), consistent with the widget geometry
        // coordinate system, no extra conversion.
        void SetFillWindow(bool on = true);

        // Whether auto-filling the window client area (default false)
        bool fillWindow() const { return fillWindow_; }

        // Sets the widget position (top-left corner). x = horizontal (pixels,
        // relative to the window client area, Y down); y = vertical (pixels).
        // Equivalent to SetX(x) + SetY(y).
        void SetPosition(float x, float y);

        // Sets the widget size. width/height in pixels. virtual: choice
        // widgets (CheckBox / RadioButton) override it to record "the user set
        // the size explicitly", disabling content auto-fit (autoSize). The
        // result is clamped into the SetMinSize / SetMaxSize range. The value
        // set here is USER INTENT (pref) and takes priority over the widget's
        // self-reported natural size: once set, Widget::AutoSize() no longer
        // touches that axis. Passing 0 on an axis RETURNS that axis to auto
        // sizing (content-driven) - so SetSize(300, 0) means "width fixed at
        // 300, height by content". See AutoSize, SetDefaultSize.
        virtual void SetSize(float width, float height);

        // Sets position and size in one call. x/y = coordinates (pixels);
        // width/height in pixels.
        void SetGeometry(float x, float y, float width, float height);

        // Sets the X coordinate (pixels)
        void SetX(float x);

        // Sets the Y coordinate (pixels)
        void SetY(float y);

        // Sets the width (pixels)
        void SetWidth(float width);

        // Sets the height (pixels)
        void SetHeight(float height);

        // Gets the X coordinate (pixels; the widget's left edge)
        float x() const;

        // Gets the Y coordinate (pixels; the widget's top edge)
        float y() const;

        // Gets the width (pixels)
        float width() const;

        // Gets the height (pixels)
        float height() const;

        // User-intent size (the BASELINE for layout algorithms). Returns the
        // user-set size after min/max clamping. Division of labor with
        // width()/height(): width() is the ACTUAL size - the value layout
        // computes and writes back (after Stretch / flexGrow), changing every
        // layout round; prefWidth() is USER INTENT - layout only reads it,
        // never writes it, and it always equals what the user set. Layout
        // algorithms must use it as the baseline (free space, grow
        // distribution, alignment) and must NOT use width(): the latter
        // contains last round's result and using it as input becomes a
        // one-way ratchet ("grows with the window but never shrinks back").
        // Returns 0 for widgets never sized ("I occupy nothing by default") -
        // exactly why flexGrow can split space from zero.
        float prefWidth() const;
        float prefHeight() const;

        // ===================================================================
        //  Auto-size ("when the user set no size, do not stay at 0x0")
        // ===================================================================
        //  Why this layer exists: the measurement protocol answers "HOW BIG
        //  should I be" (hint), but historically only layout containers turned
        //  hints into actual sizes (LayoutBase::DoLayout). So widgets without
        //  an arranger - top-level widgets attached directly to the window,
        //  children inside non-layout containers - never had their hint asked:
        //  if the user did not SetSize, the actual size stayed 0x0, nothing
        //  visible, no compiler complaint. This function supplies the "ask the
        //  hint and write back" step, giving such widgets a content-driven
        //  size. Window calls it automatically before each frame's render for
        //  "widgets without an arranger", so the usual style (no SetSize at
        //  all) needs no changes; explicit calls are only for "content just
        //  changed and this frame must size to it".
        //
        //  Not to be confused with implicit fill - two different things:
        //    - Fill (SetFillWindow / top-level LayoutBase default) follows the
        //      CLIENT AREA;
        //    - This function (auto-size) follows the CONTENT (hint).
        //  It is therefore not bound by the "implicit fill only for
        //  LayoutBase" rule: it will not stretch a lone top-level button over
        //  the whole client area, only size it to its own text.
        // ===================================================================
        // Auto-sets the actual size from the natural size (only touches axes
        // the user did not set). Returns true if the actual size changed;
        // false if no change was needed (user specified it, or computed 0).
        // Value priority is exactly the same source as layout's RestorePref
        // (the two must agree, or the same widget gets different sizes in a
        // layout vs directly on a window):
        //   user-set (pref > 0) > hint (widget's content-based report) >
        //   defaultSize (construction-time fallback)
        // Judged per axis, so after SetWidth(300) the height still auto-fits
        // content. Writes the ACTUAL size (width()/height()) and never touches
        // pref - otherwise the computed result would become "user intent" and
        // permanently crush later auto-fit (ratchet).
        //
        // When it runs automatically: Window::RenderFrame, before each frame's
        // rendering, over widgets "without an arranger": top-level widgets
        // (AddWidget-ed directly onto the window) and children inside
        // NON-layout widgets (hosts with arrangesChildren() false). Children
        // laid out by LayoutBase are excluded - their geometry is written by
        // the layout and interference here would fight it. Also skipped:
        // SetFillWindow(true) widgets (they follow the client area) and
        // top-level layout containers without a user-set size (implicit fill
        // owns them).
        //
        // Before fonts are ready (not yet Show()) the hint cannot be computed
        // -> falls back to defaultSize, so reading width() right after
        // AddWidget still yields a visible size; the first frame after fonts
        // are ready re-measures with real text. After content changes (e.g.
        // Label::SetText) the size follows automatically on the NEXT frame -
        // no manual call needed; call once explicitly to get the new size in
        // the same frame. The computed size is also clamped by SetMinSize /
        // SetMaxSize. See hintWidth, SetDefaultSize, LayoutBase::RestorePref.
        bool AutoSize();

        // ===================================================================
        //  Measurement protocol (hint - "how big I naturally want to be")
        // ===================================================================
        //  Division of labor among three size concepts (without the hint
        //  layer, "no SetSize" would mean "no layout input"):
        //    - hint   - the widget's own content-based "how big I naturally
        //      want to be" (this group of APIs)
        //    - pref   - what the USER named (written by SetSize; explicit user
        //      intent always wins)
        //    - actual - what layout writes back this round (width()/height())
        //
        //  Layout's uniform rule for "desired size" (built into LayoutBase):
        //      pref > 0  ->  use pref (user decides)
        //      pref == 0 ->  use hint (user silent -> ask the widget)
        //  This rule keeps existing behavior bit-for-bit - sized widgets take
        //  the old path; unsized ones go from "0-height blank window" to
        //  "sized by content".
        //
        //  hint is a PURE QUERY: no state changes, no touching actual sizes,
        //  callable at any layout stage. Text widgets need a font to measure;
        //  the window owns it (Window::MeasureText) and widgets reach it via
        //  window() - so text widgets (Label / Button / InputBox) ARE able to
        //  override. Without a window (not yet Show(), or not attached) they
        //  fall back to defaultSize_, then 0 - "I cannot compute it"; layout
        //  then uses pref, same as before the protocol existed, with no
        //  misreported sizes.
        // ===================================================================

        // The widget's self-reported "natural width" (default = prefWidth()).
        // Returns the suggested width; the default returns prefWidth(), i.e.
        // "0 if the user set nothing". Containers override it to SUMMARIZE
        // children (VBox takes the max child width, HBox sums + spacing...);
        // leaf widgets override it to measure content (Label = text metrics +
        // padding). Returning prefWidth() by default is a key design: widgets
        // that do not override behave exactly as before, so the protocol can
        // be adopted widget by widget instead of all at once.
        virtual float hintWidth() const { return prefWidth(); }

        // The widget's self-reported "natural height" (default = prefHeight()).
        // See hintWidth.
        virtual float hintHeight() const { return prefHeight(); }

        // Self-reports the needed height under a GIVEN available width
        // (default ignores the width -> hintHeight()). Wrapping widgets
        // (RichText, FlowLayout, CodeEditor with auto-height) cannot answer
        // their height outright: a different width wraps differently and
        // changes the height. The parameterless hintHeight() can only answer
        // "one line tall". So vertical containers run a pass before layout:
        // they hand the available width to every child with
        // heightDependsOnWidth() == true and exchange it for the real height
        // via this function (see LayoutBase::measureWidthDependentHeights).
        // availWidth = the OUTER width the parent plans to give this widget
        // (including padding and border); returns the needed height under
        // that width (also including padding and border). The default ignores
        // the parameter and forwards to hintHeight() - widgets that do not
        // depend on width are unchanged, so the protocol can be adopted
        // widget by widget. This protocol once existed only on LayoutBase
        // (parents filtered children with dynamic_cast<LayoutBase*>), which
        // was a dead end for wrapping LEAF widgets: CodeEditor sits on the
        // Widget -> BoxWidget -> TextEditBase branch and nobody ever asked
        // its height - "wrapped, but painted at fixed-line height" and extra
        // lines were cut off. Hoisted to Widget, the protocol now holds for
        // ANY widget.
        virtual float hintHeightFor(float availWidth) const { (void)availWidth; return hintHeight(); }

        // Whether this widget's height DEPENDS on width (true = the parent
        // fixes the width first, then asks hintHeightFor). Default false.
        // Wrapping widgets override to return true (may gate on a runtime
        // switch). See hintHeightFor.
        virtual bool heightDependsOnWidth() const { return false; }

        // ===================================================================
        //  Fallback size (defaultSize - "use this when the natural size
        //  cannot be computed")
        // ===================================================================
        // Sets a FALLBACK size, used only when "the user set nothing AND the
        // natural size cannot be computed". w/h = fallback width/height
        // (0 = no fallback on that axis). Unlike SetSize, it does NOT write
        // pref - so it is not "user intent" and does not outrank the natural
        // size. Value priority: pref > measured value > hint > defaultSize.
        // Why it exists: Button / Label need a "visible" default size at
        // construction, otherwise nothing shows before attachment (text not
        // measurable). But writing it via SetSize would put it in pref and
        // permanently outrank the natural size - a text-fitting button could
        // never fit. This API separates "construction-time visual fallback"
        // from "explicit user intent"; the two never interfere. Once attached
        // and the natural size is computable, the natural size WINS; only
        // when it cannot be computed (e.g. fonts not ready before Show())
        // does this fallback apply. See SetSize, hintWidth.
        void SetDefaultSize(float w, float h) {
            defaultW_ = w > 0.0f ? w : 0.0f;
            defaultH_ = h > 0.0f ? h : 0.0f;
            RequestWindowRedraw();
        }

        // Current fallback size (0 = unset). See SetDefaultSize.
        float defaultWidth() const { return defaultW_; }
        float defaultHeight() const { return defaultH_; }

        // ===================================================================
        //  Size policy (sizePolicy - "which axis of me is elastic")
        // ===================================================================
        // How a widget "wants to be treated" along an axis. Defined at
        // NAMESPACE level (see file top); this using-declaration just brings
        // it into class scope - both Widget::SizePolicy::Expanding and
        // Kit::SizePolicy::Expanding work, so old code is unaffected.
        using SizePolicy = Kit::SizePolicy;

        // Horizontal size policy (default Preferred)
        SizePolicy horizontalPolicy() const { return hPolicy_; }
        // Vertical size policy (default Preferred)
        SizePolicy verticalPolicy() const { return vPolicy_; }

        // Sets the size policy for both axes. h = horizontal; v = vertical.
        // Only affects the PARENT container's allocation decision, not this
        // widget's geometry, so no relayout notification is fired.
        void SetSizePolicy(SizePolicy h, SizePolicy v) { hPolicy_ = h; vPolicy_ = v;  RequestWindowRedraw();}
        // Sets only the horizontal policy
        void SetHorizontalPolicy(SizePolicy p) { hPolicy_ = p;  RequestWindowRedraw();}
        // Sets only the vertical policy
        void SetVerticalPolicy(SizePolicy p) { vPolicy_ = p;  RequestWindowRedraw();}

        // -------------------------------------------------------------------
        //  Size constraints (min / max width and height)
        // -------------------------------------------------------------------
        // Sets minimum size limits. minWidth/minHeight in pixels; 0 or
        // negative means unlimited (normalized to 0 internally). If the new
        // minimum exceeds the current maximum, the maximum is raised to match
        // so constraints never contradict each other. If the current size is
        // below the new minimum, the size is grown to the minimum.
        void SetMinSize(float minWidth, float minHeight);

        // Sets maximum size limits. maxWidth/maxHeight in pixels; 0 or
        // negative means unlimited (normalized to FLT_MAX internally). If the
        // new maximum is below the current minimum, the minimum is lowered to
        // match so constraints never contradict each other. If the current
        // size exceeds the new maximum, the size is shrunk to the maximum.
        void SetMaxSize(float maxWidth, float maxHeight);

        // Sets the minimum width (0 or negative = unlimited)
        void SetMinWidth(float w);

        // Sets the minimum height (0 or negative = unlimited)
        void SetMinHeight(float h);

        // Sets the maximum width (0 or negative = unlimited, normalized to FLT_MAX)
        void SetMaxWidth(float w);

        // Sets the maximum height (0 or negative = unlimited, normalized to FLT_MAX)
        void SetMaxHeight(float h);

        // Current minimum width (0 if unlimited)
        float minWidth() const;

        // Current maximum width (FLT_MAX if unlimited)
        float maxWidth() const;

        // Current minimum height (0 if unlimited)
        float minHeight() const;

        // Current maximum height (FLT_MAX if unlimited)
        float maxHeight() const;

        // -------------------------------------------------------------------
        //  Child layout properties (matching FlexStyle.h's boundary: these
        //  are "how this child wants to be placed")
        // -------------------------------------------------------------------
        //  Container properties (how to place all children) live in
        //  FlexStyle; child properties (how THIS widget wants to be placed)
        //  live in Widget. Three fields:
        //   - margin    - spacing to siblings (four sides, pixels)
        //   - flexGrow  - weight for eating main-axis leftover space
        //     (0 = takes no share)
        //   - alignSelf - overrides the parent's alignItems cross-axis
        //     alignment (unset = inherit)
        // -------------------------------------------------------------------

        // Sets four-side outer margins (spacing to siblings)
        void SetMargin(float left, float top, float right, float bottom);
        // Sets one margin per axis (left=right=h, top=bottom=v)
        void SetMargin(float horizontal, float vertical);
        // Gets the four-side outer margins
        const Margin& margin() const { return margin_; }

        // Sets the main-axis flex weight (share of leftover space; 0 = none)
        void SetFlexGrow(float grow);
        // Gets the main-axis flex weight
        float flexGrow() const { return flexGrow_; }

        // Sets the cross-axis alignment (overrides the parent's alignItems)
        void SetAlignSelf(AlignItems a);
        // Clears the explicit alignSelf, restoring inheritance of the parent's
        // alignItems. Pairs with SetAlignSelf: hasAlignSelf() returns false
        // and alignSelf() resets to Stretch (the default) - so the convention
        // "alignSelf() returns Stretch when not explicitly set" still holds
        // after clearing, with no stale value left over. The layout layer
        // branches on hasAlignSelf() (its own value vs the parent's; see
        // ZeLayoutBase.h), so after clearing the widget immediately
        // participates under the parent's alignItems.
        void ClearAlignSelf();
        // Whether an explicit alignSelf exists (false = inherit the parent's
        // alignItems)
        bool hasAlignSelf() const { return hasAlignSelf_; }
        // Gets the cross-axis alignment (returns Stretch when not explicitly
        // set; check hasAlignSelf first)
        AlignItems alignSelf() const { return alignSelf_; }

    protected:
        // -------------------------------------------------------------------
        //  Implementation of value takeover (no "may this widget take
        //  children" gating)
        // -------------------------------------------------------------------
        //  All three entry points funnel here:
        //    - Widget::AddWidget          - public, marked [[deprecated]]
        //      (this class does not arrange)
        //    - Widget::AddWidgetUnchecked - escape hatch, same impl without
        //      the deprecation
        //    - LayoutBase::AddChild / AddWidget, Widget::SetWidget - internal,
        //      always this
        //  So "warn or not" happens only at the CALL SITE; the implementation
        //  stays single-source.
        // -------------------------------------------------------------------
        // Common implementation of the AddWidget trio: moves a widget value
        // into this widget's storage. w = widget value (lvalues are moved).
        // Returns the stable in-tree reference. If, after adding, this widget
        // turns out not to arrange children (arrangesChildren() false), a
        // ONE-SHOT runtime diagnostic prints - reported once, de-duplicated by
        // host and child type names, never spamming.
        template<class T>
        T& addWidgetValue(T&& w) {
            static_assert(std::is_base_of_v<Widget, std::decay_t<T>>,
                "AddWidget requires a Kit::Widget-derived control");
            static_assert(!std::is_abstract_v<std::decay_t<T>>,
                "AddWidget cannot instantiate an abstract control (did you forget to implement Paint?)");
            using Plain = std::decay_t<T>;                // Strip references: T is T& when an lvalue is passed

            // Whether this widget arranges children (decides the two things
            // below: diagnostics + auto-size)
            const bool arranges = arrangesChildren();

            // Diagnose BEFORE the move: after moving, the real T only survives
            // as the name Plain, but the report wants the type the caller
            // actually passed (only readable when paired with the host class
            // name).
            if (!arranges)
                reportMisplacedChild(typeid(*this).name(), typeid(Plain).name());

            auto p = std::make_unique<Plain>(std::move(w));   // Library moves internally; no std::move at call sites
            Plain& ref = *p;
            ref.parent_ = this;
            children_.push_back(&ref);
            owned_.push_back(std::move(p));               // Take over the lifetime

            // This widget does not arrange children -> this child has NO
            // arranger and nothing will ever write its geometry. Adding a
            // widget without SetSize means adding 0x0: invisible blank, no
            // compiler complaint. Auto-sizing on attach is the only remedy on
            // this path. Only when arranges == false: a layout container's
            // children get geometry from DoLayout, and writing here would
            // fight the layout (and be overwritten by RestorePref right
            // after). After push: AutoSize needs window()/parent() context.
            if (!arranges) ref.AutoSize();
            return ref;
        }

        // Marks "this widget's style was changed by the user" (call in style
        // setters). Once marked, Window will not overwrite this widget's
        // styles with the theme at ATTACH time (AddWidget) - preserving the
        // "configure style first, then hand over" pattern (the flagship use
        // of value semantics). An EXPLICIT theme change (Window::SetTheme /
        // dark-mode switch) still overwrites - that is "start over"
        // semantics; see Button::ApplyTheme. Granularity is the whole widget,
        // not per-field: a deliberate trade-off to avoid a hasXxx_ sentinel
        // on every style field (widget size, branches, and maintenance cost
        // would all balloon).
        void markStyled() { styled_ = true; }

        // Notification hook when layout input changes (default empty). A
        // "layout input" is any field that changes the PARENT's layout
        // result: size, min/max constraints, margin, flexGrow, alignSelf,
        // visibility. After any of these changes, Widget notifies its DIRECT
        // parent; parents that want to relayout override this - layout
        // containers implement it as Invalidate() (see ZeLayoutBase). Why
        // this layer: if a parent's relayout condition only covered "own
        // dirty flag / own geometry / child count", the whole class of "child
        // property changed" would be missed - AddWidget first, then
        // SetSize/SetMargin/SetFlexGrow/SetVisible, and the parent never
        // relayouts. Position (SetPosition) is NOT a notification trigger: a
        // child's position is decided by the parent's layout and self-set
        // positions are overwritten by the next layout, so it is not layout
        // input. Sizes written back DURING layout do not trigger relayout
        // (layout containers filter that themselves) - no self-excitation.
        virtual void OnLayoutInputChanged() {}

        // Declares "this widget holds a native surface" (derived classes call
        // once in their CONSTRUCTOR). Once set, the host calls
        // syncNativeSurface every frame, completely independent of "whether
        // this frame paints it" (cache hits / viewport culling / zero size
        // never skip it). See hasNativeSurface. Called in the derived ctor;
        // the bit MOVES with value-semantics relocation (see the move
        // constructor), so it is not lost after AddWidget.
        void markNativeSurface() { nativeSurface_ = true; }

    private:
        float x_ = 0.0f, y_ = 0.0f, w_ = 0.0f, h_ = 0.0f;
        // User-intent size (written by SetSize / SetWidth / SetHeight;
        // layout READS only). Division of labor with w_/h_ described at
        // prefWidth(). The move constructor MUST carry these.
        float prefW_ = 0.0f, prefH_ = 0.0f;
        float minW_ = 0.0f, minH_ = 0.0f;                 // 0 = unlimited
        float maxW_ = FLT_MAX, maxH_ = FLT_MAX;           // FLT_MAX = unlimited

        // Fallback size (written by SetDefaultSize): used only when neither
        // pref nor hint can provide a value. Key difference from prefW_: not
        // "user intent", never outranks the natural size. Priority: pref >
        // measured > hint > defaultSize. The move constructor MUST carry
        // these. See SetDefaultSize.
        float defaultW_ = 0.0f, defaultH_ = 0.0f;

        // Size policy (the parent decides whether to stretch by it; moves
        // with the value). Default Preferred: wants the hint but may be
        // stretched when space allows - closest to "today's Stretch
        // semantics", changing the protocol without changing looks.
        SizePolicy hPolicy_ = SizePolicy::Preferred;
        SizePolicy vPolicy_ = SizePolicy::Preferred;

        bool visible_ = true;                             // false = not rendered, not hit

        // Whether auto-filling the window client area (SetFillWindow; moves
        // with the value)
        bool fillWindow_ = false;

        // Whether this widget holds a native surface (set by
        // markNativeSurface(); moves with the value). See hasNativeSurface()
        // / syncNativeSurface().
        bool nativeSurface_ = false;

        // The mouse cursor this widget declares (SetCursor; moves with value)
        Cursor cursor_ = Cursor::Arrow;

        std::string objectName_;                          // objectName (SetObjectName; moves with value)

        // ---- Hover tooltip (SetTooltip; moves with value) ----
        //  Three strings instead of one TooltipContent: this header must not
        //  include ZeTooltip.h back (it would form the cycle ZeWidget ->
        //  ZeTooltip -> ZeTextWidget -> ZeWidget).
        std::string tooltipText_;                         // Tooltip body
        std::string tooltipTitle_;                        // Tooltip title (optional)
        std::string tooltipIcon_;                         // Tooltip icon path (optional)

        bool focusable_ = false;                          // Focus eligibility (SetFocusable; moves with value)
        bool focused_ = false;                            // Whether focus is held (only Window::setFocused writes it)
        bool disabled_ = false;                           // Disabled state (SetEnabled; user setting, moves with value)
        bool styled_ = false;                             // Whether the user Set a style (theme injection at attach defers to it; moves with value)

        // Child layout properties ("how this widget wants to be placed",
        // matching the FlexStyle.h boundary)
        Margin    margin_;                                // Four-side outer margins
        float     flexGrow_ = 0.0f;                       // Main-axis flex weight (0 = no share)
        AlignItems alignSelf_ = AlignItems::Stretch;      // Cross-axis alignment (hasAlignSelf_ decides whether it applies)
        bool      hasAlignSelf_ = false;                  // false = inherit the parent's alignItems

        // Clamps the current size back into [min, max]
        void clampSize();

        // Restores the actual size to the "user-intent size" (called for
        // every child before each layout round). Layout input must be
        // decided only by "user intent + this round's available space",
        // never by the previous round - without restoring, w_/h_ hold last
        // round's result and grow/Stretch using it as a baseline becomes a
        // one-way ratchet.
        void restorePrefSize();

        // Writes the "actual size" directly, never touching pref (layout
        // write-back only). Layout must NEVER write back via the public
        // SetSize: that would put the layout result into pref, which the
        // next round would use as its baseline - the ratchet returns
        // instantly. Called only through LayoutAccess.
        void setActualSize(float w, float h);

    protected:

        // Notifies the DIRECT parent that "this widget's layout input
        // changed". Called only when a field that can change the parent's
        // layout result changes: size / min-max constraints / margin /
        // flexGrow / alignSelf / visibility, plus POSITION (position is a
        // layout result; external changes must force a parent relayout to be
        // corrected). Whether the parent actually relayouts is decided by the
        // parent's OnLayoutInputChanged. The notification PROPAGATES up the
        // parent chain: LayoutBase::OnLayoutInputChanged, after dirtying
        // itself, calls this again to report further up - a layout
        // container's size DEPENDS ON ITS CONTENT (LayoutBase::hintHeight() /
        // contentHeight()), so a grandchild's property change also affects
        // ancestors' layout results. (Early versions notified only the direct
        // parent, valid while "containers did not size themselves to
        // content"; once containers started reporting content-based sizes
        // that premise broke, hence propagation.) The propagating_ gate in
        // the implementation ensures each input is processed once per node on
        // the chain. protected (not private): LayoutBase::OnLayoutInputChanged
        // must call it to report up. See LayoutBase::OnLayoutInputChanged.
        void notifyLayoutInputChanged();

        // Reports "geometry changed; recompute POSITION STATE (hover / cursor
        // shape) next frame". Hover is a function of POINTER POSITION, not of
        // mouse events: scrolling / relayout can move a widget out from under
        // the pointer without the pointer moving a pixel - yet the highlight
        // must follow. So report once when RELAYOUT SETTLES, and Window
        // recomputes uniformly in the frame (see the long comment at
        // Window::requestHoverResync). Different from RequestWindowRedraw:
        // that says "I need repainting", this says "something else may now be
        // under the pointer". The report points almost always coincide
        // (geometry relayout settling), but the semantics differ - do not
        // merge them into one flag. protected: used by internal geometry
        // paths (LayoutBase::EnsureLayout); widget authors do not need it -
        // widgets just change their look in OnMouseEnter/Leave.
        void noteHoverResync() const;

        // -------------------------------------------------------------------
        //  Focus request / release (the legal channel for MODAL widgets)
        // -------------------------------------------------------------------
        //  The architectural rule is "widgets never grab focus" - focus is
        //  arbitrated by Window and ordinary widgets wait for a click / Tab
        //  to bring it. But MODAL widgets (menu-modal windows and the like)
        //  must take the keyboard the moment they open (Esc / arrows), not
        //  after the user first clicks - hence these two protected openings:
        //  derived classes opt in EXPLICITLY, and ordinary widgets cannot see
        //  them so they cannot misuse them. Implementation forwards to
        //  Window::setFocusedWidget (Widget is a friend of Window).
        // -------------------------------------------------------------------

        // Requests focus onto this widget (silently skipped while not
        // attached to a window). Should only be called by modal widgets on
        // the "open" edge - not in constructors / ApplyTheme and the like;
        // that would bypass focus arbitration.
        void requestFocus();

        // Releases focus (only if currently held; a no-op when not held)
        void clearFocus();

    private:

        // Finds among this widget's DIRECT children by objectName (non-
        // template bridge, used by FindWidget / SetWidget). Implemented in
        // ZeWidget.cpp. Returns nullptr when not found (empty names never
        // match).
        Widget* findChildByName(const std::string& name);

        // ---- Non-template bridges for recursive find / traverse ----
        //  Templates are thin wrappers (casting the returned Widget* to T)
        //  and the loop bodies compile once - same idea as findChildByName,
        //  avoiding a full traversal copy per T. Why no dynamic_cast inside
        //  the bridge: that would require the type to be known at the
        //  bridge. Instead the bridge returns the named Widget* and the
        //  template layer casts, so dynamic type validation happens there.
        Widget* findDeepByName(const std::string& name);
        void    findAllByName(const std::string& name, std::vector<Widget*>& out);
        void    visitTreeRaw(void (*fn)(Widget&, int, void*), void* ctx, int depth);

    public:
        // Collects all name matches in this widget's subtree into out
        // (pre-order). Used by Window::FindWidgetDeep / FindAll while walking
        // top-level widgets - Window and Widget private members are not
        // shared, so a public collection entry is opened here. This widget
        // itself IS included (not just descendants).
        void findDeepCollect(const std::string& name, std::vector<Widget*>& out) {
            // MUST include self: Window::FindWidgetDeep calls this once per
            // top-level widget; searching only children_ would make a
            // top-level widget attached directly to the window permanently
            // unfindable - contradicting the "covers all top-level widgets
            // and their subtrees" promise (hit in a 2026-10-13 stress test:
            // a probe widget on the window was not found by objectName,
            // FindWidgetDeep returned nullptr).
            if (hasObjectName(name)) out.push_back(this);
            findAllByName(name, out);
        }

    private:

        // Recursively finds the first widget matching both name and type
        // (template layer: casting + type validation)
        template<class T>
        T* findDeep(const std::string& name) {
            // Cast each candidate: a name match with the wrong type must keep
            // searching (not stop at the first same-named widget)
            std::vector<Widget*> hits;
            findAllByName(name, hits);
            for (Widget* w : hits)
                if (T* t = dynamic_cast<T*>(w)) return t;
            return nullptr;
        }

        // Recursively finds all widgets matching both name and type
        template<class T>
        void findAllInto(const std::string& name, std::vector<T*>& out) {
            std::vector<Widget*> hits;
            findAllByName(name, hits);
            for (Widget* w : hits)
                if (T* t = dynamic_cast<T*>(w)) out.push_back(t);
        }

        // VisitTree's implementation: wraps any callable into a C-style
        // callback (keeps the bridge non-template)
        template<class F>
        void visitTreeImpl(F& f, int depth) {
            struct Bridge {
                static void Thunk(Widget& w, int d, void* ctx) {
                    (*static_cast<F*>(ctx))(w, d);
                }
            };
            visitTreeRaw(&Bridge::Thunk, &f, depth);
        }

        // Runtime diagnostic: a widget that does NOT arrange children was
        // given a child (reported once). hostType = the host widget's type
        // name (typeid(*this).name(), MSVC-style like "class zew"); childType
        // = the child widget's type name. This backstops AddWidget's
        // [[deprecated]] compile-time interception: the compiler cannot reach
        // the escape hatch (AddWidgetUnchecked) or prebuilt external
        // binaries; this runtime check catches those. Output goes to
        // OutputDebugStringA (visible under a debugger) and stderr,
        // consistent with the library's other diagnostics. The same
        // host+child type pair is reported only once (static de-dup table).
        // Implemented in ZeWidget.cpp.
        static void reportMisplacedChild(const char* hostType, const char* childType);

        // Truly destroys a direct child (with its subtree) and frees it from
        // the owned_ storage. Implemented in ZeWidget.cpp; used by SetWidget's
        // "replace" semantics - NOT RemoveWidget.
        void destroyChild(Widget& child);

        Widget* parent_ = nullptr;                        // Parent widget (borrowed, weak reference, non-owning)
        std::vector<Widget*> children_;                   // Children (borrowed, weak references, non-owning)
        Window* owner_ = nullptr;                         // Owning window (borrowed, weak reference, non-owning)

        // -------------------------------------------------------------------
        //  Frame paint fragment cache (retained mode; see ZePaintCache.h)
        // -------------------------------------------------------------------
        //  Stores the primitives THIS widget painted last frame - not
        //  children (each child stores its own). If unchanged, they are
        //  merged into the new frame directly, so dragging one widget costs
        //  the others only one memcpy.
        //
        //  Why pimpl instead of a direct PaintFragment member: a direct
        //  member would force the public header to include
        //  ZeFrameCollector.h (an internal header), polluting the delivery
        //  surface with rendering implementation.
        //  Why a RAW POINTER instead of unique_ptr: unique_ptr's destructor
        //  would be instantiated in every TU that constructs a Widget, and
        //  those TUs only see the forward declaration - direct compile error
        //  (C2338 can't delete an incomplete type). The raw pointer is
        //  deleted in ~Widget() (defined in the .cpp, where PaintFragment is
        //  complete).
        //  paintDirty_ is a CONSERVATIVE "appearance may have changed" flag:
        //  better to re-record once too often than once too few.
        PaintFragment* paintCache_ = nullptr;   // This widget's own fragment cache (owned)
        mutable bool paintDirty_ = true;  // true = Paint must re-run this frame (mutable: repaint request is logically const)
        mutable bool paintTimed_ = false;  // Last recording scheduled a timed wake (cursor blink) -> never cached

        // -------------------------------------------------------------------
        //  Storage for AddWidget value takeover
        // -------------------------------------------------------------------
        // Children handed in via AddWidget (this widget OWNS their storage).
        // Must be declared AFTER children_: reverse-order destruction frees
        // the storage first, so children's destructors still find children_
        // valid when they callback RemoveWidget.
        std::vector<std::unique_ptr<Widget>> owned_;

        // -------------------------------------------------------------------
        //  Widget-level filters (non-owning)
        // -------------------------------------------------------------------
        // One filter installation record
        struct FilterSlot {
            EventFilter* filter = nullptr;   // The filter itself (borrowed, weak reference)
            FilterId     id = 0;             // Handle assigned at install time
        };
        std::vector<FilterSlot> filters_;    // Filters installed on this widget (order = execution order)
        FilterId nextFilterId_ = 1;          // Next available handle (0 is the "invalid" sentinel, never allocated)

        // Allocates a new handle
        FilterId allocFilterId() { return nextFilterId_++; }

        // Changes the focus state and notifies OnFocusIn / OnFocusOut on
        // EDGES. Called only by Window during focus arbitration (friend class
        // Window). Setting the same value again is side-effect free (no
        // repeated notification).
        void setFocused(bool focused);

        // Detaches a filter by id (no visibility/logging; used by the
        // ~EventFilter callback)
        void detachEventFilter(FilterId id);

        // Runs this widget's filters over a mouse event; true = intercepted
        // by some filter. Same shape as Window: NOT short-circuited - all run
        // to completion, and any true intercepts the event.
        bool runMouseFilters(Window& win, MouseEvent& e);

        // Runs this widget's filters over a key event; true = intercepted by
        // some filter
        bool runKeyFilters(Window& win, KeyEvent& e);

        friend class Window;
        friend class EventFilter;      // Lets ~EventFilter unregister itself
        friend struct HitTestAccess;   // Lets Kit::HitTest read the hierarchy (see ZeHitTest.h)
        friend struct LayoutAccess;    // Lets layout restore pref / write only actual (see below)
        friend struct PaintCacheAccess;// Lets the renderer read/write the fragment cache (see ZePaintCache.h)
        void setOwner(Window* w) { owner_ = w; }
        Window* owner() const { return owner_; }

    protected:
        // -------------------------------------------------------------------
        //  Diagnostic: accessing an already-moved widget (the classic value-
        //  semantics pitfall; see isMovedFrom)
        // -------------------------------------------------------------------
        //  Called by accessors only on already-moved objects. In debug it
        //  prints a one-shot diagnostic; in release it is an empty function
        //  (call sites are compiled out; see the accessors). Why "one-shot"
        //  de-duplication: the same shell may be read many times (every
        //  repaint, in loops) - without de-dup it would spam - same approach
        //  as reportMisplacedChild.
        //
        //  protected (not private): accessors of derived widgets on the text
        //  / box layers also call it (e.g. TextWidget::Text()). Keeping it
        //  private would lock out every derived class's accessors.
        // -------------------------------------------------------------------

        // Prints a diagnostic when a moved-away shell is accessed (debug
        // only; empty in release). what = the accessed member name (e.g.
        // "Text"), included in the diagnostic text for locating. De-duplicated
        // with a static local: the same (type, what) pair reports once.
        void diagnoseMovedFrom(const char* what) const;

    protected:
        bool movedFrom_ = false;   // Whether already moved away (sole write point is the move ctor; see isMovedFrom)

        // -------------------------------------------------------------------
        //  File drag-and-drop (widget-level drop target; see the
        //  SetAcceptFileDrop section above)
        // -------------------------------------------------------------------
        bool acceptFileDrop_ = false;  // Whether this widget is a file-drop target
        // The derivation-free callback for SetOnFileDrop (the virtual
        // OnFileDrop's default implementation calls it). std::function because
        // Widget is a movable value; the member travels with the move
        // constructor.
        std::function<void(const std::vector<std::string>&)> onFileDrop_;
    };

    // =======================================================================
    //  As<T> - casts the Widget& received in event callbacks to a concrete
    //  widget type
    // =======================================================================
    //  The unified protocol (ZeSelfAction.h) makes every callback's "sender"
    //  a Widget&, so the whole library has one protocol and new widgets cost
    //  nothing; the price is needing a cast to use concrete interfaces
    //  (Text() / SetScrollY() ...). As<T>() is the official way to cast:
    //
    //      btn.SetOnClick([](Kit::Widget& self) {
    //          Kit::As<Kit::Button>(self).SetText("done");   // one line
    //      });
    //
    //  Why static_cast (not dynamic_cast): the caller KNOWS the sender's real
    //  type - the callback was attached to a concrete widget, and the attach
    //  point decides the type. static_cast gives zero runtime cost
    //  (dynamic_cast pays RTTI lookup on hot paths), and a type mismatch is a
    //  caller programming error, not a recoverable runtime condition. Debug
    //  builds additionally validate with dynamic_cast, turning a wrong attach
    //  point into an assertion instead of UB.
    // =======================================================================

    // Downcasts a widget reference to its KNOWN type T (the official way to
    // reach concrete interfaces in event callbacks). T = target widget type
    // (must derive from Widget). w = the widget reference to cast (usually
    // the callback's self). Returns the same object as T&. Validated in
    // debug; a pure static_cast in release (zero cost). The type passed as T
    // must match the object's real type - a mismatch is a programming error.
    template <class T>
    T& As(Widget& w) {
        static_assert(std::is_base_of_v<Widget, T>,
            "Kit::As<T>: T must derive from Kit::Widget");
#if defined(_DEBUG)
        // debug: a type mismatch surfaces immediately (assert, not UB); compiled out in release.
        T* p = dynamic_cast<T*>(&w);
        assert(p && "Kit::As<T>: 目标类型与实际控件类型不符（挂错回调？）");
        return *p;
#else
        return static_cast<T&>(w);
#endif
    }

    // const overload (for read-only access to widget parameters)
    template <class T>
    const T& As(const Widget& w) {
        static_assert(std::is_base_of_v<Widget, T>,
            "Kit::As<T>: T must derive from Kit::Widget");
#if defined(_DEBUG)
        const T* p = dynamic_cast<const T*>(&w);
        assert(p && "Kit::As<T>: 目标类型与实际控件类型不符（挂错回调？）");
        return *p;
#else
        return static_cast<const T&>(w);
#endif
    }

    // Dedicated channel for layout algorithms writing child geometry.
    //
    // Why not let layout call the public SetSize directly: SetSize also
    // updates the USER-INTENT size (pref), while layout writes back a RESULT.
    // Writing results through SetSize feeds them into the baseline -> the
    // next round uses them as input -> a one-way ratchet (grows with the
    // window, never shrinks back). So layout must go through here: restore
    // the reading with RestorePref, write the result with SetActual - neither
    // touches pref. See Widget::prefWidth, Widget::restorePrefSize,
    // Widget::setActualSize.
    struct LayoutAccess {
        // Restores a child's actual size to its "user-intent size" (called at
        // the start of each layout round)
        static void RestorePref(Widget& w) { w.restorePrefSize(); }

        // Writes a child's actual size (never touches pref)
        static void SetActual(Widget& w, float width, float height) {
            w.setActualSize(width, height);
        }
    };
}
