#pragma once
// Circle

#include "Shape.h"
#include <algorithm>

class CircleShape : public Shape {
public:
    // tolerance: allowed sagitta error (world units); smaller values give a rounder circle
    CircleShape(Vec2 center, float radius, bool filled = true, float tolerance = 0.5f)
        : center_(center), radius_(radius), filled_(filled), tolerance_(tolerance) {}

    Geometry build() const override {
        int segs = segmentsFor(radius_, tolerance_);
        return filled_ ? buildFilled(segs) : buildOutline(segs);
    }

    // Static utility; other shapes (Arc / regular polygon) can reuse it
    static int segmentsFor(float r, float tol) {
        if (tol >= r) return 3;
        float n = PI / std::acos(1.f - tol / r);
        return std::max(3, static_cast<int>(std::ceil(n)));
    }

private:
    Geometry buildOutline(int segs) const {
        Geometry g;
        g.mode = DrawMode::LineLoop;
        std::vector<Vec2> pts(segs);
        for (int i = 0; i < segs; ++i) {
            float a = 2.f * PI * i / segs;
            pts[i] = { center_.x + radius_ * std::cos(a),
                       center_.y + radius_ * std::sin(a) };
        }
        g.addPolyline(pts);
        return g;
    }

    Geometry buildFilled(int segs) const {
        Geometry g;
        g.mode = DrawMode::Triangles;
        uint32_t c = g.addVertex(center_);
        std::vector<uint32_t> ring(segs);
        for (int i = 0; i < segs; ++i) {
            float a = 2.f * PI * i / segs;
            ring[i] = g.addVertex({ center_.x + radius_ * std::cos(a),
                                    center_.y + radius_ * std::sin(a) });
        }
        for (int i = 0; i < segs; ++i)
            g.addTriangle(c, ring[i], ring[(i + 1) % segs]);
        return g;
    }

    Vec2  center_;
    float radius_;
    bool  filled_;
    float tolerance_;
};