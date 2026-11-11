/**
 * @file FontDesc.h
 * @brief Font description type (the unified font parameter carrier across the ZeWin library)
 *
 * @details
 * FontDesc describes "what font is wanted", not the font resource itself:
 *  - The library uses FontDesc as a key to look up / create font resources in the font cache;
 *  - Widgets with identical descriptions share the same font resource, avoiding repeated loading;
 *  - All widgets that need text (Button, Label, Text, InputBox, etc.) use it through
 *    SetFontDesc / SetFontFile / SetFontSize and similar interfaces.
 *
 * When the file path is empty, the library built-in default font is used, so the simplest usage is to specify only the font size.
 *
 * Responsibility boundaries (important):
 *  - This file describes only the font itself (path / size / weight / italic);
 *  - Underline / strikethrough / overline are not here -- they belong to TextStyle.decoration;
 *  - Text color / alignment / line height / letter spacing are also not here -- they belong to TextStyle.
 *
 * @note The struct overloads operator< and operator==, so it can be placed directly into std::map / std::set.
 * @note Modifying any field yields a different cache key, which triggers creation of a new font resource.
 *
 * @par Basic usage
 * @code
 * #include "FontDesc.h"
 *
 * Kit::FontDesc desc;
 * desc.file      = "C:/Windows/Fonts/msyh.ttc";  // leave empty to use the built-in default font
 * desc.pixelSize = 16;
 * desc.weight    = Kit::FontWeight::Bold;        // 700
 * label.SetFontDesc(desc);
 * @endcode
 *
 * @see color4f.h Color type
 * @see ZeTextStyle.h Text style (underline/color/alignment etc. are not here)
 */

#pragma once
#include <cstdint>
#include <string>
#include <tuple>

namespace Kit {

    /// @brief Common font weight constants (matching the nine CSS font-weight levels)
    namespace FontWeight {
        constexpr uint16_t Thin = 100;
        constexpr uint16_t ExtraLight = 200;
        constexpr uint16_t Light = 300;
        constexpr uint16_t Normal = 400;
        constexpr uint16_t Medium = 500;
        constexpr uint16_t SemiBold = 600;
        constexpr uint16_t Bold = 700;
        constexpr uint16_t ExtraBold = 800;
        constexpr uint16_t Black = 900;
    }

    /**
     * @brief Font description, used to look up / create font resources
     *
     * @details
     * A set of plain data fields that fully characterize "what font is wanted". It serves both as the
     * "lookup condition" for font resources and as the key of the font cache (see @ref tie).
     *
     * Weight is numeric (100~900), consistent with CSS; bold is just a
     * convenience expression of weight >= 700, read and written via SetBold / IsBold.
     *
     * @par Example: construct a font description that is italic, semi-bold, 20px
     * @code
     * Kit::FontDesc f;
     * f.pixelSize = 20;
     * f.weight    = Kit::FontWeight::SemiBold;
     * f.italic    = true;
     *
     * if (f == other) {
     *     // identical description, the same font resource can be reused
     * }
     * @endcode
     */
    struct FontDesc {
        std::string  file;              /// < Font file path, empty = use the built-in default font
        unsigned int pixelSize = 24;    /// < Render pixel size
        uint16_t     weight = 400;   /// < Font weight (100~900, 400 = regular, 700 = bold)
        bool         italic = false; /// < Italic

        // ---- Convenience accessors (not part of the cache key, only a semantic wrapper over weight) ----
        // @brief Whether bold (weight >= 700)
        bool IsBold() const { return weight >= FontWeight::Bold; }
        /// @brief Sets bold: true -> 700, false -> 400 (overwrites the original weight)
        void SetBold(bool on) { weight = on ? FontWeight::Bold : FontWeight::Normal; }

        /**
         * @brief Packs all fields participating in comparison into a tuple, used to generate the cache key
         * @return A reference tuple ordered as file / pixelSize / weight / italic
         * @note For internal font cache and comparison operators only; generally no need to call directly.
         * @note Underline/strikethrough/overline no longer belong to the font description, so they are not in the key.
         */
        auto tie() const {
            return std::tie(file, pixelSize, weight, italic);
        }

        /// @brief Lexicographic comparison, used to make FontDesc a key of std::map / std::set
        bool operator<(const FontDesc& o) const { return tie() < o.tie(); }

        /// @brief All-field equality comparison (will hit the same font cache)
        bool operator==(const FontDesc& o) const { return tie() == o.tie(); }
    };

    /**
     * @brief Measurement result of a piece of text ("how large FontDesc measures it")
     *
     * @details
     * Measured by the render layer (`FrameCollector::MeasureText`), used by widgets for alignment, centering,
     * and future line wrapping / ellipsis / cursor positioning.
     *
     * Reason for placing it in a public header: this is a public contract needed by layout (Label line wrapping, Input cursor position
     * both need it), and widgets should not include the render layer's internal headers.
     *
     * @note Measurement and painting share the same set of glyph metrics (bearingX / advanceX),
     *       so positions computed from it exactly match the text actually painted.
     */
    struct TextMetrics {
        float width = 0.f;     /// < Visual width (sum of advances, excluding extra leading/trailing whitespace)
        float height = 0.f;    /// < Visual height (ascent + descent, i.e. line height)
        float ascent = 0.f;    /// < Height above the baseline (ascender, positive)
        float descent = 0.f;   /// < Height below the baseline (descender, positive)
    };

} // namespace Kit