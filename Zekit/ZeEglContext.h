#pragma once
/**
 * @file ZeEglContext.h
 * @brief OpenGL context for Linux (EGL + X11 window surface)
 *
 * @details
 * Two implementations of the same role as Windows' `GL::GLContext` (WGL):
 * both attach a GL context to a window and provide MakeCurrent / SwapBuffers.
 * They are not merged into one class because the binding mechanisms differ
 * completely (WGL is SetPixelFormat + wglCreateContextAttribsARB, EGL is the
 * three-stage config + surface + context), and forcing them together would
 * only produce a pile of platform branches.
 *
 * What is merged is everything downstream: `Kit::UIRenderer` and `Rhi::GlTextureSink`
 * no longer know about the window system (see `ZeGLProgram.h` / `ZeGlTextureSink.h`),
 * so the same UI rendering code is shared by the WGL and EGL contexts without
 * changing a single line.
 *
 * ## Device selection: why it lives at the context layer
 *
 * On dual-GPU machines EGL defaults to the integrated GPU (power saving first).
 * There are two ways to make Linux default to the discrete GPU, and the limits
 * of each were measured (data table in the ZeEglContext.cpp file header):
 *
 *   - EGL device enumeration (`EGL_EXT_device_enumeration` + `EGL_EXT_platform_device`)
 *     - can name a specific GPU, but on mainstream drivers that display is headless:
 *     it has no window-surface + desktop-GL config (it serves pbuffer/offscreen).
 *     So on its own it cannot solve drawing a window on the discrete GPU.
 *   - The environment variable `__NV_PRIME_RENDER_OFFLOAD=1` (NVIDIA's official switch)
 *     - once set, the X11 platform display also reaches the discrete GPU, at the
 *     cost of modifying process-global state.
 *
 * The measured conclusion is that both are needed, and the order must not be
 * reversed: try device enumeration first (use it when it can really name a GPU),
 * and if no window surface can be created fall back to the X11 platform, setting
 * `__NV_PRIME_RENDER_OFFLOAD` according to policy before falling back.
 * Verified on this machine (Intel RPL-S + RTX 4060, Mesa 25.2.8 + NVIDIA 595):
 * fall back to X11 without the variable -> Intel integrated; set it then fall
 * back to X11 -> RTX 4060 discrete.
 *
 * The policy is in `Rhi::Gpu`: `Auto` (the library default) and `Discrete` both
 * set the variable, `Integrated` clears it - that is the developer explicitly
 * asking for power saving, and it must not be overridden by a previous setting.
 * If it was already set externally, this class does not override it.
 *
 * ## Platform types do not appear in this header
 *
 * All members are `void*`. There are two reasons, both hard:
 *   1. `<EGL/egl.h>` pulls in `<X11/Xlib.h>` on X11, and Xlib's
 *      `None` / `Status` / `Bool` / `Success` are extremely common short names -
 *      this header is included by the render backend, and once it leaks out every
 *      translation unit in the library lives in Xlib's macro environment
 *      (`ZeBoxStyle.h`'s `RadiusMode::None` died exactly this way);
 *   2. this header must compile the same on machines with and without the EGL
 *      development package - if the latter fails to `#include` because
 *      `<EGL/egl.h>` is missing, the Linux backend cannot build at all.
 *
 * Same trick as `ZeVulkanContext.h` / `ZeWindowX11.h`.
 *
 * @note Lifetime: `Create()` -> `MakeCurrent()/SwapBuffers()` each frame -> `Release()`.
 *       `Release()` is idempotent, and the destructor also does it once.
 *
 * @see ZeRenderBackendLinux.cpp (the only user)
 * @see ZeGLContext.h (the Windows counterpart)
 */

#include <cstdint>

#include "include/ZeRenderBackend.h"   // Rhi::Gpu / Rhi::DeviceInfo

namespace GL {

    class EglContext {
    public:
        EglContext() = default;
        ~EglContext();

        EglContext(const EglContext&) = delete;
        EglContext& operator=(const EglContext&) = delete;

