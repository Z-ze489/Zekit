#pragma once
#include "Vec2.h"
#include <vector>
#include <cstdint>

enum class DrawMode {
    Lines,          // GL_LINES
    LineStrip,      // GL_LINE_STRIP
    LineLoop,       // GL_LINE_LOOP
    Triangles,      // GL_TRIANGLES
    TriangleStrip,  // GL_TRIANGLE_STRIP
};

class Geometry {
public:
    std::vector<Vec2>     vertices;
    std::vector<uint32_t> indices;
    DrawMode              mode = DrawMode::Triangles;

    // ---- Vertex / triangle writing ----
    uint32_t addVertex(Vec2 v) {
        vertices.push_back(v);
        return static_cast<uint32_t>(vertices.size() - 1);
    }

    void addTriangle(uint32_t a, uint32_t b, uint32_t c) {
        indices.push_back(a); indices.push_back(b); indices.push_back(c);
    }

    void addTriangle(Vec2 a, Vec2 b, Vec2 c) {
        uint32_t i = static_cast<uint32_t>(vertices.size());
        vertices.push_back(a); vertices.push_back(b); vertices.push_back(c);
        indices.push_back(i); indices.push_back(i + 1); indices.push_back(i + 2);
    }

    // Connects in order, used for line strips / line loops
    void addPolyline(const std::vector<Vec2>& pts) {
        uint32_t base = static_cast<uint32_t>(vertices.size());
        for (auto& p : pts) vertices.push_back(p);
        for (size_t i = 0; i < pts.size(); ++i)
            indices.push_back(base + static_cast<uint32_t>(i));
    }

    void clear() { vertices.clear(); indices.clear(); }
    bool empty() const { return indices.empty(); }
};