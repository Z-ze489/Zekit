// =====================================================================
//  Zekit 渲染后端抽象层 —— **苹果（macOS / iOS）实现**（Metal）
//
//  ★★ 状态：**完整实现，但从未编译/运行过（本机是 Windows，无苹果 SDK）；
//            且平台当前被 ZePlatformGate.h 编译期封禁。**
//  -------------------------------------------------------------------
//  与 `ZeRenderBackendHarmony.cpp` 形状**刻意对称** —— 都是"一个真后端 +
//  工厂"，只是鸿蒙那边是 Vulkan，这边是 Metal。
//
//  为什么是 `.mm` 而不是 `.cpp`：本文件 include 了 `ZeMetalContext.h` /
//  `ZeMetalUIRenderer.h`，它们 `#import <Metal/Metal.h>` 并用 `id<MTLDevice>`
//  这类 Objective-C 类型 —— 必须用 **Objective-C++** 编译（同 ZeMetalContext.mm）。
//
//  ## 与其它平台后端的对照
//
//    | 平台 | 文件 | 默认后端 |
//    |---|---|---|
//    | Windows | ZeRenderBackendWin32.cpp | D3D12 |
//    | Linux | ZeRenderBackendLinux.cpp | Vulkan |
//    | Android | ZeRenderBackendAndroid.cpp | Vulkan |
//    | HarmonyOS | ZeRenderBackendHarmony.cpp | Vulkan |
//    | **Apple** | **本文件** | **Metal** |
//
//  ## ★ 苹果特有的生命周期约束（与鸿蒙"surface 会换代"同源）
//
//  `CAMetalLayer` 也不是永恒不变的：窗口重建 / 显示缩放变化（拖到不同
//  Retina 屏之间）时，NSView 的 layer 会被换掉。所以：
//    · `Init()` 里拿到的 layer 是**当前这一代**；
//    · 下一代出现时必须 `Shutdown()` + 重新 `Init()`；
//    · 因此本后端**不缓存** layer（每次 Init 都从 nativeHandle 现取，
//      与鸿蒙不缓存 OHNativeWindow 同一条纪律）。
//
//  ## 两个尺寸，别混（苹果这边最不容易错）
//
//    · **drawable / layer** —— **物理**像素（问 Plat 层的 `AppleSurfaceSize`）；
//    · **UI 投影**          —— **逻辑**像素（Kit 传进来的 width/height）。
//  ★ 苹果的 `MetalContext::setClientSize()` 直接设 `layer.drawableSize`，
//    文档与实现一致 —— **不需要**鸿蒙那种"运行时判别 px 还是 vp"的兜底。
// =====================================================================

#include "pch.h"

// 本文件只参与苹果构建。★ 再套一层守卫是"双保险"，且**这里真的必要**：
//   `build_zekit_linux.sh` 用 `for f in "$SRC"/*.cpp` 之类的方式**遍历**收源文件；
//   但苹果文件是 `.mm`（不是 `.cpp`），Linux 的 glob 收不到 —— 不过将来若有人
//   把构建脚本改成"收所有 .mm 和 .cpp"，这层守卫就是唯一防线。
//   Windows 那边的 build_zekit.sh 是显式清单，不受影响。
//   （与 ZeRenderBackendHarmony.cpp 的 `#if defined(__OHOS__)` 是同一条纪律。）
#if defined(__APPLE__)

// ---- 平台图形栈（**只在本文件与 ZeMetalContext.mm 里出现**）----
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

// ---- 后端接口 ----
#include "ZeRhiBackend.h"

// ---- Metal 上下文与 UI 渲染器 ----
#include "ZeMetalContext.h"
#include "ZeMetalUIRenderer.h"

// ---- Kit 侧被"改道"的对象（只在 AttachTextureSinks 里用到具体类型）----
#include "ZeFontRenderer.h"
#include "ZeImageStore.h"
#include "ZeFrameCollector.h"

// ---- Plat 层：物理像素尺寸的唯一来源（见文件头）----
#include "ZeWindowApple.h"

#include <iostream>
#include <string>

namespace Rhi {

    namespace {

