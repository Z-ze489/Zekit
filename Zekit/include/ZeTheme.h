// ZeTheme.h: Theme, the single source of style defaults for the whole library.
//
// Theme is a pure data table, not a manager:
//   - Window holds one Theme;
//   - widgets copy default styles from it into their own member fields at
//     construction;
//   - switching themes = replacing the Window's copy and re-injecting it into
//     every widget.
//
// Theme takes no part in rendering, layout, or events. Do not make it a global
// singleton; it shares the Window's lifetime.
//

#pragma once
#include <string>
#include <map>
#include <vector>
#include <functional>
#include <utility>
#include "color4f.h"
#include "FontDesc.h"
#include "ZeBoxStyle.h"
#include "ZeTextStyle.h"
#include "ZeFlexStyle.h"

namespace Kit {

    // ===================================================================
    //  Component styles: BoxStyle + TextStyle + widget-private fields
    // ===================================================================

    struct ButtonStyle {
        BoxStyle  box;
        TextStyle text;

        // Button-private fields
        // ButtonVariant variant = Filled;   // Add once the enum is defined
        // ButtonIconPos iconPos = None;
    };

    // Card style (ZeCard.h): box + title row + shadow parameters.
    // A card is a "floating block of content": background / border / radius
    // come from `box` (six states + padding live there, consistent with
    // existing practice); the title row and shadow are separate because those
    // two are what distinguish a card from a plain `Panel`.
    struct CardStyle {
        BoxStyle     box;                  // The card's own box (six-state colors / padding / border)
        TextStyle    title;                // Title text (one weight step heavier than body)
        float        titleHeight{ 0.0f };  // Title row height (0 = no title area, content starts at padding)
        bool         shadow{ true };       // Whether to draw a shadow (cards float one step above panels)
        float        shadowOffsetY{ 2.0f };
        float        shadowSpread{ 1.0f };
        Color4f      shadowColor{ 0.0f, 0.0f, 0.0f, 0.10f };
    };

    struct LabelStyle {
        BoxStyle  box;
        TextStyle text;
    };

    // Input style (single-line edit; TextArea will reuse this section)
    struct InputStyle {
        BoxStyle  box;
        TextStyle text;
    };

    // Progress bar style (ZeProgressBar.h).
    // The bar is two stacked rounded rectangles, so two box styles are needed:
    //   - `track`: the full background (track);
    //   - `fill`: the foreground of the filled part.
    // Each clamps its radius to half its short side, which is what makes the
    // capsule progress bar work.
    //
    // Since 2026-10-24 there is also `thumb` (an optional handle) and
    // `thumbColor`: a progress bar can become a slider via SetDraggable(true),
    // and only then does it need a handle. The handle is not drawn by default,
    // so this section is dead data for a plain read-only progress bar.
    //
    // Colors do not use the BoxStyle::colors six-state array: the track / fill
    // are single values (no hover / press semantics); only the handle's outline
    // follows interaction state (brighter on hover / press, same approach as
    // the Slider's dot).
    //
    // `text` only covers the optional overlay text (color + size + weight).
    struct ProgressStyle {
        BoxStyle  track;          // Track (full background; only padding / border used here, color is trackColor)
        BoxStyle  fill;           // Fill (only box border/padding semantics, color is fillColor)
        BoxStyle  thumb;          // Handle (drawn when draggable; border / radius, color is thumbColor)
        TextStyle text;           // Overlay text (color + size + weight)

        Color4f trackColor{ 0.0f, 0.0f, 0.0f, 0.0f };  // Track color (transparent by default = not drawn separately)
        Color4f fillColor{ 0.0f, 0.0f, 0.0f, 1.0f };   // Fill color
        Color4f thumbColor{ 1.0f, 1.0f, 1.0f, 1.0f };  // Handle background (drawn only when draggable)
    };

    // Slider style (ZeSlider.h).
    // A slider is a progress bar plus a draggable round thumb, so compared to
    // ProgressStyle there is one more `thumb` box style (dot border / radius)
    // and `thumbColor`.
    //
    // As with the progress bar: track / fill / dot colors are all single
    // values, not the BoxStyle::colors six-state array. Although there is
    // hover / press, "one color per piece" is far simpler than "six states per
    // piece"; the interaction state only affects the dot's size / border
    // emphasis (adjusted in the widget via currentState()).
    //
    // `text` only covers the optional value text (color + size + weight).
    struct SliderStyle {
        BoxStyle  track;          // Track (background only; padding / border / radius used, color is trackColor)
        BoxStyle  fill;           // Filled part (radius semantics only, color is fillColor)
        BoxStyle  thumb;          // Dot (border / radius; color is thumbColor)
        TextStyle text;           // Value text (color + size + weight)

        Color4f trackColor{ 0.0f, 0.0f, 0.0f, 0.0f };  // Track color (transparent by default = not drawn separately)
        Color4f fillColor{ 0.0f, 0.0f, 0.0f, 1.0f };   // Filled part color
        Color4f thumbColor{ 1.0f, 1.0f, 1.0f, 1.0f };  // Dot color
    };

    // Checkbox / radio button style (ZeCheckbox.h / ZeRadioButton.h).
    // They are the same family of indicator widgets (square + check / ring +
    // solid dot) and share this section, since their visual skeleton is
    // identical: one indicator + spacing + one label row, with the same color
    // semantics:
    //   - `indicatorColor`: indicator background (unselected: light / white);
    //   - `indicatorBorderColor`: indicator border (unselected square edge /
    //     ring);
    //   - `markColor`: accent color. For a checked checkbox it is the square
    //     background plus the check itself; for a selected radio it is the
    //     circle fill (the center white dot uses indicatorColor).
    //
    // Colors are single values (no six-state table): the checkbox's interactive
    // feedback is the persistent "checked" state, not a transient hover /
    // press highlight; only Normal would ever be read from the six-state
    // array. If hover tinting is ever needed, markColor already provides the
    // contrast.
    //
    // `box` covers the whole widget's box (empty by default: a checkbox has no
    // background/border of its own, it is just "an indicator + a line of
    // text"); `text` covers the label text.
    struct CheckboxStyle {
        BoxStyle  box;            // Whole-widget box (empty by default: no background or border)
        TextStyle text;           // Label text (color + size + weight)

        Color4f indicatorColor{ 1.0f, 1.0f, 1.0f, 1.0f };        // Indicator background (unselected)
        Color4f indicatorBorderColor{ 0.0f, 0.0f, 0.0f, 1.0f };  // Indicator border (unselected)
        Color4f markColor{ 0.0f, 0.0f, 0.0f, 1.0f };             // Accent color (selected)
    };

    // Combo box style (ZeComboBox.h).
    // A combo box has two faces, so two box styles are needed:
    //   - `box`: the collapsed panel (selected item text + right arrow), same
    //     visual family as the input box;
    //   - `popup`: the expanded list panel, one shadow heavier because it
    //     floats.
    //
    // Colors come in two sets for different reasons:
    //   - `box.colors` / `popup.colors` use six states (background / border):
    //     the panel is a standard interactive widget with hover / focus /
    //     disabled; the popup only needs Normal, and leaving the rest empty is
    //     harmless.
    //   - `arrowColor` / `itemHoverColor` / `itemSelectedColor` are single
    //     values: the arrow never changes with state; "hover / selected" is a
    //     row-level property (which row), not the widget's state, so putting it
    //     in the WidgetState six-state table would be a mismatch.
    //
    // `item` covers list item text (with Normal / Disabled state colors),
    // `text` covers the line on the panel, and `itemHeight` is the per-item
    // height (list height = visible items x itemHeight + popup padding).
    struct ComboStyle {
        BoxStyle  box;      // Collapsed panel (background + border + radius + padding)
        TextStyle text;     // Text on the panel (selected item / placeholder)
        BoxStyle  popup;    // Expanded popup (background + border + radius + padding + shadow)
        TextStyle item;     // List item text

        float     itemHeight{ 28.0f };   // Per-item height (pixels)
        Color4f   arrowColor{ 0.0f, 0.0f, 0.0f, 1.0f };          // Right arrow color
        Color4f   itemHoverColor{ 0.0f, 0.0f, 0.0f, 0.0f };      // Hovered row background (transparent by default = none)
        Color4f   itemSelectedColor{ 0.0f, 0.0f, 0.0f, 0.0f };   // Selected row background
    };

    // Menu modal window style (ZeMenuModal.h).
    // The menu is a floating panel (same visual family as the combo popup:
    // surface background + border + shadow) holding rows of commands. So one
    // `panel` box style plus two text styles:
    //   - `item`: menu item text (Normal / Disabled states; disabled items
    //     gray out);
    //   - `shortcut`: right-side shortcut hint (muted, one size smaller, so
    //     secondary info does not compete with body text).
    //
    // All three colors are single values (no six states): highlighting is a
    // row-level property, and separators / scrim never change with state,
    // same reasoning as the combo box row colors.
    //
    // `scrimColor` is the modal scrim (a translucent dark layer over the whole
    // window while open). Turn it off with MenuModal::SetScrimEnabled(false);
    // that only skips drawing, input capture stays the same.
    struct MenuStyle {
        BoxStyle  panel;      // Menu panel (background + border + radius + padding + shadow)
        TextStyle item;       // Menu item text
        TextStyle shortcut;   // Right-side shortcut hint

        float   itemHeight{ 30.0f };   // Per-item height (pixels)
        Color4f itemHoverColor{ 0.0f, 0.0f, 0.0f, 0.0f };   // Hover / keyboard-highlighted row background
        Color4f separatorColor{ 0.0f, 0.0f, 0.0f, 0.0f };   // Separator color
        Color4f scrimColor{ 0.0f, 0.0f, 0.0f, 0.0f };       // Modal scrim color
    };

    // Tooltip style (ZeTooltip.h).
    // The bubble is a small floating panel (same visual family as the combo
    // popup / menu panel: surface background + border + shadow) holding one
    // line of small text. So one `box` for the shape and one `text` for the
    // text, isomorphic to MenuStyle's split.
    //
    // No single-value colors: the bubble receives no input (HitTest only
    // recognizes the bubble rectangle and events pass through), so there is no
    // hover / press semantics and only Normal is read from the six-state
    // array.
    //
    // Bubble shape (the tooltip's "shape container" is drawn by the renderer,
    // see ZeTooltip.cpp).
    // The carrier/container itself has a transparent background and no border
    // (floating carriers are always per-pixel transparent, and the Tooltip
    // base box never paints a background), so the visible bubble is entirely a
    // primitive drawn by the renderer according to Shape. The shape is just
    // data: adding a shape = adding an enum value + one drawing branch, with
    // no container changes.
    enum class TooltipShape {
        Rounded,   // Rounded rectangle + small arrow (default; points at the target)
        Pill,      // Capsule: fully rounded, no arrow (cleanest for a one-line hint)
        Square,    // Square rectangle, no arrow (grid-aligned industrial UI)
        Balloon,   // Speech bubble: rounded + offset long tail (conversation / annotation feel)
        Card,      // Card: rounded + left accent bar, no arrow (rich-content hint)
        Glass,     // Glass: translucent background + thin outline, no arrow (over busy visuals)
        Circle,    // Circle: the container is a circle, text centered (no sharp corners)
        Star,      // Star: the container is a five-pointed star, text centered
        Info,      // Info icon bubble: container = circle + tail pointing at the target, text centered
        Notch,     // Folded note: top-right corner clipped (sticky note / tag feel), text left-aligned
        Cloud,     // Cloud: the container is a cloud (base + three round crowns), text centered
        Neon,      // Neon: bright outline + three-layer outer glow (most visible on dark UI)
        Ribbon,    // Swallowtail ribbon: triangular notch at each end, text centered ("NEW" banner)
        Custom,    // Custom image: the container draws nothing (no background / border /
                   // shadow / arrow); the shape is entirely determined by the image
                   // set via SetBackgroundImage (image and bubble rectangle edge to edge)
    };

    struct TooltipStyle {
        BoxStyle  box;    // Bubble body (background + border + radius + padding + shadow)
        TextStyle text;   // Bubble text (size / color)

        // Bubble shape (see TooltipShape; the container is transparent and
        // borderless, the shape is entirely renderer-drawn)
        TooltipShape shape = TooltipShape::Rounded;

        // Background opacity of the Glass shape (0~1; smaller = more transparent)
        float glassAlpha = 0.78f;

        // Arrow outline color (transparent = take `box`'s border color).
        // Why a separate field: the bubble is white with a 1px light-gray
        // border, and the arrow is a small triangle with the same white fill
        // and border, nearly invisible on light pages (confirmed by
        // screenshots). So the arrow outline defaults to `muted` (one step
        // darker than `border`), visible without forming a jarring dark line
        // along the bubble edge.
        Color4f arrowBorder{ 0.0f, 0.0f, 0.0f, 0.0f };

        // Color of the Card shape's left accent bar (transparent = take primary)
        Color4f accent{ 0.0f, 0.0f, 0.0f, 0.0f };
    };

    // Toast style (ZeToast.h).
    // A toast is a transient floating strip that takes color by semantic
    // level; a 4px semantic color bar on the left is the cheapest way (tinting
    // the whole strip would make the text unreadable).
    //
    // The four colors are named fields, not enum indices: the level enum lives
    // in ZeToast.h, which includes this header back, so using the enum as an
    // array index would tie the two headers into a cycle.
    struct ToastStyle {
        BoxStyle  box;    // Toast body (background + radius + padding + shadow)
        TextStyle text;   // Toast text (size / weight; color at text.colors)

        float   accentWidth{ 4.0f };                        // Left semantic color bar width (0 = none)
        Color4f accentInfo{ 0.0f, 0.0f, 0.0f, 0.0f };       // Info
        Color4f accentSuccess{ 0.0f, 0.0f, 0.0f, 0.0f };    // Success
        Color4f accentWarning{ 0.0f, 0.0f, 0.0f, 0.0f };    // Warning
        Color4f accentError{ 0.0f, 0.0f, 0.0f, 0.0f };      // Error

        // Type icon circle (fill takes the matching accent; glyph always uses iconGlyph)
        float   iconSize{ 22.0f };                          // Icon circle diameter (0 = none)
        float   iconGap{ 10.0f };                           // Gap between icon circle and text
        Color4f iconGlyph{ 0.0f, 0.0f, 0.0f, 0.0f };        // Icon glyph color (transparent = always white)

        float   minWidth{ 300.0f };                         // Minimum width (0 = fit content)
        Color4f closeGlyph{ 0.0f, 0.0f, 0.0f, 0.0f };       // Close button glyph color (transparent = text color, lighter)
        Color4f closeHoverBg{ 0.0f, 0.0f, 0.0f, 0.0f };     // Close button hover background (transparent = 8% black)
    };

    // Menu bar style (ZeMenuBar.h).
    // The bar itself is a horizontal strip of "surface background + 1px bottom
    // divider" (VS style: bar and content area are separated by a line, not a
    // color difference). Menu titles are internal areas of the bar (not child
    // widgets), so the title's hover background / padding / radius get their
    // own section.
    struct MenuBarStyle {
        BoxStyle  bar;             // Bar body (background + bottom border)
        TextStyle title;           // Menu title text

        Color4f titleHover{ 0.0f, 0.0f, 0.0f, 0.0f };  // Title hover / open background
        float   titlePadX{ 10.0f };                    // Title horizontal padding (pixels)
        float   titleRadius{ 5.0f };                   // Title highlight radius
        float   barHeight{ 38.0f };                    // Bar height (0 = widget default 38)
    };

    // Splitter style (ZeSplitter.h).
    // The handle is the draggable narrow strip between two panels. It has no
    // box of its own, just a rounded rectangle, so only two colors + one
    // radius here. Thickness is not part of the theme: it is a layout
    // parameter (Splitter::SetHandleWidth), a different concern from looks.
    struct SplitterStyle {
        Color4f handleColor{ 0.0f, 0.0f, 0.0f, 0.0f };        // Handle resting color
        Color4f handleActiveColor{ 0.0f, 0.0f, 0.0f, 0.0f };  // Hover / dragging
        float   radius{ 0.0f };                               // Handle radius (0 = square)
    };

    // Dock panel style (ZeDockPanel.h).
    // The dock panel's visual skeleton is not one box but three distinct
    // things, so fields are split accordingly:
    //   - The container's own background (lowest layer): `background` /
    //     `border*` / `radius`. This is what shows through the gaps between
    //     panes.
    //   - Each docked pane's frame (with title bar): `paneBackground` /
    //     `paneBorder*` / `titleBarColor` / `title`. This is what the user
    //     perceives as "one pane".
    //   - The splitters (draggable gaps between panes): `handleColor` /
    //     `handleActiveColor` / `handleRadius`. Same structure as SplitterStyle
    //     but deliberately not shared: a dock panel has one splitter per
    //     internal tree node.
    //
    // Two groups of interaction feedback colors:
    //   - `drop*`: highlights the half-cell where a drag will land (drawn in an
    //     overlay, see the second pass of DockPanel::Paint);
    //   - `close*`: the two states of the pane title bar's close button.
    //
    // Size fields (`titleBarHeight` / `minPane*` / `closeSize`) live in the
    // theme because they follow the font size: bigger text needs a taller
    // title bar, not something callers should fix up everywhere. Splitter
    // thickness follows the same discipline as SplitterStyle: it is a layout
    // parameter (DockPanel::SetSplitterWidth) and does not belong in the
    // theme.
    //
    // All colors are derived from tokens (see Themes::Derive), so Dark() never
    // needs to touch this section.
    struct DockPanelStyle {
        Color4f background{ 0.0f, 0.0f, 0.0f, 0.0f };      // Container base background (shows through pane gaps)
        Color4f borderColor{ 0.0f, 0.0f, 0.0f, 0.0f };     // Container border color
        float   borderWidth{ 0.0f };                       // Container border width (0 = none)
        float   radius{ 0.0f };                            // Container radius

        Color4f   paneBackground{ 0.0f, 0.0f, 0.0f, 0.0f };  // Single pane background
        Color4f   paneBorderColor{ 0.0f, 0.0f, 0.0f, 0.0f }; // Single pane border color
        float     paneBorderWidth{ 0.0f };                   // Single pane border width (0 = none, gaps separate panes)
        // Pane radius (pixels; 0 = square). Only the top two corners are
        // rounded; the bottom two are filled in when painting (the SDF only
        // supports one radius for all four corners). Intended to be used with
        // `paneBorderWidth = 0`: no border line, just radius and gaps, so the
        // gaps carry the hierarchy (modern IDE look).
        float     paneRadius{ 0.0f };
        Color4f   titleBarColor{ 0.0f, 0.0f, 0.0f, 0.0f };   // Title bar background
        // Opacity multiplier for the divider under the title bar (0 = none;
        // default 0.5 = very faint). Title bar and content are already told
        // apart by background difference; a full-strength line would read as a
        // "black line". Half the border color is enough to divide without
        // shouting.
        float     titleBarDivider{ 0.5f };
        TextStyle title;                                     // Title bar text

