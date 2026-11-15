// color4f.h - RGBA color with normalized float components (0.0 .. 1.0).
// Used by every color-taking API in Zekit; trivially copyable, constexpr-friendly.

#pragma once

namespace Kit {
    // RGBA color, each component 0.0 .. 1.0. Values are not clamped.
    struct Color4f {
        float r = 0.0f;   // red
        float g = 0.0f;   // green
        float b = 0.0f;   // blue
        float a = 1.0f;   // alpha: 0 = transparent, 1 = opaque

        constexpr Color4f() = default;   // opaque black (0,0,0,1)

        constexpr Color4f(float r_, float g_, float b_, float a_ = 1.0f)
            : r(r_), g(g_), b(b_), a(a_) {}

        // 0..255 components
        static constexpr Color4f FromRgb(unsigned char r, unsigned char g, unsigned char b, unsigned char a = 255) {
            return Color4f(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
        }

        // 0xRRGGBB, opaque unless alpha is given
        static constexpr Color4f FromHex(unsigned int rgb, float a = 1.0f) {
            return Color4f(((rgb >> 16) & 0xFFu) / 255.0f,
                ((rgb >> 8) & 0xFFu) / 255.0f,
                (rgb & 0xFFu) / 255.0f,
                a);
        }

        // "#RRGGBB" / "RRGGBB" / "#RRGGBBAA" / "RRGGBBAA"; invalid or empty -> opaque black
        static constexpr Color4f FromHexStr(const char* hex) {
            if (!hex || hex[0] == '\0') return Color4f();
            int i = (hex[0] == '#') ? 1 : 0;
            unsigned int v = 0;
            int n = 0;
            while (hex[i] != '\0' && n < 8) {
                v <<= 4;
                const char c = hex[i];
                v |= (c >= '0' && c <= '9') ? static_cast<unsigned int>(c - '0')
                    : (c >= 'a' && c <= 'f') ? static_cast<unsigned int>(c - 'a' + 10)
                    : (c >= 'A' && c <= 'F') ? static_cast<unsigned int>(c - 'A' + 10)
                    : 0u;
                ++i; ++n;
            }
            if (n == 6) {
                return Color4f(((v >> 16) & 0xFFu) / 255.0f,
                    ((v >> 8) & 0xFFu) / 255.0f,
                    (v & 0xFFu) / 255.0f,
                    1.0f);
            }
            if (n == 8) {
                return Color4f(((v >> 24) & 0xFFu) / 255.0f,
                    ((v >> 16) & 0xFFu) / 255.0f,
                    ((v >> 8) & 0xFFu) / 255.0f,
                    (v & 0xFFu) / 255.0f);
            }
            return Color4f();
        }

        // all four components equal
        constexpr bool operator==(const Color4f& o) const {
            return r == o.r && g == o.g && b == o.b && a == o.a;
        }

        constexpr bool operator!=(const Color4f& o) const {
            return !(*this == o);
        }

        // ---- derivations: return a new value, never modify the object ----

        constexpr Color4f withAlpha(float a_) const {   // alpha only
            return Color4f(r, g, b, a_);
        }

        constexpr Color4f lighten(float k) const {      // toward white, k in 0..1
            return Color4f(r + (1.0f - r) * k, g + (1.0f - g) * k,
                b + (1.0f - b) * k, a);
        }

        constexpr Color4f darken(float k) const {       // toward black, k in 0..1
            return Color4f(r * (1.0f - k), g * (1.0f - k), b * (1.0f - k), a);
        }

        constexpr Color4f mix(const Color4f& o, float k) const {   // linear blend, k in 0..1
            return Color4f(r + (o.r - r) * k, g + (o.g - g) * k,
                b + (o.b - b) * k, a + (o.a - a) * k);
        }

        // ---- common constants ----
        static constexpr Color4f White() { return Color4f(1.0f, 1.0f, 1.0f, 1.0f); }
        static constexpr Color4f Black() { return Color4f(0.0f, 0.0f, 0.0f, 1.0f); }
        static constexpr Color4f Red() { return Color4f(1.0f, 0.0f, 0.0f, 1.0f); }
        static constexpr Color4f Green() { return Color4f(0.0f, 1.0f, 0.0f, 1.0f); }
        static constexpr Color4f Blue() { return Color4f(0.0f, 0.0f, 1.0f, 1.0f); }
        static constexpr Color4f Transparent() { return Color4f(0.0f, 0.0f, 0.0f, 0.0f); }
    };
}
