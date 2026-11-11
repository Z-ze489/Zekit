
#include "pch.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "include/ZeColorPicker.h"
#include "include/ZeOverlayWindow.h"
#include "include/Zekit.h"

#include "ZeDrawUtil.h"
#include "ZeFrameCollector.h"
#include "ZeAnchorVisible.h"
#include "include/ZeText.h"

namespace Kit {

    namespace {
        constexpr int kSVStepsX = 28;
        constexpr int kSVStepsY = 18;

        constexpr float kFallbackGap = 6.0f;
        constexpr float kFallbackPad = 10.0f;
        constexpr float kFallbackSvH = 130.0f;
        constexpr float kFallbackHueH = 14.0f;
        constexpr float kFallbackChannelH = 22.0f;
        constexpr float kFallbackSwatchH = 26.0f;
        constexpr float kFallbackRadius = 4.0f;
        constexpr float kWindowMargin = 4.0f;
        constexpr float kShadowPad = 8.0f;
        constexpr float kFallbackW = 160.0f;
        constexpr float kFallbackH = 34.0f;

        constexpr float kChannelValueW = 34.0f;
        constexpr float kChannelLabelW = 14.0f;
        constexpr float kMinPanelW = 240.0f;
        constexpr float kPresetSize = 18.0f;
        constexpr float kPresetGap = 4.0f;
        constexpr float kBtnW = 56.0f;
        constexpr float kBtnH = 24.0f;

        float BorderOf(const BoxStyle& box) {
            return std::max({ box.border.width[Border::Top], box.border.width[Border::Right],
                              box.border.width[Border::Bottom], box.border.width[Border::Left] });
        }

        int To255(float v) {
            return std::clamp(static_cast<int>(std::lround(
                std::clamp(v, 0.0f, 1.0f) * 255.0f)), 0, 255);
        }

        Color4f HsvToRgb(float hueDeg, float sat, float val) {
            const float hue = std::fmod(std::max(0.0f, hueDeg), 360.0f);
            const float s = std::clamp(sat, 0.0f, 1.0f);
            const float v = std::clamp(val, 0.0f, 1.0f);

            const float c = v * s;
            const float hp = hue / 60.0f;
            const float x = c * (1.0f - std::fabs(std::fmod(hp, 2.0f) - 1.0f));
            const float m = v - c;

            float r = 0.0f, g = 0.0f, b = 0.0f;
            if (hp < 1.0f)      { r = c; g = x; b = 0.0f; }
            else if (hp < 2.0f) { r = x; g = c; b = 0.0f; }
            else if (hp < 3.0f) { r = 0.0f; g = c; b = x; }
            else if (hp < 4.0f) { r = 0.0f; g = x; b = c; }
            else if (hp < 5.0f) { r = x; g = 0.0f; b = c; }
            else                { r = c; g = 0.0f; b = x; }

            return Color4f{ r + m, g + m, b + m, 1.0f };
        }
    }

    ColorPicker::~ColorPicker() = default;
    ColorPicker::ColorPicker(ColorPicker&&) noexcept = default;

    ColorPicker::ColorPicker() {
        ApplyTheme(Themes::Light());

        value_ = Color4f::FromHex(0x2563EB);
        draft_ = value_;
        syncHsvFromRgb();

        SetFocusable(true);
        SetCursor(Cursor::Pointer);
        SetDefaultSize(kFallbackW, kFallbackH);
    }

    const BoxStyle& ColorPicker::themeBox(const Theme& t) const { return t.colorPicker.box; }
    const TextStyle& ColorPicker::themeText(const Theme& t) const { return t.colorPicker.text; }

    void ColorPicker::ApplyTheme(const Theme& t) {
        TextWidget::ApplyTheme(t);
        styleTheme_ = t.colorPicker;
        markMeasureDirty();
    }

    void ColorPicker::requestRepaint() {
        RequestWindowRedraw();
    }

    void ColorPicker::markMeasureDirty() {
        notifyLayoutInputChanged();
        requestRepaint();
    }

    void ColorPicker::SetColor(const Color4f& c) {
        value_ = c;
        draft_ = c;
        syncHsvFromRgb();
        markMeasureDirty();

        notifyChanged();
        RequestWindowRedraw();
    }

