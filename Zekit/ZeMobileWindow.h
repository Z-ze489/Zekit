#pragma once
// =====================================================================
//  ZeMobileWindow.h: shared mobile abstraction layer (Android / iOS, one copy).
// =====================================================================
//
//  Why mobile gets its own layer instead of "another IWindow implementation":
//  desktop backends (Win32 / X11) each implement Plat::IWindow directly, which is
//  right because their differences are "a different API set for the same job".
//  Mobile is a different shape:
//  1. "No window" is a whole set of semantic differences, not a few function
//     re-implementations. Geometry / position / minimize-maximize / topmost /
//     title bar / icon / opacity / dragging - all meaningless on mobile. Writing
//     that downgrade table twice (Android once, iOS once) would inevitably drift;
//     it is written once in this layer.
//  2. Touch / soft keyboard / lifecycle / safe area are shared mobile event
//     vocabulary, parallel to the desktop mouse+keyboard set. They live in
//     MobileEvent so Android and iOS speak the same language, and MobileWindow
//     translates it for Kit.
//  3. Platform adapters (IMobileBackend) can then stay thin: carry native data
//     only, make no decisions. Adding iOS is roughly implementing this interface.
//
//  Layering:
//      Kit::Window                 platform-agnostic business layer (untouched)
//           Plat::IWindow         <- existing contract
//      desktop backends        vs   mobile shared layer
//      Win32Window / X11Window      MobileWindow            <- this file
//      (implement IWindow)          IMobileBackend          <- this file
//                                   AndroidMobileBackend / IOSMobileBackend
//                                   (ZeWindowAndroid.cpp / later)
//
//  Iron rules:
//  - No platform macros in this file's .cpp. It compiles on ALL platforms
//    (including Windows) - proof that it is not hardcoded: the same source
//    compiling on desktop shows it depends only on the IMobileBackend
//    abstraction, never secretly on Android.
//  - Platform-specific things appear only inside IMobileBackend implementations.
//  - A new mobile platform = one IMobileBackend implementation + one #elif in
//    ZeWindow.cpp (that is the dispatch file's whole job).
// =====================================================================

#include "ZePlatformWindow.h"

#include <memory>
#include <string>

namespace Plat {

	// Capability baseline shared by mobile platforms and deliverable with pure NDK.
	//
	//  Adapters start from it and adjust per reality. Currently NOT included:
	//  - InputIme / ImeSpot: a pure-NDK soft keyboard has no composing string or
	//    candidate window, and no IME object to receive the caret position. With
	//    those bits set, InputBox would wait forever for an ImeCompose. Add them
	//    once the JNI InputConnection is wired up.
	//  - SafeArea: reading WindowInsets requires JNI (see IMobileBackend notes).
	//  - Clipboard: also requires JNI (ClipboardManager).
	//  - InputHover: no coordinate exists without a finger touching the screen;
	//    mobile has no hover concept. Critical: with the bit set, hover-driven
	//    visual feedback would stay stuck at "not hovered" on phones and look
	//    like a bug.
	inline constexpr Cap kMobileBaseCapabilities =
		Cap::Fullscreen |                                   // Downgraded to "hide system bars".
		Cap::InputTouch | Cap::InputMultiTouch |
		Cap::InputKeyboard | Cap::InputOnScreenKeyboard |
		Cap::AppLifecycle |
		Cap::SystemDarkMode;

	// =================================================================
	//  Mobile event vocabulary: the only data channel shared by Android / iOS
	// =================================================================
	//  Difference from Plat::Event: MobileEvent is a literal translation of the
	//  native event (what the system gives is what you get); Plat::Event is a
	//  semantic event (after Kit's decisions). Adapters produce only MobileEvent;
	//  MobileWindow translates into Plat::Event.
	enum class MobileEventKind : uint32_t {
		None = 0,

		// ---- Touch (multi-pointer; one native event may carry any number of fingers) ----
		TouchBegin,     // A pointer went down.
		TouchMove,      // A pointer moved.
		TouchEnd,       // A pointer lifted.
		TouchCancel,    // The whole gesture was stolen by the system.

