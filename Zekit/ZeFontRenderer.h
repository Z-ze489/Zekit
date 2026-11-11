#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#include "include/FontDesc.h"
#include "include/color4f.h"

#include "ZeFreeTypeWrapper.h"
#include "ZeTextureSink.h"

namespace Kit {

    struct GlyphQuad {
        float    x = 0, y = 0, w = 0, h = 0;
        float    u0 = 0, v0 = 0, u1 = 1, v1 = 1;
        uint32_t texture = 0;
        bool     isColor = false;   /// < Color glyph (emoji): take the full RGBA of the texture instead of only coverage
    };

    /// @brief Glyph renderer: **metrics on the main thread, rasterization/upload through the pipeline**
    ///
    /// @details
    /// This class has exactly one responsibility: turn (codepoint, font size) into (texture, quad). The difficulty is all in "when to do it":
    ///
    /// ```
    ///   LayoutText ─┬─ metrics (advance)  → main thread, reads hmtx only, microseconds
    ///               ├─ ready glyph        → main thread, emits quad directly
    ///               └─ not-ready glyph    → queued to the glyph worker thread
    ///
    ///   Glyph worker thread (separate thread / separate FT_Library):
    ///       take task → FT_RENDER → bitmap + metrics → result queue
    ///
    ///   PumpReadyGlyphs (main thread, every frame):
    ///       fetch results → create textures (**rate-limited per frame**) → into cache
    /// ```
    ///
    /// **Why they must be separated** (read before changing; this is the purpose of the whole design):
    ///  Previously LayoutText's `GetOrLoad` did it in one step -- rasterization + texture creation all done synchronously during paint.
    ///  Normal UI (a few dozen characters) shows no problem; but once someone **pastes a large block of text** into an input box,
    ///  a single frame must rasterize thousands of glyphs in a row and create thousands of textures,
    ///  and the window goes straight to "Not Responding" (Windows marks windows whose message pump times out as Not Responding).
    ///
    ///  Now: metrics stay synchronous (layout must know widths immediately), bitmaps go to the worker thread,
    ///  textures are uploaded **rate-limited per frame** (the budget of `PumpReadyGlyphs`),
    ///  so long text becomes "text growing out frame by frame", and the per-frame cost of the UI thread has an upper bound.
    ///
    /// **The synchronous fast path cannot be removed**: a normal UI has only a few new glyphs, and queueing + waiting for the next frame would
    ///  make "the character just typed disappear for one frame". So as long as there is no backlog and this round's synchronous budget is not used up
    ///  (`SetSyncRasterBudget`, default 24), it is done directly on the main thread --
    ///  behavior is exactly the same as the old version; only when a backlog builds up (pasting long text) does it really go asynchronous.
    class FontRenderer {
    public:
        FontRenderer();
        ~FontRenderer();
        FontRenderer(const FontRenderer&) = delete;
        FontRenderer& operator=(const FontRenderer&) = delete;

        bool Init();                     // Call when the context is already current
        void Shutdown();                 // Call before the context is destroyed (**joins the glyph thread**)

        /// @brief Specifies the creation backend for glyph textures
        /// @param sink nullptr = built-in GL path (default, behavior exactly the same as historical versions);
        ///             non-null = textures are created by this sink (used by the Vulkan backend)
        /// @note **DropAllGlyphTextures() must be called before switching backends** -- old handles belong to the old sink,
        ///       and dropping them after the switch would hand someone else's handles to the new sink to destroy.
        /// @note Textures are created **only on the main thread** (PumpReadyGlyphs / synchronous fast path),
        ///       and the glyph worker thread never touches the sink -- GPU contexts are all thread-bound.
        void SetTextureSink(ITextureSink* sink) { sink_ = sink; }

