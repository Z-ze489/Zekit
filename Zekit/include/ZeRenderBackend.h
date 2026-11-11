/**
 * @file ZeRenderBackend.h
 * @brief Render backend selection (public part of the `Rhi` namespace)
 *
 * @details
 * This file contains only the one thing consumers **need**: the `Rhi::Api` enum --
 * it is the parameter of `Kit::Window::UseRenderer()`.
 *
 * ## Enum values are limited per platform at compile time
 *
 * Values differ per platform, and **values that do not exist are simply not in the enum** --
 *
 * | Platform | Available values |
 * |---|---|
 * | Windows | `OpenGL` / `Vulkan` / `D3D12` / `Software` |
 * | macOS / iOS | `Metal` / `OpenGL` / `Software` |
 * | Linux | `OpenGL` / `Vulkan` / `Software` |
 * | Android | `OpenGL` / `Vulkan` / `Software` |
 * | HarmonyOS | `Vulkan` / `OpenGL` (GLES, via EGL) / `Software` |
 * | All platforms | `External` (renderer provided by you) |
 *
 * So writing `Rhi::Api::Metal` on Windows is a **compile error**, not
 * "compiles, and only at runtime do you find out it is unsupported". Choosing a backend
 * this platform does not have is rejected at compile time.
 *
 * ## Usage
 *
 * @code
 * Kit::Window win;
 * win.UseRenderer(Rhi::Api::OpenGL);   // each window chooses its own, independent of others
 * win.SetTitle("Zekit");
 * win.Show();
 * win.exec();
 * @endcode
 *
 * Without calling `UseRenderer`, the default is `Rhi::DefaultApi()` -- D3D12 on Windows,
 * Metal on macOS, Vulkan on Linux / Android / HarmonyOS. On initialization failure it falls
 * back to OpenGL automatically, and **if OpenGL also fails it falls back to `Software`**
 * (needs no graphics driver, see the note below).
 *
 * ## `Software`: a way out for "machines without a renderer"
 *
 * Every GPU backend has a precondition **the user cannot fix by installing libraries**:
 * the machine must have a working graphics driver. On servers / cloud desktops / remote
 * sessions / containers / old machines with a broken driver, D3D12, Vulkan and OpenGL
 * fail to initialize **together** -- the window then pops up, the message loop keeps
 * running, and there is not a single pixel inside; the user will only conclude that
 * "this library is broken".
 *
 * `Software` covers that last case: pixels are computed on the CPU (`Rhi::SoftRasterizer`),
 * then handed to the window system for display (X11 uses `XPutImage`, Windows uses GDI's
 * DIB + `BitBlt`). Slower (milliseconds, not microseconds), but **visible, clickable,
 * usable**:
 *
 * | Item | Software backend |
 * |---|---|
 * | Required driver | **None at all** |
 * | Visual effect | Aligned with GPU item by item (rounded-corner SDF / 1px transition / premultiplied / bilinear glyphs), see `ZeCanvasShaders.h` |
 * | Per-pixel transparency | Only X11 32-bit ARGB windows; on Windows it honestly reports "unsupported" |
 * | Suitable for | Fallback / no-GPU environments / diagnosing "is it a GPU backend problem" |
 *
 * ## Performance: slow, but not too slow to use (measured numbers)
 *
 * Single-core per-pixel, measured (RaptorLake mobile CPU, O2):
 *
 * | Scenario | Per frame |
 * |---|---|
 * | 900x600 tool window, 4 cards + text | ~ 15 ms |
 * | 1440x900 regular window, 7 cards + text | ~ 41 ms |
 * | 1920x1080 fullscreen, 10 cards + text | ~ 73 ms |
 *
 * Two things to note when reading this table:
 *
 * 1. **This is pure CPU cost, excluding presentation** (X11's `XPutImage` adds a few more ms);
 * 2. **The default drive mode is `EventDriven` (paint only when there is work)**, so a static
 *    UI costs **zero** -- therefore, for "the window just sits there", the software backend
 *    consumes no more CPU than a GPU backend; only continuous animation (scrolling, dragging,
 *    `Immediate` mode) actually feels laggy.
 *
 * To force smoothness on a no-GPU machine, make the UI small (`SetFrameRate(30)` or simply
 * `EventDriven`), rather than expecting software rasterization to hit 60fps.
 *
 * It is **not** selected as the default automatically (the default is still the platform's
 * recommended GPU backend); it only comes into play when you explicitly call
 * `UseRenderer(Rhi::Api::Software)`, or when all previous backends fail to initialize.
 * To force software rendering for diagnosis, specify it explicitly -- that is the fastest
 * bisection.
 *
 *
 * @note **HarmonyOS defaults to Vulkan**: HarmonyOS does not have the GLES "each vendor
 *       does its own thing" problem, and `VK_OHOS_surface` has the same shape as Android's
 *       `VK_KHR_android_surface`; the render surface is given directly by `OHNativeWindow`
 *       (see the `vkCreateSurfaceOHOS` branch in ZeVulkanContext.cpp). The GLES/EGL path is
 *       kept only as a fallback -- exactly isomorphic to the Android trade-off (see the file
 *       header of ZeRenderBackendHarmony.cpp).
 *
 * ## Which GPU it runs on: `Rhi::Gpu`
 *
 * The render API and "which GPU" are **two independent dimensions**. On dual-GPU machines
 * (integrated + discrete), looking at the API alone is not enough -- the same Vulkan can
 * land on either the integrated or the discrete GPU, and the driver **does not guarantee**
 * which one. Measured (RaptorLake-S integrated + RTX 4060 Laptop):
 *
 * - `vkEnumeratePhysicalDevices` enumerates in the order `[0]=Intel integrated, [1]=NVIDIA discrete`,
 *   so "take the first usable one" reliably hits the integrated GPU;
 * - Mesa's GL/EGL likewise defaults to the integrated GPU.
 *
 * Therefore this library makes the device policy **explicit**: the default `Rhi::Gpu::Auto`
 * = **discrete GPU preferred**, falling back to the integrated GPU only on machines without
 * one (see the three values of `Rhi::Gpu`).
 * To find out which GPU it finally landed on, query `Kit::Window::deviceInfo()`.
 *
 * @note The backend interface (`Rhi::IBackend`) and the factory are internal implementation
 *       of the library and are not in this file -- consumers use the renderer only through
 *       `Kit::Window` and never touch backend objects directly.
 *
 * @see Window::UseRenderer / Window::UseGpu / rendererApi / deviceInfo in Zekit.h
 */

