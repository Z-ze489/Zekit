#include "pch.h"
#include "ZeD3D12Context.h"

#include <chrono>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <iostream>

namespace Dx {
    namespace {
        inline void ThrowIfFailed(HRESULT hr, const char* what) {
            if (FAILED(hr)) {
                char buf[256];
                sprintf_s(buf, sizeof(buf), "D3D12 error [%s]: HRESULT = 0x%08X",
                          what, static_cast<unsigned>(hr));
                throw std::runtime_error(buf);
            }
        }

        using PFN_DCompositionCreateDevice =
            HRESULT(WINAPI*)(IUnknown*, REFIID, void**);

        inline PFN_DCompositionCreateDevice ResolveDCompositionCreateDevice() {
            static PFN_DCompositionCreateDevice fn = [] {
                HMODULE m = ::LoadLibraryW(L"dcomp.dll");
                if (!m) return static_cast<PFN_DCompositionCreateDevice>(nullptr);
                return reinterpret_cast<PFN_DCompositionCreateDevice>(
                    ::GetProcAddress(m, "DCompositionCreateDevice"));
            }();
            return fn;
        }

        inline const char* HResultName(HRESULT hr) {
            switch (hr) {
            case DXGI_ERROR_DEVICE_REMOVED:      return "DEVICE_REMOVED";
            case DXGI_ERROR_DEVICE_RESET:        return "DEVICE_RESET";
            case DXGI_ERROR_DEVICE_HUNG:         return "DEVICE_HUNG";
            case DXGI_ERROR_UNSUPPORTED:         return "UNSUPPORTED";
            case DXGI_ERROR_INVALID_CALL:        return "INVALID_CALL";
            case DXGI_ERROR_WAS_STILL_DRAWING:   return "WAS_STILL_DRAWING";
            case E_OUTOFMEMORY:                  return "OUT_OF_MEMORY";
            default:                             return "unknown";
            }
        }
    }

    D3D12Context::~D3D12Context() { cleanup(); }

    namespace {
        constexpr UINT kVendorIntel = 0x8086;
        constexpr SIZE_T kIntegratedVideoMemLimit = 1024ull * 1024 * 1024;

        bool classifyIntegrated(const DXGI_ADAPTER_DESC1& d) {
            if (d.DedicatedVideoMemory == 0) return true;
            if (d.VendorId == kVendorIntel && d.DedicatedVideoMemory < kIntegratedVideoMemLimit)
                return true;
            return false;
        }

        bool canCreateD3D12(IDXGIAdapter1* a) {
            return SUCCEEDED(D3D12CreateDevice(a, D3D_FEATURE_LEVEL_12_0,
                __uuidof(ID3D12Device), nullptr));
        }
    }

    void D3D12Context::init(HINSTANCE hinstance, HWND hwnd, Rhi::Gpu gpu) {
        m_hinstance = hinstance;
        m_hwnd = hwnd;

        try {
            createFactory();
            pickAdapter(gpu);
            createDevice();
            createCommandQueue();
            createSwapchain();
            createRTVs();
            createCommandObjects();
            createFence();
        }
        catch (...) {
            cleanup();
            throw;
        }
    }

    bool D3D12Context::initShared(HINSTANCE hinstance, HWND hwnd, const D3D12Context& share) {
        if (!share.isReady() || !share.m_device || !share.m_commandQueue || !share.m_factory)
            return false;

        m_hinstance = hinstance;
        m_hwnd = hwnd;

        if (const char* v = std::getenv("ZEKIT_TRACE_LIFECYCLE");
            v && *v != '\0' && *v != '0') {
            DXGI_ADAPTER_DESC1 d{};
            share.m_adapter->GetDesc1(&d);
            std::cerr << "[Zekit/trace] D3D12 子窗口共享父窗口设备（不再建第二个设备），适配器="
                      << "0x" << std::hex << d.VendorId << std::dec << "\n";
        }

        try {
            m_factory      = share.m_factory;
            m_adapter      = share.m_adapter;
            m_device       = share.m_device;
            m_commandQueue = share.m_commandQueue;

            createSwapchain();
            createRTVs();
            createCommandObjects();
            createFence();
        }
        catch (...) {
            cleanup();
            return false;
        }
        return true;
    }

    void D3D12Context::createFactory() {
#ifdef _DEBUG
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&m_debugController)))) {
            m_debugController->EnableDebugLayer();
        }
#endif

        UINT flags = 0;
#ifdef _DEBUG
        flags |= DXGI_CREATE_FACTORY_DEBUG;
