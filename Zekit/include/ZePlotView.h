/**
 * @file ZePlotView.h
 * @brief Plot view: draws a batch of triangle vertices plus text labels
 *
 * @details
 * This widget is a canvas: it does not know what the data means, it only knows two things --
 * a sequence of triangle vertices (PlotVertex) and a sequence of text anchors (PlotText).
 * All business concepts (candlesticks, MACD, moving averages, scatter points) stay on the side that produces the data.
 *
 * Division of labor:
 *  - Compute data -- the business / algorithm layer splits shapes into triangles and fills them into std::vector<PlotVertex>;
 *  - Place positions -- vertex coordinates use content-area local pixels; this widget translates them to its own position
 *    and clips out-of-bounds parts by the clip region given by the container;
 *  - Paint -- submitted uniformly by the window each frame, on the same render path as other widgets.
 *
 * The payoff of this split: axis-aligned shapes follow the widget without recomputing vertices, and overlays / clipping /
 * rounded corners / theme / dragging are all handled uniformly by the window; the canvas does not need to know whether the backend is OpenGL or Vulkan.
 *
 * @note Vertex data is read once per frame (Paint is const, the widget does not modify data) -- to change the picture,
 *       the caller calls SetVertices again.
 * @note This widget does no coordinate transform (zoom / pan / grid); those happen on the side that produces vertices:
 *       vertices are already pixels, the canvas only paints. Dragging the view = change camera parameters then re-produce vertices.
 * @note Interaction belongs to the caller: this widget only forwards mouse events as-is (press / move / release /
 *       wheel, coordinates already converted to content-area local), with no interaction semantics. Click vs drag,
 *       how to pan, how much to zoom, which indicator to switch -- all decided by the caller in callbacks -- see
 *       the SetOnMousePress family of interfaces. The canvas itself remembers no view state.
 *
 * @see ZePlotTypes.h data contract for vertices and text
 * @see ZeBoxWidget.h box layer (background / border / padding)
 */

#pragma once
#include "ZeBoxWidget.h"
#include "ZePlotTypes.h"

#include <functional>
#include <vector>

namespace Kit {

    /**
     * @brief One mouse event received on the canvas (coordinates already converted to content-area local)
     *
     * @details
     * The only difference from MouseEvent is the coordinates. MouseEvent::x/y are window client-area
     * absolute coordinates, while canvas vertices are content-area local pixels (origin at the content-area top-left, with
     * widget position, four-side border, and four-side padding already subtracted). When the caller uses it to compute "which candlestick was hit",
     * it should not subtract again -- so the converted values are given directly here.
     *
     * @note Not clipped: during dragging the mouse can be outside the canvas, in which case x/y will be negative or exceed
     *       the content-area size -- this is intentional; dragging relies on dx/dy and does not need the point inside the area.
     * @note Pure value object, use as needed; not guaranteed valid after the callback returns.
     */
    struct PlotMouseEvent {
        float x = 0.0f;      /// < Content-area local X (pixels, origin at content-area top-left)
        float y = 0.0f;      /// < Content-area local Y (pixels, Y axis downward)

        /// @brief Displacement (pixels) compared with the previous event of the same kind
        /// @note Move events use it for panning; it is always 0 in press events. The first move's displacement is
        ///       computed relative to the press point, so small jitter after pressing is reflected faithfully.
        float dx = 0.0f;     /// < Horizontal displacement (right positive)
        float dy = 0.0f;     /// < Vertical displacement (down positive)

        int wheelDelta = 0;  /// < Wheel delta (plus or minus 120 per notch, positive = scroll forward / zoom-in direction)

        MouseButton       button = MouseButton::None;                  /// < Triggering button (None in move events)
        KeyboardModifiers modifiers = KeyboardModifiers::NoModifier;   /// < Modifier keys held at the same time
    };

    /// @brief Canvas mouse callback -- gives facts only, no semantics (how to split click / drag is up to the caller)
    /// @note Just an alias for std::function<void(const PlotMouseEvent&)>, shorter to write.
    using PlotMouseAction = std::function<void(const PlotMouseEvent&)>;

