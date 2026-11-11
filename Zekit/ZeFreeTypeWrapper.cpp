#include "pch.h"

#ifdef _WIN32
#include <Windows.h>
#endif

#include "ZeFreeTypeWrapper.h"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

namespace ftw {
	Library::Library() {
		if (FT_Init_FreeType(&library_))
			throw FreeTypeError("FT_Init_FreeType failed");
	}

	Library::~Library() {
		if (library_) {
			FT_Done_FreeType(library_);
			library_ = nullptr;
		}
	}

	std::shared_ptr<Library> Library::getInstance() {
		static std::shared_ptr<Library> instance(new Library());
		return instance;
	}

	std::shared_ptr<Library> Library::createIndependent() {
		return std::shared_ptr<Library>(new Library());
	}

    FontFace::FontFace(const std::string& fontPath,
        unsigned int pixelHeight,
        unsigned int faceIndex)
        : library_(Library::getInstance()) {
        load(fontPath, pixelHeight, faceIndex);
    }

    FontFace::FontFace(const std::shared_ptr<Library>& library,
        const std::string& fontPath,
        unsigned int pixelHeight,
        unsigned int faceIndex)
        : library_(library ? library : Library::getInstance()) {
        load(fontPath, pixelHeight, faceIndex);
    }

    FontFace::~FontFace() {
        if (face_) {
            FT_Done_Face(face_);
            face_ = nullptr;
        }
    }

    FontFace::FontFace(FontFace&& other) noexcept
        : library_(std::move(other.library_)),
        face_(other.face_),
        cache_(std::move(other.cache_)) {
        other.face_ = nullptr;
    }

    FontFace& FontFace::operator=(FontFace&& other) noexcept {
        if (this != &other) {
            if (face_) {
                FT_Done_Face(face_);
            }
            library_ = std::move(other.library_);
            face_ = other.face_;
            cache_ = std::move(other.cache_);
            other.face_ = nullptr;
        }
        return *this;
    }

    void FontFace::load(const std::string& fontPath,
        unsigned int pixelHeight,
        unsigned int faceIndex) {
        if (!library_) {
            library_ = Library::getInstance();
        }
        if (face_) {
            FT_Done_Face(face_);
            face_ = nullptr;
        }
        cache_.clear();
        advCache_.clear();

        if (FT_New_Face(library_->handle(),
            fontPath.c_str(),
            static_cast<FT_Long>(faceIndex),
            &face_)) {
            throw FreeTypeError("FT_New_Face failed: " + fontPath);
        }
        setPixelSize(pixelHeight);
    }

    void FontFace::load(const std::shared_ptr<Library>& library,
        const std::string& fontPath,
        unsigned int pixelHeight,
        unsigned int faceIndex) {
        if (face_) {
            FT_Done_Face(face_);
            face_ = nullptr;
        }
        cache_.clear();
        advCache_.clear();
        library_ = library ? library : Library::getInstance();
        if (FT_New_Face(library_->handle(),
            fontPath.c_str(),
            static_cast<FT_Long>(faceIndex),
            &face_)) {
            throw FreeTypeError("FT_New_Face failed: " + fontPath);
        }
        setPixelSize(pixelHeight);
    }

    void FontFace::setPixelSize(unsigned int pixelHeight) {
        if (!face_) {
            throw FreeTypeError("setPixelSize called on invalid face");
        }
        cache_.clear();
        advCache_.clear();
        if (FT_Set_Pixel_Sizes(face_, 0, pixelHeight)) {
            throw FreeTypeError("FT_Set_Pixel_Sizes failed");
        }
    }

