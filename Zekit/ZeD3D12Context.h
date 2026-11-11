#pragma once
#include <d3d12.h>
#include <dxgi1_6.h>
#include <dcomp.h>
#include <wrl/client.h>
#include <Windows.h>

#include "include/ZeRenderBackend.h"   // Rhi::Gpu / Rhi::DeviceInfo -- device policy and observation

#include <cstdint>
#include <functional>
#include <vector>

namespace Dx {
	using Microsoft::WRL::ComPtr;

	class D3D12Context {
	public:
  // One frame context
        struct FrameInfo {
            ID3D12GraphicsCommandList* cmd = nullptr;    /// < command list already open (inside a render pass)
            ID3D12CommandAllocator* allocator = nullptr;
            uint32_t   imageIndex = 0;                   /// < current back buffer index
            uint32_t   frameSlot = 0;                   /// < frame slot 0 ~ kFramesInFlight-1
            uint32_t   width = 0;
            uint32_t   height = 0;
            DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
        };

        /// Frame callback: called between ClearRenderTargetView and EndRenderPass
        using FrameRecordFn = std::function<void(const FrameInfo&)>;

        D3D12Context() = default;
        ~D3D12Context();

        D3D12Context(const D3D12Context&) = delete;
        D3D12Context& operator=(const D3D12Context&) = delete;

        // Initialization
        // @brief Builds a complete device + swap chain
        // @param gpu Device policy (`Rhi::Gpu`). Must be supplied before the adapter is created --
        //        the adapter is chosen inside this function, so setting it afterwards is too late.
        void init(HINSTANCE hinstance, HWND hwnd, Rhi::Gpu gpu = Rhi::Gpu::Auto);

        /// @brief Shares device / command queue with another context, creates only a swap chain for this window
        /// @return false = shared source unavailable (not built), caller falls back to an independent device
        ///
        /// @details
        /// Child windows take this path (see the contract of `IBackend::CreateShared`):
        ///   - Shared: `IDXGIFactory` / `IDXGIAdapter` / `ID3D12Device` /
        ///     `ID3D12CommandQueue` (all ComPtr, each holds its own reference, either may Release first);
        ///   - Built separately: swap chain / RTV heap / command allocators and lists / fence.
        /// The benefit is twofold: saves a whole block of VRAM and one device initialization; more importantly,
        /// there is only one D3D12 device in the process -- Intel's shader compiler (IGC, written as
        /// `MONZA` in logs) throws an internal C++ exception on the path of "creating a second device in the
        /// same process and compiling the pipeline again" (`MONZA::IgcThreadingContext<AdapterTraits_Gen12LP>::msg_end`),
        /// that is a driver bug, and the only way around it is to not create a second device.
        bool initShared(HINSTANCE hinstance, HWND hwnd, const D3D12Context& share);

        void cleanup();

        bool isReady() const { return m_swapchain != nullptr; }

        /// @brief Whether the swap chain really takes the per-pixel transparent path (DComp visual has been created)
        /// @details
        /// All three conditions must hold:
        ///   1. `setClearAlpha(0)` requested transparency;
        ///   2. `dcomp.dll` exists on the system (`ResolveDCompositionCreateDevice` succeeds);
        ///   3. `CreateSwapChainForComposition` + `CreateVisual` +
        ///      `SetRoot` above all succeed -- that is, `m_dcompVisual` is non-null.
        /// If any is missing, the swap chain degrades to `DXGI_ALPHA_MODE_IGNORE` (alpha ignored by DWM),
        /// and in that case the upper layer must not remove the window's redirection surface, or the whole thing disappears.
        bool isPerPixelTransparent() const { return m_dcompVisual != nullptr; }

        /// @brief The render device actually used (populated only after the device is built)
        /// @note The D3D12 layer has no concept of "discrete GPU" -- DXGI's adapter description only has
        ///       `DedicatedVideoMemory` and `VendorId`, and `isDiscrete` is inferred from these two
        ///       (see `classifyIntegrated` in `ZeD3D12Context.cpp`).
        ///       The driver version is not in DXGI (only queryable from the registry), so `driver` is left empty.
        Rhi::DeviceInfo deviceInfo() const;

        // Draws one frame
        bool drawFrame(float r, float g, float b);

