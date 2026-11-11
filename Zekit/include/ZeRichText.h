// ZeRichText.h: rich text widget - inline multi-style text runs (bold / italic /
// inline code / links / strikethrough / underline) inside one control.
//
// Why it derives Panel instead of TextWidget: (1) height that depends on width
// only participates in measurement through LayoutBase (leaf widgets are skipped
// by measureWidthDependentHeights, so a TextWidget-derived version would render
// only its first line); (2) per-run fonts need fc.AddText, an internal primitive.
// Size protocol: heightDependsOnWidth() == true, hintHeightFor(availWidth) does a
// trial wrap, hintWidth()/hintHeight() answer the unwrapped natural size - the
// same mechanism as FlowLayout, and all three are required.
// Value semantics: declare no destructor/copy/assign/move, or AddChild's
// make_unique<T>(std::move(x)) would fail to compile.

#pragma once
#include "ZePanel.h"
#include "ZeBoxStyle.h"
#include "ZeTextStyle.h"
#include "ZeSelfAction.h"

#include <string>
#include <string_view>
#include <vector>

namespace Kit {

    // ===================================================================
    //  Inline style flags
    // ===================================================================

    // Style bits for one run, freely combinable.
    //  Bits rather than an enum because styles stack (bold code is both). The
    //  bits are per-run overrides of font shape and decoration only; color is
    //  decided by the widget (see ApplyTheme) so theming keeps one source of
    //  truth. Written as constexpr unsigned constants rather than enum so
    //  RichFlag::Bold | RichFlag::Italic hits the built-in bitwise OR: defining
    //  operator| for built-in types is MSVC C2803.
    namespace RichFlag {
        constexpr unsigned None      = 0u;        // No style (body text).
        constexpr unsigned Bold      = 1u << 0;   // Bold (weight raised to Bold).
        constexpr unsigned Italic    = 1u << 1;   // Italic.
        constexpr unsigned Code      = 1u << 2;   // Inline code (monospace + background block).
        constexpr unsigned Strike    = 1u << 3;   // Strikethrough.
        constexpr unsigned Underline = 1u << 4;   // Underline.
        constexpr unsigned Link      = 1u << 5;   // Link (link color + underline + clickable + hover).
    }

    // True when a bit is set: HasRichFlag(run.flags, RichFlag::Bold).
    inline bool HasRichFlag(unsigned flags, unsigned bit) { return (flags & bit) != 0u; }

    // A span of inline text plus its style bits.
    //  A run is the smallest layout unit: wrapping does not treat a run as
    //  unbreakable (long paragraphs would have nowhere to break); breaks are
    //  found inside a run per character/word - see RichText::buildLines.
    struct RichRun {
        std::string text;              // Text (UTF-8).
        unsigned    flags = RichFlag::None;   // Style bits (RichFlag::* combination).
        std::string href;              // Link target (only meaningful with RichFlag::Link).
    };

    // ===================================================================
    //  Rich text widget
    // ===================================================================

    // Rich text widget: multi-run inline mixing + word wrap + link interaction.
    //  Inheritance: Widget -> LayoutBase -> VBoxLayout -> Panel -> RichText.
    //  No child widgets are used (DoLayout is not needed); all layout happens
    //  inside Paint - LayoutBase is derived purely for the measurement protocol
    //  identity plus the value-style background API.
    class RichText : public Panel {
    public:
        // Constructs an empty paragraph.
        //  ApplyTheme(Themes::Light()) is called here, like Label: ApplyTheme
        //  picks the theme section via virtuals, which resolve to the base
        //  version during base construction, so the derived constructor must
        //  make this call.
        RichText();

        // Constructs and inserts one body run (text, no style bits).
        explicit RichText(std::string text);

        // -------------------------------------------------------------------
        //  Content
        // -------------------------------------------------------------------

        // Replaces all runs.
        void SetRuns(std::vector<RichRun> runs);
        // All current runs.
        const std::vector<RichRun>& runs() const { return runs_; }

        // Appends a run (configuration-time use; returns self for chaining).
        RichText& AddRun(RichRun r);
        // Appends a text run: AddText("code", RichFlag::Code). flags is a
        // RichFlag::* combination (default body); href applies when flags
        // contains RichFlag::Link.
        RichText& AddText(std::string text, unsigned flags = RichFlag::None, std::string href = std::string());

        // Clears all runs.
        void Clear();
        // True when there is no text at all.
        bool empty() const;

        // -------------------------------------------------------------------
        //  Layout parameters
        // -------------------------------------------------------------------