		// ---- Native surface carrying the window ----
		// The system handed us a new surface (Android: APP_CMD_INIT_WINDOW).
		//  Receiving it means rendering may start; every GPU resource bound to
		//  the old surface is now invalid.
		SurfaceReady,
		// The surface is gone (Android: APP_CMD_TERM_WINDOW) - stop drawing.
		SurfaceLost,

		// ---- Size / configuration ----
		Resized,          // Content area size changed (rotation / keyboard intrusion / split screen).
		ConfigChanged,    // Dark mode, font scale, language, etc.

		// ---- Soft keyboard ----
		SoftKeyboardShown,
		SoftKeyboardHidden,

		// ---- Lifecycle ----
		AppResumed,
		AppPaused,
		AppLowMemory,
		AppTerminating,

		// ---- Focus ----
		FocusGained,
		FocusLost,

		// ---- Keys (physical keyboard AND soft keyboard both take this path) ----
		//  Under the pure-NDK path the soft keyboard is not IME composition:
		//  each character arrives as a key press in AInputQueue (with
		//  unicodeChar). Same event group as the physical keyboard, different source.
		KeyDown,
		KeyUp,
		// Pure character input (post-IME / post-case) - maps to Plat::EventKind::CharInput.
		//  Versus KeyDown: KeyDown reports which key was pressed, this reports
		//  which character came out. One physical press usually emits both.
		TextInput,

		// ---- Quit ----
		// The system asks the process to shut down (Android: android_app::destroyRequested).
		QuitRequested,
	};

	// A coordinate snapshot of one finger (or stylus).
	//  Coordinates are logical pixels (dp), not physical - see
	//  IMobileBackend::QuerySurface for the two coordinate systems.
	struct MobileTouchPoint {
		uint32_t id = 0;      // Platform pointer id (Android: finger index, reused after lifting).
		float    x = 0.0f;
		float    y = 0.0f;
	};

	// One mobile native event (literal translation, no Kit decisions yet).
	struct MobileEvent {
		MobileEventKind kind = MobileEventKind::None;

		// Pointers pressed simultaneously (filled only for Touch* events).
		//  10 = Android's de facto limit for simultaneous touch points.
		static constexpr uint32_t kMaxPointers = 10;
		uint32_t         pointerCount = 0;
		MobileTouchPoint pointers[kMaxPointers]{};
		// Index in pointers[] of the finger that changed in this event.
		//  Android's ACTION_POINTER_DOWN/UP only reports which one changed; this
		//  index decodes it. For ACTION_MOVE several may move at once; treating
		//  index 0 is fine (the snapshot has all of them).
		uint32_t         changedIndex = 0;

		// Content area size (pixels) - filled only for Resized / SurfaceReady.
		int   width = 0;
		int   height = 0;
		// Soft keyboard height (pixels) - filled only for SoftKeyboard*, 0 = hidden.
		int   keyboardHeight = 0;

		// ---- Keys (only KeyDown / KeyUp / TextInput use these) ----
		// Semantic key (translated by the platform layer, not a raw key code).
		//  Shares the Kit::Key numbering (see include/ZeInputEvents.h); the
		//  platform layer translates Android AKEYCODE_* / iOS UIKeyboardHIDUsage
		//  into it, matching Plat::Event::key's convention. Translation MUST
		//  happen in the adapter (that is where platform knowledge lives), not in
		//  MobileWindow - otherwise the shared layer would need both platforms'
		//  key tables, dragging the platform difference right back in.
		uint32_t key = 0;
		// Platform raw key code (escape hatch; Android = AKEYCODE_*).
		uint32_t rawKeyCode = 0;
		// Character produced by the key (UTF-32 code point; 0 = none).
		uint32_t unicodeChar = 0;
		// Modifier bits (already translated by the adapter into Plat::Modifiers).
		Modifiers mods = Modifiers::None;
		bool     isRepeat = false;
	};

	// =================================================================
	//  Platform adapter: each mobile platform implements only this interface
	// =================================================================
	//  Design goal: carry native data only, make no decisions. Judging "what does
	//  this platform support" and "what does it degrade to" lives in MobileWindow
	//  (written once); the adapter must contain no business judgment, so adding
	//  iOS cannot introduce a second rule set.
	class IMobileBackend {
	public:
		virtual ~IMobileBackend() = default;

