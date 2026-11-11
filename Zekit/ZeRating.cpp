
#include "pch.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "include/ZeRating.h"
#include "include/Zekit.h"
#include "ZeDrawUtil.h"

namespace Kit {

    namespace {
        constexpr float kPi = 3.1415926535897932f;
        constexpr float kInnerRatio = 0.42f;

        std::vector<Vec2> StarPoints(float cx, float cy, float rOuter) {
            const float rInner = rOuter * kInnerRatio;
            std::vector<Vec2> pts;
            pts.reserve(10);
            for (int i = 0; i < 10; ++i) {
                const float ang = -kPi * 0.5f + kPi * static_cast<float>(i) / 5.0f;
                const float r = ((i % 2) == 0) ? rOuter : rInner;
                pts.emplace_back(cx + r * std::cos(ang), cy + r * std::sin(ang));
            }
            return pts;
        }

        void PaintStar(FrameCollector& fc, float cx, float cy, float rOuter,
            const Color4f& color) {
            if (rOuter <= 0.0f || color.a <= 0.0f) return;
            fc.AddShape(PolygonShape(StarPoints(cx, cy, rOuter), true),
                color, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
        }
    }

    Rating::Rating(int maxStars)
        : maxStars_((maxStars > 0) ? maxStars : 1) {
        ApplyTheme(Themes::Light());
        SetFocusable(true);
        SetDefaultSize(120.0f, 22.0f);
    }

    void Rating::SetMaxStars(int n) {
        maxStars_ = (n > 0) ? n : 1;
        if (value_ > static_cast<float>(maxStars_)) value_ = static_cast<float>(maxStars_);
        hoverValue_ = -1.0f;
        notifyLayoutInputChanged();
        requestRepaint();
        RequestWindowRedraw();
    }

    float Rating::snap(float v) const {
        const float hi = static_cast<float>(maxStars_);
        if (v < 0.0f) v = 0.0f;
        if (v > hi) v = hi;
        if (allowHalf_) {
            v = std::round(v * 2.0f) * 0.5f;
            if (v < 0.5f) v = 0.0f;
        }
        else {
            v = std::round(v);
        }
        return v;
    }

    void Rating::SetValue(float v) {
        if (std::isnan(v)) v = 0.0f;
        value_ = snap(v);
        markStyled();
        requestRepaint();
        RequestWindowRedraw();
    }

    void Rating::SetAllowHalf(bool on) {
        if (allowHalf_ == on) return;
        allowHalf_ = on;
        value_ = snap(value_);
        hoverValue_ = -1.0f;
        requestRepaint();
        RequestWindowRedraw();
    }

    void Rating::SetAllowClear(bool on) { allowClear_ = on;   RequestWindowRedraw();}

    void Rating::SetReadOnly(bool ro) {
        readOnly_ = ro;
        if (ro) { hoverValue_ = -1.0f; dragging_ = false; }
        SetFocusable(!ro);
        requestRepaint();
        RequestWindowRedraw();
    }

    void Rating::SetEnabled(bool enabled) {
        BoxWidget::SetEnabled(enabled);
        if (!enabled) {
            hoverValue_ = -1.0f;
            dragging_ = false;
        }
        requestRepaint();
        RequestWindowRedraw();
    }

    void Rating::SetStarSize(float px) {
        starSize_ = (px > 0.0f) ? px : 0.0f;
        markStyled();
        notifyLayoutInputChanged();
        requestRepaint();
        RequestWindowRedraw();
    }

    void Rating::SetStarGap(float px) {
        gap_ = (px >= 0.0f) ? px : 0.0f;
        markStyled();
        notifyLayoutInputChanged();
        requestRepaint();
        RequestWindowRedraw();
    }

    void Rating::SetActiveColor(const Color4f& c) { activeColor_ = c; markStyled(); requestRepaint();   RequestWindowRedraw();}
    void Rating::SetInactiveColor(const Color4f& c) { inactiveColor_ = c; markStyled(); requestRepaint();   RequestWindowRedraw();}
    void Rating::SetHoverColor(const Color4f& c) { hoverColor_ = c; markStyled(); requestRepaint();   RequestWindowRedraw();}

    void Rating::SetHoverPreview(bool on) {
        hoverPreview_ = on;
        if (!on) hoverValue_ = -1.0f;
        requestRepaint();
        RequestWindowRedraw();
    }

    void Rating::SetOnValueChanged(SelfAction1<float> handler) { onChanged_ = std::move(handler); }
    void Rating::SetOnValueChanged(SelfAction handler) { onChangedSelf_ = std::move(handler); }

    void Rating::notify(float v) {
        if (onChanged_)     onChanged_(*this, v);
        if (onChangedSelf_) onChangedSelf_(*this);
    }

    void Rating::requestRepaint() {
        RequestWindowRedraw();
    }

    void Rating::ApplyTheme(const Theme& t) {
        const RatingStyle& rs = t.rating;
        activeColor_ = rs.activeColor;
        inactiveColor_ = rs.inactiveColor;
        hoverColor_ = rs.hoverColor;
        themeStarSize_ = (rs.starSize > 0.0f) ? rs.starSize : 22.0f;
        themeGap_ = (rs.gap >= 0.0f) ? rs.gap : 0.0f;
        hoverScale_ = (rs.hoverScale > 0.0f) ? rs.hoverScale : 1.0f;

        box_.colors = MultiStateColors{};
        box_.border = Border{};
        box_.padding = Padding{};
        box_.radiusMode = RadiusMode::None;
        box_.shadow.enabled = false;
        box_.outline.enabled = false;
        box_.opacity = 1.0f;
    }

