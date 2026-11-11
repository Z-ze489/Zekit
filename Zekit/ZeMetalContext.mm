// =====================================================================
//  ZeMetalContext.mm —— Metal 上下文实现（macOS / iOS）
// =====================================================================
//
//  ★★ 状态：**已写完整实现，但未在苹果设备上验证过，且平台当前被编译期封禁**
//  -------------------------------------------------------------------
//  本文件是"完整实现"而非骨架 —— 用户可以随时解封并拿到一份**能读、能审、
//  拿到 Mac 上大概率直接能跑**的代码。但它**从未被编译过**（本机是 Windows，
//  没装苹果 SDK），所以：
//    · 所有 API 用法按 Apple 官方文档 + Metal 编程指南写成；
//    · 但与 Vulkan/D3D12 那两份"跑过三遍"的实现相比，**它缺一次实机验证**。
//  解封后第一次上 Mac，请按 `Docs/21 macOS 移植方案.md` §五 的三个里程碑
//  逐个打勾，别指望一次全绿。
//
//  ★ 为什么是 `.mm` 而不是 `.cpp`
//  -------------------------------------------------------------------
//  它 `#import <Metal/Metal.h>` / `<QuartzCore/CAMetalLayer.h>`，用
//  `id<MTLDevice>` 这类 Objective-C 类型 —— 必须用 **Objective-C++** 编译。
//  Clang 对 `.mm` 自动开 ObjC++；对 `.cpp` 则会把 `id<...>` 当成语法错误。
//  （这也是苹果那几个后端文件都是 `.mm` 而不是 `.cpp` 的原因 ——
//    ZeRenderBackendApple.mm / ZeMetalContext.mm / ZeMetalUIRenderer.mm。）
//
//  ★ 与 ZeVulkanContext.cpp 的对应关系（想对照着读时按这个表跳）
//  -------------------------------------------------------------------
//    Vulkan 那边                      →  本文件
//    ------------------------------------------------------------------
//    createInstance() / pickPhysicalDevice() → pickDevice()
//    createDevice()                          → (Metal 里 device 自带队列，无此步)
//    createSurface()                         → init() 里认领 layer
//    createSwapchain()                       → setClientSize() 里设 drawableSize
//    createSyncObjects()                     → init() 里建 dispatch_semaphore
//    acquireImage()                          → nextDrawable
//    beginRenderPass()/endRenderPass()       → renderCommandEncoder 的建/收
//    queueSubmit() + queuePresent()          → addCompletedHandler + presentDrawable + commit
//    cleanup()                               → cleanup()
//    ------------------------------------------------------------------
//
//  ★ 五处 Vulkan 没有的 Metal 专有坑（都在下面注释里标了 ★）
// =====================================================================

#include "pch.h"

#if defined(__APPLE__)

#import "ZeMetalContext.h"

// ---- Kit 侧被"改道"的对象（只在帧回调里间接用到，这里其实不需要具体类型；
//      但 AttachTextureSinks 在 .mm 后端文件里，故本文件不 include 它们）----

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <cstdio>
#include <string>
#include <vector>

namespace Mtl {

    // =================================================================
    //  构造 / 析构
    // =================================================================
    MetalContext::~MetalContext() {
        cleanup();
    }

