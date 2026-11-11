// ===========================================================================
//  Demo.cpp — Zekit control gallery
// ===========================================================================
//  Layout mirrors the design sheet:
//    +-----------------------------------------------------------------+
//    | MenuBar   brand | File View Controls Help | right icon          |
//    | ToolBar   icon buttons | title | spacer | theme lang | pills     |
//    | Body      [ Sidebar 216px | TabView -> ScrollView -> card grid ] |
//    | StatusBar renderer / GPU / DPI ........ demo note, version      |
//    +-----------------------------------------------------------------+
//
//  Two conventions this file follows:
//    1. Layout is expressed with containers only (VBox / HBox / Grid / Flow /
//       ScrollView). There is not a single SetGeometry() call.
//    2. Callbacks capture child pointers by value (their address is stable
//       inside the tree), never `this` — the Demo instance is moved into the
//       window by AddWidget, so the stack copy is a hollow shell.
// ===========================================================================

#include "Demo.h"

#include <Zekit.h>
#include <ZeTheme.h>
#include <ZeText.h>          // Kit::SetUiLanguage (follows the demo's language button)

#include <ZeVBoxLayout.h>
#include <ZeHBoxLayout.h>
#include <ZeFlowLayout.h>
#include <ZeGridLayout.h>
#include <ZeScrollView.h>
#include <ZeSplitter.h>
#include <ZeDockPanel.h>

#include <ZeMenuBar.h>
#include <ZeToolBar.h>
#include <ZeStatusBar.h>
#include <ZeSidebar.h>
#include <ZeTabView.h>

#include <ZeCard.h>
#include <ZeLabel.h>
#include <ZeButton.h>
#include <ZeCheckbox.h>
#include <ZeRadioButton.h>
#include <ZeSwitch.h>
#include <ZeSlider.h>
#include <ZeProgressBar.h>
#include <ZeRating.h>
#include <ZeBadge.h>
#include <ZeAvatar.h>
#include <ZeIcon.h>
#include <ZeImage.h>
#include <ZeDivider.h>
#include <ZeLoading.h>
#include <ZeGroupPanel.h>
#include <ZeWatermark.h>

#include <ZeInputBox.h>
#include <ZeTextArea.h>
#include <ZeCodeEditor.h>
#include <ZeRichText.h>

#include <ZeComboBox.h>
#include <ZeEditableComboBox.h>
#include <ZeNumberBox.h>
#include <ZeDateTime.h>
#include <ZeDateTimePicker.h>
#include <ZeTimePicker.h>
#include <ZeColorPicker.h>

#include <ZeCollapse.h>
#include <ZeListView.h>
#include <ZeTreeView.h>
#include <ZeTableView.h>
#include <ZeBreadcrumb.h>
#include <ZePagination.h>
#include <ZeSteps.h>

#include <ZeToast.h>
#include <ZeToastNotifier.h>
#include <ZeModalDialog.h>
#include <ZeDrawer.h>
#include <ZePlotView.h>
#include <ZeRenderBackend.h>   // Rhi::Api / ApiName / DeviceInfo

