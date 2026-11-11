#pragma once
// =====================================================================
//  ZeWindowApple.h -- Apple (macOS / iOS) platform adapter factory
// =====================================================================
//
//  ** Status: location reserved, implementation unverified, currently blocked at compile time **
//  -------------------------------------------------------------------
//  This header and `ZeWindowApple.cpp` are the "Apple platform location"; the shape is
//  **deliberately symmetric with `ZeWindowHarmony.h` / `ZeWindowHarmony.cpp`** -- when devices and
//  funding are available, complete it following the already-working Harmony path; no redesign needed.
//
//  But **do not use it now**: `ZePlatformGate.h` will
//  directly `#error` when compiling for `__APPLE__` (not a silent fallback, not a runtime error --
//  it does not compile).
//  For the reason and how to unblock, see the long comment at the top of Zekit/include/ZePlatformGate.h.
//
//  * How thin this file is: **same shape** as `ZeWindowHarmony.h` / `ZeWindowAndroid.h`
//    -- only a few factory function declarations, **no platform types at all** (no NSWindow*,
//    no CAMetalLayer*, no UIView*).
//    Reason: the Apple window layer entity is likewise "`IMobileBackend` implementation + shared layer
//    `ZeMobileWindow`", with `ZeMobileWindow.cpp` unchanged. This is exactly what plan section 1.4
//    calls "reuse the MobileWindow shared layer, only write AppleMobileBackend".
//
//  * This header is included by `ZeWindow.cpp` (the dispatch file), so it **must not** pull in
//    `<Cocoa/Cocoa.h>` / `<UIKit/UIKit.h>` / `<Metal/Metal.h>` --
//    those headers bring in thousands of macros (`check`, `min`, `max` are all redefined in Cocoa)
//    and Objective-C runtime declarations. Platform headers always appear only in .cpp files
//    (same rule as ZeWindowWin32.h / ZeWindowX11.h / ZeWindowAndroid.h / ZeWindowHarmony.h).
//
//  * **Structural differences** from Harmony (stated clearly so no rethinking is needed later)
//  -------------------------------------------------------------------
//    - Harmony: the flow is driven by the XComponent callback group, which must be registered by the NAPI module.
//      Zekit **folds this bootstrap into the library** (`ZeWindowHarmony.cpp` self-registers NAPI),
//      so the application project needs no C++ file at all. Entry symbol `ZekitHarmonyAppMain()`.
//    - Apple **mobile (iOS)**: the flow is driven by the `UIApplicationDelegate` callback
//      group -- and it must be held by the **application side** (Objective-C/Swift); the library cannot self-register.
//      This is isomorphic to Android's `native_app_glue`: the **application side** connects the host,
//      calls `Kit::AttachPlatformHost(...)`, and Zekit provides
//      `CreateAppleMobileBackend()`.
//    - Apple **desktop (macOS)**: not "the system hands you a window" but "you create the
//      window yourself" (NSWindow) -- same side as Win32 / X11. So the macOS window layer should be
//      a **real `IWindow` implementation** (`AppleWindow`, NSWindow + NSView +
//      CAMetalLayer), not a MobileWindow adapter. See `CreateAppleDesktopWindow()` below.
//
//    ---------------------------------------------------------------
//    In one sentence: **iOS uses the mobile shared layer, macOS uses an independent IWindow.**
//    (Fully isomorphic to "Android uses the mobile shared layer, Windows/Linux use an independent IWindow".)
//    ---------------------------------------------------------------
// =====================================================================

#include "ZeMobileWindow.h"

namespace Plat {

 // -----------------------------------------------------------------
 //  1. iOS (mobile) -- isomorphic to Android / Harmony
 // -----------------------------------------------------------------

