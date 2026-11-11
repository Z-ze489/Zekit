/**
 * @file ZeBoxWidget.h
 * @brief Box base class -- the shared layer for every "visible rectangle" widget in the library
 *
 * @details
 * This layer handles only two things, and both depend solely on "widget rect + BoxStyle + current state":
 *
 *  1. Style and interface: holds BoxStyle and provides all box-related setters
 *     (color / border / radius / padding / shadow / outline / opacity / cursor).
 *     Putting this wall of setters here lets Button / Label / Input / Card / Panel
 *     each inherit one copy -- instead of copying it for every new widget (the rule at the top of
 *     ZeBoxStyle.h that says "define everything once in the library" refers to exactly this).
 *
 *  2. Box paint pipeline: Paint is a template method --
 *     shadow -> outline -> border+background -> content (virtual hook paintContent).
 *     Derived classes normally only override paintContent and need not rewrite these three layers.
 *     WARNING: Paint is deliberately not final: widgets like Input that lay out their own content area
 *     can override it entirely when needed.
 *
 * Interaction state layering: disabled (disabled_ / SetEnabled) is in the base Widget (a general state,
 * on par with SetVisible); hover / press + currentState() live in this layer -- they serve exactly
 * the box state color lookup, which any widget that changes color on hover / disable needs,
 * and BoxWidget itself implements OnMouseEnter / OnMouseLeave to maintain hover and "cancel press on leave".
 * Press / release are not implemented in the base class -- those are interaction semantics (left button? consume or not? what counts as a click?),
 * and belong to each widget (see Button). Fields are protected; derived classes maintain them directly.
 *
 * Theme injection: ApplyTheme is implemented here (copies BoxStyle wholesale),
 * but which theme section supplies the default style is answered by the derived class -- see the themeBox() virtual function.
 *
 * @note Value semantics: this class declares no destructor / copy / move -- members are POD or std::string,
 *       so the implicit move constructor moves them member by member (the same mechanism as
 *       "callbacks live in the Button layer so no hand-written move is needed").
 *       Once a destructor or copy is declared, the implicit move disappears,
 *       and `make_unique<T>(std::move(x))` in `AddWidget` fails to compile.
 *
 * @see ZeBoxStyle.h box style fields
 * @see ZeTextWidget.h box with text (the next layer of this class)
 */

#pragma once
#include "ZeWidget.h"
#include "ZeBoxStyle.h"
#include "ZeTheme.h"

namespace Kit {

    /**
     * @brief Box base class: common ancestor of visible rectangle widgets
     *
     * @details
     * Inheritance chain: `Widget -> BoxWidget -> (TextWidget) -> concrete widget`.
     * - Widgets that need only a box, no text (Panel / Card / Splitter) inherit this class directly;
     * - Widgets that need text (Button / Label) inherit TextWidget; widgets that edit text (Input) also inherit TextWidget,
     *   but lay out their content area themselves (override paintContent or Paint).
     */
    class BoxWidget : public Widget {
    public:
        BoxWidget() = default;

        // -------------------------------------------------------------------
        //  Interaction state
        //  (SetEnabled / isEnabled / disabled_ are in the base Widget -- they are general states
        //   on par with SetVisible; SetEnabled is overridden here to also clear hover/press)
        // -------------------------------------------------------------------

        /// @brief Overrides the base class: also clears hover/press state when disabled (avoids stale fake highlight)
        void SetEnabled(bool enabled) override;

        /// @brief Whether currently hovered (maintained by OnMouseEnter / OnMouseLeave)
        bool isHovered() const { return hovered_; }
        /// @brief Whether currently pressed (maintained by derived widgets, see class comment)
        bool isPressed() const { return pressed_; }

        // -------------------------------------------------------------------
        //  Style access (grab the underlying BoxStyle and modify directly)
        // -------------------------------------------------------------------

        BoxStyle& box() { return box_; }
        const BoxStyle& box() const { return box_; }

        // -------------------------------------------------------------------
        //  Colors (four states x two slots: background / border)
        // -------------------------------------------------------------------
        //  Only writes the specified state. To modify the less common Focused / Loading states directly,
        //  use `box().colors.get(WidgetState::Focused)`.
        //  Text color and decoration line color are in TextWidget (which is the holder of TextStyle).
        // -------------------------------------------------------------------

        void SetBodyColor(const Color4f& c);            /// < background color (normal)
        void SetHoverBodyColor(const Color4f& c);       /// < background color (hover)
        void SetPressedBodyColor(const Color4f& c);     /// < background color (pressed)
        void SetDisabledBodyColor(const Color4f& c);    /// < background color (disabled)

        void SetBorderColor(const Color4f& c);          /// < border color (normal)
        void SetHoverBorderColor(const Color4f& c);     /// < border color (hover)
        void SetPressedBorderColor(const Color4f& c);   /// < border color (pressed)
        void SetDisabledBorderColor(const Color4f& c);  /// < border color (disabled)

