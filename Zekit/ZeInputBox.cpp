#include "pch.h"

#include <algorithm>

#include "include/ZeInputBox.h"
#include "include/Zekit.h"
#include "ZeFrameCollector.h"

namespace Kit {

    InputBox::InputBox(std::string placeholder)
        : TextEditBase(std::move(placeholder)) {
        SetDefaultSize(200.0f, 34.0f);
    }

    void InputBox::buildLayout(FrameCollector& fc, EditLayout& out) const {
        EditLine ln;
        ln.begin = 0;
        ln.end = text_.size();
        ln.hard = true;
        const std::string_view shown = text_.empty()
            ? std::string_view(Placeholder())
            : std::string_view(text_);
        ln.width = fc.MeasureText(shown, textStyle_.font).width;
        out.lines.push_back(ln);
    }

    float InputBox::hintWidth() const {
        if (prefWidth() > 0.0f) return prefWidth();

        Window* win = window();
        if (!win) return Widget::hintWidth();

        constexpr float kCaretW = 1.5f;
        const std::string_view shown = text_.empty() ? std::string_view(Placeholder())
                                                     : std::string_view(text_);
        const float textW = win->MeasureText(shown, textStyle_.font).width;

        return textW + kCaretW
             + box_.padding.left + box_.padding.right
             + box_.border.width[1] + box_.border.width[3];
    }

    float InputBox::hintHeight() const {
        if (prefHeight() > 0.0f) return prefHeight();

        Window* win = window();
        if (!win) return Widget::hintHeight();

        const TextMetrics tm = win->MeasureText(
            text_.empty() ? std::string_view(Placeholder()) : std::string_view("Ag"),
            textStyle_.font);

        return tm.height
             + box_.padding.top + box_.padding.bottom
             + box_.border.width[0] + box_.border.width[2];
    }

}
