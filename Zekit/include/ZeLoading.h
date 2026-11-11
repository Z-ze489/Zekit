/**
 * @file ZeLoading.h
 * @brief Loading animation: nine variants, driven by the same phase
 *
 * @details
 * `Loading : BoxWidget` -- a small indicator that **spins continuously**. Nine variants:
 *
 * | `LoadingVariant` | What it draws | Best for |
 * |---|---|---|
 * | `Ring` (default) | A 270-degree arc rotating | General; can also switch to **determinate progress** (`SetPercent`) |
 * | `Dots` | A ring of dots, brightness trailing off along the rotation direction | Lightweight, unobtrusive |
 * | `Bars` | A row of vertical bars rising and falling | Audio / playback scenarios |
 * | `Spinner` | Spinner: a ring of short bars, only those near the rotating head lit (the iOS kind) | The most common spinner, more restrained than `Ring` |
 * | `Pulse` | Ripple: several concentric halos expanding outward from the center and fading | Waiting for a response / network round trip |
 * | `Grid` | Dot matrix: 3x3 dots scaling in a diagonal wave | Content area loading (the Material kind) |
 * | `Bounce` | Bouncing balls: several balls bounce in turn, squashed on landing | Light, lively contexts |
 * | `Wave` | Wave: a sine wave flowing (two overlaid with offset phase for depth) | Voice / waveform / streaming data |
 * | `Orbit` | Dual orbit: two small balls revolving in opposite directions on two orbits | Background tasks / syncing |
 * | `Skeleton` | **Text skeleton**: a few rounded placeholder bars + a highlight sweeping across (last line shorter) | Placeholder for **loading file content / paragraph text** |
 *
 * All nine share **the same time semantics** (one `phase` runs through everything), so switching variants does not require changing code logic:
 * phase, speed, running switch, color, thickness and count are all universal.
 *
 * An optional line of text on the right (`SetText`, e.g. "Loading..."), laid out horizontally with the indicator.
 *
 * @par * Three contracts of continuous animation (same set as `ProgressBar` / switches; miss one and it either "never moves" or "burns forever")
 * 1. `wantsAnimationFrame()` -- **answers only for itself** (`running_ && not determinate progress`),
 *    in OnDemand drive mode the host relies on it to decide "whether another frame is needed";
 * 2. Each frame `window()->RequestFrameWake(16)` renews the lease -- under EventDriven the main loop normally sleeps on
 *    the kernel, and without a reservation there is no next frame;
 * 3. After `SetRunning(false)` (or the widget becoming invisible) **no more reservations** -- idle does not burn CPU.
 *
 * @par Phase can be driven externally (for testing and "moving along with something else")
 * `SetPhase(radians)` writes the phase directly. After `SetRunning(false)` the phase freezes there,
 * so headless tests can capture frames phase by phase and assert "the picture really is rotating".
 *
 * @code
 * Kit::Loading sp;                                     // spinner
 * sp.SetText("Loading...");
 * Kit::Loading dots(Kit::LoadingVariant::Dots);
 * dots.SetSize(24.0f);
 * Kit::Loading ring(Kit::LoadingVariant::Ring);
 * ring.SetPercent(0.65f);                              // determinate progress: no longer spins, draws a 65% arc
 * Kit::Loading nine[9] = { ... };                      // pick any of the nine variants
 * @endcode
 *
 * @note `SetPercent` is only meaningful for `Ring` (the other variants have no concept of "progress").
 * @note `SetItemCount` on `Dots` / `Bars` / `Spinner` / `Wave` is "the number of dots / bars / petals",
 *       on **`Skeleton` it is "the number of lines"** (default 3, clamped 1~8); `Grid` is always 3x3.
 * @note `Skeleton` is the only variant **not drawn as a square**: it fills the widget width (the skeleton represents "a whole block
 *       of text", a square box dimension is meaningless), and its height is determined by line count x bar height. Its right side **does not lay out text** --
 *       the skeleton itself is the stand-in for "text not here yet".
 *
 * @par What about "typing out one character at a time"
 * That is not a loading animation, it is a **typewriter** (streaming output). `Skeleton` only handles the placeholder for "content not here yet";
 * once content arrives, to present it character by character use `LiveText` / your own timer + `SetText`.
 *
 * @see ZeProgressBar.h also a "program-driven animation" (for comparison)
 * @see ZeTheme.h       `LoadingStyle`
 */