#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace {

    using Kit::Color4f;

    // -----------------------------------------------------------------------
    //  UI language
    // -----------------------------------------------------------------------
    //  English is the default: this demo is the storefront of an open-source
    //  library, so a first-time visitor should not have to find a language
    //  switch before reading anything.
    //
    //  Every visible string is written as Tr("English", "Chinese") at its use
    //  site and registered in g_loc. Switching language walks that registry and
    //  re-sets the strings in place, so no widget is created or destroyed —
    //  which also makes it safe to call from inside a click callback.

    enum class Lang { En, Zh };
    Lang g_lang = Lang::En;

    const char* Tr(const char* en, const char* zh) {
        return g_lang == Lang::Zh ? zh : en;
    }

    enum class LocKind {
        Text,          // TextWidget::SetText (Label / Button / CheckBox / ...)
        Placeholder,   // TextEditBase / ComboBox placeholder
    };

    struct LocEntry {
        Kit::Widget* target;
        const char*  en;
        const char*  zh;
        LocKind      kind;
    };

    std::vector<LocEntry> g_loc;

    // Re-apply one string. Everything that purely holds text is a TextWidget;
    // the three branches below are the only non-TextWidget text holders used
    // by this demo.
    void Retext(const LocEntry& e) {
        const std::string s = Tr(e.en, e.zh);

        if (e.kind == LocKind::Placeholder) {
            if (auto* p = dynamic_cast<Kit::TextEditBase*>(e.target)) { p->SetPlaceholder(s); return; }
            if (auto* c = dynamic_cast<Kit::ComboBox*>(e.target))     { c->SetPlaceholder(s); return; }
            return;
        }
        if (auto* t = dynamic_cast<Kit::TextWidget*>(e.target)) { t->SetText(s); return; }
        if (auto* g = dynamic_cast<Kit::GroupPanel*>(e.target)) { g->SetTitle(s); return; }
        if (auto* w = dynamic_cast<Kit::Watermark*>(e.target))  { w->SetText(s); return; }
    }

    template <class W>
    W& Loc(W& w, const char* en, const char* zh, LocKind kind = LocKind::Text) {
        g_loc.push_back(LocEntry{ &w, en, zh, kind });
        return w;
    }

    // Composite widgets (tab titles, steps, breadcrumb, rich text, table rows)
    // do not expose a single SetText, so they register a callback that rebuilds
    // their own content whenever the language changes.
    std::vector<std::function<void()>> g_hooks;

    void OnLang(std::function<void()> fn) { g_hooks.push_back(std::move(fn)); }

    // -----------------------------------------------------------------------
    //  Pages
    // -----------------------------------------------------------------------
    //  One list, used by three places: the tab view, the sidebar and the
    //  "Controls" menu. Adding a page means touching this array only.

    constexpr int kPageCount = 7;

    const char* const kPageEn[kPageCount] = {
        "Basic", "Text", "Pickers", "Containers", "Overlays", "Charts", "Window" };
    const char* const kPageZh[kPageCount] = {
        "基础视觉", "文本与编辑", "选择器", "容器与导航", "浮层与对话", "图表与布局", "窗口" };

    // -----------------------------------------------------------------------
    //  Fixed colours
    // -----------------------------------------------------------------------
    //  Only for things that must not follow the theme: the colour swatches in
    //  the ColorPicker card are sample data, not UI chrome.

    constexpr Color4f kSwatchPrimary = Color4f::FromHex(0x3B6EF6);
    constexpr Color4f kSwatchSuccess = Color4f::FromHex(0x2F9E5E);
    constexpr Color4f kSwatchDanger  = Color4f::FromHex(0xE5484D);
    constexpr Color4f kSwatchGold    = Color4f::FromHex(0xF0B429);
    constexpr Color4f kOnSwatch      = Color4f(1.0f, 1.0f, 1.0f, 1.0f);

    // Linear blend: k = 0 gives a, k = 1 gives b.
    inline Color4f Mix(const Color4f& a, const Color4f& b, float k) {
        return Color4f(a.r + (b.r - a.r) * k, a.g + (b.g - a.g) * k,
            a.b + (b.b - a.b) * k, a.a + (b.a - a.a) * k);
    }

    // -----------------------------------------------------------------------
    //  Theme-aware widgets
    // -----------------------------------------------------------------------
    //  BoxWidget::ApplyTheme is a whole-struct overwrite (box_ = themeBox(t));
    //  it does not consult the "user styled this" flag. TextWidget behaves the
    //  same for textStyle_. So a colour set by hand is lost on SetTheme, and
    //  the only way to keep a semantic colour across theme switches is to
    //  override ApplyTheme and recompute it from the current tokens.
    //
    //  That is also why switching theme is a single recursive pass: the window
    //  pushes the new theme down the tree and each subclass recolours itself.
    //
    //  Overrides stay public — Widget::ApplyTheme is a public virtual, and
    //  hiding it under protected breaks any caller that invokes it directly.

    // Semantic role of a piece of text.
    enum class Ink { Text, Muted, Faint, Primary };

    class RoleLabel : public Kit::Label {
    public:
        RoleLabel(std::string text, Ink role, unsigned px, bool bold = false)
            : Kit::Label(std::move(text)), role_(role), px_(px), bold_(bold) {}

        void ApplyTheme(const Kit::Theme& t) override {
            Kit::Label::ApplyTheme(t);
            SetFontSize(px_);
            SetBold(bold_);
            SetTextColor(inkOf(t));
        }

    private:
        Color4f inkOf(const Kit::Theme& t) const {
            switch (role_) {
            case Ink::Muted:   return t.muted;
            case Ink::Faint:   return t.disabled;
            case Ink::Primary: return t.primary;
            default:           return t.text;
            }
        }
        Ink      role_;
        unsigned px_;
        bool     bold_;
    };

    // Four button semantics.
    enum class BtnTone { Primary, Normal, Ghost, Danger };

    class ToneButton : public Kit::Button {
    public:
        ToneButton(std::string text, BtnTone tone, float w, float h)
            : Kit::Button(std::move(text)), tone_(tone) {
            SetSize(w, h);
        }

        void ApplyTheme(const Kit::Theme& t) override {
            Kit::Button::ApplyTheme(t);   // theme lays down the base first
            SetRadius(6.0f);
            SetBorderWidth(1.0f);
            const Color4f hover = Mix(t.surface, t.border, 0.55f);
            switch (tone_) {
            case BtnTone::Primary:
                SetBodyColor(t.primary);
                SetHoverBodyColor(t.primaryHover);
                SetPressedBodyColor(t.primaryPress);
                SetBorderColor(t.primary);
                SetTextColor(t.onPrimary);
                SetHoverTextColor(t.onPrimary);
                SetPressedTextColor(t.onPrimary);
                break;
            case BtnTone::Danger:
                SetBodyColor(t.surface);
                SetHoverBodyColor(Mix(t.surface, t.danger, 0.10f));
                SetBorderColor(t.danger);
                SetTextColor(t.danger);
                SetHoverTextColor(t.danger);
                break;
            case BtnTone::Ghost:
                SetBodyColor(Color4f(0.0f, 0.0f, 0.0f, 0.0f));
                SetHoverBodyColor(hover);
                SetBorderWidth(0.0f);
                SetTextColor(t.muted);
                SetHoverTextColor(t.text);
                break;
            default:
                SetBodyColor(t.surface);
                SetHoverBodyColor(hover);
                SetBorderColor(t.border);
                SetTextColor(t.text);
                SetHoverTextColor(t.primary);
                break;
            }
        }

    private:
        BtnTone tone_;
    };

    // Small filled block used by the layout illustrations.
    enum class TileTone { Sunken, Soft, WeakPrimary };

    class ToneTile : public Kit::Panel {
    public:
        // Literal-only text (numbers, codes) — not localized.
        ToneTile(std::string text, TileTone tone)
            : tone_(tone) {
            Build(std::move(text));
        }

        // Localized text: the inner label is registered so a language switch
        // updates it in place.
        ToneTile(const char* en, const char* zh, TileTone tone)
            : tone_(tone) {
            RoleLabel& l = Build(Tr(en, zh));
            Loc(l, en, zh);
        }

        void ApplyTheme(const Kit::Theme& t) override {
            Kit::Panel::ApplyTheme(t);
            SetRadius(6.0f);
            switch (tone_) {
            case TileTone::WeakPrimary: SetBackground(Mix(t.surface, t.primary, 0.12f)); break;
            case TileTone::Soft:        SetBackground(Mix(t.surface, t.border, 0.50f)); break;
            default:                    SetBackground(Mix(t.surface, t.border, 0.35f)); break;
            }
        }

    private:
        RoleLabel& Build(std::string text) {
            RoleLabel& l = AddChild(RoleLabel(std::move(text),
                tone_ == TileTone::WeakPrimary ? Ink::Primary : Ink::Muted, 11));
            l.SetTextAlign(Kit::TextHAlign::Center, Kit::TextVAlign::Middle);
            l.SetFlexGrow(1.0f);
            return l;
        }

        TileTone tone_;
    };

    // Icon that follows the theme and reports a click.
    enum class IconTone { Muted, Primary };

    class ToneIcon : public Kit::Icon {
    public:
        ToneIcon(Kit::IconGlyph g, float side, float glyphPx, IconTone tone,
            std::function<void(Kit::Widget&)> onClick = {})
            : Kit::Icon(g), side_(side), glyph_(glyphPx), tone_(tone),
            onClick_(std::move(onClick)) {
            SetSize(side, side);
        }

        void ApplyTheme(const Kit::Theme& t) override {
            Kit::Icon::ApplyTheme(t);
            SetIconSize(glyph_);
            SetColor(tone_ == IconTone::Primary ? t.primary : t.muted);
            SetHoverBodyColor(Mix(t.surface, t.border, 0.55f));
            SetRadius(6.0f);
        }

        bool OnMousePress(Kit::MouseEvent& e) override {
            if (e.button == Kit::MouseButton::Left && onClick_) {
                onClick_(*this);
                e.accept();
                return true;
            }
            return Kit::Icon::OnMousePress(e);
        }

    private:
        float side_;
        float glyph_;
        IconTone tone_;
        std::function<void(Kit::Widget&)> onClick_;
    };

    // Image placeholder: the empty frame follows the theme.
    class ToneImage : public Kit::Image {
    public:
        ToneImage() = default;
        explicit ToneImage(std::string path) : Kit::Image(std::move(path)) {}

        void ApplyTheme(const Kit::Theme& t) override {
            Kit::Image::ApplyTheme(t);
            SetPlaceholderColor(Mix(t.surface, t.border, 0.45f));
            SetCornerRadius(6.0f);
        }
    };

    enum class AvatarTone { Primary, Success, Warning, Danger };

    class ToneAvatar : public Kit::Avatar {
    public:
        ToneAvatar(std::string name, AvatarTone tone)
            : Kit::Avatar(std::move(name)), tone_(tone) {}

        void ApplyTheme(const Kit::Theme& t) override {
            Kit::Avatar::ApplyTheme(t);
            switch (tone_) {
            case AvatarTone::Success: SetAvatarColor(t.success); break;
            case AvatarTone::Warning: SetAvatarColor(t.warning); break;
            case AvatarTone::Danger:  SetAvatarColor(t.danger); break;
            default:                  SetAvatarColor(t.primary); break;
            }
        }

    private:
        AvatarTone tone_;
    };

    class LineDivider : public Kit::Divider {
    public:
        explicit LineDivider(Kit::DividerOrientation o = Kit::DividerOrientation::Horizontal)
            : Kit::Divider(o) {}

        void ApplyTheme(const Kit::Theme& t) override {
            Kit::Divider::ApplyTheme(t);
            SetColor(t.border);
            SetGap(0.0f);
        }
    };

    class ToneWatermark : public Kit::Watermark {
    public:
        explicit ToneWatermark(std::string text) : Kit::Watermark(std::move(text)) {}

        void ApplyTheme(const Kit::Theme& t) override {
            Kit::Watermark::ApplyTheme(t);
            SetColor(t.disabled);
        }
    };

    void FillPlot(Kit::PlotView& plot, const Kit::Theme& t);

    // PlotView vertices are data, so they do not react to a theme switch by
    // themselves; this subclass re-uploads them on every ApplyTheme.
    class TonePlot : public Kit::PlotView {
    public:
        void ApplyTheme(const Kit::Theme& t) override {
            Kit::PlotView::ApplyTheme(t);
            theme_ = t;
            FillPlot(*this, t);
        }

        // The axis caption is localized, so a language switch has to re-upload
        // the vertices even though the theme did not change.
        void RefreshText() { FillPlot(*this, theme_); }

    private:
        Kit::Theme theme_{};
    };

    // -----------------------------------------------------------------------
    //  Layout helpers
    // -----------------------------------------------------------------------

    Kit::HBoxLayout& Row(Kit::LayoutBase& parent, float gap = 8.0f) {
        Kit::HBoxLayout& r = parent.AddChild(Kit::HBoxLayout());
        r.SetGap(0.0f, gap);
        r.SetAlign(Kit::AlignItems::Center);
        return r;
    }

    Kit::VBoxLayout& Col(Kit::LayoutBase& parent, float gap = 8.0f) {
        Kit::VBoxLayout& c = parent.AddChild(Kit::VBoxLayout());
        c.SetGap(gap, 0.0f);
        return c;
    }

    // Caption: small secondary text under a control.
    Kit::Label& Cap(Kit::LayoutBase& parent, const char* en, const char* zh,
        Ink ink = Ink::Muted, unsigned px = 11) {
        Kit::Label& l = parent.AddChild(RoleLabel(Tr(en, zh), ink, px));
        return Loc(l, en, zh);
    }

    Kit::Button& Btn(Kit::LayoutBase& parent, const char* en, const char* zh,
        BtnTone tone = BtnTone::Normal, float w = 0.0f, float h = 30.0f) {
        Kit::Button& b = parent.AddChild(ToneButton(Tr(en, zh), tone, w, h));
        return Loc(b, en, zh);
    }

    // Card with a hand-drawn header: title on the left, source file on the right.
    // The built-in title bar is unused because it cannot host the trailing
    // file name, and an empty title costs no vertical space.
    Kit::Card& NewCard(Kit::LayoutBase& row, const char* en, const char* zh,
        const char* meta, float minH = 0.0f) {
        Kit::Card& c = row.AddChild(Kit::Card());
        c.SetFlexGrow(1.0f);
        c.SetMinWidth(240.0f);
        if (minH > 0.0f) c.SetMinHeight(minH);
        c.SetGap(9.0f, 9.0f);

        Kit::HBoxLayout& head = c.AddChild(Kit::HBoxLayout());
        head.SetGap(0.0f, 8.0f);
        head.SetAlign(Kit::AlignItems::Center);

        Kit::Label& t = head.AddChild(RoleLabel(Tr(en, zh), Ink::Text, 13, true));
        t.SetFlexGrow(1.0f);
        Loc(t, en, zh);

        if (meta && meta[0] != '\0') head.AddChild(RoleLabel(meta, Ink::Faint, 11));

        c.AddChild(LineDivider());
        return c;
    }

    // Plain filled panel with fixed colours (colour swatches only).
    Kit::Panel MakeTile(const std::string& text, Color4f bg, Color4f fg, float radius = 6.0f) {
        Kit::Panel p;
        p.SetBackground(bg);
        p.SetRadius(radius);
        Kit::Label& l = p.AddChild(Kit::Label(text));
        l.SetTextColor(fg);
        l.SetFontSize(11);
        l.SetTextAlign(Kit::TextHAlign::Center, Kit::TextVAlign::Middle);
        l.SetFlexGrow(1.0f);
        return p;
    }

    // Tab content: a scroll view whose single child is the stack of card rows.
    Kit::VBoxLayout& Scrolled(Kit::Panel& page) {
        page.SetPadding(0.0f, 0.0f, 0.0f, 0.0f);
        page.SetGap(0.0f, 0.0f);
        // Do not give the scroll view (or its host) flexGrow: an elastic child
        // is excluded from the container's natural-size sum, so the deficit
        // turns hugely negative and nothing shrinks. The container ends up
        // taller than the window, maxScrollY stays 0, and the content is
        // simply clipped outside the window. Expanding on the main axis is the
        // supported way to say "take the remaining height".
        page.SetSizePolicy(Kit::SizePolicy::Preferred, Kit::SizePolicy::Expanding);

        Kit::ScrollView& sv = page.AddChild(Kit::ScrollView());
        sv.SetScrollDirection(Kit::ScrollDirection::Vertical);
        sv.SetSizePolicy(Kit::SizePolicy::Preferred, Kit::SizePolicy::Expanding);
        sv.SetPadding(0.0f, 0.0f, 0.0f, 0.0f);

        Kit::VBoxLayout& box = sv.AddChild(Kit::VBoxLayout());
        box.SetPadding(16.0f, 14.0f, 16.0f, 28.0f);
        box.SetGap(14.0f, 0.0f);
        return box;
    }

    // Window-level toast overlay, created on first use.
    void PopToast(Kit::Widget& self, const std::string& text, Kit::ToastLevel level) {
        Kit::Window* w = self.window();
        if (!w) return;
        static Kit::Toast* s_toast = nullptr;
        if (!s_toast) {
            s_toast = &w->AddWidget(Kit::Toast());
            s_toast->SetPlace(Kit::ToastPlace::TopRight);
            s_toast->SetMaxWidth(340.0f);
        }
        s_toast->Show(text, level, 2400);
    }

    // -----------------------------------------------------------------------
    //  Live feedback
    // -----------------------------------------------------------------------
    //  Every control in this demo now answers back. There are two sinks:
    //
    //    · the "readout" line at the bottom of a card — one short line of data
    //      ("42", "Vulkan", "checked: A, C") that changes as you interact;
    //    · the activity slot in the status bar — the last thing that happened,
    //      written by whichever control the user just touched.
    //
    //  Readouts deliberately hold DATA, not prose, so they are kept out of the
    //  localization registry: a translated "42" is still "42", and re-setting
    //  every readout on a language switch would wipe the last result.

    // A card's result line: small, muted, single line, right under the controls.
    // Registered with the localization registry so a language switch puts it
    // back to the neutral hint instead of leaving a stale English string.
    Kit::Label& Readout(Kit::Card& card) {
        Kit::Label& l = card.AddChild(RoleLabel(Tr("no action yet", "尚未操作"), Ink::Faint, 11));
        l.SetSize(0.0f, 16.0f);
        return Loc(l, "no action yet", "尚未操作");
    }

    void SetReadout(Kit::Label& l, const std::string& text) {
        l.SetText(text);
        // The container cached the old text width; without this the new, longer
        // string keeps the previous box and gets ellipsized.
        if (auto* p = dynamic_cast<Kit::LayoutBase*>(l.parent())) p->Invalidate();
        l.RequestWindowRedraw();
    }

    // Last-action slot in the status bar (section 3 of BuildStatusBar).
    // Looked up by object name: the Demo instance is moved into the window by
    // AddWidget, so callbacks must never capture `this`.
    void Activity(Kit::Widget& self, const std::string& text) {
        Kit::Window* w = self.window();
        if (!w) return;
        if (auto* sb = w->FindWidgetDeep<Kit::StatusBar>("statusbar"))
            sb->SetSectionText(3, text);
        std::printf("[demo] %s\n", text.c_str());
    }

    // A control was touched: write both sinks in one call.
    void Report(Kit::Widget& self, Kit::Label& readout, const std::string& text) {
        SetReadout(readout, text);
        Activity(self, text);
    }

    std::string Num(float v, int decimals) {
        char buf[64];
        if (decimals > 0) std::snprintf(buf, sizeof buf, "%.*f", decimals, static_cast<double>(v));
        else              std::snprintf(buf, sizeof buf, "%.0f", static_cast<double>(v));
        return buf;
    }

    std::string Hex(const Color4f& c) {
        char buf[16];
        std::snprintf(buf, sizeof buf, "#%02X%02X%02X",
            static_cast<int>(c.r * 255.0f + 0.5f),
            static_cast<int>(c.g * 255.0f + 0.5f),
            static_cast<int>(c.b * 255.0f + 0.5f));
        return buf;
    }

    // "on" / "off" as a word, localized at call time.
    const char* OnOff(bool on) { return on ? Tr("on", "开") : Tr("off", "关"); }

    // -----------------------------------------------------------------------
    //  Child windows (shared by the File menu and the Window page)
    // -----------------------------------------------------------------------
    //  CreateChildWindow is library-managed: the host owns and destroys them,
    //  so nothing here keeps a Window* around (a dangling one after Close()
    //  would be the obvious way to crash on exit).

    int g_childSeq = 0;

    // Live children, so "close them all" has something to walk. The host owns
    // them; this list is only a handle table and is cleared after destroying.
    std::vector<Kit::Window*>& Children() {
        static std::vector<Kit::Window*> v;
        return v;
    }

    int ChildCount(Kit::Window* w) { return w ? w->childWindowCount() : 0; }

    void CreateDemoChildWindow(Kit::Widget& self) {
        Kit::Window* w = self.window();
        if (!w) return;
        if (ChildCount(w) >= 4) {
            PopToast(self, Tr("Four child windows is enough for a demo",
                "演示用四个子窗口就够了"), Kit::ToastLevel::Warning);
            return;
        }
        ++g_childSeq;
        char title[64];
        std::snprintf(title, sizeof title, "%s #%d", Tr("Child window", "子窗口"), g_childSeq);

        // Owned = a normal window: system title bar, icon, min/max/close, and
        // it stacks above the host (owner semantics).
        const int dx = 60 + 34 * (g_childSeq % 4);
        const int dy = 80 + 28 * (g_childSeq % 4);
        Kit::Window& kid = w->CreateChildWindow(dx, dy, 380, 240, Kit::ChildKind::Owned);
        if (kid.IsClosed()) {
            PopToast(self, Tr("This platform refused a native child window",
                "该平台没能创建原生子窗口"), Kit::ToastLevel::Error);
            return;
        }
        kid.SetTitle(title);
        Kit::Panel& body = kid.AddWidget(Kit::Panel(Kit::FlexDirection::Column));
        body.SetPadding(16.0f, 14.0f, 16.0f, 14.0f);
        body.SetGap(8.0f, 0.0f);
        body.AddChild(RoleLabel(Tr("A real window - one exec() drives them all.",
            "这是一个真窗口 —— 一个 exec() 同时驱动所有窗口。"), Ink::Text, 12));
        body.AddChild(RoleLabel(Tr("Shares the renderer context with the host.",
            "与宿主共享渲染上下文。"), Ink::Muted, 11));
        kid.Show();
        Children().push_back(&kid);

        char buf[160];
        std::snprintf(buf, sizeof buf, "%s (%s %d)",
            Tr("child window created", "已创建子窗口"),
            Tr("total", "共"), ChildCount(w));
        Activity(self, buf);
    }

    void CloseDemoChildWindows(Kit::Widget& self) {
        Kit::Window* w = self.window();
        if (!w) return;
        auto& kids = Children();
        int closed = 0;
        for (Kit::Window* k : kids) {
            if (!k || k->IsClosed()) continue;
            w->DestroyChildWindow(*k);          // the host frees it
            ++closed;
        }
        kids.clear();
        char buf[160];
        std::snprintf(buf, sizeof buf, "%s %d (%s %d)",
            Tr("closed", "已关闭"), closed,
            Tr("left", "剩余"), ChildCount(w));
        Activity(self, buf);
    }

    // -----------------------------------------------------------------------
    //  Cross-page actions
    // -----------------------------------------------------------------------
    //  Callbacks resolve their target by object name: this panel is moved into
    //  the window by AddWidget, so no callback may capture the Demo instance.

    void GotoPage(Kit::Widget& self, int index) {
        Kit::Window* w = self.window();
        if (!w) return;
        if (auto* tv = w->FindWidgetDeep<Kit::TabView>("tabs")) tv->SetCurrentIndex(index);
        if (index >= 0 && index < kPageCount)
            Activity(self, std::string(Tr("page: ", "页面：")) + Tr(kPageEn[index], kPageZh[index]));
    }

    void ToggleSidebar(Kit::Widget& self) {
        Kit::Window* w = self.window();
        if (!w) return;
        if (auto* side = w->FindWidgetDeep<Kit::Sidebar>("sidebar")) {
            side->SetVisible(!side->isVisible());
            Activity(self, side->isVisible() ? Tr("sidebar: shown", "侧边栏：显示")
                                             : Tr("sidebar: hidden", "侧边栏：隐藏"));
        }
    }

    void ToggleDemoTheme(Kit::Widget& self) {
        Kit::Window* w = self.window();
        if (!w) return;
        if (Demo* d = w->FindWidget<Demo>("demo")) d->ToggleTheme();
    }

    void ToggleDemoLanguage(Kit::Widget& self) {
        Kit::Window* w = self.window();
        if (!w) return;
        if (Demo* d = w->FindWidget<Demo>("demo")) d->ToggleLanguage();
    }

    // -----------------------------------------------------------------------
    //  Menu content
    // -----------------------------------------------------------------------
    //  Built twice — at startup and on every language switch — from one place,
    //  so the two copies cannot drift apart. Everything here does something:
    //  navigation, theme, language, or a real window operation.

    Kit::MenuItem ShowcaseItem(const char* en, const char* zh, const char* shortcut = nullptr);

    std::vector<Kit::MenuItem> FileMenuItems() {
        return {
            { .text = Tr("New Child Window", "新建子窗口"), .shortcut = "Ctrl+N",
              .action = [](Kit::Widget& self) { CreateDemoChildWindow(self); } },
            { .separator = true },
            { .text = Tr("Quit", "退出"), .shortcut = "Alt+F4",
              .action = [](Kit::Widget& self) { self.CloseWindow(); } },
        };
    }

    std::vector<Kit::MenuItem> ViewMenuItems() {
        // No special handling here: the menu path calls exactly the same
        // ToggleTheme() the toolbar button does. The one-frame lag this used to
        // show (canvas flipped, bars did not, until the window was focused) was
        // fixed at the source — Window::SetTheme now marks the whole tree dirty
        // instead of relying on the paint-cache epoch alone.
        return {
            { .text = Tr("Dark / Light Theme", "切换深色 / 浅色主题"), .shortcut = "Ctrl+D",
              .action = [](Kit::Widget& self) { ToggleDemoTheme(self); } },
            { .text = Tr("UI Language: English / 中文", "界面语言：English / 中文"),
              .shortcut = "Ctrl+L",
              .action = [](Kit::Widget& self) { ToggleDemoLanguage(self); } },
            { .separator = true },
            { .text = Tr("Show / Hide Sidebar", "显示 / 隐藏侧边栏"), .shortcut = "Ctrl+B",
              .action = [](Kit::Widget& self) { ToggleSidebar(self); } },
        };
    }

    std::vector<Kit::MenuItem> ControlsMenuItems() {
        std::vector<Kit::MenuItem> items;
        for (int i = 0; i < kPageCount; ++i) {
            Kit::MenuItem m;
            m.text = Tr(kPageEn[i], kPageZh[i]);
            m.action = [i](Kit::Widget& self) { GotoPage(self, i); };
            items.push_back(std::move(m));
        }
        return items;
    }

    std::vector<Kit::MenuItem> HelpMenuItems() {
        return {
            ShowcaseItem("Source Map", "源码地图"),
            ShowcaseItem("Third-party Licenses", "第三方许可证"),
            { .separator = true },
            { .text = Tr("About Zekit", "关于 Zekit"), .action = [](Kit::Widget& self) {
                PopToast(self, "Zekit 1.0.0 \u00B7 C++20 \u00B7 control gallery",
                    Kit::ToastLevel::Info);
                Activity(self, "Zekit 1.0.0 \u00B7 C++20");
            } },
        };
    }

    // Menu item that only demonstrates itself: clicking explains that the demo
    // is display-only instead of silently doing nothing.
    Kit::MenuItem ShowcaseItem(const char* en, const char* zh, const char* shortcut) {
        Kit::MenuItem m;
        m.text = Tr(en, zh);
        if (shortcut) m.shortcut = shortcut;
        const std::string labelEn = en;
        const std::string labelZh = zh;
        m.action = [labelEn, labelZh](Kit::Widget& self) {
            const std::string name = Tr(labelEn.c_str(), labelZh.c_str());
            const std::string tail = (g_lang == Lang::Zh)
                ? "\u201D \u00B7 \u5C55\u793A\u9879\uFF0C\u65E0\u5B9E\u9645\u529F\u80FD"
                : "\u201D \u00B7 display only, no behaviour";
            PopToast(self, "\u201C" + name + tail, Kit::ToastLevel::Info);
        };
        return m;
    }

    // -----------------------------------------------------------------------
    //  Plot data
    // -----------------------------------------------------------------------
    //  PlotView only accepts triangles, so the sample polyline is expanded into
    //  quads here. Colours come from the theme tokens.

    void PushQuad(std::vector<Kit::PlotVertex>& v,
        float ax, float ay, float bx, float by,
        float cx, float cy, float dx, float dy, Color4f c) {
        const auto push = [&](float x, float y) {
            v.push_back(Kit::PlotVertex{ x, y, c.r, c.g, c.b, c.a });
        };
        push(ax, ay); push(bx, by); push(cx, cy);
        push(ax, ay); push(cx, cy); push(dx, dy);
    }

    std::vector<Kit::PlotVertex> BuildPlotVertices(float w, float h, const Kit::Theme& th) {
        std::vector<Kit::PlotVertex> v;
        const float padL = 30.0f, padR = 10.0f, padT = 14.0f, padB = 22.0f;
        const float innerW = w - padL - padR;
        const float innerH = h - padT - padB;

        // Grid lines.
        for (int i = 0; i < 4; ++i) {
            const float y = padT + innerH * (static_cast<float>(i) / 3.0f);
            PushQuad(v, padL, y, w - padR, y, w - padR, y + 1.0f, padL, y + 1.0f,
                Mix(th.surface, th.border, 0.85f));
        }

        // Sample series.
        const float ys[] = { 110.0f, 88.0f, 96.0f, 62.0f, 70.0f, 40.0f, 52.0f, 26.0f };
        const int n = 8;
        const float step = innerW / static_cast<float>(n - 1);
        auto px = [&](int i) { return padL + step * static_cast<float>(i); };
        auto py = [&](int i) { return padT + innerH * (ys[i] / 130.0f); };

        // Area fill.
        for (int i = 0; i + 1 < n; ++i) {
            PushQuad(v, px(i), py(i), px(i + 1), py(i + 1),
                px(i + 1), padT + innerH, px(i), padT + innerH,
                Color4f(th.primary.r, th.primary.g, th.primary.b, 0.10f));
        }

        // Polyline: one 2px quad per segment.
        for (int i = 0; i + 1 < n; ++i) {
            const float x0 = px(i), y0 = py(i), x1 = px(i + 1), y1 = py(i + 1);
            float dx = x1 - x0, dy = y1 - y0;
            const float len = std::sqrt(dx * dx + dy * dy);
            if (len < 0.001f) continue;
            dx /= len; dy /= len;
            const float nx = -dy * 1.1f, ny = dx * 1.1f;
            PushQuad(v, x0 + nx, y0 + ny, x1 + nx, y1 + ny,
                x1 - nx, y1 - ny, x0 - nx, y0 - ny, th.primary);
        }

        // Data point markers.
        for (int i = 0; i < n; ++i) {
            const float x = px(i), y = py(i);
            PushQuad(v, x - 2.4f, y - 2.4f, x + 2.4f, y - 2.4f,
                x + 2.4f, y + 2.4f, x - 2.4f, y + 2.4f, th.surface);
        }
        return v;
    }

    std::vector<Kit::PlotText> BuildPlotTexts(float w, float h, const Kit::Theme& th) {
        std::vector<Kit::PlotText> t;
        const float padT = 14.0f, padB = 22.0f;
        const float innerH = h - padT - padB;
        const char* labels[] = { "100", "75", "50", "25" };
        for (int i = 0; i < 4; ++i) {
            Kit::PlotText p;
            p.text = labels[i];
            p.x = 26.0f;
            p.y = padT + innerH * (static_cast<float>(i) / 3.0f) - 7.0f;
            p.pixelSize = 10;
            p.align = Kit::PlotTextAlign::Right;
            p.color = th.disabled;
            t.push_back(p);
        }
        Kit::PlotText cap;
        cap.text = Tr("widget count", "\u63A7\u4EF6\u6570\u91CF\u589E\u957F");
        cap.x = 30.0f + (w - 40.0f) * 0.5f;
        cap.y = h - 16.0f;
        cap.pixelSize = 10;
        cap.align = Kit::PlotTextAlign::Center;
        cap.color = th.disabled;
        t.push_back(cap);
        return t;
    }

    // Rich text runs are content, not a single string: rebuild them per language.
    void FillRichText(Kit::RichText& rt) {
        if (g_lang == Lang::Zh) {
            rt.SetRuns({
                { "一个控件里排 ", Kit::RichFlag::None, "" },
                { "多段不同样式", Kit::RichFlag::Bold, "" },
                { " 的行内文字，支持 ", Kit::RichFlag::None, "" },
                { "行内代码", Kit::RichFlag::Code, "" },
                { "、", Kit::RichFlag::None, "" },
                { "下划线", Kit::RichFlag::Underline, "" },
                { "、", Kit::RichFlag::None, "" },
                { "删除线", Kit::RichFlag::Strike, "" },
                { "，以及可点击的 ", Kit::RichFlag::None, "" },
                { "链接", Kit::RichFlag::Link, "https://example.com" },
                { "。", Kit::RichFlag::None, "" },
                });
        } else {
            rt.SetRuns({
                { "Several ", Kit::RichFlag::None, "" },
                { "different styles", Kit::RichFlag::Bold, "" },
                { " in one control, including ", Kit::RichFlag::None, "" },
                { "inline code", Kit::RichFlag::Code, "" },
                { ", ", Kit::RichFlag::None, "" },
                { "underline", Kit::RichFlag::Underline, "" },
                { ", ", Kit::RichFlag::None, "" },
                { "strike-through", Kit::RichFlag::Strike, "" },
                { " and a clickable ", Kit::RichFlag::None, "" },
                { "link", Kit::RichFlag::Link, "https://example.com" },
                { ".", Kit::RichFlag::None, "" },
                });
        }
    }

    void FillPlot(Kit::PlotView& plot, const Kit::Theme& th) {
        const float w = 900.0f, h = 168.0f;
        plot.SetVertices(BuildPlotVertices(w, h, th));
        plot.SetTexts(BuildPlotTexts(w, h, th));
    }

} // namespace