        /// @brief Creates the context and attaches it to an X11 window
        /// @param x11Display `Display*` (not owned; the caller guarantees a longer lifetime)
        /// @param xid        X11 window ID
        /// @param major,minor desired desktop GL version (default 4.6)
        /// @param coreProfile true = core profile (this project's shaders are written for core profile)
        /// @param gpu        which GPU to render on (see `Rhi::Gpu`). Default `Auto` = discrete GPU preferred.
        /// @param nativeVisualId the window's own X visual id (0 = unknown, pick by convention)
        /// @return false = cannot be created (half-built resources are already cleaned up, safe to retry)
        ///
        /// @note Why `nativeVisualId` must be passed: EGL requires the EGLConfig's visual
        ///       to exactly match the window's visual for `eglCreateWindowSurface`,
        ///       otherwise it returns `EGL_BAD_MATCH`. Ordinary windows (24-bit default
        ///       visual) match naturally, so not passing it used to be fine; but a
        ///       per-pixel-transparent overlay window is a 32-bit ARGB visual, and
        ///       without picking the config by visual the whole backend fails to build
        ///       - the symptom is "the overlay window is created but nothing is in it".
        ///
        /// @note If the desired version is unavailable it retries with a downgrade
        ///       (4.6 -> 3.3 core -> compatibility); the version actually obtained is
        ///       whatever GL_VERSION says in `deviceInfo().driver`.
        ///       Only `Gpu::Discrete` is the exception: if no discrete GPU can be picked
        ///       it fails outright instead of silently downgrading.
        bool Create(void* x11Display, unsigned long long xid,
                    int major = 4, int minor = 6, bool coreProfile = true,
                    Rhi::Gpu gpu = Rhi::Gpu::Auto,
                    unsigned long nativeVisualId = 0);

        /// @brief Creates a child-window context on the same EGLDisplay / EGLConfig as `share`,
        ///        and shares device resources with it via `eglCreateContext(..., share.m_ctx, ...)`
        /// @param x11Display X11 connection (must be the same one as `share`)
        /// @param xid        the child window's XID (EGL's X11 platform accepts any XID,
        ///                   it need not be a top-level window - which is exactly why a
        ///                   child window can get its own surface)
        /// @param share      the already-created parent context
        /// @return false = shared creation failed (the caller should fall back to an
        ///         independent context, not keep using an empty pipeline)
        ///
        /// @details
        /// Its relation to `Create` is that it skips device selection. The whole EGL
        /// device enumeration / PRIME offload / platform picking in `Create` exists for
        /// one purpose - to settle `EGLDisplay` + `EGLConfig`. When sharing, these two
        /// must be exactly the same as the parent context (a hard EGL requirement:
        /// a different display or an incompatible config makes `eglCreateContext`
        /// return `EGL_BAD_MATCH`), so they are reused directly and neither is re-picked
        /// - re-picking could land on another GPU, and then sharing is impossible.
        ///
        /// @note * This object does not own the display: `Destroy()` does not call `eglTerminate`.
        ///       `EGLDisplay` is a reference-counted process-level object, and `eglTerminate`
        ///       tears it down together with all contexts / surfaces attached to it -
        ///       calling it when a child window is destroyed invalidates the parent window
        ///       (and all sibling windows) on the spot. `m_ownsDisplay` distinguishes this explicitly.
        bool CreateShared(void* x11Display, unsigned long long xid,
                          const EglContext& share,
                          int major = 4, int minor = 6, bool coreProfile = true);

#if defined(__ANDROID__)
        /// @brief [Android] Creates a GLES 3 context and attaches it to an ANativeWindow
        /// @param nativeWindow `ANativeWindow*` (obtained by the Plat layer from `android_app::window`)
        /// @param gpu placeholder only - Android's EGL has no device enumeration; integrated/discrete is decided by the system
        /// @return false = cannot be created (half-built resources are already cleaned up)
        ///
        /// @note This and the desktop `Create()` are two entry points, not platform branches
        ///       of the same function. The desktop one does three things that do not exist
        ///       on Android: EGL device enumeration + PRIME offload (a dual-GPU laptop
        ///       thing), `eglBindAPI(EGL_OPENGL_API)` (desktop GL; Android only has ES),
        ///       and stepwise version downgrade (4.6 -> 3.3 -> compatibility; on Android
        ///       it is GLES 3, there is no other tier).
        ///       So it is implemented separately; `LoadEgl()` is the small shared piece.
        bool CreateAndroid(void* nativeWindow, Rhi::Gpu gpu = Rhi::Gpu::Auto);
#endif

#if defined(__OHOS__)
        /// @brief [HarmonyOS] Creates a GLES 3 context and attaches it to `OHNativeWindow`
        /// @param nativeWindow `OHNativeWindow*` (obtained by the Plat layer from the XComponent's
        ///        `OnSurfaceCreated` callback)
        /// @param gpu placeholder only - HarmonyOS's EGL, like Android's, has no device enumeration
        /// @return false = cannot be created (half-built resources are already cleaned up)
        ///
        /// @details This and `CreateAndroid()` are the second entry point of the same flow:
        ///          both share `CreateGlesOnNativeWindow()` (`eglGetDisplay` ->
        ///          `eglBindAPI(EGL_OPENGL_ES_API)` -> GLES3 window config ->
        ///          `eglCreateWindowSurface` -> `eglMakeCurrent`).
        ///          The only platform difference is Android's `ANativeWindow_setBuffersGeometry`
        ///          - HarmonyOS has no equivalent call (the XComponent buffer format is
        ///          decided by ArkUI), so `platformTag` is passed in and the shared function
        ///          decides per platform whether to do it.
        bool CreateOHOS(void* nativeWindow, Rhi::Gpu gpu = Rhi::Gpu::Auto);
#endif