		// The capability set this adapter can actually deliver.
		//
		//  Why the adapter reports it instead of a hardcoded "mobile capability
		//  table" in MobileWindow: "mobile" is not monolithic - the same thing
		//  differs by platform and integration depth. Example: safe area (notch)
		//  on Android needs JNI to read WindowInsets, simply unavailable before
		//  JNI is wired; hardcoding it would make the capability bit lie (claims
		//  support, actually returns 0). Lying is far worse than reporting
		//  unsupported: callers write code against support and hit undebuggable
		//  failures.
		//
		//  Start from kMobileBaseCapabilities (the pure-NDK shared set) and adjust
		//  per this platform's reality.
		virtual Cap MobileCapabilities() const = 0;

		// ---- Window and surface ----

		// Fetches the native surface handle and PHYSICAL pixel size of the
		// surface currently carrying the window.
		//  nativeSurface (out): opaque handle (Android = ANativeWindow*), the
		//  very thing handed to Rhi::IBackend::Init().
		//  widthPx, heightPx (out): PHYSICAL pixel size (the rendering layer's
		//  unit). Returns false when no surface is available right now (before
		//  INIT_WINDOW or after TERM_WINDOW).
		//  Do NOT cache: Android's ANativeWindow* can be a brand-new pointer
		//  between TERM_WINDOW -> INIT_WINDOW.
		//
		//  Two coordinate systems, do not mix (mobile-specific):
		//  - This function reports PHYSICAL pixels - what the rendering layer
		//    needs (viewport / swapchain).
		//  - MobileEvent coordinates and MobileWindow::GetClientSize report
		//    LOGICAL pixels (dp) - what the UI layer needs. Phone densities are
		//    high (2.5x-3.5x common); using physical pixels as UI coordinates
		//    makes desktop-tuned font sizes unreadably small. The adapter does
		//    the conversion when filling MobileEvent (it knows its density); the
		//    shared layer only deals in logical pixels.
		virtual bool QuerySurface(void*& nativeSurface, int& widthPx, int& heightPx) = 0;

		// ---- Event pump ----

		// Pops one native event and translates it literally into a MobileEvent.
		//  Returns true when an event was popped, false when the queue is empty.
		//  The only entry of the message pump; must not block (timeout is
		//  WaitMobileEvents' job).
		virtual bool PollMobileEvent(MobileEvent& out) = 0;

		// Blocks until a native event arrives or the timeout expires.
		//  timeoutMs < 0 = wait forever; >= 0 = wait at most that long. Maps to
		//  Android's ALooper_pollOnce / iOS's [NSRunLoop runMode:beforeDate:].
		virtual void WaitMobileEvents(int timeoutMs) = 0;

		// Pushes "time to quit" into the event queue (for in-process quit requests).
		//  Maps to Android's ALooper_wake plus a self-posted event.
		virtual void WakeEventLoop() = 0;

		// ---- Soft keyboard ----
		virtual void ShowSoftKeyboard(bool on) = 0;
		virtual bool IsSoftKeyboardVisible() const = 0;

		// ---- Display info ----
		// Display density (1.0 = mdpi/160dpi; Android = dpi/160, iOS = scale).
		virtual float Density() const = 0;

		// Sets the window background color BEFORE the first frame (ARGB 0xRRGGBB, no alpha).
		//  Pure NDK cannot change it - that layer belongs to the system
		//  compositor. With the Java side it can truly be set
		//  (View.setBackgroundColor), eliminating the flash of the system default
		//  background at startup / on resume.
		//  Default empty impl: not every mobile platform can change it (iOS works
		//  differently).
		virtual void SetWindowBackground(unsigned int /*rgb*/) {}
		// Safe area insets (notch / rounded corners / gesture bar), logical pixels.
		virtual void GetSafeAreaInsets(float& left, float& top,
			float& right, float& bottom) const = 0;

		// ---- Lifecycle / focus ----
		virtual bool HasFocus() const = 0;
		// Requests the whole app to quit (Android = ANativeActivity_finish).
		virtual void RequestAppQuit() = 0;

		// ---- System preferences ----
		virtual bool SystemPrefersDark() const = 0;

