/**
 * @file ZeMenuBar.h
 * @brief Kit::MenuBar -- VS-style custom menu bar: drop-down menu area + free widget area + optional window drag
 *
 * @details
 * A bar spanning the top of the window, in four segments (isomorphic to the VS 2026 top custom menu bar):
 *
 * ```
 * ┌──────────────────────────────────────────────────────────────────────┐
 * │ 🟦 Title   File(F)  Edit(E)  View(V)   [▶ Run] [🔍 Search…]        [✕] │
 * └──────────────────────────────────────────────────────────────────────┘
 *   └─ leading ─┘└── drop-down menu area ──┘└── middle free widget area ──┘         └─ right ─┘
 * ```
 *
 *  · Leading area: widgets added via `AddLeading(...)` are always packed flush to the far left (app icon,
 *    brand logo, sidebar toggle -- advanced apps conventionally place these before the menu titles).
 *  · Drop-down menu area: `AddMenu("File(F)")` adds a title, `SetMenuItems` gives the content --
 *    the content is `Kit::MenuItem` (same as `MenuModal`: text / shortcut hint /
 *    separator / disabled item / action). Clicking a title pops up directly below the title; clicking outside / Esc /
 *    activating an item closes it; while open, clicking another title switches directly to it (no need to close then open).
 *  · Middle free widget area: any widget added via `AddChild / AddWidget`
 *    (button / icon / input box / separator ...) flows after the menus.
 *    Developers can freely add widgets -- the menu bar does not care what you put in, it only lays out and paints.
 *  · Right area: widgets added via `AddRight(...)` are always packed to the right (window title,
 *    close button -- in borderless mode this is your "self-drawn title bar right half").
 *
 * @par Window drag (the correct approach for borderless mode)
 * After `SetBorderless(true)` the window has no system title bar and cannot be dragged -- the standard solution is not to add a
 * "drag anywhere" switch to Window (that would drag the window on every click and break all widgets),
 * but to let the menu bar double as the title bar: after `SetWindowDragEnabled(true)`, holding the menu bar's
 * blank area (a press that lands on no menu title and is not consumed by any child widget) is handed to the system
 * to drag the whole window (`Window::StartWindowDrag`, the HTCAPTION approach). Default off --
 * when not enabled it is an ordinary menu bar with zero behavior change. While maximized / fullscreen the system does not respond to
 * title bar drag anyway, so no extra check is needed.
 *
 * @par How it is built (no new low-level capability)
 * | Requirement | Where it comes from for free |
 * |---|---|
 * | Horizontal layout + free widget addition | `LayoutBase` (`DoLayout` written by hand: leading -> menu area -> middle -> right) |
 * | Drop-down menu body | Reuses `Kit::MenuModal` as-is (as an internal child widget, not one line of popup logic rewritten) |
 * | Switch directly when clicking another title while open | Derives `BarMenu` and overrides `OnMousePress`: a close press outside the panel is reported back to the bar |
 * | Window drag | `Window::StartWindowDrag()` (the library-level standard for self-drawn title bars) |
 * | Paint and hit test share one geometry | The title rect cache written by `DoLayout`; both paint and hit test only read it |
 *
 * @par Usage
 * @code
 * Kit::MenuBar bar;
 * bar.SetObjectName("menubar");
 *
 * // Leading area: icon / logo always far left, before the menu titles
 * Kit::Icon logo;
 * logo.SetSize(28.0f, 28.0f);
 * bar.AddLeading(logo);
 *
 * int mFile = bar.AddMenu("File(F)");
 * bar.SetMenuItems(mFile, {
 *     { .text = "New", .shortcut = "Ctrl+N", .action = []{ ...; } },
 *     { .text = "Open", .shortcut = "Ctrl+O", .action = [](Kit::Widget& s){ ...; } },
 *     { .separator = true },                  // separator
 *     // The correct way to close the window: self is injected by the library on dispatch (always points to the entity in the tree) +
 *     //   the Widget window convenience interface -- no capture, no dangling, no need to pass Window in
 *     { .text = "Exit", .shortcut = "Alt+F4",
 *       .action = [](Kit::Widget& s){ s.CloseWindow(); } },
 * });
 * bar.AddMenu("Edit(E)");
 * bar.SetMenuItems(1, { { "Undo", "Ctrl+Z" }, { "Redo", "Ctrl+Y" } });
 *
 * // Middle: put anything (here a button + a search box)
 * Kit::Button run("▶ Run");
 * run.SetSize(96.0f, 28.0f);
 * bar.AddChild(run);
 * Kit::InputBox search("Search (Ctrl+Q)");
 * search.SetSize(180.0f, 28.0f);
 * bar.AddChild(search);
 *
 * // Right: packed to the right (the "title + ✕" of borderless mode)
 * Kit::Button close("✕");
 * close.SetSize(36.0f, 28.0f);
 * close.SetOnClick([](Kit::Widget& self) { self.CloseWindow(); });
 * bar.AddRight(close);
 *
 * bar.SetWindowDragEnabled(true);   // off by default; only borderless mode needs it for "hold to drag"
 *
 * root.AddChild(bar);               // put it in the first row of the vertical root panel
 * @endcode
 *
 * @par Interaction
 * | Operation | Behavior |
 * |---|---|
 * | Click a menu title | Pops up the corresponding drop-down directly below the title (all `MenuModal` interaction unchanged) |
 * | Click another title while open | Switches directly to it (the outside press first closes the old menu; if the landing point is a title, opens the new one) |
 * | Click the same title again while open | Closes |
 * | Hover a title | Highlights (the currently open one stays highlighted) |
 * | Hold the blank area (drag enabled) | Handed to the system to drag the whole window |
 * | Items in the drop-down | `MenuModal` semantics: mouse / keyboard / Esc / shortcut hints all preserved |
 *
 * @par Not done / not possible (honest notes)
 *  · Alt / F10 activation and arrow-key movement between titles are not done -- the keyboard currently only covers the drop-down interior
 *    (built into `MenuModal`). To add it, the landing point is `OnKeyPress`; it is incremental.
 *  · Child items taller than the bar height are clipped: the bar is a container (`clipChildrenRect`), so for search boxes,
 *    buttons give a height of 26~30 (typical value), do not put in a 46-high large button.
 *  · Window drag only recognizes the "blank area": a press landing on a menu title always opens the menu; a press
 *    consumed by a child widget (button / input box) always belongs to the child widget -- this is deliberate design, not an omission.
 *  · Middle widgets do not wrap when they do not fit (the menu bar is single-line); those that do not fit overlap the right area --
 *    keep the total widget width in check yourself (VS is also single-line; overflow relies on a "more ..." collapse, not done in this batch).
 *
 * @see ZeMenuModal.h  the reused drop-down menu (the original of the data and interaction protocol)
 * @see Zekit.h        `Window::StartWindowDrag` (the low-level drag) / `SetBorderless`
 */

