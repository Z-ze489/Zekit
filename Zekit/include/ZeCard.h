/**
 * @file ZeCard.h
 * @brief Card: a floating content container (optional title + content area + shadow)
 *
 * @details
 * `Card : Panel` -- arrangement, padding, and child clipping all come from `Panel` (that is, `LayoutBase`).
 * This class only answers "how is a card different from a plain panel":
 *
 *  1. Title row: an optional top title (`SetTitle` / `titleLabel()`); the title area height is determined by the theme
 *     `card.titleHeight`; without a title the content goes straight to the padding;
 *  2. Shadow: a card "floats" one level above a panel -- the theme `card.shadow*` gives a soft shadow;
 *  3. Component-style add interface: `SetContent` / `AddContent` add children into the content area,
 *     not into the card itself -- so "the title is never squeezed out by children" is structurally guaranteed;
 *  4. Background image: the `SetBackgroundImage` group (path / draw mode / align / tint),
 *     fields are the same source as `BoxWidget`, but inset by the rounded-corner shoulder width -- a rounded card with an image does not show square corners.
 *
 * @par Why derive from `Panel` instead of `BoxWidget`
 * A card must hold children. The `BoxWidget` branch (`Widget` -> `BoxWidget`) can hold them but does not lay them out;
 * the real child-layout capability is in the `LayoutBase` branch. `Panel` is the "when in doubt pick it" answer in that branch
 * (see "19 Custom Widgets" section 7), and it also has value interfaces for background / rounded corners / border.
 * Wanting both the six-state colors of `BoxWidget` and the layout of `LayoutBase` is not possible (two different
 * inheritance branches); this is exactly the trade-off of "containers and layout are the same tree" in this library. So the card's appearance goes through
 * `CardStyle` (`BoxStyle` + title + shadow), painted by itself in `Paint`.
 *
 * @par Usage
 * @code
 * Kit::Card card("User Info");
 * card.SetRadius(12.0f);
 * Kit::Label& name = card.AddContent(Kit::Label("Zhang San"));
 * (void)name;
 * card.AddContent(Kit::Label("zhangsan@example.com"));
 * win.AddWidget(card);                        // a top-level card automatically fills the client area
 * @endcode
 *
 * @note A card is a container, so it has `DoLayout` (inherited from `Panel`). Child coordinates are still
 *       client-area absolute coordinates -- that is the library-wide convention (see "19" section 6.1), and cards are no exception.
 *
 * @see ZePanel.h     base class (arrangement / padding / child clipping)
 * @see ZeTheme.h     `CardStyle` (six-state box + title + shadow)
 * @see ZeCollapse.h  the "collapsible card" of the same family
 */

#pragma once
#include "ZePanel.h"
#include "ZeSelfAction.h"
#include "ZeBoxStyle.h"
#include "ZeTextStyle.h"

#include <string>

namespace Kit {

    class Label;   /// < The title uses `Kit::Label` (a forward declaration suffices; the full type is only needed in the implementation)

    /**
     * @brief Card: a floating container with title + content area
     *
     * @details
     * Internal structure (three regions of one vertical `Panel`):
     * @verbatim
     *   +--------------------------+
     *   |  Title (optional, titleHeight) |  <- made room for by the card's own top padding
     *   +--------------------------+
     *   |  Content area (content_)       |  <- everything the user AddContent's goes here
     *   +--------------------------+
     * @endverbatim
     * The implementation does not compose it from two child containers -- that would make "the card's own padding" and
     * "the child container's padding" fight each other. The approach is to write the title area height as the card's top padding
     * (the same trick as `TabView` writing the tab bar height as top padding); the title is painted directly in the card's
     * `Paint`, and content children are arranged by `Panel` within the padding.
     */
    class Card : public Panel {
    public:
        /// @brief Constructs a card
        /// @param title Title text; empty string = no title (does not occupy title area height)
        explicit Card(std::string title = std::string());

        // -------------------------------------------------------------------
        //  Title
        // -------------------------------------------------------------------

        /// @brief Sets / clears the title (empty string = no title)
        /// @note Synchronously adjusts the top padding and layout (the title area footprint changed), and requests relayout.
        void SetTitle(std::string title);

        /// @brief Current title
        const std::string& Title() const { return title_; }

        /// @brief Whether there is a title (without a title the content goes to the padding)
        bool hasTitle() const { return !title_.empty(); }

        /// @brief Title area height (pixels; 0 = no footprint)
        float titleHeight() const { return titleHeight_; }

        /// @brief Overrides the title area height (pixels)
        /// @param h Height; <= 0 means "no footprint" (equivalent to temporarily hiding the title area)
        /// @note The theme `card.titleHeight` is the default; once overridden here it is no longer washed away by `ApplyTheme`
        ///       (same convention as `TabView::SetTabBarHeight`).
        void SetTitleHeight(float h);

        /// @brief Text style of the title (can change text color / font size / font weight)
        TextStyle& titleStyle() { return titleStyle_; }
        const TextStyle& titleStyle() const { return titleStyle_; }

        // -------------------------------------------------------------------
        //  Content area (component-style add interface)
        // -------------------------------------------------------------------

