#pragma once
/**
 * @file ZeTextureSink.h
 * @brief Backend hook for texture creation -- keeps the font atlas / image cache from hardcoding GL
 *
 * @details
 * Background: `FontRenderer` (glyph rasterization) and `ImageStore` (image decoding) each
 * directly called `glGenTextures` + `glTexImage2D`, and the handle was the GL texture name.
 * This path is fully correct on the GL backend, but textures on the Vulkan backend are
 * `VkImage`, and the same uint32 cannot serve both.
 *
 * So the single task of "creating a texture" is extracted:
 *
 * ```
 *   FontRenderer --+
 *                  +--> ITextureSink --> GlTextureSink (= the original GL code)
 *   ImageStore  ---+                 +--> VkUIRenderer  (new VkImage + upload)
 * ```
 *
 * Why a "hook" rather than "rebuilding a render layer":
 *   `FrameCollector::Batch::texture` has long been an opaque uint32 -- a batch only knows
 *   "use the texture with this number", not whether it is a GL name or a VK index. So as long
 *   as the two backends each handle the "create" step, the widget layer, the collector and
 *   SDF geometry generation need not change a single line.
 *
 * @note Handle 0 always means "invalid/no texture", consistent with the `ImageHandle` convention.
 * @note A handle belongs only to the sink that created it -- before switching backends all old
 *       textures must be destroyed first (see how Window::UseRenderer does it).
 */

#include <cstdint>

namespace Kit {

    /// @brief Backend interface for texture creation/destruction
    ///
    /// An implementation only needs to answer two questions: give me pixels, return a handle /
    /// release this handle.
    class ITextureSink {
    public:
        virtual ~ITextureSink() = default;

        ITextureSink(const ITextureSink&) = delete;
        ITextureSink& operator=(const ITextureSink&) = delete;

        /**
         * @brief Uploads a single-channel 8-bit texture (glyph coverage bitmap)
         * @param w,h    Pixel dimensions (must be > 0)
         * @param pixels Pixel start address
         * @param pitch  Bytes per row (may be greater than w -- FreeType bitmap row stride carries
         *               alignment padding)
         * @return Texture handle (> 0); returns 0 on failure
         */
        virtual uint32_t CreateGray(int w, int h, const uint8_t* pixels, int pitch) = 0;

        /**
         * @brief Uploads an RGBA8 texture (image resource / color emoji glyph)
         * @param w,h    Pixel dimensions (must be > 0)
         * @param pixels Pixel start address
         * @param pitch  Bytes per row (usually = 4 * w)
         * @param bgra   true = source byte order is BGRA (FreeType color bitmaps are),
         *               the implementation must convert to RGBA itself
         * @return Texture handle (> 0); returns 0 on failure
         */
        virtual uint32_t CreateRGBA(int w, int h, const uint8_t* pixels, int pitch, bool bgra) = 0;

        /// @brief Releases a handle (invalid handles should be safely ignored)
        virtual void Destroy(uint32_t tex) = 0;

    protected:
        ITextureSink() = default;
    };

} // namespace Kit
