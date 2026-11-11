/**
 * @file ZeBoxStyle.h
 * @brief Box visual style -- style fields shared by every "visible rectangle" in the library
 *
 * @details
 * All visible rectangle widgets (Button / Card / Panel / Input / Splitter / Scrollbar)
 * share the types in this file. There are only two rules:
 *
 *  - Each thing is defined once in the whole library (no parallel definitions such as ButtonRadiusMode);
 *  - Only pure visual fields go here -- geometry belongs to Widget, layout belongs to FlexStyle.
 *
 * @see ZeTextStyle.h text style
 * @see ZeFlexStyle.h layout style
 */

#pragma once
#include <cassert>
#include <string>
#include "color4f.h"
#include "image_types.h"
#include "ZeCursorKind.h"   // Kit::Cursor (leaf enum, shared by the widget layer and the platform layer)

namespace Kit {

    // ===================================================================
    //  Enums: express "pick one from a few fixed options"
    // ===================================================================

    /// @brief Interaction state (also used as the index into the state color array)
    enum class WidgetState {
        Normal,
        Hover,
        Pressed,
        Disabled,
        Loading,
        Focused,
        Count   /// < Sentinel, must always be last
    };

    /// @brief Corner radius mode (preset levels; exact values go through Custom)
    enum class RadiusMode {
        None,
        Small,      /// < theme radiusS
        Normal,     /// < theme radiusM
        Large,      /// < theme radiusL
        Capsule,    /// < half of the short side
        Custom      /// < uses BoxStyle::customRadius
    };

    /// @brief Border line style (only those the GUI can actually draw)
    enum class BorderStyle {
        None,
        Solid,
        Dashed,
        Dotted
    };

    /// @brief Mouse cursor style -- **moved up to `ZeCursorKind.h`** (the `Plat` layer needs it too)
    /// @note Only a note is kept here; the definition is over there; the visibility of `Kit::Cursor` is unchanged for old code.

    // ===================================================================
    //  Sub-structures: group related fields together
    // ===================================================================

    /// @brief Color set for a single state
    /**
     * @brief The four slots used by one color lookup (one copy per state)
     *
     * @details
     * This structure is reused in two places, **and the slots belong to different owners**:
     *  - `background` / `border` -> box (`BoxStyle::colors`), fetched by `BoxWidget::resolvedColors`;
     *  - `text` / `decoration`   -> text (`TextStyle::colors`), fetched by `TextWidget::resolvedTextColors`.
     * Reading the box copy when painting text makes SetTextColor a dead interface (the default is opaque black -> text is always black);
     * this pitfall was actually hit, see the comment on TextWidget::resolvedTextColors.
     */
    struct ColorSet {
        Color4f background{ 0.0f, 0.0f, 0.0f, 0.0f };   /// < background fill color (box slot)
        Color4f border{ 0.0f, 0.0f, 0.0f, 0.0f };   /// < border color (box slot)
        Color4f text{ 0.0f, 0.0f, 0.0f, 1.0f };   /// < text color (text slot)
        Color4f decoration{ 0.0f, 0.0f, 0.0f, 1.0f };   /// < decoration line color (text slot)
    };

    /**
     * @brief Multi-state color set
     *
     * @details
     * The index is static_cast<int>(WidgetState).
     * To add a state, just add an entry to WidgetState; the array length follows automatically.
     */
    struct MultiStateColors {
        ColorSet sets[static_cast<int>(WidgetState::Count)];

        const ColorSet& get(WidgetState s) const {
            const int i = static_cast<int>(s);
            assert(i >= 0 && i < static_cast<int>(WidgetState::Count));
            return sets[i];
        }
        ColorSet& get(WidgetState s) {
            const int i = static_cast<int>(s);
            assert(i >= 0 && i < static_cast<int>(WidgetState::Count));
            return sets[i];
        }
    };

