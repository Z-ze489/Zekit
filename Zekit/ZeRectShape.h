#pragma once
// Axis-aligned rectangle: filled (two triangles) or outlined (line loop).

#include "Shape.h"

class RectShape : public Shape {
public:
    // pos is the top-left corner, size is width and height.
    RectShape(Vec2 pos, Vec2 size, bool filled = true)
        : pos_(pos), size_(size), filled_(filled) {}

    Geometry build() const override {
        Geometry g;
        Vec2 lo = pos_;
        Vec2 hi{ pos_.x + size_.x, pos_.y + size_.y };

        if (filled_) {
            g.mode = DrawMode::Triangles;
            uint32_t i0 = g.addVertex({ lo.x, lo.y });
            uint32_t i1 = g.addVertex({ hi.x, lo.y });
            uint32_t i2 = g.addVertex({ hi.x, hi.y });
            uint32_t i3 = g.addVertex({ lo.x, hi.y });
            g.addTriangle(i0, i1, i2);
            g.addTriangle(i0, i2, i3);
        }
        else {
            g.mode = DrawMode::LineLoop;
            g.addPolyline({ { lo.x,lo.y },{ hi.x,lo.y },{ hi.x,hi.y },{ lo.x,hi.y } });
        }
        return g;
    }

private:
    Vec2 pos_, size_;
    bool filled_;
};