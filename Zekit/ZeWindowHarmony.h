#pragma once
// =====================================================================
//  ZeWindowHarmony.h -- adapter factory for the HarmonyOS platform
// =====================================================================
//
//  NOTE how thin this file is: only a few factory function declarations, not a single platform type.
//    It has the same shape as `ZeWindowAndroid.h` -- the HarmonyOS window layer entity is likewise an
//    `IMobileBackend` implementation (`ZeWindowHarmony.cpp`), and the shared layer `ZeMobileWindow`
//    is unchanged. This is what the design document section 1.4 calls "reuse the MobileWindow shared layer,
//    only write HarmonyMobileBackend".
//
//  NOTE this header is included by `ZeWindow.cpp` (the dispatch file), so it must NOT pull in
//    `<ace/xcomponent/native_interface_xcomponent.h>` or `<napi/native_api.h>`
//    -- those headers bring in thousands of macros and the whole `napi_*` family. Platform headers appear only in .cpp files
//    (same rule as ZeWindowWin32.h / ZeWindowX11.h / ZeWindowAndroid.h).
//
//  NOTE the structural difference from Android (only one, stated here so you do not have to look):
//    Android's host (`android_app*`) comes from the NDK's bundled `native_app_glue`,
//    which is an application-side .c file; it calls `android_main()`, and `ZEKIT_APP_ENTRY`
//    hooks in there.
//    HarmonyOS has no glue -- the whole flow is driven by the XComponent callback set, and they must be
//    registered by a NAPI module. Zekit folds this into the library (the .cpp for this file carries
//    its own NAPI self-registration), so:
//      - the application project needs not a single C++ file (only an ArkTS page + module.json5);
//      - the application side still only writes `ZEKIT_APP_ENTRY(name){ ... }`, and the macro under `__OHOS__`
//        expands to `ZekitHarmonyAppMain()`, which the library calls on a separate thread once the surface is ready.
// =====================================================================

#include "ZeMobileWindow.h"

namespace Plat {

 /// @brief Creates a HarmonyOS mobile adapter
 /// @return Never returns nullptr; ownership goes to `MobileWindow` (which deletes it)
 /// @note `SetHarmonyHost()` should already have been called. It can still be created without it --
 ///       only `QuerySurface()` keeps returning false (the XComponent surface
 ///       has not been delivered yet, equivalent to "OnSurfaceCreated has not arrived yet").
	IMobileBackend* CreateHarmonyMobileBackend();

 /// @brief Hands the HarmonyOS-side session handle to the adapter
 /// @param host Created and owned by the NAPI bootstrap code inside `ZeWindowHarmony.cpp`;
 ///             opaque to upper layers (this header does not know its type)
 /// @note Process-level is honest: one application process has only one XComponent host.
 ///       Called by the library's own NAPI bootstrap code; the application need not care.
	void SetHarmonyHost(void* host);

 /// @brief Current session handle (for the library's internal NAPI bootstrap code; not visible to the application)
	void* HarmonyHostHandle();

 /// @brief HarmonyOS system dark/light preference (reads the ColorMode pushed up from ArkTS)
 /// @note Plays the same role as Win32 reading the registry, X11 reading XSETTINGS, Android reading
 ///       `Configuration.uiMode`
 ///       (see the description of `Plat::SystemPrefersDark` in ZePlatformWindow.h).
	bool SystemPrefersDark();

 /// @brief Physical pixel size of the current XComponent surface (the render layer's unit)
 /// @param widthPx,heightPx Output: physical pixels (what the swap chain / viewport needs)
 /// @return false = no usable surface at this moment (OnSurfaceCreated has not arrived yet,
 ///         or OnSurfaceDestroyed has already happened)
 ///
 /// @details
 /// Why this is provided by the Plat layer instead of letting the render layer ask `OHNativeWindow` itself:
 /// the logical pixel <-> physical pixel conversion depends on density, and the authoritative source of density is ArkTS
 /// (`display.getDefaultDisplaySync().densityPixels`), which only the Plat layer has. If the render layer called
 /// `GET_BUFFER_GEOMETRY` itself, it would have to solve the inconsistent output parameter order problem
 /// (see the note in ZeWindowHarmony.cpp) all over again --
 /// that knowledge should exist in only one place.
 /// @note This is a different trade-off from `ZeRenderBackendAndroid.cpp` calling
 ///       `ANativeWindow_getWidth` directly; the reason is that HarmonyOS has no
 ///       equivalent call that carries its own physical size.
	bool HarmonySurfaceSize(int& widthPx, int& heightPx);

} // namespace Plat
