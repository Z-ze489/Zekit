#pragma once
// =====================================================================
//  ZeWindowAndroid.h -- Android platform **adapter** factory
// =====================================================================
//
//  NOTE how thin this file is: **only three factory function declarations, not a single platform type**.
//    The Android window layer entity is `ZeMobileWindow` (platform-independent, shared by Android and iOS),
//    this file only handles "wiring the Android native data onto the shared layer".
//    When adding iOS, write a corresponding `ZeWindowIOS.h` + an adapter implementation, the shared layer stays untouched.
//
//  NOTE This header is included by `ZeWindow.cpp` (the dispatch file), so it **must not** pull in
//    `<android/*.h>` -- those headers bring in thousands of macros such as `ALOG_*`, `AKEYCODE_*`.
//    Platform headers always appear only in .cpp files (same rule as ZeWindowWin32.h / ZeWindowX11.h).
// =====================================================================

#include "ZeMobileWindow.h"

namespace Plat {

 /// @brief Creates an Android mobile adapter
 /// @return Never returns nullptr; ownership goes to `MobileWindow` (which will delete it)
 /// @note `SetAndroidHost()` should already have been called before this (see `Plat::AttachPlatformHost`).
 ///       It can still be created without that -- only `QuerySurface()` will keep returning false
 ///       (the window has not been delivered by the system yet), which is the same state as "the app just started, INIT_WINDOW has not arrived yet".
	IMobileBackend* CreateAndroidMobileBackend();

 /// @brief Hands the `android_app*` to the Android adapter (process-level)
 /// @param androidApp The pointer received by `android_main()`
 /// @note Forwarded by `Plat::AttachPlatformHost()`; the host does not need to call it directly.
 /// @note Process-level is honest: a NativeActivity process has only one `android_app`.
	void SetAndroidHost(void* androidApp);

 /// @brief Android system dark preference (reads `AConfiguration_getUiModeNight`)
 /// @note Plays the same role as reading the registry on Win32 and XSETTINGS on X11
 ///       (see the description of `Plat::SystemPrefersDark` in ZePlatformWindow.h).
	bool SystemPrefersDark();

} // namespace Plat
