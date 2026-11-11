#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>

// Uses glm::vec2 directly as the 2D point type
using Vec2 = glm::vec2;

// glm has no core function for "rotate 90 degrees counterclockwise"
// (perpendicular is in gtx/perpendicular.hpp and requires GLM_ENABLE_EXPERIMENTAL, not worth enabling an extension for it),
inline Vec2 perpendicular(Vec2 v) { return Vec2(-v.y, v.x); }

// 2D cross product (the result is a scalar).
// Note: since GLM 1.0.0, cross() keeps only the vec<3> overload, and the 2D cross product has been removed,
// so it is expanded by hand here as x1*y2 - y1*x2
inline float cross2(Vec2 u, Vec2 v) { return u.x * v.y - u.y * v.x; }
inline float cross2(Vec2 o, Vec2 a, Vec2 b) { return cross2(a - o, b - o); }

// Commonly used in circle sampling; glm::pi is a template
// (in GLM 1.0.0 it is forwarded by gtc/constants.hpp to ext/scalar_constants.hpp)
constexpr float PI = glm::pi<float>();
