// Modal dialog: a real Win32 window, called like an ordinary widget.
//
// It is a genuine OS window (ChildKind::Owned: system title bar, close box, drag,
// border), owned by the host, destroyed with it, absent from the taskbar, and
// sharing the host's render context so the font atlas is not duplicated in VRAM.
//
// Three design points:
//  1. Lifetime: Show() calls host.CreateChildWindow(...), so the object joins the
//     host's managed table -- the same promise as win.AddWidget(btn). It survives
//     the enclosing scope, so `Kit::ModalDialog::Show(win);` alone is complete
//     usage. ModalDialog is a movable lightweight handle, not the immovable Window.
//  2. Modality: opening calls host.SetEnabled(false) (Win32 EnableWindow(host,
//     FALSE)), so the OS greys out the host and blocks its input while the owned
//     dialog stays interactive. True modality without intercepting events.
//  3. Scrim: a separate window cannot cover the host, so the scrim is a
//     top-painted overlay widget on the host. It is purely visual; input blocking
//     is item 2, so SetScrimEnabled(false) only skips the darkening.
//
// Sound: SoundProfile is semantic -- the library does not define which wav to
// play. The default maps to the platform sound (Win32 MessageBeep); override it
// with SetOnSoundRequest / SetOnSoundFile or disable it with SetSoundEnabled.
//
// Mobile has no native window, so Window degrades to an in-host stand-in and this
// class needs no #if branches. Desktop never uses an overlay: an overlay must
// intercept events itself and cannot get the title bar, drag or DWM shadow.
//
// Clicking the empty area cannot close the dialog -- with a disabled host those
// events never arrive -- so that semantic is intentionally absent.
//
// Esc requests a close (OnRequestClose); Enter hits the default button
// (SetDefaultButtonName). The dialog is centered on the host client area on open.
//
// See Docs/42 (modal dialog) and Zekit.h's ChildKind / CreateChildWindow.

#pragma once
#include "Zekit.h"            // Window (real window) / Panel (content root); no cycle: Zekit.h does not know this header
#include "ZeSelfAction.h"
#include "ZeTheme.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>

namespace Kit {

    // Semantic category of the sound cue (not a specific sound file).
    // The library only decides which kind of cue fits the moment; the platform
    // maps the default to the system sound and the app may override it.
    enum class SoundProfile {
        None,       // Silent (same as SetSoundEnabled(false))
        Info,       // Notification / ordinary dialog shown
        Warning,    // Warning (default: a dialog usually wants attention)
        Question,   // Question (confirm / choose)
        Error,      // Failure / dangerous action
        Success,    // Completed
    };

    namespace detail {
        // Shared state of a dialog (created by Show; referenced by the handle and
        // by the window-close callback).
        //
        // The handle is a value that can be dropped, while the window is owned by
        // the library; this shared_ptr connects the two -- Window::SetOnClosed
        // finds the state through a weak_ptr and fires the user's onClosed. If the
        // handle is gone the lock fails and nothing is sent.
        //
        // Restoring the host and unmounting the scrim do not depend on the handle
        // still being alive.
        struct ModalState {
            Window* host = nullptr;        // Host window (not owned)
            Window* win  = nullptr;        // The dialog window itself (owned by host, not owned here)
            Panel*  content = nullptr;     // Content root (child of win, stretches to fill)

            std::string title;

            // ---- Appearance / behavior ----
            bool    scrimEnabled = true;                  // Whether the host scrim is painted
            Color4f scrimColor{ 0.0f, 0.0f, 0.0f, 0.0f }; // alpha 0 = follow theme

            bool    soundEnabled = true;
            SoundProfile soundProfile = SoundProfile::Warning;
            std::string  soundFile;

            bool    closable = true;         // Whether the system close box is enabled (SetClosable)

            std::string defaultButtonName;   // Triggered by Enter
            std::string cancelButtonName;    // Triggered by Esc

            // Set by Accept() so the close-request hook lets the programmatic path through
            bool forceClose = false;

            // ---- Callbacks ----
            std::function<bool(SoundProfile)> onSoundRequest;
            SelfAction onClosed;
            SelfAction onAccepted;
            std::function<bool(Widget&)> onRequestClose;   // true = close allowed
        };
    } // namespace detail

    // Modal dialog: a lightweight handle to a real window.
    //
    // This is not a Widget -- it wraps a Kit::Window. It is a movable value:
    // Show() returns it by value, so it can be captured for configuration or
    // discarded; the real object is owned by the host either way.
    class ModalDialog {
    public:
        // Open options (value semantics; the defaults describe a regular dialog).
        struct Options {
            std::string title;                  // Title bar text
            float width  = 420.0f;              // Client size in pixels
            float height = 260.0f;
            bool  scrim        = true;          // Paint the scrim (on by default)
            bool  sound        = true;          // Play a sound cue (on by default)
            SoundProfile soundProfile = SoundProfile::Warning;
            bool  resizable    = false;         // Allow resizing by dragging the border
        };

        // ===================================================================
        //  Creation
        // ===================================================================

        // Recommended entry point: open a modal dialog on the host, return its handle.
        //
        // host must outlive the dialog (the dialog is owned by it). The return value
        // may be ignored -- the object lives in the host's managed table and stays
        // alive as long as the host does (the CreateChildWindow promise).
        //
        // Order: create the native window, set title and size, center it, disable the
        // host, mount the scrim, play the cue. Any failure (platform without native
        // windows) leaves a closed handle whose every call is a safe no-op.
        //
        // Why two overloads instead of `Options opt = Options()`: GCC rejects a
        // default member initializer of a nested class used as a default argument
        // before the end of its enclosing class. MSVC accepts it, so the entry point
        // is split into "one argument = all defaults" and "two arguments = explicit",
        // leaving call sites unchanged.
        static ModalDialog Show(Window& host);
        static ModalDialog Show(Window& host, Options opt);

