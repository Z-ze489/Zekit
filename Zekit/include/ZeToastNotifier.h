/**
 * @file ZeToastNotifier.h
 * @brief Kit::ToastNotifier -- notification center rendered in an independent window (a stack of toasts, one notification window)
 *
 * @details
 * Relationship to `Kit::Toast`: the same stack of toasts, a different carrier.
 *   - `Kit::Toast` is a floating layer inside the host window (`paintsOnTop` + HitTest always false);
 *     it cannot extend beyond the host window bounds, nor cover other programs;
 *   - `Kit::ToastNotifier` puts the same stack of toasts into an independent native window: on Windows
 *     it is the GDI+ layered window engine (one `WS_EX_LAYERED` topmost small window per notification,
 *     per-pixel alpha -- soft shadow, truly transparent rounded corners, see below); on other desktop
 *     platforms it is a `ChildKind::Popup` owned window. Both are borderless, topmost, and do not steal
 *     focus -- they can float above the host UI, even extend beyond the host bounds, while everything
 *     underneath stays clickable.
 *
 * @par How it is built (two desktop carriers, chosen by platform)
 * | Requirement | Windows (native engine) | Other platforms / fallback form |
 * |---|---|---|
 * | Truly transparent rounded corners + soft shadow | `WS_EX_LAYERED` + GDI+ `UpdateLayeredWindow` per-pixel alpha | Not supported (see "honest notes" below) |
 * | Stack of toasts + fade in/out + timed dismissal + semantic colors | Card stack drawn inside the engine (same visual parameters as ZeCore) | Reuses `Kit::Toast` as-is |
 * | Window follows the whole stack size | One window per notification, stacking and yielding arranged by the engine itself | `Toast::measureStack()` |
 * | Window anchored to a corner of the screen | `GetMonitorInfo().rcWork` | `Window::GetScreenWorkArea()` |
 * | Cross-thread `Show` | Forwarded via hidden message window (PostMessage) | Ultimately just an InvalidateRect |
 *
 * @par Windows native engine (since 2026-10-14, default implementation of the independent window form)
 * On desktop Windows it no longer goes through `Kit::Window` (the renderer canvas is opaque, so outside the
 * rounded corners there can only be the background color, and the shadow would be clipped); instead it
 * directly manages a set of topmost small windows of WS_POPUP + WS_EX_LAYERED: one window per
 * notification, GDI+ draws "pre-rendered soft shadow + rounded card + type icon circle + text +
 * close button", submitted with per-pixel premultiplied alpha -- the visual effect is the same as ZeCore's
 * `ToastNotification` (shadow spread 22px, corner radius 10px, slide-in + fade-in on entry, smooth
 * stack yielding, hover pauses the timer, close via X / clicking the card). Theme colors still honor
 * `ApplyTheme` (card background = `surface`, text = `text`, semantic colors = the four accent levels of
 * the `toast` section), and appearance interfaces such as `SetBackground / SetTextColor / SetFontSize`
 * still take effect. In this form `window()` returns nullptr (the notification window is a native layered
 * window, not a `Kit::Window`); `usesOwnWindow()` is still true.
 * Thread model: the thread of the first `Show` call becomes the engine driver thread (usually that is the
 * UI thread, and a message pump must be running -- the timers depend on it); subsequent cross-thread
 * `Show / Clear / Close` are forwarded there automatically. Destruction of ToastNotifier itself should
 * still be done on the UI thread.
 *
 * @par How the animation runs (worth knowing, because it dictates a few usage rules)
 * - Windows native engine: each notification window is driven by its own 16ms `SetTimer -> WM_TIMER`
 *   (entry/exit, smooth stack yielding displacement, timeout), completely independent of the host's drive
 *   mode -- whether the host sets `DriveMode::Immediate` or OnDemand, notifications animate as usual.
 * - Other platforms (path reusing `Kit::Toast`): the notification window does not run its own `exec()`
 *   (child windows share the host's message pump); every frame comes from its own WndProc:
 *   `WM_PAINT` / `WM_SIZE` / `WM_TIMER` / input dispatched to it.
 *   `Kit::Toast`'s per-frame renewal lands exactly on this path: `stepAnimation()` calls
 *   `window()->RequestFrameWake(16)`, the platform layer does `SetTimer` -> `WM_TIMER` -> paints one frame
 *   in place -> renews again, until the stack is empty. So (only on this path):
 *   - Do not set `DriveMode::Immediate` on the host window and assume notifications will follow --
 *     the host's per-frame scheduling cannot see child windows (it only walks its own widget tree);
 *   - Do not set `Immediate` on the notification window either (`RequestFrameWake` returns immediately
 *     under Immediate, and nothing else drives the notification window -> fade in/out would freeze).
 *     This class does not change the drive mode; the default is already correct.
 *
 * @par Usage
 * @code
 * Kit::Window win;
 * Kit::ToastNotifier notify(win);        // attached to the host (no window is created yet at this point)
 * win.AddWidget(...);
 *
 * // Can pop anywhere (cross-thread safe: forwarded internally to the driver thread)
 * notify.ShowSuccess("Saved");
 * notify.ShowError("Connection failed: request timed out", 4000);
 *
 * return win.exec();                     // one exec drives both the host and the notification window
 * @endcode
 *
 * @par Not done / cannot be done (honest notes)
 *  - Per-pixel alpha and soft shadow: on Windows delivered by the native layered window engine
 *    (truly transparent rounded corners + shadow spread 22px). On other platforms the independent
 *    window goes through the renderer, whose canvas is opaque -- pixels outside the rounded corners are
 *    the notification window's own background color (following the host theme's `bg`), and the shadow is
 *    clipped by the window edge. True transparency would require changes to each platform's render
 *    pipeline (D3D12 swap chain `AlphaMode` + DirectComposition, etc.), out of scope for this batch.
 *  - The window does not follow the host: it is screen-anchored (the four corners of the screen work
 *    area, or the absolute position given by `SetPosition`). When the host is dragged, notifications stay
 *    in place -- this is intentional: notifications belong to the "screen" layer (even if the host is
 *    minimized or covered by other windows, notifications remain in the corner). For a form that hugs the
 *    outside of the host window, use `NotifyAnchor::Custom` + compute from the host position yourself.
 *  - It is not a floating layer inside the host: it is a real native window (topmost + tool window +
 *    no focus stealing), so it can land outside the host rectangle and cover other programs. Mobile has no
 *    multi-window concept, so only there does it degrade to a floating layer inside the host (checkable
 *    via `usesOwnWindow()`).
 *  - Toast content is clickable (since 2026-10-14): the X in the top-right closes a single toast, clicking
 *    the card's blank area closes the whole toast. For full click-through back to the old behavior, use
 *    `SetClickThrough(true)` -- click-through is window-wide, so with it on the X cannot be clicked.
 *
 * @see ZeToast.h        the reused stack of toasts (the floating-layer-inside-host form)
 * @see Zekit.h          `ChildKind::Popup` / `SetClickThrough` / `SetPositionRelativeToHost`
 */