    void ColorPicker::syncHsvFromRgb() {
        const float r = std::clamp(draft_.r, 0.0f, 1.0f);
        const float g = std::clamp(draft_.g, 0.0f, 1.0f);
        const float b = std::clamp(draft_.b, 0.0f, 1.0f);

        const float mx = std::max({ r, g, b });
        const float mn = std::min({ r, g, b });
        const float d = mx - mn;

        v_ = mx;
        s_ = (mx > 0.0f) ? (d / mx) : 0.0f;

        if (d <= 0.0f) {
            return;
        }

        if (mx == r)      h_ = 60.0f * std::fmod((g - b) / d, 6.0f);
        else if (mx == g) h_ = 60.0f * (((b - r) / d) + 2.0f);
        else              h_ = 60.0f * (((r - g) / d) + 4.0f);

        if (h_ < 0.0f) h_ += 360.0f;
    }

    void ColorPicker::syncRgbFromHsv() {
        const Color4f c = HsvToRgb(h_, s_, v_);
        draft_ = Color4f{ c.r, c.g, c.b, draft_.a };
    }

    void ColorPicker::SetHexUpperCase(bool upper) {
        hexUpper_ = upper;
        requestRepaint();
        RequestWindowRedraw();
    }

    void ColorPicker::SetShowHex(bool show) {
        showHex_ = show;
        markMeasureDirty();
        RequestWindowRedraw();
    }

    void ColorPicker::SetFormat(std::string fmt) {
        format_ = std::move(fmt);
        markMeasureDirty();
        RequestWindowRedraw();
    }

    std::string ColorPicker::formatValue(const Color4f& c) const {
        char buf[64];

        if (!format_.empty()) {
            std::string out;
            out.reserve(format_.size() + 8);
            for (std::size_t i = 0; i < format_.size(); ++i) {
                if (format_[i] != '%' || i + 1 >= format_.size()) {
                    out.push_back(format_[i]);
                    continue;
                }
                const char k = format_[++i];
                switch (k) {
                case 'R': std::snprintf(buf, sizeof(buf), "%d", To255(c.r)); out += buf; break;
                case 'G': std::snprintf(buf, sizeof(buf), "%d", To255(c.g)); out += buf; break;
                case 'B': std::snprintf(buf, sizeof(buf), "%d", To255(c.b)); out += buf; break;
                case 'A': std::snprintf(buf, sizeof(buf), "%d", To255(c.a)); out += buf; break;
                case 'r': std::snprintf(buf, sizeof(buf), "%.2f", c.r); out += buf; break;
                case 'g': std::snprintf(buf, sizeof(buf), "%.2f", c.g); out += buf; break;
                case 'b': std::snprintf(buf, sizeof(buf), "%.2f", c.b); out += buf; break;
                case '%': out.push_back('%'); break;
                default:  out.push_back('%'); out.push_back(k); break;
                }
            }
            return out;
        }

        std::snprintf(buf, sizeof(buf), hexUpper_ ? "#%02X%02X%02X" : "#%02x%02x%02x",
            To255(c.r), To255(c.g), To255(c.b));
        return std::string(buf);
    }

    void ColorPicker::SetPresets(const std::vector<Color4f>& presets) {
        presets_ = presets;
        requestRepaint();
        RequestWindowRedraw();
    }

    bool ColorPicker::popupAnchorVisible() const {
        return detail::AnchorVisibleInAncestors(*this);
    }

    void ColorPicker::openPopup() {
        if (open_) return;
        if (!popupAnchorVisible()) return;

        draft_ = value_;
        syncHsvFromRgb();
        hoverValid_ = false;
        dragging_ = false;
        dragZone_ = Zone::None;
        open_ = true;
        showPopupOverlay();
        requestRepaint();
    }

    void ColorPicker::closePopup() {
        if (!open_) return;
        open_ = false;
        popupPressedInFace_ = false;
        dragging_ = false;
        dragZone_ = Zone::None;
        hoverValid_ = false;
        hidePopupOverlay();
        requestRepaint();
    }

    bool ColorPicker::popupRect(float& x, float& y, float& w, float& h) const {
        const PopupGeom g = computePopup();
        if (g.w <= 0.0f || g.h <= 0.0f) return false;
        x = g.x; y = g.y; w = g.w; h = g.h;
        return true;
    }

    void ColorPicker::dismissPopup() {
        closePopup();
    }