#pragma once

namespace Rhi {

    // ===================================================================
    //  Selectable renderer APIs (limited per platform at compile time)
    // ===================================================================

    /**
     * @brief Render backend API
     *
     * @note Enum members are defined with conditional compilation -- values this platform does
     *       not have do not appear in the enum, and writing them is a compile error (see the
     *       file header).
     */
    enum class Api {
#if defined(_WIN32)
        OpenGL,     /// < WGL 4.6 Core. Present on all Windows machines, usable as a fallback.
        Vulkan,     /// < Requires driver support for vulkan-1.dll; on initialization failure falls back to OpenGL automatically.
        D3D12,      /// < Default backend. Requires Windows 10 1903+ and d3d12.dll; on failure falls back to OpenGL automatically.
#elif defined(__APPLE__)
        // * Apple platforms: **placeholder in place, implementation unverified, currently blocked at compile time**.
        //   The enum is written as if Metal will be used in the future, but the whole __APPLE__ block
        //   #errors at ZePlatformGate.h -- compilation never reaches here.
        //   The Metal render layer **is a complete implementation** (ZeMetalContext.mm /
        //   ZeMetalUIRenderer.mm / ZeRenderBackendApple.mm / shaders/ZeUIShader.metal),
        //   but it has never been compiled and has zero on-device verification; the window layer
        //   (ZeWindowApple.cpp) is still a skeleton.
        //   See Zekit/include/ZePlatformGate.h (the block switch and how to unblock).
        Metal,      /// < Apple native graphics API. First choice on macOS / iOS (the only path with a future).
        OpenGL,     /// < Compatibility layer (stops at 4.1 on macOS, and already marked deprecated by Apple).
#elif defined(__ANDROID__)
        OpenGL,     /// < GLES (actually EGL + GLES 3.x).
        Vulkan,     /// < Supported on some devices with Android 7.0+.
#elif defined(__OHOS__)
        // WARNING: The test must come **before** `__linux__`: OHOS's clang defines both
        //   `__linux__` and `__OHOS__` (see the note in ZeWindow.cpp);
        //   writing `#elif defined(__linux__)` means the HarmonyOS branch is never reached.
        //   The value order matches Android / Linux (OpenGL first);
        //   "default to Vulkan" is expressed by `DefaultApi()`, not by enum order.
        OpenGL,     /// < Fallback: EGL + GLES 3 (`libEGL.so` / `libGLESv3.so` are both in the NDK sysroot).
        Vulkan,     /// < **Default backend**. `VK_OHOS_surface` is provided uniformly by the system loader,
                    /// < making it the most stable render path on HarmonyOS.
#elif defined(__linux__)
        OpenGL,     ///< GLX / EGL。
        Vulkan,     /// < Requires driver support.
#else
        OpenGL,     /// < Unknown platform: keep only one as a minimum so the library compiles.
#endif
        // * `External` is placed **last** and is not inside any `#if` branch -- both are deliberate:
        //   (1) it is not "some graphics API" but "renderer provided by the caller" (`Kit::ICanvasRenderer`,
        //       see ZeCanvasRenderer.h), so every platform has it;
        //   (2) placed last -> the **numeric values** of existing members are unchanged, so adding this
        //       value does not silently change defaults / log conventions elsewhere (enum values are
        //       visible to downstream).
        //   WARNING: It does **not** enter `Rhi::CreateBackend` dispatch: passing it to the factory only
        //      yields nullptr. The entry point is `Window::UseExternalRenderer`.
        External,   /// < External renderer (adapted by the developer; `UseRenderer` does not accept it)