        // Extra spacing between lines (pixels; default 0 = tight line height).
        void SetLineSpacing(float gap);
        // Current line spacing.
        float lineSpacing() const { return lineSpacing_; }

        // Horizontal alignment of the content area (default left - body text norm).
        //  Only affects where whole lines sit in the content area, not run order.
        void SetHorizontalAlign(TextHAlign a);
        // Current horizontal alignment.
        TextHAlign horizontalAlign() const { return hAlign_; }

        // -------------------------------------------------------------------
        //  Style (value interface - no drawing types exposed)
        // -------------------------------------------------------------------

        // Base body font; all runs except RichFlag::Code derive bold/italic from it.
        //  Not overwritten by ApplyTheme once set - see hasBaseFont_ below. Window
        //  mounting and every theme switch re-inject the whole tree; an
        //  unconditional theme override would silently erase per-tier font sizes
        //  (e.g. a 26px heading) at mount time.
        void SetBaseFontDesc(const FontDesc& f);
        // Current base body font.
        const FontDesc& baseFontDesc() const { return baseFont_; }
        // Base body font size (pixels).
        void SetBaseFontSize(unsigned px);

        // Monospace font file for inline code (empty = fall back to the base
        // font, distinguished only by the background block).
        void SetMonoFontFile(std::string file);
        // Inline code size scale relative to body text (default 0.94).
        void SetMonoFontScale(float scale);

        // Body text color (unset = follows the theme text token).
        void SetTextColor(const Color4f& c);
        // Inline code text color.
        void SetCodeTextColor(const Color4f& c);
        // Inline code background block color (alpha 0 = no background).
        void SetCodeBackgroundColor(const Color4f& c);
        // Inline code background left/right padding (pixels, default 4).
        void SetCodeBackgroundPadding(float padX);
        // Link idle color.
        void SetLinkColor(const Color4f& c);
        // Link hover color.
        void SetLinkHoverColor(const Color4f& c);

        // -------------------------------------------------------------------
        //  Link interaction
        // -------------------------------------------------------------------

        // Link click callback (sender injected by the library; href is the target).
        //  Do not capture this or stack widgets, like everywhere else: take the
        //  window from the sender:
        //  [](Kit::Widget& self, const std::string& href){ auto* w = self.window(); }
        void SetOnLinkClick(SelfAction1<const std::string&> cb) { onLinkClick_ = std::move(cb); }

        // -------------------------------------------------------------------
        //  Selection / copy
        // -------------------------------------------------------------------
        //  The clipboard goes through Window::GetClipboardText / SetClipboardText,
        //  the same platform-agnostic channel as TextEditBase (Win32 on desktop,
        //  JNI on mobile), so no platform code is needed here.
        // -------------------------------------------------------------------

        // Enables select + copy (default on): mouse drag selection, Ctrl+C copy,
        // Ctrl+A select all.
        //  Also makes the widget focusable, since keys (Ctrl+C) go only to the
        //  focused widget; Tab traversal visits focusable widgets, so every
        //  RichText becomes a Tab stop. Tutorials with dozens of paragraphs make
        //  that noisy; skipping Tab would need a separate "in Tab order" flag on
        //  Widget (not present yet), a library-wide change to add only if needed.
        void SetSelectable(bool on);
        // Whether selection / copy is currently allowed.
        bool isSelectable() const { return selectable_; }

        // Whether any text is selected.
        bool hasSelection() const { rebuildFlat(); return selAnchor_ != caret_; }
        // Selected plain text (empty string when no selection).
        std::string SelectedText() const;
        // Selects everything.
        void SelectAll();
        // Clears the selection (caret and anchor collapse together).
        void ClearSelection();
        // Copies to the system clipboard; returns true when written.
        //  With no selection, copies the whole text (like browser address bars /
        //  file rename boxes); with empty content, does not touch the clipboard -
        //  a stray Ctrl+C must not erase the user's clipboard.
        bool Copy() const;

        // -------------------------------------------------------------------
        //  Measurement protocol ("how big do I want to be")
        // -------------------------------------------------------------------

        // Natural width = the longest unwrapped line + padding.
        //  Returns the explicit width after SetSize directly (user intent first,
        //  like the rest of the library).
        float hintWidth() const override;

        // Natural height = single unwrapped line + padding.
        //  The fallback when no width is available; the real height goes through
        //  hintHeightFor().
        float hintHeight() const override;

        // Height needed after wrapping at the given width (padding + line spacing).
        //  availWidth is the content width the parent plans to give (excluding
        //  this widget's padding).
        float hintHeightFor(float availWidth) const override;

