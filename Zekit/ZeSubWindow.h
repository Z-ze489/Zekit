/**
 * @file ZeSubWindow.h
 * @brief **Library internal**: the "window-shaped rectangle" painted by the renderer -- a mobile-only stand-in for a child window
 *
 * @details
 * WARNING: **This is not public API, do not use it in applications.** The fact that it is not under `Zekit/include/` means exactly that --
 * only the library's own `Zekit.cpp` (`Impl::createEmulatedChild`) uses it.
 *
 * @par What it actually is
 * **Mobile** (Android / Harmony) has no concept of "multiple windows inside an app", so there is no native child window.
 * Therefore `Kit::Window child(parent, ...)` degrades on those platforms to "mounting a painted rectangle
 * in the host window's widget tree" -- **this class is that rectangle**.
 *
 * @par It must not feed back into desktop design (a pitfall already hit)
 * A desktop (Windows / Linux) child window **is just an ordinary window**: title bar, system menu (icon),
 * minimize / maximize, close button, drag the title bar to move, drag the border to resize -- all **provided by the OS**,
 * not a single line to write. The former approach was "both ends must behave identically", so the desktop end was also made self-painted,
 * and native window decorations were stripped to accommodate the stand-in -- that is **putting the cart before the horse**:
 * mobile is "degraded because it cannot do it", not the design standard.
 *
 * | | Desktop (the right way) | Mobile (this class) |
 * |---|---|---|
 * | Implementation | a real native window of `Kit::Window` | this class: a rectangle painted on the host canvas |
 * | Decoration | **all from the system** | self-painted title bar / close button / dragging |
 * | Who does modality | disable the host window (system level) | geometric claim (`HitTest` claims the whole window) |
 *
 * @par What it reuses (zero new library capability)
 *  - **Container**: inherits `Kit::Panel` -- arrangement / padding / child clipping all for free;
 *  - **Title bar position**: written as the container's **top padding** (the same trick as in "36 Tabs" and "37 Table");
 *  - **Three halves of the overlay**: `paintsOnTop()` defers to the second paint pass, `ResetClip()` escapes the ancestor clip region;
 *  - **No size set means no implicit fill**: `Window`'s "implicit fill" of a top-level `LayoutBase` respects the user size,
 *    and one `SetSize` in the constructor locks its own rectangle.
 *
 * @note The stand-in **has no independent render context** -- it is painted together with the host's canvas. This is the essence of the
 *       mobile platform, not a TODO item.
 *
 * @see Zekit.h   `ChildKind` and `Window(Window&, int, int, int, int, ChildKind)`
 * @see ZeMenuModal.h  another implementation of the same overlay mechanism (row menu, that one is public API)
 */

#pragma once
#include "ZePanel.h"
#include "ZeSelfAction.h"
#include "ZeTextStyle.h"

#include <string>
#include <utility>

namespace Kit {

    /// @brief Creation parameters of the child window
    struct SubWindowDesc {
        std::string title;              /// < Title bar text
        float x = 0.0f;                 /// < Top-left corner (host client-area coordinates)
        float y = 0.0f;
        float width = 420.0f;           /// < Window width
        float height = 280.0f;          /// < Window height
        bool  closable = true;          /// < Whether to paint the close button
        bool  movable = true;           /// < Whether the title bar can be dragged
    };

    /**
     * @brief In-app child window (floating panel)
     */
    class SubWindow : public Panel {
    public:
        explicit SubWindow(SubWindowDesc desc = SubWindowDesc());

        // -------------------------------------------------------------------
        //  Title / geometry
        // -------------------------------------------------------------------

        /// @brief Sets the title bar text
        void SetTitle(std::string title);
        /// @brief Current title
        const std::string& title() const { return title_; }

        /// @brief Places it at the given coordinates in the host client area
        void SetWindowPosition(float x, float y);
        /// @brief Translates relative to the current position (dragging calls this)
        void MoveBy(float dx, float dy);
        /// @brief Changes the window size (also locks "not implicitly filled by the top level")
        void SetWindowSize(float w, float h);

        /// @brief Current top-left corner
        float windowX() const { return x(); }
        float windowY() const { return y(); }

        // -------------------------------------------------------------------
        //  Open / close
        // -------------------------------------------------------------------

