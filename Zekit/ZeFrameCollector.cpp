#include "pch.h"
#include "ZeFrameCollector.h"
#include "ZeImageStore.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>

namespace Kit {

    constexpr float kSdfPadAA = 1.5f;

    void FrameCollector::PushClip(float x, float y, float w, float h) {
        if (w <= 0.f || h <= 0.f) {
            clipStack_.push_back({ x, y, 0.f, 0.f });
        } else {
            clipStack_.push_back({ x, y, w, h });
        }

        if (clipStack_.size() == 1) {
            currentClip_ = clipStack_.back();
            hasClip_ = true;
        } else {
            ClipRect& n = clipStack_.back();
            float nx = std::max(currentClip_.x, n.x);
            float ny = std::max(currentClip_.y, n.y);
            float nR = std::min(currentClip_.x + currentClip_.w, n.x + n.w);
            float nB = std::min(currentClip_.y + currentClip_.h, n.y + n.h);
            currentClip_ = { nx, ny, std::max(0.f, nR - nx), std::max(0.f, nB - ny) };
            hasClip_ = true;
        }
    }

    void FrameCollector::PopClip() {
        if (clipStack_.empty()) return;
        clipStack_.pop_back();

        if (clipStack_.empty()) {
            hasClip_ = false;
            currentClip_ = { 0, 0, 0, 0 };
        } else {
            currentClip_ = clipStack_[0];
            hasClip_ = true;
            for (size_t i = 1; i < clipStack_.size(); ++i) {
                ClipRect& n = clipStack_[i];
                float nx = std::max(currentClip_.x, n.x);
                float ny = std::max(currentClip_.y, n.y);
                float nR = std::min(currentClip_.x + currentClip_.w, n.x + n.w);
                float nB = std::min(currentClip_.y + currentClip_.h, n.y + n.h);
                currentClip_ = { nx, ny, std::max(0.f, nR - nx), std::max(0.f, nB - ny) };
            }
        }
    }

    void FrameCollector::ResetClip() {
        clipStack_.clear();
        hasClip_ = false;
        currentClip_ = { 0, 0, 0, 0 };
    }

    namespace {
        constexpr int kMaxClipVerts = 16;

        inline void ClipEdge(const UIVertex& in, const UIVertex& out,
            int axis, bool keepGreater, float bound,
            UIVertex* dst, int& n) {
            float inV  = (axis == 0) ? in.x : in.y;
            float outV = (axis == 0) ? out.x : out.y;
            bool inIn  = keepGreater ? (inV >= bound) : (inV <= bound);
            bool outIn = keepGreater ? (outV >= bound) : (outV <= bound);

            if (inIn && outIn) {
                if (n < kMaxClipVerts) dst[n++] = out;
            } else if (inIn && !outIn) {
                float t = (bound - inV) / (outV - inV);
                UIVertex p = in;
                p.x = in.x + t * (out.x - in.x);
                p.y = in.y + t * (out.y - in.y);
                p.r = in.r + t * (out.r - in.r);
                p.g = in.g + t * (out.g - in.g);
                p.b = in.b + t * (out.b - in.b);
                p.a = in.a + t * (out.a - in.a);
                p.u = in.u + t * (out.u - in.u);
                p.v = in.v + t * (out.v - in.v);
                p.cx = in.cx + t * (out.cx - in.cx);
                p.cy = in.cy + t * (out.cy - in.cy);
                p.hw = in.hw + t * (out.hw - in.hw);
                p.hh = in.hh + t * (out.hh - in.hh);
                p.rad = in.rad + t * (out.rad - in.rad);
                if (n < kMaxClipVerts) dst[n++] = p;
            } else if (!inIn && outIn) {
                float t = (bound - inV) / (outV - inV);
                UIVertex p = in;
                p.x = in.x + t * (out.x - in.x);
                p.y = in.y + t * (out.y - in.y);
                p.r = in.r + t * (out.r - in.r);
                p.g = in.g + t * (out.g - in.g);
                p.b = in.b + t * (out.b - in.b);
                p.a = in.a + t * (out.a - in.a);
                p.u = in.u + t * (out.u - in.u);
                p.v = in.v + t * (out.v - in.v);
                p.cx = in.cx + t * (out.cx - in.cx);
                p.cy = in.cy + t * (out.cy - in.cy);
                p.hw = in.hw + t * (out.hw - in.hw);
                p.hh = in.hh + t * (out.hh - in.hh);
                p.rad = in.rad + t * (out.rad - in.rad);
                if (n < kMaxClipVerts) dst[n++] = p;
                if (n < kMaxClipVerts) dst[n++] = out;
            }
        }

