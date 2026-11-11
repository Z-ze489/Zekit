/**
 * @file ZeMetalUIRenderer.mm
 * @brief `MetalUIRenderer` 的实现 —— `VkUIRenderer.cpp` 的 Metal 对应物
 *
 * @details 逐段对应 `VkUIRenderer.cpp`，但每段都短得多：Metal 把 Vulkan 里
 *  「内存类型 / 图像布局屏障 / descriptor set / pipeline layout」这一大堆
 *  样板全部收进了 API 内部。对照表见头文件。
 *
 * 本文件里的注释只写**"为什么和 Vulkan 版不一样"**，一样的地方不重复。
 *
 * ★ 只在苹果平台编译（文件级守卫在 .mm 尾部注释里说明；实际由构建脚本
 *   只把这个文件加进苹果 target 来保证，头文件也自带 `#if defined(__APPLE__)`）。
 */

#import "ZeMetalUIRenderer.h"
#import "ZeMetalContext.h"

#include "ZeFrameCollector.h"

#include <cstring>
#include <cstdio>
#include <iostream>
#include <vector>

namespace Mtl {

    // =====================================================================
    //  UIConstants —— 与 ZeUIShader.metal 里的 `struct UIConstants` 逐字节对应
    // =====================================================================
    //  ★ 与 Vulkan 版的**关键差异**（这里踩过）：
    //    GLSL(std430) 的 `mat4 uProj` 是 64 字节，后面两个 int 在 64/68 —— 共 72 字节。
    //    MSL 的常量缓冲按成员**自然对齐**：float4x4 是 64 字节（对齐 16），
    //    两个 int 各 4 字节（对齐 4）紧跟在 64/68 —— **也是 72 字节**。
    //    但 HLSL 那条路需要一个 `int2 _pad` 才能对齐到 16 字节边界（共 80）——
    //    **MSL 和 GLSL 一样不需要 pad**，因为 MSL 的 cbuffer 布局规则是"按成员自然对齐
    //    并向上取整到最大成员对齐"，而 int4/float4x4 的最末成员不会强迫整个 struct 补齐。
    //    ⇒ 结构与 Vulkan 版**逐字节相同**，可以直接 memcpy 过去。
    //
    //  ⚠️ 若这里加错 pad，症状是**投影矩阵错位**（UI 整体缩在左上角或完全不见），
    //     且不会有任何 API 报错 —— 与鸿蒙那个 density 单位的坑是同一类隐蔽错误。
    struct UIConstants {
        float   proj[16];    ///< 列主序 float4x4（glm 内存布局，直接 memcpy）
        int32_t isTex;
        int32_t isColor;
    };
    static_assert(sizeof(UIConstants) == 72,
        "UIConstants 必须与 ZeUIShader.metal 里的结构体布局逐字节一致");
    static_assert(offsetof(UIConstants, isTex) == 64, "isTex 必须在 offset 64");
    static_assert(offsetof(UIConstants, isColor) == 68, "isColor 必须在 offset 68");

    // =====================================================================
    //  Metal 的两个资源存储模式（Apple Silicon vs Intel Mac）
    // =====================================================================
    //  MTLResourceStorageModeShared   —— CPU/GPU 共享同一块内存，CPU 写完即可见。
    //                                    Apple Silicon（统一内存）上是零拷贝，首选。
    //  MTLResourceStorageModeManaged  —— 独立显存的 Intel Mac 上必须用它，
    //                                    且 CPU 写完要手动 `didModifyRange:`
    //                                    （否则 GPU 读到旧数据 = 画面卡在上一帧）。
    //  用一个编译期常量收口，避免两处（顶点/索引缓冲）各写一遍条件。
#if TARGET_OS_IPHONE || defined(__arm64__) && defined(__APPLE__)
    // iOS 全部是统一内存；Apple Silicon Mac 走这一支。
    static constexpr MTLResourceOptions kBufferStorage = MTLResourceStorageModeShared;
    static constexpr bool kNeedsDidModifyRange = false;
#else
    // Intel Mac（x86_64）—— 需要 Managed + didModifyRange。
    static constexpr MTLResourceOptions kBufferStorage = MTLResourceStorageModeManaged;
    static constexpr bool kNeedsDidModifyRange = true;
#endif

    namespace {
        /// @brief 缓冲区上传后的收尾：Managed 模式下通知 Metal "这段 CPU 改过了"
        /// @note  Shared 模式下是空操作。写成函数是为了不让每个调用点都挂 #if。
        inline void syncUpload(id<MTLBuffer> buf, NSUInteger offset, NSUInteger length)
        {
            if (kNeedsDidModifyRange && buf != nil && length > 0)
                [buf didModifyRange:NSMakeRange(offset, length)];
        }
    }