 /// @brief Creates an Apple mobile (iOS) mobile adapter
 /// @return Never returns nullptr; ownership goes to `MobileWindow` (which deletes it)
 ///
 /// @note `SetAppleHost()` should already have been called (the application-side
 ///       `UIApplicationDelegate` callback passes in the `UIWindow` / `CAMetalLayer`).
 ///       It can be created without that -- only `QuerySurface()` will keep
 ///       returning false (equivalent to "the view's layer is not ready yet").
 ///
 /// @warning **Not implemented yet**: this function is a skeleton in `ZeWindowApple.cpp`,
 ///           with only a `TODO(apple)` marker and a note on "how to complete it following Harmony".
 ///           The entire Apple platform is blocked at compile time by `ZePlatformGate.h`; compilation never reaches here.
	IMobileBackend* CreateAppleMobileBackend();

 /// @brief Hands the Apple mobile host handle to the adapter
 /// @param host Held by the application side (`UIApplicationDelegate`); **opaque** to upper layers.
 ///             Typically `UIWindow*` (converted with `__bridge void*`);
 ///             the adapter extracts the `CAMetalLayer` from it internally.
 /// @note Process-level: an iOS application process usually has only one key window.
 ///       Same role as Android's `SetAndroidHost(android_app*)`.
	void SetAppleHost(void* host);

 /// @brief Current session handle (for library-internal bootstrap code; not visible to the application)
	void* AppleHostHandle();

 // -----------------------------------------------------------------
 //  2. macOS (desktop) -- same side as Win32 / X11
 // -----------------------------------------------------------------

 /// @brief Creates a macOS desktop window (NSWindow + NSView + CAMetalLayer)
 /// @return Never returns nullptr
 ///
 /// @note macOS is on the same side as Windows / Linux: "the window is created by the library itself".
 ///       So this returns a **real `IWindow`**, not an `IMobileBackend`.
 ///       `CreateNativeWindow()` in `ZeWindow.cpp` should return it when `__APPLE__`
 ///       and not iOS.
 ///
 /// @warning **Not implemented yet**: skeleton only. See `ZeWindowApple.cpp`.
	IWindow* CreateAppleDesktopWindow();

 // -----------------------------------------------------------------
 //  3. Shared: system dark/light preference
 // -----------------------------------------------------------------

 /// @brief Apple's system dark/light preference (reads `NSApp.effectiveAppearance` / iOS's
 ///        `UITraitCollection.userInterfaceStyle`)
 /// @note Same role as Win32 reading the registry, X11 reading XSETTINGS, Android reading
 ///       `Configuration.uiMode`, Harmony reading ArkTS ColorMode
 ///       (see the description of `Plat::SystemPrefersDark` in ZePlatformWindow.h).
 ///       On macOS, `SetDarkTitleBar` should also adapt semantically to this appearance.
	bool SystemPrefersDark();

 // -----------------------------------------------------------------
 //  4. Shared: **physical pixel** size of the current surface
 // -----------------------------------------------------------------

 /// @brief **Physical pixel** size of the current drawable (CAMetalLayer / NSView)
 /// @param widthPx,heightPx Output: physical pixels (what Metal's drawableSize wants)
 /// @return false = no usable surface at this moment
 ///
 /// @details
 /// Same contract as `HarmonySurfaceSize` / `AndroidSurfaceSize`:
 /// **the Plat layer is the sole source of "physical pixels"**.
 /// On Apple the conversion is direct (Retina displays):
 ///     `drawableSize = bounds.size x contentsScale`
 /// where `contentsScale` = `NSScreen.backingScaleFactor` (2.0 / 3.0) --
 /// this is Apple's "density", the same concept as Android's `densityDpi/160` and Harmony's
 /// `densityPixels`.
 /// @note Good news: Apple **does not need** Harmony's fallback of "determining at runtime whether it is px or vp" --
 ///       Metal's `drawableSize` is the authoritative physical pixel value; documentation and implementation agree.
	bool AppleSurfaceSize(int& widthPx, int& heightPx);

} // namespace Plat