        int ClipPolygonToRect(const UIVertex* in, int n,
            float cx, float cy, float cw, float ch, UIVertex* out) {
            if (n < 3) return 0;
            if (n > kMaxClipVerts) n = kMaxClipVerts;

            UIVertex bufA[kMaxClipVerts];
            UIVertex bufB[kMaxClipVerts];
            for (int i = 0; i < n; ++i) bufA[i] = in[i];

            UIVertex* src = bufA;
            UIVertex* dst = bufB;
            int count = n;

            const float x0 = cx, y0 = cy, x1 = cx + cw, y1 = cy + ch;
            const int   axes[4]    = { 0,    0,    1,    1 };
            const bool  keepGt[4]  = { true, false, true, false };
            const float bounds[4]  = { x0,   x1,   y0,   y1 };

            for (int pass = 0; pass < 4; ++pass) {
                int m = 0;
                for (int i = 0; i < count; ++i)
                    ClipEdge(src[i], src[(i + 1) % count],
                        axes[pass], keepGt[pass], bounds[pass], dst, m);

                UIVertex* t = src; src = dst; dst = t;
                count = m;
                if (count < 3) return 0;
            }

            for (int i = 0; i < count; ++i) out[i] = src[i];
            return count;
        }
    }

    void FrameCollector::AddRect(float x, float y, float w, float h, const Color4f& c,
        float radius) {
        if (w <= 0.f || h <= 0.f) return;

        const float ccx = x + w * 0.5f, ccy = y + h * 0.5f;
        const float hw = w * 0.5f, hh = h * 0.5f;
        float rad = (radius > 0.0f) ? radius : 0.0f;
        const float maxR = (hw < hh) ? hw : hh;
        if (rad > maxR) rad = maxR;

        const float pad = kSdfPadAA;
        const UIVertex a{ x - pad,         y - pad,         c.r, c.g, c.b, c.a, 0, 0, ccx, ccy, hw, hh, rad };
        const UIVertex b{ x - pad,         y + h + pad,     c.r, c.g, c.b, c.a, 0, 0, ccx, ccy, hw, hh, rad };
        const UIVertex d{ x + w + pad,     y + h + pad,     c.r, c.g, c.b, c.a, 0, 0, ccx, ccy, hw, hh, rad };
        const UIVertex e{ x + w + pad,     y - pad,         c.r, c.g, c.b, c.a, 0, 0, ccx, ccy, hw, hh, rad };
        AppendQuadClipped(a, b, d, e, 0, false);
    }