        /// @brief Makes this context current (on the current thread)
        void MakeCurrent();

        /// @brief Unbinds (call before switching backends / destroying; GL's window claim must be released first)
        void DoneCurrent();

        /// @brief Swaps the front and back buffers (on EGL this is `eglSwapBuffers`)
        void SwapBuffers();

        /// @brief Sets the swap interval: 0 = do not wait for vertical blank, 1 = wait (default)
        /// @note Uses `eglSwapInterval` (a core EGL 1.0 function, no extension needed).
        /// @note Idempotent: if the value equals the last one, it is not sent again.
        void SetSwapInterval(int interval);

        /// @brief The currently issued swap interval (default 1)
        int swapInterval() const { return m_swapInterval; }

        /// @brief Releases all resources and returns the window to the window system. Idempotent.
        void Release() { Destroy(); }

        /// @brief Whether the context and the window surface are both ready
        bool IsValid() const { return m_ctx != nullptr && m_surf != nullptr; }

        /// @brief Which render device it actually landed on (valid after successful creation)
        /// @return `valid = false` means the context is not built yet
        Rhi::DeviceInfo deviceInfo() const { return m_deviceInfo; }

    private:
        void Destroy();

#if defined(__ANDROID__) || defined(__OHOS__)
        /// @brief Shared GLES3 window-context creation flow for mobile (Android / HarmonyOS)
        /// @param nativeWindow the platform's native window handle (`ANativeWindow*` / `OHNativeWindow*`)
        /// @param platformTag prefix in error logs ("CreateAndroid" / "CreateOHOS")
        /// @param setBuffersGeometry whether to do Android's
        ///        `ANativeWindow_setBuffersGeometry()` (must be false on HarmonyOS -
        ///        that function does not exist on OHOS)
        /// @note Why it is extracted: the two paths differ in exactly this one call,
        ///       and the other 60 lines (display / API binding / config / surface / current /
        ///       deviceInfo fill-in) are identical word for word. Copying them twice would
        ///       inevitably drift.
        bool CreateGlesOnNativeWindow(void* nativeWindow, const char* platformTag,
                                      bool setBuffersGeometry);
#endif

        // -----------------------------------------------------------------
        //  Handles are all stored as void* - see the file header "platform types do not appear in this header".
        //  Real types (visible only in the .cpp):
        //    m_dpy    EGLDisplay    m_ctx   EGLContext
        //    m_surf   EGLSurface    m_config EGLConfig
        // -----------------------------------------------------------------
        void* m_dpy    = nullptr;
        void* m_ctx    = nullptr;
        void* m_surf   = nullptr;
        void* m_config = nullptr;

        /// Actual device description (filled after successful Create; `glGetString` is the final truth)
        Rhi::DeviceInfo m_deviceInfo{};

        int m_swapInterval = 1;   /// < the issued swap interval (EGL default is 1)

        /// @brief Whether this object owns the display (decides whether `Destroy` calls `eglTerminate`)
        /// @details `Create` / `CreateAndroid` / `CreateOHOS` build their own display -> `true`;
        ///          `CreateShared` reuses the parent context's display -> false.
        /// @note This is the easiest trap in shared contexts and the hardest to diagnose:
        ///       `eglTerminate` is process-level, and calling it when a child window is
        ///       destroyed makes the parent window go black on the spot - the symptom
        ///       looks like "the renderer crashed".
        bool m_ownsDisplay = true;
    };

} // namespace GL
