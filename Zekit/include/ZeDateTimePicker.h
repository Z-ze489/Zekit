/**
 * @file ZeDateTimePicker.h
 * @brief Date-time picker: an input box when collapsed, a calendar popup panel (with time) when expanded
 *
 * @details
 * `DateTimePicker : TextWidget` -- the collapsed state is "an input box with a calendar icon";
 * clicking it pops up a calendar popup panel where you can page months, click days, adjust time, then press "OK"
 * in the panel or press `Enter` to confirm.
 *
 * @par Why the popup panel is custom-drawn (instead of stuffing in a bunch of child widgets)
 * If the cells in the popup panel were child widgets, they would have to be focusable (otherwise clicking them would have focus arbitration clear
 * focus -> `OnFocusOut` -> the popup panel closes immediately, unusable). And once there are focusable child widgets,
 * focus would jump back and forth inside the popup panel, likewise triggering close. So all interaction in the popup panel is handled by this widget
 * itself: geometry is computed once (`computePopup` / `layoutCells`), shared by paint and hit test;
 * keyboard events are also all dispatched in this widget's `OnKeyPress`. This is the same path as the `ComboBox` popup panel.
 *
 * @par Calendar algorithm
 * All integer arithmetic, no time zone, no system calls (see `ZeDateTime.h`). "What weekday is the 1st of this month"
 * uses Zeller's congruence, no table and no loop.
 *
 * @par Usage
 * @code
 * Kit::DateTimePicker picker;
 * picker.SetShowTime(true, false);                  // with time, without seconds
 * picker.SetValue({ 2026, 3, 14, 9, 30, 0 });
 * picker.SetOnValueCommitted([](Kit::Widget& self, const Kit::DateTime& v) {
 *     std::printf("selected %04d-%02d-%02d %02d:%02d\n", v.year, v.month, v.day, v.hour, v.minute);
 * });
 * win.AddWidget(picker);
 * @endcode
 *
 * @note "User changed and confirmed" and "program `SetValue`" are two paths: only the former triggers the callback
 *       -- the same rule as `TabView` / `ComboBox` / `Slider`.
 * @note What is edited in the popup panel is a draft (`draft_`); pressing "Cancel" or `Esc` discards the draft,
 *       only pressing "OK" or `Enter` writes back to `value_` and invokes the callback. So "open then change your mind" is safe.
 *
 * @see ZeDateTime.h       calendar algorithm and the `DateTime` value type
 * @see ZeComboBox.h       the same "floating popup panel" reference implementation
 * @see ZeColorPicker.h    another picker of the same family
 */

#pragma once
#include "ZeTextWidget.h"
#include "ZePopupHost.h"   // Common base class of the popup panel carrier (this class only implements geometry / paint / input)
#include "ZeSelfAction.h"
#include "ZeDateTime.h"
#include "ZeTheme.h"

#include <cstddef>
#include <memory>
#include <string>

namespace Kit {

    class OverlayWindow;   /// < native popup panel carrier (shared per layer, see ZeOverlayWindow.h)

    /**
     * @brief Date-time picker
     *
     * @details
     * Inheritance chain: `Widget -> BoxWidget -> TextWidget -> DateTimePicker`.
     * The theme takes the `datePicker` section (collapsed-state box + popup panel skeleton + text of each part).
     */
    class DateTimePicker : public PopupHostWidget {
    public:
        DateTimePicker();

        /// @brief Destructor (defined in .cpp -- `unique_ptr<OverlayWindow>` needs the complete type)
        ~DateTimePicker() override;

        /// @brief Value semantics: moved along with AddWidget
        /// @note Must be explicitly restored: the explicit destructor above suppresses the implicit move constructor, and widgets
        ///       have value semantics, `AddWidget` moves them -- without it the whole tree fails to compile (same pitfall as section 26).
        DateTimePicker(DateTimePicker&&) noexcept;

        // -------------------------------------------------------------------
        //  Value (data, not style -- changing theme does not reset)
        // -------------------------------------------------------------------

        /// @brief Sets the value programmatically (does not trigger the callback; automatically clamped to a valid date)
        void SetValue(const DateTime& v);
        /// @brief Current value
        const DateTime& Value() const { return value_; }

        /// @brief Selectable range (default is very wide); setting the range clamps the current value into it
        /// @note Dates out of range in the popup panel are drawn gray and cannot be clicked (equivalent to the `SetItemEnabled` semantics).
        void SetRange(const DateTime& minValue, const DateTime& maxValue);
        /// @brief Minimum selectable value
        const DateTime& MinValue() const { return minValue_; }
        /// @brief Maximum selectable value
        const DateTime& MaxValue() const { return maxValue_; }

        /// @brief Whether to show the time part (default true)
        /// @param withSeconds Whether to show seconds (default false)
        void SetShowTime(bool showTime, bool withSeconds = false);
        /// @brief Whether to show the time part
        bool showsTime() const { return showTime_; }
        /// @brief Whether to show seconds
        bool showsSeconds() const { return showSeconds_; }

