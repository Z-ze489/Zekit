
#include "pch.h"

#include "ZeSoftSurface.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(_WIN32)

#include <windows.h>

namespace Rhi {
    namespace {

        class Win32SoftSurface final : public ISoftSurface {
        public:
            ~Win32SoftSurface() override { Detach(); }

            bool Attach(void* nativeHandle, int width, int height) override {
                Detach();
                m_hwnd = static_cast<HWND>(nativeHandle);
                if (!m_hwnd || !IsWindow(m_hwnd)) return false;

                HDC screen = GetDC(m_hwnd);
                if (!screen) return false;
                m_memDc = CreateCompatibleDC(screen);
                ReleaseDC(m_hwnd, screen);
                if (!m_memDc) { Detach(); return false; }

                if (!Resize(width, height)) { Detach(); return false; }
                return true;
            }

            bool Resize(int width, int height) override {
                if (!m_memDc) return false;
                if (width <= 0 || height <= 0) return false;
                if (width == m_w && height == m_h && m_bmp) return true;

                ReleaseBitmap();

                BITMAPINFO bi{};
                bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                bi.bmiHeader.biWidth = width;
                bi.bmiHeader.biHeight = -height;
                bi.bmiHeader.biPlanes = 1;
                bi.bmiHeader.biBitCount = 32;
                bi.bmiHeader.biCompression = BI_RGB;

                void* bits = nullptr;
                HBITMAP bmp = CreateDIBSection(m_memDc, &bi, DIB_RGB_COLORS,
                                               &bits, nullptr, 0);
                if (!bmp || !bits) {
                    if (bmp) DeleteObject(bmp);
                    return false;
                }
                m_oldBmp = SelectObject(m_memDc, bmp);
                if (!m_oldBmp || m_oldBmp == HGDI_ERROR) {
                    DeleteObject(bmp);
                    m_oldBmp = nullptr;
                    return false;
                }
                m_bmp = bmp;
                m_bits = bits;
                m_w = width;
                m_h = height;
                return true;
            }

            bool Present(const std::uint8_t* pixels,
                         std::size_t pitch, int width, int height) override {
                if (!m_hwnd || !m_memDc) return false;
                if (width <= 0 || height <= 0) return false;
                if (width != m_w || height != m_h) {
                    if (!Resize(width, height)) return false;
                }

                std::uint8_t* dst = static_cast<std::uint8_t*>(m_bits);
                const std::size_t rowBytes = static_cast<std::size_t>(width) * 4;
                for (int y = 0; y < height; ++y)
                    std::memcpy(dst + static_cast<std::size_t>(y) * rowBytes,
                                pixels + static_cast<std::size_t>(y) * pitch,
                                rowBytes);

                HDC dc = GetDC(m_hwnd);
                if (!dc) return false;
                const BOOL ok = BitBlt(dc, 0, 0, width, height, m_memDc, 0, 0, SRCCOPY);
                ReleaseDC(m_hwnd, dc);
                return ok != FALSE;
            }

            bool HasAlphaChannel() const override { return false; }

            void Detach() override {
                ReleaseBitmap();
                if (m_memDc) { DeleteDC(m_memDc); m_memDc = nullptr; }
                m_hwnd = nullptr;
                m_w = m_h = 0;
            }

            const char* Name() const override { return "Win32/GDI"; }

        private:
            void ReleaseBitmap() {
                if (!m_bmp) return;
                if (m_oldBmp) { SelectObject(m_memDc, m_oldBmp); m_oldBmp = nullptr; }
                DeleteObject(m_bmp);
                m_bmp = nullptr;
                m_bits = nullptr;
            }

            HWND    m_hwnd = nullptr;
            HDC     m_memDc = nullptr;
            HBITMAP m_bmp = nullptr;
            HGDIOBJ m_oldBmp = nullptr;
            void*   m_bits = nullptr;
            int     m_w = 0;
            int     m_h = 0;
        };

    }

    std::unique_ptr<ISoftSurface> CreateSoftSurface() {
        return std::make_unique<Win32SoftSurface>();
    }

}

