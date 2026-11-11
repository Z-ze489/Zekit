/**
 * @file ZePanel.h
 * @brief Kit::Panel -- the recommended base class for composite widgets (panels / containers)
 *
 * @details
 * When writing a composite widget that holds a set of child widgets and lets the library lay them out, which base class should you pick?
 * This class exists to make that question disappear: when in doubt, pick Kit::Panel.
 *
 * @par Why a dedicated convenience base class is needed
 * DoLayout() is a capability of Kit::LayoutBase; Kit::Widget has no concept of "layout" at all.
 * So code written on the intuition of common GUI libraries -- "class MyPanel : public Kit::Widget" plus AddWidget calls -- compiles fine but yields an all-white window:
 * the children are indeed added, but no step ever writes their geometry (a new widget is 0x0),
 * and an empty Paint() is a legal and required implementation -- the compiler says nothing.
 *
 * Panel solves three things at once:
 *   1. It is a LayoutBase branch (vertical stacking by default, switchable to horizontal at construction),
 *      so children are always laid out;
 *   2. As a top-level widget it automatically fills the client area (the implicit fill of the LayoutBase branch,
 *      no SetFillWindow() needed);
 *   3. It also provides a set of value-interface skeleton appearance (background color / corner radius / border) -- to give a panel
 *      a background color and rounded corners, you do not need to override Paint (that requires drawing primitives, and drawing
 *      primitives are currently not part of the delivery surface, see "19 Custom Widgets").
 *
 * @par Usage
 * @code
 * class MyPanel : public Kit::Panel {
 * public:
 *     MyPanel() {
 *         SetPadding(12.0f, 12.0f);                       // padding on all four sides
 *         SetGap(8.0f, 8.0f);                             // gap between children
 *         SetBackground(Kit::Color4f::FromHex(0xFFFFFF)); // background color
 *         SetRadius(10.0f);                               // corner radius
 *
 *         Kit::Label title("Title");
 *         AddChild(title);            // or AddWidget(title) -- the two are equivalent
 *         Kit::Button ok("OK");
 *         AddChild(ok);
 *     }
 * };
 *
 * MyPanel page;
 * win.AddWidget(page);                // top-level widget, automatically fills the client area
 * @endcode
 *
 * @par How it differs from deriving from VBoxLayout directly
 * The arrangement behavior is identical (Panel is VBoxLayout plus a skeleton background). The differences are only:
 *   - Clearer intent -- Panel reads as a "container" at a glance and is not mistaken for a leaf widget;
 *   - One less layer -- no need to override Paint for background / corner radius / border;
 *   - It is the recommended default answer -- when unsure which base class to pick, pick this one.
 *
 * @warning The background fills only this panel's own rectangle and does not participate in layout space: it does not push children aside;
 *          child indentation is determined by SetPadding / SetGap / SetMargin. For card-style
 *          containers with shadows, background images, or polymorphic colors, use the ready-made widgets of the BoxWidget branch.
 *
 * @see ZeLayoutBase.h   source of AddChild / SetPadding / SetGap / SetDirection
 * @see ZeVBoxLayout.h   the arrangement algorithm itself
 * @see 19 Custom Widgets.md  decision table for choosing a base class and per-pixel measurements
 */

#pragma once

#include "ZeVBoxLayout.h"   // All of Panel's arrangement capability comes from this branch (-> LayoutBase -> Widget)
#include "color4f.h"        // Color4f: the skeleton appearance accepts only this value type

namespace Kit {

    /**
     * @brief Recommended base class for composite widgets (panels): a visible container that stacks vertically by default, with an optional skeleton background
     *
     * @details
     * Inherits from Kit::VBoxLayout, so:
     *   - Children are laid out by the layout (just add them with AddChild / AddWidget);
     *   - SetDirection(FlexDirection::Row) switches to horizontal at any time;
     *   - Top-level auto-fill and clipChildrenRect clipping of children -- consistent with other layout containers.
     *
     * What this class adds is the skeleton appearance (background color / corner radius / border), expressed through value interfaces,
     * with drawing done inside the library -- users never need to know FrameCollector exists.
     */
    class Panel : public VBoxLayout {
    public:
        /// @brief Default vertical panel (equivalent to the VBoxLayout arrangement direction)
        Panel() = default;

        /**
         * @brief Panel with the specified main-axis direction
         * @param direction FlexDirection::Column (vertical, same as default) or Row (horizontal)
         * @note To change it later, use the inherited SetDirection(); no need to rebuild the panel.
         */
        explicit Panel(FlexDirection direction) { SetDirection(direction); }

