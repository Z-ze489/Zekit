/**
 * @file ZeTextStyle.h
 * @brief Text visual style -- typography fields shared by all text-bearing widgets in the library
 *
 * @details
 * All widgets with text (Button / Label / Input / TextArea) share this file.
 * Layout containers, cards and other widgets without text do not compose this file.
 *
 * Rules:
 *  - Only pure typography fields -- font, color, alignment, decoration, line height, letter spacing;
 *  - No duplicate definitions -- no parallel enums such as ButtonTextHAlign may appear in the library;
 *  - Text color is per state -- text also changes color on hover, so the color fields are multi-state too.
 *
 * @note Responsibility boundary with FontDesc.h:
 *       - FontDesc only describes "which font is wanted" (path / size / weight / italic);
 *       - underline / strikethrough / overline belong to TextDecoration in this file,
 *         FontDesc should **not** contain underline / strikethrough / overline.
 *       If your current FontDesc has them, delete them.
 *
 * @see ZeBoxStyle.h box style
 * @see ZeFlexStyle.h layout style
 */

#pragma once
#include "color4f.h"
#include "FontDesc.h"
#include "ZeBoxStyle.h"   // Reuses MultiStateColors (the single structure for per-state colors)

namespace Kit {

    // ===================================================================
    //  Enums: pick one from a few fixed options
    // ===================================================================

    /// @brief Text horizontal alignment
    enum class TextHAlign {
        Left,
        Center,
        Right
    };

    /// @brief Text vertical alignment
    enum class TextVAlign {
        Top,
        Middle,
        Bottom
    };

    /// @brief Text decoration lines (only the three used by GUI)
    enum class TextDecoration {
        None,
        Underline,      /// < underline
        LineThrough,    /// < strikethrough
        Overline        /// < overline
    };

    // ===================================================================
    //  Main structure: text visual style
    // ===================================================================

    /**
     * @brief Text typography style
     *
     * @details
     * Any widget with text composes this structure to get full typography capability.
     * Colors are per state; the remaining fields are single-valued.
     *
     * Default construction = "invisible text" (all colors transparent); visual values are injected by Theme.
     *
     * @note colors only uses the text and decoration fields of ColorSet;
     *       the background / border fields are meaningless in TextStyle (that is the box's job).
     *       This is the slight redundancy of "reusing the same per-state color structure", acceptable.
     */
    struct TextStyle {
        // ---- Font ----
        FontDesc font;

        // ---- Colors (per state) ----
        MultiStateColors colors;

        // ---- Alignment ----
        TextHAlign hAlign = TextHAlign::Center;
        TextVAlign vAlign = TextVAlign::Middle;

        // ---- Decoration ----
        TextDecoration decoration = TextDecoration::None;

        // ---- Line height / letter spacing ----
        float lineHeight = 0.0f;   /// < 0 = auto (follows font size)
        float letterSpacing = 0.0f;   /// < 0 = default letter spacing
    };

} // namespace Kit