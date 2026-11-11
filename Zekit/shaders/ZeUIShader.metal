// =============================================================================
// ZeUIShader.metal -- UI shader (MSL) for the Zekit Metal backend
// =============================================================================
// Corresponds line by line to the GLSL (kVS / kFS) in `ZeD3D12UI.hlsl` and `ZeUIRenderer.cpp`.
// The UI drawing logic of the three backends must match, otherwise the same interface will have different rounded corners/anti-aliasing on different backends.
//
// ## Compilation
//
//    xcrun -sdk macosx metal -c ZeUIShader.metal -o ZeUIShader.air
//    xcrun -sdk macosx metallib ZeUIShader.air -o default.metallib
// The product `default.metallib` ships with the app bundle and is loaded at runtime with
// [device newDefaultLibrary] or [device newLibraryWithFile:]
// **Same approach as Vulkan's SPIR-V**: compile offline, load only the binary at runtime.
// (Compiling online is also possible: `newLibraryWithSource:` -- but startup is slow, and App Store review
// is uncertain about runtime shader compilation; offline compilation is the proper way.)
//
// ## Vertex layout: strictly consistent with Kit::UIVertex
//
// struct UIVertex {          // 13 tightly packed floats = 52 bytes
//        float x, y;            // offset 0   [[attribute(0)]]
//        float r, g, b, a;      // offset 8   [[attribute(1)]]
//        float u, v;            // offset 24  [[attribute(2)]]
//        float cx, cy, hw, hh;  // offset 32  [[attribute(3)]]
//        float rad;             // offset 48  [[attribute(4)]]
//    };
//
// * MSL's `[[attribute(n)]]` corresponds one-to-one with
// `attributes[n]` in `MTLVertexDescriptor` -- and **the offset must exactly match
// what is filled in the MTLVertexDescriptor** (see the section in ZeMetalUIRenderer that builds the descriptor).
// A wrong fill shows up as "all vertex data misaligned" (like noise), with no error reported.
//
// ## * Three invariants of SDF anti-aliasing (identical to the HLSL / GLSL versions, read before changing)
//
// 1. Transition band width = **exactly 1 pixel**, coverage taken via clamp(0.5 - d/g, 0, 1);
// 2. Gradient g uses "how far the distance field travels per pixel" = length(dfdx(d), dfdy(d)),
// **not** fwidth() (in HLSL fwidth = abs(ddx) + abs(ddy), 1.41 times larger at 45 degrees);
// * In MSL the corresponding functions are `dfdx()` / `dfdy()` -- names differ from HLSL's ddx/ddy,
// functionality is the same. This is the easiest place to get wrong when translating.
// 3. p uses the **logical coordinates** passed from VS (top-left origin), not fragment coordinates --
// using fragment coordinates misplaces the SDF center to physical pixel coordinates, shifting the rounded corners as a whole.
//
// ## * One **notable** difference from HLSL: coordinate system
//
// HLSL (D3D12) / GLSL NDC: **Y up**.
// Metal NDC: **Y up** (same as D3D, opposite to Vulkan).
// * Good news: Metal and D3D12 are **consistent**, so the projection matrix can be copied from the D3D12 one,
// no need to swap bottom/top as with Vulkan.
// (Vulkan: NDC Y down, so the projection matrix in ZeVkUIRenderer has a Y flip.)
// =============================================================================

#include <metal_stdlib>
using namespace metal;

// =============================================================================
// Constant buffer -- corresponds byte-for-byte to the CPU-side Ubo struct
// =============================================================================
// * Alignment rule difference (Metal differs from HLSL!):
// HLSL cbuffer packs with 16-byte alignment;
// MSL `constant` structs pack with **natural member alignment**.
// float4x4 = 64 bytes (align 16), int = 4 bytes (align 4).
// Written as below, the CPU side can match with `simd_float4x4` + two `int32_t`,
// totaling 64 + 4 + 4 = 72 bytes (8 bytes of tail padding to 80, harmless).
// WARNING: do not copy the `int2 _pad` from the HLSL version -- MSL does not need it, and adding it causes misalignment.
struct UIConstants {
    float4x4 uProj;     // /< Orthographic projection (Metal NDC: Y up, same as D3D12)
    int      uIsTex;    // /< 0 = solid-color primitive, 1 = sample texture
    int      uIsColor;  // /< 1 = color glyph/image (multiply directly), 0 = grayscale glyph (take .r as coverage)
};

// =============================================================================
// Vertex -> fragment interpolated data
// =============================================================================
struct VOut {
    float4 clipPos [[position]];   // /< Clip space (corresponds to HLSL's SV_Position)
    float4 color;                  // /< Color
    float2 uv;                     // /< Texture coordinates
    float4 shape;                  ///< cx, cy, hw, hh
    float  rad;                    // /< Rounded corner radius
    float2 pos;                    // /< Logical coordinates (top-left origin), for SDF computation
};

