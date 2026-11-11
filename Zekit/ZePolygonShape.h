#pragma once
// Polygon

#include "Shape.h"
#include <algorithm>
#include <numeric>

class PolygonShape : public Shape {
public:
    PolygonShape(std::vector<Vec2> pts, bool filled = true)
        : points_(std::move(pts)), filled_(filled) {}

    Geometry build() const override {
        return filled_ ? buildFilled() : buildOutline();
    }

private:
    Geometry buildOutline() const {
        Geometry g;
        g.mode = DrawMode::LineLoop;
        g.addPolyline(points_);
        return g;
    }

    // Ear clipping: supports simple concave polygons (non-self-intersecting)
    Geometry buildFilled() const {
        Geometry g;
        g.mode = DrawMode::Triangles;
        if (points_.size() < 3) return g;

        // Vertices go directly into g
        std::vector<uint32_t> idx;
        idx.reserve(points_.size());
        for (auto& p : points_) idx.push_back(g.addVertex(p));

        // Ensures counterclockwise
        float area = 0.f;
        for (size_t i = 0; i < points_.size(); ++i) {
            const Vec2& a = points_[i];
            const Vec2& b = points_[(i + 1) % points_.size()];
            area += a.x * b.y - b.x * a.y;
        }
        if (area < 0) std::reverse(idx.begin(), idx.end());

        int guard = 0;
        while (idx.size() > 3 && guard++ < 10000) {
            bool cut = false;
            for (size_t i = 0; i < idx.size(); ++i) {
                uint32_t i0 = idx[(i + idx.size() - 1) % idx.size()];
                uint32_t i1 = idx[i];
                uint32_t i2 = idx[(i + 1) % idx.size()];
                Vec2 a = g.vertices[i0], b = g.vertices[i1], c = g.vertices[i2];

                if (cross2(a, b, c) <= 0) continue;   // Reflex vertex

                bool contains = false;
                for (uint32_t k : idx) {
                    if (k == i0 || k == i1 || k == i2) continue;
                    if (pointInTriangle(g.vertices[k], a, b, c)) {
                        contains = true; break;
                    }
                }
                if (contains) continue;

                g.addTriangle(i0, i1, i2);
                idx.erase(idx.begin() + i);
                cut = true;
                break;
            }
            if (!cut) break;   // Non-simple polygon, give up
        }
        if (idx.size() == 3)
            g.addTriangle(idx[0], idx[1], idx[2]);
        return g;
    }

    static bool pointInTriangle(Vec2 p, Vec2 a, Vec2 b, Vec2 c) {
        float d1 = cross2(a, b, p), d2 = cross2(b, c, p), d3 = cross2(c, a, p);
        bool hasNeg = (d1 < 0) || (d2 < 0) || (d3 < 0);
        bool hasPos = (d1 > 0) || (d2 > 0) || (d3 > 0);
        return !(hasNeg && hasPos);
    }

    std::vector<Vec2> points_;
    bool filled_;
};