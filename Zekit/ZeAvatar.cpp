
#include "pch.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include "include/ZeAvatar.h"
#include "include/Zekit.h"
#include "ZeDrawUtil.h"

namespace Kit {

    namespace {
        constexpr float kDefaultSide = 40.0f;

        std::string firstUtf8Char(const std::string& s) {
            if (s.empty()) return std::string();
            const std::vector<std::size_t> b = drawutil::Utf8Boundaries(s);
            if (b.size() < 2) return std::string();
            return s.substr(0, b[1]);
        }
    }

    Avatar::Avatar(std::string name)
        : name_(std::move(name)) {
        ApplyTheme(Themes::Light());
        SetFocusable(false);
        SetDefaultSize(kDefaultSide, kDefaultSide);
    }

    void Avatar::SetName(std::string name) {
        name_ = std::move(name);
        RequestWindowRedraw();
    }

    void Avatar::SetFallbackText(std::string text) {
        fallbackText_ = std::move(text);
        RequestWindowRedraw();
    }

    void Avatar::SetImage(std::string path) {
        imagePath_ = std::move(path);
        RequestWindowRedraw();
    }

    void Avatar::ClearImage() {
        imagePath_.clear();
        RequestWindowRedraw();
    }

    void Avatar::SetSide(float side) {
        side_ = (side > 0.0f) ? side : kDefaultSide;
        notifyLayoutInputChanged();
        RequestWindowRedraw();
    }

    void Avatar::SetShape(AvatarShape shape) {
        shape_ = shape;
        RequestWindowRedraw();
    }

    void Avatar::SetAvatarColor(const Color4f& c) {
        colorSet_ = true;
        colorOverride_ = c;
        markStyled();
        RequestWindowRedraw();
    }

    void Avatar::ClearAvatarColor() {
        colorSet_ = false;
        RequestWindowRedraw();
    }

    void Avatar::SetStatus(AvatarStatus s) {
        status_ = s;
        RequestWindowRedraw();
    }

    void Avatar::SetRingWidth(float px) {
        borderSet_ = true;
        borderWidthOverride_ = (px > 0.0f) ? px : 0.0f;
        RequestWindowRedraw();
    }

    void Avatar::ApplyTheme(const Theme& t) {
        const AvatarStyle& as = t.avatar;
        initialsStyle_ = as.initials;
        themeBorderWidth_ = (as.borderWidth > 0.0f) ? as.borderWidth : 0.0f;
        themeBorderColor_ = as.borderColor;
        fallbackColor_ = as.fallback;
        statusSize_ = (as.statusSize > 0.0f) ? as.statusSize : 10.0f;
        statusBorderWidth_ = (as.statusBorderWidth > 0.0f) ? as.statusBorderWidth : 0.0f;
        statusBorderColor_ = as.statusBorderColor;
        statusOnline_ = as.statusOnline;
        statusOffline_ = as.statusOffline;
        statusBusy_ = as.statusBusy;
        statusAway_ = as.statusAway;
        for (int i = 0; i < 6; ++i) palette_[i] = as.palette[i];

        box_.colors = MultiStateColors{};
        box_.border = Border{};
        box_.padding = Padding{};
        box_.radiusMode = RadiusMode::None;
        box_.shadow.enabled = false;
        box_.outline.enabled = false;
        box_.opacity = 1.0f;
    }

    int Avatar::paletteIndex() const {
        unsigned int h = 2166136261u;
        for (unsigned char c : name_) {
            h ^= c;
            h *= 16777619u;
        }
        return static_cast<int>(h % 6u);
    }

    Color4f Avatar::resolvedColor() const {
        if (colorSet_) return colorOverride_;
        const Color4f& c = palette_[paletteIndex()];
        if (c.a > 0.0f) return c;
        return fallbackColor_;
    }

    std::string Avatar::resolvedInitials() const {
        if (!fallbackText_.empty()) return fallbackText_;
        return firstUtf8Char(name_);
    }

    float Avatar::hintWidth() const {
        return prefWidth() > 0.0f ? prefWidth() : side_;
    }

