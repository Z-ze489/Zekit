
#include "pch.h"

#include "ZeRhiBackend.h"
#include "ZeSoftRaster.h"
#include "ZeSoftSurface.h"

#include "ZeFrameCollector.h"
#include "ZeFontRenderer.h"
#include "ZeImageStore.h"

#include <cstdio>

namespace Rhi {

    namespace {

        void ReportUnavailableOnce() {
            static bool reported = false;
            if (reported) return;
            reported = true;
            std::fprintf(stderr,
                "[Zekit] 软件渲染后端不可用：本平台还没有\"CPU 像素 → 窗口\"的实现。\n"
                "        需要补的是 ZeSoftSurface.cpp 的第三个分支（CreateSoftSurface）。\n"
                "        当前这次失败意味着**所有渲染后端都起不来**，窗口里不会有内容。\n");
        }

        class SoftBackend final : public IBackend {
        public:
            SoftBackend() = default;
            ~SoftBackend() override { Shutdown(); }

            Api  api()     const override { return Api::Software; }
            bool IsReady() const override { return m_ready && m_surface != nullptr; }

            bool IsPerPixelTransparent() const override {
                return m_surface && m_surface->HasAlphaChannel();
            }

            DeviceInfo deviceInfo() const override {
                DeviceInfo info{};
                if (!IsReady()) return info;
                std::snprintf(info.name, sizeof info.name, "CPU（软件光栅化）");
                std::snprintf(info.driver, sizeof info.driver, "Zekit SoftRasterizer · %s",
                              m_surface ? m_surface->Name() : "-");
                std::snprintf(info.api, sizeof info.api, "Software");
                info.isDiscrete = false;
                info.valid = true;
                return info;
            }

            bool Init(void* nativeHandle, int width, int height) override {
                if (!nativeHandle) return false;
                if (IsReady()) { Resize(width, height); return true; }

                if (!m_surface) m_surface = CreateSoftSurface();
                if (!m_surface) { ReportUnavailableOnce(); return false; }

                if (!m_surface->Attach(nativeHandle, width, height)) {
                    m_surface.reset();
                    return false;
                }
                m_ready = true;
                return true;
            }

            void Shutdown() override {
                if (!m_ready && !m_surface) { m_tex.Clear(); return; }
                m_tex.Clear();
                if (m_surface) { m_surface->Detach(); m_surface.reset(); }
                m_ready = false;
            }

            bool Resize(int width, int height) override {
                if (!IsReady()) return false;
                if (width <= 0 || height <= 0) return false;
                m_surface->Resize(width, height);
                return IsReady();
            }

            bool DrawFrame(const Kit::FrameCollector& fc,
                           int width, int height,
                           float r, float g, float b) override {
                if (!IsReady()) return false;
                if (width <= 0 || height <= 0) return false;

                if (!m_ras.Draw(fc, width, height, r, g, b, m_clearAlpha, m_tex))
                    return false;

                return m_surface->Present(m_ras.Pixels(), m_ras.Pitch(), width, height);
            }

            void AttachTextureSinks(void* fontRenderer, void* imageStore) override {
                if (fontRenderer)
                    static_cast<Kit::FontRenderer*>(fontRenderer)->SetTextureSink(&m_tex);
                if (imageStore)
                    static_cast<Kit::ImageStore*>(imageStore)->SetTextureSink(&m_tex);
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
                m_tex.Clear();
            }

            bool CanShareContext() const override { return false; }

            void SetClearAlpha(float alpha) override {
                m_clearAlpha = (alpha <= 0.0f) ? 0.0f : 1.0f;
            }

            void SetGpu(Gpu) override {}

            void SetSwapInterval(int) override {}

            bool ResizeDeferred() const override { return false; }

            std::size_t TextureBytes() const { return m_tex.Bytes(); }

        private:
            SoftTextureStore          m_tex;
            SoftRasterizer            m_ras;
            std::unique_ptr<ISoftSurface> m_surface;
            float m_clearAlpha = 1.0f;
            bool  m_ready = false;
        };

    }

    IBackend* CreateSoftwareBackend() {
        return new SoftBackend();
    }

}
