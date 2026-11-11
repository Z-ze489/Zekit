#pragma once
// Bezier curve

#include "Shape.h"

class BezierShape : public Shape {
public:
    BezierShape(std::vector<Vec2> controlPoints, int segments = 32)
        : cps_(std::move(controlPoints)), segments_(segments) {}

    Geometry build() const override {
        Geometry g;
        g.mode = DrawMode::LineStrip;
        if (cps_.size() < 2) return g;

        std::vector<Vec2> pts(segments_ + 1);
        for (int i = 0; i <= segments_; ++i)
            pts[i] = evaluate(static_cast<float>(i) / segments_);
        g.addPolyline(pts);
        return g;
    }

private:
    // de Casteljau, generic for any order
    Vec2 evaluate(float t) const {
        std::vector<Vec2> tmp = cps_;
        for (size_t k = tmp.size() - 1; k > 0; --k)
            for (size_t i = 0; i < k; ++i)
                // GLM 1.0.0 removed glm::lerp, use the native GLSL name glm::mix
                tmp[i] = glm::mix(tmp[i], tmp[i + 1], t);
        return tmp[0];
    }

    std::vector<Vec2> cps_;
    int segments_;
};