    // =====================================================================
    //  生命周期
    // =====================================================================
    MetalUIRenderer::~MetalUIRenderer() { Shutdown(); }

    bool MetalUIRenderer::Init(MetalContext& ctx)
    {
        if (m_pipeline != nil) return true;   // 幂等（与 Vk 版一致）

        m_ctx = &ctx;
        if (!m_ctx->isReady()) {
            std::cerr << "[MtlUI] MetalContext 未就绪；无法创建 UI 渲染器\n";
            m_ctx = nullptr;
            return false;
        }

        // 顺序：库 → 管线 → 采样器，最后哑纹理。
        // ★ 每一步失败都 Shutdown() 再返回（与 Vk 版同一条纪律）：
        //   管线**引用了 Context 的 layer/device**，留着不销毁的话，
        //   调用方随后 ctx.cleanup() 会把 device 收掉 —— 那就是"销毁仍在被引用的对象"。
        if (!createPipeline()) { Shutdown(); return false; }
        if (!createSampler())  { Shutdown(); return false; }

        // 帧槽数量与 Context 的 in-flight 上限一致（Metal 也是 triple buffering）
        m_frames.assign(MetalContext::kMaxFramesInFlight, FrameBuffers{});

        // ---- 哑纹理：1×1 不透明白 ----
        // 理由同 Vk 版：纯色批次在着色器里走 `uIsTex == 0` 分支不采样，
        // 但 `texture2d<float> uTex` 是**静态声明**的参数，
        // 每次 draw 都必须绑一个有效 texture（Metal 校验层同样这么要求）。
        const uint8_t white[4] = { 255, 255, 255, 255 };
        m_dummyTex = CreateRGBA(1, 1, white, 4, /*bgra*/false);
        if (m_dummyTex == 0) {
            std::cerr << "[MtlUI] 哑纹理创建失败\n";
            Shutdown();
            return false;
        }

        return true;
    }

    void MetalUIRenderer::Shutdown()
    {
        // ★ 与 Vk 版的一处**重要差异**：这里**不需要** waitIdle。
        //   Vulkan 销毁正在被在飞帧使用的资源 = 悬垂引用（必须先 vkDeviceWaitIdle）；
        //   Metal 的 `[commandBuffer commit]` 会 **retain** 它编码时引用过的所有资源，
        //   所以这里置 nil 是安全的：对象会活到那帧画完，再由 ARC 回收。
        //   调用方的时序保证：Backend::Shutdown() 在 ctx.cleanup() **之前**调用
        //   （见 ZeRenderBackendApple.mm），届时渲染循环已停、不再产生新命令缓冲。
        for (auto& fb : m_frames) destroyFrameBuffers(fb);
        m_frames.clear();

        m_textures.clear();
        m_freeSlots.clear();
        m_dummyTex = 0;
        m_overflowWarned = false;

        m_pipeline = nil;
        m_sampler = nil;
        m_lib = nil;
        m_ctx = nullptr;
    }