// ===========================================================================
//  Theme
// ===========================================================================
//  The two palettes only touch colour tokens; every component style is derived
//  by Themes::Derive. So one token change propagates to buttons, cards, tabs,
//  tables and menus at once.

Kit::Theme Demo::MakeTheme(bool dark) {
    Kit::Theme t = dark ? Kit::Themes::Dark() : Kit::Themes::Light();

    if (dark) {
        t.bg = Kit::Color4f::FromHex(0x0E1013);
        t.surface = Kit::Color4f::FromHex(0x181B20);
        t.border = Kit::Color4f::FromHex(0x2B313A);
        t.text = Kit::Color4f::FromHex(0xE6E9EE);
        t.muted = Kit::Color4f::FromHex(0x98A2B3);
        t.disabled = Kit::Color4f::FromHex(0x6B7280);
        t.primary = Kit::Color4f::FromHex(0x5B8CFF);
        t.primaryHover = Kit::Color4f::FromHex(0x7AA2FF);
        t.primaryPress = Kit::Color4f::FromHex(0x4269D6);
        t.danger = Kit::Color4f::FromHex(0xF0666B);
        t.success = Kit::Color4f::FromHex(0x3FB96F);
        t.warning = Kit::Color4f::FromHex(0xE0A44A);
    } else {
        t.bg = Kit::Color4f::FromHex(0xEEF1F5);
        t.border = Kit::Color4f::FromHex(0xE0E5EC);
        t.text = Kit::Color4f::FromHex(0x1B1F24);
        t.muted = Kit::Color4f::FromHex(0x667085);
        t.disabled = Kit::Color4f::FromHex(0x98A2B3);
        t.primary = Kit::Color4f::FromHex(0x3B6EF6);
        t.primaryHover = Kit::Color4f::FromHex(0x2A55C8);
        t.primaryPress = Kit::Color4f::FromHex(0x1E3F9E);
        t.danger = Kit::Color4f::FromHex(0xE5484D);
        t.success = Kit::Color4f::FromHex(0x2F9E5E);
        t.warning = Kit::Color4f::FromHex(0xD98324);
    }

    Kit::Themes::Derive(t);
    t.name = dark ? "zekit-gallery-dark" : "zekit-gallery";
    return t;
}

void Demo::ToggleTheme() {
    Kit::Window* w = window();
    if (!w) return;

    dark_ = !dark_;
    const Kit::Theme next = MakeTheme(dark_);

    // The window pushes ApplyTheme down the whole tree — including the menu
    // bar, which is a normal child of this panel.
    w->SetTheme(next);

    // Belt and braces for the window-level strips. SetTheme's tree walk is the
    // contract, but a bar that keeps its own direct children (menu titles are
    // painted by MenuBar itself, not by widgets) reads better if it is told
    // again explicitly: that is also how the dark/light mismatch the user hit
    // is guaranteed not to come back.
    if (menu_)   menu_->ApplyTheme(next);
    if (side_)   side_->ApplyTheme(next);
    if (status_) status_->ApplyTheme(next);

    // NOT SetDarkMode() here: that path recolours the *whole* palette from the
    // theme's polarity (Themes::Recolor), which would throw away the hand-tuned
    // dark tokens above. SetTheme already syncs the native title bar to the new
    // polarity inside Window::SetTheme -> applyTitleBarDarkMode().

    if (themeBtn_) themeBtn_->SetText(Tr("Dark", "深色"));
    if (auto* sb = w->FindWidgetDeep<Kit::StatusBar>("statusbar")) {
        sb->SetSectionText(3, dark_ ? Tr("theme: dark", "主题：深色")
                                    : Tr("theme: light", "主题：浅色"));
    }

    // Render the new theme **now** instead of waiting for the next loop turn.
    // SetTheme already queues a frame (frameAfterInput), but that frame is
    // consumed after the current event batch — and the window background is
    // repainted from the new tokens while the control tree is still painted
    // from this call, so a viewer can catch one frame where the canvas has
    // flipped and the bars have not. Drawing synchronously here removes the
    // window where that mismatch is observable at all.
    w->RenderNow();

    PopToast(*this, dark_ ? Tr("Switched to dark theme", "已切换到深色主题")
                          : Tr("Switched to light theme", "已切换到浅色主题"),
        Kit::ToastLevel::Success);
}

void Demo::ToggleLanguage() {
    g_lang = (g_lang == Lang::En) ? Lang::Zh : Lang::En;

    // Re-sync the strings the library draws itself (picker buttons, drop-zone
    // hints, image-preview hints). The library defaults to "follow the system
    // locale"; override it with the demo's current language to exercise the switch.
    Kit::SetUiLanguage((g_lang == Lang::Zh) ? Kit::UiLanguage::Chinese
                                            : Kit::UiLanguage::English);

    // Re-set every registered string in place. Nothing is rebuilt, so this is
    // safe even when called from a button that lives inside the tree.
    for (const LocEntry& e : g_loc) Retext(e);
    for (const auto& hook : g_hooks) hook();

    RelocalizeComposite();
    RefreshRuntimeInfo();   // status bar texts are localized too

    // Text width changed, so every container has to re-measure. Containers
    // cache their layout and SetText does not bubble up, so walk the tree and
    // invalidate each one — otherwise the labels keep their old width and the
    // new string is ellipsized.
    VisitTree([](Kit::Widget& w, int) {
        if (auto* lb = dynamic_cast<Kit::LayoutBase*>(&w)) lb->Invalidate();
        });
    RequestWindowRedraw();

    Kit::Window* w = window();
    if (w) w->SetTitle(Tr("Zekit Control Gallery \u00B7 1.0.0", "Zekit 控件全览 \u00B7 1.0.0"));
}

// ===========================================================================
//  Shell
// ===========================================================================

Demo::Demo() : Kit::Panel(Kit::FlexDirection::Column) {
    // Give the top-level widget a stable identity: callbacks cannot capture
    // `this` (the instance is moved into the window by AddWidget, the stack
    // copy is a hollow shell), so they look the real object up by name.
    SetObjectName("demo");

    SetPadding(0.0f, 0.0f, 0.0f, 0.0f);
    SetGap(0.0f, 0.0f);

    BuildMenuBar();
    BuildToolBar();
    BuildBody();
    BuildStatusBar();
}

void Demo::BuildMenuBar() {
    Kit::MenuBar& bar = AddChild(Kit::MenuBar());
    menu_ = &bar;
    bar.SetObjectName("menubar");
    bar.SetBarHeight(38.0f);

    // ---- Leading zone: brand ----
    Kit::Icon& logo = bar.AddLeading(ToneIcon(Kit::IconGlyph::Check, 20.0f, 13.0f,
        IconTone::Primary));
    logo.SetTooltip("Zekit");

    Kit::Label& brand = bar.AddLeading(RoleLabel("Zekit", Ink::Text, 13, true));
    brand.SetSize(52.0f, 22.0f);

    Kit::Badge& ver = bar.AddLeading(Kit::Badge("1.0.0"));
    ver.SetTone(Kit::BadgeTone::Neutral);
    ver.SetSize(42.0f, 18.0f);

    // ---- Dropdowns ----
    //  All four menus are real: File creates a child window, View switches
    //  theme / language / sidebar, Controls jumps to a page, Help answers back.
    //  The item lists live in the anonymous namespace so a language switch can
    //  rebuild them from the same single source.
    bar.SetMenuItems(bar.AddMenu(Tr("File", "文件")), FileMenuItems());
    bar.SetMenuItems(bar.AddMenu(Tr("View", "视图")), ViewMenuItems());
    bar.SetMenuItems(bar.AddMenu(Tr("Controls", "控件")), ControlsMenuItems());
    bar.SetMenuItems(bar.AddMenu(Tr("Help", "帮助")), HelpMenuItems());

    // ---- Right zone ----
    Kit::Icon& gear = bar.AddRight(ToneIcon(Kit::IconGlyph::Info, 26.0f, 14.0f,
        IconTone::Muted, [](Kit::Widget& self) {
            PopToast(self, Tr("Zekit 1.0.0 \u00B7 every widget here is display-only",
                              "Zekit 1.0.0 \u00B7 这里的控件只展示外观"),
                Kit::ToastLevel::Info);
        }));
    gear.SetTooltip(Tr("About Zekit 1.0.0", "关于 Zekit 1.0.0"));
}

void Demo::BuildToolBar() {
    Kit::ToolBar& tb = AddChild(Kit::ToolBar());
    tb.SetPadding(12.0f, 8.0f);
    tb.SetGap(0.0f, 6.0f);

    // Toolbar icons are decorative: clicking says so instead of doing nothing.
    const auto icon = [&tb](Kit::IconGlyph g, const char* tipEn, const char* tipZh) -> Kit::Icon& {
        Kit::Icon& ic = tb.AddChild(ToneIcon(g, 30.0f, 15.0f, IconTone::Muted,
            [tipEn, tipZh](Kit::Widget& self) {
                PopToast(self, std::string(Tr(tipEn, tipZh)) +
                    Tr(" \u00B7 display only, no behaviour", " \u00B7 展示项，无实际功能"),
                    Kit::ToastLevel::Info);
            }));
        ic.SetTooltip(Tr(tipEn, tipZh));
        return ic;
        };

    icon(Kit::IconGlyph::Plus, "New", "新建");
    icon(Kit::IconGlyph::ChevronRight, "Open", "打开");
    icon(Kit::IconGlyph::Check, "Save", "保存");
    tb.AddSeparator();
    icon(Kit::IconGlyph::Search, "Search", "搜索");
    icon(Kit::IconGlyph::More, "More", "更多");
    tb.AddSeparator();

    Kit::Label& title = tb.AddChild(RoleLabel(Tr("Control Gallery", "控件全览"), Ink::Text, 13, true));
    title.SetSize(110.0f, 30.0f);
    Loc(title, "Control Gallery", "控件全览");

    tb.AddSpacer();

    // Theme switch.
    themeBtn_ = &tb.AddChild(ToneButton(Tr("Dark", "深色"), BtnTone::Normal, 60.0f, 26.0f));
    themeBtn_->SetTooltip(Tr("Switch between light and dark theme", "切换深色 / 浅色主题"));
    themeBtn_->SetOnClick([](Kit::Widget& self) {
        if (Kit::Window* w = self.window())
            if (Demo* d = w->FindWidget<Demo>("demo")) d->ToggleTheme();
        });
    Loc(*themeBtn_, "Dark", "深色");

    // Language switch: shows the current language.
    langBtn_ = &tb.AddChild(ToneButton(Tr("EN", "中文"), BtnTone::Normal, 58.0f, 26.0f));
    langBtn_->SetTooltip(Tr("Switch UI language", "切换界面语言"));
    langBtn_->SetOnClick([](Kit::Widget& self) {
        if (Kit::Window* w = self.window())
            if (Demo* d = w->FindWidget<Demo>("demo")) d->ToggleLanguage();
        });
    Loc(*langBtn_, "EN", "中文");

    Kit::Badge& p1 = tb.AddChild(Kit::Badge(Tr("Kit \u00B7 82 public headers",
        "Kit 层 \u00B7 82 个公开头")));
    p1.SetTone(Kit::BadgeTone::Neutral);
    p1.SetSize(156.0f, 22.0f);
    Loc(p1, "Kit \u00B7 82 public headers", "Kit 层 \u00B7 82 个公开头");

    Kit::Badge& p2 = tb.AddChild(Kit::Badge(Tr("223 source files", "223 个自有文件")));
    p2.SetTone(Kit::BadgeTone::Neutral);
    p2.SetSize(124.0f, 22.0f);
    Loc(p2, "223 source files", "223 个自有文件");
}

void Demo::BuildBody() {
    Kit::Panel& body = AddChild(Kit::Panel(Kit::FlexDirection::Row));
    body.SetPadding(0.0f, 0.0f, 0.0f, 0.0f);
    body.SetGap(0.0f, 0.0f);
    body.SetSizePolicy(Kit::SizePolicy::Preferred, Kit::SizePolicy::Expanding);

    // ---- Sidebar ----
    Kit::Sidebar& side = body.AddChild(Kit::Sidebar());
    side_ = &side;
    side.SetObjectName("sidebar");     // menu items resolve it by name
    side.SetSize(216.0f, 600.0f);      // height comes from the container
    side.SetHeader(Tr("Categories", "分类导航"));
    // No SetFooter: the footer draws as a small right-aligned line hugging the
    // bottom edge, which reads as a misplaced label in a 216px column.
    side.AddItem(Tr("Basic \u00B7 15", "基础视觉 \u00B7 15"));
    side.AddItem(Tr("Text \u00B7 5", "文本与编辑 \u00B7 5"));
    side.AddItem(Tr("Pickers \u00B7 6", "选择器 \u00B7 6"));
    side.AddItem(Tr("Containers \u00B7 7", "容器与导航 \u00B7 7"));
    side.AddItem(Tr("Overlays \u00B7 6", "浮层与对话 \u00B7 6"));
    side.AddItem(Tr("Charts \u00B7 6", "图表与布局 \u00B7 6"));
    side.AddItem(Tr("Window \u00B7 9", "窗口 \u00B7 9"));
    side.SetCurrentItem(0);

    // ---- Content area ----
    Kit::Panel& main = body.AddChild(Kit::Panel());
    main.SetPadding(0.0f, 0.0f, 0.0f, 0.0f);
    main.SetGap(0.0f, 0.0f);
    main.SetFlexGrow(1.0f);

    Kit::TabView& tv = main.AddChild(Kit::TabView());
    tabs_ = &tv;
    tv.SetObjectName("tabs");          // "Controls" menu resolves it by name
    tv.SetSizePolicy(Kit::SizePolicy::Preferred, Kit::SizePolicy::Expanding);
    tv.SetTabBarHeight(44.0f);
    tv.SetTabPaddingX(14.0f);
    tv.SetPadding(0.0f, 44.0f, 0.0f, 0.0f);

    int idx[kPageCount] = {};
    for (int i = 0; i < kPageCount; ++i) idx[i] = tv.AddTab(Tr(kPageEn[i], kPageZh[i]));

    BuildBasicPage(*tv.tabContent(idx[0]));
    BuildTextPage(*tv.tabContent(idx[1]));
    BuildPickerPage(*tv.tabContent(idx[2]));
    BuildContainerPage(*tv.tabContent(idx[3]));
    BuildOverlayPage(*tv.tabContent(idx[4]));
    BuildChartPage(*tv.tabContent(idx[5]));
    BuildWindowPage(*tv.tabContent(idx[6]));

    // Sidebar <-> tab view stay in sync; both sides capture the other's stable
    // in-tree pointer.
    Kit::TabView* tabs = tabs_;
    side.SetOnItemActivated([tabs](Kit::Widget&, int i) { tabs->SetCurrentIndex(i); });
    Kit::Sidebar* sidePtr = side_;
    tv.SetOnTabChanged([sidePtr](Kit::Widget&, int i) { sidePtr->SetCurrentItem(i); });
}