    bool ColorPicker::SetOpen(bool open) {
        if (open) openPopup();
        else      closePopup();
        return open_;
    }

    ColorPicker::Layout ColorPicker::computeLayout() const {
        Layout L;

        const float pad = (styleTheme_.padding > 0.0f) ? styleTheme_.padding : kFallbackPad;
        const float svH = (styleTheme_.svHeight > 0.0f) ? styleTheme_.svHeight : kFallbackSvH;
        const float hueH = (styleTheme_.hueHeight > 0.0f) ? styleTheme_.hueHeight : kFallbackHueH;
        const float chH = (styleTheme_.channelHeight > 0.0f) ? styleTheme_.channelHeight : kFallbackChannelH;
        const float swH = (styleTheme_.swatchHeight > 0.0f) ? styleTheme_.swatchHeight : kFallbackSwatchH;

        const float contentW = std::max(kMinPanelW - pad * 2.0f, width());
        L.panel.w = contentW + pad * 2.0f;

        const float presetH = presets_.empty() ? 0.0f : (kPresetSize + 4.0f);
        L.panel.h = pad + swH + 6.0f
            + presetH
            + svH + 6.0f
            + hueH + 8.0f
            + chH * 3.0f
            + kBtnH + 8.0f
            + pad;

        const float gap = (styleTheme_.gap > 0.0f) ? styleTheme_.gap : kFallbackGap;
        float px = x();
        float py = y() + height() + gap;

        float vw = 0.0f, vh = 0.0f;
        if (Window* w = window()) {
            vw = static_cast<float>(w->width());
            vh = static_cast<float>(w->height());
        }
        if (vw > 0.0f) {
            if (px + L.panel.w > vw - kWindowMargin) px = vw - kWindowMargin - L.panel.w;
            if (px < kWindowMargin) px = kWindowMargin;
        }
        if (vh > 0.0f && py + L.panel.h > vh - kWindowMargin) {
            const float above = y() - gap - L.panel.h;
            py = (above >= kWindowMargin)
                ? above
                : std::max(kWindowMargin, vh - kWindowMargin - L.panel.h);
        }

        L.panel.x = px;
        L.panel.y = py;

        float cy = py + pad;
        L.swatchY = cy;  L.swatchH = swH;  cy += swH + 6.0f;

        L.presetY = cy;  L.presetH = presetH;
        cy += presetH;

        L.svX = px + pad;  L.svY = cy;  L.svW = contentW;  L.svH = svH;
        cy += svH + 6.0f;

        L.hueY = cy;  L.hueH = hueH;
        cy += hueH + 8.0f;

        L.chY = cy;  L.chH = chH;
        cy += chH * 3.0f;

        L.footerY = cy;  L.footerH = kBtnH + 8.0f;

        return L;
    }

    ColorPicker::PopupGeom ColorPicker::computePopup() const {
        return computeLayout().panel;
    }

    bool ColorPicker::zoneRect(Zone z, float& zx, float& zy, float& zw, float& zh) const {
        const Layout L = computeLayout();
        switch (z) {
        case Zone::SV:       zx = L.svX; zy = L.svY;                zw = L.svW; zh = L.svH; return true;
        case Zone::Hue:      zx = L.svX; zy = L.hueY;               zw = L.svW; zh = L.hueH; return true;
        case Zone::ChannelR: zx = L.svX; zy = L.chY;                zw = L.svW; zh = L.chH; return true;
        case Zone::ChannelG: zx = L.svX; zy = L.chY + L.chH;        zw = L.svW; zh = L.chH; return true;
        case Zone::ChannelB: zx = L.svX; zy = L.chY + L.chH * 2.0f; zw = L.svW; zh = L.chH; return true;
        default: return false;
        }
    }