    /**
     * @brief Canvas: displays a batch of triangle vertices and text labels computed externally
     *
     * @details
     * Inherits BoxWidget, so background color / border / rounded corners / padding / shadow / outline / opacity
     * all interfaces are available; the theme defaults to the plot section (opaque background + 1px border + zero padding).
     *
     * Padding defaults to 0: vertex coordinates are content-area coordinates, and padding would make the drawing origin
     * mismatch the data. If padding is really needed (e.g. leaving room for tick text around the edges),
     * call SetPadding -- the vertex origin moves together with the content area.
     */
    class PlotView : public BoxWidget {
    public:
        /// @brief Constructs the canvas
        /// @note Default style uses the theme's plot section; default size 320 x 200 pixels --
        ///       no content auto-sizing (the canvas cannot compute the size of vertex data), give the size explicitly:
        ///       SetGeometry / SetSize, or place it in a layout and let flexGrow expand it.
        PlotView();

        // -------------------------------------------------------------------
        //  Data
        // -------------------------------------------------------------------

        /**
         * @brief Sets vertices (whole-batch replacement, not append)
         * @param verts Triangle vertex array: every 3 consecutive vertices form one triangle;
         *              when the count is not a multiple of 3, trailing vertices fewer than 3 are ignored
         * @note Pass by value + internal move: you can call SetVertices(std::move(v)) directly,
         *       or pass a temporary -- consistent with the library-wide value semantics.
         * @note Empty array = draw nothing (independent of background color; background still shows).
         * @see vertices
         */
        void SetVertices(std::vector<PlotVertex> verts);

        /**
         * @brief Sets text labels (whole-batch replacement, not append)
         * @param texts Segments of text + anchors; entries with empty text are skipped
         * @see texts
         */
        void SetTexts(std::vector<PlotText> texts);

        /// @brief Current vertex array (read-only; to change, call SetVertices with a new batch)
        const std::vector<PlotVertex>& vertices() const { return verts_; }

        /// @brief Current text labels (read-only)
        const std::vector<PlotText>& texts() const { return texts_; }

        /// @brief Clears vertices and text (keeps size and style; background still shows)
        void Clear();

        // -------------------------------------------------------------------
        //  Content-area size
        // -------------------------------------------------------------------

        /**
         * @brief Returns the content-area size (pixels) -- the rectangle shared by vertex coordinates and mouse coordinates
         * @param[out] w Content-area width (= widget width - left/right border - left/right padding)
         * @param[out] h Content-area height (= widget height - top/bottom border - top/bottom padding)
         *
         * @details
         * This widget does no content auto-sizing (the canvas cannot compute the size of vertex data), so the caller
         * needs to know "how much can be drawn" when computing mappings: filling the canvas with data, zooming anchored at the cursor,
         * and determining which candlestick was hit all need these two numbers.
         *
         * @note Do not substitute width() / height(): those include border and padding,
         *       and using them for coordinates shifts everything by a few pixels (click here, draw there).
         * @note Returns 0 when border plus padding consume the whole widget.
         */
        void contentSize(float& w, float& h) const;

        // -------------------------------------------------------------------
        //  Mouse interaction (forwards events only, does not interpret them)
        // -------------------------------------------------------------------
        //  The four outlets correspond to four kinds of raw events; coordinates are already converted to content-area local.
        //
        //  The canvas remembers no view state: there is no concept of "current zoom" "current offset" "current indicator",
        //  and it does not know what the mouse is doing. Thus the same set of interfaces supports arbitrary interaction --
        //  drag to pan, wheel to zoom, click to switch chart, rubber-band selection, crosshair, double-click to reset...
        //
        //  When a callback is empty (default), the event is not consumed and continues bubbling to the parent widget;
        //  once a callback is set, the corresponding event is consumed by this widget (returns true).
        // -------------------------------------------------------------------