        /// @brief 从 Kit 递过来的不透明句柄里取出 CAMetalLayer
        ///
        /// @details
        /// 苹果这边**不缓存** layer（见文件头），但每次 Init 都要"从句柄拿到 layer"。
        /// 句柄的语义随形态不同：
        ///   · **桌面 macOS**：句柄是 `NSView*` —— 从 `view.layer` 取（须是
        ///     CAMetalLayer，由 `CreateAppleDesktopWindow()` 保证）；
        ///   · **移动 iOS**：句柄是 `UIView*`（或 `UIWindow*`）—— 同理取 `.layer`。
        /// 两种情况都归到"从 view 取 layer"，所以本函数只做这一件事。
        ///
        /// ⚠️ 这里用 `__bridge`（不转移所有权）—— layer 归 view 所有，
        ///    我们只是借用。用 `__bridge_retained` 会泄漏一个引用计数。
        id<CAMetalLayer> LayerFromHandle(void* nativeHandle) {
            if (nativeHandle == nullptr) return nil;
            // ★ `__unsafe_unretained` 而不是强引用：句柄是别人（Plat 层）持有的，
            //   本函数只在栈上用一下，不需要 ARC 插手它的生命期。
            id view = (__bridge id)nativeHandle;
            if (view == nil) return nil;
            // 只有 view 才有 `.layer`；用 respondsToSelector 兜底（防传错类型崩）。
            if (![view respondsToSelector:@selector(layer)]) return nil;
            id layer = [view layer];
            if (![layer isKindOfClass:[CAMetalLayer class]]) {
                std::cerr << "[Zekit] Metal: view.layer 不是 CAMetalLayer —— "
                             "建窗时忘了 makeBackingLayer / wantsLayer？\n";
                return nil;
            }
            return (CAMetalLayer*)layer;
        }

    } // namespace

    // =================================================================
    //  Metal 后端（苹果）—— 默认
    // =================================================================
    //
    //  形状与 Windows(不适用) / Linux / Android / 鸿蒙的 Vk 后端一致：
    //  Metal 也是显式分帧（acquire → 编码绘制 → present），
    //  全在 MetalContext::drawFrame 内部，中间用帧回调把绘制权交出来。
    //  本类只负责"记下 fc → drawFrame"。
    class MetalBackend final : public IBackend {
    public:
        MetalBackend() = default;
        ~MetalBackend() override { Shutdown(); }

        Api  api()     const override { return Api::Metal; }
        bool IsReady() const override { return m_ctx.isReady() && m_uiReady; }

        void SetGpu(Gpu gpu) override { m_gpu = gpu; }
        DeviceInfo deviceInfo() const override { return m_ctx.deviceInfo(); }

        // ★ **覆写 Preheat**（与鸿蒙那条不同）。
        //   理由：苹果上"GPU 驱动握手"（`MTLCreateSystemDefaultDevice()` 的内核态
        //   往返）是可观延迟，而 NSWindow 从创建到上屏之间有一段空窗。
        //   把 device/queue 的创建挪进 Preheat，Show() 之后 Init() 只需认领 layer。
        //   对应关系：X11 的 Vk 后端也是"窗口必须 map 之后才能建交换链，故需 Preheat"。
        bool Preheat(void* /*nativeHandle*/) override {
            // 预热不碰 layer（那时 view 可能还没就绪），只建设备
            return m_ctx.preheat(m_gpu);
        }

        bool Init(void* nativeHandle, int width, int height) override {
            if (IsReady()) return true;   // 幂等

            id<CAMetalLayer> layer = LayerFromHandle(nativeHandle);
            if (layer == nil) return false;   // surface 还没就绪

            (void)width; (void)height;   // 逻辑像素：渲染层不用它，见 AppleSurfaceSize

            try {
                m_ctx.init(layer, m_gpu);
                int pw = 0, ph = 0;
                if (Plat::AppleSurfaceSize(pw, ph)) m_ctx.setClientSize(pw, ph);
                // 投影用逻辑像素；drawable 用物理像素。两者分工见 setUiSize 的说明。
                m_ctx.setUiSize(width, height);
            } catch (const std::exception& e) {
                // 与其它平台一致：初始化失败不致命，交给 Kit 回退（→ OpenGL）。
                std::cerr << "[Zekit] Metal init failed: " << e.what() << std::endl;
                m_ctx.cleanup();
                return false;
            }

            if (!m_ui.Init(m_ctx)) {
                m_ctx.cleanup();
                return false;
            }

            m_ctx.setFrameRecorder([this](const Mtl::MetalContext::FrameInfo& fi) {
                if (m_pendingFrame != nullptr) m_ui.Draw(*m_pendingFrame, fi);
            });
            m_uiReady = true;
            return true;
        }