        /// @brief Display format string (`%Y`/`%m`/`%d`/`%H`/`%M`/`%S`) -- empty string = use built-in format
        /// @note The built-in format is chosen automatically by `showsTime()` / `showsSeconds()`
        ///       (`"YYYY-MM-DD"` / `"YYYY-MM-DD HH:MM"` / `"...:SS"`).
        void SetFormat(std::string fmt);
        /// @brief Current format string (empty = built-in)
        const std::string& format() const { return format_; }

        // -------------------------------------------------------------------
        //  Popup panel
        // -------------------------------------------------------------------

        /// @brief Opens / closes the popup panel programmatically (does not trigger the callback)
        /// @return Whether it is open after the request
        bool SetOpen(bool open);
        /// @brief Whether the popup panel is open
        bool isOpen() const { return open_; }

        /// @brief Jumps to "today" (uses system local time; silently does nothing on failure)
        void SelectToday();

        // -------------------------------------------------------------------
        //  Callbacks (triggered only on the user path)
        // -------------------------------------------------------------------

        /// @brief The user confirmed a new value ("OK" button / `Enter`)
        /// @note Triggered only on the user path; program `SetValue` does not send it (prevents loops, consistent across the library).
        void SetOnValueCommitted(SelfAction1<const DateTime&> cb) { onCommitted_ = std::move(cb); }
        /// @brief User confirmation callback (form that only cares about the sender)
        void SetOnValueCommitted(SelfAction cb) { onCommittedSelf_ = std::move(cb); }

        /// @brief Value changed (confirmation / cancel / programmatic set all count) -- only notifies that "the display needs updating"
        void SetOnValueChanged(SelfAction1<const DateTime&> cb) { onChanged_ = std::move(cb); }

        // -------------------------------------------------------------------
        //  Measurement protocol
        // -------------------------------------------------------------------
        float hintWidth() const override;
        float hintHeight() const override;

        // -------------------------------------------------------------------
        //  Overlay protocol (three pieces, all required -- see the notes in ZeComboBox.cpp)
        // -------------------------------------------------------------------
        bool paintsOnTop() const override;
        bool HitTest(float x, float y) const override;

        // -------------------------------------------------------------------
        //  Theme
        // -------------------------------------------------------------------
        void ApplyTheme(const Theme& t) override;

        // -------------------------------------------------------------------
        //  Paint / events
        // -------------------------------------------------------------------
        // @brief Face (date text + calendar icon) -> popup panel (header / weekdays / grid / time / buttons)
        void Paint(FrameCollector& fc) const override;

        bool OnMouseRelease(MouseEvent& e) override;
        bool OnMouseMove(MouseEvent& e) override;
        bool OnMouseWheel(MouseEvent& e) override;
        void OnMouseLeave() override;
        bool OnKeyPress(KeyEvent& e) override;
        void OnFocusOut() override;

    protected:
        const BoxStyle& themeBox(const Theme& t) const override;
        const TextStyle& themeText(const Theme& t) const override;

    private:
        // ===================================================================
        //  Popup panel geometry (shared by paint and hit test -- only one copy)
        // ===================================================================

        /// @brief Popup panel rectangle
        struct PopupGeom {
            float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
        };
        PopupGeom computePopup() const;

        /// @brief Rectangles of each block inside the popup panel (computed all at once, shared by paint and hit test)
        struct Layout {
            PopupGeom panel;
            float headY = 0.0f, headH = 0.0f;        /// < header (year-month + page arrows)
            float weekY = 0.0f, weekH = 0.0f;        /// < weekday header
            float gridX = 0.0f, gridY = 0.0f;        /// < top-left corner of the date grid
            float cellW = 0.0f, cellH = 0.0f;        /// < single cell size
            float timeY = 0.0f, timeH = 0.0f;        /// < time row
            float footerY = 0.0f, footerH = 0.0f;    /// < button row (today / OK / cancel)
        };
        Layout computeLayout() const;

        /// @brief Which clickable zone it falls on (-1 = none)
        enum class Zone {
            None = 0,
            PrevMonth, NextMonth, PrevYear, NextYear,
            Day, HourUp, HourDown, MinuteUp, MinuteDown, SecondUp, SecondDown,
            Today, Confirm, Cancel
        };
        /// @brief Hit query result
        struct Hit {
            Zone zone = Zone::None;
            int  year = 0;     /// < the cell's real year when zone == Day (the grid has adjacent-month cells at both ends)
            int  month = 0;    /// < the cell's real month when zone == Day (1~12)
            int  day = 0;      /// < the "day" when zone == Day (1~31)
        };
        Hit hitTestPopup(float px, float py) const;

        // -------------------------------------------------------------------
        //  Native popup panel carrier (OverlayWindow path; always null on fallback platforms)
        // -------------------------------------------------------------------

        /// @brief Popup panel rectangle (the base class uses it to place the carrier; returns false = should not pop up right now)
        bool popupRect(float& x, float& y, float& w, float& h) const override;
        /// @brief Should be dismissed (taken over by the same layer / clicked elsewhere on the host)
        void dismissPopup() override;