        Color4f handleColor{ 0.0f, 0.0f, 0.0f, 0.0f };        // Splitter resting color
        Color4f handleActiveColor{ 0.0f, 0.0f, 0.0f, 0.0f };  // Splitter hover / dragging
        float   handleRadius{ 0.0f };                         // Splitter radius
        // Handle thickness (pixels; 0 = auto: 45% of the gap width, clamped to
        // 2~4px). The gap width comes from DockPanel::SetSplitterWidth. The
        // handle is narrower than the gap, leaving a little air on each side;
        // that is the difference between "a thin line" and "a scratch".
        float   handleThickness{ 0.0f };
        // Whether to also draw the splitter at rest (default `false` = quiet,
        // appears only on hover / drag). Painting the full gap at rest is the
        // source of the "ring of black lines": users accept a hint, not a
        // frame. So by default only the container background shows in the gap,
        // and the handle lights up as the pointer passes.
        bool    handleAlwaysVisible{ false };

        Color4f dropColor{ 0.0f, 0.0f, 0.0f, 0.0f };   // Drop target highlight (translucent overlay)
        float   dropBorderWidth{ 0.0f };                // Drop highlight border width

        Color4f closeColor{ 0.0f, 0.0f, 0.0f, 0.0f };       // Close button resting color
        Color4f closeHoverColor{ 0.0f, 0.0f, 0.0f, 0.0f };  // Close button hover color

        float titleBarHeight{ 26.0f };   // Title bar height (pixels)
        float closeSize{ 14.0f };        // Close button edge length (pixels)
        float minPaneWidth{ 56.0f };     // Minimum pane width (lower bound when dragging splitters)
        float minPaneHeight{ 40.0f };    // Minimum pane height
    };

    // List style (ZeListView.h).
    // A list is a stack of equal-height rows; all fields are single values (no
    // six-state table): one container background + one border, two row state
    // colors (hover / selected), and a row height.
    //
    // Row hover / selected are row-level properties (which row), not the
    // widget's state, so the WidgetState six-state table would be a mismatch
    // (same reasoning as the combo box's itemHoverColor).
    //
    // Note: ZeListView inherits ScrollView (not the BoxWidget branch), so this
    // section's container appearance is split into flat fields instead of one
    // BoxStyle — the widget has no `box_` to inject into.
    struct ListStyle {
        TextStyle item;   // Row text (size / color / alignment)

        float   itemHeight{ 28.0f };                        // Row height (pixels)
        Color4f itemHoverColor{ 0.0f, 0.0f, 0.0f, 0.0f };   // Hovered row full-row background
        Color4f itemSelectedColor{ 0.0f, 0.0f, 0.0f, 0.0f };// Selected row full-row background

        Color4f background{ 0.0f, 0.0f, 0.0f, 0.0f };       // List container background
        Color4f borderColor{ 0.0f, 0.0f, 0.0f, 0.0f };      // List container border color
        float   borderWidth{ 0.0f };                        // Border width (also used as padding)
        float   radius{ 0.0f };                             // Container radius (pixels)
    };

    // Tab view style (ZeTabView.h).
    // The tab bar is one strip holding several equal tabs. Hierarchy comes
    // from "indicator line + light/dark": unselected tabs use muted text, the
    // selected one uses body color + a primary indicator line (or a light
    // block), rather than giving each tab a differently colored block, which
    // would be noisy on screen.
    //
    // As with ListStyle: flat fields, no six-state table (tab hover / selected
    // are tab-level states).
    struct TabStyle {
        TextStyle head;         // Tab text (unselected)
        TextStyle headActive;   // Tab text (selected)

        float   barHeight{ 34.0f };      // Tab bar height (pixels)
        float   minTabWidth{ 64.0f };    // Minimum tab width
        float   tabPaddingX{ 14.0f };    // Horizontal padding around tab text

        Color4f barBackground{ 0.0f, 0.0f, 0.0f, 0.0f };       // Tab bar background
        Color4f dividerColor{ 0.0f, 0.0f, 0.0f, 0.0f };        // Bottom divider of the bar
        Color4f hoverBackground{ 0.0f, 0.0f, 0.0f, 0.0f };     // Hovered tab background
        Color4f activeBackground{ 0.0f, 0.0f, 0.0f, 0.0f };    // Selected tab background (Fill indicator)
        Color4f indicatorColor{ 0.0f, 0.0f, 0.0f, 0.0f };      // Selection indicator line (Line indicator)
        float   indicatorHeight{ 2.0f };                       // Indicator line thickness
        float   radius{ 6.0f };                                // Tab radius (Fill indicator)
    };

    // Table style (ZeTableView.h).
    // A table is a stack of equal-height rows plus a fixed header; rows and
    // header each get a text style. Visual intent: the header is one step
    // weaker than the body (muted) and its background one step lower, so the
    // eye lands on the data first and the header is just a label.
    //
    // As with ListStyle: flat fields, no six-state table (row hover / selected
    // are row-level states).
    struct TableStyle {
        TextStyle header;   // Column header text
        TextStyle cell;     // Cell text

        float   rowHeight{ 28.0f };      // Row height (pixels)
        float   headerHeight{ 32.0f };   // Header height (pixels)
        float   cellPaddingX{ 8.0f };    // Horizontal padding inside cells (auto column widths include it)

        Color4f headerBackground{ 0.0f, 0.0f, 0.0f, 0.0f };  // Header background
        Color4f background{ 0.0f, 0.0f, 0.0f, 0.0f };        // Table background
        Color4f borderColor{ 0.0f, 0.0f, 0.0f, 0.0f };       // Outer border color
        float   borderWidth{ 0.0f };                         // Outer border width (0 = none)
        Color4f gridColor{ 0.0f, 0.0f, 0.0f, 0.0f };         // Vertical grid lines / header separators

        Color4f rowHoverColor{ 0.0f, 0.0f, 0.0f, 0.0f };     // Hovered row full-row background
        Color4f rowSelectedColor{ 0.0f, 0.0f, 0.0f, 0.0f };  // Selected row full-row background
    };

    // Tree view style (ZeTreeView.h).
    // Almost the same set as ListStyle (row height / two row backgrounds /
    // container skeleton), plus an indent width, an indicator area width (the
    // small `+` / `-` block), and the indicator's own color.
    struct TreeStyle {
        TextStyle item;   // Row text

        float   rowHeight{ 28.0f };       // Row height (pixels)
        float   indentWidth{ 18.0f };     // Indent per level (pixels)
        float   indicatorWidth{ 20.0f };  // Expand indicator width at row start (pixels)

        Color4f rowHoverColor{ 0.0f, 0.0f, 0.0f, 0.0f };     // Hovered row full-row background
        Color4f rowSelectedColor{ 0.0f, 0.0f, 0.0f, 0.0f };  // Selected row full-row background
        Color4f indicatorColor{ 0.0f, 0.0f, 0.0f, 0.0f };    // Color of `+` / `-`

        Color4f background{ 0.0f, 0.0f, 0.0f, 0.0f };        // Container background
        Color4f borderColor{ 0.0f, 0.0f, 0.0f, 0.0f };       // Container border color
        float   borderWidth{ 0.0f };                         // Border width (also used as padding)
        float   radius{ 0.0f };                              // Container radius (pixels)
    };

    // Style for the mobile sub-window stand-in (Zekit/ZeSubWindow.h, library
    // internal). On desktop, sub-windows / dialogs are real OS windows whose
    // decorations come from the system, so this section does not apply there
    // (see Kit::ChildKind). It only serves the mobile fallback that paints a
    // window-shaped rectangle on the host canvas.
    //
    // A sub-window is a floating window: frame + title bar + content area +
    // shadow. It belongs to the same "floating things" family as tooltip /
    // toast / menu.panel, but heavier: it carries a whole block of UI, so the
    // shadow is bigger and both frame and title bar are explicit.
    //
    // The title bar background is one step lower than the content (same trick
    // as "tab bar vs page"): light/dark says "this part is the title", no
    // divider line needed.
    struct SubWindowStyle {
        TextStyle title;   // Title bar text (fontM + 600 semi-bold)

        float   titleBarHeight{ 30.0f };   // Title bar height (pixels)
        float   radius{ 12.0f };           // Window radius (pixels)

        Color4f titleBarBackground{ 0.0f, 0.0f, 0.0f, 0.0f };  // Title bar background
        Color4f background{ 0.0f, 0.0f, 0.0f, 0.0f };          // Content area background
        Color4f borderColor{ 0.0f, 0.0f, 0.0f, 0.0f };         // Outer frame
        Color4f closeColor{ 0.0f, 0.0f, 0.0f, 0.0f };          // Close button `x` color
        Color4f closeHoverColor{ 0.0f, 0.0f, 0.0f, 0.0f };     // `x` / background on hover
        Color4f shadowColor{ 0.0f, 0.0f, 0.0f, 0.0f };         // Shadow
        float   shadowOffsetY{ 6.0f };
        float   shadowSpread{ 2.0f };

        Color4f scrimColor{ 0.0f, 0.0f, 0.0f, 0.0f };          // Modal scrim (used by `ModalDialog`)
    };

    // Modal dialog style (the `modalDialog` section of Kit::ModalDialog).
    // A close relative of SubWindowStyle (same family of "a whole floating
    // block of UI": frame + title bar + content + shadow), but deliberately
    // in separate sections: the sub-window follows the host's viewport / tool
    // window, while a dialog is something that must be dealt with, so their
    // visual weights can be tuned independently (e.g. darken only the dialog's
    // scrim without touching the sub-window).
    //
    // Kit::ModalDialog is self-drawn (not subject to system decorations), so
    // every field here really lands on pixels:
    //   - `titleBarBackground` / `background` / `borderColor` / `radius`: the
    //     panel skeleton (ModalDialog::Paint uses drawutil::PaintBox);
    //   - `shadow*`: the shadow around the panel (bigger = floatier);
    //   - `closeColor` / `closeHoverColor`: the top-right `x` (hover turns red
    //     = destructive hint);
    //   - `scrimColor`: the dark film over the whole host client area
    //     (overridable via SetScrimColor).
    //
    // Defaults are scrim on and click-outside does not close; both live in
    // ModalDialog::Options, not the theme (the theme only covers looks, not
    // behavior).
    struct ModalDialogStyle {
        TextStyle title;   // Title bar text (fontM + 600 semi-bold)

        float   titleBarHeight{ 34.0f };   // Title bar height (pixels), slightly taller than the sub-window for prominence
        float   radius{ 12.0f };           // Panel radius (pixels)

        Color4f titleBarBackground{ 0.0f, 0.0f, 0.0f, 0.0f };  // Title bar background
        Color4f background{ 0.0f, 0.0f, 0.0f, 0.0f };          // Content area background
        Color4f borderColor{ 0.0f, 0.0f, 0.0f, 0.0f };         // Outer frame
        Color4f closeColor{ 0.0f, 0.0f, 0.0f, 0.0f };          // Close button `x` color
        Color4f closeHoverColor{ 0.0f, 0.0f, 0.0f, 0.0f };     // `x` / background on hover
        Color4f shadowColor{ 0.0f, 0.0f, 0.0f, 0.0f };         // Shadow
        float   shadowOffsetY{ 8.0f };                          // Shadow offset down (dialog floats more than tool windows)
        float   shadowSpread{ 3.0f };                           // Shadow spread

        Color4f scrimColor{ 0.0f, 0.0f, 0.0f, 0.0f };          // Modal scrim (covers the whole host client area)
    };

    // ===================================================================
    //  Styles for the "skeleton container" widgets
    //  (card / collapse / sidebar / drawer / toolbar / status bar)
    // ===================================================================
    //  These six widgets share the exact same visual skeleton: one background
    //  + one border + one radius, plus an optional line of text. They differ
    //  only in where they sit and who arranges their children.
    //
    //  So they all share the SurfaceStyle struct instead of one struct each
    //  with only the names changed (exactly what the "define each thing once
    //  per library" rule at the top of ZeBoxStyle.h avoids). Same judgment as
    //  checkbox/radio sharing CheckboxStyle: look at whether the visual
    //  skeleton is isomorphic, not at widget names.
    //
    //  Note this deliberately does not use BoxStyle: that would drag in
    //  six-state colors, background image, shadow, outline, and a whole wall
    //  of fields, none of which a skeleton container needs (it does not hover
    //  or press). Consistent with ProgressStyle / TreeStyle: colors are
    //  single values, straightforward.
    // ===================================================================

    // Skeleton container style: background + border + radius + one line of
    // text. Serves Card / Collapse / Sidebar / Drawer / ToolBar / StatusBar.
    // Each widget picks its own section in its ApplyTheme; field meanings are
    // the same.
    struct SurfaceStyle {
        Color4f background{ 0.0f, 0.0f, 0.0f, 0.0f };   // Background
        Color4f borderColor{ 0.0f, 0.0f, 0.0f, 0.0f };  // Border color
        float   borderWidth{ 0.0f };                    // Border width (0 = none)
        float   radius{ 0.0f };                         // Radius (pixels)
        float   padding{ 0.0f };                        // Uniform padding (all sides)
        Color4f dividerColor{ 0.0f, 0.0f, 0.0f, 0.0f }; // Internal divider color (under header / above status bar / right of toolbar)
        float   dividerWidth{ 1.0f };                   // Divider width

        TextStyle title;   // Title row text (card title / collapse header / sidebar title / toolbar group title)
        TextStyle body;    // Body / status text (status bar segment text)
    };

    // Number box style (ZeNumberBox.h): the input section plus two step
    // buttons.
    //
    // The step buttons' look comes in two sets, do not mix them:
    //   - The square background follows the six states in stepBox.colors
    //     (fully transparent at rest, appearing only on hover / press, so the
    //     right side of the input does not show two permanent gray bumps);
    //   - The +/- glyph colors are the three single values below (they are not
    //     part of the six states because they sit on top of those state
    //     backgrounds and need their own contrast guarantee).
    struct NumberBoxStyle {
        BoxStyle  box;                 // Input area box
        TextStyle text;                // Number text
        BoxStyle  stepButton;          // Step button square (six-state background, transparent at rest)
        TextStyle stepGlyph;           // Step button font (size and weight of `+` / `-`)

        Color4f   stepColor{ 0.0f, 0.0f, 0.0f, 0.0f };        // Glyph color (rest)
        Color4f   stepHoverColor{ 0.0f, 0.0f, 0.0f, 0.0f };   // Glyph color (hover)
        Color4f   stepPressedColor{ 0.0f, 0.0f, 0.0f, 0.0f }; // Glyph color (pressed)
        float     buttonWidth{ 22.0f };  // One-side button width (pixels)
    };

    // Date-time picker style (ZeDateTimePicker.h).
    //
    // Two parts: collapsed is an input box with a calendar icon (`box` /
    // `text`); expanded is a calendar popup (the `panel*` family + various
    // grid colors).
    //
    // Popup colors are single values keyed to cell semantics (today /
    // selected / disabled / other-month), not six-state interaction states;
    // those are two orthogonal things. Hover gets a separate `dayHoverColor`
    // overlay.
    struct DatePickerStyle {
        BoxStyle  box;                  // Collapsed input box
        TextStyle text;                 // Collapsed text (date string)
        BoxStyle  panel;                // Popup box (background / border / shadow)
        TextStyle head;                 // Popup header (year / month + paging arrows)
        TextStyle weekday;              // Weekday header row
        TextStyle day;                  // Day numbers
        TextStyle value;                // Time numbers + button text ("Today" / "Cancel" / "OK")
        float     dayCellHeight{ 30.0f };   // Day cell height
        float     dayCellWidth{ 34.0f };    // Day cell width
        float     headerHeight{ 34.0f };    // Popup header height
        float     gap{ 4.0f };              // Gap between popup and widget (pixels)
        Color4f   weekHeaderColor{ 0.0f, 0.0f, 0.0f, 0.0f };  // Weekday header area background
        Color4f   dayHoverColor{ 0.0f, 0.0f, 0.0f, 0.0f };    // Day cell hover background
        Color4f   daySelectedColor{ 0.0f, 0.0f, 0.0f, 0.0f }; // Selected day background
        Color4f   dayTodayColor{ 0.0f, 0.0f, 0.0f, 0.0f };    // "Today" accent (outline / text)
        Color4f   dayMutedColor{ 0.0f, 0.0f, 0.0f, 0.0f };    // Text color for other-month / disabled days
        Color4f   arrowColor{ 0.0f, 0.0f, 0.0f, 0.0f };       // Paging arrow color
        Color4f   arrowHoverColor{ 0.0f, 0.0f, 0.0f, 0.0f };  // Paging arrow hover color
    };

    // Color picker style (ZeColorPicker.h).
    //
    // Three parts: the palette (SV plane + hue strip), the preview / hex row,
    // and the popup skeleton. The palette's gradients are computed (HSV to
    // RGB); the theme only provides border and cursor colors.
    struct ColorPickerStyle {
        BoxStyle  box;                  // Collapsed swatch box
        TextStyle text;                 // Collapsed hex text
        BoxStyle  panel;                // Popup box
        TextStyle label;                // Channel labels (R / G / B / Hex)
        TextStyle value;                // Channel value text
        float     gap{ 8.0f };          // Gap between popup and widget (pixels)
        float     padding{ 10.0f };     // Popup padding
        float     svHeight{ 130.0f };   // Palette (SV plane) height
        float     hueHeight{ 14.0f };   // Hue strip height
        float     channelHeight{ 22.0f }; // Per-channel row height
        float     swatchHeight{ 26.0f };  // Height of the top current/preview color strip
        float     radius{ 4.0f };       // Palette / hue strip radius
        Color4f   borderColor{ 0.0f, 0.0f, 0.0f, 0.0f };      // Palette outer border
        Color4f   cursorColor{ 1.0f, 1.0f, 1.0f, 1.0f };      // Palette cursor (white ring)
        Color4f   cursorShadowColor{ 0.0f, 0.0f, 0.0f, 0.6f }; // Cursor outer ring (dark edge, visible on light areas too)
        Color4f   trackColor{ 0.0f, 0.0f, 0.0f, 0.0f };       // Channel slider track color
        Color4f   thumbColor{ 1.0f, 1.0f, 1.0f, 1.0f };       // Channel slider thumb color
    };

    // Collapse panel style (ZeCollapse.h).
    //
    // A collapse panel = several collapse items, each = a clickable header row
    // + a content area. Header colors come as three single values keyed to
    // expanded / hover, not six states, same idea as TabStyle for tabs (the
    // visual meaning is "selected", not hover).
    struct CollapseStyle {
        BoxStyle  panel;                // Whole panel box (outer frame)

        TextStyle head;                 // Header text (collapsed)
        TextStyle headActive;           // Header text (expanded)
        TextStyle headDisabled;         // Header text (disabled item)

        float     headHeight{ 36.0f };  // Header row height
        float     itemGap{ 0.0f };      // Gap between items
        float     contentPadding{ 12.0f }; // Content area padding (all sides)
        float     indicatorSize{ 5.0f };   // Expand arrow size
        float     indicatorThickness{ 1.6f }; // Expand arrow line width