#pragma once
#include "ZeBoxWidget.h"
#include "ZeTextStyle.h"

#include <string>

namespace Kit {

    /// @brief Variant of the loading animation
    /// @note All nine variants consume only one `phase` (phase), so `SetSpeed` / `SetPhase` /
    ///       `SetRunning` / `SetColor` / `SetThickness` apply to every one of them.
    enum class LoadingVariant {
        Ring,     /// < Spinner (a 270-degree arc rotating)
        Dots,     /// < A ring of dots with a fading tail
        Bars,     /// < A row of vertical bars rising and falling
        Spinner,  /// < Spinner: a ring of short bars, brightness decreasing along the rotation direction
        Pulse,    /// < Ripple: concentric halos expanding outward from the center and fading
        Grid,     /// < Dot matrix: 3x3 dots scaling in a diagonal wave
        Bounce,   /// < Bouncing balls: several balls bounce in turn, squashed on landing
        Wave,     /// < Wave: two offset sine waves flowing
        Orbit,    /// < Dual orbit: two small balls revolving in opposite directions on two orbits
        Skeleton  /// < Text skeleton: a few placeholder bars + a highlight sweeping across (loading file content / paragraphs)
    };

    /**
     * @brief Loading animation widget
     */
    class Loading : public BoxWidget {
    public:
        /// @brief Constructs (default `Ring`; spinning right after construction)
        explicit Loading(LoadingVariant variant = LoadingVariant::Ring);

        // -------------------------------------------------------------------
        //  Variant / running
        // -------------------------------------------------------------------

        void SetVariant(LoadingVariant v);
        LoadingVariant variant() const { return variant_; }

        /// @brief Starts / stops (spins by default). After stopping the phase freezes and no next frame is reserved.
        void SetRunning(bool on);
        bool isRunning() const { return running_; }

        // -------------------------------------------------------------------
        //  Determinate progress (only `Ring`)
        // -------------------------------------------------------------------
        //  `percent >= 0` -> **no longer spins**, draws the arc as "percent turns clockwise from straight up".
        //  `percent < 0`  -> indeterminate progress (spins). Default -1.

        /// @brief Sets progress 0..1 (clamped); passing a negative value = back to "indeterminate"
        void SetPercent(float k);
        float percent() const { return percent_; }
        /// @brief Whether it is in "indeterminate progress" (still spinning)
        bool indeterminate() const { return percent_ < 0.0f; }

        // -------------------------------------------------------------------
        //  Appearance (value interface)
        // -------------------------------------------------------------------

        /// @brief Indicator side length (pixels); <= 0 = follow theme
        void SetSize(float px);
        float size() const { return size_; }

        /// @brief Line width (ring) / dot diameter / bar width: one field shared by three variants
        void SetThickness(float px);
        float thickness() const { return thickness_; }

        void SetColor(const Color4f& c);
        const Color4f& color() const { return color_; }
        /// @brief Track color (only `Ring`; transparent = do not draw the base circle)
        void SetTrackColor(const Color4f& c);
        const Color4f& trackColor() const { return trackColor_; }

        /// @brief Milliseconds per revolution (smaller is faster); <= 0 is treated as 1
        void SetSpeed(int ms);
        int speed() const { return speedMs_; }

        /// @brief Number of dots / bars (>= 3); under `Skeleton` it is **the number of lines** (1~8); the ring ignores it
        void SetItemCount(int n);
        int itemCount() const { return itemCount_; }

        // -------------------------------------------------------------------
        //  Text skeleton (only `Skeleton`)
        // -------------------------------------------------------------------
        // @brief Proportion of the full line width taken by the last line (default 0.62)
        // @details The last line of a real paragraph is always a bit shorter -- shrinking it by proportion keeps the skeleton from looking like
        //          "a pile of equal-length gray bars" and more like "a paragraph of text about to appear".
        void SetLastLineRatio(float k);
        float lastLineRatio() const { return lastLineRatio_; }

