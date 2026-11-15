// FontDesc.h - "what font is wanted": file / pixel size / weight / italic.
// The library uses it as the font-cache key; equal descs share one font resource.
// Empty file path = built-in default font. Underline/color/alignment live in TextStyle.

#pragma once
#include <cstdint>
#include <string>
#include <tuple>

namespace Kit {

    // CSS font-weight levels
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

    // Font description. Any field change yields a new cache key (a new font resource).
    struct FontDesc {
        std::string  file;              // font file path, empty = built-in default
        unsigned int pixelSize = 24;    // render pixel size
        uint16_t     weight = 400;      // 100..900, 400 = regular, 700 = bold
        bool         italic = false;

        bool IsBold() const { return weight >= FontWeight::Bold; }
        void SetBold(bool on) { weight = on ? FontWeight::Bold : FontWeight::Normal; }

        // cache key / comparison, in field order
        auto tie() const {
            return std::tie(file, pixelSize, weight, italic);
        }

        bool operator<(const FontDesc& o) const { return tie() < o.tie(); }
        bool operator==(const FontDesc& o) const { return tie() == o.tie(); }
    };

    // Result of FrameCollector::MeasureText, shared with widgets for layout and alignment.
    struct TextMetrics {
        float width = 0.f;     // visual width (sum of advances)
        float height = 0.f;    // ascent + descent, i.e. line height
        float ascent = 0.f;    // above baseline
        float descent = 0.f;   // below baseline
    };

} // namespace Kit