        /// @brief Paints only the popup panel (with `localPopup=true`, origin is the popup panel's top-left corner)
        void paintPopupInto(FrameCollector& fc, bool localPopup) const override;

        /// @brief Mouse inside the carrier (coordinates are already in host space)
        bool onPopupPress(MouseEvent& e) override;
        bool onPopupRelease(MouseEvent& e) override;
        bool onPopupMove(MouseEvent& e) override;
        bool onPopupWheel(MouseEvent& e) override;
        void onPopupLeave() override;

        /// @brief Date of the cell at `index` (0~41); `outOfMonth` indicates it does not belong to the currently displayed month
        void cellDate(int index, int& y, int& m, int& d, bool& outOfMonth) const;

        // -------------------------------------------------------------------
        //  Paint sub-steps (split out of Paint for readability and locating)
        // -------------------------------------------------------------------

        /// @brief Paints the collapsed-state face: date text (left) + calendar icon (right)
        /// @details
        /// WARNING: the date text must be explicitly painted here: this class's value lives in `value_` (`DateTime`),
        /// it never writes the base class's `text_`, so the default path of `TextWidget::paintContent` that "paints text_"
        /// is empty here -- missing this step means "no date in the input box"
        /// (`formatValue` is only called in `hintWidth`, so the width is measured but nobody paints it).
        void paintFace(FrameCollector& fc) const override;

        /// @brief Paints the whole popup panel (skeleton + header + weekdays + grid + time + buttons)
        /// @param L     the already-computed layout (do not recompute here -- that would break "geometry has only one copy")
        /// @param theme the theme (skeleton colors come from `fc.GetTheme()`; passing it in here only saves one call)
        void PaintPopup(FrameCollector& fc, const Layout& L, const Theme& theme) const;

        // ===================================================================
        //  State changes (unified funnel for the user path)
        // ===================================================================

        /// @brief The user clicked a zone
        void handleZone(const Hit& h);
        /// @brief Writes the draft back to the value + invokes the callback (the user path's "OK")
        void commitDraft();
        /// @brief Discards the draft (the user path's "Cancel" / `Esc`)
        void cancelDraft();
        /// @brief Fires the "display needs updating" notification (`SetOnValueChanged`)
        void notifyChanged();

        /// @brief Current time fields used when constructing "the date-time corresponding to a cell"
        /// @note Makes the range comparison of those dates in the grid include the time; returns 0 when time is not shown.
        int wantHour() const;
        int wantMinute() const;
        int wantSecond() const;

        /// @brief Opens the popup panel (copies the current value into the draft and aligns the displayed month to the current value)
        void openPopup();
        /// @brief Closes the popup panel
        void closePopup();

        /// @brief Formats the display into a string (by `format_` or the built-in format)
        std::string formatValue(const DateTime& v) const;

        /// @brief Requests a repaint (silently skipped when there is no window)
        void requestRepaint();

        /// @brief Unified funnel for "text changed -> natural size changed too"
        /// @note This class derives from the `TextWidget` branch and does not have `LayoutBase::Invalidate()`
        ///       (that only exists on layout containers) -- rely on `notifyLayoutInputChanged()` to let
        ///       the host know to re-layout, plus one more repaint request.
        void markMeasureDirty();

        /// @brief Whether the popup panel anchor is actually visible (does not pop up if hidden / clipped anywhere up the parent chain)
        bool popupAnchorVisible() const;

        /// @brief Comparison of two date-times (used for range clamping)
        static bool Less(const DateTime& a, const DateTime& b);

        // ===================================================================
        //  State
        // ===================================================================

        DateTime value_{};        /// < confirmed value
        DateTime draft_{};        /// < draft being edited in the popup panel
        DateTime minValue_{ 1900, 1, 1, 0, 0, 0 };
        DateTime maxValue_{ 2999, 12, 31, 23, 59, 59 };

        bool showTime_ = true;
        bool showSeconds_ = false;
        std::string format_;      /// < empty = built-in format

        // ---- popup panel displayed month ----
        int shownYear_ = 2000;
        int shownMonth_ = 1;

        // ---- interaction transients (do not change data, no markStyled needed) ----
        //  * `open_` and the carrier `popupOverlay_` have been moved up to PopupHostWidget
        bool  pressedInPopup_ = false;
        Hit   hover_;             /// < currently hovered zone (used for highlight painting)

        // ---- theme injection ----
        DatePickerStyle styleTheme_;

        // ---- callbacks ----
        //  `std::function` is movable and is moved along with this class's implicit move constructor by AddWidget.
        SelfAction1<const DateTime&> onCommitted_;      /// < user confirmation (user path only)
        SelfAction                   onCommittedSelf_;  /// < user confirmation (sender only)
        SelfAction1<const DateTime&> onChanged_;        /// < value change (also sent on programmatic set)
    };

} // namespace Kit
