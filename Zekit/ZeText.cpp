
#include "pch.h"

#include "include/ZeText.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(_WIN32)
#  include <windows.h>
#endif

namespace Kit {

    namespace {

        const char* const kTextEn[] = {
            "OK",
            "Cancel",
            "Today",
            "Now",
            "Hour",
            "Min",
            "Sec",
            "Drop files here",
            "Release to import %d file(s)",
            "Supports %s",
            "Loading\u2026",
            "Scroll to zoom \u00B7 Drag to pan \u00B7 Double-click to fit \u00B7 Esc to close",
        };

        const char* const kTextZh[] = {
            "确定",
            "取消",
            "今天",
            "现在",
            "时",
            "分",
            "秒",
            "把文件拖到这里",
            "松开以放入 %d 个文件",
            "支持 %s",
            "加载中\u2026",
            "滚轮缩放 \u00B7 拖动平移 \u00B7 双击适应 \u00B7 Esc 关闭",
        };

        const char* const kTextKo[] = {
            "확인",
            "취소",
            "오늘",
            "지금",
            "시",
            "분",
            "초",
            "파일을 여기에 놓으세요",
            "놓으면 파일 %d개를 가져옵니다",
            "지원 형식: %s",
            "불러오는 중\u2026",
            "스크롤로 확대 \u00B7 드래그로 이동 \u00B7 더블클릭으로 맞춤 \u00B7 Esc로 닫기",
        };

        const char* const kTextDe[] = {
            "OK",
            "Abbrechen",
            "Heute",
            "Jetzt",
            "Std",
            "Min",
            "Sek",
            "Dateien hierher ziehen",
            "Loslassen, um %d Datei(en) zu importieren",
            "Unterst\u00FCtzt %s",
            "Wird geladen\u2026",
            "Scrollen zum Zoomen \u00B7 Ziehen zum Verschieben \u00B7 "
            "Doppelklick zum Einpassen \u00B7 Esc zum Schlie\u00DFen",
        };

        const char* const kTextRu[] = {
            "ОК",
            "Отмена",
            "Сегодня",
            "Сейчас",
            "Час",
            "Мин",
            "Сек",
            "Перетащите файлы сюда",
            "Отпустите, чтобы импортировать %d файл(ов)",
            "Поддерживается %s",
            "Загрузка\u2026",
            "Прокрутка \u2014 масштаб \u00B7 Перетаскивание \u2014 панорама \u00B7 "
            "Двойной щелчок \u2014 вписать \u00B7 Esc \u2014 закрыть",
        };

        const char* const kWeekEn[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
        const char* const kWeekZh[7] = { "日", "一", "二", "三", "四", "五", "六" };
        const char* const kWeekKo[7] = { "일", "월", "화", "수", "목", "금", "토" };
        const char* const kWeekDe[7] = { "So", "Mo", "Di", "Mi", "Do", "Fr", "Sa" };
        const char* const kWeekRu[7] = { "Вс", "Пн", "Вт", "Ср", "Чт", "Пт", "Сб" };

        const char* const kMonthEn[12] = {
            "January", "February", "March", "April", "May", "June",
            "July", "August", "September", "October", "November", "December",
        };
        const char* const kMonthZh[12] = {
            "一月", "二月", "三月", "四月", "五月", "六月",
            "七月", "八月", "九月", "十月", "十一月", "十二月",
        };
        const char* const kMonthKo[12] = {
            "1월", "2월", "3월", "4월", "5월", "6월",
            "7월", "8월", "9월", "10월", "11월", "12월",
        };
        const char* const kMonthDe[12] = {
            "Januar", "Februar", "M\u00E4rz", "April", "Mai", "Juni",
            "Juli", "August", "September", "Oktober", "November", "Dezember",
        };
        const char* const kMonthRu[12] = {
            "Январь", "Февраль", "Март", "Апрель", "Май", "Июнь",
            "Июль", "Август", "Сентябрь", "Октябрь", "Ноябрь", "Декабрь",
        };

        const DateFormat kFmtEn{ kWeekEn, kMonthEn, "%M %Y", "AM", "PM", false };
        const DateFormat kFmtZh{ kWeekZh, kMonthZh, "%Y年%m月", "上午", "下午", true };
        const DateFormat kFmtKo{ kWeekKo, kMonthKo, "%Y년 %m월", "오전", "오후", true };
        const DateFormat kFmtDe{ kWeekDe, kMonthDe, "%M %Y", "vorm.", "nachm.", true };
        const DateFormat kFmtRu{ kWeekRu, kMonthRu, "%M %Y", "ДП", "ПП", true };

        UiLanguage FromTag(const char* tag) {
            if (!tag || !*tag) return UiLanguage::English;

            char a = static_cast<char>(std::tolower(static_cast<unsigned char>(tag[0])));
            char b = (tag[1] != '\0')
                ? static_cast<char>(std::tolower(static_cast<unsigned char>(tag[1])))
                : '\0';

            if (a == 'z' && b == 'h') return UiLanguage::Chinese;
            if (a == 'k' && b == 'o') return UiLanguage::Korean;
            if (a == 'd' && b == 'e') return UiLanguage::German;
            if (a == 'r' && b == 'u') return UiLanguage::Russian;
            if (a == 'e' && b == 'n') return UiLanguage::English;
            return UiLanguage::English;
        }

        UiLanguage Detect() {
#if defined(_WIN32)
            const LANGID id = ::GetUserDefaultUILanguage();
            switch (PRIMARYLANGID(id)) {
            case LANG_CHINESE: return UiLanguage::Chinese;
            case LANG_KOREAN:  return UiLanguage::Korean;
            case LANG_GERMAN:  return UiLanguage::German;
            case LANG_RUSSIAN: return UiLanguage::Russian;
            case LANG_ENGLISH: return UiLanguage::English;
            default:           return UiLanguage::English;
            }
#else
            const char* tag = std::getenv("LC_ALL");
            if (!tag || !*tag) tag = std::getenv("LC_MESSAGES");
            if (!tag || !*tag) tag = std::getenv("LANG");
            return FromTag(tag);
#endif
        }

        struct LangPack {
            const char* const* text = nullptr;
            const DateFormat*  fmt = nullptr;
        };

        const LangPack& Pack(UiLanguage lang) {
            static const LangPack kEn{ kTextEn, &kFmtEn };
            static const LangPack kZh{ kTextZh, &kFmtZh };
            static const LangPack kKo{ kTextKo, &kFmtKo };
            static const LangPack kDe{ kTextDe, &kFmtDe };
            static const LangPack kRu{ kTextRu, &kFmtRu };

            switch (lang) {
            case UiLanguage::Chinese: return kZh;
            case UiLanguage::Korean:  return kKo;
            case UiLanguage::German:  return kDe;
            case UiLanguage::Russian: return kRu;
            case UiLanguage::Auto:    return Pack(Detect());
            case UiLanguage::English:
            default:                  return kEn;
            }
        }

        UiLanguage& Setting() {
            static UiLanguage lang = UiLanguage::Auto;
            return lang;
        }

        UiLanguage& DetectedCache() {
            static UiLanguage lang = UiLanguage::Auto;
            return lang;
        }

        UiLanguage Resolved() {
            if (Setting() != UiLanguage::Auto) return Setting();
            if (DetectedCache() == UiLanguage::Auto) DetectedCache() = Detect();
            return DetectedCache();
        }

    }

