
#include "pch.h"

#include "ZeExternalBackend.h"
#include "ZeFrameCollector.h"
#include "ZeFontRenderer.h"
#include "ZeImageStore.h"

#include <cstddef>

namespace Rhi {

    namespace {
        struct ReentryGuard {
            explicit ReentryGuard(bool& f) : flag(f) { flag = true; }
            ~ReentryGuard() { flag = false; }
            ReentryGuard(const ReentryGuard&) = delete;
            ReentryGuard& operator=(const ReentryGuard&) = delete;
            bool& flag;
        };
    }

    static_assert(sizeof(Kit::CanvasVertex) == sizeof(Kit::UIVertex),
        "CanvasVertex 与 UIVertex 必须同尺寸（公开接口是零拷贝视图，不能错位）");
    static_assert(offsetof(Kit::CanvasVertex, x) == offsetof(Kit::UIVertex, x), "布局不一致：x");
    static_assert(offsetof(Kit::CanvasVertex, u) == offsetof(Kit::UIVertex, u), "布局不一致：u");
    static_assert(offsetof(Kit::CanvasVertex, cx) == offsetof(Kit::UIVertex, cx), "布局不一致：cx");
    static_assert(offsetof(Kit::CanvasVertex, rad) == offsetof(Kit::UIVertex, rad), "布局不一致：rad");

    std::uint32_t ExternalBackend::Sink::CreateGray(int w, int h,
                                                    const std::uint8_t* pixels, int pitch) {
        if (!m_r || w <= 0 || h <= 0 || !pixels) return 0;
        return m_r->CreateGray(w, h, pixels, pitch);
    }

    std::uint32_t ExternalBackend::Sink::CreateRGBA(int w, int h,
                                                    const std::uint8_t* pixels,
                                                    int pitch, bool bgra) {
        if (!m_r || w <= 0 || h <= 0 || !pixels) return 0;
        return m_r->CreateRGBA(w, h, pixels, pitch, bgra);
    }

    void ExternalBackend::Sink::Destroy(std::uint32_t tex) {
        if (!m_r || tex == 0) return;
        m_r->DestroyTexture(tex);
    }

    ExternalBackend::ExternalBackend(std::shared_ptr<Kit::ICanvasRenderer> renderer)
        : m_renderer(std::move(renderer))
        , m_sink(m_renderer.get()) {
    }

    ExternalBackend::~ExternalBackend() {
        Shutdown();
    }

    bool ExternalBackend::Init(void* nativeHandle, int width, int height) {
        if (!m_renderer) return false;

        if (m_attaching) return m_ready;
        const ReentryGuard guard(m_attaching);

        if (m_ready) {
            return m_renderer->Resize(width, height);
        }
        if (!nativeHandle || width <= 0 || height <= 0) return false;

        m_ready = m_renderer->Attach(nativeHandle, width, height);
        if (m_ready) { m_lastW = width; m_lastH = height; }
        return m_ready;
    }

    void ExternalBackend::Shutdown() {
        if (!m_renderer) {
            m_ready = false;
            return;
        }
        if (m_ready) m_renderer->Detach();
        m_ready = false;
    }

    bool ExternalBackend::Resize(int width, int height) {
        if (!m_renderer || !m_ready) return false;
        if (width <= 0 || height <= 0) return false;
        return m_renderer->Resize(width, height);
    }

    bool ExternalBackend::DrawFrame(const Kit::FrameCollector& fc,
                                    int width, int height,
                                    float r, float g, float b) {
        if (m_ready && (width != m_lastW || height != m_lastH)) {
            m_lastW = width;
            m_lastH = height;
            if (!m_renderer->Resize(width, height)) {
                m_lastW = 0;
                m_lastH = 0;
                return false;
            }
        }

        Kit::CanvasFrame frame;
        frame.vertices    = reinterpret_cast<const Kit::CanvasVertex*>(fc.Vertices().data());
        frame.vertexCount = fc.Vertices().size();
        frame.indices     = fc.Indices().data();
        frame.indexCount  = fc.Indices().size();

        m_batches.clear();
        m_batches.reserve(fc.Batches().size());
        for (const Kit::FrameCollector::Batch& b : fc.Batches()) {
            Kit::CanvasBatch cb;
            cb.texture     = b.texture;
            cb.indexOffset = b.indexOffset;
            cb.indexCount  = b.indexCount;
            cb.isColor     = b.isColor;
            m_batches.push_back(cb);
        }
        frame.batches    = m_batches.data();
        frame.batchCount = m_batches.size();

        frame.width  = width;
        frame.height = height;
        frame.clear  = Kit::Color4f{ r, g, b, 1.0f };

        if (m_renderer) m_renderer->DrawFrame(frame);
        return m_ready;
    }

    void ExternalBackend::AttachTextureSinks(void* fontRenderer, void* imageStore) {
        if (auto* f = static_cast<Kit::FontRenderer*>(fontRenderer))
            f->SetTextureSink(&m_sink);
        if (auto* s = static_cast<Kit::ImageStore*>(imageStore))
            s->SetTextureSink(&m_sink);
    }

    void ExternalBackend::DetachTextureSinks(void* fontRenderer, void* imageStore) {
        if (auto* f = static_cast<Kit::FontRenderer*>(fontRenderer)) {
            f->DropAllGlyphTextures();
            f->SetTextureSink(nullptr);
        }
        if (auto* s = static_cast<Kit::ImageStore*>(imageStore)) {
            s->Shutdown();
            s->SetTextureSink(nullptr);
        }
    }

}
