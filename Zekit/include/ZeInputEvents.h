// Input events: mouse / keyboard enums and value-semantic event objects.
//
// This file holds two things: the enums that describe "what happened"
// (EventType / MouseButton / Key / KeyboardModifiers) with their pure helpers
// (hasModifier / operator|), and the value-semantic event objects that describe
// the details (MouseEvent / KeyEvent).
//
// Design notes (consistent with the rest of Zekit):
//  - Events are pure value objects: stack allocated, no heap, copyable.
//  - No Event base class with static_cast dispatch: the receiver (a widget
//    virtual) knows the event type at compile time, so the runtime enum is only
//    used for logging and debugging.
//  - Window builds events in WndProc and hands them to widgets by value. A
//    widget sets `accepted` to mark the event consumed, which stops bubbling.
//
// Events are valid only during the current dispatch. Copy the fields you need
// instead of storing a pointer to the event.
//
// See ZeWidget.h for the event virtuals (OnMousePress / OnKeyPress ...) and
// Zekit.h for the Window that produces and dispatches events.

#pragma once

#include <cstdint>
#include <vector>

namespace Kit {

    // ===================================================================
    //  Enums: event categories
    // ===================================================================

    // Event type enum.
    //
    // With value semantics the widget receives each event through its own virtual
    // (OnMousePress / OnKeyPress ...), so handling an event does not need this enum.
    // It is for callers that only care what happened: logs, debug output, event
    // statistics, possible future record/replay.
    //
    // Must stay in sync with MouseEvent::type / KeyEvent::type: this is the one
    // place in the file where the same fact is written twice, so adding an event
    // type means touching both.
    enum class EventType {
        None,           // No event (placeholder value, ignore when received)
        MousePress,     // Mouse button pressed
        MouseRelease,   // Mouse button released
        MouseMove,      // Mouse moved
        MouseWheel,     // Mouse wheel
        KeyPress,       // Key pressed
        KeyRelease,     // Key released
        CharInput       // Character input (text with IME and case already handled)
    };

    // Mouse button enum.
    // See MouseEvent::button.
    enum class MouseButton {
        None = 0,   // No button (for events such as MouseMove)
        Left,       // Left button
        Right,      // Right button
        Middle,     // Middle button
        X1,         // Side button 1 (usually "back")
        X2          // Side button 2 (usually "forward")
    };

    // ===================================================================
    //  Enums: keys
    // ===================================================================

    // Semantic key enum (values defined by this library, identical across platforms).
    //
    // Covers common keys with readable names; the underlying type is uint32_t.
    // Each platform layer translates its native codes into these values (VK_* on
    // Win32, keysym on X11, keyCode on macOS), so widget code only sees this enum
    // and gets the same key semantics everywhere.
    //
    // Some names and values happen to match platform habits (A == 'A', F1 == 0x70);
    // that is only for readability and is not a compatibility promise. Never
    // compare Key against a native code, it breaks cross-platform code.
    //
    // If a key is not covered, KeyEvent::virtualKey returns the raw platform code
    // (platform-specific, not portable).
    //
    // This enum describes physical keys, not the resulting text; for actual input
    // use the CharInput event and KeyEvent::character.
    //
    // See KeyEvent::key, KeyEvent::virtualKey.
    enum class Key : uint32_t {
        None = 0,           // No key (placeholder value)

        // Letter keys
        A = 'A', B = 'B', C = 'C', D = 'D', E = 'E',
        F = 'F', G = 'G', H = 'H', I = 'I', J = 'J',
        K = 'K', L = 'L', M = 'M', N = 'N', O = 'O',
        P = 'P', Q = 'Q', R = 'R', S = 'S', T = 'T',
        U = 'U', V = 'V', W = 'W', X = 'X', Y = 'Y', Z = 'Z',

        // Digit keys (main keyboard area)
        Key0 = '0', Key1 = '1', Key2 = '2', Key3 = '3', Key4 = '4',
        Key5 = '5', Key6 = '6', Key7 = '7', Key8 = '8', Key9 = '9',

        // Punctuation keys (US standard keyboard layout)
        Semicolon = 0xBA,   // ; :
        Equal = 0xBB,   // = +
        Comma = 0xBC,   // , <
        Minus = 0xBD,   // - _
        Period = 0xBE,   // . >
        Slash = 0xBF,   // / ?
        Backtick = 0xC0,   // ` ~
        LeftBracket = 0xDB,   // [ {
        Backslash = 0xDC,   // \ |
        RightBracket = 0xDD,   // ] }
        Quote = 0xDE,   // ' "