    bool FrameCollector::AddDashedRect(float x, float y, float w, float h, float radius,
        float width, float dashLen, float gapLen, const Color4f& color) {
        if (w <= 0.0f || h <= 0.0f || width <= 0.0f) return false;
        if (dashLen <= 0.0f || gapLen <= 0.0f) return false;

        const float half = width * 0.5f;
        const float px = x + half, py = y + half;
        const float pw = w - width,  ph = h - width;
        if (pw <= 0.0f || ph <= 0.0f) return false;

        float r = radius - half;
        if (r < 0.0f) r = 0.0f;
        r = std::min(r, std::min(pw, ph) * 0.5f);

        std::vector<float> xs, ys;
        auto pushPoint = [&](float ax, float ay) {
            if (!xs.empty() &&
                std::fabs(xs.back() - ax) < 1e-4f && std::fabs(ys.back() - ay) < 1e-4f) {
                return;
            }
            xs.push_back(ax);
            ys.push_back(ay);
            };
        auto pushArc = [&](float ccx, float ccy, float rr, float a0, float a1) {
            constexpr int kSteps = 8;
            for (int i = 1; i <= kSteps; ++i) {
                const float t = a0 + (a1 - a0) * (static_cast<float>(i) / kSteps);
                pushPoint(ccx + rr * std::cos(t), ccy + rr * std::sin(t));
            }
            };
        constexpr float kPi = 3.14159265358979f;
        constexpr float kHalfPi = kPi * 0.5f;

        pushPoint(px + r, py);
        pushPoint(px + pw - r, py);
        pushArc(px + pw - r, py + r, r, -kHalfPi, 0.0f);
        pushPoint(px + pw, py + ph - r);
        pushArc(px + pw - r, py + ph - r, r, 0.0f, kHalfPi);
        pushPoint(px + r, py + ph);
        pushArc(px + r, py + ph - r, r, kHalfPi, kPi);
        pushPoint(px, py + r);
        pushArc(px + r, py + r, r, kPi, kPi + kHalfPi);
        pushPoint(px + r, py);

        const std::size_t n = xs.size();
        if (n < 3) return false;

        std::vector<float> seg;
        seg.reserve(n - 1);
        float total = 0.0f;
        for (std::size_t i = 0; i + 1 < n; ++i) {
            const float dx = xs[i + 1] - xs[i];
            const float dy = ys[i + 1] - ys[i];
            const float len = std::sqrt(dx * dx + dy * dy);
            seg.push_back(len);
            total += len;
        }
        if (total <= 0.0f) return false;

        auto emitThickSegment = [&](float ax, float ay, float bx, float by) {
            const float dx = bx - ax, dy = by - ay;
            const float len = std::sqrt(dx * dx + dy * dy);
            if (len <= 1e-5f) return;
            const float nx = -dy / len * half;
            const float ny = dx / len * half;
            const float cr = color.r, cg = color.g, cb = color.b, ca = color.a;
            const UIVertex v0{ ax + nx, ay + ny, cr, cg, cb, ca, 0, 0, 0, 0, 0, 0, 0.f };
            const UIVertex v1{ ax - nx, ay - ny, cr, cg, cb, ca, 0, 0, 0, 0, 0, 0, 0.f };
            const UIVertex v2{ bx - nx, by - ny, cr, cg, cb, ca, 0, 0, 0, 0, 0, 0, 0.f };
            const UIVertex v3{ bx + nx, by + ny, cr, cg, cb, ca, 0, 0, 0, 0, 0, 0, 0.f };
            AppendQuadClipped(v0, v1, v2, v3, 0u, false);
            };

        constexpr int kMaxDashes = 4096;
        const float period = dashLen + gapLen;
        int drawn = 0;
        for (float s0 = 0.0f; s0 < total; s0 += period) {
            if (++drawn > kMaxDashes) break;

            const float s1 = std::min(s0 + dashLen, total);
            if (s1 <= s0) break;

            float acc = 0.0f;
            for (std::size_t i = 0; i + 1 < n; ++i) {
                const float a = acc;
                const float b = acc + seg[i];
                acc = b;
                if (b <= s0) continue;
                if (a >= s1) break;

                const float t0 = (std::max(a, s0) - a) / seg[i];
                const float t1 = (std::min(b, s1) - a) / seg[i];
                emitThickSegment(
                    xs[i] + (xs[i + 1] - xs[i]) * t0,
                    ys[i] + (ys[i + 1] - ys[i]) * t0,
                    xs[i] + (xs[i + 1] - xs[i]) * t1,
                    ys[i] + (ys[i + 1] - ys[i]) * t1);
            }
        }
        return true;
    }

