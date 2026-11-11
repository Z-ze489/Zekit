/**
 * @file ZeRadioButton.h
 * @brief Radio button: a circular indicator + one line of label, one-of-many within a group
 *
 * @details
 * `RadioButton : TextWidget` -- same family of interaction as `Checkbox` (click to toggle),
 * but different semantics: mutually exclusive within a group; clicking one clears all others in the group.
 *
 * This class adds only three things:
 *  1. Selected state: `SetSelected(bool)` / `selected()` -- same kind as the checkbox's `checked`,
 *     but cannot be cleared by itself (clicking an already selected radio button does not turn it off -- that is radio semantics:
 *     either stay on it, or switch to another in the group; to clear, use programmatic `SetSelected(false)` or
 *     `ClearGroup()`);
 *  2. Grouping: `SetGroup("name")` -- group name + the same widget tree together define a group.
 *     When a radio button is clicked, it walks up to the tree root, traverses the whole tree, and clears all other radio buttons
 *     with the same group name;
 *  3. Selected callback: `SetOnSelected` / `SetOnSelected(SelfAction)`, triggered only on the user path
 *     (click / keyboard Space); programmatic `SetSelected` does not trigger -- same convention as Slider / Checkbox.
 *
 * @par How grouping works (important)
 * Group = group name (`group()`) + the same widget tree (walk up from this widget along `parent()` to the tree root,
 * find other radio buttons with the same name in the whole tree). As long as they are in the same tree, the same group name means one group,
 * mutually exclusive; different group names mean two independent groups. The default group name is the empty string -- the empty string is also a group,
 * so "radio buttons with no group name set" automatically form one group (mutually exclusive).
 * To separate them completely, explicitly `SetGroup("A")` / `SetGroup("B")`.
 *
 * > Why use "group name + tree" instead of "parent container": using the parent container as the group requires the user to guarantee that the radio buttons are direct children of the same parent,
 * > which is too fragile (wrapping in a Panel / VBox breaks it). With group name + tree, any nesting depth is stable.
 * >
 * > Why the tree root instead of `window()`: mutual exclusion is a "within the same tree" semantic -- a widget tree must also hold
 * > before it is attached to a `Window` (for example in tests, or a pre-built Panel subtree). Early implementations
 * > started from `window()`, which returns `nullptr` when not attached => mutual exclusion silently fails; starting from the tree root,
 * > it holds with or without a window.
 *
 * @par Basic usage
 * @code
 * Kit::RadioButton a("Small",  true);
 * Kit::RadioButton b("Medium");
 * Kit::RadioButton c("Large");
 * a.SetGroup("size"); b.SetGroup("size"); c.SetGroup("size");
 * a.SetOnSelected([](Kit::Widget& self) {
 *     std::printf("selected is %s\n", Kit::As<Kit::RadioButton>(self).Text().c_str());
 * });
 * win.AddWidget(a); win.AddWidget(b); win.AddWidget(c);
 * @endcode
 *
 * @par Size
 * Overrides `hintWidth` / `hintHeight`; natural size = indicator diameter + spacing + label text width
 * x one line of text height (plus padding and border). Same measurement protocol as Checkbox.
 *
 * > WARNING: hint must be overridden -- otherwise placing it in a `VBoxLayout` collapses to 0 height, is completely invisible, and reports no error.
 * > See the collapse chain in "25 Progress Bar" section 2.1.
 *
 * @see ZeCheckbox.h checkbox (same kind of interaction, but not mutually exclusive, multi-select)
 */

#pragma once
#include "ZeTextWidget.h"
#include "ZeSelfAction.h"
#include "ZeIndicator.h"

#include <functional>
#include <string>

namespace Kit {