    UiLanguage CurrentUiLanguage() { return Resolved(); }

    UiLanguage UiLanguageSetting() { return Setting(); }

    void SetUiLanguage(UiLanguage lang) {
        switch (lang) {
        case UiLanguage::Auto:
            Setting() = UiLanguage::Auto;
            DetectedCache() = UiLanguage::Auto;
            return;
        case UiLanguage::English:
        case UiLanguage::Chinese:
        case UiLanguage::Korean:
        case UiLanguage::German:
        case UiLanguage::Russian:
            Setting() = lang;
            return;
        default:
            Setting() = UiLanguage::English;
            return;
        }
    }

    std::string UiText(TextId id) {
        const int i = static_cast<int>(id);
        if (i < 0 || i >= static_cast<int>(TextId::Count)) return {};
        const char* s = Pack(Resolved()).text[i];
        return s ? std::string(s) : std::string();
    }

    std::string UiText(TextId id, int n) {
        const int i = static_cast<int>(id);
        if (i < 0 || i >= static_cast<int>(TextId::Count)) return {};
        const char* pattern = Pack(Resolved()).text[i];
        if (!pattern) return {};

        if (std::strstr(pattern, "%d") == nullptr) return std::string(pattern);

        char buf[256];
        std::snprintf(buf, sizeof buf, pattern, (n < 0) ? 0 : n);
        return std::string(buf);
    }

    std::string UiText(TextId id, std::string_view arg) {
        const int i = static_cast<int>(id);
        if (i < 0 || i >= static_cast<int>(TextId::Count)) return {};
        const char* pattern = Pack(Resolved()).text[i];
        if (!pattern) return {};

        if (std::strstr(pattern, "%s") == nullptr) return std::string(pattern);

        const std::string s(arg);
        char buf[512];
        std::snprintf(buf, sizeof buf, pattern, s.c_str());
        return std::string(buf);
    }

    const DateFormat& CurrentDateFormat() { return *Pack(Resolved()).fmt; }

    std::string FormatYearMonth(int year, int month) {
        const DateFormat& f = CurrentDateFormat();
        if (month < 1) month = 1;
        if (month > 12) month = 12;

        char num[16];
        std::string out;
        out.reserve(24);

        for (const char* p = f.yearMonth; p && *p; ++p) {
            if (*p != '%') { out.push_back(*p); continue; }

            const char* next = p + 1;
            switch (*next) {
            case 'Y':
                std::snprintf(num, sizeof num, "%04d", year);
                out += num;
                p = next;
                break;
            case 'm':
                std::snprintf(num, sizeof num, "%02d", month);
                out += num;
                p = next;
                break;
            case 'M':
                if (f.months && f.months[month - 1]) out += f.months[month - 1];
                else { std::snprintf(num, sizeof num, "%02d", month); out += num; }
                p = next;
                break;
            case '\0':
                out.push_back('%');
                break;
            default:
                out.push_back('%');
                out.push_back(*next);
                p = next;
                break;
            }
        }
        return out;
    }

}
