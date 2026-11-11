/**
 * @file ZeCanvasRenderer.h
 * @brief Kit::ICanvasRenderer -- the external renderer interface for the canvas (widgets paint as usual, the GPU step is yours)
 *
 * @details
 * ## What problem it solves
 *
 * Previously the model of `CanvasView` was "hand over the whole block": after `SetRenderEnabled(false)`, Kit
 * paints not a single pixel in that sub-window; what gets painted is entirely up to you. The cost is that no Kit
 * widgets may exist in that region -- no widgets means no hit test, no focus, no hover, no IME,
 * and not even "put a button on the canvas".
 *
 * This file supplies the other path: Kit lays out as usual and collects the display list as usual, only the final step
 * "submit triangles to the GPU" is replaced by your implementation. So widgets can be placed normally in the canvas window,
 * and input / focus / hover / scrolling / IME / animation are all present -- because it is
 * a real `Kit::Window`, only the renderer has changed hands.
 *
 * ## Why an "interface" instead of "just give a FrameCollector"
 *
 * Exactly the same approach as `ZeFontRenderer` (glyph cache), `ZeImageStore` (image cache) and
 * `ZeFreeTypeWrapper` (font loading):
 *
 * ```
 *   widgets (only speak, never touch the GPU)
 *        ↓  FrameCollector
 *   layout / batching / clipping / primitive triangulation      <- library (incl. ZeFontRenderer / ZeImageStore)
 *        ↓  CanvasFrame (vertices + indices + batches)
 *   ICanvasRenderer                    <- **you** (GL / Vulkan / D3D12 / D2D / software raster)
 * ```
 *
 * The three resource caches (glyphs / images / font faces) need not change a single line: texture creation goes through
 * the `ITextureSink` hooks (`CreateGray` / `CreateRGBA` / `DestroyTexture`),
 * the same mechanism the Vulkan / D3D12 backends use.
 *
 * ## Minimum implementation checklist (7 methods)
 *
 * | Method | When it is called | What you must do |
 * |---|---|---|
 * | `Attach`   | canvas window created / recreated | create context / swap chain, return true on success |
 * | `Detach`   | window destroyed / renderer swapped | release the above; **idempotent** |
 * | `Resize`   | client area size changed | recreate swap chain / change viewport |
 * | `CreateGray`  | new glyph | create a single-channel texture, return handle |
 * | `CreateRGBA`  | new image / color emoji | create an RGBA texture, return handle |
 * | `DestroyTexture` | glyph / image invalidated | release that handle |
 * | `DrawFrame`   | every frame | clear -> draw (your scene + triangles in `frame`) -> Present |
 *
 * ## What one frame looks like (`CanvasFrame`)
 *
 * - `vertices` / `indices`: all geometry of one draw, triangle list, 32-bit indices;
 * - `batches`: texture switch boundaries. `batch.texture == 0` = solid-color primitive (no texture),
 *   otherwise the handle returned by `CreateGray` / `CreateRGBA`; `batch.isColor == true` means
 *   the texture is a color image (emoji / image), otherwise use it as coverage (take `.r`);
 * - each batch = starting at `indices + batch.indexOffset`, `batch.indexCount` indices;
 * - clipping is already done on the CPU side (crossing quads are cut into polygons and re-triangulated),
 *   you do not need scissor / clip rect;
 * - coordinate system: top-left origin, y down, unit = pixel. Orthographic projection:
 *   `ortho(0, w, h, 0, -1, 1)` (left, right, bottom, top);
 * - colors are premultiplied alpha: use `ONE / ONE_MINUS_SRC_ALPHA` for blending.
 *
 * ## Vertex semantics (`CanvasVertex`, 13 floats)
 *
 * | Field | Meaning |
 * |---|---|
 * | `x, y` | position (top-left origin, pixels) |
 * | `r, g, b, a` | color (not premultiplied; multiply by alpha at shader output) |
 * | `u, v` | texture coordinates (meaningless for solid-color batches) |
 * | `cx, cy, hw, hh, rad` | rounded-rect SDF: center / half width and height / corner radius |
 *
 * When `hw > 0 || hh > 0`, the fragment shader uses it for an edge coverage transition of exactly 1 pixel
 * (anti-aliasing). For text / image vertices these five fields are always 0 and the texture branch is taken.
 * See the GLSL in `ZeUIRenderer.cpp` for a reference implementation (the one inside this library, semantics identical to here).
 *
 * @par Usage
 * @code
 * class MyGLRenderer : public Kit::ICanvasRenderer {
 *     bool Attach(void* hwnd, int w, int h) override;   // create context / viewport
 *     void Detach() override;
 *     bool Resize(int w, int h) override;
 *     std::uint32_t CreateGray(int w, int h, const std::uint8_t* px, int pitch) override;
 *     std::uint32_t CreateRGBA(int w, int h, const std::uint8_t* px, int pitch, bool bgra) override;
 *     void DestroyTexture(std::uint32_t tex) override;
 *     void DrawFrame(const Kit::CanvasFrame& frame) override;   // clear + draw + SwapBuffers
 * };
 *
 * auto renderer = std::make_shared<MyGLRenderer>();
 * canvas.SetRenderer(renderer);          // <- hand over the renderer (call before the first frame)
 * canvas.SetOnReady([](Kit::Widget& self) {
 *     auto& cv = Kit::As<Kit::CanvasView>(self);
 *     // place widgets normally in the canvas window -- they are painted by renderer
 *     auto& panel = cv.canvasWindow()->AddWidget(Kit::Panel());
 *     panel.AddChild(Kit::Button("Click me"));
 * });
 * @endcode
 *
 * @note Threading: all methods are called on the UI thread (the thread running the message loop),
 *       including `CreateGray` (glyph upload) -- you do not need to add locks yourself.
 * @note Context: `CreateGray` / `CreateRGBA` may be called outside `DrawFrame`
 *       (the glyph pipeline uploads during frame collection). In the implementation call `MakeCurrent` yourself,
 *       do not assume "the previous frame's context is current".
 * @note Handle `0` always means "invalid / no texture", consistent with the `Kit::ImageHandle` convention.
 *
 * @see ZeCanvasView.h    `CanvasView::SetRenderer` (how the canvas uses it)
 * @see Zekit.h           `Window::UseExternalRenderer` (hand a window over to it directly)
 */

