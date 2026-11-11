#pragma once
#include "include/color4f.h"
#include "include/FontDesc.h"
#include "include/ZePlotTypes.h"    // PlotVertex: foreign vertices, triangles fed in by the canvas
#include "include/ZeTheme.h"        // Theme: the single source of style values
#include "include/image_types.h"    // ImageHandle / BackgroundImage / BgImageDrawMode: image draw params
#include "ZeFontRenderer.h"

#include "Shape.h"      // Primitive layer (project root, internal; include/ is the public API)

#include <cstdint>
#include <string_view>
#include <vector>

namespace Kit {

    class ImageStore;   // Image resource cache (see ZeImageStore.h in the render layer)

    // Vertex: position + color + UV + the five rounded-rect SDF params.
    //
    //   The SDF params only matter for solid-color primitives; text goes through
    //   texture sampling and ignores them (AddText vertices fill all five with 0).
    //   cx / cy = rounded-rect center, hw / hh = half extents,
    //   rad = corner radius (0 for a plain rect).
    //   Alpha fades over exactly 1 px at the edge to match the hardware MSAA
    //   transition width, so SDF and MSAA never look sharp-vs-blurry.
    //   Do not widen that band (a 3 px smoothstep once made shapes look fuzzy):
    //   band width, gradient math and geometry padding are one set of invariants --
    //   read kFS in ZeUIRenderer.cpp and kSdfPadAA in ZeFrameCollector.cpp first.
    struct UIVertex {
        float x = 0.f, y = 0.f;
        float r = 0.f, g = 0.f, b = 0.f, a = 1.f;
        float u = 0.f, v = 0.f;
        float cx = 0.f, cy = 0.f;    // Rounded-rect center
        float hw = 0.f, hh = 0.f;    // Half width / half height
        float rad = 0.f;             // Corner radius (0 = square rect, no SDF)
    };
    static_assert(sizeof(UIVertex) == 13 * sizeof(float),
        "UIVertex must be exactly 13 tightly packed floats");

    class FrameCollector {
    public:
        struct Batch {
            uint32_t texture = 0;   // 0 = solid color, no texture
            uint32_t indexOffset = 0;
            uint32_t indexCount = 0;
            bool     isColor = false;   // True when the texture is a color glyph (emoji)
        };

        // -----------------------------------------------------------------
        //  Clip stack (primitive-level rect clipping; keeps overlays unclipped)
        // -----------------------------------------------------------------
        //  Clipping is a pipeline stage, not a widget property:
        //   - Layout containers call PushClip(content rect) before their children,
        //     cutting away scrolled-out or overflowing content;
        //   - Overlay widgets (combo boxes etc.) call ResetClip() before drawing
        //     their popup, leaving the container clip domain so it draws in full --
        //     the root fix for the old "clipped popup" bug.
        // -----------------------------------------------------------------

        // Push a clip rect, intersected with the current clip domain (only shrinks).
        void PushClip(float x, float y, float w, float h);

        // Pop one clip level, restoring the clip domain from before the push.
        void PopClip();

        // Clear the whole clip stack back to "no clipping" (overlays escape containers).
        void ResetClip();

        // Read the current effective clip rect in absolute client coordinates.
        // Returns false when nothing is clipped (the whole client area is visible),
        // in which case the out params are meaningless.
        // Read-only probe used by Window for viewport culling: a widget lying fully
        // outside the visible area skips Paint. Without it the whole tree was painted
        // unconditionally every frame, and clipping only discarded the geometry after
        // layout and triangulation had already paid for it.
        bool CurrentClip(float& x, float& y, float& w, float& h) const {
            if (!hasClip_) return false;
            x = currentClip_.x; y = currentClip_.y;
            w = currentClip_.w; h = currentClip_.h;
            return true;
        }

        void AddRect(float x, float y, float w, float h, const Color4f& c, float radius = 0.0f);