    // =====================================================================
    //  着色器库
    // =====================================================================
    id<MTLLibrary> MetalUIRenderer::loadLibrary()
    {
        id<MTLDevice> dev = m_ctx->device();

        // ---- 首选：bundle 里编译好的 default.metallib ----
        // 由构建脚本 `xcrun -sdk macosx metal ZeUIShader.metal -o default.metallib`
        // 离线产出并打进 app bundle。生产路径**必须**走这里：
        // 快、无编译错误风险、不需要在运行时携带 MSL 源码。
        NSError* err = nil;
        id<MTLLibrary> lib = [dev newDefaultLibrary];
        if (lib != nil) return lib;

        // ---- 退路：运行时在线编译源码 ----
        // 只在开发期（忘了把 .metal 加进 bundle）才走到这里。
        // ⚠️ 这条路**不能**用于 App Store 生产：`newLibraryWithSource:` 会显著拖慢启动，
        //    且需要把 .metal 源码作为资源打进包（.metal 会被视为脚本类资源，审核存疑）。
        NSLog(@"[MtlUI] default.metallib 未找到（%@），退回运行时编译。", err);

        // 源码以字符串字面量内嵌：这样即使 .metal 没进 bundle 也能跑起来，
        // 代价是本文件与 ZeUIShader.metal **必须手工保持一致**。
        // 想避免这份重复，正确做法是把构建脚本改成"用 xxcl 生成头文件"
        // （与 Vk 版的 ZeVkShaderSpv.h 完全同理）—— 见 TODO(applebundle)。
        //
        // ⚠️⚠️ 下面这段是 `Zekit/shaders/ZeUIShader.metal` 的**逐字副本**。
        //     改任何一个，都必须同步改另一个 —— 否则会出现
        //     "dev 机上（走在线编译）好看、打包后（走 default.metallib）发虚"
        //     这种最难查的差异。之所以保留内嵌，只是因为开发期方便。
        static NSString* const kSource = @R"MSL(
#include <metal_stdlib>
using namespace metal;

struct UIConstants {
    float4x4 uProj;
    int      uIsTex;
    int      uIsColor;
};

struct VOut {
    float4 clipPos [[position]];
    float4 color;
    float2 uv;
    float4 shape;
    float  rad;
    float2 pos;
};

vertex VOut uiVertex(
    uint vid                        [[vertex_id]],
    const device float* vtx         [[buffer(0)]],
    constant UIConstants& u         [[buffer(1)]])
{
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
    o.pos   = pos;
    o.clipPos = u.uProj * float4(pos, 0.0f, 1.0f);
    return o;
}

static inline float roundedRectSDF(float2 p, float2 b, float r)
{
    float2 q = abs(p) - b + float2(r, r);
    return min(max(q.x, q.y), 0.0f) + length(max(q, float2(0.0f))) - r;
}

fragment float4 uiFragment(
    VOut in                          [[stage_in]],
    constant UIConstants& u          [[buffer(1)]],
    texture2d<float> uTex            [[texture(0)]],
    sampler uSampler                 [[sampler(0)]])
{
    if (u.uIsTex == 0) {
        if (in.shape.z > 0.0f || in.shape.w > 0.0f) {
            const float2 c = in.shape.xy;
            const float2 b = in.shape.zw;
            const float  r = clamp(in.rad, 0.0f, min(b.x, b.y));
            const float2 p = in.pos - c;
            const float  d = roundedRectSDF(p, b, r);
            const float2 grad = float2(dfdx(d), dfdy(d));
            const float  g = max(length(grad), 1e-6f);
            const float coverage = clamp(0.5f - d / g, 0.0f, 1.0f);
            return float4(in.color.rgb, in.color.a * coverage);
        } else {
            return in.color;
        }
    } else {
        const float4 t = uTex.sample(uSampler, in.uv);
        if (u.uIsColor == 1) {
            return in.color * t;
        } else {
            return float4(in.color.rgb, in.color.a * t.r);
        }
    }
}
)MSL";

