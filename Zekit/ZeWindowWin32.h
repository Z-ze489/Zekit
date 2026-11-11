#pragma once

// =====================================================================
// Plat::Win32Window -- window implementation for the Windows platform
// =====================================================================
// Does only one thing: "a native window exists". Participates in the build only on Windows at compile time:
// Non-Windows platforms do not compile this file at all (see the #if dispatch in ZeWindow.cpp).
//
// Internally holds HWND / HINSTANCE / HICON, all locked inside Impl in the .cpp --
// this header contains no <windows.h> types; including it does not pollute the caller's macro environment.
// =====================================================================

#include "ZePlatformWindow.h"
#include <memory>

namespace Plat {

 /// @brief The drop target this window registers with the system (defined in ZeWindowWin32.cpp)
 /// @note The forward declaration here exists only for the friend line below -- it needs access to `Impl` (a private nested type).
	class Win32DropTarget;

	class Win32Window final : public IWindow {
	public:
		Win32Window();
		~Win32Window() override;

		Win32Window(const Win32Window&) = delete;
		Win32Window& operator=(const Win32Window&) = delete;

		bool Create(const WindowDesc& desc, IWindowHost& host) override;

  /// @brief Windows is a platform with "real movable windows" -> the full desktop capability set
  /// @note Uses the shared constant instead of copying the bit operations, so the two desktop backends do not each write their own and
  ///       miss one when a capability bit is added later (the symptom of a miss is "the capability query says no, but it actually works",
  ///       which is hard to diagnose).
  /// @note On top of this baseline, Win32 **additionally** delivers four (see the style branches in `Create`):
  ///       `PopupWindow` (`WS_POPUP` + `WS_EX_TOOLWINDOW` + `WS_EX_NOACTIVATE`),
  ///       `ClickThrough` (`WS_EX_LAYERED | WS_EX_TRANSPARENT`),
  ///       `FileDrop` (`IDropTarget`, with a fallback tier of `WM_DROPFILES` when OLE is unavailable),
  ///       `NativeFileDialog` (`IFileOpenDialog` / `IFileSaveDialog`).
  ///       All four are "real differences that can be queried": X11 has no corresponding implementation, so they cannot go into
  ///       the baseline shared by the two desktop backends.
		Cap Capabilities() const override {
			return kDesktopCapabilities | Cap::PopupWindow | Cap::ClickThrough
				| Cap::FileDrop | Cap::NativeFileDialog | Cap::PerPixelTransparent;
		}

		void Show() override;
		void Hide() override;
		void Destroy() override;

		bool PollEvent() override;
		void WaitEvents(int timeoutMs) override;
		int  ExitCode() const override;

		void GetClientSize(int& w, int& h) const override;
		void SetClientSize(int w, int h) override;

  /// @brief The work area (excluding the taskbar) of the monitor this window is on
  /// @note Takes the screen the **host** is on: a dependent window (notification popup) is still at (0,0) before placement,
  ///       so using its HWND with `MonitorFromWindow` would get the primary screen instead of the host's screen.
		bool GetScreenWorkArea(int& x, int& y, int& w, int& h) const override;
		void ClientOriginToScreen(int& x, int& y) const override;
  /// @brief Non-client area thickness (outer frame - client area): needed when centering by the outer frame
		void GetFrameInsets(int& left, int& top, int& right, int& bottom) const override;
		void SetCenterOnResize(bool on) override;
		void SetPosition(int x, int y) override;
		void SetPositionRelativeToHost(int x, int y) override;
		void CenterOnScreen() override;
  /// @brief Removes the OS-level owner, promoting a dependent window to an independent top-level window (used when a child window inherits its host)
  /// @note `Owned` / `Free` can be removed; `Child` (WS_CHILD) and `Popup` return false.
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
		void SetFullscreen(bool on) override;
		void SetBorderless(bool on) override;
		void SetBorderlessResizable(bool on) override;
		bool IsBorderlessResizable() const override;
		void SetRoundedCorners(bool on) override;
		bool IsRoundedCorners() const override;
		void ApplyTitleBar(bool titleBarVisible,
			bool minBtn, bool maxBtn, bool closeBtn) override;
		void SetIconFromFile(const std::string& utf8Path) override;
		void SetIconFromResource(int resId) override;
		void SetIconFromHandle(void* hIcon) override;
		void SetOpacity(float alpha) override;
  /// @brief Per-pixel transparency: removes the redirection surface (`WS_EX_NOREDIRECTIONBITMAP`)
  /// @note Only allowed after the backend confirms it goes through the composition swap chain -- see the IWindow interface documentation.
		void SetPerPixelTransparent(bool on) override;
		void SetClickThrough(bool on) override;
		void SetDarkTitleBar(bool dark) override;

  /// @brief Enables / disables the window (Win32 = `EnableWindow`) -- the mechanism behind modal dialogs
		void SetEnabled(bool on) override;

  // ---- System clipboard (CF_UNICODETEXT; see the implementation notes in ZeWindowWin32.cpp) ----
		std::string GetClipboardText() override;
		void        SetClipboardText(const std::string& utf8) override;

  /// @brief Changes the mouse cursor ("what to show" is decided by the widget layer; this only does the SetCursor step)
		void SetMouseCursor(Kit::Cursor c) override;

  // ---- File drag and drop / native file dialog (see the implementation notes in ZeWindowWin32.cpp) ----
  // @brief Accepts external file drops (full tier `IDropTarget`, fallback `WM_DROPFILES` when OLE is unavailable)
		void SetAcceptFileDrop(bool on) override;
  /// @brief System native file dialog (`IFileOpenDialog` / `IFileSaveDialog`)
		bool ShowFileDialog(const FileDialogDesc& desc, std::vector<std::string>& out) override;
  /// @brief Cross-thread wakeup of the message loop (`PostMessage` + `WM_APP`)
		void PostWake() override;

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

		void* NativeHandle() const override;

	private:
  /// @brief The drop target needs to read and write `Impl` (`m_host` / `m_hwnd` / `EmitFileEvent`)
		friend class Win32DropTarget;

		struct Impl;
		std::unique_ptr<Impl> m_impl;
	};

} // namespace Plat
