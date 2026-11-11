/**
 * @file ZeMenuModal.h
 * @brief Menu modal window: a command menu popped above all widgets that blocks input to the whole window while open
 *
 * @details
 * `MenuModal : BoxWidget` -- it has **no** "collapsed face": when closed it is nothing
 * (takes no layout space, paints nothing, never hit), and only after `OpenAt(x, y)` does it become a floating panel with a shadow.
 *
 * This class is assembled from two existing mechanisms, with **zero new library capability**:
 *
 *  1. **The three overlay halves** (the same set as the ComboBox popup panel, see "29 ComboBox" section 3):
 *     `paintsOnTop()` defers to the second paint pass + `HitTest` claims the overlay rect +
 *     `fc.ResetClip()` inside `paintContent()` escapes the ancestor clip region --
 *     so the menu always paints above all ordinary widgets and can extend beyond any container;
 *
 *  2. **Modal capture** (this widget turns the "modal mask" mentioned in "29 ComboBox" section 3.3 into a finished product):
 *     while open, `HitTest` returns true for the **entire client area** -- any mouse press reaches
 *     this widget first. If the point is outside the panel -> close immediately and consume the event in place (the widgets below never see it);
 *     if the point is inside the panel -> follow the menu item press/release rhythm. Same for the wheel: consumed while open,
 *     the background must not scroll. That is the whole meaning of "modal" -- not a separate window, but **geometric claiming**.
 *
 * @par Basic usage (three steps: configure data -> attach to top level -> pop up in one line)
 * @code
 * Kit::MenuModal menu;                          // pure overlay: no position to set, no layout to manage
 * menu.AddItem("New", "Ctrl+N");
 * menu.AddItem("Open", "Ctrl+O");
 * menu.AddSeparator();
 * menu.AddItem("Exit");
 * win.AddWidget(menu);                          // value takeover, then use the returned reference or find by name
 *
 * // Pop up in one line from any widget's callback -- the menu opens wherever the mouse is (context menu idiom):
 * anyWidget.SetOnMousePress([&](Kit::Widget& self, Kit::MouseEvent& e) {
 *     if (e.button == Kit::MouseButton::Right && menu.isOpen() == false)
 *         menu.OpenAtCursor();
 *     return false;                             // return false: do not swallow the event
 * });
 * @endcode
 *
 * @note The scrim is **off by default**: modal capture (blocking input) is always active; the dark film is only an optional
 *       visual layer -- call `SetScrimEnabled(true)` when you want the "whole window dimmed one notch" cue.
 *
 * Data and interaction follow the library-wide conventions:
 *  - **Menu items are commands, not state**: `OnItemSelected` fires on **every** user activation
 *    (clicking the same item twice fires twice) -- deliberately different from ComboBox's "fire only when the value changes";
 *  - Each item may carry its own `MenuItem::action` (dual signature: `void()` old style / `void(Widget&)`
 *    sender style, see @ref MenuAction -- **the latter is recommended**: self is injected by the library at dispatch time,
 *    and with the Widget window convenience interface it can close the window / change the title without captures, eliminating dangling captures).
 *    Activation order = close the menu first -> item action -> OnItemSelected
 *    (inside the callback `isOpen()` is already false, and modifying the menu / Clear inside the action is safe);
 *  - Callbacks fire only on the **user path** (mouse activation / keyboard Enter); programmatic data changes send no notification;
 *  - `OnClosed` is a lifecycle notification: fires on outside click / Esc / selecting an item / programmatic Close.
 *
 * @par Size and positioning
 * The panel **natural width** = the widest row (text + shortcut hint) + padding + border, with a minimum width;
 * `SetMenuWidth` pins it. Height = sum of rows (menu item row height / separator slot height) + padding + border.
 * The anchor given to `OpenAt` is **clamped into the window** (4px margin on all sides), so even an uncertain position never pops off-screen.
 *
 * @par Why hint returns 0
 * It is a **pure overlay**: it does not enter layout (collapsing to 0 height inside a VBox/HBox is **exactly the expected behavior**),
 * the correct usage is `win.AddWidget(menu)` to attach it at the top level. If the user explicitly called `SetSize`, the user value is respected.
 *
 * @par Interaction
 * | Operation | Behavior |
 * |---|---|
 * | `OpenAtCursor()` | Opens at the **current mouse position** (the usual entry for context menus / popup menus) |
 * | `OpenAt(x, y)` | Opens at the given window coordinates (the anchor is automatically clamped into the window) |
 * | Press outside the panel while open (any button) | Closes immediately, event consumed in place |
 * | Left press on a menu item -> release | Activates that item (close menu first -> item action -> OnItemSelected) |
 * | Press then drag to another row and release | Settles on the row at the **release point** (same rhythm as ComboBox) |
 * | `Esc` | Closes, no activation |
 * | `Up` / `Down` / `Home` / `End` | Moves the highlight (**skipping** separators and disabled items, wrapping at both ends). No **preset highlight** on open: the first `Down` highlights the first item, the first `Up` highlights the last item |
 * | `Enter` / `Space` | Activates the current highlighted item (no highlight before any arrow key press -> activates nothing) |
 * | Wheel while open | Always consumed (the background must not scroll) |
 * | Focus lost | Closes (e.g. Tab away) |
 * | Mouse leaves the panel | Clears the hovered row highlight |
 *
 * @see ZeComboBox.h the first implementation of the three overlay halves (popup panel mechanism reference)
 * @see "29 ComboBox" section 3 overlay mechanism, "30 Menu Modal Window" the full manual for this widget
 */

