#pragma once

// =====================================================================
// Plat::X11Window -- window implementation for the Linux (X11 / Xlib) platform
// =====================================================================
// Fully symmetric with ZeWindowWin32.h: it does only one thing, "the native window exists".
// It participates in the build only on Linux at compile time (see the #if dispatch in ZeWindow.cpp) -- non-Linux
// platforms do not compile this file at all (compiling <X11/Xlib.h> on Windows is a compile failure).
//
// Internally it holds Display* / ::Window(XID) / XIC, all locked inside Impl in the .cpp --
// this header exposes no Xlib types, so including it does not pollute the caller's macro environment
// (in Xlib, the extremely common short names `Window` / `None` / `Success` / `Status`
//  would clash with identically named identifiers in user code once leaked).
// =====================================================================

#include "ZePlatformWindow.h"
#include <memory>

namespace Plat {

	class X11Window final : public IWindow {
	public:
		X11Window();
		~X11Window() override;

		X11Window(const X11Window&) = delete;
		X11Window& operator=(const X11Window&) = delete;

		bool Create(const WindowDesc& desc, IWindowHost& host) override;

  /// @brief X11 likewise "has a real movable window" -> full desktop capability set
  /// @note Two entries are protocol capability differences, not implementation shortcuts (see the notes in each implementation):
  ///       - `SetDarkTitleBar` is only a hint to the WM (_GTK_THEME_VARIANT),
  ///         only GTK-based WMs read it -- but it does do something, so the bit stays;
  ///       the only one that truly "does nothing when called" is `SetIconFromHandle` (an escape hatch, never in the
  ///       capability bits to begin with).
  /// @brief The X11 capability set -- full desktop capability set + three overlay capabilities
  /// @details
  /// The `ChildWindow` bit was once cleared: when `Create` did not yet handle `WindowDesc::parent`,
  /// keeping the bit would make `Kit::Window(parent, ...)` create the child window as a top-level window
  /// (centered on screen, with WM decorations) -- that was a silent error. Now the `XCreateWindow` child window
  /// path is in place (see the separate branch in `Create`), so the bit is added back.
  ///
  /// 2026-11-07 added the three overlay items (previously none were declared, so all overlay widgets fell back to
  /// host self-painting -- content correct, but clipped by the window bounds and unable to stay above other windows):
  ///  - `PopupWindow`        -- override-redirect undecorated window + does not take focus + does not occupy the taskbar;
  ///  - `ClickThrough`       -- `XShape` input shape set empty, clicks fall through to the window below;
  ///  - `PerPixelTransparent`-- 32-bit ARGB visual, and set only when a compositor actually exists on the system.
  ///
  /// This last bit is determined at runtime, not a constant: on an X11 session without a compositor,
  /// an ARGB window is painted as a black blob, worse than opaque, so on such machines it must truthfully be 0
  /// (see the note on this bit in `ZeCapability.h`).
		Cap Capabilities() const override;

		void Show() override;
		void Hide() override;
		void Destroy() override;

		bool PollEvent() override;
		void WaitEvents(int timeoutMs) override;
		int  ExitCode() const override;

		void GetClientSize(int& w, int& h) const override;
		void SetClientSize(int w, int h) override;
		void SetCenterOnResize(bool on) override;
		void SetPosition(int x, int y) override;
		void SetPositionRelativeToHost(int x, int y) override;
		void CenterOnScreen() override;
  /// @brief Removes WM_TRANSIENT_FOR, promoting a dependent window to an independent top-level window (child windows inherit from the host)
  /// @note `Owned` / `Free` can be released; `Child` (X child window) and `Popup` return false.
		bool ReleaseFromHost() override;

		float ScaleFactor() const override;
		int   Dpi() const override;

		void Minimize() override;
		void Maximize() override;
		void Restore() override;
		bool IsMinimized() const override;
		bool IsMaximized() const override;
		void SetTopmost(bool on) override;

		void SetResizable(bool on) override;
		void SetSizeLimits(int minW, int minH, int maxW, int maxH) override;
		void SetFullscreen(bool on) override;
		void SetBorderless(bool on) override;
		void ApplyTitleBar(bool titleBarVisible,
			bool minBtn, bool maxBtn, bool closeBtn) override;
		void SetIconFromFile(const std::string& utf8Path) override;
		void SetIconFromResource(int resId) override;
		void SetIconFromHandle(void* hIcon) override;
		void SetOpacity(float alpha) override;
  /// @brief Per-pixel transparency (X11 = 32-bit ARGB visual; effective only at window creation, see the note in the implementation)
		void SetPerPixelTransparent(bool on) override;
  /// @brief Mouse click-through for the whole window (`XShape` input shape set empty -- clicks fall through to the window below)
		void SetClickThrough(bool on) override;
  /// @brief Mouse cursor (X11 cursor font; mapping in the table in the implementation)
		void SetMouseCursor(Kit::Cursor c) override;
  /// @brief Screen work area (`_NET_WORKAREA` / `_NET_CURRENT_DESKTOP`; falls back to the full screen if unreadable)
		bool GetScreenWorkArea(int& x, int& y, int& w, int& h) const override;
  /// @brief Client area origin -> absolute screen coordinates (asks the server via XTranslateCoordinates)
		void ClientOriginToScreen(int& x, int& y) const override;
  /// @brief Edges occupied by window decorations (`_NET_FRAME_EXTENTS`; 0 if the WM does not provide it)
		void GetFrameInsets(int& left, int& top, int& right, int& bottom) const override;
  /// @brief Enables / disables the whole window (when disabled, the platform layer drops mouse and keyboard events directly)
		void SetEnabled(bool on) override;
		void SetDarkTitleBar(bool dark) override;
		void SetBackdropColor(unsigned int rgb) override;
		void SetImeSpot(float x, float lineTop, float lineBottom) override;

