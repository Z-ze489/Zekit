#pragma once
/**
 * @file ZeVkUIRenderer.h
 * @brief Vulkan UI renderer for Zekit -- counterpart of `Kit::UIRenderer` (GL version)
 *
 * @details
 * The division of labor is identical to the GL version: consume a `Kit::FrameCollector` (pure display list),
 * push vertices/indices to the GPU and draw. The widget layer, `FrameCollector`, layout, input
 * **need not change a single character** -- this is the seam that the "de facto RHI"
 * `FrameCollector` already left open.
 *
 * But the two have a fundamental difference, which is also why this class exists:
 *
 * |            | GL version `UIRenderer`               | Vk version `VkUIRenderer`          |
 * |------------|---------------------------------------|------------------------------------|
 * | Frame      | Does not exist (`Draw` finishes in one call) | **Must explicitly split frames**: acquire->draw->present |
 * | Vertex data | `glBufferSubData` writes freely      | Must avoid the frame "the GPU is reading" (rings) |
 * | Texture    | `glBindTexture` binds freely         | One descriptor set per texture     |
 * | Sampler    | Parameters carried by the texture    | Separate `VkSampler`               |
 * | Projection matrix | `glUniformMatrix4fv`          | push constant                      |
 * | Destruction | `glDelete*` deletes freely          | Cannot delete resources used by in-flight frames |
 *
 * So this class is **both** of two things:
 *  1. An implementation of `Kit::ITextureSink` -- textures for the font atlas and image cache go through here;
 *  2. The in-frame painter -- called by the `VulkanContext` frame callback between `vkCmdBeginRenderPass`
 *     and `vkCmdEndRenderPass`.
 *
 * @note This class does **not own** VulkanContext; it only borrows its device/queue/commandPool.
 *       Lifetime: `Init(ctx)` must come after `ctx` is ready, `Shutdown()` must come
 *       before `ctx.cleanup()`.
 * @note The **only coupling point** with Context is `VulkanContext::renderPass()` --
 *       the graphics pipeline must bind to the render pass at the frame boundary (fetched once in `Init`).
 *       Because it depends only on the render pass, and the render pass depends only on the swapchain **format**,
 *       this class does **not** need to rebuild the pipeline when the swapchain is recreated due to size changes.
 */

#include "ZeTextureSink.h"
#include "ZeVulkanContext.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace Kit { class FrameCollector; }

namespace Vk {

    class VkUIRenderer : public Kit::ITextureSink {
    public:
        VkUIRenderer() = default;
        ~VkUIRenderer() override;

        VkUIRenderer(const VkUIRenderer&) = delete;
        VkUIRenderer& operator=(const VkUIRenderer&) = delete;

        /// @brief Creates pipeline / descriptors / sampler / per-frame vertex buffers
        /// @return false = creation failed (the caller should fall back to GL rather than keep drawing with an empty pipeline)
        bool Init(VulkanContext& ctx);

        /// @brief Destroys all resources (must be called before ctx.cleanup())
        void Shutdown();

        bool isReady() const { return m_pipeline != VK_NULL_HANDLE; }

        /// @brief Draws one frame of UI. Called by the VulkanContext frame callback; must be inside a render pass.
        /// @param fc    Display list for this frame (widgets already filled in during frame collection)
        /// @param frame Current frame info (command buffer / frame slot / size)
        /// @return Number of draw calls actually submitted (0 = nothing to draw this frame)
        int Draw(const Kit::FrameCollector& fc, const VulkanContext::FrameInfo& frame);

        // ---------------------------------------------------------------
        //  Kit::ITextureSink -- textures for the font atlas / image cache go through here
        // ---------------------------------------------------------------
        uint32_t CreateGray(int w, int h, const uint8_t* pixels, int pitch) override;
        uint32_t CreateRGBA(int w, int h, const uint8_t* pixels, int pitch, bool bgra) override;
        void     Destroy(uint32_t tex) override;

        /// @brief Number of currently cached textures (including free slots)
        size_t textureCount() const { return m_textures.size(); }

    private:
        // ===============================================================
        //  Resources
        // ===============================================================