    bool FrameCollector::AddGeometry(const Geometry& geo, const Color4f& color,
        float cx, float cy, float hw, float hh, float rad) {
        if (geo.vertices.empty() || geo.indices.empty()) return true;

        if (geo.mode != DrawMode::Triangles) {
            static bool warned[8] = {};
            const int m = static_cast<int>(geo.mode);
            if (m >= 0 && m < 8 && !warned[m]) {
                warned[m] = true;
                std::cerr << "[Zekit] AddGeometry: unsupported DrawMode (" << m
                    << "); only Triangles is accepted -- primitive skipped (printed once)\n";
            }
            return false;
        }

        const bool sdfOn = (hw > 0.0f || hh > 0.0f);
        const float kx = (sdfOn && hw > 0.0f) ? (hw + kSdfPadAA) / hw : 1.0f;
        const float ky = (sdfOn && hh > 0.0f) ? (hh + kSdfPadAA) / hh : 1.0f;
        const bool needsPad = (kx != 1.0f || ky != 1.0f);
        auto mapP = [&](const Vec2& p) -> Vec2 {
            if (!needsPad) return p;
            return { cx + (p.x - cx) * kx, cy + (p.y - cy) * ky };
        };

        if (hasClip_) {
            const float clx = currentClip_.x, cly = currentClip_.y;
            const float clw = currentClip_.w, clh = currentClip_.h;
            const float crx = clx + clw, cby = cly + clh;

            UIVertex tri[3];
            UIVertex clipped[kMaxClipVerts];
            verts_.reserve(verts_.size() + geo.vertices.size());

            for (size_t i = 0; i + 2 < geo.indices.size(); i += 3) {
                const Vec2 q0 = mapP(geo.vertices[geo.indices[i + 0]]);
                const Vec2 q1 = mapP(geo.vertices[geo.indices[i + 1]]);
                const Vec2 q2 = mapP(geo.vertices[geo.indices[i + 2]]);

                const float minX = std::min({ q0.x, q1.x, q2.x });
                const float maxX = std::max({ q0.x, q1.x, q2.x });
                const float minY = std::min({ q0.y, q1.y, q2.y });
                const float maxY = std::max({ q0.y, q1.y, q2.y });

                if (maxX <= clx || minX >= crx || maxY <= cly || minY >= cby) continue;

                if (minX >= clx && maxX <= crx && minY >= cly && maxY <= cby) {
                    const uint32_t base = static_cast<uint32_t>(verts_.size());
                    verts_.push_back({ q0.x, q0.y, color.r, color.g, color.b, color.a, 0.f, 0.f, cx, cy, hw, hh, rad });
                    verts_.push_back({ q1.x, q1.y, color.r, color.g, color.b, color.a, 0.f, 0.f, cx, cy, hw, hh, rad });
                    verts_.push_back({ q2.x, q2.y, color.r, color.g, color.b, color.a, 0.f, 0.f, cx, cy, hw, hh, rad });
                    const uint32_t start = static_cast<uint32_t>(idx_.size());
                    idx_.push_back(base);
                    idx_.push_back(base + 1);
                    idx_.push_back(base + 2);
                    RegisterBatch(0u, false, start, 3);
                    continue;
                }

                tri[0] = { q0.x, q0.y, color.r, color.g, color.b, color.a, 0.f, 0.f, cx, cy, hw, hh, rad };
                tri[1] = { q1.x, q1.y, color.r, color.g, color.b, color.a, 0.f, 0.f, cx, cy, hw, hh, rad };
                tri[2] = { q2.x, q2.y, color.r, color.g, color.b, color.a, 0.f, 0.f, cx, cy, hw, hh, rad };

                const int m = ClipPolygonToRect(tri, 3, clx, cly, clw, clh, clipped);
                if (m < 3) continue;

                const uint32_t base = static_cast<uint32_t>(verts_.size());
                for (int k = 0; k < m; ++k) verts_.push_back(clipped[k]);
                const uint32_t start = static_cast<uint32_t>(idx_.size());
                for (int k = 1; k + 1 < m; ++k) {
                    idx_.push_back(base);
                    idx_.push_back(base + static_cast<uint32_t>(k));
                    idx_.push_back(base + static_cast<uint32_t>(k + 1));
                }
                RegisterBatch(0u, false, start, static_cast<uint32_t>((m - 2) * 3));
            }
            return true;
        }

        const uint32_t base = static_cast<uint32_t>(verts_.size());
        const uint32_t start = static_cast<uint32_t>(idx_.size());

        verts_.reserve(verts_.size() + geo.vertices.size());
        for (const Vec2& p : geo.vertices) {
            const Vec2 q = mapP(p);
            verts_.push_back({ q.x, q.y, color.r, color.g, color.b, color.a, 0.f, 0.f, cx, cy, hw, hh, rad });
        }

        idx_.reserve(idx_.size() + geo.indices.size());
        for (uint32_t i : geo.indices)
            idx_.push_back(base + i);

        RegisterBatch(0u, false, start, static_cast<uint32_t>(geo.indices.size()));
        return true;
    }