        Color4f   headColor{ 0.0f, 0.0f, 0.0f, 0.0f };        // Header background (rest)
        Color4f   headHoverColor{ 0.0f, 0.0f, 0.0f, 0.0f };   // Header background (hover)
        Color4f   headActiveColor{ 0.0f, 0.0f, 0.0f, 0.0f };  // Header background (expanded)
        Color4f   dividerColor{ 0.0f, 0.0f, 0.0f, 0.0f };     // Divider between items
        Color4f   indicatorColor{ 0.0f, 0.0f, 0.0f, 0.0f };   // Expand arrow color
        float     radius{ 0.0f };       // Header / panel radius
    };

    // Shared style for breadcrumb / pagination / step strip (the
    // ZeItemStripBase.h family).
    //
    // These three widgets share one skeleton: a row of discrete items that are
    // clickable, hoverable, and have a current item. Only "what an item looks
    // like" differs (breadcrumb: text + slashes; pagination: square numbers;
    // steps: circles + connectors), and that is laid out in each widget's
    // Paint. The shared section holds only what all three need: item text,
    // item background (rest / hover), current-item accent color, and item
    // spacing.
    //
    // Same judgment as CheckboxStyle serving both Checkbox and RadioButton.
    struct ItemStripStyle {
        TextStyle item;                 // Item text (rest)
        TextStyle itemActive;           // Item text (current / selected item)

        float     gap{ 6.0f };          // Horizontal gap between items
        float     itemPaddingX{ 10.0f };// Horizontal padding inside an item
        float     itemHeight{ 28.0f };  // Item height (basis for pagination squares / step dots)

        Color4f   itemHoverColor{ 0.0f, 0.0f, 0.0f, 0.0f };    // Item hover background
        Color4f   itemActiveColor{ 0.0f, 0.0f, 0.0f, 0.0f };   // Current item background (pagination solid block / step dot)
        Color4f   separatorColor{ 0.0f, 0.0f, 0.0f, 0.0f };    // Separator color (breadcrumb `/`, pagination `...`, step connectors)
        Color4f   disabledColor{ 0.0f, 0.0f, 0.0f, 0.0f };     // Disabled items (pagination prev/next unavailable)
        float     radius{ 0.0f };       // Item background radius
    };

    // Divider style (ZeDivider.h).
    // A divider is a thin line (optionally with text in the middle). Color /
    // width / text are all single values: it is not interactive (no hover /
    // press semantics), so only Normal is ever read from the six-state table.
    struct DividerStyle {
        Color4f   color{ 0.0f, 0.0f, 0.0f, 0.0f };   // Line color
        float     thickness{ 1.0f };                  // Line width (pixels)
        float     gap{ 8.0f };                        // Space around the text (horizontal / vertical by orientation)
        TextStyle label;                              // Optional middle text
    };

    // Badge style (ZeBadge.h).
    // Five semantic colors (primary / danger / success / warning / neutral),
    // each a pair of background + text color. Single values, not six states:
    // a badge's meaning is what it represents (unread / error / online...),
    // not "the mouse is over it".
    struct BadgeStyle {
        TextStyle text;                       // Badge text (smaller + bold + centered)

        float     minWidth{ 18.0f };          // Minimum width (single-digit counts still look round)
        float     height{ 18.0f };            // Height
        float     paddingX{ 6.0f };           // Horizontal padding around text
        float     radius{ 9.0f };             // Radius (capsule by default)
        float     dotSize{ 8.0f };            // Diameter of the dot form

        Color4f   primaryBackground{ 0.0f, 0.0f, 0.0f, 0.0f };
        Color4f   dangerBackground{ 0.0f, 0.0f, 0.0f, 0.0f };
        Color4f   successBackground{ 0.0f, 0.0f, 0.0f, 0.0f };
        Color4f   warningBackground{ 0.0f, 0.0f, 0.0f, 0.0f };
        Color4f   neutralBackground{ 0.0f, 0.0f, 0.0f, 0.0f };

        Color4f   primaryText{ 1.0f, 1.0f, 1.0f, 1.0f };
        Color4f   dangerText{ 1.0f, 1.0f, 1.0f, 1.0f };
        Color4f   successText{ 1.0f, 1.0f, 1.0f, 1.0f };
        Color4f   warningText{ 1.0f, 1.0f, 1.0f, 1.0f };
        Color4f   neutralText{ 0.0f, 0.0f, 0.0f, 1.0f };
    };

    // Avatar style (ZeAvatar.h).
    // An avatar = a square/circle base + (image or initials) + optional status
    // dot. `palette` holds candidate colors for "color by name when there is
    // no image", so everyone in a roster gets a stable, distinct background
    // without introducing new hues (all derived from existing tokens).
    struct AvatarStyle {
        TextStyle initials;                             // Initials text when there is no image

        float     borderWidth{ 2.0f };                  // Outer ring stroke (lifts the avatar off the background)
        Color4f   borderColor{ 0.0f, 0.0f, 0.0f, 0.0f };

        Color4f   fallback{ 0.0f, 0.0f, 0.0f, 0.0f };   // Fallback background when the palette misses

        float     statusSize{ 10.0f };                  // Status dot diameter
        float     statusBorderWidth{ 2.0f };            // Status dot stroke (same color as the avatar base, "punching out" a ring)
        Color4f   statusBorderColor{ 0.0f, 0.0f, 0.0f, 0.0f };
        Color4f   statusOnline{ 0.0f, 0.0f, 0.0f, 0.0f };
        Color4f   statusOffline{ 0.0f, 0.0f, 0.0f, 0.0f };
        Color4f   statusBusy{ 0.0f, 0.0f, 0.0f, 0.0f };
        Color4f   statusAway{ 0.0f, 0.0f, 0.0f, 0.0f };

        Color4f   palette[6]{};
    };

    // Icon style (ZeIcon.h).
    // Built-in glyphs are drawn with strokes, so color / thickness / size each
    // get a single value.
    struct IconStyle {
        Color4f color{ 0.0f, 0.0f, 0.0f, 1.0f };   // Glyph color
        float   size{ 16.0f };                     // Glyph bounding square side
        float   stroke{ 1.6f };                    // Stroke thickness
    };

    // Image style (ZeImage.h).
    // The box only handles the "frame" (background / border / radius /
    // padding); the image itself goes through Image's value interface.
    struct ImageStyle {
        BoxStyle box;                                 // Frame (background / border / radius / padding)
        Color4f  placeholderColor{ 0.0f, 0.0f, 0.0f, 0.0f };  // Placeholder background when there is no image
        Color4f  placeholderGlyph{ 0.0f, 0.0f, 0.0f, 0.0f };  // Placeholder icon (mountain / slash) color
    };

    // Native canvas style (ZeCanvasView.h).
    //
    // Only three fields, and they are barely visible: once the canvas is
    // covered by the external renderer's sub-window, these only show on the
    // first frame before the sub-window exists and while the canvas is
    // hidden. Their purpose is "don't look like a hole when exposed": a
    // region matching the background + a thin border.
    //
    // Do not expect to color the canvas through the theme: the drawn content
    // belongs to the external renderer and is out of the theme's reach.
    struct CanvasStyle {
        Color4f background{ 0.0f, 0.0f, 0.0f, 0.0f };   // Placeholder background
        Color4f borderColor{ 0.0f, 0.0f, 0.0f, 0.0f };  // Canvas border color
        float   borderWidth{ 1.0f };                     // Border width (0 = none)
    };

    // Image previewer style (ZeImagePreview.h).
    //
    // The previewer is a dark overlay covering the whole window: a scrim dims
    // the screen, the image is centered, and top/bottom translucent info bars
    // hold the title and caption.
    //
    // Why the info bar background deliberately does not follow `surface`: it
    // is painted over arbitrary images, and only translucent black + white
    // text stays readable on both bright and dark images. A themed surface
    // would turn into a "white strip over a white image" in light themes.
    //
    // `bar` is the shared box for both info bars (background + radius +
    // padding); each bar's height is set separately (the top bar holds the
    // title and close button and is usually taller).
    struct ImagePreviewStyle {
        BoxStyle  bar;                                        // Top / bottom info bar
        TextStyle title;                                      // Top bar title
        TextStyle info;                                       // Bottom bar caption / index

        Color4f scrim{ 0.0f, 0.0f, 0.0f, 0.0f };              // Whole-window scrim color (alpha = dimming)
        Color4f controlGlyph{ 0.0f, 0.0f, 0.0f, 0.0f };       // Close button / paging arrow glyph color
        Color4f controlHoverBg{ 0.0f, 0.0f, 0.0f, 0.0f };     // Hover background of those controls

        float topBarHeight{ 44.0f };                          // Top bar height (0 = none)
        float bottomBarHeight{ 30.0f };                       // Bottom bar height (0 = none)
    };

    // Group panel style (ZeGroupPanel.h).
    //
    // A group panel is a container that "fences in a block of content and
    // names it": outer frame + title sitting on the frame. The difference
    // from a card (CardStyle): a card expresses "floating" with a shadow, a
    // group expresses "enclosure" with a border, and the title sits on the
    // frame line (fieldset look).
    struct GroupPanelStyle {
        Color4f   background{ 0.0f, 0.0f, 0.0f, 0.0f };   // Background (must be opaque; the title "sits on the line" by masking a gap)
        Color4f   borderColor{ 0.0f, 0.0f, 0.0f, 0.0f };
        float     borderWidth{ 1.0f };
        float     radius{ 0.0f };
        float     padding{ 12.0f };                       // Content area padding (all sides)
        float     titleHeight{ 24.0f };                   // Title row height (content starts below it)
        float     titlePaddingX{ 6.0f };                  // Horizontal padding around title text
        TextStyle title;
    };

    // Time picker style (ZeTimePicker.h).
    //
    // The collapsed state reuses the `input` section (a picker looks like an
    // input box); expanded is a floating popup with several columns (hour /
    // minute / second), each a list of numbers. Column colors are single
    // values keyed to number semantics (rest / hover / selected), same
    // reasoning as calendar cells.
    struct TimePickerStyle {
        BoxStyle  box;                                  // Collapsed input box
        TextStyle text;                                 // Collapsed text (time string)

        BoxStyle  panel;                                // Popup box (background / border / shadow)
        TextStyle head;                                 // Column titles (hour / minute / second)
        TextStyle value;                                // Numbers in the columns
        TextStyle action;                               // Bottom action text (now / OK)

        float     itemHeight{ 26.0f };                  // Number row height
        float     columnWidth{ 46.0f };                 // Column width
        float     headerHeight{ 22.0f };                // Column title row height
        float     footerHeight{ 28.0f };                // Bottom action row height (0 = hidden)
        float     gap{ 4.0f };                          // Gap between popup and widget
        float     radius{ 0.0f };                       // Radius of hover / selected rows

        Color4f   itemHoverColor{ 0.0f, 0.0f, 0.0f, 0.0f };
        Color4f   itemSelectedColor{ 0.0f, 0.0f, 0.0f, 0.0f };
        Color4f   selectedTextColor{ 1.0f, 1.0f, 1.0f, 1.0f };
        Color4f   arrowColor{ 0.0f, 0.0f, 0.0f, 0.0f };
        Color4f   dividerColor{ 0.0f, 0.0f, 0.0f, 0.0f };
    };

    // Rating style (ZeRating.h).
    //
    // Stars come in three colors, all single values: `activeColor` (selected
    // stars) / `inactiveColor` (empty stars) / `hoverColor` (preview while
    // hovering). Same reasoning as the checkbox: the rating's visual meaning
    // is "how many stars were given", not a transient hover / press state.
    struct RatingStyle {
        Color4f   activeColor{ 0.0f, 0.0f, 0.0f, 0.0f };    // Selected stars
        Color4f   inactiveColor{ 0.0f, 0.0f, 0.0f, 0.0f };  // Unselected stars
        Color4f   hoverColor{ 0.0f, 0.0f, 0.0f, 0.0f };     // Hover preview color (transparent = none)
        float     starSize{ 22.0f };                        // Bounding square side of one star
        float     gap{ 2.0f };                              // Gap between stars
        float     hoverScale{ 1.06f };                      // Scale factor on hover preview (1 = no scaling)
    };

    // Switch style (ZeSwitch.h).
    //
    // A switch = a capsule track + a round knob. The track has two shades for
    // on / off (single values, no six-state table: a switch's meaning is its
    // state); the knob has one color; plus a label text style on the right.
    struct SwitchStyle {
        Color4f   onColor{ 0.0f, 0.0f, 0.0f, 0.0f };       // Track (on)
        Color4f   offColor{ 0.0f, 0.0f, 0.0f, 0.0f };      // Track (off)
        Color4f   onDisabledColor{ 0.0f, 0.0f, 0.0f, 0.0f };   // Track (on + disabled)
        Color4f   offDisabledColor{ 0.0f, 0.0f, 0.0f, 0.0f };  // Track (off + disabled)
        Color4f   knobColor{ 1.0f, 1.0f, 1.0f, 1.0f };     // Knob
        Color4f   knobDisabledColor{ 1.0f, 1.0f, 1.0f, 1.0f };

        float     trackWidth{ 44.0f };      // Track width
        float     trackHeight{ 24.0f };     // Track height
        float     knobInset{ 2.0f };        // Gap between knob and track edge
        float     textGap{ 8.0f };          // Gap between track and right label

        TextStyle text;                     // Right label text (empty label takes no space)
    };

    // Watermark style (ZeWatermark.h).
    //
    // A watermark is repeating text (or an image) tiled over the whole
    // container. Color + opacity are merged into one `color` value (the alpha
    // is the intensity); splitting into two fields would only make people
    // guess which one applies.
    //
    // Note: the library has no rotated-text channel (FrameCollector::AddText
    // takes no angle), so watermark text can only repeat horizontally and
    // cannot produce the common 45-degree slant.
    struct WatermarkStyle {
        TextStyle text;                                  // Watermark text (size + weight; color at color)
        Color4f   color{ 0.0f, 0.0f, 0.0f, 0.12f };      // Watermark color (alpha = intensity)
        Color4f   imageTint{ 1.0f, 1.0f, 1.0f, 1.0f };   // Image watermark tint (white = unchanged)
        float     gapX{ 96.0f };                         // Horizontal gap between tiles
        float     gapY{ 64.0f };                         // Vertical gap between tiles
        float     offsetX{ 0.0f };                       // First-tile offset (to shift the grid)
        float     offsetY{ 0.0f };
    };

    // Loading animation style (ZeLoading.h).
    //
    // Three forms (ring / dots / bar) share this section: one foreground
    // color, one track color (the ring's base circle), one size and
    // thickness, plus an optional line of text at bottom right.
    struct LoadingStyle {
        TextStyle text;                                  // Optional text at right / below
        Color4f   color{ 0.0f, 0.0f, 0.0f, 1.0f };       // Foreground (spinning arc / dots / bar)
        Color4f   trackColor{ 0.0f, 0.0f, 0.0f, 0.0f };  // Track color (ring only, transparent = none)
        float     size{ 28.0f };                         // Bounding square side
        float     thickness{ 3.0f };                     // Ring width / dot diameter / bar width
        float     textGap{ 8.0f };                       // Gap between indicator and text
        int       itemCount{ 8 };                        // Number of dots / bars (ring only cares about arcs > 2)
        int       speedMs{ 900 };                        // Milliseconds per revolution
    };

    // File drop zone style (ZeFileDropZone.h).
    //
    // Two states: rest (`box` / `text` / `subText`) and drag-over
    // (`activeBox` / `activeText`). Drag-over means "release to place here",
    // so it uses the primary color, not a warning color: this is "allowed",
    // not "dangerous".
    //
    // Why the sub-hint gets its own TextStyle instead of multiplying by 0.85
    // in the widget: once the size derivation chain hides inside code, theme
    // authors can no longer adjust it (a different gray for the sub-hint, or
    // bolding it, both become impossible). Styles live in the theme; that rule
    // has held since day one.
    struct FileDropZoneStyle {
        BoxStyle  box;        // Rest box: light background + 1px border + radius ("this is open ground")
        TextStyle text;       // Main hint
        TextStyle subText;    // Sub hint (smaller / fainter)
        BoxStyle  activeBox;  // Drag-over box: primary border + light primary background
        Color4f   activeText{ 0.0f, 0.0f, 0.0f, 1.0f };  // Drag-over text color (usually = primary)
    };

    // Code editor style (ZeCodeEditor.h).
    //
    // The code editor shares its origin with the input box (same background /
    // border / focus outline, same font), so `box` / `text` inherit the
    // `input` section directly in Derive; to adjust only the code area,
    // change these two without touching plain input boxes.
    //
    // The seven extra `tokenXxx` colors are syntax highlighting semantic
    // colors, and they must live in the theme. Lesson learned the hard way:
    //
    // Why token colors cannot live in the widget: previously CodeEditor wrote
    // keyword / string / identifier colors as construction-time constants
    // (identifiers were #0F172A, near-black). When the theme switched to
    // dark, ApplyTheme only re-injected the Default and Comment classes, so
    // the identifiers, which make up most of the text, stayed near-black on a
    // dark background, i.e. invisible. On a light background the same colors
    // were fine, so the problem only appeared after a theme switch and was
    // hard to attribute.
    //
    // Conclusion: coloring is theme data, not widget constants. Derive pushes
    // each semantic color to a readable brightness based on the background
    // lightness (see Themes::ReadableOn), so a new theme never needs a new
    // code palette.
    struct CodeStyle {
        BoxStyle  box;                                   // Code area box (inherits `input.box` by default)
        TextStyle text;                                  // Body (inherits `input.text` by default, always top-left aligned)

        Color4f   tokenDefault{ 0.0588f, 0.0902f, 0.1647f, 1.0f };     // Plain text (#0F172A)
        Color4f   tokenKeyword{ 0.4863f, 0.2275f, 0.9294f, 1.0f };     // Keyword (#7C3AED)
        Color4f   tokenString{ 0.0863f, 0.6392f, 0.2902f, 1.0f };      // String (#16A34A)
        Color4f   tokenNumber{ 0.9176f, 0.3451f, 0.0471f, 1.0f };      // Number (#EA580C)
        Color4f   tokenComment{ 0.3922f, 0.4549f, 0.5451f, 1.0f };     // Comment (#64748B)
        Color4f   tokenPreprocessor{ 0.1451f, 0.3882f, 0.9216f, 1.0f };// Preprocessor directive (#2563EB)
        Color4f   tokenIdentifier{ 0.0588f, 0.0902f, 0.1647f, 1.0f };  // Identifier (#0F172A)
    };

    // ===================================================================
    //  Theme
    // ===================================================================

    struct Theme {
        std::string name = "custom";   // Theme name (built-ins all carry one; SetThemeByName looks it up)

        // ---- Color tokens ----
        Color4f bg = Color4f::FromHex(0xF8FAFC);
        Color4f surface = Color4f::FromHex(0xFFFFFF);
        Color4f border = Color4f::FromHex(0xE2E8F0);
        Color4f text = Color4f::FromHex(0x0F172A);
        Color4f muted = Color4f::FromHex(0x64748B);
        Color4f disabled = Color4f::FromHex(0x94A3B8);

        Color4f primary = Color4f::FromHex(0x2563EB);
        Color4f primaryHover = Color4f::FromHex(0x1D4ED8);
        Color4f primaryPress = Color4f::FromHex(0x1E40AF);
        Color4f onPrimary = Color4f::White();