void Demo::BuildStatusBar() {
    Kit::StatusBar& sb = AddChild(Kit::StatusBar());
    status_ = &sb;
    // Callbacks find the bar by name — the Demo instance itself is moved into
    // the window, so no child may hold a pointer to its owner.
    sb.SetObjectName("statusbar");
    sb.SetPadding(12.0f, 4.0f);

    // The first three sections are filled with real data by
    // RefreshRuntimeInfo() after the window is shown; the indices below are the
    // AddSection order and must match it.
    sb.AddSection(Tr("Renderer: \u2014", "渲染后端：\u2014"));
    sb.AddSection(Tr("GPU: \u2014", "GPU：\u2014"));
    sb.AddSection(Tr("DPI \u2014", "DPI \u2014"));

    sb.AddStretch();

    // Section 3 is the live activity line: every control writes the result of
    // the last interaction here (see Activity() above). It starts with a hint
    // instead of a disclaimer — nothing in this demo is display-only any more.
    sb.AddSection(Tr("Ready \u00B7 try any control", "就绪 \u00B7 动手试试任意控件"));
    sb.AddSection(Tr("Kit \u00B7 Display list \u00B7 Rhi \u00B7 Plat",
        "Kit \u00B7 显示列表 \u00B7 Rhi \u00B7 Plat"));
    sb.AddSection("Zekit 1.0.0 \u00B7 C++20");
}

void Demo::RefreshRuntimeInfo() {
    Kit::Window* w = window();
    if (!w || !status_) return;

    // Real renderer backend: D3D12 is the default on Windows, OpenGL only
    // appears when initialisation falls back.
    const Rhi::Api api = w->rendererApi();
    status_->SetSectionText(0, std::string(Tr("Renderer: ", "渲染后端：")) + Rhi::ApiName(api));

    // Which GPU actually does the drawing.
    const Rhi::DeviceInfo dev = w->deviceInfo();
    if (dev.valid && dev.name[0] != '\0') {
        status_->SetSectionText(1, std::string(Tr("GPU: ", "GPU：")) + dev.name);
    } else {
        status_->SetSectionText(1, Tr("GPU: unavailable", "GPU：不可查询"));
    }

    // 96 DPI is 100%; 144 is 150%.
    const int dpi = w->GetDpi();
    const float k = w->GetScaleFactor();
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%d%% \u00B7 %d DPI",
        static_cast<int>(k * 100.0f + 0.5f), dpi);
    status_->SetSectionText(2, buf);
}

// Widgets whose text is not a single TextWidget field.
void Demo::RelocalizeComposite() {
    if (themeBtn_) themeBtn_->SetText(Tr("Dark", "深色"));
    if (langBtn_)  langBtn_->SetText(Tr("EN", "中文"));

    if (tabs_) {
        for (int i = 0; i < static_cast<int>(tabs_->tabCount()) && i < kPageCount; ++i)
            tabs_->SetTabTitle(i, Tr(kPageEn[i], kPageZh[i]));
    }

    if (side_) {
        side_->SetHeader(Tr("Categories", "分类导航"));
        side_->SetItems({
            Tr("Basic \u00B7 15", "基础视觉 \u00B7 15"),
            Tr("Text \u00B7 5", "文本与编辑 \u00B7 5"),
            Tr("Pickers \u00B7 6", "选择器 \u00B7 6"),
            Tr("Containers \u00B7 7", "容器与导航 \u00B7 7"),
            Tr("Overlays \u00B7 6", "浮层与对话 \u00B7 6"),
            Tr("Charts \u00B7 6", "图表与布局 \u00B7 6"),
            Tr("Window \u00B7 9", "窗口 \u00B7 9"),
            });
        side_->SetCurrentItem(tabs_ ? tabs_->currentIndex() : 0);
    }

    if (status_) {
        // Section 3 is the live activity line — normally owned by the controls
        // that write it. A language switch is an explicit "start over", so it
        // is reset to the hint here together with the static label beside it.
        status_->SetSectionText(3, Tr("Ready \u00B7 try any control", "就绪 \u00B7 动手试试任意控件"));
        status_->SetSectionText(4, Tr("Kit \u00B7 Display list \u00B7 Rhi \u00B7 Plat",
            "Kit \u00B7 显示列表 \u00B7 Rhi \u00B7 Plat"));
    }

    // Menus are rebuilt because item texts are captured at construction time.
    // The four builders are shared with BuildMenuBar, so the two copies cannot
    // drift apart when an item is added or removed.
    if (menu_) {
        Kit::MenuBar& bar = *menu_;
        if (bar.menuCount() >= 4) {
            bar.SetMenuTitle(0, Tr("File", "文件"));
            bar.SetMenuItems(0, FileMenuItems());
            bar.SetMenuTitle(1, Tr("View", "视图"));
            bar.SetMenuItems(1, ViewMenuItems());
            bar.SetMenuTitle(2, Tr("Controls", "控件"));
            bar.SetMenuItems(2, ControlsMenuItems());
            bar.SetMenuTitle(3, Tr("Help", "帮助"));
            bar.SetMenuItems(3, HelpMenuItems());
        }
    }
}

// ===========================================================================
//  Tab 1 — basic visuals
// ===========================================================================

void Demo::BuildBasicPage(Kit::Panel& page) {
    Kit::VBoxLayout& content = Scrolled(page);

    // ---- Row 1: buttons / check box / radio ----
    Kit::HBoxLayout& r1 = Row(content, 14.0f);

    {
        Kit::Card& c = NewCard(r1, "Button", "按钮", "ZeButton.h \u00B7 276");
        Kit::Label& out = Readout(c);
        Kit::Label* op = &out;

        Kit::HBoxLayout& a = Row(c, 6.0f);
        Btn(a, "Primary", "主要按钮", BtnTone::Primary, 78.0f).SetOnClick([op](Kit::Widget& self) {
            Report(self, *op, std::string(Tr("clicked: ", "点击：")) + Tr("Primary", "主要按钮"));
            });
        Btn(a, "Secondary", "次要按钮", BtnTone::Normal, 84.0f).SetOnClick([op](Kit::Widget& self) {
            Report(self, *op, std::string(Tr("clicked: ", "点击：")) + Tr("Secondary", "次要按钮"));
            });
        Btn(a, "Ghost", "幽灵", BtnTone::Ghost, 56.0f).SetOnClick([op](Kit::Widget& self) {
            Report(self, *op, std::string(Tr("clicked: ", "点击：")) + Tr("Ghost", "幽灵"));
            });
        Kit::HBoxLayout& b = Row(c, 6.0f);
        Btn(b, "Delete", "删除", BtnTone::Danger, 62.0f).SetOnClick([op](Kit::Widget& self) {
            Report(self, *op, std::string(Tr("clicked: ", "点击：")) + Tr("Delete", "删除"));
            });
        Kit::Button& dis = Btn(b, "Disabled", "禁用", BtnTone::Normal, 68.0f);
        dis.SetEnabled(false);
        Btn(b, "Small", "小号", BtnTone::Normal, 50.0f, 24.0f).SetOnClick([op](Kit::Widget& self) {
            Report(self, *op, std::string(Tr("clicked: ", "点击：")) + Tr("Small", "小号"));
            });
        Cap(c, "Disabled ignores clicks; the rest report themselves",
            "禁用态不响应 · 其余按钮点击后自报");
    }
    {
        Kit::Card& c = NewCard(r1, "Check Box", "复选框", "ZeCheckbox.h \u00B7 287");
        Kit::Label& out = Readout(c);
        Kit::Label* op = &out;

        Kit::Checkbox& c1 = Loc(c.AddChild(Kit::Checkbox(Tr("Checked", "已勾选"), true)),
            "Checked", "已勾选");
        c1.SetSize(0.0f, 22.0f);
        Kit::Checkbox& c2 = Loc(c.AddChild(Kit::Checkbox(Tr("Unchecked", "未勾选"), false)),
            "Unchecked", "未勾选");
        c2.SetSize(0.0f, 22.0f);
        Kit::Checkbox& c3 = Loc(c.AddChild(Kit::Checkbox(Tr("Indeterminate", "半选状态"), false)),
            "Indeterminate", "半选状态");
        c3.SetTriState(true);
        c3.SetState(Kit::CheckState::Indeterminate);
        c3.SetSize(0.0f, 22.0f);
        Kit::Checkbox& c4 = Loc(c.AddChild(Kit::Checkbox(Tr("Disabled", "禁用"), true)),
            "Disabled", "禁用");
        c4.SetSize(0.0f, 22.0f);
        c4.SetEnabled(false);

        // Three boxes report as one line: "checked: A C~" — the third one is
        // tri-state, so its state is spelled out rather than a plain tick.
        Kit::Checkbox* p1 = &c1;
        Kit::Checkbox* p2 = &c2;
        Kit::Checkbox* p3 = &c3;
        auto refresh = [op, p1, p2, p3](Kit::Widget& self) {
            std::string s = Tr("checked: ", "已勾选：");
            std::string hits;
            if (p1->checked()) hits += "A ";
            if (p2->checked()) hits += "B ";
            if (p3->state() == Kit::CheckState::Checked) hits += "C ";
            else if (p3->state() == Kit::CheckState::Indeterminate) hits += "C~ ";
            if (hits.empty()) hits = Tr("nothing", "无");
            Report(self, *op, s + hits);
            };
        c1.SetOnToggled(refresh);
        c2.SetOnToggled(refresh);
        c3.SetOnToggled(refresh);
    }
    {
        Kit::Card& c = NewCard(r1, "Radio Button", "单选框", "ZeRadioButton.h \u00B7 239");
        Kit::Label& out = Readout(c);
        Kit::Label* op = &out;

        Kit::RadioButton& a = Loc(c.AddChild(Kit::RadioButton(Tr("Option A", "选项 A"), true)),
            "Option A", "选项 A");
        a.SetGroup("demo");
        a.SetSize(0.0f, 22.0f);
        Kit::RadioButton& b = Loc(c.AddChild(Kit::RadioButton(Tr("Option B", "选项 B"))),
            "Option B", "选项 B");
        b.SetGroup("demo");
        b.SetSize(0.0f, 22.0f);
        Kit::RadioButton& d = Loc(c.AddChild(Kit::RadioButton(Tr("Option C", "选项 C"))),
            "Option C", "选项 C");
        d.SetGroup("demo");
        d.SetSize(0.0f, 22.0f);

        // Every item in the group reports the same thing: which one is selected
        // right now. The address of a child is stable inside the tree, so
        // capturing the three of them by pointer is safe.
        Kit::RadioButton* pa = &a;
        Kit::RadioButton* pb = &b;
        Kit::RadioButton* pd = &d;
        auto refresh = [op, pa, pb, pd](Kit::Widget& self) {
            const char* pick = pa->selected() ? "A" : pb->selected() ? "B" : pd->selected() ? "C" : "-";
            Report(self, *op, std::string(Tr("selected: ", "已选中：")) + pick);
            };
        a.SetOnSelected(refresh);
        b.SetOnSelected(refresh);
        d.SetOnSelected(refresh);
        Cap(c, "Selecting one clears the others in the group",
            "同组多选一 · 点另一项自动取消本项");
    }

    // ---- Row 2: switch / slider / progress ----
    Kit::HBoxLayout& r2 = Row(content, 14.0f);

    {
        Kit::Card& c = NewCard(r2, "Switch", "开关", "ZeSwitch.h \u00B7 204");
        Kit::Label& out = Readout(c);
        Kit::Label* op = &out;

        Kit::HBoxLayout& a = Row(c, 10.0f);
        Kit::Switch& s1 = a.AddChild(Kit::Switch(true));
        s1.SetTrackSize(38.0f, 21.0f);
        s1.SetSize(38.0f, 21.0f);
        Cap(a, "On", "已开启");
        Kit::HBoxLayout& b = Row(c, 10.0f);
        Kit::Switch& s2 = b.AddChild(Kit::Switch(false));
        s2.SetTrackSize(38.0f, 21.0f);
        s2.SetSize(38.0f, 21.0f);
        Cap(b, "Off", "已关闭");

        Kit::Switch* ps1 = &s1;
        auto refresh = [op, ps1](Kit::Widget& self, bool on) {
            // The sender is the switch that changed: compare addresses rather
            // than caching a second copy of the state.
            const bool first = (&self == static_cast<Kit::Widget*>(ps1));
            std::string s = first ? Tr("left switch: ", "左开关：") : Tr("right switch: ", "右开关：");
            s += on ? Tr("on", "开") : Tr("off", "关");
            Report(self, *op, s);
            };
        s1.SetOnToggled(refresh);
        s2.SetOnToggled(refresh);
        Cap(c, "Capsule track with an animated knob", "胶囊轨道 + 滑块，切换带动画");
    }
    {
        Kit::Card& c = NewCard(r2, "Slider", "滑块", "ZeSlider.h \u00B7 339");
        Kit::Label& out = Readout(c);
        Kit::Label* op = &out;

        Kit::Slider& s = c.AddChild(Kit::Slider());
        s.SetRange(0.0f, 100.0f);
        s.SetValue(45.0f);
        s.SetShowText(false);
        s.SetSize(0.0f, 22.0f);
        Kit::HBoxLayout& legend = Row(c, 0.0f);
        legend.SetJustify(Kit::JustifyContent::SpaceBetween);
        Cap(legend, "0", "0", Ink::Faint);
        Kit::Label& mid = Cap(legend, "45", "45");
        mid.SetBold(true);
        Cap(legend, "100", "100", Ink::Faint);

        // Drag the knob and both the legend and the readout follow: this is the
        // clearest proof that SetOnValueChanged fires on the user path.
        Kit::Label* pmid = &mid;
        s.SetOnValueChanged([op, pmid](Kit::Widget& self, float v) {
            const std::string txt = Num(v, 0);
            SetReadout(*pmid, txt);
            Report(self, *op, std::string(Tr("slider: ", "滑块：")) + txt);
            });
        Cap(c, "Drag the knob or use the arrow keys", "拖动滑块或按方向键");
    }
    {
        Kit::Card& c = NewCard(r2, "Progress Bar", "进度条", "ZeProgressBar.h \u00B7 418");
        Kit::Label& out = Readout(c);
        Kit::Label* op = &out;

        Kit::ProgressBar& p1 = c.AddChild(Kit::ProgressBar());
        p1.SetRange(0.0f, 100.0f);
        p1.SetValue(62.0f);
        p1.SetSize(0.0f, 8.0f);

        Kit::ProgressBar& p2 = c.AddChild(Kit::ProgressBar());
        p2.SetRange(0.0f, 100.0f);
        p2.SetValue(34.0f);
        p2.SetSize(0.0f, 8.0f);

        Kit::ProgressBar& p3 = c.AddChild(Kit::ProgressBar());
        p3.SetRange(0.0f, 100.0f);
        p3.SetValue(72.0f);
        p3.SetDraggable(true);
        p3.SetShowThumb(true);
        p3.SetSize(0.0f, 8.0f);

        // The third bar is draggable, so it reports its own value; the two
        // static bars get a button that runs them to 100% and back.
        Kit::ProgressBar* pp1 = &p1;
        Kit::ProgressBar* pp2 = &p2;
        Kit::ProgressBar* pp3 = &p3;
        p3.SetOnValueChanged([op, pp3](Kit::Widget& self, float v) {
            Report(self, *op, std::string(Tr("drag bar: ", "拖动条：")) + Num(v, 0) + "%");
            });

        Kit::HBoxLayout& a = Row(c, 6.0f);
        Btn(a, "Fill", "填满", BtnTone::Normal, 64.0f, 24.0f).SetOnClick([op, pp1, pp2](Kit::Widget& self) {
            pp1->SetValue(100.0f);
            pp2->SetValue(100.0f);
            Report(self, *op, Tr("both bars at 100%", "两条都到 100%"));
            });
        Btn(a, "Reset", "归零", BtnTone::Normal, 64.0f, 24.0f).SetOnClick([op, pp1, pp2](Kit::Widget& self) {
            pp1->SetValue(0.0f);
            pp2->SetValue(0.0f);
            Report(self, *op, Tr("both bars at 0%", "两条都归零"));
            });
        Cap(c, "The third one is a drag bar (SetDraggable)", "第三条 SetDraggable(true) —— 拖动条");
    }

    // ---- Row 3: rating / badge / avatar ----
    Kit::HBoxLayout& r3 = Row(content, 14.0f);

    {
        Kit::Card& c = NewCard(r3, "Rating", "评分", "ZeRating.h \u00B7 183");
        Kit::Label& out = Readout(c);
        Kit::Label* op = &out;

        Kit::Rating& r = c.AddChild(Kit::Rating());
        r.SetMaxStars(5);
        r.SetAllowHalf(true);
        r.SetValue(4.5f);
        r.SetStarSize(20.0f);
        r.SetSize(0.0f, 28.0f);
        Kit::Rating& r2b = c.AddChild(Kit::Rating());
        r2b.SetMaxStars(5);
        r2b.SetValue(3.0f);
        r2b.SetStarSize(16.0f);
        r2b.SetReadOnly(true);
        r2b.SetSize(0.0f, 24.0f);

        r.SetOnValueChanged([op](Kit::Widget& self, float v) {
            std::string s = std::string(Tr("rated ", "评分 ")) + Num(v, 1)
                + " / " + Num(5.0f, 0);
            Report(self, *op, s);
            });
        Cap(c, "Half-star precision; the second one is read-only",
            "支持半星精度 · 第二个是只读");
    }
    {
        Kit::Card& c = NewCard(r3, "Badge", "徽章", "ZeBadge.h \u00B7 153");
        Kit::HBoxLayout& a = Row(c, 10.0f);
        Kit::Badge& b1 = a.AddChild(Kit::Badge("12"));
        b1.SetTone(Kit::BadgeTone::Danger);
        b1.SetSize(26.0f, 18.0f);
        Kit::Badge& b2 = a.AddChild(Kit::Badge("99+"));
        b2.SetSize(38.0f, 18.0f);
        Kit::Badge& b3 = Loc(a.AddChild(Kit::Badge(Tr("Draft", "草稿"))), "Draft", "草稿");
        b3.SetTone(Kit::BadgeTone::Neutral);
        b3.SetSize(44.0f, 18.0f);
        Kit::Badge& b4 = a.AddChild(Kit::Badge());
        b4.SetDot(true);
        b4.SetSize(8.0f, 8.0f);

        Kit::HBoxLayout& b = Row(c, 8.0f);
        Btn(b, "Notify", "通知", BtnTone::Normal, 76.0f).SetTooltip(
            Tr("3 unread messages", "3 条未读消息"));
        Kit::Badge& dot = b.AddChild(Kit::Badge());
        dot.SetDot(true);
        dot.SetTone(Kit::BadgeTone::Success);
        dot.SetSize(8.0f, 8.0f);
    }
    {
        Kit::Card& c = NewCard(r3, "Avatar", "头像", "ZeAvatar.h \u00B7 169");
        Kit::HBoxLayout& a = Row(c, 12.0f);
        Kit::Avatar& av1 = a.AddChild(ToneAvatar("Z", AvatarTone::Primary));
        av1.SetSide(38.0f);
        av1.SetStatus(Kit::AvatarStatus::Online);
        Kit::Avatar& av2 = a.AddChild(ToneAvatar("K", AvatarTone::Success));
        av2.SetSide(38.0f);
        av2.SetShape(Kit::AvatarShape::Square);
        av2.SetStatus(Kit::AvatarStatus::Away);
        Kit::Avatar& av3 = a.AddChild(ToneAvatar("T", AvatarTone::Danger));
        av3.SetSide(38.0f);
        av3.SetStatus(Kit::AvatarStatus::Busy);

        Kit::HBoxLayout& b = Row(c, 10.0f);
        const float sides[] = { 26.0f, 30.0f, 34.0f };
        for (float s : sides) {
            Kit::Avatar& av = b.AddChild(ToneAvatar("A", AvatarTone::Primary));
            av.SetSide(s);
        }
        Cap(b, "Sizes", "尺寸可调");
    }

    // ---- Row 4: icon / image / divider ----
    Kit::HBoxLayout& r4 = Row(content, 14.0f);

    {
        Kit::Card& c = NewCard(r4, "Icon", "图标", "ZeIcon.h \u00B7 151");
        Kit::HBoxLayout& a = Row(c, 12.0f);
        const Kit::IconGlyph glyphs[] = {
            Kit::IconGlyph::Search, Kit::IconGlyph::Check, Kit::IconGlyph::Info,
            Kit::IconGlyph::Warning, Kit::IconGlyph::Close
        };
        for (Kit::IconGlyph g : glyphs) {
            a.AddChild(ToneIcon(g, 22.0f, 18.0f, IconTone::Primary));
        }
        Cap(c, "Built-in vector glyphs; bitmaps are supported too",
            "内置矢量字形，也可加载图片");
    }
    {
        Kit::Card& c = NewCard(r4, "Image", "图片", "ZeImage.h \u00B7 119");
        Kit::HBoxLayout& a = Row(c, 10.0f);
        a.SetAlign(Kit::AlignItems::Stretch);
        Kit::Image& i1 = a.AddChild(ToneImage());
        i1.SetFlexGrow(1.0f);
        i1.SetSize(0.0f, 64.0f);
        i1.SetDrawMode(Kit::BgImageDrawMode::Fill);

        Kit::Image& i2 = a.AddChild(ToneImage());
        i2.SetFlexGrow(1.0f);
        i2.SetSize(0.0f, 64.0f);
        i2.SetDrawMode(Kit::BgImageDrawMode::Fit);
        Cap(c, "Stretch / Fit / Fill / Tiled / Origin",
            "Stretch / Fit / Fill / Tiled / Origin");
    }
    {
        Kit::Card& c = NewCard(r4, "Divider", "分隔线", "ZeDivider.h \u00B7 122");
        c.AddChild(LineDivider());
        Kit::Divider& d2 = c.AddChild(LineDivider());
        d2.SetLabel(Tr("Section", "分组标题"));
        Kit::Divider* sectionDivider = &d2;
        OnLang([sectionDivider] { sectionDivider->SetLabel(Tr("Section", "分组标题")); });
        c.AddChild(LineDivider());

        Kit::HBoxLayout& a = Row(c, 10.0f);
        a.SetAlign(Kit::AlignItems::Stretch);
        Cap(a, "Left", "左", Ink::Faint);
        Kit::Divider& dv = a.AddChild(LineDivider(Kit::DividerOrientation::Vertical));
        dv.SetSize(1.0f, 26.0f);
        Cap(a, "Right", "右", Ink::Faint);
    }

    // ---- Row 5: loading / group panel / watermark ----
    Kit::HBoxLayout& r5 = Row(content, 14.0f);

    {
        Kit::Card& c = NewCard(r5, "Loading", "加载", "ZeLoading.h \u00B7 187");
        Kit::HBoxLayout& a = Row(c, 22.0f);
        {
            Kit::VBoxLayout& v = Col(a, 5.0f);
            v.SetAlign(Kit::AlignItems::Center);
            Kit::Loading& l = v.AddChild(Kit::Loading(Kit::LoadingVariant::Ring));
            l.SetSize(22.0f);
            Cap(v, "Ring", "转圈", Ink::Faint, 10);
        }
        {
            Kit::VBoxLayout& v = Col(a, 5.0f);
            v.SetAlign(Kit::AlignItems::Center);
            Kit::Loading& l = v.AddChild(Kit::Loading(Kit::LoadingVariant::Dots));
            l.SetItemCount(3);
            l.SetSize(18.0f);
            Cap(v, "Dots", "圆点", Ink::Faint, 10);
        }
        {
            Kit::VBoxLayout& v = Col(a, 5.0f);
            v.SetAlign(Kit::AlignItems::Center);
            Kit::Loading& l = v.AddChild(Kit::Loading(Kit::LoadingVariant::Bars));
            l.SetItemCount(4);
            l.SetSize(18.0f);
            Cap(v, "Bars", "音柱", Ink::Faint, 10);
        }
    }
    {
        Kit::Card& c = NewCard(r5, "Group Panel", "分组面板", "ZeGroupPanel.h \u00B7 130");
        Kit::GroupPanel& g = c.AddChild(Kit::GroupPanel(Tr("Basic settings", "基础设置")));
        Loc(g, "Basic settings", "基础设置");
        g.SetPadding(10.0f, 6.0f, 10.0f, 10.0f);
        g.SetMinHeight(116.0f);
        Kit::Checkbox& k1 = Loc(g.AddChild(Kit::Checkbox(Tr("Theme injection", "启用主题注入"), true)),
            "Theme injection", "启用主题注入");
        k1.SetSize(0.0f, 22.0f);
        Kit::Checkbox& k2 = Loc(g.AddChild(Kit::Checkbox(Tr("Debug borders", "显示调试边框"), false)),
            "Debug borders", "显示调试边框");
        k2.SetSize(0.0f, 22.0f);
    }
    {
        Kit::Card& c = NewCard(r5, "Watermark", "水印", "ZeWatermark.h \u00B7 137");
        Kit::Watermark& wm = c.AddChild(ToneWatermark(Tr("ZEKIT   PREVIEW", "ZEKIT   内部预览")));
        Loc(wm, "ZEKIT   PREVIEW", "ZEKIT   内部预览");
        wm.SetMinHeight(84.0f);
        wm.SetFontSize(11);
        wm.SetGap(20.0f, 14.0f);
        wm.SetStagger(true);
    }
}

