/**
 * @file ZeDrawer.h
 * @brief Drawer: a floating panel that slides out (here "appears") from one edge, optionally with a scrim
 *
 * @details
 * `Drawer : Panel` -- it is a floating layer (`paintsOnTop()`); the difference from `Sidebar`
 * is not in appearance but in layer and interaction model:
 *
 *  | | `Sidebar` | `Drawer` |
 *  |---|---|---|
 *  | What it is | an ordinary widget in the layout | a floating layer over the whole window |
 *  | Who lays it out | the parent layout (`HBoxLayout` left column etc.) | itself (computes an absolute rect from edge + size) |
 *  | When open | always present | painted and clickable only after `Open()` |
 *  | Scrim | none | optional (`SetScrimVisible`), blocks and intercepts outside clicks |
 *  | How it closes | not applicable | `Close()` / close button / `Esc` / click the scrim |
 *
 * @par The two halves of the floating-layer contract (both required, see the notes in `ZeComboBox.cpp`)
 *  1. Paint: `paintsOnTop()` returns true -> `Window` defers it to the second paint pass,
 *     so it covers all ordinary widgets;
 *  2. Hit: `HitTest` counts both the scrim and the panel as hittable while open --
 *     the scrim being hittable is exactly why "click outside = close the drawer" works (otherwise that click
 *     would land on the widget underneath, and the user would "click something else through the gap").
 *
 * Plus clip escape: call `fc.ResetClip()` before painting, otherwise the clip region pushed by the parent
 * container would cut off the floating layer.
 *
 * @par Usage
 * @code
 * Kit::Drawer drawer;
 * drawer.SetSide(Kit::DrawerSide::Right);
 * drawer.SetPanelSize(320.0f);
 * drawer.SetTitle("Settings");
 * drawer.SetOnClosed([](Kit::Widget& self) { std::printf("drawer closed\n"); });
 *
 * // Add content the same way as Panel
 * Kit::Panel* body = drawer.bodyPanel();
 * body->AddChild(Kit::Label("drawer content here"));
 *
 * win.AddWidget(drawer);
 * drawer.Open();                       // open (with scrim by default)
 * @endcode
 *
 * @note Are `Open()` / `Close()` programmatic paths that fire `SetOnOpened` / `SetOnClosed`?
 *       Yes -- this is a deliberate exception: a drawer's "opened / closed" is a user-perceivable
 *       UI state change, and callers usually need to sync button highlight or fetch data accordingly.
 *       But it does not emit `SetOnRequestClose` (that is the "user wants to close" query hook).
 *
 * @see ZeSidebar.h persistent side navigation in the layout (the non-floating counterpart of this class)
 * @see ZePanel.h   base class (content area arrangement)
 */

#pragma once
#include "ZePanel.h"
#include "ZeSelfAction.h"
#include "ZeTheme.h"

#include <cstddef>
#include <memory>
#include <string>

namespace Kit {

    class OverlayWindow;   /// < native panel carrier (shared per layer, see ZeOverlayWindow.h)

    /// @brief Which edge the drawer appears from
    enum class DrawerSide {
        Left,    /// < left
        Right,   /// < right
        Top,     /// < top
        Bottom   /// < bottom
    };

    /**
     * @brief Drawer (floating panel)
     *
     * @details
     * Theme section `drawer` (`SurfaceStyle`): background / border / title text / inner divider.
     * Scrim color comes from theme `subwindow.scrimColor` (same scrim layer as modal dialogs, defined once library-wide).
     */
    class Drawer : public Panel {
    public:
        Drawer();

        /// @brief Destructor (defined in .cpp -- `unique_ptr<OverlayWindow>` needs the complete type)
        ~Drawer() override;

        /// @brief Value semantics: moved along with AddWidget (the explicit destructor suppresses the implicit move, must restore it)
        Drawer(Drawer&&) noexcept;

        // -------------------------------------------------------------------
        //  Open / close
        // -------------------------------------------------------------------

        /// @brief Opens (programmatic path; fires `SetOnOpened`)
        /// @note After opening, pair with `RequestRedraw` -- this function already does it internally.
        void Open();

        /// @brief Closes (programmatic path; fires `SetOnClosed`)
        void Close();

        /// @brief Toggles
        void Toggle() { if (isOpen()) Close(); else Open(); }

        /// @brief Whether currently open
        bool isOpen() const { return open_; }

        // -------------------------------------------------------------------
        //  Position and size
        // -------------------------------------------------------------------

        /// @brief Which edge it appears from (default `Right`)
        void SetSide(DrawerSide side);
        /// @brief Current edge
        DrawerSide side() const { return side_; }

