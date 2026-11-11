/**
 * @file ZeLiveText.h
 * @brief Data-driven text: the library refreshes the content on a tick, the app writes no loop
 *
 * @details
 * ## What problem it solves
 *
 * "This piece of text must keep changing" is a common need of every tool-type app: market prices, connection status,
 * collection counts, clocks, progress descriptions. The naive approach comes in two forms, both wrong:
 *
 * @code
 * // X Approach one: loop on the UI thread
 * while (running) {
 *     label.SetText(poll());
 *     Sleep(200);           // <- during these 200ms the window is completely unresponsive: no repaint, no input handling
 * }
 *
 * // X Approach two: spawn a background thread and touch the widget directly
 * std::thread([&]{
 *     while (running) {
 *         label.SetText(poll());   // <- touching the widget across threads: data race + sometimes crashes, sometimes fails to paint
 *         Sleep(200);
 *     }
 * }).detach();
 * @endcode
 *
 * There is only one correct shape: hand the tick to the library. `LiveText` is exactly that thin wrapper --
 * it uses `Window::SetInterval` to hook itself into the main loop's time slice, pulls the data once per tick,
 * and repaints only when it changed. The UI thread is never blocked for a single millisecond.
 *
 * ## Two data sources
 *
 * | Source | Interface | Applies to |
 * |---|---|---|
 * | Pull (the library calls you on a timer) | `SetPull(fn, 200)` | Data lives in your own process (counter / state machine / global variable) |
 * | Push (you feed it from any thread) | `Post(text)` | Data comes from another thread (network callback / collection thread / log) |
 *
 * Both can be used at once: content from `Post` overrides the current display, and the next tick overrides it again with the pull result
 * (or the other way round -- depending on what your `SetPull` returns).
 *
 * @par Usage
 * @code
 * Kit::LiveText status("Disconnected");
 * status.SetPull([&]{ return link.isConnected() ? "Connected" : "Disconnected"; }, 500);
 * win.AddWidget(status);
 *
 * // Another path: the collection thread pushes directly (thread-safe)
 * std::thread([&, &status]{
 *     for (;;) {
 *         status.Post("Collected " + std::to_string(count()) + " items");
 *         std::this_thread::sleep_for(std::chrono::seconds(1));
 *     }
 * }).detach();
 * @endcode
 *
 * @note First refresh: `SetPull` pulls once immediately (the UI should not show an empty frame before the content appears);
 *       afterwards it follows the interval.
 * @note The tick starts at "on screen": while the widget is not yet attached to a window it cannot register a task, so it
 *       starts on the first `Paint`. Therefore between `SetPull` and the first paint there is no tick --
 *       in normal scenarios that is "the next paint", which is imperceptible.
 * @note An unchanged value does not repaint (can be turned off with `SetRedrawOnlyOnChange`): polling a constant value
 *       should not keep burning CPU. This is where it saves more than "SetText directly every tick".
 * @note Text comparison is byte by byte. When formatting numbers, control the decimal places yourself, otherwise
 *       high-frequency jitter like "1.2300001 -> 1.23" keeps it repainting (that is the correct behavior,
 *       but expensive) -- formatting should happen inside `pull`.
 *
 * @see SetInterval / SetTimeout / Post in Zekit.h (lower-level general scheduling)
 * @see ZeLabel.h read-only text (the base class of this class)
 */

