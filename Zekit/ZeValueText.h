/**
 * @file ZeValueText.h
 * @brief **Library internal**: formatting of the "value text" for value widgets (slider / progress bar)
 *
 * @details
 * The user-supplied template has three forms, and both widgets must interpret them **identically**:
 *
 * | What the template contains | Behavior | Example |
 * |---|---|---|
 * | `{}`  | Placeholder replaced with an **integer** | `"{} %"` -> `"42 %"` |
 * | `%`   | Used as a C format string (argument is a floating-point value) | `"%.1f%%"` -> `"42.0%"` |
 * | Neither | Displayed as-is | `"progress"` |
 *
 * @par Why this single copy is required (instead of writing it once in each place)
 * The `Slider` copy passed the `double` directly to the user's format string -- a user writing `"%d%%"` (the most natural form)
 * means **`double` paired with `%d`, which is undefined behavior**: in practice it prints garbage values such as `"536870912%"`
 * (`ProgressBar` already hit and fixed this on 2026-10-13, but the `Slider` copy was never synced).
 * Now both places go through this function, and `%d` / `%i` are first rewritten to `%ld` before a `long` is passed.
 *
 * @note Only the most common `%d` / `%i` are rewritten; for `%u` / `%x` and the like, callers must write the correct type themselves --
 *       guessing "what type this specifier wants" is more likely to introduce new bugs than fixing one UB.
 * @note Buffer is 64 bytes: the value text is never longer, and overlong text is truncated by `snprintf` (no overflow).
 */

#pragma once

#include <cmath>
#include <cstdio>
#include <string>

namespace Kit {
    namespace detail {

        /**
         * @brief Formats a numeric value according to a template
         * @param fmt   The user's template (see the three forms in the file header)
         * @param value The numeric value (the slider passes the current value, the progress bar passes the percentage)
         * @return The text to display (returns an empty string when the template is empty)
         */
        inline std::string FormatValueText(const std::string& fmt, double value) {
            if (fmt.empty()) return {};

            // (1) "{}": integer placeholder
            if (fmt.find("{}") != std::string::npos) {
                std::string text = fmt;
                const long iv = std::lround(value);
                text.replace(text.find("{}"), 2, std::to_string(iv));
                return text;
            }

            // (2) "%": C format string. %d / %i are first rewritten to %ld (see the UB note in the file header)
            if (fmt.find('%') != std::string::npos) {
                std::string f = fmt;
                bool wantInt = false;
                for (const char* sp : { "%d", "%i" }) {
                    for (std::size_t at = f.find(sp); at != std::string::npos;
                        at = f.find(sp, at + 3)) {
                        f.replace(at, 2, "%ld");    // "%ld" does not contain "%d", so it does not self-match
                        wantInt = true;
                    }
                }
                char buf[64];
                if (wantInt) std::snprintf(buf, sizeof buf, f.c_str(), static_cast<long>(std::lround(value)));
                else         std::snprintf(buf, sizeof buf, f.c_str(), value);
                return std::string(buf);
            }

            // (3) Static text
            return fmt;
        }

    } // namespace detail
} // namespace Kit