#pragma once

#include "color4f.h"

#include <cstddef>
#include <cstdint>

namespace Kit {

    // =================================================================
    //  Vertices / batches / frames
    // =================================================================

    /**
     * @brief One UI vertex (position + color + UV + rounded-rect SDF)
     *
     * @note The layout is field-for-field identical to the in-library `Kit::UIVertex` (13 tightly packed floats),
     *       so frame data is handed over with zero copy -- do not change the field order, `CanvasFrame` would misalign.
     */
    struct CanvasVertex {
        float x = 0.f, y = 0.f;      /// < position (top-left origin, pixels)
        float r = 0.f, g = 0.f, b = 0.f, a = 1.f;   /// < color (NOT premultiplied)
        float u = 0.f, v = 0.f;      /// < texture coordinates (meaningless for solid-color batches)
        float cx = 0.f, cy = 0.f;    /// < rounded-rect center (SDF)
        float hw = 0.f, hh = 0.f;    /// < half width / half height (SDF; both 0 = no SDF)
        float rad = 0.f;             /// < corner radius (0 = right angle)
    };

    /// @brief A batch of triangles sharing one texture (the range of one draw)
    struct CanvasBatch {
        std::uint32_t texture = 0;      /// < 0 = solid color (no sampling); otherwise a texture handle
        std::uint32_t indexOffset = 0;  /// < offset relative to `CanvasFrame::indices`
        std::uint32_t indexCount = 0;   /// < index count (triangle list)
        bool          isColor = false;  /// < true = color texture; false = coverage (take .r)
    };

    /**
     * @brief Complete draw data for one frame (borrowed, valid only during the `DrawFrame` call)
     *
     * @note Do not store the pointers -- the same buffers are reused next frame.
     * @note Empty frames (`indexCount == 0`) are also handed in: then just clear + Present.
     */
    struct CanvasFrame {
        const CanvasVertex* vertices = nullptr;   /// < vertex array
        std::size_t         vertexCount = 0;      /// < vertex count
        const std::uint32_t* indices = nullptr;   /// < index array (triangle list)
        std::size_t         indexCount = 0;       /// < index count
        const CanvasBatch*  batches = nullptr;    /// < batch table (draw in order)
        std::size_t         batchCount = 0;       /// < batch count
        int                 width = 0;            /// < client area width in pixels (= your viewport)
        int                 height = 0;           /// < client area height in pixels
        Color4f             clear{ 0.f, 0.f, 0.f, 1.f };   /// < clear color (theme background, already premultiplied)
    };

    // =================================================================
    //  Renderer interface
    // =================================================================

