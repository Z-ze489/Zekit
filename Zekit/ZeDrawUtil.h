// ZeDrawUtil.h - internal drawing helpers shared by new widgets (not part of the public API).
//
// Lives in Zekit/ (source root), not include/ - the same rule as ZeFrameCollector.h:
// it knows FrameCollector / RoundedRectShape, so it is library-internal. Users only
// ever get value interfaces (colors / radii / line widths) and never see these types.
//
// Why it exists: the "paint a rounded rect (all five SDF params required)" snippet
// was copy-pasted into anonymous namespaces in ZePanel.cpp / ZeTabView.cpp /
// ZeProgressBar.cpp / ZeSlider.cpp. If new widgets (cards / toolbars / sidebars /
// collapse panels / pagers / breadcrumbs / steps / the two picker popups...) kept
// copying it, one copy would eventually miss an SDF param - the symptom is
// "rounded corners degrade to hard edges", silent, no error reported.
// So new widgets always go through here instead of writing their own copy.
//
// All functions are inline: this header is included by several .cpp files;
// without inline they would be multiply defined.
// The namespace is Kit::drawutil (not Kit::detail): call sites read
// drawutil::PaintRoundedRect(...) and are visibly using internal helpers.
// See ZeFrameCollector.h for the AddShape / AddText / PushClip contracts.

#pragma once

#include "ZeFrameCollector.h"
#include "ZeRoundedRectShape.h"
#include "ZeCircleShape.h"      // Color-picker dots / step nodes / round arrow caps
#include "ZePolygonShape.h"     // Chevron arms (AddGeometry only accepts Triangles)
#include "include/color4f.h"
#include "include/ZeBoxStyle.h"     // BoxInsets / PaintBoxShadow (box shadow and padding)
#include "include/ZeTextStyle.h"    // AlignOffsetX/Y (text alignment enums)

