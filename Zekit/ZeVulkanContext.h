#pragma once
// Platform-independent Vulkan context: instance -> surface -> device -> swapchain -> frame loop.
// The native window arrives as an opaque triple (instance / display / window): Win32 = HWND,
// X11 = Display* + XID, Android = ANativeWindow*. No platform header is included here, and
// VK_USE_PLATFORM_* stays undefined: Xlib's `None` macro would break RadiusMode::None and
// BorderStyle::None in every translation unit that includes this header. Only the .cpp that
// needs platform surfaces defines it, ahead of including this header.
#include <vulkan/vulkan.h>

#include "include/ZeRenderBackend.h"   // Rhi::Gpu / Rhi::DeviceInfo

#include <cstdint>
#include <exception>
#include <functional>
#include <string>
#include <vector>

namespace Vk {
    class VulkanContext {
    public:
        // Native window binding: a platform-independent triple (see the file header).
        // All three fields are opaque handles; this header does not interpret them.
        struct NativeWindow {
            void*              instance = nullptr;   // Win32: HINSTANCE; X11 / Android: unused
            void*              display  = nullptr;   // X11: Display*; Win32 / Android: unused
            unsigned long long window   = 0;         // Win32: HWND; X11: XID; Android: ANativeWindow* (cast)
        };

        // Per-frame context handed to the frame callback: where to draw and how large.
        struct FrameInfo {
            VkCommandBuffer cmd = VK_NULL_HANDLE;   // Command buffer already begun (inside the render pass)
            uint32_t        imageIndex = 0;         // Index of the acquired swapchain image
            uint32_t        frameSlot = 0;          // Frame slot, 0 .. kFramesInFlight-1
            VkExtent2D      extent{};               // Swapchain size = physical pixels
            // UI coordinate size in logical pixels: the projection matrix uses this, not `extent`.
            // Desktop keeps both identical (setUiSize is never called), so this stays invisible there.
            // Mobile has high DPI: the UI lives in logical pixels while the swapchain, viewport and
            // scissor need physical pixels. Mixing them up shrinks the whole UI into the top-left.
            VkExtent2D      uiExtent{};
            VkFormat        format = VK_FORMAT_UNDEFINED;
        };

        // Frame callback, invoked inside the render pass between begin and end.
        // This is the only entry point for UI drawing: acquire/submit/present stay in this class,
        // and the drawer only records commands into the given command buffer.
        using FrameRecordFn = std::function<void(const FrameInfo&)>;

        VulkanContext() = default;
        ~VulkanContext();

        // Non-copyable
        VulkanContext(const VulkanContext&) = delete;
        VulkanContext& operator=(const VulkanContext&) = delete;

        // Initializes instance, surface, device, queues, swapchain, command buffers and sync.
        // Throws std::runtime_error on failure after releasing every partially created resource,
        // so a retry is safe. Equivalent to initDevice() + initSwapchain(); both are idempotent.
        void init(const NativeWindow& native, Rhi::Gpu gpu = Rhi::Gpu::Auto);

        // Preheats only the half that does not need a visible window: driver load, instance,
        // surface handle, physical device, logical device.
        // Returns true on success (a later init only adds the swapchain); false is not an error,
        // the following init then takes the full path. Failures are traced internally only.
        bool preheat(const NativeWindow& native, Rhi::Gpu gpu = Rhi::Gpu::Auto);

        // Builds only the device half (idempotent); shared by preheat and init.
        bool initDevice(const NativeWindow& native, Rhi::Gpu gpu = Rhi::Gpu::Auto);

        // Builds only the swapchain half (idempotent): swapchain, image views, semaphores, pool,
        // command buffers and sync. Throws std::runtime_error after cleanup on failure.
        void initSwapchain();

        // Releases all resources (idempotent)
        void cleanup();

        // True when render resources are ready and drawFrame is safe to call
        bool isReady() const { return m_swapchain != VK_NULL_HANDLE; }

        // True once any Vulkan resource exists, even just an instance or a device.
        // Kept distinct from isReady() on purpose: after preheat() the swapchain is missing but the
        // surface exists, and skipping cleanup() would leave that surface holding the window XID,
        // so any later backend rebuild hits NATIVE_WINDOW_IN_USE_KHR.
        bool hasAnyResource() const { return m_device != VK_NULL_HANDLE || m_instance != VK_NULL_HANDLE; }