// ===========================================================================
//  Tab 2 — text and editing
// ===========================================================================

void Demo::BuildTextPage(Kit::Panel& page) {
    Kit::VBoxLayout& content = Scrolled(page);

    Kit::HBoxLayout& r1 = Row(content, 14.0f);

    {
        Kit::Card& c = NewCard(r1, "Label", "标签", "ZeLabel.h \u00B7 96");
        Kit::Label& heading = c.AddChild(RoleLabel(Tr("Heading", "标题文字"), Ink::Text, 15, true));
        Loc(heading, "Heading", "标题文字");
        Kit::Label& p = c.AddChild(RoleLabel(
            Tr("Read-only multiline label; splits on \\n and tolerates \\r\\n.\n"
               "Overflow can be clipped, ellipsized or wrapped.",
               "只读多行标签，按 \\n 切行，\n同时兼容 \\r\\n；\n可配置溢出处理：裁剪 / 省略号 / 换行。"),
            Ink::Muted, 12));
        p.SetLineHeight(22.0f);
    }
    {
        Kit::Card& c = NewCard(r1, "Input Box", "输入框", "ZeInputBox.h \u00B7 86");
        Kit::Label& out = Readout(c);
        Kit::Label* op = &out;

        Kit::InputBox& a = c.AddChild(Kit::InputBox());
        Loc(a, "Type something", "请输入内容…", LocKind::Placeholder);
        a.SetPlaceholder(Tr("Type something", "请输入内容…"));
        a.SetSize(0.0f, 30.0f);
        Kit::InputBox& b = c.AddChild(Kit::InputBox());
        b.SetText(Tr("Filled text", "已填充的文本"));
        b.SetSize(0.0f, 30.0f);
        Kit::InputBox& d = c.AddChild(Kit::InputBox());
        Loc(d, "Read-only", "只读状态", LocKind::Placeholder);
        d.SetPlaceholder(Tr("Read-only", "只读状态"));
        d.SetReadOnly(true);
        d.SetSize(0.0f, 30.0f);
        d.SetEnabled(false);

        a.SetOnTextChanged([op](Kit::Widget& self, const std::string& s) {
            Report(self, *op, std::string(Tr("length: ", "长度：")) + std::to_string(s.size())
                + Tr(" bytes", " 字节"));
            });
        a.SetOnReturn([op](Kit::Widget& self, const std::string& s) {
            Report(self, *op, std::string(Tr("submitted: ", "已提交：")) + (s.empty() ? "-" : s));
            });
    }
    {
        Kit::Card& c = NewCard(r1, "Text Area", "多行文本", "ZeTextArea.h \u00B7 151");
        Kit::Label& out = Readout(c);
        Kit::Label* op = &out;

        Kit::TextArea& ta = c.AddChild(Kit::TextArea());
        Loc(ta, "Soft wrap, vertical scroll, tab indent", "软换行 + 纵向滚动 + Tab 缩进",
            LocKind::Placeholder);
        ta.SetPlaceholder(Tr("Soft wrap, vertical scroll, tab indent", "软换行 + 纵向滚动 + Tab 缩进"));
        ta.SetText(Tr(
            "A few lines of editable text to show the scrolling behaviour.\n"
            "Line 2: Tab inserts indentation.\n"
            "Line 3: the scroll bar appears only when needed.",
            "这里是一段多行文本，用于演示 TextArea 的编辑与滚动行为。\n"
            "第二行：Tab 键插入缩进。\n"
            "第三行：滚动条按需出现。"));
        ta.SetVisibleLines(5);
        ta.SetSize(0.0f, 120.0f);
        ta.SetOnTextChanged([op](Kit::Widget& self, const std::string& s) {
            std::size_t lines = 1;
            for (char ch : s) if (ch == '\n') ++lines;
            Report(self, *op, std::to_string(lines) + Tr(" lines, ", " 行，")
                + std::to_string(s.size()) + Tr(" bytes", " 字节"));
            });
    }

    // ---- CodeEditor (full width) ----
    Kit::HBoxLayout& r2 = Row(content, 14.0f);
    {
        Kit::Card& c = NewCard(r2, "Code Editor", "代码编辑器", "ZeCodeEditor.h \u00B7 241");
        Kit::CodeEditor& ce = c.AddChild(Kit::CodeEditor());
        ce.SetHighlighter(std::make_shared<Kit::CppHighlighter>());
        ce.SetVisibleLines(7);
        ce.SetSize(0.0f, 150.0f);
        ce.SetText(
            "// widgets describe, they never touch the GPU\n"
            "void Button::Paint(FrameCollector& fc) const {\n"
            "    fc.PushClip(bounds_);\n"
            "    fc.FillRoundedRect(bounds_, style_.radius, bg());\n"
            "    fc.DrawText(text_, textRect(), style_.textStyle);\n"
            "    fc.PopClip();\n"
            "}");
        Cap(c, "Pluggable syntax highlighting, line numbers, bracket matching",
            "可插拔语法高亮 · 行号 · 括号匹配");
    }

    // ---- RichText (full width) ----
    Kit::HBoxLayout& r3 = Row(content, 14.0f);
    {
        Kit::Card& c = NewCard(r3, "Rich Text", "富文本", "ZeRichText.h \u00B7 465");
        Kit::RichText& rt = c.AddChild(Kit::RichText());
        Kit::RichText* rich = &rt;
        FillRichText(rt);
        OnLang([rich] { FillRichText(*rich); });
        rt.SetMinHeight(56.0f);
        rt.SetLineSpacing(8.0f);
        Cap(c, "Flattened offsets with cross-run hit testing", "展平偏移建模 + 跨 run 命中测试");
    }
}

// ===========================================================================
//  Tab 3 — pickers
// ===========================================================================