        // Always true: height depends on width (parent must set width first).
        //  Without this, VBox / ScrollView never call hintHeightFor and the
        //  paragraph shows only its first line with no scrollbar. See
        //  measureWidthDependentHeights.
        bool heightDependsOnWidth() const override { return true; }

        // -------------------------------------------------------------------
        //  Theme / paint / interaction
        // -------------------------------------------------------------------

        // Re-injects fonts and colors from the theme (body from the label section,
        // links from primary, etc.).
        //  Calls Panel::ApplyTheme(t) first: currently a no-op, but called
        //  explicitly so background/corner/border survive once Panel gets real
        //  theme injection. All fonts and colors here derive from tokens;
        //  user-set values (has*_) are layered on top.
        void ApplyTheme(const Theme& t) override;

        // Paints the Panel background plus the text run by run.
        void Paint(FrameCollector& fc) const override;

        // Mouse move: extends the selection while dragging, else link hover.
        bool OnMouseMove(MouseEvent& e) override;
        // Mouse press: fires the callback on a link, else starts a drag selection.
        bool OnMousePress(MouseEvent& e) override;
        // Mouse release: ends the drag selection (consumes events symmetrically with Press).
        bool OnMouseRelease(MouseEvent& e) override;
        // Mouse leave: clears the link hover state.
        void OnMouseLeave() override;
        // Keys: Ctrl+C copy / Ctrl+A select all / Esc clear selection /
        // left-right arrows move the caret (Shift extends).
        bool OnKeyPress(KeyEvent& e) override;
        // Focus loss: clears the selection (avoids a stale highlight on another paragraph).
        void OnFocusOut() override;

    private:
        // -------------------------------------------------------------------
        //  Layout intermediates (cached)
        // -------------------------------------------------------------------

        // A text span with a resolved font and x position within its line.
        struct Piece {
            const RichRun* run = nullptr;  // Source run (style bits / href).
            std::string    text;           // Actual text to draw (possibly a substring of the run).
            FontDesc       font;           // Font derived from the run style bits.
            TextMetrics    tm;             // Text metrics of this piece.
            float          x = 0.0f;       // Start relative to the content area's left edge.
            std::size_t    flat = 0;       // First char's offset in the flattened text (for selection).
        };

        // One line.
        struct Line {
            std::vector<Piece> pieces;
            float top = 0.0f;      // Line top relative to the content area's top edge (line spacing included).
            float ascent = 0.0f;   // Largest ascent on the line (baseline = top + ascent).
            float height = 0.0f;   // Largest line height (ascent + descent).
            float width = 0.0f;    // Actual used width (right edge of the last piece).
        };

        // Per-frame resolved color set (decided by theme / value interface).
        struct Palette {
            Color4f text{ 0.0f, 0.0f, 0.0f, 1.0f };
            Color4f codeText{ 0.0f, 0.0f, 0.0f, 1.0f };
            Color4f codeBg{ 0.0f, 0.0f, 0.0f, 0.0f };
            Color4f link{ 0.0f, 0.0f, 0.0f, 1.0f };
            Color4f linkHover{ 0.0f, 0.0f, 0.0f, 1.0f };
        };

        // Visual parameters of one run (font + decoration + background),
        // independent of where it is drawn.
        struct PieceStyle {
            FontDesc       font;
            TextDecoration decoration = TextDecoration::None;
            bool           isCode = false;
            bool           isLink = false;
        };

        // Wraps runs_ at avail and fills out (each Line's x starts at 0).
        //  Measure is a callable: TextMetrics(std::string_view, const FontDesc&).
        //  The wrap algorithm is templated on the measure function so one copy
        //  serves both paths: paint time (FrameCollector::MeasureText, sharing
        //  glyph metrics with drawing) and layout time (Window::MeasureText,
        //  no fc). Both must be same-source or the measured and painted line
        //  counts would disagree.
        template <class Measure>
        void buildLines(Measure measure, const Palette& pal, float avail,
                        std::vector<Line>& out) const;

        // Resolves one run's font / decoration (style bits -> PieceStyle).
        PieceStyle resolveStyle(const RichRun& run) const;

        // Text color for a piece per its style bits; hovered switches link color.
        Color4f pieceColor(const PieceStyle& st, const Palette& pal, bool hovered) const;

        // Composes the effective frame palette: theme-derived values + manual overrides (has*_).
        Palette effectivePalette() const;

        // Ensures the lines_ cache matches the current width + font (recomputes when dirty).
        //  avail is the current available content width. Wrapping uses
        //  Window::MeasureText (shared glyph metrics with painting); without a
        //  window the cache is cleared.
        void ensureLines(float avail) const;

