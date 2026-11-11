/**
 * @file ZePopupHost.h
 * @brief Base class for widgets with a native popup carrier (shared by the four popup widgets: combo box / time / date / color)
 *
 * @details
 * ## What it solves
 *
 * The popups of these four widgets have three possible paths:
 *  1. Host self-paint (second pass of `paintsOnTop()`) -- always available, but the popup cannot leave the host window;
 *  2. Native carrier (`OverlayWindow`) -- the popup is a real window that can cover the canvas / child windows;
 *  3. Collapsed -- nothing is drawn.
 *
 * The state machine is the same for all three forms: `open_` records the logical switch, the anchor being clipped means collapse, being taken over by a later widget in the same layer means collapse, clicking elsewhere in the host means collapse, and the carrier not being ready falls back to self-paint.
 * This "carrier wiring" was previously written once in each of the four .cpp files (`showNativePopup` 35 lines x4, of which the ColorPicker and DateTimePicker copies were verbatim identical), with a high risk of changing one and forgetting another -- for example, missing the "collapse when taken over" rule shows up as "open the combo box and the calendar still hangs beside it".
 *
 * So the carrier wiring is collected into this base class; the derived class only has three things left:
 *
 * | What the derived class implements | Purpose |
 * |---|---|
 * | `popupRect()` | Popup rectangle (window absolute coordinates); returns false = should not pop up now |
 * | `paintPopupInto()` | Paints the popup into the collector (when `localPopup=true` the origin is the popup top-left corner) |
 * | `onPopupPress/Release/Move/Wheel/Leave()` | Input inside the popup (coordinates are already in host terms) |
 * | `dismissPopup()` | "Should collapse" (the collapse side effects differ per widget: clear draft / clear highlight) |
 *
 * ## Why it lives in `include/` instead of inside the library
 *
 * The derived classes are all in public headers (`ZeComboBox.h` etc.), so the base class must be visible at the same level -- a public header must not depend in reverse on library-internal headers (otherwise downstream code that only gets `include/` fails to compile). So it is a public base class, but only exposes `protected` hooks and does not form a call surface.
 *
 * @note The carrier is shared per layer (`OverlayHost`): on the same host window, all `Panel` layer popups share one window. So "open me" may also mean "someone else must collapse" -- this is exactly what `SetOnTakenOver` is for.
 *
 * @see OverlayWindow   Public interface of the carrier (position / content / mouse pass-through)
 * @see ZeAnchorVisible.h  Anchor visibility criterion (`popupActive()` uses it)
 */

#pragma once

#include "ZeTextWidget.h"

#include <memory>

namespace Kit {
    class OverlayWindow;      /// < Native popup carrier (the complete type is only needed in the .cpp)
    class FrameCollector;     /// < Display list collector

    /**
     * @brief Common base class for popup widgets: handles the carrier lifecycle, not what the popup looks like
     *
     * @details
     * Inheritance chain: `Widget -> BoxWidget -> TextWidget -> PopupHostWidget -> concrete widget`.
     * The concrete widget is still a `TextWidget` externally; the existing API and usage are fully unchanged.
     *
     * @par When the carrier is created / destroyed
     * Created only on the first real popup (it must already be on screen before `Show`, see the notes on `OverlayWindow`);
     * collapse only calls `Hide` (reuse the window, avoiding rebuilding the swap chain on every open);
     * destroyed with the widget (`unique_ptr`).
     *
     * @par Carrier unavailable (mobile / insufficient capability)
     * `OverlayWindow::SupportsNative()` is false -> this base class returns silently and the derived class continues on the host self-paint path of `paintsOnTop()`. The geometry / painting / input of both paths are the same two implementations, so the fallback does not introduce a second set of logic.
     */
    class PopupHostWidget : public TextWidget {
    public:
        ~PopupHostWidget() override;

        /// @brief Value semantics: moves with `AddWidget` (an explicit destructor suppresses the implicit move, so it must be restored)
        /// @note Only the move constructor is restored: the widget itself is not assignable (the `Widget` assignment operator is deleted),
        ///       and forcing in a move assignment would only hit the base class deleted declaration at compile time.
        PopupHostWidget(PopupHostWidget&&) noexcept;

        PopupHostWidget(const PopupHostWidget&) = delete;
        PopupHostWidget& operator=(const PopupHostWidget&) = delete;

    protected:
        PopupHostWidget();

        // -------------------------------------------------------------------
        //  Hooks the derived class must implement
        // -------------------------------------------------------------------

        /// @brief Popup rectangle (window absolute coordinates, same terms as the anchor)
        /// @param x,y,w,h Output
        /// @return false = should not pop up now (size cannot be computed / data empty / anchor clipped)
        /// @note The geometry is the one shared by painting and hit testing (`computePopup()`) -- here you only give
        ///       where and how large the carrier should be placed; do not compute a second set here.
        virtual bool popupRect(float& x, float& y, float& w, float& h) const = 0;

