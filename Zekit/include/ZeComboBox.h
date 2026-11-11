// ZeComboBox.h: dropdown - a "select, don't type" input face plus the option
// list popped open below it (an overlay).
//
// Collapsed it looks like an input box (BoxStyle panel + one line of text) with
// an arrow on the right; clicking shows no caret (a selector, not an editor).
// The class adds four things: item data (SetItems / AddItem / itemAt, moved with
// value semantics, not reset by theme switches), selection state (-1 = nothing
// selected, placeholder shown instead), open state (Open / Close / Toggle /
// isOpen), and a selection callback that fires only on user paths (mouse pick /
// keyboard Enter), never on programmatic SetSelectedIndex - the same loop
// prevention rule as Slider / Checkbox.
//
// How the popup is drawn over sibling widgets (library-level mechanism):
//  1. Paint: while open, paintsOnTop() returns true so Window defers this widget
//     to a final overlay pass; the popup is always on top.
//  2. Hit: HitTest includes the popup rect while open, and hit testing checks
//     overlays before normal widgets - "visible means clickable" holds for the
//     popup too, otherwise clicks on the popup would fall through to covered
//     widgets. FrameCollector::ResetClip() lets the popup escape the clip regions
//     pushed by parent containers.
//
// The popup occupies no layout: open/close changes no sizes and needs no parent
// re-layout (hintWidth/hintHeight describe the collapsed face).
//
// Sizes: hintWidth/hintHeight are overridden - natural size = widest text
// (placeholder or items) + gap + arrow, times one text line height (plus padding
// and border). Overriding the hints is mandatory, or a VBoxLayout collapses the
// widget to zero height with no error at all.
//
// Interactions: left click toggles; clicking an item selects and closes (press
// highlights, release commits); Esc closes without changing the selection;
// Up/Down/Home/End move the highlight (wrapping), Enter/Space commit; Enter/
// Space/Up/Down while collapsed open it; wheel scrolls the list; focus loss
// closes; leaving the popup clears the row highlight.