        // Default constructor = empty handle (isOpen() is false, all calls are no-ops).
        // Use it as a "not shown yet / already gone" placeholder; use Show() to open.
        ModalDialog() = default;

        // ---- Value semantics: move only (two copies would close twice) ----
        ModalDialog(ModalDialog&&) noexcept = default;
        ModalDialog& operator=(ModalDialog&&) noexcept = default;
        ModalDialog(const ModalDialog&) = delete;
        ModalDialog& operator=(const ModalDialog&) = delete;

        // Destructor does not close the dialog -- the library-owned window stays.
        // Intentional: `Show(win);` must work, and closing here would make that
        // one-liner flash and vanish.
        ~ModalDialog() = default;

        // ===================================================================
        //  Content
        // ===================================================================

        // Add content to the dialog (takes ownership; laid out in the inner Panel).
        // Same shape as win.AddWidget(btn) -- that is what "call it like a widget" means.
        template<class T>
        T& AddWidget(T&& w) {
            return m_state->content->AddWidget(std::forward<T>(w));
        }

        // Content root panel (padding and gap preset; SetPadding / SetGap still apply)
        Panel& content() const { return *m_state->content; }

        // ===================================================================
        //  Title / geometry
        // ===================================================================

        void SetTitle(std::string title);
        const std::string& title() const;

        // Set the client size in pixels; effective immediately while open
        void SetWindowSize(float w, float h);
        // Client width in pixels; 0 when the handle is invalid
        float windowWidth() const;
        // Client height in pixels; 0 when the handle is invalid
        float windowHeight() const;

        // Re-center on the host client area
        void CenterInHost();

        // Whether the border can be dragged to resize
        void SetResizable(bool on);

        // ===================================================================
        //  State / closing
        // ===================================================================

        // Whether the dialog is still open (false when the handle is invalid)
        bool isOpen() const;

        // Close the dialog (idempotent). Restores the host, unmounts the scrim and
        // fires OnClosed.
        // Closing is not destroying: the object stays in the host's managed table
        // (reclaim it with Window::DestroyClosedChildWindows()).
        void Close();

        // Confirm: fire OnAccepted then Close()
        void Accept();

        // Cancel: go through the OnRequestClose hook, then Close() if not blocked
        void Reject();

        // Show or hide the system close box; with it hidden, Esc still requests a close
        void SetClosable(bool on);
        // Whether the system close box is currently enabled
        bool isClosable() const;

        // ===================================================================
        //  Scrim (the darkening layer on the host)
        // ===================================================================

        // Whether the scrim is painted (on by default)
        // Visual only: modality comes from disabling the host, not from the scrim.
        void SetScrimEnabled(bool on);
        // Whether the scrim is currently painted
        bool scrimEnabled() const;

        // Scrim color (alpha 0 = follow the theme's modalDialog.scrimColor)
        void SetScrimColor(const Color4f& c);

        // ===================================================================
        //  Sound
        // ===================================================================

        // Whether a sound cue is wanted (on by default)
        void SetSoundEnabled(bool on);
        // Whether a sound cue is currently wanted
        bool soundEnabled() const;

        // Semantic category of the cue (Warning by default)
        void SetSoundProfile(SoundProfile p);
        // Current cue category
        SoundProfile soundProfile() const;

        // Play the cue manually (Show() does it automatically; apps may call it later)
        void PlaySound();

        // Replace the default cue with your own (return false = not handled, use default)
        void SetOnSoundRequest(std::function<bool(SoundProfile)> cb);

        // Convenience: use an audio file as the cue (best effort per platform)
        void SetOnSoundFile(std::string path);

        // ===================================================================
        //  Buttons
        // ===================================================================

        // Set the objectName of the default button (triggered by Enter); ignored if not found
        void SetDefaultButtonName(std::string name);
        // Set the objectName of the cancel button (triggered by Esc; when unset, Esc
        // goes straight to the close-request hook)
        void SetCancelButtonName(std::string name);

        // ===================================================================
        //  Callbacks
        // ===================================================================

        // After closing, for any reason (close box / Esc / Accept / programmatic Close)
        // Inside the callback isOpen() is already false and the host is restored.
        void SetOnClosed(SelfAction cb);

        // User confirmed (Accept())
        void SetOnAccepted(SelfAction cb);

        // User requested a close (close box / Esc): asked before closing, you decide.
        //
        // cb returns true = allow the close, false = block it (window stays open).
        // This is where "confirm before discarding unsaved changes" belongs.
        //
        // Do not call Close() from the callback to "allow" it -- that re-enters the
        // same hook (recursion). Return true instead. To show a second confirmation
        // dialog first, call Close() from that dialog's callback, where you are no
        // longer on this stack.
        // An empty callback allows every close.
        void SetOnRequestClose(std::function<bool(Widget&)> cb);

        // ===================================================================
        //  Low-level escape hatch
        // ===================================================================

        // The underlying native window (nullptr when the handle is invalid)
        Window* nativeWindow() const;

        // Content root (useful with Kit::As<Panel> inside callbacks; rarely needed)
        Widget* sender() const;

    private:
        std::shared_ptr<detail::ModalState> m_state;
    };

} // namespace Kit
