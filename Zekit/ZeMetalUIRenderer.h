#pragma once
/**
 * @file ZeMetalUIRenderer.h
 * @brief Metal UI renderer for Zekit -- counterpart of `Vk::VkUIRenderer`
 *
 * @details
 * The division of labor is exactly the same as the GL / Vk versions: consumes a `Kit::FrameCollector` (pure display list),
 * pushes vertices/indices to the GPU and draws. The widget layer, `FrameCollector`, layout, input
 * need not change a single character -- this is exactly the value of `FrameCollector` as the "de facto RHI".
 *
 * Comparison of the three backends (the Metal column is this class):
 *
 * |            | GL `UIRenderer`      | Vk `VkUIRenderer`              | Metal `MetalUIRenderer` (this class)        |
 * |------------|----------------------|--------------------------------|--------------------------------------|
 * | Frame      | None (draw in one pass) | Explicit frame acquire->draw->present | Explicit frame (driven by MetalContext) |
 * | Vertex data | `glBufferSubData`   | Avoid the frame the GPU is reading (rings) | Same as Vk: triple-buffered rings |
 * | Texture    | `glBindTexture`      | One descriptor set per texture  | One `id<MTLTexture>` + sampler per texture |
 * | Sampler    | Texture's own params | Separate `VkSampler`            | Separate `id<MTLSamplerState>`             |
 * | Projection matrix | `glUniformMatrix4fv` | push constant           | `setVertexBytes` (constant buffer)           |
 * | State object | Immediate state machine | pipeline + descriptor layout | `MTLRenderPipelineState` + `MTLSamplerState` |
 * | Destroy    | `glDelete*` freely   | Cannot delete resources of in-flight frames | Same as Vk: Metal also does not guarantee CPU deletion is safe |
 *
 * So this class is simultaneously two things (isomorphic to the Vk version):
 *  1. An implementation of `Kit::ITextureSink` -- textures for the font atlas and image cache go through here;
 *  2. The in-frame painter -- invoked by MetalContext's frame callback (encoder has already startEncoding).
 *
 * @note This class does not own MetalContext; it only borrows its device / queue.
 *       Lifetime: `Init(ctx)` must come after `ctx` is ready, `Shutdown()` must come
 *       before `ctx.cleanup()`.
 * @note The only coupling point with Context is the pixel format (`drawablePixelFormat()`) --
 *       the pipeline state must match the layer's format (`MTLPixelFormatBGRA8Unorm`).
 *       As with the Vulkan version, if the format is unchanged, size changes do not require rebuilding the pipeline.
 *
 * * This header is only included on Apple platforms and must be compiled as Objective-C++ (`.mm`).
 */

#if defined(__APPLE__)

#import <Metal/Metal.h>

#include "ZeTextureSink.h"
#include "ZeMetalContext.h"

#include <cstdint>
#include <vector>

namespace Kit { class FrameCollector; }

namespace Mtl {

    class MetalUIRenderer : public Kit::ITextureSink {
    public:
        MetalUIRenderer() = default;
        ~MetalUIRenderer() override;

        MetalUIRenderer(const MetalUIRenderer&) = delete;
        MetalUIRenderer& operator=(const MetalUIRenderer&) = delete;

        /// @brief Creates pipelines / samplers / per-frame vertex buffers
        /// @return false = creation failed (caller should fall back, not keep drawing with an empty pipeline)
        ///
        /// @note An important difference from the Vulkan version: Metal shaders are not
        ///       created at runtime from bytecode, but loaded from `default.metallib` in the app bundle
        ///       (compiled offline by `xcrun metal`, see the notes in ZeUIShader.metal).
        ///       So this function first tries `[device newDefaultLibrary]`;
        ///       if it is not in the bundle (e.g. forgot to compile during debugging), it falls back to
        ///       `newLibraryWithSource:` online compilation (slow, but keeps development from stalling).
        bool Init(MetalContext& ctx);

        /// @brief Destroys all resources (must be called before ctx.cleanup())
        void Shutdown();

        bool isReady() const { return m_pipeline != nil; }