    // =================================================================
    //  设备挑选 —— 对应 Vulkan 的 pickPhysicalDevice
    // =================================================================
    //
    //  ★ 与 Vulkan 的**根本差异**：Vulkan 要先枚举 physical device、
    //    读它的 queue family、算 feature、再挑；Metal 这边
    //    `MTLCreateSystemDefaultDevice()` 一句话就给了"系统默认 GPU"。
    //
    //    多 GPU 的 Mac（Intel 机型：核显 + 独显）上，要按 Gpu 策略挑，
    //    必须用 `MTLCopyAllDevices()`（**注意**：iOS 上没有这个 API，
    //    那边恒为单 GPU，直接用 default 即可）。
    bool MetalContext::pickDevice(Rhi::Gpu gpu) {
        if (m_device != nil) return true;   // 幂等：preheat 已经建过了

#if TARGET_OS_IPHONE
        // iOS / iPadOS：单 GPU，`Gpu` 策略无意义（恒为那块统一内存 GPU）
        (void)gpu;
        m_device = MTLCreateSystemDefaultDevice();
#else
        // macOS：可能多 GPU。按策略挑。
        if (gpu == Rhi::Gpu::Auto || gpu == Rhi::Gpu::Discrete) {
            // 默认取系统默认（Apple Silicon 上就是唯一那块；
            // Intel 机型上系统默认是"性能优先"那块，正合 Auto 的语义）
            m_device = MTLCreateSystemDefaultDevice();
        }

        if (gpu == Rhi::Gpu::Integrated) {
            // ★ 想要低功耗：得枚举所有设备找 `lowPower` 的那块。
            //   `MTLCopyAllDevices` 在 macOS 10.13+ 可用。
            NSArray<id<MTLDevice>>* all = MTLCopyAllDevices();
            for (id<MTLDevice> d in all) {
                if (d.isLowPower) { m_device = d; break; }
            }
            if (m_device == nil) m_device = MTLCreateSystemDefaultDevice();
        }

        if (m_device == nil) m_device = MTLCreateSystemDefaultDevice();
#endif

        if (m_device == nil) {
            std::fprintf(stderr, "[Zekit] Metal：MTLCreateSystemDefaultDevice 返回 nil"
                                 "（这台机器没有可用 GPU？）\n");
            return false;
        }

        // ---- 建命令队列（Vulkan 是 vkGetDeviceQueue，Metal 是创建）----
        //  ★ 与 Vulkan 的差异：Vulkan 的队列是**设备创建时**从 queue family 里
        //    取出来的；Metal 的队列是**独立创建**的对象（`newCommandQueue`）。
        //    一个 device 可以建多个队列，本库只需要一个（图形队列）。
        m_queue = [m_device newCommandQueue];
        if (m_queue == nil) {
            std::fprintf(stderr, "[Zekit] Metal：newCommandQueue 失败\n");
            // ★ 不要写 `[m_device release]` —— 在 ARC 下这是**编译错误**
            //   （"release is unavailable: not available in automatic reference
            //   counting mode"）。Xcode 对 .mm 默认开 ARC，本项目按 ARC 写。
            //   置 nil 即可：ARC 会在此处插入 release。
            m_device = nil;
            return false;
        }
        return true;
    }

    // =================================================================
    //  预热 —— 对应 VulkanContext::preheat
    // =================================================================
    bool MetalContext::preheat(Rhi::Gpu gpu) {
        if (m_device != nil) return true;
        return pickDevice(gpu);
    }

    // =================================================================
    //  初始化 —— 建设备 + 认领 layer
    // =================================================================
    bool MetalContext::init(id<CAMetalLayer> layer, Rhi::Gpu gpu) {
        if (m_layer != nil && m_psoFormatReady) return true;   // 幂等

        if (layer == nil) {
            std::fprintf(stderr, "[Zekit] Metal：init 收到 nil layer（surface 还没就绪）\n");
            return false;
        }

        // ---- 设备（preheat 过就直接用）----
        if (!pickDevice(gpu)) return false;

        // ---- 认领 layer ----
        //  ★ 三件事，缺一不可：
        //    ① layer.device 指到我们的 device（不指 = drawable 是 nil）；
        //    ② pixelFormat 定成 BGRA8Unorm（**必须**与 UI 渲染器的管线格式一致，
        //       否则 encoder 建管线状态时断言失败）；
        //    ③ framebufferOnly = YES（默认值）—— 我们只当渲染目标用，不当纹理采样，
        //       设 YES 可以让驱动省掉一份拷贝。若将来要在 layer 上做后处理才设 NO。
        m_layer = layer;
        m_layer.device = m_device;
        m_layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        m_layer.framebufferOnly = YES;

        // ★★ 防"主线程卡死"的三条设置（见 ZeMetalContext.h 的说明 3）
        m_layer.maximumDrawableCount = kMaxFramesInFlight;
        // ⚠️ `allowsNextDrawableTimeout` 是 **macOS 10.13+ / iOS 11+** 的属性。
        //    低版本上编译不过 —— 本库的部署目标定在 macOS 13 / iOS 16，够用。
        m_layer.allowsNextDrawableTimeout = NO;

        // ---- 在飞帧信号量（对应 Vulkan 的 per-frame fence）----
        //  ★ 初值 = kMaxFramesInFlight：允许最多 3 帧同时在飞。
        //    Vulkan 那边是建 3 个 fence（每个对应一帧，wait 那个具体的）；
        //    Metal 这边用一个计数信号量就够（谁画完了就 signal 谁）。
        if (m_inFlight == nil) {
            m_inFlight = dispatch_semaphore_create(kMaxFramesInFlight);
        }

        m_psoFormatReady = true;
        return true;
    }

