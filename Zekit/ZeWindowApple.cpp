
#include "pch.h"

#if defined(__APPLE__)

#include "ZeWindowApple.h"
#include "ZePlatformWindow.h"
#include "ZeMobileWindow.h"

#include <cstdio>

namespace Plat {

	namespace {

		[[maybe_unused]] void* g_appleSession = nullptr;

	}

	bool SystemPrefersDark() {
		return false;
	}

	IMobileBackend* CreateAppleMobileBackend() {
		return nullptr;
	}

	void SetAppleHost(void* host) {
		g_appleSession = host;
	}

	void* AppleHostHandle() { return g_appleSession; }

	IWindow* CreateAppleDesktopWindow() {
		return nullptr;
	}

	bool AppleSurfaceSize(int& widthPx, int& heightPx) {
		widthPx = heightPx = 0;
		return false;
	}

}

#endif