    const GlyphBitmap& FontFace::loadGlyph(char32_t codepoint, bool antiAliased) {
        if (!face_) {
            throw FreeTypeError("loadGlyph called on invalid face");
        }

        auto it = cache_.find(codepoint);
        if (it != cache_.end()) {
            return it->second;
        }

        FT_Int32 flags = FT_LOAD_RENDER;
        if (!antiAliased) {
            flags |= FT_LOAD_TARGET_MONO;
        }
        else {
            flags |= FT_LOAD_TARGET_LIGHT;
            flags |= FT_LOAD_COLOR;
        }

        if (FT_Load_Char(face_, codepoint, flags)) {
            throw FreeTypeError("FT_Load_Char failed for U+" +
                std::to_string(static_cast<uint32_t>(codepoint)));
        }

        FT_GlyphSlot slot = face_->glyph;
        FT_Bitmap& bmp = slot->bitmap;

        GlyphBitmap gb;
        gb.width = static_cast<int>(bmp.width);
        gb.height = static_cast<int>(bmp.rows);
        gb.bearingX = slot->bitmap_left;
        gb.bearingY = slot->bitmap_top;
        gb.advanceX = static_cast<int>(slot->advance.x >> 6);
        gb.advanceY = static_cast<int>(slot->advance.y >> 6);

        gb.isColor = (bmp.pixel_mode == FT_PIXEL_MODE_BGRA);

        if (bmp.buffer && bmp.rows > 0) {
            gb.pitch = std::abs(bmp.pitch);

            const std::size_t rowBytes = static_cast<std::size_t>(gb.pitch);
            gb.buffer.resize(rowBytes * static_cast<std::size_t>(bmp.rows));
            for (unsigned int r = 0; r < bmp.rows; ++r) {
                const auto* src = bmp.buffer + static_cast<std::ptrdiff_t>(r) * bmp.pitch;
                std::memcpy(gb.buffer.data() + rowBytes * r, src, rowBytes);
            }
        }

        auto [inserted, _] = cache_.emplace(codepoint, std::move(gb));
        return inserted->second;
    }

    int FontFace::advanceOf(char32_t codepoint) {
        if (!face_) return 0;

        auto it = advCache_.find(codepoint);
        if (it != advCache_.end()) return it->second;

        int adv = 0;
        const FT_Int32 flags = FT_LOAD_NO_BITMAP | FT_LOAD_TARGET_LIGHT;
        if (FT_Get_Char_Index(face_, codepoint) != 0 &&
            FT_Load_Char(face_, codepoint, flags) == 0) {
            adv = static_cast<int>(face_->glyph->advance.x >> 6);
        }

        advCache_.emplace(codepoint, adv);
        return adv;
    }

    int FontFace::ascender() const {
        if (!face_ || !face_->size) return 0;
        return static_cast<int>(face_->size->metrics.ascender >> 6);
    }

    int FontFace::descender() const {
        if (!face_ || !face_->size) return 0;
        return static_cast<int>(face_->size->metrics.descender >> 6);
    }

    int FontFace::lineHeight() const {
        if (!face_ || !face_->size) return 0;
        return static_cast<int>(face_->size->metrics.height >> 6);
    }

    bool FontFace::hasGlyph(char32_t codepoint) const {
        if (!face_) return false;
        return FT_Get_Char_Index(face_, codepoint) != 0;
    }

    namespace {
        struct FontCandidate {
            const char* path;
            const char* note;
        };

        constexpr FontCandidate kPrimaryFontPaths[] = {
            { "C:/Windows/Fonts/msyh.ttc",        "微软雅黑" },
            { "C:/Windows/Fonts/simhei.ttf",      "黑体" },
            { "C:/Windows/Fonts/simsun.ttc",      "宋体" },
            { "/System/Library/Fonts/PingFang.ttc", "苹方" },
            { "/system/fonts/NotoSansCJK-Regular.ttc",      "Noto Sans CJK（Android 标配）" },
            { "/system/fonts/NotoSansCJKsc-Regular.otf",    "Noto Sans CJK SC" },
            { "/system/fonts/DroidSansFallback.ttf",        "Droid Sans Fallback（Android 老机型）" },
            { "/system/fonts/DroidSansFallbackBBK.ttf",     "Droid Sans Fallback BBK（vivo/OPPO 定制）" },
            { "/system/fonts/DroidSans.ttf",                "Droid Sans（Android 纯西文）" },
            { "/system/fonts/Roboto-Regular.ttf",           "Roboto（Android）" },
            { "/system/fonts/HarmonyOS_Sans_SC_Regular.ttf", "HarmonyOS Sans SC（鸿蒙默认中文）" },
            { "/system/fonts/HarmonyOS_Sans_SC.ttf",         "HarmonyOS Sans SC" },
            { "/system/fonts/HarmonyOS_Sans_Regular.ttf",    "HarmonyOS Sans（鸿蒙默认西文）" },
            { "/system/fonts/HarmonyOS_Sans.ttf",            "HarmonyOS Sans" },
            { "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc", "Noto Sans CJK（Ubuntu / Fedora 默认）" },
            { "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",         "文泉驿微米黑（Debian / 老 CentOS 常见）" },
            { "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",           "文泉驿正黑" },
            { "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf", "Droid Sans Fallback" },
            { "/usr/share/fonts/truetype/arphic/uming.ttc",             "AR PL 明体" },
            { "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",                "DejaVu Sans" },
            { "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf","Liberation Sans" },
            { "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",            "Noto Sans" },
        };