    ColorPicker::Hit ColorPicker::hitTestPopup(float px, float py) const {
        Hit h;
        if (!open_) return h;

        const Layout L = computeLayout();
        if (!drawutil::HitRect(px, py, L.panel.x, L.panel.y, L.panel.w, L.panel.h))
            return h;

        if (!presets_.empty() && py >= L.presetY && py < L.presetY + L.presetH) {
            float sx = L.svX;
            for (std::size_t i = 0; i < presets_.size(); ++i) {
                if (px >= sx && px < sx + kPresetSize) {
                    h.zone = Zone::Preset;
                    h.index = static_cast<int>(i);
                    return h;
                }
                sx += kPresetSize + kPresetGap;
            }
            return h;
        }

        const Zone probes[5] = { Zone::SV, Zone::Hue, Zone::ChannelR, Zone::ChannelG, Zone::ChannelB };
        for (const Zone z : probes) {
            float zx = 0.0f, zy = 0.0f, zw = 0.0f, zh = 0.0f;
            if (!zoneRect(z, zx, zy, zw, zh)) continue;
            if (drawutil::HitRect(px, py, zx, zy, zw, zh)) {
                h.zone = z;
                return h;
            }
        }

        if (py >= L.footerY && py < L.footerY + L.footerH) {
            const float byy = L.footerY + (L.footerH - kBtnH) * 0.5f;
            const float rx = L.panel.x + L.panel.w - kFallbackPad;
            const float confirmX = rx - kBtnW;
            const float cancelX = confirmX - 6.0f - kBtnW;

            if (drawutil::HitRect(px, py, cancelX, byy, kBtnW, kBtnH)) { h.zone = Zone::Cancel;  return h; }
            if (drawutil::HitRect(px, py, confirmX, byy, kBtnW, kBtnH)) { h.zone = Zone::Confirm; return h; }
            return h;
        }

        return h;
    }

    bool ColorPicker::popupActive() const {
        return open_ && popupAnchorVisible();
    }

    bool ColorPicker::paintsOnTop() const {
        return popupActive() && !popupOverlayVisible();
    }

    bool ColorPicker::HitTest(float px, float py) const {
        if (Widget::HitTest(px, py)) return true;
        if (!popupActive() || popupOverlayVisible()) return false;

        const PopupGeom g = computePopup();
        return drawutil::HitRect(px, py, g.x, g.y, g.w, g.h);
    }

    float ColorPicker::hintWidth() const {
        if (prefWidth() > 0.0f) return prefWidth();

        Window* win = window();
        if (!win) return Widget::hintWidth();

        const float swatchW = (height() > 0.0f) ? height() * 0.55f : 18.0f;
        const float textW = showHex_
            ? win->MeasureText(formatValue(value_), textStyle_.font).width
            : 0.0f;
        const Padding& p = box_.padding;
        return swatchW + 6.0f + textW + p.left + p.right + BorderOf(box_) * 2.0f + 6.0f;
    }

    float ColorPicker::hintHeight() const {
        if (prefHeight() > 0.0f) return prefHeight();

        Window* win = window();
        if (!win) return Widget::hintHeight();

        const TextMetrics tm = win->MeasureText("#FFFFFF", textStyle_.font);
        const Padding& p = box_.padding;
        return tm.height + 10.0f + p.top + p.bottom + BorderOf(box_) * 2.0f;
    }

    void ColorPicker::Paint(FrameCollector& fc) const {
        paintHostAndPopup(fc);
    }

    void ColorPicker::paintPopupInto(FrameCollector& fc, bool localPopup) const {
        Layout L = computeLayout();
        const Theme& theme = fc.GetTheme();

        if (localPopup) {
            const float dx = L.panel.x;
            const float dy = L.panel.y;
            L.panel.x = 0.0f;  L.panel.y = 0.0f;
            L.swatchY -= dy;
            L.presetY -= dy;
            L.svX -= dx;  L.svY -= dy;
            L.hueY -= dy;
            L.chY -= dy;
            L.footerY -= dy;
        }

        fc.ResetClip();

        fc.PushClip(L.panel.x - kShadowPad, L.panel.y - kShadowPad,
            L.panel.w + kShadowPad * 2.0f, L.panel.h + kShadowPad * 2.0f);
        const Shadow& sh = styleTheme_.panel.shadow;
        if (sh.enabled) {
            drawutil::PaintShadow(fc, L.panel.x, L.panel.y, L.panel.w, L.panel.h,
                BoxWidget::resolveRadius(styleTheme_.panel, theme),
                sh.color, sh.offsetX, sh.offsetY, sh.spread, styleTheme_.panel.opacity);
        }
        fc.PopClip();

        fc.PushClip(L.panel.x, L.panel.y, L.panel.w, L.panel.h);
        PaintPopup(fc, L, theme);
        fc.PopClip();
    }