        Color4f danger = Color4f::FromHex(0xDC2626);
        Color4f success = Color4f::FromHex(0x16A34A);
        Color4f warning = Color4f::FromHex(0xD97706);

        // ---- Shadow ----
        //  Shadow base color for every "floating thing" in the library (popups
        //  / menus / bubbles / toasts / dialogs). These used to be hard-coded
        //  black in Derive, which made it impossible to tint the shadow for
        //  dark themes (e.g. a cool bluish shadow). Alpha differs per widget
        //  (0.10~0.28), so only the hue is given here; Derive picks each
        //  widget's intensity with withAlpha().
        Color4f shadow{ 0.0f, 0.0f, 0.0f, 1.0f };

        // ---- Scrollbar ----
        //  Derived from tokens (see Derive): changing border/muted in a dark
        //  theme updates these automatically, no need to rewrite them in
        //  Dark(). The initial values here only exist so the scrollbar is
        //  visible even without running Derive.
        Color4f scrollbarTrack = Color4f::FromHex(0xE2E8F0);   // Track
        Color4f scrollbarThumb = Color4f::FromHex(0x94A3B8);   // Thumb

        // ---- Size tokens ----
        float radiusS = 4.0f, radiusM = 8.0f, radiusL = 12.0f;
        float spaceS = 8.0f, spaceM = 16.0f, spaceL = 24.0f;
        float ctrlHS = 28.0f, ctrlHM = 36.0f, ctrlHL = 44.0f;

        // ---- Font size tokens ----
        unsigned fontS = 12, fontM = 14, fontL = 16, fontXL = 20;

        // ---- Component styles ----
        ButtonStyle button;
        CardStyle   card;
        LabelStyle  label;
        InputStyle  input;
        BoxStyle    plot;      // Canvas (PlotView): box only, no text style (label colors follow the data)
        ProgressStyle progress; // Progress bar
        SliderStyle   slider;   // Slider
        CheckboxStyle checkbox; // Checkbox / radio (shared section)
        ComboStyle    combo;    // Combo box (collapsed panel + expanded popup)
        MenuStyle     menu;     // Menu modal (panel + menu items + shortcuts)
        TooltipStyle  tooltip;  // Tooltip (hover bubble)
        ToastStyle    toast;    // Toast (floating strip + semantic color bar)
        MenuBarStyle  menubar;  // Menu bar (VS-style custom menu bar)
        SplitterStyle splitter; // Splitter (draggable handle)
        ListStyle     list;     // List (rows + container skeleton)
        TabStyle      tab;      // Tabs (tab bar + tab state colors)
        TableStyle    table;    // Table (fixed header + data rows)
        TreeStyle     tree;     // Tree (indented rows + expand indicator)
        SubWindowStyle subwindow; // Sub-window / modal dialog
        ModalDialogStyle modalDialog; // Modal dialog (Kit::ModalDialog, self-drawn panel)

        // ---- 2026-10: theme sections for a batch of new widgets ----
        //  These containers share the same skeleton (background + border +
        //  radius + one title row), so they share SurfaceStyle's field
        //  semantics, but each occupies its own section, so tuning one (e.g.
        //  darkening the status bar one step) does not touch the others. The
        //  card uses CardStyle (extra title area and shadow, see above); the
        //  collapse panel uses CollapseStyle (its header needs expanded /
        //  hover states, more than the skeleton).
        SurfaceStyle    sidebar;      // Sidebar
        SurfaceStyle    drawer;       // Drawer
        SurfaceStyle    toolbar;      // Toolbar
        SurfaceStyle    statusBar;    // Status bar

        CodeStyle       code;         // Code editor (box/text inherit input, plus seven syntax token colors)

        NumberBoxStyle  numberBox;    // Number box
        DatePickerStyle datePicker;   // Date-time picker
        ColorPickerStyle colorPicker; // Color picker
        CollapseStyle   collapse;     // Collapse panel
        ItemStripStyle  itemStrip;    // Breadcrumb / pagination / step strip (shared)

        // ---- 2026-10-14: a batch of lightweight display widgets + time picker ----
        DividerStyle    divider;      // Divider
        BadgeStyle      badge;        // Badge
        AvatarStyle     avatar;       // Avatar
        IconStyle       icon;         // Icon
        ImageStyle      image;        // Image
        ImagePreviewStyle imagePreview; // Image previewer (whole-window overlay: scrim + top/bottom info bars)
        CanvasStyle     canvas;       // Native canvas (drawn by the external renderer; Kit only paints a placeholder)
        GroupPanelStyle groupPanel;   // Group panel
        TimePickerStyle timePicker;   // Time picker (time only)

        // ---- 2026-10-15: dock panel ----
        DockPanelStyle  dockPanel;    // Dock panel (grid docking + dragging + draggable splitters)

        // ---- 2026-10-14 (second batch): rating / switch / watermark / loading ----
        RatingStyle     rating;       // Rating
        //  The field cannot be named `switch` (a C++ keyword, also unusable as
        //  a member name), so it is called `toggleSwitch` even though the
        //  class and the type are both named Switch.
        SwitchStyle     toggleSwitch; // Switch
        WatermarkStyle  watermark;    // Watermark
        LoadingStyle    loading;      // Loading animation

        // ---- 2026-10-28: file drop zone ----
        FileDropZoneStyle fileDropZone;  // File drop zone (Kit::FileDropZone)
    };

    // ===================================================================
    //  Built-in themes
    // ===================================================================

    namespace Themes {

        // -------------------------------------------------------------------
        //  Brightness / readability helpers, for deriving colors that "must
        //  be readable"
        // -------------------------------------------------------------------

        // Relative luminance (0 = black, 1 = white).
        // The usual Rec.601 weighting; no sRGB linearization because this only
        // answers "is this background dark or light, is this text bright
        // enough", where the precision is more than enough.
        inline float Luminance(const Color4f& c) {
            return 0.299f * c.r + 0.587f * c.g + 0.114f * c.b;
        }

        // Adjust `fg` until it reads clearly on `surface`, keeping its hue.
        //
        // The rule is simple: pass if |luminance(fg) - luminance(surface)| >=
        // minDelta; otherwise blend toward white (dark background) or black
        // (light background) in 1/16 steps and take the first passing result.
        //
        // Why hue-preserving blending instead of two hard-coded palettes:
        //   - Two hard-coded sets = one more palette to maintain per theme,
        //     and something will be missed eventually (CodeEditor's black text
        //     came from exactly that);
        //   - Blending only luminance keeps green green and purple purple,
        //     automatically becoming "one step brighter green/purple" on dark
        //     backgrounds, the standard practice in editor circles.
        //
        // fg: original color (usually a semantic color); surface: the
        // background it will sit on; minDelta: minimum luminance difference
        // (0.40 is the usual contrast in light themes; comments can go
        // lower). Returns the adjusted color (alpha preserved).
        inline Color4f ReadableOn(const Color4f& fg, const Color4f& surface,
            float minDelta = 0.40f) {
            const float sl = Luminance(surface);
            const bool  dark = (sl < 0.5f);
            const Color4f pole(dark ? 1.0f : 0.0f, dark ? 1.0f : 0.0f,
                dark ? 1.0f : 0.0f, fg.a);
            constexpr int kSteps = 16;
            Color4f c = fg;
            for (int i = 0; i < kSteps; ++i) {
                const float d = Luminance(c) - sl;
                if ((d < 0.0f ? -d : d) >= minDelta) break;
                c = fg.mix(pole, static_cast<float>(i + 1) / static_cast<float>(kSteps));
            }
            c.a = fg.a;
            return c;
        }

