#include "Demo.h"

#include <Zekit.h>
#include <ZekitApp.h>
#include <ZeRenderBackend.h>
#include <ZeText.h>          // Kit::SetUiLanguage —— 库自己画的字（弹板按钮 / 拖放提示）

ZEKIT_APP_ENTRY(myApp) {
	Kit::Window win;

	win.UseRenderer(Rhi::DefaultApi());

	win.SetTitle("Zekit Control Gallery · 1.0.0");
	win.SetSize(1360, 900);
	// Theme comes from Demo::MakeTheme (a light and a dark variant;
	// switch them from the tool bar or the View menu).
	win.SetTheme(Demo::MakeTheme(false));

	// Strings that the library draws itself (picker buttons, drop-zone hints,
	// image-preview hints) follow the system locale by default (see ZeText.h).
	// The demo manages its own UI language instead, so align the library here --
	// otherwise the demo would be English while a date picker pops up Chinese.
	// Demo::ToggleLanguage re-syncs this when the tool-bar button is used.
	// 想验证"库自己跟随系统"的表现：把本行换成 SetUiLanguage(UiLanguage::Auto)
	// 或直接注释掉，中文系统上弹板就会显示中文。
	Kit::SetUiLanguage(Kit::UiLanguage::English);

	Demo app;
	Demo& ui = win.AddWidget(app);     // the copy that lives in the widget tree
	win.CenterOnScreen();
	win.Show();

	// The renderer backend is ready now: fill the status bar with the real
	// backend name / GPU / DPI (unavailable during construction).
	ui.RefreshRuntimeInfo();

	return win.exec();
}
