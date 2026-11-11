#pragma once
/**
 * @file ZeD3D12UIRenderer.h
 * @brief D3D12 UI renderer for Zekit -- counterpart of `Vk::VkUIRenderer` / `Kit::UIRenderer`
 *
 * @details
 * The third of the three backends. Division of labor is identical to the other two: consumes a `Kit::FrameCollector` (pure display list),
 * pushes vertices/indices to the GPU and draws. Widget layer, `FrameCollector`, layout, input need **not a single character changed**.
 *
 * Comparison of the three (how the same thing is expressed in three APIs):
 *
 * |            | GL `UIRenderer`      | Vk `VkUIRenderer`              | D3D12 `D3D12UIRenderer`              |
 * |------------|----------------------|--------------------------------|--------------------------------------|
 * | Frame      | none (draw in one go) | acquire->draw->present         | driven by `D3D12Context::drawFrame`  |
 * | Projection matrix | `glUniformMatrix4fv` | push constant           | **root constants** (32-bit constants) |
 * | Vertex data | `glBufferSubData`   | per-frame slot mapped VBO/IBO   | per-frame slot upload-heap ring buffer |
 * | Texture    | `glBindTexture`      | one descriptor set per texture  | one **SRV** per texture (shader-visible heap) |
 * | Sampler    | parameters on texture | separate `VkSampler`           | separate **sampler descriptor**      |
 * | Destruction | `glDelete*` freely  | cannot delete in-flight frames  | same as Vk: only non-in-flight frames can be deleted |
 *
 * This class is **both** of the following:
 *  1. An implementation of `Kit::ITextureSink` -- textures for the font atlas and image cache go through here;
 *  2. The in-frame painter -- called by the `D3D12Context` frame callback between `ClearRenderTargetView`
 *     and `EndRenderPass`.
 *
 * @note This class does **not own** `D3D12Context`, only borrows its device/queue/allocator.
 *       Lifetime: `Init(ctx)` after `ctx` is ready, `Shutdown()` before `ctx.cleanup()`.
 */

#include "ZeTextureSink.h"
#include "ZeD3D12Context.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace Kit { class FrameCollector; }

namespace Dx {

    class D3D12UIRenderer : public Kit::ITextureSink {
    public:
        D3D12UIRenderer() = default;
        ~D3D12UIRenderer() override;

        D3D12UIRenderer(const D3D12UIRenderer&) = delete;
        D3D12UIRenderer& operator=(const D3D12UIRenderer&) = delete;

        /// @brief Creates root signature / PSO / sampler / descriptor heaps / per-frame upload buffers
        /// @return false = creation failed (caller should fall back to GL/Vk instead of drawing with an empty pipeline)
        bool Init(D3D12Context& ctx);

        /// @brief Destroys all resources (must be called before ctx.cleanup())
        void Shutdown();

        bool isReady() const { return m_pipeline != nullptr; }

        /// @brief Paints one UI frame. Called by the D3D12Context frame callback, must be inside a render pass.
        /// @param fc    display list of this frame (widgets already filled during frame collection)
        /// @param frame current frame info (command list / frame slot / size / target format)
        /// @return number of draw calls actually submitted (0 = nothing to draw this frame)
        int Draw(const Kit::FrameCollector& fc, const D3D12Context::FrameInfo& frame);

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

        /// @brief One uploaded texture and its SRV / sampler descriptor slots
        ///
        /// Why "one SRV slot per texture": in D3D12 a texture must be handed to the shader via a descriptor,
        /// and `SetGraphicsRootDescriptorTable` is **per draw call**.
        /// The batch loop binds once each time it encounters a new texture -- one-to-one with GL's `glBindTexture`
        /// and Vk's `vkCmdBindDescriptorSets`.
        /// Texture content does not change after creation, the SRV **is written once and stays valid forever**, reusable across frames.
        struct Texture {
            Microsoft::WRL::ComPtr<ID3D12Resource>     resource;
            D3D12_CPU_DESCRIPTOR_HANDLE                srvCpu{};   /// < read-only view does not need a CPU handle, kept for reference
            UINT                                       srvSlot = 0;/// < SRV slot in the shader-visible heap
            bool                                       occupied = false; /// < false = destroyed, reusable
        };

