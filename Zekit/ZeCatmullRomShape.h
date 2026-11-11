#pragma once
//  Catmull-Rom spline

#include "Shape.h"

class CatmullRomShape : public Shape {
public:
    CatmullRomShape(std::vector<Vec2> points, int perSegment = 16)
        : pts_(std::move(points)), perSeg_(perSegment) {}

    Geometry build() const override {
        Geometry g;
        g.mode = DrawMode::LineStrip;
        if (pts_.size() < 2) { g.addPolyline(pts_); return g; }

        // Duplicates the first and last points so the curve passes through the first and last point
        std::vector<Vec2> p;
        p.push_back(pts_.front());
        p.insert(p.end(), pts_.begin(), pts_.end());
        p.push_back(pts_.back());

        std::vector<Vec2> out;
        for (size_t i = 0; i + 3 < p.size(); ++i)
            for (int j = 0; j < perSeg_; ++j)
                out.push_back(eval(p[i], p[i + 1], p[i + 2], p[i + 3],
                    static_cast<float>(j) / perSeg_));
        out.push_back(pts_.back());
        g.addPolyline(out);
        return g;
    }

private:
    static Vec2 eval(Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, float t) {
        float t2 = t * t, t3 = t2 * t;
        auto f = [&](float a, float b, float c, float d) {
            return 0.5f * ((2 * b) + (-a + c) * t
                + (2 * a - 5 * b + 4 * c - d) * t2
                + (-a + 3 * b - 3 * c + d) * t3);
            };
        return { f(p0.x,p1.x,p2.x,p3.x), f(p0.y,p1.y,p2.y,p3.y) };
    }

    std::vector<Vec2> pts_;
    int perSeg_;
};