// =============================================================================
// Vertex shader -- corresponds to HLSL's VSMain
// =============================================================================
vertex VOut uiVertex(
    uint vid                        [[vertex_id]],
    const device float* vtx         [[buffer(0)]],   // /< Interleaved vertex data (13 floats / vertex)
    constant UIConstants& u         [[buffer(1)]])
{
    // * **Difference** from Vulkan/D3D12: those two backends use a vertex descriptor to let the hardware
    // split the interleaved data; MSL can do the same here (see MTLVertexDescriptor in the .mm),
    // but **using a buffer + manual offsets** is more direct and does not depend on correct descriptor configuration.
    // Both work -- this implementation chooses manual offsets, because the vertex layout is already fixed in UIVertex,
    // an extra descriptor layer is just one more place to get wrong.
    //
    // Interleaved layout: 13 floats per vertex (52 bytes)
    const uint base = vid * 13u;
    const float2 pos   = float2(vtx[base + 0], vtx[base + 1]);
    const float4 color = float4(vtx[base + 2], vtx[base + 3], vtx[base + 4], vtx[base + 5]);
    const float2 uv    = float2(vtx[base + 6], vtx[base + 7]);
    const float4 shape = float4(vtx[base + 8], vtx[base + 9], vtx[base + 10], vtx[base + 11]);
    const float  rad   = vtx[base + 12];

    VOut o;
    o.color = color;
    o.uv    = uv;
    o.shape = shape;
    o.rad   = rad;
    o.pos   = pos;    // Pass logical coordinates through unchanged (for SDF)

    // * Same form as GLSL/HLSL: clipPos = uProj * vec4(pos, 0, 1)
    // Metal's NDC is Y up (same as D3D12), so the projection matrix can be copied from the D3D12 one --
    // this differs from Vulkan (the Vulkan matrix has a Y flip).
    o.clipPos = u.uProj * float4(pos, 0.0f, 1.0f);
    return o;
}

// =============================================================================
// Signed distance field (SDF) of a rounded rectangle -- corresponds to HLSL's roundedRectSDF
// =============================================================================
// Returns the signed distance from the pixel point to the rounded rectangle edge (negative = inside, positive = outside).
// p = coordinates relative to the rectangle center, b = half width/half height, r = corner radius.
static inline float roundedRectSDF(float2 p, float2 b, float r)
{
    float2 q = abs(p) - b + float2(r, r);
    return min(max(q.x, q.y), 0.0f) + length(max(q, float2(0.0f))) - r;
}

// =============================================================================
// Fragment shader -- corresponds to HLSL's PSMain
// =============================================================================
fragment float4 uiFragment(
    VOut in                          [[stage_in]],
    constant UIConstants& u          [[buffer(1)]],
    texture2d<float> uTex            [[texture(0)]],
    sampler uSampler                 [[sampler(0)]])
{
    if (u.uIsTex == 0) {
        // ---- Solid-color primitive ----
        // If the primitive carries a shape description (half width/height nonzero), apply an **exactly 1 pixel** edge coverage transition via SDF.
        if (in.shape.z > 0.0f || in.shape.w > 0.0f) {
            const float2 c = in.shape.xy;                        // Rectangle center (logical coordinates, top-left origin)
            const float2 b = in.shape.zw;                        // Half width/half height
            const float  r = clamp(in.rad, 0.0f, min(b.x, b.y)); // Corner radius clamped to no more than half the short side

            // Offset of the current fragment relative to the rectangle center -- uses the logical coordinates passed from VS, same coordinate system as the center
            const float2 p = in.pos - c;

            const float d = roundedRectSDF(p, b, r);

            // * Gradient uses the 2-norm (isotropic): see the explanation in ZeD3D12UI.hlsl.
            // In MSL these are `dfdx` / `dfdy` (not HLSL's ddx/ddy) -- an easy translation mistake.
            const float2 grad = float2(dfdx(d), dfdy(d));
            const float  g = max(length(grad), 1e-6f);

            const float coverage = clamp(0.5f - d / g, 0.0f, 1.0f);
            return float4(in.color.rgb, in.color.a * coverage);
        } else {
            return in.color;   // No shape information: degrade to hard edges
        }
    } else {
        // * Difference from HLSL: HLSL's `uTex.Sample(uSampler, uv)` in MSL is
        // `uTex.sample(uSampler, uv)` -- lowercase method name.
        const float4 t = uTex.sample(uSampler, in.uv);
        if (u.uIsColor == 1) {
            return in.color * t;                              // Color glyph (emoji) / image: take whole
        } else {
            return float4(in.color.rgb, in.color.a * t.r);    // Grayscale glyph: only .r is coverage
        }
    }
}
