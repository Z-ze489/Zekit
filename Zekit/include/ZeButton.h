/**
 * @file ZeButton.h
 * @brief Button: rounded box (background + border) + content (icon + text), with click interaction
 *
 * @details
 * This class now only holds what belongs to the button itself -- everything else is inherited from the two base layers:
 *
 * | From | Content |
 * |---|---|
 * | `TextWidget` | text content + TextStyle + font/alignment/decoration setters + text four-state colors |
 * | `BoxWidget`  | BoxStyle + box setter wall + shadow/outline/border background painting + interaction state |
 * | `Widget`     | geometry / visibility / hit test / events / focus fields / value semantics |
 *
 * The button keeps three things of its own (criterion: other widgets cannot use them, or the semantics hold only for buttons):
 *  1. Icon block: `IconPosition` + the `SetImage` family, participating in the overall alignment of the
 *     content block "icon + spacing + text". When Label / CheckBox also need icons, promote it to
 *     TextWidget under the rule "define once library-wide" -- only then is the common part known.
 *  2. Action callbacks: `SetOnPress / SetOnRelease / SetOnClick` + `Activate()`;
 *     they bind the "click" semantics, which other widgets have no counterpart for.
 *  3. Click determination: press established -> not moved out -> release. Moving out cancels (see OnMouseLeave, base class implementation).
 *
 * Painting only overrides `paintContent()`: the three box layers (shadow / outline / border background) go through
 * the template method of BoxWidget::Paint, so the paint order is not rewritten here.
 *
 * @note Changing the theme = Window iterates all widgets and calls ApplyTheme() to re-inject.
 *       The button does not override it -- by default it takes the theme's button section (see BoxWidget::themeBox / TextWidget::themeText).
 *
 * @see ZeBoxWidget.h box layer (style fields and box painting)
 * @see ZeTextWidget.h text layer (font and single-line layout)
 * @see ZeTheme.h theme (source of default values)
 */

#pragma once
#include <functional>
#include <string>
#include <utility>

#include "ZeSelfAction.h"    // Event callback protocol: self is injected by the library at dispatch time
#include "ZeTextWidget.h"

namespace Kit {

    /**
     * @brief Placement direction of the icon relative to the text
     *
     * @details
     * Determines how the content block "icon + text" is arranged: horizontal (Left/Right) or vertical (Top/Bottom).
     * The spacing between icon and text is controlled by SetIconSpacing; the position of the whole block in the content area is
     * controlled by TextStyle's hAlign / vAlign (same alignment semantics as text-only).
     *
     * None = do not arrange the icon (the button degrades to text only) -- the icon path stays in the member and can be re-enabled at any time.
     * To show only the icon without text, set the text to an empty string.
     *
     * @note Defined in the button header (currently only the button uses it). When Label / CheckBox also need icons,
     *       promote it to TextWidget under the rule "define once library-wide".
     */
    enum class IconPosition {
        Left,   /// < icon on the left of the text (default)
        Right,  /// < icon on the right of the text
        Top,    /// < icon above the text
        Bottom, /// < icon below the text
        None    /// < do not arrange the icon
    };

    class Button : public TextWidget {
    public:
        /// @brief Constructs the button
        /// @param text initial text
        /// @note Uses the built-in light theme as default at construction; it is re-injected after registration with Window
        explicit Button(std::string text = std::string());

        // -------------------------------------------------------------------
        //  Action callbacks (outward notification -- the only assembly-style extension point in the library)
        // -------------------------------------------------------------------
        //  Difference from virtual functions (read this before choosing):
        //   - Virtual functions (OnMousePress etc.) mean "the widget itself knows what to do" --
        //     you must derive a class, and one button class can have only one behavior;
        //   - Callbacks mean "assemble an action onto this one button from outside" -- no derivation, assign in place,
        //     ten buttons ten lambdas, and callbacks are moved along with the widget value by AddWidget.
        //  The two do not conflict: this class still uses virtual functions to maintain internal state; callbacks only notify outward.
        //
        //  Conventions:
        //   - No parameters (or one self parameter), no return value, synchronous call (executed during event dispatch);
        //   - The three callbacks target the left mouse button only (consistent with the internal pressed_ determination);
        //   - In the disabled state (SetEnabled(false)) none of them fire;
        //   - If you need mouse coordinates / modifier keys, override the corresponding virtual function; do not expect callbacks to pass parameters.
        //
        //  Two signatures (both usable, independent of each other):
        //   - Action      = std::function<void()>          -- use when the sender is not needed, simplest;
        //   - SelfAction  = std::function<void(Widget&)>   -- use when you need (to modify yourself inside the callback,
        //       or read the sender). The self parameter is injected by the library at dispatch time (not the this
        //       at registration time) and always points to the entity in the tree -- so the following "stranger style" does not
        //       fall into the trap of "capturing a stack object that becomes invalid":
        //          a.SetOnClick([](Kit::Widget& self) {
        //              std::cout << Kit::As<Kit::Button>(self).Text() << std::endl;
        //          });
        //       Compare the capture style [&a]{ a.Text() }: that &a is already moved out when return a happens.
        //
        //  WARNING: SelfAction is the only one library-wide (defined in ZeSelfAction.h); this class no longer
        //     defines its own alias -- new widgets therefore get this protocol inherently, without per-widget handling.
        // -------------------------------------------------------------------