#pragma once
#include "ZeToast.h"      // The reused stack of toasts (also part of the public surface: ToastLevel / ToastPlace come from here)
#include "ZeSelfAction.h"

#include <cstddef>
#include <functional>
#include <string>

namespace Kit {

    class Window;
    struct ToastEngineImpl;   /// < Win32 native notification engine (implementation hidden in ZeToastNotifier.cpp)

    /**
     * @brief Which position on the screen the notification window is anchored to
     *
     * @details
     * Not the same thing as `Kit::Toast`'s `ToastPlace`, do not confuse them:
     *  - `ToastPlace` is "where the stack of toasts is placed within the host client area" (for the floating layer inside the host);
     *  - This enum is "where the notification window is placed within the screen work area".
     *
     * The notification window is screen-anchored: it does not move with the host window and can land outside
     * the host rectangle (even when the host is minimized it stays in the screen corner). To pin it to an
     * absolute position, use `Custom` with `SetPosition()` (screen coordinates, pixels).
     */
    enum class NotifyAnchor {
        TopLeft,       /// < Top-left corner of the screen work area
        TopCenter,     /// < Top-center of the screen work area (the notification stance of Linux / GNOME)
        TopRight,      /// < Top-right corner of the screen work area
        BottomLeft,    /// < Bottom-left corner of the screen work area
        BottomRight,   /// < Bottom-right corner of the screen work area (the Windows notification habit)
        Custom         /// < Custom absolute position (screen coordinates, see `SetPosition`)
    };
    /// @note The default when unspecified is platform-dependent (see the constructor):
    ///        - Windows -- `BottomRight` (bottom-right corner, where the action center is);
    ///        - Other desktops (X11/Wayland) -- `TopCenter` (top-center of the screen,
    ///          the same stance as GNOME/KDE system notification bars).
    ///       To fix a corner, call `SetAnchor` explicitly; do not rely on the default.