#endif

        ThrowIfFailed(
            CreateDXGIFactory2(flags, IID_PPV_ARGS(&m_factory)),
            "CreateDXGIFactory2");
    }

    void D3D12Context::pickAdapter(Rhi::Gpu gpu) {

        if (gpu == Rhi::Gpu::Auto) {
            for (UINT i = 0; ; ++i) {
                ComPtr<IDXGIAdapter1> candidate;
                if (FAILED(m_factory->EnumAdapterByGpuPreference(
                    i,
                    DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                    IID_PPV_ARGS(&candidate)))
                    || !candidate)
                    break;

                DXGI_ADAPTER_DESC1 desc{};
                candidate->GetDesc1(&desc);
                if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
                if (!canCreateD3D12(candidate.Get())) continue;

                m_adapter = candidate;
                return;
            }
        }

        std::vector<ComPtr<IDXGIAdapter1>> usable;
        for (UINT i = 0; ; ++i) {
            ComPtr<IDXGIAdapter1> candidate;
            if (FAILED(m_factory->EnumAdapters1(i, &candidate)) || !candidate)
                break;

            DXGI_ADAPTER_DESC1 desc{};
            candidate->GetDesc1(&desc);
            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
            if (!canCreateD3D12(candidate.Get())) continue;
            usable.push_back(candidate);
        }

        if (usable.empty()) {
            throw std::runtime_error(
                "No suitable D3D12 adapter found (all adapters are software or < FL 12_0)");
        }

        if (usable.size() == 1) {
            m_adapter = usable.front();
            return;
        }

        const bool wantIntegrated = (gpu == Rhi::Gpu::Integrated);
        SIZE_T bestMem = wantIntegrated ? SIZE_MAX : 0;
        for (const auto& a : usable) {
            DXGI_ADAPTER_DESC1 desc{};
            a->GetDesc1(&desc);
            const SIZE_T mem = desc.DedicatedVideoMemory;
            const bool better = wantIntegrated ? (mem < bestMem) : (mem > bestMem);
            if (better) { bestMem = mem; m_adapter = a; }
        }
        if (!m_adapter) m_adapter = usable.front();
    }

    void D3D12Context::createDevice() {
        ThrowIfFailed(
            D3D12CreateDevice(
                m_adapter.Get(),
                D3D_FEATURE_LEVEL_12_0,
                IID_PPV_ARGS(&m_device)),
            "D3D12CreateDevice");

#ifdef _DEBUG
        ComPtr<ID3D12InfoQueue> infoQueue;
        if (SUCCEEDED(m_device.As(&infoQueue))) {
            infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
            infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
        }
#endif
    }

    void D3D12Context::createCommandQueue() {
        D3D12_COMMAND_QUEUE_DESC desc{};
        desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
        desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

        ThrowIfFailed(
            m_device->CreateCommandQueue(&desc, IID_PPV_ARGS(&m_commandQueue)),
            "CreateCommandQueue");
    }

    void D3D12Context::createSwapchain() {
        RECT rc{};
        GetClientRect(m_hwnd, &rc);
        m_width = static_cast<uint32_t>(rc.right - rc.left);
        m_height = static_cast<uint32_t>(rc.bottom - rc.top);
        if (m_width == 0 || m_height == 0) {
            m_width = m_height = 1;
        }

        DXGI_FORMAT wantFormat = DXGI_FORMAT_B8G8R8A8_UNORM;

        const PFN_DCompositionCreateDevice createDComp = ResolveDCompositionCreateDevice();

        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = m_width;
        desc.Height = m_height;
        desc.Format = wantFormat;
        desc.Stereo = FALSE;
        desc.SampleDesc = { 1, 0 };
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = kFramesInFlight;

        const bool composition = (m_clearAlpha <= 0.0f) && (createDComp != nullptr);

        if (const char* v = std::getenv("ZEKIT_TRACE_SWAPCHAIN"); v && *v != '\0' && *v != '0') {
            std::cerr << "[Zekit/swapchain] hwnd=" << m_hwnd
                      << " " << m_width << "x" << m_height
                      << " clearAlpha=" << m_clearAlpha
                      << " composition=" << (composition ? 1 : 0)
                      << " dcomp=" << (createDComp ? "yes" : "no") << "\n";
        }

        if (composition) {
            desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
            desc.Scaling    = DXGI_SCALING_STRETCH;
            desc.AlphaMode  = DXGI_ALPHA_MODE_PREMULTIPLIED;
            desc.Flags      = 0;
        } else {
            desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            desc.AlphaMode  = DXGI_ALPHA_MODE_IGNORE;
            desc.Flags      = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
        }

        ComPtr<IDXGISwapChain1> swapchain1;

        if (composition) {
            ThrowIfFailed(
                m_factory->CreateSwapChainForComposition(
                    m_commandQueue.Get(), &desc, nullptr, &swapchain1),
                "CreateSwapChainForComposition");

            ThrowIfFailed(
                createDComp(nullptr, IID_PPV_ARGS(&m_dcompDevice)),
                "DCompositionCreateDevice");
            ThrowIfFailed(
                m_dcompDevice->CreateTargetForHwnd(m_hwnd, TRUE, &m_dcompTarget),
                "DComp::CreateTargetForHwnd");
            ThrowIfFailed(
                m_dcompDevice->CreateVisual(&m_dcompVisual),
                "DComp::CreateVisual");
            ThrowIfFailed(
                m_dcompVisual->SetContent(swapchain1.Get()),
                "DComp::Visual::SetContent");
            ThrowIfFailed(
                m_dcompTarget->SetRoot(m_dcompVisual.Get()),
                "DComp::Target::SetRoot");
            ThrowIfFailed(
                m_dcompDevice->Commit(),
                "DComp::Commit");
        } else {
            ThrowIfFailed(
                m_factory->CreateSwapChainForHwnd(
                    m_commandQueue.Get(),
                    m_hwnd,
                    &desc,
                    nullptr,
                    nullptr,
                    &swapchain1),
                "CreateSwapChainForHwnd");

            m_factory->MakeWindowAssociation(m_hwnd, DXGI_MWA_NO_ALT_ENTER);
        }

        swapchain1.As(&m_swapchain);
        {
            DXGI_SWAP_CHAIN_DESC1 actual{};
            ThrowIfFailed(m_swapchain->GetDesc1(&actual), "SwapChain::GetDesc1");
            m_swapFormat = actual.Format;
        }

        m_backBuffers.resize(kFramesInFlight);
        for (uint32_t i = 0; i < kFramesInFlight; ++i) {
            ThrowIfFailed(
                m_swapchain->GetBuffer(i, IID_PPV_ARGS(&m_backBuffers[i])),
                "SwapChain::GetBuffer");
        }
    }

    void D3D12Context::createRTVs() {
        D3D12_DESCRIPTOR_HEAP_DESC desc{};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        desc.NumDescriptors = kFramesInFlight;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        desc.NodeMask = 0;

        ThrowIfFailed(
            m_device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_rtvHeap)),
            "CreateDescriptorHeap(RTV)");

        m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        for (uint32_t i = 0; i < kFramesInFlight; ++i) {
            D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
            rtvDesc.Format = m_swapFormat;
            rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
            rtvDesc.Texture2D.MipSlice = 0;
            rtvDesc.Texture2D.PlaneSlice = 0;

            m_device->CreateRenderTargetView(m_backBuffers[i].Get(), &rtvDesc, rtv);

            rtv.ptr += m_rtvDescriptorSize;
        }
    }

    void D3D12Context::createCommandObjects() {

        m_allocators.resize(kFramesInFlight);
        for (uint32_t i = 0; i < kFramesInFlight; ++i) {
            ThrowIfFailed(
                m_device->CreateCommandAllocator(
                    D3D12_COMMAND_LIST_TYPE_DIRECT,
                    IID_PPV_ARGS(&m_allocators[i])),
                "CreateCommandAllocator");
        }

        ThrowIfFailed(
            m_device->CreateCommandList(
                0,
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                m_allocators[0].Get(),
                nullptr,
                IID_PPV_ARGS(&m_commandList)),
            "CreateCommandList");

        ThrowIfFailed(m_commandList->Close(), "CommandList::Close(initial)");
    }

    void D3D12Context::createFence() {

        ThrowIfFailed(
            m_device->CreateFence(
                0,
                D3D12_FENCE_FLAG_NONE,
                IID_PPV_ARGS(&m_fence)),
            "CreateFence");

        m_fenceValues.assign(kFramesInFlight, 0);

        m_fenceEvent = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
        if (!m_fenceEvent) {
            throw std::runtime_error("CreateEventEx for fence failed");
        }
    }

    void D3D12Context::waitIdle() {
        if (!m_commandQueue || !m_fence) return;

        const uint64_t target = ++m_globalFenceValue;
        ThrowIfFailed(
            m_commandQueue->Signal(m_fence.Get(), target),
            "Queue::Signal(waitIdle)");

        if (m_fence->GetCompletedValue() < target) {
            ThrowIfFailed(
                m_fence->SetEventOnCompletion(target, m_fenceEvent),
                "Fence::SetEventOnCompletion");
            WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);
        }

        m_fenceValues.assign(kFramesInFlight, target);
    }

    Rhi::DeviceInfo D3D12Context::deviceInfo() const {
        Rhi::DeviceInfo info;
        if (!m_device || !m_adapter) return info;

        DXGI_ADAPTER_DESC1 desc{};
        if (FAILED(m_adapter->GetDesc1(&desc))) return info;

        WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1,
                            info.name, static_cast<int>(sizeof(info.name)), nullptr, nullptr);
        strcpy_s(info.driver, sizeof(info.driver), "");
        strcpy_s(info.api, sizeof(info.api), "D3D12");
        info.isDiscrete = !classifyIntegrated(desc);
        info.valid      = true;
        return info;
    }

    void D3D12Context::cleanup() {
        if (m_device) {
            waitIdle();

            m_backBuffers.clear();

            if (m_fenceEvent) { CloseHandle(m_fenceEvent); m_fenceEvent = nullptr; }
            m_fence.Reset();
            m_commandList.Reset();
            m_allocators.clear();
            m_rtvHeap.Reset();

            if (m_dcompTarget && m_dcompVisual)
                m_dcompTarget->SetRoot(nullptr);
            m_dcompVisual.Reset();
            m_dcompTarget.Reset();
            m_dcompDevice.Reset();

            m_swapchain.Reset();
            m_commandQueue.Reset();
            m_device.Reset();
        }

        m_adapter.Reset();
        m_factory.Reset();

#ifdef _DEBUG
        m_debugController.Reset();
#endif

        m_frameIndex = 0;
        m_globalFenceValue = 0;
        m_fenceValues.clear();
        m_width = m_height = 0;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE D3D12Context::rtvFor(uint32_t imageIndex) const {
        D3D12_CPU_DESCRIPTOR_HANDLE h = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        h.ptr += static_cast<SIZE_T>(imageIndex) * m_rtvDescriptorSize;
        return h;
    }

    ID3D12Resource* D3D12Context::backBuffer(uint32_t imageIndex) const {
        if (imageIndex >= m_backBuffers.size()) return nullptr;
        return m_backBuffers[imageIndex].Get();
    }

    bool D3D12Context::drawFrame(float r, float g, float b) {
        if (!isReady()) return false;

        bool sizeChanged = false;

        try {
            RECT rc{};
            GetClientRect(m_hwnd, &rc);
            const uint32_t cw = static_cast<uint32_t>(rc.right - rc.left);
            const uint32_t ch = static_cast<uint32_t>(rc.bottom - rc.top);
            if (cw == 0 || ch == 0) return false;

            const uint64_t nowMs = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count());
            if (cw != m_lastClientW || ch != m_lastClientH) {
                m_lastClientW = cw;
                m_lastClientH = ch;
                m_lastSizeChangeMs = nowMs;
            }

            if (cw != m_width || ch != m_height) {
                const bool streaming = (m_lastSizeChangeMs != 0)
                    && (nowMs - m_lastSizeChangeMs < kResizeSettleMs);

                const bool staleBuffer = (m_lastRecreateMs == 0)
                    || (nowMs - m_lastRecreateMs > kResizeHardRefreshMs);

                if (streaming && !staleBuffer) {
                    m_resizeDeferred = true;
                }
                else {
                    m_resizeDeferred = false;
                    sizeChanged = true;
                    if (!recreateSwapchain()) return false;
                    m_lastRecreateMs = nowMs;
                }
            }
            else {
                m_resizeDeferred = false;
            }

            if (m_fence->GetCompletedValue() < m_fenceValues[m_frameIndex]) {
                ThrowIfFailed(
                    m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent),
                    "Fence::SetEventOnCompletion(frame)");
                WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);
            }

            ThrowIfFailed(m_allocators[m_frameIndex]->Reset(), "Allocator::Reset");
            ThrowIfFailed(m_commandList->Reset(m_allocators[m_frameIndex].Get(), nullptr),
                "CommandList::Reset");

            const uint32_t imageIndex = m_swapchain->GetCurrentBackBufferIndex();

            {
                D3D12_RESOURCE_BARRIER barrier{};
                barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
                barrier.Transition.pResource = m_backBuffers[imageIndex].Get();
                barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
                barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
                m_commandList->ResourceBarrier(1, &barrier);
            }

            const D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtvFor(imageIndex);
            const float ca = m_clearAlpha;
            const float clearColor[4] = { r * ca, g * ca, b * ca, ca };
            m_commandList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
            m_commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

            D3D12_VIEWPORT vp{};
            vp.TopLeftX = 0.0f;
            vp.TopLeftY = 0.0f;
            vp.Width = static_cast<float>(m_width);
            vp.Height = static_cast<float>(m_height);
            vp.MinDepth = 0.0f;
            vp.MaxDepth = 1.0f;
            m_commandList->RSSetViewports(1, &vp);

            D3D12_RECT sc{ 0, 0, (LONG)m_width, (LONG)m_height };
            m_commandList->RSSetScissorRects(1, &sc);

            if (m_recordFn) {
                FrameInfo fi;
                fi.cmd = m_commandList.Get();
                fi.allocator = m_allocators[m_frameIndex].Get();
                fi.imageIndex = imageIndex;
                fi.frameSlot = m_frameIndex;
                fi.width = cw;
                fi.height = ch;
                fi.format = m_swapFormat;
                m_recordFn(fi);
            }

            {
                D3D12_RESOURCE_BARRIER barrier{};
                barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
                barrier.Transition.pResource = m_backBuffers[imageIndex].Get();
                barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
                barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
                m_commandList->ResourceBarrier(1, &barrier);
            }

            ThrowIfFailed(m_commandList->Close(), "CommandList::Close");

            ID3D12CommandList* lists[] = { m_commandList.Get() };
            m_commandQueue->ExecuteCommandLists(1, lists);

            const uint64_t nextFence = ++m_globalFenceValue;
            m_fenceValues[m_frameIndex] = nextFence;
            ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), nextFence), "Queue::Signal");

            const int syncInterval = m_dcompDevice
                ? 1
                : ((sizeChanged || m_resizeDeferred) ? 1 : m_syncInterval);
            const HRESULT hr = m_swapchain->Present(syncInterval, 0);
            if (FAILED(hr)) {
                if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
                    std::cerr << "[D3D12] device lost during Present: "
                        << HResultName(hr) << std::endl;
                    return false;
                }
                ThrowIfFailed(hr, "SwapChain::Present");
            }

            m_frameIndex = (m_frameIndex + 1) % kFramesInFlight;
            return true;

        }
        catch (const std::exception& e) {
            std::cerr << "[D3D12] drawFrame failed: " << e.what() << std::endl;
            if (m_commandList) {
                m_commandList->Close();
            }
            return false;
        }
    }

    bool D3D12Context::recreateSwapchain() {
        RECT rc{};
        GetClientRect(m_hwnd, &rc);
        const uint32_t cw = static_cast<uint32_t>(rc.right - rc.left);
        const uint32_t ch = static_cast<uint32_t>(rc.bottom - rc.top);
        if (cw == 0 || ch == 0) return false;

        const bool trace = [] {
            const char* v = std::getenv("ZEKIT_TRACE_FRAME");
            return v && *v != '\0' && *v != '0';
            }();
        auto nowMs = [] {
            return std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            };
        const double t0 = trace ? nowMs() : 0.0;

        waitIdle();
        const double t1 = trace ? nowMs() : 0.0;

        m_backBuffers.clear();

        ThrowIfFailed(
            m_swapchain->ResizeBuffers(
                kFramesInFlight,
                cw, ch,
                m_swapFormat,
                0),
            "SwapChain::ResizeBuffers");

        const double t2 = trace ? nowMs() : 0.0;

        m_width = cw;
        m_height = ch;

        m_backBuffers.resize(kFramesInFlight);
        for (uint32_t i = 0; i < kFramesInFlight; ++i) {
            ThrowIfFailed(
                m_swapchain->GetBuffer(i, IID_PPV_ARGS(&m_backBuffers[i])),
                "SwapChain::GetBuffer(recreate)");
        }

        D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        for (uint32_t i = 0; i < kFramesInFlight; ++i) {
            D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
            rtvDesc.Format = m_swapFormat;
            rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
            rtvDesc.Texture2D.MipSlice = 0;
            rtvDesc.Texture2D.PlaneSlice = 0;
            m_device->CreateRenderTargetView(m_backBuffers[i].Get(), &rtvDesc, rtv);
            rtv.ptr += m_rtvDescriptorSize;
        }

        if (trace) {
            const double t3 = nowMs();
            std::cerr << "[D3D12/resize] " << cw << "x" << ch
                << " | 等GPU " << (t1 - t0) << "ms"
                << " | ResizeBuffers " << (t2 - t1) << "ms"
                << " | 重取缓冲 " << (t3 - t2) << "ms\n";
        }
        return true;
    }
}