        // Re-derive all component styles from the current tokens.
        inline void Derive(Theme& t) {
            // ---- Scrollbar (derived tokens: computed from existing tokens, no hard-coded colors) ----
            //  Translucent: the scrollbar sits on top of content and should
            //  not steal visual attention.
            t.scrollbarTrack = t.border;
            t.scrollbarTrack.a = 0.55f;
            t.scrollbarThumb = t.muted;
            t.scrollbarThumb.a = 0.70f;

            // ---- Button ----
            t.button.box.border.width[Border::Top] =
                t.button.box.border.width[Border::Right] =
                t.button.box.border.width[Border::Bottom] =
                t.button.box.border.width[Border::Left] = 1.0f;

            t.button.box.padding.left = 12.0f;
            t.button.box.padding.right = 12.0f;
            t.button.box.padding.top = 6.0f;
            t.button.box.padding.bottom = 6.0f;

            t.button.box.radiusMode = RadiusMode::Normal;

            // Six-state colors.
            //  Keep the sets separate: background/border go into the box's set
            //  (box.colors), text color into the text's set (text.colors);
            //  widgets also read them separately (BoxWidget::resolvedColors /
            //  TextWidget::resolvedTextColors). Text colors used to be written
            //  into box.colors and still showed because text painting
            //  accidentally read the box's slot, which broke SetTextColor.
            //  Fixed.
            auto setBtn = [&](WidgetState s, Color4f bg, Color4f bd, Color4f fg) {
                ColorSet& c = t.button.box.colors.get(s);
                c.background = bg;
                c.border = bd;
                t.button.text.colors.get(s).text = fg;
                };
            setBtn(WidgetState::Normal, t.primary, t.primaryHover, t.onPrimary);
            setBtn(WidgetState::Hover, t.primaryHover, t.primaryPress, t.onPrimary);
            setBtn(WidgetState::Pressed, t.primaryPress, t.primaryPress, t.onPrimary);
            setBtn(WidgetState::Disabled, t.disabled, t.disabled, t.muted);
            setBtn(WidgetState::Focused, t.primary, t.primaryPress, t.onPrimary);
            setBtn(WidgetState::Loading, t.disabled, t.disabled, t.muted);

            // Font
            t.button.text.font.pixelSize = t.fontM;
            t.button.text.font.weight = FontWeight::Normal;
            t.button.text.hAlign = TextHAlign::Center;
            t.button.text.vAlign = TextVAlign::Middle;

            // ---- Card ----
            t.card.box.colors.get(WidgetState::Normal).background = t.surface;
            t.card.box.colors.get(WidgetState::Normal).border = t.border;
            t.card.box.border.width[Border::Top] =
                t.card.box.border.width[Border::Right] =
                t.card.box.border.width[Border::Bottom] =
                t.card.box.border.width[Border::Left] = 1.0f;
            t.card.box.radiusMode = RadiusMode::Normal;
            t.card.box.padding = { 12.0f, 12.0f, 12.0f, 12.0f };

            // ---- Label ----
            //  Pure text: only the text set needs configuring (the box set
            //  stays all default: no background, border, or padding).
            t.label.text.font.pixelSize = t.fontM;
            t.label.text.colors.get(WidgetState::Normal).text = t.text;
            t.label.text.colors.get(WidgetState::Disabled).text = t.muted;   // Grayed out when disabled
            t.label.text.hAlign = TextHAlign::Left;
            t.label.text.vAlign = TextVAlign::Middle;

            // ---- Input ----
            //  Box: surface background + border; the border turns primary on
            //  focus (the payoff of six-state colors). Text: the text token
            //  normally, grayed out when disabled. Alignment: always left +
            //  vertically centered.
            t.input.box.colors.get(WidgetState::Normal).background = t.surface;
            t.input.box.colors.get(WidgetState::Normal).border = t.border;
            t.input.box.colors.get(WidgetState::Hover).background = t.surface;
            t.input.box.colors.get(WidgetState::Hover).border = t.border;
            t.input.box.colors.get(WidgetState::Focused).background = t.surface;
            t.input.box.colors.get(WidgetState::Focused).border = t.primary;
            t.input.box.colors.get(WidgetState::Disabled).background = t.bg;
            t.input.box.colors.get(WidgetState::Disabled).border = t.border;
            t.input.box.border.width[Border::Top] =
                t.input.box.border.width[Border::Right] =
                t.input.box.border.width[Border::Bottom] =
                t.input.box.border.width[Border::Left] = 1.0f;
            t.input.box.radiusMode = RadiusMode::Normal;
            t.input.box.padding = { 8.0f, 6.0f, 8.0f, 6.0f };   // left, top, right, bottom
            t.input.text.font.pixelSize = t.fontM;
            t.input.text.colors.get(WidgetState::Normal).text = t.text;
            t.input.text.colors.get(WidgetState::Disabled).text = t.muted;
            t.input.text.hAlign = TextHAlign::Left;
            t.input.text.vAlign = TextVAlign::Middle;

            // ---- Plot (canvas) ----
            //  Padding must be 0: vertex coordinates are content-area
            //  coordinates; any padding would misalign the plot origin with
            //  the data (leave room for tick text in the data instead).
            //  No radius: shapes are clipped to the rectangular bounds;
            //  rounded corners would bite off the corners of primitives.
            t.plot.colors.get(WidgetState::Normal).background = t.surface;
            t.plot.colors.get(WidgetState::Normal).border = t.border;
            t.plot.border.width[Border::Top] =
                t.plot.border.width[Border::Right] =
                t.plot.border.width[Border::Bottom] =
                t.plot.border.width[Border::Left] = 1.0f;
            t.plot.radiusMode = RadiusMode::None;
            t.plot.padding = Padding{};

            // ---- Progress (progress bar) ----
            //  Two rounded rectangles: the track fills the whole strip, the
            //  fill covers it proportionally. Track / fill colors are single
            //  values (no six-state table) since the default is read-only
            //  display with no hover / press semantics; only the draggable
            //  handle gets a third piece whose outline needs six states.
            //
            //  The track uses the border family (light gray), the fill uses
            //  primary (accent), matching the "completed" semantics; changing
            //  border / primary in a dark theme propagates automatically, no
            //  rewrite in Dark() needed.
            //
            //  Padding 0: the track is the content area itself (padding would
            //  leave a ring of background between fill and track at 100%,
            //  looking unfinished).
            //  Capsule radius: half the short side — a 12px-tall bar naturally
            //  becomes a capsule, and smaller heights degrade to square
            //  (radius clamped to 0) without drawing wrong.
            t.progress.trackColor = t.border;
            t.progress.fillColor = t.primary;
            t.progress.track.border = Border{};
            t.progress.track.padding = Padding{};
            t.progress.track.radiusMode = RadiusMode::Capsule;
            t.progress.fill.border = Border{};
            t.progress.fill.padding = Padding{};
            t.progress.fill.radiusMode = RadiusMode::Capsule;

            //  Handle (drawn only with `SetDraggable(true)`): same origin as
            //  the slider dot, surface background + primary outline, so it
            //  stands out in light themes ("white dot, blue ring") and holds
            //  up in dark ones too. Default 2px outline, Capsule radius
            //  (diameter = side, so a perfect circle).
            //  A plain progress bar never draws the handle, so this section is
            //  dead data for it, but it lives in the theme so a draggable bar
            //  can be reskinned in one place instead of hard-coding white in
            //  the widget.
            t.progress.thumbColor = t.surface;
            t.progress.thumb.padding = Padding{};
            t.progress.thumb.radiusMode = RadiusMode::Capsule;
            t.progress.thumb.border.width[Border::Top] =
                t.progress.thumb.border.width[Border::Right] =
                t.progress.thumb.border.width[Border::Bottom] =
                t.progress.thumb.border.width[Border::Left] = 2.0f;
            t.progress.thumb.colors.get(WidgetState::Normal).border = t.primary;
            t.progress.thumb.colors.get(WidgetState::Hover).border = t.primaryHover;
            t.progress.thumb.colors.get(WidgetState::Pressed).border = t.primaryPress;
            t.progress.thumb.colors.get(WidgetState::Focused).border = t.primaryPress;
            t.progress.thumb.colors.get(WidgetState::Disabled).border = t.disabled;

            //  Overlay text: text token normally, size follows fontS (small
            //  text on the bar).
            t.progress.text.font.pixelSize = t.fontS;
            t.progress.text.font.weight = FontWeight::Normal;
            t.progress.text.colors.get(WidgetState::Normal).text = t.text;
            t.progress.text.colors.get(WidgetState::Disabled).text = t.muted;

            // ---- Slider ----
            //  Same origin as the progress bar: track from the border family,
            //  filled part from primary. The dot uses surface (white / dark
            //  base) + primary outline, so "small white circle + blue ring"
            //  stands out in light themes and holds up in dark ones (surface
            //  darkens but primary brightens).
            //
            //  Track padding 0: same reasoning as the progress bar; padding
            //  would show a ring of background at 100% fill.
            //  Capsule radius: the track becomes a capsule; the dot uses
            //  Circle (radius = half side) for a perfect circle.
            t.slider.trackColor = t.border;
            t.slider.fillColor = t.primary;
            t.slider.thumbColor = t.surface;

            t.slider.track.border = Border{};
            t.slider.track.padding = Padding{};
            t.slider.track.radiusMode = RadiusMode::Capsule;

            t.slider.fill.border = Border{};
            t.slider.fill.padding = Padding{};
            t.slider.fill.radiusMode = RadiusMode::Capsule;

            //  Dot: 2px primary outline, Capsule radius. The dot is square
            //  (width = height = diameter), so Capsule (half the short side =
            //  half the side length) turns it into a perfect circle.
            t.slider.thumb.padding = Padding{};
            t.slider.thumb.radiusMode = RadiusMode::Capsule;
            t.slider.thumb.border.width[Border::Top] =
                t.slider.thumb.border.width[Border::Right] =
                t.slider.thumb.border.width[Border::Bottom] =
                t.slider.thumb.border.width[Border::Left] = 2.0f;
            t.slider.thumb.colors.get(WidgetState::Normal).border = t.primary;
            t.slider.thumb.colors.get(WidgetState::Hover).border = t.primaryHover;
            t.slider.thumb.colors.get(WidgetState::Pressed).border = t.primaryPress;
            t.slider.thumb.colors.get(WidgetState::Focused).border = t.primaryPress;
            t.slider.thumb.colors.get(WidgetState::Disabled).border = t.disabled;

            //  Value text: text token normally, size follows fontS.
            t.slider.text.font.pixelSize = t.fontS;
            t.slider.text.font.weight = FontWeight::Normal;
            t.slider.text.colors.get(WidgetState::Normal).text = t.text;
            t.slider.text.colors.get(WidgetState::Disabled).text = t.muted;

            // ---- Checkbox / RadioButton (shared section) ----
            //  Indicator: surface background (white / dark base), border
            //  (light gray). Accent markColor = primary: a checked checkbox
            //  gets an accent square + white check; a selected radio gets an
            //  accent circle + white center dot. One shared look.
            t.checkbox.indicatorColor = t.surface;
            t.checkbox.indicatorBorderColor = t.border;
            t.checkbox.markColor = t.primary;

            //  The whole widget's box: entirely empty — a checkbox has no
            //  background/border of its own; it is just "an indicator + a
            //  line of text". Leaving it empty avoids painting a background
            //  behind the text.
            t.checkbox.box.border = Border{};
            t.checkbox.box.padding = Padding{};

            //  Label text: text token normally, grayed when disabled; size
            //  follows fontM (same tier as body).
            t.checkbox.text.font.pixelSize = t.fontM;
            t.checkbox.text.font.weight = FontWeight::Normal;
            t.checkbox.text.colors.get(WidgetState::Normal).text = t.text;
            t.checkbox.text.colors.get(WidgetState::Disabled).text = t.muted;

            // ---- ComboBox ----
            //  Collapsed panel: exactly the same origin as Input (surface
            //  background + border, focus turns primary) — to users a combo
            //  box is "an input box you can only pick from", so the look must
            //  match.
            t.combo.box.colors.get(WidgetState::Normal).background = t.surface;
            t.combo.box.colors.get(WidgetState::Normal).border = t.border;
            t.combo.box.colors.get(WidgetState::Hover).background = t.surface;
            t.combo.box.colors.get(WidgetState::Hover).border = t.muted;
            t.combo.box.colors.get(WidgetState::Focused).background = t.surface;
            t.combo.box.colors.get(WidgetState::Focused).border = t.primary;
            t.combo.box.colors.get(WidgetState::Disabled).background = t.bg;
            t.combo.box.colors.get(WidgetState::Disabled).border = t.border;
            t.combo.box.border.width[Border::Top] =
                t.combo.box.border.width[Border::Right] =
                t.combo.box.border.width[Border::Bottom] =
                t.combo.box.border.width[Border::Left] = 1.0f;
            t.combo.box.radiusMode = RadiusMode::Normal;
            t.combo.box.padding = { 8.0f, 6.0f, 8.0f, 6.0f };   // left, top, right, bottom

            //  Text on the panel: the same set as Input (size / color / left +
            //  vertically centered).
            t.combo.text.font.pixelSize = t.fontM;
            t.combo.text.font.weight = FontWeight::Normal;
            t.combo.text.colors.get(WidgetState::Normal).text = t.text;
            t.combo.text.colors.get(WidgetState::Hover).text = t.text;
            t.combo.text.colors.get(WidgetState::Focused).text = t.text;
            t.combo.text.colors.get(WidgetState::Disabled).text = t.muted;
            t.combo.text.hAlign = TextHAlign::Left;
            t.combo.text.vAlign = TextVAlign::Middle;

            //  Expanded popup: surface background + border + one shadow (it
            //  floats; without a shadow it blurs into the content below and
            //  no longer reads as a floating layer).
            t.combo.popup.colors.get(WidgetState::Normal).background = t.surface;
            t.combo.popup.colors.get(WidgetState::Normal).border = t.border;
            t.combo.popup.border.width[Border::Top] =
                t.combo.popup.border.width[Border::Right] =
                t.combo.popup.border.width[Border::Bottom] =
                t.combo.popup.border.width[Border::Left] = 1.0f;
            t.combo.popup.radiusMode = RadiusMode::Normal;
            t.combo.popup.padding = { 4.0f, 4.0f, 4.0f, 4.0f };   // left, top, right, bottom
            t.combo.popup.shadow.enabled = true;
            t.combo.popup.shadow.color = t.shadow.withAlpha(0.18f);
            t.combo.popup.shadow.offsetX = 0.0f;
            t.combo.popup.shadow.offsetY = 3.0f;
            t.combo.popup.shadow.blur = 6.0f;      // Reserved (see Shadow::blur)
            t.combo.popup.shadow.spread = 1.0f;

            //  List item text: size follows fontM (same tier as body), text
            //  token normally.
            t.combo.item.font.pixelSize = t.fontM;
            t.combo.item.font.weight = FontWeight::Normal;
            t.combo.item.colors.get(WidgetState::Normal).text = t.text;
            t.combo.item.colors.get(WidgetState::Disabled).text = t.muted;
            t.combo.item.hAlign = TextHAlign::Left;
            t.combo.item.vAlign = TextVAlign::Middle;

            //  Row height / arrow / two row background colors. The arrow uses
            //  muted (secondary info, should not compete with text); row
            //  backgrounds are translucent primary — plain primary would
            //  drown the text, and tinting the whole row is the standard
            //  list-item look.
            t.combo.itemHeight = 28.0f;
            t.combo.arrowColor = t.muted;
            t.combo.itemHoverColor = t.primary;
            t.combo.itemHoverColor.a = 0.10f;
            t.combo.itemSelectedColor = t.primary;
            t.combo.itemSelectedColor.a = 0.16f;

            // ---- MenuModal ----
            //  Panel: exactly the same origin as combo.popup (surface
            //  background + border + one shadow); both are floating selection
            //  panels and must look like the same family.
            t.menu.panel.colors.get(WidgetState::Normal).background = t.surface;
            t.menu.panel.colors.get(WidgetState::Normal).border = t.border;
            t.menu.panel.border.width[Border::Top] =
                t.menu.panel.border.width[Border::Right] =
                t.menu.panel.border.width[Border::Bottom] =
                t.menu.panel.border.width[Border::Left] = 1.0f;
            t.menu.panel.radiusMode = RadiusMode::Normal;
            t.menu.panel.padding = { 4.0f, 4.0f, 4.0f, 4.0f };
            t.menu.panel.shadow.enabled = true;
            t.menu.panel.shadow.color = t.shadow.withAlpha(0.18f);
            t.menu.panel.shadow.offsetX = 0.0f;
            t.menu.panel.shadow.offsetY = 3.0f;
            t.menu.panel.shadow.blur = 6.0f;
            t.menu.panel.shadow.spread = 1.0f;

            //  Menu item text: size follows fontM (same tier as body), grayed
            //  when disabled.
            t.menu.item.font.pixelSize = t.fontM;
            t.menu.item.font.weight = FontWeight::Normal;
            t.menu.item.colors.get(WidgetState::Normal).text = t.text;
            t.menu.item.colors.get(WidgetState::Disabled).text = t.muted;
            t.menu.item.hAlign = TextHAlign::Left;
            t.menu.item.vAlign = TextVAlign::Middle;

            //  Shortcut hints: one size smaller + muted — secondary info, must
            //  not compete with item text.
            t.menu.shortcut.font.pixelSize = t.fontS;
            t.menu.shortcut.font.weight = FontWeight::Normal;
            t.menu.shortcut.colors.get(WidgetState::Normal).text = t.muted;
            t.menu.shortcut.hAlign = TextHAlign::Right;
            t.menu.shortcut.vAlign = TextVAlign::Middle;

            //  Row height / highlight row background / separator / scrim. The
            //  highlight matches the combo rows (translucent primary —
            //  tinting the whole row is the standard list-item look); the
            //  separator uses border (one step lighter than text); the scrim
            //  is translucent black (dims the background, telling the user
            //  "the menu is the only world right now").
            t.menu.itemHeight = 30.0f;
            t.menu.itemHoverColor = t.primary;
            t.menu.itemHoverColor.a = 0.10f;
            t.menu.separatorColor = t.border;
            t.menu.scrimColor = Color4f{ 0.0f, 0.0f, 0.0f, 0.25f };

            // ---- Tooltip ----
            //  Container look (changed 2026-11-02): transparent background,
            //  no border; the visible bubble is entirely a primitive the
            //  renderer draws from `tooltip.shape` (floating carriers are
            //  always per-pixel transparent and the Tooltip base box never
            //  paints a background). So background / outline / arrow / accent
            //  bar are all part of the shape's own painting: box.background
            //  serves only as the "bubble fill color", and box.border is no
            //  longer read (the outline semantics belong to the shape, see
            //  paintPopupInto). Padding is one step tighter than the menu
            //  (bubbles are small); radius uses radiusM (raised from radiusS
            //  on 2026-11-10: a 4px radius on a 28px bubble looks like a
            //  square sticker; 8px is the modern bubble feel).
            t.tooltip.box.colors.get(WidgetState::Normal).background = t.surface;
            t.tooltip.box.colors.get(WidgetState::Normal).border = t.border;
            t.tooltip.box.border.width[Border::Top] =
                t.tooltip.box.border.width[Border::Right] =
                t.tooltip.box.border.width[Border::Bottom] =
                t.tooltip.box.border.width[Border::Left] = 0.0f;   // Outline is drawn by the shape itself
            t.tooltip.box.radiusMode = RadiusMode::Normal;
            t.tooltip.box.padding = { 10.0f, 6.0f, 10.0f, 6.0f };   // left, top, right, bottom
            t.tooltip.box.shadow.enabled = true;
            t.tooltip.box.shadow.color = t.shadow.withAlpha(0.18f);
            t.tooltip.box.shadow.offsetX = 0.0f;
            t.tooltip.box.shadow.offsetY = 3.0f;
            t.tooltip.box.shadow.blur = 6.0f;
            t.tooltip.box.shadow.spread = 1.0f;

            //  Bubble text: size follows fontS (hints are secondary, one size
            //  smaller); text token normally.
            t.tooltip.text.font.pixelSize = t.fontS;
            t.tooltip.text.font.weight = FontWeight::Normal;
            t.tooltip.text.colors.get(WidgetState::Normal).text = t.text;
            t.tooltip.text.colors.get(WidgetState::Disabled).text = t.muted;
            t.tooltip.text.hAlign = TextHAlign::Left;
            t.tooltip.text.vAlign = TextVAlign::Top;

            t.tooltip.shape = TooltipShape::Rounded;

            //  The arrow outline is one step darker than the bubble border
            //  (see TooltipStyle::arrowBorder): with the same color, a white
            //  triangle on a white bubble over a white page would be
            //  invisible.
            t.tooltip.arrowBorder = t.muted;
            //  The Card shape's left accent bar = primary (same family as the
            //  progress bar / slider semantic bars).
            t.tooltip.accent = t.primary;

            // ---- Toast ----
            //  Body = surface background + one shadow (floats), no border
            //  (separated from the background by the shadow). A 4px semantic
            //  color bar on the left tells "what kind of message this is".
            t.toast.box.colors.get(WidgetState::Normal).background = t.surface;
            t.toast.box.border = Border{};
            t.toast.box.radiusMode = RadiusMode::Normal;
            //  Left padding reserves room for the color bar (left 12 gives the text breathing room)
            t.toast.box.padding = { 16.0f, 12.0f, 16.0f, 12.0f };  // left, top, right, bottom
            t.toast.box.shadow.enabled = true;
            t.toast.box.shadow.color = t.shadow.withAlpha(0.22f);
            t.toast.box.shadow.offsetX = 0.0f;
            t.toast.box.shadow.offsetY = 4.0f;
            t.toast.box.shadow.blur = 8.0f;
            t.toast.box.shadow.spread = 1.0f;

            t.toast.text.font.pixelSize = t.fontM;
            t.toast.text.font.weight = FontWeight::Normal;
            t.toast.text.colors.get(WidgetState::Normal).text = t.text;
            t.toast.text.colors.get(WidgetState::Disabled).text = t.muted;
            t.toast.text.hAlign = TextHAlign::Left;
            t.toast.text.vAlign = TextVAlign::Middle;

            //  Four semantic colors: info uses primary, the rest use tokens
            //  directly (dark theme token changes propagate automatically)
            t.toast.accentWidth = 4.0f;
            t.toast.accentInfo = t.primary;
            t.toast.accentSuccess = t.success;
            t.toast.accentWarning = t.warning;
            t.toast.accentError = t.danger;

            //  Type icon circle: fill takes the matching accent; the glyph is
            //  always white (all four accents are dark enough)
            t.toast.iconSize = 22.0f;
            t.toast.iconGap = 10.0f;
            t.toast.iconGlyph = Color4f::White();

            //  Close button / minimum width: interactive notifications
            //  (standalone window form) need them
            t.toast.minWidth = 300.0f;
            t.toast.closeGlyph = t.muted;
            t.toast.closeHoverBg = t.text.withAlpha(0.08f);   // Follows polarity: light = faint black, dark = faint white

            // ---- MenuBar ----
            //  Bar body = surface background + 1px bottom divider (separates
            //  bar from content, VS style: by line, not color difference;
            //  works for both themes). Title text uses fontM + text token.
            t.menubar.bar.colors.get(WidgetState::Normal).background = t.surface;
            t.menubar.bar.border = Border{};
            t.menubar.bar.border.width[Border::Bottom] = 1.0f;
            t.menubar.bar.colors.get(WidgetState::Normal).border = t.border;
            t.menubar.bar.padding = { 6.0f, 0.0f, 6.0f, 0.0f };

            t.menubar.title.font.pixelSize = t.fontM;
            t.menubar.title.font.weight = FontWeight::Normal;
            t.menubar.title.colors.get(WidgetState::Normal).text = t.text;
            t.menubar.title.colors.get(WidgetState::Disabled).text = t.muted;

            //  Hover background: 8% derived from the text token — faint black
            //  in light themes, faint white in dark ones, polarity follows
            //  automatically (unlike toast.closeHoverBg's fixed 8% black; the
            //  title highlight area is large, so it must follow the theme's
            //  polarity to stay clean).
            t.menubar.titleHover = Color4f{ t.text.r, t.text.g, t.text.b, 0.08f };
            t.menubar.titlePadX = 10.0f;
            t.menubar.titleRadius = 5.0f;
            t.menubar.barHeight = 38.0f;

            // ---- Splitter ----
            //  The handle rests in border color (quiet), switching to primary
            //  on hover / drag (a clear "got it").
            t.splitter.handleColor = t.border;
            t.splitter.handleActiveColor = t.primary;
            t.splitter.radius = 2.0f;

            // ---- DockPanel ----
            //  2026-11-03 revision: drop the "1px border around every pane".
            //  The old scheme was "pane 1px border + same-color handle in the
            //  gap"; the combined look was the "black lines all around" users
            //  reported. Now it is "cards + gaps":
            //   - The gap (container background) = bg leaning one step toward
            //     border. It must be noticeably darker than the pane
            //     background, or the "card" does not read (measured: plain bg
            //     differs from white by only 7/255, panes blur together and
            //     the radius is wasted);
            //   - Pane background = surface (same family as cards/popups) + 8px
            //     radius, no border;
            //   - Title bar = bg, separated from content by background
            //     difference + a very faint divider (no solid line);
            //   - Nothing is drawn in the gap at rest; the primary handle
            //     appears as the pointer passes.
            t.dockPanel.background = t.border;
            t.dockPanel.borderColor = t.border;
            t.dockPanel.borderWidth = 0.0f;      // The container draws no frame: cards + gaps are enough
            t.dockPanel.radius = 0.0f;

            t.dockPanel.paneBackground = t.surface;
            t.dockPanel.paneBorderColor = t.border;
            t.dockPanel.paneBorderWidth = 0.0f;  // No outline anymore: that ring was the source of the "black lines"
            t.dockPanel.paneRadius = 8.0f;
            t.dockPanel.titleBarColor = t.bg;
            t.dockPanel.titleBarDivider = 0.45f;

            t.dockPanel.title.font.pixelSize = t.fontS;
            t.dockPanel.title.font.weight = 600;
            t.dockPanel.title.colors.get(WidgetState::Normal).text = t.muted;
            t.dockPanel.title.colors.get(WidgetState::Disabled).text = t.disabled;
            t.dockPanel.title.hAlign = TextHAlign::Left;
            t.dockPanel.title.vAlign = TextVAlign::Middle;

            t.dockPanel.handleColor = t.border;
            t.dockPanel.handleActiveColor = t.primary;
            t.dockPanel.handleRadius = 999.0f;   // Round caps (clamped to half the handle thickness in practice)
            t.dockPanel.handleThickness = 0.0f;  // 0 = auto (45% of the gap width, clamped to 2~4px)
            t.dockPanel.handleAlwaysVisible = false;

            //  The drop highlight is translucent primary: it paints over
            //  content and must not hide what is underneath — the user needs
            //  to see both where the drop lands and what was there.
            t.dockPanel.dropColor = t.primary;
            t.dockPanel.dropColor.a = 0.28f;
            t.dockPanel.dropBorderWidth = 2.0f;

            t.dockPanel.closeColor = t.muted;
            t.dockPanel.closeHoverColor = t.danger;

            t.dockPanel.titleBarHeight = 26.0f;
            t.dockPanel.closeSize = 14.0f;
            t.dockPanel.minPaneWidth = 56.0f;
            t.dockPanel.minPaneHeight = 40.0f;

            // ---- ListView ----
            //  Container in the same family as the input box (surface
            //  background + border), row height one step looser (28). Row
            //  backgrounds tint the whole row: hover one step lighter,
            //  selected one step deeper — easy to tell apart without glare.
            t.list.background = t.surface;
            t.list.borderColor = t.border;
            t.list.borderWidth = 1.0f;
            t.list.radius = t.radiusM;

            t.list.itemHeight = 28.0f;
            t.list.itemHoverColor = t.primary;
            t.list.itemHoverColor.a = 0.08f;
            t.list.itemSelectedColor = t.primary;
            t.list.itemSelectedColor.a = 0.18f;

            t.list.item.font.pixelSize = t.fontM;
            t.list.item.font.weight = FontWeight::Normal;
            t.list.item.colors.get(WidgetState::Normal).text = t.text;
            t.list.item.colors.get(WidgetState::Disabled).text = t.muted;
            t.list.item.hAlign = TextHAlign::Left;
            t.list.item.vAlign = TextVAlign::Middle;

            // ---- TabView ----
            //  The tab bar background uses bg (one step below surface, keeping
            //  "bar" and "page" apart); the divider uses border. Unselected
            //  text is muted, selected uses body color + primary indicator
            //  line — hierarchy from "line + light/dark".
            t.tab.barBackground = t.bg;
            t.tab.dividerColor = t.border;
            t.tab.hoverBackground = t.muted;
            t.tab.hoverBackground.a = 0.10f;
            t.tab.activeBackground = t.primary;
            t.tab.activeBackground.a = 0.12f;
            t.tab.indicatorColor = t.primary;
            t.tab.indicatorHeight = 2.0f;
            t.tab.radius = t.radiusS;
            t.tab.barHeight = 34.0f;
            t.tab.minTabWidth = 64.0f;
            t.tab.tabPaddingX = 14.0f;

            t.tab.head.font.pixelSize = t.fontM;
            t.tab.head.font.weight = FontWeight::Normal;
            t.tab.head.colors.get(WidgetState::Normal).text = t.muted;
            t.tab.head.colors.get(WidgetState::Disabled).text = t.disabled;
            t.tab.head.hAlign = TextHAlign::Center;
            t.tab.head.vAlign = TextVAlign::Middle;

            //  Selected tab: body color + one weight step up (the glyph itself
            //  is also heavier than the line)
            t.tab.headActive.font.pixelSize = t.fontM;
            t.tab.headActive.font.weight = 700;
            t.tab.headActive.colors.get(WidgetState::Normal).text = t.text;
            t.tab.headActive.colors.get(WidgetState::Disabled).text = t.muted;
            t.tab.headActive.hAlign = TextHAlign::Center;
            t.tab.headActive.vAlign = TextVAlign::Middle;

            // ---- TableView ----
            //  The header is one step weaker than the body (muted +
            //  semi-bold) and its background one step lower (bg), so the eye
            //  lands on the data first. Grid lines use border (one step
            //  lighter than text, enough without glare).
            t.table.headerBackground = t.bg;
            t.table.background = t.surface;
            t.table.borderColor = t.border;
            t.table.borderWidth = 1.0f;
            t.table.gridColor = t.border;

            t.table.rowHoverColor = t.primary;
            t.table.rowHoverColor.a = 0.06f;
            t.table.rowSelectedColor = t.primary;
            t.table.rowSelectedColor.a = 0.16f;

            t.table.rowHeight = 28.0f;
            t.table.headerHeight = 32.0f;
            t.table.cellPaddingX = 8.0f;

            t.table.header.font.pixelSize = t.fontM;
            t.table.header.font.weight = 500;                 // Semi-bold: identification without competing with body text
            t.table.header.colors.get(WidgetState::Normal).text = t.muted;
            t.table.header.colors.get(WidgetState::Disabled).text = t.disabled;
            t.table.header.hAlign = TextHAlign::Left;
            t.table.header.vAlign = TextVAlign::Middle;

            t.table.cell.font.pixelSize = t.fontM;
            t.table.cell.font.weight = FontWeight::Normal;
            t.table.cell.colors.get(WidgetState::Normal).text = t.text;
            t.table.cell.colors.get(WidgetState::Disabled).text = t.muted;
            t.table.cell.hAlign = TextHAlign::Left;
            t.table.cell.vAlign = TextVAlign::Middle;

            // ---- TreeView ----
            //  The same look as the list (same two row backgrounds, same
            //  container skeleton), plus indent and indicator — the two often
            //  appear side by side, so they must be one family.
            t.tree.background = t.surface;
            t.tree.borderColor = t.border;
            t.tree.borderWidth = 1.0f;
            t.tree.radius = t.radiusM;

            t.tree.rowHoverColor = t.primary;
            t.tree.rowHoverColor.a = 0.08f;
            t.tree.rowSelectedColor = t.primary;
            t.tree.rowSelectedColor.a = 0.18f;
            t.tree.indicatorColor = t.muted;                  // `+` / `-` are secondary marks

            t.tree.rowHeight = 28.0f;
            t.tree.indentWidth = 18.0f;
            t.tree.indicatorWidth = 20.0f;

            t.tree.item.font.pixelSize = t.fontM;
            t.tree.item.font.weight = FontWeight::Normal;
            t.tree.item.colors.get(WidgetState::Normal).text = t.text;
            t.tree.item.colors.get(WidgetState::Disabled).text = t.muted;
            t.tree.item.hAlign = TextHAlign::Left;
            t.tree.item.vAlign = TextVAlign::Middle;

            // ---- SubWindow / ModalDialog ----
            //  Same family as tooltip / toast / menu.panel (surface background
            //  + border + shadow) but heavier: it carries a whole block of
            //  UI, so a bigger shadow and bigger radius (radiusL).
            t.subwindow.background = t.surface;
            t.subwindow.borderColor = t.border;
            t.subwindow.radius = t.radiusL;
            t.subwindow.shadowColor = t.shadow.withAlpha(0.24f);
            t.subwindow.shadowOffsetY = 8.0f;
            t.subwindow.shadowSpread = 3.0f;

            //  The title bar is one step below the content (bg) — same trick
            //  as "tab bar vs page": light/dark says "this is the title", no
            //  divider line.
            t.subwindow.titleBarBackground = t.bg;
            t.subwindow.titleBarHeight = 30.0f;

            t.subwindow.title.font.pixelSize = t.fontM;
            t.subwindow.title.font.weight = 600;          // Title slightly heavier than body
            t.subwindow.title.colors.get(WidgetState::Normal).text = t.text;
            t.subwindow.title.colors.get(WidgetState::Disabled).text = t.muted;
            t.subwindow.title.hAlign = TextHAlign::Left;
            t.subwindow.title.vAlign = TextVAlign::Middle;

            //  Close button: muted at rest (quiet), danger (red) on hover —
            //  closing a window is destructive, and the color says so early.
            t.subwindow.closeColor = t.muted;
            t.subwindow.closeHoverColor = t.danger;

            //  The modal scrim is one step heavier than the menu's (a dialog
            //  is more solemn).
            t.subwindow.scrimColor = Color4f{ 0.0f, 0.0f, 0.0f, 0.32f };

            // ---- ModalDialog ----
            //  Same family as subwindow (surface background + border + shadow),
            //  derived from the same tokens but more prominent: a dialog is
            //  "must be dealt with", so a taller title bar, bigger shadow,
            //  heavier scrim. Tokens match, so Dark() only changes tokens and
            //  this follows automatically.
            t.modalDialog.background = t.surface;
            t.modalDialog.borderColor = t.border;
            t.modalDialog.radius = t.radiusL;
            t.modalDialog.shadowColor = t.shadow.withAlpha(0.28f);
            t.modalDialog.shadowOffsetY = 8.0f;
            t.modalDialog.shadowSpread = 3.0f;

            //  The title bar is one step below the content (bg) — same trick
            //  as subwindow.
            t.modalDialog.titleBarBackground = t.bg;
            t.modalDialog.titleBarHeight = 34.0f;   // Slightly taller than subwindow (30): more prominent

            t.modalDialog.title.font.pixelSize = t.fontM;
            t.modalDialog.title.font.weight = 600;          // Title slightly heavier than body
            t.modalDialog.title.colors.get(WidgetState::Normal).text = t.text;
            t.modalDialog.title.colors.get(WidgetState::Disabled).text = t.muted;
            t.modalDialog.title.hAlign = TextHAlign::Left;
            t.modalDialog.title.vAlign = TextVAlign::Middle;

            //  Close button: muted at rest, danger on hover (same discipline
            //  as subwindow)
            t.modalDialog.closeColor = t.muted;
            t.modalDialog.closeHoverColor = t.danger;

            //  Scrim heavier than subwindow (0.32) — when a dialog appears the
            //  world should quiet down
            t.modalDialog.scrimColor = Color4f{ 0.0f, 0.0f, 0.0f, 0.36f };

            // =============================================================
            //  2026-10: theme derivation for a batch of new widgets
            // =============================================================
            //  Same discipline as above: everything derives from tokens, no
            //  hard-coded colors — so Dark() only changes tokens and these
            //  widgets follow automatically (see Dark()).

            // ---- Card ----
            //  The existing card.box six-state colors / border / radius /
            //  padding stay unchanged (see above); here we add the title row
            //  and the shadow (a card floats one step above a panel, told by
            //  the shadow).
            t.card.title.font.pixelSize = t.fontM;
            t.card.title.font.weight = 600;                 // Title slightly heavier than body
            t.card.title.colors.get(WidgetState::Normal).text = t.text;
            t.card.title.colors.get(WidgetState::Disabled).text = t.muted;
            t.card.title.hAlign = TextHAlign::Left;
            t.card.title.vAlign = TextVAlign::Middle;
            t.card.titleHeight = t.ctrlHM;                  // Title area height = medium control height
            t.card.shadow = true;
            t.card.shadowOffsetY = 2.0f;
            t.card.shadowSpread = 1.0f;
            t.card.shadowColor = t.shadow.withAlpha(0.10f);

            // ---- Sidebar ----
            //  A sidebar is a vertical panel hugging the window edge: only
            //  one edge-hugging divider line (expressed via divider), no
            //  four-sided border, no radius — a rounded corner against the
            //  edge would expose the background and stop looking like a
            //  sidebar.
            t.sidebar.background = t.surface;
            t.sidebar.borderColor = t.border;
            t.sidebar.borderWidth = 0.0f;                   // Four-sided border off (not needed against the edge)
            t.sidebar.radius = 0.0f;
            t.sidebar.padding = t.spaceS;
            t.sidebar.dividerColor = t.border;              // The vertical line between sidebar and content
            t.sidebar.dividerWidth = 1.0f;
            t.sidebar.title.font.pixelSize = t.fontS;
            t.sidebar.title.font.weight = 600;
            t.sidebar.title.colors.get(WidgetState::Normal).text = t.muted;   // Group names are "labels", one step quieter
            t.sidebar.title.colors.get(WidgetState::Disabled).text = t.disabled;
            t.sidebar.title.hAlign = TextHAlign::Left;
            t.sidebar.title.vAlign = TextVAlign::Middle;
            t.sidebar.body.font.pixelSize = t.fontM;
            t.sidebar.body.colors.get(WidgetState::Normal).text = t.text;

            // ---- Drawer ----
            //  A drawer floats: surface background + border + shadow
            //  (hierarchy told by the shadow and the scrim).
            t.drawer.background = t.surface;
            t.drawer.borderColor = t.border;
            t.drawer.borderWidth = 0.0f;
            t.drawer.radius = 0.0f;
            t.drawer.padding = t.spaceM;
            t.drawer.dividerColor = t.border;               // Horizontal line between drawer header and content
            t.drawer.dividerWidth = 1.0f;
            t.drawer.title.font.pixelSize = t.fontL;
            t.drawer.title.font.weight = 600;
            t.drawer.title.colors.get(WidgetState::Normal).text = t.text;
            t.drawer.title.colors.get(WidgetState::Disabled).text = t.muted;
            t.drawer.title.hAlign = TextHAlign::Left;
            t.drawer.title.vAlign = TextVAlign::Middle;
            t.drawer.body.font.pixelSize = t.fontM;
            t.drawer.body.colors.get(WidgetState::Normal).text = t.text;
            t.drawer.body.colors.get(WidgetState::Disabled).text = t.muted;

            // ---- ToolBar ----
            //  The toolbar is one step "brighter" than the status bar: surface
            //  background (tools are the primary operating surface), while
            //  the status bar uses bg (status is auxiliary).
            t.toolbar.background = t.surface;
            t.toolbar.borderColor = t.border;
            t.toolbar.borderWidth = 0.0f;
            t.toolbar.radius = 0.0f;
            t.toolbar.padding = 6.0f;
            t.toolbar.dividerColor = t.border;              // Vertical lines between groups
            t.toolbar.dividerWidth = 1.0f;
            t.toolbar.title.font.pixelSize = t.fontS;
            t.toolbar.title.font.weight = 600;
            t.toolbar.title.colors.get(WidgetState::Normal).text = t.muted;
            t.toolbar.title.hAlign = TextHAlign::Left;
            t.toolbar.title.vAlign = TextVAlign::Middle;
            t.toolbar.body.font.pixelSize = t.fontM;
            t.toolbar.body.colors.get(WidgetState::Normal).text = t.text;

            // ---- StatusBar ----
            //  A strip hugging the window bottom: bg background (slightly
            //  sunk versus the content), divider on the top edge, muted text
            //  (status is secondary and must not compete with content).
            t.statusBar.background = t.bg;
            t.statusBar.borderColor = t.border;
            t.statusBar.borderWidth = 0.0f;
            t.statusBar.radius = 0.0f;
            t.statusBar.padding = 6.0f;
            t.statusBar.dividerColor = t.border;            // The line along the top edge
            t.statusBar.dividerWidth = 1.0f;
            t.statusBar.title.font.pixelSize = t.fontS;
            t.statusBar.title.font.weight = 400;
            t.statusBar.title.colors.get(WidgetState::Normal).text = t.muted;
            t.statusBar.title.hAlign = TextHAlign::Left;
            t.statusBar.title.vAlign = TextVAlign::Middle;
            t.statusBar.body.font.pixelSize = t.fontS;
            t.statusBar.body.colors.get(WidgetState::Normal).text = t.muted;
            t.statusBar.body.hAlign = TextHAlign::Left;
            t.statusBar.body.vAlign = TextVAlign::Middle;

            // ---- NumberBox ----
            //  The input area fully reuses the input section (it is an input
            //  box plus two step buttons), so "the number box looks like the
            //  input box" is structurally guaranteed, not tuned twice in two
            //  places.
            t.numberBox.box = t.input.box;
            t.numberBox.text = t.input.text;
            t.numberBox.stepButton = t.input.box;
            t.numberBox.stepButton.colors.get(WidgetState::Normal).background =
                Color4f{ 0.0f, 0.0f, 0.0f, 0.0f };          // Button transparent at rest (appears only on hover)
            t.numberBox.stepButton.colors.get(WidgetState::Hover).background = t.bg;
            t.numberBox.stepButton.colors.get(WidgetState::Pressed).background = t.border;
            t.numberBox.stepButton.border.width[Border::Top] =
                t.numberBox.stepButton.border.width[Border::Right] =
                t.numberBox.stepButton.border.width[Border::Bottom] =
                t.numberBox.stepButton.border.width[Border::Left] = 0.0f;
            t.numberBox.stepGlyph.font.pixelSize = t.fontM;
            t.numberBox.stepGlyph.font.weight = 400;
            t.numberBox.stepGlyph.hAlign = TextHAlign::Center;
            t.numberBox.stepGlyph.vAlign = TextVAlign::Middle;
            t.numberBox.stepColor = t.muted;
            t.numberBox.stepHoverColor = t.primary;          // Arrow turns primary on hover ("pressable")
            t.numberBox.stepPressedColor = t.primaryPress;
            t.numberBox.buttonWidth = 22.0f;

            // ---- ItemStrip (breadcrumb / pagination / step strip, shared) ----
            t.itemStrip.item.font.pixelSize = t.fontM;
            t.itemStrip.item.font.weight = 400;
            t.itemStrip.item.colors.get(WidgetState::Normal).text = t.text;
            t.itemStrip.item.colors.get(WidgetState::Disabled).text = t.muted;
            t.itemStrip.item.hAlign = TextHAlign::Center;
            t.itemStrip.item.vAlign = TextVAlign::Middle;

            t.itemStrip.itemActive.font.pixelSize = t.fontM;
            t.itemStrip.itemActive.font.weight = 600;        // Current item bolded (double emphasis with color)
            //  Current-item text = primary. This section serves three widgets,
            //  and two of them (breadcrumb / step strip titles) paint their
            //  text on the normal background, where primary is correct.
            //  The one exception is the widget whose text sits on a solid
            //  primary block: the current-page number of Pagination must
            //  invert to onPrimary in its own ApplyTheme (see
            //  ZePagination::activeInk_). Do not change this to onPrimary
            //  here: that would turn the step strip's title into white on
            //  white (a real regression observed 2026-10-27).
            t.itemStrip.itemActive.colors.get(WidgetState::Normal).text = t.primary;
            t.itemStrip.itemActive.colors.get(WidgetState::Disabled).text = t.disabled;
            t.itemStrip.itemActive.hAlign = TextHAlign::Center;
            t.itemStrip.itemActive.vAlign = TextVAlign::Middle;

            t.itemStrip.gap = 6.0f;
            t.itemStrip.itemPaddingX = 10.0f;
            t.itemStrip.itemHeight = 28.0f;
            t.itemStrip.radius = t.radiusS;

            //  Hover background = translucent primary (same trick as list /
            //  tree hover rows: no new hue)
            t.itemStrip.itemHoverColor = t.primary;
            t.itemStrip.itemHoverColor.a = 0.08f;
            //  Current-item background = primary (the pagination solid block).
            //  Widgets that paint a whole row with it, like Sidebar, mute it
            //  in their own ApplyTheme (see ZeSidebar::ApplyTheme), while
            //  Pagination, whose text sits on the block, inverts to
            //  onPrimary.
            t.itemStrip.itemActiveColor = t.primary;
            t.itemStrip.separatorColor = t.muted;
            t.itemStrip.disabledColor = t.disabled;

            // ---- Collapse ----
            //  The whole panel gets a card-like frame (surface + border); the
            //  header has three states.
            t.collapse.panel.colors.get(WidgetState::Normal).background = t.surface;
            t.collapse.panel.colors.get(WidgetState::Normal).border = t.border;
            t.collapse.panel.border.width[Border::Top] =
                t.collapse.panel.border.width[Border::Right] =
                t.collapse.panel.border.width[Border::Bottom] =
                t.collapse.panel.border.width[Border::Left] = 1.0f;
            t.collapse.panel.radiusMode = RadiusMode::Normal;
            t.collapse.panel.padding = { 0.0f, 0.0f, 0.0f, 0.0f };   // Padding managed by each part

            t.collapse.head.font.pixelSize = t.fontM;
            t.collapse.head.font.weight = 500;
            t.collapse.head.colors.get(WidgetState::Normal).text = t.text;
            t.collapse.head.colors.get(WidgetState::Disabled).text = t.muted;
            t.collapse.head.hAlign = TextHAlign::Left;
            t.collapse.head.vAlign = TextVAlign::Middle;

            t.collapse.headActive.font.pixelSize = t.fontM;
            t.collapse.headActive.font.weight = 600;
            t.collapse.headActive.colors.get(WidgetState::Normal).text = t.primary;
            t.collapse.headActive.colors.get(WidgetState::Disabled).text = t.muted;
            t.collapse.headActive.hAlign = TextHAlign::Left;
            t.collapse.headActive.vAlign = TextVAlign::Middle;

            t.collapse.headDisabled.font.pixelSize = t.fontM;
            t.collapse.headDisabled.font.weight = 400;
            t.collapse.headDisabled.colors.get(WidgetState::Normal).text = t.disabled;
            t.collapse.headDisabled.hAlign = TextHAlign::Left;
            t.collapse.headDisabled.vAlign = TextVAlign::Middle;

            t.collapse.headHeight = t.ctrlHM;              // 36
            t.collapse.itemGap = 0.0f;                     // Items separated by dividers, not gaps
            t.collapse.contentPadding = t.spaceM;          // 16
            t.collapse.indicatorSize = 5.0f;
            t.collapse.indicatorThickness = 1.6f;
            t.collapse.headColor = Color4f{ 0.0f, 0.0f, 0.0f, 0.0f };   // Transparent at rest (shows the panel background)
            t.collapse.headHoverColor = t.bg;                            // Light background on hover
            t.collapse.headActiveColor = t.bg;                           // Light background when expanded
            t.collapse.dividerColor = t.border;
            t.collapse.indicatorColor = t.muted;                         // Arrow quieter than the title
            t.collapse.radius = t.radiusM;

            // ---- DatePicker ----
            //  The collapsed state reuses the input section directly (a
            //  picker that looks like an input box, same as NumberBox).
            t.datePicker.box = t.input.box;
            t.datePicker.text = t.input.text;
            //  Popup = floating layer: surface background + border + explicit
            //  shadow (same family as combo.popup).
            t.datePicker.panel.colors.get(WidgetState::Normal).background = t.surface;
            t.datePicker.panel.colors.get(WidgetState::Normal).border = t.border;
            t.datePicker.panel.border.width[Border::Top] =
                t.datePicker.panel.border.width[Border::Right] =
                t.datePicker.panel.border.width[Border::Bottom] =
                t.datePicker.panel.border.width[Border::Left] = 1.0f;
            t.datePicker.panel.radiusMode = RadiusMode::Normal;
            t.datePicker.panel.padding = { 8.0f, 8.0f, 8.0f, 8.0f };
            t.datePicker.panel.shadow.enabled = true;
            t.datePicker.panel.shadow.color = t.shadow.withAlpha(0.22f);
            t.datePicker.panel.shadow.offsetY = 6.0f;
            t.datePicker.panel.shadow.spread = 2.0f;

            t.datePicker.head.font.pixelSize = t.fontM;
            t.datePicker.head.font.weight = 600;
            t.datePicker.head.colors.get(WidgetState::Normal).text = t.text;
            t.datePicker.head.colors.get(WidgetState::Disabled).text = t.muted;
            t.datePicker.head.hAlign = TextHAlign::Center;
            t.datePicker.head.vAlign = TextVAlign::Middle;

            t.datePicker.weekday.font.pixelSize = t.fontS;
            t.datePicker.weekday.font.weight = 400;
            t.datePicker.weekday.colors.get(WidgetState::Normal).text = t.muted;
            t.datePicker.weekday.hAlign = TextHAlign::Center;
            t.datePicker.weekday.vAlign = TextVAlign::Middle;

            t.datePicker.day.font.pixelSize = t.fontM;
            t.datePicker.day.font.weight = 400;
            t.datePicker.day.colors.get(WidgetState::Normal).text = t.text;
            t.datePicker.day.colors.get(WidgetState::Disabled).text = t.disabled;
            t.datePicker.day.hAlign = TextHAlign::Center;
            t.datePicker.day.vAlign = TextVAlign::Middle;

            //  Time numbers and popup button text: one size smaller than day
            //  numbers, centered
            t.datePicker.value.font.pixelSize = t.fontS;
            t.datePicker.value.font.weight = 400;
            t.datePicker.value.colors.get(WidgetState::Normal).text = t.text;
            t.datePicker.value.colors.get(WidgetState::Disabled).text = t.disabled;
            t.datePicker.value.hAlign = TextHAlign::Center;
            t.datePicker.value.vAlign = TextVAlign::Middle;

            t.datePicker.dayCellHeight = 30.0f;
            t.datePicker.dayCellWidth = 34.0f;
            t.datePicker.headerHeight = 34.0f;
            t.datePicker.gap = 4.0f;
            t.datePicker.weekHeaderColor = t.bg;
            t.datePicker.dayHoverColor = t.primary;
            t.datePicker.dayHoverColor.a = 0.10f;
            t.datePicker.daySelectedColor = t.primary;          // Selected day = solid primary
            t.datePicker.dayTodayColor = t.primary;             // "Today" gets a primary outline / text
            t.datePicker.dayMutedColor = t.disabled;            // Other month
            t.datePicker.arrowColor = t.muted;
            t.datePicker.arrowHoverColor = t.primary;

            // ---- ColorPicker ----
            t.colorPicker.box = t.input.box;
            t.colorPicker.text = t.input.text;
            t.colorPicker.panel.colors.get(WidgetState::Normal).background = t.surface;
            t.colorPicker.panel.colors.get(WidgetState::Normal).border = t.border;
            t.colorPicker.panel.border.width[Border::Top] =
                t.colorPicker.panel.border.width[Border::Right] =
                t.colorPicker.panel.border.width[Border::Bottom] =
                t.colorPicker.panel.border.width[Border::Left] = 1.0f;
            t.colorPicker.panel.radiusMode = RadiusMode::Normal;
            t.colorPicker.panel.shadow.enabled = true;
            t.colorPicker.panel.shadow.color = t.shadow.withAlpha(0.22f);
            t.colorPicker.panel.shadow.offsetY = 6.0f;
            t.colorPicker.panel.shadow.spread = 2.0f;

            t.colorPicker.label.font.pixelSize = t.fontS;
            t.colorPicker.label.font.weight = 400;
            t.colorPicker.label.colors.get(WidgetState::Normal).text = t.muted;
            t.colorPicker.label.hAlign = TextHAlign::Left;
            t.colorPicker.label.vAlign = TextVAlign::Middle;

            t.colorPicker.value.font.pixelSize = t.fontS;
            t.colorPicker.value.font.weight = 400;
            t.colorPicker.value.colors.get(WidgetState::Normal).text = t.text;
            t.colorPicker.value.hAlign = TextHAlign::Right;
            t.colorPicker.value.vAlign = TextVAlign::Middle;

            t.colorPicker.gap = 8.0f;
            t.colorPicker.padding = 10.0f;
            t.colorPicker.svHeight = 130.0f;
            t.colorPicker.hueHeight = 14.0f;
            t.colorPicker.channelHeight = 22.0f;
            t.colorPicker.swatchHeight = 26.0f;
            t.colorPicker.radius = t.radiusS;
            t.colorPicker.borderColor = t.border;
            t.colorPicker.cursorColor = Color4f::White();
            t.colorPicker.cursorShadowColor = Color4f{ 0.0f, 0.0f, 0.0f, 0.6f };
            t.colorPicker.trackColor = t.border;
            t.colorPicker.thumbColor = Color4f::White();

            // =============================================================
            //  2026-10-14: theme derivation for lightweight display widgets
            //  + time picker
            // =============================================================
            //  Same discipline: everything derives from tokens, no hard-coded
            //  colors.

            // ---- Divider ----
            //  A divider is the quietest line: border color, never competing
            //  with content.
            t.divider.color = t.border;
            t.divider.thickness = 1.0f;
            t.divider.gap = t.spaceS;
            t.divider.label.font.pixelSize = t.fontS;
            t.divider.label.font.weight = 400;
            t.divider.label.colors.get(WidgetState::Normal).text = t.muted;
            t.divider.label.colors.get(WidgetState::Disabled).text = t.disabled;
            t.divider.label.hAlign = TextHAlign::Center;
            t.divider.label.vAlign = TextVAlign::Middle;

            // ---- Badge ----
            //  Text one size smaller + bold (a badge is tiny; weight buys
            //  recognizability).
            t.badge.text.font.pixelSize = t.fontS;
            t.badge.text.font.weight = 600;
            t.badge.text.colors.get(WidgetState::Normal).text = t.onPrimary;
            t.badge.text.hAlign = TextHAlign::Center;
            t.badge.text.vAlign = TextVAlign::Middle;

            t.badge.minWidth = 18.0f;
            t.badge.height = 18.0f;
            t.badge.paddingX = 6.0f;
            t.badge.radius = 9.0f;                       // Capsule
            t.badge.dotSize = 8.0f;

            t.badge.primaryBackground = t.primary;
            t.badge.primaryText = t.onPrimary;
            t.badge.dangerBackground = t.danger;
            t.badge.dangerText = Color4f::White();
            t.badge.successBackground = t.success;
            t.badge.successText = Color4f::White();
            t.badge.warningBackground = t.warning;
            t.badge.warningText = Color4f::White();
            //  The neutral badge sits on content; use the lighter border as
            //  background (darkens automatically in dark themes)
            t.badge.neutralBackground = t.border;
            t.badge.neutralText = t.text;

            // ---- Avatar ----
            t.avatar.initials.font.pixelSize = t.fontM;
            t.avatar.initials.font.weight = 600;
            t.avatar.initials.colors.get(WidgetState::Normal).text = Color4f::White();
            t.avatar.initials.hAlign = TextHAlign::Center;
            t.avatar.initials.vAlign = TextVAlign::Middle;

            t.avatar.borderWidth = 2.0f;
            t.avatar.borderColor = t.surface;            // Same color as the card background, as if "punched out" of the card
            t.avatar.fallback = t.muted;

            t.avatar.statusSize = 10.0f;
            t.avatar.statusBorderWidth = 2.0f;
            t.avatar.statusBorderColor = t.surface;
            t.avatar.statusOnline = t.success;
            t.avatar.statusOffline = t.disabled;
            t.avatar.statusBusy = t.danger;
            t.avatar.statusAway = t.warning;

            //  Palette: all from existing hues (no new colors), so it reads as
            //  one design language
            t.avatar.palette[0] = t.primary;
            t.avatar.palette[1] = t.success;
            t.avatar.palette[2] = t.warning;
            t.avatar.palette[3] = t.danger;
            t.avatar.palette[4] = Color4f::FromHex(0x7C3AED);   // Purple
            t.avatar.palette[5] = Color4f::FromHex(0x0EA5E9);   // Sky blue

            // ---- Icon ----
            t.icon.color = t.text;
            t.icon.size = 16.0f;
            t.icon.stroke = 1.6f;

            // ---- Image ----
            t.image.box.colors.get(WidgetState::Normal).background = t.bg;
            t.image.box.colors.get(WidgetState::Normal).border = t.border;
            t.image.box.border.width[Border::Top] =
                t.image.box.border.width[Border::Right] =
                t.image.box.border.width[Border::Bottom] =
                t.image.box.border.width[Border::Left] = 1.0f;
            t.image.box.radiusMode = RadiusMode::Normal;
            t.image.box.padding = { 0.0f, 0.0f, 0.0f, 0.0f };
            t.image.placeholderColor = t.bg;
            t.image.placeholderGlyph = t.muted;

            // ---- ImagePreview (whole-window image previewer) ----
            //  Scrim: near-black translucent (0.92 — any darker hides the
            //  window below, which is the hint that "this is an overlay";
            //  0.75 fails to suppress the colorful UI below).
            //  Info bars: fixed translucent black + white text, not surface —
            //  they paint over arbitrary images, and a light theme's white
            //  bar would smear over a white image (see ImagePreviewStyle).
            t.imagePreview.bar.colors.get(WidgetState::Normal).background =
                Color4f{ 0.0f, 0.0f, 0.0f, 0.62f };
            t.imagePreview.bar.colors.get(WidgetState::Normal).border = Color4f{ 0.0f, 0.0f, 0.0f, 0.0f };
            t.imagePreview.bar.border = Border{};
            t.imagePreview.bar.radiusMode = RadiusMode::None;
            t.imagePreview.bar.padding = { 0.0f, 0.0f, 0.0f, 0.0f };
            t.imagePreview.bar.shadow.enabled = false;

            t.imagePreview.title.font.pixelSize = t.fontM;
            t.imagePreview.title.font.weight = FontWeight::Medium;
            t.imagePreview.title.colors.get(WidgetState::Normal).text = Color4f{ 1.0f, 1.0f, 1.0f, 0.95f };

            t.imagePreview.info.font.pixelSize = t.fontS;
            t.imagePreview.info.font.weight = FontWeight::Normal;
            t.imagePreview.info.colors.get(WidgetState::Normal).text = Color4f{ 1.0f, 1.0f, 1.0f, 0.70f };

            // The preview scrim is translucent by default: dim until the host
            // background is still discernible (at 0.92 it was nearly pure
            // black, contradicting "the overlay is translucent"). Use
            // SetScrimColor for fully transparent or solid.
            t.imagePreview.scrim = Color4f{ 0.0f, 0.0f, 0.0f, 0.55f };
            t.imagePreview.controlGlyph = Color4f{ 1.0f, 1.0f, 1.0f, 0.88f };
            t.imagePreview.controlHoverBg = Color4f{ 1.0f, 1.0f, 1.0f, 0.16f };
            t.imagePreview.topBarHeight = 44.0f;
            t.imagePreview.bottomBarHeight = 30.0f;

            // ---- CanvasView ----
            //  Only visible on "the frame before the sub-window exists" and
            //  "while the canvas is hidden": background-matching color + a
            //  thin border, so the area does not look like a hole. Real
            //  content belongs to the external renderer, out of the theme's
            //  reach.
            t.canvas.background = t.bg;
            t.canvas.borderColor = t.border;
            t.canvas.borderWidth = 1.0f;

            // ---- GroupPanel ----
            t.groupPanel.background = t.surface;         // Must be opaque: the title "sits on the line" by masking a gap
            t.groupPanel.borderColor = t.border;
            t.groupPanel.borderWidth = 1.0f;
            t.groupPanel.radius = t.radiusM;
            t.groupPanel.padding = 12.0f;
            t.groupPanel.titleHeight = 22.0f;
            t.groupPanel.titlePaddingX = 6.0f;
            t.groupPanel.title.font.pixelSize = t.fontM;
            t.groupPanel.title.font.weight = 600;
            t.groupPanel.title.colors.get(WidgetState::Normal).text = t.text;
            t.groupPanel.title.colors.get(WidgetState::Disabled).text = t.muted;
            t.groupPanel.title.hAlign = TextHAlign::Left;
            t.groupPanel.title.vAlign = TextVAlign::Middle;

            // ---- TimePicker ----
            t.timePicker.box = t.input.box;
            t.timePicker.text = t.input.text;
            //  Popup = floating layer: surface background + border + explicit
            //  shadow (same family as combo.popup).
            t.timePicker.panel.colors.get(WidgetState::Normal).background = t.surface;
            t.timePicker.panel.colors.get(WidgetState::Normal).border = t.border;
            t.timePicker.panel.border.width[Border::Top] =
                t.timePicker.panel.border.width[Border::Right] =
                t.timePicker.panel.border.width[Border::Bottom] =
                t.timePicker.panel.border.width[Border::Left] = 1.0f;
            t.timePicker.panel.radiusMode = RadiusMode::Normal;
            t.timePicker.panel.padding = { 6.0f, 6.0f, 6.0f, 6.0f };
            t.timePicker.panel.shadow.enabled = true;
            t.timePicker.panel.shadow.color = t.shadow.withAlpha(0.22f);
            t.timePicker.panel.shadow.offsetY = 6.0f;
            t.timePicker.panel.shadow.spread = 2.0f;

            t.timePicker.head.font.pixelSize = t.fontS;
            t.timePicker.head.font.weight = 400;
            t.timePicker.head.colors.get(WidgetState::Normal).text = t.muted;
            t.timePicker.head.hAlign = TextHAlign::Center;
            t.timePicker.head.vAlign = TextVAlign::Middle;

            t.timePicker.value.font.pixelSize = t.fontM;
            t.timePicker.value.font.weight = 400;
            t.timePicker.value.colors.get(WidgetState::Normal).text = t.text;
            t.timePicker.value.colors.get(WidgetState::Disabled).text = t.disabled;
            t.timePicker.value.hAlign = TextHAlign::Center;
            t.timePicker.value.vAlign = TextVAlign::Middle;

            t.timePicker.action.font.pixelSize = t.fontS;
            t.timePicker.action.font.weight = 500;
            t.timePicker.action.colors.get(WidgetState::Normal).text = t.primary;
            t.timePicker.action.hAlign = TextHAlign::Center;
            t.timePicker.action.vAlign = TextVAlign::Middle;

            t.timePicker.itemHeight = 26.0f;
            t.timePicker.columnWidth = 46.0f;
            t.timePicker.headerHeight = 22.0f;
            t.timePicker.footerHeight = 28.0f;
            t.timePicker.gap = 4.0f;
            t.timePicker.radius = t.radiusS;
            t.timePicker.itemHoverColor = t.primary;
            t.timePicker.itemHoverColor.a = 0.10f;
            t.timePicker.itemSelectedColor = t.primary;
            t.timePicker.selectedTextColor = t.onPrimary;
            t.timePicker.arrowColor = t.muted;
            t.timePicker.dividerColor = t.border;

            // =============================================================
            //  2026-10-14 (second batch): rating / switch / watermark / loading
            // =============================================================
            //  Same discipline: everything derives from tokens, no hard-coded
            //  colors.

            // ---- Rating ----
            //  Selected stars use warning (amber) — gold is the conventional
            //  color for "rating" in UI.
            t.rating.activeColor = t.warning;
            //  Unselected stars: border color darkened one step (light gray in
            //  light themes, deep border color in dark ones)
            t.rating.inactiveColor = t.border;
            t.rating.inactiveColor.a = 0.55f;
            //  Hover preview: primary (distinct from "selected", so preview
            //  reads as preview at a glance)
            t.rating.hoverColor = t.primary;
            t.rating.hoverColor.a = 0.65f;
            t.rating.starSize = 22.0f;
            t.rating.gap = 2.0f;
            t.rating.hoverScale = 1.06f;

            // ---- Switch ----
            t.toggleSwitch.onColor = t.primary;
            t.toggleSwitch.offColor = t.border;
            t.toggleSwitch.onDisabledColor = t.primary;
            t.toggleSwitch.onDisabledColor.a = 0.38f;
            t.toggleSwitch.offDisabledColor = t.border;
            t.toggleSwitch.offDisabledColor.a = 0.55f;
            t.toggleSwitch.knobColor = Color4f::White();
            t.toggleSwitch.knobDisabledColor = Color4f::White();
            t.toggleSwitch.knobDisabledColor.a = 0.75f;
            t.toggleSwitch.trackWidth = 44.0f;
            t.toggleSwitch.trackHeight = 24.0f;
            t.toggleSwitch.knobInset = 2.0f;
            t.toggleSwitch.textGap = 8.0f;
            t.toggleSwitch.text.font.pixelSize = t.fontM;
            t.toggleSwitch.text.font.weight = 400;
            t.toggleSwitch.text.colors.get(WidgetState::Normal).text = t.text;
            t.toggleSwitch.text.colors.get(WidgetState::Disabled).text = t.disabled;
            t.toggleSwitch.text.hAlign = TextHAlign::Left;
            t.toggleSwitch.text.vAlign = TextVAlign::Middle;

            // ---- Watermark ----
            //  Visible without stealing attention: muted color + low alpha.
            //  The alpha is the intensity; to go fainter, change color.a (no
            //  separate opacity field needed).
            t.watermark.text.font.pixelSize = t.fontL;
            t.watermark.text.font.weight = 600;
            t.watermark.text.colors.get(WidgetState::Normal).text = t.muted;
            t.watermark.text.hAlign = TextHAlign::Left;
            t.watermark.text.vAlign = TextVAlign::Middle;
            t.watermark.color = t.muted;
            t.watermark.color.a = 0.12f;
            t.watermark.imageTint = Color4f::White();
            t.watermark.imageTint.a = 0.12f;
            t.watermark.gapX = 96.0f;
            t.watermark.gapY = 64.0f;
            t.watermark.offsetX = 0.0f;
            t.watermark.offsetY = 0.0f;

            // ---- Loading ----
            t.loading.color = t.primary;
            t.loading.trackColor = t.border;
            t.loading.trackColor.a = 0.45f;
            t.loading.size = 28.0f;
            t.loading.thickness = 3.0f;
            t.loading.textGap = t.spaceS;
            t.loading.itemCount = 8;
            t.loading.speedMs = 900;
            t.loading.text.font.pixelSize = t.fontM;
            t.loading.text.font.weight = 400;
            t.loading.text.colors.get(WidgetState::Normal).text = t.muted;
            t.loading.text.colors.get(WidgetState::Disabled).text = t.disabled;
            t.loading.text.hAlign = TextHAlign::Left;
            t.loading.text.vAlign = TextVAlign::Middle;

            // ---- FileDropZone ----
            //  Rest uses a dashed border: the industry-standard visual for a
            //  drop target ("throw things in here"). The render layer already
            //  supports BorderStyle::Dashed (see ZeFrameCollector's dashed
            //  stroke), so no hand-drawn small rectangles in the widget.
            //  Drag-over switches to solid + primary: the certain state of
            //  "release now to place".
            t.fileDropZone.box.colors.get(WidgetState::Normal).background = t.bg;
            //  The dashes must be visible to say "place here": muted
            //  translucent instead of border (border is the light gray meant
            //  for solid dividers; as dash segments it is nearly invisible —
            //  measured).
            t.fileDropZone.box.colors.get(WidgetState::Normal).border = t.muted;
            t.fileDropZone.box.colors.get(WidgetState::Normal).border.a = 0.45f;
            for (int i = 0; i < 4; ++i) t.fileDropZone.box.border.width[i] = 1.5f;
            //  Warning: this must use Solid, not Dashed. Observed:
            //  BorderStyle::Dashed goes through FrameCollector::AddDashedRect
            //  (laying small segments along the path), which in a dependent
            //  child window corrupts subsequent text rendering — every other
            //  character drops and the spacing goes ragged, and this block's
            //  own border also fails to appear. The same widget with dashes
            //  in a top-level window is fine (verified by
            //  live_text_dropzone_probe), so this is an AddDashedRect +
            //  child-window combination issue, to be fixed separately. Until
            //  then, the drop zone uses a 1.5px solid line at muted@45% —
            //  equally light in feel, and stable.
            t.fileDropZone.box.border.style = BorderStyle::Solid;
            t.fileDropZone.box.radiusMode = RadiusMode::Normal;
            t.fileDropZone.box.padding.left = t.spaceL;
            t.fileDropZone.box.padding.right = t.spaceL;
            t.fileDropZone.box.padding.top = t.spaceM;
            t.fileDropZone.box.padding.bottom = t.spaceM;

            //  Text inherits the label section and then overrides a few
            //  fields: the font file / weight / line height must match Label
            //  exactly. It was observed that assembling a TextStyle by hand
            //  with an empty font.file left this widget without the glyph set
            //  in a child window, and not only did it fail to draw text —
            //  every text drawn after it started dropping characters (every
            //  other one missing).
            t.fileDropZone.text = t.label.text;
            t.fileDropZone.text.font.pixelSize = t.fontM;
            //  Warning: the weight can only be 400 or 700. Weight 500 hits a
            //  bad entry in the font resource: its glyph set returns wrong
            //  metrics, causing (1) text laid out with wrong advances
            //  (missing characters, ragged spacing) and (2) the widget's
            //  PaintCache recording to fail midway — even the dashed box
            //  drawn by paintBox disappears (observed: text garbled first,
            //  then the whole box vanished). Back at 400 everything is fine.
            //  For emphasis use SetBold(true) (= 700).
            t.fileDropZone.text.font.weight = 400;
            t.fileDropZone.text.colors.get(WidgetState::Normal).text = t.muted;
            t.fileDropZone.text.colors.get(WidgetState::Disabled).text = t.disabled;
            t.fileDropZone.text.hAlign = TextHAlign::Center;
            t.fileDropZone.text.vAlign = TextVAlign::Middle;

            t.fileDropZone.subText = t.label.text;   // Same as above: the font must share Label's origin
            t.fileDropZone.subText.font.pixelSize = t.fontS;
            t.fileDropZone.subText.font.weight = 400;
            t.fileDropZone.subText.colors.get(WidgetState::Normal).text = t.muted;
            t.fileDropZone.subText.colors.get(WidgetState::Normal).text.a = 0.8f;
            t.fileDropZone.subText.hAlign = TextHAlign::Center;
            t.fileDropZone.subText.vAlign = TextVAlign::Middle;

            t.fileDropZone.activeBox = t.fileDropZone.box;
            t.fileDropZone.activeBox.border.style = BorderStyle::Solid;
            t.fileDropZone.activeBox.colors.get(WidgetState::Normal).border = t.primary;
            //  0.14 instead of 0.08: 8% is nearly invisible on a light
            //  background and users would think the drop zone did not
            //  respond. Drag-over is the only immediate feedback, so err on
            //  the visible side.
            for (int i = 0; i < 4; ++i) t.fileDropZone.activeBox.border.width[i] = 2.0f;
            t.fileDropZone.activeBox.colors.get(WidgetState::Normal).background = {
                t.primary.r, t.primary.g, t.primary.b, 0.14f };
            t.fileDropZone.activeText = t.primary;

            // =============================================================
            //  CodeEditor
            // =============================================================
            //  Box / font inherit the input section: the two share an origin
            //  (both are "boxes you type in"), so the look naturally matches;
            //  only the alignment is pinned to top-left (code is not a
            //  single-line input).

            t.code.box  = t.input.box;
            t.code.text = t.input.text;
            t.code.text.hAlign = TextHAlign::Left;
            t.code.text.vAlign = TextVAlign::Top;

            //  The background is what the code area actually paints (the input
            //  section's rest background = surface). The seven token colors
            //  all derive from it: light backgrounds keep the originals
            //  (contrast already suffices), dark backgrounds push them
            //  brighter automatically — the root cure for "black code text on
            //  dark themes": no hand-made palette per theme, and no missed
            //  token class.
            const Color4f codeBg =
                t.code.box.colors.get(WidgetState::Normal).background;

            // Body / identifiers = the theme's text color. Do not give
            // identifiers a separate color: identifiers make up most words in
            // code, and changing it recolors everything at once.
            t.code.tokenDefault    = t.text;
            t.code.tokenIdentifier = t.text;
            // Comments one step weaker (no competing with content), but still
            // readable: threshold lowered to 0.32
            t.code.tokenComment = ReadableOn(t.muted, codeBg, 0.32f);
            // Semantic colors (purple / green / orange / blue) keep their hue,
            // brightness pushed per background
            t.code.tokenKeyword      = ReadableOn(Color4f::FromHex(0x7C3AED), codeBg);
            t.code.tokenString       = ReadableOn(Color4f::FromHex(0x16A34A), codeBg);
            t.code.tokenNumber       = ReadableOn(Color4f::FromHex(0xEA580C), codeBg);
            t.code.tokenPreprocessor = ReadableOn(Color4f::FromHex(0x2563EB), codeBg);

            // Derive any private fields of ButtonStyle / CardStyle /
            // LabelStyle / InputStyle here as well
        }