        constexpr FontCandidate kFallbackFontPaths[] = {
            { "C:/Windows/Fonts/malgun.ttf",                          "Malgun Gothic（Windows 韩文）" },
            { "/system/fonts/NotoSansKR-Regular.otf",                  "Noto Sans KR（Android / 鸿蒙）" },
            { "/usr/share/fonts/truetype/noto/NotoSansKR-Regular.otf", "Noto Sans KR（Linux）" },
            { "C:/Windows/Fonts/segoeui.ttf",                          "Segoe UI（西里尔 / 希腊 / 拉丁）" },
            { "C:/Windows/Fonts/seguiemj.ttf",                     "Segoe UI Emoji" },
            { "C:/Windows/Fonts/seguisym.ttf",                     "Segoe UI Symbol" },
            { "/usr/share/fonts/truetype/noto/NotoColorEmoji.ttf", "Noto Color Emoji (Linux)" },
            { "/system/fonts/NotoColorEmoji.ttf",                  "Noto Color Emoji (Android)" },
            { "/System/Library/Fonts/Apple Color Emoji.ttc",       "Apple Color Emoji (macOS)" },
        };

        bool FontFileExists(const char* path) {
            std::error_code ec;
            return std::filesystem::is_regular_file(path, ec);
        }

        bool HasFamilyToken(const std::string& name, const char* token) {
            const std::string tok(token);
            std::size_t p = name.find(tok);
            while (p != std::string::npos) {
                const bool leftOk = (p == 0) || name[p - 1] == ' ' || name[p - 1] == '-';
                const std::size_t e = p + tok.size();
                const bool rightOk = (e == name.size()) || name[e] == ' ' || name[e] == '-';
                if (leftOk && rightOk) return true;
                p = name.find(tok, e);
            }
            return false;
        }

        int ScoreFamilyName(const char* family) {
            if (!family || !*family) return 0;
            const std::string name(family);
            int score = 0;
            if (HasFamilyToken(name, "SC") || HasFamilyToken(name, "GB") || HasFamilyToken(name, "CN"))
                score += 2;
            if (HasFamilyToken(name, "TC") || HasFamilyToken(name, "HK") ||
                HasFamilyToken(name, "JP") || HasFamilyToken(name, "KR"))
                score -= 2;
            if (HasFamilyToken(name, "Mono")) score -= 1;
            if (score == 0) score = 1;
            return score;
        }

        unsigned int PickPreferredFace(const std::shared_ptr<Library>& library, const char* path) {
            FT_Library lib = library ? library->handle() : nullptr;
            if (!lib || !path) return 0;

            FT_Face probe = nullptr;
            if (FT_New_Face(lib, path, -1, &probe)) return 0;
            const long faces = probe->num_faces;
            FT_Done_Face(probe);
            if (faces <= 1) return 0;

            unsigned int best = 0;
            int bestScore = -1000;
            for (long i = 0; i < faces; ++i) {
                FT_Face f = nullptr;
                if (FT_New_Face(lib, path, i, &f)) continue;
                const int score = ScoreFamilyName(f->family_name);
                FT_Done_Face(f);
                if (score > bestScore) {
                    bestScore = score;
                    best = static_cast<unsigned int>(i);
                }
            }
            return best;
        }

        void tryLoadFonts(std::vector<std::unique_ptr<FontFace>>& out,
            const FontCandidate* list, size_t count,
            unsigned int pixelSize,
            const std::shared_ptr<Library>& library) {
            for (size_t i = 0; i < count; ++i) {
                if (!FontFileExists(list[i].path)) continue;
                try {
                    auto f = std::make_unique<FontFace>();
                    f->load(library, list[i].path, pixelSize,
                        PickPreferredFace(library ? library : Library::getInstance(), list[i].path));
                    out.push_back(std::move(f));
                }
                catch (const std::exception&) {
                }
            }
        }

#if defined(__linux__) && !defined(__OHOS__)
        struct NameHint {
            const char* needle;
            int         score;
        };

