#pragma once
/**
 * @file ZeExternalBackend.h
 * @brief `Rhi::ExternalBackend` -- wraps a **developer-provided renderer** into a standard backend
 *
 * @details
 * ## Where it sits in the pipeline
 *
 * ```
 *   Kit::Window::Impl
 *        └─ Rhi::IBackend ─┬─ GLBackend      (in-library)
 *                          ├─ VulkanBackend  (in-library)
 *                          ├─ D3D12Backend   (in-library)
 *                          └─ ExternalBackend <- **this file**: forwards to Kit::ICanvasRenderer
 * ```
 *
 * Once this backend is installed, Kit's **entire pipeline runs unchanged**:
 * theme injection -> layout -> fragment cache -> frame collection (including `ZeFontRenderer` glyph layout,
 * `ZeImageStore` image decoding, CPU-side clipping and batching) -> this backend -> your renderer.
 *
 * In other words, "the features a widget should have" are not supplied by this file; they work **because it is
 * a real `Kit::Window`** -- this file only handles the last step, "who receives the triangles".
 *
 * ## Why textures go through `ITextureSink`
 *
 * In the GL backend, glyph and image handles are `GLuint`; in the Vulkan backend they are indices --
 * here it is **the same**: `Attach` installs a forwarding sink, and `CreateGray` / `CreateRGBA`
 * land directly on your `ICanvasRenderer::CreateGray` / `CreateRGBA`.
 * `ZeFontRenderer` / `ZeImageStore` / `ZeFreeTypeWrapper` have no idea
 * that a different graphics stack sits behind them (this is the same mechanism used by the Vulkan / D3D12 backends).
 *
 * ## Difference from `SetRenderEnabled(false)` (easy to confuse)
 *
 * | Usage | Does Kit paint | Can the canvas hold widgets |
 * |---|---|---|
 * | `SetRenderEnabled(false)` | **No** (not even a backend is created), the whole HWND is yours | ✗ |
 * | `UseExternalRenderer(r)` | **Paints as usual**, only the last step is handed to you | ✓ |
 *
 * @note This file is an **internal header**: `Kit::ICanvasRenderer` is public (`include/ZeCanvasRenderer.h`),
 *       but "how to wire it into the backend" is a library implementation detail.
 */

#include "ZeRhiBackend.h"          // Rhi::IBackend (in-library interface)
#include "ZeTextureSink.h"         // Kit::ITextureSink (texture redirection)
#include "include/ZeCanvasRenderer.h"   // * Public interface (full path written out, meaning "this is exactly the one I want")

#include <memory>
#include <vector>

namespace Rhi {

    /**
     * @brief Treats `Kit::ICanvasRenderer` as a render backend (forwarding layer, zero policy)
     *
     * @details
     * Lifecycle mapping:
     *
     * | `IBackend`      | `ICanvasRenderer` |
     * |---|---|
     * | `Init(hwnd,w,h)` | `Attach(hwnd,w,h)` |
     * | `Resize(w,h)`    | `Resize(w,h)` |
     * | `DrawFrame(fc)`  | `DrawFrame(CanvasFrame)` |
     * | `Shutdown()`     | `Detach()` |
     * | `AttachTextureSinks` | installs a sink that forwards the three operations to the renderer |
     *
     * @note No fallback is performed ("switch to GL if the renderer breaks" is meaningless here -- the
     *       context on that HWND is **yours**). If `Init` fails, nothing is painted this frame and it retries next frame.
     * @note `api()` is always `Api::External`; `deviceInfo()` is always invalid (the library does not know which GPU you run on).
     */
    class ExternalBackend final : public IBackend {
    public:
        /// @brief Binds the renderer (`renderer` must not be null; ownership stays outside this backend)
        explicit ExternalBackend(std::shared_ptr<Kit::ICanvasRenderer> renderer);
        ~ExternalBackend() override;

        ExternalBackend(const ExternalBackend&) = delete;
        ExternalBackend& operator=(const ExternalBackend&) = delete;

        // ---- Queries ----
        Api  api() const override { return Api::External; }
        bool IsReady() const override { return m_ready; }

        // ---- Lifecycle ----
        bool Init(void* nativeHandle, int width, int height) override;
        void Shutdown() override;
        bool Resize(int width, int height) override;

        // ---- Paint one frame ----
        bool DrawFrame(const Kit::FrameCollector& fc,
                       int width, int height,
                       float r, float g, float b) override;

        // ---- Texture redirection ----
        void AttachTextureSinks(void* fontRenderer, void* imageStore) override;
        void DetachTextureSinks(void* fontRenderer, void* imageStore) override;

        /// @brief The bound renderer (for diagnostics / upper-layer verification)
        Kit::ICanvasRenderer* renderer() const { return m_renderer.get(); }

    private:
        /// @brief Forwards the three entry points of `Kit::ITextureSink` to the renderer
        /// @note Made a member rather than a temporary object: `FontRenderer` / `ImageStore` store **raw pointers**,
        ///       so its lifetime must extend past `DetachTextureSinks`.
        class Sink final : public Kit::ITextureSink {
        public:
            explicit Sink(Kit::ICanvasRenderer* r) : m_r(r) {}
            std::uint32_t CreateGray(int w, int h, const std::uint8_t* pixels, int pitch) override;
            std::uint32_t CreateRGBA(int w, int h, const std::uint8_t* pixels,
                                     int pitch, bool bgra) override;
            void Destroy(std::uint32_t tex) override;

        private:
            Kit::ICanvasRenderer* m_r = nullptr;
        };

        std::shared_ptr<Kit::ICanvasRenderer> m_renderer;
        Sink  m_sink;
        bool  m_ready = false;

        /// @brief "Attach is in progress" (recursion guard, see the explanation in `Init`)
        bool  m_attaching = false;

        /// @brief Client-area size last reported to the renderer (used to turn "size changed" into `Resize`)
        /// @note 0 means "must notify once on the next frame" (deliberately cleared on rebuild failure to force a retry).
        int   m_lastW = 0;
        int   m_lastH = 0;

        /// @brief Reusable buffer for batch conversion (only a few push_back per frame, no allocation in steady state)
        std::vector<Kit::CanvasBatch> m_batches;
    };

} // namespace Rhi
