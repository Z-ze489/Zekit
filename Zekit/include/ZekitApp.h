#pragma once
// =====================================================================
//  ZekitApp.h -- write once, correct on every platform: the **application entry**
// =====================================================================
//
//  ## What problem it solves
//
//  Zekit's window/widget/event code is platform-independent to begin with; the only
//  platform difference an application cannot avoid is the **shape of the entry function**:
//
//      Desktop (Windows / Linux / macOS)   int main()
//      Android                          extern "C" void android_main(android_app*)
//      iOS / HarmonyOS                   launched by the host (HarmonyOS exports ZekitHarmonyAppMain)
//
//  This difference cannot be avoided (the entry symbols themselves differ), but it can
//  **appear only once**, and not in the application code -- that is what this header does.
//
//  ## Usage (one copy of the code for both ends)
//
//  @code
//  #include <Zekit.h>
//  #include <ZekitApp.h>
//
//  ZEKIT_APP_ENTRY(zewApp) {
//      Kit::Window win;
//      win.SetTitle("My App");
//      win.SetSize(1000, 780);
//
//      MyPanel panel;
//      win.AddWidget(panel);
//
//      win.Show();
//      return win.exec();
//  }
//  @endcode
//
//  * `ZEKIT_APP_ENTRY(name)` followed by a pair of braces is the entry function body;
//    **you can write `return` inside it**. `name` is just an internal function name
//    (only needs to be unique within one executable).
//
//  * On mobile, the macro expansion first calls `Kit::AttachPlatformHost()` to hand the
//    host handle given by the system to Zekit (on Android it is `android_app*`).
//
//  =====================================================================
//  ## Two entries on desktop -- the default is a **windowed program** (no console)
//  =====================================================================
//
//      ZEKIT_APP_ENTRY(name)          windowed program: **no console** on Windows
//      ZEKIT_APP_ENTRY_CONSOLE(name)  console program: has a console, stdout/stderr visible
//      hand-written int main()        no macro = today's behavior = console program
//
//  * **No console by default** is the sole purpose of this change: in a release build,
//    "a black window plus a window" is the last thing that should appear, and it used
//    to be the default (`int main()` means the console subsystem).
//
//  * But "no visible console" has a cost: the library's diagnostics (backend falling
//    back to OpenGL, `ZEKIT_TRACE_*` tracing) and the application's own `std::cout` /
//    `std::cerr` all lose their destination. So there are three ways to get the console
//    back, **from lightest to heaviest**:
//
//    1. **Launch from a terminal -- do nothing.**
//       A window-subsystem process does not attach a console, but the standard handles
//       are **inherited** from the parent process: `std::cout` / `std::cerr` still print
//       to that terminal (verified in practice, see `StdoutHasDestination` in
//       `ZeConsole.cpp`). The most common option during development.
//    2. **`ZEKIT_CONSOLE=1` environment variable** -- allocate a console window for
//       itself at startup. Works when launched from a debugger or by double-click,
//       with no code change and no relink:
//       @code
//       ZEKIT_CONSOLE=1  MyApp.exe           :: cmd
//       $env:ZEKIT_CONSOLE=1; .\MyApp.exe    # PowerShell
//       @endcode
//       See `Kit::SetupEntryConsole()`.
//    3. **Switch the entry to the console form** -- `ZEKIT_APP_ENTRY_CONSOLE(name)`, or
//       define `ZEKIT_APP_CONSOLE` for the whole project (the latter is the handiest
//       way to get "console in Debug, none in Release": add the define only to the
//       Debug configuration, change no code).
//
//  ## What each platform expands to
//
//  | Target | `ZEKIT_APP_ENTRY` | `ZEKIT_APP_ENTRY_CONSOLE` |
//  |---|---|---|
//  | Windows (MSVC / clang-cl) | `main()` + `/SUBSYSTEM:WINDOWS` + `/ENTRY:mainCRTStartup` | same shape, but `/SUBSYSTEM:CONSOLE` |
//  | Windows (MinGW etc.) | `main()`, runtime `SetupEntryConsole()` fallback (adding `-mwindows` is recommended) | `main()`, runtime `ShowConsole()` |
//  | Linux / macOS | `main()` | `main()` (already runs in a terminal there) |
//  | Android | `android_main()` (calls `AttachPlatformHost` first) | **the same** -- no console there, logs go to logcat |
//  | HarmonyOS | `ZekitHarmonyAppMain()` | **the same** -- logs go to HiLog |
//
//  * Why Windows uses "subsystem + entry point" and **not** a `WinMain`:
//    writing `WinMain` looks more orthodox, but it forces the public headers to know
//    `HINSTANCE` -- which means `#include <windows.h>` (this library's public headers
//    never do that), or declaring it as a `void*` version; and the latter, as soon as it
//    meets a translation unit that really includes `<windows.h>`, is C2733 ("cannot
//    overload functions with extern "C" linkage") -- both include orders were tested and
//    both fail. With link directives, the entry **is still `main`**, and the public
//    headers need not know a single platform type.
//
//  * Link directives (`.drectve`) were measured to **override** `/SUBSYSTEM` on the
//    project / command line (both directions verified: project set to CONSOLE + macro
//    asking for GUI -> GUI comes out; and vice versa). So "which macro is used" decides,
//    not the project settings -- which is exactly how "entry shape determined by code"
//    should behave.
//
//  ## Do not want the macro? (some projects ban macros)
//
//  The equivalent hand-written version, 3 lines per desktop target:
//
//  @code
//  // Desktop - windowed program (no console)
//  #pragma comment(linker, "/SUBSYSTEM:WINDOWS")
//  #pragma comment(linker, "/ENTRY:mainCRTStartup")   // entry is still main, no WinMain needed
//  int main() { ...your code... }
//
//  // Desktop - console program (default shape, add nothing)
//  int main() { ...your code... }
//
//  // Android (only this layer is extra, and it does not touch business code)
//  #if defined(__ANDROID__)
//  extern "C" void android_main(android_app* app) {
//      Kit::AttachPlatformHost(app);
//      // ...move the main body above here, or extract it into a plain function called from both...
//  }
//  #endif
//  @endcode
// =====================================================================