        // Tells the backend the current client area size in pixels.
        // Call it before drawFrame or whenever the size changes; this class decides whether the
        // swapchain must be rebuilt and never asks the platform itself.
        // Returns whether the size changed versus the previous call.
        bool setClientSize(int width, int height);

        // Tells the backend the UI coordinate size in logical pixels.
        // Desktop does not need it: uiExtent follows extent, keeping the old behaviour.
        // Mobile must call it (high DPI makes logical and physical sizes differ), otherwise the
        // projection uses the physical size and the whole UI shrinks into the top-left corner.
        // Passing 0 for w or h restores "follow the swapchain size".
        // setClientSize() decides swapchain rebuilds (physical size); this one only affects projection.
        void setUiSize(int w, int h) {
            m_uiW = (w > 0) ? static_cast<uint32_t>(w) : 0u;
            m_uiH = (h > 0) ? static_cast<uint32_t>(h) : 0u;
        }

        // Sets the clear alpha (0 = transparent background, 1 = opaque; default 1).
        // Call it before creating the swapchain: it picks compositeAlpha (POST_MULTIPLIED for
        // transparency, matching the UI renderer blend) and also feeds the clear color alpha.
        // Changing it later only affects later frames' clear alpha, not compositeAlpha.
        void setClearAlpha(float alpha) { m_clearAlpha = (alpha <= 0.0f) ? 0.0f : 1.0f; }

        // True when this context really presents with an alpha-capable composite mode.
        // Derived from the swapchain's final compositeAlpha: OPAQUE means opaque, POST/PRE_MULTIPLIED
        // means per-pixel transparency really landed. Without this query, per-pixel transparent
        // windows on the Vulkan path reported false and callers branched the wrong way.
        bool isPerPixelTransparent() const { return m_perPixelTransparent; }

        // Draws one frame: acquire -> clear -> callback -> present.
        // r,g,b are the clear color in linear 0..1, same scale as theme tokens.
        // Returns false when nothing was presented (minimized, swapchain just rebuilt, device lost).
        // A client size change rebuilds the swapchain internally; the size comes from setClientSize().
        bool drawFrame(float r, float g, float b);

        // Sets the frame callback where UI drawing hooks in.
        // It runs once per frame inside the render pass; pass nullptr to clear it.
        // The frame slot's fence has already been waited on, so writing into the slot is safe.
        // Do not throw from it: drawFrame catches the exception and aborts the frame.
        void setFrameRecorder(FrameRecordFn fn) { m_recordFn = std::move(fn); }

        // Blocks until the GPU is idle (before swapchain rebuilds and before destruction)
        void waitIdle();

        // The render device this context actually landed on (valid after a successful init).
        // Returns valid = false when not initialized or not ready.
        Rhi::DeviceInfo deviceInfo() const;

        // Accessors
        VkInstance       instance() const { return m_instance; }
        VkSurfaceKHR     surface()  const { return m_surface; }
        VkPhysicalDevice physicalDevice() const { return m_physicalDevice; }
        VkDevice         device()   const { return m_device; }
        VkQueue          graphicsQueue() const { return m_graphicsQueue; }
        VkQueue          presentQueue()  const { return m_presentQueue; }
        VkCommandPool    commandPool()  const { return m_commandPool; }   // One-shot commands for texture uploads
        uint32_t         framesInFlight() const { return kFramesInFlight; }
        uint32_t         graphicsQueueFamily() const { return m_graphicsQueueFamily; }
        uint32_t         presentQueueFamily()  const { return m_presentQueueFamily; }
        VkFormat         swapchainFormat() const { return m_swapFormat; }
        VkExtent2D       swapchainExtent() const { return m_swapExtent; }

        // Render pass used for frame boundaries; graphics pipelines must be built against it.
        // A classic render pass replaces dynamic rendering: fill this handle plus subpass 0 instead
        // of VK_NULL_HANDLE + VkPipelineRenderingCreateInfo.
        // It depends only on the swapchain format, so a resize never invalidates it.
        VkRenderPass     renderPass() const { return m_renderPass; }

    private:
        // ---- Steps 1-5 (implemented) ----
        void createInstance();
        void setupDebugMessenger();     // optional
        void createSurface();
        void pickPhysicalDevice();
        void createLogicalDevice();