		// ---- Fullscreen / immersive (optional) ----
		// Hides / restores the system bars (status bar + navigation bar).
		//  The semantic downgrade target of mobile SetFullscreen: on desktop it
		//  means "fill the monitor"; the mobile window already fills the screen,
		//  and the closest equivalent is hiding the system bars.
		//  Defaults to false (bars visible), matching Android app convention.
		virtual void SetImmersive(bool on) { (void)on; }
		virtual bool IsImmersive() const { return false; }

		// Light/dark of system bar icons (follows the app theme).
		//  dark true = app is on a dark theme, so the system bar background is
		//  dark and icons must be light; false = light theme, icons dark.
		//  Mobile needs this where desktop has "title bar dark mode"
		//  (SetDarkTitleBar): there is no title bar, but status/nav bar icon
		//  colors must follow the theme, or a light theme with white icons makes
		//  the clock and battery invisible. Unwired platforms show
		//  "icons decided by the system theme" - a safe degradation.
		virtual void SetDarkSystemBar(bool dark) { (void)dark; }

		// ---- Clipboard (optional) ----
		//  Not supported by default: the Android clipboard must go through JNI
		//  (ClipboardManager); iOS uses UIPasteboard. Unwired platforms behave as
		//  "paste has no content", consistent with unwired desktop platforms (see
		//  Plat::IWindow::GetClipboardText).
		virtual std::string GetClipboardText() { return std::string(); }
		virtual void SetClipboardText(const std::string&) {}

		// ---- Keep screen on (optional) ----
		virtual void SetKeepScreenOn(bool on) { (void)on; }

		// =============================================================
		//  Mobile landing points of desktop semantics (internal, not for apps)
		// =============================================================
		//  Role of these methods: a desktop window API has a semantic equivalent
		//  on mobile, but that equivalent is an app-level / system-level native
		//  action (move to background, set title, change window alpha). MobileWindow
		//  translates the desktop API into these; application code changes not a
		//  line and never learns these methods exist.
		//
		//  All default to empty impls: platforms that do not implement them (later
		//  iOS, or a trimmed Android backend) do nothing, and the corresponding
		//  desktop API behaves as a silent no-op - the same principle as "the
		//  interface always exists, differences show only in behavior", one layer
		//  further down.
		//
		//  This generalizes the existing SetFullscreen -> SetImmersive and
		//  SetDarkTitleBar -> SetDarkSystemBar pattern: those two already worked
		//  this way; this just extends the shape to the other desktop APIs.

		// Requests the app to move to the background (desktop Minimize).
		//  Android = Activity.moveTaskToBack(true). From the user's view identical
		//  to desktop minimizing: "I don't want to see it, but don't kill it - it
		//  is still in recent tasks".
		virtual void RequestAppMoveToBack() {}

		// Sets the Activity / app title (desktop SetTitle).
		//  utf8: title (UTF-8). Android = Activity.setTitle(...), shown in the
		//  recent-tasks list and system-level dialogs. No window title bar on
		//  mobile, but "what the app is called" still matters.
		virtual void SetActivityTitle(const std::string& utf8) { (void)utf8; }

		// Sets the window alpha (desktop SetOpacity).
		//  alpha 0.0 (fully transparent) to 1.0 (opaque). Android =
		//  WindowManager.LayoutParams.alpha (via Activity.getWindow().getAttributes()).
		//  Not all platforms can set it; the default empty impl is a silent no-op.
		virtual void SetWindowAlpha(float alpha) { (void)alpha; }
	};

	// =================================================================
	//  Mobile implementation of IWindow
	// =================================================================
	//  Translates IMobileBackend's native events into Plat::Event and downgrades
	//  the desktop-only slice of the interface. Not a single platform macro.
	//
	//  What "downgrade" means (the core stance of this layer; read before changing):
	//  desktop APIs (geometry / position / minimize-maximize / topmost / title
	//  bar / icon / opacity / dragging...) are not ERRORS on mobile - the platform
	//  simply has no such physical concept. The downgrade rule is one line:
	//      mappable to a mobile equivalent -> map (e.g. SetFullscreen -> SetImmersive)
	//      no equivalent                   -> silent no-op
	//      Neither case reports, diagnoses, or logs anything.
	//
	//  Why not report (it once did; removed deliberately): "tell the developer
	//  about downgrades" is a desktop-library instinct. This library promises the
	//  developer writes one codebase that runs unchanged on Windows / Linux /
	//  Android. Reporting a downgrade tells the developer at runtime "this code
	//  did not take effect here", steering attention toward platform forking -
	//  exactly breaking the promise. The interface exists and is callable on ALL
	//  platforms; differences live in behavior alone. The developer need not know
	//  where they are or which call was downgraded - only that the program does
	//  not crash and the UI appears sensibly. That is why zew.cpp runs on all
	//  three platforms without ever calling Supports(). Downgrade is design, not
	//  a fault to report.
	class MobileWindow final : public IWindow {
	public:
		// Takes ownership of a platform adapter. backend must not be nullptr.
		explicit MobileWindow(IMobileBackend* backend);
		~MobileWindow() override;

