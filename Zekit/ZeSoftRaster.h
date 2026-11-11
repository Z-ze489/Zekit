#pragma once
/**
 * @file ZeSoftRaster.h
 * @brief Software rasterizer -- a `FrameCollector` consumer that touches no graphics API
 *
 * @details
 * Parallel to `Kit::UIRenderer` (GL) / `Vk::VkUIRenderer` / `D3D12UIRenderer` as
 * one of three implementations of the same layer: input is always `Kit::FrameCollector`
 * (vertices + indices + batches), output is always "that block of pixels on screen".
 * The only difference -- this file computes pixels on the **CPU**:
 *
 * ```
 *   FrameCollector (vertices / indices / batches / clipped primitives)
 *          |
 *          +- UIRenderer      -> GL     -> SwapBuffers
 *          +- VkUIRenderer    -> Vulkan -> present
 *          +- D3D12UIRenderer -> D3D12  -> present
 *          +- SoftRasterizer  -> **a block of BGRA in memory** -> (presented by ZeSoftSurface)
 * ```
 *
 * ## Why it is needed (this is the entire reason this file exists)
 *
 * All three GPU backends share a precondition the user cannot fix by installing a library:
 * the machine must have a working graphics driver. Real-world cases include -- servers /
 * cloud desktops / remote sessions with no GPU at all; a missing or broken graphics driver;
 * `libvulkan.so` absent or an empty ICD list; old GPUs whose GL only reaches 2.1;
 * `/dev/dri` not mapped in a container. On these machines every GPU backend fails `Init`,
 * so the window opens and the message loop runs, but there is not a single pixel inside --
 * the user sees "this library is broken" rather than "this machine has no renderer".
 *
 * Software rasterization covers that last case: as long as the window system can hand out a
 * writable pixel buffer, the UI can be drawn. Slower (milliseconds, not microseconds),
 * but visible, clickable, usable.
 *
 * ## Semantics must match the GPU backends item by item (not "roughly")
 *
 * The five things in a vertex are given their meaning in the fragment shader, and software
 * rasterization follows them literally (source in the public header `include/ZeCanvasShaders.h`;
 * this file and the GL backend reference the same semantics):
 *
 * | Convention | Where it lands in this file |
 * |---|---|
 * | Solid primitives carry the five rounded-rect SDF parameters | the `hw/hh > 0` branch of `ShadePixel` |
 * | Edge transition is exactly 1 pixel | `0.5f - d` (not smoothstep, see the comment there) |
 * | Color output is premultiplied alpha | `rgb *= a` at the end of `ShadePixel` |
 * | Gray textures use `.r` as coverage | `SampleGray` |
 * | Sampling = linear + ClampToEdge | `SampleBilinear*` |
 * | Blending = `ONE / ONE_MINUS_SRC_ALPHA` | `BlendPremultiplied` |
 *
 * If any of these six drifts, the symptom is "under software rendering the UI looks fuzzy /
 * text looks blurry / translucency shows halos", while under GPU it is perfectly fine --
 * that kind of difference is extremely hard to track down. So copy it even if slower;
 * do not "optimize" the formulas.
 *
 * ## Known differences from the GPU backends (record them honestly, do not "fix" them as bugs)
 *
 * 1. No MSAA. GPU 4x sampling softens the edges of "triangles without SDF" (`AddTriangles`,
 *    i.e. PlotView's raw vertices); here there is only 1 sample point, so slanted edges have
 *    hard aliasing. This is exactly consistent with the Vulkan backend (1x) -- that is,
 *    this is not a defect of software rendering but the historical fact that "only GL
 *    requested 4x". SDF primitives (all rounded rects / circles / borders) are equally
 *    smooth on both sides, because they do not rely on sample count for antialiasing.
 * 2. Rounded-rect/circle coverage uses the analytic gradient (`|grad d| = 1`) rather than
 *    `fwidth`. Under a 1:1 pixel-mapped orthographic projection the two are mathematically
 *    equivalent, and the analytic value has no derivative noise -- that is, software
 *    rendering's rounded corners are not worse than GL, and at some angles are even cleaner.
 * 3. Full redraw every frame. GPU backends have swap chains / back buffers, and tricks like
 *    `ResizeDeferred` are meaningless here (resizing on the CPU is just swapping an array),
 *    so `Resize` is immediate.
 *
 * @note This file is an internal header (not in `include/`): consumers never see the
 *       rasterizer and use it only through `Kit::Window` -- same positioning as `ZeUIRenderer.h`.
 * @note Thread model: consistent with the whole library, used only on the render thread
 *       (UI thread), with no locking.
 *
 * @see ZeSoftSurface.h    how pixels reach the screen (the platform-dependent half)
 * @see ZeRenderBackendSoft.cpp  the software implementation of `Rhi::IBackend` (wires the two together)
 * @see include/ZeCanvasShaders.h  the same set of semantics on the GPU side (read side by side)
 */

