// ZeCanvasShaders.h: reference shaders (GLSL/HLSL) for external renderers.
//
// Kit::ICanvasRenderer (see ZeCanvasRenderer.h) receives vertices whose
// interpretation follows library conventions - not OpenGL-tutorial conventions:
//  - Vertices carry rounded-rect SDF five params (antialiased corners / circles
//    / capsule edges without MSAA).
//  - Edge transition is exactly 1 pixel: wider blurs the whole screen, narrower
//    jaggies everywhere (measured in-library; see the long comment below).
//  - Colors output premultiplied: layered / composited swapchains require it;
//    multiplying by 1 is a no-op for normal windows.
//  - Gray textures use .r as coverage: glyphs are single-channel bitmaps.
//  - Sampling is linear + ClampToEdge: glyphs are rasterized on whole pixels;
//    bilinear is their established look.
//
// Deriving these from scratch yields "looks close but fuzzy corners / blurry
// text". So the library's own sources are published verbatim: kVertexGlsl +
// kFragmentGlsl are exactly what the GL backend uses (ZeUIRenderer.cpp includes
// them directly - not a copy).
//
// Usage (OpenGL 3.3+ / 4.6): prepend kVersionGl330 (or kVersionGl460) to
// kVertexGlsl / kFragmentGlsl and compile.
//
// Vertex attribute layout (MUST match Kit::CanvasVertex field by field,
// stride = 13 floats):
//   location 0: aPos - position (2)
//   location 1: aColor - color, NON-premultiplied (4)
//   location 2: aUV - texture coordinates (2)
//   location 3: aShape - rounded-rect SDF: center (cx,cy) + half extents (hw,hh) (4)
//   location 4: aRad - corner radius (1)
//
// Uniforms: uProj (orthographic), uTex (always texture unit 0), uIsTex
// (0 = solid primitive, 1 = sample texture), uIsColor (1 = color texture).
//
// Render state (all three required):
//   glEnable(GL_BLEND);
//   glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);   // Premultiplied blending, NOT SRC_ALPHA.
//   glDisable(GL_SCISSOR_TEST);                    // Clipping already done CPU-side;
//                                                  // scissor would cut the rounded-corner outset.
//
// Orthographic projection: top-left origin, y down (widget coordinates),
// column-major, fed directly to glUniformMatrix4fv(..., GL_FALSE, m).
//
// Other APIs (Vulkan / D3D12 / Metal / software raster): this file ships THREE
// ready-made sources - OpenGL (GLSL), Vulkan (GLSL 450 -> SPIR-V), D3D12
// (HLSL). Just compile them (see the notes above each). Custom or exotic
// renderers: translate the GLSL semantics exactly - the roundedRectSDF formula,
// the 0.5 - d/g coverage computation, and the final rgb *= a. None may change.

#pragma once

namespace Kit {
namespace CanvasShaders {

    // =================================================================
    //  Version lines (prepend to the shader body yourself)
    // =================================================================

    // Desktop GL 3.3 core (recommended: enough for these shaders, old-driver friendly).
    inline constexpr const char* kVersionGl330 = "#version 330 core\n";
    // Desktop GL 4.6 core (what the in-library GL backend itself uses).
    inline constexpr const char* kVersionGl460 = "#version 460 core\n";
    // GLES 3.0 (Android / HarmonyOS).
    //  ES MUST declare float precision, or the fragment shader fails to compile
    //  - and the symptom ("clear color present, no geometry at all") does not
    //  point at the precision line.
    inline constexpr const char* kVersionEs300 = "#version 300 es\nprecision mediump float;\n";

    // =================================================================
    //  Vertex shader body (without the #version line)
    // =================================================================

    // Vertex shader: unpacks CanvasVertex's five attributes, applies the
    //  orthographic projection. Coordinates pass through as vPos - the fragment
    //  shader needs it for the SDF; gl_FragCoord cannot be used (bottom-left
    //  origin plus physical pixels, both would misplace things).
    inline constexpr const char* kVertexGlsl = R"(
        layout(location=0) in vec2 aPos;
        layout(location=1) in vec4 aColor;
        layout(location=2) in vec2 aUV;
        layout(location=3) in vec4 aShape;   // Rounded rectangle SDF: center (cx,cy) + half width/height (hw,hh)
        layout(location=4) in float aRad;    // Corner radius
        uniform mat4 uProj;
        out vec4 vColor;
        out vec2 vUV;
        out vec4 vShape;
        out float vRad;
        out vec2 vPos;      // Logical coordinates (top-left origin), used by the SDF to compute the local position
        void main(){
            vColor = aColor;
            vUV    = aUV;
            vShape = aShape;
            vRad   = aRad;
            vPos   = aPos;
            gl_Position = uProj * vec4(aPos, 0.0, 1.0);
        }
        )";