    /**
     * @brief External renderer for the canvas (you implement it, the library calls you)
     *
     * @details
     * The lifecycle strictly follows this order; violating it crashes:
     *
     *     1. `Attach(nativeHandle, w, h)`   <- canvas window created (or recreated)
     *     2. `CreateGray` / `CreateRGBA`    <- any time (when a glyph / image first appears)
     *     3. `Resize(w, h)`                 <- client area size changed (may be frequent)
     *     4. `DrawFrame(frame)`             <- every frame
     *     5. `DestroyTexture(tex)`          <- any time
     *     6. `Detach()`                     <- window destroyed (window mode switch / renderer swap / close)
     *
     * After `Detach` an `Attach` may come again (switching the canvas window mode recreates the window) --
     * so the implementation must be able to go through this cycle repeatedly; do not make the state "initialize once only".
     */
    class ICanvasRenderer {
    public:
        virtual ~ICanvasRenderer() = default;

        ICanvasRenderer() = default;
        ICanvasRenderer(const ICanvasRenderer&) = delete;
        ICanvasRenderer& operator=(const ICanvasRenderer&) = delete;

        // -------------------------------------------------------------
        //  Lifecycle
        // -------------------------------------------------------------

        /**
         * @brief Window ready: create context / swap chain / pipeline here
         * @param nativeHandle native window handle (Windows = `HWND`, X11 = `Window`)
         * @param width,height client area size in pixels (your initial viewport)
         * @return false = cannot create. The library does not crash, this frame just has no content --
         *         the next frame tries again (so an idempotent / self-healing implementation is best).
         *
         * @note The window may be recreated (canvas switching to a standard window / borderless window); then `Detach`
         *       is called first and this function again -- the context / swap chain hang off that handle and must be recreated too.
         */
        virtual bool Attach(void* nativeHandle, int width, int height) = 0;

        /// @brief Window is going away: release context / swap chain / pipeline (must be idempotent)
        /// @note Textures do not have to be released one by one here -- but releasing them cleanly is your responsibility:
        ///       the library only notifies you to destroy the ones it created before switching backends (`DestroyTexture`).
        virtual void Detach() = 0;

        /**
         * @brief Client area size changed
         * @return false = recreation failed (skip this frame, retry next frame)
         * @note Dragging the window fires this many times in a row -- do not do heavy work here.
         */
        virtual bool Resize(int width, int height) = 0;

        // -------------------------------------------------------------
        //  Textures (glyph / image resource lifetime is managed by the library; here only create / release)
        // -------------------------------------------------------------

        /**
         * @brief Upload a single-channel 8-bit texture (glyph coverage bitmap)
         * @param w,h    pixel size (> 0)
         * @param pixels start address of pixels (valid range = `pitch * h` bytes)
         * @param pitch  bytes per row (may be > w: FreeType bitmap rows carry alignment padding)
         * @return texture handle (> 0); 0 on failure
         *
         * @note Use linear filtering + ClampToEdge for sampling: glyphs are rasterized on whole pixels,
         *       bilinear is their established look (see pixel snapping in `FontRenderer::LayoutText`).
         * @note Use as coverage in the shader: `color.a * tex.r`.
         */
        virtual std::uint32_t CreateGray(int w, int h, const std::uint8_t* pixels, int pitch) = 0;

        /**
         * @brief Upload an RGBA8 texture (image resources / color emoji glyphs)
         * @param bgra true = source byte order is BGRA (FreeType color bitmaps are), the implementation swaps it
         * @return texture handle (> 0); 0 on failure
         * @note Use as a color image in the shader: `color * texel` (see `CanvasBatch::isColor`).
         */
        virtual std::uint32_t CreateRGBA(int w, int h, const std::uint8_t* pixels,
                                         int pitch, bool bgra) = 0;

        /// @brief Releases a texture handle (invalid handles should be safely ignored)
        virtual void DestroyTexture(std::uint32_t tex) = 0;

        // -------------------------------------------------------------
        //  One frame
        // -------------------------------------------------------------

        /**
         * @brief Draw one frame: clear -> draw -> Present (all in one)
         * @param frame display list of this frame (borrowed, invalid immediately after return)
         *
         * @details
         * Even when you have drawn nothing, clear + Present -- otherwise the window shows the previous frame's leftovers.
         *
         * Want to overlay widgets on your own scene (3D scene + Kit UI)? In this function:
         * @code
         * glClear(...);            // or your scene clear
         * DrawMyScene();           // <- your 3D
         * DrawCanvasFrame(frame);  // <- Kit UI (opaque / translucent both fine)
         * SwapBuffers();
         * @endcode
         * For "scene first, then UI on top" use this order; to draw only UI, do only the last two steps.
         *
         * @note Vertical sync / swap interval is yours (you created the swap chain). When the UI frame rate is high,
         *       turn vsync off during interaction and on when idle -- the same trade-off as the in-library GL backend.
         */
        virtual void DrawFrame(const CanvasFrame& frame) = 0;
    };

} // namespace Kit