void Demo::BuildPickerPage(Kit::Panel& page) {
    Kit::VBoxLayout& content = Scrolled(page);

    Kit::HBoxLayout& r1 = Row(content, 14.0f);

    {
        Kit::Card& c = NewCard(r1, "Combo Box", "下拉框", "ZeComboBox.h \u00B7 411");
        Kit::Label& out = Readout(c);
        Kit::Label* op = &out;

        Kit::ComboBox& cb = c.AddChild(Kit::ComboBox());
        Loc(cb, "Pick a renderer", "请选择一个选项", LocKind::Placeholder);
        cb.SetPlaceholder(Tr("Pick a renderer", "请选择一个选项"));
        cb.SetItems({
            Tr("Vulkan", "Vulkan"), Tr("OpenGL", "OpenGL"),
            Tr("D3D12", "D3D12"), Tr("Metal", "Metal") });
        cb.SetSelectedIndex(1);
        cb.SetSize(0.0f, 30.0f);
        Kit::ComboBox& cb2 = c.AddChild(Kit::ComboBox());
        Loc(cb2, "Disabled", "禁用态", LocKind::Placeholder);
        cb2.SetPlaceholder(Tr("Disabled", "禁用态"));
        cb2.SetItems({ Tr("Not selectable", "不可选") });
        cb2.SetSize(0.0f, 30.0f);
        cb2.SetEnabled(false);

        Kit::ComboBox* pcb = &cb;
        cb.SetOnSelectionChanged([op, pcb](Kit::Widget& self, int index) {
            const std::string pick = (index >= 0) ? pcb->itemAt(index) : std::string("-");
            Report(self, *op, std::string(Tr("picked: ", "已选择：")) + pick);
            });
        Cap(c, "Pick only, no typing; options open in an overlay",
            "只选不打 · 浮层选项列表");
    }
    {
        Kit::Card& c = NewCard(r1, "Editable Combo", "可编辑下拉框", "ZeEditableComboBox.h \u00B7 131");
        Kit::Label& out = Readout(c);
        Kit::Label* op = &out;

        Kit::EditableComboBox& ec = c.AddChild(Kit::EditableComboBox());
        Loc(ec, "Custom platform", "自定义平台", LocKind::Placeholder);
        ec.SetPlaceholder(Tr("Custom platform", "自定义平台"));
        ec.SetItems({ "Windows", "Linux", "Android", "HarmonyOS" });
        ec.SetValue(Tr("Custom platform", "自定义平台"));
        ec.SetSize(0.0f, 30.0f);
        Kit::EditableComboBox* editableCombo = &ec;
        OnLang([editableCombo] { editableCombo->SetValue(Tr("Custom platform", "自定义平台")); });

        ec.SetOnTextChanged([op](Kit::Widget& self, const std::string& s) {
            Report(self, *op, std::string(Tr("text: ", "输入："))
                + (s.empty() ? std::string("-") : s));
            });
        Cap(c, "Pick from the list or type freely", "能选也能直接打字");
    }
    {
        Kit::Card& c = NewCard(r1, "Number Box", "数字输入框", "ZeNumberBox.h \u00B7 240");
        Kit::Label& out = Readout(c);
        Kit::Label* op = &out;

        Kit::HBoxLayout& a = Row(c, 10.0f);
        Kit::NumberBox& n1 = a.AddChild(Kit::NumberBox(42.0f));
        n1.SetRange(0.0f, 100.0f, 1.0f);
        n1.SetSize(90.0f, 30.0f);
        Cap(a, "Range [0,100], step 1", "范围 [0,100] · 步长 1");

        Kit::HBoxLayout& b = Row(c, 10.0f);
        Kit::NumberBox& n2 = b.AddChild(Kit::NumberBox(3.14f));
        n2.SetDecimals(2);
        n2.SetSize(90.0f, 30.0f);
        Cap(b, "Two decimals", "精度 2 位");

        n1.SetOnValueChanged([op](Kit::Widget& self, float v) {
            Report(self, *op, std::string(Tr("int box: ", "整数框：")) + Num(v, 0));
            });
        n2.SetOnValueChanged([op](Kit::Widget& self, float v) {
            Report(self, *op, std::string(Tr("decimal box: ", "小数框：")) + Num(v, 2));
            });
    }

    Kit::HBoxLayout& r2 = Row(content, 14.0f);

    {
        Kit::Card& c = NewCard(r2, "Date Time Picker", "日期时间", "ZeDateTimePicker.h \u00B7 284");
        Kit::Label& out = Readout(c);
        Kit::Label* op = &out;

        Kit::DateTimePicker& dp = c.AddChild(Kit::DateTimePicker());
        Kit::DateTime v;
        v.year = 2026; v.month = 10; v.day = 24;
        dp.SetValue(v);
        dp.SetSize(0.0f, 30.0f);
        Kit::DateTimePicker& dp2 = c.AddChild(Kit::DateTimePicker());
        dp2.SetShowTime(true, true);
        dp2.SetSize(0.0f, 30.0f);

        dp.SetOnValueCommitted([op](Kit::Widget& self, const Kit::DateTime& d) {
            char buf[64];
            std::snprintf(buf, sizeof buf, "%04d-%02d-%02d", d.year, d.month, d.day);
            Report(self, *op, std::string(Tr("date: ", "日期：")) + buf);
            });
        dp2.SetOnValueCommitted([op](Kit::Widget& self, const Kit::DateTime& d) {
            char buf[80];
            std::snprintf(buf, sizeof buf, "%04d-%02d-%02d %02d:%02d:%02d",
                d.year, d.month, d.day, d.hour, d.minute, d.second);
            Report(self, *op, std::string(Tr("date+time: ", "日期时间：")) + buf);
            });
        Cap(c, "Calendar popup plus time; the second one has seconds",
            "日历弹板 + 时分（第二个带秒）");
    }
    {
        Kit::Card& c = NewCard(r2, "Time Picker", "时间选择", "ZeTimePicker.h \u00B7 256");
        Kit::Label& out = Readout(c);
        Kit::Label* op = &out;

        Kit::HBoxLayout& a = Row(c, 10.0f);
        Kit::TimePicker& t1 = a.AddChild(Kit::TimePicker());
        t1.SetTime(14, 30, 0);
        t1.SetSize(120.0f, 30.0f);
        Cap(a, "Hour / minute / second within a day", "一天之内：时 / 分 / 秒");

        Kit::HBoxLayout& b = Row(c, 10.0f);
        Kit::TimePicker& t2 = b.AddChild(Kit::TimePicker());
        t2.SetWithSeconds(true);
        t2.SetMinuteStep(5);
        t2.SetSize(150.0f, 30.0f);
        // Capture the picker itself: the address of a child is stable inside
        // the tree, unlike `this`.
        Kit::TimePicker* picker = &t2;
        Kit::Button& now = Btn(b, "Now", "现在", BtnTone::Normal, 60.0f, 28.0f);
        now.SetOnClick([picker, op](Kit::Widget& self) {
            (void)picker->SetNow();
            Report(self, *op, Tr("Now() - jumped to the current time", "Now() —— 跳到当前时间"));
            });
        now.SetTooltip(Tr("SetNow() jumps to the current time", "SetNow() —— 跳到当前时间"));

        t1.SetOnTimeChanged([op](Kit::Widget& self, int secs) {
            char buf[48];
            std::snprintf(buf, sizeof buf, "%02d:%02d:%02d",
                secs / 3600, (secs % 3600) / 60, secs % 60);
            Report(self, *op, std::string(Tr("time: ", "时间：")) + buf);
            });
    }
    {
        Kit::Card& c = NewCard(r2, "Color Picker", "颜色选择", "ZeColorPicker.h \u00B7 266");
        Kit::Label& out = Readout(c);
        Kit::Label* op = &out;

        Kit::ColorPicker& cp = c.AddChild(Kit::ColorPicker());
        cp.SetColor(kSwatchPrimary);
        cp.SetPresets({ kSwatchPrimary, kSwatchSuccess, kSwatchDanger, kSwatchGold });
        cp.SetSize(0.0f, 30.0f);

        Kit::HBoxLayout& a = Row(c, 6.0f);
        const Color4f swatches[] = { kSwatchPrimary, kSwatchSuccess, kSwatchDanger, kSwatchGold };
        for (Color4f s : swatches) {
            Kit::Panel& p = a.AddChild(MakeTile("", s, kOnSwatch, 4.0f));
            p.SetSize(18.0f, 18.0f);
        }
        Cap(a, "#3B6EF6 and presets", "#3B6EF6 · 预设色板");

        cp.SetOnColorChanged([op](Kit::Widget& self, const Color4f& col) {
            Report(self, *op, std::string(Tr("colour: ", "颜色：")) + Hex(col));
            });
    }
}

// ===========================================================================
//  Tab 4 — containers and navigation
// ===========================================================================

void Demo::BuildContainerPage(Kit::Panel& page) {
    Kit::VBoxLayout& content = Scrolled(page);

    Kit::HBoxLayout& r1 = Row(content, 14.0f);

    {
        Kit::Card& c = NewCard(r1, "Collapse", "折叠面板", "ZeCollapse.h \u00B7 253");
        Kit::Label& out = Readout(c);
        Kit::Label* op = &out;

        Kit::Collapse& cl = c.AddChild(Kit::Collapse());
        if (Kit::Panel* p0 = cl.AddItem(Tr("Renderer settings", "渲染后端配置"))) {
            Kit::Label& b0 = p0->AddChild(RoleLabel(Tr(
                "Vulkan, OpenGL or D3D12 - fixed at compile time.",
                "Vulkan / OpenGL / D3D12 四选一，编译期限定。"), Ink::Muted, 12));
            Loc(b0, "Vulkan, OpenGL or D3D12 - fixed at compile time.",
                "Vulkan / OpenGL / D3D12 四选一，编译期限定。");
        }
        if (Kit::Panel* p1 = cl.AddItem(Tr("Window properties", "窗口属性"))) {
            Kit::Label& b1 = p1->AddChild(RoleLabel(Tr(
                "Size, DPI scaling, cursor shape, safe area.",
                "尺寸、DPI 缩放、光标样式、安全区。"), Ink::Muted, 12));
            Loc(b1, "Size, DPI scaling, cursor shape, safe area.",
                "尺寸、DPI 缩放、光标样式、安全区。");
        }
        cl.SetItemExpanded(0, true);
        cl.SetMinHeight(130.0f);

        cl.SetOnItemToggled([op](Kit::Widget& self, int i, bool open) {
            const std::string title = Kit::As<Kit::Collapse>(self).itemTitle(i);
            Report(self, *op, std::string(open ? Tr("opened: ", "展开：") : Tr("closed: ", "收起：")) + title);
            });

        // Collapse keeps its titles in an item list, not in child widgets.
        Kit::Collapse* collapse = &cl;
        OnLang([collapse] {
            collapse->SetItemTitle(0, Tr("Renderer settings", "渲染后端配置"));
            collapse->SetItemTitle(1, Tr("Window properties", "窗口属性"));
            });
    }
    {
        Kit::Card& c = NewCard(r1, "List View", "列表", "ZeListView.h \u00B7 187");
        Kit::Label& out = Readout(c);
        Kit::Label* op = &out;

        Kit::ListView& lv = c.AddChild(Kit::ListView());
        lv.SetItems({ "ZeButton.cpp \u00B7 353", "ZeCheckbox.cpp \u00B7 377",
                     "ZeSlider.cpp \u00B7 591", "ZeProgressBar.cpp \u00B7 604" });
        lv.SetSelectedIndex(0);
        lv.SetItemHeight(28.0f);
        lv.SetMinHeight(130.0f);
        lv.SetOnSelectionChanged([op](Kit::Widget& self, int) {
            Report(self, *op, std::string(Tr("row: ", "选中行："))
                + Kit::As<Kit::ListView>(self).selectedText());
            });
    }
    {
        Kit::Card& c = NewCard(r1, "Tree View", "树形控件", "ZeTreeView.h \u00B7 242");
        Kit::Label& out = Readout(c);
        Kit::Label* op = &out;

        Kit::TreeView& tv = c.AddChild(Kit::TreeView());
        const int root = tv.AddRoot("Zekit");
        const int inc = tv.AddNode(root, "include");
        tv.AddNode(inc, "Zekit.h");
        tv.AddNode(inc, "ZeWidget.h");
        const int src = tv.AddNode(root, "src");
        tv.AddNode(src, "Zekit.cpp");
        tv.AddNode(src, "ZeWidget.cpp");
        tv.SetExpanded(root, true);
        tv.SetExpanded(inc, true);
        tv.SetSelectedNode(inc);
        tv.SetMinHeight(130.0f);
        tv.SetOnNodeSelected([op](Kit::Widget& self, int) {
            Report(self, *op, std::string(Tr("node: ", "节点："))
                + Kit::As<Kit::TreeView>(self).selectedText());
            });
    }

    // ---- TableView (full width) ----
    Kit::HBoxLayout& r2 = Row(content, 14.0f);
    {
        Kit::Card& c = NewCard(r2, "Table View", "表格", "ZeTableView.h \u00B7 291");
        Kit::Label& out = Readout(c);
        Kit::Label* op = &out;

        Kit::TableView& tb = c.AddChild(Kit::TableView());
        tb.SetColumns({
            { Tr("Widget", "控件"), 160.0f },
            { Tr("Header", "头文件"), 220.0f },
            { Tr("Lines", "行数"), 90.0f },
            { Tr("State", "状态"), 120.0f } });
        tb.SetRows({
            { "Button",       "ZeButton.h",       "276", Tr("Ready", "就绪") },
            { "ProgressBar",  "ZeProgressBar.h",  "418", Tr("Draggable", "可拖动") },
            { "ToastNotifier","ZeToastNotifier.h","340", Tr("Overlay", "浮层") },
            { "TableView",    "ZeTableView.h",    "291", Tr("Read-only", "只读") },
            });
        tb.SetRowHeight(30.0f);
        tb.SetSelectedRow(1);
        tb.SetOnRowSelected([op](Kit::Widget& self, int row) {
            const auto& cells = Kit::As<Kit::TableView>(self).rowAt(static_cast<std::size_t>(row));
            const std::string line = cells.empty() ? std::string("-") : cells[0];
            Report(self, *op, std::string(Tr("row ", "第 ")) + std::to_string(row) + ": " + line);
            });
        Kit::TableView* table = &tb;
        OnLang([table] {
            table->SetColumns({
                { Tr("Widget", "控件"), 160.0f },
                { Tr("Header", "头文件"), 220.0f },
                { Tr("Lines", "行数"), 90.0f },
                { Tr("State", "状态"), 120.0f } });
            table->SetRows({
                { "Button",       "ZeButton.h",       "276", Tr("Ready", "就绪") },
                { "ProgressBar",  "ZeProgressBar.h",  "418", Tr("Draggable", "可拖动") },
                { "ToastNotifier","ZeToastNotifier.h","340", Tr("Overlay", "浮层") },
                { "TableView",    "ZeTableView.h",    "291", Tr("Read-only", "只读") } });
            });
        tb.SetMinHeight(180.0f);
    }

    // ---- Steps (full width: four steps do not fit in a third of the row) ----
    Kit::HBoxLayout& r2b = Row(content, 14.0f);
    {
        Kit::Card& c = NewCard(r2b, "Steps", "步骤条", "ZeSteps.h \u00B7 149");
        Kit::Label& out = Readout(c);
        Kit::Label* op = &out;

        Kit::Steps& st = c.AddChild(Kit::Steps());
        if (g_lang == Lang::Zh)
            st.SetSteps({ "配置", "编译", "链接", "发布" });
        else
            st.SetSteps({ "Configure", "Compile", "Link", "Release" });
        st.SetCurrentStep(2);
        st.SetSize(0.0f, 64.0f);
        st.SetOnStepChanged([op](Kit::Widget& self, int i) {
            Report(self, *op, std::string(Tr("step ", "第 ")) + std::to_string(i + 1) + " / 4");
            });
        Kit::Steps* steps = &st;
        OnLang([steps] {
            if (g_lang == Lang::Zh) steps->SetSteps({ "配置", "编译", "链接", "发布" });
            else steps->SetSteps({ "Configure", "Compile", "Link", "Release" });
            });
    }

    Kit::HBoxLayout& r3 = Row(content, 14.0f);

    {
        Kit::Card& c = NewCard(r3, "Breadcrumb", "面包屑", "ZeBreadcrumb.h \u00B7 152");
        Kit::Label& out = Readout(c);
        Kit::Label* op = &out;

        Kit::Breadcrumb& bc = c.AddChild(Kit::Breadcrumb());
        if (g_lang == Lang::Zh)
            bc.SetCrumbs({ "首页", "文档", "控件", "按钮" });
        else
            bc.SetCrumbs({ "Home", "Docs", "Controls", "Button" });
        bc.SetCurrentCrumb(3);
        bc.SetSize(0.0f, 26.0f);
        bc.SetOnNavigate([op](Kit::Widget& self, int index) {
            const std::string which = (g_lang == Lang::Zh)
                ? std::string("第 ") + std::to_string(index + 1) + " 段"
                : std::string("segment ") + std::to_string(index + 1);
            Report(self, *op, std::string(Tr("navigate to ", "跳转到 ")) + which);
            });
        Kit::Breadcrumb* crumb = &bc;
        OnLang([crumb] {
            if (g_lang == Lang::Zh) crumb->SetCrumbs({ "首页", "文档", "控件", "按钮" });
            else crumb->SetCrumbs({ "Home", "Docs", "Controls", "Button" });
            crumb->SetCurrentCrumb(3);
            });
        Cap(c, "Hierarchical path; click any segment", "层级路径导航 · 点任意段触发回调");
        // Known library issue: Breadcrumb does not override ApplyTheme like
        // Sidebar / Pagination do, so the current crumb still paints the solid
        // itemActive block on top of primary-coloured text. The fix is the same
        // as the 2026-10-27 pair: override ApplyTheme in Breadcrumb and drop the
        // row background to alpha ~0.14.
    }
    {
        Kit::Card& c = NewCard(r3, "Pagination", "分页", "ZePagination.h \u00B7 154");
        Kit::Label& out = Readout(c);
        Kit::Label* op = &out;

        Kit::Pagination& pg = c.AddChild(Kit::Pagination());
        pg.SetPageCount(12);
        pg.SetCurrentPage(1);
        pg.SetMaxButtons(5);
        pg.SetSize(0.0f, 30.0f);
        pg.SetOnPageChanged([op](Kit::Widget& self, int page) {
            Report(self, *op, std::string(Tr("page ", "第 ")) + std::to_string(page) + " / 12");
            });
        Cap(c, "Pages, arrows and ellipsis collapse", "页码 + 箭头 + 超长折叠");
    }
}

// ===========================================================================
//  Tab 5 — overlays and dialogs
// ===========================================================================

