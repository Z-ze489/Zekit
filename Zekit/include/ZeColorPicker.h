/**
 * @file ZeColorPicker.h
 * @brief Color picker: a color swatch when collapsed, an HSV palette when expanded (swatch + hue bar + RGB/Hex)
 *
 * @details
 * `ColorPicker : TextWidget` -- collapsed state is "a color swatch + hex text";
 * click to pop up the palette: the SV swatch (saturation x value) at top left, a hue bar below,
 * then three R/G/B channel bars and a Hex input display, and finally "OK / Cancel".
 *
 * @par Why the popup is self-drawn (instead of stuffing in a bunch of child widgets)
 * Same reason as `DateTimePicker`, and more pronounced here: the palette requires dragging the cursor.
 * If the swatch / hue bar / channel bars were each focusable child widgets, dragging would trigger focus jumping between them,
 * and each jump triggers `OnFocusOut` -> popup closes -- dragging becomes impossible.
 * So all interaction inside the popup is handled by this widget itself: geometry is computed once (`computeLayout`),
 * shared by painting and hit testing; drag state (`dragging_`) is also maintained by this widget.
 *
 * @par How gradients are drawn
 * This library has no gradient primitive, so a gradient = a series of thin strips (`drawutil::PaintHGradient` /
 * `PaintVGradient`). The swatch uses 28 x 18 strips, a compromise between minimum acceptable appearance and vertex count:
 * more strips make no visual difference, fewer strips show banding.
 *
 * @par Usage
 * @code
 * Kit::ColorPicker cp;
 * cp.SetColor(Kit::Color4f::FromHex(0x2563EB));
 * cp.SetOnColorCommitted([](Kit::Widget& self, const Kit::Color4f& c) {
 *     std::printf("picked #%02X%02X%02X\n", (int)(c.r * 255), (int)(c.g * 255), (int)(c.b * 255));
 * });
 * win.AddWidget(cp);
 * @endcode
 *
 * @note "User confirmation" and "programmatic `SetColor`" are two paths: only the former triggers
 *       `SetOnColorCommitted` -- consistent with the library-wide loop-prevention convention.
 * @note The popup edits a draft (`draft_`); `Cancel` / `Esc` discards the draft.
 *
 * @see ZeDateTimePicker.h the same "overlay + draft + self-drawn popup" structure
 * @see ZeTheme.h          `ColorPickerStyle`
 */

#pragma once
#include "ZeTextWidget.h"
#include "ZePopupHost.h"   // Common base class of the popup carrier (this class only implements geometry / painting / input)
#include "ZeSelfAction.h"
#include "ZeBoxStyle.h"
#include "ZeTheme.h"

#include <memory>
#include <string>
#include <vector>

namespace Kit {

    class OverlayWindow;   /// < native popup carrier (shared per layer, see ZeOverlayWindow.h)

    /**
     * @brief Color picker
     *
     * @details
     * Inheritance chain: `Widget -> BoxWidget -> TextWidget -> ColorPicker`.
     * Theme section `colorPicker` (collapsed swatch + popup skeleton + channel text).
     */
    class ColorPicker : public PopupHostWidget {
    public:
        ColorPicker();

        /// @brief Destructor (defined in .cpp -- `unique_ptr<OverlayWindow>` needs the complete type)
        ~ColorPicker() override;

        /// @brief Value semantics: moved with AddWidget (explicit destructor suppresses implicit move, must restore it)
        ColorPicker(ColorPicker&&) noexcept;

        // -------------------------------------------------------------------
        //  Color (data, not style)
        // -------------------------------------------------------------------

        /// @brief Programmatic color set (does not trigger the commit callback)
        void SetColor(const Color4f& c);
        /// @brief Current color
        const Color4f& color() const { return value_; }

        /// @brief Display hex in uppercase / lowercase (default `true` = uppercase)
        void SetHexUpperCase(bool upper);
        /// @brief Whether to show hex text
        void SetShowHex(bool show);
        /// @brief Whether to show hex text
        bool showsHex() const { return showHex_; }

