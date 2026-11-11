#pragma once
/**
 * @file ZeIndicatorPaint.h
 * @brief Indicator paint primitives (library-internal header, not part of the delivery surface)
 *
 * @details
 * `Checkbox` and `RadioButton` paint the same set of parts (container + mark); the only difference is
 * which enum decides what gets painted. Painting is collected here so that:
 *
 *  - One geometry: the container's corner radius/inset, the mark's proportions, and the line width factor are written once --
 *    writing them separately in each widget inevitably produces "the checkbox tick and the radio tick look different";
 *  - One place to extend styles: adding a new container/mark only changes this file, and both widgets get it automatically;
 *  - Clean delivery surface: primitive types (`Shape` / `Geometry` / `Vec2`) stay inside the library,
 *    and only enums appear in public headers (see `include/ZeIndicator.h`).
 *
 * All inline: this file is included only by these two cpp files and has no separate translation unit --
 * so it does not increase the member count of `Zekit.lib` (the member-count criterion before release is unchanged).
 *
 * @par Coordinate system
 * All coordinates are absolute client-area coordinates (consistent with what `contentRect()` gives in `paintContent`).
 * `(x, y, size)` describes a square outer box: the container fills it, and the mark is painted inside it.
 *
 * @par Anti-aliasing
 * Rounded rectangles use SDF (the five SDF parameters of `AddShape`), and edges have a 1px coverage transition;
 * diamonds / stars / slashes are polygons and quads with no SDF, relying only on MSAA
 * (4x on the GL backend, 1x on Vulkan). This is the same existing tradeoff as `LineShape`,
 * and it is not visible on 18~20px indicators.
 *
 * @see ZeCheckbox.h  ZeRadioButton.h  include/ZeIndicator.h
 */

#include <algorithm>
#include <cmath>
#include <vector>

#include "include/ZeIndicator.h"
#include "ZeFrameCollector.h"
#include "ZePolygonShape.h"
#include "ZeRoundedRectShape.h"
#include "ZeLineShape.h"

namespace Kit {
    namespace indicator {

        /// @brief Paints a rounded rectangle (all five SDF parameters must be passed; omitting them degrades corners to hard edges)
        inline void PaintRoundRect(FrameCollector& fc, float x, float y, float w, float h,
            float radius, const Color4f& color) {
            if (w <= 0.0f || h <= 0.0f || color.a <= 0.0f) return;
            const float r = std::min(radius, std::min(w, h) * 0.5f);
            fc.AddShape(RoundedRectShape(Vec2{ x, y }, Vec2{ w, h }, r), color,
                x + w * 0.5f, y + h * 0.5f, w * 0.5f, h * 0.5f, r);
        }

        /// @brief Paints a thick line segment (when thickness > 1, LineShape generates a quad that can enter the triangle pipeline)
        inline void PaintSegment(FrameCollector& fc, Vec2 a, Vec2 b, float thickness,
            const Color4f& color) {
            if (color.a <= 0.0f || thickness <= 0.0f) return;
            fc.AddGeometry(LineShape(a, b, thickness).build(), color);
        }

        /// @brief Paints a solid polygon (no SDF -- relies on MSAA)
        inline void PaintPolygon(FrameCollector& fc, std::vector<Vec2> pts, const Color4f& color) {
            if (pts.size() < 3 || color.a <= 0.0f) return;
            fc.AddGeometry(PolygonShape(std::move(pts), true).build(), color);
        }

        /// @brief Scales a set of vertices about the center (used for the "inset stroke" of diamonds/stars)
        inline std::vector<Vec2> ScaleAround(std::vector<Vec2> pts, float cx, float cy, float k) {
            for (Vec2& p : pts) {
                p.x = cx + (p.x - cx) * k;
                p.y = cy + (p.y - cy) * k;
            }
            return pts;
        }

        /// @brief Four diamond vertices (the inscribed diamond of the square outer box (x,y,size))
        inline std::vector<Vec2> DiamondPoints(float x, float y, float size) {
            const float cx = x + size * 0.5f, cy = y + size * 0.5f;
            return { { cx, y }, { x + size, cy }, { cx, y + size }, { x, cy } };
        }