        // Stroke a dashed or dotted line along a rounded-rect path, with the stroke
        // lying inside the given rect (outer edge inward by width pixels).
        // x / y / w / h describe the outer edge, radius is its corner radius,
        // width the stroke width, dashLen / gapLen the segment and gap lengths (> 0),
        // color the stroke color.
        // Returns true when appended; false on degenerate params
        // (w / h / width <= 0, dashLen / gapLen <= 0), appending nothing.
        //
        // Why the render layer: a dashed line is not a shape but a run of small quads
        // generated along the path (four straight edges plus arcs sampled into
        // polylines, split by arc length into alternating dash / gap segments).
        // Widgets only say "I want a dashed border"; geometry belongs here, the same
        // split of duties as AddText / AddImage.
        //  - The path is inset by width / 2 so the stroke fills exactly the outer
        //    band and lines up with solid borders;
        //  - Arcs are sampled at about 10 degrees, straight edges keep two points;
        //  - Each segment emits a thick-line quad with no SDF params (an axis-aligned
        //    SDF would clip a rotated quad);
        //  - Vertex count is caller-controlled: perimeter / (dashLen + gapLen)
        //    segments, typically a hundred quads for a button border.
        // Solid borders do not need this (two AddShape rounded rects are cheaper);
        // only Dashed / Dotted styles come here.
        bool AddDashedRect(float x, float y, float w, float h, float radius,
            float width, float dashLen, float gapLen, const Color4f& color);

        // Append a segment of primitive geometry (Shapes describe geometry only;
        // the caller supplies the color).
        // geo is Shape::build() output, color applies to the whole segment.
        // cx / cy / hw / hh / rad are the rounded-rect SDF params (center, half
        // extents, corner radius): pass non-zero half extents to get the exact 1 px
        // edge fade, and the geometry is padded by 1.5 px so the band stays visible;
        // leave them 0 for no SDF, which falls back to hard edges plus MSAA.
        // Returns true when appended; false when the DrawMode is unsupported (line
        // modes) and has been skipped.
        // Only DrawMode::Triangles is accepted: the primitive layer's Lines /
        // LineStrip / LineLoop cannot feed a GL_TRIANGLES pipeline, so they are
        // skipped with a one-time log rather than drawn wrong. Lines in UI are
        // stroked quads, which GL_LINES cannot express anyway.
        bool AddGeometry(const Geometry& geo, const Color4f& color,
            float cx = 0.f, float cy = 0.f,
            float hw = 0.f, float hh = 0.f, float rad = 0.f);

        // Append one shape (calls shape.build() internally); same as AddGeometry.
        // cx / cy / hw / hh / rad are the same SDF params.
        // Always pass the five params for circles and rounded rects, otherwise the
        // primitive takes the no-SDF path and the edge is plain MSAA (polygonal
        // circles). Canonical form:
        //   fc.AddShape(CircleShape(Vec2{cx,cy}, r), color, cx, cy, r, r, r);
        //   fc.AddShape(RoundedRectShape(lo, sz, r), color, cx, cy, sz.x/2, sz.y/2, r);
        // cx, cy must be the shape's own center; geometry padding is done around it.
        bool AddShape(const Shape& shape, const Color4f& color,
            float cx = 0.f, float cy = 0.f,
            float hw = 0.f, float hh = 0.f, float rad = 0.f);

        // Append a batch of foreign triangles whose vertices carry position and
        // color; they are merged in triangle by triangle.
        // verts is the PlotVertex array (x / y / r / g / b / a), count the vertex
        // count and must be a multiple of 3, dx / dy translate the whole batch from
        // its local space into client coordinates.
        // Returns true when appended; false on degenerate input (count 0 or not a
        // multiple of 3), appending nothing.
        //
        // Split of duties vs AddGeometry: that one takes library primitives (Shape /
        // Geometry, one color plus SDF params), this one takes raw precomputed
        // vertices (per-vertex color, no SDF) produced by the business layer.
        // Like every other primitive:
        //  - Clipping: with a clip domain, triangles crossing it are cut into
        //    polygons and re-triangulated, never drawn outside the domain;
        //  - Batching: joins texture-0 (solid) batches with adjacent solid geometry.
        // Vertices carry no SDF params, so only MSAA smooths edges: the OpenGL
        // backend requests 4x while Vulkan runs 1x and shows hard stair-steps on
        // diagonals. Axis-aligned rects (candles, hatches) are fine on both.
        // Per-vertex color means one batch can carry a gradient; winding order is
        // arbitrary since neither backend culls faces.
        bool AddTriangles(const PlotVertex* verts, std::size_t count,
            float dx = 0.f, float dy = 0.f);

        void AddText(std::string_view utf8, const FontDesc& font,
            float x, float y, const Color4f& color);

        // Measure a text run (forwarded to the internal FontRenderer) for alignment.
        // Returns the text bounding box (width = advance sum, height = ascent +
        // descent), or 0 when there is no font or the text is empty.
        TextMetrics MeasureText(std::string_view utf8, const FontDesc& font);