    // =================================================================
    //  Fragment shader body (without the #version / precision lines)
    // =================================================================

    // Fragment shader: solid primitives get SDF antialiasing, textured
    //  primitives get coverage / color blending.
    //  Do NOT change the three numbers: 0.5 - d/g (1-pixel transition), the
    //  half-extent clamp of vShape.zw, and the final rgb *= a (premultiply).
    //  The rationale is written in the shader's own comments below.
    inline constexpr const char* kFragmentGlsl = R"(
        in vec4 vColor;
        in vec2 vUV;
        in vec4 vShape;   // (cx, cy, hw, hh)
        in float vRad;
        in vec2 vPos;     // Logical coordinates (top-left origin); after interpolation these are the local coordinates of the current fragment
        out vec4 FragColor;
        uniform sampler2D uTex;
        uniform int uIsTex;
        uniform int uIsColor;

        // Rounded rectangle signed distance field (SDF):
        // Returns the signed distance from the pixel to the rounded rectangle edge (negative = inside, positive = outside).
        // p = coordinates relative to the rectangle center, b = half width/height, r = corner radius.
        float roundedRectSDF(vec2 p, vec2 b, float r) {
            vec2 q = abs(p) - b + vec2(r);
            return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
        }

        void main(){
            if (uIsTex == 0) {
                // ---- Solid color primitives ----
                // If the primitive carries a shape description (half width/height nonzero), use the SDF for an edge coverage transition of exactly 1 pixel.
                //
                // WARNING: this was a trap before, do not change it back: the transition band was once written as a "±1.5px smoothstep",
                // and the comment even claimed "3px wide is softer, no fuzz" -- the opposite is true --
                // hardware MSAA edge transitions measure about 1.1px, while the SDF gave 2.1~3.4px,
                // so circles/rounded rectangles were twice as blurry as straight edges in the same UI, circles especially bad,
                // which looks like "fuzz/blur".
                // The correct approach (also consistent with NanoVG / ImGui): normalize the SDF distance into "pixel count",
                // then use clamp(0.5 - px, 0, 1) to get the pixel coverage -- that is a
                // linear transition centered on the shape edge, 0.5px on each side, exactly 1 pixel in total.
                if (vShape.z > 0.0 || vShape.w > 0.0) {
                    vec2 c = vShape.xy;                       // Rectangle center (logical coordinates, top-left origin)
                    vec2 b = vShape.zw;                       // Half width/height
                    float r = clamp(vRad, 0.0, min(b.x, b.y)); // Corner radius clamped to no more than half the short side
                    // Offset of the current fragment relative to the rectangle center -- uses the logical coordinates passed from the VS, same coordinate system as the center,
                    // naturally avoiding both mismatches of gl_FragCoord: "bottom-left origin + physical pixels".
                    vec2 p = vPos - c;
                    float d = roundedRectSDF(p, b, r);
                    // Gradient uses the 2-norm (isotropic): fwidth() is |dFdx|+|dFdy|,
                    // which on a 45-degree edge is about 1.41 times the true "change per pixel" --
                    // the transition band computed that way widens and narrows with angle, so a circle is sharp on one side and blurry on the other.
                    // length(vec2(dFdx, dFdy)) is the true "distance the field travels per pixel", consistent in all directions.
                    float g = max(length(vec2(dFdx(d), dFdy(d))), 1e-6);
                    float coverage = clamp(0.5 - d / g, 0.0, 1.0);
                    FragColor = vec4(vColor.rgb, vColor.a * coverage);
                } else {
                    FragColor = vColor;   // No shape info: degrades to a hard edge
                }
            } else {
                vec4 t = texture(uTex, vUV);
                if (uIsColor == 1) {
                    FragColor = vColor * t;             // Color glyphs (emoji) / images: take as-is
                } else {
                    FragColor = vec4(vColor.rgb, vColor.a * t.r);   // Grayscale glyphs: only .r is coverage
                }
            }

            // * Premultiplied alpha: this library's framebuffer is uniformly stored as premultiplied --
            // Windows layered windows (WS_EX_LAYERED) and the D3D12 composition swap chain
            // (DXGI_ALPHA_MODE_PREMULTIPLIED) both require RGB to be already multiplied by alpha, otherwise
            // semi-transparent edges (rounded corners / shadows / glyph antialiasing) produce a halo.
            // For ordinary opaque windows the framebuffer alpha is always 1, and multiplying by 1 is a no-op,
            // so this line has no side effects for ordinary windows.
            FragColor.rgb *= FragColor.a;
        }
        )";


    // =================================================================
    //  Vulkan (GLSL 450 -> SPIR-V)
    // =================================================================
    //  The only difference from the OpenGL pair is where parameters come from:
    //  Vulkan has no default uniforms, so the projection matrix and the two
    //  switches travel in a push constant, and the sampler in descriptor
    //  set 0 / binding 0. Math matches the GL version character for character.
    //
    //  Compiling (glslangValidator ships with the Vulkan SDK):
    //      glslangValidator -V ui.vert -o ui.vert.spv
    //      glslangValidator -V ui.frag -o ui.frag.spv
    //
    //  Pipeline-side things that must align:
    //  - VkPushConstantRange { stageFlags = VERTEX|FRAGMENT, offset = 0, size = 72 }
    //    (mat4 takes 64 bytes with std430 16-byte alignment; ints at 64 / 68).
    //  - Descriptor set layout: one VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER at
    //    binding 0. Solid batches bound to texture 0 still need a 1x1 white
    //    image: Vulkan does not allow a dangling sampler like GL.
    //  - Vertex input: binding 0 = the whole vertex buffer (stride 52), five
    //    attributes R32G32_SFLOAT / R32G32B32A32_SFLOAT / R32G32_SFLOAT /
    //    R32G32B32A32_SFLOAT / R32_SFLOAT (offsets per CanvasVertex).
    //  - Blend: srcColorBlendFactor = ONE, dstColorBlendFactor = ONE_MINUS_SRC_ALPHA.

    // Vulkan version line.
    inline constexpr const char* kVersionVk450 = "#version 450\n";

    // Vulkan vertex shader (push constant block named pc; layout as above).
    inline constexpr const char* kVkVertexGlsl = R"(
        layout(push_constant) uniform Push {
            mat4 uProj;    // offset 0
            int  uIsTex;   // offset 64
            int  uIsColor; // offset 68
        } pc;

        layout(location=0) in vec2 aPos;
        layout(location=1) in vec4 aColor;
        layout(location=2) in vec2 aUV;
        layout(location=3) in vec4 aShape;
        layout(location=4) in float aRad;

        layout(location=0) out vec4 vColor;
        layout(location=1) out vec2 vUV;
        layout(location=2) out vec4 vShape;
        layout(location=3) out float vRad;
        layout(location=4) out vec2 vPos;

        void main(){
            vColor = aColor;
            vUV    = aUV;
            vShape = aShape;
            vRad   = aRad;
            vPos   = aPos;
            gl_Position = pc.uProj * vec4(aPos, 0.0, 1.0);
        }
        )";

    // Vulkan fragment shader (sampler at set 0 / binding 0).
    inline constexpr const char* kVkFragmentGlsl = R"(
        layout(push_constant) uniform Push {
            mat4 uProj;
            int  uIsTex;
            int  uIsColor;
        } pc;

        layout(set=0, binding=0) uniform sampler2D uTex;

        layout(location=0) in vec4 vColor;
        layout(location=1) in vec2 vUV;
        layout(location=2) in vec4 vShape;
        layout(location=3) in float vRad;
        layout(location=4) in vec2 vPos;

        layout(location=0) out vec4 FragColor;

        float roundedRectSDF(vec2 p, vec2 b, float r) {
            vec2 q = abs(p) - b + vec2(r);
            return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
        }

        void main(){
            if (pc.uIsTex == 0) {
                if (vShape.z > 0.0 || vShape.w > 0.0) {
                    vec2 b = vShape.zw;
                    float r = clamp(vRad, 0.0, min(b.x, b.y));
                    float d = roundedRectSDF(vPos - vShape.xy, b, r);
                    float g = max(length(vec2(dFdx(d), dFdy(d))), 1e-6);
                    FragColor = vec4(vColor.rgb, vColor.a * clamp(0.5 - d / g, 0.0, 1.0));
                } else {
                    FragColor = vColor;
                }
            } else {
                vec4 t = texture(uTex, vUV);
                FragColor = (pc.uIsColor == 1) ? vColor * t
                                               : vec4(vColor.rgb, vColor.a * t.r);
            }
            FragColor.rgb *= FragColor.a;   // Premultiply (same convention as the GL version)
        }
        )";

    // =================================================================
    //  D3D12 (HLSL, SM 5.0+)
    // =================================================================
    //  Compiling: the library links d3dcompiler, so D3DCompile is directly
    //  callable; fxc works too:
    //      fxc /T vs_5_0 /E VSMain ui.hlsl /Fo ui.vs.cso
    //      fxc /T ps_5_0 /E PSMain ui.hlsl /Fo ui.ps.cso
    //
    //  Pipeline-side things that must align:
    //  - Input layout (D3D12_INPUT_ELEMENT_DESC, semantic names per the struct):
    //      POSITION  R32G32_Float        offset 0
    //      COLOR     R32G32B32A32_Float  offset 8
    //      TEXCOORD0 R32G32_Float        offset 24
    //      TEXCOORD1 R32G32B32A32_Float  offset 32
    //      TEXCOORD2 R32_Float           offset 48
    //    (stride = 52, single slot, per-vertex).
    //  - Constant buffer b0: float4x4 uProj (64B) + int uIsTex + int uIsColor +
    //    8B padding = 80 bytes total. HLSL cbuffer packing cannot split an int
    //    across a 16B boundary, so 80 here versus 72 in Vulkan - do not mix.
    //  - Blend state: SrcBlend = ONE / DestBlend = INV_SRC_ALPHA (premultiplied).
    //    Sampler: linear filtering + clamp (D3D12_FILTER_MIN_MAG_MIP_LINEAR /
    //    TEXTURE_ADDRESS_CLAMP).

    // D3D12 vertex shader (entry point VSMain).
    inline constexpr const char* kD3dVertexHlsl = R"(
        cbuffer FrameCB : register(b0)
        {
            float4x4 uProj;
            int      uIsTex;
            int      uIsColor;
            float2   _pad;
        };

        struct VSIn
        {
            float2 pos   : POSITION;
            float4 color : COLOR;
            float2 uv    : TEXCOORD0;
            float4 shape : TEXCOORD1;   // cx, cy, hw, hh
            float  rad   : TEXCOORD2;
        };

        struct VSOut
        {
            float4 pos   : SV_Position;
            float4 color : COLOR;
            float2 uv    : TEXCOORD0;
            float4 shape : TEXCOORD1;
            float  rad   : TEXCOORD2;
            float2 lpos  : TEXCOORD3;
        };

        VSOut VSMain(VSIn i)
        {
            VSOut o;
            o.pos   = mul(uProj, float4(i.pos, 0.0, 1.0));
            o.color = i.color;
            o.uv    = i.uv;
            o.shape = i.shape;
            o.rad   = i.rad;
            o.lpos  = i.pos;
            return o;
        }
        )";

    // D3D12 pixel shader (entry point PSMain).
    inline constexpr const char* kD3dPixelHlsl = R"(
        cbuffer FrameCB : register(b0)
        {
            float4x4 uProj;
            int      uIsTex;
            int      uIsColor;
            float2   _pad;
        };

        Texture2D    uTex  : register(t0);
        SamplerState uSamp : register(s0);

        struct VSOut
        {
            float4 pos   : SV_Position;
            float4 color : COLOR;
            float2 uv    : TEXCOORD0;
            float4 shape : TEXCOORD1;
            float  rad   : TEXCOORD2;
            float2 lpos  : TEXCOORD3;
        };

        float roundedRectSDF(float2 p, float2 b, float r)
        {
            float2 q = abs(p) - b + r;
            return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
        }

        float4 PSMain(VSOut i) : SV_Target
        {
            float4 c;
            if (uIsTex == 0)
            {
                if (i.shape.z > 0.0 || i.shape.w > 0.0)
                {
                    float2 b = i.shape.zw;
                    float  r = clamp(i.rad, 0.0, min(b.x, b.y));
                    float  d = roundedRectSDF(i.lpos - i.shape.xy, b, r);
                    float  g = max(length(float2(ddx(d), ddy(d))), 1e-6);
                    c = float4(i.color.rgb, i.color.a * saturate(0.5 - d / g));
                }
                else
                {
                    c = i.color;
                }
            }
            else
            {
                float4 t = uTex.Sample(uSamp, i.uv);
                c = (uIsColor == 1) ? i.color * t
                                    : float4(i.color.rgb, i.color.a * t.r);
            }
            c.rgb *= c.a;   // Premultiply (same convention as the GL / Vulkan versions)
            return c;
        }
        )";

} // namespace CanvasShaders
} // namespace Kit