        void Shutdown() override {
            // 判据用 hasAnyResource()（只预热过的中间态也持有 device）
            if (!m_ctx.hasAnyResource() && !m_uiReady) return;
            m_ctx.setFrameRecorder(nullptr);
            if (m_uiReady) {
                // ★ 顺序铁律：UI 渲染器的资源**必须**在 ctx.cleanup() 之前释放
                //   （管线引用了 device/layer 的格式；同 Vk 版）。
                m_ui.Shutdown();
                m_uiReady = false;
            }
            m_ctx.cleanup();
            m_pendingFrame = nullptr;
        }

        bool Resize(int width, int height) override {
            (void)width; (void)height;   // 逻辑像素；物理尺寸问 Plat 层
            int pw = 0, ph = 0;
            if (Plat::AppleSurfaceSize(pw, ph)) m_ctx.setClientSize(pw, ph);
            return IsReady();
        }

        bool DrawFrame(const Kit::FrameCollector& fc,
                       int width, int height,
                       float r, float g, float b) override {
            if (!IsReady()) return false;
            // ★ 每帧把**真实窗口**物理尺寸喂进去 —— 与鸿蒙/Android 同一个坑：
            //   喂逻辑尺寸会让"客户区尺寸 != drawable 尺寸"恒成立，
            //   Metal 上表现为 layer.drawableSize 反复被重设（画面错位/抖动）。
            (void)width; (void)height;
            int pw = 0, ph = 0;
            if (Plat::AppleSurfaceSize(pw, ph)) m_ctx.setClientSize(pw, ph);
            // UI 投影用逻辑像素（与 Kit 的坐标同一尺度）
            if (width > 0 && height > 0) m_ctx.setUiSize(width, height);

            m_pendingFrame = &fc;
            const bool drewOk = m_ctx.drawFrame(r, g, b);
            m_pendingFrame = nullptr;
            return drewOk;
        }

        void AttachTextureSinks(void* fontRenderer, void* imageStore) override {
            // 调用方（Kit）已保证旧纹理先丢干净（见 Window::UseRenderer 的顺序）。
            if (fontRenderer)
                static_cast<Kit::FontRenderer*>(fontRenderer)->SetTextureSink(&m_ui);
            if (imageStore)
                static_cast<Kit::ImageStore*>(imageStore)->SetTextureSink(&m_ui);
        }

        void DetachTextureSinks(void* fontRenderer, void* imageStore) override {
            // 顺序铁律：先用当前 sink 把纹理丢干净，再摘钩子
            if (fontRenderer) {
                Kit::FontRenderer* f = static_cast<Kit::FontRenderer*>(fontRenderer);
                f->DropAllGlyphTextures();
                f->SetTextureSink(nullptr);
            }
            if (imageStore) {
                Kit::ImageStore* s = static_cast<Kit::ImageStore*>(imageStore);
                s->Shutdown();
                s->SetTextureSink(nullptr);
            }
        }

    private:
        Mtl::MetalContext     m_ctx;
        Mtl::MetalUIRenderer  m_ui;
        Rhi::Gpu              m_gpu = Rhi::Gpu::Auto;
        bool                  m_uiReady = false;
        const Kit::FrameCollector* m_pendingFrame = nullptr;
    };

    // =================================================================
    //  工厂
    // =================================================================
    //
    //  ★ 与鸿蒙那条的差异：苹果**只提供 Metal**。
    //    `Api::OpenGL`（macOS 上的兼容层，止步 4.1 且已 deprecated）
    //    在枚举里留着位置，但**没有后端实现** —— 返回 nullptr，
    //    让 Kit 的 ensureBackend() 走它既有的"没有可用后端"路径。
    //    为什么不实现 GL：苹果的 GL 是死路（Apple 已弃用、Metal 才是唯一有未来的），
    //    且实现它需要一套独立的 EGL/CGL 上下文 —— 收益低、维护贵。
    //    将来若真要兜底，照 `ZeRenderBackendHarmony.cpp` 的 `EglGlBackend` 补一个即可。
    IBackend* CreateBackend(Api api) {
        switch (api) {
        case Api::Metal:  return new MetalBackend();
        case Api::OpenGL: return nullptr;   // 见上：苹果不提供 GL 后端
        // 软件光栅化：装配是跨平台的（ZeRenderBackendSoft.cpp）；苹果的
        // "像素怎么上屏"（CGContext / CALayer contents）**尚未实现** ——
        // 因此这里的 CreateSoftwareBackend 会建出对象，但 Init 会如实失败并打印原因。
        case Api::Software: return CreateSoftwareBackend();
        }
        return nullptr;
    }

    void DestroyBackend(IBackend* backend) {
        delete backend;
    }

} // namespace Rhi

#endif // __APPLE__