        /**
         * @brief Sets the mouse-press callback
         * @param handler Callback; pass empty (nullptr / {}) to clear
         * @note dx/dy is always 0 in the event, and the displacement baseline is reset to the press point --
         *       so the first move after pressing has displacement equal to "how far from the press point".
         * @note To distinguish which button, see PlotMouseEvent::button.
         * @see SetOnMouseMove, SetOnMouseRelease
         */
        void SetOnMousePress(PlotMouseAction handler);

        /**
         * @brief Sets the mouse-move callback
         * @param handler Callback; pass empty to clear
         *
         * @note Moves while not pressed do not trigger a repaint (hundreds of mouse events per second; unconditional frame
         *       requests would drag down the event-driven mode). Only while a mouse button is held does this widget consume move events
         *       and make the window request a frame -- so drag following is smooth.
         * @note To update the picture also while not pressed (crosshair, hover highlight), call
         *       window()->RequestRedraw() yourself in the callback.
         * @note During dragging the mouse can leave the canvas; x/y will be negative or exceed the content area,
         *       so pan by dx/dy -- there is no need and no reason to clamp coordinates back into the area.
         * @see SetOnMousePress
         */
        void SetOnMouseMove(PlotMouseAction handler);

        /**
         * @brief Sets the mouse-release callback
         * @param handler Callback; pass empty to clear
         * @note Always arrives: even if the mouse is released outside the canvas -- the window sends the release event
         *       back to the widget hit at press time. So click-vs-drag determination can safely use
         *       the distance between press point and release point, without worrying about lost events.
         * @see SetOnMousePress
         */
        void SetOnMouseRelease(PlotMouseAction handler);

        /**
         * @brief Sets the mouse-wheel callback
         * @param handler Callback; pass empty to clear
         * @note PlotMouseEvent::wheelDelta is the raw delta, one notch = plus or minus 120;
         *       the sign direction is decided by the system, the zoom factor is decided by the caller.
         * @note x/y is the cursor position -- to "zoom centered on the cursor", use it as the anchor.
         */
        void SetOnMouseWheel(PlotMouseAction handler);

    protected:
        /// @brief Paints: vertices are translated to the content-area origin then appended; text landing points are converted by alignment
        void paintContent(FrameCollector& fc) const override;

        /// @brief Default style uses the theme's plot section (t.plot)
        const BoxStyle& themeBox(const Theme& t) const override;

        // Event virtual functions: convert window coordinates to content-area local then forward to callbacks.
        // Return value = whether consumed (true means no further bubbling, and one frame request is triggered).
        bool OnMousePress(MouseEvent& e) override;
        bool OnMouseMove(MouseEvent& e) override;
        bool OnMouseRelease(MouseEvent& e) override;
        bool OnMouseWheel(MouseEvent& e) override;

    private:
        /// @brief Coordinates of the content-area top-left in the window client area (= widget position + left border + left padding)
        /// @note Uses the same basis as paintContent; the two must agree, so it is factored into one function.
        void contentOrigin(float& cx, float& cy) const;

        /// @brief Window client-area coordinates -> content-area local coordinates
        void toLocal(float winX, float winY, float& lx, float& ly) const;

        std::vector<PlotVertex> verts_;   /// < Triangle vertices to paint this frame
        std::vector<PlotText>   texts_;   /// < Text labels to paint this frame

        // Mouse callbacks (std::function is movable, moved along with this class's implicit move constructor by AddWidget)
        PlotMouseAction onPress_;      /// < Press callback
        PlotMouseAction onMove_;       /// < Move callback
        PlotMouseAction onRelease_;    /// < Release callback
        PlotMouseAction onWheel_;      /// < Wheel callback

        // Displacement baseline: content-area local coordinates of the previous event (reset on press, used to compute dx/dy)
        float lastX_ = 0.0f;
        float lastY_ = 0.0f;
        bool  lastValid_ = false;      /// < false when no event has been received yet (dx/dy is 0 in that case)

        bool  held_ = false;           /// < Whether a mouse button is currently pressed on the canvas (decides whether move events are consumed)
    };

} // namespace Kit
