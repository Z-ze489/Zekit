#pragma once
/**
 * @file ZeRhiBackend.h
 * @brief Zekit render backend abstraction layer -- **library-internal interface** (`Rhi::IBackend` and factory)
 *
 * @details
 * The second pillar alongside `ZePlatformWindow.h` (the `Plat` layer).
 *
 * ## Why it is called `ZeRhiBackend.h` and not `ZeRenderBackend.h`
 *
 * It used to be called `ZeRenderBackend.h`, and the public `include/ZeRenderBackend.h` had the same name --
 * **two files with the same name, different contents**. A `#include` in `""` form searches the directory of the current file first, so:
 *
 *     a .cpp in the root dir  writes #include "ZeRenderBackend.h"  ->  gets the **library-internal version** (IBackend)
 *     a header in include/   writes the same name              ->  gets the **public version** (Rhi::Api)
 *
 * Both sides are "accidentally correct", but **the name collision itself is a trap**: add a piece of code to the wrong copy and it compiles without error,
 * behavior does not change at all -- you think it "did not take effect", when in fact you "changed the other copy".
 * Renamed on 2026-10-29 so that the result of `#include` resolution is decided by the name, no longer by the containing directory.
 *
 * > The public `Rhi::Api` / `Gpu` / `DeviceInfo` are still pulled in by line 58 of this file --
 * > this file is "the public part + `IBackend`", not a replacement.
 *
 * ## Why it is needed
 *
 * Render APIs (OpenGL / Vulkan / D3D12 / Metal) are **platform-dependent**:
 * their headers, handle types and availability all vary by platform. If a library header contained
 *
 *     #include <d3d12.h>
 *
 * then compiling that line on Linux is a `fatal error C1083` -- a compile-time failure that
 * a runtime branch cannot rescue.
 *
 * So this layer does two things:
 *   1. **Collect all platform graphics APIs into .cpp files** (see `ZeRenderBackendWin32.cpp` /
 *      `ZeRenderBackendLinux.cpp`); the remaining translation units in the library only know this file's interface;
 *   2. **Restrict the selectable API enum by platform** (see `include/ZeRenderBackend.h`),
 *      so that "selecting a backend this platform does not have" becomes a **compile error** instead of a runtime false.
 *
 * ## Implementation files per platform
 *
 * | Platform | File | Backends |
 * |---|---|---|
 * | Windows | `ZeRenderBackendWin32.cpp` | GL(WGL) / Vulkan / D3D12 |
 * | Linux | `ZeRenderBackendLinux.cpp` | Vulkan / GL(EGL) |
 * | Other | `ZeRenderBackend.cpp` | `UnsupportedBackend` (compile-time fallback) |
 * | **All platforms** | `ZeRenderBackendSoft.cpp` | **software rasterizer** (`Api::Software`) |
 *
 * The **compile-time boundary** of the first three rows is drawn with `#if`: each platform compiles only its own .cpp, and the other translation
 * units do not exist in the object files at all. Adding a platform = adding a .cpp + allowing it in the build script;
 * this interface and the Kit layer need not change a single line.
 *
 * The software backend is the **only cross-platform** one (it depends on no graphics API): assembly in
 * `ZeRenderBackendSoft.cpp`, rasterization in `ZeSoftRaster.cpp`, presentation in
 * `ZeSoftSurface.cpp` (only this one dispatches by platform). It is the end of the fallback chain -- see
 * the description of `CreateSoftwareBackend`.
 *
 * ## Dependency direction with Kit
 *
 *     Kit::Window::Impl  --holds-->  Rhi::IBackend  --implemented by-->  GL / Vk / D3D12
 *
 * The `Rhi` layer **does not know `Kit`** -- it only forward-declares `Kit::FrameCollector`; the concrete members
 * are passed in by the holder (Kit) as `const&` only in the implementation.
 *
 * ## Interface boundary: unify only the "entry", not the "internals"
 *
 * This interface covers only the part where the three backends have the **same shape**: init / draw one frame / resize / destroy.
 * Resource management inside each backend (Vk's descriptor sets / command buffer fences, D3D12's SRV heap /
 * fence, GL's immediate state machine) is **deliberately not abstracted** -- forcing unification only yields a pile of
 * `if (api == ...)`, or an all-purpose "resource" abstraction that slows all three down.
 *
 * This is the same cut as `Plat::IWindow` unifying only "the entry of the message pump" and not "how Win32 messages run".
 *
 * @note This file is an **internal header** and does not go into `include/`. Consumers only see
 *       the `Rhi::Api` enum in `include/ZeRenderBackend.h` -- consumers do not touch
 *       backend objects directly; they use the renderer only through `Kit::Window::UseRenderer()`.
 */

