/**
 * @file ZeBadge.h
 * @brief Badge: a small color block (number / short text), or a plain dot
 *
 * @details
 * `Badge : TextWidget` -- it carries one line of fixed text ("9", "99+", "NEW", "draft"),
 * so it inherits `TextWidget` (same reasoning as `Checkbox` / `RadioButton`: the label is
 * part of the widget identity, not a `Label` hung beside it).
 *
 * @par Two forms
 *  - Capsule (default): background color + text; height / min width / horizontal padding come from the theme;
 *  - Dot (`SetDot(true)`): a solid small circle with no text -- for cases like "new message"
 *    that need only a signal, not a count.
 *
 * @par Semantic colors
 * `BadgeTone` has five levels (primary / danger / success / warning / neutral), each mapping to a pair
 * of "background color + text color" in the theme. Colors do not change on hover -- a badge's semantics are "what it represents",
 * not "the mouse is hovering over it".
 *
 * @code
 * Kit::Badge unread;
 * unread.SetTone(Kit::BadgeTone::Danger);
 * unread.SetCount(128);            // shows "99+"
 * Kit::Badge dot;
 * dot.SetDot(true);                // a red dot
 * @endcode
 *
 * @see ZeTheme.h `BadgeStyle`
 */

#pragma once
#include "ZeTextWidget.h"

#include <string>

namespace Kit {

    /// @brief Badge semantic color
    enum class BadgeTone {
        Primary,   /// < Primary color (default)
        Danger,    /// < Danger / error
        Success,   /// < Success / online
        Warning,   /// < Warning
        Neutral    /// < Neutral (gray)
    };

    /**
     * @brief Badge widget
     */
    class Badge : public TextWidget {
    public:
        /// @brief Constructs
        /// @param text Badge text (may be empty; combined with `SetDot` it is a plain dot)
        explicit Badge(std::string text = std::string());

        // -------------------------------------------------------------------
        //  Form
        // -------------------------------------------------------------------

        /// @brief Switches to the "plain dot" form (true) / back to capsule (false)
        void SetDot(bool dot);
        bool isDot() const { return dot_; }

        /// @brief Dot diameter (pixels); <= 0 follows theme `badge.dotSize`
        void SetDotSize(float px);
        float dotSize() const {
            return dotSizeOverride_ > 0.0f ? dotSizeOverride_ : themeDotSize_;
        }

        // -------------------------------------------------------------------
        //  Semantic color
        // -------------------------------------------------------------------

        void SetTone(BadgeTone tone);
        BadgeTone tone() const { return tone_; }

        // -------------------------------------------------------------------
        //  Count convenience entry
        // -------------------------------------------------------------------

        /// @brief Sets the text from a count (shows `"limit+"` when `n > maxDisplay()`)
        /// @note The count is "data", not style -- changing the theme does not reset it (same as Slider's fill ratio).
        void SetCount(int n);

        /// @brief Display limit (default 99 -> "99+"); <= 0 means no cap
        void SetMaxDisplay(int maxDisplay);
        int maxDisplay() const { return maxDisplay_; }

        // -------------------------------------------------------------------
        //  Appearance (value interface)
        // -------------------------------------------------------------------

        /// @brief Overrides the background color (after override it no longer changes with `SetTone`)
        /// @note Named differently from `BoxWidget::SetBodyColor` to avoid shadowing the base class interface of the same name.
        void SetBadgeColor(const Color4f& c);
        /// @brief Overrides the text color (after override it no longer changes with `SetTone`)
        void SetLabelColor(const Color4f& c);

        // -------------------------------------------------------------------
        //  Measurement protocol
        // -------------------------------------------------------------------
        float hintWidth() const override;
        float hintHeight() const override;

        // -------------------------------------------------------------------
        //  Theme
        // -------------------------------------------------------------------
        void ApplyTheme(const Theme& t) override;

    protected:
        /// @brief Paints the capsule / dot background first, then the text (text uses the base class centered layout)
        void paintContent(FrameCollector& fc) const override;

    private:
        /// @brief Returns the background color for the current tone
        const Color4f& toneBackground() const;
        /// @brief Returns the text color for the current tone
        const Color4f& toneForeground() const;
        /// @brief Syncs the current tone's text color into textStyle_ (the base class takes its color from it)
        void syncTextColor();

        BadgeTone tone_ = BadgeTone::Primary;
        bool  dot_ = false;
        int   maxDisplay_ = 99;

        float dotSizeOverride_ = 0.0f;
        float themeDotSize_ = 8.0f;
        float minWidth_ = 18.0f;
        float fixedHeight_ = 18.0f;
        float paddingX_ = 6.0f;
        float radius_ = 9.0f;

        bool  bodyColorSet_ = false;   /// < User explicitly set a background color -> no longer overridden by tone
        bool  textColorSet_ = false;   /// < User explicitly set a text color -> no longer overridden by tone

        Color4f bodyOverride_{ 0.0f, 0.0f, 0.0f, 0.0f };
        Color4f textOverride_{ 0.0f, 0.0f, 0.0f, 1.0f };

        // Five semantic color levels (injected by theme)
        Color4f bgPrimary_{ 0.0f, 0.0f, 0.0f, 1.0f };
        Color4f bgDanger_{ 0.0f, 0.0f, 0.0f, 1.0f };
        Color4f bgSuccess_{ 0.0f, 0.0f, 0.0f, 1.0f };
        Color4f bgWarning_{ 0.0f, 0.0f, 0.0f, 1.0f };
        Color4f bgNeutral_{ 0.0f, 0.0f, 0.0f, 1.0f };

        Color4f fgPrimary_{ 1.0f, 1.0f, 1.0f, 1.0f };
        Color4f fgDanger_{ 1.0f, 1.0f, 1.0f, 1.0f };
        Color4f fgSuccess_{ 1.0f, 1.0f, 1.0f, 1.0f };
        Color4f fgWarning_{ 1.0f, 1.0f, 1.0f, 1.0f };
        Color4f fgNeutral_{ 0.0f, 0.0f, 0.0f, 1.0f };
    };

} // namespace Kit