void Demo::BuildOverlayPage(Kit::Panel& page) {
    Kit::VBoxLayout& content = Scrolled(page);

    Kit::HBoxLayout& r1 = Row(content, 14.0f);

    {
        Kit::Card& c = NewCard(r1, "Tooltip", "工具提示", "ZeTooltip.h \u00B7 388");
        Kit::HBoxLayout& a = Row(c, 16.0f);
        Btn(a, "Hover me", "悬停我", BtnTone::Normal, 84.0f).SetTooltip(
            Tr("This is a hover tooltip", "这是一个悬停提示"));
        Btn(a, "Primary pill", "上方气泡", BtnTone::Primary, 90.0f).SetTooltip(
            Tr("Custom delay and placement supported", "支持自定义延迟与位置"));
        Cap(c, "Avoids window edges; delay plus fade-in", "自动避让窗口边缘 · 延迟 + 淡入");
    }
    {
        Kit::Card& c = NewCard(r1, "Toast", "轻提示", "ZeToast.h \u00B7 330");
        Kit::HBoxLayout& a = Row(c, 6.0f);
        Btn(a, "Success", "成功", BtnTone::Normal, 66.0f).SetOnClick([](Kit::Widget& self) {
            PopToast(self, Tr("Saved", "保存成功"), Kit::ToastLevel::Success);
            });
        Btn(a, "Warning", "警告", BtnTone::Normal, 66.0f).SetOnClick([](Kit::Widget& self) {
            PopToast(self, Tr("Low disk space", "磁盘空间不足"), Kit::ToastLevel::Warning);
            });
        Btn(a, "Error", "错误", BtnTone::Normal, 60.0f).SetOnClick([](Kit::Widget& self) {
            PopToast(self, Tr("Build failed", "编译失败"), Kit::ToastLevel::Error);
            });
        Btn(a, "Info", "信息", BtnTone::Normal, 56.0f).SetOnClick([](Kit::Widget& self) {
            PopToast(self, Tr("Loading…", "正在加载…"), Kit::ToastLevel::Info);
            });
        Cap(c, "Overlay strip, auto dismiss, fade in and out", "浮层条 + 自动消失 + 淡入淡出");
    }
    {
        Kit::Card& c = NewCard(r1, "Modal Dialog", "模态框", "ZeModalDialog.h \u00B7 338");
        Kit::HBoxLayout& a = Row(c, 8.0f);
        Btn(a, "Open dialog", "打开模态框", BtnTone::Primary, 108.0f).SetOnClick(
            [](Kit::Widget& self) {
                Kit::Window* w = self.window();
                if (!w) return;
                Kit::ModalDialog::Options opt;
                opt.title = Tr("Confirm", "确认操作");
                opt.width = 380.0f;
                opt.height = 190.0f;
                Kit::ModalDialog dlg = Kit::ModalDialog::Show(*w, opt);
                Kit::Label& t = dlg.AddWidget(Kit::Label(
                    Tr("Rebuild Zekit.lib now?", "确定要重新构建 Zekit.lib 吗？")));
                t.SetSize(0.0f, 24.0f);
                Kit::Label& d = dlg.AddWidget(RoleLabel(
                    Tr("The current artifact is older than the source tree.",
                       "当前产物落后于源码树。"), Ink::Muted, 12));
                d.SetSize(0.0f, 22.0f);
                // The dialog content root is a vertical panel, so the button row
                // has to be an explicit horizontal layout, right aligned.
                Kit::HBoxLayout& foot = dlg.AddWidget(Kit::HBoxLayout());
                foot.SetGap(0.0f, 8.0f);
                foot.SetJustify(Kit::JustifyContent::End);
                foot.SetAlign(Kit::AlignItems::Center);
                foot.SetMinHeight(34.0f);
                Btn(foot, "Cancel", "取消", BtnTone::Normal, 80.0f);
                Btn(foot, "OK", "确定", BtnTone::Primary, 80.0f);
            });
        Cap(c, "A real Win32 window, used like any other widget",
            "真的是一扇 Win32 窗口，像控件一样调用");
    }

    Kit::HBoxLayout& r2 = Row(content, 14.0f);

    {
        Kit::Card& c = NewCard(r2, "Drawer", "抽屉", "ZeDrawer.h \u00B7 259");
        Btn(Row(c, 8.0f), "Slide in from right", "从右侧滑出", BtnTone::Normal, 140.0f)
            .SetOnClick([](Kit::Widget& self) {
                Kit::Window* w = self.window();
                if (!w) return;
                static Kit::Drawer* s_drawer = nullptr;
                if (!s_drawer) {
                    s_drawer = &w->AddWidget(Kit::Drawer());
                    s_drawer->SetSide(Kit::DrawerSide::Right);
                    s_drawer->SetPanelSize(290.0f);
                    s_drawer->SetTitle(Tr("Drawer", "抽屉面板"));
                    Kit::Panel& body = *s_drawer->bodyPanel();
                    body.AddChild(RoleLabel(Tr(
                        "A floating panel that slides in from any edge, with scrim, "
                        "Esc to close and a configurable width.",
                        "从某条边滑出的浮层面板，支持遮罩、ESC 关闭与自定义宽度。"), Ink::Muted, 12));
                    Kit::Checkbox& k1 = body.AddChild(Kit::Checkbox(Tr("Enable scrim", "启用遮罩"), true));
                    k1.SetSize(0.0f, 22.0f);
                    Kit::Checkbox& k2 = body.AddChild(Kit::Checkbox(Tr("Close on outside click", "点击外部关闭"), false));
                    k2.SetSize(0.0f, 22.0f);
                    Kit::Checkbox& k3 = body.AddChild(Kit::Checkbox(Tr("Close on Esc", "按 ESC 关闭"), true));
                    k3.SetSize(0.0f, 22.0f);
                }
                s_drawer->Open();
            });
        Cap(c, "Floating panel, any of the four edges", "浮层面板，支持四条边");
    }
    {
        Kit::Card& c = NewCard(r2, "Menu Modal", "菜单模态", "ZeMenuModal.h \u00B7 465");
        Kit::Label& out = Readout(c);
        Kit::Label* op = &out;

        Kit::Button& ctx = Btn(Row(c, 8.0f), "Context menu (modal)", "右键菜单（模态）",
            BtnTone::Normal, 160.0f);
        ctx.SetTooltip(Tr("The File and View menus on the menu bar use the same control",
            "菜单栏里点『文件 / 视图』就能看到同一款下拉"));
        ctx.SetOnClick([op](Kit::Widget& self) {
                Kit::Window* w = self.window();
                if (!w) return;
                // One window-level instance, created on first use: the modal
                // lives above the whole tree, not inside this card.
                static Kit::MenuModal* s_menu = nullptr;
                if (!s_menu) {
                    s_menu = &w->AddWidget(Kit::MenuModal());
                    // Each item writes its own name into the status bar: the
                    // modal is the sender, and Activity() resolves the bar by
                    // object name, so nothing dangles.
                    s_menu->SetItems({
                        { .text = Tr("Rename", "重命名"), .shortcut = "F2",
                          .action = [](Kit::Widget& s) { Activity(s, "menu: rename"); } },
                        { .text = Tr("Duplicate", "创建副本"), .shortcut = "Ctrl+D",
                          .action = [](Kit::Widget& s) { Activity(s, "menu: duplicate"); } },
                        { .separator = true },
                        { .text = Tr("Delete", "删除"), .shortcut = "Del",
                          .action = [](Kit::Widget& s) { Activity(s, "menu: delete"); } },
                        });
                }
                s_menu->OpenAtCursor();
                Report(self, *op, Tr("menu opened at the cursor", "已在光标处打开菜单"));
            });
        Cap(c, "Blocks input for the whole window while open", "打开期间挡住整窗输入");
    }
    {
        Kit::Card& c = NewCard(r2, "Toast Notifier", "通知中心", "ZeToastNotifier.h \u00B7 340");
        Kit::HBoxLayout& a = Row(c, 8.0f);
        Btn(a, "Build done", "构建完成", BtnTone::Primary, 96.0f).SetOnClick(
            [](Kit::Widget& self) {
                Kit::Window* w = self.window();
                if (!w) return;
                static std::unique_ptr<Kit::ToastNotifier> s_notifier;
                if (!s_notifier) s_notifier = std::make_unique<Kit::ToastNotifier>(*w);
                s_notifier->ShowSuccess(Tr("Build finished - 80,644 lines", "构建完成 · 80,644 行"));
            });
        Btn(a, "Switch API", "切后端", BtnTone::Normal, 88.0f).SetOnClick(
            [](Kit::Widget& self) {
                Kit::Window* w = self.window();
                if (!w) return;
                static std::unique_ptr<Kit::ToastNotifier> s_notifier;
                if (!s_notifier) s_notifier = std::make_unique<Kit::ToastNotifier>(*w);
                s_notifier->ShowInfo(Tr("Renderer switched to Vulkan", "渲染后端已切换到 Vulkan"));
            });
        Cap(c, "Its own window, stacking notifications", "独立窗口渲染一叠提示条");
    }
}

// ===========================================================================
//  Tab 6 — charts and layout
// ===========================================================================

void Demo::BuildChartPage(Kit::Panel& page) {
    Kit::VBoxLayout& content = Scrolled(page);

    // ---- PlotView (full width) ----
    Kit::HBoxLayout& r1 = Row(content, 14.0f);
    {
        Kit::Card& c = NewCard(r1, "Plot View", "图表", "ZePlotView.h \u00B7 240");
        // Vertices are data: they do not follow the theme by themselves, so
        // TonePlot re-uploads them inside ApplyTheme.
        TonePlot& plot = c.AddChild(TonePlot());
        plot.SetSize(0.0f, 168.0f);
        TonePlot* plotPtr = &plot;
        OnLang([plotPtr] { plotPtr->RefreshText(); });
    }

    // ---- Layout illustration: VBox/HBox and Grid/Flow ----
    Kit::HBoxLayout& r2 = Row(content, 14.0f);

    {
        Kit::Card& c = NewCard(r2, "Layout \u00B7 VBox / HBox", "布局 · VBox / HBox",
            "ZeVBoxLayout.h \u00B7 ZeHBoxLayout.h");
        Kit::VBoxLayout& v = Col(c, 6.0f);
        static const char* const childEn[] = { "Child A", "Child B", "Child C" };
        static const char* const childZh[] = { "子控件 A", "子控件 B", "子控件 C" };
        for (int i = 0; i < 3; ++i) {
            Kit::Panel& t = v.AddChild(ToneTile(childEn[i], childZh[i], TileTone::WeakPrimary));
            t.SetSize(0.0f, 22.0f);
        }
        Kit::HBoxLayout& h = Row(c, 6.0f);
        for (const char* s : { "A", "B", "C" }) {
            Kit::Panel& t = h.AddChild(ToneTile(s, TileTone::WeakPrimary));
            t.SetFlexGrow(1.0f);
            t.SetSize(0.0f, 26.0f);
        }
    }
    {
        Kit::Card& c = NewCard(r2, "Layout \u00B7 Grid / Flow", "布局 · Grid / Flow",
            "ZeGridLayout.h \u00B7 ZeFlowLayout.h");
        Kit::GridLayout& g = c.AddChild(Kit::GridLayout());
        g.SetColumns(3);
        g.SetGap(6.0f, 6.0f);
        g.SetMinHeight(74.0f);
        for (int i = 1; i <= 6; ++i) {
            Kit::Panel& t = g.AddChild(ToneTile(std::to_string(i), TileTone::WeakPrimary));
            t.SetSize(0.0f, 34.0f);
        }
        Kit::FlowLayout& f = c.AddChild(Kit::FlowLayout());
        f.SetGap(6.0f, 6.0f);
        f.SetMinHeight(56.0f);
        static const char* const tagEn[] = { "tag", "flow", "wraps", "layout" };
        static const char* const tagZh[] = { "标签", "流式", "自动换行", "布局" };
        for (int i = 0; i < 4; ++i) {
            Kit::Panel& t = f.AddChild(ToneTile(tagEn[i], tagZh[i], TileTone::Soft));
            t.SetSize(56.0f, 24.0f);
        }
    }

    // ---- Splitter (full width) ----
    Kit::HBoxLayout& r3 = Row(content, 14.0f);
    {
        Kit::Card& c = NewCard(r3, "Splitter", "分隔面板", "ZeSplitter.h \u00B7 204", 148.0f);
        Kit::Splitter& sp = c.AddChild(Kit::Splitter(true));
        sp.SetFlexGrow(1.0f);
        sp.SetMinHeight(110.0f);
        Kit::Panel& left = sp.SetFirst(ToneTile("Pane A", "面板 A", TileTone::Sunken));
        left.SetMinWidth(80.0f);
        Kit::Panel& right = sp.SetSecond(ToneTile("Pane B", "面板 B", TileTone::Sunken));
        right.SetMinWidth(80.0f);
        sp.SetFirstRatio(0.38f);
        Cap(c, "Drag the handle to change the ratio", "拖动中间的分隔条改变比例");
    }

    // ---- ScrollView / DockPanel ----
    Kit::HBoxLayout& r4 = Row(content, 14.0f);

    {
        Kit::Card& c = NewCard(r4, "Scroll View", "滚动视图", "ZeScrollView.h \u00B7 237");
        Kit::Label& out = Readout(c);
        Kit::Label* op = &out;

        Kit::ScrollView& sv = c.AddChild(Kit::ScrollView());
        sv.SetScrollDirection(Kit::ScrollDirection::Vertical);
        sv.SetSize(0.0f, 130.0f);
        sv.SetOnScroll([op](Kit::Widget& self, float, float y) {
            char buf[96];
            std::snprintf(buf, sizeof buf, "scrollY %.0f / %.0f",
                static_cast<double>(y),
                static_cast<double>(Kit::As<Kit::ScrollView>(self).maxScrollY()));
            Report(self, *op, buf);
            });
        // Static literals: the registry keeps raw pointers for the process
        // lifetime, so they must not come from a temporary string.
        static const char* const rowEn[] = {
            "Row 1 of content", "Row 2 of content", "Row 3 of content", "Row 4 of content",
            "Row 5 of content", "Row 6 of content", "Row 7 of content", "Row 8 of content" };
        static const char* const rowZh[] = {
            "第 1 行内容", "第 2 行内容", "第 3 行内容", "第 4 行内容",
            "第 5 行内容", "第 6 行内容", "第 7 行内容", "第 8 行内容" };
        for (int i = 0; i < 8; ++i) {
            Kit::Label& l = sv.AddChild(RoleLabel(Tr(rowEn[i], rowZh[i]), Ink::Muted, 12));
            l.SetSize(0.0f, 24.0f);
            Loc(l, rowEn[i], rowZh[i]);
        }
        Cap(c, "Scroll offset, clip rect and scroll bar", "滚动偏移 + 裁剪域 + 滚动条");
    }
    {
        Kit::Card& c = NewCard(r4, "Dock Panel", "停靠面板", "ZeDockPanel.h \u00B7 552");
        Kit::Label& out = Readout(c);
        Kit::Label* op = &out;

        Kit::DockPanel& dock = c.AddChild(Kit::DockPanel());
        dock.SetMinHeight(130.0f);
        dock.SetFloatEnabled(false);
        dock.SetOnDockChanged([op](Kit::Widget& self) {
            Report(self, *op, std::string(Tr("panes: ", "面板数："))
                + std::to_string(Kit::As<Kit::DockPanel>(self).paneCount()));
            });
        Kit::Widget& center = dock.AddPane(Tr("Scene", "场景"),
            ToneTile("Center", "停靠区", TileTone::WeakPrimary), Kit::DockSide::Center);
        Kit::Widget& leftPane = dock.AddPane(Tr("Assets", "资源"),
            ToneTile("Pane", "面板", TileTone::Soft), Kit::DockSide::Left);
        Kit::Widget& rightPane = dock.AddPane(Tr("Properties", "属性"),
            ToneTile("Pane", "面板", TileTone::Soft), Kit::DockSide::Right);

        Kit::DockPanel* dockPanel = &dock;
        Kit::Widget* c0 = &center;
        Kit::Widget* c1 = &leftPane;
        Kit::Widget* c2 = &rightPane;
        OnLang([dockPanel, c0, c1, c2] {
            dockPanel->SetPaneTitle(*c0, Tr("Scene", "场景"));
            dockPanel->SetPaneTitle(*c1, Tr("Assets", "资源"));
            dockPanel->SetPaneTitle(*c2, Tr("Properties", "属性"));
            });
        Cap(c, "Drag to dock; can tear off into a floating window", "可拖拽停靠 · 可撕离成浮窗");
    }
}

// ===========================================================================
//  Tab 7 — the window itself
// ===========================================================================
//  The other six tabs showcase controls; this one showcases the *window*. It
//  is the same public Kit::Window surface the zew test panel drives, rebuilt
//  with this demo's own styling — no mock, no "display only": every button
//  calls the library, then reports what happened into the card readout and the
//  status bar.
//
//  Conventions this tab makes explicit:
//    · callbacks capture child pointers only — the Demo instance is moved into
//      the window by AddWidget, so `this` inside a callback is a hollow shell;
//    · anything the library exposes a getter for is read back from the window
//      (IsFullscreen / IsRoundedCorners / centerOnResize / childWindowCount),
//      never mirrored in a local flag;
//    · only getter-less state (title bar visibility, topmost, opacity) lives in
//      function-local statics, which survive the click and cannot dangle.