        /// @brief Highlight sweep switch (on by default)
        /// @details On: a bright band more solid than the base color sweeps from left to right with the phase (all lines **sweep in sync**,
        ///          appearing as one diagonal/vertical bright band passing over the whole skeleton); off: only static placeholder bars remain.
        /// @note Only affects `Skeleton`; other variants ignore it.
        void SetShimmer(bool on);
        bool isShimmerEnabled() const { return shimmer_; }

        /// @brief Text on the right (empty string = takes no space)
        void SetText(std::string text);
        const std::string& Text() const { return text_; }
        bool hasText() const { return !text_.empty(); }
        TextStyle& textStyle() { return textStyle_; }

        // -------------------------------------------------------------------
        //  Phase (external drive / testing)
        // -------------------------------------------------------------------

        /// @brief Current phase (radians, always in `[0, 2pi)`)
        float phase() const { return phase_; }
        /// @brief Writes the phase directly (with `SetRunning(false)` allows frame-by-frame inspection)
        void SetPhase(float radians);

        // -------------------------------------------------------------------
        //  Measurement protocol
        // -------------------------------------------------------------------
        float hintWidth() const override;
        float hintHeight() const override;

        /// @brief Override: tells the host "I need another frame" while spinning (not needed for determinate progress)
        bool wantsAnimationFrame() const override;

        // -------------------------------------------------------------------
        //  Theme
        // -------------------------------------------------------------------
        void ApplyTheme(const Theme& t) override;

    protected:
        /// @brief Indicator + optional text
        void paintContent(FrameCollector& fc) const override;

    private:
        /// @brief Advances the phase (called during paint) and reserves the next frame
        void advance() const;
        /// @brief Indicator geometry: side length of the square bounding box (already clamped to available space)
        float indicatorSide() const;
        /// @brief Currently effective line width / dot diameter
        float resolvedThickness() const;

        void paintRing(FrameCollector& fc, float ax, float ay, float side) const;
        void paintDots(FrameCollector& fc, float ax, float ay, float side) const;
        void paintBars(FrameCollector& fc, float ax, float ay, float side) const;
        void paintSpinner(FrameCollector& fc, float ax, float ay, float side) const;
        void paintPulse(FrameCollector& fc, float ax, float ay, float side) const;
        void paintGrid(FrameCollector& fc, float ax, float ay, float side) const;
        void paintBounce(FrameCollector& fc, float ax, float ay, float side) const;
        void paintWave(FrameCollector& fc, float ax, float ay, float side) const;
        void paintOrbit(FrameCollector& fc, float ax, float ay, float side) const;
        /// @brief Text skeleton (**not a square**: fills the widget width, line height / spacing follow their own rules)
        void paintSkeleton(FrameCollector& fc) const;

        LoadingVariant variant_ = LoadingVariant::Ring;
        bool  running_ = true;
        float percent_ = -1.0f;

        std::string text_;
        TextStyle   textStyle_;

        float size_ = 0.0f;         /// < Explicit side length (0 = follow theme)
        float thickness_ = 0.0f;    /// < Explicit line width (0 = follow theme)
        int   speedMs_ = 0;         /// < Explicit speed (0 = follow theme)
        int   itemCount_ = 0;       /// < Explicit dot / bar count (0 = follow theme)

        Color4f color_{ 0.0f, 0.0f, 0.0f, 1.0f };
        Color4f trackColor_{ 0.0f, 0.0f, 0.0f, 0.0f };

        // ---- Text skeleton (Skeleton) ----
        float lastLineRatio_ = 0.62f;   /// < Proportion of line width taken by the last line
        bool  shimmer_ = true;          /// < Highlight sweep

        bool colorSet_ = false;        /// < User explicitly set foreground color -> no longer overridden by theme
        bool trackColorSet_ = false;   /// < Same as above (track color)

        // Theme injection
        float themeSize_ = 28.0f;
        float themeThickness_ = 3.0f;
        float themeTextGap_ = 8.0f;
        int   themeItemCount_ = 8;
        int   themeSpeedMs_ = 900;

        // Animation state (rewritten during paint -> mutable)
        mutable float phase_ = 0.0f;
        mutable long long lastTickMs_ = 0;
    };

} // namespace Kit
