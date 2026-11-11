/**
 * @file ZeTooltip.h
 * @brief Kit::Tooltip -- a tooltip bubble that shows on hover (pure overlay widget)
 *
 * @details
 * A small panel floating above the interface: it can be a single sentence, or rich content of
 * "icon + title + multi-line body". `ShowAt` / `ShowFor` pop immediately, `RequestShowFor` pops
 * after a delay (the default hover rhythm), `Hide` / `Dismiss` retract. It belongs to the same
 * family as `MenuModal` (the same three-half overlay mechanism), but does not block input --
 * the bubble is "something to look at", any mouse event passes through to the widget below.
 *
 * @par Three-half mechanism (same set as the ComboBox popup / MenuModal, zero new library capability)
 *  1. Paint: `paintsOnTop()` returns true while shown -> deferred to the second paint pass, always
 *     above ordinary widgets;
 *  2. Hit: `HitTest` always returns false -- the bubble does not participate in hit testing at
 *     all, mouse events pass through to the widget below. This is the most critical difference
 *     from `MenuModal`: both are overlays, one (modal) grabs input, one (tooltip) does not;
 *  3. Clip: `paintContent()` starts with `fc.ResetClip()` to escape the ancestor clip region, so
 *     a widget inside a scroll area can still pop a complete bubble.
 *
 * @par Background image (bubble image fill, actually painted since 2026-11-06)
 * The interface is entirely in the `BoxWidget` wall of setters (`SetBackgroundImage` /
 * `ClearBackgroundImage` / `backgroundImagePath` / `SetBackgroundDrawMode` / `SetBackgroundAlign` /
 * `SetBackgroundTint`), this class does not redeclare them. Previously calling them had no
 * effect -- then `paintPopupInto` only drew the shape, not the image; now the image is drawn
 * "after the fill / vertical bar, before the text", so the text always sits on top of the image.
 *
 * Geometry follows the shape (the image is an axis-aligned rectangle, the pipeline has no
 * rounded / circular mask):
 * | Shape | Where the image lands |
 * |---|---|
 * | `Square` / `Rounded` / `Balloon` / `Card` / `Glass` / `Pill` | bubble rect minus border, then inset on all four sides by the "shoulder width" `r x (1 - 1/sqrt2)` -- corners do not poke out of the rounded corners |
 * | `Circle` / `Info` | inscribed square of the circle (side = diameter x 0.707) |
 * | `Star` | inscribed square computed from the inner radius (side = 0.42R x 1.414) -- does not pierce the star points |
 *
 * @note Image opacity follows only the fade-in (`tint.a x fade`); the `Glass` shape additionally
 *       multiplies by `glassAlpha` -- otherwise translucent glass becomes an opaque block once
 *       an image is applied.
 * @note To fill edge to edge (no shoulder inset): switch to `SetShape(TooltipShape::Square)`,
 *       the inset for a right-angled container is 0.
 *
 * @par Four usages (from simplest to finest)
 * @code
 * // (1) One-line tooltip (recommended): the widget carries its own tooltip text, the window
 * //     handles popping and retracting
 * saveBtn.SetTooltip("Save current config");
 * helpBtn.SetTooltip(Kit::TooltipContent{ "Help", "F1 opens the help center", "icon.png" });
 *
 * // (2) Manual pop: pop at window coordinates anywhere
 * Kit::Tooltip tip;
 * win.AddWidget(tip);                    // * mount first (Attach / ShowFor need a window)
 * tip.ShowFor(saveBtn, "Save current config");   // centered below the button, flips above if out of bounds
 *
 * // (3) Auto hover: bind a batch of widgets, show/hide automatically on mouse enter/leave
 * //     (existing widgets like Label can be bound too)
 * tip.Attach(saveBtn, "Save current config");
 * tip.Attach(helpBtn, Kit::TooltipContent{ "Help", "F1 opens the help center" });
 *
 * // (4) Delayed pop: same rhythm as (3), but the caller decides when to "enter"
 * tip.RequestShowFor(saveBtn, Kit::TooltipContent{ "Help", "F1 opens the help center" }, 400);
 * @endcode
 *
 * @par Show rhythm (delay + fade-in, both can be turned off)
 * | Behavior | Default | Interface |
 * |---|---|---|
 * | Hover time before popping | 400ms | `SetShowDelay` |
 * | Pop fade-in duration | 120ms | `SetFadeDuration(0)` turns it off |
 * | Gap between bubble and target | 6px | `SetOffset` |
 * | Whether the arrow is drawn | drawn | `SetArrowEnabled(false)` turns it off |
 *
 * @note Pure overlay: does not enter layout (`hintWidth` / `hintHeight` are always 0, collapsing
 *       to 0 height in a VBox is exactly the expected behavior), the correct usage is
 *       `win.AddWidget(tip)` to mount it at the top level.
 * @note `Attach` uses a window-level event filter (reads the mouse position to decide which
 *       target the pointer is on), so the Tooltip must be mounted to the window first, then
 *       `Attach` called.
 * @note `\n` in the text breaks lines; this widget does not auto-wrap by width (bubble width is
 *       computed from the longest line).
 * @note Delay and fade-in both advance during the render phase (`wantsAnimationFrame` +
 *       `RequestFrameWake`), no timer from the caller is needed and no CPU is burned when idle.
 *
 * @see ZeMenuModal.h same-family overlay widget (modal version)
 * @see ZeComboBox.h  the first implementation of the three-half overlay
 * @see ZeWidget.h    `SetTooltip` (the entry point for one-line tooltips)
 */

