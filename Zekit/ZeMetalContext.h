#pragma once
/**
 * @file ZeMetalContext.h
 * @brief Metal context for Apple (macOS / iOS) -- the counterpart of `Vk::VulkanContext`
 *
 * @details
 * ## What this class manages
 *
 * Its division of labor **corresponds one-to-one** with `ZeVulkanContext`:
 *
 * | Responsibility  | VulkanContext                          | MetalContext (this class)                |
 * |----------------|-----------------------------------------|-----------------------------------------|
 * | Device          | `VkInstance` + `VkPhysicalDevice`       | `id<MTLDevice>`                          |
 * | Queue           | `VkQueue`                               | `id<MTLCommandQueue>`                    |
 * | Surface binding | `VkSurfaceKHR` (`vkCreate*Surface`)     | `CAMetalLayer` (`view.layer`)            |
 * | Swapchain       | `VkSwapchainKHR`                        | drawable ring of `CAMetalLayer`          |
 * | Acquire next frame | `vkAcquireNextImageKHR`              | `[layer nextDrawable]`                   |
 * | Submit          | `vkQueueSubmit` + `vkQueuePresentKHR`   | `[cmdBuffer presentDrawable:]` + `commit` |
 * | Frame semaphore | `VkSemaphore` / `VkFence`               | `dispatch_semaphore_t` (at most N frames in flight) |
 *
 * ## The three differences a Vulkan programmer trips over most (documented here to remember them)
 *
 * 1. **Memory management is automatic**. Vulkan requires manual `vkAllocateMemory` / `vkBindImageMemory`;
 *    Metal allocates on texture / buffer creation, there is **no** vkAllocateMemory step.
 *    So this class has **no** `findMemoryType` or the like -- not missing, just unnecessary.
 *
 * 2. **Command buffers are one-shot**. Vulkan's `VkCommandBuffer` can be reset and reused;
 *    Metal's `MTLCommandBuffer` **is spent after one use** (can `enqueue` but usually one per frame).
 *    So each frame `drawFrame()` **creates** a new command buffer, it cannot be cached and reused.
 *
 * 3. **`nextDrawable` blocks**. This is the most critical one:
 *    `[layer nextDrawable]` by default **blocks up to 1 second** until a drawable is available.
 *    Blocking on the iOS/macOS main thread (UI thread) = frozen UI.
 *    Countermeasures (already implemented in this class):
 *      - Set `layer.maximumDrawableCount = 3` (the default is 3, but state the intent explicitly);
 *      - Set `layer.allowsNextDrawableTimeout = NO` (return nil on timeout instead of freezing);
 *      - When `nextDrawable` returns nil, **skip this frame** (return false), no retry, no wait.
 *    The Vulkan counterpart is the `VK_TIMEOUT`/`VK_NOT_READY` return value of
 *    `vkAcquireNextImageKHR` -- Vulkan is non-blocking by nature, on Metal **you must do it yourself**.
 *
 * ## Two sizes, do not mix them (identical to other platforms)
 *
 *   - **drawable (swapchain)** -- **physical** pixels: `layer.drawableSize`
 *     (= `view.bounds.size x contentsScale`, computed by the Plat layer and passed in);
 *   - **UI projection**      -- **logical** pixels: width/height passed in by Kit.
 *   Using physical for both (UI shrinks to the top-left corner) or logical for both (image occupies only a corner) is wrong.
 *
 * ## Lifecycle
 *
 *     MetalContext ctx;
 *     ctx.init(layer, gpu);          // create device/queue, claim layer
 *     VkMetalUIRenderer ui;  ui. Init(ctx);
 *     while (...) ctx.drawFrame(r,g,b);   // internally calls the ui frame callback
 *     ui. Shutdown();                // must be before ctx.cleanup()
 *     ctx.cleanup();
 *
 * @note This header is included **only on Apple platforms** (`ZeRenderBackendApple.mm`).
 *       It uses Objective-C types such as `id<MTLDevice>`, so it **must** be compiled as
 *       Objective-C++ (`.mm`) -- which is why both ZeRenderBackendApple
 *       and ZeMetalContext are `.mm` rather than `.cpp`.
 */

