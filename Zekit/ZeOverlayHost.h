/**
 * @file ZeOverlayHost.h
 * @brief Library-internal header: `OverlaySurface` + `OverlayHost` -- one per host window,
 *        the "overlay carrier pool" (shared per layer, hidden and reused, follows the host).
 *
 * @details
 * This layer solves the three practical problems left by sections 28/29; the public surface is still only
 * `include/ZeOverlayWindow.h` (widgets use that one; generally no need to touch this):
 *
 *  1. Always creating windows -- if each overlay widget held its own Popup window, the UI would have as many
 *     HWNDs as there are dropdowns (each with its own swap chain). This layer shares carriers by
 *     "host window x layer": each layer is created only once, after which there is only `Show/Hide` +
 *     resize + repaint. When no overlay is needed the window stays (hidden); when needed it is shown directly.
 *  2. Not following the host -- the carrier is a `WS_POPUP` top-level window; the system will not move it with the host.
 *     Position sync is driven only by the host geometry change notification (`IWindowHost::OnHostGeometryChanged`
 *     -> Win32 `WM_WINDOWPOSCHANGED`), the only channel that still arrives during the system modal drag / resize
 *     loop (library-level timers do not run at all then).
 *  3. Background three states -- the carrier is always per-pixel transparent (clear alpha = 0);
 *     the background is a rectangle painted by the carrier itself: alpha=0 fully transparent (content only),
 *     0<alpha<1 semi-transparent, alpha=1 solid. Switching the background does not require rebuilding the swap chain.
 *
 * @par Layers (at most one occupant per layer at a time)
 * | Layer | Used by | Notes |
 * |---|---|---|
 * | `Panel`   | Selector popup / drawer / image preview / menu modal | Interactive overlays |
 * | `Toast`   | Light notification bar | Sits above interactive overlays |
 * | `Tooltip` | Hover bubble | Always on top, and click-through |
 *
 * Layering exists so that combinations that appear at the same time, such as "a bubble over a popup" or
 * "a toast over a drawer", each get their own window instead of fighting over the same one.
 *
 * @note Only platforms with both the `ChildWindow && PopupWindow` capability bits can create a carrier
 *       (Win32 is done; X11 / mobile honestly do not promise it per the capability-bit discipline, and callers fall back
 *       to the old `paintsOnTop` path).
 */

#pragma once
#include "include/ZeOverlayWindow.h"   // OverlayLayer / OverlayContent (the public surface is defined right here)
#include "ZePanel.h"

#include <functional>
#include <memory>

namespace Kit {

    class OverlaySurface;

    // ===================================================================
    //  Carrier pool
    // ===================================================================

    /**
     * @brief One carrier pool per host window (static facade; registered internally by host)
     *
     * @note All interfaces are called on the UI thread only (same discipline as `Window`).
     */
    class OverlayHost {
    public:
        /// @brief Whether native carriers can be created on this host (capability-bit test, not platform macros)
        static bool Supports(const Window& host);

        /// @brief Gets the carrier for a layer (creates the window on first call; reuses the same one afterwards)
        /// @param host Host window
        /// @param layer Layer (see `OverlayLayer`)
        /// @return The carrier; null if capability is insufficient / the host is closed
        /// @note The returned carrier may already be occupied by someone else -- the caller should
        ///       `SetOwner()` to take it over; taking over notifies the previous occupant that it was replaced.
        static std::shared_ptr<OverlaySurface> Acquire(Window& host, OverlayLayer layer);

        /// @brief Host geometry changed: repositions all visible carriers by anchor
        /// @note Called by `Window::Impl::OnHostGeometryChanged` (message-driven).
        static void NotifyGeometryChanged(Window& host);

        /// @brief Host received a mouse press: lets visible carriers decide whether this press is outside
        /// @note Called by `Window::dispatchMousePress` before dispatch.
        static void NotifyHostPressed(Window& host, float x, float y);