        // -------------------------------------------------------------------
        //  Background image
        // -------------------------------------------------------------------
        //  The widget stores only the path; decoding / texture upload / caching all live in the render layer (ZeImageStore).
        //  Same pattern as Button icons -- so SetBackgroundImage can be called before AddWidget
        //  (when there is no window yet, let alone a graphics context).
        //
        //  Paint position: the box area inside the border, drawn on top of the background color (background color underneath, image on top),
        //  so an opaque background color does not hide the image. Image scaling / tiling / alignment is handled by
        //  FrameCollector (Stretch / Fit / Fill / Tiled / Origin).
        // -------------------------------------------------------------------

        /// @brief Sets the background image (empty string = clear)
        /// @param path image file path (PNG / JPG / BMP / TGA / DDS..., supported by SOIL2)
        /// @note Only records the path, does not load -- actual loading happens at paint time in the render layer.
        /// @note If the path does not exist the widget still paints (image silently skipped); the error is reported once on first load.
        /// @see SetBackgroundDrawMode, SetBackgroundAlign, SetBackgroundTint
        void SetBackgroundImage(std::string path);

        /// @brief Clears the background image
        void ClearBackgroundImage();

        /// @brief Current background image path (empty = no background image)
        const std::string& backgroundImagePath() const { return box_.imagePath; }

        /// @brief Background image draw mode (Stretch / Fit / Fill / Tiled / Origin, default Stretch)
        void SetBackgroundDrawMode(BgImageDrawMode mode);

        /// @brief Background image alignment (only effective in Origin mode)
        void SetBackgroundAlign(BgImageHAlign hAlign, BgImageVAlign vAlign);

        /// @brief Background image tint (multiplied component-wise with the texture; white = output as-is, can darken / colorize overall)
        void SetBackgroundTint(const Color4f& c);

        // -------------------------------------------------------------------
        //  Border / radius / padding
        // -------------------------------------------------------------------

        /// @brief Sets border width (same on all four sides)
        void SetBorderWidth(float w);
        /// @brief Sets border width (top / right / bottom / left, same order as CSS border-width)
        void SetBorderWidth(float top, float right, float bottom, float left);
        /// @brief Sets border line style (solid / dashed / dotted / none)
        void SetBorderStyle(BorderStyle style);

        /// @brief Sets radius mode (preset level)
        void SetRadiusMode(RadiusMode mode);
        /// @brief Gives pixel radius directly (internally switches to RadiusMode::Custom)
        void SetRadius(float radius);
        /// @brief Custom corner radius (equivalent to SetRadius)
        void SetCustomRadius(float radius);

        /// @brief Sets padding on four sides (top / right / bottom / left, same order as CSS padding)
        /// @note Same order as the four-argument SetBorderWidth; do not confuse with the field declaration order of the Padding struct.
        void SetPadding(float top, float right, float bottom, float left);
        /// @brief Sets padding (one value for horizontal / vertical each)
        void SetPadding(float horizontal, float vertical);
        /// @brief Sets the same padding on all four sides
        void SetPadding(float all);

        // -------------------------------------------------------------------
        //  Visual effects (shadow / outline / opacity / cursor)
        // -------------------------------------------------------------------
        //  Shadow and outline are both off by default (enabled = false in BoxStyle),
        //  so these interfaces do not affect any existing appearance; they take effect only when explicitly enabled.
        // -------------------------------------------------------------------

        /// @brief Sets all shadow parameters at once
        void SetShadow(bool enabled, const Color4f& color,
            float offsetX, float offsetY, float blur, float spread);
        void SetShadowEnabled(bool enabled);
        void SetShadowColor(const Color4f& c);
        void SetShadowOffset(float offsetX, float offsetY);
        void SetShadowBlur(float blur);
        void SetShadowSpread(float spread);

        /// @brief Sets all outline parameters at once
        void SetOutline(bool enabled, float width, const Color4f& color, float offset);
        void SetOutlineEnabled(bool enabled);
        void SetOutlineWidth(float width);
        void SetOutlineColor(const Color4f& c);
        void SetOutlineOffset(float offset);
        void SetOutlineStyle(BorderStyle style);

        /// @brief Sets overall opacity (0 = fully transparent, 1 = opaque)
        void SetOpacity(float opacity);

        /// @brief Sets the cursor shape on mouse hover -- moved up to `Widget::SetCursor`
        /// @note Not redeclared here: the cursor is widget-level semantics (text widgets need an I-beam,
        ///       splitters need left-right arrows), and the `Panel` / `LayoutBase` branch also needs it,
        ///       so `Widget` is the single source of truth. Old code `box.SetCursor(c)` still works
        ///       (inherited); behavior changes from "silently no-op" to "actually changes the cursor".