    bool FrameCollector::AddShape(const Shape& shape, const Color4f& color,
        float cx, float cy, float hw, float hh, float rad) {
        const Geometry geo = shape.build();

        static const bool trace = [] {
            const char* v = std::getenv("ZEKIT_TRACE_SHAPES");
            return v && *v != '\0' && *v != '0';
            }();
        if (trace && !geo.vertices.empty()) {
            float x0 = geo.vertices[0].x, x1 = x0;
            float y0 = geo.vertices[0].y, y1 = y0;
            for (const Vec2& v : geo.vertices) {
                x0 = std::min(x0, v.x); x1 = std::max(x1, v.x);
                y0 = std::min(y0, v.y); y1 = std::max(y1, v.y);
            }
            std::fprintf(stderr,
                "[shape] bbox=(%.1f,%.1f)-(%.1f,%.1f) verts=%zu color=(%.3f,%.3f,%.3f,%.3f) sdf=(%.1f,%.1f,%.1f,%.1f,%.1f)\n",
                x0, y0, x1, y1, geo.vertices.size(),
                color.r, color.g, color.b, color.a, cx, cy, hw, hh, rad);
        }

        return AddGeometry(geo, color, cx, cy, hw, hh, rad);
    }

    bool FrameCollector::AddTriangles(const PlotVertex* verts, std::size_t count,
        float dx, float dy) {
        if (!verts || count == 0 || (count % 3) != 0) return false;

        auto make = [&](const PlotVertex& v) -> UIVertex {
            return UIVertex{ v.x + dx, v.y + dy, v.r, v.g, v.b, v.a,
                             0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f };
        };

        if (!hasClip_) {
            const uint32_t base = static_cast<uint32_t>(verts_.size());
            const uint32_t start = static_cast<uint32_t>(idx_.size());
            verts_.reserve(verts_.size() + count);
            idx_.reserve(idx_.size() + count);
            for (std::size_t i = 0; i < count; ++i)
                verts_.push_back(make(verts[i]));
            for (std::size_t i = 0; i < count; ++i)
                idx_.push_back(base + static_cast<uint32_t>(i));
            RegisterBatch(0u, false, start, static_cast<uint32_t>(count));
            return true;
        }

        const float clx = currentClip_.x, cly = currentClip_.y;
        const float clw = currentClip_.w, clh = currentClip_.h;
        const float crx = clx + clw, cby = cly + clh;

        UIVertex tri[3];
        UIVertex clipped[kMaxClipVerts];
        verts_.reserve(verts_.size() + count);

        for (std::size_t i = 0; i + 2 < count; i += 3) {
            tri[0] = make(verts[i + 0]);
            tri[1] = make(verts[i + 1]);
            tri[2] = make(verts[i + 2]);

            const float minX = std::min({ tri[0].x, tri[1].x, tri[2].x });
            const float maxX = std::max({ tri[0].x, tri[1].x, tri[2].x });
            const float minY = std::min({ tri[0].y, tri[1].y, tri[2].y });
            const float maxY = std::max({ tri[0].y, tri[1].y, tri[2].y });

            if (maxX <= clx || minX >= crx || maxY <= cly || minY >= cby) continue;

            const bool inside = (minX >= clx && maxX <= crx && minY >= cly && maxY <= cby);
            const int m = inside ? 3
                                 : ClipPolygonToRect(tri, 3, clx, cly, clw, clh, clipped);
            if (m < 3) continue;

            const UIVertex* src = inside ? tri : clipped;

            const uint32_t base = static_cast<uint32_t>(verts_.size());
            const uint32_t start = static_cast<uint32_t>(idx_.size());
            for (int k = 0; k < m; ++k) verts_.push_back(src[k]);
            for (int k = 1; k + 1 < m; ++k) {
                idx_.push_back(base);
                idx_.push_back(base + static_cast<uint32_t>(k));
                idx_.push_back(base + static_cast<uint32_t>(k + 1));
            }
            RegisterBatch(0u, false, start, static_cast<uint32_t>((m - 2) * 3));
        }
        return true;
    }