        /// @brief Host is closing (not yet destructed): hides every lit carrier of this host
        /// @note Division of labor with `Forget`: that one is "host is dead -> unregister + hide"; this one is
        ///       "host is closing -> hide only, keep the registration" (the object still exists, `~Window` will collect again,
        ///       both are idempotent).
        /// @note Why this must exist separately (2026-11-09): `Close()` only destroys the native
        ///       window and does not destruct the object, while the carrier is an independent top-level window -- Win32 relies on the owner
        ///       relationship for cascade destruction, X11 only has `WM_TRANSIENT_FOR`, and the host disappearing does not take
        ///       it away. Hence "click X to close the child window, the overlay stays on screen" (see Source Map section 52).
        /// @note Must be called before the native window is destroyed (the platform layer's Close release point) --
        ///       at that time the carrier window is still alive, so `Hide()` is a normal Unmap rather than spinning
        ///       on a dead handle.
        static void Dismiss(Window& host);

        /// @brief Host destructed: removes all carrier registrations of this host
        /// @note Called by `Window::~Window` after the widget table is cleared and before the native window is destroyed --
        ///       widgets are gone by then, so nobody holds a carrier pointer anymore.
        static void Forget(Window& host);
    };

    // ===================================================================
    //  Carrier
    // ===================================================================

    /**
     * @brief A reusable overlay carrier (`ChildKind::Popup` child window)
     *
     * @details
     * Lifecycle: the window is created on the first `Show()` (hidden after creation); every subsequent `Show/Hide`
     * is just "show / hide", and the window and swap chain are reused throughout.
     *
     * Internal structure (the carrier's own widget tree, bottom to top):
     * ```
     * Root Panel (fills the carrier)
     *   |- Background (one rectangle; transparent / semi-transparent / solid decided by SetBackground)
     *   |- SurfacePane (self-painted content: calls OverlayContent::paint)
     *   |- Container Panel (real widget tree: container overlays such as drawer content go here)
     * ```
     */
    class OverlaySurface : public std::enable_shared_from_this<OverlaySurface> {
    public:
        OverlaySurface(Window& host, OverlayLayer layer);
        ~OverlaySurface();

        OverlaySurface(const OverlaySurface&) = delete;
        OverlaySurface& operator=(const OverlaySurface&) = delete;

        // ---- State ----
        // @brief Ensures the carrier window is created (idempotent; returns false if creation fails)
        // @note The common widget pattern is `if (!overlay.isReady()) return;` before Show --
        //       and Show is the creation point. "Query also creates" is folded in here so callers need not
        //       call `content()` or similar to trigger creation (that is a hidden ordering trap).
        bool ensureReady();
        bool isReady() const;                    /// < Carrier window created and not closed
        bool isVisible() const { return visible_; }
        Window* nativeWindow();                  /// < Carrier window (nullptr = not created)
        OverlayLayer layer() const { return layer_; }

        // ---- Occupancy ----
        // @brief Takes over this carrier (the previous occupant receives `onEvict`)
        void SetOwner(void* owner, std::function<void()> onEvict);
        /// @brief Releases this carrier (only the current occupant can release; does not hide -- the caller decides)
        void ReleaseOwner(void* owner);
        /// @brief Whether this occupant still holds this carrier
        /// @note After "taken over by a later occupant of the same layer", the old occupant's `Hide()` must become a no-op --
        ///       otherwise it would close the new occupant's popup as soon as it hides (observed: a dropdown disappears right after opening).
        bool ownedBy(const void* owner) const { return owner_ == owner; }

        // ---- Content ----
        // @brief Sets the self-painted content (paint / mouse); passing an empty struct = clear
        void SetContent(OverlayContent content);
        /// @brief Container-form content root (holds real widgets; `Panel` fills the whole carrier)
        Panel& content();
        /// @brief Repaints the carrier (repaints only this window, does not touch the host)
        void Repaint();

        // ---- Show / Hide ----
        // @param hostX,hostY Anchor (host client-area coordinates)
        void Show(float hostX, float hostY, float width, float height);
        void Hide();

        // ---- Appearance / Behavior ----
        // @brief Background color: alpha=0 fully transparent / 0<alpha<1 semi-transparent / alpha=1 solid
        void SetBackground(const Color4f& bg);
        /// @brief Follows host move / resize (default on)
        void SetFollowHost(bool on) { followHost_ = on; }
        /// @brief Mouse click-through for the whole window (for bubble types)
        void SetClickThrough(bool on);
        /// @brief Whether always on top (default on -- this is what puts it above the CanvasView canvas)
        void SetTopmost(bool on);
        /// @brief Theme (content and carrier re-skin together; the host theme is passed in by the caller)
        void SetTheme(const Theme& t);
        /// @brief Callback on host geometry change (for overlays that must recompute geometry, such as drawers / full-window previews)
        /// @note Called before repositioning: the callback may call `Show()` again with the new geometry.
        void SetOnHostGeometryChanged(std::function<void()> cb) { onHostGeom_ = std::move(cb); }

