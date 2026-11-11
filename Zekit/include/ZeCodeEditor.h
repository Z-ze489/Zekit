/**
 * @file ZeCodeEditor.h
 * @brief Code editor: multi-line text editing + pluggable syntax highlighting
 *
 * @details
 * `CodeEditor : TextEditBase` -- the third layout of the same editing core as `TextArea`:
 * typing, backspace, selection, mouse drag-select, Ctrl+A/C/X/V, cursor blink, IME input, soft wrap,
 * vertical scrolling are all provided by `TextEditBase`. This class adds exactly one thing: syntax highlighting.
 *
 * ---------------------------------------------------------------------------
 *  Design trade-off: why the "highlight rules" are an interface instead of all languages built in
 * ---------------------------------------------------------------------------
 * There are too many programming languages (C/C++, Python, Java, Go, Rust, JS...), each with its own syntax.
 * Three approaches, each with pros and cons:
 *
 *   - Built in all languages: the library grows without bound, and each language needs a maintained
 *     lexical rule set -- for a GUI widget library this is a mismatched responsibility (a widget library
 *     should not carry the burden of a language server).
 *   - Let developers hand-write highlighting: equivalent to providing no capability; every consumer
 *     would rewrite "split tokens -> color each segment", duplicated work that is easy to get wrong
 *     (UTF-8 boundaries / newlines / comment state).
 *   - Provide an interface + a reference implementation (this approach): the library encapsulates the
 *     mechanical work of "split tokens, color each segment" and leaves only "which tokens this line
 *     splits into and what color each is" to the developer; it also ships a built-in C/C++ highlighter
 *     as a template, and copying it for a new language usually takes < 100 lines.
 *
 * Conclusion: provide the `SyntaxHighlighter` abstract interface; language rules are customized by
 * the developer implementing this interface, rather than by passing a pile of rule data. Thus:
 *   - Supporting a new language = writing a `SyntaxHighlighter` subclass (no need to touch the widget,
 *     no need to modify the library);
 *   - The library itself ships only a lightweight C/C++ reference implementation and does not carry
 *     the burden of "all languages in the world".
 *
 * ---------------------------------------------------------------------------
 *  Highlight hook: `TextEditBase::paintLineToken`
 * ---------------------------------------------------------------------------
 * When the paint skeleton of `TextEditBase` (`paintContent`) draws text line by line, it calls the
 * virtual function `paintLineToken(body, lx, ly, tm, cs)` (default = single color for the whole line).
 * This class overrides it, hands the whole line `body` to the highlighter to split into tokens, then
 * paints each segment with its own color via `paintTextAt`.
 * The editing core, cursor, selection, and IME are not changed by a single frame -- highlighting happens
 * only at the coloring layer.
 *
 * ---------------------------------------------------------------------------
 *  Value semantics discipline
 * ---------------------------------------------------------------------------
 * Like all widgets, this class declares no destructor / copy / assignment (otherwise `AddWidget` would
 * not compile). The highlighter is held via `std::shared_ptr` -- it is a strategy object that can be
 * shared by multiple editors, not an exclusive member of this widget, so shared ownership is
 * appropriate (and it avoids dangling raw pointers after the widget is moved).
 *
 * @par Basic usage
 * @code
 * Kit::CodeEditor editor;
 * editor.SetText("#include <cstdio>\nint main() { return 0; }");
 * editor.SetHighlighter(std::make_shared<Kit::CppHighlighter>());  // built-in C/C++
 * win.AddWidget(editor);
 * @endcode
 *
 * @par Supporting a new language (Python as an example)
 * @code
 * class PyHighlighter : public Kit::SyntaxHighlighter {
 *     void highlight(std::string_view line, std::vector<Kit::SyntaxToken>& out) const override {
 *         // scan line, split keywords / strings / comments / numbers into tokens and fill out
 *         // see the CppHighlighter implementation (in ZeCodeEditor.cpp)
 *     }
 * };
 * @endcode
 *
 * @see ZeTextEditBase.h editing core (all editing behavior of this class lives there)
 * @see ZeTextArea.h     multi-line text box (another layout of the same core, no highlighting)
 */