    /**
     * @brief Radio button: circular indicator + label text, mutually exclusive within a group
     *
     * @details
     * Shares almost the entire appearance skeleton with `Checkbox` (indicator + spacing + text); only two differences:
     *  - the indicator is a circle (not a square box); when selected, a solid dot is drawn in the center (not a check mark);
     *  - the toggle semantics are mutually exclusive (selecting itself clears others in the group), and it cannot be cleared by clicking itself.
     */
    class RadioButton : public TextWidget {
    public:
        /// @brief Constructs a radio button
        /// @param text     label text
        /// @param selected whether initially selected (default no)
        /// @note At construction, the checkbox section of the built-in light theme is used as default (shared with Checkbox --
        ///       both are indicator widgets of the same family; `themeBox` / `themeText` are virtual functions,
        ///       so they cannot be called in the base class constructor, otherwise only the base class version is hit).
        explicit RadioButton(std::string text = std::string(), bool selected = false);

        // -------------------------------------------------------------------
        //  Selected state
        // -------------------------------------------------------------------
        //  * Difference from Checkbox: clicking an already selected self does not clear it (radio semantics).
        //    To clear, there are only two ways: programmatic SetSelected(false), or click another in the group (which clears
        //    this one). So there is no Toggle() here. The selected state is persistent (not a transient pressed state).
        // -------------------------------------------------------------------

        /// @brief Sets whether selected (programmatic path, does not trigger callback)
        /// @param on true = selected (clears others in the group); false = clear selection
        /// @note Passing true also clears other radio buttons in the group -- the programmatic path also obeys mutual exclusion semantics.
        void SetSelected(bool on);

        /// @brief Whether currently selected
        bool selected() const { return selected_; }

        // -------------------------------------------------------------------
        //  Grouping
        // -------------------------------------------------------------------

        /// @brief Sets the group name (default empty string -- the empty string is also a group)
        /// @param name group name; radio buttons with the same group name in the same tree are mutually exclusive
        /// @note Changing the group name does not automatically adjust the current selected state (it only affects subsequent mutual exclusion behavior).
        void SetGroup(std::string name);

        /// @brief Current group name
        const std::string& group() const { return group_; }

        // -------------------------------------------------------------------
        //  Selected callback (outward notification -- triggered only on the user path)
        // -------------------------------------------------------------------
        //  * Triggered only when the user clicks this item (click / keyboard Space). Programmatic SetSelected(true)
        //    does not trigger -- same convention as Slider / Checkbox, preventing a parent setting a value from triggering its own callback.
        //    WARNING: other radio buttons cleared by mutual exclusion also do not trigger the callback
        //    (they were not clicked by the user). To detect "cleared", traverse the group externally.
        // -------------------------------------------------------------------

        /// @brief Selected callback type (only carries the sender -- a radio button's selection is binary, on is always true)
        using SelectedAction = SelfAction;

        /// @brief Sets the "selected" callback (triggered when the user clicks this item)
        /// @param handler callback; `self` is the radio button itself; pass empty to clear
        void SetOnSelected(SelectedAction handler);

        /// @brief Sets the "selected" callback (payload version: whether selected, always true -- for uniform style)
        /// @param handler callback; pass empty to clear
        void SetOnSelected(std::function<void(Widget&, bool)> handler);

        // -------------------------------------------------------------------
        //  Appearance
        // -------------------------------------------------------------------

        /// @brief Indicator diameter (pixels); 0 = automatic (matches one line of text height)
        void SetIndicatorSize(float size);
        /// @brief Current indicator diameter (0 = automatic)
        float indicatorSize() const { return indicatorSize_; }

        /// @brief Indicator shape (default `Circle`)
        /// @note A square radio button is also legal ("square radio" is common in toolbars / compact layouts);
        ///       semantics are unchanged -- mutual exclusion is still determined by group name + widget tree, regardless of the drawn shape.
        /// @see ZeIndicator.h
        void SetIndicatorShape(IndicatorShape shape);
        /// @brief Current indicator shape
        IndicatorShape indicatorShape() const { return shape_; }

        /// @brief Mark drawn in the container when selected (dot / solid / thick ring / check / bar)
        /// @note Default `Dot` (classic donut + core appearance).
        ///       The mark color is taken automatically from the container (on a solid container, the indicator background color); no separate setting needed.
        void SetMarkStyle(RadioMark mark);
        /// @brief Current selected mark
        RadioMark markStyle() const { return mark_; }