    void ColorPicker::paintFace(FrameCollector& fc) const {
        float cx = 0.0f, cy = 0.0f, cw = 0.0f, ch = 0.0f;
        contentRect(cx, cy, cw, ch);
        if (cw <= 0.0f || ch <= 0.0f) return;

        const Theme& theme = fc.GetTheme();
        const Color4f edge = (styleTheme_.borderColor.a > 0.0f) ? styleTheme_.borderColor : theme.border;

        const float swatchW = std::min(ch, cw * 0.5f);
        if (swatchW > 2.0f) {
            const float sy = cy + (ch - swatchW) * 0.5f;
            drawutil::PaintBox(fc, cx, sy, swatchW, swatchW, 3.0f, 1.0f, edge, value_);
        }

        if (!showHex_) return;

        const ColorSet cs = resolvedTextColors(currentState());
        const FontDesc& font = textStyle_.font;
        const float textX = cx + swatchW + 6.0f;
        const float textW = std::max(0.0f, cx + cw - textX);
        if (textW <= 0.0f) return;

        const std::string shown = formatValue(value_);
        const TextMetrics tm = fc.MeasureText(shown, font);
        if (tm.width <= 0.0f) return;

        const float ty = cy + (ch - tm.height) * 0.5f;
        fc.AddText(shown, font, textX, ty + tm.ascent, cs.text);
    }

