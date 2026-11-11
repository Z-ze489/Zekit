/**
 * @file ZeTextArea.h
 * @brief Multi-line text box: multi-line text editing (soft wrap / vertical scroll / up-down lines / Tab indent)
 *
 * @details
 * `TextArea : TextEditBase` -- and `InputBox` are two layouts of the same editing kernel:
 * typing, backspace, selection, mouse drag-select, Ctrl+A/C/X/V, cursor blink, Chinese IME (including candidate window positioning),
 * click-to-position cursor, all provided by `TextEditBase`; this class only answers "which line these characters go on".
 *
 * The differences from the single-line box reduce to four switches (see the class comment in `ZeInputBox.h` for the comparison table):
 *
 *  | Behavior | This class | `InputBox` |
 *  |---|---|---|
 *  | Enter | line break (`Ctrl+Enter` submits) | submit |
 *  | Tab | insert indent (can be disabled) | return to window for focus traversal |
 *  | Up/Down | up-down lines (with remembered column) | return to window |
 *  | Overwide | soft wrap to next line (can be disabled; when disabled, horizontal scroll) | horizontal scroll follows cursor |
 *
 * ---------------------------------------------------------------------------
 *  Layout (`buildLayout`)
 * ---------------------------------------------------------------------------
 *  1. First split into paragraphs by `'\n'` (hard line boundaries);
 *  2. Each paragraph is then soft-wrapped by content area width: accumulate width character by character, break before the character that does not fit;
 *     for Latin text, back off to the last space (do not split a word in half); for Chinese without spaces, break at any character boundary.
 *
 *  Break points always fall on UTF-8 character boundaries -- Chinese / emoji are never cut mid-byte.
 *
 * ---------------------------------------------------------------------------
 *  Scrolling
 * ---------------------------------------------------------------------------
 *  Vertical: when content is taller than the content area, both the wheel (two lines per notch) and cursor movement scroll.
 *  - Cursor movement / editing -> automatically bring the cursor into the viewport;
 *  - User scrolls the view away with the wheel -> the cursor is not dragged back on the next frame ("snapping back after scrolling away" is unpleasant).
 *
 *  WARNING: This version has no scrollbar: scrolling relies on the wheel / keyboard / cursor auto-follow, with no draggable thumb.
 *  This is intentional -- drawing a thumb that cannot be dragged is worse than not drawing one (looks usable, does nothing when clicked).
 *
 * @par Basic usage
 * @code
 * Kit::TextArea memo("write something here...");
 * memo.SetGeometry(20, 20, 360, 160);
 * memo.SetOnTextChanged([](const std::string& s) { std::printf("now %zu bytes\n", s.size()); });
 * memo.SetOnReturn([](const std::string& s) { std::printf("Ctrl+Enter submits: %s\n", s.c_str()); });
 * win.AddWidget(memo);            // click -> take focus -> type directly / Enter for newline
 * @endcode
 *
 * @see ZeTextEditBase.h editing kernel (all editing behavior of this class lives there)
 * @see ZeInputBox.h     single-line version (same kernel, another layout)
 */

#pragma once
#include "ZeTextEditBase.h"

#include <string>
#include <string_view>

namespace Kit {

    /**
     * @brief Multi-line text box
     *
     * @details
     * Inheritance chain: `Widget -> BoxWidget -> TextWidget -> TextEditBase -> TextArea`.
     * The theme uses the theme's input section (`t.input.box` / `t.input.text`), the same section as the input box --
     * the two look consistent side by side. The text block is always top-left aligned (see the note on `ApplyTheme`).
     */
    class TextArea : public TextEditBase {
    public:
        /// @brief Constructs a multi-line text box (focusable: `SetFocusable(true)` is declared in TextEditBase)
        /// @param placeholder Placeholder text (shown when content is empty, uses the theme muted color)
        explicit TextArea(std::string placeholder = std::string());

        // -------------------------------------------------------------------
        //  Layout switches
        // -------------------------------------------------------------------