#include "include/ZeRenderBackend.h"   // Rhi::Api / IsApiSupported / ApiName (public part)
                                       // * The path on this line is **deliberately** written with the include/ prefix:
                                       //   it must get the public version. Writing "ZeRenderBackend.h"
                                       //   would also work now (there is no same-named file in the root dir anymore), but the full path
                                       //   makes "which copy I want" visible from the code, not dependent on resolution rules.

namespace Kit { class FrameCollector; }

namespace Rhi {

    // =================================================================
    //  Backend interface
    // =================================================================
    //
    // @brief An abstraction of a render backend.
    //
    // @details Lifecycle (strict order; violating it crashes or leaks VRAM):
    //
    //     1. `CreateBackend(api)`  -> get an instance (uninitialized)
    //     2. `Init(hwnd, w, h)`   -> create context / swap chain / render pipeline
    //     3. each frame `DrawFrame(fc, w, h, r, g, b)`
    //     4. `Shutdown()`         -> release everything
    //
    // @note `Init` / `Shutdown` / `DrawFrame` must all be **idempotent and repeatable**:
    //       the upper layer calls Shutdown on both the `exec()` and `~Window()` paths;
    //       DrawFrame is called repeatedly while the window is minimized.
    class IBackend {
    public:
        virtual ~IBackend() = default;

        IBackend(const IBackend&) = delete;
        IBackend& operator=(const IBackend&) = delete;

        // ---------------------------------------------------------------
        //  Queries
        // ---------------------------------------------------------------

        /// @brief The API actually used by this backend
        virtual Api api() const = 0;

        /// @brief Whether initialization is complete and frames can be drawn safely
        virtual bool IsReady() const = 0;

        /// @brief Whether this backend is currently using a **per-pixel transparent** composition swap chain
        /// @return true = the swap chain carries alpha and is bound to the compositor (Windows: D3D12's
        ///         `CreateSwapChainForComposition` + DirectComposition visual).
        ///         The window can then drop its redirection surface
        ///         (`IWindow::SetPerPixelTransparent(true)`), and only the pixels drawn by the renderer
        ///         remain on screen -- that is "content visible, window invisible".
        ///
        /// @note Default false. **Must be honest**: if it reports true while not actually carrying alpha, the window
        ///       disappears entirely once the redirection surface is dropped (this is the case for GL -- its
        ///       default framebuffer has no alpha channel that DWM can composite).
        /// @note This is a "runtime fact", not a "capability declaration": D3D12 likewise returns false when dcomp.dll is missing or it is downgraded
        ///       to `DXGI_ALPHA_MODE_IGNORE`.
        virtual bool IsPerPixelTransparent() const { return false; }

        // ---------------------------------------------------------------
        //  Device policy and observation (optional overrides)
        // ---------------------------------------------------------------

        /// @brief Sets which GPU to render with
        /// @param gpu See `Rhi::Gpu`. **Must be called before `Init()`** -- device selection
        ///        happens inside Init (the step that creates the surface / picks the physical device).
        /// @note The default implementation is a **no-op**: single-GPU platforms (and older backends that do not yet support this policy)
        ///       need not override it. It is deliberately left non-pure-virtual -- the policy is an "enhancement",
        ///       and should not force every backend (including D3D12) to implement it.
        virtual void SetGpu(Gpu /*gpu*/) {}

        /// @brief The render device **actually** used by this backend (valid only after a successful Init)
        /// @return Defaults to `valid = false` (= this backend does not support the query);
        ///         the caller shows "unknown" based on this instead of inventing a name.
        virtual DeviceInfo deviceInfo() const { return {}; }

        // ---------------------------------------------------------------
        //  Shared-context derivation (for multiple native desktop windows; optional override)
        // ---------------------------------------------------------------