    void ColorPicker::PaintPopup(FrameCollector& fc, const Layout& L, const Theme& theme) const {
        const float corner = (styleTheme_.radius > 0.0f) ? styleTheme_.radius : kFallbackRadius;
        const Color4f edge = (styleTheme_.borderColor.a > 0.0f) ? styleTheme_.borderColor : theme.border;
        const Color4f trackCol = (styleTheme_.trackColor.a > 0.0f) ? styleTheme_.trackColor : theme.border;
        const Color4f thumbCol = styleTheme_.thumbColor;
        const Color4f cursorCol = styleTheme_.cursorColor;
        const Color4f cursorShadow = styleTheme_.cursorShadowColor;
        const Color4f kNone{ 0.0f, 0.0f, 0.0f, 0.0f };

        {
            const float radius = BoxWidget::resolveRadius(styleTheme_.panel, theme);
            const ColorSet panelCs = styleTheme_.panel.colors.get(WidgetState::Normal);
            Color4f pbg = panelCs.background;
            Color4f pbd = panelCs.border;
            pbg.a *= styleTheme_.panel.opacity;
            pbd.a *= styleTheme_.panel.opacity;
            drawutil::PaintBox(fc, L.panel.x, L.panel.y, L.panel.w, L.panel.h,
                radius, BorderOf(styleTheme_.panel), pbd, pbg);
        }

        {
            const float half = L.panel.w * 0.5f;
            drawutil::PaintRoundedRect(fc, L.panel.x, L.swatchY, half, L.swatchH, corner, value_);
            drawutil::PaintRoundedRect(fc, L.panel.x + half, L.swatchY,
                L.panel.w - half, L.swatchH, corner, draft_);
            drawutil::PaintVLine(fc, L.panel.x + half - 0.5f, L.swatchY, L.swatchH, 1.0f, trackCol);
            drawutil::PaintBox(fc, L.panel.x, L.swatchY, L.panel.w, L.swatchH, corner, 1.0f,
                edge, kNone);
        }

        if (!presets_.empty() && L.presetH > 0.0f) {
            float sx = L.svX;
            const float sy = L.presetY + (L.presetH - kPresetSize) * 0.5f;
            for (std::size_t i = 0; i < presets_.size(); ++i) {
                drawutil::PaintRoundedRect(fc, sx, sy, kPresetSize, kPresetSize, 3.0f, presets_[i]);
                const bool hov = hoverValid_ && hover_.zone == Zone::Preset
                    && hover_.index == static_cast<int>(i);
                if (hov) {
                    drawutil::PaintBox(fc, sx - 1.5f, sy - 1.5f,
                        kPresetSize + 3.0f, kPresetSize + 3.0f, 3.0f, 1.5f, theme.primary, kNone);
                }
                sx += kPresetSize + kPresetGap;
            }
        }

        {
            const Color4f hueCol = HsvToRgb(h_, 1.0f, 1.0f);
            const float stepW = L.svW / static_cast<float>(kSVStepsX);
            const float stepH = L.svH / static_cast<float>(kSVStepsY);

            for (int iy = 0; iy < kSVStepsY; ++iy) {
                const float ty = static_cast<float>(iy) / static_cast<float>(kSVStepsY - 1);
                const float val = 1.0f - ty;
                for (int ix = 0; ix < kSVStepsX; ++ix) {
                    const float tx = static_cast<float>(ix) / static_cast<float>(kSVStepsX - 1);
                    const Color4f base = drawutil::LerpColor(Color4f{ 1, 1, 1, 1 }, hueCol, tx);
                    const Color4f col{ base.r * val, base.g * val, base.b * val, 1.0f };
                    drawutil::PaintRoundedRect(fc,
                        L.svX + stepW * static_cast<float>(ix),
                        L.svY + stepH * static_cast<float>(iy),
                        stepW + 0.6f, stepH + 0.6f, 0.0f, col);
                }
            }
            drawutil::PaintBox(fc, L.svX, L.svY, L.svW, L.svH, corner, 1.0f, edge, kNone);

            const float cxp = L.svX + L.svW * std::clamp(s_, 0.0f, 1.0f);
            const float cyp = L.svY + L.svH * (1.0f - std::clamp(v_, 0.0f, 1.0f));
            drawutil::PaintCircle(fc, cxp, cyp, 6.0f, cursorShadow);
            drawutil::PaintCircle(fc, cxp, cyp, 4.5f, cursorCol);
        }

        {
            drawutil::PaintHueStrip(fc, L.svX, L.hueY, L.svW, L.hueH, 1.0f, 1.0f, 32);
            drawutil::PaintBox(fc, L.svX, L.hueY, L.svW, L.hueH, corner, 1.0f, edge, kNone);

            const float hx = L.svX + L.svW * (std::clamp(h_, 0.0f, 359.99f) / 360.0f);
            drawutil::PaintRoundedRect(fc, hx - 3.0f, L.hueY - 2.0f, 6.0f, L.hueH + 4.0f,
                3.0f, cursorShadow);
            drawutil::PaintRoundedRect(fc, hx - 2.0f, L.hueY - 1.0f, 4.0f, L.hueH + 2.0f,
                2.0f, cursorCol);
        }

        {
            const TextStyle& ls = styleTheme_.label;
            const TextStyle& vs = styleTheme_.value;
            Color4f labelCol = ls.colors.get(WidgetState::Normal).text;
            Color4f valueCol = vs.colors.get(WidgetState::Normal).text;
            if (labelCol.a <= 0.0f) labelCol = theme.muted;
            if (valueCol.a <= 0.0f) valueCol = theme.text;

            for (int ch = 0; ch < 3; ++ch) {
                const float cy0 = L.chY + L.chH * static_cast<float>(ch);

                Color4f lo = draft_, hi = draft_;
                float val = 0.0f;
                const char* name = "R";
                if (ch == 0)      { lo.r = 0.0f; hi.r = 1.0f; val = draft_.r; name = "R"; }
                else if (ch == 1) { lo.g = 0.0f; hi.g = 1.0f; val = draft_.g; name = "G"; }
                else              { lo.b = 0.0f; hi.b = 1.0f; val = draft_.b; name = "B"; }
                lo.a = 1.0f; hi.a = 1.0f;

                const float trackX = L.svX + kChannelLabelW;
                const float trackW = std::max(10.0f, L.svW - kChannelLabelW - kChannelValueW - 6.0f);
                const float trackY = cy0 + (L.chH - 8.0f) * 0.5f;
                const float trackH = 8.0f;

                drawutil::PaintTextCentered(fc, name, ls.font,
                    L.svX, cy0, kChannelLabelW, L.chH, labelCol);

                drawutil::PaintHGradient(fc, trackX, trackY, trackW, trackH, lo, hi, 16);
                drawutil::PaintBox(fc, trackX, trackY, trackW, trackH, 4.0f, 1.0f, edge, kNone);

                const float tx = trackX + trackW * std::clamp(val, 0.0f, 1.0f);
                drawutil::PaintCircle(fc, tx, trackY + trackH * 0.5f, 6.0f, cursorShadow);
                drawutil::PaintCircle(fc, tx, trackY + trackH * 0.5f, 4.5f, thumbCol);

                char buf[8];
                std::snprintf(buf, sizeof(buf), "%d", To255(val));
                drawutil::PaintTextCentered(fc, buf, vs.font,
                    L.svX + L.svW - kChannelValueW, cy0, kChannelValueW, L.chH, valueCol);
            }
        }

        {
            const float byy = L.footerY + (L.footerH - kBtnH) * 0.5f;
            const float confirmX = L.panel.x + L.panel.w - kFallbackPad - kBtnW;
            const float cancelX = confirmX - 6.0f - kBtnW;

            Color4f btnText = styleTheme_.value.colors.get(WidgetState::Normal).text;
            if (btnText.a <= 0.0f) btnText = theme.text;

            if (hoverValid_ && hover_.zone == Zone::Cancel)
                drawutil::PaintRoundedRect(fc, cancelX, byy, kBtnW, kBtnH, corner, theme.bg);
            drawutil::PaintTextCentered(fc, Kit::UiText(TextId::Cancel), styleTheme_.value.font,
                cancelX, byy, kBtnW, kBtnH, btnText);

            drawutil::PaintRoundedRect(fc, confirmX, byy, kBtnW, kBtnH, corner, theme.primary);
            drawutil::PaintTextCentered(fc, Kit::UiText(TextId::Ok), styleTheme_.value.font,
                confirmX, byy, kBtnW, kBtnH, theme.onPrimary);
        }
    }