#pragma once
#include "ZeTextEditBase.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <string_view>
#include <vector>

namespace Kit {

    /**
     * @brief The category of a syntax token
     *
     * @details
     * The category is a semantic label (not the color itself) -- the color is decided by the highlighter /
     * theme, so the same highlighter switched to a dark theme changes color automatically instead of
     * hard-coding colors into tokens. `CodeEditor` looks up colors based on this.
     */
    enum class SyntaxTokenKind {
        Default,     /// < plain text
        Keyword,     /// < keyword (if / for / class / int ...)
        String,      /// < string literal
        Number,      /// < numeric literal
        Comment,     /// < comment
        Preprocessor,/// < preprocessor directive (#include / #define ...)
        Identifier,  /// < identifier / function name / type name (optional, the highlighter may subdivide)
        Count        /// < sentinel (always last)
    };

    /**
     * @brief A colored range within one line
     *
     * @details
     * `begin` / `end` are UTF-8 byte indices (half-open range `[begin, end)`) and must fall on character
     * boundaries. When scanning by character, the highlighter can use `Utf8Boundaries` to get the boundaries.
     */
    struct SyntaxToken {
        std::size_t     begin = 0;                 /// < start byte index
        std::size_t     end = 0;                   /// < end byte index (exclusive)
        SyntaxTokenKind kind = SyntaxTokenKind::Default;  /// < category
    };

    /**
     * @brief Syntax highlighter interface (called once per line, no cross-line state)
     *
     * @details
     * This is the only entry point for letting developers customize language rules. The library
     * encapsulates "color each segment" in `CodeEditor::paintLineToken`; the highlighter only needs to
     * answer: "which tokens does this line split into".
     *
     * Constraints:
     *   - Tokens in `out` must be in ascending `begin` order, non-overlapping, and completely cover
     *     `[0, line.size())` (fill unrecognized parts with `SyntaxTokenKind::Default`);
     *   - Each token's `begin`/`end` must fall on UTF-8 character boundaries;
     *   - This interface has no cross-line state (coloring of block comments "slash star star slash"
     *     spanning lines is left to the caller -- see the comments in `CodeEditor`; for simple cases
     *     per-line independence is enough).
     *
     * @note Designed per line rather than per full text so it naturally aligns with the soft-wrap
     *       line breaking of `TextEditBase`: after wrapping, each line is an independent unit, and
     *       per-line highlighting is stateless and incremental.
     */
    class SyntaxHighlighter {
    public:
        virtual ~SyntaxHighlighter() = default;

        /// @brief Highlights one line of text
        /// @param line this line's text (UTF-8, without the trailing `'\n'`)
        /// @param out  output token list (cleared before the call)
        virtual void highlight(std::string_view line,
            std::vector<SyntaxToken>& out) const = 0;
    };

    /**
     * @brief Built-in C/C++ highlighter (reference implementation)
     *
     * @details
     * Covers: keywords / strings (including `"` and `'` and escapes) / line comments "double slash" /
     * numbers / preprocessor directives `#...`. Block comments "slash star" are handled per line
     * independently (if a line starts with slash star the whole line is treated as a comment, and if it
     * ends with star slash it stops at the end), with no cross-line state tracking -- sufficient for the
     * vast majority of code, and keeps the interface simple as stateless per-line.
     *
     * This is exactly the reference implementation meant as a template for developers: when adding a
     * language, copy it and replace the keyword table and lexical scanning rules.
     */
    class CppHighlighter : public SyntaxHighlighter {
    public:
        void highlight(std::string_view line,
            std::vector<SyntaxToken>& out) const override;
    };

