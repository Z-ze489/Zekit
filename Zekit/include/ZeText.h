/**
 * @file ZeText.h
 * @brief UI strings and date formats -- follows the system language; the library ships with Chinese / English / Korean / German / Russian
 *
 * @details
 * ## Why the library ships these tables instead of "letting the app translate"
 *
 * Zekit is a widget library, and some text on screen is drawn by the library itself: the "Today / Cancel / OK" of the date popup, the "Hour / Min / Sec / Now" of the time picker, the "Drop files here" of the drop zone, the bottom hint of the image preview.
 * That text is not written by the app; the app has no handle to it and cannot replace it -- it would only see English with a few Chinese words mixed in.
 *
 * So the library must ship a default set of strings. But it does not introduce a translator:
 *  - no file format (not `.qm`, not `.mo`);
 *  - no runtime loading;
 *  - no plural rule engine (Russian 1 файл / 2 файла / 5 файлов is covered by the generic `%d файл(ов)` form, which is sufficient and cannot be mistranslated);
 *  - just a few `const char*` tables, about 4 KB in total.
 *
 * ## How the language is determined
 *
 * The default is `UiLanguage::Auto` -- call nothing and it follows the system language: Windows reads `GetUserDefaultUILanguage`; Linux / Android / HarmonyOS read the first two letters of `LC_ALL` -> `LC_MESSAGES` -> `LANG`.
 * Only five are recognized: Chinese / English / Korean / German / Russian; everything else falls back to English (including a failed detection).
 * That is, a developer on an English system gets English straight away, with no sudden Chinese.
 *
 * The app can override it with one line of `SetUiLanguage()` (for example "the UI language follows my own settings"), and later hand it back to the system with `SetUiLanguage(UiLanguage::Auto)`.
 *
 * @par Why no Japanese
 * Japanese is a deliberate gap, not an omission: it is the library author's choice. To add it, the cost is one more 12-entry `const char*` table in `Zekit/ZeText.cpp` + one `LangPack` entry + one more `UiLanguage` enum value -- ten minutes of work, whoever needs it adds it.
 *
 * ## Second thing: date and time formats
 *
 * Translating only the buttons and not the format produces "the buttons say Cancel / OK but the title still says 2026 年 11 月".
 * That looks more like a half-finished product than not translating at all. So this header also provides:
 *  - `CurrentDateFormat()` -- weekday names / month names / year-month pattern / AM-PM / whether 24-hour;
 *  - `FormatYearMonth()` -- assembles "2026 年 11 月" / "November 2026" using the current language's pattern.
 *
 * @note This header is included by every .cpp that uses strings, but there is no global construction/destruction (the language is determined lazily on first access), so there is no static initialization order problem.
 * @note Not thread-safe: read and write on the UI thread only (consistent with all other state in the library). Call `SetUiLanguage()` once, uniformly, before or after creating windows; do not change it during painting.
 */

#pragma once

#include <string>
#include <string_view>

namespace Kit {

    /// @brief The UI languages built into the library
    /// @note `Auto` is the default value; it means "determine from the system language" -- it is not a string language.
    enum class UiLanguage {
        Auto = 0,   /// < Follow the system (default; failed detection / unknown language -> English)
        English,
        Chinese,
        Korean,
        German,
        Russian
    };

    /// @brief The strings the library draws itself (only these; app strings are not handled here)
    enum class TextId {
        Ok = 0,        /// < OK / OK / 확인 / OK / ОК
        Cancel,        /// < Cancel / Cancel / 취소 / Abbrechen / Отмена
        Today,         /// < Today / Today / 오늘 / Heute / Сегодня
        Now,           /// < Now / Now / 지금 / Jetzt / Сейчас
        Hour,          /// < Hour / Hour / 시 / Std / Час
        Minute,        /// < Min / Min / 분 / Min / Мин
        Second,        /// < Sec / Sec / 초 / Sek / Сек
        DropHere,      /// < Drop files here / Drop files here / …
        DropRelease,   /// < Contains `%d`: Release to import %d file(s) / Release to import %d file(s) / …
        Supports,      /// < Contains `%s`: Supports %s / Supports %s / …
        Loading,       /// < Loading… / Loading… / …
        PreviewHint,   /// < Bottom action hint of the image preview
        Count          /// < Sentinel (do not use as a string)
    };

    /// @brief The UI language actually in effect (an `Auto` setting is resolved to a concrete language before returning)
    /// @note Never returns `Auto` -- the caller gets the language currently being displayed and can compare it directly with `UiLanguage::Chinese` etc. to set menu check states. To see whether the user set follow-system, use `UiLanguageSetting()`.
    UiLanguage CurrentUiLanguage();

    /// @brief The language set by the app (may be `Auto`)
    /// @note The only difference from `CurrentUiLanguage()`: this returns the value you set, unchanged.
    UiLanguage UiLanguageSetting();

    /// @brief Overrides the UI language; passing `Auto` hands it back to system detection
    /// @note Only affects strings fetched afterwards; strings already painted (for example the result of a Label's `SetText`) are not recomputed automatically -- those are app strings.
    void SetUiLanguage(UiLanguage lang);

    /// @brief Returns a string (plain text)
    /// @note The name is `UiText`, not `Text`: `TextWidget` has a `Text()` member (returns the widget's own text), and a function named `Text` would be shadowed by that member function in every widget subclass -- writing `Text(TextId::Ok)` would report "function does not take 1 argument", with the error pointing at the call site and no hint that an inherited member is in the way. That kind of trap is not worth leaving for users.
    std::string UiText(TextId id);

    /// @brief Returns a string with a count (`%d` is substituted)
    /// @note Returns the string unchanged if it has no `%d`; `n < 0` is treated as 0.
    std::string UiText(TextId id, int n);

    /// @brief Returns a string with a string argument (`%s` is substituted)
    std::string UiText(TextId id, std::string_view arg);

    /**
     * @brief Date and time format for one language
     *
     * @details
     * These are formats, not strings: the same "2026 年 11 月" is written "November 2026" in English, "2026年11月" in Chinese, "2026년 11월" in Korean. The common industry approach is an internationalization layer with a locale database; this library deliberately stays light: one plain POD plus one formatting function -- sufficient, and dependent on no locale data.
     */
    struct DateFormat {
        /// @brief Short weekday names, 7 entries, index 0 = Sunday (same order as `tm_wday` / `DateTime::weekday`)
        const char* const* weekdays = nullptr;
        /// @brief Month names, 12 entries, index 0 = January (used only for `%M`; numeric months use `%m`)
        const char* const* months = nullptr;
        /// @brief Year-month pattern, supports three tokens: `%Y` four-digit year, `%m` two-digit month, `%M` month name
        /// @note Chinese is `"%Y年%m月"`, English / German / Russian is `"%M %Y"`, Korean is `"%Y년 %m월"`
        const char* yearMonth = "%M %Y";
        /// @brief AM / PM (used for 12-hour display)
        const char* am = "AM";
        const char* pm = "PM";
        /// @brief Whether the language defaults to 24-hour time (used as the initial value when constructing `TimePicker`)
        bool use24Hour = true;
    };

    /// @brief Date format of the current language
    const DateFormat& CurrentDateFormat();

    /**
     * @brief Assembles "year month" using the current language's pattern
     * @param year  Four-digit year (2026)
     * @param month 1~12
     * @return For example `2026年11月` / `November 2026` / `2026년 11월`
     * @note Out-of-range months are clamped to 1~12 (better to show January than to produce `2026年13月`).
     */
    std::string FormatYearMonth(int year, int month);

} // namespace Kit