        /// @brief Ten five-pointed-star vertices (point up, outer radius outR, inner radius inR)
        inline std::vector<Vec2> StarPoints(float cx, float cy, float outR, float inR) {
            std::vector<Vec2> pts;
            pts.reserve(10);
            for (int i = 0; i < 10; ++i) {
                // Starting at -90 degrees: the first point is the tip directly above
                const float a = -1.57079632679f + static_cast<float>(i) * 0.6283185307f; // π/5
                const float r = (i % 2 == 0) ? outR : inR;
                pts.push_back(Vec2{ cx + std::cos(a) * r, cy + std::sin(a) * r });
            }
            return pts;
        }

        /**
         * @brief Paints the container (the ring/box around the mark)
         * @param x,y,size  Square outer box (absolute client-area coordinates)
         * @param fill      Fill color inside the container (solid state = accent color, hollow state = indicator background color)
         * @param border    Border color; treated as "no border" when `a <= 0` or `borderW <= 0`
         * @param borderW   Border width (pixels)
         * @note This library has no "stroke" primitive; the border is approximated with concentric double shapes: first paint the outer layer in `border` color,
         *       then cover the inset shape with `fill` -- for rounded rectangles the inset directly reduces the radius,
         *       and for polygons the inset scales proportionally about the center (a diamond scaled this way is still a diamond, so the shape does not distort).
         */
        inline void PaintContainer(FrameCollector& fc, IndicatorShape shape,
            float x, float y, float size,
            const Color4f& fill, const Color4f& border, float borderW) {
            if (size <= 0.0f) return;
            const bool hasBorder = (borderW > 0.0f && border.a > 0.0f);

            if (shape == IndicatorShape::Diamond) {
                const float cx = x + size * 0.5f, cy = y + size * 0.5f;
                const std::vector<Vec2> outer = DiamondPoints(x, y, size);
                if (hasBorder) {
                    PaintPolygon(fc, outer, border);
                    const float inner = size - 2.0f * borderW;
                    if (inner > 0.0f)
                        PaintPolygon(fc, ScaleAround(outer, cx, cy, inner / size), fill);
                }
                else {
                    PaintPolygon(fc, outer, fill);
                }
                return;
            }

            float radius = 0.0f;
            switch (shape) {
            case IndicatorShape::Square:  radius = std::min(4.0f, size * 0.22f); break;
            case IndicatorShape::Rounded: radius = size * 0.34f; break;
            case IndicatorShape::Circle:  radius = size * 0.5f; break;
            default:                      radius = std::min(4.0f, size * 0.22f); break;
            }

            if (hasBorder) {
                PaintRoundRect(fc, x, y, size, size, radius, border);
                const float inner = size - 2.0f * borderW;
                if (inner > 0.0f)
                    PaintRoundRect(fc, x + borderW, y + borderW, inner, inner,
                        std::max(0.0f, radius - borderW), fill);
            }
            else {
                PaintRoundRect(fc, x, y, size, size, radius, fill);
            }
        }

        /// @brief Check mark (the checkbox and radio button share the same shape -- ticks in the same library should look the same)
        inline void PaintTick(FrameCollector& fc, float x, float y, float size, const Color4f& c) {
            const float t = std::max(1.4f, size * 0.16f);
            const Vec2 p1{ x + size * 0.24f, y + size * 0.52f };
            const Vec2 p2{ x + size * 0.43f, y + size * 0.71f };
            const Vec2 p3{ x + size * 0.77f, y + size * 0.30f };
            PaintSegment(fc, p1, p2, t, c);
            PaintSegment(fc, p2, p3, t, c);
        }

        /// @brief Horizontal bar (thick horizontal line, centered)
        inline void PaintBar(FrameCollector& fc, float x, float y, float size, const Color4f& c) {
            const float h = std::max(1.4f, size * 0.16f);
            PaintRoundRect(fc, x + size * 0.24f, y + (size - h) * 0.5f,
                size * 0.52f, h, h * 0.5f, c);
        }

