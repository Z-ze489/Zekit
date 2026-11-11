/**
 * @file ZeTimePicker.h
 * @brief Time picker: handles only the time within a single day (hour / minute / second), with a popup panel
 *
 * @details
 * `TimePicker : TextWidget` -- when collapsed it looks like an input box (one line of time text + arrow on the right),
 * when expanded it pops up a column-style popup panel below (one column each for hour / minute / second, optionally an AM/PM column),
 * clicking a number in a column commits that field.
 *
 * @par Division of labor with `DateTimePicker`
 * That one handles "year-month-day + hour-minute" (calendar popup panel), this class handles only "hour-minute-second" (column-style popup panel) --
 * everything the two share is below, not written twice:
 *  - value type and formatting -- `FormatTime` / `NowLocal` in `ZeDateTime.h`;
 *  - the three-half overlay convention -- `paintsOnTop()` / `HitTest()` / `ResetClip()`
 *    (same path as `ComboBox` / time popup panel, see each header's notes);
 *  - drawing primitives -- `ZeDrawUtil.h`.
 *
 * @par Interaction
 * | Operation | Behavior |
 * |---|---|
 * | Left click on the face | Expand <-> collapse |
 * | Left click on a number in a column | Commits that field (takes effect immediately) |
 * | Wheel while expanded | Scrolls within the column under the pointer |
 * | `Up` / `Down` while expanded | Value of the current column +/-1 |
 * | `Left` / `Right` while expanded | Switches the current column |
 * | `Esc` / `Enter` while expanded | Collapse |
 * | Loses focus | Collapse |
 *
 * @code
 * Kit::TimePicker tp;
 * tp.SetWithSeconds(true);
 * tp.SetUse24Hour(false);
 * tp.SetTime(9, 30, 0);
 * tp.SetOnTimeChanged([](Kit::Widget& self) {
 *     auto& t = Kit::As<Kit::TimePicker>(self);
 *     std::printf("%02d:%02d\n", t.hour(), t.minute());
 * });
 * @endcode
 *
 * @see ZeDateTimePicker.h date + time picker (calendar popup panel)
 * @see ZeTheme.h `TimePickerStyle`
 */

#pragma once
#include "ZeTextWidget.h"
#include "ZePopupHost.h"   // Common base class of the popup panel carrier (this class implements only geometry / painting / input)
#include "ZeSelfAction.h"

#include <functional>
#include <memory>
#include <string>

namespace Kit {

    class OverlayWindow;   /// < native popup panel carrier (shared per layer, see ZeOverlayWindow.h)

    /**
     * @brief Time picker (time only)
     */
    class TimePicker : public PopupHostWidget {
    public:
        /// @brief Destructor (defined in .cpp -- `unique_ptr<OverlayWindow>` needs the complete type)
        ~TimePicker() override;

        /// @brief Value semantics: moves with AddWidget (explicit destructor suppresses implicit move, must be restored)
        TimePicker(TimePicker&&) noexcept;

        /// @brief Constructs (default 0:00, 24-hour clock, no seconds)
        TimePicker();

        // -------------------------------------------------------------------
        //  Values (data, not style -- changing theme does not reset)
        // -------------------------------------------------------------------

        /// @brief Sets the time directly (out-of-range values are clamped to the legal range), does not trigger callbacks
        void SetTime(int hour, int minute, int second = 0);

        /// @brief Sets to the current local time (via `ZeDateTime.h::NowLocal`), does not trigger callbacks
        /// @return true = system time was obtained
        bool SetNow();

        int hour() const { return hour_; }
        int minute() const { return minute_; }
        int second() const { return second_; }

        /// @brief Whether to show seconds (when false neither the face nor the popup panel shows "second")
        void SetWithSeconds(bool on);
        bool withSeconds() const { return withSeconds_; }

        /// @brief 24-hour clock (false = 12-hour clock + AM/PM column)
        void SetUse24Hour(bool on);
        bool use24Hour() const { return use24Hour_; }

        /// @brief Minute step (1 = every minute; e.g. 5 -> only lists 00/05/10...)
        void SetMinuteStep(int step);
        int minuteStep() const { return minuteStep_; }

