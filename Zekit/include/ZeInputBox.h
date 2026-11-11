/**
 * @file ZeInputBox.h
 * @brief Input box: **single-line** text editing (caret / placeholder / horizontal scroll / clipboard / callbacks)
 *
 * @details
 * `InputBox : TextEditBase` -- the editing capabilities (typing / backspace / selection / drag-select / Ctrl+A/C/X/V /
 * caret blink / Chinese IME / click-to-caret) **all come from `TextEditBase`**; this class only answers
 * "where do these characters go": **one line**. So what remains in this header is only three things:
 *
 *   - `buildLayout()` -- treats the whole text as **one line** (width = pixel width of that line of text);
 *   - `hintWidth()` / `hintHeight()` -- report the natural size (height is always one line high);
 *   - sets the default size to 200x34 in the constructor.
 *
 * Every difference between single-line and multi-line collapses into a few switches on `TextEditBase`:
 *
 *  | Switch | This class | `TextArea` |
 *  |---|---|---|
 *  | `acceptsNewline()` | **false** -- Enter = submit; pasted newlines fold into spaces | true -- Enter = newline, Ctrl+Enter submits |
 *  | `acceptsTab()` | **false** -- Tab goes back to the window for focus traversal | true -- Tab inserts indentation |
 *  | `keyVerticalMove()` | **not overridden** -- Up/Down go back to the window | overridden -- previous/next line (with remembered column) |
 *  | Wrapping | one line, horizontal scroll follows the caret when too wide | multi-line soft wrap + vertical scroll |
 *
 * The remaining interfaces (`SetText` / `SetPlaceholder` / `SetReadOnly` / `SelectAll` / `Copy` /
 * `Cut` / `Paste` / `SetCaretColor` / `SetPlaceholderColor` / `SetOnReturn` /
 * `SetOnTextChanged`) are **inherited from `TextEditBase`**; see that header for signatures and semantics.
 *
 * @par Basic usage
 * @code
 * Kit::InputBox name("Enter user name");
 * name.SetGeometry(20, 20, 220, 34);
 * name.SetOnReturn([](const std::string& s) { std::printf("submit %s\n", s.c_str()); });
 * win.AddWidget(name);            // click it -> takes focus automatically -> type directly
 * @endcode
 *
 * @see ZeTextEditBase.h editing core (all editing behavior of this class lives there)
 * @see ZeTextArea.h     multi-line version (same core, different layout)
 * @see ZeTextWidget.h   text layer primitives (paintTextAt / Utf8Boundaries / MeasureText)
 * @see ZeBoxWidget.h    box layer (background / border / focus-state color lookup)
 */

#pragma once
#include "ZeTextEditBase.h"

#include <string>

namespace Kit {

    /**
     * @brief Single-line input box
     *
     * @details
     * Inheritance chain: `Widget -> BoxWidget -> TextWidget -> TextEditBase -> InputBox`.
     * The theme uses the **input section** (`t.input.box` / `t.input.text`) -- when focused,
     * the border automatically turns primary (six-state color); when disabled it is grayed out. All of this is in TextEditBase.
     */
    class InputBox : public TextEditBase {
    public:
        /// @brief Constructs the input box (focusable: `SetFocusable(true)` is declared in TextEditBase)
        /// @param placeholder Placeholder text (shown when content is empty, uses the theme muted color)
        explicit InputBox(std::string placeholder = std::string());

    protected:
        // -------------------------------------------------------------------
        //  Layout: the whole text = one line
        // -------------------------------------------------------------------

        /// @brief Single-line layout: produces one line `[0, Text().size())`
        /// @note When content is empty, the line width uses the width of the **placeholder text** -- otherwise the placeholder
        ///       in an empty input box would be aligned as a "zero-width line" (drifting outside the box when centered/right-aligned).
        void buildLayout(FrameCollector& fc, EditLayout& out) const override;

        // -------------------------------------------------------------------
        //  Natural size reporting (measurement protocol)
        // -------------------------------------------------------------------

        /// @brief Natural width = text (or placeholder) width + caret width + padding + border
        /// @note Returns the explicit value if `SetSize` was called explicitly. When content is empty, measures by the **placeholder text**,
        ///       otherwise an empty input box would shrink to a line; adds one caret width so the trailing caret is not clipped.
        float hintWidth() const override;

        /// @brief Natural height = one line of text height + padding + border
        /// @note The input box is always single-line, so the height is constant (does not change with content length).
        float hintHeight() const override;
    };

} // namespace Kit