        /// @brief Display format string: supports `%R` `%G` `%B` `%A` (0~255 / alpha 0~255), `%r` `%g` `%b` (float, two decimals)
        /// @note Empty string = built-in (`"#RRGGBB"`).
        void SetFormat(std::string fmt);
        /// @brief Current format string
        const std::string& format() const { return format_; }

        // -------------------------------------------------------------------
        //  Popup
        // -------------------------------------------------------------------

        /// @brief Programmatically open / close the popup (does not trigger callbacks)
        bool SetOpen(bool open);
        /// @brief Whether the popup is open
        bool isOpen() const { return open_; }

        /// @brief Optional swatch presets (lists common colors at the top of the popup, click to select)
        /// @note Empty = no preset row shown
        void SetPresets(const std::vector<Color4f>& presets);
        /// @brief Current presets
        const std::vector<Color4f>& presets() const { return presets_; }

        // -------------------------------------------------------------------
        //  Callbacks (triggered only on the user path)
        // -------------------------------------------------------------------

        /// @brief User confirmed a new color ("OK" / dragging end does not auto-confirm)
        void SetOnColorCommitted(SelfAction1<const Color4f&> cb) { onCommitted_ = std::move(cb); }
        /// @brief User commit callback (sender-only form)
        void SetOnColorCommitted(SelfAction cb) { onCommittedSelf_ = std::move(cb); }

        /// @brief Color changed (fired on every drag step, for live preview)
        /// @note Separating this from "commit" is deliberate: callers can choose "live preview but persist only on commit".
        void SetOnColorChanged(SelfAction1<const Color4f&> cb) { onChanged_ = std::move(cb); }

        // -------------------------------------------------------------------
        //  Measurement protocol
        // -------------------------------------------------------------------
        float hintWidth() const override;
        float hintHeight() const override;

        // -------------------------------------------------------------------
        //  Overlay protocol (three pieces, all required)
        // -------------------------------------------------------------------
        bool paintsOnTop() const override;
        bool HitTest(float x, float y) const override;

        // -------------------------------------------------------------------
        //  Theme
        // -------------------------------------------------------------------
        void ApplyTheme(const Theme& t) override;

        // -------------------------------------------------------------------
        //  Painting / events
        // -------------------------------------------------------------------
        // @brief Collapsed swatch + Hex text -> popup (swatch / hue bar / channels / buttons)
        void Paint(FrameCollector& fc) const override;

        bool OnMouseRelease(MouseEvent& e) override;
        bool OnMouseMove(MouseEvent& e) override;
        bool OnMouseWheel(MouseEvent& e) override;
        void OnMouseLeave() override;
        bool OnKeyPress(KeyEvent& e) override;
        void OnFocusOut() override;

    protected:
        const BoxStyle& themeBox(const Theme& t) const override;
        const TextStyle& themeText(const Theme& t) const override;

    private:
        // ===================================================================
        //  Geometry (shared by painting and hit testing, single copy)
        // ===================================================================

        /// @brief Popup outer rectangle
        struct PopupGeom {
            float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
        };

        /// @brief Popup internal regions (computed all at once)
        struct Layout {
            PopupGeom panel;
            float swatchY = 0.0f, swatchH = 0.0f;   /// < top "current color / draft color" comparison bar
            float presetY = 0.0f, presetH = 0.0f;   /// < preset row (may be 0 height)
            float svX = 0.0f, svY = 0.0f, svW = 0.0f, svH = 0.0f;   /// < SV swatch
            float hueY = 0.0f, hueH = 0.0f;         /// < hue bar
            float chY = 0.0f;                       /// < y of the first channel row
            float chH = 0.0f;                       /// < single channel row height
            float footerY = 0.0f, footerH = 0.0f;   /// < button row
        };
        Layout computeLayout() const;

        /// @brief Popup outer rectangle (= convenience accessor for `computeLayout().panel`)
        PopupGeom computePopup() const;

        // -------------------------------------------------------------------
        //  Native popup carrier (OverlayWindow path; always null on fallback platforms)
        // -------------------------------------------------------------------