    /**
     * @brief Border
     *
     * @details
     * Width is independent per side (indexed with Border::Top / Right / Bottom / Left),
     * style is a single value (shared by all four sides), color varies by state and is not stored here.
     */
    struct Border {
        // Four-side indices, to avoid magic numbers width[0]/[1]/[2]/[3] reading the wrong direction
        static constexpr int Top = 0;
        static constexpr int Right = 1;
        static constexpr int Bottom = 2;
        static constexpr int Left = 3;

        float       width[4] = { 0.0f, 0.0f, 0.0f, 0.0f };  /// < top right bottom left
        BorderStyle style = BorderStyle::Solid;
    };

    /// @brief Padding (independent per side)
    struct Padding {
        float left = 0.0f;
        float top = 0.0f;
        float right = 0.0f;
        float bottom = 0.0f;
    };

    /// @brief Shadow (single shadow for now; switch to an array when stacking is needed)
    struct Shadow {
        bool    enabled = false;
        Color4f color{ 0.0f, 0.0f, 0.0f, 0.30f };
        float   offsetX = 0.0f;
        float   offsetY = 4.0f;
        float   blur = 4.0f;
        float   spread = 0.0f;
    };

    /// @brief Outline (outer stroke that takes no layout space; used for the focus ring)
    struct Outline {
        bool        enabled = false;
        float       width = 0.0f;
        Color4f     color{ 0.0f, 0.0f, 0.0f, 0.0f };
        BorderStyle style = BorderStyle::Solid;
        float       offset = 0.0f;   /// < how many pixels to expand outward
    };

    // ===================================================================
    //  Main structure: aggregate all fields into one box style
    // ===================================================================

    /// @brief Box visual style (shared by all visible rectangle widgets)
    struct BoxStyle {
        // ---- Colors (by state) ----
        MultiStateColors colors;

        // ---- Background image ----
        //  Two ways to supply an image, **path takes priority**:
        //   - imagePath non-empty -> use the file path (the widget layer stores only the string; decoding / texture upload
        //     belongs to the render layer, so the config phase needs no window and certainly no graphics context -- same pattern as the Button icon);
        //   - otherwise look at image.handle (a pre-resolved handle, used for atlases / when several widgets share one texture).
        //  Paint geometry is uniformly delegated to FrameCollector::AddImage; the widget layer does not reimplement it.
        //  WARNING: switching backends (UseRenderer) invalidates old texture handles -- so unless you know for sure
        //     the backend will not change, use imagePath, not image.handle.
        std::string     imagePath;   /// < background image file path (empty = no background image)
        BackgroundImage image;       /// < pre-resolved texture handle (optional, only consulted when the path is empty)
        BgImageDrawMode drawMode = BgImageDrawMode::Stretch;
        BgImageHAlign   hAlign = BgImageHAlign::Center;
        BgImageVAlign   vAlign = BgImageVAlign::Middle;
        //  Tint: a vertex color **multiplied component-wise with the texture**, so white = output as-is.
        //  WARNING: do not use Transparent() (0,0,0,0) as the default -- that multiplies the image to fully transparent and nothing shows.
        //     Keep the same default as FrameCollector::AddImage / the Button icon tint.
        Color4f         tint = Color4f::White();

        // ---- Border / corner radius ----
        Border     border;
        RadiusMode radiusMode = RadiusMode::None;
        float      customRadius = 0.0f;   /// < used only when radiusMode == Custom

        // ---- Padding ----
        Padding padding;

        // ---- Visual effects ----
        Shadow  shadow;
        Outline outline;
        float   opacity = 1.0f;
        /// @brief WARNING: **legacy field, no consumers** -- the cursor is now declared by `Widget::SetCursor`
        ///        and pushed to the platform layer by `Window` after the hit test (see dispatchMouseMove in Zekit.cpp).
        ///        This field is kept only so that layout changes in `BoxStyle` do not disturb other code;
        ///        new code should use `Widget::SetCursor`.
        Cursor  cursor = Cursor::Arrow;
    };

} // namespace Kit