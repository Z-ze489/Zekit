
#include "pch.h"

#include "ZeRhiBackend.h"
#include "ZePlatformWindow.h"

#include "ZeVulkanContext.h"
#include "ZeVkUIRenderer.h"
#include "ZeEglContext.h"
#include "ZeUIRenderer.h"
#include "ZeGlTextureSink.h"

#include "ZeFontRenderer.h"
#include "ZeImageStore.h"
#include "ZeFrameCollector.h"

#include <iostream>
#include <chrono>
#include <android/native_window.h>

namespace Rhi {

    namespace {

        Vk::VulkanContext::NativeWindow MakeVkNativeWindow(void* nativeHandle) {
            Vk::VulkanContext::NativeWindow nw;
            nw.instance = nullptr;
            nw.display  = nullptr;
            nw.window   = reinterpret_cast<uintptr_t>(nativeHandle);
            return nw;
        }

        bool PhysicalSizeOf(void* nativeWindow, int& w, int& h) {
            w = h = 0;
            if (!nativeWindow) return false;
            ANativeWindow* win = static_cast<ANativeWindow*>(nativeWindow);
            w = ANativeWindow_getWidth(win);
            h = ANativeWindow_getHeight(win);
            return w > 0 && h > 0;
        }

    }

    class VkAndroidBackend final : public IBackend {
    public:
        VkAndroidBackend() = default;
        ~VkAndroidBackend() override { Shutdown(); }

        Api  api()     const override { return Api::Vulkan; }
        bool IsReady() const override { return m_ctx.isReady() && m_uiReady; }

        void SetGpu(Gpu gpu) override { m_gpu = gpu; }
        DeviceInfo deviceInfo() const override { return m_ctx.deviceInfo(); }

        bool Init(void* nativeHandle, int width, int height) override {
            if (!nativeHandle) return false;
            if (IsReady()) return true;

            (void)width; (void)height;
            m_nativeWindow = nativeHandle;

            const bool trace = Plat::TraceLifecycle();

            try {
                m_ctx.init(MakeVkNativeWindow(nativeHandle), m_gpu);
                int pw = 0, ph = 0;
                if (PhysicalSizeOf(m_nativeWindow, pw, ph)) m_ctx.setClientSize(pw, ph);
                m_ctx.setUiSize(width, height);
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
            (void)width; (void)height;
            int pw = 0, ph = 0;
            if (PhysicalSizeOf(m_nativeWindow, pw, ph)) m_ctx.setClientSize(pw, ph);
            return IsReady();
        }

        bool DrawFrame(const Kit::FrameCollector& fc,
                       int width, int height,
                       float r, float g, float b) override {
            if (!IsReady()) return false;
            (void)width; (void)height;
            int pw = 0, ph = 0;
            if (PhysicalSizeOf(m_nativeWindow, pw, ph)) m_ctx.setClientSize(pw, ph);
            if (width > 0 && height > 0) m_ctx.setUiSize(width, height);
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
        bool m_uiReady = false;
        const Kit::FrameCollector* m_pendingFrame = nullptr;

        void* m_nativeWindow = nullptr;
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

            m_nativeWindow = nativeHandle;

            if (!m_gl.CreateAndroid(nativeHandle, m_gpu)) {
                return false;
            }

            m_gl.MakeCurrent();
            if (!m_ui.Init()) {
                m_gl.DoneCurrent();
                m_gl.Release();
                return false;
            }
            m_uiReady = true;
            int pw = 0, ph = 0;
            if (PhysicalSizeOf(m_nativeWindow, pw, ph)) { m_w = pw; m_h = ph; }
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
            (void)width; (void)height;
            int pw = 0, ph = 0;
            if (PhysicalSizeOf(m_nativeWindow, pw, ph)) { m_w = pw; m_h = ph; }
            return IsReady();
        }

        bool DrawFrame(const Kit::FrameCollector& fc,
                       int width, int height,
                       float r, float g, float b) override {
            if (!IsReady()) return false;

            int pw = 0, ph = 0;
            if (!PhysicalSizeOf(m_nativeWindow, pw, ph)) return false;
            const uint32_t uw = (width  > 0) ? static_cast<uint32_t>(width)  : static_cast<uint32_t>(pw);
            const uint32_t uh = (height > 0) ? static_cast<uint32_t>(height) : static_cast<uint32_t>(ph);

            const bool sizeChanged = (pw != m_w || ph != m_h);
            m_w = pw;
            m_h = ph;

            m_gl.SetSwapInterval(sizeChanged ? 0 : 1);

            m_gl.MakeCurrent();
            glViewport(0, 0, m_w, m_h);
            glClearColor(r, g, b, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);

            m_ui.Draw(fc, static_cast<int>(uw), static_cast<int>(uh));
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
        GL::EglContext   m_gl;
        Kit::UIRenderer  m_ui;
        GlTextureSink    m_sink;
        Rhi::Gpu         m_gpu = Rhi::Gpu::Auto;
        bool             m_uiReady = false;
        int              m_w = 0;
        int              m_h = 0;
        void*            m_nativeWindow = nullptr;
    };

    IBackend* CreateBackend(Api api) {
        switch (api) {
        case Api::Vulkan: return new VkAndroidBackend();
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
