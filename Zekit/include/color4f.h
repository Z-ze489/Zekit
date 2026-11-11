/**
 * @file color4f.hpp
 * @brief RGBA floating-point color type (the color representation used uniformly across ZeWin)
 *
 * @details
 * Color4f is one of the most fundamental value types in ZeWin. Every interface that involves color
 * (background color, border color, text color, shadow color, etc.) takes it as a parameter type:
 *  - All four components are @c float, with a value range of 0.0 ~ 1.0 (not 0~255);
 *  - Normalized floats are used instead of byte integers, so the color can participate directly in GPU rendering and color interpolation;
 *  - Default construction yields opaque black (0, 0, 0, 1).
 *
 * This struct is a trivially copyable plain data type, with no virtual functions and no heap allocation,
 * and can be freely passed and stored by value.
 *
 * @note When a component value is outside 0.0~1.0, the behavior depends on the specific rendering backend; ensure the range is valid yourself.
 * @note The alpha a is opacity: 1.0 means fully opaque, 0.0 means fully transparent.
 *
 * @par Basic usage
 * @code
 * #include "color4f.hpp"
 *
 * Win::Color4f red  (1.0f, 0.0f, 0.0f);        // opaque red (a defaults to 1.0)
 * Win::Color4f mask (0.0f, 0.0f, 0.0f, 0.5f);  // semi-transparent black mask
 * Win::Color4f none;                            // default opaque black
 *
 * btn.SetBackgroundColor(Win::ButtonState::Normal, red);
 * @endcode
 *
 * @see FontDesc font description type
 * @see image_types.hpp image / texture related types
 */

#pragma once

namespace Kit {
    /**
     * @brief RGBA floating-point color (each component 0.0 ~ 1.0)
     *
     * @details
     * Represents the red, green, blue and alpha channels as normalized floats:
     *  - @c r / @c g / @c b: color channels, 0.0 is darkest, 1.0 is brightest;
     *  - @c a: opacity channel, 0.0 is fully transparent, 1.0 is fully opaque (default).
     *
     * The type itself does not clamp the range; the caller ensures the values are valid. Construction is constexpr,
     * so it can be used for compile-time constants.
     *
     * @par Example
     * @code
     * constexpr Win::Color4f kTheme(0.10f, 0.50f, 1.00f);  // compile-time constant
     * Win::Color4f dim = kTheme; dim.a = 0.3f;             // adjust opacity
     * @endcode
     */
    struct Color4f {
        float r = 0.0f;         /// < Red component, value 0.0 ~ 1.0
        float g = 0.0f;         /// < Green component, value 0.0 ~ 1.0
        float b = 0.0f;         /// < Blue component, value 0.0 ~ 1.0
        float a = 1.0f;         /// < Opacity component, 0.0 = fully transparent, 1.0 = fully opaque

        /// @brief Default constructor, yields opaque black (0, 0, 0, 1)
        constexpr Color4f() = default;

        /**
         * @brief Constructs a color from the specified components
         * @param r_ Red component (0.0 ~ 1.0)
         * @param g_ Green component (0.0 ~ 1.0)
         * @param b_ Blue component (0.0 ~ 1.0)
         * @param a_ Opacity component (0.0 ~ 1.0), default 1.0 means fully opaque
         *
         * @note This constructor is constexpr and can be evaluated at compile time.
         */
        constexpr Color4f(float r_, float g_, float b_, float a_ = 1.0f)
            : r(r_), g(g_), b(b_), a(a_) {}