        /// @brief Shows it (and takes keyboard focus)
        virtual void Open();
        /// @brief Hides it + emits `OnClosed` (idempotent no-op when already hidden)
        virtual void Close();
        /// @brief Whether it is currently shown
        bool isOpen() const { return isVisible(); }

        /// @brief Whether to paint the close button (when off, the right side of the title bar is left empty)
        void SetClosable(bool on);
        /// @brief Whether the title bar can be dragged
        void SetMovable(bool on) { movable_ = on;  RequestWindowRedraw();}

        // -------------------------------------------------------------------
        //  Callbacks (lifecycle notifications -- also emitted on programmatic paths)
        // -------------------------------------------------------------------
        // @brief Close notification (emitted on close button click / programmatic `Close()`; inside the callback it is already "hidden")
        void SetOnClosed(SelfAction cb) { onClosed_ = std::move(cb); }

        // -------------------------------------------------------------------
        //  Appearance
        // -------------------------------------------------------------------

        /// @brief Title bar height (pixels); also resets the container's top padding
        void SetTitleBarHeight(float h);
        float titleBarHeight() const { return titleBarHeight_; }

        void SetTitleBarBackground(const Color4f& c);
        void SetTitleColor(const Color4f& c);
        /// @brief Close button color (normal state; hover uses the theme's hover color)
        void SetCloseButtonColor(const Color4f& c);
        /// @brief Padding on all four sides of the content area (indented further below the title bar)
        void SetContentPadding(float all);

        // -------------------------------------------------------------------
        //  Overlay / hit test
        // -------------------------------------------------------------------
        // @brief Returns true when shown -- makes Window defer it (with its subtree) to the second paint pass
        bool paintsOnTop() const override;
        /// @brief Claims only the **window rectangle** (clicks outside the window pass through to the widgets below)
        bool HitTest(float x, float y) const override;

        // -------------------------------------------------------------------
        //  Interaction
        // -------------------------------------------------------------------
        bool OnMousePress(MouseEvent& e) override;
        bool OnMouseMove(MouseEvent& e) override;
        bool OnMouseRelease(MouseEvent& e) override;
        void OnMouseLeave() override;

        // -------------------------------------------------------------------
        //  Paint / theme
        // -------------------------------------------------------------------
        // @brief Paints shadow + outer frame + background + title bar + title + close button (takes over entirely)
        void Paint(FrameCollector& fc) const override;

        /// @brief Re-injects from the theme's `subwindow` section
        void ApplyTheme(const Theme& t) override;

        /// @brief Requests a repaint (silently skipped when there is no window)
        void requestRepaint();

    protected:
        /// @brief A rectangle (the ones shared by paint and hit test)
        struct Rect { float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f; };

        /// @brief Title bar rectangle (= the widget's top edge strip, height `titleBarHeight_`)
        Rect titleBarRect() const;
        /// @brief Close button square (attached to the right side of the title bar; returns a degenerate rectangle when not closable)
        Rect closeButtonRect() const;

        /// @brief Side length of the close button square
        float closeButtonSize() const { return 22.0f; }

    private:
        std::string title_;
        float   titleBarHeight_ = 30.0f;
        bool    closable_ = true;
        bool    movable_ = true;

        // ---- Dragging ----
        bool  dragging_ = false;
        float dragOffsetX_ = 0.0f;   /// < Offset of "pointer - window top-left" at press time
        float dragOffsetY_ = 0.0f;

        // ---- Hover state (close button) ----
        bool hoverClose_ = false;

        // ---- Appearance (theme-injected, user-overridable) ----
        Color4f titleBarBg_{ 0, 0, 0, 0 };
        Color4f titleColor_{ 0, 0, 0, 1 };
        Color4f closeColor_{ 0, 0, 0, 1 };
        Color4f closeHoverColor_{ 0, 0, 0, 1 };
        Color4f background_{ 0, 0, 0, 0 };
        Color4f borderColor_{ 0, 0, 0, 0 };
        Color4f shadowColor_{ 0, 0, 0, 0 };
        float   shadowOffsetY_ = 6.0f;
        float   shadowSpread_ = 2.0f;
        float   radius_ = 8.0f;
        TextStyle titleTextStyle_;

        SelfAction onClosed_;
    };

} // namespace Kit