#pragma once
#include "ZeTextWidget.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Kit {

    class OverlayWindow;

    /**
     * @brief Which side of the target the bubble shows on (parameter of `Tooltip::SetPlacement`)
     * @details Placed in the Kit namespace (same level as `TooltipShape`) rather than a nested enum
     *          of Tooltip -- the theme layer / callers write `Kit::Placement::Left`, shorter than
     *          `Kit::Tooltip::Placement::Left` and reads consistently.
     */
    enum class Placement {
        Auto,     /// < Below preferred, flips above automatically if it does not fit (default; historical behavior)
        Top,      /// < Force above (clamped into the window if it does not fit, no flip)
        Bottom,   /// < Force below
        Left,     /// < Force left of the target (vertically centered)
        Right     /// < Force right of the target (vertically centered)
    };

    /**
     * @brief Tooltip content (pure data: icon + title + body)
     *
     * @details
     * All three fields may be empty, combined by the "draw if present" rule:
     *  - only `text`: the most common one-line / multi-line tooltip (equivalent to the old
     *    `ShowFor(target, "text")`);
     *  - `title` + `text`: a bold title line, with the body following below;
     *  - `icon`: a small square image on the left (18x18 by `Fit`, size adjustable).
     *
     * @note The icon is a file path (same set as `Image::SetSource` / the `Button` icon), only the
     *       path is stored, not loaded -- decoding and texture upload happen in the render layer.
     * @note `\n` in the body breaks lines; this widget does not auto-wrap by width.
     */
    struct TooltipContent {
        std::string title;   /// < Title (one bold line; may be empty)
        std::string text;    /// < Body (`\n` breaks lines; may be empty)
        std::string icon;    /// < Left icon path (may be empty)

        /// @brief All three items empty (empty content does not pop, `Show*` treats it as retract)
        bool empty() const { return title.empty() && text.empty() && icon.empty(); }

        /// @brief Field-by-field equality (idempotency criterion for delayed requests: same target and content does not pop twice)
        bool operator==(const TooltipContent& o) const {
            return title == o.title && text == o.text && icon == o.icon;
        }
        bool operator!=(const TooltipContent& o) const { return !(*this == o); }
    };

    /**
     * @brief Window-level hover filter: reports the mouse position to the Tooltip (implementation in ZeTooltip.cpp)
     *
     * @details
     * Installed on the `Window` (not on the target widget) -- only at window level can the
     * "pointer left the target" event be received; a filter installed on a widget does not
     * receive it when the pointer leaves it.
     *
     * WARNING: The definition of this class must be in the header (cannot be only a forward
     * declaration): `Tooltip` holds it via a `std::unique_ptr<TooltipHoverFilter>`, and the
     * destructor of `unique_ptr` needs the complete type -- with only a forward declaration, the
     * consumer (a TU that only includes this header) fails to compile with
     * `can't delete an incomplete type`. Putting the implementation in the `.cpp` satisfies both.
     *
     * @note It does not hold a raw pointer to the Tooltip (the widget moves with AddWidget), only
     *       records the objectName and looks the entity up each time via `Window::FindWidgetDeep` --
     *       no dangling no matter how many times it moves.
     */
    class TooltipHoverFilter : public EventFilter {
    public:
        std::string name_;   /// < objectName of the target Tooltip

        /// @brief Observe only, do not intercept (always returns false)
        bool onMouse(Window& win, MouseEvent& e) override;
    };

    /**
     * @brief Tooltip bubble: can be popped manually, or bound to a widget to pop automatically on hover
     */
    class Tooltip : public TextWidget {
    public:
        /// @brief Arrow direction (the arrow always points from the bubble back to the target)
        enum class Arrow {
            None,   /// < No arrow (when positioning manually with `ShowAt` there is no "target")
            Up,     /// < Bubble below the target, arrow on the top edge of the bubble pointing up
            Down,   /// < Bubble flipped above the target, arrow on the bottom edge of the bubble pointing down
            Left,   /// < Bubble right of the target, arrow on the left edge of the bubble pointing left
            Right   /// < Bubble left of the target, arrow on the right edge of the bubble pointing right
        };

        Tooltip();

        // -------------------------------------------------------------------
        //  Show / hide (immediate)
        // -------------------------------------------------------------------

        /// @brief Shows the bubble at window coordinates (x, y) ((x, y) = top-left corner of the bubble)
        /// @param text Text (`\n` breaks lines)
        void ShowAt(float x, float y, std::string text);

        /// @brief Shows a rich-content bubble at window coordinates ((x, y) = top-left corner of the bubble)
        void ShowAt(float x, float y, TooltipContent content);

        /**
         * @brief Shows next to a widget (centered below by default; flips above if it does not fit below)
         * @param target Reference widget (must be in the tree, otherwise falls back to the top-left corner)
         * @param text   Text (`\n` breaks lines)
         * @note Horizontally it is clamped into the window (4px margin on all sides), never pops off-screen.
         */
        void ShowFor(Widget& target, std::string text);

        /// @brief Shows rich content (icon + title + body) next to a widget
        void ShowFor(Widget& target, TooltipContent content);

        /// @brief Retracts the bubble (idempotent no-op when already retracted)
        void Hide();

        /// @brief Whether it is currently shown
        bool isVisible() const { return visible_; }

        /// @brief Current bubble text (empty string when not shown; for rich content takes the body item)
        const std::string& currentText() const { return Text(); }

        /// @brief Current bubble content
        const TooltipContent& content() const { return content_; }

        // -------------------------------------------------------------------
        //  Delayed show (the single implementation of the hover rhythm)
        // -------------------------------------------------------------------

        /**
         * @brief Requests showing next to target "after delay milliseconds" (idempotent for same target and content)
         * @param target   Reference widget
         * @param content Tooltip content
         * @param delayMs Delay (`<= 0` = immediate; defaults to `showDelay()`)
         *
         * @details
         * This is the unified entry point for hover popups: both the automatic path of `Attach` and
         * the window path of `Widget::SetTooltip` go through it -- so "how long before popping, how to
         * fade in, how to flip when out of bounds" has only one implementation.
         *
         * Timing does not require the caller to start a timer: this widget declares it in
         * `wantsAnimationFrame()` and schedules a frame wake via `RequestFrameWake(remaining ms)`;
         * when due it is advanced by the render phase. Switching to another target in the meantime
         * automatically cancels the previous request (same semantics as "retract on leave").
         */
        void RequestShowFor(Widget& target, TooltipContent content, int delayMs = -1);

        /// @brief Requests showing at window coordinates "after delay milliseconds" (no arrow, no flip)
        void RequestShowAt(float x, float y, TooltipContent content, int delayMs = -1);

        /// @brief Cancels a pending delayed request and retracts the shown bubble
        void Dismiss();

        /// @brief Whether a delayed request is waiting (not yet popped)
        bool isPending() const { return pendingShow_; }

        // -------------------------------------------------------------------
        //  Auto hover (window-level filter)
        // -------------------------------------------------------------------

        /// @brief Binds a widget to a tooltip: shows automatically `showDelay` after mouse enter, hides on leave
        /// @param target Target widget (must be mounted)
        /// @param text   Tooltip text
        /// @note Attaching the same target again overwrites its content (does not stack).
        /// @note Requires the Tooltip to be mounted to the window -- otherwise the filter cannot be installed (silently ineffective).
        void Attach(Widget& target, std::string text);

        /// @brief Binds a widget to a rich-content tooltip
        void Attach(Widget& target, TooltipContent content);

        /// @brief Unbinds a target
        void Detach(Widget& target);

        /// @brief Unbinds all targets (the filter is uninstalled as well)
        void DetachAll();

        // -------------------------------------------------------------------
        //  Appearance
        // -------------------------------------------------------------------

        /// @brief Bubble shape (a "graphic container" drawn by the renderer, kinds see `TooltipShape`)
        /// @details
        /// The carrier / container body of the bubble is transparent background + no border -- the
        /// entire bubble you see (fill / stroke / arrow / vertical bar) is drawn by the renderer as
        /// primitives according to the Shape. Changing the shape does not change the content layout,
        /// follows the theme (`tooltip.shape`) or is overridden per instance.
        void SetShape(TooltipShape shape);
        /// @brief Current bubble shape
        TooltipShape shape() const { return shape_; }

        // -------------------------------------------------------------------
        //  Custom image bubble (TooltipShape::Custom)
        // -------------------------------------------------------------------
        //  "The container is fully transparent, the shape is decided by my image" -- this is the exit
        //  left for artwork: use it when you do not want any of the library-drawn shapes and do not
        //  want to be bound by rounded corners / shadow / arrow.
        //
        //  Behavior contract (important):
        //   - The container draws nothing: no fill, no border, no shadow, no arrow. The transparent
        //     pixels in the image are the shape outside the bubble, the per-pixel transparent carrier
        //     makes them truly transparent;
        //   - The image and the bubble rect are edge to edge (no rounded-corner inset) -- the image
        //     itself is the outline;
        //   - The text is centered on top of the image, use `SetPadding` to control its distance from
        //     the image edge;
        //   - Size: explicit `SetBubbleSize` > image natural size > content + padding. The image
        //     natural size is only available during the paint phase (decoding is in the render layer),
        //     so after the first paint it corrects the size once and re-lays out, at most one frame late.
        // -------------------------------------------------------------------

        /// @brief Uses an image as the bubble: equivalent to SetBackgroundImage(path) + SetShape(Custom)
        /// @param path Image path (PNG / JPG / BMP / TGA / DDS..., supports alpha channel)
        /// @note Passing an empty string clears the image (the shape is still Custom, so nothing is drawn -- please also SetShape).
        void SetBubbleImage(std::string path);

        /// @brief Fixed size of the custom bubble (pixels)
        /// @param w,h Width and height; `<= 0` = automatic (image natural size, then content + padding)
        /// @note Once set explicitly it no longer follows the image / content (the image is scaled by
        ///       `SetBackgroundDrawMode`, the default Stretch stretches -- to keep the aspect ratio use
        ///       `BgImageDrawMode::Fit` or `Fill`).
        void SetBubbleSize(float w, float h);

        /// @brief Position nudge for centered layout (pixels, positive X right / positive Y down; default 0,0)
        /// @details
        /// Used for the centered-text shapes `Circle` / `Star` / `Info` / `Cloud` / `Ribbon` / `Custom`
        /// -- the visible area of these shapes is often not the geometric center:
        ///  - Most common with custom images: the image has built-in top/bottom padding and a tail at
        ///    the bottom, so centering by the rect puts the text too low overall (looks like it is
        ///    floating). Set `dy` negative to move it up;
        ///  - Same for star / circle, use it to push the text into the inscribed area.
        /// @note Why not adjust with `SetPadding`: padding participates in the bubble size computation
        ///       (`content + padding`), which for a custom image would make the bubble larger than the
        ///       image and the image and bubble would no longer be edge to edge. This interface only
        ///       moves the text, not the geometry.
        void SetContentOffset(float dx, float dy);

        // -------------------------------------------------------------------
        //  Background image
        // -------------------------------------------------------------------
        //  This group is not declared here -- they are public interfaces of `BoxWidget`, inherited
        //  as-is by the bubble: `SetBackgroundImage` / `ClearBackgroundImage` / `backgroundImagePath` /
        //  `SetBackgroundDrawMode` / `SetBackgroundAlign` / `SetBackgroundTint`. The fields are also
        //  that group in `BoxStyle` (imagePath / image / drawMode / hAlign / vAlign / tint),
        //  `ApplyTheme` preserves them as-is (the theme has no image resources).
        //  Geometry follows the shape, see the table at the top of the file.

        /// @brief Bubble fill color
        void SetBackground(const Color4f& c);
        /// @brief Bubble border (line width <= 0 = no border)
        void SetBorder(float width, const Color4f& c);
        /// @brief Bubble text (body) color
        void SetTextColor(const Color4f& c);
        /// @brief Bubble text (body) font size (pixels)
        void SetFontSize(unsigned int pixelSize);
        /// @brief Title color (defaults to theme `tooltip.title`, brightened from the body color when not configured)
        void SetTitleColor(const Color4f& c);
        /// @brief Title font size (pixels); 0 = body font size +2
        void SetTitleFontSize(unsigned int pixelSize);
        /// @brief Icon side length (pixels, default 18; 0 = do not draw the icon)
        void SetIconSize(float size);
        /// @brief Corner radius (pixels)
        void SetRadius(float radius);
        /// @brief Padding on all four sides (pixels)
        void SetPadding(float all);
        /// @brief Gap to the reference widget (used by `ShowFor`; default 6)
        void SetOffset(float offset);
        /// @brief Minimum margin between the bubble and the window edge (default 4)
        void SetWindowMargin(float margin);

        /// @brief Hover delay (milliseconds, default 400; `<= 0` = immediate)
        void SetShowDelay(int ms);
        int  showDelay() const { return delayMs_; }
        /// @brief Fade-in duration (milliseconds, default 120; `<= 0` = no fade-in)
        void SetFadeDuration(int ms);
        int  fadeDuration() const { return fadeMs_; }

        /// @brief Whether to draw the arrow pointing at the target (default: drawn)
        void SetArrowEnabled(bool on);
        bool arrowEnabled() const { return arrow_; }
        /// @brief Arrow length / half base width (pixels, default 6)
        void SetArrowSize(float size);

        /// @brief Which side of the target the bubble shows on (default Auto = below, flips above if it does not fit)
        /// @details
        /// `Top / Bottom` keep "horizontally centered + horizontally clamped into the window";
        /// `Left / Right` change to vertically centered on the target + vertically clamped into the
        /// window, and the arrow (for shapes with an arrow) points sideways accordingly. Forced
        /// placements do not flip: if it does not fit it is clamped to the window edge -- "whichever
        /// side the user picks is the side".
        void SetPlacement(Placement p);
        /// @brief Current placement strategy
        Placement placement() const { return placement_; }

        // -------------------------------------------------------------------
        //  Measurement protocol (pure overlay does not occupy layout)
        // -------------------------------------------------------------------
        // @brief Returns the user's explicit SetSize if any, otherwise 0 (see the file header)
        float hintWidth() const override;
        /// @brief Same as hintWidth
        float hintHeight() const override;

        // -------------------------------------------------------------------
        //  Animation / three-half overlay
        // -------------------------------------------------------------------
        // @brief Waiting on the delay, or fade-in not finished -> needs another frame
        bool wantsAnimationFrame() const override;
        /// @brief Returns true while shown (or about to show) -- lets Window defer it to the "top-level overlay phase" paint
        bool paintsOnTop() const override;
        /// @brief Always returns false: the bubble does not participate in hit testing (mouse events pass through to the widget below)
        bool HitTest(float x, float y) const override;

    protected:
        /// @brief Default box style takes the tooltip section of the theme
        const BoxStyle& themeBox(const Theme& t) const override;
        /// @brief Default text style takes the tooltip section of the theme
        const TextStyle& themeText(const Theme& t) const override;

        /// @brief Theme injection: shape / glass opacity / arrow outline / card vertical bar color follow along
        /// @note If the user explicitly called `SetShape` -> the theme no longer overrides it (same rule as titleColor).
        void ApplyTheme(const Theme& t) override;

        /// @brief Takes over the base class paint template entirely: draws nothing when retracted (same as MenuModal)
        void Paint(FrameCollector& fc) const override;
        /// @brief Paints the bubble (shadow + arrow + border + fill + icon + title + multi-line body)
        void paintContent(FrameCollector& fc) const override;

        // -------------------------------------------------------------------
        //  Native carrier (desktop Popup capability platforms; always empty on fallback platforms)
        // -------------------------------------------------------------------

        /// @brief Whether the bubble is currently carried by a native carrier
        bool nativePopup() const;

        /// @brief Paints the bubble into the given collector
        /// @param localPopup true = relative to the carrier window origin (the carrier rect includes the arrow overhang,
        ///                    see showNativePopup); false = host window coordinates
        /// @param drawArrow  Whether to draw the arrow pointing at the target
        void paintPopupInto(FrameCollector& fc, bool localPopup, bool drawArrow) const;

        /// @brief Shows the native carrier (bubble moved into Popup + click-through; silently skipped on fallback platforms)
        void showNativePopup();

        /// @brief Retracts the native carrier
        void hideNativePopup();

    private:
        friend class TooltipHoverFilter;

        /// @brief Bubble geometry (shared by paint and hit -- eliminates "looks here, clicks there")
        struct BubbleGeom {
            float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;   /// < Bubble body (excluding the arrow)
            float arrowX = 0.0f;                            /// < Arrow tip X (the tip touches the target)
            float arrowY = 0.0f;                            /// < Arrow tip Y
            Arrow arrow = Arrow::None;                      /// < Arrow direction
            bool  valid = false;
        };

        /// @brief Computes the bubble geometry (pure query, does not write state)
        BubbleGeom computeBubble() const;

        /// @brief Natural content size (width / height, excluding padding and border)
        void measureContent(float& w, float& h) const;

        /// @brief Window origin of the native carrier (window rect = bubble + arrow overhang, see showNativePopup)
        /// @note paintPopupInto paints the bubble with this as the local origin; always (0,0) on the fallback path.
        mutable float popupOriginX_ = 0.0f;
        mutable float popupOriginY_ = 0.0f;

        /// @brief Body / title line height (asks the font if there is a window, otherwise estimates from the font size)
        float lineHeightFor(const FontDesc& font) const;

        /// @brief Title font (body font size +2, bold)
        FontDesc titleFont() const;

        /// @brief Whether the current shape has an arrow / tail "pointing at the target" (Balloon long tail / Rounded small arrow /
        ///       Info round tail; Pill / Square / Card / Glass / Circle / Star none)
        /// @note Both positioning and painting query this -- shapes without an arrow leave only `offset_`
        ///       between them and the target, no longer reserving the arrow length.
        bool shapeHasArrow() const;

        /// @brief Whether the "container" of the current shape is non-rectangular (Circle / Star / Info) --
        ///       text in these shapes is laid out horizontally + vertically centered, no longer left to right.
        bool centerContent() const;

        /// @brief Arrow length of the current shape (the Balloon tail is one step longer than the small arrow)
        /// @note Positioning (gap to the target) and painting (triangle height) must use the same value,
        ///       otherwise the tail pokes into the target widget or floats in the air.
        float arrowLength() const;

        /// @brief Bubble corner radius (derived from the shape; Square=0, Pill/Circle/Info=half height, others take the theme)
        float bubbleRadius(float themeRadius) const;

        /// @brief Derives the bubble size from the natural content size (shape-dependent: circle / star / info icon are square)
        void sizeBubble(float contentW, float contentH);

        /// @brief Positions next to a widget: computes the bubble top-left corner + arrow tip / orientation
        void anchorForTarget(const Widget& target, float bw, float bh,
                             float& ax, float& ay, Arrow& arrow) const;

        /// @brief Commits the show (widget-relative version): the single "show immediately" exit
        void applyShowFor(Widget& target, TooltipContent content);

        /// @brief Advances during the render phase: pops when the delay expires, advances the fade-in one step, continues frames as needed
        /// @note `const`: called by `Paint` / `wantsAnimationFrame`; the shown state is marked mutable
        void stepTimers() const;

        /// @brief Binding record for one target
        struct AttachEntry {
            Widget*         target = nullptr;
            TooltipContent  content;
        };

        /// @brief Called back by the filter when the mouse position changes (only arrives when Attach has bindings)
        void NotifyHover(float px, float py);
        /// @brief Installs / uninstalls the window-level filter as needed
        void syncHoverFilter();
        /// @brief Requests a repaint (silently skipped when there is no window)
        void requestRepaint();
        /// @brief Current time (steady_clock milliseconds; same time source as Toast / ProgressBar)
        static long long nowMs();

        // ---- Content ----
        TooltipContent content_;

        // ---- Shown state (mutable: delay expiry / fade-in advance happen in the render phase) ----
        mutable bool      visible_ = false;
        mutable float     anchorX_ = 0.0f;   /// < Top-left corner of the bubble body (window coordinates)
        mutable float     anchorY_ = 0.0f;
        mutable float     bw_ = 0.0f;        /// < Bubble body width (measured once when committed, shared by paint and positioning)
        mutable float     bh_ = 0.0f;        /// < Bubble body height
        mutable Arrow     arrowDir_ = Arrow::None;
        mutable float     arrowTipX_ = 0.0f; /// < Arrow tip (the end touching the target)
        mutable float     arrowTipY_ = 0.0f;
        mutable float     fade_ = 1.0f;      /// < Current opacity (0~1)
        mutable long long shownAtMs_ = 0;    /// < Start time of this show (for fade-in)
        mutable Widget*   shownTarget_ = nullptr; /// < Widget the current bubble is attached to (idempotency criterion)

        // ---- Delayed request (mutable: expiry is decided in the render phase) ----
        mutable bool           pendingShow_ = false;
        mutable Widget*        pendingTarget_ = nullptr;
        mutable TooltipContent pendingContent_;
        mutable float          pendingX_ = 0.0f;
        mutable float          pendingY_ = 0.0f;
        mutable bool           pendingHasAnchor_ = false;
        mutable long long      pendingSinceMs_ = 0;
        mutable int            pendingDelayMs_ = 0;

        // ---- Layout parameters ----
        float   offset_ = 6.0f;       /// < Gap to the reference widget (used by ShowFor)
        float   winMargin_ = 4.0f;    /// < Minimum margin between the bubble and the window edge
        float   iconSize_ = 18.0f;    /// < Icon side length (0 = do not draw)
        float   iconGap_ = 8.0f;      /// < Gap between icon and text
        int     delayMs_ = 400;       /// < Hover delay
        int     fadeMs_ = 120;        /// < Fade-in duration
        bool    arrow_ = true;        /// < Whether to draw the arrow
        float   arrowSize_ = 6.0f;    /// < Arrow length / half base width
        unsigned titlePixelSize_ = 0; /// < Title font size override (0 = body +2)
        Placement placement_ = Placement::Auto;  /// < Which side of the target to show on

        // ---- Bubble shape (theme tooltip.shape can give the default; SetShape is a per-instance override) ----
        TooltipShape shape_ = TooltipShape::Rounded;
        bool    shapeSet_ = false;    /// < The user explicitly called SetShape -> the theme no longer overrides
        float   glassAlpha_ = 0.78f;  /// < Fill opacity of the Glass shape (given by the theme)
        Color4f arrowBorder_{ 0.0f, 0.0f, 0.0f, 0.0f };  /// < Arrow outline color (given by the theme)
        Color4f accent_{ 0.0f, 0.0f, 0.0f, 0.0f };       /// < Card left vertical bar color (given by the theme)

        // ---- Custom image bubble (Custom) ----
        float customW_ = 0.0f;   /// < Width set explicitly by SetBubbleSize (0 = automatic)
        float customH_ = 0.0f;   /// < Height (0 = automatic)
        /// Position nudge for centered content (see SetContentOffset)
        float   contentDX_ = 0.0f;
        float   contentDY_ = 0.0f;
        /// Image natural size (only known during the paint phase: decoding is in the render layer). 0 = not known yet.
        mutable float customImgW_ = 0.0f;
        mutable float customImgH_ = 0.0f;
        /// During paint, if "the size computed from the image differs from the current bubble" -> set the flag,
        /// and the next `stepTimers()` (in the host frame, not in the carrier paint) re-lays out once.
        mutable bool  customResize_ = false;

        /// @brief Title / icon color override (transparent = derived from the theme / body color)
        Color4f titleColor_{ 0.0f, 0.0f, 0.0f, 0.0f };
        bool    titleColorSet_ = false;

        // ---- Hover bindings ----
        std::vector<AttachEntry> attach_;
        Widget* hoverTarget_ = nullptr;   /// < Target the pointer is currently on (nullptr if none)

        /// @brief Window-level filter (created lazily; empty = not installed)
        std::unique_ptr<TooltipHoverFilter> hoverFilter_;
        FilterId installedId_ = 0;        /// < Handle installed on the window (0 = not installed)

        // * Uses shared_ptr rather than unique_ptr: the unique_ptr destructor needs the complete type, which forces
        //   an explicit move/destructor (strong symbol) that collides with the old implicit COMDAT in Zekit.cpp.obj;
        //   the shared_ptr destructor is type-erased, so Tooltip keeps implicit move/destructor (inline).
        std::shared_ptr<OverlayWindow> overlay_; /// < Native carrier (non-empty only on desktop Popup capability platforms)
    };

} // namespace Kit