        // * `Software` is likewise not inside any `#if` branch, for the same reasons as `External`:
        //   (1) software rasterization depends on no graphics API, so **every platform should have it**
        //       (that is what "fallback" means);
        //   (2) appended last -> none of the numeric values of the members above changed.
        //   WARNING: Difference from `External`: it **does** enter the factory (`Rhi::CreateSoftwareBackend`),
        //      and `UseRenderer(Api::Software)` is a legal call.
        //   WARNING: It is also the **end** of the `Kit` fallback chain (the last path when all GPU
        //      backends are dead), see ZeRenderBackendSoft.cpp and ensureBackend in Zekit.cpp.
        Software,   /// < Software rasterization (CPU drawing, needs no graphics driver; slowest but always present)
    };

    /**
     * @brief Whether this platform supports a given renderer API
     * @param api the API to query
     * @return always true within the current platform's value range (enum members are already trimmed per platform)
     *
     * @note The value of this function is that it writes "whether supported" as a **queryable
     *       compile-time constant**, giving upper layers an explicit basis for `if constexpr`
     *       branches -- rather than guessing with hard-coded `#if`.
     */
    [[maybe_unused]] constexpr bool IsApiSupported(Api api) {
        switch (api) {
        case Api::External: return true;   // Platform-independent: whether it works depends on whether you implement it correctly
        // Platform-independent, and **always true**: its only precondition is "the window can receive a
        // block of pixels", and every platform in the table above has a presentation path (platforms
        // without an implementation fail honestly in Init, see the third branch of ZeSoftSurface.cpp).
        case Api::Software: return true;
#if defined(_WIN32)
        case Api::OpenGL:
        case Api::Vulkan:
        case Api::D3D12:  return true;
#elif defined(__APPLE__)
        case Api::Metal:
        case Api::OpenGL: return true;
#elif defined(__ANDROID__) || defined(__linux__) || defined(__OHOS__)
        case Api::OpenGL:
        case Api::Vulkan: return true;
#else
        case Api::OpenGL: return true;
#endif
        }
        return false;
    }

    /**
     * @brief Readable name of the API (for logs / error messages)
     * @param api the API to query
     * @return static string literal, no lifetime management needed; unknown values return "Unknown"
     */
    inline const char* ApiName(Api api) {
        switch (api) {
        case Api::External: return "External";
        case Api::Software: return "Software";
        case Api::OpenGL: return "OpenGL";
#if defined(_WIN32) || defined(__linux__) || defined(__ANDROID__) || defined(__OHOS__)
        case Api::Vulkan: return "Vulkan";
#endif
#if defined(_WIN32)
        case Api::D3D12:  return "D3D12";
#endif
#if defined(__APPLE__)
        case Api::Metal:  return "Metal";
#endif
        }
        return "Unknown";
    }

    /**
     * @brief Recommended default render backend for this platform (compile-time constant)
     * @return the most suitable `Api` value on the current platform, **guaranteed to be a member that really exists in this platform's enum**
     *
     * @note Uses `constexpr` rather than a macro: type-safe, completable in the IDE,
     *       usable in overloads and `static_assert`, and does not pollute the global namespace.
     *
     * @note This **never** returns `Api::Software`: software rasterization is a **fallback**, not a
     *       default. The default answers "which graphics API should this machine normally use";
     *       software rendering answers "what to do when all of those fail to start". To force it,
     *       call `UseRenderer(Api::Software)` explicitly.
     *
     * @par Example
     * @code
     * win.UseRenderer(Rhi::DefaultApi());   // let the library pick per platform, do not hard-code
     * @endcode
     */
    constexpr Api DefaultApi() {
#if defined(_WIN32)
        return Api::D3D12;    /// < Windows: first choice. Requires Win10 1903+, falls back to OpenGL automatically on initialization failure
#elif defined(__APPLE__)
        // * Placeholder fixed: Apple defaults to Metal (the user's explicit requirement).
        //   But __APPLE__ is currently blocked at compile time (see ZePlatformGate.h).
        //   The Metal render layer is a complete implementation (not compile-verified); **this value
        //   takes effect once unblocked**.
        return Api::Metal;    /// < macOS / iOS: Apple native graphics API
#else
        return Api::Vulkan;   /// < Linux / Android / HarmonyOS: prefer Vulkan when the driver supports it
#endif
    }

