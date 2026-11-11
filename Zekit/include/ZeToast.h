// Toast: transient floating notification strip with auto dismiss and fade.
// Add it to a window with AddWidget and call Show() from anywhere; messages then
// stack inward from the dock edge while they live.
//
// Three mechanisms, all already provided by the library:
//  - Overlay: paintsOnTop() returns true while messages exist, so drawing is
//    deferred to the second pass and stays above ordinary widgets. HitTest() is
//    always false (events pass through) unless SetInteractive() is enabled.
//    paintContent() calls ResetClip() to escape ancestor clipping.
//  - Fade: per-item alpha derives from elapsed lifetime (linear over fadeMs);
//    wantsAnimationFrame() reports it, the same contract as ProgressBar.
//  - Timed dismissal: RequestFrameWake is renewed per frame and stops once the
//    stack is empty, so an idle toast drops back to zero CPU.
//
// Pure overlay: hint size is always 0, so use win.AddWidget(toast).
// durationMs <= 0 means persistent; such a strip only animates while fading in.

#pragma once
#include "ZeBoxWidget.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Kit {

    class OverlayWindow;   // Native overlay carrier, shared per layer; see ZeOverlayWindow.h

    // Semantic level of a toast; selects the accent bar color.
    enum class ToastLevel {
        Info,      // Informational (primary color)
        Success,   // Success (green)
        Warning,   // Warning (orange)
        Error      // Error (red)
    };

    // Dock position of the toast stack.
    enum class ToastPlace {
        TopCenter,     // Top center
        TopRight,      // Top right; stacks downward from the top edge
        BottomCenter,  // Bottom center
        BottomRight,   // Bottom right; stacks upward from the bottom edge (default)
        Center,        // Center of the screen
        // Appended at the end so existing enum values keep their numbers, since a
        // stored value would otherwise shift. ToastNotifier anchors its window to
        // the screen corners, and the embedded fallback maps those anchors back to
        // ToastPlace, so the left column is required for a complete mapping.
        TopLeft,       // Top left; stacks downward from the top edge
        BottomLeft     // Bottom left; stacks upward from the bottom edge
    };

    // Notification sound mode, same semantics as the legacy ZeCore notifications.
    enum class ToastSound {
        System,   // Follow the system beep (MessageBeep mapped by level; silent if muted) - default
        Custom,   // Custom: play soundFile (wav) if set, otherwise call the customSound function
        Off       // Silent
    };

    // Toast: one widget manages a stack of messages that fade out on their own.
    class Toast : public BoxWidget {
    public:
        // Destructor lives in the .cpp: unique_ptr<OverlayWindow> needs a complete type.
        ~Toast() override;

        // Value semantics: moved by AddWidget. The explicit destructor suppresses
        // the implicit move, so it has to be restored here.
        Toast(Toast&&) noexcept;

        Toast();

        // -------------------------------------------------------------------
        //  Pushing messages
        // -------------------------------------------------------------------

        // Push a message. text may contain '\n' for line breaks; level selects the
        // accent bar color; durationMs <= 0 means persistent (no auto dismiss).
        void Show(std::string text, ToastLevel level = ToastLevel::Info, int durationMs = 2200);

        // Convenience wrappers per level.
        void ShowInfo(std::string text, int durationMs = 2200);
        // Success level
        void ShowSuccess(std::string text, int durationMs = 2200);
        // Warning level
        void ShowWarning(std::string text, int durationMs = 3000);
        // Error level
        void ShowError(std::string text, int durationMs = 3000);

        // Remove all messages immediately, without fading out.
        void Clear();

        // Current message count.
        std::size_t count() const { return items_.size(); }
        // Whether anything is showing.
        bool isShowing() const { return !items_.empty(); }

        // -------------------------------------------------------------------
        //  Appearance and behavior
        // -------------------------------------------------------------------

        // Dock position (default bottom right).
        void SetPlace(ToastPlace p);
        ToastPlace place() const { return place_; }

        // Maximum visible messages; the oldest is dropped first. Default 4, <= 0 = unlimited.
        void SetMaxVisible(int n);
        int maxVisible() const { return maxVisible_; }

        // Maximum width of one strip in pixels; 0 = fit text naturally (default 340).
        void SetMaxWidth(float w);
        // Margin to the window edge (default 24).
        void SetMargin(float m);
        // Gap between two strips (default 8).
        void SetItemGap(float g);
        // Fade in and fade out duration each (default 180ms).
        void SetFadeDuration(int ms);

        // Strip background color.
        void SetBackground(const Color4f& c);
        // Strip text color.
        void SetTextColor(const Color4f& c);
        // Strip text size in pixels.
        void SetFontSize(unsigned int pixelSize);

        // -------------------------------------------------------------------
        //  Interaction (only for the standalone window form; the host overlay
        //  stays click-through)
        // -------------------------------------------------------------------
        // Enable interaction: draw a close button and allow click to close (default off).
        // ToastNotifier turns this on automatically after creating its carrier.
        void SetInteractive(bool on);
        bool isInteractive() const { return interactive_; }
        // Whether a click on empty card area also closes it (default true; interactive only).
        void SetClickToClose(bool on);

        // -------------------------------------------------------------------
        //  Sound (System = MessageBeep mapped by level; Custom = wav file or function)
        // -------------------------------------------------------------------
        void SetSound(ToastSound mode);
        ToastSound sound() const { return sound_; }
        // Custom mode wav path; takes precedence over customSound, played asynchronously.
        void SetSoundFile(const std::string& path);
        // Custom mode callback, used when no wav file is set.
        void SetCustomSound(std::function<void()> fn);

        // -------------------------------------------------------------------
        //  Measurement protocol (a pure overlay occupies no layout space)
        // -------------------------------------------------------------------
        // Returns the explicit SetSize value if set, otherwise 0.
        float hintWidth() const override;
        // Same as hintWidth.
        float hintHeight() const override;

        // Measure the natural size of the whole stack, excluding dock margins.
        // width and height receive the outer size in pixels.
        // Returns false when the stack holds no messages (both outputs are then 0).
        //
        // hintWidth() / hintHeight() answer how much layout space the overlay takes,
        // which is always 0 for a pure overlay. This function answers a different
        // question: how large the stack actually is. That is the only input
        // ToastNotifier needs, because its window must match the stack exactly or
        // the strips become one large block of background covering the UI.
        //
        // The width rule matches computeStack (including the SetMaxWidth clamp) minus
        // the final clamp to the window width, whose input is this function's own
        // output: feeding that back would shrink the window on every call.
        // Measure only after the window is on screen; text width comes from
        // Window::MeasureText and font faces load on Show(), so before that the size
        // is estimated from the font size and ToastNotifier re-measures to correct it.
        bool measureStack(float& width, float& height) const;

        // -------------------------------------------------------------------
        //  Animation contract (three rules, same as ProgressBar)
        // -------------------------------------------------------------------
        // True while any message is fading in, fading out, or still counting down.
        // The host keeps scheduling frames on that basis and drops to zero CPU once
        // everything is still. This answers only for itself; recursion is the host's job.
        bool wantsAnimationFrame() const override;

        // -------------------------------------------------------------------
        //  Overlay (never blocks input)
        // -------------------------------------------------------------------
        // True while messages exist, so Window defers painting to the top overlay pass.
        bool paintsOnTop() const override;
        // Always false: strips never take part in hit testing, so events pass through.
        bool HitTest(float x, float y) const override;

        // -------------------------------------------------------------------
        //  Theme injection
        // -------------------------------------------------------------------
        // Re-inject box, text, the four semantic colors, and the accent bar width.
        void ApplyTheme(const Theme& t) override;

    protected:
        // Default box style comes from the toast section of the theme.
        const BoxStyle& themeBox(const Theme& t) const override;

        // Fully overrides the base paint template: draws nothing when there are no messages.
        void Paint(FrameCollector& fc) const override;
        // Draw the whole stack; each strip is background, accent bar, icon circle, text, close glyph.
        void paintContent(FrameCollector& fc) const override;
        // Draw the stack; with local = true the origin is the stack's top-left (native carrier).
        void paintStack(FrameCollector& fc, bool local) const;

        // -------------------------------------------------------------------
        //  Native overlay carrier (OverlayWindow path; empty while interactive or
        //  on fallback platforms)
        // -------------------------------------------------------------------

        // Whether the overlay is currently hosted by a native popup window.
        bool nativePopup() const;
        // Sync the carrier with the current messages and dock position; hide when empty,
        // repaint only when the geometry is unchanged.
        void syncOverlay() const;

        // ---- Interaction (only while interactive_; the host overlay stays click-through) ----
        //  HitTest is declared publicly; see its comment for the region active in interactive mode.
        bool OnMousePress(MouseEvent& e) override;
        bool OnMouseMove(MouseEvent& e) override;
        bool OnMouseRelease(MouseEvent& e) override;
        void OnMouseLeave() override;

    private:
        // One message.
        struct Item {
            std::string text;
            ToastLevel  level = ToastLevel::Info;
            long long   bornMs = 0;        // Creation time in steady_clock milliseconds
            int         durationMs = 2200; // <= 0 = persistent
        };

        // Outer rect of one message.
        struct RowRect { float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f; };

        // Native overlay carrier; non-null only when not interactive and the desktop
        // supports popups. mutable because syncing happens in the const Paint path:
        // the carrier is positioned right before drawing, which avoids having to call
        // a sync from every message mutator.
        mutable std::unique_ptr<OverlayWindow> overlay_;
        // Last stack geometry pushed to the carrier; equal geometry means repaint only.
        mutable float overlayX_ = 0.0f, overlayY_ = 0.0f;
        mutable float overlayW_ = 0.0f, overlayH_ = 0.0f;

        // Geometry of the whole stack, shared by painting and hit testing so what is
        // drawn and what is clicked can never diverge.
        struct StackGeom {
            float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
            std::vector<RowRect> rows;   // One rect per message, in items_ order
            bool  valid = false;
        };

        // Compute the stack geometry; pure query, no state written.
        StackGeom computeStack() const;
        // Height of one strip: lines x line height + vertical padding + border.
        float itemHeight(const Item& it) const;
        // Natural width of one strip: longest line + accent bar + horizontal padding + border.
        float itemWidth(const Item& it) const;
        // Visual height of one text line; measured from the window when available, else estimated.
        float lineHeight() const;
        // Current opacity of one strip in 0..1, linear at both fade ends.
        float alphaOf(const Item& it, long long nowMs) const;
        // Remove expired strips and decide whether another frame is needed.
        // const is the established shape for in-library animation; items_ is mutable.
        void  stepAnimation() const;
        // Whether the stack needs another frame right now.
        bool  needNextFrame(long long nowMs) const;
        // Request a repaint, waking the main loop across threads.
        void  requestRepaint();

        // Accent bar color for a level.
        Color4f accentOf(ToastLevel level) const;
        // Horizontal span taken by the icon circle, including its gap to the text (0 when no icon).
        float iconSpan() const { return (iconSize_ > 0.0f) ? iconSize_ + iconGap_ : 0.0f; }

        // ---- Interaction helpers (shared geometry for painting and hit testing) ----
        // Which strip contains the point (-1 = none; needs interactive mode and a valid stack).
        int rowAtPoint(float px, float py) const;
        // Close button rect of one strip, given as center plus side length.
        void closeRectOf(const RowRect& r, float& cx, float& cy, float& size) const;
        // Whether the point is on the close button of that strip.
        bool hitCloseOf(const RowRect& r, float px, float py) const;
        // Remove strip idx; the event-path close exit, triggers repaint and frame renewal.
        void removeAt(std::size_t idx);
        // Play the notification sound (called from Show; silent outside Windows).
        void playSound(ToastLevel level);

        // ---- Messages (mutable: const rendering prunes expired entries, see stepAnimation) ----
        mutable std::vector<Item> items_;

        // Text style of the strips. BoxWidget has no such member (that belongs to
        // TextWidget); this widget needs only a text style, not single-line layout,
        // so it carries one instead of changing the inheritance chain.
        TextStyle textStyle_;

        // ---- Layout parameters ----
        ToastPlace place_ = ToastPlace::BottomRight;
        int    maxVisible_ = 4;
        float  maxWidth_ = 340.0f;
        float  margin_ = 24.0f;
        float  itemGap_ = 8.0f;
        int    fadeMs_ = 180;

        // ---- Semantic colors injected by the theme, selected per level ----
        float   accentWidth_ = 4.0f;   // Accent bar width (0 = do not draw)
        Color4f accentInfo_{ 0, 0, 0, 0 };
        Color4f accentSuccess_{ 0, 0, 0, 0 };
        Color4f accentWarning_{ 0, 0, 0, 0 };
        Color4f accentError_{ 0, 0, 0, 0 };

        // ---- Level icon circle. The standalone window notification needs it: a plain
        //      white rectangle reads as clearly worse than a system notification, and
        //      the disc takes the level accent color. ----
        float   iconSize_ = 22.0f;     // Icon circle diameter (0 = do not draw)
        float   iconGap_ = 10.0f;      // Gap between icon circle and text
        Color4f iconGlyph_{ 1, 1, 1, 1 };  // Icon glyph color

        // ---- Interaction and sound ----
        bool interactive_ = false;         // Interactive mode (on for the standalone window; off for host overlay)
        bool clickToClose_ = true;         // Clicking empty card area also closes
        ToastSound sound_ = ToastSound::System;
        std::string soundFile_;            // Custom mode: wav path
        std::function<void()> customSound_; // Custom mode: function used when no wav is set
        float   minWidth_ = 300.0f;        // Minimum strip width, injected by the theme
        Color4f closeGlyph_{ 0, 0, 0, 0 };
        Color4f closeHoverBg_{ 0, 0, 0, 0 };

        // ---- Hover and press state (mutable: read during const painting) ----
        mutable bool hoverAny_ = false;    // Pointer is over a card (visual only; timers keep running)
        mutable int  hoverRow_ = -1;       // Hovered row (-1 = none)
        mutable bool hoverClose_ = false;  // Pointer is over the close button
        int  pressedCloseRow_ = -1;        // Row whose close button is pressed
        bool pressedBody_ = false;         // Press started on empty card area (for clickToClose)
    };

} // namespace Kit