#if defined(__APPLE__)

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include "include/ZeRenderBackend.h"   // Rhi::Gpu / Rhi::DeviceInfo (pure header, no platform types)

#include <cstdint>
#include <functional>

namespace Kit { class FrameCollector; }

namespace Mtl {

    /// @brief Metal render context -- owns device / queue, claims a CAMetalLayer
    class MetalContext {
    public:
        MetalContext() = default;
        ~MetalContext();

        MetalContext(const MetalContext&) = delete;
        MetalContext& operator=(const MetalContext&) = delete;

        /// @brief Information for this frame -- passed to the UI renderer callback (corresponds to VulkanContext::FrameInfo)
        ///
        /// @note **Structural difference** from the Vulkan version: on Vulkan it is "begin render pass
        ///       then callback", because the render pass is an explicitly created device object;
        ///       on Metal it is "get drawable first, then create encoder",
        ///       so the `encoder` the callback receives is an **already created** render command encoder.
        struct FrameInfo {
            id<MTLRenderCommandEncoder> encoder = nil;   /// < render encoder (already startEncoding)
            id<CAMetalDrawable>         drawable = nil;  /// < drawable for this frame
            uint32_t                    width = 0;       /// < **physical** pixel width
            uint32_t                    height = 0;      /// < **physical** pixel height
            uint32_t                    slot = 0;        /// < frame slot index (0..kMaxFramesInFlight-1)
            /// @brief Scale ratio for UI projection (the logical -> NDC matrix is computed by the UI renderer itself)
            /// @note Same as the Vulkan version: the projection matrix lives in the UI renderer, here only the size is given.
        };

        /// @brief Frame callback type (corresponds to VulkanContext::FrameRecorder)
        using FrameRecorder = std::function<void(const FrameInfo&)>;

        // ---------------------------------------------------------------
        //  Init / cleanup
        // ---------------------------------------------------------------

        /// @brief Creates device / queue and **claims** this CAMetalLayer
        /// @param layer CAMetalLayer obtained by the Plat layer from NSView / UIView (nil = failure)
        /// @param gpu   Which GPU to use. On Apple this is **basically a no-op**:
        ///              Apple Silicon has only one GPU; Intel Macs have integrated/discrete,
        ///              but Metal's `MTLCopyAllDevices()` returns multiple only on multi-GPU machines.
        ///              This implementation picks one per the gpu policy (low power / high performance), ignored on single GPU.
        /// @return false = failure (layer is nil / device creation failed / claim failed)
        /// @note On failure it **cleans up after itself** ("all or nothing", same convention as IBackend::Init)
        bool init(id<CAMetalLayer> layer, Rhi::Gpu gpu = Rhi::Gpu::Auto);

        /// @brief Creates only device / queue, does not claim the layer (corresponds to VulkanContext::preheat)
        /// @return true = preheat succeeded; afterwards `init()` only needs to claim the layer, much faster
        /// @note Same intent as the Vulkan version: move the "GPU driver load / device creation" cost
        ///       to **before** the window goes on screen. On Apple this cost mainly comes from
        ///       `MTLCreateSystemDefaultDevice()` (kernel-mode driver handshake).
        bool preheat(Rhi::Gpu gpu = Rhi::Gpu::Auto);

        /// @brief Releases everything. Must be idempotent.
        void cleanup();

        // ---------------------------------------------------------------
        //  Queries
        // ---------------------------------------------------------------

        /// @brief Whether render resources are ready (drawFrame can be called safely)
        /// @note Same meaning as the Vulkan version: after `preheat()` the device exists but the layer is not claimed,
        ///       this function returns false; `hasAnyResource()` is the criterion for "whether to clean up".
        bool isReady() const { return m_layer != nil && m_psoFormatReady; }

        /// @brief Whether any resource requiring cleanup is held (the preheated intermediate state counts too)
        bool hasAnyResource() const { return m_device != nil || m_layer != nil; }

        // ---------------------------------------------------------------
        //  Sizes
        // ---------------------------------------------------------------

