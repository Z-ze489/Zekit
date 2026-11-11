/**
 * @file ZeOverlayWindow.h
 * @brief Kit::OverlayWindow -- the overlay carrier handle: moves overlay content onto an independent
 *        `ChildKind::Popup` undecorated owned window (shared, reused, follows the host).
 *
 * @details
 * Problem it solves: Kit overlays (ComboBox popup / menu / tooltip / drawer / date-time and color
 * pickers / toast / image preview) traditionally use "second-pass painting" (`paintsOnTop()`) --
 * they paint inside the host window's own backend. Once a native child window appears in the host
 * client area (the separate HWND that `Kit::CanvasView` hands to an external renderer), second-pass
 * painting can never beat it in composition order -- the overlay gets swallowed by the canvas.
 *
 * `OverlayWindow` moves overlay content into an independent window:
 *  - It has its own HWND (`WS_POPUP` + `WS_EX_NOACTIVATE` + `CS_DROPSHADOW` + rounded corners),
 *    and `WS_EX_TOPMOST` naturally beats every non-topmost window -- including the CanvasView canvas;
 *  - The render backend is shared with the host (GL share group: the font atlas does not duplicate
 *    VRAM; backends that do not support sharing fall back to an independent context, functionality
 *    is not broken);
 *  - `WS_EX_NOACTIVATE`: neither popup nor click steals host focus;
 *  - Shared per layer, reused while hidden: each layer on the same host has only one carrier window,
 *    created on first use and then reused via `Show/Hide` -- no repeated window creation / swap chain
 *    creation overhead.
 *
 * @par Layers (`OverlayLayer`)
 * A layer has only one occupant at a time (a later occupant pushes out the previous one and notifies
 * it to collapse). Layering lets combinations that appear simultaneously, such as "tooltip over popup"
 * or "toast over drawer", each have their own window instead of fighting over the same one.
 *
 * @par Background three states (`SetBackground`)
 * The carrier is always per-pixel transparent (clear alpha=0); the background is a rectangle the
 * carrier paints itself:
 * ```
 * alpha = 0     -> fully transparent: only content is visible, the background is the host's own
 *                  image (used by image preview)
 * 0 < alpha < 1 -> semi-transparent: content + a dimming layer (default form of image preview)
 * alpha = 1     -> solid: content + opaque base color
 * ```
 * The three can be switched at any time without recreating the swap chain (the clear color stays
 * constant, so the composition mode does not change).
 *
 * @par Following the host
 * The carrier is a top-level window; the system does not move it along with the host. Position
 * synchronization is driven by host geometry change notifications
 * (`WM_WINDOWPOSCHANGED` -> `IWindowHost::OnHostGeometryChanged`) -- only message-driven
 * notifications arrive inside the system modal drag / resize loop (library-level timers do not run
 * there, which is the root cause of the old "overlay does not follow while dragging the window" bug).
 *
 * @par Basic usage (self-painted overlay)
 * @code
 * class MyPopup {
 *     Kit::OverlayWindow overlay_;
 * public:
 *     explicit MyPopup(Kit::Window& w) : overlay_(w, Kit::OverlayLayer::Panel) {
 *         Kit::OverlayContent c;
 *         c.paint = [this](Kit::FrameCollector& fc) { paintInto(fc); };   // origin = overlay top-left
 *         c.onPress = [this](Kit::MouseEvent& e) { return onPress(e); };  // coords = host frame
 *         overlay_.SetContent(std::move(c));
 *         overlay_.SetBackground({0, 0, 0, 0.45f});   // semi-transparent
 *     }
 *     void Open(float x, float y, float w, float h) { overlay_.Show(x, y, w, h); }
 *     void Close() { overlay_.Hide(); }
 * };
 * @endcode
 *
 * @par Basic usage (container overlay: real widgets attached)
 * @code
 * Kit::OverlayWindow overlay(win);
 * overlay.content().AddChild(Kit::Label("I am overlay content"));   // content root fills the carrier
 * overlay.SetBackground({ 1, 1, 1, 1 });
 * overlay.Show(120, 80, 240, 180);
 * @endcode
 *
 * @par Platform capability
 * Only platforms with `Supports(ChildWindow) && Supports(PopupWindow)` can create a native carrier
 * (Win32 is implemented; X11 / mobile honestly do not promise it per capability-bit discipline).
 * The test is the static `SupportsNative(host)`: callers use it to decide between the native carrier
 * and falling back to the old `paintsOnTop` path.
 *
 * @note The `FrameCollector` used by `OverlayContent::paint` has the overlay top-left corner as origin;
 *       the coordinates received by mouse callbacks are already converted back to host client-area
 *       coordinates -- widgets can hit-test directly with their own geometry, identical to when
 *       "painted in the host".
 * @note The semantics of "auto-collapse when clicking outside the overlay" while the popup is shown
 *       still belong to the overlay widget itself -- this class only manages the carrier.
 *
 * @see ZeToastNotifier.h precedent for the same mechanism (independent window form of the
 *      notification bar)
 * @see Zekit.h `ChildKind::Popup`, `SetPositionRelativeToHost`, `Capability::PopupWindow`
 */