        /// @brief Sets the render scale of "how many physical pixels per logical unit" (given by the platform layer, default 1.0)
        ///
        /// @details
        /// On desktop it is always 1.0 (logical coordinates are physical pixels), and behavior is **exactly the same** as historical versions --
        /// all the divisions by scale below are identity transforms at 1.0.
        /// On mobile it is the density (e.g. 3.5): glyph bitmaps are rasterized at the **physical** size of "logical font size x scale"
        /// so they are not blurred after projection scaling; while the glyph quad and measured width are both
        /// divided by scale back to logical units, so **layout and text measurement results are unchanged character for character**;
        /// only the texture resolution changes.
        /// @note When the scale actually changes, all glyph textures are dropped and font faces are
        ///       re-sized on next use (the old bitmaps' resolution is no longer correct). Repeated calls with the same value are idempotent,
        ///       so it can be called unconditionally once per frame. In-flight glyph tasks are invalidated by the "size epoch" change.
        void SetScale(float scale);

        /// @brief Drops all uploaded glyph textures at once (used when switching backends / shutting down)
        /// @note The semantics are "force re-upload on next use"; font faces (faces_) are unaffected --
        ///       so it is not the same as Shutdown() (the latter also clears font faces).
        /// @note Each handle is released by the **current** sink_, so
        ///       **it must be called before switching backends (SetTextureSink)**, otherwise old handles would be
        ///       destroyed by the new sink as if they were its own IDs.
        /// @note In-flight glyph tasks are also invalidated (size epoch +1, returned results are discarded).
        void DropAllGlyphTextures();

        /// @brief Lays out a run of text into glyph quads (vertex level, for FrameCollector assembly)
        /// @param baseX, baseY start x and **baseline** y (top-left origin coordinate system)
        /// @return one quad per visible glyph (with texture name and UV)
        /// @note **The origin of the whole text run is snapped to integer pixels** -- see the long comment in the implementation:
        ///       glyph bitmaps are generated on an integer-pixel grid anyway, and at x.5/y.5 GL_LINEAR bilinearly smears the strokes
        ///       (measured on 12px Chinese: "full-coverage pixels 62 -> 25, midtones 264 -> 583").
        ///       Center/right-aligned positions naturally land on .5, so this step is key to text sharpness across the library.
        /// @note Glyphs not yet rasterized **emit no quad** (but still advance the pen) --
        ///       spacing stays stable and characters appear one by one, without making the whole line jump around.
        std::vector<GlyphQuad> LayoutText(std::string_view utf8,
            const FontDesc& font,
            float baseX, float baseY);

        /// @brief Measures a run of text (no vertices, only the bounding box), for alignment/centering layout
        /// @param utf8 text
        /// @param font font description (pixelSize determines the font size; when empty, uses faces_'s default size)
        /// @return width (sum of advances) and height (ascent+descent); returns 0 when faces_ is empty or text is empty
        /// @note Shares the same glyph metrics (bearingX/advanceX) as LayoutText,
        ///       so measurement results exactly match the actually painted text.
        /// @note **No longer triggers rasterization**: only the `FT_LOAD_NO_BITMAP` metrics path is used.
        ///       This is the prerequisite for "long-text wrapping without stutter" -- wrapping asks for each character's width once.
        TextMetrics MeasureText(std::string_view utf8, const FontDesc& font);

        // -----------------------------------------------------------------
        //  Glyph pipeline (used by backend / window side)
        // -----------------------------------------------------------------

        /// @brief Fetches glyphs already rasterized by the worker thread and creates textures for **at most budget** of them
        /// @return the number of textures actually created
        /// @details
        /// Called once per frame (the window calls it before frame collection). budget is the gate for "how many glyph textures
        /// may be submitted to the GPU this frame" -- long text relies on it to spread GPU uploads over multiple frames,
        /// instead of throwing thousands of glTexImage2D calls into one frame.
        /// @note Must be called by the thread that **holds the render context** (texture creation goes through the sink).
        /// @note LayoutText also calls it internally (only when the result queue is non-empty),
        ///       so even if the host forgets to call it, glyphs still flow out -- only the pacing is decided by LayoutText.
        int PumpReadyGlyphs(int budget);

        /// @brief Whether any glyph is still alive in the pipeline (queued / rasterizing / awaiting upload)
        /// @note Used to decide "whether another frame is needed": true means the picture has not settled yet.
        bool HasGlyphWork() const;

        /// @brief Sets the "new glyph ready" callback (**called on the glyph thread**)
        /// @note The window uses it to post "one more frame" into the message queue (RequestRedraw is enqueue semantics, thread-safe).
        ///       WARNING: the callback must **absolutely not** touch the GPU / widget tree / renderer -- those belong to the UI thread.
        ///       Passing an empty function object unregisters it. Shutdown() clears it after joining.
        void SetWakeCallback(std::function<void()> cb);

