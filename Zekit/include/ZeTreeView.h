/**
 * @file ZeTreeView.h
 * @brief Kit::TreeView -- expandable / collapsible hierarchical list (single selection + vertical scrolling)
 *
 * @details
 * A tree is flattened into rows: one row per node, hierarchy expressed by indentation, and a leading `+` / `-`
 * expands or collapses the subtree; clicking a row selects it.
 *
 * @par Same structure as `ListView`
 * Also inherits `ScrollView` -- scrolling / clipping / scrollbars all come for free; also uses row widgets
 * to carry "what a row looks like", and row widgets do not handle any mouse events (events bubble up to `TreeView`,
 * which maps the row's actual rectangle back to a row index).
 *
 * @par How expand / collapse works
 * Each node has its own row widget from creation; collapsing just sets `SetVisible(false)` on all rows of the
 * subtree -- hidden children do not participate in layout, so positions need not be recomputed and
 * nothing is destroyed or rebuilt (a node's row widget is permanent, its address stable).
 *
 * @par Data model
 * Nodes live in a flat array, with parent-child relations expressed by `parent` indices (not a nested structure):
 * so traversal, lookup by id and `SetVisible` are all O(1), and deep copies are not a concern.
 * The node id returned by `AddRoot` / `AddChild` is the array index -- it never changes.
 *
 * @par Usage
 * @code
 * Kit::TreeView tree;
 * int a = tree.AddRoot("src");
 * tree.AddNode(a, "main.cpp");
 * tree.AddNode(a, "util.cpp");
 * int inc = tree.AddRoot("include");
 * tree.AddNode(inc, "ZeTreeView.h");
 * tree.SetSelectedNode(a);
 * win.AddWidget(tree);
 * @endcode
 *
 * @note "User selection / expansion" and programmatic `SetSelectedNode` / `SetExpanded` are two paths:
 *       only the former triggers callbacks.
 *
 * @see ZeListView.h the isomorphic one-dimensional version
 * @see ZeScrollView.h the source of scrolling capability
 */

#pragma once
#include "ZeScrollView.h"
#include "ZeSelfAction.h"
#include "ZeTextStyle.h"

#include <cstddef>
#include <string>
#include <vector>

namespace Kit {

    /**
     * @brief Expandable / collapsible hierarchical list
     */
    class TreeView : public ScrollView {
    public:
        TreeView();

        // -------------------------------------------------------------------
        //  Nodes (node id = internal index, valid forever once created)
        // -------------------------------------------------------------------

        /// @brief Appends a root node and returns the node id
        int AddRoot(std::string text);
        /// @brief Appends a child node under `parentId` and returns the node id
        /// @note If `parentId` is out of range it is treated as a root node (silently, no throw).
        /// @note Not named `AddChild` -- that name is taken by the base class's "add child widget to container"
        ///       (`LayoutBase::AddChild`), and a same-named method would shadow it.
        int AddNode(int parentId, std::string text);

        /// @brief Clears the whole tree (also clears selection; does not trigger callbacks)
        void Clear();

        /// @brief Total number of nodes
        std::size_t nodeCount() const { return nodes_.size(); }
        /// @brief Returns the text of a node (out of range returns a static empty string)
        const std::string& nodeText(int id) const;
        /// @brief Sets the text of a node (out of range is silently ignored)
        void SetNodeText(int id, std::string text);

        /// @brief Parent node id of a node (root node / out of range returns -1)
        int parentOf(int id) const;
        /// @brief Number of direct children of a node (out of range returns 0)
        int childCount(int id) const;
        /// @brief Depth of a node (root = 0; out of range returns -1)
        int depthOf(int id) const;

        // -------------------------------------------------------------------
        //  Expand / collapse
        // -------------------------------------------------------------------

        /// @brief Programmatic expand / collapse (does not trigger callbacks)
        void SetExpanded(int id, bool on);
        /// @brief Whether expanded (out of range returns false)
        bool isExpanded(int id) const;

        /// @brief Expand all / collapse all (does not trigger callbacks)
        void ExpandAll();
        void CollapseAll();

        // -------------------------------------------------------------------
        //  Selection
        // -------------------------------------------------------------------

        /// @brief Programmatically sets the selected node (does not trigger callbacks; `-1` = clear; out of range treated as -1)
        /// @note If the target node is currently invisible (an ancestor is collapsed), its ancestors are expanded automatically.
        void SetSelectedNode(int id);
        /// @brief Current selected node id (-1 = none)
        int selectedNode() const { return selected_; }
        /// @brief Text of the current selected node (empty string when nothing is selected)
        const std::string& selectedText() const;

        // -------------------------------------------------------------------
        //  Callbacks (triggered only on the user path)
        // -------------------------------------------------------------------

        /// @brief Sets the node selection callback (with sender + node id)
        void SetOnNodeSelected(SelfAction1<int> cb) { onSel_ = std::move(cb); }
        /// @brief Sender-only form (dispatch calls the payload version first, then this one)
        void SetOnNodeSelected(SelfAction cb) { onSelSelf_ = std::move(cb); }

