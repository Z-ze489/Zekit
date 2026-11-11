
#include "pch.h"

#include "ZeSoftRaster.h"
#include "ZeFrameCollector.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Rhi {

    namespace {

        inline float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

        inline int ToByte(float v) {
            const float s = v * 255.0f + 0.5f;
            if (!(s > 0.0f)) return 0;
            return s >= 255.0f ? 255 : static_cast<int>(s);
        }

        inline float RoundedRectSDF(float px, float py, float bx, float by, float r) {
            const float qx = std::fabs(px) - bx + r;
            const float qy = std::fabs(py) - by + r;
            const float mx = qx > 0.0f ? qx : 0.0f;
            const float my = qy > 0.0f ? qy : 0.0f;
            const float inner = std::min(std::max(qx, qy), 0.0f);
            float len = 0.0f;
            if (mx > 0.0f || my > 0.0f) len = std::sqrt(mx * mx + my * my);
            return inner + len - r;
        }

        inline int ClampTexel(int i, int n) {
            return i < 0 ? 0 : (i >= n ? n - 1 : i);
        }

        inline float SampleGray(const SoftTexture& t, float u, float v) {
            if (t.w <= 0 || t.h <= 0 || t.px.empty()) return 0.0f;
            const float fx = u * static_cast<float>(t.w) - 0.5f;
            const float fy = v * static_cast<float>(t.h) - 0.5f;
            const int   x0 = static_cast<int>(std::floor(fx));
            const int   y0 = static_cast<int>(std::floor(fy));
            const float tx = fx - static_cast<float>(x0);
            const float ty = fy - static_cast<float>(y0);
            const int   xa = ClampTexel(x0, t.w), xb = ClampTexel(x0 + 1, t.w);
            const int   ya = ClampTexel(y0, t.h), yb = ClampTexel(y0 + 1, t.h);

            const std::uint8_t* rowA = t.px.data() + static_cast<std::size_t>(ya) * t.w;
            const std::uint8_t* rowB = t.px.data() + static_cast<std::size_t>(yb) * t.w;
            const float v00 = rowA[xa], v10 = rowA[xb];
            const float v01 = rowB[xa], v11 = rowB[xb];
            const float top = v00 + (v10 - v00) * tx;
            const float bot = v01 + (v11 - v01) * tx;
            return (top + (bot - top) * ty) * (1.0f / 255.0f);
        }

        inline void SampleRGBA(const SoftTexture& t, float u, float v,
                               float& r, float& g, float& b, float& a) {
            r = g = b = 0.0f; a = 0.0f;
            if (t.w <= 0 || t.h <= 0 || t.px.size() < static_cast<std::size_t>(t.w) * t.h * 4)
                return;
            const float fx = u * static_cast<float>(t.w) - 0.5f;
            const float fy = v * static_cast<float>(t.h) - 0.5f;
            const int   x0 = static_cast<int>(std::floor(fx));
            const int   y0 = static_cast<int>(std::floor(fy));
            const float tx = fx - static_cast<float>(x0);
            const float ty = fy - static_cast<float>(y0);
            const int   xa = ClampTexel(x0, t.w), xb = ClampTexel(x0 + 1, t.w);
            const int   ya = ClampTexel(y0, t.h), yb = ClampTexel(y0 + 1, t.h);

            const auto fetch = [&](int x, int y, float& cr, float& cg, float& cbl, float& ca) {
                const std::uint8_t* p =
                    t.px.data() + (static_cast<std::size_t>(y) * t.w + x) * 4;
                cr = p[0] * (1.0f / 255.0f);
                cg = p[1] * (1.0f / 255.0f);
                cbl = p[2] * (1.0f / 255.0f);
                ca = p[3] * (1.0f / 255.0f);
            };

            float r00, g00, b00, a00, r10, g10, b10, a10;
            float r01, g01, b01, a01, r11, g11, b11, a11;
            fetch(xa, ya, r00, g00, b00, a00);
            fetch(xb, ya, r10, g10, b10, a10);
            fetch(xa, yb, r01, g01, b01, a01);
            fetch(xb, yb, r11, g11, b11, a11);

            const float t00 = r00 + (r10 - r00) * tx, t01 = r01 + (r11 - r01) * tx;
            r = t00 + (t01 - t00) * ty;
            const float s00 = g00 + (g10 - g00) * tx, s01 = g01 + (g11 - g01) * tx;
            g = s00 + (s01 - s00) * ty;
            const float u00 = b00 + (b10 - b00) * tx, u01 = b01 + (b11 - b01) * tx;
            b = u00 + (u01 - u00) * ty;
            const float w00 = a00 + (a10 - a00) * tx, w01 = a01 + (a11 - a01) * tx;
            a = w00 + (w01 - w00) * ty;
        }

    }

    std::uint32_t SoftTextureStore::CreateGray(int w, int h,
                                               const std::uint8_t* pixels, int pitch) {
        if (w <= 0 || h <= 0 || !pixels) return 0;
        if (pitch <= 0) pitch = w;

        SoftTexture t;
        t.w = w; t.h = h; t.gray = true;
        t.px.resize(static_cast<std::size_t>(w) * h);
        for (int y = 0; y < h; ++y)
            std::memcpy(t.px.data() + static_cast<std::size_t>(y) * w,
                        pixels + static_cast<std::size_t>(y) * pitch,
                        static_cast<std::size_t>(w));

        const std::uint32_t id = m_next++;
        m_tex.emplace(id, std::move(t));
        return id;
    }

    std::uint32_t SoftTextureStore::CreateRGBA(int w, int h,
                                               const std::uint8_t* pixels,
                                               int pitch, bool bgra) {
        if (w <= 0 || h <= 0 || !pixels) return 0;
        if (pitch <= 0) pitch = w * 4;

        SoftTexture t;
        t.w = w; t.h = h; t.gray = false;
        t.px.resize(static_cast<std::size_t>(w) * h * 4);
        for (int y = 0; y < h; ++y) {
            const std::uint8_t* src = pixels + static_cast<std::size_t>(y) * pitch;
            std::uint8_t* dst = t.px.data() + static_cast<std::size_t>(y) * w * 4;
            if (!bgra) {
                std::memcpy(dst, src, static_cast<std::size_t>(w) * 4);
            } else {
                for (int x = 0; x < w; ++x) {
                    dst[x * 4 + 0] = src[x * 4 + 2];
                    dst[x * 4 + 1] = src[x * 4 + 1];
                    dst[x * 4 + 2] = src[x * 4 + 0];
                    dst[x * 4 + 3] = src[x * 4 + 3];
                }
            }
        }

        const std::uint32_t id = m_next++;
        m_tex.emplace(id, std::move(t));
        return id;
    }

    void SoftTextureStore::Destroy(std::uint32_t tex) {
        if (!tex) return;
        m_tex.erase(tex);
    }

    const SoftTexture* SoftTextureStore::Get(std::uint32_t tex) const {
        if (!tex) return nullptr;
        const auto it = m_tex.find(tex);
        return it == m_tex.end() ? nullptr : &it->second;
    }

    void SoftTextureStore::Clear() {
        m_tex.clear();
    }

    std::size_t SoftTextureStore::Bytes() const {
        std::size_t n = 0;
        for (const auto& kv : m_tex) n += kv.second.px.size();
        return n;
    }

    bool SoftRasterizer::Resize(int width, int height) {
        if (width <= 0 || height <= 0) { m_w = m_h = 0; m_px.clear(); return false; }
        if (width == m_w && height == m_h && !m_px.empty()) return true;

        m_w = width;
        m_h = height;
        m_px.assign(static_cast<std::size_t>(m_w) * m_h * 4, 0);
        return true;
    }

    bool SoftRasterizer::Draw(const Kit::FrameCollector& fc,
                              int width, int height,
                              float r, float g, float b, float clearAlpha,
                              const SoftTextureStore& textures) {
        if (!Resize(width, height)) return false;

        const float ca = Clamp01(clearAlpha);
        const std::uint8_t cb = static_cast<std::uint8_t>(ToByte(b * ca));
        const std::uint8_t cg = static_cast<std::uint8_t>(ToByte(g * ca));
        const std::uint8_t cr = static_cast<std::uint8_t>(ToByte(r * ca));
        const std::uint8_t caB = static_cast<std::uint8_t>(ToByte(ca));
        for (std::size_t i = 0, n = static_cast<std::size_t>(m_w) * m_h; i < n; ++i) {
            std::uint8_t* p = m_px.data() + i * 4;
            p[0] = cb; p[1] = cg; p[2] = cr; p[3] = caB;
        }

        const auto& verts = fc.Vertices();
        const auto& idx = fc.Indices();
        if (verts.empty() || idx.empty()) return true;

        const std::size_t vcount = verts.size();
        const std::size_t icount = idx.size();

        for (const auto& batch : fc.Batches()) {
            const SoftTexture* tex = nullptr;
            Mode mode = Mode::Solid;
            if (batch.texture != 0) {
                tex = textures.Get(batch.texture);
                if (!tex) continue;
                mode = batch.isColor ? Mode::Color : Mode::Gray;
            }

            const std::size_t last = static_cast<std::size_t>(batch.indexOffset)
                                   + static_cast<std::size_t>(batch.indexCount);
            if (last > icount) break;

            for (std::size_t i = static_cast<std::size_t>(batch.indexOffset); i + 2 < last; i += 3) {
                const std::uint32_t i0 = idx[i], i1 = idx[i + 1], i2 = idx[i + 2];
                if (i0 >= vcount || i1 >= vcount || i2 >= vcount) continue;

                const auto toV = [](const Kit::UIVertex& v) {
                    Vertex o{};
                    o.x = v.x; o.y = v.y;
                    o.r = v.r; o.g = v.g; o.b = v.b; o.a = v.a;
                    o.u = v.u; o.v = v.v;
                    o.cx = v.cx; o.cy = v.cy;
                    o.hw = v.hw; o.hh = v.hh;
                    o.rad = v.rad;
                    return o;
                };
                RasterizeTriangle(toV(verts[i0]), toV(verts[i1]), toV(verts[i2]), mode, tex);
            }
        }
        return true;
    }

    void SoftRasterizer::RasterizeTriangle(const Vertex& a, const Vertex& b, const Vertex& c,
                                           Mode mode, const SoftTexture* tex) {
        const auto edge = [](float ax, float ay, float bx, float by, float px, float py) {
            return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
        };

        const float area = edge(a.x, a.y, b.x, b.y, c.x, c.y);
        if (!(std::fabs(area) > 1e-9f)) return;

        const float minx = std::min(a.x, std::min(b.x, c.x));
        const float maxx = std::max(a.x, std::max(b.x, c.x));
        const float miny = std::min(a.y, std::min(b.y, c.y));
        const float maxy = std::max(a.y, std::max(b.y, c.y));

        int px0 = static_cast<int>(std::floor(minx));
        int py0 = static_cast<int>(std::floor(miny));
        int px1 = static_cast<int>(std::ceil(maxx));
        int py1 = static_cast<int>(std::ceil(maxy));
        if (px0 < 0) px0 = 0;
        if (py0 < 0) py0 = 0;
        if (px1 > m_w - 1) px1 = m_w - 1;
        if (py1 > m_h - 1) py1 = m_h - 1;
        if (px0 > px1 || py0 > py1) return;

        const float sign = (area > 0.0f) ? 1.0f : -1.0f;
        const float invArea = 1.0f / (sign * area);

        const float e0dx =  (c.y - b.y) * sign, e0dy = -(c.x - b.x) * sign;
        const float e1dx =  (a.y - c.y) * sign, e1dy = -(a.x - c.x) * sign;
        const float e2dx =  (b.y - a.y) * sign, e2dy = -(b.x - a.x) * sign;

        const bool flatCol = a.r == b.r && b.r == c.r && a.g == b.g && b.g == c.g
                          && a.b == c.b && a.a == b.a && b.a == c.a;
        const bool flatUV  = a.u == b.u && b.u == c.u && a.v == b.v && b.v == c.v;
        const bool flatShp = a.cx == b.cx && b.cx == c.cx && a.cy == b.cy && b.cy == c.cy
                          && a.hw == b.hw && b.hw == c.hw && a.hh == b.hh && b.hh == c.hh
                          && a.rad == b.rad && b.rad == c.rad;

        const float sx0 = static_cast<float>(px0) + 0.5f;
        float sy = static_cast<float>(py0) + 0.5f;
        float row0 = sign * edge(b.x, b.y, c.x, c.y, sx0, sy);
        float row1 = sign * edge(c.x, c.y, a.x, a.y, sx0, sy);
        float row2 = sign * edge(a.x, a.y, b.x, b.y, sx0, sy);

        for (int y = py0; y <= py1; ++y, ++sy, row0 += e0dy, row1 += e1dy, row2 += e2dy) {
            float e0 = row0, e1 = row1, e2 = row2;

            for (int x = px0; x <= px1; ++x, e0 += e0dx, e1 += e1dx, e2 += e2dx) {
                if (e0 < 0.0f || e1 < 0.0f || e2 < 0.0f) continue;

                const float l0 = e0 * invArea;
                const float l1 = e1 * invArea;
                const float l2 = 1.0f - l0 - l1;

                float ir, ig, ib, ia;
                if (flatCol) {
                    ir = a.r; ig = a.g; ib = a.b; ia = a.a;
                } else {
                    ir = a.r * l0 + b.r * l1 + c.r * l2;
                    ig = a.g * l0 + b.g * l1 + c.g * l2;
                    ib = a.b * l0 + b.b * l1 + c.b * l2;
                    ia = a.a * l0 + b.a * l1 + c.a * l2;
                }
                if (ia <= 0.0f) continue;

                float iu, iv;
                if (flatUV) { iu = a.u; iv = a.v; }
                else {
                    iu = a.u * l0 + b.u * l1 + c.u * l2;
                    iv = a.v * l0 + b.v * l1 + c.v * l2;
                }

                float scx, scy, shw, shh, srad;
                if (flatShp) {
                    scx = a.cx; scy = a.cy; shw = a.hw; shh = a.hh; srad = a.rad;
                } else {
                    scx = a.cx * l0 + b.cx * l1 + c.cx * l2;
                    scy = a.cy * l0 + b.cy * l1 + c.cy * l2;
                    shw = a.hw * l0 + b.hw * l1 + c.hw * l2;
                    shh = a.hh * l0 + b.hh * l1 + c.hh * l2;
                    srad = a.rad * l0 + b.rad * l1 + c.rad * l2;
                }

                ShadeAndBlend(x, y, ir, ig, ib, ia, iu, iv,
                              scx, scy, shw, shh, srad, mode, tex);
            }
        }
    }

    void SoftRasterizer::ShadeAndBlend(int px, int py,
                                       float cr, float cg, float cb, float ca,
                                       float u, float v,
                                       float cx, float cy, float hw, float hh, float rad,
                                       Mode mode, const SoftTexture* tex) {
        float sr = cr, sg = cg, sb = cb, sa = ca;

        if (mode == Mode::Solid) {
            if (hw > 0.0f || hh > 0.0f) {
                const float lx = static_cast<float>(px) + 0.5f;
                const float ly = static_cast<float>(py) + 0.5f;

                const float rr = std::min(std::max(rad, 0.0f), std::min(hw, hh));
                const float d = RoundedRectSDF(lx - cx, ly - cy, hw, hh, rr);

                if (d >= 0.5f) return;
                if (d > -0.5f) sa *= 0.5f - d;
            }
        } else if (mode == Mode::Gray) {
            if (!tex) return;
            const float cov = SampleGray(*tex, u, v);
            if (cov <= 0.0f) return;
            sa *= cov;
        } else {
            if (!tex) return;
            float tr, tg, tb, ta;
            SampleRGBA(*tex, u, v, tr, tg, tb, ta);
            sr = cr * tr;
            sg = cg * tg;
            sb = cb * tb;
            sa = ca * ta;
        }

        if (sa <= 0.0f) return;

        sr *= sa;
        sg *= sa;
        sb *= sa;

        std::uint8_t* d = m_px.data()
                        + (static_cast<std::size_t>(py) * m_w + px) * 4;
        const int ia = ToByte(sa);

        if (ia >= 255) {
            d[0] = static_cast<std::uint8_t>(ToByte(sb));
            d[1] = static_cast<std::uint8_t>(ToByte(sg));
            d[2] = static_cast<std::uint8_t>(ToByte(sr));
            d[3] = 255;
            return;
        }

        const int inv = 255 - ia;

        const int dr = ToByte(sr) + (d[2] * inv + 127) / 255;
        const int dg = ToByte(sg) + (d[1] * inv + 127) / 255;
        const int db = ToByte(sb) + (d[0] * inv + 127) / 255;
        const int da = ia + (d[3] * inv + 127) / 255;

        d[0] = static_cast<std::uint8_t>(db > 255 ? 255 : db);
        d[1] = static_cast<std::uint8_t>(dg > 255 ? 255 : dg);
        d[2] = static_cast<std::uint8_t>(dr > 255 ? 255 : dr);
        d[3] = static_cast<std::uint8_t>(da > 255 ? 255 : da);
    }

}