    /**
     * @brief Notification center in an independent window: one notification window hosts a stack of toasts
     */
    class ToastNotifier {
    public:
        /**
         * @brief Attaches to a host window
         * @param host Host window. Must outlive this object (the notification window is its owned window)
         * @note Construction does not create a window: the notification window is created lazily on the first
         *       `Show` -- because creating a child window also builds the host's render backend, and doing so
         *       while "the host has not yet decided which renderer to use (`UseRenderer`)" would lock in the
         *       backend. By the time the first toast pops, the host has usually already called `Show()`, so the
         *       timing is naturally correct.
         */
        explicit ToastNotifier(Window& host);
        ~ToastNotifier();

        // Window identity is unique (contains ownership of an owned window); copy and move are forbidden
        ToastNotifier(const ToastNotifier&) = delete;
        ToastNotifier& operator=(const ToastNotifier&) = delete;
        ToastNotifier(ToastNotifier&&) = delete;
        ToastNotifier& operator=(ToastNotifier&&) = delete;

        // ===================================================================
        //  Pop messages (one-to-one with Kit::Toast -- a control that changes carrier should not change usage)
        // ===================================================================
        // @brief Pops one toast (`durationMs <= 0` = persistent)
        void Show(std::string text, ToastLevel level = ToastLevel::Info, int durationMs = 2200);
        void ShowInfo(std::string text, int durationMs = 2200);
        void ShowSuccess(std::string text, int durationMs = 2200);
        void ShowWarning(std::string text, int durationMs = 3000);
        void ShowError(std::string text, int durationMs = 3000);
        /// @brief Clears all messages (stack emptied -> notification window collapses automatically)
        void Clear();

        /// @brief Current count
        std::size_t count() const;
        /// @brief Whether currently showing
        bool isShowing() const;

        // ===================================================================
        //  Position / appearance (forwarded to the internal stack of toasts)
        // ===================================================================
        // @brief Which corner of the screen the notification window is anchored to (default `BottomRight`)
        // @note It is screen-anchored (inset `margin` from the screen work area), not the host client area --
        //       so the notification window can land outside the host window and does not move with the host.
        // @note Platforms that cannot obtain the screen work area (default implementation returns false) degrade
        //       automatically to the equivalent corner relative to the host client area; functionality is unaffected.
        void SetAnchor(NotifyAnchor a);
        NotifyAnchor anchor() const { return anchor_; }

        /// @brief Pins to an absolute screen position (pixels, screen coordinates; top-left aligned)
        /// @note Calling this is equivalent to `SetAnchor(NotifyAnchor::Custom)`.
        void SetPosition(float x, float y);
        float positionX() const { return customX_; }
        float positionY() const { return customY_; }

        /// @brief Margin from the screen work area edge (default 24)
        /// @note Under the `Custom` anchor this value does not participate in the computation (the position is absolute).
        void SetMargin(float m);
        float margin() const { return margin_; }

        /// @brief Maximum number shown at once (excess drops the oldest; default 4)
        void SetMaxVisible(int n);
        /// @brief Maximum width of a single toast (default 340; 0 = natural width of the text)
        void SetMaxWidth(float w);
        /// @brief Spacing between two toasts (default 8)
        void SetItemGap(float g);
        /// @brief Fade-in / fade-out duration (default 180ms)
        void SetFadeDuration(int ms);
        /// @brief Toast background color
        void SetBackground(const Color4f& c);
        /// @brief Toast text color
        void SetTextColor(const Color4f& c);
        /// @brief Toast text font size (pixels)
        void SetFontSize(unsigned int pixelSize);