#include "Zekit.h"

#if defined(__ANDROID__)
#  include <android_native_app_glue.h>
#endif

namespace Kit {

 // The declaration of `AttachPlatformHost(void*)` is in `Zekit.h` (this header already includes it).
 // It lives there rather than here so that people who "write the entry by hand" need not include one more header.
 //
 // Semantics summary (see Zekit.h for details): no-op on desktop; on mobile it receives the "window handle
 // delivered by the system" and must be called **before** constructing Kit::Window.
 //
 // * HarmonyOS is an exception; it is explained here so readers do not think it was omitted:
 //   there the application **does not need to call it** -- the NAPI module and XComponent callbacks that
 //   drive the whole flow are inside the Zekit library (`ZeWindowHarmony.cpp`); the library attaches the
 //   host when the surface is ready, then calls the entry function defined by this macro on a **separate
 //   thread**. Android requires the application to call it itself because `native_app_glue` is an
 //   application-side .c file. The **interface surface** of the two is the same (the application only
 //   writes `ZEKIT_APP_ENTRY(name){...}`); the only difference is which side is responsible for hooking
 //   up the host.

} // namespace Kit

// =====================================================================
//  The **link-time** shape of the desktop entry
// =====================================================================
//  On Windows, "windowed program / console program" is decided at **link time** (subsystem bit + entry
//  point); getting the function name right is not enough -- so the two macros below each carry a set of
//  link directives:
//
//    /SUBSYSTEM:WINDOWS     the process is **not** given a console (double-click shows no black window)
//    /SUBSYSTEM:CONSOLE     has a console (stdout / stderr / Chinese code page all work)
//    /ENTRY:mainCRTStartup  the entry point is still `main` -- so no `WinMain` is needed, and the
//                           public headers need not know HINSTANCE / windows.h (reason in the file header)
//
//  * `__pragma` (not `#pragma`) is used because it must appear inside a **macro expansion**: `#pragma`
//    is not syntax outside preprocessing directives and does not survive in a macro body.
//  * Emitted only for MSVC / clang-cl (`_MSC_VER`): these two directives are things the MSVC linker
//    understands; GCC / native clang would only get a string of "unknown pragma" warnings. There the
//    runtime `Kit::SetupEntryConsole()` provides the fallback (see below), with consistent behavior.
// =====================================================================
#if defined(_MSC_VER)
#  define ZEKIT_DETAIL_LINK_GUI_ENTRY                                  \
		__pragma(comment(linker, "/SUBSYSTEM:WINDOWS"))                \
		__pragma(comment(linker, "/ENTRY:mainCRTStartup"))