    void ColorPicker::handlePress(const Hit& h, float px, float py) {
        switch (h.zone) {
        case Zone::SV: {
            float zx = 0.0f, zy = 0.0f, zw = 0.0f, zh = 0.0f;
            if (!zoneRect(Zone::SV, zx, zy, zw, zh)) return;
            s_ = std::clamp((px - zx) / std::max(1.0f, zw), 0.0f, 1.0f);
            v_ = 1.0f - std::clamp((py - zy) / std::max(1.0f, zh), 0.0f, 1.0f);
            syncRgbFromHsv();
            break;
        }

        case Zone::Hue: {
            float zx = 0.0f, zy = 0.0f, zw = 0.0f, zh = 0.0f;
            if (!zoneRect(Zone::Hue, zx, zy, zw, zh)) return;
            h_ = std::clamp((px - zx) / std::max(1.0f, zw), 0.0f, 0.9999f) * 360.0f;
            syncRgbFromHsv();
            break;
        }

        case Zone::ChannelR:
        case Zone::ChannelG:
        case Zone::ChannelB: {
            float zx = 0.0f, zy = 0.0f, zw = 0.0f, zh = 0.0f;
            if (!zoneRect(h.zone, zx, zy, zw, zh)) return;
            const float trackX = zx + kChannelLabelW;
            const float trackW = std::max(10.0f, zw - kChannelLabelW - kChannelValueW - 6.0f);
            const float t = std::clamp((px - trackX) / trackW, 0.0f, 1.0f);
            if (h.zone == Zone::ChannelR)      draft_.r = t;
            else if (h.zone == Zone::ChannelG) draft_.g = t;
            else                               draft_.b = t;
            syncHsvFromRgb();
            break;
        }

        case Zone::Preset:
            if (h.index >= 0 && h.index < static_cast<int>(presets_.size())) {
                draft_ = presets_[static_cast<std::size_t>(h.index)];
                syncHsvFromRgb();
            }
            break;

        case Zone::Confirm:
            commitDraft();
            return;

        case Zone::Cancel:
            cancelDraft();
            return;

        case Zone::None:
        default:
            return;
        }

        notifyChanged();
        requestRepaint();
    }

    void ColorPicker::handleDrag(float px, float py) {
        Hit h;
        h.zone = dragZone_;
        handlePress(h, px, py);
    }

    void ColorPicker::commitDraft() {
        value_ = draft_;
        closePopup();
        markMeasureDirty();

        notifyCommitted();
    }

    void ColorPicker::cancelDraft() {
        draft_ = value_;
        syncHsvFromRgb();
        closePopup();
    }

    void ColorPicker::notifyChanged() {
        if (onChanged_) onChanged_(*this, draft_);
    }

    void ColorPicker::notifyCommitted() {
        if (onCommitted_)     onCommitted_(*this, value_);
        if (onCommittedSelf_) onCommittedSelf_(*this);
    }