    void FrameCollector::AddText(std::string_view utf8, const FontDesc& font,
        float x, float y, const Color4f& color) {
        if (!font_ || utf8.empty()) return;

        auto quads = font_->LayoutText(utf8, font, x, y);
        for (const GlyphQuad& q : quads) {
            const float cr = q.isColor ? 1.0f : color.r;
            const float cg = q.isColor ? 1.0f : color.g;
            const float cb = q.isColor ? 1.0f : color.b;

            UIVertex a{ q.x,       q.y,       cr, cg, cb, color.a, q.u0, q.v0, 0.f, 0.f, 0.f, 0.f, 0.f };
            UIVertex b{ q.x,       q.y + q.h, cr, cg, cb, color.a, q.u0, q.v1, 0.f, 0.f, 0.f, 0.f, 0.f };
            UIVertex d{ q.x + q.w, q.y + q.h, cr, cg, cb, color.a, q.u1, q.v1, 0.f, 0.f, 0.f, 0.f, 0.f };
            UIVertex e{ q.x + q.w, q.y,       cr, cg, cb, color.a, q.u1, q.v0, 0.f, 0.f, 0.f, 0.f, 0.f };
            AppendQuadClipped(a, b, d, e, q.texture, q.isColor);
        }
    }

    TextMetrics FrameCollector::MeasureText(std::string_view utf8, const FontDesc& font) {
        if (!font_) return TextMetrics{};
        return font_->MeasureText(utf8, font);
    }

    FrameCollector::ImageDrawRect FrameCollector::ComputeImageRect(const BackgroundImage& img,
        float x, float y, float w, float h,
        BgImageDrawMode mode, BgImageHAlign hAlign, BgImageVAlign vAlign) {
        ImageDrawRect r{};
        if (img.width <= 0 || img.height <= 0 || w <= 0.f || h <= 0.f) return r;

        const float iw = static_cast<float>(img.width);
        const float ih = static_cast<float>(img.height);

        auto alignX = [](BgImageHAlign a, float available) -> float {
            switch (a) {
            case BgImageHAlign::Left:   return 0.0f;
            case BgImageHAlign::Right:  return available;
            case BgImageHAlign::Center: return available * 0.5f;
            }
            return 0.0f;
            };
        auto alignY = [](BgImageVAlign a, float available) -> float {
            switch (a) {
            case BgImageVAlign::Top:    return 0.0f;
            case BgImageVAlign::Bottom: return available;
            case BgImageVAlign::Middle: return available * 0.5f;
            }
            return 0.0f;
            };

        switch (mode) {
        case BgImageDrawMode::Stretch:
            r = { x, y, w, h, 0.0f, 0.0f, 1.0f, 1.0f };
            break;

        case BgImageDrawMode::Fit: {
            const float s = std::min(w / iw, h / ih);
            const float dw = iw * s, dh = ih * s;
            r = { x + (w - dw) * 0.5f, y + (h - dh) * 0.5f, dw, dh, 0.0f, 0.0f, 1.0f, 1.0f };
            break;
        }

        case BgImageDrawMode::Fill: {
            const float s = std::max(w / iw, h / ih);
            const float visW = w / s;
            const float visH = h / s;
            const float u0 = (iw - visW) * 0.5f / iw;
            const float v0 = (ih - visH) * 0.5f / ih;
            r = { x, y, w, h, u0, v0, u0 + visW / iw, v0 + visH / ih };
            break;
        }

        case BgImageDrawMode::Tiled:
            r = { x, y, iw, ih, 0.0f, 0.0f, 1.0f, 1.0f };
            break;

        case BgImageDrawMode::Origin:
            r = { x + alignX(hAlign, w - iw), y + alignY(vAlign, h - ih), iw, ih,
                  0.0f, 0.0f, 1.0f, 1.0f };
            break;
        }
        return r;
    }

    bool FrameCollector::AddImage(std::string_view path,
        float x, float y, float w, float h,
        BgImageDrawMode mode, BgImageHAlign hAlign, BgImageVAlign vAlign,
        const Color4f& tint) {
        if (!images_) {
            static bool warned = false;
            if (!warned) {
                warned = true;
                std::cerr << "[Zekit] AddImage: no image store injected this frame"
                    << " (Window should call fc.SetImageStore(...) every frame)"
                    << " -- image skipped (printed once)\n";
            }
            return false;
        }
        if (path.empty()) return false;

        const ImageHandle handle = images_->Load(path);
        if (!handle) return false;

        return AddImageHandle(handle, x, y, w, h, mode, hAlign, vAlign, tint);
    }