        /// @brief Sets the expand / collapse callback (with sender + node id) -- triggered only when the user clicks the indicator
        void SetOnNodeToggled(SelfAction1<int> cb) { onTog_ = std::move(cb); }
        /// @brief Sender-only form
        void SetOnNodeToggled(SelfAction cb) { onTogSelf_ = std::move(cb); }

        // -------------------------------------------------------------------
        //  Appearance
        // -------------------------------------------------------------------

        /// @brief Row height (pixels, must be > 0)
        void SetRowHeight(float h);
        float rowHeight() const { return rowHeight_; }

        /// @brief Indentation per level (pixels) -- a child is shifted right by this much relative to its parent
        void SetIndentWidth(float w);
        float indentWidth() const { return indentW_; }

        /// @brief Width of the leading expand indicator area (pixels)
        void SetIndicatorWidth(float w);
        float indicatorWidth() const { return indicatorW_; }

        void SetRowHoverColor(const Color4f& c);
        void SetRowSelectedColor(const Color4f& c);
        /// @brief Color of the `+` / `-` indicator
        void SetIndicatorColor(const Color4f& c);

        /// @brief Row text style -- can be modified directly (call `ApplyTheme` again afterwards to sync existing rows)
        TextStyle& itemTextStyle() { return itemTextStyle_; }
        const TextStyle& itemTextStyle() const { return itemTextStyle_; }

        void SetBackground(const Color4f& c);
        /// @brief Outer frame (line width <= 0 = no border)
        void SetBorder(float width, const Color4f& c);
        void SetRadius(float r);

        // -------------------------------------------------------------------
        //  Keyboard / mouse
        // -------------------------------------------------------------------
        // @brief `Up` / `Down` move selection, `Right` expands, `Left` collapses, `Home` / `End` jump to first / last
        bool OnKeyPress(KeyEvent& e) override;

        /// @brief Left button press: scrollbar first; otherwise on the indicator area = expand / collapse, on a row = prepare selection
        bool OnMousePress(MouseEvent& e) override;

        /// @brief Left button release: selection is committed only if released on the row that was pressed
        bool OnMouseRelease(MouseEvent& e) override;

        // -------------------------------------------------------------------
        //  Paint / theme
        // -------------------------------------------------------------------
        // @brief Paints the outer frame (background + border), then lets the base class paint the scrollbars
        void Paint(FrameCollector& fc) const override;

        /// @brief Re-injects row height / indentation / colors / text style from the theme `tree` section
        void ApplyTheme(const Theme& t) override;

    private:
        /// @brief A node
        struct Node {
            std::string text;
            int  parent = -1;        /// < Parent node id (-1 = root)
            int  depth = 0;          /// < Depth (root = 0)
            bool expanded = true;    /// < Whether its own subtree is expanded
            bool hasChildren = false;/// < Whether it has children (determines whether the indicator is painted)
        };

        /// @brief Builds all row widgets from nodes_ (clears and rebuilds; used only on Add/Clear)
        void rebuildRows();
        /// @brief Whether the node is visible right now (all ancestors expanded)
        bool nodeVisible(int id) const;
        /// @brief Recomputes visibility of every row widget + refreshes visible_ (call after expansion state changes)
        void refreshVisibility();
        /// @brief Pushes node info (indentation / indicator state / selection) to a row widget
        void syncRow(int id);
        /// @brief Which row widget a point falls on (returns the node id, -1 = not on any row)
        int  nodeAtPoint(float px, float py) const;
        /// @brief Whether a point falls on the expand indicator area of a node
        bool hitIndicator(float px, float py, int id) const;
        /// @brief User clicks the indicator -> toggles expansion state + sends notification
        void toggleByUser(int id);
        /// @brief Scrolls a row fully into the viewport
        void ensureVisible(int id);
        /// @brief Selection exit for the user path: sets the value + notifies
        void selectByUser(int id);
        /// @brief Finds an adjacent node in the visible node sequence (dir = +/-1)
        int  nextVisible(int id, int dir) const;
        /// @brief Requests a repaint
        void requestRedraw();

        // ---- Data ----
        std::vector<Node>    nodes_;
        std::vector<int>     visible_;   /// < Visible node ids (in display order)
        std::vector<Widget*> rows_;      /// < Row widgets (one-to-one with nodes_)

        // ---- Selection state ----
        int selected_ = -1;
        int pressedRow_ = -1;

        // ---- Appearance ----
        float   rowHeight_ = 28.0f;
        float   indentW_ = 18.0f;
        float   indicatorW_ = 20.0f;
        Color4f rowHoverColor_{ 0, 0, 0, 0 };
        Color4f rowSelectedColor_{ 0, 0, 0, 0 };
        Color4f indicatorColor_{ 0, 0, 0, 0 };
        TextStyle itemTextStyle_;
        Color4f background_{ 0, 0, 0, 0 };
        Color4f borderColor_{ 0, 0, 0, 0 };
        float   borderWidth_ = 0.0f;
        float   radius_ = 0.0f;

        // ---- Callbacks ----
        SelfAction1<int> onSel_;
        SelfAction       onSelSelf_;
        SelfAction1<int> onTog_;
        SelfAction       onTogSelf_;
    };

} // namespace Kit