    /**
     * @brief Code editor widget
     *
     * @details
     * Inheritance chain: `Widget -> BoxWidget -> TextWidget -> TextEditBase -> CodeEditor`.
     * Behavior = `TextArea` (multi-line editing + soft wrap + vertical scrolling + Tab indent) +
     * syntax highlighting. Box / font come from the theme's `code` section (derived from the `input`
     * section, see `Themes::Derive`) -- the code editor shares its origin with the input box, so the look
     * is naturally consistent.
     *
     * The semantic colors of the seven token categories are also theme data (`Theme::code.tokenXxx`),
     * and `Themes::Derive` automatically derives brightness from whether the code area background is
     * light. The widget also has its own light fallback, but that is only the initial value before a
     * theme is injected -- do not expect it to be readable under a dark theme.
     *
     * @par Why token colors must be re-injected with the theme (a pitfall hit before)
     * Before the fix, only `Default` / `Comment` changed with the theme; keywords / strings / identifiers
     * were all fixed values from construction time. After switching to dark, identifiers were still
     * `#0F172A` (near black), and identifiers make up the vast majority of body text -- the symptom is
     * "under a dark theme a whole block of text is invisible, but some characters are fine".
     */
    class CodeEditor : public TextEditBase {
    public:
        /// @brief Constructs (focusable; no highlighter attached by default = plain text)
        /// @param placeholder placeholder text (shown when content is empty)
        explicit CodeEditor(std::string placeholder = std::string());

        // -------------------------------------------------------------------
        //  Syntax highlighting
        // -------------------------------------------------------------------

        /// @brief Sets the syntax highlighter (`nullptr` = turn off highlighting, back to single color)
        /// @note Shared ownership: the same highlighter can be shared by multiple editors, avoiding duplicate allocation.
        void SetHighlighter(std::shared_ptr<SyntaxHighlighter> h);
        std::shared_ptr<SyntaxHighlighter> Highlighter() const { return highlighter_; }

        /// @brief Color of each token category (after the highlighter splits tokens, this decides what color to use)
        ///
        /// @details Initial values take the built-in colors of the light theme; on attach / theme change they are
        ///          re-injected from `Theme::code.tokenXxx`.
        ///
        /// @note A category that has been overridden is no longer changed by the theme (one "user has taken
        ///       over" flag per category). Otherwise, after the developer calls `SetTokenColor` and then
        ///       switches theme, the color would be silently overwritten -- the same discipline as in
        ///       `AddWidget`, which only touches widgets whose style has not been customized.
        void SetTokenColor(SyntaxTokenKind kind, const Color4f& c);

        // -------------------------------------------------------------------
        //  Layout switches (aligned with TextArea: soft wrap / Tab indent / visible lines)
        // -------------------------------------------------------------------

        /// @brief Soft wrap switch (on by default)
        void SetWordWrap(bool on);
        bool isWordWrap() const { return wordWrap_; }

        /// @brief How many spaces one Tab equals (default 4)
        void SetTabWidth(int spaces);
        int tabWidth() const { return tabWidth_; }

        /// @brief How many lines the natural height is based on (default 8 lines, more than the text box)
        /// @note After `SetAutoHeight(true)` is enabled, this only serves as a fallback when the parent container gives no width.
        void SetVisibleLines(int lines);

        /**
         * @brief Height adapts to content: grows according to the actual number of lines after wrapping (off by default)
         *
         * @details
         * When off, height = `SetVisibleLines(line count)` (fixed line count, overflow scrolls inside).
         * When on, the parent container first gives it a width, then asks "how tall do you want at this
         * width" -- so lines produced by wrapping long lines also count toward the height, and the content
         * is always fully displayed, with scrolling left to the outer layer (`ScrollView` / `VBox`).
         *
         * @par When to enable
         * Scenarios that fully display a piece of code to a person -- code blocks in documentation, log
         * viewing, diff comparison. In these cases inner scrolling is the most awkward thing (two layers of
         * scrolling, and the scrollbar is invisible). Conversely, "the editor occupies a fixed area of the
         * screen and lets code scroll inside" is another product form, so keep it off.
         *
         * @note The prerequisite is `SetWordWrap(true)` (on by default). When soft wrap is off this switch
         *       has no effect -- talking about "growing by width" with wrapping off is self-contradictory.
         * @note The implementation relies on the `Widget::hintHeightFor` / `heightDependsOnWidth` protocol
         *       (the parent container's `LayoutBase::measureWidthDependentHeights` will ask).
         */
        void SetAutoHeight(bool on);
        bool isAutoHeight() const { return autoHeight_; }

        /// @brief Theme injection: sets the background to the code area colors + re-injects each token color
        void ApplyTheme(const Theme& t) override;

