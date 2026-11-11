
#include "pch.h"

#include "ZePlatformWindow.h"

#if defined(_WIN32)

#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <iostream>

namespace {

	bool ConsoleRequestedByEnv() {
		static const bool on = [] {
			const char* v = std::getenv("ZEKIT_CONSOLE");
			return v && *v != '\0' && *v != '0';
			}();
		return on;
	}

	bool OwnsConsoleAlone() {
		DWORD owners[2] = { 0, 0 };
		return ::GetConsoleProcessList(owners, 2) == 1;
	}

	bool StdoutHasDestination() {
		const HANDLE h = ::GetStdHandle(STD_OUTPUT_HANDLE);
		return h != nullptr && h != INVALID_HANDLE_VALUE;
	}

	bool AttachedToAnyConsole() {
		DWORD owners[2] = { 0, 0 };
		return ::GetConsoleProcessList(owners, 2) > 0;
	}

	void WireStandardStreams() {
		std::FILE* f = nullptr;
		f = std::freopen("CONOUT$", "w", stdout); (void)f;
		f = std::freopen("CONOUT$", "w", stderr); (void)f;
		f = std::freopen("CONIN$", "r", stdin);   (void)f;

		::SetConsoleOutputCP(CP_UTF8);
		::SetConsoleCP(CP_UTF8);

		std::ios::sync_with_stdio(true);

	}

	void Note(const wchar_t* msg) {
		::OutputDebugStringW(msg);
	}

}

namespace Plat {

	bool HideConsole() {
		const HWND console = ::GetConsoleWindow();
		if (console == nullptr) return false;
		if (!OwnsConsoleAlone()) return false;

		::ShowWindow(console, SW_HIDE);
		Note(L"[Zekit] 已隐藏本进程独占的控制台窗口（设 ZEKIT_CONSOLE=1 可保留）。\n");
		return true;
	}

	bool ShowConsole() {
		if (const HWND console = ::GetConsoleWindow()) {
			::ShowWindow(console, SW_SHOW);
			if (!StdoutHasDestination()) WireStandardStreams();
			return true;
		}

		if (AttachedToAnyConsole()) {
			return false;
		}

		if (StdoutHasDestination()) return true;

		if (!::AllocConsole()) return false;
		WireStandardStreams();
		if (const HWND console = ::GetConsoleWindow())
			::SetWindowPos(console, HWND_BOTTOM, 0, 0, 0, 0,
				SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		Note(L"[Zekit] ZEKIT_CONSOLE=1：已为本进程分配控制台（stdout / stderr 已接入）。\n");
		return true;
	}

	void SetupEntryConsole() {
		if (ConsoleRequestedByEnv()) ShowConsole();
		else                         HideConsole();
	}

}

#else

namespace Plat {

	bool HideConsole() { return false; }
	bool ShowConsole() { return true; }
	void SetupEntryConsole() {}

}

#endif