#pragma once
#include "Zekit.h"
#include "ZePanel.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

namespace Kit {

    // ===================================================================
    //  Layers
    // ===================================================================

    /// @brief Overlay layer (one occupant per layer at a time; the layer number is the z order, larger is higher)
    enum class OverlayLayer {
        Panel = 0,   /// < interactive overlays: picker popups / drawers / image preview / menu modals
        Toast = 1,   /// < toast bar
        Tooltip = 2  /// < hover tooltip (click-through)
    };

    // ===================================================================
    //  Self-painted content contract
    // ===================================================================

    /**
     * @brief Overlay content (self-painted form): the carrier only places this window and routes clicks back
     *
     * @details
     * The frame of reference for all callbacks is fixed as follows, so widgets need not guess the
     * coordinate system:
     *  - `paint` -- the `FrameCollector` passed to it has the overlay top-left corner as origin
     *    (widgets subtract the overlay origin from host coordinates before painting, i.e.
     *    `paintPopupInto(fc, true)`);
     *  - mouse callbacks -- the `x/y` of `MouseEvent` are already converted back to host client-area
     *    coordinates, so widgets can hit-test directly with their own geometry, exactly as when
     *    "painted in the host".
     */
    struct OverlayContent {
        /// @brief Paints (origin = overlay top-left corner)
        std::function<void(FrameCollector&)> paint;
        /// @brief Left button press / release / move / wheel (coordinates already converted back to host frame)
        std::function<bool(MouseEvent&)> onPress;
        std::function<bool(MouseEvent&)> onRelease;
        std::function<bool(MouseEvent&)> onMove;
        std::function<bool(MouseEvent&)> onWheel;
        /// @brief Pointer leaves the carrier
        std::function<void()> onLeave;
        /// @brief The host window received a mouse press (coordinates = host client-area frame)
        /// @details
        /// The carrier is an independent window, so clicks in the host do not reach the carrier -- yet
        /// "collapse when clicking outside the overlay" is exactly the standard behavior of this kind of
        /// overlay. The carrier calls this back on every host press, and the widget uses it to decide
        /// "whether this press is on me" (if yes, ignore; if no, collapse).
        std::function<void(float hostX, float hostY)> onHostPress;
    };

    class OverlaySurface;   /// < library-internal implementation (ZeOverlayHost.h): a reusable carrier

    /**
     * @brief Handle to one overlay carrier (widgets hold it; the real window is shared per layer)
     *
     * @details
     * The handle does not own the window: windows are registered in the `OverlayHost` pool by
     * "host x layer", and this object merely borrows a layer. Therefore:
     *  - When a second occupant of the same layer appears, the previous one is taken over
     *    (`isVisible()` becomes false), so two windows never fight;
     *  - The taken-over party must collapse its own popup -- register a callback with
     *    `SetOnTakenOver`. Merely being marked "invisible" is not enough: the widget usually still
     *    keeps `open_` state, and once the "use native popup" test becomes false it falls back to
     *    `paintsOnTop` and paints the popup back into the host.
     *    (Observed 2026-11-04: "open the combo box and the date picker calendar is still hanging there".)
     *  - Handle destruction = return this layer (the window stays for the next user).
     */
    class OverlayWindow {
    public:
        /**
         * @brief Whether a native overlay carrier can be created on this host
         * @param host host window
         * @return true = `Show()` lands on an independent Popup window;
         *         false = the platform lacks this capability, the caller should fall back to the old
         *         `paintsOnTop` path
         * @note The test is the capability bit, not a platform macro (same discipline as ToastNotifier).
         */
        static bool SupportsNative(const Window& host);

        /// @brief Attaches to the host window (does not create a window: the carrier is created on first `Show()`)
        /// @param host host window. Must outlive this object
        /// @param layer layer (see `OverlayLayer`; default `Panel`)
        explicit OverlayWindow(Window& host, OverlayLayer layer = OverlayLayer::Panel);

        /// @brief Destructor: hides the overlay and returns this layer (the window stays in the pool for reuse)
        ~OverlayWindow();

        OverlayWindow(const OverlayWindow&) = delete;
        OverlayWindow& operator=(const OverlayWindow&) = delete;

        // -------------------------------------------------------------------
        //  Content
        // -------------------------------------------------------------------

        /// @brief Sets self-painted content (paint / mouse callbacks; empty struct = clear)
        void SetContent(OverlayContent content);

        /// @brief Container-form content root (`Panel`, automatically fills the whole carrier on Show)
        /// @note The first call creates the carrier window (hidden after creation). Use it to host real
        ///       widgets (drawers / panels); use `SetContent` for self-painted overlays; both can coexist
        ///       (container content paints above self-painted content).
        Panel& content();