        /// @brief Single-round budget for "rasterize synchronously when there is no backlog" (count)
        /// @param n <= 0 = disable the synchronous fast path (all glyphs go to the worker thread; the first frame will have one "empty text" moment)
        /// @note Default 24. This number decides "whether a just-typed character flickers":
        ///       a batch of new glyphs smaller than it is done on the spot (within about 1ms); only larger batches are queued.
        void SetSyncRasterBudget(int n) { syncBudget_ = n; }

        /// @brief Number of currently pending (not uploaded) glyph tasks (for diagnostics)
        int PendingGlyphCount() const;

        // -----------------------------------------------------------------
        //  Two observables for fragment caching
        // -----------------------------------------------------------------
        //  Background: since 2026-10-27 `Window` caches "the fragment a widget painted last frame",
        //  and unchanged widgets are memcpy'd directly without re-running Paint. Text is the only thing in these fragments that **changes
        //  by itself** (glyph textures are produced asynchronously and land batch by batch), so it needs two comparable
        //  epochs:
        //
        //   · ContentEpoch -- glyph **content** epoch. +1 for every batch of new textures that lands and every global invalidation
        //     (SetScale / DropAll). A fragment records the epoch at record time; if the epoch changed it cannot be reused.
        //   · MissCount -- cumulative count of "this glyph has no texture yet" during layout (monotonically increasing).
        //     Comparing before and after recording tells whether "this text was missing glyphs at the time": a fragment with missing
        //     glyphs must not be reused even if the epoch is unchanged (otherwise those characters would never appear).
        //
        //  * Why not simply "invalidate everything whenever there is any new glyph": that equals "type one character, repaint the whole tree"
        //    -- with tens of thousands of widgets every keystroke would take tens of milliseconds. After recording "I was missing glyphs"
        //    per fragment, only the widgets that actually missed glyphs are re-recorded.
        // -----------------------------------------------------------------

        /// @brief Glyph content epoch (+1 when new textures land / global invalidation)
        uint64_t ContentEpoch() const { return contentEpoch_; }

        /// @brief Glyph texture **destruction** epoch (+1 only in DropAllGlyphTextures)
        /// @note Division of labor with ContentEpoch: that one also counts "a few more new glyphs were made", used to catch
        ///       "missing glyphs at record time, now filled in"; this one only tracks **handle invalidation** (backend switch / scale change),
        ///       and once it changes, all fragments must be re-recorded, no exceptions.
        uint64_t DropEpoch() const { return dropEpoch_; }

        /// @brief Cumulative count of missing glyphs during layout (monotonically increasing, compared only as a difference)
        uint64_t MissCount() const { return missCount_; }

    private:
        /// @brief State machine of one glyph
        /// @details The old version had only two states, "has texture / no texture", and judged missing glyphs by tex==0;
        ///          after introducing asynchrony it must distinguish "not made yet" from "genuinely absent",
        ///          otherwise missing glyphs would be repeatedly re-queued (asking FreeType once per frame).
        enum class GState : uint8_t {
            Pending,   /// < needs to be made, but not yet queued (queue full / no sink)
            Queued,    /// < handed to the glyph thread (or currently rasterizing)
            Ready,     /// < has texture, can emit quad
            Missing    /// < the font does not contain this glyph (or the bitmap is empty) -- final result, no retry
        };

        struct GlyphTex {
            /// Texture handle: **opaque to the backend**. Created by GL by default (= GL texture name);
            /// after an ITextureSink is installed it is allocated by that sink (the Vulkan backend uses a VkImage table index).
            uint32_t tex = 0;
            int      w = 0, h = 0;
            int      bearingX = 0, bearingY = 0;
            int      advanceX = 0;
            bool     isColor = false;
            GState   state = GState::Ready;

            bool drawable() const {
                return state == GState::Ready && tex != 0 && w > 0 && h > 0;
            }
        };

        /// @brief Creation backend for glyph textures (nullptr = built-in GL path)
        ITextureSink* sink_ = nullptr;