        /// @brief Paints one UI frame. Called by MetalContext's frame callback (encoder is already encoding).
        /// @param fc    This frame's display list (widgets already filled during frame collection)
        /// @param frame Current frame info (encoder / drawable / size / frame slot)
        /// @return Number of draw calls actually submitted (0 = nothing to draw this frame)
        int Draw(const Kit::FrameCollector& fc, const MetalContext::FrameInfo& frame);

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

        /// @brief An uploaded texture
        ///
        /// * Simplification vs the Vulkan version: Vulkan needs image + deviceMemory + imageView +
        ///   descriptorSet, four pieces; Metal only needs one `id<MTLTexture>` --
        ///   memory allocation, views, and descriptor binding are all merged by Metal (see note 1 in MetalContext.h).
        ///   So this class lacks the whole descriptor pool / set layout set compared to the Vk version.
        struct Texture {
            id<MTLTexture> tex = nil;
            bool           occupied = false;   /// < false = destroyed, slot reusable
        };

        /// @brief Vertex/index buffers for one frame slot
        ///
        /// Why per-frame slots (same reason as the Vulkan version):
        /// While the GPU is still reading frame N's vertices, the CPU is already preparing frame N+1.
        /// Sharing one buffer would overwrite data being read (tearing/flicker).
        /// * Metal buffers use `MTLResourceStorageModeShared` (Apple Silicon)
        ///   or `Managed` (Intel Mac) -- see the notes in the .mm.
        struct FrameBuffers {
            id<MTLBuffer> vbo = nil;
            uint32_t      vboCap = 0;    /// < capacity (bytes)
            id<MTLBuffer> ibo = nil;
            uint32_t      iboCap = 0;    /// < capacity (bytes)
        };

        // ===============================================================
        //  Create/Destroy
        // ===============================================================
        bool createPipeline();
        bool createSampler();
        void destroyFrameBuffers(FrameBuffers& fb);

        /// @brief Loads the shader library (default.metallib first, online compilation on failure)
        id<MTLLibrary> loadLibrary();

        /// @brief Uploads one texture (shared implementation: grayscale / RGBA differ only in format and repacking)
        /// @param format        MTLPixelFormatR8Unorm or MTLPixelFormatRGBA8Unorm
        /// @param bytesPerPixel 1 or 4
        /// @param swapRedBlue   true = source is BGRA, swap to RGBA on upload
        /// @return Texture handle (0 = failure)
        uint32_t uploadTexture(int w, int h, const uint8_t* pixels, int pitch,
                               MTLPixelFormat format, int bytesPerPixel, bool swapRedBlue);

        /// @brief Handle -> texture; returns a dummy texture (1x1 white) if invalid or out of range
        id<MTLTexture> textureFor(uint32_t handle) const;

        /// @brief Acquires/releases a texture slot
        uint32_t allocSlot();
        void     releaseSlot(uint32_t handle);

        /// @brief Ensures frame slot's vertex/index buffers can hold at least n bytes
        bool ensureCapacity(FrameBuffers& fb, uint32_t vertexBytes, uint32_t indexBytes);

        /// @brief Computes the orthographic projection matrix (logical pixels -> NDC; Y up, same as D3D12)
        void buildProjection(float* out16, int uiW, int uiH) const;

    private:
        MetalContext* m_ctx = nullptr;

        // ---- Pipelines ----
        id<MTLLibrary>            m_lib = nil;
        id<MTLRenderPipelineState> m_pipeline = nil;
        id<MTLSamplerState>       m_sampler = nil;

        // ---- Vertex/index buffers (per frame slot) ----
        std::vector<FrameBuffers> m_frames;

        // ---- Texture table ----
        std::vector<Texture>  m_textures;
        std::vector<uint32_t> m_freeSlots;
        uint32_t              m_dummyTex = 0;   /// < 1x1 white: solid-color batches must also have a valid texture
        bool                  m_overflowWarned = false;

        /// @brief Texture slot limit (fixed large array instead of dynamic growth, same tradeoff as the Vk version)
        static constexpr uint32_t kMaxTextures = 8192;
    };

} // namespace Mtl

#endif // __APPLE__