void Demo::BuildWindowPage(Kit::Panel& page) {
    Kit::VBoxLayout& content = Scrolled(page);

    // Geometry presets cycled by the Resize button.
    struct Geom { int w, h, x, y; };
    static const Geom kGeoms[] = {
        { 1200, 820, 140,  70 },
        { 1360, 900,  90,  40 },
        { 1000, 700, 260, 140 },
    };
    static int geomNo = 0;

    // Getter-less window state.
    static bool  titleBarOn = true, minBtnOn = true, maxBtnOn = true, closeBtnOn = true;
    static bool  borderlessOn = false, topmostOn = false;
    static float opacity = 1.0f;
    static int   titleNo = 0;

    // ---------------------------------------------------------------- row 1
    Kit::HBoxLayout& r1 = Row(content, 14.0f);
    r1.SetAlign(Kit::AlignItems::Stretch);   // equal-height cards per row

    {
        Kit::Card& c = NewCard(r1, "Geometry", "几何", "GetClientSize \u00B7 SetSize \u00B7 SetPosition");
        Kit::Label& out = Readout(c);
        Kit::Label* op = &out;

        Kit::HBoxLayout& a = Row(c, 6.0f);
        Btn(a, "Read", "读取", BtnTone::Primary, 74.0f, 28.0f).SetOnClick([op](Kit::Widget& self) {
            Kit::Window* w = self.window();
            if (!w) return;
            int cw = 0, ch = 0;
            w->GetClientSize(cw, ch);
            char buf[192];
            std::snprintf(buf, sizeof buf, "%d \u00D7 %d  \u00B7  win %d \u00D7 %d  \u00B7  %d%%",
                cw, ch, w->width(), w->height(),
                static_cast<int>(w->GetScaleFactor() * 100.0f + 0.5f));
            Report(self, *op, buf);
            });

        Btn(a, "Resize", "改尺寸", BtnTone::Normal, 84.0f, 28.0f).SetOnClick([op](Kit::Widget& self) {
            Kit::Window* w = self.window();
            if (!w) return;
            const Geom& g = kGeoms[geomNo % 3];
            ++geomNo;
            // SetSize keeps the window centred by default; SetPosition right
            // after it is what pins the corner instead (see the "centre on
            // resize" switch below for the two-step jump this avoids).
            w->SetSize(g.w, g.h);
            w->SetPosition(g.x, g.y);
            char buf[160];
            std::snprintf(buf, sizeof buf, "SetSize(%d,%d) \u2192 (%d,%d)", g.w, g.h, g.x, g.y);
            Report(self, *op, buf);
            });

        Btn(a, "Centre", "居中", BtnTone::Normal, 76.0f, 28.0f).SetOnClick([op](Kit::Widget& self) {
            Kit::Window* w = self.window();
            if (!w) return;
            w->CenterOnScreen();
            Report(self, *op, "CenterOnScreen()");
            });

        Kit::HBoxLayout& b = Row(c, 6.0f);
        Btn(b, "Min size", "最小尺寸", BtnTone::Normal, 88.0f, 28.0f).SetOnClick([op](Kit::Widget& self) {
            Kit::Window* w = self.window();
            if (!w) return;
            static bool on = false;
            on = !on;
            w->SetMinimumSize(on ? 720 : 0, on ? 520 : 0);
            char buf[160];
            std::snprintf(buf, sizeof buf, "SetMinimumSize(%d, %d)", on ? 720 : 0, on ? 520 : 0);
            Report(self, *op, buf);
            });

        Btn(b, "Max size", "最大尺寸", BtnTone::Normal, 88.0f, 28.0f).SetOnClick([op](Kit::Widget& self) {
            Kit::Window* w = self.window();
            if (!w) return;
            static bool on = false;
            on = !on;
            w->SetMaximumSize(on ? 1180 : 0, on ? 820 : 0);
            char buf[160];
            std::snprintf(buf, sizeof buf, "SetMaximumSize(%d, %d)", on ? 1180 : 0, on ? 820 : 0);
            Report(self, *op, buf);
            });

        Btn(b, "Resizable", "可拖拽", BtnTone::Normal, 92.0f, 28.0f).SetOnClick([op](Kit::Widget& self) {
            Kit::Window* w = self.window();
            if (!w) return;
            static bool on = true;
            on = !on;
            w->SetResizable(on);
            std::string msg = std::string("SetResizable(") + OnOff(on) + ")";
            Report(self, *op, msg);
            });
        Cap(c, "Read size, cycle three geometries", "读尺寸 · 三套几何循环");
    }

    {
        Kit::Card& c = NewCard(r1, "Window State", "显示状态", "Maximize \u00B7 Minimize \u00B7 Restore");
        Kit::Label& out = Readout(c);
        Kit::Label* op = &out;

        Kit::HBoxLayout& a = Row(c, 6.0f);
        Btn(a, "Maximize", "最大化", BtnTone::Normal, 88.0f, 28.0f).SetOnClick([op](Kit::Widget& self) {
            if (Kit::Window* w = self.window()) { w->Maximize(); Report(self, *op, "Maximize()"); }
            });
        Btn(a, "Minimize", "最小化", BtnTone::Normal, 88.0f, 28.0f).SetOnClick([op](Kit::Widget& self) {
            if (Kit::Window* w = self.window()) { Report(self, *op, "Minimize() \u2192 restore from the taskbar"); w->Minimize(); }
            });
        Btn(a, "Restore", "还原", BtnTone::Normal, 80.0f, 28.0f).SetOnClick([op](Kit::Widget& self) {
            if (Kit::Window* w = self.window()) { w->Restore(); Report(self, *op, "Restore()"); }
            });

        Kit::HBoxLayout& b = Row(c, 6.0f);
        Btn(b, "Topmost", "置顶", BtnTone::Normal, 80.0f, 28.0f).SetOnClick([op](Kit::Widget& self) {
            Kit::Window* w = self.window();
            if (!w) return;
            topmostOn = !topmostOn;
            w->SetTopmost(topmostOn);
            Report(self, *op, std::string("SetTopmost(") + OnOff(topmostOn) + ")");
            });
        Btn(b, "Redraw", "重绘一帧", BtnTone::Normal, 92.0f, 28.0f).SetOnClick([op](Kit::Widget& self) {
            if (Kit::Window* w = self.window()) { w->RequestRedraw(); Report(self, *op, "RequestRedraw()"); }
            });
        Cap(c, "Minimise goes to the taskbar", "最小化会收到任务栏");
    }

    {
        Kit::Card& c = NewCard(r1, "Opacity", "不透明度", "SetOpacity 0.2 \u2026 1.0");
        Kit::Label& out = Readout(c);
        Kit::Label* op = &out;

        Kit::HBoxLayout& a = Row(c, 6.0f);
        Btn(a, "\u2212 0.1", "− 0.1", BtnTone::Normal, 64.0f, 28.0f).SetOnClick([op](Kit::Widget& self) {
            Kit::Window* w = self.window();
            if (!w) return;
            opacity = (opacity - 0.1f < 0.2f) ? 0.2f : opacity - 0.1f;
            w->SetOpacity(opacity);
            char buf[96];
            std::snprintf(buf, sizeof buf, "SetOpacity(%.2f)", static_cast<double>(opacity));
            Report(self, *op, buf);
            });
        Btn(a, "+ 0.1", "+ 0.1", BtnTone::Normal, 64.0f, 28.0f).SetOnClick([op](Kit::Widget& self) {
            Kit::Window* w = self.window();
            if (!w) return;
            opacity = (opacity + 0.1f > 1.0f) ? 1.0f : opacity + 0.1f;
            w->SetOpacity(opacity);
            char buf[96];
            std::snprintf(buf, sizeof buf, "SetOpacity(%.2f)", static_cast<double>(opacity));
            Report(self, *op, buf);
            });
        Btn(a, "Opaque", "不透明", BtnTone::Primary, 76.0f, 28.0f).SetOnClick([op](Kit::Widget& self) {
            Kit::Window* w = self.window();
            if (!w) return;
            opacity = 1.0f;
            w->SetOpacity(1.0f);
            Report(self, *op, "SetOpacity(1.00)");
            });
        Cap(c, "Clamped at 0.2 - lower is unclickable", "钳在 0.2 · 再低就点不着");
    }

    // ---------------------------------------------------------------- row 2
    Kit::HBoxLayout& r2 = Row(content, 14.0f);
    r2.SetAlign(Kit::AlignItems::Stretch);

    {
        Kit::Card& c = NewCard(r2, "System Chrome", "系统按钮", "SetTitleBarVisible \u00B7 \u2026ButtonEnabled");
        Kit::Label& out = Readout(c);
        Kit::Label* op = &out;

        Kit::HBoxLayout& a = Row(c, 6.0f);
        Btn(a, "Title bar", "标题栏", BtnTone::Normal, 84.0f, 28.0f).SetOnClick([op](Kit::Widget& self) {
            Kit::Window* w = self.window();
            if (!w) return;
            titleBarOn = !titleBarOn;
            w->SetTitleBarVisible(titleBarOn);
            Report(self, *op, std::string("TitleBarVisible(") + OnOff(titleBarOn) + ")");
            });
        Btn(a, "Min button", "最小化钮", BtnTone::Normal, 92.0f, 28.0f).SetOnClick([op](Kit::Widget& self) {
            Kit::Window* w = self.window();
            if (!w) return;
            minBtnOn = !minBtnOn;
            w->SetMinimizeButtonEnabled(minBtnOn);
            Report(self, *op, std::string("MinimizeButton(") + OnOff(minBtnOn) + ")");
            });
        Btn(a, "Max button", "最大化钮", BtnTone::Normal, 92.0f, 28.0f).SetOnClick([op](Kit::Widget& self) {
            Kit::Window* w = self.window();
            if (!w) return;
            maxBtnOn = !maxBtnOn;
            w->SetMaximizeButtonEnabled(maxBtnOn);
            Report(self, *op, std::string("MaximizeButton(") + OnOff(maxBtnOn) + ")");
            });

        Kit::HBoxLayout& b = Row(c, 6.0f);
        Btn(b, "Close button", "关闭钮", BtnTone::Danger, 100.0f, 28.0f).SetOnClick([op](Kit::Widget& self) {
            Kit::Window* w = self.window();
            if (!w) return;
            closeBtnOn = !closeBtnOn;
            w->SetCloseButtonEnabled(closeBtnOn);
            // Turning the ✕ off takes Alt+F4 and Close() with it, so say so.
            Report(self, *op, closeBtnOn
                ? std::string("CloseButton(on)")
                : std::string("CloseButton(off) \u2014 press again to re-enable"));
            });
        Cap(c, "The ✕ switch also kills Alt+F4 - click again", "关闭钮还会连带 Alt+F4 · 再点恢复");
    }

    {
        Kit::Card& c = NewCard(r2, "Frame", "外框形态", "SetBorderless \u00B7 SetFullscreen \u00B7 SetRoundedCorners");
        Kit::Label& out = Readout(c);
        Kit::Label* op = &out;

        Kit::HBoxLayout& a = Row(c, 6.0f);
        Btn(a, "Borderless", "无边框", BtnTone::Normal, 88.0f, 28.0f).SetOnClick([op](Kit::Widget& self) {
            Kit::Window* w = self.window();
            if (!w) return;
            borderlessOn = !borderlessOn;
            w->SetBorderless(borderlessOn);
            Report(self, *op, std::string("SetBorderless(") + OnOff(borderlessOn) + ")");
            });
        Btn(a, "Fullscreen", "全屏", BtnTone::Normal, 88.0f, 28.0f).SetOnClick([op](Kit::Widget& self) {
            Kit::Window* w = self.window();
            if (!w) return;
            const bool next = !w->IsFullscreen();
            w->SetFullscreen(next);
            Report(self, *op, std::string("SetFullscreen(") + OnOff(next) + ")");
            });
        Btn(a, "Rounded", "圆角", BtnTone::Normal, 80.0f, 28.0f).SetOnClick([op](Kit::Widget& self) {
            Kit::Window* w = self.window();
            if (!w) return;
            const bool next = !w->IsRoundedCorners();
            w->SetRoundedCorners(next);
            Report(self, *op, std::string("SetRoundedCorners(") + OnOff(next) + ")");
            });

        Kit::HBoxLayout& b = Row(c, 6.0f);
        Btn(b, "Centre on resize", "改尺寸居中", BtnTone::Normal, 116.0f, 28.0f).SetOnClick([op](Kit::Widget& self) {
            Kit::Window* w = self.window();
            if (!w) return;
            // Read the real state: the window is the only owner of this flag.
            const bool next = !w->centerOnResize();
            w->SetCenterOnResize(next);
            Report(self, *op, std::string("CenterOnResize(") + OnOff(next) + ")");
            });
        Cap(c, "Fullscreen covers the taskbar", "全屏连任务栏一起盖住");
    }

    {
        Kit::Card& c = NewCard(r2, "Title & Mode", "标题与驱动", "SetTitle \u00B7 driveMode \u00B7 frameRate");
        Kit::Label& out = Readout(c);
        Kit::Label* op = &out;

        static const char* const kTitles[][2] = {
            { "Zekit Control Gallery \u00B7 1.0.0", "Zekit 控件全览 \u00B7 1.0.0" },
            { "Zekit \u00B7 every widget is live", "Zekit \u00B7 每个控件都能交互" },
            { "Zekit \u00B7 C++20 UI kit", "Zekit \u00B7 C++20 界面库" },
        };

        Kit::HBoxLayout& a = Row(c, 6.0f);
        Btn(a, "Cycle title", "换标题", BtnTone::Normal, 92.0f, 28.0f).SetOnClick([op](Kit::Widget& self) {
            Kit::Window* w = self.window();
            if (!w) return;
            const char* t = kTitles[titleNo % 3][g_lang == Lang::Zh ? 1 : 0];
            ++titleNo;
            w->SetTitle(t);
            Report(self, *op, t);
            });
        Btn(a, "Query mode", "查询驱动", BtnTone::Normal, 100.0f, 28.0f).SetOnClick([op](Kit::Widget& self) {
            Kit::Window* w = self.window();
            if (!w) return;
            const char* mode = (w->driveMode() == Kit::Window::DriveMode::EventDriven) ? "EventDriven"
                : (w->driveMode() == Kit::Window::DriveMode::OnDemand) ? "OnDemand" : "Immediate";
            char buf[160];
            std::snprintf(buf, sizeof buf, "%s  \u00B7  %d fps  \u00B7  %s",
                mode, w->frameRate(), w->isRenderEnabled() ? "render on" : "render off");
            Report(self, *op, buf);
            });
        Cap(c, "Drive mode is fixed at exec()", "驱动模式在 exec() 入口定格");
    }

    // ---------------------------------------------------------------- row 3
    Kit::HBoxLayout& r3 = Row(content, 14.0f);
    r3.SetAlign(Kit::AlignItems::Stretch);

    {
        Kit::Card& c = NewCard(r3, "Theme", "主题与深色", "SetTheme \u00B7 SetDarkMode \u00B7 FollowSystem");
        Kit::Label& out = Readout(c);
        Kit::Label* op = &out;

        Kit::HBoxLayout& a = Row(c, 6.0f);
        Btn(a, "Demo theme", "Demo 主题", BtnTone::Primary, 100.0f, 28.0f).SetOnClick([](Kit::Widget& self) {
            if (Kit::Window* w = self.window())
                if (Demo* d = w->FindWidget<Demo>("demo")) d->ToggleTheme();
            });
        Btn(a, "Query", "查询", BtnTone::Normal, 70.0f, 28.0f).SetOnClick([op](Kit::Widget& self) {
            Kit::Window* w = self.window();
            if (!w) return;
            char buf[200];
            std::snprintf(buf, sizeof buf, "%s %s  \u00B7  %s %s  \u00B7  %s %s",
                Tr("follow", "跟随"),
                w->isFollowingSystemDarkMode() ? Tr("on", "开") : Tr("off", "关"),
                Tr("window dark", "窗口深色"),
                w->isDarkMode() ? Tr("yes", "是") : Tr("no", "否"),
                Tr("system dark", "系统深色"),
                Kit::Window::SystemPrefersDarkMode() ? Tr("yes", "是") : Tr("no", "否"));
            Report(self, *op, buf);
            });

        Kit::HBoxLayout& b = Row(c, 6.0f);
        Btn(b, "Follow system", "跟随系统", BtnTone::Normal, 104.0f, 28.0f).SetOnClick([op](Kit::Widget& self) {
            Kit::Window* w = self.window();
            if (!w) return;
            // Read the real state: the library default is "following", so the
            // first click turns it OFF — a blind toggle without this hint reads
            // as "clicked, nothing happened".
            const bool next = !w->isFollowingSystemDarkMode();
            w->SetFollowSystemDarkMode(next);
            Report(self, *op, next
                ? std::string("FollowSystem(on) \u2014 the OS decides again")
                : std::string("FollowSystem(off) \u2014 SetTheme / SetDarkMode decide"));
            });
        Btn(b, "Dark mode", "深色模式", BtnTone::Normal, 100.0f, 28.0f).SetOnClick([op](Kit::Widget& self) {
            Kit::Window* w = self.window();
            if (!w) return;
            // SetDarkMode recolours canvas + controls + native title bar from the
            // theme's polarity (Themes::Recolor keeps the accent hue).
            const bool next = !w->isDarkMode();
            w->SetDarkMode(next);
            Report(self, *op, std::string("SetDarkMode(") + OnOff(next) + ")");
            });
        Cap(c, "Flips canvas, controls and title bar", "同时翻画布、控件与标题栏");
    }

    {
        Kit::Card& c = NewCard(r3, "Child Windows", "子窗口", "CreateChildWindow \u00B7 DestroyChildWindow");
        Kit::Label& out = Readout(c);
        Kit::Label* op = &out;

        Kit::HBoxLayout& a = Row(c, 6.0f);
        Btn(a, "Create", "新建", BtnTone::Primary, 80.0f, 28.0f).SetOnClick([](Kit::Widget& self) {
            CreateDemoChildWindow(self);
            });
        Btn(a, "Close all", "全部关闭", BtnTone::Danger, 96.0f, 28.0f).SetOnClick([](Kit::Widget& self) {
            CloseDemoChildWindows(self);
            });
        Btn(a, "Count", "计数", BtnTone::Normal, 72.0f, 28.0f).SetOnClick([op](Kit::Widget& self) {
            Kit::Window* w = self.window();
            if (!w) return;
            char buf[128];
            std::snprintf(buf, sizeof buf, "%s %d  \u00B7  %s %s",
                Tr("children", "子窗口"), w->childWindowCount(),
                Tr("native", "原生能力"),
                w->Supports(Kit::Capability::ChildWindow) ? Tr("yes", "支持") : Tr("no", "不支持"));
            Report(self, *op, buf);
            });
        Cap(c, "One exec() drives every window", "一个 exec() 驱动全部窗口");
    }

    {
        Kit::Card& c = NewCard(r3, "Diagnostics", "诊断", "hwnd \u00B7 capabilities \u00B7 client origin");
        Kit::Label& out = Readout(c);
        Kit::Label* op = &out;

        Kit::HBoxLayout& a = Row(c, 6.0f);
        Btn(a, "Native handle", "原生句柄", BtnTone::Normal, 108.0f, 28.0f).SetOnClick([op](Kit::Widget& self) {
            Kit::Window* w = self.window();
            if (!w) return;
            char buf[128];
            std::snprintf(buf, sizeof buf, "hwnd %p", w->hwnd());
            Report(self, *op, buf);
            });
        Btn(a, "Client origin", "客户区原点", BtnTone::Normal, 108.0f, 28.0f).SetOnClick([op](Kit::Widget& self) {
            Kit::Window* w = self.window();
            if (!w) return;
            int ox = 0, oy = 0;
            w->GetClientOrigin(ox, oy);
            char buf[128];
            std::snprintf(buf, sizeof buf, "origin (%d, %d)", ox, oy);
            Report(self, *op, buf);
            });
        Btn(a, "Mouse", "鼠标位置", BtnTone::Normal, 92.0f, 28.0f).SetOnClick([op](Kit::Widget& self) {
            Kit::Window* w = self.window();
            if (!w) return;
            int mx = 0, my = 0;
            w->GetMousePosition(mx, my);
            char buf[128];
            std::snprintf(buf, sizeof buf, "mouse (%d, %d)  \u00B7  %s %s",
                mx, my, Tr("left down", "左键按下"),
                w->IsMouseButtonDown(Kit::MouseButton::Left) ? Tr("yes", "是") : Tr("no", "否"));
            Report(self, *op, buf);
            });
        Cap(c, "HWND on Windows, XID on X11", "Windows 是 HWND，X11 是 XID");
    }
}
