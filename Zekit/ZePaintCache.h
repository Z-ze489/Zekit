#pragma once
// =====================================================================
//  ZePaintCache.h -- the widget's "frame collection fragment" cache (retained mode)
//
//  Why it is needed
//  ------------
//  Before 2026-10-27, Window had to re-Paint every widget of the **whole tree** on every frame:
//  text re-laid out, primitives re-triangulated, batches re-registered. Viewport culling (source map section 15) removed
//  the off-screen part, but **on-screen unchanged widgets** were still redrawn every frame -- drag one switch,
//  and a thousand motionless widgets next to it were redrawn along with it; this is the last stretch of "the more widgets, the less responsive".
//
//  This structure is the solution to that last stretch: the vertices/indices/batches a widget painted last time are kept as-is, and if unchanged they are directly
//  merged into this frame (one memcpy); only when changed is Paint rerun. Deciding "changed or not" does not rely on widgets reporting it themselves
//  (that would require changing every setter of 40+ widgets), but on **comparing the environment at record time**:
//
//    - geometry (x/y/w/h)      -- layout moves and it no longer matches, so it is naturally re-recorded;
//    - clip region             -- scrolling / container scaling changes it. The geometry in the fragment is **clipped
//                              by the clip region at that time**, so if the clip region changes it must be re-recorded (otherwise the part
//                              scrolled out of the container stays in place);
//    - theme (pointer + token) -- `Window::applyTheme` changes m_theme **in place**, the pointer does not change,
//                              so a content token must be carried;
//    - glyphs (two tokens)     -- the texture is destroyed (backend switch / render scale change) -> all handles invalid, the whole block is discarded;
//                              merely "a few new glyphs landed" -> only affects fragments that **were missing glyphs** at the time.
//
//  This key set is "self-validating": the cost of a missed mark is not drawing wrong, but **drawing stale** (any next change of geometry / theme /
//  glyphs heals it). The only thing that truly needs an explicit mark is the "appearance changed but geometry did not" category
//  (hover, press, select, value change) -- those already must call `RequestRedraw()`, otherwise
//  even immediate mode would not refresh, so marking dirty inside `Widget::RequestRedraw()` is enough.
//
//  Which widgets **do not enter the cache** (see the recording section of `Window::RenderFrame`)
//  ------------------------------------------------------
//   1. Those currently animating (`wantsAnimationFrame()`): their animation state is advanced inside `paintContent`
//      (the established form of this library), reusing the fragment would freeze the animation;
//   2. Those that scheduled a timed wakeup at last record time (`RequestFrameWake`, typically cursor blinking):
//      likewise, appearance changes with time, and "time" is not in the invalidation key;
//   3. Overlays (`paintsOnTop()`): they need `ResetClip` to escape the clip region, the record environment cannot be expressed
//      equivalently as "one rectangle", and they change every frame anyway.
// =====================================================================

#include "ZeFrameCollector.h"

#include <cstdint>
#include <memory>

namespace Kit {

    struct Theme;   // include/ZeTheme.h -- only the pointer and token are compared here, it is not included
    class Widget;

    /// @brief Environment at fragment record time (the part identical for all widgets within a frame)
    struct PaintEnv {
        const Theme* theme = nullptr;
        uint64_t themeEpoch = 0;         /// < theme content token (in-place color change also counts as changed)
        uint64_t glyphDropEpoch = 0;     /// < glyph texture **destroy** token (all handles invalid)
        uint64_t glyphContentEpoch = 0;  /// < glyph content token (includes "a few more landed")
    };

    /// @brief Cache of "the batch of primitives a widget painted itself", plus the key deciding whether it can be reused
    struct PaintFragment {
        FrameCollector::Fragment frag;   /// < vertices / indices / batches (client-area absolute coordinates)

        // ---- environment at record time (all items must be equal to allow reuse) ----
        float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;   /// < widget geometry
        bool  hasClip = false;                          /// < whether there was a clip region at record time
        float clipX = 0.0f, clipY = 0.0f, clipW = 0.0f, clipH = 0.0f;
        PaintEnv env;

        // ---- state ----
        bool valid = false;             /// < fragment usable (recorded successfully and not marked dirty)
        bool hadMissingGlyph = false;   /// < glyphs were missing at record time
        bool timed = false;             /// < a timed wakeup was scheduled at record time (cursor blinking and the like)

        void Invalidate() { valid = false; }

        /// @brief Whether this fragment can still be reused directly
        /// @param e           environment of this frame
        /// @param animatingSelf whether this widget is animating -- animating ones are never reused
        /// @param hasClipNow / cx,cy,cw,ch  clip region of this frame
        bool Reusable(const PaintEnv& e, bool animatingSelf,
                      float wx, float wy, float ww, float wh,
                      bool hasClipNow, float cx, float cy, float cw, float ch) const {
            if (!valid || timed || animatingSelf) return false;
            if (x != wx || y != wy || w != ww || h != wh) return false;
            if (hasClip != hasClipNow) return false;
            if (hasClipNow && (clipX != cx || clipY != cy || clipW != cw || clipH != ch))
                return false;
            if (env.theme != e.theme || env.themeEpoch != e.themeEpoch) return false;
            if (env.glyphDropEpoch != e.glyphDropEpoch) return false;
            // Only fragments that "were missing glyphs at record time" are affected by newly landed glyphs: the quads of a complete fragment
            // reference texture handles that already exist, new glyphs do not change it.
            if (hadMissingGlyph && env.glyphContentEpoch != e.glyphContentEpoch) return false;
            return true;
        }
    };
    /// @brief The only channel for the render side to read and write the fragment cache (friend of `Widget`)
    /// @details Uses an Access struct instead of making members public: the fragment cache is a **render implementation**
    ///          detail, the public header should only keep a `unique_ptr` forward pointer, so the delivery surface is not polluted.
    struct PaintCacheAccess {
        static PaintFragment* Get(const Widget& w);
        static PaintFragment& Ensure(Widget& w);
        static bool IsDirty(const Widget& w);
        static void MarkDirty(const Widget& w);
        static void ClearDirty(const Widget& w);
        static bool Timed(const Widget& w);
        static void SetTimed(const Widget& w, bool v);
    };

} // namespace Kit