        // -------------------------------------------------------------------
        //  Skeleton appearance (value interface -- exposes no drawing / primitive types)
        // -------------------------------------------------------------------
        //  These setters all call markStyled(): the semantics are "the user manually set a style".
        //  Currently Panel does not receive theme injection (ApplyTheme is an empty implementation), so this step has no
        //  visible effect; but it is the correct marking -- if Panel later gets theme defaults,
        //  the pattern of "configure the style first, then hand it over" will automatically not be wiped, with no need to revisit.
        // -------------------------------------------------------------------

        /// @brief Sets the panel background color (default fully transparent = no background painted)
        /// @param c Fill color; when alpha is 0 no primitive is produced
        void SetBackground(const Color4f& c);

        /// @brief Current background color
        const Color4f& backgroundColor() const { return background_; }

        /**
         * @brief Sets the corner radius of the four corners (pixels)
         * @param radius Radius; pass 0 or a negative value for square corners
         * @note Automatically clamped to no more than half the short side (consistent with the library-wide corner rule; no need to compute it yourself).
         * @note The corner radius applies to both the background and the border.
         */
        void SetRadius(float radius);

        /// @brief Current corner radius (0 = square corners)
        float radius() const { return radius_; }

        /**
         * @brief Sets the border in one call: line width + color
         * @param width Line width (pixels); <= 0 means no border
         * @param color Border color; also not painted when alpha is 0
         * @note The border is painted on the inside of the panel rectangle (the background shrinks accordingly), so it never exceeds its own
         *       geometry and never affects child positions -- consistent with the border semantics of BoxStyle.
         */
        void SetBorder(float width, const Color4f& color);

        /// @brief Sets only the border line width (<= 0 = remove the border)
        void SetBorderWidth(float width);

        /// @brief Sets only the border color
        void SetBorderColor(const Color4f& color);

        /// @brief Current border line width (0 = no border)
        float borderWidth() const { return borderWidth_; }

        /// @brief Current border color
        const Color4f& borderColor() const { return borderColor_; }

        // -------------------------------------------------------------------
        //  Layout: dispatched by the current main-axis direction (the only substantive difference between Panel and VBoxLayout)
        // -------------------------------------------------------------------
        /**
         * @brief Dispatches to linear stacking by direction(): Column -> vertical, Row -> horizontal
         *
         * @details
         * Why override: VBoxLayout::DoLayout() hardcodes layoutLinear(true)
         * -- correct for VBoxLayout (it is a vertical container), but Panel advertises
         * "can be switched to horizontal"; copying that would make it fake: the direction field changes but the layout does not.
         *
         * This also explains an easily misunderstood point: LayoutBase::SetDirection on
         * VBoxLayout / HBoxLayout is only self-description ("I am a vertical/horizontal container")
         * and does not drive layout -- the direction of those two classes is a fact fixed at construction. Panel is
         * the first layout class to use direction as a switch, so it must dispatch itself.
         *
         * @note The implementation calls layoutLinear(vertical), which carries RestorePref +
         *       measureWidthDependentHeights and follows the same algorithm as VBoxLayout / HBoxLayout -- so behavior is identical; there is no "Panel lays out differently from the others".
         */
        void DoLayout() override;

        // -------------------------------------------------------------------
        //  Paint: draws only "what it looks like itself"; children are painted recursively by Window (consistent with all layout containers)
        // -------------------------------------------------------------------
        /**
         * @brief Describes the panel skeleton (border + background) into this frame
         * @param fc In-frame collector
         *
         * @details
         * The drawing follows the library-wide two rounded rectangles of "border underneath + background inset" (the same
         * strategy as BoxWidget::paintBox), so the line width is a true inner line.
         *
         * @note The container's own description happens before its children (container below, children above),
         *       so the background is always beneath the children; no extra layering is needed.
         */
        void Paint(FrameCollector& fc) const override;

    private:
        Color4f background_{ 0.0f, 0.0f, 0.0f, 0.0f };   /// < background color (default transparent = not painted)
        Color4f borderColor_{ 0.0f, 0.0f, 0.0f, 0.0f };  /// < border color (default transparent = not painted)
        float   radius_ = 0.0f;                           /// < corner radius (0 = square corners)
        float   borderWidth_ = 0.0f;                      /// < border line width (0 = no border)
    };

} // namespace Kit
