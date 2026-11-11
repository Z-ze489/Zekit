
#include "pch.h"

#include "ZeRhiBackend.h"

#include "ZeWindowX11.h"

#include "ZeVulkanContext.h"
#include "ZeEglContext.h"

#include "ZeVkUIRenderer.h"
#include "ZeUIRenderer.h"
#include "ZeGlTextureSink.h"

#include "ZeFontRenderer.h"
#include "ZeImageStore.h"
#include "ZeFrameCollector.h"

#include <iostream>
#include <chrono>

namespace Rhi {

    namespace {

        Vk::VulkanContext::NativeWindow MakeVkNativeWindow(void* nativeHandle) {
            Vk::VulkanContext::NativeWindow nw;
            nw.instance = nullptr;
            nw.display  = Plat::X11DisplayHandle();
            nw.window   = Plat::X11WindowId(nativeHandle);
            return nw;
        }

    }

    class VkX11Backend final : public IBackend {
    public:
        VkX11Backend() = default;
        ~VkX11Backend() override { Shutdown(); }

        Api  api()     const override { return Api::Vulkan; }
        bool IsReady() const override { return m_ctx.isReady() && m_uiReady; }

        bool IsPerPixelTransparent() const override { return m_ctx.isPerPixelTransparent(); }

        void SetGpu(Gpu gpu) override { m_gpu = gpu; }
        DeviceInfo deviceInfo() const override { return m_ctx.deviceInfo(); }

        void SetClearAlpha(float alpha) override {
            m_clearAlpha = (alpha <= 0.0f) ? 0.0f : 1.0f;
            m_ctx.setClearAlpha(m_clearAlpha);
        }

        bool Preheat(void* nativeHandle) override {
            if (!nativeHandle) return false;
            if (m_ctx.device() != VK_NULL_HANDLE) return true;

            return m_ctx.preheat(MakeVkNativeWindow(nativeHandle), m_gpu);
        }

        bool Init(void* nativeHandle, int width, int height) override {
            if (!nativeHandle) return false;
            if (IsReady()) return true;

            const bool trace = Plat::TraceLifecycle();

            try {
                m_ctx.setClearAlpha(m_clearAlpha);
                m_ctx.init(MakeVkNativeWindow(nativeHandle), m_gpu);
                m_ctx.setClientSize(width, height);
            } catch (const std::exception& e) {
                std::cerr << "[Zekit] Vulkan init failed: " << e.what() << std::endl;
                m_ctx.cleanup();
                return false;
            }

            const auto tCtx = std::chrono::steady_clock::now();
            if (!m_ui.Init(m_ctx)) {
                m_ctx.cleanup();
                return false;
            }
            if (trace)
                std::cerr << "[Zekit/trace] ⑤ UI 渲染器（着色器/管线）"
                << std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - tCtx).count() << "ms\n";

            m_ctx.setFrameRecorder([this](const Vk::VulkanContext::FrameInfo& fi) {
                m_ui.Draw(*m_pendingFrame, fi);
            });
            m_uiReady = true;
            return true;
        }

        void Shutdown() override {
            if (!m_ctx.hasAnyResource() && !m_uiReady) return;
            m_ctx.setFrameRecorder(nullptr);
            if (m_uiReady) {
                m_ui.Shutdown();
                m_uiReady = false;
            }
            m_ctx.cleanup();
            m_pendingFrame = nullptr;
        }

        bool Resize(int width, int height) override {
            m_ctx.setClientSize(width, height);
            return IsReady();
        }

        bool DrawFrame(const Kit::FrameCollector& fc,
                       int width, int height,
                       float r, float g, float b) override {
            (void)width; (void)height;
            if (!IsReady()) return false;
            m_pendingFrame = &fc;
            const bool ok = m_ctx.drawFrame(r, g, b);
            m_pendingFrame = nullptr;
            return ok;
        }

        void AttachTextureSinks(void* fontRenderer, void* imageStore) override {
            if (fontRenderer)
                static_cast<Kit::FontRenderer*>(fontRenderer)->SetTextureSink(&m_ui);
            if (imageStore)
                static_cast<Kit::ImageStore*>(imageStore)->SetTextureSink(&m_ui);
        }

        void DetachTextureSinks(void* fontRenderer, void* imageStore) override {
            if (fontRenderer) {
                Kit::FontRenderer* f = static_cast<Kit::FontRenderer*>(fontRenderer);
                f->DropAllGlyphTextures();
                f->SetTextureSink(nullptr);
            }
            if (imageStore) {
                Kit::ImageStore* s = static_cast<Kit::ImageStore*>(imageStore);
                s->Shutdown();
                s->SetTextureSink(nullptr);
            }
        }