        constexpr NameHint kLinuxNameHints[] = {
            { "NotoSansCJK",          100 },
            { "SourceHanSans",         95 },
            { "wqy-microhei",          90 },
            { "wqy-zenhei",            85 },
            { "DroidSansFallback",     80 },
            { "uming",                 70 },
            { "ukai",                  65 },
            { "DejaVuSans.ttf",        40 },
            { "LiberationSans-Regular.ttf", 35 },
            { "NotoSans-Regular.ttf",  30 },
        };

        constexpr const char* kFontScanRoots[] = {
            "/usr/share/fonts",
            "/usr/local/share/fonts",
        };

        bool LooksRegular(const std::filesystem::path& p) {
            constexpr const char* kNegative[] = {
                "-Bold", "-Italic", "-Oblique", "-Light", "-Thin", "-Medium",
                "-Black", "-Demi", "-Narrow", "Italic", "Mono", "Serif",
            };
            const std::string name = p.filename().string();
            for (const char* neg : kNegative)
                if (name.find(neg) != std::string::npos) return false;
            return true;
        }

        int ScoreFontFile(const std::filesystem::path& p) {
            const std::string ext = p.extension().string();
            if (ext != ".ttf" && ext != ".ttc" && ext != ".otf") return 0;
            if (!LooksRegular(p)) return 0;

            const std::string name = p.filename().string();
            for (const NameHint& h : kLinuxNameHints)
                if (name.find(h.needle) != std::string::npos) return h.score;
            return 0;
        }

        void ScanSystemFonts(std::vector<std::string>& out, std::size_t maxCount) {
            struct Hit { int score; std::string path; };
            std::vector<Hit> hits;
            std::error_code ec;

            for (const char* root : kFontScanRoots) {
                if (!std::filesystem::is_directory(root, ec)) continue;

                std::filesystem::recursive_directory_iterator it(
                    root, std::filesystem::directory_options::skip_permission_denied, ec);
                const std::filesystem::recursive_directory_iterator end;

                for (; it != end; it.increment(ec)) {
                    if (ec) { ec.clear(); continue; }
                    const int score = ScoreFontFile(it->path());
                    if (score > 0) hits.push_back({ score, it->path().string() });
                }
            }

            std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) {
                if (a.score != b.score) return a.score > b.score;
                return a.path < b.path;
            });

            for (const Hit& h : hits) {
                if (out.size() >= maxCount) break;
                if (std::find(out.begin(), out.end(), h.path) != out.end()) continue;
                out.push_back(h.path);
            }
        }