#pragma once
#include "ZeTextWidget.h"
#include "ZePopupHost.h"   // Common base class for popup carriers (this class implements geometry / paint / input)
#include "ZeSelfAction.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Kit {

    class OverlayWindow;

    // Dropdown: collapsed face + expanded option list (overlay).
    //  Derives TextWidget: the selected item / placeholder line is the widget's
    //  own text (Text() syncs to the selected item text), so font size / color /
    //  alignment reuse the text layer interfaces. Popup rows use the theme's
    //  combo.item section (itemTextStyle_), not the face style - same pixel size
    //  maybe, but two different semantics; changing one must not touch the other.
    //
    //  The popup has no child widgets: it is a self-drawn rect plus self-drawn
    //  rows, with hit testing as the single line (y - listTop) / itemH. That
    //  saves a whole child-widget lifecycle and keeps "one geometry" natural
    //  (draw and hit share computePopup()).
    class ComboBox : public PopupHostWidget {
    public:
        // Constructs a dropdown.
        //  placeholder: hint text shown when nothing is selected (default empty).
        //  Uses the built-in light theme's combo section as default; themeBox /
        //  themeText are virtual and cannot be called from the base constructor
        //  (only the base version would run).
        explicit ComboBox(std::string placeholder = std::string());

        // Destructor (defined in the .cpp; unique_ptr<OverlayWindow> needs a complete type).
        ~ComboBox() override;

        // Value semantics: moved with AddWidget (an explicit destructor suppresses
        // implicit moves; this restores it). Defined in the .cpp (unique_ptr needs
        // a complete type). overlay_ moves with the default handling: the carrier
        // is shared per layer, so a moved handle stays valid. (Like the base:
        // move construction only, no move assignment.)
        ComboBox(ComboBox&&) noexcept;

        // -------------------------------------------------------------------
        //  Item data (data, not style - theme switches do not reset it)
        // -------------------------------------------------------------------

        // Replaces the whole item list, keeping the current selection when its
        // text still exists (first match); otherwise resets to -1. No callback
        // (programmatic path).
        void SetItems(std::vector<std::string> items);

        // Appends one item (no callback).
        void AddItem(std::string text);

        // Clears all items (also clears selection / highlight / open state; no callback).
        void ClearItems();

        // Number of items.
        std::size_t itemCount() const { return items_.size(); }

        // All items (read-only).
        const std::vector<std::string>& items() const { return items_; }

        // Text of item at index (out of range returns an empty-string reference).
        const std::string& itemAt(std::size_t index) const;

        // -------------------------------------------------------------------
        //  Selection state
        // -------------------------------------------------------------------
        //  Like Checkbox's checked_: persistent data, not transient state like
        //  pressed_; theme switches do not reset it. Difference: pick one of
        //  many, with "none selected" allowed.
        // -------------------------------------------------------------------

        // Sets the selected item (programmatic path, no callback).
        //  index: -1 = clear the selection (face returns to the placeholder);
        //  out-of-range values are treated as -1. The selected text syncs to
        //  Text() (that is what the face paints).
        void SetSelectedIndex(int index);

        // Current selected index (-1 = none).
        int selectedIndex() const { return selected_; }

        // Text of the selected item (empty string when none).
        std::string selectedText() const;

        // Whether an item is selected.
        bool hasSelection() const { return selected_ >= 0 && static_cast<std::size_t>(selected_) < items_.size(); }

        // Sets the placeholder text (shown when nothing is selected).
        void SetPlaceholder(std::string text);
        // Current placeholder text.
        const std::string& placeholder() const { return placeholder_; }

        // -------------------------------------------------------------------
        //  Open state
        // -------------------------------------------------------------------

        // Whether currently open.
        bool isOpen() const { return open_; }

        // Opens (no-op with no items); the highlight lands on the selected item.
        void Open();

        // Closes (clears highlight / hovered row).
        void Close();

        // Opens or closes.
        void Toggle();

        // Sets the open state; returns whether it actually changed.
        bool SetOpen(bool open);

        // -------------------------------------------------------------------
        //  Selection callback (outward notification - user paths only)
        // -------------------------------------------------------------------
        //  Fires only when the user really changes the selection (mouse pick /
        //  keyboard commit) and only when the index actually changed. Programmatic
        //  SetSelectedIndex does not fire - the callback means "the user acted",
        //  not "the value changed", otherwise a parent setting a value would
        //  trigger its own callback.
        // -------------------------------------------------------------------

        // Selection-changed callback type (sender + new index, -1 = deselected).
        using SelectionChangedAction = SelfAction1<int>;

        // Sets the selection-changed callback (user pick / keyboard commit).
        //  index is the new selection; null clears it.
        void SetOnSelectionChanged(SelectionChangedAction handler);

        // Sets the selection-changed callback (sender-only form; null clears it).
        //  Registerable alongside the payload version; dispatch runs the payload
        //  version first, then the sender version.
        void SetOnSelectionChanged(SelfAction handler);

        // -------------------------------------------------------------------
        //  Appearance (popup sizing / arrow / row colors)
        // -------------------------------------------------------------------

        // Max items shown at once (the rest via wheel). n <= 0 = unlimited
        // (popup height covers all items).
        void SetMaxVisibleItems(int n);
        // Current visible item limit (<= 0 = unlimited).
        int maxVisibleItems() const { return maxVisibleItems_; }

        // Row height (pixels); 0 = use the theme combo.itemHeight.
        void SetItemHeight(float h);
        // Effective row height (explicit value wins, else theme value).
        float itemHeight() const { return resolvedItemHeight(); }

        // Arrow size (pixels); 0 = auto (tied to one text line height).
        void SetArrowSize(float size);
        // Current arrow size (0 = auto).
        float arrowSize() const { return arrowSize_; }

        // Gap between face text and arrow (pixels, default 8).
        void SetGap(float gap);
        // Current gap.
        float gap() const { return gap_; }

        // Arrow color.
        void SetArrowColor(const Color4f& c);
        // Current arrow color.
        const Color4f& arrowColor() const { return arrowColor_; }

        // Full-row background of a hovered item (default translucent primary).
        void SetItemHoverColor(const Color4f& c);
        // Full-row background of the selected item.
        void SetItemSelectedColor(const Color4f& c);
        // Current hovered row background.
        const Color4f& itemHoverColor() const { return itemHoverColor_; }
        // Current selected row background.
        const Color4f& itemSelectedColor() const { return itemSelectedColor_; }

        // Popup box (background / border / corners / padding / shadow); editable directly.
        BoxStyle& popupBox() { return popupBox_; }
        const BoxStyle& popupBox() const { return popupBox_; }

        // Item text style; editable directly.
        TextStyle& itemTextStyle() { return itemTextStyle_; }
        const TextStyle& itemTextStyle() const { return itemTextStyle_; }

        // -------------------------------------------------------------------
        //  Intrinsic natural size (measurement protocol) - required for VBox / FlowLayout
        // -------------------------------------------------------------------
        //  Library-wide rule: an explicitly set user value wins; otherwise these
        //  functions compute. Natural size describes the collapsed face, open
        //  state irrelevant (the popup occupies no layout).
        // -------------------------------------------------------------------

        // Natural width = max(placeholder / item text widths) + gap + arrow + padding + border.
        float hintWidth() const override;

        // Natural height = max(text line height, arrow height) + padding + border.
        float hintHeight() const override;

        // -------------------------------------------------------------------
        //  Overlay / hit testing (library-level convention; see file header)
        // -------------------------------------------------------------------

        // True while open: Window defers this widget to the final overlay pass.
        //  False when closed, so normally it behaves exactly like other widgets.
        bool paintsOnTop() const override;

        // Hittable area = collapsed face UNION the popup rect while open.
        //  Without the override, clicks on the popup (outside the face) fall
        //  through to covered widgets - visible-but-unclickable is the worst
        //  possible interaction bug.
        bool HitTest(float x, float y) const override;

        // -------------------------------------------------------------------
        //  Theme injection
        // -------------------------------------------------------------------

        // Re-injects styles from the theme (face + popup + items).
        void ApplyTheme(const Theme& t) override;

        // -------------------------------------------------------------------
        //  Interaction events
        // -------------------------------------------------------------------

        bool OnMousePress(MouseEvent& e) override;
        bool OnMouseRelease(MouseEvent& e) override;
        bool OnMouseMove(MouseEvent& e) override;
        bool OnMouseWheel(MouseEvent& e) override;
        void OnMouseLeave() override;
        bool OnKeyPress(KeyEvent& e) override;

        // Closes the popup on focus loss (clicking elsewhere or Tabbing away both land here).
        //  This is the ONLY implementation of "click outside closes the popup":
        //  the popup is not a separate window and never sees external clicks, so
        //  focus arbitration is the existing broadcast channel to borrow.
        void OnFocusOut() override;

    protected:
        // Default box style from the theme's combo section (collapsed face).
        const BoxStyle& themeBox(const Theme& t) const override;
        // Default text style from the theme's combo section (face text).
        const TextStyle& themeText(const Theme& t) const override;

        // Paints the face text + arrow; the popup is layered on while open.
        void paintContent(FrameCollector& fc) const override;

        // -------------------------------------------------------------------
        //  Popup (shared by the native carrier / self-drawn overlay paths)
        // -------------------------------------------------------------------
        //  Native mode (desktop + popup capability): the popup moves into an
        //  OverlayWindow (an independent popup window) so it can cover native
        //  child windows like CanvasView. Fallback (other platforms): the popup
        //  is still a rect drawn by this widget in the second paint pass. Both
        //  paths share exactly the same geometry / paint / input logic (the
        //  onPopupXxx hooks below).
        // -------------------------------------------------------------------

        // Whether the popup should exist right now (open + has items + anchor visible).
        //  Both native and fallback paths funnel through this predicate;
        //  paintsOnTop() adds one more layer on top of it.
        bool popupActive() const;

        // Paints the popup into the given collector.
        //  fc: primitive collector (coordinate system per localPopup).
        //  localPopup: true = paint with the popup's top-left as origin (native
        //  carrier path, drawing into the OverlayWindow's content root); false =
        //  paint in host window coordinates (fallback second pass).
        void paintPopupInto(FrameCollector& fc, bool localPopup) const;

        // ---- Popup interaction (coordinates are host-window; native path forwards after conversion) ----
        bool onPopupPress(MouseEvent& e);
        bool onPopupRelease(MouseEvent& e);
        bool onPopupMove(MouseEvent& e);
        bool onPopupWheel(MouseEvent& e);
        void onPopupLeave();

        // -------------------------------------------------------------------
        //  Two hooks for derived classes (EditableComboBox)
        // -------------------------------------------------------------------
        //  An editable combo = a real input box on the face. It reuses ALL of the
        //  popup machinery (geometry / paint / hit / focus-close) and changes two
        //  points: the face text is no longer drawn here (an embedded editor draws
        //  it, so caret / selection exist), and a user pick must sync the value
        //  into that editor. Exactly these two points are exposed as hooks.
        // -------------------------------------------------------------------

        // Whether this class draws the face text itself.
        //  true (default) = this class draws Text(); false = the derived class
        //  draws that line. Either way the frame and arrow are still drawn here.
        virtual bool drawsOwnFaceText() const { return true; }

        // A user picked an item (the ONLY user-path selection exit: fires the
        //  callback and syncs text).
        //  Raised from private to protected and made virtual so derived classes
        //  can sync their editor after the user commits (EditableComboBox).
        //  Programmatic paths use the public SetSelectedIndex (no callback).
        virtual void commitSelection(int index);

    private:
        // Popup geometry (one copy shared by draw and hit test, structurally
        // excluding "looks like item 3, clicks as item 2").
        struct PopupGeom {
            float x = 0.0f;          // Popup left edge.
            float y = 0.0f;          // Popup top edge.
            float w = 0.0f;          // Popup width.
            float h = 0.0f;          // Popup height.
            float listTop = 0.0f;    // Top edge of the first drawable row (= y + top padding + border).
            float itemH = 0.0f;      // Row height.
            int   visible = 0;       // Number of items shown at once.
            bool  flipped = false;   // true = not enough space below, flipped above the face.
        };

        // Computes the popup geometry (pure query, no state change).
        PopupGeom computePopup() const;

        // Whether the popup SHOULD exist: is the anchor (this widget) still inside
        // every clipping ancestor's visible area?
        //  Returns false when some ancestor (ScrollView / Panel) has clipped the
        //  anchor away entirely. Needed because the popup escapes all clip regions
        //  via fc.ResetClip() and would not follow the clipping: scroll an outer
        //  ScrollView while the dropdown is open and the popup would keep floating
        //  with an anchor already off-screen (caught in stress testing). An
        //  invisible anchor makes the popup meaningless.
        //  Checks up the parent chain via each ancestor's clipChildrenRect(), a
        //  public interface, so the check holds on all platforms (also covers
        //  widgets laid out outside the window). When the anchor is invisible,
        //  paintsOnTop() also returns false, so paint / draw / hit all collapse
        //  into this single predicate - spreading it over three places would
        //  surely miss one.
        bool popupAnchorVisible() const;

        // Effective row height (explicit value first, then theme, then 28).
        float resolvedItemHeight() const;

        // Effective arrow size (explicit value first, else tied to line height, then 10).
        float resolvedArrowSize() const;

        // Which popup item contains a point (-1 = none).
        int itemAtPoint(float x, float y) const;

        // Scrolls the highlight row into view (after keyboard moves).
        void ensureHighlightVisible();

        // Clamps the scroll offset into the valid range.
        void clampScroll();

        // Fires registered callbacks (payload version first, then sender version).
        void notifySelectionChanged(int index);

        // Requests a repaint (silently skipped without a window; unmounted widgets need none).
        void requestRepaint();

        // ---- Native popup carrier (OverlayWindow path; always null on fallback platforms) ----

        // Popup rect (the base uses it to place the carrier; false = no popup now).
        bool popupRect(float& x, float& y, float& w, float& h) const override;

        // Should close now (taken over by a sibling / clicked elsewhere on the host).
        void dismissPopup() override;

        // The popupOverlay_ carrier and open_ live in PopupHostWidget (shared by four popup widgets).

        // ---- Data (moves with value semantics) ----
        std::vector<std::string> items_;         // Item list.
        std::string              placeholder_;   // Hint text when nothing is selected.
        int                      selected_ = -1; // Selected index (-1 = none).

        // ---- Open state / interaction transients ----
        bool open_ = false;                      // Whether open.
        int  highlight_ = -1;                    // Keyboard / default highlighted row.
        int  hoverRow_ = -1;                     // Mouse-hovered row (-1 = none).
        int  scroll_ = 0;                        // Index of the first visible row (scroll offset).
        bool pressedInFace_ = false;             // Press landed on the face (release toggles).
        bool pressedInPopup_ = false;            // Press landed on the popup (release commits).

        // ---- Appearance (user-overridable) ----
        int   maxVisibleItems_ = 8;              // Max items shown at once (<=0 = unlimited).
        float itemHeightOverride_ = 0.0f;        // Explicit row height (0 = follow theme).
        float themeItemHeight_ = 28.0f;          // Theme row height (ApplyTheme; overridden by explicit value).
        float arrowSize_ = 0.0f;                 // Arrow size (0 = auto).
        float gap_ = 8.0f;                       // Gap between text and arrow.

        Color4f arrowColor_{ 0.0f, 0.0f, 0.0f, 1.0f };          // Arrow color.
        Color4f itemHoverColor_{ 0.0f, 0.0f, 0.0f, 0.0f };      // Hovered row background.
        Color4f itemSelectedColor_{ 0.0f, 0.0f, 0.0f, 0.0f };   // Selected row background.

        BoxStyle  popupBox_;        // Popup box (theme-injected, user-editable).
        TextStyle itemTextStyle_;   // Item text style (theme-injected).

        // Callbacks (std::function is movable and moves with the implicit move
        // constructor when AddWidget relocates this widget).
        SelectionChangedAction onChanged_;      // Selection callback (with index).
        SelfAction             onChangedSelf_;  // Selection callback (self only).
    };

} // namespace Kit