        /**
         * @brief Adds a child to the content area (takes ownership by value)
         * @param child Widget value
         * @return Reference to the copy in the tree (stable address)
         * @note Equivalent to `AddChild` -- a card's children all go into the content area anyway.
         *       The name is provided so that "what is added is content" is readable at the call site.
         */
        template<class T>
        T& AddContent(T&& child) { return this->AddChild(std::forward<T>(child)); }

        /// @brief Usable rectangle of the content area (client-area absolute coordinates) -- for external custom layout
        /// @note Already the result of "card rectangle minus border padding, then minus title area".
        void contentArea(float& x, float& y, float& w, float& h) const;

        // -------------------------------------------------------------------
        //  Appearance (value interfaces -- does not expose paint types)
        // -------------------------------------------------------------------

        /// @brief Whether the card paints a shadow (default follows the theme)
        void SetShadowEnabled(bool on);
        bool shadowEnabled() const { return shadowEnabled_; }

        // -------------------------------------------------------------------
        //  Background image (value interfaces -- only records the path, does not load)
        // -------------------------------------------------------------------
        //  The fields are exactly the same source as the `BoxWidget` group (`BoxStyle`'s imagePath / image /
        //  drawMode / hAlign / vAlign / tint); the card just goes through the `LayoutBase` branch and
        //  cannot inherit `BoxWidget`, so these six interfaces are forwarded here -- semantics aligned one by one
        //  (setting a path clears the handle, empty string = clear, theme injection does not touch this group).
        //
        //  * The only difference from `BoxWidget`: a card is rounded by default, while an image is an axis-aligned rectangle
        //    with no rounded-corner mask. So here the image is inset on all four sides by the "shoulder width"
        //    (`r x (1 - 1/sqrt2) ~ 0.293r`) -- the inset rectangle falls entirely inside the rounded rectangle,
        //    so the four corners do not poke out. This is the same idea as `Avatar` taking the inscribed square in a circular avatar.
        //    To fill to the edge, turn off the rounded corners (theme `card.box.radiusMode = RadiusMode::None`).
        //
        //  Paint position: inside the border, above the background color, below the title -- so an opaque card background
        //  color does not block the image, and the title is always on top of the image.
        // -------------------------------------------------------------------

        /// @brief Sets the background image (pass an empty string = clear)
        /// @param path Image file path (PNG / JPG / BMP / TGA / DDS..., supported by SOIL2)
        /// @note Only records the path, does not load -- decoding / texture upload happens in the render layer (same
        ///       pattern as `Button`'s icon and `BoxWidget`'s background image), so this interface
        ///       can be called before `AddWidget` (there is no window yet, let alone a graphics context).
        /// @note If the path does not exist the card paints as usual (the image is silently skipped); the error is only reported once on first load.
        /// @note If a pre-parsed handle was given earlier, it is cleared here (path takes priority).
        /// @see SetBackgroundDrawMode, SetBackgroundAlign, SetBackgroundTint
        void SetBackgroundImage(std::string path);

        /// @brief Clears the background image (clears both path and pre-parsed handle)
        void ClearBackgroundImage();

        /// @brief Current background image path (empty = no background image)
        const std::string& backgroundImagePath() const { return cardBox_.imagePath; }

        /// @brief Background image draw mode (Stretch / Fit / Fill / Tiled / Origin, default Stretch)
        void SetBackgroundDrawMode(BgImageDrawMode mode);

        /// @brief Background image alignment (only effective in Origin mode)
        void SetBackgroundAlign(BgImageHAlign hAlign, BgImageVAlign vAlign);

        /// @brief Background image tint (multiplied component-wise with the texture; white = output as-is, can darken / colorize overall)
        void SetBackgroundTint(const Color4f& c);

        // -------------------------------------------------------------------
        //  Measurement protocol: how big the card wants to be = what the content area wants + padding + title area
        // -------------------------------------------------------------------
        float hintWidth() const override;
        float hintHeight() const override;

        // -------------------------------------------------------------------
        //  Theme
        // -------------------------------------------------------------------
        // @brief Re-injects from the theme `card` section (box + title text + title area height + shadow)
        void ApplyTheme(const Theme& t) override;

        // -------------------------------------------------------------------
        //  Paint
        // -------------------------------------------------------------------
        // @brief Shadow -> box (border + background color) -> title -> children handed to the window
        void Paint(FrameCollector& fc) const override;

    protected:
        /// @brief The card manages its own layout (making room for the title area) -- the override only makes the "top padding" follow
        ///        the title area height; the arrangement algorithm still reuses `Panel`'s
        void DoLayout() override;

    private:
        /// @brief Recomputes the padding in all four directions from the theme padding + title area height
        /// @note Card padding = theme padding (four sides), with the title area height added on top.
        void syncPadding();

        std::string title_;
        TextStyle   titleStyle_;
        BoxStyle    cardBox_;                 /// < Card box style (injected from theme card.box)
        float       titleHeight_ = 0.0f;      /// < Title area height (0 = no title area)
        float       titleHeightOverride_ = -1.0f; /// < When >= 0, overrides the theme value
        float       padLeft_ = 12.0f;
        float       padTop_ = 12.0f;
        float       padRight_ = 12.0f;
        float       padBottom_ = 12.0f;
        bool        shadowEnabled_ = true;
        Color4f     shadowColor_{ 0.0f, 0.0f, 0.0f, 0.10f };
        float       shadowOffsetY_ = 2.0f;
        float       shadowSpread_ = 1.0f;
    };

} // namespace Kit