        /// @brief Whether the carrier window has been created (and not closed)
        bool isReady() const;

        /// @brief Whether the overlay is currently shown (automatically becomes false after being taken over by a later occupant of the same layer)
        bool isVisible() const;

        /// @brief The carrier window (nullptr if not created). Use it to call Popup-specific interfaces
        Window* nativeWindow();

        // -------------------------------------------------------------------
        //  Show / hide
        // -------------------------------------------------------------------

        /**
         * @brief Shows the overlay
         * @param hostX,hostY anchor, in host client-area coordinates (the popup top-left corner lands here)
         * @param width,height overlay client-area size; content fills it automatically
         * @note Follows host move / resize while shown (see `SetFollowHost`).
         */
        void Show(float hostX, float hostY, float width, float height);

        /// @brief Hides the overlay (the window is kept and reused by the next `Show` -- zero creation overhead for frequent toggling)
        void Hide();

        /// @brief Repaints only this carrier region (use when content changed but size did not)
        void Repaint();

        // -------------------------------------------------------------------
        //  Behavior / appearance
        // -------------------------------------------------------------------

        /// @brief Background: alpha=0 fully transparent / 0<alpha<1 semi-transparent / alpha=1 solid
        /// @note Only affects the rectangle the carrier paints itself, does not recreate the swap chain; can be changed at any time.
        void SetBackground(const Color4f& bg);

        /// @brief Whether to follow host move / resize while shown (default true)
        void SetFollowHost(bool on);

        /// @brief Whole-window opacity (0.0 ~ 1.0; 0 = disable the effect and restore opaque)
        /// @note Uses the platform layered window (`Capability::WindowOpacity`); silently no-op on platforms that do not support it.
        void SetOpacity(float alpha);

        /// @brief Mouse click-through for the whole window (clicks land on the window below the overlay; used by tooltips; off by default)
        void SetClickThrough(bool on);

        /// @brief Whether topmost (`WS_EX_TOPMOST`; on by default -- this is what beats the CanvasView canvas)
        void SetTopmost(bool on);

        /// @brief Changes theme (overlay content and carrier together; the user forwards it once when the host changes theme)
        void SetTheme(const Theme& theme);

        /// @brief Callback when taken over by a later occupant of the same layer (the widget collapses itself here)
        ///
        /// @details
        /// A layer has only one window. A later overlay takes the layer as soon as it calls `SetContent` /
        /// `Show`, and merely marking the old occupant "invisible" is not enough -- the widget still keeps
        /// `open_` state, while `nativePopup()` has already become false, so it falls back to `paintsOnTop`
        /// and paints the popup back into the host window. What the user sees is "open the combo box and
        /// the date picker calendar is still hanging there".
        ///
        /// So this notification is what is missing: on receipt, close your own popup (not hide the carrier --
        /// that window is no longer yours, `Hide()` is blocked by the ownership check).
        ///
        /// @note In the callback only collapse: change state, request host repaint. Do not call `Show()`
        ///       again (that would in turn push out the new occupant, and the two widgets fight).
        void SetOnTakenOver(std::function<void()> cb);

        /// @brief Host geometry change callback (for overlays that need to recompute geometry, such as drawers / whole-window previews)
        /// @note Called before repositioning: in the callback just call `Show()` again with the new geometry.
        void SetOnHostGeometryChanged(std::function<void()> cb);

    private:
        /// @brief Claims occupancy (and wires "taken over" to `onTakenOver_`)
        void attachOwner();

        std::shared_ptr<OverlaySurface> surface_;   /// < borrowed carrier (shared in the pool)
        //  * There is deliberately no cached "I remember I am shown" flag here (`shown_` was removed
        //    2026-11-10). `Show` is deferred to screen by the carrier within the render frame, so the
        //    cached value can only record false at that moment, and the deferred present never comes back
        //    to update it -- so the widget always thinks "not shown", falls back to host self-painting, and
        //    two copies of content stack on screen. Visibility has only one reliable source: ask the
        //    carrier now (see `isVisible`).
        std::function<void()>            onTakenOver_;   /// < notification when pushed out by a later occupant of the same layer

        /// @brief The content this handle last set (reinstalled on `Show`)
        ///
        /// @details
        /// The carrier is shared: after I am taken over, `content_` is already someone else's paint
        /// callback. If I only `Show` without resetting content when opening again, someone else's content
        /// appears on screen (observed: reopening the date picker shows the previous combo box's items in
        /// the popup). So a copy of the content is kept here and reinstalled before every `Show` -- this is
        /// the only reliable place for "whoever opens is responsible for its own picture" (the 9 overlay
        /// widgets do not each write it).
        std::optional<OverlayContent>    ownContent_;
    };

} // namespace Kit