#  define ZEKIT_DETAIL_LINK_CUI_ENTRY                                  \
		__pragma(comment(linker, "/SUBSYSTEM:CONSOLE"))                \
		__pragma(comment(linker, "/ENTRY:mainCRTStartup"))
#else
#  define ZEKIT_DETAIL_LINK_GUI_ENTRY
#  define ZEKIT_DETAIL_LINK_CUI_ENTRY
#endif

// **Runtime** actions before the entry function body (the runtime half of whether the platform supports
//   a "console"):
//   * Windowed shape: `SetupEntryConsole()` -- with `ZEKIT_CONSOLE=1` it provides a console, otherwise
//     it hides the one "owned by this process" (a no-op on MSVC: the link stage already fixed the window
//     subsystem, so the process has no console at all; its use is on the path where link directives do
//     not take effect: MinGW / clang).
//   * Console shape: `ShowConsole()` -- ensures output has a destination (touches nothing if one exists).
// Not called on non-Windows: there is no such thing as "hiding the console" there (the application runs
// in a terminal).
#if defined(_WIN32)
#  define ZEKIT_DETAIL_ENTRY_PRELUDE_GUI()  ::Kit::SetupEntryConsole()
#  define ZEKIT_DETAIL_ENTRY_PRELUDE_CUI()  ::Kit::ShowConsole()
#else
#  define ZEKIT_DETAIL_ENTRY_PRELUDE_GUI()
#  define ZEKIT_DETAIL_ENTRY_PRELUDE_CUI()
#endif

// Assembles "link directives + entry shell + business function body" into one entry. The three platforms
//   share this one shape:
//   * The business body is a **separate function**, so it can `return` normally (`return <int>` inside a
//     `void` function is a compile error, so the business body cannot be expanded directly into a void
//     entry such as android_main);
//   * The pre-actions go in the **shell**, and the first line of the business body is still the line the
//     user wrote.
#define ZEKIT_DETAIL_APP_ENTRY(name, linkDirectives, prelude)          \
	linkDirectives                                                     \
	static int name();                                                 \
	int main() { prelude; return name(); }                             \
	static int name()

// =====================================================================
//  ZEKIT_APP_ENTRY -- default shape: **windowed program**
// =====================================================================
#if defined(__ANDROID__)
 /// @brief Defines the application entry (on Android expands to `android_main` + one internal int function)
 /// @note Why it is split into "declare first, define later": this way `android_main` can appear at the
 ///       front of the file (easier to read), while the business function body it calls follows -- and at
 ///       the same time the business function **can `return` normally** (writing `return <int>` inside a
 ///       `void` function is a compile error, so the business body cannot be expanded directly into
 ///       `android_main`).
#  define ZEKIT_APP_ENTRY(name)                                        \
	 static int name();                                                \
	 extern "C" void android_main(::android_app* zekitHostApp) {       \
	     ::Kit::AttachPlatformHost(zekitHostApp);                      \
	     (void)name();                                                 \
	 }                                                                 \
	 static int name()
 /// @brief There is no such thing as a "console program" on Android: same entry as @ref ZEKIT_APP_ENTRY
 /// @note Logs go to logcat (`__android_log_print`), not stdout -- so there is **nothing to do** here;
 ///       the two macros expand to the same thing, and cross-platform code need not branch for it.
#  define ZEKIT_APP_ENTRY_CONSOLE(name)  ZEKIT_APP_ENTRY(name)
#elif defined(__OHOS__)
 /// @brief Defines the application entry (on HarmonyOS expands to `ZekitHarmonyAppMain` + one internal int function)
 ///
 /// @details
 /// HarmonyOS has **neither** `main` nor `android_main`: the whole application is launched by ArkUI, and
 /// the C++ side only has the NAPI module + XComponent callbacks. Zekit folds this bootstrap into the
 /// library (`ZeWindowHarmony.cpp` self-registers the NAPI module), and it asks only one thing of the
 /// application: **export a `ZekitHarmonyAppMain`**.
 /// This macro does exactly that -- the application-side code is **exactly the same** as on Android / desktop.
 ///
 /// @note One difference from the Android version: here it does **not** call `AttachPlatformHost` --
 ///       the host handle (the XComponent's `OHNativeWindow`) only exists after `OnSurfaceCreated`, and
 ///       the library attaches it at that moment, then calls this function.
 ///       See the explanation in the `namespace Kit` block above.
 ///
 /// @note Call timing and thread: the library calls it on a **separate render thread** (**must not** run
 ///       the application loop on ArkUI's UI thread -- touch callbacks would then never arrive again).
 ///       Returning means the application has ended, and the library lets the ArkTS side finish the Ability.
