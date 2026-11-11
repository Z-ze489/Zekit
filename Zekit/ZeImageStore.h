#pragma once
/**
 * @file ZeImageStore.h
 * @brief Image resource cache (render layer, parallel to FontRenderer)
 *
 * @details
 * Has exactly one responsibility: path -> texture handle + original size, deduplicated by path.
 *
 * Why not load inside widgets (important convention):
 *  - Decoding and texture upload are GL operations; the widget layer "only describes, never touches GL" (see the Widget::Paint convention);
 *  - A widget may be configured before a window exists (`Button btn("OK"); btn.SetImage("a.png");`
 *    at this point window() is still nullptr), so the widget has no usable GL context at all;
 *  - Therefore widgets only store the path string; the actual decode/upload happens in the render layer:
 *    when a widget paints, it hands the path to FrameCollector, which forwards it to this class for resolution (zero cost on cache hit).
 *
 * Relation to existing practice in this library:
 *  - FontRenderer follows the same pattern (on-demand glyph decoding + glGenTextures upload + cache);
 *  - This class is its image counterpart, also owned by Window, with the same lifetime.
 *
 * @note Texture creation/deletion must happen while the GL context is current.
 * @note Handle 0 = no image (see image_types.h); paths that fail to load are "negatively cached",
 *       so the same bad path is not decoded repeatedly and does not spam the log.
 *
 * @see FrameCollector::AddImage / ImageSize (the indirect entry point on the widget side)
 * @see image_types.h ImageHandle / BackgroundImage / BgImageDrawMode
 */

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "include/image_types.h"
#include "ZeTextureSink.h"

namespace Kit {

    /**
     * @brief Image resource cache: caches "texture handle + original size" by path
     *
     * @details
     * One ImageStore serves one GL context (owned by one Window).
     * Not thread-safe -- consistent with the whole library, use only on the render thread.
     *
     * @par Typical usage (library internal)
     * @code
     * // Held inside Window::Impl
     * ImageStore m_imageStore;
     * // Handed to the collector every frame; widgets use it only indirectly via FrameCollector
     * fc.SetImageStore(&m_imageStore);
     * // Before the context is destroyed
     * m_imageStore.Shutdown();
     * @endcode
     *
     * @note No Init() needed: textures are created on demand at first Load; there is no state to set up in advance.
     *       Only Shutdown() is mandatory (otherwise handles leak after the GL context is destroyed).
     */
    class ImageStore {
    public:
        ImageStore() = default;
        ~ImageStore();

        // Non-owning semantics: texture handles are managed by this class; copying would cause double glDeleteTextures
        ImageStore(const ImageStore&) = delete;
        ImageStore& operator=(const ImageStore&) = delete;

        /**
         * @brief Gets an image: returns directly on cache hit, otherwise decodes + uploads the texture
         * @param path Image file path (formats supported by SOIL2: PNG/JPG/BMP/TGA/DDS...)
         * @return Texture handle (>0); returns 0 on failure or empty path
         *
         * @note Must be called while the GL context is current.
         * @note On load failure, prints one line to stderr (the library's silent principle: only output errors),
         *       and records the path as "known failure"; subsequent calls return 0 directly.
         */
        ImageHandle Load(std::string_view path);

        /**
         * @brief Gets an image from memory bytes: returns directly on cache hit, otherwise decodes + uploads the texture
         * @param key Cache key (caller-defined; recommend a pseudo-path prefixed with ":xxx:" to avoid colliding with real file paths)
         * @param data, size Raw bytes of the image file (PNG/JPG/BMP/TGA/DDS..., same format set as Load)
         * @return Texture handle (>0); returns 0 on decode failure or empty bytes
         * @note Shares the same cache (including negative cache) with Load -- data with the same key is not decoded twice.
         */
        ImageHandle LoadFromMemory(const std::string& key,
            const unsigned char* data, std::size_t size);

        /**
         * @brief Gets an image from an executable RCDATA resource (Windows only)
         * @param resId Resource ID (an RCDATA resource in the .rc, embedding PNG/JPG image bytes)
         * @return Texture handle (>0); returns 0 if the resource does not exist / decoding fails
         * @details
         * The image ships with the exe, with no loose files at runtime. Internally = `FindResourceW(RT_RCDATA)`
         * to get bytes -> `LoadFromMemory` (cache key `":res:<id>"`).
         * @note Note this is not an ICON resource -- SOIL2 cannot decode the .ico container. For a window title bar
         *       icon from an ICON resource, use `Window::SetWindowIconFromResource`
         *       (that path uses the native LoadImage and is unrelated to this class).
         * @note Only Windows has the "resource" concept; other platforms always return 0 (treated as load failure).
         */
        ImageHandle LoadFromResource(int resId);

        /**
         * @brief Gets the image description by handle (handle + original width/height)
         * @param h Handle returned by Load
         * @return Description pointer; returns nullptr if the handle is invalid
         * @note The pointer refers to internal storage and stays valid until Shutdown / Clear.
         */
        const BackgroundImage* Get(ImageHandle h) const;

        /**
         * @brief Queries the original size of an image (for layout measurement; triggers loading)
         * @param[in]  path Image path
         * @param[out] w, h Original pixel size
         * @return true = size obtained; false = no such image (w/h unchanged)
         */
        bool SizeOf(std::string_view path, int& w, int& h);

        /// @brief Releases all textures and clears the cache (all handles become invalid)
        /// @note Must be called before the backend that created them (GL context / Vulkan device) is destroyed.
        void Shutdown();

        /// @brief Sets the backend that creates image textures
        /// @param sink nullptr = built-in GL path (default, behavior identical to previous versions);
        ///             non-null = textures are created by this sink (used by the Vulkan backend)
        /// @note Must call Shutdown() before switching backends -- old handles belong to the old sink;
        ///       dropping them after the switch would hand someone else's handles to the new sink for destruction.
        void SetTextureSink(ITextureSink* sink) { sink_ = sink; }

        /// @brief Clears all caches without resetting the "ready" state (alias of Shutdown, self-explanatory)
        void Clear() { Shutdown(); }

        /// @brief Number of currently cached images (including negatively cached failed entries)
        std::size_t Count() const { return entries_.size(); }

    private:
        /// @brief One cache record (index + 1 = handle)
        struct Entry {
            BackgroundImage img;    /// < handle = 0 means "this path is known to have failed loading"
            std::string     path;
        };

        std::vector<Entry>                    entries_;   /// < index + 1 = handle (handles are not recycled; valid until Shutdown)
        std::unordered_map<std::string, ImageHandle> byPath_;   /// < path -> handle (failures are recorded too, with value 0)
        ImageHandle                           next_ = 1;  /// < next handle (0 is the "no image" sentinel, never allocated)

        /// @brief Backend that creates image textures (nullptr = built-in GL path)
        ITextureSink* sink_ = nullptr;

        /// @brief Unified ingestion of decoded RGBA: uploads the texture + records the cache (on failure frees pixels and negatively caches)
        ImageHandle Commit(const std::string& key, int w, int h, unsigned char* pixels);
    };

} // namespace Kit
