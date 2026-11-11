/**
 * @file ZeValueAnim.h
 * @brief Library-internal: the single implementation of value animation (easing + frame pacing) and "current milliseconds"
 *
 * @details
 * The animation logic of `Slider` and `ProgressBar` used to be two verbatim-identical 31-line copies:
 * the same ease-out cubic, the same "schedule the next frame by remaining time" pacing, the same "land exactly on the value at the deadline".
 * The feel must be identical on both sides (two widgets in the same app animating at different speeds looks sloppy),
 * so the logic can only exist once.
 *
 * This deliberately introduces no window/paint dependency: this function only computes "what value this frame has and how many milliseconds remain",
 * and frame continuation (`Window::RequestFrameWake`) is done by the caller -- so this header can be included by any
 * widget, and there is no hidden reentrancy surface.
 *
 * @par Easing
 * ease-out cubic: `1 - (1 - t)^3`, fast start, slow finish. All value animations in the library use it.
 *
 * @see Slider::stepAnimation  use site one
 * @see ProgressBar::stepAnimation  use site two
 */

#pragma once

#include <chrono>
#include <cmath>

namespace Kit {
    namespace detail {

        /// @brief Monotonic millisecond counter (`steady_clock`)
        /// @note Independent of system time; used only for "how much elapsed between two samples".
        ///       Toast / Tooltip / ProgressBar / Slider each previously had their own copy of the same implementation.
        inline long long NowMs() {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
        }

        /// @brief Result of advancing a value animation by one frame
        struct ValueAnimStep {
            float value = 0.0f;   /// < Value to display for this frame
            int   remainMs = 0;   /// < Milliseconds remaining until the end (meaningless when `done == true`)
            bool  done = false;   /// < true = this frame reached the endpoint (the caller should clear `animating_`)
        };

        /**
         * @brief Advances a value animation by one frame
         * @param from    Start value of this animation round (the value recorded at the start)
         * @param target  Target value
         * @param startMs Start time of this animation round (same clock as `NowMs()`)
         * @param durMs   Total duration (<= 0 means no animation, land the value directly)
         * @return This frame's value + remaining time
         *
         * @note On reaching the endpoint, return `target` exactly; do not approach it via floating-point interpolation --
         *       otherwise it ends up stuck at 0.9999 and the progress bar looks "a hair short of full".
         */
        inline ValueAnimStep StepValueAnim(float from, float target,
            long long startMs, int durMs) {
            ValueAnimStep out;

            const float dur = static_cast<float>(durMs);
            const long long now = NowMs();
            const long long elapsed = now - startMs;

            float t = (dur <= 0.0f) ? 1.0f : static_cast<float>(elapsed) / dur;
            if (t < 0.0f) t = 0.0f;

            if (t >= 1.0f) {
                out.value = target;
                out.done = true;
                return out;
            }

            const float e = 1.0f - std::pow(1.0f - t, 3.0f);   // ease-out cubic
            out.value = from + (target - from) * e;
            out.remainMs = static_cast<int>(dur - static_cast<float>(elapsed));
            if (out.remainMs <= 0) out.remainMs = 1;
            return out;
        }

    } // namespace detail
} // namespace Kit
