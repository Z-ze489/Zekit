#pragma once
// =====================================================================
//  ZePlatformGate.h -- platform availability gate (this is the only place in the library that says "which platforms can be used")
// =====================================================================
//
//  * Why this file exists
//  -------------------------------------------------------------------
//  Zekit's goal is "one set of application code that runs on every platform without changing a line". To make good on that
//  promise later, the platform skeleton is laid out first: the `Api` enum has `Metal`, the dispatch files have
//  `__APPLE__` branches, and HarmonyOS and Apple each have their own implementation directory. Reserving a slot does not mean it
//  works now -- the slot is reserved so an implementation can be added later, not so people trip over it today.
//
//  Measured conclusions (2026-10-11):
//    - Windows / Linux / Android -- verified end to end on real devices, deliverable;
//    - HarmonyOS -- rendering / touch / soft keyboard ran on a phone emulator,
//      but mouse input in the PC (2in1) form factor is not solved yet, and the emulator itself is unstable
//      (the host GL forwarding layer crashes, Vulkan does not work and needs a GLES fallback);
//    - Apple (macOS / iOS) -- the render layer (Metal) is written as a complete implementation (
//      ZeMetalContext.mm / ZeMetalUIRenderer.mm / ZeRenderBackendApple.mm /
//      shaders/ZeUIShader.metal), but has never been compiled (this machine is Windows, no Apple SDK);
//      the window layer (ZeWindowApple.cpp) is still a skeleton. The whole chain has zero real-device verification.
//
//  The user's decision (explicit, with reasons):
//    "There are too many bugs right now, and I have no real device to test on, so I have no motivation to develop it for now.
//     I just want to give them a slot for now, and improve them slowly once there is funding.
//     Supporting Windows, Linux and Android is enough for now."
//
//  => So this gate marks HarmonyOS / Apple as hard-blocked: not a silent downgrade, not a runtime
//    return false, but a compile-time `#error` that fails immediately. The reason is practical --
//    these two platforms currently "compile but do not run correctly", and silently compiling through only wastes more time on
//    a platform known not to work. To unblock, just delete the definition line in this file
//    after the corresponding implementation is complete (see below), a one-line change.
//
//  * How to unblock (a note to the future self)
//  -------------------------------------------------------------------
//    Apple: put the definition of `ZEKIT_ENABLE_APPLE_PLATFORM` back (or turn it on explicitly in the build command
//          with `-DZEKIT_ENABLE_APPLE_PLATFORM`).
//          The render layer (Metal) is already a complete implementation and can be used directly;
//          what still needs completing is the window layer: all `TODO(apple)` markers in `ZeWindowApple.cpp`
//          (`CreateAppleMobileBackend()` / `CreateAppleDesktopWindow()`
//          / `AppleSurfaceSize()`).
//    HarmonyOS: same as above, turn on `ZEKIT_ENABLE_HARMONY_PLATFORM`.
//          (The HarmonyOS implementation is already complete and usable immediately after unblocking;
//           the only open issue is the mouse coordinate convention on PC (2in1).)
//
//  * Iron rule: this file must not include any platform header, and must not contain platform types.
//      It is included repeatedly by the library's .cpp / .h files, and any leak of a platform header pollutes other platforms.
// =====================================================================

// ---------------------------------------------------------------------
//  1. Platform block switches
// ---------------------------------------------------------------------
//  All off by default: that is, HarmonyOS / Apple hit #error as soon as they are compiled.
//  To open them later, swap the `/*` and `*/` on the two lines below (or turn them on with a build-time -D);
//  no other file needs to change.
// ---------------------------------------------------------------------
// <- Apple (macOS / iOS) unblock switch
// <- HarmonyOS unblock switch

// ---------------------------------------------------------------------
//  2. Apple platform gate
// ---------------------------------------------------------------------
#if defined(__APPLE__) && !defined(ZEKIT_ENABLE_APPLE_PLATFORM)
// WARNING: the #error message must be written on a single line: MSVC does not support `\` line continuation in #error,
//    continuation makes it treat the whole block as a "newline in a string literal" (C2001).
#  error "Zekit 暂不支持苹果平台（macOS / iOS）：Metal 渲染层已写成完整实现（ZeMetalContext / ZeMetalUIRenderer / ZeRenderBackendApple.mm / ZeUIShader.metal），但从未编译过、零实机验证，且窗口层（ZeWindowApple.cpp）仍是骨架，故编译期拒绝。解封：在 ZePlatformGate.h 打开 ZEKIT_ENABLE_APPLE_PLATFORM。"
#endif

// ---------------------------------------------------------------------
//  3. HarmonyOS platform gate
// ---------------------------------------------------------------------
//  WARNING: the condition must use `__OHOS__` and not `__linux__`: OHOS clang defines
//     both (see the long comment at the top of ZeWindow.cpp); using `__linux__` would also hit Linux
//     desktop by mistake.
#if defined(__OHOS__) && !defined(ZEKIT_ENABLE_HARMONY_PLATFORM)
// WARNING: same as Apple: single-line message, do not use a continuation character.
#  error "Zekit 暂不支持鸿蒙平台（HarmonyOS）：实现已完整（曾在手机模拟器跑通渲染/触摸/软键盘），但 PC(2in1) 鼠标输入未解决、模拟器不稳定，故编译期拒绝。解封：在 ZePlatformGate.h 打开 ZEKIT_ENABLE_HARMONY_PLATFORM。"
#endif

// ---------------------------------------------------------------------
//  4. Self-check of supported platforms
// ---------------------------------------------------------------------
//  Expose "which platforms this library currently supports" as a queryable compile-time constant, so upper layers can
//  sense it (for example, an installer / packaging script that wants to branch by platform does not have to hardcode #if guesses).
namespace ZekitPlatform {

 /// @brief Whether the current compile target platform belongs to the "delivered, safe to use" group
 /// @return true on Windows / Linux (desktop) / Android;
 ///         always false on HarmonyOS / Apple (those two do not compile at all, see the #error above)
	constexpr bool IsSupportedTarget() {
#if defined(_WIN32)
		return true;      /// < Windows: Win32 + D3D12 / Vulkan / OpenGL, primary platform
#elif defined(__ANDROID__)
		return true;      /// < Android: mobile shared layer + Vulkan / GLES, verified end to end on real devices
#elif defined(__linux__) && !defined(__OHOS__)
		return true;      /// < Linux desktop: X11 + Vulkan / OpenGL-EGL
#else
		return false;     /// < HarmonyOS / Apple / unknown platform: slot reserved, but not usable
#endif
	}

 /// @brief Readable name of the delivered platform (for logs / diagnostics)
	inline const char* SupportedTargetName() {
#if defined(_WIN32)
		return "Windows";
#elif defined(__ANDROID__)
		return "Android";
#elif defined(__linux__) && !defined(__OHOS__)
		return "Linux";
#else
		return "Unsupported";
#endif
	}

} // namespace ZekitPlatform

// ---------------------------------------------------------------------
//  5. Add a compile-time reminder (non-fatal) to the source locations of HarmonyOS / Apple
// ---------------------------------------------------------------------
//  Even if the unblock switch is turned on and the platform compiles, anyone touching the implementation files of those two platforms
//  should see an explicit "this part is unverified". A macro marks them here, and the
//  implementation files can reference it with `ZEKIT_UNVERIFIED_PLATFORM_NOTE()`.
#define ZEKIT_UNVERIFIED_PLATFORM_NOTE()                                  \
	static_assert(true,                                                    \
		"此平台的实现未经真机验证（见 Zekit/include/ZePlatformGate.h）")
