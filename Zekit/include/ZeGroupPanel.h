/**
 * @file ZeGroupPanel.h
 * @brief Group panel: "encloses a block of content and gives it a name" (fieldset look)
 *
 * @details
 * `GroupPanel : Panel` -- arrangement / padding / child clipping all come from `Panel` (that is,
 * `LayoutBase`); this class only answers "how a group panel differs from a plain panel":
 *
 *  1. A ring of border (not a shadow) -- a group expresses itself by "delimiting", a card by "floating";
 *  2. A title sitting on the border line -- the title occupies one top padding strip (the same trick as `Card` / `TabView`
 *     writing the "title area" as top padding), and the top edge leaves a notch at the title segment;
 *  3. A component-style add interface -- `AddContent` is just `AddChild`, so call sites read as
 *     "adding content".
 *
 * @par Why not draw the border with `BoxWidget`'s box
 * `BoxWidget`'s border is two rectangles -- an outer layer filled with the border color plus an inner layer filled with the base color -- when the base color is transparent the
 * outer layer fills the whole panel with the border color. A group panel allows "drawing just a ring of line on the background",
 * so the border is instead drawn as a thick line segment along the rounded-rectangle outline (see the implementation), which holds whether or not the base color is transparent.
 *
 * @par Usage
 * @code
 * Kit::GroupPanel group("Account info");
 * group.AddContent(Kit::Label("Username"));
 * group.AddContent(Kit::InputBox("Please enter"));
 * win.AddWidget(group);
 * @endcode
 *
 * @see ZeCard.h      the "floating" container of the same family
 * @see ZeTheme.h     `GroupPanelStyle`
 */

#pragma once
#include "ZePanel.h"
#include "ZeTextStyle.h"

#include <string>
#include <utility>

namespace Kit {

    /**
     * @brief Group panel: border + title sitting on the border line + content area
     */
    class GroupPanel : public Panel {
    public:
        /// @brief Constructs
        /// @param title title text; empty string = no title (occupies no title area, draws only a ring of border)
        explicit GroupPanel(std::string title = std::string());

        // -------------------------------------------------------------------
        //  Title
        // -------------------------------------------------------------------

        /// @brief Sets / clears the title (empty string = no title)
        void SetTitle(std::string title);
        const std::string& Title() const { return title_; }
        bool hasTitle() const { return !title_.empty(); }

        /// @brief Title row height (pixels; <= 0 = occupies no space)
        void SetTitleHeight(float h);
        float titleHeight() const { return titleHeight_; }

        /// @brief Title text style
        TextStyle& titleStyle() { return titleStyle_; }
        const TextStyle& titleStyle() const { return titleStyle_; }

        // -------------------------------------------------------------------
        //  Appearance (value interface -- forwards to Panel's skeleton)
        // -------------------------------------------------------------------

        /// @brief Border line width (pixels; 0 = no border)
        void SetFrameWidth(float px);
        float frameWidth() const { return frameWidth_; }

        /// @brief Border color
        void SetFrameColor(const Color4f& c);
        const Color4f& frameColor() const { return frameColor_; }

        /// @brief Width of the notch left on the border on both sides of the title text
        void SetTitleGap(float px);
        float titleGap() const { return titleGap_; }

        // -------------------------------------------------------------------
        //  Content area
        // -------------------------------------------------------------------

        /// @brief Adds a child to the content area (takes ownership by value) -- equivalent to `AddChild`
        template<class T>
        T& AddContent(T&& child) { return this->AddChild(std::forward<T>(child)); }

        // -------------------------------------------------------------------
        //  Measurement protocol
        // -------------------------------------------------------------------
        float hintWidth() const override;
        float hintHeight() const override;

        // -------------------------------------------------------------------
        //  Theme
        // -------------------------------------------------------------------
        void ApplyTheme(const Theme& t) override;

        // -------------------------------------------------------------------
        //  Paint
        // -------------------------------------------------------------------
        // @brief Border (with the notch at the title) -> title text; children are painted recursively by the window
        void Paint(FrameCollector& fc) const override;

    protected:
        /// @brief The content area origin must subtract the title area -> sync the padding once before each layout round
        void DoLayout() override;

    private:
        /// @brief Recomputes Panel's four-side padding from the current padding / title area height
        void syncPadding();

        std::string title_;
        TextStyle   titleStyle_;
        Color4f     background_{ 0.0f, 0.0f, 0.0f, 0.0f };
        Color4f     frameColor_{ 0.0f, 0.0f, 0.0f, 1.0f };
        float       frameWidth_ = 1.0f;
        float       radius_ = 8.0f;
        float       padLeft_ = 12.0f;
        float       padTop_ = 12.0f;
        float       padRight_ = 12.0f;
        float       padBottom_ = 12.0f;
        float       titleHeight_ = 22.0f;
        float       titleGap_ = 6.0f;
    };

} // namespace Kit
