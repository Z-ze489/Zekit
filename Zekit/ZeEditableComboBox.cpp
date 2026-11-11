
#include "pch.h"

#include <algorithm>
#include <string>
#include <utility>

#include "include/ZeEditableComboBox.h"
#include "include/ZeInputBox.h"
#include "include/Zekit.h"

namespace Kit {

    namespace detail {

        class FaceEditor : public InputBox {
        public:
            using InputBox::InputBox;

            void ApplyTheme(const Theme& t) override {
                InputBox::ApplyTheme(t);
                MakeTransparent();
            }

        private:
            void MakeTransparent() {
                box_.colors = MultiStateColors{};
                box_.border = Border{};
                box_.padding = Padding{};
                box_.radiusMode = RadiusMode::None;
                box_.customRadius = 0.0f;
                box_.shadow.enabled = false;
                box_.outline.enabled = false;
                box_.opacity = 1.0f;
            }
        };

    }

    using FaceEditor = detail::FaceEditor;

    namespace {
        constexpr float kMinArrowStrip = 14.0f;

        void ForceAllTextColors(MultiStateColors& c, const Color4f& col) {
            const int n = static_cast<int>(WidgetState::Count);
            for (int i = 0; i < n; ++i) {
                ColorSet& cs = c.get(static_cast<WidgetState>(i));
                cs.text = col;
                cs.decoration = col;
            }
        }
    }

    EditableComboBox::EditableComboBox(std::string placeholder)
        : ComboBox(placeholder) {
        FaceEditor box(placeholder);
        box.SetPlaceholder(placeholder);
        box.SetPadding(0.0f, 0.0f, 0.0f, 0.0f);
        box.SetBorderWidth(0.0f);
        box.SetRadius(0.0f);
        box.textStyle() = textStyle_;

        editor_ = &this->AddWidgetUnchecked(std::move(box));

        editor_->SetOnTextChanged(SelfAction1<const std::string&>(
            [](Widget& self, const std::string& s) {
                if (auto* ecb = dynamic_cast<EditableComboBox*>(self.parent()))
                    ecb->notifyTextChanged(s);
            }));
    }

    int EditableComboBox::indexOfText(const std::string& text) const {
        const auto& list = items();
        for (std::size_t i = 0; i < list.size(); ++i)
            if (list[i] == text) return static_cast<int>(i);
        return -1;
    }

    void EditableComboBox::SetValue(const std::string& text) {
        if (!editor_) return;

        suppressTextNotify_ = true;
        editor_->SetText(text);
        suppressTextNotify_ = false;

        const int idx = indexOfText(text);
        if (idx != selectedIndex()) ComboBox::SetSelectedIndex(idx);

        RequestWindowRedraw();
    }

    std::string EditableComboBox::value() const {
        return editor_ ? editor_->Text() : std::string();
    }

    void EditableComboBox::SetPlaceholder(std::string text) {
        ComboBox::SetPlaceholder(text);
        if (editor_) editor_->SetPlaceholder(text);
        RequestWindowRedraw();
    }

    void EditableComboBox::SetEditorReadOnly(bool ro) {
        if (editor_) editor_->SetReadOnly(ro);
        RequestWindowRedraw();
    }

    bool EditableComboBox::editorHasFocus() const {
        return editor_ && editor_->hasFocus();
    }

    void EditableComboBox::SetOnTextChanged(std::function<void(const std::string&)> handler) {
        onText_ = std::move(handler);
    }

    void EditableComboBox::SetOnTextChanged(SelfAction1<const std::string&> handler) {
        onTextSelf_ = std::move(handler);
    }

    void EditableComboBox::notifyTextChanged(const std::string& text) {
        const int idx = indexOfText(text);
        if (idx != selectedIndex()) ComboBox::SetSelectedIndex(idx);

        if (!suppressTextNotify_) {
            if (onText_)     onText_(text);
            if (onTextSelf_) onTextSelf_(*this, text);
        }
        RequestWindowRedraw();
    }

    void EditableComboBox::commitSelection(int index) {
        ComboBox::commitSelection(index);

        if (editor_) {
            editor_->SetText(selectedText());
        }
        RequestWindowRedraw();
    }

    void EditableComboBox::ApplyTheme(const Theme& t) {
        ComboBox::ApplyTheme(t);
        syncEditorStyle();
    }

    void EditableComboBox::syncEditorStyle() const {
        if (!editor_) return;

        TextStyle& es = editor_->textStyle();

        const FontDesc& want = textStyle_.font;
        if (!(editor_->textStyle().font == want)) {
            editor_->SetFontDesc(want);
        }

        es.colors = textStyle_.colors;
        ForceAllTextColors(es.colors, textStyle_.colors.get(WidgetState::Normal).text);

        es.hAlign = TextHAlign::Left;
        es.vAlign = TextVAlign::Middle;
    }

    void EditableComboBox::paintContent(FrameCollector& fc) const {
        syncEditorStyle();

        if (editor_) {
            float cx = 0.0f, cy = 0.0f, cw = 0.0f, ch = 0.0f;
            contentRect(cx, cy, cw, ch);

            const float strip = gap() + std::max(arrowSize(), kMinArrowStrip) + 2.0f;
            const float ew = std::max(0.0f, cw - strip);

            const float bt = box().border.width[Border::Top];
            const float bb = box().border.width[Border::Bottom];
            const float innerY = y() + bt;
            const float innerH = std::max(0.0f, height() - bt - bb);

            if (cx != lastX_ || innerY != lastY_ || ew != lastW_ || innerH != lastH_) {
                editor_->SetGeometry(cx, innerY, ew, innerH);
                lastX_ = cx;
                lastY_ = innerY;
                lastW_ = ew;
                lastH_ = innerH;
            }
        }

        ComboBox::paintContent(fc);
    }

    bool EditableComboBox::clipChildrenRect(float& ox, float& oy,
        float& ow, float& oh) const {
        const float bl = box().border.width[Border::Left];
        const float br = box().border.width[Border::Right];
        const float bt = box().border.width[Border::Top];
        const float bb = box().border.width[Border::Bottom];

        ox = x() + bl;
        oy = y() + bt;
        ow = std::max(0.0f, width() - bl - br);
        oh = std::max(0.0f, height() - bt - bb);
        return ow > 0.0f && oh > 0.0f;
    }

}