        /// @brief Panel thickness (left/right: width; top/bottom: height) -- the other axis is always the whole window
        /// @param size pixels; <= 0 falls back to theme `ctrlHL * 8`
        void SetPanelSize(float size);
        /// @brief Current panel thickness
        float panelSize() const { return panelSize_; }

        // -------------------------------------------------------------------
        //  Appearance / parts
        // -------------------------------------------------------------------

        /// @brief Title (empty string = no title bar painted)
        void SetTitle(std::string title);
        /// @brief Current title
        const std::string& title() const { return title_; }

        /// @brief Title bar height (pixels; 0 = follow theme `ctrlHL`)
        void SetTitleHeight(float h);
        /// @brief Current title bar height
        float titleHeight() const { return titleHeight_; }

        /// @brief Whether to show the top-right close button (default true)
        void SetClosable(bool closable);
        /// @brief Whether the close button is shown
        bool isClosable() const { return closable_; }

        /// @brief Whether to show the scrim (default true)
        void SetScrimVisible(bool visible);
        /// @brief Whether the scrim is shown
        bool isScrimVisible() const { return scrimVisible_; }

        /// @brief Overrides the scrim color (default from theme `subwindow.scrimColor`)
        void SetScrimColor(const Color4f& c);
        /// @brief Current scrim color
        const Color4f& scrimColor() const { return scrimColor_; }

        /// @brief Whether **Esc** closes it (default true)
        void SetCloseOnEscape(bool on) { closeOnEscape_ = on;  RequestWindowRedraw();}
        /// @brief Whether clicking the scrim closes it (default true)
        void SetCloseOnScrimClick(bool on) { closeOnScrimClick_ = on;  RequestWindowRedraw();}

        // -------------------------------------------------------------------
        //  Content area
        // -------------------------------------------------------------------

        /// @brief Content area container (add widgets into it; the height taken by the title / close button is already reserved by the layout)
        /// @return content area `Panel*` (stable; its address does not change when this widget moves)
        Panel* bodyPanel() { return body_; }
        /// @brief Content area container (read-only)
        const Panel* bodyPanel() const { return body_; }

        // -------------------------------------------------------------------
        //  Callbacks
        // -------------------------------------------------------------------

        /// @brief After opening (with sender)
        void SetOnOpened(SelfAction cb) { onOpened_ = std::move(cb); }
        /// @brief After closing (with sender)
        void SetOnClosed(SelfAction cb) { onClosed_ = std::move(cb); }

        /// @brief User requests close (close button / `Esc` / click the scrim) -- query hook
        /// @note When this callback is not set, all three paths call `Close()` directly (default behavior: a visible X must be able to close).
        ///       Once set, the semantics become "ask first": the library no longer closes for you; call `Close()` in the callback to close.
        ///       This gives interception such as "confirm first when there are unsaved changes" a place to live, without making the library guess business rules.
        ///       All three paths (`X` / `Esc` / click the scrim) share this single exit -- behavior is always consistent.
        void SetOnRequestClose(SelfAction cb) { onRequestClose_ = std::move(cb); }

        // -------------------------------------------------------------------
        //  Floating-layer protocol
        // -------------------------------------------------------------------

        /// @brief Injects fallback values from theme section `drawer` (title bar height / panel thickness / scrim color)
        /// @note Skeleton colors are not cached -- they are read from `fc.GetTheme()` at paint time
        ///       (same approach as `ToolBar::Strip` / `StatusBar::Divider`).
        void ApplyTheme(const Theme& t) override;

        /// @brief Returns true when open -> `Window` puts it in the second paint pass (covers all ordinary widgets)
        bool paintsOnTop() const override { return open_; }

        /// @brief When open, counts both the scrim and the panel as hittable (otherwise clicking the scrim would hit the widget underneath)
        bool HitTest(float x, float y) const override;

        /// @brief Does not clip children -- the floating layer must be able to paint outside the clip region given by the parent container
        bool clipChildrenRect(float& x, float& y, float& w, float& h) const override;

        // -------------------------------------------------------------------
        //  Paint / events
        // -------------------------------------------------------------------
        // @brief Manual arrangement: places the content area container into the panel rect (this is the only step that positions it)
        // @note The drawer itself calls `SetFillWindow(true)` to fill the whole client area, and `body_` is its
        //       child -- without this step the child would be laid out by the parent layout as "full client width, starting at the top",
        //       which visually means "the content is not inside the drawer". Geometry reuses the single `panelRect()`.
        void DoLayout() override;

        /// @brief Scrim -> panel skeleton -> title bar + close button (content is painted recursively by the window)
        void Paint(FrameCollector& fc) const override;

