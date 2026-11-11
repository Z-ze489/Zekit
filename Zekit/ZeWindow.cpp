
#include "pch.h"

#include "ZePlatformWindow.h"

#if defined(_WIN32)
#  include "ZeWindowWin32.h"
#elif defined(__ANDROID__)
#  include "ZeWindowAndroid.h"
#  include "ZeMobileWindow.h"
#elif defined(__OHOS__)
#  include "ZeWindowHarmony.h"
#  include "ZeMobileWindow.h"
#elif defined(__APPLE__)
#  include "ZeWindowApple.h"
#  include "ZeMobileWindow.h"
#elif defined(__linux__)
#  include "ZeWindowX11.h"
#endif

namespace Plat {

	IWindow* CreateNativeWindow() {
#if defined(_WIN32)
		return new Win32Window();
#elif defined(__ANDROID__)
		return new MobileWindow(CreateAndroidMobileBackend());
#elif defined(__OHOS__)
		return new MobileWindow(CreateHarmonyMobileBackend());
#elif defined(__APPLE__)
#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
		return new MobileWindow(CreateAppleMobileBackend());
#else
		return CreateAppleDesktopWindow();
#endif
#elif defined(__linux__)
		return new X11Window();
#else
		namespace {
			class UnsupportedWindow final : public IWindow {
			public:
				Cap Capabilities() const override { return Cap::None; }
				bool Create(const WindowDesc&, IWindowHost&) override {
					m_closed = true;
					return false;
				}
				void Show() override {}
				void Hide() override {}
				void Destroy() override {}
				bool PollEvent() override { return false; }
				void WaitEvents(int) override {}
				int  ExitCode() const override { return m_exitCode; }
				void GetClientSize(int& w, int& h) const override { w = 0; h = 0; }
				void SetClientSize(int, int) override {}
				void SetPosition(int, int) override {}
				void CenterOnScreen() override {}
				float ScaleFactor() const override { return 1.0f; }
				int   Dpi() const override { return 96; }
				void Minimize() override {}
				void Maximize() override {}
				void Restore() override {}
				bool IsMinimized() const override { return false; }
				bool IsMaximized() const override { return false; }
				void SetTopmost(bool) override {}
				void SetResizable(bool) override {}
				void SetFullscreen(bool) override {}
				void SetBorderless(bool) override {}
				void ApplyTitleBar(bool, bool, bool, bool) override {}
				void SetIconFromFile(const std::string&) override {}
				void SetIconFromResource(int) override {}
				void SetIconFromHandle(void*) override {}
				void SetOpacity(float) override {}
				void SetDarkTitleBar(bool) override {}
				void SetTitle(const std::string&) override {}
				bool IsClosed() const override { return m_closed; }
				bool IsActive() const override { return false; }
				void RequestRedraw() override {}
				void RequestWakeup(int) override {}
				void CancelWakeup() override {}
				void SetTimerPrecision(bool) override {}
				void PostClose() override {}
				void RequestQuit(int exitCode) override { m_exitCode = exitCode; }
				void StartWindowDrag() override {}
				bool IsMouseButtonDown(MouseButton) const override { return false; }
				void* NativeHandle() const override { return nullptr; }
			private:
				bool m_closed = true;
				int  m_exitCode = 0;
			};
		}
		return new UnsupportedWindow();
#endif
	}

	void AttachPlatformHost(void* host) {
#if defined(__ANDROID__)
		SetAndroidHost(host);
#elif defined(__OHOS__)
		SetHarmonyHost(host);
#elif defined(__APPLE__)
		SetAppleHost(host);
#else
		(void)host;
#endif
	}

#if !defined(_WIN32) && !defined(__linux__) && !defined(__ANDROID__) \
 && !defined(__OHOS__) && !defined(__APPLE__)
	bool SystemPrefersDark() { return false; }
#endif

#if !defined(_WIN32) && !(defined(__linux__) && !defined(__OHOS__))
	bool QueryPrimaryDisplay(DisplayGeometry& out) {
		(void)out;
		return false;
	}

	int QueryDisplays(DisplayGeometry* out, int maxCount) {
		(void)out; (void)maxCount;
		return 0;
	}
#endif

}