  // ---- Clipboard (X11 Selection protocol; see the implementation notes in ZeWindowX11.cpp) ----
		std::string GetClipboardText() override;
		void        SetClipboardText(const std::string& utf8) override;

		void SetTitle(const std::string& utf8) override;
		bool IsClosed() const override;
		bool IsActive() const override;
		void RequestRedraw() override;
		void RequestWakeup(int afterMs) override;
		void CancelWakeup() override;
		void SetTimerPrecision(bool on) override;
		void PostClose() override;

		void RequestQuit(int exitCode) override;

		void StartWindowDrag() override;
		bool IsMouseButtonDown(MouseButton b) const override;

  // ---- File drag and drop (XDND) / file dialog (external dialog program) ----
  //  Previously none of these were implemented on the X11 side (base class default = do nothing), so
  //  FileDropZone "does not react to a drop" and FileDialog "does nothing when clicked".
		void SetAcceptFileDrop(bool on) override;
		bool ShowFileDialog(const FileDialogDesc& desc,
			std::vector<std::string>& out) override;

		void* NativeHandle() const override;

	private:
		struct Impl;
		std::unique_ptr<Impl> m_impl;

  // =================================================================
  //  The four small pieces of the message pump (implementation in the "message pump" section of the .cpp)
  // =================================================================
  //  These four are parts of the message pump, not utilities for external reuse: they only share process-level state
  //  such as "which windows have pending work" between PollEvent / WaitEvents
  //  (the implementation lives in an anonymous namespace in the .cpp, see X11State).
  //  They are class members rather than file-level static functions because their signatures must name
  //  X11Window::Impl -- a private nested type of this class, which only member functions can name.
  //
  //  * The first two follow the same convention: return true immediately after dispatching one event. The host callback may
  //    destroy some window (close -> destroy native window -> remove from the process-level window table),
  //    so the caller receiving true must return immediately and must never "keep iterating after dispatching".

  /// @brief Dispatches one due timer wakeup (synthetic event 1, highest priority)
  /// @return true = one EventKind::Redraw was dispatched
		static bool DeliverDueWakeup();

  /// @brief Dispatches one pending repaint (synthetic event 2, lowest priority)
  /// @return true = one EventKind::Redraw was dispatched
		static bool DeliverPendingRedraw();

  /// @brief Computes how long this select should sleep (X11 has no SetTimer; wakeups are implemented by changing the timeout)
  /// @param requestedMs timeout requested by the caller (< 0 = wait indefinitely)
  /// @return actual milliseconds; 0 = do not sleep (synthetic events are waiting to be dispatched); -1 = wait indefinitely
		static int ComputeWaitTimeoutMs(int requestedMs);

  /// @brief Marks wakeups past their deadline on the window (dispatch is left to the next PollEvent round)
		static void MarkExpiredWakeups();
	};

 /// @brief Returns the process-shared X11 Display connection (for the Linux render backend of the Rhi layer)
 ///
 /// @details
 /// The Plat layer and the Rhi layer are two pillars of the same library; this function is the only
 /// handshake point between them: the render backend must obtain the X11 connection and
 /// the window XID to create a GLX context / Vulkan surface.
 ///
 /// - The connection is process-shared (each X11Window does not open its own) -- this is the X11
 ///   convention: one connection, one event queue; multiple connections turn select / event dispatch into a mess;
 /// - The return type is deliberately `void*` rather than `Display*`: this header does not pull in Xlib,
 ///   so the caller (`ZeRenderBackendLinux.cpp`, compiled only on Linux) casts it itself:
 ///   @code
 ///   auto* dpy = static_cast<Display*>(Plat::X11DisplayHandle());
 ///   @endcode
 ///
 /// @return Display connection; returns nullptr when the connection has not been established yet (no window has been created).
 /// @note Must be called after at least one window has been created; closed automatically on process exit.
	void* X11DisplayHandle();

 /// @brief Returns the XID of a native window on X11 (also for the render backend)
 /// @param nativeHandle return value of @ref IWindow::NativeHandle
 /// @return XID (returned as `unsigned long` to avoid pulling in Xlib types); 0 = invalid
 /// @note Difference from `NativeHandle()`: the latter gives an opaque `void*` (Kit only uses it as
 ///       "is it nullptr"), while this function restores it to the integer the render backend can pass directly to
 ///       `glXMakeCurrent(dpy, xid, ctx)`. Both refer to the same window.
	unsigned long X11WindowId(void* nativeHandle);

 /// @brief Returns the visual id of an existing X11 window (for EGL config selection)
 /// @param nativeHandle return value of @ref IWindow::NativeHandle
 /// @return visual id; 0 = invalid window or connection not established
 ///
 /// @details
 /// A hard EGL requirement: the visual of the EGLConfig used by `eglCreateWindowSurface` must match
 /// the window's visual, otherwise `EGL_BAD_MATCH`. Ordinary windows use the 24-bit default visual,
 /// which almost always matches "pick any config", so this function was not needed before;
 /// but per-pixel transparent overlay windows use a 32-bit ARGB visual -- without selecting the config by it,
 /// the whole render backend cannot be created (the overlay window exists but stays blank inside).
	unsigned long X11VisualId(void* nativeHandle);

} // namespace Plat