        // ===================================================================
        //  Notification sound / hover pause (same semantics as Kit::Toast)
        // ===================================================================
        // @brief Notification sound mode (default System: MessageBeep mapped by level, follows the system sound scheme)
        void SetSound(ToastSound mode);
        ToastSound sound() const { return sound_; }
        /// @brief wav path for Custom mode (takes precedence over the customSound function; played asynchronously, non-blocking)
        void SetSoundFile(const std::string& path);
        /// @brief Custom notification sound function for Custom mode (called when there is no wav file)
        void SetCustomSound(std::function<void()> fn);
        /// @brief Pauses the auto-close countdown while the mouse hovers over the notification (default on)
        void SetPauseOnHover(bool on);
        bool pausesOnHover() const { return pauseOnHover_; }

        // ===================================================================
        //  Window behavior
        // ===================================================================
        // @brief Mouse click-through (default on): clicking a notification is like clicking air, falls through to the host
        // @note Turning it off makes notifications clickable -- but then "whatever the notification covers also cannot be clicked",
        //       and this class does not provide "click to dismiss"; wire it up yourself (see the notes on `window()`).
        // @brief Mouse click-through (default off: the X close button and click-blank-to-close both need the mouse)
        // @note With click-through on, notifications do not receive the mouse at all (`WS_EX_TRANSPARENT` is window-wide;
        //       there is no semi-transparent form that only passes through the card's blank area) -- so the X cannot be clicked either.
        void SetClickThrough(bool on);
        bool isClickThrough() const { return clickThrough_; }

        /// @brief Topmost (default on: notifications should sit above other windows)
        void SetTopmost(bool on);
        bool isTopmost() const { return topmost_; }

        /// @brief Whole-window opacity (0~1, default 1). Use for "fade in the whole stack together"
        /// @note This is different from `Kit::Toast`'s per-toast fade in/out: that is each toast's own alpha,
        ///       this is the composite opacity of the whole window.
        void SetOpacity(float alpha);

        /// @brief Makes the notification window follow the given theme (applies to both the window canvas and the stack of toasts)
        /// @note When the child window is created it inherits the host's theme at that moment; later host re-theming
        ///       (`Window::SetTheme` / light-dark switching) does not automatically reach the notification window --
        ///       call this interface when needed (typically `notify.ApplyTheme(win.theme())`).
        void ApplyTheme(const Theme& t);

        // ===================================================================
        //  Window lifecycle / state
        // ===================================================================
        // @brief Whether the notification window is created right now (false if no message has been popped)
        bool hasWindow() const { return win_ != nullptr || eng_ != nullptr; }
        /// @brief Whether the notification is showing right now
        bool isOpen() const;
        /// @brief Whether it uses an independent window or a floating layer inside the host (mobile degradation, see below)
        /// @note Always true on Windows: either the native layered window engine or a popup window.
        bool usesOwnWindow() const { return win_ != nullptr || eng_ != nullptr; }
        /// @brief The notification window itself (nullptr if not created)
        /// @note It is also an ordinary `Kit::Window` -- adding widgets, setting a title, measuring text all work.
        ///       WARNING: but do not call `exec()` on it (child windows are driven by the host), and do not destroy
        ///       this object inside its own callbacks.
        /// @note The Windows native engine form returns nullptr (the notification window is a native layered window
        ///       that does not go through `Kit::Window`) -- in this form there is no widget surface to operate on.
        Window* window() const { return win_; }

        /// @brief Collapses the notification window immediately (clears messages + hides the window; does not destroy the object)
        void Close();

        /// @brief Host window
        Window& host() const { return host_; }

    private:
        // -------------------------------------------------------------------
        //  Internal: the host carrier of the stack of toasts (a Toast that syncs window geometry after painting)
        // -------------------------------------------------------------------
        //  Why derive instead of "polling the size elsewhere": `Toast`'s whole-stack size is only real on the frame
        //  after it has painted (expired items are removed in `Paint`). Deriving a subclass and syncing the window
        //  after `Paint` is the only place that neither computes an extra time nor lags a frame.
        //  Made a private nested class: it should not be part of the delivery surface, but needs access to
        //  `ToastNotifier`'s private members.
        class NotifierToast : public Toast {
        public:
            explicit NotifierToast(ToastNotifier& owner) : owner_(owner) {}
        protected:
            /// @brief Paints as usual first (includes stepAnimation: purge expired + renew frame), then syncs window geometry
            void Paint(FrameCollector& fc) const override;
        private:
            ToastNotifier& owner_;
        };