        /// @brief Whether this backend can derive a context that **shares device resources**
        /// @return Default false.
        /// @note A backend that does not support it must **honestly report false**; it must not "derive a backend that does not actually share" --
        ///       that would make the caller manage resources as if they were shared (e.g. child windows not building their own font atlas),
        ///       while in reality there are two sets, showing up as all-white text in child windows / invalid texture handles.
        virtual bool CanShareContext() const { return false; }

        /// @brief Derives a new backend that **shares resources** with this backend and initializes it to `nativeHandle`
        /// @param nativeHandle Native handle of the new window (desktop child window)
        /// @param width,height Client area size of the new window
        /// @param clearAlpha Clear alpha of the child window (0 = transparent background / 1 = opaque; default 1).
        ///        A transparent child window (overlay carrier) must receive it **before the swap chain is created** -- D3D12/Vulkan
        ///        swap chain alpha mode can only be set at creation and cannot be changed by ResizeBuffers afterwards.
        /// @return The new backend (**already Init'ed**; the caller is responsible for `DestroyBackend`);
        ///         nullptr = this backend does not support sharing, or shared creation failed -- in both cases the caller
        ///         treats it as "degrade to an independent context" (functionality still correct, just no VRAM savings).
        ///
        /// @par How much is shared is an implementation detail of each backend
        /// - GL    -- same share group: textures / VBOs / programs shared, **VAOs / FBOs not shared**;
        /// - Vulkan-- reuses `VkInstance` / `VkDevice` / queues, creates another surface + swapchain;
        /// - D3D12 -- reuses device / command queue, creates another swapchain.
        /// This interface only promises "it is safe for the caller to manage resources as shared"; it does not promise the exact shared set.
        ///
        /// @note The derived backend has an **independent lifecycle**: `Shutdown()` on it does not tear down the main instance
        ///       (each holds its own swap chain / renderer), but it **must be destroyed before the main instance** --
        ///       once the main instance Shutdowns, the texture atlas in the share group is gone.
        virtual IBackend* CreateShared(void* /*nativeHandle*/, int /*width*/, int /*height*/,
                                       float /*clearAlpha*/ = 1.0f) {
            return nullptr;
        }

        // ---------------------------------------------------------------
        //  Lifecycle
        // ---------------------------------------------------------------

        /// @brief Creates the context, swap chain and render pipeline
        /// @param nativeHandle Native window handle (HWND on Windows; platform-specific types elsewhere)
        /// @param width  Client area width in pixels (GL uses it to set the viewport; Vk/D3D12 use it to create the swap chain)
        /// @param height Client area height in pixels
        /// @return false = creation failed (the caller should fall back to another backend instead of drawing with an empty pipeline)
        ///
        /// @note On failure it **must clean up the half-built resources itself**; the caller is not required to remedy it.
        ///       This is the "all or nothing" principle: avoid the intermediate state of "switched over but black screen".
        virtual bool Init(void* nativeHandle, int width, int height) = 0;

        /// @brief **Pre-warm**: performs only the part of initialization that is "independent of whether the window is on screen" -- loading the GPU
        ///        driver / creating the instance / selecting the device. **Does not create the swap chain**, so `IsReady()` remains false afterwards.
        /// @param nativeHandle Native window handle (same as Init; the window may not be mapped yet at this point)
        /// @return true = pre-warm succeeded (`Init()` then only needs to add the swap chain, much faster);
        ///         false = this backend does not support pre-warm / pre-warm failed. **Neither case is an error** --
        ///         the caller need not remember the return value; `Init()` always takes the full path as a fallback.
        ///
        /// @note Why the default is a no-op rather than pure virtual: see the same reason as `SetGpu` -- this is
        ///       an "enhancement" and should not force every backend to implement it.
        ///       - The Win32 Vk backend **does not need** it -- there `CanPrepareRendererBeforeShow()`
        ///         is already true, and `Init()` runs entirely before the window is shown;
        ///       - The X11 Vk backend **needs** it -- there the swap chain must be created after map, so
        ///         `Init()` is stuck after "the window is already visible", and driver loading time becomes user-visible latency.
        /// @note Call timing: inside `Kit::Window::Show()`, **before** the platform layer shows the window. At this point the window object
        ///       (XID / HWND) already exists, it is just not shown yet -- this is the premise that makes this function work.
        virtual bool Preheat(void* /*nativeHandle*/) { return false; }

