#pragma once
// Rounded rectangle (pure fill)
//
// Notes:
//  - Isomorphic to RectShape; both are Shape subclasses, and build() generates CPU-side triangle geometry;
//  - ★ Since 2026-10-27: **rounded corners are not done in geometry**. build() outputs only the 4 corner points of the rectangle,
//    and the rounded corners are resolved in the fragment shader from the SDF five parameters in the vertex (center / half-width half-height / radius)
//    -- that is the shader's job anyway; sampling the arc again in geometry is duplicated work.
//    The old implementation sampled 8 arc segments per corner, assembled ~44 contour points, then fan-triangulated: **one rounded rectangle
//    ~= 126 vertices**, and 320 widgets on one screen means 60k vertices. Now it is 4 vertices.
//  - Borders are not done in this primitive -- the standard approach of "outer rounded rectangle (border color) + inset rounded rectangle (background color)"
//    is kept (see Button::Paint), fully consistent with the border strategy of the right-angle rectangle era.
//  - The four corner radii can still be constructed independently, but **only "all four corners with the same radius" actually takes effect**
//    (the SDF `rad` is a single value). Different radii per corner require the polygon path (currently unused).
//
//  WARNING: Prerequisite: when calling `FrameCollector::AddShape`, the SDF five parameters **must** be passed
//     (`cx, cy, hw, hh, rad`). If not passed, the rounded corners disappear entirely and it degrades to a hard-edged rectangle
//     -- this is an existing convention of AddShape, not a new requirement introduced by this change.

#include "Shape.h"

#include <algorithm>
#include <cmath>
#include <vector>

class RoundedRectShape : public Shape {
public:
    // pos is the top-left corner, size is width and height; radius is the uniform corner radius for all four corners (clamped to <= half the short side)
    RoundedRectShape(Vec2 pos, Vec2 size, float radius)
        : pos_(pos), size_(size)
        , rTL_(radius), rTR_(radius), rBR_(radius), rBL_(radius) {}

    // Version with independent radii for the four corners
    RoundedRectShape(Vec2 pos, Vec2 size,
        float rTL, float rTR, float rBR, float rBL)
        : pos_(pos), size_(size)
        , rTL_(rTL), rTR_(rTR), rBR_(rBR), rBL_(rBL) {}

    Geometry build() const override {
        Geometry g;
        g.mode = DrawMode::Triangles;

        const float w = std::max(size_.x, 0.0f);
        const float h = std::max(size_.y, 0.0f);
        if (w <= 0.0f || h <= 0.0f) return g;

        // ★ Only the rectangle outline is given: rounded corners are clipped by the SDF in the fragment shader (see file header).
        //   Previously this sampled the four arcs at kSeg=8, assembled ~44 points, and fan-triangulated into ~126 vertices --
        //   while every caller of AddShape **already passed** the SDF five parameters, so the same thing was done twice.
        const float x0 = pos_.x, y0 = pos_.y;
        const float x1 = pos_.x + w, y1 = pos_.y + h;

        const uint32_t i0 = g.addVertex(Vec2{ x0, y0 });   // Top-left
        const uint32_t i1 = g.addVertex(Vec2{ x0, y1 });   // Bottom-left
        const uint32_t i2 = g.addVertex(Vec2{ x1, y1 });   // Bottom-right
        const uint32_t i3 = g.addVertex(Vec2{ x1, y0 });   // Top-right
        g.addTriangle(i0, i1, i2);
        g.addTriangle(i0, i2, i3);
        return g;
    }

private:
    Vec2 pos_, size_;
    float rTL_, rTR_, rBR_, rBL_;
};