        /// @brief Creates the carrier (called on the first `Show`): an independent window, or degrades to a host floating layer when the platform does not support it
        /// @return false = even the degradation is impossible (theoretically cannot happen)
        bool ensureAttached();

        /// @brief Syncs the notification window to the current whole-stack size (size / position / visibility); idempotent
        /// @note Called by `NotifierToast::Paint` and all public interfaces -- "geometry is computed in exactly one place"
        void syncWindow();

        /// @brief Measures + sets size + places; returns "is there anything left in the stack"
        bool applyGeometry();

        /// @brief Pushes all appearance options remembered by this class to `toast_` (used once when creating the carrier)
        void pushOptions();

        /// @brief Tears down immediately when the host closes (called internally by the library, see `NotifyToastHostClosing`)
        /// @details
        /// The only difference from `Close()`: it does not fade out. The host is closing, the message pump will
        /// likely exit on the next tick, and the fade-out animation's timer cannot finish -- those card windows
        /// would freeze on a semi-transparent frame, which looks much worse than "disappearing directly".
        /// Also it does not assume the host is still alive: after `win_->IsClosed()` everything goes through
        /// idempotent no-ops.
        void shutdownWithHost();

        /// @brief Library internal: looks up by host and tears down when the host closes (defined in ZeToastNotifier.cpp)
        friend void NotifyToastHostClosing(Window& host);

    private:
        Window& host_;

        Window* win_ = nullptr;              /// < Notification window (managed by the library; detached when this object is destroyed)
        Toast*  toast_ = nullptr;            /// < Stack of toasts (belongs to win_, or to host_ in the degraded form)
        bool    toastOwnedByHost_ = false;   /// < true = degraded form (widgets attached to the host)
        bool    visible_ = false;            /// < Whether the notification window is currently shown
        bool    suppressSync_ = false;       /// < Suppresses syncing while creating the carrier (avoids flashing an empty window for one frame)

        int     lastX_ = -1, lastY_ = -1;    /// < Last placement (screen coordinates; -1 = not placed yet)

        // ---- Appearance options (recorded here before the carrier is created, pushed to the stack of toasts once after creation) ----
        NotifyAnchor anchor_ = NotifyAnchor::BottomRight;
        float customX_ = 0.0f;               /// < Absolute X under the `Custom` anchor (screen coordinates)
        float customY_ = 0.0f;
        float margin_ = 24.0f;
        int   maxVisible_ = 4;
        float maxWidth_ = 340.0f;
        float itemGap_ = 8.0f;
        int   fadeMs_ = 180;

        bool    bgSet_ = false;          /// < User explicitly set the background color (only pushed if set)
        Color4f bg_{ 0, 0, 0, 0 };
        bool    fgSet_ = false;          /// < User explicitly set the text color
        Color4f fg_{ 0, 0, 0, 0 };
        unsigned fontSize_ = 0;          /// < 0 = not set (use the theme font size)

        bool  clickThrough_ = false;     /// < Default off: X / click-blank-to-close need the mouse (click-through must be enabled explicitly)
        bool  topmost_ = true;
        bool  opacitySet_ = false;
        float opacity_ = 1.0f;

        // ---- Notification sound / hover pause (both carriers honor these) ----
        ToastSound sound_ = ToastSound::System;
        std::string soundFile_;               /// < Custom mode: wav path
        std::function<void()> customSound_;   /// < Custom mode: function (used when there is no wav)
        bool  pauseOnHover_ = true;

        // ---- Win32 native notification engine (created lazily on the first Show; implementation hidden in .cpp) ----
        //   The independent window form on desktop Windows is taken over by it: per-pixel alpha layered window +
        //   GDI+ self-drawing. It is a friend -- the engine reads the settings above directly.
        ToastEngineImpl* eng_ = nullptr;
        friend struct ToastEngineImpl;
    };

} // namespace Kit
