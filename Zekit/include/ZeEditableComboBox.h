/**
 * @file ZeEditableComboBox.h
 * @brief Editable combo box: select from the list, or type directly on the face
 *
 * @details
 * `EditableComboBox : ComboBox` -- popup panel, arrow, item data, selected state, the three-part overlay
 * convention (`paintsOnTop` / `HitTest` / `ResetClip`), collapse on focus loss ... all of it comes
 * from `ComboBox`. This class adds exactly one thing: the line of text on the face becomes a real edit box.
 *
 * @par How it is added (where "do not reinvent the wheel" lands)
 * The text area inside the face hosts an `InputBox` child widget (the `TextEditBase` branch), so typing /
 * backspace / selection / drag-select / `Ctrl+A/C/X/V` / cursor blink / IME input -- none of it is
 * rewritten. The editing core is the existing one. This class only does three pieces of glue:
 *
 *  1. `drawsOwnFaceText()` overridden to `false` -> the base class no longer paints its own line of text
 *     (the edit box paints it);
 *  2. `commitSelection()` overridden -> after the user picks an item from the popup panel, the value is
 *     synced into the edit box;
 *  3. edit box content changes -> look up its index among the items and sync it to the base class selected
 *     state (no match means "free text", selected state set to -1).
 *
 * @par How the child widget is mounted
 * Uses `AddWidgetUnchecked` (this class positions the child itself: the face frame is painted by the base
 * class, the edit box is placed in the face text area, leaving room for the arrow on the right). The edit
 * box box layer is forced transparent (`FaceEditor` overrides `ApplyTheme`), so it only paints text and
 * cursor and does not smear another frame onto the face.
 *
 * @par Interaction
 * | Action | Behavior |
 * |---|---|
 * | Click in the text area | Focuses the edit box, typing works directly (the popup panel collapses) |
 * | Click the right arrow | Expands / collapses the popup panel |
 * | Click an item in the popup panel | That text is written into the edit box + collapses (and fires the selection callback) |
 * | Typing | Free text; when exactly equal to an item, syncs to "that item selected" |
 * | `Esc` (while the edit box has focus) | Handed back to the parent (this class does not swallow it) |
 *
 * @note Programmatic paths (`SetValue` / `SetSelectedIndex`) do not fire callbacks -- consistent with the
 *       rest of the library; callback semantics are "the user changed it".
 *
 * @code
 * Kit::EditableComboBox city("Please select or enter a city");
 * city.SetItems({ "Beijing", "Shanghai", "Guangzhou" });
 * city.SetValue("Nanjing");                    // free text is also valid
 * city.SetOnTextChanged([](const std::string& s) { std::printf("%s\n", s.c_str()); });
 * @endcode
 *
 * @see ZeComboBox.h         popup panel and overlay mechanism (base class)
 * @see ZeInputBox.h         edit box on the face (editing core)
 */

#pragma once
#include "ZeComboBox.h"

#include <functional>
#include <string>

namespace Kit {

    namespace detail { class FaceEditor; }   /// < in-face edit box (implemented in .cpp)

    /**
     * @brief Editable combo box
     */
    class EditableComboBox : public ComboBox {
    public:
        /// @brief Constructs
        /// @param placeholder placeholder hint when empty (fed to both the face and the edit box)
        explicit EditableComboBox(std::string placeholder = std::string());

        // -------------------------------------------------------------------
        //  Value (free text)
        // -------------------------------------------------------------------

        /// @brief Sets the text on the face (programmatic path: does not fire callbacks)
        /// @note Text exactly equal to an item -> selected state syncs to that item; otherwise free text (-1).
        void SetValue(const std::string& text);

        /// @brief Returns the current text on the face
        std::string value() const;

        /// @brief Sets the placeholder hint (updates both the base class copy and the edit box copy)
        /// @note Shadows `ComboBox::SetPlaceholder` -- semantics are a superset (changes both places).
        void SetPlaceholder(std::string text);

        // -------------------------------------------------------------------
        //  Callbacks (fired only on the user path)
        // -------------------------------------------------------------------

        /// @brief "Text changed" callback (fired after the user types / deletes / pastes)
        void SetOnTextChanged(std::function<void(const std::string&)> handler);
        /// @brief "Text changed" callback (sender-carrying version)
        void SetOnTextChanged(SelfAction1<const std::string&> handler);

        // -------------------------------------------------------------------
        //  In-face edit box (optional fine-tuning)
        // -------------------------------------------------------------------

        /// @brief Whether the in-face edit box is read-only (when read-only, the arrow still selects from the list)
        void SetEditorReadOnly(bool ro);
        /// @brief Whether the in-face edit box currently holds keyboard focus
        bool editorHasFocus() const;

    protected:
        /// @brief The text on the face is painted by the embedded edit box -> the base class no longer paints that line
        bool drawsOwnFaceText() const override { return false; }

        /// @brief This widget positions its own child (the single embedded edit box, position computed in `paintContent`)
        /// @details
        /// Not overriding it does not produce "wrong layout"; it prints a line to stderr on every startup:
        /// `layout warning: EditableComboBox is not a layout container, yet a child widget FaceEditor was added` --
        /// that is a hint for people who "mistake a Widget for a container", while the mounting here is intentional
        /// (the edit box position depends on the arrow area width and the text area, which only this class can
        /// compute). Overriding it writes that intent into the type, so the diagnostic should no longer fire for
        /// this class.
        bool arrangesChildren() const override { return true; }

        /// @brief The user picked an item from the popup panel -> sync it into the edit box
        void commitSelection(int index) override;

        /// @brief Positions the edit box + syncs the text style, then hands back to the base class to paint the face frame / arrow / popup panel
        void paintContent(FrameCollector& fc) const override;

        /// @brief Clips the in-face edit box paint / hit test to inside the face frame (within the stroke)
        /// @details
        /// The edit box is a child widget, and `Widget::clipChildrenRect` returns false by default (no clipping) --
        /// so the text it paints can run outside the face frame:
        ///   - when the font size exceeds the frame height, it overflows the face frame vertically ("font exceeds frame");
        ///   - when the value/placeholder is wider than the text area, it paints rightward past the arrow and beyond the right frame.
        /// The face text used to be painted by `ComboBox` itself (with `ellipsizeLine`); after moving into the edit box
        /// both protections were lost -- this restores the "cannot paint outside the frame" half (see the implementation notes in .cpp).
        bool clipChildrenRect(float& ox, float& oy, float& ow, float& oh) const override;

        /// @brief Theme injection (the base class copy + syncs the face text style to the edit box)
        void ApplyTheme(const Theme& t) override;

    private:
        /// @brief Body changed (the edit box "text changed" callback is routed here)
        void notifyTextChanged(const std::string& text);
        /// @brief Finds the item exactly equal to text among the items (-1 = none)
        int indexOfText(const std::string& text) const;
        /// @brief Syncs the face text style to the edit box (once per frame, with a difference check)
        void syncEditorStyle() const;

        detail::FaceEditor* editor_ = nullptr;   /// < the copy in the tree (raw pointer: the object lives in this widget storage)

        // position placed last frame (written back to the edit box only on change, avoiding per-frame SetGeometry jitter)
        mutable float lastX_ = -1.0f, lastY_ = -1.0f, lastW_ = -1.0f, lastH_ = -1.0f;

        bool suppressTextNotify_ = false;        /// < suppresses callbacks during programmatic SetValue
        std::function<void(const std::string&)> onText_;
        SelfAction1<const std::string&> onTextSelf_;
    };

} // namespace Kit