#pragma once
#include "ZeBoxWidget.h"
#include "ZeSelfAction.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace Kit {

    class OverlayWindow;

    /**
     * @brief Menu item action -- a **dual-signature** action wrapper: accepts both `void()` and `void(Widget&)`
     *
     * @details
     * This is the sender protocol (see ZeSelfAction.h) applied to menu items: previously
     * `MenuItem::action` was a bare `std::function<void()>`, **without a sender** -- so
     * developers could never get self inside the action and had to capture objects externally (`[&]{ win.Close(); }`),
     * and captures are exactly the number one source of dangling in a value-semantics library (real pitfall 2026-10-15: a local Window
     * created inside a panel constructor function and then captured crashes with 0xC0000005 on click).
     *
     * This type makes both styles **legal, with no change to callers**:
     *  @code
     *  // New style (recommended): self is **injected by the library at dispatch time**, always pointing to the entity in the tree --
     *  // self is the dropdown menu itself; with the Widget window convenience interface it works without captures:
     *  { .text = "Exit", .action = [](Kit::Widget& self) { self.CloseWindow(); } }
     *
     *  // Old style (still usable): for parameterless actions that do not care about the window / host
     *  { .text = "About", .action = [] { ShowAboutDialog(); } }
     *  @endcode
     *
     * @note Dispatch rule: if the sender version is installed it is used first (only one of the two can be installed --
     *       classified automatically by the lambda's parameter signature; a single callable cannot be both).
     * @note sender at dispatch time = **the dropdown menu itself** (the one inside MenuModal / MenuBar),
     *       which lives in the widget tree, so `self.window()` / `self.CloseWindow()` are naturally reachable along the parent chain.
     * @see ZeSelfAction.h   description of the sender protocol (the root fix for value-semantics move pitfalls)
     * @see ZeWidget.h       window convenience interface (window actions directly usable on self)
     */
    class MenuAction {
    public:
        MenuAction() = default;

        /// @brief Constructs from any callable: stores it under the new protocol if it accepts a sender, otherwise as a parameterless action
        /// @note A template rather than two std::function overload constructors -- designated initializer
        ///       (`.action = [...] {...}`) is copy-initialization and allows only **one** user-defined
        ///       conversion: the chain lambda -> std::function -> MenuAction is two, which does not compile;
        ///       constructing directly from the lambda in one step is the only legal form.
        template<class F,
                 std::enable_if_t<
                     !std::is_same_v<std::decay_t<F>, MenuAction> &&
                     (std::is_invocable_v<F&, Widget&> || std::is_invocable_v<F&>),
                 int> = 0>
        MenuAction(F&& f) {
            if constexpr (std::is_invocable_v<F&, Widget&>)
                withSelf_ = std::forward<F>(f);   // [](Widget& self){...}
            else
                plain_   = std::forward<F>(f);    // []{...}
        }

        /// @brief Whether an action is installed (empty actions are skipped on activation)
        explicit operator bool() const {
            return static_cast<bool>(plain_) || static_cast<bool>(withSelf_);
        }

        /// @brief Activates the action (sender = the dropdown menu itself; dispatches by which version is installed)
        void invoke(Widget& sender) const {
            if (withSelf_)      withSelf_(sender);
            else if (plain_)    plain_();
        }

    private:
        std::function<void()> plain_;     /// < parameterless action (old style []{...})
        SelfAction            withSelf_;  /// < sender action (new style [](Widget& self){...})
    };

    /**
     * @brief Menu item (pure data, moved with value semantics)
     *
     * @details
     * An item with `separator == true` is a **separator**: `text` / `shortcut` / `action` are all ignored,
     * it only occupies one separator slot row. A disabled item (`enabled == false`) is drawn grayed out, cannot be clicked by mouse or keyboard,
     * and keyboard navigation skips it entirely.
     */
    struct MenuItem {
        std::string           text;        /// < menu item text (ignored for separators)
        std::string           shortcut;    /// < right-side shortcut hint (may be empty; ignored for separators)
        bool                  separator = false;  /// < true = this item is drawn as a separator
        bool                  enabled = true;     /// < disabled items are grayed out and cannot be activated
        /// @brief Fires on user activation (may be empty; the menu closes before it fires)
        /// @details Two styles (see MenuAction):
        ///  - `[](Kit::Widget& self){...}` -- **recommended**: self is injected by the library at dispatch time,
        ///    always the entity in the tree, no capture and no dangling; with the Widget window convenience interface it works directly:
        ///    @code .action = [](Kit::Widget& self){ self.CloseWindow(); } @endcode
        ///  - `[]{...}` -- old style still usable; WARNING: do not capture external windows / widgets by reference
        ///    (under value semantics the capture made at construction time is a stack box that will become invalid).
        MenuAction            action;
    };

    /**
     * @brief Menu modal window: floating command menu + whole-window input capture while open
     */
    class MenuModal : public BoxWidget {
    public:
    
        /// @brief Constructs the menu modal window
        /// @note At construction the menu section of the built-in light theme is used as default (`themeBox` is virtual,
        ///       so it cannot be called in the base class constructor). The window theme is injected again after attaching to a window.
        MenuModal();

        /// @brief Destructor (defined in .cpp -- `unique_ptr<OverlayWindow>` needs the complete type)
        ~MenuModal() override;

        /// @brief Value semantics: moved with AddWidget (the explicit destructor suppresses the implicit move, restored here)
        /// @note Defined in .cpp; overlay_ moves with the default move.
        MenuModal(MenuModal&&) noexcept;

        // -------------------------------------------------------------------
        //  Menu item data (data, not style -- not reset by theme changes)
        // -------------------------------------------------------------------

        /// @brief Replaces all menu items (**triggers no callbacks**)
        /// @note Replacing while open: stays open, anchor unchanged, geometry recomputed from the new content; closes automatically if the content becomes empty.
        void SetItems(std::vector<MenuItem> items);

        /// @brief Appends a menu item (**triggers no callbacks**)
        /// @param text     menu item text
        /// @param shortcut right-side shortcut hint (may be empty)
        /// @param action  fires on user activation (may be empty); both `void()` and `void(Widget&)`
        ///                signatures are accepted, see @ref MenuAction
        /// @return the index of the item
        int AddItem(std::string text, std::string shortcut = std::string(),
                    MenuAction action = MenuAction());

        /// @brief Appends a separator (**triggers no callbacks**)
        /// @return the index of the item
        int AddSeparator();

        /// @brief Clears all menu items (also closes; **triggers no callbacks**)
        void Clear();

        /// @brief Number of menu items (including separators)
        std::size_t itemCount() const { return items_.size(); }

        /// @brief Returns the menu item at index (out of range returns a static empty item; the reference is stable, no throw, no crash)
        const MenuItem& itemAt(std::size_t index) const;

        /// @brief Enables / disables an item (takes effect immediately while open; **program path, triggers no callbacks**)
        /// @note An out-of-range index is a silent no-op.
        void SetItemEnabled(std::size_t index, bool enabled);

        /// @brief Whether at least one **activatable** item exists (non-separator); the gate for `OpenAt`
        bool hasActivableItem() const;

        // -------------------------------------------------------------------
        //  Open state (modal)
        // -------------------------------------------------------------------

        /// @brief Whether currently open
        bool isOpen() const { return open_; }

        /// @brief Opens the menu at window coordinates (x, y) (the anchor is clamped into the window, 4px margin on all sides)
        /// @note Cannot open when there is no **activatable item** (all separators / empty list). Opening takes keyboard focus
        ///       (`Esc` / arrow keys work immediately) and clears the previous highlight residue;
        ///       **no preset highlight** -- the highlight is produced only by hover / arrow keys (to prevent "a stray Enter
        ///       accidentally activating the first item").
        void OpenAt(float x, float y);

        /// @brief Opens the menu at the **current mouse position** -- the usual entry for context menus / popup menus
        /// @details
        /// Internally reads `Window::GetMousePosition` (the client-area coordinates of the last mouse event),
        /// then goes through `OpenAt` (clamped into the window, takes focus, everything else the same). So in any widget's callback
        /// one line is a context menu:
        /// @code
        /// bool OnMousePress(MouseEvent& e) override {
        ///     if (e.button == MouseButton::Right) menu.OpenAtCursor();  // open at the cursor
        ///     return false;                                             // do not swallow the event
        /// }
        /// @endcode
        /// @note When no mouse message has been received yet (window just opened, cursor never entered) there is no "mouse
        ///       position" to speak of -- **silently does not open** (same convention as the library-wide "unsupported means silent no-op").
        void OpenAtCursor();

        /// @brief Closes (clears highlight / hover / pressed state; idempotent no-op when already closed)
        /// @note Fires `OnClosed` (lifecycle notification, also fired on the program path -- closing is not a "value change").
        void Close();

        // -------------------------------------------------------------------
        //  Callbacks (activation only on the user path; closing is a lifecycle notification)
        // -------------------------------------------------------------------

        /// @brief "Menu item activated" callback type (with sender + index of the activated item)
        using ItemSelectedAction = SelfAction1<int>;

        /// @brief Sets the "menu item activated" callback (**user path**: mouse activation / keyboard Enter)
        /// @param handler callback; `index` is the index of the activated item; pass empty to clear
        /// @note Unlike ComboBox: menu items are **commands, not state**, so it **fires on every activation**
        ///       (clicking the same item twice fires twice). Programmatic data changes do not fire.
        void SetOnItemSelected(ItemSelectedAction handler);

        /// @brief Sets the "menu item activated" callback (sender-only style)
        /// @note Can be registered together with the payload version; at dispatch the **payload version fires first, then the sender version**.
        void SetOnItemSelected(SelfAction handler);

        /// @brief Sets the "menu closed" callback (fires on outside click / Esc / activating an item / programmatic Close)
        /// @note When the callback fires `isOpen()` is already false; the menu closes first, then the notification is sent.
        void SetOnClosed(SelfAction handler);

        // -------------------------------------------------------------------
        //  Appearance (scrim / width / row height / three styles)
        // -------------------------------------------------------------------

        /// @brief Whether to paint the modal scrim (a semi-transparent dark layer covering the whole window; **off by default**)
        /// @note Enabling it only **paints** an extra dark film -- modal capture (blocking input) is unrelated to the scrim
        ///       and is always active while open.
        void SetScrimEnabled(bool on);
        /// @brief Whether the scrim is currently painted
        bool scrimEnabled() const { return scrimEnabled_; }

        /// @brief Pins the panel width (pixels); 0 = automatic (widest row + padding, with a minimum width)
        void SetMenuWidth(float w);
        /// @brief Current width override (0 = automatic)
        float menuWidth() const { return menuWidthOverride_; }

        /// @brief Single row height (pixels); 0 = use theme `menu.itemHeight`
        void SetItemHeight(float h);
        /// @brief Current effective row height (explicit value first, otherwise the theme value)
        float itemHeight() const { return resolvedItemHeight(); }

        /// @brief Panel box (background / border / corner radius / padding / shadow) -- directly modifiable
        BoxStyle& panelBox() { return panelBox_; }
        const BoxStyle& panelBox() const { return panelBox_; }

        /// @brief Menu item text style -- directly modifiable
        TextStyle& itemTextStyle() { return itemTextStyle_; }
        const TextStyle& itemTextStyle() const { return itemTextStyle_; }

        /// @brief Shortcut hint text style (right side, muted color) -- directly modifiable
        TextStyle& shortcutTextStyle() { return shortcutTextStyle_; }
        const TextStyle& shortcutTextStyle() const { return shortcutTextStyle_; }

        /// @brief Full-row background color of the hovered / keyboard-highlighted row
        void SetItemHoverColor(const Color4f& c);
        const Color4f& itemHoverColor() const { return itemHoverColor_; }

        /// @brief Separator color
        void SetSeparatorColor(const Color4f& c);
        const Color4f& separatorColor() const { return separatorColor_; }

        /// @brief Modal scrim color (semi-transparent black by default)
        void SetScrimColor(const Color4f& c);
        const Color4f& scrimColor() const { return scrimColor_; }

        // -------------------------------------------------------------------
        //  Self-reported natural size (measurement protocol)
        // -------------------------------------------------------------------
        //  NOTE: deliberately different from other leaf widgets: MenuModal is a **pure overlay** and does not enter layout --
        //    without an explicit SetSize, hint returns 0, and collapsing to 0 height inside a VBox/HBox is **exactly the expectation**
        //    (it should not occupy any layout space). The only correct usage is win.AddWidget() to attach at the top level.
        // -------------------------------------------------------------------

        /// @brief Returns the explicit pref if set, otherwise 0 (a pure overlay takes no layout space, see above)
        float hintWidth() const override;
        /// @brief Same as hintWidth
        float hintHeight() const override;

        // -------------------------------------------------------------------
        //  Overlay / modal hit test (library-level convention, see the file header)
        // -------------------------------------------------------------------

        /// @brief Returns true when open -- lets Window defer this widget to the "top overlay phase" for painting
        /// @note Returns false when closed; normally it is an ordinary widget that paints nothing.
        bool paintsOnTop() const override;

        /// @brief Closed: never hit; **open: the entire client area counts as its own hittable region** (modal capture)
        /// @note Clicks outside the panel do not pass through to the widgets below -- while open they receive no
        ///       stateful input (press / wheel are all consumed in place by this widget).
        bool HitTest(float x, float y) const override;

        // -------------------------------------------------------------------
        //  Theme injection
        // -------------------------------------------------------------------

        /// @brief Re-injects styles from the theme (panel + menu item + shortcut + single-value colors)
        void ApplyTheme(const Theme& t) override;

        // -------------------------------------------------------------------
        //  Interaction events
        // -------------------------------------------------------------------

        bool OnMousePress(MouseEvent& e) override;
        bool OnMouseRelease(MouseEvent& e) override;
        bool OnMouseMove(MouseEvent& e) override;
        bool OnMouseWheel(MouseEvent& e) override;
        void OnMouseLeave() override;
        bool OnKeyPress(KeyEvent& e) override;

        /// @brief Closes on focus loss (Tab away / clicking another focusable widget all end up here)
        void OnFocusOut() override;

    protected:
        /// @brief Default box style takes the theme's menu section (panel)
        const BoxStyle& themeBox(const Theme& t) const override;

        /// @brief **Fully takes over** the base class paint template (see below)
        /// @details
        /// This widget has no "collapsed face", so its paint rules differ from other box widgets:
        ///  - Closed: **paints nothing** -- must bypass the two gates of `BoxWidget::Paint`
        ///    (0x0 geometry returns immediately; paints the box_'s shadow/border/background three layers first), otherwise
        ///    it either paints nothing or leaves a 1x1 color dot. The overlay's "closed = completely nonexistent"
        ///    must be guaranteed at the Paint level;
        ///  - Open: calls `paintContent()` directly -- the panel's shadow / border / background / rows are all
        ///    painted by itself (on the overlay rect at the anchor, unrelated to the widget's own rect).
        void Paint(FrameCollector& fc) const override;

        /// @brief Paints: scrim + panel + rows (only called while open)
        /// @note Native carrier mode: only the **scrim** is painted here (the panel is painted by the proxy inside OverlayWindow);
        ///       fallback: both scrim and panel are painted here.
        void paintContent(FrameCollector& fc) const override;

        // -------------------------------------------------------------------
        //  Popup panel (shared by the native carrier / self-painted overlay paths, same migration pattern as ComboBox)
        // -------------------------------------------------------------------

        /// @brief Whether the popup panel is currently hosted by the native carrier (a separate Popup window)
        bool nativePopup() const;

        /// @brief Paints the panel into the given collector
        /// @param localPopup true = origin at the panel's top-left corner (native carrier path); false = host window coordinates
        void paintPopupInto(FrameCollector& fc, bool localPopup) const;

        // ---- Panel interaction (coordinates are always host window coordinates; the native path is converted and forwarded by the carrier proxy) ----
        bool onPopupPress(MouseEvent& e);
        bool onPopupRelease(MouseEvent& e);
        bool onPopupMove(MouseEvent& e);
        bool onPopupWheel(MouseEvent& e);
        void onPopupLeave();

        /// @brief Shows the native carrier (moves the panel into it when Popup is available; silently skipped on fallback platforms)
        void showNativePopup();

        /// @brief Hides the native carrier (Hide, not destroy -- zero creation cost for frequent toggling)
        void hideNativePopup();

    private:
        /// @brief Panel geometry (shared by painting and hit testing -- eliminates "looks like item 3, clicks item 2")
        struct MenuGeom {
            float x = 0.0f;        /// < panel left edge (window coordinates, already clamped into the window)
            float y = 0.0f;        /// < panel top edge
            float w = 0.0f;        /// < panel width
            float h = 0.0f;        /// < panel height
            float listTop = 0.0f;  /// < top edge of the first row (= y + top padding + border)
            float rowH = 0.0f;     /// < menu item row height
            bool  valid = false;   /// < false = no displayable content, the caller skips as 0
        };

        /// @brief Computes the panel geometry (pure query, does not change state; the anchor is clamped into the window)
        MenuGeom computeMenu() const;

        /// @brief Row height occupied by item index (menu item -> itemHeight; separator -> separator slot height)
        float rowHeight(std::size_t index) const;

        /// @brief Which menu item a point falls on (-1 = not on an activatable item, including separators / padding)
        int itemAtPoint(float px, float py) const;

        /// @brief Index of the first activatable item (-1 if none)
        int firstEnabled() const;

        /// @brief Finds the next activatable item from from along dir (+/-1) (wraps, skips separators and disabled items)
        int nextEnabled(int from, int dir) const;

        /// @brief Current effective row height (explicit value first, otherwise the theme value, fallback 30)
        float resolvedItemHeight() const;

        /// @brief Current effective panel width (override first, otherwise computed from content, fallback minimum width)
        float resolvedMenuWidth() const;

        /// @brief User activates an item (the **only** activation exit: close menu first -> item action -> OnItemSelected)
        void activateItem(int index);

        /// @brief Fires the registered activation callbacks (payload version first, then sender version)
        void notifyItemSelected(int index);

        /// @brief Requests a repaint (silently skipped when no window is available -- an unmounted widget needs no repaint)
        void requestRepaint();

        // ---- Data (moved with value semantics) ----
        std::vector<MenuItem> items_;   /// < menu items (including separators)

        // ---- Open state / interaction transients ----
        bool  open_ = false;            /// < whether open
        float anchorX_ = 0.0f;          /// < anchor recorded by OpenAt (window coordinates)
        float anchorY_ = 0.0f;
        int   highlight_ = -1;          /// < keyboard-highlighted item
        int   hoverRow_ = -1;           /// < mouse-hovered item (-1 = none)
        int   pressedItem_ = -1;        /// < item the press landed on (activated on release)

        // ---- Appearance (user-overridable) ----
        bool  scrimEnabled_ = false;    /// < whether to paint the modal scrim (off by default -- capture is unrelated to it)
        float menuWidthOverride_ = 0.0f;  /// < panel width override (0 = automatic)
        float itemHeightOverride_ = 0.0f; /// < explicit row height (0 = follow theme)
        float themeItemHeight_ = 30.0f;   /// < row height from the theme (injected by ApplyTheme)

        Color4f itemHoverColor_{ 0.0f, 0.0f, 0.0f, 0.0f };   /// < highlighted row background color
        Color4f separatorColor_{ 0.0f, 0.0f, 0.0f, 0.0f };   /// < separator color
        Color4f scrimColor_{ 0.0f, 0.0f, 0.0f, 0.0f };       /// < scrim color

        BoxStyle  panelBox_;           /// < panel box (theme-injected, user can modify further)
        TextStyle itemTextStyle_;      /// < menu item text style (theme-injected)
        TextStyle shortcutTextStyle_;  /// < shortcut hint text style (theme-injected)

        //  Callbacks (std::function is movable, moved along with the implicit move constructor by AddWidget)
        ItemSelectedAction onSelected_;       /// < activation callback (with index)
        SelfAction         onSelectedSelf_;   /// < activation callback (self only)
        SelfAction         onClosed_;         /// < close notification (lifecycle)
        std::unique_ptr<OverlayWindow> overlay_; /// < native carrier (non-null only on platforms with desktop Popup capability)
    };

} // namespace Kit