        /// @brief Records a press when on the close button; consumes when inside the panel (no pass-through)
        bool OnMousePress(MouseEvent& e) override;
        /// @brief Release: requests close only when on the same close button (dragging away = cancel)
        bool OnMouseRelease(MouseEvent& e) override;
        /// @brief Maintains close button hover
        bool OnMouseMove(MouseEvent& e) override;
        /// @brief Clears hover on leave
        void OnMouseLeave() override;
        /// @brief `Esc` = request close
        bool OnKeyPress(KeyEvent& e) override;

        /// @brief While open, prevents focus from moving outside the drawer (floating-layer semantics)
        bool isFocusTarget() const override { return open_; }

    private:
        /// @brief Keeps the content area clear of the title bar (writes the title bar height as the content area container's top padding)
        void syncBodyPadding();

        /// @brief Panel rect (client-area absolute coordinates) -- shared by paint and hit
        void panelRect(float& px, float& py, float& pw, float& ph) const;

        // -------------------------------------------------------------------
        //  Native panel carrier (OverlayWindow path; always empty on fallback platforms)
        // -------------------------------------------------------------------
        //
        //  The drawer is a container floating layer: the panel holds the real widget tree the developer added (`body_`).
        //  So the carrier does not just move a paint block; it moves `body_` entirely into the carrier:
        //   - on open, `body_` is detached from this widget and taken over by the carrier (the panel rect equals the carrier rect,
        //     so the content area is still filled by its own layout);
        //   - on close, it is moved back (`body_` still belongs to this widget, so the developer's pointer stays valid).
        //  Scrim is the exception: it is a solid color covering the whole window and is still painted in the host's second pass (the carrier only handles the panel part).

        /// @brief Whether the panel is currently hosted by the native carrier
        bool nativePopup() const;
        /// @brief Syncs the carrier to the panel rect (unloads and hides when closed; only repaints when geometry is unchanged)
        void syncOverlay() const;
        /// @brief Moves `body_` into / out of the carrier (idempotent)
        void mountBody() const;
        void unmountBody() const;
        /// @brief Panel skeleton (shadow / background / border / title / close button)
        /// @param local true = origin at the panel's top-left corner (used inside the carrier)
        void paintChrome(FrameCollector& fc, bool local) const;

        /// @brief Mouse inside the carrier (coordinates are already in host space -- same geometry as when painted in the host)
        bool onPopupPress(MouseEvent& e);
        bool onPopupRelease(MouseEvent& e);
        bool onPopupMove(MouseEvent& e);
        void onPopupLeave();
        /// @brief Close button rect (shared by paint and hit); returns false when not closable
        bool closeButtonRect(float& bx, float& by, float& bw, float& bh) const;

        /// @brief User-path close exit (close button / Esc / scrim): ask first, then close
        void requestCloseByUser();

        /// @brief Requests a repaint (silently skipped when there is no window)
        void requestRepaint();

        // ---- State ----
        bool        open_ = false;
        DrawerSide  side_ = DrawerSide::Right;
        float       panelSize_ = 0.0f;        /// < 0 = follow theme fallback

        std::string title_;
        float       titleHeight_ = 0.0f;      /// < 0 = follow theme
        bool        closable_ = true;
        bool        scrimVisible_ = true;
        Color4f     scrimColor_{ 0.0f, 0.0f, 0.0f, 0.0f };   /// < alpha 0 = follow theme
        bool        closeOnEscape_ = true;
        bool        closeOnScrimClick_ = true;

        bool        hoverClose_ = false;
        bool        pressedClose_ = false;

        // ---- Panel shadow (injected by ApplyTheme; the floating layer relies on it to convey "above the content") ----
        Color4f     shadowColor_{ 0.0f, 0.0f, 0.0f, 0.20f };
        float       shadowOffset_ = 4.0f;   /// < offset toward the content side (the edge side should have no shadow)
        float       shadowSpread_ = 2.0f;

        Panel*      body_ = nullptr;          /// < content area container (borrowed; heap object address is stable)

        /// @brief Native panel carrier (non-null only on desktop platforms with Popup capability; shared per layer)
        /// @note `mutable`: sync happens in `Paint` (const)
        mutable std::unique_ptr<OverlayWindow> overlay_;
        /// @brief Whether `body_` is currently taken over by the carrier (yes -> this widget no longer writes geometry for it)
        mutable bool bodyMounted_ = false;
        /// @brief Last panel geometry pushed to the carrier (debounce)
        mutable float overlayX_ = 0.0f, overlayY_ = 0.0f;
        mutable float overlayW_ = 0.0f, overlayH_ = 0.0f;

        // ---- Callbacks ----
        SelfAction onOpened_;
        SelfAction onClosed_;
        SelfAction onRequestClose_;
    };

} // namespace Kit