    // =================================================================
    //  尺寸 —— 对应 VulkanContext::setClientSize / recreateSwapchain
    // =================================================================
    //
    //  ★ 这里比 Vulkan **简单一大截**：
    //    Vulkan 换尺寸要 `vkDeviceWaitIdle` + 销毁旧 swapchain + 重建 imageView /
    //    render pass / framebuffer —— 一大串。
    //    Metal 只要把 `layer.drawableSize` 一改，**下一次 nextDrawable 就是新尺寸**，
    //    没有对象要重建（因为 drawable 由 layer 按需分配）。
    //    这也是 MetalContext 没有 `RetiredSwapchain` 那套延迟销毁的原因。
    bool MetalContext::setClientSize(int widthPx, int heightPx) {
        if (m_layer == nil) return false;
        if (widthPx <= 0 || heightPx <= 0) return false;
        if (widthPx == m_pxW && heightPx == m_pxH) return false;   // 无变化

        m_pxW = widthPx;
        m_pxH = heightPx;
        m_layer.drawableSize = CGSizeMake(widthPx, heightPx);
        return true;   // 尺寸变了
    }

    // =================================================================
    //  画一帧 —— 对应 VulkanContext::drawFrame
    // =================================================================
    bool MetalContext::drawFrame(float r, float g, float b) {
        if (!isReady()) return false;
        if (m_pxW <= 0 || m_pxH <= 0) return false;

        // ---- ① 等在飞帧腾出位置 ----
        //  ★ 对应 Vulkan 的 `vkWaitForFences(frame.fence)`。
        //    与 Vulkan 不同的是：这里**不会真正等很久** —— 因为下面
        //    `addCompletedHandler` 里会 signal，正常情况下一帧几毫秒就回来了。
        //    超时设 DISPATCH_TIME_FOREVER 是安全的（对端一定会 signal）。
        dispatch_semaphore_wait(m_inFlight, DISPATCH_TIME_FOREVER);

        // ---- ② 取 drawable ----
        //  ★★ 本函数唯一可能"没图可画"的地方。`allowsNextDrawableTimeout=NO`
        //     让我们在超时（默认 1 秒）时拿到 nil 而不是卡死 —— 这时**直接跳过本帧**。
        //     Vulkan 的对应物是 `vkAcquireNextImageKHR` 返回 VK_NOT_READY/VK_TIMEOUT。
        @autoreleasepool {
            id<CAMetalDrawable> drawable = [m_layer nextDrawable];
            if (drawable == nil) {
                dispatch_semaphore_signal(m_inFlight);   // 归还位置，别把信号量漏掉
                return false;   // 本帧不出图（窗口被遮挡 / 后台 / 尺寸为 0）
            }

            // ---- ③ 建命令缓冲 ----
            //  ★ Metal 的 command buffer **一次性**（见头文件说明 2）：
            //    每帧都要新申请一个。不要缓存、不要复用（除了 enqueue 的例外用法）。
            id<MTLCommandBuffer> cmd = [m_queue commandBuffer];
            if (cmd == nil) {
                dispatch_semaphore_signal(m_inFlight);
                return false;
            }

            // 为"这一帧画完了"挂信号量归还（对应 Vulkan 的 fence signal）
            __block dispatch_semaphore_t inFlight = m_inFlight;
            [cmd addCompletedHandler:^(id<MTLCommandBuffer> /*cb*/) {
                dispatch_semaphore_signal(inFlight);
            }];

            // ---- ④ 建渲染通道描述符（清屏色在这里）----
            MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
            rp.colorAttachments[0].texture     = drawable.texture;
            rp.colorAttachments[0].loadAction  = MTLLoadActionClear;
            rp.colorAttachments[0].storeAction = MTLStoreActionStore;
            // 清屏色用线性 0~1（与主题 token 同量纲，与其它三个后端一致）
            rp.colorAttachments[0].clearColor  = MTLClearColorMake(r, g, b, 1.0f);

            // ---- ⑤ 建编码器 ----
            id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:rp];
            if (enc == nil) {
                [cmd commit];
                return false;
            }

            // ---- ⑥ 调帧回调（UI 渲染器在这里编码绘制命令）----
            //  ★ 对应 Vulkan 的 `setFrameRecorder` 在 render pass 内被调起。
            //    与 Vulkan 的差异：Vulkan 那边回调拿到的 FrameInfo 里
            //    cmd 是裸的 VkCommandBuffer（render pass 已经 begin）；
            //    这里给的是已 startEncoding 的 encoder。
            if (m_recorder) {
                FrameInfo fi;
                fi.encoder  = enc;
                fi.drawable = drawable;
                fi.width    = static_cast<uint32_t>(m_pxW);
                fi.height   = static_cast<uint32_t>(m_pxH);
                // 帧槽：用 drawable 的轮转序号摸不到，简单用 (帧计数 % N)。
                // UI 渲染器用它选顶点缓冲环的槽位（避免覆盖还在被读的缓冲）。
                fi.slot = m_frameCounter % kMaxFramesInFlight;
                ++m_frameCounter;
                // 回调里**不要抛异常**：这里兜一层，宁可跳帧也别让 UI 线程 unwind
                @try {
                    m_recorder(fi);
                } @catch (NSException* e) {
                    std::fprintf(stderr, "[Zekit] Metal 帧回调抛异常，跳过本帧：%s\n",
                                 [[e reason] UTF8String]);
                }
            }

            // ---- ⑦ 收编码器 ----
            [enc endEncoding];

            // ---- ⑧ present + commit ----
            //  ★ 与 Vulkan 的差异：Vulkan 是 `vkQueuePresentKHR` 单独一次调用；
            //    Metal 是**挂**在 command buffer 上（presentDrawable），
            //    随 commit 一起生效。这一句就等价于 Vulkan 的 queuePresent。
            [cmd presentDrawable:drawable];
            [cmd commit];
            // ⚠️ 故意**不** `[cmd waitUntilCompleted]` —— 那是同步等待，会卡住调用线程。
            //    同步点由上面的 inFlight 信号量保证（下一帧取位置时自然等）。
        }
        return true;
    }

    // =================================================================
    //  设备信息 —— 对应 VulkanContext::deviceInfo
    // =================================================================
    Rhi::DeviceInfo MetalContext::deviceInfo() const {
        Rhi::DeviceInfo info{};
        if (m_device == nil) return info;   // valid=false

        info.valid = true;
        // ★ Metal 没有 `VkPhysicalDeviceType` 那种枚举，判断集显/独显用
        //   `isLowPower` / `isRemovable` / `hasUnifiedMemory`（都是 MTLDevice 属性）。
        //   命名规则：lowPower = 集显；否则视为独显（Apple Silicon 是统一内存，
        //   严格说不是"独显"，但它性能足够，归到 Discrete 一侧更符合直觉）。
        info.discrete = !m_device.isLowPower;

        // 设备名（NSString → Rhi::DeviceInfo::name 的 char[160]）。
        // `name` 属性 macOS 10.13+ / iOS 11+ 都有。
        // ★ 不能写 `info.name = [name UTF8String]` —— name 是**定长 char 数组**
        //   （见 include/ZeRenderBackend.h 的说明：跨库边界不给 std::string），
        //   数组不能整体赋值。用 snprintf 拷进去并保证截断安全。
        NSString* name = m_device.name;
        const char* utf8 = (name != nil) ? [name UTF8String] : nullptr;
        std::snprintf(info.name, sizeof(info.name), "%s",
                      (utf8 != nullptr) ? utf8 : "Metal Device");

        // 驱动信息：Metal 没有"驱动版本"概念，填系统 + Metal 家族名，
        // 让 deviceInfo() 的输出与其它后端一样有内容可看。
        std::snprintf(info.driver, sizeof(info.driver), "Metal (%s)",
                      [m_device hasUnifiedMemory] ? "unified memory" : "discrete memory");

        std::snprintf(info.api, sizeof(info.api), "%s", "Metal");
        return info;
    }

    // =================================================================
    //  清理 —— 对应 VulkanContext::cleanup
    // =================================================================
    void MetalContext::cleanup() {
        m_recorder = nullptr;

        // ★ 顺序：先断 layer 对 device 的引用，再放队列，最后放设备。
        //   与 Vulkan 的"先 idle、再销毁 swapchain、再 device、再 instance"同理 ——
        //   把"依赖别人的"先放掉，别留悬空引用。
        if (m_layer != nil) {
            // 让 layer 不再指向我们的 device（避免 device 释放后 layer 还拿着它）
            m_layer.device = nil;
            m_layer = nil;
        }
        m_psoFormatReady = false;

        // ---- 等在飞帧全部画完再放设备 ----
        //  ★ 对应 Vulkan 的 `vkDeviceWaitIdle`。
        //    不做这一步就释放 device，正在飞的 command buffer 会拿到废止的
        //    资源 → 崩溃或画面异常（而且往往在别处炸，很难查）。
        if (m_inFlight != nil) {
            for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
                dispatch_semaphore_wait(m_inFlight, DISPATCH_TIME_FOREVER);
            }
            // 到这里说明 3 个位置都空着 = 所有 command buffer 都画完了
            m_inFlight = nil;
        }

        if (m_queue != nil) {
            // ★ ARC 下**不能**写 `[m_queue release]`（编译错误，见 pickDevice 的说明）。
            //   置 nil = ARC 在此插入 release；队列是强引用属性，这样够了。
            m_queue = nil;
        }
        if (m_device != nil) {
            m_device = nil;
        }

        m_pxW = m_pxH = 0;
        m_uiW = m_uiH = 0;
    }

} // namespace Mtl

#endif // __APPLE__