        /// @brief Releases all resources. Must be idempotent (repeatable).
        /// @note There are two call sites: normal exit of `Window::exec()`, and the `~Window()` fallback.
        virtual void Shutdown() = 0;

        /// @brief Notifies the backend of a client area size change (GL updates the viewport; Vk/D3D12 recreate the swap chain)
        /// @return false = recreation failed (do not draw this frame; retry next frame)
        virtual bool Resize(int width, int height) = 0;

        // ---------------------------------------------------------------
        //  Draw one frame
        // ---------------------------------------------------------------

        /// @brief Draws one frame: clear -> draw the display list -> present
        /// @param fc    This frame's display list (widgets already filled in during frame collection)
        /// @param width  Client area width in pixels
        /// @param height Client area height in pixels
        /// @param r,g,b  Clear color (linear 0~1, same scale as theme tokens)
        /// @return false = no image produced this frame (window minimized / swap chain just recreated / device lost / not ready)
        ///
        /// @note * The **fundamental shape differences** of the three backends are absorbed here, so the upper layer needs only one call:
        ///       - GL    -- MakeCurrent -> viewport -> clear -> UIRenderer::Draw -> SwapBuffers
        ///       - Vk    -- VulkanContext::drawFrame internally does acquire / render pass / present,
        ///                 and the UI drawing is invoked inside the pass by the frame callback it installs
        ///       - D3D12 -- same as Vk, with D3D12Context::drawFrame
        ///       `fc` must remain valid until return (the frame callback reads it inside this function).
        virtual bool DrawFrame(const Kit::FrameCollector& fc,
                               int width, int height,
                               float r, float g, float b) = 0;

        // ---------------------------------------------------------------
        //  Present throttling (vertical sync)
        // ---------------------------------------------------------------

        /// @brief Sets this frame's swap interval (`0` = do not wait for vblank, `1` = wait for one vblank)
        /// @details
        /// **Why it must vary per frame**: waiting for vblank means `SwapBuffers` / `Present` **blocks**
        /// until the next refresh boundary (up to 16.7ms). That block happens on the **UI thread**, so
        /// mouse input arriving every 8ms during a drag can only be processed once every 16.7ms -- this is the **real**
        /// reason "rendering cannot keep up with the mouse" (measured: `paint` is only 0.07ms, while `draw` is occupied for 14.7ms).
        /// So interactive frames (recent input) pass 0 and idle frames pass 1: dragging follows the hand, idle does not spin.
        /// @note In windowed mode (flip model + DWM composition) interval 0 **does not tear** -- presentation is still
        ///       composited uniformly by DWM, it just no longer waits for vblank. This is the same reason as the existing handling when dragging the border.
        /// @note The default implementation is a no-op (the Vulkan backend's present mode is fixed at swap chain creation,
        ///       so this interface has no effect on it) -- consistent with the library's convention of "silently no-op when unsupported".
        virtual void SetSwapInterval(int /*interval*/) {}

        // ---------------------------------------------------------------
        //  Clear alpha (transparent child window)
        // ---------------------------------------------------------------
        // @brief Sets the clear alpha (0 = transparent background, 1 = opaque; default 1)
        // @details The overlay carrier (OverlayWindow's Popup) uses it to clear the canvas to transparent,
        //       while content primitives carry alpha=1 -- so the layered window composites by per-pixel alpha,
        //       pixels with content are visible and hit-testable, and transparent areas pass through naturally (the desktop pet's transparent child window).
        // @note Default no-op: only the GL backend needs it (Vulkan / D3D12 swap chain formats are a separate matter;
        //       implement silently when supported, no functional impact when not).
        virtual void SetClearAlpha(float /*alpha*/) {}