        // Light theme (currently the only built-in light theme).
        inline Theme Light() {
            Theme t;
            t.name = "light";
            Derive(t);
            return t;
        }

        // Dark theme.
        //
        // Only the color tokens are overridden, then Derive() re-derives all
        // component styles as usual — exactly the payoff of "component styles
        // must derive from tokens": adding a palette does not require
        // rewriting every component (button / card / label six-state colors,
        // borders, padding all follow automatically).
        //
        // Palette intent: a dark blue-gray background (not pure black,
        // avoiding large OLED black areas and smeared edges), accents one
        // step brighter than the light theme (pure colors need higher
        // brightness to pop on dark backgrounds).
        //
        // Chosen automatically by Window when following the system dark mode,
        // see Window::SetFollowSystemDarkMode / SetDarkMode / isDarkMode.
        inline Theme Dark() {
            Theme t;
            t.name = "dark";

            // ---- Color tokens ----
            t.bg = Color4f::FromHex(0x0B1220);   // Canvas background (Window uses it for glClearColor)
            t.surface = Color4f::FromHex(0x151E2E);   // Card / popup background
            t.border = Color4f::FromHex(0x2A3650);
            t.text = Color4f::FromHex(0xE6EDF7);
            t.muted = Color4f::FromHex(0x94A3B8);
            t.disabled = Color4f::FromHex(0x475569);

            // Accents must be brighter on dark backgrounds, or the blue
            // smears into the background
            t.primary = Color4f::FromHex(0x3B82F6);
            t.primaryHover = Color4f::FromHex(0x2563EB);
            t.primaryPress = Color4f::FromHex(0x1D4ED8);
            t.onPrimary = Color4f::White();

            t.danger = Color4f::FromHex(0xF87171);
            t.success = Color4f::FromHex(0x4ADE80);
            t.warning = Color4f::FromHex(0xFBBF24);

            // Size / font tokens match the light theme (dark mode does not
            // mean "smaller")
            Derive(t);
            return t;
        }