#elif defined(__linux__) && !defined(__ANDROID__) && !defined(__OHOS__)

#include "ZeWindowX11.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>

namespace Rhi {
    namespace {

        class X11SoftSurface final : public ISoftSurface {
        public:
            ~X11SoftSurface() override { Detach(); }

            bool Attach(void* nativeHandle, int width, int height) override {
                Detach();

                m_dpy = static_cast<Display*>(Plat::X11DisplayHandle());
                m_xid = Plat::X11WindowId(nativeHandle);
                if (!m_dpy || !m_xid) return false;

                XWindowAttributes attr{};
                if (!XGetWindowAttributes(m_dpy, m_xid, &attr)) return false;
                if (attr.depth != 24 && attr.depth != 32) {
                    static bool reported = false;
                    if (!reported) {
                        reported = true;
                        std::fprintf(stderr,
                            "[Zekit] 软件渲染：本窗口是 %d 位色，只支持 24/32 位 TrueColor。\n"
                            "        这条兜底路径需要一个能直接收 32bpp ZPixmap 的窗口。\n",
                            attr.depth);
                    }
                    return false;
                }
                m_visual = attr.visual;
                m_depth = attr.depth;

                m_gc = XCreateGC(m_dpy, m_xid, 0, nullptr);
                if (!m_gc) { Detach(); return false; }

                if (!Resize(width, height)) { Detach(); return false; }
                return true;
            }

            bool Resize(int width, int height) override {
                if (!m_dpy || !m_xid || !m_gc) return false;
                if (width <= 0 || height <= 0) return false;
                if (width == m_w && height == m_h && m_img) return true;

                FreeImage();

                const std::size_t bytes = static_cast<std::size_t>(width) * height * 4;
                char* data = static_cast<char*>(std::malloc(bytes));
                if (!data) return false;

                m_img = XCreateImage(m_dpy, m_visual, m_depth, ZPixmap, 0,
                                     data, width, height,  32, 0);
                if (!m_img) { std::free(data); return false; }

                m_w = width;
                m_h = height;
                return true;
            }

            bool Present(const std::uint8_t* pixels,
                         std::size_t pitch, int width, int height) override {
                if (!m_img) return false;
                if (width <= 0 || height <= 0) return false;
                if (width != m_w || height != m_h) {
                    if (!Resize(width, height)) return false;
                }

                const std::size_t rowBytes = static_cast<std::size_t>(width) * 4;
                for (int y = 0; y < height; ++y)
                    std::memcpy(m_img->data
                                    + static_cast<std::size_t>(y) * m_img->bytes_per_line,
                                pixels + static_cast<std::size_t>(y) * pitch,
                                rowBytes);

                XPutImage(m_dpy, m_xid, m_gc, m_img, 0, 0, 0, 0, width, height);
                XFlush(m_dpy);
                return true;
            }

            bool HasAlphaChannel() const override { return m_depth == 32; }

            void Detach() override {
                FreeImage();
                if (m_gc && m_dpy) { XFreeGC(m_dpy, m_gc); m_gc = nullptr; }
                m_dpy = nullptr;
                m_xid = 0;
                m_visual = nullptr;
                m_depth = 0;
                m_w = m_h = 0;
            }

            const char* Name() const override { return "X11/XPutImage"; }

        private:
            void FreeImage() {
                if (!m_img) return;
                XDestroyImage(m_img);
                m_img = nullptr;
            }

            Display*    m_dpy = nullptr;
            unsigned long m_xid = 0;
            Visual*     m_visual = nullptr;
            int         m_depth = 0;
            GC          m_gc = nullptr;
            XImage*     m_img = nullptr;
            int         m_w = 0;
            int         m_h = 0;
        };

    }

    std::unique_ptr<ISoftSurface> CreateSoftSurface() {
        return std::make_unique<X11SoftSurface>();
    }

}

#else

namespace Rhi {

    std::unique_ptr<ISoftSurface> CreateSoftSurface() {
        return nullptr;
    }

}

#endif