		MobileWindow(const MobileWindow&) = delete;
		MobileWindow& operator=(const MobileWindow&) = delete;

		Cap Capabilities() const override;

		// Mobile is always "buildable a bit later" (the system delivers the
		//  surface asynchronously).
		//  See Plat::IWindow::RenderBackendPending: failing to build a backend at
		//  Show() time is normal sequencing and must not make Kit give up on
		//  drawing forever.
		bool RenderBackendPending() const override { return true; }

		bool Create(const WindowDesc& desc, IWindowHost& host) override;

		void Show() override;
		void Hide() override;
		void Destroy() override;

		bool PollEvent() override;
		void WaitEvents(int timeoutMs) override;
		int  ExitCode() const override;

		void GetClientSize(int& w, int& h) const override;
		void SetClientSize(int w, int h) override;
		void SetPosition(int x, int y) override;
		void CenterOnScreen() override;
		void SetCenterOnResize(bool on) override;
		void SetSizeLimits(int minW, int minH, int maxW, int maxH) override;

		float ScaleFactor() const override;
		int   Dpi() const override;

		// Render scale = density (e.g. 3.5): logical unit = dp, physical pixel = dp * density.
		//  Must be the exact inverse of GetClientSize()'s conversion (physical /
		//  density); both must draw from the same source (IMobileBackend::Density())
		//  or glyphs and UI geometry will misalign. See Plat::IWindow::RenderScale().
		float RenderScale() const override;

		// A mobile window is born with the system bars VISIBLE (platform convention).
		//  Only seeds Kit's m_fullscreen initial value to match the platform:
		//  Android apps start with status/nav bars visible; only games / video go
		//  immersive on their own, so this returns false ("not born fullscreen").
		bool StartsImmersive() const override;

		void Minimize() override;
		void Maximize() override;
		void Restore() override;
		bool IsMinimized() const override;
		bool IsMaximized() const override;
		void SetTopmost(bool on) override;

		void SetResizable(bool on) override;
		void SetFullscreen(bool on) override;
		void SetBorderless(bool on) override;
		void ApplyTitleBar(bool titleBarVisible,
			bool minBtn, bool maxBtn, bool closeBtn) override;
		void SetIconFromFile(const std::string& utf8Path) override;
		void SetIconFromResource(int resId) override;
		void SetIconFromHandle(void* hIcon) override;
		void SetOpacity(float alpha) override;
		void SetDarkTitleBar(bool dark) override;
		void SetBackdropColor(unsigned int rgb) override;

		std::string GetClipboardText() override;
		void        SetClipboardText(const std::string& utf8) override;

		void SetImeSpot(float x, float lineTop, float lineBottom) override;
		void ShowSoftKeyboard(bool on) override;
		bool IsSoftKeyboardVisible() const override;
		void GetSafeAreaInsets(float& left, float& top,
			float& right, float& bottom) const override;

		void SetTitle(const std::string& utf8) override;
		bool IsClosed() const override;
		bool IsActive() const override;
		void RequestRedraw() override;
		void RequestWakeup(int afterMs) override;
		void CancelWakeup() override;
		void SetTimerPrecision(bool on) override;
		void PostClose() override;
		void RequestQuit(int exitCode) override;

		void StartWindowDrag() override;
		bool IsMouseButtonDown(MouseButton b) const override;

		void* NativeHandle() const override;

	private:
		struct Impl;
		std::unique_ptr<Impl> m_impl;
	};

} // namespace Plat