        /// @brief Sets the clear alpha (0 = transparent background, 1 = opaque; default 1)
        /// @details Must be called before `init` / `initShared` -- it determines the swap chain's
        ///        `AlphaMode` (transparent windows use `DXGI_ALPHA_MODE_STRAIGHT`, normal windows use
        ///        `IGNORE`), and AlphaMode can only be set at `CreateSwapChainForHwnd` time,
        ///        `ResizeBuffers` cannot change it afterwards (same meaning as GL's "enable alpha bit in pixel format").
        ///        It is also the alpha component of the per-frame `ClearRenderTargetView` clear color.
        void setClearAlpha(float alpha) { m_clearAlpha = (alpha <= 0.0f) ? 0.0f : 1.0f; }

        void waitIdle();

        void setFrameRecorder(FrameRecordFn fn) { m_recordFn = std::move(fn); }

        /// @brief Swap interval for this frame (`0` = do not wait for vblank / `1` = wait once)
        /// @details Kit issues this every frame: interactive frames 0, idle frames 1. Waiting for vblank blocks the UI thread
        ///          for up to 16.7ms, which is the direct source of "lagging behind the mouse" while dragging (measured paint is only
        ///          0.07ms, Present alone takes 14.7ms). In windowed mode Present(0) is composited by DWM
        ///          uniformly and does not tear -- the same reason as the existing handling when dragging the border.
        void setSwapInterval(int interval) { m_syncInterval = (interval <= 0) ? 0 : 1; }

        /// @brief Whether this frame deferred the swap chain rebuild (= window size is being changed continuously)
        /// @details Kit uses this to schedule a "draw later", so that after the size stabilizes it can draw one more frame
        ///          at the exact size (otherwise the image stays in a stretched state).
        bool resizeDeferred() const { return m_resizeDeferred; }
        // ---- Accessors ----
        ID3D12Device* device()       const { return m_device.Get(); }
        ID3D12CommandQueue* commandQueue() const { return m_commandQueue.Get(); }
        IDXGISwapChain4* swapchain()    const { return m_swapchain.Get(); }
        DXGI_FORMAT         swapchainFormat() const { return m_swapFormat; }
        uint32_t            framesInFlight() const { return kFramesInFlight; }
        uint32_t            rtvDescriptorSize() const { return m_rtvDescriptorSize; }
        ID3D12DescriptorHeap* rtvHeap() const { return m_rtvHeap.Get(); }

        /// Gets the RTV of the specified back buffer (used by the UI renderer when drawing to the current frame)
        D3D12_CPU_DESCRIPTOR_HANDLE rtvFor(uint32_t imageIndex) const;

        /// Gets the resource of the current back buffer (for layout transitions / creating SRVs)
        ID3D12Resource* backBuffer(uint32_t imageIndex) const;

    private:
        // ---- Staged initialization
        void createFactory();
        /// @brief Picks the adapter according to the `Rhi::Gpu` policy (the policy is applied here, not judged elsewhere)
        void pickAdapter(Rhi::Gpu gpu);
        void createDevice();
        void createCommandQueue();
        void createSwapchain();
        void createRTVs();
        void createCommandObjects();
        void createFence();

        void cleanupSwapchain();
        bool recreateSwapchain();

        /// @brief Swap chain buffer count = frame slot count
        /// @details 3 rather than 2: under the flip model, `Present(0)` (no vblank wait) returns immediately only when there is still
        ///   a free buffer; with 2 buffers at most 1 frame is queued, and the second `Present(0)`
        ///   blocks until DWM takes a frame -- behaving exactly like vsync on (measured 15ms/frame,
        ///   while drawing the frame content takes only 0.07ms). 3 buffers give one slot of headroom between "queued" and "on screen",
        ///   so interactive frames really do not wait.
        static constexpr uint32_t kFramesInFlight = 3;

        HINSTANCE m_hinstance = nullptr;
        HWND      m_hwnd = nullptr;

        // ---- Device family ----
        ComPtr<IDXGIFactory6>       m_factory;
        ComPtr<IDXGIAdapter1>       m_adapter;
        ComPtr<ID3D12Device>        m_device;
        ComPtr<ID3D12CommandQueue>  m_commandQueue;

        // ---- Swap chain family ----
        ComPtr<IDXGISwapChain4>     m_swapchain;
        ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
        uint32_t                    m_rtvDescriptorSize = 0;
        std::vector<ComPtr<ID3D12Resource>> m_backBuffers;
        DXGI_FORMAT                 m_swapFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        uint32_t                    m_width = 0;
        uint32_t                    m_height = 0;

