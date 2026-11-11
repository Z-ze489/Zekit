#pragma once
// Arc

#include "Shape.h"
#include "ZeCircleShape.h"

class ArcShape : public Shape {
public:
    ArcShape(Vec2 center, float radius,
        float startAngle, float sweepAngle,
        float tolerance = 0.5f)
        : center_(center), radius_(radius),
        start_(startAngle), sweep_(sweepAngle), tolerance_(tolerance) {}

    Geometry build() const override {
        int full = CircleShape::segmentsFor(radius_, tolerance_);
        int segs = std::max(2, static_cast<int>(
            std::ceil(full * std::abs(sweep_) / (2.f * PI))));

        Geometry g;
        g.mode = DrawMode::LineStrip;
        std::vector<Vec2> pts(segs + 1);
        for (int i = 0; i <= segs; ++i) {
            float a = start_ + sweep_ * i / segs;
            pts[i] = { center_.x + radius_ * std::cos(a),
                       center_.y + radius_ * std::sin(a) };
        }
        g.addPolyline(pts);
        return g;
    }

private:
    Vec2  center_;
    float radius_, start_, sweep_, tolerance_;
};