        // ---- Steps 5-7 (swapchain / sync / command buffers) ----
        void createSwapchain(VkSwapchainKHR oldSwapchain = VK_NULL_HANDLE);
        void createImageViews();
        // Creates or reuses the render pass; it only depends on m_swapFormat, so resizes keep it.
        void createRenderPass();
        // Creates one framebuffer per swapchain image (binds m_renderPass + that image view).
        void createFramebuffers();
        // Destroys a batch of framebuffers. Shared by the three destruction sites so the
        // "framebuffer before image view" order cannot be broken.
        void destroyFramebuffers(std::vector<VkFramebuffer>& fbs);
        void createCommandPool();
        void createCommandBuffers();
        void createSyncObjects();
        // Creates one renderFinished semaphore per swapchain image (see the .cpp notes)
        void createRenderFinishedSemaphores();

        void cleanupSwapchain();        // Destroys only the resources tied to the swapchain lifetime
        bool recreateSwapchain();       // Rebuilds after a size change or OUT_OF_DATE

        // Retires the current swapchain: instead of destroying it now, it goes into a deferred queue.
        // Its image views and renderFinished semaphores may still be referenced by in-flight frames,
        // so they can only be destroyed once the GPU is done.
        void retireCurrentSwapchain();
        // Called every frame: decrements framesToWait and destroys retired swapchains at zero.
        // Waiting kFramesInFlight+1 frames is a safe upper bound covering all in-flight frames plus
        // the present, so no waitIdle is needed and nothing is destroyed too early.
        void collectRetiredSwapchains();

        VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>&) const;
        VkPresentModeKHR   choosePresentMode(const std::vector<VkPresentModeKHR>&) const;
        VkExtent2D         chooseExtent(const VkSurfaceCapabilitiesKHR&) const;

        // Helpers
        bool checkValidationLayerSupport() const;
        std::vector<const char*> getRequiredExtensions() const;
        bool isDeviceSuitable(VkPhysicalDevice device) const;

        // Picks a device from the candidates by `gpu` policy; returns VK_NULL_HANDLE when the
        // Discrete policy finds none, leaving "report or retry" to the caller.
        VkPhysicalDevice choosePhysicalDevice(
            const std::vector<VkPhysicalDevice>& devices, Rhi::Gpu gpu) const;

        // Records "clear + draw" into the given command buffer (render pass open/close is closed here)
        void recordClear(VkCommandBuffer cmd, uint32_t imageIndex,
                         float r, float g, float b);

        // Client area size in logical pixels. Returns false when never set: the platform-specific
        // GetClientRect is now fed in through setClientSize().
        bool clientSize(uint32_t& w, uint32_t& h) const;

        // Dumps window state when swapchain creation fails, to locate NATIVE_WINDOW_IN_USE.
        // Platform specific, so the implementation stays in the .cpp.
        void reportSurfaceDiagnostics() const;

    private:
        static constexpr uint32_t kFramesInFlight = 2;   // CPU prepares the next frame while the GPU draws the previous one

        NativeWindow m_native{};
        Rhi::Gpu     m_gpu = Rhi::Gpu::Auto;

        // Clear alpha (0 = transparent background, 1 = opaque; see setClearAlpha).
        // Two consumers must stay in sync: createSwapchain picks compositeAlpha with it and
        // recordClear uses it as the clear color alpha.
        float        m_clearAlpha = 1.0f;

        // The swapchain's compositeAlpha is not OPAQUE, i.e. per-pixel transparency really landed.
        // Assigned together with ci.compositeAlpha in initSwapchain, read by isPerPixelTransparent().
        bool         m_perPixelTransparent = false;

        // Client area size (fed by setClientSize)
        uint32_t m_clientW = 0;
        uint32_t m_clientH = 0;

        // UI coordinate size (fed by setUiSize; 0 = follow the swapchain size).
        // Desktop keeps it at 0, so FrameInfo::uiExtent == extent and nothing changes there;
        // mobile backends fill in logical pixels.
        uint32_t m_uiW = 0;
        uint32_t m_uiH = 0;

        // Description of the selected device (filled by pickPhysicalDevice, read by deviceInfo())
        Rhi::DeviceInfo m_deviceInfo{};