    float Avatar::hintHeight() const {
        return prefHeight() > 0.0f ? prefHeight() : side_;
    }

    void Avatar::paintContent(FrameCollector& fc) const {
        const float w = width();
        const float h = height();
        if (w <= 0.0f || h <= 0.0f) return;

        const float d = std::min(w, h);
        const float rx = x() + (w - d) * 0.5f;
        const float ry = y() + (h - d) * 0.5f;

        const float ring = std::max(0.0f, borderSet_ ? borderWidthOverride_ : themeBorderWidth_);
        const Color4f bg = resolvedColor();
        const bool circle = (shape_ == AvatarShape::Circle);
        const float rad = (shape_ == AvatarShape::Rounded) ? d * 0.22f : 0.0f;

        const auto paintShape = [&](float sx, float sy, float dd, float rr, const Color4f& c) {
            if (circle) {
                drawutil::PaintCircle(fc, sx + dd * 0.5f, sy + dd * 0.5f, dd * 0.5f, c);
            }
            else {
                drawutil::PaintRoundedRect(fc, sx, sy, dd, dd, rr, c);
            }
        };

        paintShape(rx, ry, d, rad, bg);

        const bool hasRing = (ring > 0.0f && themeBorderColor_.a > 0.0f);
        if (hasRing) {
            Color4f ringColor = themeBorderColor_;
            ringColor.a *= box_.opacity;
            paintShape(rx, ry, d, rad, ringColor);
            paintShape(rx + ring, ry + ring, d - ring * 2.0f, std::max(0.0f, rad - ring), bg);
        }

        const float inset = hasRing ? ring : 0.0f;
        const float ix = rx + inset;
        const float iy = ry + inset;
        const float id = std::max(0.0f, d - inset * 2.0f);
        if (id <= 0.0f) return;

        if (hasImage()) {
            float ax = ix, ay = iy, aw = id, ah = id;
            if (circle) {
                float side = 0.0f;
                drawutil::InscribedSquare(ix + id * 0.5f, iy + id * 0.5f, id * 0.5f,
                    ax, ay, side);
                aw = side;
                ah = side;
            }
            fc.AddImage(imagePath_, ax, ay, aw, ah,
                BgImageDrawMode::Fill, BgImageHAlign::Center, BgImageVAlign::Middle,
                Color4f::White());
        }
        else {
            const std::string initials = resolvedInitials();
            Color4f fg = initialsStyle_.colors.get(WidgetState::Normal).text;
            fg.a *= box_.opacity;

            if (!initials.empty()) {
                drawutil::PaintTextCentered(fc, initials, initialsStyle_.font,
                    ix, iy, id, id, fg);
            }
            else {
                Color4f glyph = fg;
                glyph.a *= 0.85f;
                if (glyph.a > 0.0f) {
                    const float ccx = ix + id * 0.5f;
                    const float ccy = iy + id * 0.5f;
                    drawutil::PaintCircle(fc, ccx, ccy - id * 0.11f, id * 0.155f, glyph);
                    const float sw = id * 0.46f;
                    const float sh = id * 0.26f;
                    drawutil::PaintRoundedRect(fc, ccx - sw * 0.5f, ccy + id * 0.14f,
                        sw, sh, sh * 0.5f, glyph);
                }
            }
        }

        if (status_ == AvatarStatus::None) return;

        const Color4f dotColor =
            (status_ == AvatarStatus::Online) ? statusOnline_ :
            (status_ == AvatarStatus::Offline) ? statusOffline_ :
            (status_ == AvatarStatus::Busy) ? statusBusy_ : statusAway_;

        const float r = statusSize_ * 0.5f;
        const float dx = x() + w - r - 1.0f;
        const float dy = y() + h - r - 1.0f;

        if (statusBorderWidth_ > 0.0f && statusBorderColor_.a > 0.0f) {
            drawutil::PaintCircle(fc, dx, dy, r + statusBorderWidth_, statusBorderColor_);
        }
        drawutil::PaintCircle(fc, dx, dy, r, dotColor);
    }

}