    float Rating::step() const {
        const float s = (starSize_ > 0.0f) ? starSize_ : themeStarSize_;
        const float g = (gap_ >= 0.0f) ? gap_ : themeGap_;
        return s + g;
    }

    float Rating::hintWidth() const {
        if (prefWidth() > 0.0f) return prefWidth();
        const float s = (starSize_ > 0.0f) ? starSize_ : themeStarSize_;
        const float g = (gap_ >= 0.0f) ? gap_ : themeGap_;
        const int n = (maxStars_ > 0) ? maxStars_ : 1;
        return s * static_cast<float>(n) + g * static_cast<float>(n - 1);
    }

    float Rating::hintHeight() const {
        if (prefHeight() > 0.0f) return prefHeight();
        return (starSize_ > 0.0f) ? starSize_ : themeStarSize_;
    }

    float Rating::valueAtX(float px) const {
        const float st = step();
        if (st <= 0.0f) return 0.0f;

        const float local = px - x();
        if (local <= 0.0f) return 0.0f;

        const float starSize = (starSize_ > 0.0f) ? starSize_ : themeStarSize_;
        const int star = static_cast<int>(local / st);
        if (star >= maxStars_) return static_cast<float>(maxStars_);

        if (!allowHalf_) return snap(static_cast<float>(star + 1));

        const float within = local - static_cast<float>(star) * st;
        return snap(static_cast<float>(star) + ((within <= starSize * 0.5f) ? 0.5f : 1.0f));
    }

    void Rating::setByUser(float v) {
        const float nv = snap(v);
        const float before = value_;
        value_ = nv;
        if (nv != before) notify(nv);
        requestRepaint();
    }

    bool Rating::OnMousePress(MouseEvent& e) {
        if (!isEnabled() || readOnly_) return false;
        if (e.button != MouseButton::Left) return false;
        if (!HitTest(e.x, e.y)) return false;

        const float v = valueAtX(e.x);

        if (allowClear_ && v == value_ && v > 0.0f) {
            setByUser(0.0f);
        }
        else {
            setByUser(v);
        }
        dragging_ = true;
        SetFocusable(true);
        requestFocus();
        e.accept();
        return true;
    }

    bool Rating::OnMouseMove(MouseEvent& e) {
        if (!isEnabled() || readOnly_) return false;

        if (dragging_) {
            const float v = valueAtX(e.x);
            if (v != value_) setByUser(v);
            return true;
        }

        if (!hoverPreview_) return false;
        const float v = valueAtX(e.x);
        if (v == hoverValue_) return false;
        hoverValue_ = v;
        requestRepaint();
        return true;
    }

    bool Rating::OnMouseRelease(MouseEvent& e) {
        if (e.button != MouseButton::Left) return false;
        if (!dragging_) return false;

        dragging_ = false;
        const float v = valueAtX(e.x);
        if (v != value_) setByUser(v);
        return true;
    }

    void Rating::OnMouseLeave() {
        BoxWidget::OnMouseLeave();
        if (hoverValue_ >= 0.0f) {
            hoverValue_ = -1.0f;
            requestRepaint();
        }
    }

    bool Rating::OnKeyPress(KeyEvent& e) {
        if (!isEnabled() || readOnly_) return false;

        const float d = allowHalf_ ? 0.5f : 1.0f;
        switch (e.key) {
        case Key::LeftArrow:  setByUser(value_ - d); e.accept(); return true;
        case Key::RightArrow: setByUser(value_ + d); e.accept(); return true;
        case Key::Home:       setByUser(0.0f);       e.accept(); return true;
        case Key::End:        setByUser(static_cast<float>(maxStars_)); e.accept(); return true;
        default:
            return false;
        }
    }

    void Rating::paintContent(FrameCollector& fc) const {
        const float w = width();
        const float h = height();
        if (w <= 0.0f || h <= 0.0f) return;

        const float s = (starSize_ > 0.0f) ? starSize_ : themeStarSize_;
        const float g = (gap_ >= 0.0f) ? gap_ : themeGap_;
        const float st = s + g;
        if (st <= 0.0f) return;

        const float shown = (hoverPreview_ && hoverValue_ >= 0.0f) ? hoverValue_ : value_;
        const bool previewing = (hoverPreview_ && hoverValue_ >= 0.0f);

        Color4f active = activeColor_;
        if (previewing) active = hoverColor_;
        Color4f inactive = inactiveColor_;
        const float dim = isEnabled() ? 1.0f : 0.45f;
        active.a *= box_.opacity * dim;
        inactive.a *= box_.opacity * dim;

        const float cy = y() + h * 0.5f;

        for (int i = 0; i < maxStars_; ++i) {
            const float cx = x() + s * 0.5f + st * static_cast<float>(i);

            float fill = shown - static_cast<float>(i);
            fill = std::clamp(fill, 0.0f, 1.0f);

            float r = s * 0.5f;
            if (previewing && static_cast<float>(i) < shown) r *= hoverScale_;
            r = std::min(r, std::min(w, h) * 0.5f);

            PaintStar(fc, cx, cy, r, inactive);

            if (fill <= 0.0f) continue;

            const float clipW = r * 2.0f * fill;
            fc.PushClip(cx - r, cy - r, clipW, r * 2.0f);
            PaintStar(fc, cx, cy, r, active);
            fc.PopClip();
        }
    }

}