        /// @brief **Upload buffer** of one frame slot (upload heap, CPU can write directly)
        ///
        /// Why one slot per frame: while the GPU is still reading frame N's vertices, the CPU is already preparing frame N+1.
        /// Sharing one buffer would overwrite data being read (tearing/flicker).
        ///
        /// Difference from Vk: Vulkan uses HOST_VISIBLE|HOST_COHERENT memory + `memcpy`;
        /// D3D12 uses a `D3D12_HEAP_TYPE_UPLOAD` resource, **Map once to get a long-lived pointer**,
        /// then each frame just `memcpy` to the corresponding offset (do not Map/Unmap every frame).
        struct FrameBuffers {
            Microsoft::WRL::ComPtr<ID3D12Resource> vbo;
            uint8_t*       vboMapped = nullptr;
            D3D12_GPU_VIRTUAL_ADDRESS vboGpu = 0;
            UINT64         vboCap = 0;

            Microsoft::WRL::ComPtr<ID3D12Resource> ibo;
            uint8_t*       iboMapped = nullptr;
            D3D12_GPU_VIRTUAL_ADDRESS iboGpu = 0;
            UINT64         iboCap = 0;
        };

        // ===============================================================
        //  Creation/Destruction
        // ===============================================================
        bool createRootSignature();
        bool createPipeline(DXGI_FORMAT rtvFormat);
        bool createSampler();
        bool createDescriptorHeap();
        void createFrameBuffers();
        void destroyFrameBuffers(FrameBuffers& fb);

        /// @brief Uploads one texture (shared implementation: grayscale / RGBA differ only in format and repacking)
        /// @param srcFormat  DXGI_FORMAT_R8_UNORM or DXGI_FORMAT_R8G8B8A8_UNORM
        /// @param bytesPerPixel 1 or 4
        /// @param swapRedBlue  true = source is BGRA, swap to RGBA on upload
        /// @return texture handle (0 = failure)
        uint32_t uploadTexture(int w, int h, const uint8_t* pixels, int pitch,
                               DXGI_FORMAT srcFormat, int bytesPerPixel, bool swapRedBlue);

        /// @brief Handle -> SRV slot; returns the dummy texture's slot when invalid or out of range
        UINT srvSlotFor(uint32_t handle) const;

        uint32_t allocSlot();
        void     releaseSlot(uint32_t handle);

        // ===============================================================
        //  Utilities
        // ===============================================================
        // @brief Executes a command segment immediately on the command queue and waits for it to complete (used for texture upload)
        void runOneShot(const std::function<void(ID3D12GraphicsCommandList*)>& fn);

        /// @brief Ensures the vertex/index buffer of frame slot slot can hold at least n bytes
        bool ensureCapacity(FrameBuffers& fb, UINT64 vertexBytes, UINT64 indexBytes);

        /// @brief Creates an upload-heap buffer and Maps it long-term
        static bool createUploadBuffer(ID3D12Device* device, UINT64 bytes,
                                       ID3D12Resource** out,
                                       uint8_t** outMapped,
                                       D3D12_GPU_VIRTUAL_ADDRESS* outGpu);

    private:
        D3D12Context* m_ctx = nullptr;

        // ---- Pipeline ----
        Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rootSignature;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pipeline;

        // ---- Descriptor heaps (SRV + sampler) ----
        // One heap holding both SRV and sampler: D3D12 allows it (as long as allocation advances by each one's size),
        // but the clearer approach is two heaps. Here **two heaps** are used: SRV heap (shader-visible) +
        // sampler heap, each occupying one table in the root signature.
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_srvHeap;
        UINT m_srvDescriptorSize = 0;
        UINT m_srvNextSlot = 0;          /// < next free SRV slot (simple linear allocation)
        std::vector<UINT> m_freeSrvSlots;/// < slots reclaimed by Destroy

        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_samplerHeap;
        UINT m_samplerDescriptorSize = 0;
        bool m_samplerWritten = false;

        // ---- Vertex/index buffers (per frame slot) ----
        std::vector<FrameBuffers> m_frames;

        // ---- Texture table ----
        std::vector<Texture>  m_textures;
        uint32_t              m_dummyTex = 0;   /// < 1x1 white image: solid-color batches also need a valid SRV
        bool                  m_overflowWarned = false;

        /// @brief SRV heap limit (fixed large heap instead of dynamic growth)
        static constexpr UINT kMaxTextures = 8192;
    };

} // namespace Dx