    bool FrameCollector::AddImageResource(int resId,
        float x, float y, float w, float h,
        BgImageDrawMode mode, BgImageHAlign hAlign, BgImageVAlign vAlign,
        const Color4f& tint) {
        if (!images_) {
            static bool warned = false;
            if (!warned) {
                warned = true;
                std::cerr << "[Zekit] AddImageResource: no image store injected this frame"
                    << " (Window should call fc.SetImageStore(...) every frame)"
                    << " -- image skipped (printed once)\n";
            }
            return false;
        }
        if (resId <= 0) return false;

        const ImageHandle handle = images_->LoadFromResource(resId);
        if (!handle) return false;

        return AddImageHandle(handle, x, y, w, h, mode, hAlign, vAlign, tint);
    }

    bool FrameCollector::AddImageHandle(ImageHandle handle,
        float x, float y, float w, float h,
        BgImageDrawMode mode, BgImageHAlign hAlign, BgImageVAlign vAlign,
        const Color4f& tint) {
        if (!images_ || handle == 0) return false;

        const BackgroundImage* img = images_->Get(handle);
        if (!img) return false;

        const ImageDrawRect first = ComputeImageRect(*img, x, y, w, h, mode, hAlign, vAlign);
        if (first.w <= 0.0f || first.h <= 0.0f) return false;

        auto emit = [&](const ImageDrawRect& r) {
            const UIVertex a{ r.x,       r.y,       tint.r, tint.g, tint.b, tint.a, r.u0, r.v0, 0, 0, 0, 0, 0.f };
            const UIVertex b{ r.x,       r.y + r.h, tint.r, tint.g, tint.b, tint.a, r.u0, r.v1, 0, 0, 0, 0, 0.f };
            const UIVertex c{ r.x + r.w, r.y + r.h, tint.r, tint.g, tint.b, tint.a, r.u1, r.v1, 0, 0, 0, 0, 0.f };
            const UIVertex d{ r.x + r.w, r.y,       tint.r, tint.g, tint.b, tint.a, r.u1, r.v0, 0, 0, 0, 0, 0.f };
            AppendQuadClipped(a, b, c, d, img->handle, true);
            };

        if (mode != BgImageDrawMode::Tiled) {
            emit(first);
            return true;
        }

        const float stepX = first.w, stepY = first.h;
        if (stepX <= 0.0f || stepY <= 0.0f) return false;

        const int nx = static_cast<int>(std::ceil(w / stepX));
        const int ny = static_cast<int>(std::ceil(h / stepY));

        constexpr int kMaxTiles = 4096;
        if (nx <= 0 || ny <= 0 || static_cast<long long>(nx) * ny > kMaxTiles) {
            emit(first);
            return true;
        }

        PushClip(x, y, w, h);
        for (int j = 0; j < ny; ++j) {
            for (int i = 0; i < nx; ++i) {
                ImageDrawRect r = first;
                r.x = x + static_cast<float>(i) * stepX;
                r.y = y + static_cast<float>(j) * stepY;
                emit(r);
            }
        }
        PopClip();
        return true;
    }

    bool FrameCollector::ImageSize(std::string_view path, float& w, float& h) const {
        if (!images_) {
            static bool warned = false;
            if (!warned) {
                warned = true;
                std::cerr << "[Zekit] ImageSize: no image store injected this frame"
                    << " (Window should call fc.SetImageStore(...) every frame)"
                    << " -- size unavailable (printed once)\n";
            }
            return false;
        }
        if (path.empty()) return false;

        int iw = 0, ih = 0;
        if (!images_->SizeOf(path, iw, ih)) return false;

        w = static_cast<float>(iw);
        h = static_cast<float>(ih);
        return true;
    }

    void FrameCollector::AppendQuad(const UIVertex& a, const UIVertex& b,
        const UIVertex& c, const UIVertex& d,
        uint32_t texture, bool isColor) {
        const uint32_t base = static_cast<uint32_t>(verts_.size());
        const uint32_t start = static_cast<uint32_t>(idx_.size());

        verts_.push_back(a);
        verts_.push_back(b);
        verts_.push_back(c);
        verts_.push_back(d);
        idx_.push_back(base + 0); idx_.push_back(base + 1); idx_.push_back(base + 2);
        idx_.push_back(base + 0); idx_.push_back(base + 2); idx_.push_back(base + 3);

        RegisterBatch(texture, isColor, start, 6);
    }