        // =================================================================
        //  Images (texture primitives)
        // =================================================================
        //  Widgets only hold the image path and hand it in at Paint time, so the
        //  widget layer never touches GL and needs no window at config time
        //  (window() is still nullptr there). Decoding and texture upload belong to
        //  ImageStore, deduplicated and cached by path. Drawing shares the text
        //  path: AppendQuadClipped plus a (texture, isColor=true) batch.
        //  isColor=true picks the `FragColor = vColor * texture(uTex, vUV)` branch,
        //  so the vertex color acts as a tint (white outputs the image unchanged)
        //  with no shader changes.
        // =================================================================

        // Image cache in effect for this frame (injected by Window every frame, borrowed).
        void SetImageStore(ImageStore* store) { images_ = store; }
        // Currently injected image cache (may be nullptr).
        ImageStore* GetImageStore() const { return images_; }

        // Final draw rect and matching UVs for one image (a draw-mode conversion).
        // Produced by ComputeImageRect; Tiled mode describes only the first tile.
        struct ImageDrawRect {
            float x = 0.f, y = 0.f, w = 0.f, h = 0.f;   // Draw rect (client coordinates)
            float u0 = 0.f, v0 = 0.f, u1 = 1.f, v1 = 1.f; // Matching texture UVs
        };

        // Convert draw mode and alignment into a draw rect plus UVs (pure, no GL).
        // img supplies the natural size, x / y / w / h the target rect, and
        // hAlign / vAlign apply to Origin only (see the contract in image_types.h).
        // Returns an all-zero rect when w / h or the image is invalid.
        // Modes: Stretch ignores the aspect ratio; Fit scales to contain and centers;
        // Fill scales to cover and crops the overflow via centered UVs; Tiled repeats
        // at the natural size and returns only the first tile (AddImageHandle loops
        // and clips the overflow); Origin keeps the size and places it by alignment.
        static ImageDrawRect ComputeImageRect(const BackgroundImage& img,
            float x, float y, float w, float h,
            BgImageDrawMode mode = BgImageDrawMode::Stretch,
            BgImageHAlign   hAlign = BgImageHAlign::Center,
            BgImageVAlign   vAlign = BgImageVAlign::Middle);

        // Append an image by path (the widget-facing entry point).
        // path is resolved by ImageStore and is free when cached.
        // Returns true when appended; false when no image cache is injected or the
        // path fails to load. Requires a current GL context, same as AddText:
        // missing glyphs upload textures too, so this is the class's usual contract.
        bool AddImage(std::string_view path,
            float x, float y, float w, float h,
            BgImageDrawMode mode = BgImageDrawMode::Stretch,
            BgImageHAlign   hAlign = BgImageHAlign::Center,
            BgImageVAlign   vAlign = BgImageVAlign::Middle,
            const Color4f& tint = Color4f::White());

        // Append an image by resource ID (for RCDATA images embedded in the exe).
        // resId is an RCDATA id holding PNG / JPG bytes, cached by ImageStore under
        // ":res:<id>". Returns true when appended; false when no image cache is
        // injected or the resource is missing / fails to decode.
        // The resource must be RCDATA rather than ICON: SOIL2 cannot read .ico
        // containers (use Window::SetWindowIconFromResource for title bar icons).
        // See ImageStore::LoadFromResource.
        bool AddImageResource(int resId,
            float x, float y, float w, float h,
            BgImageDrawMode mode = BgImageDrawMode::Stretch,
            BgImageHAlign   hAlign = BgImageHAlign::Center,
            BgImageVAlign   vAlign = BgImageVAlign::Middle,
            const Color4f& tint = Color4f::White());

        // Append an image by resolved handle (atlases, or a reused texture).
        // handle comes from ImageStore::Load. Returns true when appended; false on
        // an invalid handle or a missing image cache.
        bool AddImageHandle(ImageHandle handle,
            float x, float y, float w, float h,
            BgImageDrawMode mode = BgImageDrawMode::Stretch,
            BgImageHAlign   hAlign = BgImageHAlign::Center,
            BgImageVAlign   vAlign = BgImageVAlign::Middle,
            const Color4f& tint = Color4f::White());

        // Query an image's natural pixel size for layout measurement (may load it).
        // Returns true when the size is available; false when no image cache is
        // injected or the load fails.
        // Widgets use it to size icons when no explicit icon size is given.
        bool ImageSize(std::string_view path, float& w, float& h) const;

