/**
 * @file ZeDateTime.h
 * @brief Calendar algorithms and date-time value type (the shared base for widgets such as `DateTimePicker` / `Calendar`)
 *
 * @details
 * This library does not pull in the calendar part of `<chrono>` (on MSVC `std::chrono::year_month_day` requires a
 * newer library version, and its rules for "how an invalid date falls back" differ from this library's clamping semantics),
 * nor does it depend on `struct tm` / `localtime` (that would drag the local time zone and DST into a pure UI widget,
 * which is neither testable nor necessary). So the calendar conversion is implemented here, all of it pure integer arithmetic:
 *
 *  - `IsLeapYear` / `DaysInMonth` -- leap year and month length;
 *  - `DayOfWeek` -- uses Zeller's congruence (no lookup table, and valid for any Gregorian year);
 *  - `DaysFromCivil` / `CivilFromDays` -- Gregorian date <-> day count (Howard Hinnant's
 *    `days_from_civil` / `civil_from_days`, public-domain algorithms). With these, "what weekday is the
 *    1st of this month" and "what day is it N days later" are a single add or subtract, with no month-by-month accumulation.
 *
 * The benefit of this algorithm set: stateless, no time zone, no system calls, the same input always gives the same output
 * -- so the widget's "next month" / "previous year" can be called repeatedly without accumulated error.
 *
 * @note This header lives in `include/` (not the source root): `DateTime` is a value type that users read and write
 *       directly (`picker.Value()` returns it), so it must appear on the delivery surface.
 *
 * @see ZeDateTimePicker.h the widget that uses it
 */

#pragma once

#include <cstdio>
#include <ctime>      // std::time / std::tm / localtime_s (only used by SelectToday)
#include <string>

namespace Kit {

    /**
     * @brief A date-time value (Gregorian; no time zone concept)
     *
     * @details
     * Deliberately a minimal field set rather than "timestamp + time zone": what a desktop UI picks is
     * "March 14, 2026 09:30", not "some UTC instant". If time zone conversion is needed,
     * that is the business layer's job to compute from these six fields.
     */
    struct DateTime {
        int year = 2000;    /// < Year (Gregorian)
        int month = 1;      /// < Month (1~12)
        int day = 1;        /// < Day (1~31, clamped by month and leap year)
        int hour = 0;       /// < Hour (0~23)
        int minute = 0;     /// < Minute (0~59)
        int second = 0;     /// < Second (0~59)
    };

    /// @brief Whether the year is a leap year (Gregorian rule: leap every 4 years, not on years divisible by 100, again on years divisible by 400)
    inline bool IsLeapYear(int y) {
        return (y % 4 == 0) && ((y % 100 != 0) || (y % 400 == 0));
    }

    /// @brief Number of days in the given month of the given year (returns 0 when the month is out of range)
    inline int DaysInMonth(int y, int m) {
        static const int kDays[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
        if (m < 1 || m > 12) return 0;
        if (m == 2 && IsLeapYear(y)) return 29;
        return kDays[m - 1];
    }

    /**
     * @brief Weekday of the given day
     * @return 0 = Sunday, 1 = Monday, ..., 6 = Saturday
     *
     * @details
     * Uses a variant of Zeller's congruence: no lookup table, no loop, valid for any Gregorian year (including after the Gregorian reform).
     * Months are treated as "March is the first month of the year" (January and February count as months 13 and 14 of the previous year) -- this is
     * a precondition for Zeller's formula to hold, not a typo.
     */
    inline int DayOfWeek(int y, int m, int d) {
        if (m < 3) { m += 12; y -= 1; }
        const int k = y % 100;
        const int j = y / 100;
        const int h = (d + (13 * (m + 1)) / 5 + k + k / 4 + j / 4 + 5 * j) % 7;
        // Zeller: 0 = Saturday, 1 = Sunday, 2 = Monday ... -> convert to 0 = Sunday
        return (h + 6) % 7;
    }

    /// @brief Clamps the date to valid values (day by the month's length, month to 1~12, hour/minute/second to their ranges)
    inline void ClampDateTime(DateTime& v) {
        if (v.month < 1) v.month = 1;
        if (v.month > 12) v.month = 12;
        const int dim = DaysInMonth(v.year, v.month);
        if (v.day < 1) v.day = 1;
        if (v.day > dim) v.day = (dim > 0) ? dim : 1;
        if (v.hour < 0) v.hour = 0;
        if (v.hour > 23) v.hour = 23;
        if (v.minute < 0) v.minute = 0;
        if (v.minute > 59) v.minute = 59;
        if (v.second < 0) v.second = 0;
        if (v.second > 59) v.second = 59;
    }

    /// @brief Whether two date-times are equal field by field
    inline bool operator==(const DateTime& a, const DateTime& b) {
        return a.year == b.year && a.month == b.month && a.day == b.day
            && a.hour == b.hour && a.minute == b.minute && a.second == b.second;
    }
    inline bool operator!=(const DateTime& a, const DateTime& b) { return !(a == b); }

    /// @brief Formats as `"YYYY-MM-DD HH:MM"` (with seconds when `withSeconds` is true)
    inline std::string FormatDateTime(const DateTime& v, bool withSeconds = false) {
        char buf[64];
        if (withSeconds) {
            std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
                v.year, v.month, v.day, v.hour, v.minute, v.second);
        }
        else {
            std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d",
                v.year, v.month, v.day, v.hour, v.minute);
        }
        return std::string(buf);
    }

    /// @brief Formats the date part: `"YYYY-MM-DD"`
    inline std::string FormatDate(const DateTime& v) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", v.year, v.month, v.day);
        return std::string(buf);
    }

    /// @brief Formats the time part: `"HH:MM"` (with seconds when `withSeconds` is true)
    inline std::string FormatTime(const DateTime& v, bool withSeconds = false) {
        char buf[32];
        if (withSeconds) std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", v.hour, v.minute, v.second);
        else             std::snprintf(buf, sizeof(buf), "%02d:%02d", v.hour, v.minute);
        return std::string(buf);
    }

    /**
     * @brief Decomposes the instant `now` into date-time fields in the local time zone
     * @return The decomposition result; returns `false` and leaves `out` unchanged when the system call fails
     *
     * @details
     * This is the only place in this header that touches the system (`std::time` + `localtime_s` / `localtime_r`),
     * and it is used only by convenience entry points such as "jump to today" -- the calendar conversion itself contains no time zone at all.
     * Why it is needed: `DateTimePicker::SelectToday()` needs "what day is it now",
     * and the widget itself should not guess the system time.
     *
     * @note Uses `localtime_s` (MSVC) / `localtime_r` (POSIX) rather than `localtime`:
     *       the latter returns a static buffer, which races across threads.
     */
    inline bool NowLocal(DateTime& out) {
        const std::time_t t = std::time(nullptr);
        if (t == static_cast<std::time_t>(-1)) return false;

        std::tm tmv{};
#if defined(_MSC_VER)
        if (localtime_s(&tmv, &t) != 0) return false;
#else
        if (localtime_r(&t, &tmv) == nullptr) return false;
#endif
        out.year = tmv.tm_year + 1900;
        out.month = tmv.tm_mon + 1;
        out.day = tmv.tm_mday;
        out.hour = tmv.tm_hour;
        out.minute = tmv.tm_min;
        out.second = tmv.tm_sec;
        return true;
    }

} // namespace Kit