        // -------------------------------------------------------------------
        //  Carrier internal protocol (called by the carrier's own proxy widgets; widget side does not touch it)
        // -------------------------------------------------------------------

        /// @brief Paints background + self-painted content (origin = overlay top-left)
        void paintInto(FrameCollector& fc) const;
        /// @brief Hit test inside the carrier (the whole area counts as this overlay)
        bool hitTestAt(float x, float y) const;
        /// @brief Host press notification (forwarded to `OverlayContent::onHostPress`)
        void notifyHostPressed(float x, float y);

        /// @brief Mouse dispatch (coordinates converted back to host space, then handed to `OverlayContent`)
        bool dispatchPress(MouseEvent& e);
        bool dispatchRelease(MouseEvent& e);
        bool dispatchMove(MouseEvent& e);
        bool dispatchWheel(MouseEvent& e);
        void dispatchLeave();

    private:
        friend class OverlayHost;

        /// @brief Lazy window creation (idempotent)
        void ensureWindow();
        /// @brief The actual on-screen show (second half of `Show`; geometry taken from pendW_/pendH_)
        void showNow();
        /// @brief Fulfills the deferred on-screen show at frame end (nested-frame guard of `Show`, see .cpp)
        void flushDeferredShow();
        /// @brief Positions the carrier at host client-area coordinates (anchorX_, anchorY_) by anchor
        void reposition();
        /// @brief Host geometry change: first asks content whether to recompute, then repositions
        void followHost();

        Window& host_;                                   /// < Host (must outlive the carrier)
        OverlayLayer layer_ = OverlayLayer::Panel;
        std::shared_ptr<Window> popup_;                  /// < Carrier window (self-owned, see section 28 pitfall 1)
        Panel*  root_ = nullptr;                          /// < Carrier root panel
        Widget* pane_ = nullptr;                          /// < Self-painted content proxy
        Panel*  contentHost_ = nullptr;                    /// < Container content panel
        void*   owner_ = nullptr;                          /// < Current occupant
        std::function<void()> onEvict_;                    /// < Notifies the previous occupant on eviction
        OverlayContent content_;                           /// < Self-painted content
        Color4f background_{ 0.0f, 0.0f, 0.0f, 0.0f };    /// < Background (fully transparent by default)
        bool    visible_ = false;
        bool    followHost_ = true;
        bool    clickThrough_ = false;
        // ---- Nested-frame guard (defers on-screen show when Show is called inside a render frame) ----
        bool    showDeferred_ = false;                     /// < An on-screen show is waiting for frame end
        bool    deferQueued_ = false;                      /// < Fulfillment callback already scheduled (0ms timer)
        float   pendW_ = 0.0f, pendH_ = 0.0f;              /// < Latest geometry recorded during deferral
        float   anchorX_ = 0.0f;                           /// < Anchor (host client-area coordinates)
        float   anchorY_ = 0.0f;
        std::function<void()> onHostGeom_;                 /// < Host geometry callback
    };

    // ===================================================================
    //  Cleanup on host close (same family as carriers: "the host is gone but its stuff is still on screen")
    // ===================================================================

    /**
     * @brief Host close: also tears down the notification center attached to that host
     *
     * @details
     * Neither carrier of the notification center disappears when the host closes (see Source Map section 52):
     *  - Windows native engine -- each notification is a `WS_POPUP` small window with an empty owner,
     *    so the system has no idea who it belongs to;
     *  - Other platforms -- the `ChildKind::Popup` notification window created by `ToastNotifier`;
     *    on X11 it is only `WM_TRANSIENT_FOR`, and host destruction does not take it away.
     *
     * So the library can only notify them one by one at the close release point (here it looks up the registry by host).
     *
     * @note Implemented in `ZeToastNotifier.cpp` (only it knows that class); only the declaration is here --
     *       the reason it lives in this header rather than a new library-internal header: `Zekit.cpp` already includes this header,
     *       and this and `OverlayHost::Dismiss` are the two landing points of the same cleanup.
     * @note Called on the UI thread only.
     */
    void NotifyToastHostClosing(Window& host);

} // namespace Kit