#pragma once
#include "ZeLayoutBase.h"     // LayoutBase: horizontal container + DoLayout hook
#include "ZeMenuModal.h"     // MenuModal / MenuItem: the drop-down menu body and data

#include <string>
#include <vector>

namespace Kit {

    /**
     * @brief Menu bar theme section: `MenuBarStyle` (defined in `ZeTheme.h`, same convention as `ToastStyle`)
     *
     * The bar body = a horizontal strip of "surface background + 1px bottom divider line" (same as VS: the bar and the content area are separated by
     * a line, not by color difference). Menu titles are internal regions on the bar (not child widgets),
     * so the title's hover background / padding / corner radius are a separate set.
     */

    /**
     * @brief VS-style custom menu bar: drop-down menu area + free widget area + optional window drag
     */
    class MenuBar : public LayoutBase {
    public:
        /// @brief Constructs the menu bar (horizontal single line; padding and spacing preset to the VS look)
        /// @note On construction a built-in light-theme menubar section is applied as default; after attaching to a window
        ///       `ApplyTheme` is injected again. Construction does not create the drop-down -- that is this widget's
        ///       internal child widget, entering and leaving the widget tree together with this widget.
        MenuBar();

        // -------------------------------------------------------------------
        //  Drop-down menu
        // -------------------------------------------------------------------
        // @brief Adds a menu title (returns the menu index; subsequent interfaces use it)
        // @note The title text is displayed as-is -- write "File(F)" with the mnemonic parentheses directly.
        int AddMenu(std::string title);

        /// @brief Changes the title of a menu (changing while open: the title refreshes immediately, the drop-down is unaffected)
        void SetMenuTitle(int index, std::string title);

        /// @brief Replaces the drop-down content of a menu entirely (same semantics as `MenuModal::SetItems`)
        /// @note An out-of-range index is a silent no-op. Replacing the content of itself while open: the drop-down stays open,
        ///       and the geometry is recomputed from the new content (consistent with the MenuModal convention).
        void SetMenuItems(int index, std::vector<MenuItem> items);