        /// @brief Second step (1 = every second)
        void SetSecondStep(int step);
        int secondStep() const { return secondStep_; }

        // -------------------------------------------------------------------
        //  Expanded state
        // -------------------------------------------------------------------

        bool isOpen() const { return open_; }
        void Open();
        void Close();
        void Toggle();
        bool SetOpen(bool open);

        // -------------------------------------------------------------------
        //  Callbacks (triggered only on the user path)
        // -------------------------------------------------------------------
        //  Consistent with the whole library: programmatic `SetTime` does not trigger -- callback semantics are "the user changed it".

        /// @brief "Time changed" callback (carries only the sender; read values via `hour()/minute()/second()`)
        void SetOnTimeChanged(SelfAction handler);

        /// @brief "Time changed" callback (carries a payload of "seconds since midnight")
        void SetOnTimeChanged(SelfAction1<int> handler);

        // -------------------------------------------------------------------
        //  Appearance
        // -------------------------------------------------------------------

        /// @brief Maximum rows shown per column (overflow scrolls via wheel); <= 0 = unlimited
        void SetMaxVisibleItems(int n);
        int maxVisibleItems() const { return maxVisibleItems_; }

        /// @brief Row height of a single row (pixels); <= 0 = follow theme
        void SetItemHeight(float px);

        /// @brief Color of the right arrow
        void SetArrowColor(const Color4f& c);

        // -------------------------------------------------------------------
        //  Measurement protocol
        // -------------------------------------------------------------------
        float hintWidth() const override;
        float hintHeight() const override;

        // -------------------------------------------------------------------
        //  Overlay (library-level convention)
        // -------------------------------------------------------------------
        bool paintsOnTop() const override;
        bool HitTest(float x, float y) const override;

        // -------------------------------------------------------------------
        //  Theme
        // -------------------------------------------------------------------
        void ApplyTheme(const Theme& t) override;

        // -------------------------------------------------------------------
        //  Interaction events
        // -------------------------------------------------------------------
        bool OnMousePress(MouseEvent& e) override;
        bool OnMouseRelease(MouseEvent& e) override;
        bool OnMouseMove(MouseEvent& e) override;
        bool OnMouseWheel(MouseEvent& e) override;
        void OnMouseLeave() override;
        bool OnKeyPress(KeyEvent& e) override;
        void OnFocusOut() override;

    protected:
        /// @brief Default box style taken from theme `timePicker.box`
        const BoxStyle& themeBox(const Theme& t) const override;
        /// @brief Default text style taken from theme `timePicker.text`
        const TextStyle& themeText(const Theme& t) const override;

        /// @brief Text + arrow on the face; when expanded also overlays the popup panel
        void paintContent(FrameCollector& fc) const override;

    private:
        /// @brief One column in the popup panel
        enum class Col { Hour = 0, Minute = 1, Second = 2, Meridiem = 3 };

        /// @brief Popup panel geometry (shared by painting and hit test -- avoids "looks like row 3, clicks row 2")
        struct PopupGeom {
            float x = 0, y = 0, w = 0, h = 0;
            float innerX = 0, innerY = 0;
            float colW = 0, headerH = 0, itemH = 0;
            float footerH = 0;
            int   colCount = 0;
            int   rows = 0;            /// < number of visible rows per column
            int   maxRows = 0;         /// < maximum number of rows across all columns (popup panel height is computed from this)
            bool  flipped = false;     /// < true = pops above the face
        };

        PopupGeom computePopup() const;
        bool popupAnchorVisible() const;

        /// @brief Total number of entries in column col
        int itemCountOf(Col c) const;
        /// @brief Text to display for row row in column col
        std::string itemTextOf(Col c, int row) const;
        /// @brief Index of the currently selected item in column col (-1 = none)
        int selectedRowOf(Col c) const;
        /// @brief Applies row row of column col to the value (user path, does not trigger callbacks)
        void applyRow(Col c, int row);
        /// @brief Column header text
        std::string headerOf(Col c) const;