    protected:
        // -------------------------------------------------------------------
        //  Layout (same as TextArea: segmentation + soft wrap)
        // -------------------------------------------------------------------
        void buildLayout(FrameCollector& fc, EditLayout& out) const override;
        float hintWidth() const override;
        float hintHeight() const override;

        /// @brief Real height at a given width (effective when `SetAutoHeight(true)`, see that function's description)
        float hintHeightFor(float availWidth) const override;
        /// @brief Whether height depends on width (only when auto height is on and soft wrap is on)
        bool heightDependsOnWidth() const override {
            return autoHeight_ && wordWrap_;
        }

        bool acceptsNewline() const override { return true; }
        bool acceptsTab() const override { return true; }
        int tabSpaces() const override { return tabWidth_; }
        bool keyVerticalMove(int delta, bool select) override {
            return beginVerticalMove(delta, select);
        }

        /// @brief Theme box takes the codeEditor section
        const BoxStyle& themeBox(const Theme& t) const override;
        /// @brief Theme text takes the codeEditor section
        const TextStyle& themeText(const Theme& t) const override;

        /// @brief Highlight coloring: hands the whole line to the highlighter to split into tokens, colors each segment
        void paintLineToken(FrameCollector& fc, std::string_view body,
            float lx, float ly, const TextMetrics& tm, const ColorSet& cs) const override;

    private:
        /**
         * @brief Text width measurement function (`std::string_view` -> pixel width)
         *
         * @details
         * The same wrapping algorithm must serve two callers:
         *  - Paint time -- width measurement relies on `FrameCollector` (glyph textures are already in it);
         *  - Height measurement time (`hintHeightFor`) -- there is no `FrameCollector`, so it can only go
         *    through `window()->MeasureText`.
         * Rather than copying the algorithm twice (which will drift sooner or later), parameterize it as
         * "just give me a width measurement function".
         */
        using WidthFn = std::function<float(std::string_view)>;

        /// @brief Wraps the whole text: splits by `'\n'`, hands each segment to `wrapParagraph`
        void wrapAll(const WidthFn& measure, float availW, EditLayout& out) const;

        /// @brief Text content hash (FNV-1a) -- used as the key for the `hintHeightFor` memo
        std::size_t textHash() const;

        /// @brief Paragraph wrapping (same algorithm as TextArea::wrapParagraph, width measurement injected)
        void wrapParagraph(const WidthFn& measure, std::string_view all,
            std::size_t begin, std::size_t end, bool hard,
            float avail, EditLayout& out) const;

        std::shared_ptr<SyntaxHighlighter> highlighter_;   /// < syntax highlighter (nullable)
        Color4f tokenColor_[static_cast<int>(SyntaxTokenKind::Count)]; /// < color of each category
        /// Whether each category has been taken over by `SetTokenColor` (true = not overwritten on theme change)
        bool    tokenCustom_[static_cast<int>(SyntaxTokenKind::Count)] = {};
        bool    wordWrap_ = true;              /// < soft wrap switch
        bool    autoHeight_ = false;           /// < height adapts to wrapping result (fixed line count by default)

        // ---- `hintHeightFor` memo ----
        //  Why needed: `ScrollView` scrolling only moves position and does not change size, while the parent
        //  container counts "position changed" as dirty (geoChanged in `LayoutBase::EnsureLayout`) => every
        //  scrolled frame re-asks the height of every code block. Without a memo, this means measuring the
        //  text of the whole code line by line every frame. Key = width + text hash + font size + line
        //  height; recompute if any changes.
        mutable float       memoW_ = -1.0f;    /// < last queried available width (-1 = invalid)
        mutable std::size_t memoHash_ = 0;     /// < last text hash
        mutable unsigned    memoFontPx_ = 0;   /// < last font size
        mutable float       memoLineH_ = -1.0f;/// < last `TextStyle::lineHeight`
        mutable float       memoH_ = 0.0f;     /// < last computed height
        int     tabWidth_ = 4;                 /// < how many spaces one Tab equals
        int     visibleLines_ = 8;             /// < how many lines the natural height is based on
    };

} // namespace Kit