        /// @brief Enables / disables a menu (disabled: grayed out, cannot open; an already open one is unaffected)
        void SetMenuEnabled(int index, bool enabled);
        /// @brief Whether a menu is currently enabled (returns false if out of range)
        bool menuEnabled(int index) const;

        /// @brief Removes the index-th menu (removing while open also closes the drop-down)
        void RemoveMenu(int index);
        /// @brief Clears all menus (also closes the drop-down; does not touch the child widgets of the free widget area)
        void ClearMenus();

        /// @brief Number of menus
        int menuCount() const { return static_cast<int>(menus_.size()); }
        /// @brief Title of the index-th menu (returns an empty string if out of range)
        const std::string& menuTitle(int index) const;

        /// @brief Which menu's drop-down is currently open (-1 = none)
        int openedMenu() const { return openIndex_; }

        /// @brief Closes the currently open drop-down (idempotent no-op when none is open)
        void CloseMenu();

        // -------------------------------------------------------------------
        //  Free widget area
        // -------------------------------------------------------------------
        // @brief Adds a child widget and puts it in the leading area (always far left, before the menu titles;
        //        = AddChild + leading mark)
        // @details
        // The layout order is: leading area -> drop-down menu title area -> middle free children -> right area.
        // To have the app icon / logo before the menu titles (VS / modern IDE look),
        // use this; to have the menu titles flush to the far left (traditional desktop app look), use `AddChild`.
        // @return Reference to the one in the tree
        template<class T>
        T& AddLeading(T&& w) {
            T& r = AddChild(std::move(w));
            leadingZone_.push_back(&r);
            return r;
        }

        /// @brief Puts a child widget into / out of the leading area (call `AddChild` first, then this interface)
        /// @note Choose one of this and `AddLeading`; this interface suits "changing the segment of a widget already in the tree".
        ///       WARNING: only recognizes widgets in the tree (the reference returned by AddChild).
        void SetLeading(Widget& w, bool on = true);
        /// @brief Whether a child widget belongs to the leading area
        bool isLeading(const Widget& w) const;

        /// @brief Adds a child widget and puts it in the right area (always packed to the right; = AddChild + right mark)
        /// @return Reference to the one in the tree (the stack copy has been moved out; use this return value to modify it later)
        template<class T>
        T& AddRight(T&& w) {
            T& r = AddChild(std::move(w));
            rightZone_.push_back(&r);
            return r;
        }

        /// @brief Puts a child widget into the right area (call `AddChild` first, then this interface)
        /// @note Choose one of this and `AddRight`; this interface suits "changing the segment of a widget already in the tree".
        ///       WARNING: only recognizes widgets in the tree (the reference returned by AddChild) -- the stack copy is
        ///       the emptied shell; passing it in is silently ignored.
        void SetRightAligned(Widget& w, bool on = true);
        /// @brief Whether a child widget belongs to the right area
        bool isRightAligned(const Widget& w) const;

        // -------------------------------------------------------------------
        //  Window drag (the correct approach for borderless mode, see the file header)
        // -------------------------------------------------------------------
        // @brief Turns "hold the blank area to drag the window" on / off (default off)
        // @note When enabled: holding the menu bar's blank area (a press that lands on no title and is not consumed
        //       by a child widget) = drag the whole window. A press landing on a title always opens the menu; a press consumed by a child widget
        //       always belongs to the child widget -- unaffected by this switch.
        // @note It is harmless to enable it on a bordered window (equivalent to an extra draggable "title bar").
        void SetWindowDragEnabled(bool on) { dragEnabled_ = on;  RequestWindowRedraw();}
        /// @brief Whether window drag is currently enabled
        bool isWindowDragEnabled() const { return dragEnabled_; }

        // -------------------------------------------------------------------
        //  Appearance
        // -------------------------------------------------------------------
        // @brief Overrides the bar height (pixels; when not overridden uses the theme `menubar.barHeight`, default 38)
        void SetBarHeight(float h) { barHeightOverride_ = (h > 12.0f) ? h : 12.0f;  RequestWindowRedraw();}
        /// @brief The currently effective bar height
        float barHeight() const;

        // -------------------------------------------------------------------
        //  Measurement protocol
        // -------------------------------------------------------------------
        // @brief The bar's self-reported height = effective bar height (including top and bottom padding; vertical padding preset to 0)
        // @note Width is not self-reported -- the menu bar should span the whole row, so leave it to the parent container to stretch
        //       (VBox horizontal stretch / `SetFlexGrow(1)`).
        float hintHeight() const override;