        /// @brief Spacing between indicator and label text (pixels, default 8)
        void SetSpacing(float gap);
        /// @brief Current spacing between indicator and text
        float spacing() const { return spacing_; }

        /// @brief Indicator (unselected) background color
        void SetIndicatorColor(const Color4f& c);
        /// @brief Indicator border color (ring when unselected)
        void SetIndicatorBorderColor(const Color4f& c);
        /// @brief Selected color (ring + center solid dot)
        void SetMarkColor(const Color4f& c);

        /// @brief Current indicator background color
        const Color4f& indicatorColor() const { return indicatorColor_; }
        /// @brief Current indicator border color
        const Color4f& indicatorBorderColor() const { return indicatorBorderColor_; }
        /// @brief Current selected color
        const Color4f& markColor() const { return markColor_; }

        // -------------------------------------------------------------------
        //  Self-reported natural size (measurement protocol) -- required for VBox / FlowLayout
        // -------------------------------------------------------------------

        /// @brief Natural width = indicator diameter + spacing + label text width + padding + border
        float hintWidth() const override;

        /// @brief Natural height = max(indicator diameter, one line of text height) + padding + border
        float hintHeight() const override;

        // -------------------------------------------------------------------
        //  Theme injection
        // -------------------------------------------------------------------

        /// @brief Re-injects style from the theme (shares the checkbox section with Checkbox)
        void ApplyTheme(const Theme& t) override;

        // -------------------------------------------------------------------
        //  Interaction events
        // -------------------------------------------------------------------

        bool OnMousePress(MouseEvent& e) override;
        bool OnMouseRelease(MouseEvent& e) override;
        bool OnKeyPress(KeyEvent& e) override;

        /// @brief Selects this item (clears others in the group + triggers callback) -- the only selection exit
        /// @note Both mouse click and keyboard Space call it. In disabled state it returns immediately.
        ///       Calling it again when already selected: does not re-trigger the callback (idempotent).
        virtual void Activate();

    protected:
        /// @brief Default box style takes the theme's checkbox section (shared with Checkbox)
        const BoxStyle& themeBox(const Theme& t) const override;
        /// @brief Default text style takes the theme's checkbox section (shared with Checkbox)
        const TextStyle& themeText(const Theme& t) const override;

        /// @brief Paints: circular indicator (ring + optional solid dot) + label text
        void paintContent(FrameCollector& fc) const override;

    private:
        /// @brief Computes the indicator diameter (explicit value takes priority, otherwise matches one line of text height, fallback 18)
        float resolvedIndicatorSize() const;

        /// @brief Clears the selected state of other radio buttons in the group (traverses the whole tree from the root)
        /// @param self this widget (skipped, does not clear itself)
        void uncheckSiblings(const Widget& self);

        bool        selected_ = false;               /// < whether selected
        std::string group_;                          /// < group name (the empty string is also a group)

        float       indicatorSize_ = 0.0f;           /// < indicator diameter (0 = automatic)
        float       spacing_ = 8.0f;                 /// < spacing between indicator and text

        IndicatorShape shape_ = IndicatorShape::Circle;  /// < container shape (default circle)
        RadioMark      mark_ = RadioMark::Dot;           /// < selected mark (default center dot)

        Color4f     indicatorColor_{ 1.0f, 1.0f, 1.0f, 1.0f };       /// < indicator background color
        Color4f     indicatorBorderColor_{ 0.0f, 0.0f, 0.0f, 1.0f };  /// < unselected ring color
        Color4f     markColor_{ 0.0f, 0.0f, 0.0f, 1.0f };            /// < selected color (ring + dot)

        SelectedAction                    onSelected_;       /// < selected callback (only self)
        std::function<void(Widget&, bool)> onSelectedBool_;  /// < selected callback (with bool)
    };

} // namespace Kit