        VkInstance       m_instance = VK_NULL_HANDLE;
        VkSurfaceKHR     m_surface = VK_NULL_HANDLE;
        VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
        VkDevice         m_device = VK_NULL_HANDLE;
        VkQueue          m_graphicsQueue = VK_NULL_HANDLE;
        VkQueue          m_presentQueue = VK_NULL_HANDLE;
        uint32_t         m_graphicsQueueFamily = 0;
        uint32_t         m_presentQueueFamily = 0;

        VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;

        // ---- Swapchain family ----
        VkSwapchainKHR           m_swapchain = VK_NULL_HANDLE;
        std::vector<VkImage>     m_swapImages;      // Not owned: destroyed with the swapchain
        std::vector<VkImageView> m_swapImageViews;  // Owned: must be destroyed explicitly
        VkFormat                 m_swapFormat = VK_FORMAT_UNDEFINED;
        VkExtent2D               m_swapExtent{};

        // Classic render pass declaring the frame boundaries (replaces dynamic rendering).
        // Not destroyed with the swapchain: it depends only on the format, so a resize keeps it and
        // only a real format change rebuilds it, detected through m_renderPassFormat.
        VkRenderPass             m_renderPass = VK_NULL_HANDLE;
        VkFormat                 m_renderPassFormat = VK_FORMAT_UNDEFINED;  // Format it was built with

        // One framebuffer per swapchain image, binding that image's view into the render pass.
        // Lives and dies with the swapchain since framebuffers carry a size.
        // Destruction order is hard: framebuffers must go before m_swapImageViews, otherwise
        // VUID-vkDestroyImageView-01034 fires. See cleanupSwapchain().
        std::vector<VkFramebuffer> m_swapFramebuffers;
        // Present mode used for the last swapchain (only to log a line when the mode changes)
        VkPresentModeKHR         m_presentMode = VK_PRESENT_MODE_FIFO_KHR;

        // Batch of retired swapchain resources left over from a smooth oldSwapchain rebuild.
        // In-flight frames may still use them, so they wait for collectRetiredSwapchains().
        struct RetiredSwapchain {
            VkSwapchainKHR           swapchain = VK_NULL_HANDLE;
            std::vector<VkImageView> views;            // Owned: must be destroyed explicitly
            std::vector<VkFramebuffer> framebuffers;   // Owned: must be destroyed before views
            std::vector<VkSemaphore> renderFinished;   // Owned: must be destroyed explicitly
            uint32_t                 framesToWait = 0; // Frames left before destruction is safe
        };
        std::vector<RetiredSwapchain> m_retired;

        // ---- Commands and sync ----
        VkCommandPool                m_commandPool = VK_NULL_HANDLE;
        std::vector<VkCommandBuffer> m_commandBuffers;   // One per frame (owned by the pool, not destroyed individually)
        std::vector<VkSemaphore>     m_imageAvailable;   // One per frame: acquire done, drawing may start
        std::vector<VkSemaphore>     m_renderFinished;   // One per swapchain image: drawn, present allowed
        std::vector<VkFence>         m_inFlight;         // One per frame: the CPU waits for the GPU to finish it
        uint32_t                     m_frameIndex = 0;   // Current frame slot (0 .. kFramesInFlight-1)

        // Frame callback (UI drawing hooks in; empty = clear only)
        FrameRecordFn                m_recordFn;

        const std::vector<const char*> m_validationLayers = {
            "VK_LAYER_KHRONOS_validation"
        };
        const std::vector<const char*> m_deviceExtensions = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME
        };

        // Compile-time "do we want" the validation layers, off by default and opted in explicitly.
        // Do not use NDEBUG as the debug switch: the Linux build uses -O2 without -DNDEBUG, so every
        // release binary asked for a debug layer Ubuntu does not ship and printed a warning each
        // start. Enable with -DZEKIT_WANT_VULKAN_VALIDATION or ZEKIT_VULKAN_VALIDATION=1 at runtime.
        // Kept separate from m_useValidationLayers because "wanting" and "getting" differ.
#ifdef ZEKIT_WANT_VULKAN_VALIDATION
        static constexpr bool kWantValidationLayers = true;
#else
        static constexpr bool kWantValidationLayers = false;
#endif

        // Runtime "actually enabled" flag, set by createInstance() after checking driver support.
        // A missing layer degrades with a log line instead of throwing; it also decides whether
        // getRequiredExtensions() includes VK_EXT_debug_utils, since an unconsumed extension
        // makes some loaders report "extension not present".
        bool m_useValidationLayers = false;
    };
}