        /// @brief Render scale: how many physical pixels per logical unit (desktop 1.0 / mobile density)
        /// @note Affects only two places: **rasterization font size** (x scale) and **geometry conversion** (/ scale),
        ///       and enters no public semantics -- callers still see only logical units.
        float scale_ = 1.0f;

        /// @brief Logical font size -> physical font size used for rasterization (applies scale_, lower bound 1)
        /// @param logicalPx logical font size; 0 = use the default size (pixelSize_)
        unsigned int PhysicalSizeOf(unsigned int logicalPx) const {
            if (logicalPx == 0) logicalPx = pixelSize_ ? pixelSize_ : 1u;
            const float s = (scale_ > 0.0f) ? scale_ : 1.0f;
            const unsigned int px = static_cast<unsigned int>(logicalPx * s + 0.5f);
            return px > 0 ? px : 1u;
        }

        std::vector<std::unique_ptr<ftw::FontFace>> faces_;   /// < primary font first, fallback fonts (emoji/symbol) after

        /// @brief Glyph cache for one font size (codepoint -> uploaded glyph texture)
        struct SizeCache {
            std::unordered_map<char32_t, GlyphTex> glyphs;

            /// @brief Codepoint -> advance (**metrics cache**, independent of bitmaps)
            /// @details Layout asks "how wide is this character" every frame, and that is in fact unrelated to whether a bitmap
            ///          can be drawn. With separate caching, `MeasureText` never triggers rasterization --
            ///          so the wrapping algorithm (asking width per character) drops from "one FT_RENDER per character"
            ///          to "one hash lookup per character".
            std::unordered_map<char32_t, int> advances;
        };

        /// @brief Glyph cache **bucketed by font size** -- multiple font sizes can coexist library-wide
        ///
        /// @details
        /// Previously this was "one cache_ + one pixelSize_": as soon as some text needed a different font size than the current
        /// one, it did setPixelSize + dropped **all** glyph textures. That shows no problem in single-font-size UIs,
        /// but is fatal when two font sizes appear in the same frame -- quads already registered in FrameCollector store
        /// **GL texture names**, and after those names are freed by glDeleteTextures, the glyph textures created next for another font size
        /// recycle those names; so text painted earlier gets painted as other glyphs (manifesting as garbled text on screen).
        /// Reference reproduction: `probe_font_size.cpp` (same frame, single font size vs two font sizes, group A differs in 1058/16000 pixels).
        /// After bucketing, textures are released only at Shutdown, and no font size invalidates another.
        ///
        /// @note The cost is texture usage per "font size x codepoint"; with many font sizes the texture count goes up
        ///       (the design is one texture per glyph anyway). To really save resources one should build a glyph atlas, which is another matter.
        std::unordered_map<unsigned int, SizeCache> caches_;

        /// @brief Font size currently loaded by faces_ (affects only the bitmap size obtained by subsequent loadGlyph)
        /// @note The semantics are "**all** faces have successfully been set to this font size".
        ///       If any face fails to change size, this stays 0 (unconfirmed),
        ///       so the next call retries -- otherwise it would stay forever at the old size and lay out with old bitmaps.
        unsigned int faceSize_ = 0;

        /// @brief Confirmed font size of each face (same length as faces_; 0 = unconfirmed)
        /// @note Used to avoid "clearing all faces' bitmap caches when switching font size" --
        ///       setPixelSize internally calls clearCache, and unconditional re-calls would mean re-rasterizing all glyphs of the primary font + emoji
        ///       on every font size switch (emoji BGRA bitmaps are not small).
        std::vector<unsigned int> faceSizes_;

        unsigned int                                pixelSize_ = 0;   /// < default font size (used when FontDesc does not specify one)

        /// @brief Sets each face to font size px (does not touch any uploaded glyph texture)
        void            EnsureFaceSize(unsigned int px);

        // =================================================================
        //  Glyph pipeline (the two ends: worker thread + main thread)
        // =================================================================
        //  * Threading contract (violating it crashes; read carefully first)
        //    · Touched only on the **main thread**: faces_ / faceSizes_ / caches_ / sink_ / texture handles
        //    · Touched only on the **glyph thread**: workerFaces_ / workerLib_
        //    · Shared by both: tasks_ / done_ protected by mtx_
        //    · FreeType libraries are **not thread-safe per instance**, so the glyph thread uses a
        //      separate FT_Library (ftw::Library::createIndependent),
        //      with zero sharing with the main thread's one. See the threading model notes in ZeFreeTypeWrapper.h.
        // =================================================================

