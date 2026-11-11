#pragma once
// Line

#include "Shape.h"

class LineShape : public Shape {
public:
    // Uses GL_LINE_STRIP when thickness <= 1, expands to a quad when > 1
    LineShape(Vec2 a, Vec2 b, float thickness = 1.f)
        : a_(a), b_(b), thickness_(thickness) {}

    Geometry build() const override {
        Geometry g;
        if (thickness_ <= 1.f) {
            g.mode = DrawMode::LineStrip;
            g.addPolyline({ a_, b_ });
        }
        else {
            g.mode = DrawMode::Triangles;
            // glm::vec2 has no .normalized() / .perpendicular() members,
            // use the free function glm::normalize for normalization and perpendicular from Vec2.h for the perpendicular direction
            Vec2 dir = glm::normalize(b_ - a_);
            Vec2 n = perpendicular(dir) * (thickness_ * 0.5f);
            uint32_t i0 = g.addVertex(a_ + n);
            uint32_t i1 = g.addVertex(a_ - n);
            uint32_t i2 = g.addVertex(b_ + n);
            uint32_t i3 = g.addVertex(b_ - n);
            g.addTriangle(i0, i1, i2);
            g.addTriangle(i2, i1, i3);
        }
        return g;
    }

private:
    Vec2  a_, b_;
    float thickness_;
};