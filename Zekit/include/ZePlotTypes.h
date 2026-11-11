/**
 * @file ZePlotTypes.h
 * @brief Plot data contract: PlotVertex (vertex) and PlotText (text annotation)
 *
 * @details
 * This file is the only agreement between the data-producing side and the data-drawing side.
 * It contains only two pure data types, includes no other Zekit header, and depends on no graphics API:
 *
 *  - The data-producing side (business / algorithm layer) includes this file to produce vertices;
 *  - The data-drawing side (Kit::PlotView) reads the same structures and draws them.
 *
 * Thus the data-producing side does not need to know PlotView, nor link a graphics library --
 * switching the render backend (OpenGL / Vulkan) is irrelevant to it.
 *
 * @note Coordinates are always local pixel coordinates of the widget content area: the origin is at
 *       the top-left of the content area, X to the right, Y downward, in pixels, with no DPI
 *       scaling (consistent with the coordinate system of the whole library).
 *       Vertex data does not need to know the widget position in the window -- that is handled by
 *       PlotView when appending, via translation.
 *
 * @see ZePlotView.h the widget that consumes this data
 */

#pragma once

#include "color4f.h"      // Color4f (the default color source for PlotText)

#include <cstddef>
#include <string>

namespace Kit {

    /**
     * @brief A plot vertex: position + color
     *
     * @details
     * Vertices are organized as triangles: every three consecutive vertices form one triangle, and the
     * whole batch is concatenated end to end, with no index needed -- whatever is appended is drawn.
     *
     * @note Vertex winding order (clockwise / counter-clockwise) is arbitrary: neither render backend
     *       performs face culling.
     *
     * Each vertex carries its own color, so colors can vary per vertex within one batch (gradients,
     * red for up and green for down all rely on this).
     *
     * @note The six fields are tightly packed with no padding -- the whole vertex batch can be passed
     *       to the render layer directly as contiguous memory without constructing each vertex
     *       individually (PlotView does exactly this internally).
     * @note A vertex itself carries no primitive type: the render pipeline accepts only triangles. To
     *       draw a line, widen it into a quad and split it into two triangles; to draw a point, split
     *       it into a small square.
     *       This conversion is done on the data-producing side (pure geometry, applicable to any
     *       backend).
     */
    struct PlotVertex {
        float x = 0.0f;   /// < X coordinate (widget content area pixels, origin at top-left)
        float y = 0.0f;   /// < Y coordinate (widget content area pixels, Y axis downward)
        float r = 0.0f;   /// < Red component (0.0 ~ 1.0)
        float g = 0.0f;   /// < Green component (0.0 ~ 1.0)
        float b = 0.0f;   /// < Blue component (0.0 ~ 1.0)
        float a = 1.0f;   /// < Opacity (0.0 = fully transparent, 1.0 = fully opaque)
    };

    static_assert(sizeof(PlotVertex) == 6 * sizeof(float),
        "PlotVertex must be exactly 6 tightly packed floats");

    /**
     * @brief Horizontal alignment of the text anchor relative to x
     *
     * @details
     * Tick labels have different placement conventions: price axis numbers are usually right-aligned
     * (flush against the axis), while time axis dates are usually centered on the tick line. This enum
     * determines which edge the passed-in `x` refers to.
     */
    enum class PlotTextAlign {
        Left,     /// < x is the left edge of the text
        Center,   /// < x is the horizontal midpoint of the text
        Right,    /// < x is the right edge of the text
    };

    /**
     * @brief A piece of annotation text
     *
     * @details
     * Vertices can only draw shapes, not text -- text is a separate channel sampled from a glyph
     * atlas (glyph textures, metrics, and line breaking all live in the render layer), so the canvas
     * must carry text anchors itself; otherwise annotations such as axis ticks and cursor values
     * could not travel together with the data.
     *
     * @note `y` is the top edge of the text (not the baseline, not the center) -- consistent with the
     *       typesetting semantics of the widget layer of this library; the render layer converts it to
     *       a baseline internally.
     * @note The font exposes only two switches: font size + bold. Annotations on the canvas need only
     *       these two; for other fonts, place a Kit::Label widget directly.
     */
    struct PlotText {
        std::string   text;                            /// < Text content (UTF-8, may contain '\n' line breaks)
        float         x = 0.0f;                        /// < Anchor X coordinate (meaning determined by align)
        float         y = 0.0f;                        /// < Y coordinate of the text top edge
        unsigned      pixelSize = 12;                  /// < Font size (pixels)
        bool          bold = false;                    /// < Whether bold
        PlotTextAlign align = PlotTextAlign::Left;     /// < Which edge of the text x refers to
        Color4f       color = Color4f(0.0f, 0.0f, 0.0f, 1.0f);   /// < Text color (default opaque black)
    };

} // namespace Kit