        // Pick a built-in theme by darkness (Window uses this when following
        // the system).
        inline Theme ForDarkMode(bool dark) {
            return dark ? Dark() : Light();
        }

        // Flip a theme to the target polarity (light <-> dark) while keeping
        // its color scheme.
        //
        // Why it exists: the system dark mode only says "the user wants a
        // dark background", not "the user wants a different palette". After
        // an app picks `Ocean` (sea blue + big radii), switching the system
        // to dark should yield "sea blue + big radii + dark background", not
        // a fallback to the built-in dark blue.
        //
        // Method (same technique as CodeStyle, see the CodeStyle part of
        // Derive):
        //   - Neutral tokens (bg / surface / border / text / muted / disabled
        //     / shadow) borrow the built-in Light / Dark set directly; they
        //     are the answer to "dark/light background" itself;
        //   - Accent colors (primary / danger / success / warning) keep their
        //     hue and are pushed to readable brightness on the new background
        //     via ReadableOn — blue stays blue, just one step brighter on
        //     dark;
        //   - onPrimary takes black/white by primary's luminance (Nord-style
        //     light primaries need dark text);
        //   - Sizes / font sizes / radii / spacing untouched (dark mode does
        //     not mean "smaller").
        //
        // base: the original theme (the app's pick); dark: target polarity;
        // minDelta: minimum luminance difference between accents and
        // background (see ReadableOn). Returns the recolored theme; if
        // `base` is already at the target polarity it is returned as is
        // (idempotent).
        //
        // Idempotency matters: Window recomputes from the app-provided theme
        // on every system event (not from the previous result), so toggling
        // back and forth never drifts. See ForDarkMode(const Theme&, bool).
        inline Theme Recolor(const Theme& base, bool dark, float minDelta = 0.38f) {
            const bool baseDark = Luminance(base.bg) < 0.5f;
            if (baseDark == dark) return base;      // Already at the target polarity — leave it alone

            Theme t = base;
            const Theme ref = dark ? Dark() : Light();

            // ---- Neutral tokens: borrow the "dark/light background" set directly ----
            t.bg       = ref.bg;
            t.surface  = ref.surface;
            t.border   = ref.border;
            t.text     = ref.text;
            t.muted    = ref.muted;
            t.disabled = ref.disabled;
            t.shadow   = ref.shadow;

            // ---- Accents: keep hue, push to readable on the new background ----
            t.primary      = ReadableOn(t.primary,      t.bg, minDelta);
            t.primaryHover = ReadableOn(t.primaryHover, t.bg, minDelta);
            t.primaryPress = ReadableOn(t.primaryPress, t.bg, minDelta);
            t.danger       = ReadableOn(t.danger,       t.bg, minDelta);
            t.success      = ReadableOn(t.success,      t.bg, minDelta);
            t.warning      = ReadableOn(t.warning,      t.bg, minDelta);
            t.onPrimary    = (Luminance(t.primary) > 0.55f)
                           ? Color4f::FromHex(0x0B1220) : Color4f::White();

            // The name gets a polarity suffix, so logs / traces show "this is
            // the flipped ocean"
            if (!t.name.empty()) t.name += (dark ? "-dark" : "-light");

            Derive(t);   // All component styles re-derived from tokens (the library's single rule)
            return t;
        }