        /// @brief Paints only the popup (without the host background)
        /// @param localPopup true = origin is the popup top-left corner (carrier path); false = host coordinates (self-paint path)
        virtual void paintPopupInto(FrameCollector& fc, bool localPopup) const = 0;

        /// @brief Mouse inside the popup (coordinates converted by the carrier back to host terms)
        virtual bool onPopupPress(MouseEvent& e) = 0;
        virtual bool onPopupRelease(MouseEvent& e) = 0;
        virtual bool onPopupMove(MouseEvent& e) = 0;
        virtual bool onPopupWheel(MouseEvent& e) = 0;
        virtual void onPopupLeave() = 0;

        /// @brief Paints the content on the face (color block / date text / icon...)
        /// @note Empty by default: the faces of `ComboBox` / `TimePicker` are painted in `paintContent` and do not
        ///       go through this; `ColorPicker` / `DateTimePicker` override it.
        virtual void paintFace(FrameCollector& fc) const { (void)fc; }

        /// @brief "Face pressed" in the collapsed state (default: record the press and consume it, toggle on release)
        /// @return true = consumed
        virtual bool popupFacePress(MouseEvent& e);

        /// @brief "Collapse the popup" (taken over by the same layer / clicked elsewhere in the host)
        /// @note The side effects differ per widget (clear draft, clear highlighted row), so the derived class implements it;
        ///       the implementation should also set `open_` to false and call `hidePopupOverlay()`.
        virtual void dismissPopup() = 0;

        // -------------------------------------------------------------------
        //  Shared skeleton (called directly by the derived class, no need to rewrite it)
        // -------------------------------------------------------------------

        /// @brief Paints the host pass: box + face + self-painted popup
        /// @note All three orders matter:
        ///       (1) `TextWidget::Paint` paints the box and text (including the content clip region);
        ///       (2) `paintFace` paints the things on the face (color block / date) in the same paint pass;
        ///       (3) the popup must be painted outside the content clip region (it must go out of the box), so it goes last,
        ///           and only when `paintsOnTop()` is true (the native carrier path is painted by the carrier itself).
        ///
        /// @par Why it is collected into the base class
        /// This skeleton was verbatim identical in `ColorPicker` and `DateTimePicker` (14 lines).
        /// The face differs for every popup widget, but the paint order of "face + popup" must be consistent --
        /// a one-position order error shows up as "the popup is cut off by the content clip region", and both places must be changed to fix it.
        void paintHostAndPopup(FrameCollector& fc) const;

        // -------------------------------------------------------------------
        //  Carrier (implemented by the base class; the derived class only uses it)
        // -------------------------------------------------------------------

        /// @brief Whether it is logically open (maintained by the derived class in state transitions)
        bool popupOpen() const { return open_; }

        /// @brief Whether the popup is currently carried by the native carrier
        bool popupOverlayVisible() const;

        // -------------------------------------------------------------------
        //  Widget overrides (default implementations; override again when the derived class has more complex face logic)
        // -------------------------------------------------------------------

        /// @brief Press: when the popup is open, give it to the popup first; clicking outside the popup collapses it; otherwise it lands on the face
        /// @note `ComboBox` / `TimePicker` have their own handling (scroll bar, row hit testing) and override this implementation.
        bool OnMousePress(MouseEvent& e) override;

        /// @brief Carrier pointer (may be null: not popped yet / insufficient capability)
        /// @note For forwarding by the derived class, such as "push the theme into the carrier on skin change" -- the carrier is a shared window,
        ///       and its own background / corner radius must follow the host theme.
        OverlayWindow* popupOverlay() const { return popupOverlay_.get(); }

        /// @brief Creates the carrier (first time) + wires it up + places it by `popupRect()` and shows it
        /// @note Returns silently when capability is insufficient / the window is not ready -- the caller then still paints
        ///       on the standard `paintsOnTop()` path, with no branch needed.
        void showPopupOverlay();

        /// @brief Hides the carrier (only Hide, no destroy -- the same window is reused next time)
        void hidePopupOverlay();

        // -------------------------------------------------------------------
        //  State
        // -------------------------------------------------------------------

        /// @brief Popup logical switch ("open in the user's eyes")
        /// @note It does not equal "popup visible": `open_ && paintsOnTop()` is visible on the self-paint path,
        ///       `open_ && popupOverlayVisible()` is visible on the carrier path. The two paths are mutually exclusive.
        bool open_ = false;

        /// @brief "Face pressed" in the collapsed state (opens only when released at the same point; cancelled on leave)
        bool popupPressedInFace_ = false;

        /// @brief Native carrier (null when capability is insufficient / not popped yet; shared per layer, see OverlayHost)
        std::unique_ptr<OverlayWindow> popupOverlay_;
    };

} // namespace Kit