        void Clear();
        bool Empty() const;
        const std::vector<UIVertex>& Vertices() const { return verts_; }
        const std::vector<uint32_t>& Indices()  const { return idx_; }
        const std::vector<Batch>& Batches()  const { return batches_; }

        // =================================================================
        //  Fragments (the retained-mode payload)
        // =================================================================
        //  A snapshot of the primitives one widget drew, in absolute client coords.
        //  While a widget stays clean (same invalidation key) Window skips its Paint
        //  and merges the fragment verbatim, cutting a repaint from `nodes x Paint`
        //  to `nodes x memcpy`.
        //
        //  A fragment must carry its own batch list: batches are texture-switch
        //  boundaries, and dropping them collapses the frame into one draw call for
        //  all textures. Merging re-coalesces adjacent same-texture batches (see
        //  AppendFragment), so the result matches a full tree repaint.
        //  indexOffset inside a fragment is 0-based at the fragment's own start, so a
        //  fragment can be merged anywhere without knowing its offset in the frame.
        struct Fragment {
            std::vector<UIVertex> verts;
            std::vector<uint32_t> idx;
            std::vector<Batch>    batches;

            void Clear() { verts.clear(); idx.clear(); batches.clear(); }
            bool Empty() const { return idx.empty(); }
        };

        // Merge a fragment verbatim (vertices copied, indices offset by the current
        // vertex count, batches re-coalesced).
        // Merging only happens when the frame's last batch shares the fragment's
        // first texture; indices are already contiguous, so this yields the same
        // batch split as individual RegisterBatch calls.
        void AppendFragment(const Fragment& f);

        // Enter "record into fragment" mode: swaps this collector's three containers
        // with those of f.
        // Swapping instead of copying after recording matters because recording runs
        // once per dirty widget and copying the vertices would repeat the most
        // expensive step of frame collection. The swap is O(1) (three vector
        // pointers), so recording differs from direct drawing by one index
        // relocation. f's previous contents become this collector's buffer once
        // swapped in: clear() discards them while keeping the capacity, which is why
        // steady state stops allocating. Must be paired with EndCapture on the same f.
        void BeginCapture(Fragment& f);

        // Leave record mode: hand the accumulated primitives back to f.
        void EndCapture(Fragment& f);

        // Current clip stack depth (recording uses it to confirm a widget left the
        // stack in balance).
        int ClipDepth() const { return static_cast<int>(clipStack_.size()); }

        // Injected by Window every frame (borrowed, not owned).
        void SetFontRenderer(FontRenderer* fr) { font_ = fr; }

        // Inject the theme for this frame (Window calls this every frame; borrowed).
        void SetTheme(const Theme* theme) { theme_ = theme; }

        // Theme in effect for this frame.
        // Falls back to a built-in light theme when nothing was injected, so widgets
        // always get a value instead of null-checking everywhere.
        const Theme& GetTheme() const {
            static const Theme kFallback = Themes::Light();
            return theme_ ? *theme_ : kFallback;
        }

    private:
        std::vector<UIVertex>  verts_;
        std::vector<uint32_t>  idx_;
        std::vector<Batch>     batches_;
        FontRenderer* font_ = nullptr;
        const Theme*  theme_ = nullptr;
        ImageStore*   images_ = nullptr;   // Image cache (borrowed, injected by Window each frame)

        // ---- Clip stack ----
        // A rect array holds the clip domain at each push; currentClip_ caches the
        // intersected top. An empty stack means no clipping (hasClip_ == false).
        struct ClipRect { float x, y, w, h; };
        std::vector<ClipRect> clipStack_;
        ClipRect              currentClip_{ 0, 0, 0, 0 };
        bool                  hasClip_ = false;

        void AppendQuad(const UIVertex& a, const UIVertex& b,
            const UIVertex& c, const UIVertex& d,
            uint32_t texture, bool isColor);

        // Clip a single quad against the current clip domain, or append it as is.
        // Returns false when the quad lies entirely outside the domain (skipped).
        bool AppendQuadClipped(const UIVertex& a, const UIVertex& b,
            const UIVertex& c, const UIVertex& d,
            uint32_t texture, bool isColor);

        // Single entry point for batch registration (merge rules live only here,
        // so they cannot drift apart).
        void RegisterBatch(uint32_t texture, bool isColor,
            uint32_t indexStart, uint32_t indexCount);
    };

} // namespace Kit