#include "ZeTextureSink.h"        // Kit::ITextureSink (glyph / image redirection)
#include "include/color4f.h"      // Kit::Color4f

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Kit { class FrameCollector; }

namespace Rhi {

    // =================================================================
    //  CPU textures
    // =================================================================

    /**
     * @brief A texture that lives in memory (glyph coverage bitmap / image RGBA)
     *
     * @details
     * GPU backend textures are `GLuint` / `VkImage`; here a "texture" is just a run of bytes.
     * Both forms are expressed by the same struct and distinguished by `gray` -- the sampling
     * paths differ (see `SampleGray` and `SampleBilinearRGBA`), matching the two `uIsColor`
     * branches in the shader one to one.
     *
     * @note Gray textures store only 1 byte per pixel: glyph coverage is single-channel anyway,
     *       and storing it as RGBA is 4x the memory -- a CJK font has hundreds of glyphs per
     *       size and one cache per size, so the difference is obvious.
     * @note RGBA is stored as straight alpha, matching the bytes produced by `SOIL2` / FreeType;
     *       premultiplication happens at shading time (`ShadePixel`), the same ordering as the
     *       GPU side.
     */
    struct SoftTexture {
        int    w = 0;
        int    h = 0;
        bool   gray = true;          /// < true = single-channel coverage; false = RGBA8 straight alpha
        std::vector<std::uint8_t> px;   /// < gray: w*h; RGBA: w*h*4 (tightly packed rows, no padding)
    };

    /**
     * @brief The CPU implementation of `Kit::ITextureSink` -- the destination for glyph / image resources
     *
     * @details
     * Plays the same role as `GlTextureSink` / `VkUIRenderer` (which also implements this
     * interface): `FontRenderer` and `ImageStore` only know `ITextureSink`, so "whether the
     * texture lives in VRAM or in memory" is completely transparent to them -- this is exactly
     * the payoff of extracting this layer, and the software backend connects with zero changes.
     *
     * Handle allocation starts at 1 (0 is always "no texture"), consistent with `GlTextureSink`
     * (GL texture names) and the `ImageHandle` convention. Handles are not recycled: `Destroy`
     * only removes the entry and the next handle keeps incrementing -- recycling would let an
     * old handle that was already drawn this frame and destroyed next frame be taken over by a
     * new texture (the GPU side has the same pitfall, see the long comment in
     * `FontRenderer::SizeCache`).
     *
     * @note Must be aligned with the lifetime of `FontRenderer::SetTextureSink` /
     *       `ImageStore::SetTextureSink`: release all textures with this sink before switching
     *       backends, then unhook.
     */
    class SoftTextureStore final : public Kit::ITextureSink {
    public:
        std::uint32_t CreateGray(int w, int h, const std::uint8_t* pixels, int pitch) override;
        std::uint32_t CreateRGBA(int w, int h, const std::uint8_t* pixels,
                                 int pitch, bool bgra) override;
        void Destroy(std::uint32_t tex) override;

        /// @brief Looks up a texture (returns nullptr if the handle is invalid / destroyed)
        const SoftTexture* Get(std::uint32_t tex) const;

        /// @brief Releases all textures (call before switching backends / destruction; all handles become invalid)
        void Clear();

        /// @brief Number of currently live textures (for diagnostics / leak checks)
        std::size_t Count() const { return m_tex.size(); }

        /// @brief Bytes currently occupied by textures (for diagnostics: the bulk of software backend memory is here)
        std::size_t Bytes() const;

    private:
        std::unordered_map<std::uint32_t, SoftTexture> m_tex;
        std::uint32_t m_next = 1;   /// < next handle (0 is the "no texture" sentinel, never allocated)
    };

    // =================================================================
    //  Rasterizer
    // =================================================================

