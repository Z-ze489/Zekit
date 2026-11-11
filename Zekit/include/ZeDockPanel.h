// ZeDockPanel.h: a draggable dock panel grid container.
//
// Lays out titled panes on a grid with draggable splitters; dragging a title
// bar onto another pane's left/right/top/bottom/center splits the grid or
// swaps panes in place. Unlike nested Splitters it supports drag-to-rearrange,
// user-resizable proportions and arbitrary nesting depth.
//
// Deliberately out of scope in this version: tab merging (needs TabView
// RemoveTab), floating-pane to floating-pane docking (drop a float back into
// the host first), and layout persistence (record paneCount() / paneSideOf()
// yourself, or wait for tree serialization).
//
// Look (2026-11-03 revision): rounded card panes (default 6px, no border) with
// quiet gaps of splitterWidth; splitters only reveal a grip on hover/drag.
// The divider is "space left", not "a line drawn", so nesting never shows
// black frames. All adjustable via SetPaneRadius / SetPaneBorderWidth /
// SetHandleAlwaysVisible / SetHandleThickness / SetSplitterWidth and the
// dockPanel theme section.
//
// Pane content widgets are managed by value semantics (AddPane moves them into
// the tree) and AddPane returns a stable reference to the placed copy.
// See also ZeSplitter.h (simple two-pane version) and ZePanel.h (base class).