        struct GlyphTask {
            unsigned int px = 0;
            char32_t     cp = 0;
            uint64_t     epoch = 0;   /// < size epoch; returned results with a mismatched epoch are discarded
        };

        struct GlyphDone {
            GlyphTask        task;
            ftw::GlyphBitmap bmp;     /// < the bitmap itself (produced by the worker thread, consumed by the main thread)
            bool             found = false;   /// < false = the font genuinely does not contain this glyph
        };

        std::thread                       worker_;
        mutable std::mutex                mtx_;
        std::condition_variable           cv_;
        std::deque<GlyphTask>             tasks_;      /// < main thread -> glyph thread
        std::deque<GlyphDone>             done_;       /// < glyph thread -> main thread
        std::atomic<bool>                 stopping_{ false };
        std::atomic<int>                  queued_{ 0 };   /// < number of glyphs in the pipeline (including in-flight)
        std::atomic<uint64_t>             epoch_{ 0 };    /// < size epoch (+1 on SetScale / DropAll)

        std::shared_ptr<ftw::Library>              workerLib_;     /// < library exclusive to the glyph thread
        std::vector<std::unique_ptr<ftw::FontFace>> workerFaces_;   /// < font faces exclusive to the glyph thread
        std::vector<unsigned int>                  workerFaceSizes_; /// < confirmed font sizes, same length as workerFaces_
        unsigned int                               workerFacePx_ = 0;

        std::function<void()>   wake_;            /// < callback when new glyphs are ready (executed on the glyph thread)

        int  syncBudget_ = 24;      /// < number allowed to rasterize synchronously when there is no backlog (per "batch")
        int  syncUsed_ = 0;         /// < how many were done synchronously this round

        /// @brief Glyph content epoch (see the ContentEpoch notes; read/written only on the main thread)
        uint64_t contentEpoch_ = 1;
        uint64_t dropEpoch_ = 1;    /// < texture destruction epoch (+1 only in DropAllGlyphTextures)
        uint64_t missCount_ = 0;    /// < layout missing-glyph counter (monotonically increasing)

        void StartWorker();
        void StopWorker();
        void WorkerMain();

        /// @brief Implementation of PumpReadyGlyphs (**does not reset** the synchronous budget)
        /// @details LayoutText's fallback fetch goes here: it runs "once per widget",
        ///          and if it also reset the budget, 30 widgets on one screen would amplify synchronous rasterization 30x --
        ///          which is exactly the "thousands of glyphs per frame" this change eliminates.
        int PumpInternal(int budget);

        /// @brief Gets the cache entry for codepoint cp at font size px (creates it if absent; queues it into the pipeline when needed)
        /// @return pointer to the cache entry (may not be in a drawable state)
        const GlyphTex* Resolve(char32_t cp, unsigned int pxPhys);

        /// @brief Synchronous rasterization + upload (main thread, fast path when there is no backlog)
        /// @return whether a drawable glyph was successfully produced
        bool RasterizeSync(char32_t cp, unsigned int pxPhys, GlyphTex& out);

        /// @brief Queues a task into the pipeline
        /// @return false = queue is full (limit kMaxQueuedTasks); the caller should keep it Pending
        bool Enqueue(char32_t cp, unsigned int pxPhys);

        /// @brief Advance width of a codepoint at that physical font size (**metrics only**, does not trigger rasterization)
        /// @details Results are cached in SizeCache::advances. This is the core of "measurement/wrapping no longer stutters":
        ///          `FT_LOAD_NO_BITMAP` makes FreeType compute metrics only, 2~5 microseconds per call.
        int AdvanceOf(char32_t cp, unsigned int pxPhys);

        /// @brief Whether the pipeline is idle (nothing queued / in flight / awaiting upload)
        bool PipelineIdle() const;

        /// @brief Commits one rasterization result into the cache (main thread; texture creation happens here)
        void CommitResult(GlyphDone&& r);
    };

} // namespace Kit