        // ---- DirectComposition (for transparent child windows only) ----
        //  * DXGI rule: alpha-blended swap chains (STRAIGHT / PREMULTIPLIED) cannot use
        //    `CreateSwapChainForHwnd` (it returns DXGI_ERROR_INVALID_CALL); they must use
        //    `CreateSwapChainForComposition` + DirectComposition bound to the HWND.
        //    Only transparent windows with clearAlpha=0 take this path; normal windows still use the HWND swap chain.
        ComPtr<IDCompositionDevice> m_dcompDevice;   /// < DComp device (host of the composition swap chain)
        ComPtr<IDCompositionTarget> m_dcompTarget;   /// < composition target bound to the HWND
        ComPtr<IDCompositionVisual> m_dcompVisual;   /// < visual node carrying the swap chain content

        // ---- Command family ----
        std::vector<ComPtr<ID3D12CommandAllocator>> m_allocators;
        ComPtr<ID3D12GraphicsCommandList> m_commandList;

        // ---- Synchronization family ----
        ComPtr<ID3D12Fence> m_fence;
        /// @brief The global fence value corresponding to the last submission of each frame slot (0 = that slot has not submitted yet)
        ///
        /// Semantics (important): this records a value, not a counter. Global progress is
        /// represented by the monotonically increasing m_globalFenceValue; this array only answers "which number the frame using this slot is at".
        ///   - per-slot wait: GetCompletedValue() < m_fenceValues[slot]  -> that slot's work is done
        ///   - queue drain:   GetCompletedValue() < m_globalFenceValue    -> all work is done
        /// If, as earlier, each slot incremented its own value, the two slots would climb to unrelated values,
        /// and waitIdle using a single slot as the global value would be less than the real progress -> waiting for nothing.
        std::vector<uint64_t> m_fenceValues;
        uint64_t m_globalFenceValue = 0;   /// < globally monotonically increasing fence value (total queue progress)
        HANDLE m_fenceEvent = nullptr;
        uint32_t m_frameIndex = 0;

        FrameRecordFn m_recordFn;

        /// @brief Swap interval for this frame (issued by Kit every frame; see setSwapInterval)
        int m_syncInterval = 1;

        /// @brief Clear alpha (0 = transparent background, 1 = opaque; see setClearAlpha)
        /// @details Two uses: `createSwapchain` uses it to pick AlphaMode; `drawFrame` uses it
        ///       as the alpha component of the clear color. Both must share the same source, otherwise "swap chain wants transparency but clear writes
        ///        1.0" produces a fully opaque rectangle.
        float m_clearAlpha = 1.0f;

        // ---- Interactive resize: do not rebuild the swap chain while dragging ----
        //  `ResizeBuffers` waits for one composition cycle each time (measured 6~13ms, about one frame at 120Hz),
        //  while the mouse advances every 8ms -- rebuilding every frame would stall the system's drag modal loop
        //  for that long, and the window border itself cannot keep up with the mouse. See the explanation in drawFrame for the fix.
        bool     m_resizeDeferred = false;
        uint64_t m_lastSizeChangeMs = 0;   /// < time the client area size was last seen changing (steady milliseconds)
        uint64_t m_lastRecreateMs = 0;     /// < time of the last actual ResizeBuffers (fallback for long drags)
        uint32_t m_lastClientW = 0;        /// < client area size seen in the previous frame (to detect "changed again")
        uint32_t m_lastClientH = 0;

        /// @brief If the size is still changing within this many milliseconds, the user is considered to be dragging the border (use "deferred rebuild")
        /// @details The value must be greater than one frame's composition cycle (120Hz ~ 8.3ms) and less than the latency
        ///          perceivable after the user releases. 80ms ~ 10 composition cycles: normal dragging (125Hz mouse)
        ///          never has an 80ms gap, and after release the exact-size frame is filled in within at most 80ms.
        static constexpr uint64_t kResizeSettleMs = 80;

        /// @brief If dragging continues beyond this long, force one swap chain rebuild (otherwise it stays stretched to the old buffer)
        /// @details 800ms is the trade-off of "rather slightly blurry than stuttering" -- see the measurements in drawFrame in the .cpp.
        static constexpr uint64_t kResizeHardRefreshMs = 800;

        // Debug layer (enabled only in Debug builds, equivalent to your validation layers)
#ifdef _DEBUG
        ComPtr<ID3D12Debug> m_debugController;
#endif
	};
}