#pragma once
#include "Geometry.h"
#include <memory>

class Shape {
public:
    virtual ~Shape() = default;

    // The only interface a subclass must implement: generates CPU-side geometry data
    virtual Geometry build() const = 0;
};

using ShapePtr = std::unique_ptr<Shape>;