#  define ZEKIT_APP_ENTRY(name)                                        \
	 static int name();                                                \
	 extern "C" int ZekitHarmonyAppMain() {                            \
	     return name();                                                \
	 }                                                                 \
	 static int name()
 /// @brief There is no such thing as a "console program" on HarmonyOS: same entry as @ref ZEKIT_APP_ENTRY
 /// @note Logs go to HiLog, not stdout -- the two macros expand to the same thing.
#  define ZEKIT_APP_ENTRY_CONSOLE(name)  ZEKIT_APP_ENTRY(name)
#elif defined(_WIN32) && defined(ZEKIT_APP_CONSOLE)
 // ---- Project-level switch: the whole project wants a console (see item 3 in the file header) ----
 //  A typical use is adding `ZEKIT_APP_CONSOLE` to the preprocessor definitions of the **Debug
 //  configuration**: then Debug has a console and Release does not, with no code change.
 // @brief Defines the application entry (console program when `ZEKIT_APP_CONSOLE` is in effect)
#  define ZEKIT_APP_ENTRY(name)                                        \
	 ZEKIT_DETAIL_APP_ENTRY(name, ZEKIT_DETAIL_LINK_CUI_ENTRY,         \
	                        ZEKIT_DETAIL_ENTRY_PRELUDE_CUI())
 /// @brief Defines the application entry (explicit console program, equivalent to the line above)
#  define ZEKIT_APP_ENTRY_CONSOLE(name)                                \
	 ZEKIT_DETAIL_APP_ENTRY(name, ZEKIT_DETAIL_LINK_CUI_ENTRY,         \
	                        ZEKIT_DETAIL_ENTRY_PRELUDE_CUI())
#elif defined(_WIN32)
 /// @brief Defines the application entry (on Windows expands to a **windowed program** by default: no console)
 /// @note Want a console: replace this macro with @ref ZEKIT_APP_ENTRY_CONSOLE, or define
 ///       `ZEKIT_APP_CONSOLE`, or launch with `ZEKIT_CONSOLE=1` (see the file header for the trade-offs
 ///       of the three options).
#  define ZEKIT_APP_ENTRY(name)                                        \
	 ZEKIT_DETAIL_APP_ENTRY(name, ZEKIT_DETAIL_LINK_GUI_ENTRY,         \
	                        ZEKIT_DETAIL_ENTRY_PRELUDE_GUI())
 /// @brief Defines the application entry (**console program**: stdout / stderr attached to the console)
 /// @note The only difference from @ref ZEKIT_APP_ENTRY is the link-time subsystem bit; business code is
 ///       the same; if the link directives do not take effect (MinGW etc.), a console is opened at runtime
 ///       to make up for it.
 /// @note `ZEKIT_APP_CONSOLE` does not conflict with it: both want a console, so the result is the same.
#  define ZEKIT_APP_ENTRY_CONSOLE(name)                                \
	 ZEKIT_DETAIL_APP_ENTRY(name, ZEKIT_DETAIL_LINK_CUI_ENTRY,         \
	                        ZEKIT_DETAIL_ENTRY_PRELUDE_CUI())
#else
 /// @brief Defines the application entry (on Linux / macOS expands to `int main()`)
 /// @note There it is already the console shape (runs in a terminal, or in a .desktop / service with
 ///       output going to journald / a log file) -- there is no such thing as "hiding the console", so
 ///       nothing to do.
#  define ZEKIT_APP_ENTRY(name)  int main()
 /// @brief Defines the application entry (identical to @ref ZEKIT_APP_ENTRY on non-Windows)
 /// @note It is kept so that the statement "I want a console" can also be written in **cross-platform
 ///       code**: on Windows it means something, on other platforms it is the default shape.
#  define ZEKIT_APP_ENTRY_CONSOLE(name)  int main()
#endif