        /// @brief Popup "logically exists and anchor is visible" (criterion shared by both paths)
        bool popupActive() const;
        /// @brief Popup rectangle (base class uses it to place the carrier; returns false = should not pop up now)
        bool popupRect(float& x, float& y, float& w, float& h) const override;
        /// @brief Should be dismissed (taken over by same layer / clicked elsewhere in host)
        void dismissPopup() override;

        /// @brief Paints only the popup (origin at popup top-left when `localPopup=true`)
        void paintPopupInto(FrameCollector& fc, bool localPopup) const override;

        /// @brief Mouse inside the carrier (coordinates already in host space)
        bool onPopupPress(MouseEvent& e) override;
        bool onPopupRelease(MouseEvent& e) override;
        bool onPopupMove(MouseEvent& e) override;
        bool onPopupWheel(MouseEvent& e) override;
        void onPopupLeave() override;

        /// @brief Clickable / draggable region
        enum class Zone {
            None = 0,
            SV, Hue, ChannelR, ChannelG, ChannelB,
            Preset, Confirm, Cancel
        };
        struct Hit {
            Zone zone = Zone::None;
            int  index = -1;    /// < preset index when zone == Preset
        };
        Hit hitTestPopup(float px, float py) const;

        // ===================================================================
        //  Interaction
        // ===================================================================

        /// @brief State change on the user path
        void handlePress(const Hit& h, float px, float py);
        /// @brief Update during drag (converts coordinates to color by current zone)
        void handleDrag(float px, float py);
        /// @brief Write back and invoke callback (commit)
        void commitDraft();
        /// @brief Discard draft
        void cancelDraft();
        /// @brief Fire the "changed" callback (during drag)
        void notifyChanged();
        /// @brief Fire the "commit" callback
        void notifyCommitted();

        void openPopup();
        void closePopup();
        void requestRepaint();
        void markMeasureDirty();

        /// @brief Whether the popup anchor is actually visible
        bool popupAnchorVisible() const;

        // ===================================================================
        //  HSV / Hex conversion
        // ===================================================================

        /// @brief Re-decompose the current draft RGB into H/S/V (0~1, H in degrees 0~360)
        void syncHsvFromRgb();
        /// @brief Rebuild the draft RGB from current H/S/V (preserving alpha)
        void syncRgbFromHsv();
        /// @brief Display string (per `format_` or built-in hex)
        std::string formatValue(const Color4f& c) const;

        /// @brief Hit point of swatch / hue bar / channel bar (shared by painting and hit testing), returns false if out of bounds
        bool zoneRect(Zone z, float& x, float& y, float& w, float& h) const;

        // ===================================================================
        //  Paint sub-steps
        // ===================================================================

        /// @brief Paint collapsed state: swatch + hex text
        void paintFace(FrameCollector& fc) const override;
        /// @brief Paint popup
        void PaintPopup(FrameCollector& fc, const Layout& L, const Theme& theme) const;

        // ===================================================================
        //  State
        // ===================================================================

        Color4f value_{ 0.0f, 0.0f, 0.0f, 1.0f };   /// < committed color
        Color4f draft_{ 0.0f, 0.0f, 0.0f, 1.0f };   /// < draft being edited in the popup

        float h_ = 0.0f;   /// < hue 0~360
        float s_ = 1.0f;   /// < saturation 0~1
        float v_ = 1.0f;   /// < value 0~1

        bool showHex_ = true;
        bool hexUpper_ = true;
        std::string format_;
        std::vector<Color4f> presets_;

        // ---- interaction transient ----
        //  NOTE `open_` and carrier `popupOverlay_` have moved up to PopupHostWidget (shared by the four popup widgets)
        bool  dragging_ = false;
        Zone  dragZone_ = Zone::None;
        Hit   hover_;
        bool  hoverValid_ = false;

        ColorPickerStyle styleTheme_;

        // ---- callbacks ----
        //  `std::function` is movable and is moved along with this class's implicit move constructor by AddWidget.
        SelfAction1<const Color4f&> onCommitted_;      /// < user commit (user path only)
        SelfAction                  onCommittedSelf_;  /// < user commit (sender only)
        SelfAction1<const Color4f&> onChanged_;        /// < color changed (including during drag)
    };

} // namespace Kit