        /// @brief Whether this frame **deferred** the swap chain recreation (= window size is being changed continuously)
        /// @details
        /// Only backends where "recreating the swap chain is expensive" defer it (D3D12: `ResizeBuffers` waits
        /// one composition cycle each time, measured 6~13ms). During the deferral the image is "content scaled proportionally into the old buffer,
        /// stretched by DWM" -- clean, responsive, but **not the exact size**.
        ///
        /// So when Kit gets true it **schedules a later repaint** (about 100ms later),
        /// so that once the size stabilizes, that frame at the exact size is filled in. Without this revisit,
        /// the image would stay stretched forever (which looks like "the text is a bit blurry").
        ///
        /// @note Default false: GL resizing is **free** (just glViewport),
        ///       there is no cost to avoid, so it stays exact every frame.
        virtual bool ResizeDeferred() const { return false; }

        // ---------------------------------------------------------------
        //  Texture redirection (font atlas / image cache)
        // ---------------------------------------------------------------

        /// @brief Redirects texture creation to this backend (font glyphs / image resources go through here)
        /// @param fontRenderer **Kit::FontRenderer*** (passed as void* to keep Kit headers out of this layer's public surface)
        /// @param imageStore   **Kit::ImageStore***
        ///
        /// @note The GL backend's implementation is a no-op -- on the GL path textures are created directly by FontRenderer / ImageStore
        ///       via glGenTextures (historical design), no hook needed.
        /// @note The caller (Kit) must call `DropAllGlyphTextures()` +
        ///       `ImageStore::Shutdown()` **before** calling this function, otherwise old handles would be destroyed by the new sink as if they were its own IDs.
        virtual void AttachTextureSinks(void* fontRenderer, void* imageStore) = 0;

        /// @brief Redirects texture creation back to the **built-in GL path** (call before switching backends / destroying)
        /// @note The implementation first releases textures with the **current** sink, then sets the sink back to nullptr --
        ///       nullptr always means "use the built-in GL path" in the FontRenderer / ImageStore convention.
        /// @note The GL backend's implementation is a no-op (it was never redirected).
        virtual void DetachTextureSinks(void* fontRenderer, void* imageStore) = 0;

    protected:
        IBackend() = default;
    };

    // =================================================================
    //  Factory (compile-time dispatch)
    // =================================================================

    /// @brief Creates a backend instance by API (**compile-time dispatch**)
    /// @param api The desired API. Returns nullptr when given a value this platform does not have
    ///            (under normal paths it cannot be passed in -- enum members are already trimmed by platform).
    /// @return Backend instance (uninitialized). The caller is responsible for `SetGpu()` (optional),
    ///         `Init()` and `Shutdown()`.
    ///
    /// @note Dispatch is done at **compile time**: each platform's .cpp compiles only its own `case`
    ///       branches; the other branches do not exist in the object files at all.
    /// @note Returning nullptr means "this platform does not have this backend", not "initialization failed".
    ///       Initialization failure must be expressed by `Init()` returning false (see `IBackend::Init`).
    IBackend* CreateBackend(Api api);

    /// @brief Destroys a backend instance (used with `CreateBackend`, so the caller need not know the concrete type)
    void DestroyBackend(IBackend* backend);

    // =================================================================
    //  Software rasterizer backend (**cross-platform**, not part of any platform dispatch table)
    // =================================================================
    //
    //  * Why it is not written in the "platform -> .cpp" table above:
    //    the **only platform-dependent part of software rendering is "how pixels get on screen"**, and that is cut into
    //    ZeSoftSurface.cpp (compile-time dispatch within one file). The rest is pure CPU code
    //    (ZeSoftRaster.cpp) and assembly (ZeRenderBackendSoft.cpp), shared by all three platforms.
    //    Each platform's `CreateBackend` only needs one extra line `case Api::Software:
    //    return CreateSoftwareBackend();`.
    //
    //  * It is the **end** of the `Kit` fallback chain: when all GPU backends (including OpenGL) fail to initialize,
    //    `Window::Impl::ensureBackend` falls through to here. For semantics see
    //    "`Software`: a way out for 'machines without a renderer installed'" in `include/ZeRenderBackend.h`.

    /// @brief Creates a software rasterizer backend (uninitialized)
    /// @return Backend instance. **Always non-null** -- it needs no driver, so there is no "this platform does not have it"
    ///         case; "presentation surface not implemented" is reported truthfully by `Init()` returning false.
    IBackend* CreateSoftwareBackend();

} // namespace Rhi
