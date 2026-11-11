
#include "pch.h"

#include <Windows.h>

#include "ZeRhiBackend.h"

#include "ZeGLContext.h"
#include "ZeGlTextureSink.h"
#include "ZeUIRenderer.h"
#include "ZeVulkanContext.h"
#include "ZeVkUIRenderer.h"
#include "ZeD3D12Context.h"
#include "ZeD3D12UIRenderer.h"

#include "ZeFontRenderer.h"
#include "ZeImageStore.h"
#include "ZeFrameCollector.h"
#include "ZeTextureSink.h"

#include <iostream>

namespace Rhi {

    class GLBackend final : public IBackend {
    public:
        GLBackend() = default;
        ~GLBackend() override { Shutdown(); }

        Api  api()     const override { return Api::OpenGL; }
        bool IsReady() const override { return m_gl.IsValid() && m_uiReady; }

        bool Init(void* nativeHandle, int width, int height) override {
            return InitImpl(nativeHandle, width, height, nullptr);
        }

        bool CanShareContext() const override { return true; }

        IBackend* CreateShared(void* nativeHandle, int width, int height, float clearAlpha) override {
            if (!m_gl.IsValid()) return nullptr;
            auto* child = new GLBackend();
            child->m_clearAlpha = clearAlpha;
            if (!child->InitImpl(nativeHandle, width, height, &m_gl)) {
                delete child;
                return nullptr;
            }
            return child;
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

            m_gl.SetSwapInterval(sizeChanged ? 1 : m_swapInterval);

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

    private:
        bool InitImpl(void* nativeHandle, int width, int height, const GL::GLContext* shareFrom) {
            if (!nativeHandle) return false;
            if (IsReady()) { Resize(width, height); return true; }

            const bool ctxOk = shareFrom
                ? m_gl.CreateShared(static_cast<HWND>(nativeHandle), *shareFrom, 4, 6,  true)
                : m_gl.Create(static_cast<HWND>(nativeHandle), 4, 6,  true);
            if (!ctxOk) return false;

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

        GL::GLContext   m_gl;
        Kit::UIRenderer m_ui;
        GlTextureSink   m_sink;
        bool m_uiReady = false;
        int  m_w = 0;
        int  m_h = 0;

        int  m_swapInterval = 1;

        float m_clearAlpha = 1.0f;

    public:
        void SetSwapInterval(int interval) override {
            m_swapInterval = (interval <= 0) ? 0 : 1;
        }
        void SetClearAlpha(float alpha) override {
            m_clearAlpha = (alpha <= 0.0f) ? 0.0f : 1.0f;
        }
    };

    class VulkanBackend final : public IBackend {
    public:
        VulkanBackend() = default;
        ~VulkanBackend() override { Shutdown(); }

        Api  api()     const override { return Api::Vulkan; }
        bool IsReady() const override { return m_ctx.isReady() && m_uiReady; }

        void SetGpu(Gpu gpu) override { m_gpu = gpu; }
        DeviceInfo deviceInfo() const override { return m_ctx.deviceInfo(); }

        void SetClearAlpha(float alpha) override {
            m_clearAlpha = (alpha <= 0.0f) ? 0.0f : 1.0f;
            m_ctx.setClearAlpha(m_clearAlpha);
        }

        bool Init(void* nativeHandle, int width, int height) override {
            if (!nativeHandle) return false;
            if (IsReady()) return true;

            try {
                Vk::VulkanContext::NativeWindow nw;
                nw.instance = GetModuleHandle(nullptr);
                nw.window   = reinterpret_cast<unsigned long long>(nativeHandle);
                m_ctx.setClearAlpha(m_clearAlpha);
                m_ctx.init(nw, m_gpu);
                m_ctx.setClientSize(width, height);
            } catch (const std::exception& e) {
                std::cerr << "[Zekit] Vulkan init failed: " << e.what() << std::endl;
                OutputDebugStringA("[Zekit] Vulkan init failed: ");
                OutputDebugStringA(e.what());
                OutputDebugStringA("\n");
                m_ctx.cleanup();
                return false;
            }

            if (!m_ui.Init(m_ctx)) {
                m_ctx.cleanup();
                return false;
            }

            m_ctx.setFrameRecorder([this](const Vk::VulkanContext::FrameInfo& fi) {
                m_ui.Draw(*m_pendingFrame, fi);
            });
            m_uiReady = true;
            return true;
        }

        void Shutdown() override {
            if (!m_ctx.isReady() && !m_uiReady) return;
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

    class D3D12Backend final : public IBackend {
    public:
        D3D12Backend() = default;
        ~D3D12Backend() override { Shutdown(); }

        Api  api()     const override { return Api::D3D12; }
        bool IsReady() const override { return m_ctx.isReady() && m_uiReady; }

        bool IsPerPixelTransparent() const override { return m_ctx.isPerPixelTransparent(); }

        void SetGpu(Gpu gpu) override { m_gpu = gpu; }
        Rhi::DeviceInfo deviceInfo() const override { return m_ctx.deviceInfo(); }

        void SetClearAlpha(float alpha) override {
            m_clearAlpha = (alpha <= 0.0f) ? 0.0f : 1.0f;
            m_ctx.setClearAlpha(m_clearAlpha);
        }

        bool CanShareContext() const override { return true; }

        IBackend* CreateShared(void* nativeHandle, int width, int height, float clearAlpha) override {
            (void)width; (void)height;
            if (!nativeHandle || !m_ctx.isReady()) return nullptr;

            auto* child = new D3D12Backend();
            child->m_gpu = m_gpu;
            child->m_clearAlpha = clearAlpha;
            if (!child->initShared(nativeHandle, m_ctx)) {
                delete child;
                return nullptr;
            }
            return child;
        }

        bool Init(void* nativeHandle, int width, int height) override {
            (void)width; (void)height;
            if (!nativeHandle) return false;
            if (IsReady()) return true;

            try {
                m_ctx.setClearAlpha(m_clearAlpha);
                m_ctx.init(GetModuleHandle(nullptr), static_cast<HWND>(nativeHandle), m_gpu);
            } catch (const std::exception& e) {
                std::cerr << "[Zekit] D3D12 init failed: " << e.what() << std::endl;
                OutputDebugStringA("[Zekit] D3D12 init failed: ");
                OutputDebugStringA(e.what());
                OutputDebugStringA("\n");
                m_ctx.cleanup();
                return false;
            }
            if (!m_ctx.isReady()) {
                m_ctx.cleanup();
                return false;
            }

            if (!m_ui.Init(m_ctx)) {
                m_ctx.cleanup();
                return false;
            }

            m_ctx.setFrameRecorder([this](const Dx::D3D12Context::FrameInfo& fi) {
                m_ui.Draw(*m_pendingFrame, fi);
            });
            m_uiReady = true;
            return true;
        }

        void Shutdown() override {
            if (!m_ctx.isReady() && !m_uiReady) return;
            m_ctx.setFrameRecorder(nullptr);
            if (m_uiReady) {
                m_ui.Shutdown();
                m_uiReady = false;
            }
            m_ctx.cleanup();
            m_pendingFrame = nullptr;
        }

        bool Resize(int width, int height) override {
            (void)width; (void)height;
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
        bool initShared(void* nativeHandle, const Dx::D3D12Context& share) {
            m_ctx.setClearAlpha(m_clearAlpha);
            if (!m_ctx.initShared(GetModuleHandle(nullptr),
                                  static_cast<HWND>(nativeHandle), share))
                return false;

            if (!m_ui.Init(m_ctx)) {
                m_ctx.cleanup();
                return false;
            }

            m_ctx.setFrameRecorder([this](const Dx::D3D12Context::FrameInfo& fi) {
                m_ui.Draw(*m_pendingFrame, fi);
            });
            m_uiReady = true;
            return true;
        }

        Dx::D3D12Context     m_ctx;
        Dx::D3D12UIRenderer  m_ui;
        Rhi::Gpu             m_gpu = Rhi::Gpu::Auto;
        float                m_clearAlpha = 1.0f;
        bool m_uiReady = false;
        const Kit::FrameCollector* m_pendingFrame = nullptr;

        int  m_swapInterval = 1;

    public:
        void SetSwapInterval(int interval) override {
            m_swapInterval = (interval <= 0) ? 0 : 1;
            m_ctx.setSwapInterval(m_swapInterval);
        }

        bool ResizeDeferred() const override { return m_ctx.resizeDeferred(); }
    };

    IBackend* CreateBackend(Api api) {
        switch (api) {
        case Api::OpenGL:  return new GLBackend();
        case Api::Vulkan:  return new VulkanBackend();
        case Api::D3D12:   return new D3D12Backend();
        case Api::External: return nullptr;
        case Api::Software: return CreateSoftwareBackend();
        }
        return nullptr;
    }

    void DestroyBackend(IBackend* backend) {
        delete backend;
    }

}
