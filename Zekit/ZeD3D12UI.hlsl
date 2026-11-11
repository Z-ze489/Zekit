// =============================================================================
//  ZeD3D12UI.hlsl —— Zekit D3D12 后端的 UI 着色器
// =============================================================================
//  与 ZeUIRenderer.cpp 里的 GLSL（kVS / kFS）**逐行对应**。
//
//  顶点布局：与 Kit::UIVertex 严格一致（13 个 tightly packed float）：
//      float2 pos      // offset 0   (location 0)
//      float4 color    // offset 8   (location 1)
//      float2 uv       // offset 24  (location 2)
//      float4 shape    // offset 32  (location 3)  cx, cy, hw, hh
//      float  rad      // offset 48  (location 4)
//  共 52 字节 = 13 float。static_assert 在 .cpp 里守着。
//
//  ★ SDF 抗锯齿的三条不变量（改动前先读 ZeUIRenderer.cpp 的 kFS 与
//    ZeFrameCollector.cpp 的 kSdfPadAA）：
//    1. 过渡带宽度 = **正好 1 像素**，靠 clamp(0.5 - d/g, 0, 1) 取覆盖率；
//    2. 梯度 g 用「距离场每像素走多远」= length(ddx(d), ddy(d))，
//       **不是** fwidth()（HLSL 里 fwidth = abs(ddx) + abs(ddy)，45° 上大 1.41 倍）；
//    3. p 用 VS 传来的**逻辑坐标**（左上原点），不用 SV_Position（那是像素中心坐标系）。
//    第 3 条尤其重要：用 SV_Position.xy 会把 SDF 中心错位到物理像素坐标，
//    圆角整体偏移。
// =============================================================================

cbuffer UIConstants : register(b0)
{
    float4x4 uProj;    // 正交投影（已按 D3D 的 Y-up NDC 组织）
    int      uIsTex;   // 0 = 纯色图元，1 = 采样纹理
    int      uIsColor; // 1 = 彩色字形/图片（直接乘），0 = 灰度字形（取 .r 当覆盖率）
    int2     _pad;     // 补齐到 16 字节边界（常量缓冲按 4×float4 对齐更保险）
};

struct VSInput
{
    float2 pos   : POSITION;
    float4 color : COLOR;
    float2 uv    : TEXCOORD0;
    float4 shape : TEXCOORD1;   // cx, cy, hw, hh
    float  rad   : TEXCOORD2;   // 圆角半径
};

struct VSOutput
{
    float4 clipPos : SV_Position;
    float4 color   : COLOR;
    float2 uv      : TEXCOORD0;
    float4 shape   : TEXCOORD1;
    float  rad     : TEXCOORD2;
    float2 pos     : TEXCOORD3;   // 逻辑坐标（左上原点），供 SDF 计算局部位置
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;

    output.color = input.color;
    output.uv    = input.uv;
    output.shape = input.shape;
    output.rad   = input.rad;
    output.pos   = input.pos;      // 原样传递逻辑坐标

    // ★ 与 GL 版同形：gl_Position = uProj * vec4(aPos, 0, 1)
    //   投影矩阵的 Y 方向由 CPU 侧按各自 NDC 约定给（D3D NDC: Y 向上，与 GL 相同，
    //   与 Vulkan 相反 —— 所以这里的矩阵和 GL 版一致，不是 Vk 版那种 bottom/top 对调）。
    output.clipPos = mul(uProj, float4(input.pos, 0.0f, 1.0f));

    return output;
}

Texture2D    uTex     : register(t0);
SamplerState uSampler : register(s0);

// 圆角矩形有向距离场（SDF）：
//   返回像素点到圆角矩形边缘的带符号距离（负 = 内部，正 = 外部）。
//   p = 相对矩形中心的坐标，b = 半宽/半高，r = 圆角半径。
float roundedRectSDF(float2 p, float2 b, float r)
{
    float2 q = abs(p) - b + float2(r, r);
    return min(max(q.x, q.y), 0.0f) + length(max(q, float2(0.0f, 0.0f))) - r;
}

float4 PSMain(VSOutput input) : SV_Target
{
    if (uIsTex == 0)
    {
        // ---- 纯色图元 ----
        // 若该图元带了形状描述（半宽半高非零），按 SDF 做**正好 1 像素**的边缘覆盖率过渡。
        if (input.shape.z > 0.0f || input.shape.w > 0.0f)
        {
            float2 c = input.shape.xy;                        // 矩形中心（逻辑坐标，左上原点）
            float2 b = input.shape.zw;                        // 半宽半高
            float  r = clamp(input.rad, 0.0f, min(b.x, b.y)); // 圆角半径钳到不超过短边一半

            // 当前片元相对矩形中心的偏移 —— 用 VS 传来的逻辑坐标，与中心同坐标系，
            // 天然规避 SV_Position 的「像素中心坐标系」错位。
            float2 p = input.pos - c;

            float d = roundedRectSDF(p, b, r);

            // ★ 梯度取 2-范数（各向同性）：fwidth() 是 |ddx|+|ddy|，
            //   在 45° 边缘上比真实"每像素变化量"大约 1.41 倍 ——
            //   那样算出来的过渡带会随角度变宽变窄，圆就会一边锐一边糊。
            //   取 length(float2(ddx, ddy)) 才是"距离场每像素走多远"，各方向一致。
            float2 grad = float2(ddx(d), ddy(d));
            float  g = max(length(grad), 1e-6f);

            float coverage = clamp(0.5f - d / g, 0.0f, 1.0f);
            return float4(input.color.rgb, input.color.a * coverage);
        }
        else
        {
            return input.color;   // 无形状信息：退化为硬边缘
        }
    }
    else
    {
        float4 t = uTex.Sample(uSampler, input.uv);
        if (uIsColor == 1)
        {
            return input.color * t;                              // 彩色字形（emoji）/ 图片：整取
        }
        else
        {
            return float4(input.color.rgb, input.color.a * t.r); // 灰度字形：只有 .r 是覆盖率
        }
    }
}