        /// @brief Action callback type (no parameters): the simplest form when the sender is not needed
        /// @note Just an alias of std::function<void()>, shorter to write.
        using Action = std::function<void()>;

        /**
         * @brief Sets the press callback (fires when the left mouse button is pressed and lands on this button)
         * @param handler callback; pass empty (nullptr / {}) to clear
         * @note Does not fire in the disabled state -- when disabled, press events never reach this button.
         * @note Fires after pressed_ is set, so isPressed() is already true inside the callback.
         * @see SetOnRelease, SetOnClick
         */
        void SetOnPress(Action handler);

        /// @brief Press callback (with sender) -- register this to modify yourself on press
        /// @param handler callback; `self` is the button itself (`Widget&`, use Kit::As<T> to get the concrete interface),
        ///                pass empty to clear
        /// @note Can be registered together with the `Action` version; at dispatch the no-parameter version runs first, then the sender version.
        /// @see SetOnPress
        void SetOnPress(SelfAction handler);

        /**
         * @brief Sets the release callback
         * @param handler callback; pass empty to clear
         * @details
         * Fires only when "this release concludes this press" -- i.e. the left button landed on this button
         * and the mouse did not move out in between (moving out cancels the pressed state, see BoxWidget::OnMouseLeave).
         * It is therefore the paired notification of OnPress and never appears alone.
         * @note Fires when pressed_ has already been reset to false.
         * @see SetOnPress, SetOnClick
         */
        void SetOnRelease(Action handler);

        /// @brief Release callback (with sender) -- see the no-parameter version for details
        /// @param handler callback; `self` is the button itself, pass empty to clear
        /// @see SetOnRelease
        void SetOnRelease(SelfAction handler);

        /**
         * @brief Sets the click callback -- the most commonly used one
         * @param handler callback; pass empty to clear
         * @details
         * The condition is "press established -> not moved out -> release"; when satisfied, Activate() is called.
         * Releasing after moving out, or pressing outside this button, does not fire it.
         * @see Activate, SetOnPress, SetOnRelease
         */
        void SetOnClick(Action handler);

        /// @brief Click callback (with sender) -- register this to modify yourself on click (text/size/...)
        /// @param handler callback; `self` is the button itself, pass empty to clear
        /// @see SetOnClick
        void SetOnClick(SelfAction handler);

        /**
         * @brief Activates the button -- equivalent to "a click", fires the onClick callback
         *
         * @details
         * This is the only click exit: when a mouse click is established it is called internally by OnMouseRelease;
         * future keyboard activation (Enter / Space in the focus state) should also call it,
         * so "mouse click" and "keyboard press" share one path and the widget needs no two sets of logic.
         *
         * Virtual: derived classes can override it to change the meaning of "activate" (e.g. validate before commit,
         * or write the behavior directly in the derived class without callbacks).
         *
         * Returns immediately in the disabled state without firing any callback -- so keyboard / programmatic calls
         * are equally protected by SetEnabled(false).
         *
         * @note Callbacks run synchronously; do not call Activate() again inside a callback -- it recurses infinitely.
         * @see SetOnClick
         */
        virtual void Activate();

        // -------------------------------------------------------------------
        //  Icon (foreground image)
        // -------------------------------------------------------------------
        //  The widget stores only the path; decoding / texture upload / caching all live in the render layer (ZeImageStore).
        //  Thus SetImage can be called before AddWidget -- when there is no window yet, let alone a GL context.
        //  At paint time Paint hands the path to FrameCollector; a cache hit costs nothing extra.
        //  Contract: icon and text form one content block that participates in alignment together -- with an icon,
        //        the centered object is the whole "icon + spacing + text", not the text alone.
        // -------------------------------------------------------------------