        /// @brief Sets the **physical** pixel size of the swapchain (drawable)
        /// @note Can be called every frame or on size change; returns false directly if same as last time (no change)
        /// @return Whether it changed compared to last time
        bool setClientSize(int widthPx, int heightPx);

        /// @brief Sets the **logical** pixel size for UI projection (same scale as Kit coordinates)
        /// @note Counterpart of the Vulkan version's `setUiSize`. The UI renderer computes the orthographic projection from it.
        void setUiSize(int widthVp, int heightVp) { m_uiW = widthVp; m_uiH = heightVp; }

        /// @brief Returns the pixel format of the current drawable (needed by the UI renderer to create pipelines)
        /// @note Fixed to `MTLPixelFormatBGRA8Unorm` (the CAMetalLayer default, also the most
        ///       common on Apple) -- Vulkan requires enumerating `VkSurfaceFormatKHR`,
        ///       Metal **has no such choice**, so a fixed value is used. This is also why this class has no
        ///       `pickSurfaceFormat()`.
        MTLPixelFormat drawablePixelFormat() const { return MTLPixelFormatBGRA8Unorm; }

        /// @brief Number of frame slots (in-flight frame limit) -- the UI renderer uses it to size the vertex buffer ring
        static constexpr uint32_t kMaxFramesInFlight = 3;

        // ---------------------------------------------------------------
        //  Draw one frame
        // ---------------------------------------------------------------

        /// @brief Acquire drawable -> create encoder -> call frame callback -> present -> commit
        /// @param r,g,b Clear color (linear 0~1)
        /// @return false = no image this frame (no drawable / not ready / size is 0)
        ///
        /// @note This is the place closest in shape to `VulkanContext::drawFrame`,
        ///       but with three differences:
        ///         1. acquire is `[layer nextDrawable]` (may return nil, non-blocking is **configured**);
        ///         2. there is an extra `renderCommandEncoder` create/end in between (Vulkan uses a render pass object);
        ///         3. present is not a separate API but `[cmdBuffer presentDrawable:]` -- attach it,
        ///            it takes effect with commit.
        bool drawFrame(float r, float g, float b);

        /// @brief Sets the frame callback (the UI renderer is invoked here)
        /// @note Pass nullptr to clear. Requirement: the callback **must not throw** (drawFrame catches it and skips this frame).
        void setFrameRecorder(FrameRecorder rec) { m_recorder = std::move(rec); }

        // ---------------------------------------------------------------
        //  Device info
        // ---------------------------------------------------------------

        /// @brief Description of the render device actually used by this context (valid after init succeeds)
        Rhi::DeviceInfo deviceInfo() const;

        // ---------------------------------------------------------------
        //  Raw handles for the UI renderer (they need device / queue to create pipelines)
        // ---------------------------------------------------------------
        id<MTLDevice>       device() const { return m_device; }
        id<MTLCommandQueue> queue()  const { return m_queue; }

    private:
        bool pickDevice(Rhi::Gpu gpu);

        // ---- Device and queue (created by init / preheat) ----
        id<MTLDevice>        m_device = nil;
        id<MTLCommandQueue>  m_queue  = nil;

        // ---- Surface (claimed by init) ----
        id<CAMetalLayer>     m_layer  = nil;
        bool                 m_psoFormatReady = false;   /// < layer claimed and format fixed

        // ---- Sizes ----
        int m_pxW = 0, m_pxH = 0;    /// < drawable physical pixels
        int m_uiW = 0, m_uiH = 0;    /// < UI projection logical pixels

        // ---- Frame slot synchronization (corresponds to Vulkan's per-frame fence + semaphore) ----
        // @brief Semaphore limiting frames in flight: guarantees at most kMaxFramesInFlight command buffers
        //        are not yet finished, so the CPU does not write frame N+3 vertex data into a buffer still being read.
        //        Vulkan uses `vkWaitForFences` for the same purpose.
        dispatch_semaphore_t m_inFlight = nil;

        /// @brief Drawn frame counter (used to rotate the frame slot index, see the fi.slot note in drawFrame)
        uint32_t m_frameCounter = 0;

        // ---- Frame callback ----
        FrameRecorder m_recorder;
    };

} // namespace Mtl

#endif // __APPLE__
