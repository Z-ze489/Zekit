#pragma once

#include <ft2build.h>
#include FT_FREETYPE_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ftw {
    // Unified exception type
	class FreeTypeError : public std::runtime_error {
	public:
		explicit FreeTypeError(const std::string& msg)
			:std::runtime_error("[FreeType]" + msg){}
	};

    // Bitmap and metrics of a single glyph (plain value type, copyable)
    struct GlyphBitmap {
        int width = 0;   // Bitmap width (pixels)
        int height = 0;   // Bitmap height (pixels)
        int pitch = 0;   // Bytes per row (includes FreeType alignment padding, often != width)
        int bearingX = 0;   // Horizontal offset relative to the baseline
        int bearingY = 0;   // Vertical offset relative to the baseline (bitmap_top)
        int advanceX = 0;   // Horizontal advance to the next glyph (pixels)
        int advanceY = 0;   // Vertical advance (usually 0)
        bool isColor = false;
        std::vector<uint8_t> buffer; // 1 byte per pixel (grayscale) or 4 bytes (BGRA color), row stride is pitch
    };

    // ---- Library management: singleton + shared lifetime ----
    //
    //  WARNING: Threading model (required reading when changing the font pipeline)
    //
    //  FreeType's `FT_Library` is not thread-safe, and this unsafety is
    //  instance-level: two FT_Face objects on the same FT_Library also cannot be used
    //  by two threads at once (the internal memory manager, module list and glyph slot
    //  buffers are all shared at library level).
    //
    //  The official guidance offers only two options:
    //    1. Lock everywhere (this degrades parallelism to serial execution);
    //    2. One FT_Library per thread (each thread calls FT_Init_FreeType itself).
    //
    //  Zekit takes option 2: the UI thread uses the `getInstance()` singleton for
    //  measurement (advance / ascender / synchronous rasterization), and the glyph
    //  worker thread calls `createIndependent()` for a library of its own -- each side
    //  has its own FT_Library + FT_Face, with zero sharing, zero locking and zero
    //  cross-thread calls. See GlyphWorker in ZeFontRenderer.cpp.
    class Library {
    public:
        static std::shared_ptr<Library> getInstance();

        /// @brief Creates a library independent of the singleton, for exclusive use by another thread
        /// @note The only difference from getInstance() is that nothing is shared. The creator uses it; do not pass it across threads.
        static std::shared_ptr<Library> createIndependent();

        FT_Library handle() const { return library_; }

        Library(const Library&) = delete;
        Library& operator=(const Library&) = delete;

        ~Library();

    private:
        Library();
        FT_Library library_ = nullptr;
    };

    // ---- Font face ----
    class FontFace {
    public:
        FontFace() = default;

        // Constructs and loads directly
        FontFace(const std::string& fontPath, unsigned int pixelHeight,
            unsigned int faceIndex = 0);

        /// @brief Loads with the specified library (does not touch the singleton)
        /// @param library The library exclusively owned by that thread (see the threading model notes on Library)
        /// @note The glyph worker thread must use this overload -- the default constructor
        ///       falls back to the process-wide shared `Library::getInstance()` library,
        ///       so two threads touch the same FT_Library at once (undefined behavior;
        ///       symptoms are occasional garbled text / misplaced glyphs / FreeType
        ///       internal assertions).
        FontFace(const std::shared_ptr<Library>& library,
            const std::string& fontPath, unsigned int pixelHeight,
            unsigned int faceIndex = 0);

        ~FontFace();

        // Copying is forbidden, moving is allowed
        FontFace(const FontFace&) = delete;
        FontFace& operator=(const FontFace&) = delete;
        FontFace(FontFace&& other) noexcept;
        FontFace& operator=(FontFace&& other) noexcept;

        // Loads a font file (UTF-8 path)
        void load(const std::string& fontPath, unsigned int pixelHeight,
            unsigned int faceIndex = 0);

        /// @brief Loads with the specified library (thread-exclusive library version; see the constructor overload above)
        /// @param faceIndex Which glyph set inside a font collection (.ttc / .otc).
        /// @warning Do not assume 0 is fine. A font collection often holds JP / KR / SC / TC
        ///          glyph sets at once, and the same code point has different forms under
        ///          different faces -- these are not minor variations of one shape: the
        ///          Japanese form of the character for "straight" has one fewer horizontal
        ///          stroke inside, the horizontal stroke in the upper box of the character
        ///          for "bone" points left in Chinese but right in Japanese, and the
        ///          character for "each" is written differently in Japanese. Passing 0 on
        ///          NotoSansCJK-Regular.ttc yields exactly the Japanese face -- so Chinese
        ///          UIs show "incomplete strokes". (Windows msyh.ttc has a Chinese first
        ///          face, so this only shows up on Linux / collections with multiple
        ///          language faces.)
        ///          Which face to pick is decided by `PickPreferredFace` in
        ///          `ZeFreeTypeWrapper.cpp` (prefers SC by family name), and
        ///          `createDefaultFontFaces` already wires it up; only when loading a font
        ///          file directly yourself do you need to care about this parameter.
        void load(const std::shared_ptr<Library>& library,
            const std::string& fontPath, unsigned int pixelHeight,
            unsigned int faceIndex = 0);

        // Changing the pixel size clears the glyph cache
        void setPixelSize(unsigned int pixelHeight);
        bool isValid() const { return face_ != nullptr; }
        // Queries whether the current font contains a glyph for the code point
        bool hasGlyph(char32_t codepoint) const;

        // Loads a glyph (with cache). antiAliased=false uses monochrome rendering.
        const GlyphBitmap& loadGlyph(char32_t codepoint, bool antiAliased = true);

        /// @brief Returns only the advance width, producing no bitmap (for layout/measurement)
        /// @return The advanceX of the code point (pixels); returns 0 if the font has no such glyph
        /// @details
        /// This is the cheapest answer to "how wide is this text": `FT_LOAD_NO_BITMAP` makes
        /// FreeType take only the metrics path (read hmtx + scale + light hinting) with no
        /// rasterization, about 2-5 microseconds per call; `loadGlyph` (`FT_LOAD_RENDER`) runs
        /// the rasterizer, which can take tens of microseconds for complex CJK glyphs and
        /// also copies a bitmap.
        ///
        /// Why the distinction matters: the line-breaking algorithm asks for the width of
        /// every character (see ZeTextArea::wrapParagraph). Asking via loadGlyph means
        /// rasterizing every glyph of the whole text each time, so pasting a long text
        /// freezes immediately -- this is exactly the problem this change fixes.
        /// With metrics and rasterization separated, line breaking pays only the metrics
        /// cost and bitmaps are left to the worker thread.
        ///
        /// @note Results are cached by code point; like the loadGlyph cache, it is cleared on setPixelSize.
        /// @note The returned advance uses the same hinting flags as the advanceX reported by
        ///       loadGlyph, so the measured width and the painted width match pixel for pixel.
        int advanceOf(char32_t codepoint);

        // Metrics (pixels)
        int ascender()  const;
        int descender() const;
        int lineHeight() const;

        // Clears the glyph cache (called automatically after changing the pixel size, for example)
        void clearCache() { cache_.clear(); advCache_.clear(); }

    private:
        std::shared_ptr<Library> library_;
        FT_Face face_ = nullptr;
        std::unordered_map<char32_t, GlyphBitmap> cache_;
        std::unordered_map<char32_t, int>          advCache_;   /// < result of advanceOf
    };
    
    // ---- UTF-8 decoding utilities ----
    //  Both entry points accept string_view: the layout hot path (LayoutText / MeasureText)
    //  only has a view anyway, and should not materialize the whole string into std::string
    //  just to call it (one heap allocation per widget per frame).
    std::vector<char32_t> utf8ToCodePoints(std::string_view utf8);

    // ---- Encoding-adaptive decoding: auto-detects UTF-8 or the local encoding (GBK on Windows) ----
    // Callers can pass std::string directly and need not care about the source file encoding.
    // @note Accepts string_view: the local-encoding branch handles NUL termination / length
    //       information itself, so callers need not pay a full-string copy just to decode.
    std::vector<char32_t> textToCodePoints(std::string_view text);

    // ---- Zero-allocation code point cursor (for the layout / measurement hot path) ----
    //
    //  Previously LayoutText / MeasureText called `textToCodePoints(...)` first to decode
    //  into a `std::vector<char32_t>`: one heap allocation per call. A single call seems
    //  harmless, but the line-breaking algorithm asks for widths character by character
    //  (ZeTextArea::wrapParagraph), so a 10,000-character text allocates 10,000 vectors
    //  per frame -- the allocator alone eats several milliseconds and flushes the CPU
    //  cache as a side effect.
    //
    //  This cursor separates the two cases:
    //    - The input is valid UTF-8 (the vast majority of cases) -> iterate over bytes
    //      directly, zero allocation;
    //    - The input is not valid UTF-8 (GBK literals in source / legacy projects) ->
    //      convert once into a code point array (exactly the same rules as
    //      textToCodePoints), then iterate as usual.
    //  So behavior matches the previous byte-by-byte version; it just no longer pays
    //  allocation for normal input.
    class CodePointCursor {
    public:
        explicit CodePointCursor(std::string_view text);

        /// @brief Returns the next code point; false means the end is reached
        bool next(char32_t& out);

        /// @brief Returns the whole text at once (only use when the full code point array is really needed -- allocates)
        static std::vector<char32_t> toVector(std::string_view text);

    private:
        std::string_view        raw_;            /// < fast path: raw UTF-8 bytes
        std::size_t             pos_ = 0;
        std::vector<char32_t>   converted_;      /// < slow path: code points decoded from the local encoding
        std::size_t             cpos_ = 0;
        bool                    convertedMode_ = false;
    };

    // ---- Font discovery ----
    //  @param library The specified library (the glyph worker thread passes its own); nullptr = use the singleton
    std::vector<std::unique_ptr<FontFace>>
        createDefaultFontFaces(unsigned int pixelSize,
            const std::shared_ptr<Library>& library = nullptr);
}