        // -------------------------------------------------------------------
        //  Theme injection
        // -------------------------------------------------------------------
        // @brief Re-injects from the theme (bar body + title text + hover background + bar height)
        // @note The internal drop-down is a child widget; the theme is recursively injected into it by the tree (this class does not forward it).
        void ApplyTheme(const Theme& t) override;

    protected:
        /// @brief Layout: leading area -> menu titles -> middle free widgets -> right area (packed to the right)
        /// @note The title rects are written to the cache here; paint and hit test only read the cache -- the same geometry.
        void DoLayout() override;

        /// @brief Paint: bar background + bottom divider line + each menu title (hover / open highlight)
        void Paint(FrameCollector& fc) const override;

        // ---- Interaction ----
        bool OnMousePress(MouseEvent& e) override;
        bool OnMouseMove(MouseEvent& e) override;
        void OnMouseLeave() override;

    private:
        /// @brief One menu (title + drop-down content + layout cache)
        struct MenuEntry {
            std::string           title;
            std::vector<MenuItem> items;
            bool  enabled = true;
            //  Title rect (window absolute coordinates; written by DoLayout, read-only for paint and hit test)
            float x = 0.0f;
            float w = 0.0f;
        };

        /**
         * @brief Internal drop-down: `MenuModal` + one report of "report to the host bar when closed outside the panel"
         *
         * @details
         * Why derive instead of attaching a callback: a callback only gets `self` and would have to walk the tree to find the bar; and "clicking
         * another title while open switches directly" can only be done in the outside-press path (`OnClosed`
         * cannot distinguish "clicked outside" from "activated an item", and the latter's mouse landing point may also happen to be on the bar).
         * The derived class first runs the base class in `OnMousePress` (outside press -> close + consume), then
         * reports the landing point to the bar -- the bar checks whether the landing point is another title and opens that one if so.
         */
        class BarMenu : public MenuModal {
        public:
            BarMenu() = default;
        protected:
            bool OnMousePress(MouseEvent& e) override;
        };

        // -------------------------------------------------------------------
        //  Internal
        // -------------------------------------------------------------------
        // @brief Title text width (measures real glyphs if a window exists, otherwise estimates from font size as fallback)
        float titleTextWidth(const MenuEntry& m) const;
        /// @brief Which menu title the landing point is on (-1 = not on any title; reads the layout cache)
        int   titleAtPoint(float px, float py) const;
        /// @brief Opens the drop-down of the index-th menu (directly below the title; closes first if already open)
        void  OpenDropdown(int index);
        /// @brief The drop-down was closed by an "outside press": checks the landing point and switches to another title (if any)
        /// @note Reported by `BarMenu::OnMousePress`; `wasOpen` distinguishes
        ///       "clicking the open title = close" from "clicking another title = switch".
        void  menuClosedByOutsidePress(float mx, float my);
        /// @brief The currently effective bar height (override first -> theme -> default 38)
        float resolvedBarHeight() const;
        /// @brief Requests a repaint (silently skips when no window is available)
        void  requestRepaint();

        // ---- Data (moves with the value; pointer members point to heap objects in the tree, moving does not invalidate them) ----
        std::vector<MenuEntry> menus_;
        MenuModal* menu_ = nullptr;          /// < Internal drop-down (tree object created by AddChild)
        std::vector<Widget*> leadingZone_;   /// < Leading area members (far left, before the menu titles)
        std::vector<Widget*> rightZone_;     /// < Right area members (stable pointers after AddChild)

        // ---- State ----
        int  openIndex_ = -1;                /// < Open menu (-1 = none)
        int  hoverIndex_ = -1;               /// < Hovered title (-1 = none)
        bool dragEnabled_ = false;           /// < Window drag (default off)

        // ---- Appearance (theme injection + override) ----
        BoxStyle  barBox_;                   /// < Bar body (theme injection)
        TextStyle titleStyle_;               /// < Title text (theme injection)
        Color4f   titleHover_{ 0.0f, 0.0f, 0.0f, 0.0f };
        float     titlePadX_ = 10.0f;
        float     titleRadius_ = 5.0f;
        float     themeBarHeight_ = 38.0f;   /// < Bar height from the theme
        float     barHeightOverride_ = 0.0f; /// < User override (0 = no override)
    };

} // namespace Kit