        /**
         * @brief Sets the icon (foreground image)
         * @param path image file path (PNG / JPG / BMP / TGA / DDS..., supported by SOIL2);
         *             empty string = remove the icon
         * @note Only records the path, does not load -- actual loading is done by the render layer at paint time.
         * @note If the path does not exist, the button still paints the text (the icon is silently skipped); the error is reported once on first load.
         * @see SetIconPosition, SetIconSize, SetIconSpacing, SetIconDrawMode
         */
        void SetImage(std::string path);

        /// @brief Current icon path (empty = no icon)
        const std::string& Image() const { return iconPath_; }

        /// @brief Sets the icon position relative to the text (default Left)
        void SetIconPosition(IconPosition pos);
        /// @brief Current icon position
        IconPosition iconPosition() const { return iconPos_; }

        /**
         * @brief Sets the icon box size (pixels)
         * @param w, h icon drawing area; either being 0 means "use the image's original size"
         * @note This is the box; the actual drawing is decided by iconDrawMode (default Fit = scale to fit completely).
         *       To force stretching, pair with SetIconDrawMode(BgImageDrawMode::Stretch).
         */
        void SetIconSize(float w, float h);

        /// @brief Spacing between icon and text (pixels, default 6)
        void SetIconSpacing(float gap);

        /// @brief Sets the icon scaling mode (Stretch / Fit / Fill / Tiled / Origin)
        void SetIconDrawMode(BgImageDrawMode mode);

        /// @brief Sets the icon tint (multiplied with the texture; white = output as-is, usable for recoloring monochrome icons)
        void SetIconTint(const Color4f& c);

        // -------------------------------------------------------------------
        //  Interaction events
        // -------------------------------------------------------------------
        //  Left button only; after pressing, this click is consumed (no further bubbling).
        //  hover / cancel-press-on-leave are handled by BoxWidget's default implementation and not rewritten here.
        // -------------------------------------------------------------------

        bool OnMousePress(MouseEvent& e) override;
        bool OnMouseRelease(MouseEvent& e) override;

        // -------------------------------------------------------------------
        //  Self-reported natural size (measurement protocol): measures "how big I originally want to be" from "icon + spacing + text"
        // -------------------------------------------------------------------
        // @brief Natural width = content block width (icon + spacing + text, arranged horizontally/vertically per iconPos) + padding + border
        // @note Returns the explicit value when width was set explicitly via `SetSize` (user intent takes priority).
        // @note Icon size comes from the original image in `ImageStore` (or explicit `SetIconSize`);
        //       when no window/image is available, that part counts as 0.
        float hintWidth() const override;

        /// @brief Natural height = content block height + padding + border
        /// @see hintWidth
        float hintHeight() const override;

    protected:
        /// @brief Content painting: aligns "icon + spacing + text" as one whole within the content area
        /// @note The three box layers (shadow/outline/background) are handled by BoxWidget::Paint.
        void paintContent(FrameCollector& fc) const override;

        /// @brief Size of the content block (icon + spacing + text, in the arrangement direction determined by iconPos)
        /// @param[out] w,h content block width / height (excluding padding and border)
        /// @return false = neither icon nor text (content empty, size 0)
        /// @note Reused by `hintWidth()` / `hintHeight()` to avoid arranging twice.
        ///       Must be consistent with the arrangement rules of `paintContent`, otherwise the measured box will be
        ///       misaligned with the actually painted content.
        bool contentBlockSize(float& w, float& h) const;

    private:
        // ---- Icon (foreground image) ----
        //  Path only: decoding and texture upload belong to the render layer (ZeImageStore), so these fields
        //  can be set normally during the configuration phase "before there is a window" and are moved along with the value by AddWidget.
        std::string     iconPath_;                             /// < image path (empty = no icon)
        IconPosition    iconPos_ = IconPosition::Left;         /// < icon position relative to the text
        float           iconW_ = 0.0f, iconH_ = 0.0f;          /// < icon box size (0 = use original image size)
        float           iconGap_ = 6.0f;                       /// < spacing between icon and text (pixels)
        BgImageDrawMode iconMode_ = BgImageDrawMode::Fit;      /// < icon scaling mode
        Color4f         iconTint_ = Color4f::White();          /// < icon tint (white = as-is)

        // Action callbacks (std::function is movable and is moved along with this class's implicit move constructor by AddWidget).
        // The two signature sets are stored separately: the no-parameter version and the self version are independent and may be registered together (both are called at dispatch).
        Action     onPress_;        /// < press callback (no parameters)
        Action     onRelease_;      /// < release callback (no parameters, conclusion of OnPress)
        Action     onClick_;        /// < click callback (no parameters, fired by Activate)
        SelfAction selfOnPress_;    /// < press callback (with self)
        SelfAction selfOnRelease_;  /// < release callback (with self)
        SelfAction selfOnClick_;    /// < click callback (with self)
    };

} // namespace Kit