    private:
        Vk::VulkanContext  m_ctx;
        Vk::VkUIRenderer   m_ui;
        Rhi::Gpu           m_gpu = Rhi::Gpu::Auto;
        float              m_clearAlpha = 1.0f;
        bool m_uiReady = false;
        const Kit::FrameCollector* m_pendingFrame = nullptr;
    };

    class EglGlBackend final : public IBackend {
    public:
        EglGlBackend() = default;
        ~EglGlBackend() override { Shutdown(); }

        Api  api()     const override { return Api::OpenGL; }
        bool IsReady() const override { return m_gl.IsValid() && m_uiReady; }

        void SetGpu(Gpu gpu) override { m_gpu = gpu; }
        DeviceInfo deviceInfo() const override { return m_gl.deviceInfo(); }

        bool Init(void* nativeHandle, int width, int height) override {
            if (!nativeHandle) return false;
            if (IsReady()) { Resize(width, height); return true; }

            if (!m_gl.Create(Plat::X11DisplayHandle(),
                             Plat::X11WindowId(nativeHandle),
                              4,  6,  true,
                             m_gpu,
                             Plat::X11VisualId(nativeHandle))) {
                return false;
            }

            m_gl.MakeCurrent();
            if (!m_ui.Init()) {
                m_gl.DoneCurrent();
                m_gl.Release();
                return false;
            }
            m_uiReady = true;
            m_w = width;
            m_h = height;
            return true;
        }

        void Shutdown() override {
            if (!m_gl.IsValid()) {
                m_uiReady = false;
                return;
            }
            m_gl.MakeCurrent();
            if (m_uiReady) {
                m_ui.Shutdown();
                m_uiReady = false;
            }
            m_gl.DoneCurrent();
            m_gl.Release();
        }

        bool Resize(int width, int height) override {
            m_w = width;
            m_h = height;
            return IsReady();
        }

        bool DrawFrame(const Kit::FrameCollector& fc,
                       int width, int height,
                       float r, float g, float b) override {
            if (!IsReady()) return false;
            if (width <= 0 || height <= 0) return false;

            const bool sizeChanged = (width != m_w || height != m_h);
            m_w = width;
            m_h = height;

            m_gl.SetSwapInterval(sizeChanged ? 0 : 1);

            m_gl.MakeCurrent();
            glViewport(0, 0, m_w, m_h);
            glClearColor(r, g, b, m_clearAlpha);
            glClear(GL_COLOR_BUFFER_BIT);

            m_ui.Draw(fc, m_w, m_h);

            m_gl.SwapBuffers();
            return true;
        }

        void AttachTextureSinks(void* fontRenderer, void* imageStore) override {
            if (fontRenderer)
                static_cast<Kit::FontRenderer*>(fontRenderer)->SetTextureSink(&m_sink);
            if (imageStore)
                static_cast<Kit::ImageStore*>(imageStore)->SetTextureSink(&m_sink);
        }

        void DetachTextureSinks(void* fontRenderer, void* imageStore) override {
            if (fontRenderer) {
                Kit::FontRenderer* f = static_cast<Kit::FontRenderer*>(fontRenderer);
                f->DropAllGlyphTextures();
                f->SetTextureSink(nullptr);
            }
            if (imageStore) {
                Kit::ImageStore* s = static_cast<Kit::ImageStore*>(imageStore);
                s->Shutdown();
                s->SetTextureSink(nullptr);
            }
        }

        bool CanShareContext() const override { return true; }

        IBackend* CreateShared(void* nativeHandle, int width, int height, float clearAlpha) override {
            if (!m_gl.IsValid()) return nullptr;
            auto* child = new EglGlBackend();
            child->m_clearAlpha = clearAlpha;
            if (!child->InitShared(nativeHandle, width, height, m_gl)) {
                delete child;
                return nullptr;
            }
            return child;
        }

    private:
        bool InitShared(void* nativeHandle, int width, int height,
                        const GL::EglContext& share) {
            if (!nativeHandle) return false;
            if (!m_gl.CreateShared(Plat::X11DisplayHandle(),
                                   Plat::X11WindowId(nativeHandle),
                                   share,  4,  6,  true)) {
                return false;
            }

            m_gl.MakeCurrent();
            if (!m_ui.Init()) {
                m_gl.DoneCurrent();
                m_gl.Release();
                return false;
            }
            m_uiReady = true;
            m_w = width;
            m_h = height;
            return true;
        }

        GL::EglContext   m_gl;
        Kit::UIRenderer  m_ui;
        GlTextureSink    m_sink;
        Rhi::Gpu         m_gpu = Rhi::Gpu::Auto;
        bool             m_uiReady = false;
        int              m_w = 0;
        int              m_h = 0;
        float            m_clearAlpha = 1.0f;

    public:
        void SetClearAlpha(float alpha) override {
            m_clearAlpha = (alpha <= 0.0f) ? 0.0f : 1.0f;
        }
    };

    IBackend* CreateBackend(Api api) {
        switch (api) {
        case Api::Vulkan: return new VkX11Backend();
        case Api::OpenGL: return new EglGlBackend();
        case Api::External: return nullptr;
        case Api::Software: return CreateSoftwareBackend();
        }
        return nullptr;
    }

    void DestroyBackend(IBackend* backend) {
        delete backend;
    }

}