    /**
     * @brief Paints one frame's display list into a CPU pixel buffer
     *
     * @details
     * Lifetime is fully symmetric with GL's `UIRenderer`: `Resize` (= glViewport) ->
     * `Draw` each frame (= clear + drawElements per batch). The pixel buffer is owned by this
     * class and reallocated when the size changes; the pointer handed out by `Pixels()` is
     * valid until the next `Resize` / destruction.
     *
     * Pixel format: in memory it is four bytes `B, G, R, A`, alpha already premultiplied.
     * This order is not arbitrary -- it is simultaneously
     *   - the byte order of a Windows 32bpp `BI_RGB` DIB (`0xAARRGGBB` little-endian),
     *   - the byte order of X11 `ZPixmap` at 32bpp under 24/32-bit TrueColor (`0x00RRGGBB`,
     *     the high byte is ignored by the server or read as alpha);
     * so the presentation side (`ZeSoftSurface`) can memcpy directly, without reordering a
     * single pixel. Per-channel reads and writes deliberately use `uint8_t*` rather than
     * `uint32_t`, to avoid welding byte-order assumptions into the rasterization code.
     *
     * @note Row stride = `4 * Width()`, with no extra alignment -- both presentation targets
     *       accept tightly packed rows.
     * @note No clipping is done: the display list has already been clipped on the CPU side
     *       (`FrameCollector::AppendQuadClipped`); here there is only viewport clipping
     *       (triangle bounding box intersected with [0,W)x[0,H)). This is the same convention
     *       as the `glDisable(GL_SCISSOR_TEST)` line in `ZeCanvasShaders.h`.
     */
    class SoftRasterizer {
    public:
        /**
         * @brief Prepares a width x height pixel buffer
         * @return true = size is valid (> 0) and the buffer is ready
         * @note Calling repeatedly with the same size is idempotent (no reallocation), so it can be
         *       called unconditionally once per frame.
         */
        bool Resize(int width, int height);

        /**
         * @brief Paints one frame: clear -> rasterize batch by batch
         * @param fc          this frame's display list (clipping done, coordinates = client-area pixels)
         * @param width,height client-area pixel size (same coordinate system as `fc`, i.e. the orthographic projection range)
         * @param r,g,b       clear color (the theme's bg token, 0~1)
         * @param clearAlpha  clear alpha (1 for normal windows; 0 for overlay hosts = transparent background)
         * @param textures    texture table available this frame (glyphs / images)
         * @return false = degenerate size (<= 0), nothing was painted
         *
         * @note The clear color is written premultiplied (`rgb *= clearAlpha`). This is not done
         *       casually: under a transparent window an unpremultiplied background color gets pulled
         *       back by `1 - srcA` on every blend, showing up as "transparent areas turn gray" --
         *       the same root cause as the D3D12 clear bug recorded in section 37.
         */
        bool Draw(const Kit::FrameCollector& fc,
                  int width, int height,
                  float r, float g, float b, float clearAlpha,
                  const SoftTextureStore& textures);

        int Width() const { return m_w; }
        int Height() const { return m_h; }
        std::size_t Pitch() const { return static_cast<std::size_t>(m_w) * 4; }

        /// @brief Base address of the pixel buffer (`B,G,R,A` premultiplied; invalid after `Resize`)
        const std::uint8_t* Pixels() const { return m_px.data(); }

    private:
        /// @brief Shading mode of a triangle -- decides which sampling path each pixel takes
        enum class Mode {
            Solid,   /// < solid color (possibly with SDF)
            Gray,    /// < gray texture (glyph coverage)
            Color,   /// < color texture (emoji / image)
        };

        /// @brief This layer's own vertex copy -- fields correspond one-to-one with `Kit::UIVertex`
        ///
        /// @details Why not consume `Kit::UIVertex` directly: that type is defined in
        /// `ZeFrameCollector.h`, which drags in `ZeTheme.h` (2,500 lines). This header is used only
        /// by `ZeRenderBackendSoft.cpp`, and pulling in the whole theme dependency for one type
        /// reference is not worth it. Conversion happens only in this class's .cpp, in one place.
        struct Vertex {
            float x, y;            /// < position (client-area pixels, top-left origin)
            float r, g, b, a;      /// < color (not premultiplied)
            float u, v;            /// < texture coordinate (0~1)
            float cx, cy;          /// < rounded-rect center
            float hw, hh;          /// < rounded-rect half width / half height
            float rad;             /// < corner radius
        };

        /// @brief Rasterizes one triangle (vertices already converted to pixel coordinates)
        void RasterizeTriangle(const Vertex& a, const Vertex& b, const Vertex& c,
                               Mode mode, const SoftTexture* tex);

        /// @brief Paints one pixel (interpolated attributes -> coverage -> premultiply -> blend)
        /// @note Position is not a parameter: the SDF needs the pixel center, and under an
        ///       orthographic 1:1 mapping the interpolated vertex position always equals the pixel
        ///       center -- one less interpolation, with bit-identical results.
        void ShadeAndBlend(int px, int py, float cr, float cg, float cb, float ca,
                           float u, float v,
                           float cx, float cy, float hw, float hh, float rad,
                           Mode mode, const SoftTexture* tex);

        int m_w = 0;
        int m_h = 0;
        std::vector<std::uint8_t> m_px;   /// < `B,G,R,A` premultiplied, tightly packed rows
    };

} // namespace Rhi