        /// @brief Soft wrap switch (default on)
        /// @param on true = overwide text wraps to the next line; false = no wrap, overwide text scrolls horizontally
        /// @note Disabling it suits cases like "showing long lines / logs" that should not be wrapped.
        void SetWordWrap(bool on);
        bool isWordWrap() const { return wordWrap_; }

        /// @brief How many spaces one Tab equals (default 4, minimum 1)
        /// @note Only affects the indent amount inserted by the Tab key and the expansion of pasted `'\t'` --
        ///       no literal `'\t'` is stored in the text (it has no visible width in the glyph library).
        void SetTabWidth(int spaces);
        int tabWidth() const { return tabWidth_; }

        /// @brief Whether the Tab key inserts an indent (default true)
        /// @param on false = Tab returns to the window for focus traversal (disable it when keyboard navigation is wanted in a form)
        void SetTabInsertsIndent(bool on) { tabInsertsIndent_ = on;  RequestWindowRedraw();}
        bool isTabInsertsIndent() const { return tabInsertsIndent_; }

        /// @brief How many lines the self-reported natural height counts (default 4 lines)
        /// @note Only affects the natural height when no explicit `SetSize` is given; an explicit size takes precedence.
        void SetVisibleLines(int lines);
        int visibleLines() const { return visibleLines_; }

        // -------------------------------------------------------------------
        //  Theme
        // -------------------------------------------------------------------

        /// @brief Injects the theme first, then pins the text block to top-left alignment
        /// @note The theme's input section is tuned for the single-line box (`vAlign = Middle` centers that line vertically).
        ///       Copying it to the multi-line box makes "content only two lines tall, yet the whole block floats in the middle of the box" -- so here it is pinned
        ///       to top alignment. This function runs again on theme change, so the constraint is not overwritten by the theme.
        void ApplyTheme(const Theme& t) override;

    protected:
        // -------------------------------------------------------------------
        //  Layout
        // -------------------------------------------------------------------

        /// @brief Multi-line layout: split by `'\n'` into paragraphs -> soft-wrap within each paragraph (see class comment)
        void buildLayout(FrameCollector& fc, EditLayout& out) const override;

        /// @brief Natural width = default width + padding + border (an explicit `SetSize` takes precedence)
        float hintWidth() const override;
        /// @brief Natural height = line height x visible lines + padding + border (same as above)
        float hintHeight() const override;

        /// @brief Enter = line break (`Ctrl+Enter` submits) -- this is the defining behavior of the multi-line box
        bool acceptsNewline() const override { return true; }
        /// @brief Whether Tab inserts an indent (default yes; when disabled, returns to the window for focus traversal)
        bool acceptsTab() const override { return tabInsertsIndent_; }
        /// @brief How many spaces one Tab equals
        int tabSpaces() const override { return tabWidth_; }

        /// @brief Up/Down: delegates to the editing kernel's "remembered-column up-down movement" (the actual conversion happens at paint time)
        bool keyVerticalMove(int delta, bool select) override {
            return beginVerticalMove(delta, select);
        }

    private:
        /// @brief Paragraph wrapping: splits the segment `[begin, end)` into lines appended to `out.lines`
        /// @param all   View of the whole text (used to slice substrings by absolute index)
        /// @param begin Paragraph start (absolute byte index)
        /// @param end   Paragraph end (absolute byte index, excluding the trailing `'\n'`)
        /// @param hard  Whether this segment ends with `'\n'` (hard line boundary)
        /// @param avail Available width (content area width; <= 0 degrades to "one line per paragraph")
        void wrapParagraph(FrameCollector& fc, std::string_view all,
            std::size_t begin, std::size_t end, bool hard,
            float avail, EditLayout& out) const;

        bool wordWrap_ = true;              /// < soft wrap switch
        bool tabInsertsIndent_ = true;      /// < whether Tab inserts an indent
        int  tabWidth_ = 4;                 /// < one Tab = how many spaces
        int  visibleLines_ = 4;             /// < how many lines the natural height counts
    };

} // namespace Kit
