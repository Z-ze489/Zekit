// ZeTextEditBase.h: shared editing core for text widgets -- text buffer /
// caret / selection / clipboard / IME.
//
// Inheritance: Widget -> BoxWidget -> TextWidget -> TextEditBase -> concrete
// widgets. InputBox (single line) and TextArea (multi line) share identical
// editing behavior (typing, backspace, selection, mouse drag-select,
// Ctrl+A/C/X/V, caret blink, CJK IME); only layout differs. This layer owns
// all editing; derived classes only answer "which line and position each
// character goes to".
//
// Layer split:
// - This class (editing + common paint skeleton): text buffer with caret and
//   selection anchor (UTF-8 byte indices), insert/erase/delete + callback
//   dispatch, selection and clipboard ops (SelectAll / ClearSelection / Copy /
//   Cut / Paste), scrolling with caret-follow clamping, point -> caret mapping,
//   keyboard navigation dispatch, caret blink and continued frames, IMM32
//   composition drawing and candidate window positioning, and the paint
//   skeleton (layout -> pending coords -> scroll clamp -> selection -> lines
//   -> composition -> caret).
// - Derived classes (layout): buildLayout() is the only layout virtual;
//   acceptsNewline() decides whether Enter is a newline or a commit;
//   acceptsTab() / tabSpaces() decide whether Tab inserts an indent or returns
//   to the window for focus traversal; keyVerticalMove() supports up/down
//   (multi line only); hintWidth() / hintHeight() are mandatory for leaf
//   widgets, otherwise they silently occupy no space in any layout.
//
// Two recurring techniques (inherited from InputBox):
// 1. Press/drag only record coordinates; conversion is deferred to paint time
//    (FrameCollector, the only glyph measurer, is only available during
//    painting). Delay <= 1 frame, invisible. keyVerticalMove's target column
//    follows the same path (pendingVertDelta_).
// 2. One geometry source: caret drawing and point-to-caret mapping share the
//    same functions (linePrefixWidth / byteAtXInLine); computing them twice
//    inevitably drifts into "looks 60%, clicks at 70%".
//
// Coordinate semantics: widget geometry uses absolute client coordinates
// (ZeHitTest.h: no offsets between parent and child), same as MouseEvent::x/y.
// Event x/y are used directly; subtracting the content-area origin gives
// content-local coordinates. scrollX_/scrollY_ zero = text block top-left
// (content local); EditLayout::lines[i].top is content local too (first line
// top = 0).
//
// Value semantics discipline: this class declares NO destructor / copy /
// assignment -- declaring any of them removes the implicit move and breaks
// AddWidget's make_unique<T>(std::move(x)). Same for derived classes.
//
// Theme: defaults to the theme's input section (t.input.box / t.input.text),
// shared by single and multi line so the outer look stays consistent (see the
// InputStyle comment in ZeTheme.h).
// See also ZeInputBox.h, ZeTextArea.h, ZeTextWidget.h.

#pragma once
#include "ZeSelfAction.h"    // Event callback protocol: self is injected by the library at dispatch
#include "ZeTextWidget.h"