        float resolvedItemHeight() const;
        float resolvedColWidth() const;
        float resolvedArrowSize() const;

        /// @brief Which column and row of the popup panel the pointer is in (-1 = not in)
        void hitPopup(float px, float py, int& col, int& row) const;
        /// @brief Whether the pointer is on the bottom "Now" button
        bool hitFooter(float px, float py) const;

        /// @brief Current column (for keyboard operation)
        Col activeCol() const { return activeCol_; }

        // -------------------------------------------------------------------
        //  Native popup panel carrier (OverlayWindow path; always empty on fallback platforms)
        // -------------------------------------------------------------------

        /// @brief The popup panel "logically exists and the anchor is visible" (criterion shared by the carrier / host paths)
        bool popupActive() const;
        /// @brief Popup panel rectangle (base class uses it to place the carrier; returns false = should not pop right now)
        bool popupRect(float& x, float& y, float& w, float& h) const override;
        /// @brief Should collapse (taken over by the same layer / clicked elsewhere on the host)
        void dismissPopup() override;

        /// @brief Paints only the popup panel (when `localPopup=true` the origin is the popup panel's top-left corner)
        void paintPopupInto(FrameCollector& fc, bool localPopup) const override;

        /// @brief Mouse in the carrier (coordinates are already in host space)
        bool onPopupPress(MouseEvent& e) override;
        bool onPopupRelease(MouseEvent& e) override;
        bool onPopupMove(MouseEvent& e) override;
        bool onPopupWheel(MouseEvent& e) override;
        void onPopupLeave() override;

        /// @brief Clamps the scroll of column @p col (visible column index, 0 = first column) back to the legal range.
        ///        The whole library uses "column index" as the subscript of scroll_ -- do not index with Col enum values,
        ///        in 12-hour clock without a seconds column (columns = hour/minute/AM-PM) the two are misaligned.
        void clampScroll(int col);
        /// @brief Fills the visible column list (in display order) according to the current withSeconds_ / use24Hour_,
        ///        returns the column count. Every place that "iterates columns" must go through it -- column index = array subscript.
        int  fillCols(Col cols[4]) const;
        void requestRepaint();
        void notifyTimeChanged();
        void syncFaceText();

        // ---- Values ----
        int  hour_ = 0;
        int  minute_ = 0;
        int  second_ = 0;
        bool withSeconds_ = false;
        bool use24Hour_ = true;
        int  minuteStep_ = 1;
        int  secondStep_ = 1;

        // ---- Popup panel state ----
        //  NOTE: `open_` and the carrier `popupOverlay_` have moved up to PopupHostWidget
        int  scroll_[4] = { 0, 0, 0, 0 };   /// < first visible row of each column
        int  hoverCol_ = -1;
        int  hoverRow_ = -1;
        bool hoverFooter_ = false;
        Col  activeCol_ = Col::Hour;
        bool pressedInFace_ = false;
        bool pressedInPopup_ = false;
        bool pressedInFooter_ = false;

        // ---- Appearance ----
        int   maxVisibleItems_ = 7;
        float itemHeightOverride_ = 0.0f;
        float themeItemHeight_ = 26.0f;
        float themeColWidth_ = 46.0f;
        float themeHeaderH_ = 22.0f;
        float themeFooterH_ = 28.0f;
        float themeGap_ = 4.0f;
        float themeRadius_ = 4.0f;
        Color4f arrowColor_{ 0.0f, 0.0f, 0.0f, 1.0f };
        Color4f itemHoverColor_{ 0.0f, 0.0f, 0.0f, 0.0f };
        Color4f itemSelectedColor_{ 0.0f, 0.0f, 0.0f, 0.0f };
        Color4f selectedTextColor_{ 1.0f, 1.0f, 1.0f, 1.0f };
        Color4f dividerColor_{ 0.0f, 0.0f, 0.0f, 0.0f };

        BoxStyle  popupBox_;
        TextStyle headStyle_;
        TextStyle valueStyle_;
        TextStyle actionStyle_;

        // ---- Callbacks ----
        SelfAction      onChanged_;
        SelfAction1<int> onChangedSecs_;
    };

} // namespace Kit