        // After any content / style / font change: mark dirty and ask the parent to re-measure.
        //  Both steps are required: dirty without notify leaves the parent with a
        //  stale layout (text changed but height did not).
        void invalidate();

        // Content area (widget rect minus padding).
        void contentBox(float& cx, float& cy, float& cw, float& ch) const;

        // Hit test: which LINK piece contains content-area (lx, ly); returns a runs_ index or -1.
        int hitTestLink(float lx, float ly) const;

        // -------------------------------------------------------------------
        //  Selection / copy (selections are always flattened offsets)
        // -------------------------------------------------------------------
        //  Flat offsets instead of (run, byteOffset) pairs because selections
        //  span runs: with pairs, cross-style selections need a pile of boundary
        //  conditions. Flattened to one dimension, hit test / paint / copy share
        //  the same coordinates and the only condition is which side is smaller.
        //  Cost: a flat_ cache rebuilt on any content change (rebuildFlat).
        // -------------------------------------------------------------------

        // Keeps flat_ / runStart_ in sync with runs_ (rebuilds when dirty).
        void rebuildFlat() const;
        // Hit flattening: content-area (lx, ly) -> flattened text index.
        std::size_t hitTestFlat(float lx, float ly) const;
        // Previous / next UTF-8 character boundary (for arrow keys).
        std::size_t prevCharPos(std::size_t flat) const;
        std::size_t nextCharPos(std::size_t flat) const;

        // -------------------------------------------------------------------
        //  Data
        // -------------------------------------------------------------------

        std::vector<RichRun> runs_;

        FontDesc   baseFont_;                                  // Base body font.
        // Whether the caller explicitly set the base font (SetBaseFontDesc /
        // SetBaseFontSize). ApplyTheme falls back to the theme label font only
        // when unset, or per-tier sizes (26px heading, 14px body) would be washed
        // to one size on mount / theme switch. Same rule as ChromeButton keeping
        // its flat style: tree-wide re-injection washes user styles, customized
        // parts must be layered back on after injection.
        bool       hasBaseFont_ = false;
        std::string monoFile_;                                 // Monospace font file (empty = base font).
        float       monoScale_ = 0.94f;                        // Inline code size scale.
        float       codeBgPadX_ = 4.0f;                        // Inline code background side padding.
        float       lineSpacing_ = 0.0f;                       // Extra spacing between lines.
        TextHAlign  hAlign_ = TextHAlign::Left;                // Horizontal alignment.

        // Manual colors (has* false = follow the theme token).
        bool    hasText_ = false;   Color4f textColor_;
        bool    hasCodeText_ = false; Color4f codeTextColor_;
        bool    hasCodeBg_ = false;   Color4f codeBgColor_;
        bool    hasLink_ = false;     Color4f linkColor_;
        bool    hasLinkHover_ = false; Color4f linkHoverColor_;

        // Theme-injected colors (used when has* does not override).
        Palette themePalette_{};

        // Link click callback (sender injected by the library).
        SelfAction1<const std::string&> onLinkClick_;

        // runs_ index of the hovered link (-1 = none). A run index, not a piece
        // index: one run may wrap into several pieces but stays one link.
        int hoverRun_ = -1;

        // ---- Selection / copy state ----
        bool        selectable_ = true;   // Select + copy allowed (also decides focusability).
        bool        dragging_   = false;  // Drag-selecting (left button held and moving).
        bool        pressConsumed_ = false; // Last left press consumed by this widget (release must pair).
        std::size_t caret_      = 0;      // Caret (flattened offset).
        std::size_t selAnchor_  = 0;      // Selection anchor (== caret_ means no selection).
        Color4f     selColor_{ 0.2f, 0.4f, 0.9f, 0.28f };  // Selection background (theme-injected).

        // Flattened text (all run texts concatenated) and each run's start offset.
        //  mutable: selection hit / paint / copy are const methods, so the cache
        //  is rebuilt in place. Image run text carries the alt text; copying alt
        //  beats losing the segment entirely.
        mutable std::string              flat_;
        mutable std::vector<std::size_t> runStart_;   // Length = runs_.size() + 1.
        mutable bool                     flatDirty_ = true;

        // ---- Wrap cache (mutable: hintHeightFor / Paint are const) ----
        mutable std::vector<Line> lines_;          // Cached wrap result (each line's x starts at 0).
        mutable float cachedAvail_ = -1.0f;        // Available width the cache matches (-1 = invalid).
        mutable bool  cacheDirty_ = true;          // Set dirty on font / content changes.
    };

} // namespace Kit
