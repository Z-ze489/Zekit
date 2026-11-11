#pragma once
/**
 * @file ZeGlTextureSink.h
 * @brief OpenGL implementation of `Kit::ITextureSink` (platform independent)
 *
 * @details
 * `FontRenderer` (glyph atlas) and `ImageStore` (image cache) do not call GL themselves --
 * they only know `Kit::ITextureSink`, which is injected by the backend. Thus "which backend is used" is fully transparent to them.
 * This file is the GL implementation.
 *
 * ## Why a separate file (instead of buried in a backend .cpp)
 *
 * This code must be byte-for-byte identical in the Windows / Linux GL backends --
 * if the texture format and parameters differ, switching backends shows font weight and image color tone drift:
 *
 *   - grayscale glyphs    -> single channel `GL_R8`, sample only `.r` as coverage;
 *   - color glyphs / images -> `GL_RGBA8` (BGRA is swizzled in place by GL, CPU untouched);
 *   - both use `LINEAR` filtering + `CLAMP_TO_EDGE`, neither has mipmap.
 *
 * Buried in each .cpp it would be two copies of the same code -- they will drift sooner or later. Here there is only one copy,
 * shared by `#include` in both backends.
 *
 * ## Why it can be platform independent
 *
 * This class only uses `glad` (GL function pointers), does not touch the window system, does not touch `<Windows.h>` /
 * `<Xlib.h>`. So both the Windows WGL context and the Linux EGL context use it directly.
 *
 * @note WARNING: call timing: only allowed when the GL context is current. Kit's render loop
 *       guarantees the context is still current during the frame collection phase (texture creation) -- see the
 *       contract of `IBackend::DrawFrame`, and the note in `GLBackend::DrawFrame` about "deliberately not calling DoneCurrent".
 *
 * @note Handle 0 always means "invalid/no texture", consistent with the `ImageHandle` contract.
 * @note The same handle belongs only to the sink that created it -- before switching backends all old textures must be destroyed
 *       (see the order in `Window::UseRenderer`).
 *
 * @see Interface definition in ZeTextureSink.h
 */

#include "ZeGLApi.h"

#include "ZeTextureSink.h"   // Kit::ITextureSink

namespace Rhi {

    class GlTextureSink final : public Kit::ITextureSink {
    public:
        uint32_t CreateGray(int w, int h, const uint8_t* pixels, int pitch) override {
            if (w <= 0 || h <= 0 || !pixels) return 0;
            GLuint tex = 0;
            glGenTextures(1, &tex);
            glBindTexture(GL_TEXTURE_2D, tex);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            // Grayscale: 1 byte per pixel, UNPACK_ROW_LENGTH is in pixels
            if (pitch != w)
                glPixelStorei(GL_UNPACK_ROW_LENGTH, pitch);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0,
                GL_RED, GL_UNSIGNED_BYTE, pixels);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            ApplyParams();
            return tex;
        }

        uint32_t CreateRGBA(int w, int h, const uint8_t* pixels, int pitch, bool bgra) override {
            if (w <= 0 || h <= 0 || !pixels) return 0;
            GLuint tex = 0;
            glGenTextures(1, &tex);
            glBindTexture(GL_TEXTURE_2D, tex);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            // 4 bytes per pixel, UNPACK_ROW_LENGTH is in pixels
            if (pitch != 4 * w)
                glPixelStorei(GL_UNPACK_ROW_LENGTH, pitch / 4);
#if ZEKIT_GL_IS_GLES
            // * ES 3.0 TexImage2D does not accept GL_BGRA (that is a desktop/extension thing),
            //   and FreeType color glyphs are given in BGRA order -- can only be swizzled once on the CPU.
            //   Only affects the emoji / color glyph path, the volume is small, not worth extension probing for this.
            //   (Grayscale glyphs go through CreateGray, not here.)
            std::vector<uint8_t> swizzled;
            const uint8_t* src = pixels;
            if (bgra) {
                swizzled.assign(pixels, pixels + static_cast<size_t>(pitch) * h);
                for (int y = 0; y < h; ++y) {
                    uint8_t* row = swizzled.data() + static_cast<size_t>(y) * pitch;
                    for (int x = 0; x < w; ++x) {
                        uint8_t* px = row + static_cast<size_t>(x) * 4;
                        uint8_t t = px[0]; px[0] = px[2]; px[2] = t;
                    }
                }
                src = swizzled.data();
            }
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0,
                GL_RGBA, GL_UNSIGNED_BYTE, src);
#else
            // Color bitmap (FreeType gives BGRA order): let GL swizzle in place, CPU does not need to touch it
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0,
                bgra ? GL_BGRA : GL_RGBA, GL_UNSIGNED_BYTE, pixels);
#endif
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            ApplyParams();
            return tex;
        }

        void Destroy(uint32_t tex) override {
            if (!tex) return;
            GLuint t = tex;
            glDeleteTextures(1, &t);
        }

    private:
        static void ApplyParams() {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }
    };

} // namespace Rhi