        // Function keys F1 ~ F24
        F1 = 0x70, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
        F13 = 0x7C, F14, F15, F16, F17, F18, F19, F20, F21, F22, F23, F24,

        // Control keys
        Escape = 0x1B,
        Tab = 0x09,
        CapsLock = 0x14,
        Shift = 0x10,
        Control = 0x11,
        Alt = 0x12,
        Space = 0x20,
        Enter = 0x0D,
        Backspace = 0x08,

        // Editing keys
        Insert = 0x2D,
        Delete = 0x2E,
        Home = 0x24,
        End = 0x23,
        PageUp = 0x21,
        PageDown = 0x22,

        // Arrow keys
        LeftArrow = 0x25,
        UpArrow = 0x26,
        RightArrow = 0x27,
        DownArrow = 0x28,

        // Numeric keypad
        NumPad0 = 0x60, NumPad1, NumPad2, NumPad3, NumPad4,
        NumPad5, NumPad6, NumPad7, NumPad8, NumPad9,
        NumPadMultiply = 0x6A,
        NumPadAdd = 0x6B,
        NumPadSubtract = 0x6D,
        NumPadDecimal = 0x6E,
        NumPadDivide = 0x6F,
        //NumPadEnter = 0x0D, Enter

        // Media keys
        VolumeMute = 0xAD,
        VolumeDown = 0xAE,
        VolumeUp = 0xAF,
        MediaNextTrack = 0xB0,
        MediaPrevTrack = 0xB1,
        MediaStop = 0xB2,
        MediaPlayPause = 0xB3,
        LaunchMail = 0xB4,
        LaunchMediaSelect = 0xB5,
        LaunchApp1 = 0xB6,
        LaunchApp2 = 0xB7,

        // System keys
        LeftWindows = 0x5B,
        RightWindows = 0x5C,
        Apps = 0x5D,
        PrintScreen = 0x2C,
        ScrollLock = 0x91,
        Pause = 0x13,
        NumLock = 0x90,
        Sleep = 0x5F,
    };

    // Keyboard modifier keys (bit flags that can be combined).
    //
    // Describes the modifiers held down when the event occurred. Each value is an
    // independent bit flag: combine with operator| and test with hasModifier.
    //
    // See hasModifier, operator|(KeyboardModifiers, KeyboardModifiers).
    enum class KeyboardModifiers : uint32_t {
        NoModifier = 0,     // No modifier held
        Shift = 1 << 0,     // Shift key
        Control = 1 << 1,   // Ctrl / Control key
        Alt = 1 << 2,       // Alt key
    };

    // Returns true when the given modifier flag is held down in `mods`.
    inline bool hasModifier(KeyboardModifiers mods, KeyboardModifiers flag) {
        return (static_cast<uint32_t>(mods) & static_cast<uint32_t>(flag)) != 0;
    }

