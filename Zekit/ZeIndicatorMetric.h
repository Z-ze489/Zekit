/**
 * @file ZeIndicatorMetric.h
 * @brief **Library internal**: shared measurement and click rhythm for checkbox / radio button
 *
 * @details
 * `Checkbox` and `RadioButton` differ only in "what the mark looks like" (`CheckMark` / `RadioMark`);
 * otherwise their **behavior is identical**: the indicator side follows one line of text height, the natural size = indicator + spacing +
 * text + in-box padding, press records `pressed_`, and release on the same point counts as one click.
 *
 * This logic was previously written once in each of the two .cpp files (`resolvedIndicatorSize` / `hintWidth` /
 * `hintHeight` / `OnMousePress` / `OnMouseRelease`, five functions **character-for-character identical**),
 * so a change on one side was forgotten on the other -- for example RadioButton's `Activate()` lacked
 * `RequestWindowRedraw()`, leaving a stale dot after a programmatic toggle (fixed).
 *
 * Two reasons to extract it here:
 *  1. **Change one place only**: there is a single copy of the measurement rules and click rhythm;
 *  2. **Do not touch the public inheritance surface**: the `public TextWidget` relation of both classes stays unchanged -- making a common base class
 *     would require putting the base into `include/` (public headers cannot depend back on library-internal headers), a cost far exceeding the benefit.
 *
 * @note Painting is not here: the container shape and mark already live in `ZeIndicatorPaint.h` (shared by the same widgets).
 * @see ZeIndicatorPaint.h  how the indicator container / mark is painted
 * @see TextWidget           the actual base class of both classes (text + box layout)
 */

#pragma once

#include "include/ZeBoxStyle.h"      // BoxStyle (in-box padding)
#include "include/ZeInputEvents.h"   // MouseEvent
#include "include/FontDesc.h"        // TextMetrics
#include "include/ZeWidget.h"        // Widget（HitTest / isEnabled）

#include "ZeDrawUtil.h"              // drawutil::BoxInsets

namespace Kit {
    namespace detail {

        /// @brief Natural size of the three-piece set (return value of the measurement protocol)
        struct IndicatorHint {
            float width = 0.0f;    /// < natural width (indicator + spacing + text + in-box padding)
            float height = 0.0f;   /// < natural height (max(indicator, one line of text height) + in-box padding)
        };

        /// @brief Fallback indicator side length when text metrics are unavailable
        constexpr float kFallbackIndicator = 18.0f;

        /**
         * @brief Resolves the indicator side length
         * @param explicitSize user-set value (<= 0 = automatic)
         * @param textHeight   height of one line of text (pass 0 when metrics are unavailable)
         * @return explicit value > text height > `kFallbackIndicator`
         * @note "Automatic" means "matches one line of text height": a larger font makes the indicator larger.
         */
        inline float ResolveIndicatorSize(float explicitSize, float textHeight) {
            if (explicitSize > 0.0f) return explicitSize;
            if (textHeight > 0.0f) return textHeight;
            return kFallbackIndicator;
        }

        /**
         * @brief Natural size (both classes share the same arithmetic)
         * @param box           box style (padding + border go into "extra padding")
         * @param indicatorSize resolved indicator side length
         * @param spacing       spacing between indicator and text
         * @param tm            text metrics (width / height; all 0 when not ready)
         *
         * @details
         * width = indicator + spacing + text width (**no spacing when text is empty**) + in-box padding;
         * height = max(indicator, one line of text height) + in-box padding.
         *
         * @note The caller is responsible for handling "user-set size" first (`prefWidth()/prefHeight() > 0`
         *       returns directly): that is the interface semantics of each class and does not belong here.
         */
        inline IndicatorHint IndicatorHints(const BoxStyle& box, float indicatorSize,
            float spacing, const TextMetrics& tm) {
            float dx = 0.0f, dy = 0.0f;
            drawutil::BoxInsets(box, dx, dy);

            const float textW = (tm.width > 0.0f) ? tm.width : 0.0f;
            const float textH = (tm.height > 0.0f) ? tm.height : 0.0f;

            IndicatorHint out;
            out.width = indicatorSize + (textW > 0.0f ? spacing + textW : 0.0f) + dx;
            out.height = std::max(indicatorSize, textH) + dy;
            return out;
        }

        /**
         * @brief Press: hit + left button -> record `pressed_` and consume the event
         * @param self    the widget itself (only needs `HitTest`)
         * @param e       mouse event
         * @param enabled `isEnabled()`
         * @param pressed the widget's own `pressed_` (set to true)
         * @return true = consumed
         */
        inline bool IndicatorPress(const Widget& self, MouseEvent& e,
            bool enabled, bool& pressed) {
            if (!enabled) return false;
            if (e.button != MouseButton::Left) return false;
            if (!self.HitTest(e.x, e.y)) return false;

            pressed = true;
            e.accept();
            return true;
        }

        /**
         * @brief Release: only "finishing this press + still on the widget at release" counts as one click
         * @param hit output: true = should call `Activate()` (the caller toggles state / fires callbacks itself)
         * @return true = event consumed (`hit` false means "this really is this widget's release, but cancelled")
         *
         * @note Leaving the widget clears `pressed_` via `OnMouseLeave` -- that gate lives in each class,
         *       so here only "was pressed" and "still on it now" are checked.
         */
        inline bool IndicatorRelease(const Widget& self, MouseEvent& e,
            bool enabled, bool& pressed, bool& hit) {
            hit = false;
            if (!enabled) return false;
            if (e.button != MouseButton::Left) return false;

            const bool wasPressed = pressed;
            pressed = false;
            if (!wasPressed) return false;
            if (!self.HitTest(e.x, e.y)) return false;

            hit = true;
            e.accept();
            return true;
        }

    } // namespace detail
} // namespace Kit