        // -------------------------------------------------------------------
        //  Theme injection (called by Window; users may also call it to override)
        // -------------------------------------------------------------------

        /// @brief Re-injects box style from Theme (overwrites fields the user changed manually)
        /// @note Overrides Widget::ApplyTheme. Injection = "start over".
        ///       Defaults to the theme's button section -- see themeBox(); Label / Card override it to use another section.
        /// @note Exception: the background image group (imagePath / image / drawMode / hAlign / vAlign /
        ///       tint) is not in the theme (the theme has no image resources) and is preserved as-is on theme change --
        ///       otherwise pressing D to toggle light/dark (which re-injects the theme) would wipe all background images.
        void ApplyTheme(const Theme& t) override;

        // -------------------------------------------------------------------
        //  Paint
        // -------------------------------------------------------------------

        /// @brief Template method: shadow -> outline -> border+background -> content (clipped to padding box) -> paintContent
        /// @note Not final: widgets needing a different paint order can override it directly.
        /// @note Content clipping is a library-wide step, done in this function (see clipsContent).
        void Paint(FrameCollector& fc) const override;

        // -------------------------------------------------------------------
        //  Interaction events (pure state bookkeeping only; interaction semantics belong to derived widgets)
        // -------------------------------------------------------------------

        /// @brief Mouse enter: sets hover state (not set when disabled)
        void OnMouseEnter() override;
        /// @brief Mouse leave: clears hover state and cancels press state (leaving cancels; returning does not restore)
        void OnMouseLeave() override;

    protected:
        // -------------------------------------------------------------------
        //  Questions the derived class must answer
        // -------------------------------------------------------------------

        /// @brief Content paint hook (empty by default -- pure box widgets Panel / Card need no code)
        /// @note What is drawn in this function is uniformly clipped to the padding box by Paint, so it never draws outside the widget --
        ///       therefore widgets do not need to (and should not) PushClip themselves to enforce "no overflow".
        ///       When a smaller clip region is genuinely needed (e.g. Label overflow handling clipped to the content area), push another layer:
        ///       PushClip intersects with the current region, so it only tightens and never breaks the outer clip.
        ///       When drawing outside the box is genuinely needed (overlay / focus ring):
        ///       either put it in paintOutline (before clipping), or ResetClip before push to escape.
        virtual void paintContent(FrameCollector& fc) const;

        /// @brief Whether content is clipped to the "padding box" (widget rect minus border)
        ///
        /// @details
        /// Default true -- the library-wide "content must not draw outside the widget" relies on this, regardless of widget kind:
        /// Button's long text, Label's multiple lines, oversized icons in Button all go the same way.
        /// Currently no widget overrides it (Label's Visible mode does not either --
        /// see the TextOverflow comment in ZeTextWidget.h: overflow visible != outside the widget border).
        /// Only widgets that genuinely need to draw outside the box (possible future overlay / focus ring) should consider overriding it to false;
        /// but those should rather use paintOutline (before clipping) or ResetClip(), not turn this off.
        virtual bool clipsContent() const;

        /// @brief The interaction state to take color from (default priority: disabled > pressed > focus > hover > normal)
        /// @note Derived classes that introduce new states (e.g. checked) override it.
        virtual WidgetState currentState() const;

        /// @brief Which theme section this widget's default style comes from (box)
        /// @note Default `t.button.box`; Label overrides to `t.label.box`, Card overrides to `t.card.box`.
        virtual const BoxStyle& themeBox(const Theme& t) const;

        // -------------------------------------------------------------------
        //  Paint helpers (derived classes may reuse directly)
        // -------------------------------------------------------------------

        /// @brief Gets the color for a state and folds in opacity (shared by paint stages)
        ColorSet resolvedColors(WidgetState s) const;

        /// @brief Converts the radiusMode enum to actual pixel radius (needs theme tokens, hence not member state)
        static float resolveRadius(const BoxStyle& box, const Theme& theme);

        /// @brief Shadow (bottom-most, painted first; rounded rect expanded by offset/spread)
        void paintShadow(FrameCollector& fc) const;

        /// @brief Outer outline (painted before the box, so the box covers its inner half, reading as an "outer stroke")
        void paintOutline(FrameCollector& fc) const;

        /// @brief Border + background (border-color rounded rect underneath, background color inset by four-side widths, then background image on top)
        void paintBox(FrameCollector& fc) const;

        // -------------------------------------------------------------------
        //  State (disabled_ is in the base Widget; derived classes maintain hover / press)
        // -------------------------------------------------------------------

        bool hovered_ = false;    /// < hover state
        bool pressed_ = false;    /// < press state

        BoxStyle box_;            /// < box style (composes this widget's visuals)
    };

} // namespace Kit
