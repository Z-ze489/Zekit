#pragma once
#include <ZePanel.h>
#include <ZeTheme.h>   // Kit::Theme is returned by value

namespace Kit {
    class TabView;
    class Sidebar;
    class StatusBar;
    class MenuBar;
    class Button;
}

// Zekit control gallery.
//
// A rebuild of the "Zekit control overview" design sheet using Zekit's own
// widgets: menu bar, tool bar, [sidebar | tab view], status bar. Each tab is a
// scrollable grid of cards, one card per widget.
//
// This demo is meant to be the public face of Zekit, so the UI is bilingual.
// English is the default, and every label can be switched at runtime without
// rebuilding the widget tree.
//
// ── Not a style reference ─────────────────────────────────────────────────
// This file exists to showcase the *API*, not Zekit's own coding style.
// Zekit's sources follow a heavily documented house style that lives inside
// the library; nothing in Zekit expects your application to imitate it.
// Write your app in your project's own conventions — plain // comments like
// the ones here are perfectly fine. (See README.md.)
//
class Demo : public Kit::Panel {
public:
    Demo();

    // Fill the status bar with real runtime data (renderer / GPU / DPI).
    // Must be called after Window::Show() and before exec(): during
    // construction there is no window yet and no renderer backend.
    void RefreshRuntimeInfo();

    // Flip between the light and dark theme. Every widget recolours itself
    // through ApplyTheme, so this never touches the tree structure.
    void ToggleTheme();

    // Flip between English and Chinese. All texts live in a registry built at
    // construction time; switching walks it and re-sets the strings in place.
    void ToggleLanguage();

    bool isDark() const { return dark_; }

    // Light / dark palette as a full theme (tokens + derived component styles).
    // Static because main.cpp needs it before the Demo is attached to a window.
    static Kit::Theme MakeTheme(bool dark);

private:
    void BuildMenuBar();
    void BuildToolBar();
    void BuildBody();
    void BuildStatusBar();

    // One builder per tab.
    void BuildBasicPage(Kit::Panel& page);
    void BuildTextPage(Kit::Panel& page);
    void BuildPickerPage(Kit::Panel& page);
    void BuildContainerPage(Kit::Panel& page);
    void BuildOverlayPage(Kit::Panel& page);
    void BuildChartPage(Kit::Panel& page);

    // Tab 7: the window itself — geometry, chrome, state, theme, child windows.
    // Every button calls a real Kit::Window method and writes the outcome into
    // the card's readout line and the status bar.
    void BuildWindowPage(Kit::Panel& page);

    // Re-apply strings on widgets that are not plain text holders
    // (tab titles, sidebar items, status bar sections, menus).
    void RelocalizeComposite();

    Kit::TabView*   tabs_ = nullptr;      // addresses are stable inside the tree
    Kit::Sidebar*   side_ = nullptr;
    Kit::StatusBar* status_ = nullptr;
    Kit::MenuBar*   menu_ = nullptr;
    Kit::Button*    themeBtn_ = nullptr;
    Kit::Button*    langBtn_ = nullptr;
    bool dark_ = false;
};