    // Bitwise or: combines two modifier sets, handy for compound shortcuts.
    inline KeyboardModifiers operator|(KeyboardModifiers a, KeyboardModifiers b) {
        return static_cast<KeyboardModifiers>(
            static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }

    // Bitwise or-assign: merges modifier set `b` into `a`.
    inline KeyboardModifiers& operator|=(KeyboardModifiers& a, KeyboardModifiers b) {
        a = a | b;
        return a;
    }

    // ===================================================================
    //  Value-semantic event objects
    // ===================================================================

    // Mouse event (press, release, move, wheel).
    //
    // Pure value object: stack allocated, copyable, no heap allocation. The fields
    // cover EventType::MousePress / MouseRelease / MouseMove / MouseWheel.
    //
    // Coordinates are client-area coordinates (origin top-left, Y down, pixels),
    // the same space as widget geometry, so hit testing and placement can use them
    // directly without conversion.
    //
    // Typical use inside a widget:
    //   if (e.button != MouseButton::Left) return false;  // not handled, back to parent
    //   e.accepted = true;                                // consumed, stop bubbling
    //
    // See KeyEvent, MouseButton, EventType.
    struct MouseEvent {
        EventType         type = EventType::None;   // Event type (logging/debug only)
        float             x = 0.0f;                 // Client X coordinate (pixels)
        float             y = 0.0f;                 // Client Y coordinate (pixels)
        MouseButton       button = MouseButton::None; // Triggering button (meaningless on move)
        int               wheelDelta = 0;           // Wheel delta (plus or minus 120 per notch)
        KeyboardModifiers modifiers = KeyboardModifiers::NoModifier; // Modifiers held down

        // True once the event is marked handled (stops bubbling to the parent widget).
        bool accepted = false;

        // Marks the event handled (same as `accepted = true`, more readable).
        void accept() { accepted = true; }

        // Returns whether the event was already handled.
        bool isAccepted() const { return accepted; }
    };

    // Touch phase (used with TouchEvent).
    enum class TouchPhase : uint32_t {
        Begin = 0,   // This finger just went down
        Update,      // A finger is moving
        End,         // This finger was lifted
        Cancel,      // The whole gesture was taken over by the system (call / back gesture / app backgrounded)
    };

    // Application lifecycle state (mobile only).
    //
    // On desktop the lifecycle is "process starts, user closes the window, process
    // ends", simple enough to need no notification. On mobile the system may suspend
    // the app at any time, or kill it outright in the background (without asking
    // when memory runs low). So "when to save state" needs an explicit signal:
    // destructors never run when the process is killed.
    //
    // The one rule: on Paused, persist what must survive immediately. LowMemory is
    // the more urgent variant (release everything you can).
    //
    // Desktop platforms never receive these notifications (the
    // `Capability::AppLifecycle` bit is 0).
    //
    // See Window::SetOnAppLifecycle.
    enum class AppLifecycle : uint32_t {
        None = 0,
        Resumed,      // Back in the foreground with a window, work can continue
        Paused,       // Moved to the background and may be killed at any time (save now)
        LowMemory,    // System memory is tight, release everything possible
        Terminating,  // About to exit (last chance to clean up)
    };

    // One finger position (an element of TouchEvent::pointers).
    struct TouchPoint {
        uint32_t id = 0;    // Pointer id (given by the platform, reused after the finger lifts)
        float    x = 0.0f;  // Client X coordinate (pixels)
        float    y = 0.0f;  // Client Y coordinate (pixels)
    };

    /**
     * @brief Touch event (multi-touch)
     *
     * @details
     * Two channels, each with its own job -- this is the core convention that makes Zekit work on mobile:
     *
     *  1. Existing widgets go through mouse events. The platform layer's "primary pointer" (the first finger pressed)
     *     is synthesized into a `MouseEvent` (Left button) and fed into the existing dispatch chain
     *     OnMousePress/Release/Move -- so `Button` / `ScrollView` / `InputBox` work on touch screens
     *     without changing a single line. This is deliberate: making every existing widget rewrite itself
     *     in a cross-platform library would be absurd.
     *
     *  2. Widgets that need multiple fingers go through this event (`OnTouchBegin/Update/End/Cancel`).
     *     Two-finger zoom, rotation, multi-finger gestures -- the mouse model cannot express these.
     *
     * So one touch may produce two things at once: a synthesized mouse event + a touch event.
     * Widgets that only care about a single finger just override the mouse callbacks; those that need
     * multi-finger handling override the touch callbacks.
     *
     * @note Coordinates are client-area pixels, the same coordinate system as mouse events.
     * @note `pointers` is a snapshot of all fingers pressed at the instant the event occurs --
     *       so widgets need not track "how many fingers are down and where" themselves.
     *       WARNING: but the snapshot of an `End` event still contains the finger that just lifted
     *       (platform convention), so do not treat it as the "currently pressed set"; to check, count
     *       `activeCount()`.
     *
     * @see MouseEvent, TouchPhase, Widget::OnTouchBegin
     */
    struct TouchEvent {
        TouchPhase phase = TouchPhase::Begin;   /// < Which phase of the gesture this event is in

        /// @brief The finger that changed in this phase
        /// @note Single-finger widgets only need id/x/y; no need to look through pointers[].
        uint32_t   pointerId = 0;
        float      x = 0.0f;                    /// < Coordinates of the changed finger (follows pointerId)
        float      y = 0.0f;

        /// @brief Snapshot of all fingers pressed at the same time (`pointerCount` is the number of valid elements)
        static constexpr uint32_t kMaxPointers = 10;
        uint32_t   pointerCount = 0;
        TouchPoint pointers[kMaxPointers]{};

        KeyboardModifiers modifiers = KeyboardModifiers::NoModifier; /// < Modifier keys held at the same time (external keyboard)

        /// @brief Whether the event has been marked as handled (true = bubbling stops, not passed to the parent widget)
        bool accepted = false;

        /// @brief Marks the event as handled (equivalent to `accepted = true`, semantic form)
        void accept() { accepted = true; }

        /// @brief Queries whether the event has been handled
        bool isAccepted() const { return accepted; }

        /// @brief Number of fingers still held down at this moment
        /// @note `Cancel` is always 0; `End` does not count the finger that lifted --
        ///       because the snapshot still keeps it (see the description of this struct), directly counting
        ///       `pointerCount` would misjudge the moment of release as still pressed.
        uint32_t activeCount() const {
            if (phase == TouchPhase::Cancel) return 0;
            if (phase == TouchPhase::End)
                return pointerCount > 0 ? pointerCount - 1 : 0;
            return pointerCount;
        }
    };

    /**
     * @brief Keyboard event (key press, key release, character input)
     *
     * @details
     * Pure value object: constructed on the stack, copyable, no heap allocation. Covers the three event
     * types EventType::KeyPress / KeyRelease / CharInput, and provides information in two dimensions:
     * "physical key" and "input text":
     *  - Physical dimension: virtualKey / key / nativeScanCode / isRepeat;
     *  - Text dimension: character (valid only for CharInput, case and input method already handled).
     *
     * Writing shortcuts should be based on key + modifiers; handling text entry should be based on
     * the character of a CharInput event (`char32_t`, full code point).
     *
     * @see MouseEvent, Key, KeyboardModifiers
     */
    struct KeyEvent {
        EventType         type = EventType::None;   /// < Event type (KeyPress / KeyRelease / CharInput)
        Key               key = Key::None;          /// < Semantic key (uncovered keys are Key::None)

        /// @brief Platform raw key code (escape hatch, only needed when key is None)
        /// @warning The content varies by platform (Win32 is VK_*, X11 is keysym, macOS is keyCode),
        ///          cross-platform values are not guaranteed to be consistent; programs that branch on it
        ///          will break when moved to another platform.
        ///          Cross-platform logic should always be based on @ref key.
        uint32_t          virtualKey = 0;

        uint32_t          nativeScanCode = 0;       /// < Keyboard hardware scan code (for distinguishing physical key positions)
        bool              isRepeat = false;         /// < Whether this is an auto-repeat caused by holding the key
        KeyboardModifiers modifiers = KeyboardModifiers::NoModifier; /// < Modifier keys held at the same time

        /// @brief The character actually input (valid only for CharInput)
        /// @note Uses `char32_t` instead of `wchar_t`: the latter is only 16 bits on Windows and cannot
        ///       hold code points outside the BMP (emoji, etc.). The platform layer passes the full code
        ///       point, and the widget side encodes it as UTF-8; no manual surrogate-pair assembly needed.
        char32_t          character = 0;

        /// @brief Whether the event has been marked as handled (true = bubbling stops, not passed to the parent widget)
        bool accepted = false;

        /// @brief Marks the event as handled (equivalent to `accepted = true`, semantic form)
        void accept() { accepted = true; }

        /// @brief Queries whether the event has been handled
        bool isAccepted() const { return accepted; }
    };

    // ===================================================================
    //  Event filter
    // ===================================================================

    class Window;
    class Widget;

    /**
     * @brief Event filter base class -- looks at an event before it reaches a widget and may intercept it
     *
     * @details
     * After an event enters from Win32 and before hit testing, it passes through all installed filters
     * in order. Any filter returning `true` means "intercepted": the event is discarded and widgets do
     * not receive it.
     *
     * @par Installation target: Window or any Widget
     * Filters can be installed at two levels, with different semantics:
     *  - Installed on a `Window` -> global: all events pass through it first (regardless of which widget is hit);
     *  - Installed on a `Widget` -> local: only events received by "that widget and its subtree" pass through it.
     *
     * The latter is key to modular development -- a self-contained module (panel, toolbar, table)
     * can install its own filter on itself without knowing that a Window exists:
     * @code
     * class MyPanel : public Kit::Widget {
     *     ShortcutFilter filter_;          // the module's own filter
     *     void Init() {
     *         InstallEventFilter(filter_); // <- installed on itself, no Window involved
     *     }
     * };
     * @endcode
     *
     * @par Difference from widget virtual functions
     * Widgets (Widget::OnMousePress etc.) are "the event found me, I handle it" -- they need a
     * geometric position and participate in hit testing; filters are "I take a look as the event passes
     * by" -- they ignore position, do not participate in hit testing, and only intervene at their level.
     *
     * Typical uses: global shortcuts, drag coordination, event logging/analytics, modal overlay
     * blocking, debug-time event statistics.
     *
     * @par Usage
     * @code
     * // 1) Derive a filter: intercept the space key while space is held
     * struct SpaceBlocker : Kit::EventFilter {
     *     bool onKey(Kit::Window&, Kit::KeyEvent& e) override {
     *         return e.key == Kit::Key::Space;      // true = intercept, widget does not receive it
     *     }
     * };
     *
     * SpaceBlocker blocker;
     * auto id = win.InstallEventFilter(blocker);   // installed on the window = global
     * win.RemoveEventFilter(id);
     *
     * // 2) The same filter class can also be installed on a widget, affecting only that widget's subtree
     * auto id2 = myPanel.InstallEventFilter(blocker);
     * @endcode
     *
     * @par Lifetime
     * The host (Window / Widget) does not own the filter, it only stores its address (weak reference).
     * The filter may be destroyed before the host -- `~EventFilter()` automatically unregisters itself
     * from all hosts; it may also be destroyed after the host. Therefore a filter can be a value on the
     * stack or a heap object held by `unique_ptr`.
     * @warning Like other "attached things", the address must not change once installed:
     *          copying/moving the filter itself is forbidden.
     *
     * @par Execution order
     * Called in installation order, one after another, and all of them are executed (no short-circuit)
     * -- because later-installed ones may be mere observers. If any returns `true`, the event is
     * discarded after the whole chain finishes.
     * If you need "first installed has priority and higher priority", decide inside the filter and
     * return true yourself.
     *
     * @par Timing of widget-level filters
     * After hit testing finds the target widget: starting from the target widget and walking up the
     * parent chain, each widget's own filter chain runs first; if intercepted at any level, the event
     * is discarded entirely and no longer bubbles.
     *
     * @see Window::InstallEventFilter, Widget::InstallEventFilter
     * @see Widget's event virtual functions (handling after the event reaches a widget)
     */
    class EventFilter {
    public:
        virtual ~EventFilter();

        /**
         * @brief Mouse event filtering (press / release / move / wheel)
         * @param win Window the event belongs to
         * @param e   The event (may be modified, or observed read-only)
         * @return true = intercept the event, widgets no longer receive it; false = let it through
         * @note Passes through by default. Filters that only care about the keyboard need not override this.
         */
        virtual bool onMouse(Window& win, MouseEvent& e) { (void)win; (void)e; return false; }

        /**
         * @brief Keyboard event filtering (press / release / character input)
         * @param win Window the event belongs to
         * @param e   The event (may be modified, or observed read-only)
         * @return true = intercept the event, widgets no longer receive it; false = let it through
         * @note Passes through by default. Filters that only care about the mouse need not override this.
         */
        virtual bool onKey(Window& win, KeyEvent& e) { (void)win; (void)e; return false; }

    private:
        friend class Window;
        friend class Widget;

        /// @brief A record of "what I am installed on" (for reverse unregistration at destruction)
        /// @note Window and Widget are never both non-null -- the installation target is either a window or a widget.
        struct Install {
            Window*  window = nullptr;
            Widget*  widget = nullptr;
            uint64_t id = 0;
        };

        /// @brief Which hosts this filter is currently installed on (non-owning; hosts detach on destruction)
        /// @note One filter can be installed on multiple hosts at once, so a table is needed rather than a single pointer.
        std::vector<Install> installs_;

        /// @brief Records a window-level installation (called only by Window::InstallEventFilter)
        void addInstall(Window* w, uint64_t id) { installs_.push_back(Install{ w, nullptr, id }); }

        /// @brief Records a widget-level installation (called only by Widget::InstallEventFilter)
        void addInstall(Widget* w, uint64_t id) { installs_.push_back(Install{ nullptr, w, id }); }

        /// @brief Forgets an installation on a window (called on window destruction or RemoveEventFilter)
        void forgetInstall(Window* w);

        /// @brief Forgets an installation on a widget (called on widget destruction or RemoveEventFilter)
        void forgetInstall(Widget* w);

        /// @brief After a widget is moved (AddWidget value takeover), replaces the old address in the installation record with the new one
        /// @note Called only by Widget's move constructor -- the address changed, so the ledger must follow.
        void relinkWidget(Widget* from, Widget* to) {
            for (Install& r : installs_)
                if (r.widget == from) r.widget = to;
        }
    };

} // namespace Kit