#include <algorithm>
#include <cmath>        // std::fabs / std::fmod (hue strip math)
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace Kit {
    namespace drawutil {

        // =================================================================
        //  Geometry: fit a rectangular item into a rounded container / circle
        // =================================================================
        //  Why this group exists: image primitives (FrameCollector::AddImage) land
        //  as axis-aligned rectangles, and the render pipeline has NO rounded /
        //  circular masking - SDF only applies to solid-color primitives (see the
        //  UIVertex notes). So "keep images from poking out of the corners" is the
        //  widget layer's own geometry job: getting it wrong shows right-angle
        //  corners, and doing it ad hoc means every rounded container copies magic
        //  numbers like 0.2929 / 0.7071.
        //
        //  Consolidated 2026-11-06: Card background image, Tooltip background
        //  (rounded family / circle / star), and the image inside circular avatars
        //  each had their own copy before; now they all go through here.

        // "Shoulder width" of a rounded rect: how much the largest axis-aligned
        // inscribed rectangle must inset from each of the four sides.
        // radius: corner radius (<= 0 returns 0).
        // For radius r, the largest axis-aligned inscribed rectangle insets each
        // side by r * (1 - 1/sqrt(2)) ≈ 0.2929r - its four corners then touch the
        // four arcs (corner-to-center distance sqrt(2)*(r - d) = r).
        // One-line derivation: corner (d, d) on the arc => (r-d)^2 + (r-d)^2 = r^2
        // => d = r(1 - 1/sqrt(2)).
        // For "fill to the edge" do not use a rounded container (this returns 0
        // when radius = 0).
        inline float CornerInset(float radius) {
            if (radius <= 0.0f) return 0.0f;
            constexpr float kOneMinusInvSqrt2 = 1.0f - 0.70710678118f;   // ≈ 0.2928932
            return radius * kOneMinusInvSqrt2;
        }

        // Largest axis-aligned inscribed rectangle inside a rounded rect (four out params).
        // x,y,w,h: outer rect of the rounded shape.
        // radius: corner radius (clamped internally to half the short side, same rule as AddRect).
        // ox,oy,ow,oh: the inscribed rect (the caller's own variables).
        //
        // The input is the OUTER rect; whether to subtract the border first is the
        // caller's decision: box widgets usually inset by the line width before
        // calling (the border is not "usable" area), while a background image that
        // must cover the inside of the box passes the already-inset rect.
        // Degenerate sizes (w/h <= 0) return a zero-size rect; callers can simply
        // check ow/oh > 0 - no need to re-check themselves.
        inline void InscribedRect(float x, float y, float w, float h, float radius,
            float& ox, float& oy, float& ow, float& oh) {
            if (w <= 0.0f || h <= 0.0f) {
                ox = x; oy = y; ow = 0.0f; oh = 0.0f;
                return;
            }
            const float r = std::clamp(radius, 0.0f, std::min(w, h) * 0.5f);
            const float inset = CornerInset(r);
            ox = x + inset;
            oy = y + inset;
            ow = std::max(0.0f, w - inset * 2.0f);
            oh = std::max(0.0f, h - inset * 2.0f);
        }

        // Largest axis-aligned inscribed SQUARE inside a circle (centered on the circle).
        // cx,cy: circle center.
        // radius: the circle's radius (NOT the bounding rect's half-width; for a
        // star pass the inner radius).
        // ox,oy,side: square top-left corner and side length.
        //
        // Side = sqrt(2) * r (the diagonal equals the diameter). Used to fit a
        // rectangular image into non-rectangular containers: images inside circular
        // avatars and circle / star bubble backgrounds.
        //
        // Stars need the INNER radius (the pentagram's concave vertex radius, 0.42R
        // in this library) - passing the outer radius pokes the square's corners
        // through the star points, looking like "the image is leaking out".
        inline void InscribedSquare(float cx, float cy, float radius,
            float& ox, float& oy, float& side) {
            const float r = (radius > 0.0f) ? radius : 0.0f;
            side = r * 1.41421356237f;      // √2 · r
            ox = cx - side * 0.5f;
            oy = cy - side * 0.5f;
        }

        // Paint a rounded rectangle.
        // fc: frame collector; x,y: top-left; w,h: size.
        // radius: corner radius (clamped internally to half the short side).
        // color: fill color (alpha 0 produces no primitives).
        //
        // All five SDF params MUST be passed (center x/y, half-width, half-height,
        // corner radius) - missing any degrades the corners to "MSAA-only hard
        // edges". And cx/cy must be the shape's OWN center: geometry expansion
        // pivots around it. This is exactly why this function exists (hand copies
        // by each widget tend to miss it).
        inline void PaintRoundedRect(FrameCollector& fc, float x, float y, float w, float h,
            float radius, const Color4f& color) {
            if (w <= 0.0f || h <= 0.0f || color.a <= 0.0f) return;

            // 2026-10-27: take the 4-vertex + SDF fast path (FrameCollector::AddRect
            // with radius) instead of building a RoundedRectShape. The latter samples
            // 8 arc segments per corner and fans out to ~44 points - one rounded rect
            // ≈126 vertices, while the shader already computes rounded coverage from
            // the SDF params carried by each vertex; 4 vertices suffice, with
            // analytic distances (smoother than polylines, no polygon facets).
            // Measured (ZEKIT_TRACE_FRAME, 320 mixed widgets): vertices 61977 -> ~5k,
            // frame collection dropped from 6-7ms to ~1ms.
            //
            // SDF only supports one radius for all four corners. Only when truly
            // different per-corner radii are needed, fall back to
            // fc.AddShape(RoundedRectShape(pos, size, rTL, rTR, rBR, rBL), ...).
            fc.AddRect(x, y, w, h, color, radius);
        }

        // Paint a circle.
        // cx,cy: center; r: radius.
        // Used by the color-picker dots / hue-strip end caps and step-bar nodes.
        inline void PaintCircle(FrameCollector& fc, float cx, float cy, float r,
            const Color4f& color) {
            if (r <= 0.0f || color.a <= 0.0f) return;
            fc.AddShape(CircleShape(Vec2{ cx, cy }, r), color, cx, cy, r, r, r);
        }


        // Paint a box as "border underneath + background inset" (the library-wide border strategy).
        // fc: frame collector.
        // x,y,w,h: box rect; radius: corner radius.
        // borderWidth: border line width (<= 0 or a transparent border color = no border).
        // borderColor: border color.
        // background: fill color (alpha 0 = paint the border ring only, no fill).
        //
        // With a background this is two rounded rects: the outer one fills the
        // border color, the inner one insets by the line width and fills the
        // background - so the "border" reads as an inner line: the geometry stays
        // unchanged and neighbors are not pushed away. Same technique as
        // BoxWidget::paintBox / Panel::Paint.
        //
        // When the background is transparent, do NOT keep the "outer fills all"
        // two-rect approach: the inner skips a <= 0 entirely, so the outer
        // border-color rect STAYS in place covering the whole rect - the symptom is
        // "wanted an outline, got a solid block" (the ColorPicker popup going all
        // gray came exactly from this: after the SV pad / hue strip / channel track
        // gradients were painted, each was covered by its own transparent-background
        // outline). So background.a <= 0 builds the ring from FOUR edges: top and
        // bottom full-width, left and right nested between them - non-overlapping
        // (low-alpha same color drawn twice makes dark corner dots).
        //
        // The four edges each use hard corners (radius 0): the ring width is usually
        // 1-2px, the rounding difference is sub-pixel and far smaller than the
        // "outline becomes a solid block" cost. With an opaque background the
        // two-rect approach stays; corner alignment is guaranteed by shrinking both
        // radii in sync (the inner ring must use radius - borderWidth, otherwise
        // the two arcs misalign and corners end up thick on one side, thin on the
        // other).
        inline void PaintBox(FrameCollector& fc, float x, float y, float w, float h,
            float radius, float borderWidth,
            const Color4f& borderColor, const Color4f& background) {
            if (w <= 0.0f || h <= 0.0f) return;

            const bool hasBorder = (borderWidth > 0.0f && borderColor.a > 0.0f);
            if (!hasBorder) {
                PaintRoundedRect(fc, x, y, w, h, radius, background);
                return;
            }

            // Transparent background = outline only: build the ring from four edges (why not fill-all, see the note above)
            if (background.a <= 0.0f) {
                const float bw = std::min(borderWidth, std::min(w, h) * 0.5f);
                PaintRoundedRect(fc, x, y, w, bw, 0.0f, borderColor);                            // top
                PaintRoundedRect(fc, x, y + h - bw, w, bw, 0.0f, borderColor);                   // bottom
                PaintRoundedRect(fc, x, y + bw, bw, h - bw * 2.0f, 0.0f, borderColor);           // left
                PaintRoundedRect(fc, x + w - bw, y + bw, bw, h - bw * 2.0f, 0.0f, borderColor);  // right
                return;
            }

            // Outer layer = border color fills the whole rect
            PaintRoundedRect(fc, x, y, w, h, radius, borderColor);

            // Inner layer = background inset (inner corner radius shrunk in sync)
            const float iw = w - borderWidth * 2.0f;
            const float ih = h - borderWidth * 2.0f;
            if (iw <= 0.0f || ih <= 0.0f) return;
            const float ri = std::max(0.0f, radius - borderWidth);
            PaintRoundedRect(fc, x + borderWidth, y + borderWidth, iw, ih, ri, background);
        }

        // Paint a thin horizontal line (separators / step connector lines / breadcrumb slash base).
        // Uses a rounded rect, not LineShape: at 1-2px widths a rounded rect has
        // natural antialiased edges and matches the library's other thin-line
        // implementations (that is how all separators are drawn).
        inline void PaintHLine(FrameCollector& fc, float x, float y, float w, float thickness,
            const Color4f& color) {
            PaintRoundedRect(fc, x, y, w, thickness, thickness * 0.5f, color);
        }

        // Paint a thin vertical line (sidebar / toolbar separators).
        inline void PaintVLine(FrameCollector& fc, float x, float y, float h, float thickness,
            const Color4f& color) {
            PaintRoundedRect(fc, x, y, thickness, h, thickness * 0.5f, color);
        }

        // =================================================================
        //  Thick segments / arcs - the single implementation of "line" primitives
        // =================================================================
        //  Needed because FrameCollector::AddGeometry only accepts TRIANGLES, so
        //  LineShape / LineStrip / LineLoop all draw nothing (silently skipped).
        //  A "thick line" must therefore be decomposed into a quad + round caps at
        //  both ends. Icons (glyph strokes / rings), grouped panels (notched border
        //  outlines), loading animations (rotating arcs) all need this technique -
        //  kept here so three copies do not drift and one does not forget the caps.

        // Paint a thick line with round caps (a quad + two circles of radius = half the line width).
        // x1,y1,x2,y2: endpoints.
        // thickness: line width in pixels.
        // roundCaps: true = round caps at both ends (default); false = flat (saves two circles).
        //
        // Zero-length ends (x1==x2 && y1==y2) degenerate to a single dot.
        inline void PaintThickSegment(FrameCollector& fc, float x1, float y1, float x2, float y2,
            float thickness, const Color4f& color, bool roundCaps = true) {
            if (thickness <= 0.0f || color.a <= 0.0f) return;

            const float dx = x2 - x1;
            const float dy = y2 - y1;
            const float len = std::sqrt(dx * dx + dy * dy);
            const float h = thickness * 0.5f;

            if (len <= 0.0001f) {
                PaintCircle(fc, x1, y1, h, color);
                return;
            }

            const float nx = -dy / len * h;
            const float ny = dx / len * h;

            const std::vector<Vec2> quad{
                Vec2{ x1 + nx, y1 + ny },
                Vec2{ x2 + nx, y2 + ny },
                Vec2{ x2 - nx, y2 - ny },
                Vec2{ x1 - nx, y1 - ny },
            };
            fc.AddShape(PolygonShape(quad, true), color, 0.f, 0.f, 0.f, 0.f, 0.f);

            if (roundCaps) {
                PaintCircle(fc, x1, y1, h, color);
                PaintCircle(fc, x2, y2, h, color);
            }
        }

        // Paint an arc (a0 -> a1; screen coordinates: angles grow clockwise, 0 = right).
        // cx,cy: center; r: radius.
        // a0,a1: start/end angles in radians; a1 < a0 is supported (draws reversed).
        // thickness: line width; caps are added by PaintThickSegment.
        // segs: arc segment count (larger = smoother).
        //
        // For a full circle use PaintCircleOutline (it saves the two duplicated end caps).
        inline void PaintArc(FrameCollector& fc, float cx, float cy, float r,
            float a0, float a1, float thickness, const Color4f& color, int segs = 0) {
            if (r <= 0.0f || thickness <= 0.0f || color.a <= 0.0f) return;

            const float sweep = std::fabs(a1 - a0);
            if (sweep <= 0.0001f) return;

            if (segs <= 0) {
                // One segment per ~15 degrees; at least 3, at most 96 (more is visually indistinguishable)
                segs = static_cast<int>(std::ceil(sweep / 0.2618f));
            }
            segs = std::max(2, std::min(segs, 96));

            float px = cx + r * std::cos(a0);
            float py = cy + r * std::sin(a0);
            for (int i = 1; i <= segs; ++i) {
                const float t = static_cast<float>(i) / static_cast<float>(segs);
                const float a = a0 + (a1 - a0) * t;
                const float nx = cx + r * std::cos(a);
                const float ny = cy + r * std::sin(a);
                PaintThickSegment(fc, px, py, nx, ny, thickness, color);
                px = nx;
                py = ny;
            }
        }

        // Paint a CLOSED ring outline (approximated by a series of thick segments; the caps fill the joint).
        // Equivalent to PaintArc(0, 2pi) but skips one duplicated pair of caps.
        inline void PaintCircleOutline(FrameCollector& fc, float cx, float cy, float r,
            float thickness, const Color4f& color, int segs = 24) {
            if (r <= 0.0f || thickness <= 0.0f || color.a <= 0.0f) return;
            segs = std::max(3, std::min(segs, 96));

            constexpr float kTau = 6.2831853071795865f;
            float px = cx + r;
            float py = cy;
            for (int i = 1; i <= segs; ++i) {
                const float a = kTau * static_cast<float>(i) / static_cast<float>(segs);
                const float nx = cx + r * std::cos(a);
                const float ny = cy + r * std::sin(a);
                PaintThickSegment(fc, px, py, nx, ny, thickness, color);
                px = nx;
                py = ny;
            }
        }

        // Pi (shared by several angle conversions in this header).
        constexpr float kPi = 3.14159265358979323846f;
        // 2 * pi.
        constexpr float kTau = 6.28318530717958647692f;

        // Paint one line of text horizontally and vertically centered inside [x, x+w) x [y, y+h).
        // fc: frame collector (used for measuring too, so measuring and drawing share one source).
        // text: the text (UTF-8); font: the font; color: text color.
        // Returns true if painted; false for empty text / zero size / font not ready.
        // No ellipsis truncation here - callers that need it (breadcrumbs, step
        // titles) handle it first via TextWidget::ellipsizeLine or clip to the rect
        // themselves.
        inline bool PaintTextCentered(FrameCollector& fc, std::string_view text,
            const FontDesc& font, float x, float y, float w, float h,
            const Color4f& color) {
            if (text.empty() || w <= 0.0f || h <= 0.0f || color.a <= 0.0f) return false;

            const TextMetrics tm = fc.MeasureText(text, font);
            if (tm.width <= 0.0f || tm.height <= 0.0f) return false;

            const float tx = x + (w - tm.width) * 0.5f;
            const float ty = y + (h - tm.height) * 0.5f;
            fc.AddText(text, font, tx, ty + tm.ascent, color);   // AddText's y is the baseline
            return true;
        }

        // Paint one line of left-aligned text at (x, y) (y = top of the text block).
        // Returns the painted text width (0 when the font is not ready).
        inline float PaintTextAt(FrameCollector& fc, std::string_view text,
            const FontDesc& font, float x, float y, const Color4f& color) {
            if (text.empty() || color.a <= 0.0f) return 0.0f;
            const TextMetrics tm = fc.MeasureText(text, font);
            if (tm.width <= 0.0f) return 0.0f;
            fc.AddText(text, font, x, y + tm.ascent, color);
            return tm.width;
        }

        // Linearly interpolate two colors (palette gradients, step "done -> pending" transitions).
        // t: 0 = a, 1 = b (clamped to the endpoints when out of range).
        inline Color4f LerpColor(const Color4f& a, const Color4f& b, float t) {
            t = std::clamp(t, 0.0f, 1.0f);
            return Color4f{ a.r + (b.r - a.r) * t,
                            a.g + (b.g - a.g) * t,
                            a.b + (b.b - a.b) * t,
                            a.a + (b.a - a.a) * t };
        }

        // Scale a color's RGB by a factor (darken / brighten, alpha kept).
        // Only for tweaks like "same hue one step darker"; for precision, use
        // LerpColor toward black / white instead.
        inline Color4f ScaleRgb(const Color4f& c, float k) {
            return Color4f{ std::clamp(c.r * k, 0.0f, 1.0f),
                            std::clamp(c.g * k, 0.0f, 1.0f),
                            std::clamp(c.b * k, 0.0f, 1.0f),
                            c.a };
        }

        // Point-in-rect test (half-open range, matching hit testing: [x, x+w) x [y, y+h)).
        inline bool HitRect(float px, float py, float x, float y, float w, float h) {
            return px >= x && px < x + w && py >= y && py < y + h;
        }

        // =================================================================
        //  Gradients
        // =================================================================
        //  No gradient primitive in this library, so a "gradient" = a series of thin
        //  strips, each one interpolated color. The caller gives the strip count:
        //  too few shows banding, too many burns vertices for nothing. Empirical:
        //  28x18 strips (~500 quads) is enough in both looks and cost.

        // Paint a horizontal linear gradient (left to right).
        // from,to: the two end colors.
        // steps: strip count (>= 2; effectively rounded against the width).
        inline void PaintHGradient(FrameCollector& fc, float x, float y, float w, float h,
            const Color4f& from, const Color4f& to, int steps) {
            if (w <= 0.0f || h <= 0.0f) return;
            if (steps < 2) steps = 2;
            if (steps > 64) steps = 64;      // cap: more is visually indistinguishable, wasted vertices

            const float bw = w / static_cast<float>(steps);
            for (int i = 0; i < steps; ++i) {
                const float t0 = static_cast<float>(i) / static_cast<float>(steps - 1);
                // Each strip uses its left-edge color and is drawn half a strip wider to avoid seams between strips (neighbors overlap slightly)
                const Color4f c = LerpColor(from, to, t0);
                PaintRoundedRect(fc, x + bw * static_cast<float>(i), y, bw + 0.5f, h, 0.0f, c);
            }
        }

        // Paint a vertical linear gradient (top to bottom).
        inline void PaintVGradient(FrameCollector& fc, float x, float y, float w, float h,
            const Color4f& from, const Color4f& to, int steps) {
            if (w <= 0.0f || h <= 0.0f) return;
            if (steps < 2) steps = 2;
            if (steps > 64) steps = 64;

            const float bh = h / static_cast<float>(steps);
            for (int i = 0; i < steps; ++i) {
                const float t0 = static_cast<float>(i) / static_cast<float>(steps - 1);
                const Color4f c = LerpColor(from, to, t0);
                PaintRoundedRect(fc, x, y + bh * static_cast<float>(i), w, bh + 0.5f, 0.0f, c);
            }
        }

        // Paint a hue strip (red -> yellow -> green -> cyan -> blue -> magenta -> red, horizontal).
        // saturation/value: which S/V level to render at (a pure hue strip = 1/1).
        // The hue strip is the H axis of HSV and CANNOT be expressed as a two-color
        // PaintHGradient (H is cyclic; crossing 0 degrees must wrap back to red) -
        // hence the 6 hardcoded segments.
        inline void PaintHueStrip(FrameCollector& fc, float x, float y, float w, float h,
            float saturation, float value, int steps) {
            if (w <= 0.0f || h <= 0.0f) return;
            if (steps < 6) steps = 6;
            if (steps > 64) steps = 64;

            const float bw = w / static_cast<float>(steps);
            for (int i = 0; i < steps; ++i) {
                const float hue = 360.0f * static_cast<float>(i) / static_cast<float>(steps);
                // HSV -> RGB (used only here, written inline instead of exposing a public API)
                const float c = value * saturation;
                const float hp = hue / 60.0f;
                const float xx = c * (1.0f - std::fabs(std::fmod(hp, 2.0f) - 1.0f));
                const float m = value - c;
                float r = 0.0f, g = 0.0f, b = 0.0f;
                if (hp < 1.0f)      { r = c; g = xx; b = 0.0f; }
                else if (hp < 2.0f) { r = xx; g = c; b = 0.0f; }
                else if (hp < 3.0f) { r = 0.0f; g = c; b = xx; }
                else if (hp < 4.0f) { r = 0.0f; g = xx; b = c; }
                else if (hp < 5.0f) { r = xx; g = 0.0f; b = c; }
                else                { r = c; g = 0.0f; b = xx; }

                const Color4f col{ r + m, g + m, b + m, 1.0f };
                PaintRoundedRect(fc, x + bw * static_cast<float>(i), y, bw + 0.5f, h, 0.0f, col);
            }
        }

        // UTF-8 character boundary indices: 0, each character's start, ..., size().
        // Slicing by BYTES would cut CJK / emoji into half characters (the renderer
        // gets an invalid sequence = tofu boxes), so per-character operations must
        // fetch the boundary table first.
        inline std::vector<std::size_t> Utf8Boundaries(std::string_view s) {
            std::vector<std::size_t> out;
            out.reserve(s.size() + 1);
            std::size_t i = 0;
            const std::size_t n = s.size();
            while (i < n) {
                out.push_back(i);
                const unsigned char c = static_cast<unsigned char>(s[i]);
                std::size_t len = 1;
                if ((c & 0x80u) == 0x00u)      len = 1;   // 0xxxxxxx
                else if ((c & 0xE0u) == 0xC0u) len = 2;   // 110xxxxx
                else if ((c & 0xF0u) == 0xE0u) len = 3;   // 1110xxxx
                else if ((c & 0xF8u) == 0xF0u) len = 4;   // 11110xxx
                i += len;
            }
            out.push_back(n);
            return out;
        }

        // Truncate one line of text to the available width, appending U+2026 ("...").
        // fc: measurer (same glyph metrics as drawing).
        // line: the whole original line (UTF-8); font: the font; avail: available width.
        // Returns the truncated string (ellipsis included); an empty string when even
        // the ellipsis does not fit.
        //
        // Same semantics and implementation constraints as TextWidget::ellipsizeLine:
        // cut only at character boundaries; trailing spaces are eaten (avoiding
        // "abc ...").
        //
        // Why a second copy exists: that one is a protected member of TextWidget,
        // and the LayoutBase branch (cards / collapse panels / breadcrumbs / steps...)
        // does not derive from TextWidget - the two inheritance branches never meet,
        // so it cannot be called. Their titles need the same "ellipsis when it does
        // not fit", so the tool lives in the shared drawing helpers.
        //
        // Measure first and do not call when it fits - this function assumes it does
        // NOT fit and will actively eat characters before appending the ellipsis.
        inline std::string Ellipsize(FrameCollector& fc, std::string_view line,
            const FontDesc& font, float avail) {
            if (line.empty() || avail <= 0.0f) return std::string();

            const std::string kEllipsis = "\xE2\x80\xA6";   // U+2026
            const float ellipsisW = fc.MeasureText(kEllipsis, font).width;
            if (ellipsisW > avail) return std::string();    // even the ellipsis does not fit

            const std::vector<std::size_t> b = Utf8Boundaries(line);
            if (b.size() <= 1) return std::string();

            // Find the last character boundary where "content + ellipsis" still fits
            std::size_t best = 0;
            for (std::size_t k = 1; k + 1 < b.size(); ++k) {
                std::string_view piece = line.substr(0, b[k]);
                if (fc.MeasureText(piece, font).width + ellipsisW <= avail)
                    best = k;
                else
                    break;                                  // width grows monotonically, everything after is wider
            }

            std::string out(line.substr(0, b[best]));
            while (!out.empty() && out.back() == ' ') out.pop_back();   // eat trailing spaces
            out += kEllipsis;
            return out;
        }

        // Paint a "box shadow" (the same rounded rect, expanded and translated).
        // fc: frame collector.
        // x,y,w,h: the box rect (the shadow is derived from it by offset / spread).
        // radius: box corner radius.
        // color: shadow color (alpha sets the intensity).
        // offsetX,offsetY: shadow offset relative to the box.
        // spread: expansion beyond the box on all sides.
        // opacity: extra opacity multiplier (the box's own SetOpacity).
        //
        // Same approach as BoxWidget::paintShadow: a shadow is NOT a blur, but one
        // rounded rect larger than the box and translated. There is no blur channel
        // in this library, so "softness" is expressed with low alpha + small spread,
        // not a Gaussian kernel.
        //
        // Paint the shadow BEFORE the box (the box covers its inner part); the
        // expanded part falls outside the widget rect - the caller must ensure it
        // is not clipped by the parent container, or call fc.ResetClip() before
        // painting the shadow (popovers / cards that need "bleed").
        inline void PaintShadow(FrameCollector& fc, float x, float y, float w, float h,
            float radius, const Color4f& color,
            float offsetX, float offsetY, float spread, float opacity = 1.0f) {
            if (w <= 0.0f || h <= 0.0f) return;
            if (color.a <= 0.0f || opacity <= 0.0f) return;
            if (spread <= 0.0f && offsetX == 0.0f && offsetY == 0.0f) return;

            Color4f c = color;
            c.a *= opacity;
            if (c.a <= 0.0f) return;

            // The spread applies to all four sides, so width/height each add 2 * spread;
            // the corner radius must grow with it, otherwise the expanded corners look
            // squarer than the box's (the two arcs misalign).
            const float r = (radius > 0.0f) ? (radius + spread) : 0.0f;
            PaintRoundedRect(fc, x + offsetX - spread, y + offsetY - spread,
                w + spread * 2.0f, h + spread * 2.0f, r, c);
        }

        // Chevron direction (pager prev/next, collapse indicator, drawer handle, step bars).
        enum class ChevronDir { Left, Right, Up, Down };

        // Paint a "V" chevron pointing at dir.
        // cx,cy: chevron center (where the tip side converges).
        // size: half spread of the two arms (larger = wider).
        // thickness: arm width.
        // dir: direction (Down is the common "expanded" triangle head).
        //
        // Method: one four-point polygon (outer vertex = center, each arm has inner
        // and outer corner points) + two endpoint circles.
        // Why not LineStrip: FrameCollector::AddGeometry only accepts
        // DrawMode::Triangles; line modes are skipped with a one-time log - nothing
        // gets drawn. Why two extra circles: the polygon ends in right-angle cuts;
        // adding a circle of radius = half the thickness makes round caps, matching
        // the library-wide "thin lines are rounded rects / round caps" look.
        //
        // The tip side stays sharp (no circle) - that is the direction the arrow
        // "points"; rounding it would hide the orientation.
        inline void PaintChevron(FrameCollector& fc, float cx, float cy, float size,
            float thickness, ChevronDir dir, const Color4f& color) {
            if (size <= 0.0f || thickness <= 0.0f || color.a <= 0.0f) return;

            // Direction unit vector (screen coordinates, Y axis down)
            float ax = 0.0f, ay = 0.0f;
            switch (dir) {
            case ChevronDir::Left:  ax = -1.0f; ay = 0.0f;  break;
            case ChevronDir::Right: ax = 1.0f;  ay = 0.0f;  break;
            case ChevronDir::Up:    ax = 0.0f;  ay = -1.0f; break;
            case ChevronDir::Down:  ax = 0.0f;  ay = 1.0f;  break;
            }

            const float ux = -ax, uy = -ay;   // "Backward" direction (the arms spread from the center backward)
            const float px = -ay, py = ax;    // Perpendicular vector

            // Two arm endpoints (center + backward * size ± perpendicular * size)
            const float e1x = cx + ux * size + px * size;
            const float e1y = cy + uy * size + py * size;
            const float e2x = cx + ux * size - px * size;
            const float e2y = cy + uy * size - py * size;

            // Two CONVEX quads (one per arm), not one concave hexagon: ear clipping
            // does support concave polygons, but a thick V's notch-side vertices are
            // extremely close and can numerically degenerate ("neither convex nor
            // contained"). Two convex quads ALWAYS hold (convex polygons ear-clip
            // cleanly). Each quad = center outer/inner two points + endpoint
            // outer/inner two points, ordered so it does not self-intersect.
            const float h = thickness * 0.5f;

            const auto armQuad = [&](float ex, float ey) {
                const std::vector<Vec2> q{
                    Vec2{ cx + px * h, cy + py * h },   // center outer
                    Vec2{ ex + px * h, ey + py * h },   // endpoint outer
                    Vec2{ ex - px * h, ey - py * h },   // endpoint inner
                    Vec2{ cx - px * h, cy - py * h },   // center inner
                };
                fc.AddShape(PolygonShape(q, true), color, 0.f, 0.f, 0.f, 0.f, 0.f);
            };

            armQuad(e1x, e1y);
            armQuad(e2x, e2y);

            // Round caps: a circle of radius = h at each end (quad endpoints are right-angle cuts)
            PaintCircle(fc, e1x, e1y, h, color);
            PaintCircle(fc, e2x, e2y, h, color);
        }

        // =================================================================
        //  Alignment / box geometry / ellipsis - consolidated 2026-11-07
        // =================================================================
        //  These used to have one copy per widget (and the semantics had already
        //  started to diverge):
        //   - alignment conversion: TextWidget::alignOffsetX / RichText's AlignOffset / inline in Splitter;
        //   - box inset: five copies across Slider / ProgressBar / Checkbox / RadioButton / ComboBox;
        //   - ellipsis: five copies across TableView / TreeView / Toast / MenuModal / SubWindow,
        //     two of them truncating bytes inconsistently with drawutil::Utf8Boundaries.
        //  Unified here: one semantics, one place to fix.

        // Horizontal alignment conversion: within leftover space rest, Left = 0 / Center = rest/2 / Right = rest.
        // TextWidget::alignOffsetX forwards here (that protected interface stays,
        // because derived classes keep using it; the implementation is only this one).
        inline float AlignOffsetX(TextHAlign a, float rest) {
            switch (a) {
            case TextHAlign::Left:   return 0.0f;
            case TextHAlign::Center: return rest * 0.5f;
            case TextHAlign::Right:  return rest;
            }
            return 0.0f;
        }

        // Vertical alignment conversion: within leftover space rest, Top = 0 / Middle = rest/2 / Bottom = rest.
        inline float AlignOffsetY(TextVAlign a, float rest) {
            switch (a) {
            case TextVAlign::Top:    return 0.0f;
            case TextVAlign::Middle: return rest * 0.5f;
            case TextVAlign::Bottom: return rest;
            }
            return 0.0f;
        }

        // Horizontal offset of one line inside its own row (rest = available width - line width).
        // Returns the alignment offset only when the line fits; returns 0 when it
        // does not (flush left, clipping finishes the job).
        // Do NOT stack this on top of the whole text block's offset - that would
        // center twice (symptom: a SetTextAlign(Center) input's text hugging the
        // right edge).
        inline float LineAlignX(TextHAlign a, float availW, float lineW) {
            const float rest = availW - lineW;
            return (rest > 0.0f) ? AlignOffsetX(a, rest) : 0.0f;
        }

        // Content area "extra space" = padding + border (self-described sizes must add it).
        // box: box style; dx,dy: out params, how much to add horizontally / vertically.
        inline void BoxInsets(const BoxStyle& box, float& dx, float& dy) {
            const float bt = std::max({ box.border.width[0], box.border.width[1],
                                        box.border.width[2], box.border.width[3] });
            dx = box.padding.left + box.padding.right + bt * 2.0f;
            dy = box.padding.top + box.padding.bottom + bt * 2.0f;
        }

        // Capsule radius = half the short side (clamped to the short side so edges match the geometry).
        inline float CapsuleRadius(float w, float h) {
            return 0.5f * std::min(w, h);
        }

        // Paint one shadow using the box's own shadow params (the same rounded rect, expanded + translated).
        // box: provides shadow (enabled / offset / spread / color) and opacity.
        // Same approach as BoxWidget::paintShadow. For boxes that are NOT this
        // widget's box_ (popover boxes etc.) - the base class only paints its own.
        inline void PaintBoxShadow(FrameCollector& fc, const BoxStyle& box,
            float x, float y, float w, float h, float radius) {
            const Shadow& sh = box.shadow;
            if (!sh.enabled || w <= 0.0f || h <= 0.0f) return;

            Color4f sc = sh.color;
            sc.a *= box.opacity;
            if (sc.a <= 0.0f) return;

            const float ex = sh.spread;
            PaintRoundedRect(fc, x + sh.offsetX - ex, y + sh.offsetY - ex,
                w + 2.0f * ex, h + 2.0f * ex, radius, sc);
        }

        // Split by '\n' (keeps empty lines; returns at least one line).
        // Toast and Tooltip each had a word-for-word identical copy before.
        inline std::vector<std::string> SplitLines(std::string_view s) {
            std::vector<std::string> out;
            std::size_t start = 0;
            while (true) {
                const std::size_t at = s.find('\n', start);
                if (at == std::string_view::npos) { out.emplace_back(s.substr(start)); break; }
                out.emplace_back(s.substr(start, at - start));
                start = at + 1;
            }
            if (out.empty()) out.emplace_back();
            return out;
        }

        // Truncate one line of text to the available width (returns it as-is when it
        // fits; otherwise appends U+2026).
        // fc: frame collector (measuring and drawing share one source).
        // text: text (UTF-8); font: the font; avail: available width (<= 0 returns an empty string).
        // Returns the truncated text; an empty string when even the ellipsis does not fit.
        //
        // Division of labor vs Ellipsize: that one assumes "it does not fit" (the
        // caller measured first); this one MEASURES FIRST - list / table / toast
        // contexts where every line may be short use this, so call sites do not each
        // write "return text if it fits".
        //
        // Cuts only at character boundaries (via Utf8Boundaries) and advances by
        // binary search - for long text in narrow cells, per-character measuring is
        // O(n^2) glyph metrics.
        inline std::string EllipsizeToWidth(FrameCollector& fc, std::string_view text,
            const FontDesc& font, float avail) {
            if (avail <= 0.0f) return std::string();
            if (text.empty()) return std::string();
            if (fc.MeasureText(text, font).width <= avail) return std::string(text);
            return Ellipsize(fc, text, font, avail);
        }

    } // namespace drawutil
} // namespace Kit