#endif
    }

    std::vector<std::unique_ptr<FontFace>>
        createDefaultFontFaces(unsigned int pixelSize,
            const std::shared_ptr<Library>& library) {
        std::vector<std::unique_ptr<FontFace>> faces;

        tryLoadFonts(faces, kPrimaryFontPaths,
            sizeof(kPrimaryFontPaths) / sizeof(kPrimaryFontPaths[0]),
            pixelSize, library);

#if defined(__linux__) && !defined(__OHOS__)
        if (faces.empty()) {
            std::vector<std::string> found;
            ScanSystemFonts(found, 4);
            for (const std::string& path : found) {
                try {
                    auto f = std::make_unique<FontFace>();
                    f->load(library, path, pixelSize,
                        PickPreferredFace(library ? library : Library::getInstance(), path.c_str()));
                    faces.push_back(std::move(f));
                }
                catch (const std::exception&) {
                }
            }
        }
#endif

        tryLoadFonts(faces, kFallbackFontPaths,
            sizeof(kFallbackFontPaths) / sizeof(kFallbackFontPaths[0]),
            pixelSize, library);

        return faces;
    }

    std::vector<char32_t> utf8ToCodePoints(std::string_view utf8) {
        std::vector<char32_t> out;
        out.reserve(utf8.size());

        size_t i = 0;
        const size_t n = utf8.size();
        while (i < n) {
            unsigned char c = static_cast<unsigned char>(utf8[i]);
            char32_t cp = 0;
            int extra = 0;

            if (c < 0x80) {
                cp = c;
            }
            else if ((c & 0xE0) == 0xC0) {
                cp = c & 0x1F; extra = 1;
            }
            else if ((c & 0xF0) == 0xE0) {
                cp = c & 0x0F; extra = 2;
            }
            else if ((c & 0xF8) == 0xF0) {
                cp = c & 0x07; extra = 3;
            }
            else {
                ++i;
                continue;
            }

            if (i + extra >= n) break;
            for (int k = 1; k <= extra; ++k) {
                unsigned char cc = static_cast<unsigned char>(utf8[i + k]);
                if ((cc & 0xC0) != 0x80) { cp = 0; break; }
                cp = (cp << 6) | (cc & 0x3F);
            }
            out.push_back(cp);
            i += extra + 1;
        }
        return out;
    }

    namespace {

        bool IsValidUtf8(std::string_view s) {
            const size_t n = s.size();
            size_t i = 0;
            while (i < n) {
                unsigned char c = static_cast<unsigned char>(s[i]);
                int extra = 0;

                if (c < 0x80) { ++i; continue; }
                else if ((c & 0xE0) == 0xC0) { extra = 1; if (c < 0xC2) return false; }
                else if ((c & 0xF0) == 0xE0) { extra = 2; }
                else if ((c & 0xF8) == 0xF0) { extra = 3; if (c > 0xF4) return false; }
                else return false;

                if (i + extra >= n) return false;
                for (int k = 1; k <= extra; ++k) {
                    unsigned char cc = static_cast<unsigned char>(s[i + k]);
                    if ((cc & 0xC0) != 0x80) return false;
                }
                i += extra + 1;
            }
            return true;
        }

#ifdef _WIN32
        std::vector<char32_t> FromLocalAnsi(std::string_view s) {
            std::vector<char32_t> out;
            if (s.empty()) return out;

            int wlen = MultiByteToWideChar(CP_ACP, 0, s.data(),
                static_cast<int>(s.size()), nullptr, 0);
            if (wlen <= 0) return out;

            std::wstring w(wlen, L'\0');
            MultiByteToWideChar(CP_ACP, 0, s.data(), static_cast<int>(s.size()),
                w.data(), wlen);

            out.reserve(wlen);
            for (size_t i = 0; i < w.size(); ++i) {
                wchar_t c = w[i];
                if (c >= 0xD800 && c <= 0xDBFF && i + 1 < w.size() &&
                    w[i + 1] >= 0xDC00 && w[i + 1] <= 0xDFFF) {
                    char32_t cp = 0x10000 +
                        (static_cast<char32_t>(c - 0xD800) << 10) +
                        static_cast<char32_t>(w[i + 1] - 0xDC00);
                    out.push_back(cp);
                    ++i;
                }
                else {
                    out.push_back(static_cast<char32_t>(c));
                }
            }
            return out;
        }
#endif

    }

    std::vector<char32_t> textToCodePoints(std::string_view text) {
        if (IsValidUtf8(text)) {
            return utf8ToCodePoints(text);
        }
#ifdef _WIN32
        return FromLocalAnsi(text);
#else
        return utf8ToCodePoints(text);
#endif
    }

    CodePointCursor::CodePointCursor(std::string_view text) {
        if (IsValidUtf8(text)) {
            raw_ = text;
            return;
        }
#ifdef _WIN32
        converted_ = FromLocalAnsi(text);
#else
        converted_ = utf8ToCodePoints(text);
#endif
        convertedMode_ = true;
    }

    bool CodePointCursor::next(char32_t& out) {
        if (convertedMode_) {
            if (cpos_ >= converted_.size()) return false;
            out = converted_[cpos_++];
            return true;
        }

        const std::size_t n = raw_.size();
        if (pos_ >= n) return false;

        const unsigned char c = static_cast<unsigned char>(raw_[pos_]);
        char32_t cp = 0;
        int extra = 0;

        if (c < 0x80) {
            out = c;
            ++pos_;
            return true;
        }
        if ((c & 0xE0) == 0xC0)      { cp = c & 0x1F; extra = 1; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
        else {
            ++pos_;
            return next(out);
        }

        if (pos_ + extra >= n) { pos_ = n; return false; }
        for (int k = 1; k <= extra; ++k) {
            const unsigned char cc = static_cast<unsigned char>(raw_[pos_ + k]);
            if ((cc & 0xC0) != 0x80) { pos_ += 1; return next(out); }
            cp = (cp << 6) | (cc & 0x3F);
        }
        pos_ += static_cast<std::size_t>(extra) + 1;
        out = cp;
        return true;
    }

    std::vector<char32_t> CodePointCursor::toVector(std::string_view text) {
        return textToCodePoints(text);
    }
}