    bool ColorPicker::onPopupPress(MouseEvent& e) {
        if (e.button != MouseButton::Left || !popupActive()) return false;

        const Hit h = hitTestPopup(e.x, e.y);
        if (h.zone != Zone::None) {
            dragging_ = true;
            dragZone_ = h.zone;
            handlePress(h, e.x, e.y);
            e.accept();
            return true;
        }

        const PopupGeom g = computePopup();
        if (drawutil::HitRect(e.x, e.y, g.x, g.y, g.w, g.h)) {
            e.accept();
            return true;
        }
        return false;
    }

    bool ColorPicker::onPopupRelease(MouseEvent& e) {
        if (!dragging_) return false;
        dragging_ = false;
        dragZone_ = Zone::None;
        e.accept();
        return true;
    }

    bool ColorPicker::OnMouseRelease(MouseEvent& e) {
        if (!popupOverlayVisible() && onPopupRelease(e)) return true;
        if (popupPressedInFace_) {
            popupPressedInFace_ = false;
            if (!Widget::HitTest(e.x, e.y)) { e.accept(); return true; }
            openPopup();
            e.accept();
            return true;
        }
        return false;
    }

    bool ColorPicker::OnMouseMove(MouseEvent& e) {
        if (popupOverlayVisible()) return false;
        return onPopupMove(e);
    }

    bool ColorPicker::onPopupMove(MouseEvent& e) {
        if (!popupActive()) return false;

        if (dragging_ && dragZone_ != Zone::None) {
            handleDrag(e.x, e.y);
            e.accept();
            return true;
        }

        const Hit h = hitTestPopup(e.x, e.y);
        const bool changed = (!hoverValid_) || h.zone != hover_.zone || h.index != hover_.index;
        hover_ = h;
        hoverValid_ = true;
        if (changed) {
            requestRepaint();
            return true;
        }
        return false;
    }

    bool ColorPicker::OnMouseWheel(MouseEvent& e) {
        if (popupOverlayVisible()) return false;
        return onPopupWheel(e);
    }

    bool ColorPicker::onPopupWheel(MouseEvent& e) {
        if (!popupActive()) return false;

        const float step = hasModifier(e.modifiers, KeyboardModifiers::Shift) ? 10.0f : 2.0f;
        h_ += (e.wheelDelta > 0) ? -step : step;
        if (h_ < 0.0f) h_ += 360.0f;
        if (h_ >= 360.0f) h_ -= 360.0f;

        syncRgbFromHsv();
        notifyChanged();
        requestRepaint();
        e.accept();
        return true;
    }

    void ColorPicker::OnMouseLeave() {
        onPopupLeave();
    }

    void ColorPicker::onPopupLeave() {
        if (!hoverValid_) return;
        hoverValid_ = false;
        requestRepaint();
    }

    bool ColorPicker::OnKeyPress(KeyEvent& e) {
        if (!isEnabled()) return false;

        if (!open_) {
            switch (e.key) {
            case Key::Enter:
            case Key::Space:
            case Key::DownArrow:
                openPopup();
                e.accept();
                return true;
            default:
                return false;
            }
        }

        const bool shift = hasModifier(e.modifiers, KeyboardModifiers::Shift);
        switch (e.key) {
        case Key::Escape:
            cancelDraft();
            e.accept();
            return true;

        case Key::Enter:
            commitDraft();
            e.accept();
            return true;

        case Key::LeftArrow:
            h_ -= shift ? 10.0f : 1.0f;
            if (h_ < 0.0f) h_ += 360.0f;
            syncRgbFromHsv();
            notifyChanged();
            requestRepaint();
            e.accept();
            return true;

        case Key::RightArrow:
            h_ += shift ? 10.0f : 1.0f;
            if (h_ >= 360.0f) h_ -= 360.0f;
            syncRgbFromHsv();
            notifyChanged();
            requestRepaint();
            e.accept();
            return true;

        case Key::UpArrow:
            v_ = std::clamp(v_ + (shift ? 0.1f : 0.02f), 0.0f, 1.0f);
            syncRgbFromHsv();
            notifyChanged();
            requestRepaint();
            e.accept();
            return true;

        case Key::DownArrow:
            v_ = std::clamp(v_ - (shift ? 0.1f : 0.02f), 0.0f, 1.0f);
            syncRgbFromHsv();
            notifyChanged();
            requestRepaint();
            e.accept();
            return true;

        default:
            return false;
        }
    }

    void ColorPicker::OnFocusOut() {
        if (open_) cancelDraft();
    }

}