        MTLCompileOptions* opts = [[MTLCompileOptions alloc] init];
        lib = [dev newLibraryWithSource:kSource options:opts error:&err];
        if (lib == nil) {
            std::cerr << "[MtlUI] 在线编译着色器失败: "
                      << (err ? [[err localizedDescription] UTF8String] : "unknown") << "\n";
        }
        return lib;
    }

    // =====================================================================
    //  管线 / 采样器
    // =====================================================================
    bool MetalUIRenderer::createPipeline()
    {
        // ---- ① 库 ----
        // ⚠️ 只调一次：loadLibrary() 在退路分支里会**在线编译**（几十毫秒），
        //    调两次 = 白编译两遍 + 泄漏一个 MTLLibrary。
        m_lib = loadLibrary();
        if (m_lib == nil) return false;

        id<MTLFunction> vfn = [m_lib newFunctionWithName:@"uiVertex"];
        id<MTLFunction> ffn = [m_lib newFunctionWithName:@"uiFragment"];
        if (vfn == nil || ffn == nil) {
            std::cerr << "[MtlUI] 着色器入口未找到（uiVertex / uiFragment）\n";
            return false;
        }

        // ---- ② 管线状态对象 ----
        // ★ 与 Vulkan 的**最大差异**：Metal **没有**"顶点输入布局"这个概念。
        //   Vulkan 要显式声明 binding + 5 个 attribute（格式/偏移）；
        //   Metal 的顶点着色器自己去 `vtx[base + n]` 取数,管线不需要知道布局。
        //   所以下面这一整块（Vk 版的 40 行）在这里**根本不存在**。
        MTLRenderPipelineDescriptor* pd = [[MTLRenderPipelineDescriptor alloc] init];
        pd.vertexFunction   = vfn;
        pd.fragmentFunction = ffn;
        // 格式必须与 CAMetalLayer 一致（MetalContext::drawablePixelFormat()）
        pd.colorAttachments[0].pixelFormat = m_ctx->drawablePixelFormat();

        // ---- ③ 混合：对齐 GL 的 glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA) ----
        // RGB 和 A **同时**做同样的混合（同 Vk 版；否则嵌套半透明会变脏）。
        MTLRenderPipelineColorAttachmentDescriptor* ca = pd.colorAttachments[0];
        ca.blendingEnabled = YES;
        ca.sourceRGBBlendFactor        = MTLBlendFactorSourceAlpha;
        ca.destinationRGBBlendFactor   = MTLBlendFactorOneMinusSourceAlpha;
        ca.rgbBlendOperation           = MTLBlendOperationAdd;
        ca.sourceAlphaBlendFactor      = MTLBlendFactorSourceAlpha;
        ca.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        ca.alphaBlendOperation         = MTLBlendOperationAdd;
        ca.writeMask = MTLColorWriteMaskAll;

        // 采样数 = 1（drawable 无 MSAA）。圆角/圆形的抗锯齿靠片元里的 SDF，不靠 MSAA。
        pd.rasterSampleCount = 1;

        // 背面剔除关闭：UI 四边形朝向不保证（同 Vk 的 VK_CULL_MODE_NONE）
        // ★ 注意 Metal 的退绕方向与 Vulkan 一样是"默认逆时针为正面"，
        //   但既然不剔除了就不必设 winding。这里显式写出来只为可读性。
        pd.inputPrimitiveTopology = MTLPrimitiveTopologyClassTriangle;

        NSError* err = nil;
        m_pipeline = [m_ctx->device() newRenderPipelineStateWithDescriptor:pd error:&err];
        if (m_pipeline == nil) {
            std::cerr << "[MtlUI] 创建管线状态失败: "
                      << (err ? [[err localizedDescription] UTF8String] : "unknown") << "\n";
            return false;
        }

        // ---- ④ 深度/模板 ----
        // drawable 的 MTLRenderPassDescriptor 不附深度/模板纹理，
        // 所以这里什么都不用配（Vulkan 那边还要显式给两个"关闭"的 state）。
        // 2D UI 的层叠顺序由**绘制顺序**保证（FrameCollector 已经排好序），
        // 这正是三个后端共用的同一套约定。

        return true;
    }

    bool MetalUIRenderer::createSampler()
    {
        // 一个共享采样器就够 —— 全库所有纹理参数一致（LINEAR + CLAMP_TO_EDGE + 无 mip）。
        // 与 Vk 版逐项对应：
        //   VK_FILTER_LINEAR          → MTLSamplerMinMagFilterLinear
        //   VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE → MTLSamplerAddressModeClampToEdge
        //   maxLod = 0（无 mip）       → lodMinClamp/lodMaxClamp = 0
        MTLSamplerDescriptor* sd = [[MTLSamplerDescriptor alloc] init];
        sd.minFilter    = MTLSamplerMinMagFilterLinear;
        sd.magFilter    = MTLSamplerMinMagFilterLinear;
        sd.mipFilter    = MTLSamplerMipFilterNotMipmapped;
        sd.sAddressMode = MTLSamplerAddressModeClampToEdge;
        sd.tAddressMode = MTLSamplerAddressModeClampToEdge;
        sd.rAddressMode = MTLSamplerAddressModeClampToEdge;
        sd.lodMinClamp  = 0.0f;
        sd.lodMaxClamp  = 0.0f;   // 只有 level 0
        sd.maxAnisotropy = 1;

        m_sampler = [m_ctx->device() newSamplerStateWithDescriptor:sd];
        if (m_sampler == nil) {
            std::cerr << "[MtlUI] 创建采样器失败\n";
            return false;
        }
        return true;
    }

    // =====================================================================
    //  顶点 / 索引缓冲
    // =====================================================================
    bool MetalUIRenderer::ensureCapacity(FrameBuffers& fb, uint32_t vertexBytes, uint32_t indexBytes)
    {
        if (fb.vboCap >= vertexBytes && fb.iboCap >= indexBytes) return true;

        // ★ 与 Vk 版的一处**本质差异**：这里**不需要** waitIdle。
        //   原因：Metal 的 `[commandBuffer commit]` 会 **retain** 它在编码时
        //   引用过的所有资源（buffer/texture/pipeline）。所以就算 CPU 立刻把
        //   缓冲换成新的、把旧的 release 掉，GPU 手里的那张"票"仍然有效 ——
        //   对象会活到那帧画完为止，被 ARC 自动回收。
        //   Vulkan 没有这个机制（销毁即悬垂），所以 Vk 版必须先 m_ctx->waitIdle()。
        //   ⇒ 少一次全设备同步，成长时不会卡帧。
        destroyFrameBuffers(fb);

        // 容量按 1.5 倍留余量，避免每帧 resize 都重建
        const uint32_t vbCap = vertexBytes ? (vertexBytes + vertexBytes / 2u) : 4096u;
        const uint32_t ibCap = indexBytes  ? (indexBytes  + indexBytes  / 2u) : 4096u;

        id<MTLDevice> dev = m_ctx->device();

        fb.vbo = [dev newBufferWithLength:vbCap options:kBufferStorage];
        fb.ibo = [dev newBufferWithLength:ibCap options:kBufferStorage];
        if (fb.vbo == nil || fb.ibo == nil) {
            std::cerr << "[MtlUI] 分配顶点/索引缓冲失败 (vbo=" << vbCap
                      << " ibo=" << ibCap << ")\n";
            destroyFrameBuffers(fb);
            return false;
        }
        fb.vboCap = vbCap;
        fb.iboCap = ibCap;
        return true;
    }

    void MetalUIRenderer::destroyFrameBuffers(FrameBuffers& fb)
    {
        // Metal 的资源是 ARC 管理的：置 nil 即可，不需要显式 free。
        fb.vbo = nil;
        fb.ibo = nil;
        fb.vboCap = 0;
        fb.iboCap = 0;
    }

    // =====================================================================
    //  纹理上传（Kit::ITextureSink 的实现）
    // =====================================================================
    uint32_t MetalUIRenderer::CreateGray(int w, int h, const uint8_t* pixels, int pitch)
    {
        return uploadTexture(w, h, pixels, pitch,
            MTLPixelFormatR8Unorm, 1, /*swapRedBlue*/false);
    }

    uint32_t MetalUIRenderer::CreateRGBA(int w, int h, const uint8_t* pixels, int pitch, bool bgra)
    {
        // 目标格式恒为 RGBA8Unorm（不用 BGRA）：Metal 支持 BGRA8Unorm 纹理，
        // 但那样 shader 里采样出来的通道序就与 GL/Vk 版不同（除非再换 swizzle）。
        // 保持"统一 RGBA"这条约定，转换只在 CPU 侧做一次（与 Vk 版完全一样）。
        return uploadTexture(w, h, pixels, pitch,
            MTLPixelFormatRGBA8Unorm, 4, /*swapRedBlue*/bgra);
    }

    uint32_t MetalUIRenderer::uploadTexture(int w, int h, const uint8_t* pixels, int pitch,
                                            MTLPixelFormat format, int bytesPerPixel, bool swapRedBlue)
    {
        if (!m_ctx || w <= 0 || h <= 0 || !pixels || bytesPerPixel <= 0) return 0;

        const size_t rowBytes = static_cast<size_t>(w) * bytesPerPixel;

        // ---- ① CPU 侧压成紧密排布，顺带做 BGRA→RGBA ----
        // FreeType 的位图行距常带填充（pitch > w*bpp），而
        // `replaceRegion:...bytesPerRow:` 只认"紧密"的源（bytesPerRow = w*bpp）。
        // 与 Vk 版逐行对应（Vulkan 那边也是必须自己整理，因为 vkCmdCopyBufferToImage
        // 的 bufferRowLength=0 表示紧凑；GL 那条路才有 GL_UNPACK_ROW_LENGTH）。
        std::vector<uint8_t> packed(rowBytes * static_cast<size_t>(h));

        for (int y = 0; y < h; ++y) {
            const uint8_t* src = pixels + static_cast<size_t>(y) * pitch;
            uint8_t* dst = packed.data() + static_cast<size_t>(y) * rowBytes;

            if (swapRedBlue && bytesPerPixel == 4) {
                for (int x = 0; x < w; ++x) {
                    dst[x * 4 + 0] = src[x * 4 + 2];
                    dst[x * 4 + 1] = src[x * 4 + 1];
                    dst[x * 4 + 2] = src[x * 4 + 0];
                    dst[x * 4 + 3] = src[x * 4 + 3];
                }
            }
            else {
                std::memcpy(dst, src, rowBytes);
            }
        }

        // ---- ② 申请纹理 ----
        // ★ 与 Vk 版最大的简化：**没有** image / deviceMemory / imageView / staging buffer
        //   这一整套（Vulkan 那边是 6 步）。Metal 只需要一个 MTLTextureDescriptor
        //   直接 `newTextureWithDescriptor:` —— 内存分配、视图、默认布局全被 API 收走。
        MTLTextureDescriptor* td =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format
                                                               width:static_cast<NSUInteger>(w)
                                                              height:static_cast<NSUInteger>(h)
                                                           mipmapped:NO];
        // 纹理只被 shader 读 → ShaderRead。
        // （Vulkan 那边还要在 TRANSFER_DST / SHADER_READ_ONLY 两个 layout 之间手动转，
        //   这里由 Metal 自己管，我们一行都不用写。）
        td.usage = MTLTextureUsageShaderRead;

        id<MTLDevice> dev = m_ctx->device();
        id<MTLTexture> tex = [dev newTextureWithDescriptor:td];
        if (tex == nil) {
            std::cerr << "[MtlUI] 创建纹理失败 (" << w << "x" << h << ")\n";
            return 0;
        }

        // ---- ③ 上传 ----
        // ★ 与 Vk 版对比：Vulkan 要建 staging buffer → vkCmdCopyBufferToImage →
        //   两次 imageBarrier → waitFence。这里**一次调用**搞定，
        //   且是 CPU 阻塞完成（不像 Vulkan 那样是异步 GPU 拷贝）。
        //   对 UI 纹理（字形、图片，都是一次性上传）这是最省心的做法。
        [tex replaceRegion:MTLRegionMake2D(0, 0,
                        static_cast<NSUInteger>(w), static_cast<NSUInteger>(h))
               mipmapLevel:0
                 withBytes:packed.data()
               bytesPerRow:rowBytes];

        // ---- ④ 入库 ----
        const uint32_t handle = allocSlot();
        if (handle == 0) {
            tex = nil;   // ARC 回收
            return 0;
        }

        Texture& t = m_textures[handle - 1];
        t.tex = tex;
        // ★ 采样器在 draw 时用 setFragmentSamplerState 单独设（Metal 把 sampler
        //   从纹理里拆出来了，与 Vulkan 一样；GL 那条路才是挂在纹理上）。
        //   所以这里**不需要**像 Vk 版那样写 descriptor set —— 少一整块代码。

        return handle;
    }

    void MetalUIRenderer::Destroy(uint32_t tex)
    {
        if (tex == 0 || !m_ctx || tex > m_textures.size()) return;

        Texture& t = m_textures[tex - 1];
        if (!t.occupied && t.tex == nil) return;

        // ★ 不需要像 Vk 版那样先 waitIdle：command buffer 已 retain 引用，
        //   这里置 nil 后对象会活到在飞的帧画完，由 ARC 释放。
        t.tex = nil;
        releaseSlot(tex);
    }

    uint32_t MetalUIRenderer::allocSlot()
    {
        if (!m_freeSlots.empty()) {
            const uint32_t slot = m_freeSlots.back();
            m_freeSlots.pop_back();
            m_textures[slot].occupied = true;
            return slot + 1;
        }

        if (m_textures.size() >= kMaxTextures) {
            // 只报一次，避免刷屏。超出后字形会安静地不渲染（LayoutText 跳过 tex==0 的字形）
            if (!m_overflowWarned) {
                m_overflowWarned = true;
                std::cerr << "[MtlUI] 纹理槽位已达上限 (" << kMaxTextures
                          << ")，后续纹理不再创建（该上字形图集了）\n";
            }
            return 0;
        }

        m_textures.emplace_back();
        m_textures.back().occupied = true;
        return static_cast<uint32_t>(m_textures.size());
    }

    void MetalUIRenderer::releaseSlot(uint32_t handle)
    {
        if (handle == 0 || handle > m_textures.size()) return;
        m_textures[handle - 1].occupied = false;
        m_freeSlots.push_back(handle - 1);
    }

    id<MTLTexture> MetalUIRenderer::textureFor(uint32_t handle) const
    {
        if (handle != 0 && handle <= m_textures.size()) {
            const Texture& t = m_textures[handle - 1];
            if (t.occupied && t.tex != nil) return t.tex;
        }
        if (m_dummyTex != 0 && m_dummyTex <= m_textures.size()) {
            const Texture& d = m_textures[m_dummyTex - 1];
            if (d.tex != nil) return d.tex;
        }
        return nil;
    }

    // =====================================================================
    //  投影矩阵
    // =====================================================================
    void MetalUIRenderer::buildProjection(float* out16, int uiW, int uiH) const
    {
        // 正交投影：把**逻辑像素** (0..uiW, 0..uiH) 映射到 NDC。
        //
        // ★ 三后端的 NDC Y 方向：
        //     GL      Y 向上（+1 = 屏幕顶部）→ ortho(0, W, H, 0)
        //     Vulkan  Y 向下（-1 = 屏幕顶部）→ ortho(0, W, 0, H)   ← bottom/top 对调
        //     Metal   Y 向上（同 D3D12）      → ortho(0, W, H, 0)   ← 与 GL 同形
        //   ⇒ 这里和 GL 版**形状一样**，和 Vk 版**相反**。
        //     写反的症状：整个界面上下颠倒 + SDF 圆角跑到对角。
        //
        // ★ 必须用 uiW/uiH（逻辑像素）而不是物理像素：顶点坐标活在逻辑像素里，
        //   投影必须按同一尺度算，否则界面缩到左上角一块（与鸿蒙那个 density 坑同源）。
        //
        // 列主序 mat4（与 glm::ortho 的内存布局一致，可以直接喂给 muProj）。
        // 手写而不是调 glm：这里只需要 4 个非零元素，避免把 glm 拖进 .mm 的编译单元
        // （Objective-C++ 里 include glm 会显著变慢，且没必要）。
        //
        //   x_ndc = 2/uiW * x - 1
        //   y_ndc = 2/uiH * y - 1        （Y 向上：y=0 → -1 屏幕底,  y=uiH → +1 屏幕顶）
        //       ⚠️ y=0 是屏幕**顶**（我们的坐标系原点在左上），所以要让 y=0 映到 +1：
        //          y_ndc = 1 - 2/uiH * y = -(2/uiH)*y + 1
        //       ⇒ 这正是 ortho(0, W, H, 0) 的那条（bottom=H, top=0）。
        //   列主序 = 每 4 个 float 是一列：m[0..3] 第 0 列, m[4..7] 第 1 列, ...
        const float w = (uiW > 0) ? static_cast<float>(uiW) : 1.0f;
        const float h = (uiH > 0) ? static_cast<float>(uiH) : 1.0f;

        for (int i = 0; i < 16; ++i) out16[i] = 0.0f;

        out16[0]  =  2.0f / w;    // 列0.x
        out16[5]  = -2.0f / h;    // 列1.y  ← 负号 = Y 翻转（左上原点 → NDC 上下）
        out16[10] =  1.0f;        // 列2.z
        out16[12] = -1.0f;        // 列3.x  （平移）
        out16[13] =  1.0f;        // 列3.y
        out16[15] =  1.0f;        // 列3.w
    }

    // =====================================================================
    //  绘制
    // =====================================================================
    int MetalUIRenderer::Draw(const Kit::FrameCollector& fc, const MetalContext::FrameInfo& frame)
    {
        if (m_pipeline == nil || !m_ctx) return 0;
        if (frame.encoder == nil) return 0;
        if (frame.slot >= m_frames.size()) {
            // 越界 = 帧槽位表错位，属致命编程错误；静默返回 0 会把问题藏在
            // "画面突然空了"背后，这里明确报出来（与 Vk 版同一条纪律）。
            std::cerr << "[MtlUI] Draw: 帧槽越界 (" << frame.slot
                      << " >= " << m_frames.size() << ")\n";
            return 0;
        }

        const auto& verts = fc.Vertices();
        const auto& idxs = fc.Indices();
        const auto& batches = fc.Batches();
        if (verts.empty() || idxs.empty() || batches.empty()) return 0;

        const uint32_t vertBytes = static_cast<uint32_t>(verts.size() * sizeof(Kit::UIVertex));
        const uint32_t idxBytes  = static_cast<uint32_t>(idxs.size() * sizeof(uint32_t));

        FrameBuffers& fb = m_frames[frame.slot];
        if (!ensureCapacity(fb, vertBytes, idxBytes)) return 0;

        // 本帧槽位的数据是安全的：MetalContext 在调本回调**之前**已经
        // dispatch_semaphore_wait 过这个槽位的信号量（= GPU 已经画完上一次用它的那帧）。
        std::memcpy([fb.vbo contents], verts.data(), vertBytes);
        std::memcpy([fb.ibo contents], idxs.data(), idxBytes);
        syncUpload(fb.vbo, 0, vertBytes);   // Managed 模式下必做，Shared 下是空操作
        syncUpload(fb.ibo, 0, idxBytes);

        id<MTLRenderCommandEncoder> enc = frame.encoder;

        // 投影尺寸 ≠ 渲染目标尺寸（移动端）。
        //   桌面：uiExtent 恒等于 extent，下面这两行等价于没有。
        //   移动：extent 是物理像素（视口），uiExtent 是逻辑像素（投影）。
        // ★ 与 Vk 版的一处差异：**这里不需要设视口**。
        //   Metal 的 `MTLRenderCommandEncoder` 默认视口 = 整块 render target
        //   （= 本帧 drawable，物理像素），正是我们想要的"铺满"。
        //   只有在要画到子区域时才需要 `setViewport:`（我们不需要）。
        //   同理 Metal 没有"动态 scissor"——默认就是整块。
        //   所以 Vk 版那段 vkCmdSetViewport / vkCmdSetScissor 在这里**不存在**。
        //   ⚠️ 别去找 `renderTargetWidth`：那是 GPU 侧(如 D3D12 的
        //      D3D12_VIEWPORT)的概念，Metal 里没有这个描述符字段。
        const uint32_t UW = (frame.width  > 0) ? frame.width  : 1;
        const uint32_t UH = (frame.height > 0) ? frame.height : 1;

        // ---- ① 管线 + 缓冲绑定（整帧一次）----
        [enc setRenderPipelineState:m_pipeline];

        // 顶点缓冲：与 Vk 版一样绑在 slot 0（对应 shader 里 `[[buffer(0)]]`）
        [enc setVertexBuffer:fb.vbo offset:0 atIndex:0];
        // 采样器绑到 fragment 的 slot 0（对应 shader 里 `[[sampler(0)]]`）。
        //   索引缓冲**不需要**在这里绑 —— 它是 drawIndexedPrimitives 的参数。
        [enc setFragmentSamplerState:m_sampler atIndex:0];

        // ---- ② 常量缓冲：buffer(1) 里是 { proj[16], isTex, isColor } ----
        // ★ 与 Vk 版的对应：Vulkan 用 push constant（因为 uIsTex/uIsColor 逐批次变）。
        //   Metal 用 `setVertexBytes/setFragmentBytes` —— 小常量直接内联进
        //   command buffer，无需预分配 buffer，语义完全一致。
        //
        // ★ Metal **没有** `setVertexBytes:length:offset:atIndex:` 这种带 offset 的重载：
        //   它永远从 index 槽位的**起点**写。所以"只更新结构体后半段（isTex/isColor）"
        //   是做不到的（除非拆成两个 buffer）。⇒ 干脆每批次推**完整 72 字节**：
        //   几何意义简单、与 Vk 版行为一致，代价只是每批次多一次 72 字节的内存拷贝
        //   （对一帧几十个批次完全可忽略）。刻意**不做**那点微优化 —— 少一类错位风险。
        //
        //   ⚠️ MSL 侧 `[[buffer(1)]]` 是**整块**绑定的：Metal 不会替你把 isTex
        //     对齐到偏移 64。CPU 侧写进去的那段数据必须让 isTex 恰好落在 64 处 ——
        //     UIConstants 的结构布局（见文件头）就是为这件事服务的。
        UIConstants pc{};
        buildProjection(pc.proj, static_cast<int>(UW), static_cast<int>(UH));

        // ---- ③ 批次循环：与 GL/Vk 版逐行对应 ----
        //  GL:  glBindTexture + glUniform1i(uIsTex) + glDrawElements
        //  Vk:  bindDescriptorSets + pushConstants   + vkCmdDrawIndexed
        // Mtl:  setFragmentTexture + setVertex/FragmentBytes + drawIndexedPrimitives
        int draws = 0;
        for (const auto& b : batches) {
            if (b.indexCount == 0) continue;

            // uIsTex / uIsColor 每个批次都可能变 → 每批次重设（与 Vk 推 push constant 同理）
            pc.isTex   = (b.texture != 0) ? 1 : 0;
            pc.isColor = (b.isColor ? 1 : 0);

            // 顶点侧与片元侧都绑 buffer(1) 的同一份数据。
            // 顶点侧本帧其实只读 proj（不变），但重推的开销可忽略，
            // 换来的是"两侧看到的一定是同一个结构体实例"，少一类错位风险。
            [enc setVertexBytes:&pc length:sizeof(pc) atIndex:1];
            [enc setFragmentBytes:&pc length:sizeof(pc) atIndex:1];

            id<MTLTexture> tex = (b.texture != 0) ? textureFor(b.texture)
                                                  : textureFor(m_dummyTex);
            if (tex == nil) continue;   // 表已耗尽：跳过而不是绑空纹理

            [enc setFragmentTexture:tex atIndex:0];

            // ★ 索引偏移的单位是**字节**（不是索引个数），
            //   所以 indexOffset 要乘 sizeof(uint32_t)。写成"个数"会让第 2 个批次起
            //   读到错位的索引（画面表现为"只有第一批次对，后面全乱"）。
            [enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                            indexCount:b.indexCount
                             indexType:MTLIndexTypeUInt32
                           indexBuffer:fb.ibo
                     indexBufferOffset:static_cast<NSUInteger>(b.indexOffset) * sizeof(uint32_t)];
            ++draws;
        }

        return draws;
    }

} // namespace Mtl