        /**
         * @brief Paints the check mark (all values of `CheckMark`)
         * @note `Fill` paints no symbol -- it expresses "the whole block is accent color", and the container has already painted it.
         *       Callers may still call this function as usual; it is a no-op here.
         */
        inline void PaintCheckMark(FrameCollector& fc, CheckMark mark,
            float x, float y, float size, const Color4f& color) {
            if (size <= 0.0f || color.a <= 0.0f) return;
            const float cx = x + size * 0.5f, cy = y + size * 0.5f;

            switch (mark) {
            case CheckMark::Tick:
                PaintTick(fc, x, y, size, color);
                break;

            case CheckMark::Cross: {
                const float t = std::max(1.4f, size * 0.16f);
                const float a0 = size * 0.30f, a1 = size * 0.70f;
                PaintSegment(fc, Vec2{ x + a0, y + a0 }, Vec2{ x + a1, y + a1 }, t, color);
                PaintSegment(fc, Vec2{ x + a1, y + a0 }, Vec2{ x + a0, y + a1 }, t, color);
                break;
            }

            case CheckMark::Plus: {
                const float t = std::max(1.4f, size * 0.16f);
                const float a0 = size * 0.28f, a1 = size * 0.72f;
                PaintSegment(fc, Vec2{ x + a0, cy }, Vec2{ x + a1, cy }, t, color);
                PaintSegment(fc, Vec2{ cx, y + a0 }, Vec2{ cx, y + a1 }, t, color);
                break;
            }

            case CheckMark::Dot: {
                const float d = size * 0.34f;
                PaintRoundRect(fc, cx - d * 0.5f, cy - d * 0.5f, d, d, d * 0.5f, color);
                break;
            }

            case CheckMark::Bar:
                PaintBar(fc, x, y, size, color);
                break;

            case CheckMark::Fill:
                break;      // The container itself is fully colored, so there is no symbol to paint

            case CheckMark::Star:
                PaintPolygon(fc, StarPoints(cx, cy, size * 0.40f, size * 0.17f), color);
                break;
            }
        }

        /**
         * @brief Paints the radio mark (all values of `RadioMark`)
         * @param shape Container shape -- only `Ring` uses it (the hole must match the outer box shape: a square container gets a square hole, a diamond gets a diamond hole)
         * @param glyph Symbol color painted on the solid accent color (usually the container background color; see RadioButton::paintContent)
         * @param hole  Color used to cut out the `Ring` hole (container background color)
         * @note `Fill` works the same way as `CheckMark::Fill`: the container is already fully colored, so this is a no-op.
         *
         * @details
         * `Ring` works differently from other marks: the container (`PaintContainer`) is already filled with the accent color,
         * so here we only need to cut out a center piece -- what remains is naturally a uniform ring, with no need to "fill first and then cut".
         * The hole uses an inset shape with the same shape as the container, so a square container yields a square ring and a diamond yields a diamond ring.
         *
         * WARNING: Fixed 2026-10-31: previously both `glyph` and `hole` were passed the container background color, so the ring erased itself,
         *    which appeared as "the selected state looks like an unselected hollow ring". The parameters are passed separately so that this mistake fails to compile.
         */
        inline void PaintRadioMark(FrameCollector& fc, RadioMark mark, IndicatorShape shape,
            float x, float y, float size, const Color4f& glyph, const Color4f& hole) {
            if (size <= 0.0f) return;
            const float cx = x + size * 0.5f, cy = y + size * 0.5f;

            switch (mark) {
            case RadioMark::Dot: {
                const float d = size * 0.42f;
                PaintRoundRect(fc, cx - d * 0.5f, cy - d * 0.5f, d, d, d * 0.5f, glyph);
                break;
            }

            case RadioMark::Fill:
                break;      // Solid circle = container fully colored

            case RadioMark::Ring: {
                // The container is already fully colored -> cutting the hole yields the ring. Ring width = 24% of the side length (0.24 on both sides -> the center 52% is the hole).
                const float inset = size * 0.24f;
                const float inner = size - 2.0f * inset;
                if (inner > 0.0f && hole.a > 0.0f) {
                    PaintContainer(fc, shape, x + inset, y + inset, inner,
                        hole, Color4f{ 0.0f, 0.0f, 0.0f, 0.0f }, 0.0f);
                }
                break;
            }

            case RadioMark::Tick:
                PaintTick(fc, x, y, size, glyph);
                break;

            case RadioMark::Bar:
                PaintBar(fc, x, y, size, glyph);
                break;
            }
        }

    } // namespace indicator
} // namespace Kit