        // -------------------------------------------------------------------
        //  Convenience construction (0~255 / HEX)
        // -------------------------------------------------------------------
        /**
         * @brief Constructs a color from 0~255 components (automatically normalized to 0.0~1.0)
         * @param r Red component (0 ~ 255)
         * @param g Green component (0 ~ 255)
         * @param b Blue component (0 ~ 255)
         * @param a Opacity component (0 ~ 255), default 255 means fully opaque
         *
         * @par Example
         * @code
         * constexpr Win::Color4f c = Win::Color4f::FromRgb(255, 99, 71);  // tomato
         * @endcode
         */
        static constexpr Color4f FromRgb(unsigned char r, unsigned char g, unsigned char b, unsigned char a = 255) {
            return Color4f(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
        }

        /**
         * @brief Constructs a color from a 0xRRGGBB integer (opaque)
         * @param rgb 24-bit color value, e.g. 0xFF6347 means tomato
         * @param a Opacity (0.0 ~ 1.0), default 1.0
         *
         * @par Example
         * @code
         * constexpr Win::Color4f c = Win::Color4f::FromHex(0xFF6347);
         * @endcode
         */
        static constexpr Color4f FromHex(unsigned int rgb, float a = 1.0f) {
            return Color4f(((rgb >> 16) & 0xFFu) / 255.0f,
                ((rgb >> 8) & 0xFFu) / 255.0f,
                (rgb & 0xFFu) / 255.0f,
                a);
        }

        /**
         * @brief Parses a color from a string
         * @param hex Supports "#RRGGBB", "RRGGBB", "#RRGGBBAA", "RRGGBBAA";
         *            an invalid or empty string falls back to opaque black
         * @return The parsed color
         *
         * @par Example
         * @code
         * Win::Color4f c = Win::Color4f::FromHexStr("#FF6347");   // tomato
         * Win::Color4f d = Win::Color4f::FromHexStr("#FF634780"); // semi-transparent tomato (RRGGBBAA)
         * @endcode
         */
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

        // -------------------------------------------------------------------
        //  Comparison
        // -------------------------------------------------------------------
        // @brief All-component equality comparison (r/g/b/a all equal)
        constexpr bool operator==(const Color4f& o) const {
            return r == o.r && g == o.g && b == o.b && a == o.a;
        }

        /// @brief All-component inequality comparison
        constexpr bool operator!=(const Color4f& o) const {
            return !(*this == o);
        }

        // -------------------------------------------------------------------
        //  Color tools (for theme derivation: compute a whole set of hover/press/light background from one primary color)
        // -------------------------------------------------------------------
        //  This group of functions turns "switching to another theme" from "hand-writing 20 colors" into "giving 1 primary color":
        //   · lighten/darken -- push each channel by an equal amount toward white/black (preserving hue);
        //   · mix           -- blend proportionally with another color (computes values like "a 10% light background of the primary color");
        //   · withAlpha     -- changes only the opacity (most common: turn a token into a semi-transparent highlight background).
        //  All return a new value and do not modify the object itself; out-of-range components are not clamped (consistent with the existing convention of this type,
        //  the caller ensures validity -- but as long as the input is valid, the output is always valid: 0.12x0.8+... will not go out of range).
        // -------------------------------------------------------------------

        /// @brief Changes the opacity (RGB unchanged) -- the shortest path to turn a token into a semi-transparent highlight background
        /// @par Example @code c = primary.withAlpha(0.10f);   // hover row background at 10% of the primary color @endcode
        constexpr Color4f withAlpha(float a_) const {
            return Color4f(r, g, b, a_);
        }

        /// @brief Moves toward white (preserving hue) -- the hover level for light themes
        /// @param k 0~1: 0 = unchanged, 1 = pure white
        constexpr Color4f lighten(float k) const {
            return Color4f(r + (1.0f - r) * k, g + (1.0f - g) * k,
                b + (1.0f - b) * k, a);
        }

        /// @brief Moves toward black (preserving hue) -- the hover / press level for dark tones
        /// @param k 0~1: 0 = unchanged, 1 = pure black
        constexpr Color4f darken(float k) const {
            return Color4f(r * (1.0f - k), g * (1.0f - k), b * (1.0f - k), a);
        }

        /// @brief Linearly blends with another color -- computes values like "8% of the primary color laid over the surface color"
        /// @param k Weight of other (0 = unchanged, 1 = pure other); alpha is blended as well
        constexpr Color4f mix(const Color4f& o, float k) const {
            return Color4f(r + (o.r - r) * k, g + (o.g - g) * k,
                b + (o.b - b) * k, a + (o.a - a) * k);
        }

        // -------------------------------------------------------------------
        //  Common color constants
        // -------------------------------------------------------------------
        static constexpr Color4f White() { return Color4f(1.0f, 1.0f, 1.0f, 1.0f); }
        static constexpr Color4f Black() { return Color4f(0.0f, 0.0f, 0.0f, 1.0f); }
        static constexpr Color4f Red() { return Color4f(1.0f, 0.0f, 0.0f, 1.0f); }
        static constexpr Color4f Green() { return Color4f(0.0f, 1.0f, 0.0f, 1.0f); }
        static constexpr Color4f Blue() { return Color4f(0.0f, 0.0f, 1.0f, 1.0f); }
        /// @brief Fully transparent (a = 0 under any RGB)
        static constexpr Color4f Transparent() { return Color4f(0.0f, 0.0f, 0.0f, 0.0f); }
    };
}