    bool FrameCollector::AppendQuadClipped(const UIVertex& a, const UIVertex& b,
        const UIVertex& c, const UIVertex& d,
        uint32_t texture, bool isColor) {
        if (!hasClip_) {
            AppendQuad(a, b, c, d, texture, isColor);
            return true;
        }

        const float cx = currentClip_.x, cy = currentClip_.y;
        const float cw = currentClip_.w, ch = currentClip_.h;
        if (cw <= 0.f || ch <= 0.f) return false;

        float minX = std::min({ a.x, b.x, c.x, d.x });
        float maxX = std::max({ a.x, b.x, c.x, d.x });
        float minY = std::min({ a.y, b.y, c.y, d.y });
        float maxY = std::max({ a.y, b.y, c.y, d.y });
        if (maxX <= cx || minX >= cx + cw || maxY <= cy || minY >= cy + ch)
            return false;

        if (minX >= cx && maxX <= cx + cw && minY >= cy && maxY <= cy + ch) {
            AppendQuad(a, b, c, d, texture, isColor);
            return true;
        }

        const UIVertex quad[4] = { a, b, c, d };
        UIVertex clipped[kMaxClipVerts];
        const int m = ClipPolygonToRect(quad, 4, cx, cy, cw, ch, clipped);
        if (m < 3) return false;

        const uint32_t base = static_cast<uint32_t>(verts_.size());
        for (int k = 0; k < m; ++k) verts_.push_back(clipped[k]);
        const uint32_t start = static_cast<uint32_t>(idx_.size());
        for (int k = 1; k + 1 < m; ++k) {
            idx_.push_back(base);
            idx_.push_back(base + static_cast<uint32_t>(k));
            idx_.push_back(base + static_cast<uint32_t>(k + 1));
        }
        RegisterBatch(texture, isColor, start, static_cast<uint32_t>((m - 2) * 3));
        return true;
    }

    void FrameCollector::RegisterBatch(uint32_t texture, bool isColor,
        uint32_t indexStart, uint32_t indexCount) {
        if (!batches_.empty() && batches_.back().texture == texture
            && batches_.back().isColor == isColor) {
            batches_.back().indexCount += indexCount;
        }
        else {
            batches_.push_back({ texture, indexStart, indexCount, isColor });
        }
    }

    void FrameCollector::Clear() {
        verts_.clear(); idx_.clear(); batches_.clear();
        clipStack_.clear();
        hasClip_ = false;
        currentClip_ = { 0, 0, 0, 0 };
    }
    bool FrameCollector::Empty() const { return idx_.empty(); }

    void FrameCollector::BeginCapture(Fragment& f) {
        EndCapture(f);
    }

    void FrameCollector::EndCapture(Fragment& f) {
        std::swap(f.verts, verts_);
        std::swap(f.idx, idx_);
        std::swap(f.batches, batches_);

        verts_.clear();
        idx_.clear();
        batches_.clear();
        clipStack_.clear();
        hasClip_ = false;
        currentClip_ = { 0, 0, 0, 0 };
    }

    void FrameCollector::AppendFragment(const Fragment& f) {
        if (f.idx.empty()) return;

        const uint32_t vbase = static_cast<uint32_t>(verts_.size());
        const uint32_t ibase = static_cast<uint32_t>(idx_.size());

        verts_.insert(verts_.end(), f.verts.begin(), f.verts.end());

        const std::size_t n = f.idx.size();
        idx_.resize(ibase + n);
        uint32_t* dst = idx_.data() + ibase;
        const uint32_t* src = f.idx.data();
        for (std::size_t i = 0; i < n; ++i) dst[i] = src[i] + vbase;

        for (std::size_t k = 0; k < f.batches.size(); ++k) {
            const Batch& b = f.batches[k];
            if (k == 0 && !batches_.empty()
                && batches_.back().texture == b.texture
                && batches_.back().isColor == b.isColor) {
                batches_.back().indexCount += b.indexCount;
            } else {
                batches_.push_back({ b.texture, ibase + b.indexOffset, b.indexCount, b.isColor });
            }
        }
    }

}