#pragma once
#include "ZePanel.h"
#include "ZeSelfAction.h"
#include "ZeTheme.h"     // DockPanelStyle / TextStyle (the container needs its theme section)

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Kit {

    // Dock side (shared by drag drop targets and code-driven docking)
    enum class DockSide {
        Center,   // Center: the root cell when the tree is empty, otherwise swap with the target
        Left,     // Left half of the target
        Right,    // Right half of the target
        Top,      // Top half of the target
        Bottom,   // Bottom half of the target
    };

    // Dock panel: titled panes on a grid, with drag docking and draggable splitters
    class DockPanel : public Panel {
    public:
        DockPanel();
        ~DockPanel() override;

        // ---- Value semantics: the container can be configured before being
        // added to a window (win.AddWidget(DockPanel())) ----
        //  The move constructor must be declared explicitly: declaring a
        //  destructor suppresses the implicit move, so AddWidget's
        //  make_unique<DockPanel>(std::move(w)) would fall back to the copy
        //  constructor (deleted in the base) and fail to compile.
        //  Moving is safe because DockPane::ownerPane() computes the owner along
        //  the parent chain instead of caching a container reference, so there
        //  is no dangling "pane pointing at the old address" after a move.
        DockPanel(DockPanel&&) = default;
        DockPanel& operator=(DockPanel&&) = delete;
        DockPanel(const DockPanel&) = delete;
        DockPanel& operator=(const DockPanel&) = delete;

        // ===================================================================
        //  Adding panes
        // ===================================================================
        // Adds a titled pane docked at `side`.
        // title: title bar text. content: content widget (value semantics: an
        // lvalue is moved into the tree). Returns the reference of the placed
        // copy in the tree (address-stable across layout changes).
        //
        // Placement rules (identical to the drag drop semantics, so code-built
        // and drag-built layouts run through one logic):
        // - Empty tree: any side makes it the root cell (fills the container);
        //   Center is the natural choice.
        // - Non-empty + Left/Right/Top/Bottom: wraps the whole tree on that side
        //   ("Center, then Left, Right, Bottom" yields the classic grid).
        // - Non-empty + Center: sits beside the root pane (same as the default
        //   Right split) -- the pane is never silently dropped.
        //
        // The new side's default width/height comes from SetDefaultDockSize
        // (200px): "left column 200px" is easier to write and read than 25%.
        template<class T>
        T& AddPane(std::string title, T&& content, DockSide side = DockSide::Center) {
            // 1. The pane frame (with title bar) enters this container's child
            //    list first, same value semantics as Splitter::SetFirst(Panel()):
            //    the temporary is moved into the tree and the return value is the
            //    tree's copy. The pane frame holds no container reference (see
            //    DockPane::ownerPane), so moving the container is always safe.
            DockPane& pane = AddChild(DockPane(std::move(title)));
            // 2. Content goes into the pane (value semantics, parent chain set up)
            T& placed = pane.AddChild(std::forward<T>(content));
            pane.setContent(placed);
            // 3. Register in the pane table -- required: paneCount / paneAt /
            //    paneOf and all "find pane by content" APIs walk this table (the
            //    dock tree only holds Leaf pointers; this table is the single
            //    ledger of which panes still live in this container).
            panes_.push_back(&pane);
            // 4. Attach to the dock tree (touches only the tree structure, not the widget tree)
            attachPane(&pane, side);
            return placed;
        }

        // Removes a pane (its content widget is destroyed too). Returns true if found.
        // Do not remove panes during a drag: pointer capture is attached to the
        // pane's title bar and the release would reach an already destroyed
        // widget (known m_pressed dangling issue). Remove after release.
        bool RemovePane(Widget& content);

        // Removes all panes (content widgets destroyed too)
        void ClearPanes();

        // Pane count (including panes hidden via HidePane)
        std::size_t paneCount() const { return panes_.size(); }

        // Content widget of pane `index` (order = add order; nullptr if out of range)
        Widget* paneAt(std::size_t index) const;

        // Title of pane `index` (empty string if out of range)
        const std::string& paneTitleAt(std::size_t index) const;

        // Title of the pane holding `content`
        void SetPaneTitle(Widget& content, std::string title);
        const std::string& paneTitle(Widget& content) const;

        // Index of the pane holding `content` (-1 if not found)
        int indexOf(const Widget& content) const;
        // Whether `content` belongs to this container
        bool contains(const Widget& content) const { return indexOf(content) >= 0; }

        // ===================================================================
        //  Docking (code-driven)
        // ===================================================================
        // Re-docks a pane already in the container (equivalent to dragging it to
        // a side of the whole tree). Returns false if the content does not belong
        // to this container or the tree only holds it.
        bool Dock(Widget& content, DockSide side);

        // Docks a pane to one side of another pane (the key to fine grids).
        // content: pane to move; target: target pane (not itself);
        // side: Center swaps the two panes, other sides split the target's cell.
        // Returns false if either content is not in this container, they are the
        // same, or the target is invisible.
        bool Dock(Widget& content, Widget& target, DockSide side);

        // ===================================================================
        //  Visibility (hiding keeps the content in the tree; show it again anytime)
        // ===================================================================
        // Collapses a pane (neighbors take over its cell; the splitter disappears)
        void HidePane(Widget& content);
        // Shows it again
        void ShowPane(Widget& content);
        // Whether the pane is currently visible
        bool isPaneVisible(const Widget& content) const;

        // ===================================================================
        //  Layout parameters
        // ===================================================================
        // Title bar height in pixels (also the top edge of the pane content area)
        void SetTitleBarHeight(float h);
        float titleBarHeight() const { return titleBarHeight_; }

        // Splitter thickness in pixels (also the gap width between cells)
        void SetSplitterWidth(float w);
        float splitterWidth() const { return splitterW_; }

        // -------------------------------------------------------------------
        //  Appearance (rounded cards + quiet splitters)
        // -------------------------------------------------------------------
        // Corner radius of a pane in pixels (0 = square). The title bar rounds
        // only its top two corners, so no round notches appear under it. The
        // radius is visual only: hit testing stays rectangular (library-wide
        // convention); with gaps >= 6px the difference is invisible, so no
        // per-pixel hit testing is added.
        void SetPaneRadius(float r);
        float paneRadius() const { return paneRadius_; }

        // Pane border (default 0 = none). Panes already have gaps and background
        // contrast; an extra outline caused the reported "black line" look. Only
        // worth enabling when a pane sits on a non-container background (e.g. a
        // lone pane in a floating window).
        void SetPaneBorder(float width, const Color4f& color);
        float paneBorderWidth() const { return paneBorderW_; }
        const Color4f& paneBorderColor() const { return paneBorderC_; }

        // Whether splitters are also drawn at rest (default false: hover/drag
        // only). Turning this on restores the old "line in every gap" look;
        // kept as an option, not recommended.
        void SetHandleAlwaysVisible(bool on);
        bool isHandleAlwaysVisible() const { return handleAlwaysVisible_; }

        // Grip thickness in pixels (0 = auto: 45% of the gap width, clamped to 2-4px)
        void SetHandleThickness(float px);
        float handleThickness() const { return handleThickness_; }

        // Minimum cell size (lower bound when dragging splitters)
        void SetPaneMinSize(float minW, float minH);
        float minPaneWidth() const { return minPaneW_; }
        float minPaneHeight() const { return minPaneH_; }

        // Main-axis pixels a code-docked pane takes (default 200)
        void SetDefaultDockSize(float px);
        float defaultDockSize() const { return defaultDockSize_; }

        // Whether pane title bars can be dragged (the close button keeps working)
        void SetDragEnabled(bool on) { dragEnabled_ = on;  RequestWindowRedraw();}
        bool isDragEnabled() const { return dragEnabled_; }

        // ===================================================================
        //  Floating (tear-off): dragging outside the host window turns the pane
        //  into a standalone borderless window
        // ===================================================================
        // Whether tearing off into a floating window is allowed (default yes).
        // The torn-off window shares the main window's renderer context
        // (ChildKind::Owned + SetBorderless); its title bar drags and its four
        // edges resize. Dropping back inside the host re-docks with the same
        // drop semantics as in-container dragging. When the platform does not
        // support owned windows (Capability::ChildWindow not declared), this
        // degrades to the old behavior: dragging out = cancel.
        void SetFloatEnabled(bool on) { floatEnabled_ = on;  RequestWindowRedraw();}
        bool isFloatEnabled() const { return floatEnabled_; }
        // Whether a pane is currently floating (host-side query)
        bool hasFloatingPane() const { return floatDock_ != nullptr; }

        // ===================================================================
        //  Interaction state (read by both painting and diagnostics)
        // ===================================================================
        // Whether a pane is being dragged right now
        bool isDragging() const { return dragging_; }
        // Current drop side while dragging (Center when not dragging / no target)
        DockSide dragTargetSide() const { return dropSide_; }
        // Content widget of the pane under the drop target (nullptr if none)
        Widget* dragTargetPane() const { return dropTargetContent_; }
        // Drop target rect (client coordinates; returns false when no target)
        bool dropRect(float& x, float& y, float& w, float& h) const;

        // ===================================================================
        //  Callbacks
        // ===================================================================
        // The user clicked the close button of a pane's title bar (sender = this
        // container, payload = the content widget). This class does not collapse
        // automatically: the callback decides whether to collapse or confirm.
        // Without a callback it falls back to collapsing in place (HidePane) --
        // otherwise the close button does nothing.
        void SetOnPaneCloseRequest(SelfAction1<Widget*> cb) { onPaneCloseRequest_ = std::move(cb); }

        // Dock structure changed (drag settled / code Dock succeeded / pane removed)
        void SetOnDockChanged(SelfAction cb) { onDockChanged_ = std::move(cb); }

        // ===================================================================
        //  Measurement / layout / painting / theme
        // ===================================================================
        // Natural size = the grid size computed recursively from the dock tree
        // (not a linear sum of children)
        float hintWidth() const override;
        float hintHeight() const override;

        // Lays out every cell recursively + computes splitter rects (shared by
        // painting and hit testing)
        void DoLayout() override;

        // Container skeleton + all splitters + the drop target highlight
        void Paint(FrameCollector& fc) const override;

        // Injects from the theme's dockPanel section (forwarded to every pane)
        void ApplyTheme(const Theme& t) override;

        // ---- Interaction ----
        bool OnMousePress(MouseEvent& e) override;
        bool OnMouseMove(MouseEvent& e) override;
        bool OnMouseRelease(MouseEvent& e) override;
        void OnMouseLeave() override;

        // Double arrow over a splitter (or while dragging one) -- matches Splitter
        Cursor cursorAt(float x, float y) const override;

    private:
        // -------------------------------------------------------------------
        //  Internal part: one pane (title bar + content area)
        // -------------------------------------------------------------------
        //  Must be defined in the header (not only in the .cpp): AddPane is a
        //  template whose body calls AddChild on DockPane, which needs the
        //  complete type. A private nested class keeps it out of the public
        //  surface (external code cannot name Kit::DockPanel::DockPane) without
        //  hurting AddPane. Same technique as ToolBar's Strip / Spacer.
        class DockPane : public Panel {
        public:
            explicit DockPane(std::string title);

            // The outer container is the only legitimate pane-dismantler
            // (tear-off needs title_ / content_). Nested classes can access the
            // outer class's private members, but not the other way around
            // (C2248 on MSVC/GCC), hence this explicit declaration.
            friend class DockPanel;

            // The container this pane belongs to (computed along the parent
            // chain, no cached pointer). Returns nullptr when not found
            // (transient moments while a pane is being removed). Why not a
            // DockPanel& owner_ member: the container is a movable value
            // (win.AddWidget(DockPanel()) relocates it) and the pane frame is a
            // child that moves with the widget tree -- a cached reference would
            // dangle at the pre-move address. One dynamic cast per mouse event
            // is negligible next to "never wrong after moves".
            DockPanel* ownerPane() const;

            void SetTitle(std::string title) { title_ = std::move(title);  RequestWindowRedraw();}
            const std::string& title() const { return title_; }

            // Content widget (filled back in by the container after AddPane)
            Widget* content() const { return content_; }
            // Claims the content widget for this cell (it becomes the cell's only
            // child). Also sets its main-axis flex to 1: DockPane is a vertical
            // Panel and a Preferred child would only get its own hint height
            // (e.g. 66px), leaving the rest as blank pane background instead of
            // content filling the cell (verified by pixel-scanning screenshots
            // on 2026-10-14). Cross-axis (width) is covered by Panel's
            // alignItems = Stretch.
            void setContent(Widget& c) {
                content_ = &c;
                c.SetFlexGrow(1.0f);
            }

            // Injects from the theme section (background / border / title bar / text / close colors)
            void setStyle(const DockPanelStyle& s, float titleBarHeight);
            // Whether the title bar is currently pressed (drag highlight)
            void setTitleActive(bool on);

            void Paint(FrameCollector& fc) const override;
            void ApplyTheme(const Theme& t) override;

            bool OnMousePress(MouseEvent& e) override;
            bool OnMouseMove(MouseEvent& e) override;
            bool OnMouseRelease(MouseEvent& e) override;
            void OnMouseLeave() override;

        private:
            // A rect (the few shapes shared by painting and hit testing)
            struct Rect { float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f; };
            // Title bar rect (the top edge of this pane; height = theme titleBarHeight)
            Rect titleBarRect() const;
            // Close button square (right side of the title bar; empty rect when too small to fit)
            Rect closeRect() const;
            // Whether (px, py) is on the title bar's draggable area (outside the close button)
            bool hitTitleBar(float px, float py) const;
            // Whether (px, py) is on the close button
            bool hitClose(float px, float py) const;

            std::string title_;
            Widget*    content_ = nullptr;   // Borrowed (content owned by this pane)

            bool hoverClose_ = false;        // Close button hover state
            bool pressedClose_ = false;      // Close button pressed (counts only if released on it)
            bool titleActive_ = false;       // Title bar held / being dragged

            // ---- Theme injection ----
            Color4f   background_{ 0, 0, 0, 0 };
            Color4f   borderColor_{ 0, 0, 0, 0 };
            float     borderWidth_ = 0.0f;
            float     radius_ = 0.0f;              // Card corner radius (title bar takes the top two)
            Color4f   titleBarColor_{ 0, 0, 0, 0 };
            float     titleDividerAlpha_ = 0.5f;   // Alpha multiplier of the title bar divider line
            Color4f   accentColor_{ 0, 0, 0, 0 };   // Accent while the title bar is held (= theme handleActiveColor)
            TextStyle titleStyle_;
            Color4f   closeColor_{ 0, 0, 0, 0 };
            Color4f   closeHoverColor_{ 0, 0, 0, 0 };
            float     titleBarHeight_ = 26.0f;
            // Bottom padding of the content area (= card radius, exposing the bottom corner arcs)
            float     padBottom_ = 0.0f;
            float     closeSize_ = 14.0f;
        };

        // -------------------------------------------------------------------
        //  Internal part: drop target highlight (overlay, not hit-testable)
        // -------------------------------------------------------------------
        class DropIndicator : public Widget {
        public:
            DropIndicator() = default;

            // Wants a second paint pass only while dragging with a drop target
            bool paintsOnTop() const override;
            // Always false: the hint is for looking, it must not block the mouse
            bool HitTest(float x, float y) const override { (void)x; (void)y; return false; }
            void Paint(FrameCollector& fc) const override;
            void ApplyTheme(const Theme& t) override;

        private:
            Color4f dropColor_{ 0, 0, 0, 0 };
            float   dropBorderWidth_ = 2.0f;
        };

        // -------------------------------------------------------------------
        //  Dock tree
        // -------------------------------------------------------------------
        // Tree node: either a cell (leaf = one pane) or a split
        struct Node {
            bool  leaf = true;

            // ---- Leaf ----
            DockPane* pane = nullptr;      // Pane in this cell (borrowed; widget owned by the container)

            // ---- Split ----
            bool  horizontal = true;       // true = left/right split (first on the left); false = top/bottom (first on top)
            float ratio = 0.5f;            // First's share of the available main axis (0-1)
            int   first = -1;              // Child node index (-1 = none)
            int   second = -1;

            // Pixels requested by code docking for this side (converted to a
            // ratio at first layout). Not converted inside attachPane because
            // the container is usually still 0x0 then (AddPane is typically
            // called before Show()), which would yield NaN / 0 ratios.
            float pendingPx = 0.0f;
            bool  pendingIsFirst = false;  // Does pendingPx refer to the first (Left/Top) side
            bool  pending = false;

            // Splitter rect computed this frame (written by DoLayout, read by Paint / hit testing)
            float hx = 0.0f, hy = 0.0f, hw = 0.0f, hh = 0.0f;
            // This cell's rect computed this frame (leaf; read by drop-target tests)
            float cx = 0.0f, cy = 0.0f, cw = 0.0f, ch = 0.0f;
            // The rect assigned to this node itself (split nodes). Splitter drag
            // ratio math must use it as the denominator, NOT the whole
            // DockPanel's width(): with nested splits this node's main-axis
            // length is only a segment assigned by the outer level, and using
            // the whole panel makes the splitter lag behind the pointer
            // (a 1px drag moves only k px).
            float aw = 0.0f, ah = 0.0f;
        };

        // ---- Tree operations (all in the .cpp) ----
        int  newLeaf(DockPane* pane);
        int  newSplit(bool horizontal, int first, int second, float ratio);
        void attachPane(DockPane* pane, DockSide side);
        void compact();                       // Rebuilds the node table by DFS from the root, dropping unreachable/empty nodes
        int  findLeaf(const DockPane* pane) const;
        bool subtreeVisible(int idx) const;
        void layoutNode(int idx, float x, float y, float w, float h);
        float hintNode(int idx, bool alongWidth) const;
        void notifyDockChanged();

        DockPane* paneOf(const Widget* content) const;

        // ---- Splitter dragging ----
        // Which splitter is hit
        int hitSplitter(float px, float py, float* rx = nullptr, float* ry = nullptr,
                        float* rw = nullptr, float* rh = nullptr) const;
        // Updates this splitter's ratio while dragging (relative to the drag
        // start, avoiding error accumulation)
        void applySplitterDrag(float px, float py);

        // Re-attaches a subtree: docks `dragged` to a side of `target` (shared by
        // dragging and Dock). Both must already be in the tree and distinct.
        bool restructure(DockPane* dragged, DockPane* target, DockSide side);

        // ---- Pane dragging (forwarded by DockPane) ----
        // Title bar pressed: start a candidate drag (not moving yet)
        void beginPaneDrag(DockPane& pane, float px, float py);
        // Pointer moved: update the drop target
        void updatePaneDrag(float px, float py);
        // Released: settle (no target = cancel)
        void finishPaneDrag();
        // Current drop target (client coordinates): computes side + preview rect
        bool computeDropTarget(float px, float py, int& nodeIdx, DockSide& side) const;
        void dropRectOf(int nodeIdx, DockSide side, float& x, float& y, float& w, float& h) const;
        // Close button clicked (forwarded by DockPane)
        void requestPaneClose(DockPane& pane);

        // ---- Floating (tear-off) ----
        // Is the pointer outside the host window (with a forgiving margin)
        bool outsideHostWindow(float px, float py, float margin) const;
        // Tear-off: moves the content into a new borderless floating window (the
        // pane frame stays for now, destroyed on release)
        void tryTearOut(float px, float py);
        // Removes a pane frame (out of the tree and the ledger; does NOT destroy
        // the widget -- the tear-off path needs it)
        void detachPaneFrame(DockPane* pane);
        // Host-side adoption: re-docks content moved back from the float window
        // using the drop semantics
        bool adoptTornPane(const std::string& title, std::unique_ptr<Widget> content,
                           float px, float py);
        // Drop preview for external drags (float title dragged back into the host)
        void previewExternalDrop(float px, float py);
        void clearExternalPreview();
        // Follows the pointer after tear-off (host client coordinates)
        void floatFollowTo(float px, float py);
        // Float side: title drag = move the window + send a preview to the host
        void floatTitleDragMove(float px, float py);
        // Float side: title drag finish (pointer inside the host = re-dock, else stay floating)
        void finishFloatDrag();
        // Float edge hit test (resize hot zone; bit0=L 1=R 2=B)
        bool resizeZoneAt(float px, float py, unsigned& mask) const;
        void applyResizeDrag(float px, float py);

    private:
        std::vector<DockPane*> panes_;   // Pane frames (add order; borrowed, widgets owned by the container)
        std::vector<Node>      nodes_;   // Dock tree (root_ is the root index, -1 = empty)
        int  root_ = -1;

        // ---- Layout parameters ----
        float titleBarHeight_ = 26.0f;
        float splitterW_ = 6.0f;
        float minPaneW_ = 56.0f;
        float minPaneH_ = 40.0f;
        float defaultDockSize_ = 200.0f;
        bool  dragEnabled_ = true;

        // ---- Appearance (cards + quiet splitters) ----
        float paneRadius_ = 8.0f;          // Pane corner radius (theme injects again)
        float paneBorderW_ = 0.0f;         // Pane border width (off by default)
        Color4f paneBorderC_{ 0, 0, 0, 0 };// Pane border color
        float handleThickness_ = 0.0f;     // Grip thickness (0 = auto)
        bool  handleAlwaysVisible_ = false;// Whether splitters are drawn at rest

        // ---- "Explicitly set by the user" flags (theme injection keeps user
        // intent, same rule as markStyled library-wide) ----
        bool  titleBarSet_ = false;
        bool  splitterWSet_ = false;
        bool  minPaneSet_ = false;
        bool  dockSizeSet_ = false;
        bool  paneRadiusSet_ = false;
        bool  paneBorderSet_ = false;
        bool  handleThicknessSet_ = false;
        bool  handleVisibleSet_ = false;

        // ---- Splitter drag state ----
        bool  splitDragging_ = false;
        int   splitNode_ = -1;
        float splitStartPos_ = 0.0f;
        float splitStartRatio_ = 0.5f;
        int   hoverSplitNode_ = -1;     // Splitter under the pointer (-1 = none; draws the highlight)

        // ---- Pane drag state ----
        bool       dragging_ = false;
        DockPane*  dragPane_ = nullptr;
        float      dragStartX_ = 0.0f, dragStartY_ = 0.0f;
        DockSide   dropSide_ = DockSide::Center;
        int        dropNode_ = -1;
        Widget*    dropTargetContent_ = nullptr;   // Content of the target pane (nullptr if none)

        // ---- Floating (tear-off) state ----
        bool  floatEnabled_ = true;    // Allow tearing out of the host window
        bool  floatRoot_ = false;      // This container is the float window's root (title = move/re-dock, edges = resize)
        Window* floatWin_ = nullptr;   // floatRoot_: the float window (borrowed; lifetime managed by the host's table)
        DockPanel* hostDock_ = nullptr;   // floatRoot_: the host dock this float was torn from (borrowed)
        DockPanel* floatDock_ = nullptr;  // Host side: the root dock inside the float window (borrowed)
        DockPane*  tornPane_ = nullptr;   // Host side: the emptied old pane frame (destroyed on release)

        // Tear-off follow (host side: pointer capture is on the host window, so
        // coordinates are always host client coordinates)
        bool  tornFollow_ = false;
        float followDx_ = 0.0f, followDy_ = 0.0f;   // Grab offset relative to the float window's origin
        float lastHostX_ = 0.0f, lastHostY_ = 0.0f; // Last pointer position (host client coordinates)

        // Float title drag / external preview / resize (each side uses what it needs).
        // Pointer positions are always converted to SCREEN coordinates before
        // window geometry math: client coordinates move inversely with the
        // window itself, and using them to decide where the window should go is
        // a positive feedback loop (observed as violent vibration).
        bool  floatDrag_ = false;
        float dragGrabX_ = 0.0f, dragGrabY_ = 0.0f;  // Press point offset from the float window's client origin
        float floatLastScreenX_ = 0.0f, floatLastScreenY_ = 0.0f; // Last pointer position (screen coordinates)
        bool  extPreviewOn_ = false;                    // Drop preview for external drags (drawn by the host)
        int   extNode_ = -1;
        DockSide extSide_ = DockSide::Center;
        bool  resizing_ = false;
        unsigned resizeMask_ = 0;                       // bit0=L 1=R 2=B
        float rsGrabScreenX_ = 0.0f, rsGrabScreenY_ = 0.0f;  // Resize grab point (screen coordinates)
        float rsWinX_ = 0.0f, rsWinY_ = 0.0f, rsWinW_ = 0.0f, rsWinH_ = 0.0f;

        // ---- Theme injection (the container's own part) ----
        DockPanelStyle style_;          // Last injected section (reused when heights change, avoiding a re-inject from the built-in theme)
        Color4f containerBg_{ 0, 0, 0, 0 };
        Color4f containerBorder_{ 0, 0, 0, 0 };
        float   containerBorderW_ = 0.0f;
        float   containerRadius_ = 0.0f;
        Color4f handleColor_{ 0, 0, 0, 0 };
        Color4f handleActiveColor_{ 0, 0, 0, 0 };
        float   handleRadius_ = 2.0f;

        // ---- Callbacks ----
        SelfAction1<Widget*> onPaneCloseRequest_;
        SelfAction           onDockChanged_;
    };

} // namespace Kit