#include <chrono>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace Kit {

    // One line of the layout (soft-wrapped lines count too).
    // begin / end are UTF-8 byte indices, half-open [begin, end), always on
    // character boundaries -- wrapping must never cut a multi-byte character in
    // half. The line-ending '\n' belongs to no line (it sits between end and the
    // next line's begin); hard marks whether this line ends with one.
    // Empty text still has one line (begin == end == 0), otherwise the caret
    // has no line to dock to.
    struct EditLine {
        std::size_t begin = 0;      // Byte index of the line start
        std::size_t end = 0;        // Byte index of the line end (excluding '\n')
        bool        hard = false;   // Ends with '\n' (or end of text) -- hard boundary
        float       width = 0.0f;   // Rendered width in pixels
        float       top = 0.0f;     // Line top (content-local coordinates, first line = 0)
    };

    // One frame's layout result (the output of buildLayout).
    // Derived buildLayout() fills lines; the remaining fields (top / totalH /
    // maxWidth / caretLine) are computed by the base class, so line-top
    // accumulation has exactly one implementation and derived classes need not
    // (and must not) maintain their own.
    struct EditLayout {
        std::vector<EditLine> lines;   // All lines (at least one)
        float lineHeight = 0.0f;       // Line height (= ascent + descent, uniform per line)
        float maxWidth = 0.0f;         // Longest line width (upper bound for horizontal scrolling)
        float totalH = 0.0f;           // Total height of all lines
        float areaX = 0.0f;            // Content area top-left (absolute client coordinates)
        float areaY = 0.0f;
        float areaW = 0.0f;            // Content area size
        float areaH = 0.0f;
        std::size_t caretLine = 0;     // Line the caret is on (computed by the base from caret_)
        bool valid = false;            // Whether this layout is valid (rebuilt by the base when invalid)
    };

    // Shared editing core for editable text widgets (not instantiated directly:
    // buildLayout is pure virtual). Public surface = everything "editing a text
    // box" needs (content / read-only / placeholder / clipboard / keyboard
    // callbacks); protected surface = layout primitives and the questions
    // derived classes must answer. Derived classes override buildLayout to
    // wrap Text() into lines (see ZeInputBox.cpp for the single-line version)
    // and provide hintWidth / hintHeight.
    class TextEditBase : public TextWidget {
    public:
        // Constructor (focusable: declares SetFocusable(true)).
        // placeholder: shown when Text() is empty, in the theme's muted color.
        explicit TextEditBase(std::string placeholder = std::string());

        // -------------------------------------------------------------------
        //  Content
        // -------------------------------------------------------------------

        // Sets the text (caret moves to the end, selection collapses; fires
        // OnTextChanged). Shadows TextWidget::SetText (the base one is not
        // virtual): for input widgets "set text, then follow the caret to the
        // end" is the semantic, not just changing data.
        void SetText(const std::string& text);
        using TextWidget::Text;              // Text() comes straight from the base

        // Sets the placeholder text (shown when content is empty)
        void SetPlaceholder(const std::string& text) { placeholder_ = text;  RequestWindowRedraw();}
        const std::string& Placeholder() const { return placeholder_; }

        // Read-only: no input or deletion, but the caret still moves and Copy
        // works. Enforced once at the data layer (insertText / eraseChars /
        // deleteSelection each check readOnly_), so every entry point (including
        // the IME GCS_RESULTSTR commit path) is blocked -- adding per-entry
        // checks is exactly the "miss one and it leaks" pattern. Copy() /
        // SelectAll() still work (read-only must stay selectable and copyable).
        // Switching to read-only cancels any in-flight composition so no stale
        // preedit string sits in the widget.
        void SetReadOnly(bool ro) {
            if (readOnly_ == ro) return;
            readOnly_ = ro;
            if (ro) cancelComposition();
            RequestWindowRedraw();
        }
        bool isReadOnly() const { return readOnly_; }

        // Default content length limit in CHARACTERS (not bytes): 50000. Why a
        // limit (not just performance): typing/pasting is external input, so
        // without one the user decides how much work this frame does -- pasting
        // hundreds of thousands of characters freezes the UI; and UTF-8 byte
        // counts differ from character counts (3 bytes per CJK char), so
        // characters are the user-perceived length. 50k is deliberately generous:
        // code files, logs and long documents fit; pasting a whole book is the
        // pathological case.
        static constexpr std::size_t kDefaultMaxLength = 50000;

        // Sets the content length limit in characters (UTF-8 code points).
        // 0 = unlimited (when you accept the "paste a whole book" consequences).
        // The gate sits at the data layer (insertText / SetText), the same layer
        // as readOnly_: keyboard input, paste, IME commit and completions all
        // converge there, so no entry point can bypass it. Truncation cuts on
        // complete characters only -- never splitting a UTF-8 multi-byte
        // sequence (which would garble the display and never delete cleanly).
        // Content already over the limit is NOT truncated immediately; this only
        // governs what is accepted from now on (typing one more char stops at
        // the new limit -- everyone's expectation of a length cap).
        void SetMaxLength(std::size_t n) { maxLength_ = n;  RequestWindowRedraw();}
        std::size_t MaxLength() const { return maxLength_; }

        // Current character count (UTF-8 code points, same measure as MaxLength)
        std::size_t TextLength() const;

        // Whether the content is empty (equivalent to Text().empty(), nicer to read)
        bool IsEmpty() const { return text_.empty(); }

        // -------------------------------------------------------------------
        //  Selection / clipboard
        // -------------------------------------------------------------------
        //  These public methods and the Ctrl+A/C/X/V shortcuts share ONE
        //  implementation (the shortcuts just forward): two implementations
        //  drifting apart is a classic editor-widget bug source. The clipboard
        //  goes through the platform layer (Windows CF_UNICODETEXT / X11
        //  Selection protocol), usable across processes.

        // Selects all (Ctrl+A)
        void SelectAll();

        // Clears the selection (no-op without one)
        void ClearSelection();

        // Whether a selection exists (anchor != caret)
        bool HasSelection() const { return selAnchor_ != caret_; }

        // Vertical scroll offset of the inner scroll (content-local, 0 = top).
        // Non-zero only when content is taller than the content area (changed by
        // OnMouseWheel and clamped once more during paint to follow the caret).
        float scrollY() const { return scrollY_; }

        // Horizontal scroll offset (content-local, 0 = leftmost)
        float scrollX() const { return scrollX_; }

        // Selected text (empty string without a selection)
        std::string SelectedText() const;

        // Copies: selection if any, otherwise the whole text (Ctrl+C). Returns
        // true if something was actually written to the clipboard; false when
        // there is nothing to copy (nothing written). Allowed while read-only
        // (a read operation). Empty content does not touch the clipboard -- an
        // accidental Ctrl+C must not wipe it.
        bool Copy() const;

        // Cuts: copy selection + delete (Ctrl+X). Returns true if executed;
        // false = read-only / no selection (no action in either case). Without a
        // selection it does nothing rather than "cut the whole text": the latter
        // wipes the entire box on a single accidental press -- a data-loss class
        // mistake, and the Win32 / X11 standard behavior is also to do nothing.
        bool Cut();

        // Pastes clipboard text at the caret (replacing any selection) (Ctrl+V).
        // Returns true if pasted; false = read-only / clipboard has no text.
        // No clipboard text = do nothing -- in particular never delete the
        // selection.
        bool Paste();

        // -------------------------------------------------------------------
        //  Appearance (defaults from the theme: caret = primary, placeholder = muted)
        // -------------------------------------------------------------------

        void SetCaretColor(const Color4f& c) { caretColor_ = c; caretColorSet_ = true;  RequestWindowRedraw();}
        void SetPlaceholderColor(const Color4f& c) { placeholderColor_ = c; placeholderColorSet_ = true;  RequestWindowRedraw();}

        // -------------------------------------------------------------------
        //  Callbacks (protocol types in ZeSelfAction.h, single library-wide copy)
        // -------------------------------------------------------------------
        //  Two signatures coexist without interfering: the plain one is simplest;
        //  the self variant is for callbacks that also need the sender
        //  (Kit::As<T>(self) to recover the concrete interface). self is injected
        //  by the library at dispatch and always points at the tree entity --
        //  capturing widget pointers is what dangles.

        // The commit key was pressed (single-line Enter / multi-line Ctrl+Enter)
        void SetOnReturn(std::function<void(const std::string&)> cb) { onReturn_ = std::move(cb); }
        // Text changed (typing / deleting / pasting / SetText all count)
        void SetOnTextChanged(std::function<void(const std::string&)> cb) { onTextChanged_ = std::move(cb); }

        // Commit (sender variant)
        void SetOnReturn(SelfAction1<const std::string&> cb) { selfOnReturn_ = std::move(cb); }
        // Text changed (sender variant). Both variants may be registered at once;
        // dispatch runs the plain one first, then the sender one.
        void SetOnTextChanged(SelfAction1<const std::string&> cb) { selfOnTextChanged_ = std::move(cb); }

    protected:
        // ===================================================================
        //  Derived classes must answer: layout
        // ===================================================================

        // Wraps Text() into lines by content-area width and fills out.lines.
        // fc: the measurer (only available during painting -- layout therefore
        // happens at paint time). out: areaX/Y/W/H and lineHeight are already
        // filled; only lines needs filling. Obligations:
        // 1. begin/end must fall on UTF-8 character boundaries;
        // 2. produce at least one line -- empty text still gets one
        //    (begin == end == 0), otherwise the caret has no line to dock to
        //    and caretLine() goes out of range;
        // 3. fill width with the line's rendered width (the base uses it for the
        //    horizontal scroll bound). top / totalH / maxWidth need no attention --
        //    the base computes them.
        virtual void buildLayout(FrameCollector& fc, EditLayout& out) const = 0;

        // Whether Enter means newline (true) or commit (false). Single line:
        // false (Enter commits; pasted newlines are folded into spaces).
        // Multi line: true (Enter inserts a newline, Ctrl+Enter commits).
        virtual bool acceptsNewline() const { return false; }

        // Whether Tab inserts an indent (true) or goes back to the window for
        // focus traversal (false)
        virtual bool acceptsTab() const { return false; }

        // How many spaces one Tab equals (effective when acceptsTab() is true)
        virtual int tabSpaces() const { return 4; }

        // Up / down: moves the caret across lines. delta: +1 next / -1 previous;
        // select: Shift held (keep the selection anchor). Returns true if
        // handled; false = this widget has no vertical movement (passed back up).
        // Default false -- single-line boxes have no lines and must not swallow
        // up/down. Multi line implementations (see ZeTextArea.cpp) record the
        // target column and defer the real conversion to paint time (where the
        // measurer lives).
        virtual bool keyVerticalMove(int delta, bool select) { (void)delta; (void)select; return false; }

        // Declares natural width (pure virtual: leaf widgets must provide).
        // An unoverridden leaf silently occupies no space in any layout (zero
        // errors), so this is pure virtual to turn "forgot to write it" from a
        // silent runtime issue into a compile error.
        float hintWidth() const override = 0;
        // Declares natural height (pure virtual) -- same reason
        float hintHeight() const override = 0;

        // Natural height from a visible-line count (shared by TextArea /
        // CodeEditor). visibleLines: lines to show (>= 1). Returns
        // line height x lines + vertical padding + border; <= 0 = font not ready
        // (callers should fall back to Widget::hintHeight()). The line-height
        // rule must match the layout: TextStyle::lineHeight when > 0, otherwise
        // the font's ascent + descent (measured on "Ag"). ZeTextArea.cpp and
        // ZeCodeEditor.cpp used to carry two character-identical copies of this
        // -- now there is only this one.
        float linesHintHeight(int visibleLines) const;

        // ===================================================================
        //  Primitives for derived classes
        // ===================================================================

        // The latest layout result (computed at paint time, read at keyboard time).
        // Note it is the PREVIOUS frame's: right after an edit the line bounds
        // may lag one frame. Home / End style "line start/end" decisions tolerate
        // this -- the next frame aligns, invisible to the eye.
        const EditLayout& layout() const { return layout_; }

        // The caret's line index into layout().lines (always valid)
        std::size_t caretLineIndex() const;

        // Marks the layout invalid (text / content size / line height changed;
        // rebuilt next frame). The base calls this automatically after text
        // changes; derived classes changing wrap-affecting parameters (e.g.
        // SetWordWrap on multi line) must call it too.
        void markLayoutDirty() { layoutDirty_ = true; }

        // ===================================================================
        //  Editing internals (for derived classes that trigger edits)
        // ===================================================================

        // Inserts UTF-8 at the caret (replacing any selection first; fires
        // OnTextChanged). Control characters fold per acceptsNewline(): single
        // line turns newline/tab into spaces; multi line normalizes \r\n / \r
        // to \n and expands \t to tabSpaces() spaces.
        void insertText(const std::string& utf8);
        // Deletes count characters before/after the caret (UTF-8 safe)
        void eraseChars(bool before, std::size_t count);
        // Deletes the current selection (no-op without one)
        void deleteSelection();
        // Fires both OnTextChanged callbacks (all text changes funnel here)
        void changed();
        // Fires both OnReturn callbacks (Enter / Ctrl+Enter land here)
        void Submit();

        // Moves the caret to `byte` (clamped to a legal character boundary) and
        // collapses the selection when not selecting
        void setCaret(std::size_t byte, bool select);

        // Starts a vertical move (up/down). delta: +1 next / -1 previous;
        // select: Shift held (keep the selection anchor). Always returns true
        // (this only records; the real conversion happens at paint time).
        // Called from keyVerticalMove implementations -- multi line boxes in
        // ZeTextArea.cpp just forward. Repeated presses keep the original column.
        bool beginVerticalMove(int delta, bool select);

        // ===================================================================
        //  Theme / painting / events (derived classes usually need no overrides)
        // ===================================================================

        // Default box style from the theme's input section (focused border = primary)
        const BoxStyle& themeBox(const Theme& t) const override;
        // Default text style from the theme's input section
        const TextStyle& themeText(const Theme& t) const override;

        // Common paint skeleton (see the class comment's layer split)
        void paintContent(FrameCollector& fc) const override;

        bool OnMousePress(MouseEvent& e) override;
        // Drag-select: extends the selection while moving after a press (no-op without one)
        bool OnMouseMove(MouseEvent& e) override;
        // Release ends drag-select
        bool OnMouseRelease(MouseEvent& e) override;
        // Wheel: consumed only when actually scrolled; at either end it bubbles
        // up (nestable inside ScrollView). Three rules, same source as
        // ScrollView::OnMouseWheel:
        // - content fits -> return false (bubble to the outer view);
        // - already at the boundary in that direction -> also return false. This
        //   one matters: unconditionally returning true when scrollable meant the
        //   inner widget kept eating the wheel after hitting bottom -- pages with
        //   a code block / long text could not scroll at all;
        // - vertical first; horizontal only with Shift held (silently turning the
        //   vertical wheel horizontal makes users think the page ignores them).
        bool OnMouseWheel(MouseEvent& e) override;
        bool OnKeyPress(KeyEvent& e) override;
        bool OnCharInput(KeyEvent& e) override;
        void OnFocusIn() override;
        void OnFocusOut() override;

        // Editable widgets should pop the on-screen keyboard on focus (mobile);
        // no-op on desktop
        bool WantsSoftKeyboard() const override { return true; }

        // ---- IME (IMM32): overrides Widget's IME hooks, draws the composition
        // string itself + positions the candidate window ----
        bool OnImeStartComposition() override;
        bool OnImeComposition(uint32_t flags, uintptr_t lParam) override;
        bool OnImeEndComposition() override;
        bool OnImeChar(wchar_t ch) override;

        // ===================================================================
        //  Paint-time caches for derived classes (geometry needed at keyboard time)
        // ===================================================================

        // Caret x in widget coordinates (computed during the previous paint).
        // For "vertical moves remember the column": keyboard time has no
        // measurer, so only this cache is readable.
        float caretPixelX() const { return caretPixelX_; }

        // -------------------------------------------------------------------
        //  Geometry (needs the measurer -- callable only during painting)
        // -------------------------------------------------------------------

        // Prefix width within a line: the pixel width of [line.begin, byteIdx).
        // byteIdx: absolute byte index, guaranteed by the caller to lie within
        // [line.begin, line.end]. Measures incrementally per character (not the
        // whole prefix each time): measuring single characters beats long
        // prefixes, and total measurements stay on the order of the character count.
        float linePrefixWidth(FrameCollector& fc, const EditLine& line, std::size_t byteIdx) const;

        // Line-local x -> byte index (prefix width is monotonic in the index -> binary search)
        // x: pixel offset relative to the line start (scroll and alignment removed)
        // Returns an absolute byte index (on a character boundary)
        std::size_t byteAtXInLine(FrameCollector& fc, const EditLine& line, float x) const;

        // Content-local coordinates -> byte index (line by y first, then binary search by x)
        // localX/localY: content-local coordinates (scroll offsets already added back)
        std::size_t pointToCaret(FrameCollector& fc, float localX, float localY) const;

        // -------------------------------------------------------------------
        //  Syntax highlighting hook (default: whole line in one color;
        //  CodeEditor overrides for per-token coloring)
        // -------------------------------------------------------------------

        // Paints the colored segments of one line (paintLines calls it per line).
        // Default = the whole line painted in cs.text, matching TextEditBase
        // behavior, so InputBox / TextArea change nothing. Syntax-highlighting
        // widgets (CodeEditor) override it, split body into tokens and paint
        // each with paintTextAt in its own color -- syntax coloring without
        // rewriting the editing core.
        // fc: measurer / painter (needed to measure and draw tokens).
        // body: this line's text (UTF-8, whole line, uncolored).
        // lx: line left edge x (alignment and scroll included, absolute client
        // coordinates). ly: line top y (same; baseline = ly + tm.ascent).
        // cs: opacity-folded color set (the fallback when not highlighting).
        virtual void paintLineToken(FrameCollector& fc, std::string_view body,
            float lx, float ly, const TextMetrics& tm, const ColorSet& cs) const;

    private:
        // ---- Edit actions (shared by shortcuts and public methods) ----
        // Whether a selection exists (anchor != caret)
        bool hasSelection() const { return selAnchor_ != caret_; }
        // Collapses the selection (anchor follows the caret)
        void clearSelection() { selAnchor_ = caret_; }
        // Resets the blink phase anchor (just edited -> caret immediately visible)
        void resetBlink() { caretAnchor_ = std::chrono::steady_clock::now(); }
        // Cancels a pending vertical move (any non-vertical operation should clear it)
        void cancelVerticalPending();
        // Requests "keep the caret in view" (scroll clamped once on next paint)
        void requestCaretVisible() { ensureCaretVisible_ = true; }
        // Inserts an indent (Tab goes here when acceptsTab() is true)
        void insertTabIndent();
        // Outdents (Shift+Tab: removes up to one Tab width of spaces before the caret on this line)
        void outdentLine();

        // ---- Paint-time sub-steps (split out of paintContent) ----
        // Rebuilds the layout (only when text / content size / line height changed)
        void ensureLayout(FrameCollector& fc, float cx, float cy, float cw, float ch) const;
        // Applies paint-time pending items: press/drag coordinates, vertical moves
        void applyPendingInputs(FrameCollector& fc) const;
        // Applies a pending vertical move (converts the target column x to a byte index)
        void applyPendingVertical(FrameCollector& fc) const;
        // Clamps scroll offsets so the caret stays in view
        void clampScroll(FrameCollector& fc) const;
        // Paints the selection highlight (per-line rects; translucent blue)
        void paintSelection(FrameCollector& fc, float blockOffsetX, float blockOffsetY) const;
        // Paints each line's text (or the placeholder)
        void paintLines(FrameCollector& fc, float blockOffsetX, float blockOffsetY) const;
        // Paints the IME composition string (drawn at the caret + underline)
        void paintComposition(FrameCollector& fc, float blockOffsetX, float blockOffsetY) const;
        // Paints the caret (blinking) + schedules the next flip
        void paintCaret(FrameCollector& fc, float blockOffsetX, float blockOffsetY) const;
        // Horizontal/vertical alignment offsets of the text block (converts the
        // content area's leftover space into a start point)
        void blockOffsets(float& offsetX, float& offsetY) const;

        // ---- IME internals ----
        // Activates the IME context (once on focus)
        void activateIme();
        // Cancels an unfinished composition (on blur / deactivate / click)
        void cancelComposition();
        // Moves the IME candidate / composition window to the caret (client coordinates)
        void updateImeCaretRect() const;
        // Consumes the duplicated character re-sent after an IME commit (returns
        // true = swallowed, do not insert again). ch is the full code point
        // (char32_t): the two re-send channels pass different types (WM_IME_CHAR
        // gives wchar_t, WM_CHAR gives char32_t), so comparing code points avoids
        // surrogate-pair mismatches.
        bool consumePendingImeChar(char32_t ch);

        // -------------------------------------------------------------------
        //  State
        // -------------------------------------------------------------------
        std::string placeholder_;
        Color4f placeholderColor_{};
        bool    placeholderColorSet_ = false;
        Color4f caretColor_{};
        bool    caretColorSet_ = false;
        bool    readOnly_ = false;

        // Content length limit in characters (0 = unlimited). Defaults to
        // kDefaultMaxLength, not 0: a library-level default must exist, otherwise
        // every app using it must defend against "pasting a whole book" itself.
        // Explicit SetMaxLength(0) lifts it.
        std::size_t maxLength_ = kDefaultMaxLength;

        // The following are mutable: caret-by-click, scroll clamping, selection
        // changes and caret position caching all happen at PAINT time (the
        // measurer is only available in paintContent), so editing state is
        // written even in const painting -- the established shape of this
        // library's self-drawn widgets, not laziness.
        mutable std::size_t caret_ = 0;        // Caret = UTF-8 byte index (always on a character boundary)
        mutable std::size_t selAnchor_ = 0;    // Selection anchor (!= caret_ means a selection exists)
        mutable float scrollX_ = 0.0f;         // Horizontal scroll offset (clamped with the caret at paint time)
        mutable float scrollY_ = 0.0f;         // Vertical scroll offset (same; always 0 for single line)

        mutable EditLayout layout_;            // This frame's layout (derived fills lines, base completes)
        mutable bool layoutDirty_ = true;      // Text/size/line height changed -> rebuilt next frame

        // Caret position cache: written at paint time, read at keyboard time and
        // by updateImeCaretRect. One computation, many readers -- the IME code
        // used to re-measure "caret x / line top y" itself, and the two lines
        // could differ (empty box: one measured the placeholder, the other
        // text_), putting the candidate window outside the placeholder. Merged
        // into one copy so drift is structurally impossible.
        mutable float caretPixelX_ = 0.0f;     // Caret x (widget coordinates, alignment and scroll included)
        mutable float caretPixelY_ = 0.0f;     // Top y of the caret's line (widget coordinates)
        mutable float caretLineH_ = 0.0f;      // Line height (ascent + descent)

        // Pending input (events record coordinates only; paint time converts)
        mutable float pressX_ = 0.0f;          // Press point (content-local coordinates)
        mutable float pressY_ = 0.0f;
        mutable bool  hasPress_ = false;
        mutable float dragX_ = 0.0f;           // Drag-select move point (content-local coordinates)
        mutable float dragY_ = 0.0f;
        mutable bool  hasDrag_ = false;
        mutable bool  dragging_ = false;       // Mouse drag-select in progress (left button held, moving inside)

        // Vertical moves (up/down): the target column x is converted to a byte
        // index at paint time -- the same deferred path as "point". vertRun_
        // keeps the ORIGINAL column during repeated presses (otherwise moving to
        // a short line loses the column).
        mutable int   pendingVertDelta_ = 0;   // Pending vertical line offset (0 = none)
        mutable bool  pendingVertSelect_ = false; // Whether that move keeps the selection anchor
        mutable bool  vertRun_ = false;        // Whether a continuous vertical move is in progress
        mutable float vertGoalX_ = 0.0f;       // Target column x (widget coordinates)

        // Whether to keep the caret in view (scroll follow). Set on edits / caret
        // moves (see setCaret etc.), cleared after one paint-time clamp; user
        // wheel scrolling away does NOT drag the caret back. Always true for
        // single line (horizontal scrolling always follows the caret -- standard
        // behavior).
        mutable bool ensureCaretVisible_ = true;

        // Blink phase anchor: reset on focus / every edit so the caret "lights up
        // for 530ms first". This class deliberately does NOT override
        // wantsAnimationFrame() (like InputBox). The caret is discrete timing (a
        // flip every 530ms), not per-frame animation: paint time schedules the
        // next frame via Window::RequestFrameWake(time to next phase), honored by
        // both EventDriven and OnDemand drive modes. Switching to
        // wantsAnimationFrame() -> hasFocus() would mean a focused idle input box
        // keeps producing frames and burns CPU under OnDemand.
        std::chrono::steady_clock::time_point caretAnchor_{};

        // ---- IME (IMM32) state ----
        bool         imeComposing_ = false;   // Composing (blocks normal keys/chars during composition)
        std::wstring imeCompText_;            // Composition pinyin (preedit string, self-drawn)
        std::wstring imeLastCommit_;          // Last committed result string (dedup)
        unsigned long long imeLastCommitMs_ = 0;   // Last commit time (dedup window)
        std::u32string imePending_;           // Pending re-sent code points (two-channel dedup)
        unsigned long long imePendingMs_ = 0;      // Start of the re-send dedup window

        std::function<void(const std::string&)> onReturn_;
        std::function<void(const std::string&)> onTextChanged_;
        SelfAction1<const std::string&> selfOnReturn_;
        SelfAction1<const std::string&> selfOnTextChanged_;
    };

} // namespace Kit