#pragma once
#include "ZeLabel.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace Kit {

    /**
     * @brief Data-driven text widget (periodic pull / cross-thread push)
     *
     * @details
     * Inherits `Label`: font, alignment, overflow, multi-line all stay the same; this class only adds "where the content comes from"
     * and "how often it comes". So its paint code is zero lines -- after receiving new text it is still
     * the familiar label.
     */
    class LiveText : public Label {
    public:
        /// @brief Pull function: called on the UI thread, returns the text to display
        /// @note Reading global variables / calling lightweight state queries here is allowed. Do not do
        ///       blocking IO here (network requests, reading large files) -- it runs on the UI thread, blocking it blocks the UI.
        ///       Put slow work on a background thread and send the result back with `Post`.
        using PullFn = std::function<std::string()>;

        /// @brief Constructs (empty text)
        LiveText();
        /// @brief Constructs with an initial text (this is what is shown before the first tick arrives)
        explicit LiveText(std::string initial);

        /// @brief Destructor: unregisters the timer task, disconnects the landing point of cross-thread pushes
        /// @note Must be written explicitly (rather than `= default`): the window's task table holds a callback
        ///       that captures `this`; not clearing it means a dangling call.
        ~LiveText() override;

        // * A destructor is declared -> the move constructor is no longer implicitly generated (C++ rule), and the copy constructor
        //   is deleted at the Widget layer. The result is that `win.AddWidget(live)` tries to call copy and
        //   fails to compile. So the move must be added back manually (`= default` member-wise move is enough,
        //   exactly the same as the Label path). This is the fixed cost of a custom destructor in a value-semantics widget.
        LiveText(LiveText&&) noexcept = default;
        /// @note Assignment stays disabled: once a widget's address is attached it cannot change (consistent with the Widget convention).
        LiveText& operator=(LiveText&&) = delete;

        // -------------------------------------------------------------------
        //  Data source: pull
        // -------------------------------------------------------------------

        /**
         * @brief Sets the pull function and the polling interval (pulls once immediately)
         * @param fn Pull function (called on the UI thread); pass empty = stop pulling (does not affect `Post`)
         * @param intervalMs Interval (milliseconds, >=1; pass 0 = keep only manual `Refresh`)
         * @note Calling it again unregisters the old task first, then registers the new one (the correct way to change the interval).
         * @note `fn` is only called on the UI thread, so reading widget state is safe.
         */
        void SetPull(PullFn fn, int intervalMs = 200);

        /// @brief Stops periodic pulling (keeps the `pull` function, can `Refresh` manually at any time)
        void Stop();

        /**
         * @brief Changes only the polling interval, does not touch the pull function
         * @param intervalMs New interval (milliseconds; <=0 = stop polling, equivalent to `Stop()` but keeps the function)
         * @note Internally it is "unregister old task + register new task" -- the window's task table does not support changing the interval in place.
         *       So it restarts timing immediately with the new tick, without waiting for the old period to finish.
         * @note Before it is on screen it only records the value and registers with the new value on the first paint.
         * @see SetPull (changes both the function and the interval)
         */
        void SetInterval(int intervalMs);

        /// @brief Current polling interval (0 = not polling)
        int interval() const { return intervalMs_; }
        /// @brief Whether periodic pulling is active
        bool isLive() const { return intervalMs_ > 0 && pull_ != nullptr; }

        /// @brief Pulls once immediately (without waiting for the next tick)
        /// @note No-op when `pull` has never been set.
        void Refresh();

        /// @brief Whether to skip repaint when the content is unchanged (default true)
        /// @param on false = repaint every tick (for cases where the content looks the same but must refresh, such as a timestamp)
        void SetRedrawOnlyOnChange(bool on) { redrawOnlyOnChange_ = on; }
        bool redrawOnlyOnChange() const { return redrawOnlyOnChange_; }

        // -------------------------------------------------------------------
        //  Data source: push (thread-safe)
        // -------------------------------------------------------------------

        /**
         * @brief Pushes a piece of text up from any thread
         * @param text The content to display
         *
         * @details
         * The calling thread does only two things: put the text into a locked slot, and wake the UI thread. The actual
         * `SetText` happens on the UI thread (via `Window::Post`), so there is no
         * "touching the widget across threads".
         *
         * @note Only the latest one is kept: when the UI thread cannot keep up, the intermediate ones are overwritten --
         *       which is exactly the semantics a status display wants (the user sees "what it is now", not the history).
         * @note Calling it while the widget is not yet attached to a window (or already destroyed) is a safe no-op.
         * @warning The caller must ensure `this` is still alive before the UI thread processes it. The library does not manage
         *          lifetime for the caller -- background threads should usually stop before the window.
         */
        void Post(std::string text);

        // -------------------------------------------------------------------
        //  Paint
        // -------------------------------------------------------------------

        /// @brief Inherits Label's paint; also takes on the duty of "starting the timer on first on-screen"
        void paintContent(FrameCollector& fc) const override;

    private:
        /// @brief Shared slot for cross-thread pushes (shared by the widget and the posting lambda, released when the widget is destroyed)
        struct Shared;

        /// @brief If not yet registered and already attached to a window, registers the periodic task (idempotent)
        void ensureAttached();
        /// @brief Registers the periodic task according to the current `pull_` / `intervalMs_` (clears the old one first)
        void startTimer();
        /// @brief One tick: pull once, set the text if it changed
        void tick();
        /// @brief Unregisters the timer task (shared by destructor / interval change / Stop)
        void detachTimer();

        std::shared_ptr<Shared> shared_;   /// < Cross-thread push slot (the only thing that needs to cross threads)

        PullFn pull_;               /// < Pull function
        int    intervalMs_ = 0;     /// < Polling interval (0 = no polling)
        uint64_t timerId_ = 0;      /// < Window::TimerId (stored as uint64 to avoid this header depending on Zekit.h)
        bool   redrawOnlyOnChange_ = true;  /// < Whether to skip repaint when the content is unchanged
        bool   attached_ = false;   /// < Whether registration has been attempted (prevents retrying every frame)
    };

} // namespace Kit