    // ===================================================================
    //  Render device (GPU) selection
    // ===================================================================

    /**
     * @brief Which GPU to render with
     *
     * @details
     * Only meaningful on **multi-GPU machines**. On single-GPU machines all three values give
     * the same result (all are that one card) -- so code can just write `UseGpu(Gpu::Auto)`.
     *
     * Determining "discrete/integrated" uses the device type each API reports, **without guessing
     * from device names**:
     *   - Vulkan -- `VkPhysicalDeviceType` (`DISCRETE_GPU` / `INTEGRATED_GPU` / `CPU`)
     *   - OpenGL -- vendor keywords in `glGetString(GL_RENDERER)` (GL has no type enum)
     */
    enum class Gpu {
        /**
         * @brief **Default**: discrete GPU preferred, falls back to integrated only without one
         *
         * Priority: `discrete -> integrated -> other (virtual / CPU software rendering)`.
         * On dual-GPU laptops this is equivalent to "always use the discrete GPU"; thin-and-light
         * laptops with only an integrated GPU also work. This is the "performance default" and the
         * policy this library uses by default.
         */
        Auto,

        /**
         * @brief Discrete GPU only -- if unavailable, **initialization fails**, no silent fallback to integrated
         *
         * For programs that are performance-sensitive and where "running on the integrated GPU is
         * worse than an outright error". Note: after failure, `Kit`'s fallback chain still tries
         * OpenGL (see the note on UseRenderer), so in strict mode it is advisable to also point
         * `UseRenderer` at the API you want.
         */
        Discrete,

        /**
         * @brief Prefer the integrated GPU (power saving)
         *
         * Priority: `integrated -> discrete -> other`. Suitable for background windows and
         * low-load utility programs.
         */
        Integrated,
    };

    /**
     * @brief Default GPU policy for this platform
     * @return always `Gpu::Auto` (discrete GPU preferred)
     *
     * @note Why "discrete GPU preferred" is the default rather than "let the driver decide": on
     *       dual-GPU machines the driver's default behavior is to **hand window rendering to the
     *       integrated GPU** (power saving first), whereas this library is for GUIs and defaults
     *       to performance. To save power, call `UseGpu(Gpu::Integrated)` explicitly.
     */
    constexpr Gpu DefaultGpu() { return Gpu::Auto; }

    /**
     * @brief Readable name of the GPU policy (for logs / error messages)
     * @param gpu the policy to query
     * @return static string literal, no lifetime management needed
     */
    inline const char* GpuName(Gpu gpu) {
        switch (gpu) {
        case Gpu::Auto:       return "Auto";
        case Gpu::Discrete:   return "Discrete";
        case Gpu::Integrated: return "Integrated";
        }
        return "Unknown";
    }

    /**
     * @brief Which render device the backend **actually** landed on (meaningful only after successful initialization)
     *
     * @details
     * The sole reason this struct exists is **observability**: a device policy failure ("I asked
     * for the discrete GPU but ended up on the integrated one") must be discoverable by the
     * program, not guessed by eye.
     * See `Kit::Window::deviceInfo()` for usage.
     *
     * @note Strings use **fixed-length char arrays** rather than `std::string`: this struct is
     *       returned to consumers across the library boundary, and fixed-length arrays have no
     *       heap allocation and no ABI issues (the same consideration as `Rhi::Api` returning
     *       names as `const char*`).
     */
    struct DeviceInfo {
        char   name[160] = {};   /// < Device name, e.g. "NVIDIA GeForce RTX 4060 Laptop GPU"
        char   driver[96] = {};  /// < Driver name + version, e.g. "NVIDIA 595.91.07" / "Intel open-source Mesa driver 25.2.8"
        char   api[24] = {};     /// < Name of the API actually running ("Vulkan" / "OpenGL")
        bool   isDiscrete = false;  /// < Whether it is judged to be a discrete GPU
        bool   valid = false;       /// < false = backend not ready / query unsupported, remaining fields meaningless
    };

} // namespace Rhi