        /// @brief An uploaded texture and its descriptor
        ///
        /// Why "one descriptor set per texture":
        ///   In GL, `glBindTexture` is global state, swapped freely; in Vulkan, textures must be handed to the shader
        ///   via descriptors, and `vkCmdBindDescriptorSets` is **per draw call**.
        ///   The batch loop binds once each time it encounters a new texture -- this corresponds one-to-one with GL's `glBindTexture`.
        ///   Because texture contents do not change after creation, the descriptor **is written once and stays valid forever**, reusable across frames.
        struct Texture {
            VkImage         image = VK_NULL_HANDLE;
            VkDeviceMemory  memory = VK_NULL_HANDLE;
            VkImageView     view = VK_NULL_HANDLE;
            VkDescriptorSet set = VK_NULL_HANDLE;
            bool            occupied = false;   /// < false = destroyed, slot reusable
        };

        /// @brief Vertex/index buffers for one frame slot
        ///
        /// Why slot per frame: while the GPU is still reading frame N's vertices, the CPU is already preparing frame N+1.
        /// Sharing one buffer would overwrite data being read (tearing/flicker).
        /// In GL the driver covers this for you (or you never noticed), Vulkan does not.
        struct FrameBuffers {
            VkBuffer       vbo = VK_NULL_HANDLE;
            VkDeviceMemory vboMem = VK_NULL_HANDLE;
            uint8_t*       vboMapped = nullptr;
            VkDeviceSize   vboCap = 0;

            VkBuffer       ibo = VK_NULL_HANDLE;
            VkDeviceMemory iboMem = VK_NULL_HANDLE;
            uint8_t*       iboMapped = nullptr;
            VkDeviceSize   iboCap = 0;
        };

        // ===============================================================
        //  Create/Destroy
        // ===============================================================
        bool createDescriptorSetLayout();
        bool createDescriptorPool();
        bool createPipelineLayout();
        bool createPipeline();
        bool createSampler();
        void destroyFrameBuffers(FrameBuffers& fb);

        /// @brief Uploads one texture (shared implementation: grayscale / RGBA differ only in format and repacking)
        /// @param format        VK_FORMAT_R8_UNORM or VK_FORMAT_R8G8B8A8_UNORM
        /// @param bytesPerPixel 1 or 4
        /// @param swapRedBlue   true = source is BGRA, swap to RGBA on upload
        /// @return Texture handle (0 = failure)
        uint32_t uploadTexture(int w, int h, const uint8_t* pixels, int pitch,
                               VkFormat format, int bytesPerPixel, bool swapRedBlue);

        /// @brief Handle -> descriptor set; returns the dummy texture's set when invalid or out of range
        VkDescriptorSet descriptorFor(uint32_t handle) const;

        /// @brief Acquires/releases a texture slot
        uint32_t allocSlot();
        void     releaseSlot(uint32_t handle);

        // ===============================================================
        //  Utilities
        // ===============================================================
        uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags props) const;

        /// @brief Executes a command segment immediately on the graphics queue and waits for it to finish (used for texture upload)
        void runOneShot(const std::function<void(VkCommandBuffer)>& fn);

        /// @brief Ensures the vertex/index buffers of frame slot at least fit n bytes
        bool ensureCapacity(FrameBuffers& fb, VkDeviceSize vertexBytes, VkDeviceSize indexBytes);

    private:
        VulkanContext* m_ctx = nullptr;

        // ---- Pipeline ----
        VkDescriptorSetLayout m_setLayout = VK_NULL_HANDLE;
        VkDescriptorPool      m_pool = VK_NULL_HANDLE;
        VkPipelineLayout      m_pipelineLayout = VK_NULL_HANDLE;
        VkPipeline            m_pipeline = VK_NULL_HANDLE;
        VkSampler             m_sampler = VK_NULL_HANDLE;

        // ---- Vertex/index buffers (per frame slot) ----
        std::vector<FrameBuffers> m_frames;
        uint32_t m_frameSlotCount = 0;

        // ---- Texture table ----
        std::vector<Texture>  m_textures;
        std::vector<uint32_t> m_freeSlots;
        uint32_t              m_dummyTex = 0;   /// < 1x1 white image: solid-color batches must also have a valid descriptor
        bool                  m_overflowWarned = false;

        /// @brief Descriptor pool limit (see the note in .cpp: a fixed large pool replaces dynamic growth)
        static constexpr uint32_t kMaxTextures = 8192;
    };

} // namespace Vk