        // Get the corresponding light/dark version of a given theme (for apps
        // that pick their own palette and follow the system).
        //
        // base: the theme the app picked (the copy Window::SetTheme kept);
        // dark: target polarity. Returns the dark / light version of `base`
        // (returned as is if already at the target polarity). See Recolor.
        inline Theme ForDarkMode(const Theme& base, bool dark) {
            return Recolor(base, dark);
        }

        // Ocean light theme — a demonstration of the "big radii + cool colors"
        // design language.
        //
        // The difference from light is not "a different blue" but three token
        // axes moving together:
        //   - Radii: radiusS/M/L all enlarged (8/14/20), so every widget using
        //     RadiusMode::Small/Normal/Large (buttons, inputs, cards, popups,
        //     ...) follows automatically, no per-widget edits;
        //   - Shadow: the shadow token turns bluish (cool shadow), making the
        //     floating layers feel softer;
        //   - Accent: primary becomes sea blue.
        // This is the payoff of "styles derive from tokens": a new look = one
        // new token table.
        inline Theme Ocean() {
            Theme t;
            t.name = "ocean";

            t.bg = Color4f::FromHex(0xF0F7FB);
            t.surface = Color4f::FromHex(0xFFFFFF);
            t.border = Color4f::FromHex(0xC9DCE8);
            t.text = Color4f::FromHex(0x12333F);
            t.muted = Color4f::FromHex(0x5B7684);
            t.disabled = Color4f::FromHex(0x92A9B5);

            t.primary = Color4f::FromHex(0x0284C7);
            t.primaryHover = Color4f::FromHex(0x0369A1);
            t.primaryPress = Color4f::FromHex(0x075985);
            t.onPrimary = Color4f::White();

            t.danger = Color4f::FromHex(0xDC2626);
            t.success = Color4f::FromHex(0x059669);
            t.warning = Color4f::FromHex(0xD97706);

            t.shadow = Color4f::FromHex(0x0C4A6E);   // Cool shadow

            t.radiusS = 8.0f; t.radiusM = 14.0f; t.radiusL = 20.0f;

            Derive(t);
            return t;
        }

        // Nord dark theme — Nordic minimal palette (Polar Night + Frost).
        //
        // Nord (nordtheme) is one of the most enduring dark palettes in
        // editor circles. Two intentional differences from the default Dark:
        //   - primary is light cyan (88C0D0); large solid buttons on a dark
        //     background get harsher the brighter they are, so onPrimary uses
        //     dark text (2E3440) instead of white, making solid buttons
        //     "bright background, dark text", closer to VS Code's light
        //     accent blocks;
        //   - Radii one step larger than default (radiusM=10), matching
        //     Nord's rounded minimalism.
        inline Theme Nord() {
            Theme t;
            t.name = "nord";

            t.bg = Color4f::FromHex(0x2E3440);       // Polar Night 0
            t.surface = Color4f::FromHex(0x3B4252);  // Polar Night 1
            t.border = Color4f::FromHex(0x4C566A);   // Polar Night 2
            t.text = Color4f::FromHex(0xECEFF4);     // Snow Storm
            t.muted = Color4f::FromHex(0x9DA8B8);
            t.disabled = Color4f::FromHex(0x616E88);

            t.primary = Color4f::FromHex(0x88C0D0);  // Frost light cyan
            t.primaryHover = Color4f::FromHex(0x8FBCBB);
            t.primaryPress = Color4f::FromHex(0x81A1C1);
            t.onPrimary = Color4f::FromHex(0x2E3440);   // Dark text: primary is light, white has no contrast

            t.danger = Color4f::FromHex(0xBF616A);   // Aurora red
            t.success = Color4f::FromHex(0xA3BE8C);  // Aurora green
            t.warning = Color4f::FromHex(0xEBCB8B);  // Aurora yellow

            t.radiusS = 6.0f; t.radiusM = 10.0f; t.radiusL = 16.0f;

            Derive(t);
            return t;
        }

        // Warm paper light theme — a demonstration of the "small radii + warm
        // colors" design language.
        //
        // Most interesting in contrast with ocean: both are light, but radii
        // shrink across the line (2/6/10) + an ochre primary + an off-white
        // background. Put side by side, the abstract word "design language"
        // becomes concrete. The primary is a warm orange-ochre, with
        // hover/press darkening step by step (same derivation rule as
        // WithAccent).
        inline Theme Warm() {
            Theme t;
            t.name = "warm";

            t.bg = Color4f::FromHex(0xFDF8F1);       // Warm paper beige
            t.surface = Color4f::FromHex(0xFFFEFB);
            t.border = Color4f::FromHex(0xEBDFCE);
            t.text = Color4f::FromHex(0x33291C);
            t.muted = Color4f::FromHex(0x7D6B53);
            t.disabled = Color4f::FromHex(0xB0A189);

            t.primary = Color4f::FromHex(0xEA580C);  // Ochre orange
            t.primaryHover = Color4f::FromHex(0xC2410C);
            t.primaryPress = Color4f::FromHex(0x9A3412);
            t.onPrimary = Color4f::White();

            t.danger = Color4f::FromHex(0xB91C1C);
            t.success = Color4f::FromHex(0x4D7C0F);
            t.warning = Color4f::FromHex(0xB45309);

            t.shadow = Color4f::FromHex(0x713F12);   // Warm brown shadow

            t.radiusS = 2.0f; t.radiusM = 6.0f; t.radiusL = 10.0f;

            Derive(t);
            return t;
        }

        // -------------------------------------------------------------------
        //  Theme customization: derive a whole accent family from one color
        //  (no need to hand-write the six hover/press steps)
        // -------------------------------------------------------------------

        // Replace a theme's whole primary family (hover/press derived
        // automatically).
        //
        // The derivation matches both built-in themes: hover = darken 8%,
        // press = darken 16%; both built-ins get darker as pressed, so no
        // polarity split here. After the tokens change, Derive runs again, so
        // button six-states, slider outlines, list hover backgrounds, etc.
        // all follow: change one place, everything moves.
        //
        // Example (purple-accented ocean theme):
        //   win.SetTheme(Kit::Themes::WithAccent(Kit::Themes::Get("ocean"),
        //                                         Kit::Color4f::FromHex(0x7C3AED)));
        inline Theme WithAccent(Theme t, const Color4f& accent) {
            t.primary = accent;
            t.primaryHover = accent.darken(0.08f);
            t.primaryPress = accent.darken(0.16f);
            Derive(t);
            return t;
        }

        // -------------------------------------------------------------------
        //  Theme registry: get a theme by name (the backend of
        //  Window::SetThemeByName)
        // -------------------------------------------------------------------
        //  Built-ins are registered up front; user themes are added via
        //  Register(). The registry stores factories, not Theme instances:
        //  a Theme is a big pile of data (dozens of style sections), so
        //  storing factories means building only on use, with zero startup
        //  cost.
        // -------------------------------------------------------------------

        // The registry itself (function-local static: initialized on first
        // call, thread-safe).
        inline std::map<std::string, std::function<Theme()>>& registry() {
            static std::map<std::string, std::function<Theme()>> r = {
                {"light", [] { return Light(); }},
                {"dark",  [] { return Dark(); }},
                {"ocean", [] { return Ocean(); }},
                {"nord",  [] { return Nord(); }},
                {"warm",  [] { return Warm(); }},
            };
            return r;
        }

        // Register a custom theme (a duplicate name replaces the old factory).
        // Example:
        //   Kit::Themes::Register("mine", [] {
        //       Kit::Theme t = Kit::Themes::Get("dark");
        //       t.name = "mine";
        //       t.bg = Kit::Color4f::FromHex(0x1A1B26);
        //       // ... change tokens ...
        //       Kit::Themes::Derive(t);
        //       return t;
        //   });
        //   win.SetThemeByName("mine");
        inline void Register(std::string name, std::function<Theme()> factory) {
            registry()[std::move(name)] = std::move(factory);
        }

        // Whether a theme with this name is registered (built-ins and user
        // registrations both count).
        inline bool Has(const std::string& name) {
            return registry().count(name) != 0;
        }

        // Names of all registered themes (in registration order: built-ins
        // first, user ones after).
        inline std::vector<std::string> Names() {
            std::vector<std::string> out;
            out.reserve(registry().size());
            for (const auto& kv : registry()) out.push_back(kv.first);
            return out;
        }

        // Get a theme by name.
        // name: a name in the registry (see Names()). Returns the matching
        // theme; unregistered names fall back to Light() (the caller gets no
        // "failure" signal; use Has() or Window::SetThemeByName — the latter
        // returns bool — to detect failure).
        inline Theme Get(const std::string& name) {
            auto& r = registry();
            auto it = r.find(name);
            return it != r.end() ? it->second() : Light();
        }
    } // namespace Themes

} // namespace Kit