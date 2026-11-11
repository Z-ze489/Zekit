#include "pch.h"
#include "ZeD3D12UIRenderer.h"
#include "ZeFrameCollector.h"

#include <d3dcompiler.h>

#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

namespace Dx {
    namespace {

        struct RootConstants {
            float   proj[16];
            int32_t isTex;
            int32_t isColor;
            int32_t pad[2];
        };
        static_assert(sizeof(RootConstants) == 80,
            "root constants must match the HLSL cbuffer layout");

        constexpr UINT kRootParamConstants = 0;
        constexpr UINT kRootParamSrvTable = 1;
        constexpr UINT kRootParamSamplerTable = 2;

        const D3D12_INPUT_ELEMENT_DESC kInputLayout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,       0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,  8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 2, DXGI_FORMAT_R32_FLOAT,          0, 48, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        const char* kHLSL = R"(
cbuffer UIConstants : register(b0)
{
    float4x4 uProj;
    int      uIsTex;
    int      uIsColor;
    int2     _pad;
};

struct VSInput
{
    float2 pos   : POSITION;
    float4 color : COLOR;
    float2 uv    : TEXCOORD0;
    float4 shape : TEXCOORD1;
    float  rad   : TEXCOORD2;
};

struct VSOutput
{
    float4 clipPos : SV_Position;
    float4 color   : COLOR;
    float2 uv      : TEXCOORD0;
    float4 shape   : TEXCOORD1;
    float  rad     : TEXCOORD2;
    float2 pos     : TEXCOORD3;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    output.color = input.color;
    output.uv    = input.uv;
    output.shape = input.shape;
    output.rad   = input.rad;
    output.pos   = input.pos;
    output.clipPos = mul(uProj, float4(input.pos, 0.0f, 1.0f));
    return output;
}

Texture2D    uTex     : register(t0);
SamplerState uSampler : register(s0);

float roundedRectSDF(float2 p, float2 b, float r)
{
    float2 q = abs(p) - b + float2(r, r);
    return min(max(q.x, q.y), 0.0f) + length(max(q, float2(0.0f, 0.0f))) - r;
}

float4 PSMain(VSOutput input) : SV_Target
{
    float4 c;
    if (uIsTex == 0)
    {
        if (input.shape.z > 0.0f || input.shape.w > 0.0f)
        {
            float2 c2 = input.shape.xy;
            float2 b = input.shape.zw;
            float  r = clamp(input.rad, 0.0f, min(b.x, b.y));
            float2 p = input.pos - c2;
            float d = roundedRectSDF(p, b, r);
            float2 grad = float2(ddx(d), ddy(d));
            float  g = max(length(grad), 1e-6f);
            float coverage = clamp(0.5f - d / g, 0.0f, 1.0f);
            c = float4(input.color.rgb, input.color.a * coverage);
        }
        else
        {
            c = input.color;
        }
    }
    else
    {
        float4 t = uTex.Sample(uSampler, input.uv);
        if (uIsColor == 1)
        {
            c = input.color * t;
        }
        else
        {
            c = float4(input.color.rgb, input.color.a * t.r);
        }
    }
    // ★ 预乘 alpha：合成交换链（DXGI_ALPHA_MODE_PREMULTIPLIED）要求 RGB 已乘 alpha，
    //   否则半透明边缘出 halo。普通窗口 framebuffer alpha 恒 1，乘 1 无副作用。
    c.rgb *= c.a;
    return c;
}
)";

        Microsoft::WRL::ComPtr<ID3DBlob> compileShader(const char* entry, const char* target) {
            UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
            flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
            flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

            Microsoft::WRL::ComPtr<ID3DBlob> code, err;
            const HRESULT hr = D3DCompile(
                kHLSL, std::strlen(kHLSL), "ZeD3D12UI", nullptr, nullptr,
                entry, target, flags, 0, &code, &err);

            if (FAILED(hr)) {
                std::string msg = "HLSL compile failed [";
                msg += entry;
                msg += "]: ";
                if (err) msg.append(static_cast<const char*>(err->GetBufferPointer()),
                                    err->GetBufferSize());
                throw std::runtime_error(msg);
            }
            return code;
        }

    }

    D3D12UIRenderer::~D3D12UIRenderer() { Shutdown(); }

    bool D3D12UIRenderer::Init(D3D12Context& ctx)
    {
        if (m_pipeline) return true;

        if (!ctx.isReady()) {
            std::cerr << "[DxUI] D3D12Context is not ready; cannot create the UI renderer\n";
            return false;
        }

        static_assert(sizeof(Kit::UIVertex) == 52,
            "UIVertex must be 13 tightly packed floats (52 bytes) to match the D3D12 input layout");

        m_ctx = &ctx;

        try {
            if (!createRootSignature()) return false;
            if (!createPipeline(ctx.swapchainFormat())) return false;
            if (!createSampler()) return false;
            if (!createDescriptorHeap()) return false;
            createFrameBuffers();
        }
        catch (const std::exception& e) {
            std::cerr << "[DxUI] Init failed: " << e.what() << "\n";
            Shutdown();
            return false;
        }

        {
            const uint8_t white[4] = { 255, 255, 255, 255 };
            m_dummyTex = uploadTexture(1, 1, white, 4, DXGI_FORMAT_R8G8B8A8_UNORM, 4, false);
            if (m_dummyTex == 0) {
                std::cerr << "[DxUI] failed to create the dummy texture\n";
                Shutdown();
                return false;
            }
        }

        return true;
    }

    void D3D12UIRenderer::Shutdown()
    {
        if (!m_ctx) return;

        m_ctx->waitIdle();

        for (auto& fb : m_frames) destroyFrameBuffers(fb);
        m_frames.clear();

        m_textures.clear();
        m_freeSrvSlots.clear();

        m_samplerHeap.Reset();
        m_srvHeap.Reset();
        m_pipeline.Reset();
        m_rootSignature.Reset();

        m_srvNextSlot = 0;
        m_samplerWritten = false;
        m_overflowWarned = false;
        m_dummyTex = 0;
        m_ctx = nullptr;
    }

    bool D3D12UIRenderer::createRootSignature()
    {
        D3D12_ROOT_PARAMETER params[3]{};

        params[kRootParamConstants].ParameterType =
            D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[kRootParamConstants].Constants.ShaderRegister = 0;
        params[kRootParamConstants].Constants.RegisterSpace = 0;
        params[kRootParamConstants].Constants.Num32BitValues =
            sizeof(RootConstants) / sizeof(uint32_t);
        params[kRootParamConstants].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_DESCRIPTOR_RANGE srvRange{};
        srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange.NumDescriptors = 1;
        srvRange.BaseShaderRegister = 0;
        srvRange.RegisterSpace = 0;
        srvRange.OffsetInDescriptorsFromTableStart = 0;

        params[kRootParamSrvTable].ParameterType =
            D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[kRootParamSrvTable].DescriptorTable.NumDescriptorRanges = 1;
        params[kRootParamSrvTable].DescriptorTable.pDescriptorRanges = &srvRange;
        params[kRootParamSrvTable].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_DESCRIPTOR_RANGE samplerRange{};
        samplerRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
        samplerRange.NumDescriptors = 1;
        samplerRange.BaseShaderRegister = 0;
        samplerRange.RegisterSpace = 0;
        samplerRange.OffsetInDescriptorsFromTableStart = 0;

        params[kRootParamSamplerTable].ParameterType =
            D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[kRootParamSamplerTable].DescriptorTable.NumDescriptorRanges = 1;
        params[kRootParamSamplerTable].DescriptorTable.pDescriptorRanges = &samplerRange;
        params[kRootParamSamplerTable].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC desc{};
        desc.NumParameters = _countof(params);
        desc.pParameters = params;
        desc.NumStaticSamplers = 0;
        desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        Microsoft::WRL::ComPtr<ID3DBlob> sig, err;
        HRESULT hr = D3D12SerializeRootSignature(
            &desc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err);
        if (FAILED(hr)) {
            std::string msg = "D3D12SerializeRootSignature failed: ";
            if (err) msg.append(static_cast<const char*>(err->GetBufferPointer()),
                                err->GetBufferSize());
            std::cerr << "[DxUI] " << msg << "\n";
            return false;
        }

        hr = m_ctx->device()->CreateRootSignature(
            0, sig->GetBufferPointer(), sig->GetBufferSize(),
            IID_PPV_ARGS(&m_rootSignature));
        if (FAILED(hr)) {
            std::cerr << "[DxUI] CreateRootSignature failed\n";
            return false;
        }
        return true;
    }

    bool D3D12UIRenderer::createPipeline(DXGI_FORMAT rtvFormat)
    {
        Microsoft::WRL::ComPtr<ID3DBlob> vs, ps;
        try {
            vs = compileShader("VSMain", "vs_5_0");
            ps = compileShader("PSMain", "ps_5_0");
        }
        catch (const std::exception& e) {
            std::cerr << "[DxUI] " << e.what() << "\n";
            return false;
        }

        D3D12_RENDER_TARGET_BLEND_DESC rtBlend{};
        rtBlend.BlendEnable = TRUE;
        rtBlend.LogicOpEnable = FALSE;
        rtBlend.SrcBlend = D3D12_BLEND_ONE;
        rtBlend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        rtBlend.BlendOp = D3D12_BLEND_OP_ADD;
        rtBlend.SrcBlendAlpha = D3D12_BLEND_ONE;
        rtBlend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        rtBlend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        rtBlend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

        D3D12_BLEND_DESC blend{};
        blend.AlphaToCoverageEnable = FALSE;
        blend.IndependentBlendEnable = FALSE;
        blend.RenderTarget[0] = rtBlend;

        D3D12_RASTERIZER_DESC raster{};
        raster.FillMode = D3D12_FILL_MODE_SOLID;
        raster.CullMode = D3D12_CULL_MODE_NONE;
        raster.FrontCounterClockwise = FALSE;
        raster.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
        raster.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
        raster.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
        raster.DepthClipEnable = TRUE;
        raster.MultisampleEnable = FALSE;
        raster.AntialiasedLineEnable = FALSE;
        raster.ForcedSampleCount = 0;
        raster.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

        D3D12_DEPTH_STENCIL_DESC depth{};
        depth.DepthEnable = FALSE;
        depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        depth.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        depth.StencilEnable = FALSE;

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
        pso.pRootSignature = m_rootSignature.Get();
        pso.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
        pso.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
        pso.BlendState = blend;
        pso.SampleMask = UINT_MAX;
        pso.RasterizerState = raster;
        pso.DepthStencilState = depth;
        pso.InputLayout = { kInputLayout, _countof(kInputLayout) };
        pso.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
        pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pso.NumRenderTargets = 1;
        pso.RTVFormats[0] = rtvFormat;
        pso.DSVFormat = DXGI_FORMAT_UNKNOWN;
        pso.SampleDesc = { 1, 0 };
        pso.NodeMask = 0;
        pso.CachedPSO = { nullptr, 0 };
        pso.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

        const HRESULT hr = m_ctx->device()->CreateGraphicsPipelineState(
            &pso, IID_PPV_ARGS(&m_pipeline));
        if (FAILED(hr)) {
            std::cerr << "[DxUI] CreateGraphicsPipelineState failed, hr = 0x"
                      << std::hex << static_cast<unsigned>(hr) << std::dec << "\n";
            return false;
        }
        return true;
    }

    bool D3D12UIRenderer::createSampler()
    {
        D3D12_DESCRIPTOR_HEAP_DESC desc{};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
        desc.NumDescriptors = 1;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        desc.NodeMask = 0;

        const HRESULT hr = m_ctx->device()->CreateDescriptorHeap(
            &desc, IID_PPV_ARGS(&m_samplerHeap));
        if (FAILED(hr)) {
            std::cerr << "[DxUI] CreateDescriptorHeap(Sampler) failed\n";
            return false;
        }
        m_samplerDescriptorSize = m_ctx->device()->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);

        D3D12_SAMPLER_DESC sd{};
        sd.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sd.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sd.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sd.MipLODBias = 0.0f;
        sd.MaxAnisotropy = 1;
        sd.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
        sd.BorderColor[0] = sd.BorderColor[1] = sd.BorderColor[2] = sd.BorderColor[3] = 0.0f;
        sd.MinLOD = 0.0f;
        sd.MaxLOD = D3D12_FLOAT32_MAX;

        m_ctx->device()->CreateSampler(
            &sd, m_samplerHeap->GetCPUDescriptorHandleForHeapStart());
        m_samplerWritten = true;
        return true;
    }

    bool D3D12UIRenderer::createDescriptorHeap()
    {
        D3D12_DESCRIPTOR_HEAP_DESC desc{};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.NumDescriptors = kMaxTextures;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        desc.NodeMask = 0;

        const HRESULT hr = m_ctx->device()->CreateDescriptorHeap(
            &desc, IID_PPV_ARGS(&m_srvHeap));
        if (FAILED(hr)) {
            std::cerr << "[DxUI] CreateDescriptorHeap(SRV) failed\n";
            return false;
        }

        m_srvDescriptorSize = m_ctx->device()->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        m_srvNextSlot = 0;
        return true;
    }

    void D3D12UIRenderer::createFrameBuffers()
    {
        constexpr UINT64 kInitVertBytes = 64 * 1024;
        constexpr UINT64 kInitIdxBytes = 32 * 1024;

        m_frames.resize(m_ctx->framesInFlight());
        for (auto& fb : m_frames) {
            ensureCapacity(fb, kInitVertBytes, kInitIdxBytes);
        }
    }

    void D3D12UIRenderer::destroyFrameBuffers(FrameBuffers& fb)
    {
        if (fb.vboMapped) { fb.vbo->Unmap(0, nullptr); fb.vboMapped = nullptr; }
        if (fb.iboMapped) { fb.ibo->Unmap(0, nullptr); fb.iboMapped = nullptr; }
        fb.vbo.Reset();
        fb.ibo.Reset();
        fb.vboCap = fb.iboCap = 0;
        fb.vboGpu = fb.iboGpu = 0;
    }

    bool D3D12UIRenderer::createUploadBuffer(ID3D12Device* device, UINT64 bytes,
                                             ID3D12Resource** out,
                                             uint8_t** outMapped,
                                             D3D12_GPU_VIRTUAL_ADDRESS* outGpu)
    {
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_UPLOAD;
        heap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heap.CreationNodeMask = 1;
        heap.VisibleNodeMask = 1;

        D3D12_RESOURCE_DESC rd{};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Alignment = 0;
        rd.Width = bytes;
        rd.Height = 1;
        rd.DepthOrArraySize = 1;
        rd.MipLevels = 1;
        rd.Format = DXGI_FORMAT_UNKNOWN;
        rd.SampleDesc = { 1, 0 };
        rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        rd.Flags = D3D12_RESOURCE_FLAG_NONE;

        const HRESULT hr = device->CreateCommittedResource(
            &heap, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr, IID_PPV_ARGS(out));
        if (FAILED(hr)) return false;

        D3D12_RANGE readRange{ 0, 0 };
        if (FAILED((*out)->Map(0, &readRange, reinterpret_cast<void**>(outMapped)))) {
            (*out)->Release();
            *out = nullptr;
            return false;
        }
        *outGpu = (*out)->GetGPUVirtualAddress();
        return true;
    }

    bool D3D12UIRenderer::ensureCapacity(FrameBuffers& fb, UINT64 vertexBytes, UINT64 indexBytes)
    {
        if (vertexBytes > fb.vboCap) {
            UINT64 cap = fb.vboCap ? fb.vboCap : 4096;
            while (cap < vertexBytes) cap *= 2;
            if (fb.vboMapped) { fb.vbo->Unmap(0, nullptr); fb.vboMapped = nullptr; }
            fb.vbo.Reset();
            if (!createUploadBuffer(m_ctx->device(), cap, &fb.vbo, &fb.vboMapped, &fb.vboGpu)) {
                std::cerr << "[DxUI] failed to grow the vertex buffer\n";
                return false;
            }
            fb.vboCap = cap;
        }

        if (indexBytes > fb.iboCap) {
            UINT64 cap = fb.iboCap ? fb.iboCap : 4096;
            while (cap < indexBytes) cap *= 2;
            if (fb.iboMapped) { fb.ibo->Unmap(0, nullptr); fb.iboMapped = nullptr; }
            fb.ibo.Reset();
            if (!createUploadBuffer(m_ctx->device(), cap, &fb.ibo, &fb.iboMapped, &fb.iboGpu)) {
                std::cerr << "[DxUI] failed to grow the index buffer\n";
                return false;
            }
            fb.iboCap = cap;
        }

        return true;
    }

    uint32_t D3D12UIRenderer::CreateGray(int w, int h, const uint8_t* pixels, int pitch)
    {
        return uploadTexture(w, h, pixels, pitch, DXGI_FORMAT_R8_UNORM, 1, false);
    }

    uint32_t D3D12UIRenderer::CreateRGBA(int w, int h, const uint8_t* pixels, int pitch, bool bgra)
    {
        return uploadTexture(w, h, pixels, pitch, DXGI_FORMAT_R8G8B8A8_UNORM, 4, bgra);
    }

    uint32_t D3D12UIRenderer::uploadTexture(int w, int h, const uint8_t* pixels, int pitch,
                                            DXGI_FORMAT srcFormat, int bytesPerPixel, bool swapRedBlue)
    {
        if (!m_ctx || w <= 0 || h <= 0 || !pixels) return 0;

        ID3D12Device* device = m_ctx->device();

        const UINT copyPitch = static_cast<UINT>(w) * bytesPerPixel;
        std::vector<uint8_t> packed(static_cast<size_t>(copyPitch) * h);

        for (int y = 0; y < h; ++y) {
            const uint8_t* src = pixels + static_cast<size_t>(y) * pitch;
            uint8_t* dst = packed.data() + static_cast<size_t>(y) * copyPitch;

            if (bytesPerPixel == 1) {
                std::memcpy(dst, src, static_cast<size_t>(w));
            }
            else if (swapRedBlue) {
                for (int x = 0; x < w; ++x) {
                    dst[x * 4 + 0] = src[x * 4 + 2];
                    dst[x * 4 + 1] = src[x * 4 + 1];
                    dst[x * 4 + 2] = src[x * 4 + 0];
                    dst[x * 4 + 3] = src[x * 4 + 3];
                }
            }
            else {
                std::memcpy(dst, src, static_cast<size_t>(copyPitch));
            }
        }

        Microsoft::WRL::ComPtr<ID3D12Resource> tex;
        {
            D3D12_HEAP_PROPERTIES heap{};
            heap.Type = D3D12_HEAP_TYPE_DEFAULT;
            heap.CreationNodeMask = 1;
            heap.VisibleNodeMask = 1;

            D3D12_RESOURCE_DESC rd{};
            rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            rd.Alignment = 0;
            rd.Width = static_cast<UINT64>(w);
            rd.Height = static_cast<UINT>(h);
            rd.DepthOrArraySize = 1;
            rd.MipLevels = 1;
            rd.Format = srcFormat;
            rd.SampleDesc = { 1, 0 };
            rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            rd.Flags = D3D12_RESOURCE_FLAG_NONE;

            const HRESULT hr = device->CreateCommittedResource(
                &heap, D3D12_HEAP_FLAG_NONE, &rd,
                D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&tex));
            if (FAILED(hr)) {
                std::cerr << "[DxUI] CreateCommittedResource(Texture) failed\n";
                return 0;
            }
        }

        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        UINT numRows = 0;
        UINT64 rowSizeInBytes = 0;
        UINT64 totalBytes = 0;
        const D3D12_RESOURCE_DESC texDesc = tex->GetDesc();
        device->GetCopyableFootprints(&texDesc, 0, 1, 0,
            &footprint, &numRows, &rowSizeInBytes, &totalBytes);

        Microsoft::WRL::ComPtr<ID3D12Resource> upload;
        uint8_t* uploadMapped = nullptr;
        D3D12_GPU_VIRTUAL_ADDRESS uploadGpu = 0;
        if (!createUploadBuffer(device, totalBytes, &upload, &uploadMapped, &uploadGpu)) {
            std::cerr << "[DxUI] failed to create the texture upload buffer\n";
            return 0;
        }

        {
            uint8_t* dst = uploadMapped + footprint.Offset;
            for (UINT y = 0; y < numRows; ++y) {
                std::memcpy(dst + static_cast<size_t>(y) * footprint.Footprint.RowPitch,
                            packed.data() + static_cast<size_t>(y) * copyPitch,
                            copyPitch);
            }
        }

        bool uploaded = false;
        runOneShot([&](ID3D12GraphicsCommandList* cmd) {
            D3D12_TEXTURE_COPY_LOCATION src{};
            src.pResource = upload.Get();
            src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            src.PlacedFootprint = footprint;

            D3D12_TEXTURE_COPY_LOCATION dst{};
            dst.pResource = tex.Get();
            dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            dst.SubresourceIndex = 0;

            cmd->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

            D3D12_RESOURCE_BARRIER b{};
            b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            b.Transition.pResource = tex.Get();
            b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
            b.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            cmd->ResourceBarrier(1, &b);
            uploaded = true;
        });

        if (uploadMapped) upload->Unmap(0, nullptr);
        if (!uploaded) return 0;

        const UINT slot = allocSlot();
        if (slot == UINT_MAX) {
            if (!m_overflowWarned) {
                std::cerr << "[DxUI] texture heap exhausted (" << kMaxTextures
                          << "); further textures will be rejected\n";
                m_overflowWarned = true;
            }
            return 0;
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = srcFormat;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MostDetailedMip = 0;
        srv.Texture2D.MipLevels = 1;
        srv.Texture2D.PlaneSlice = 0;
        srv.Texture2D.ResourceMinLODClamp = 0.0f;

        D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
        srvHandle.ptr += static_cast<SIZE_T>(slot) * m_srvDescriptorSize;
        device->CreateShaderResourceView(tex.Get(), &srv, srvHandle);

        const uint32_t handle = slot + 1;

        if (handle >= m_textures.size()) m_textures.resize(handle + 1);
        m_textures[handle].resource = tex;
        m_textures[handle].srvSlot = slot;
        m_textures[handle].occupied = true;

        return handle;
    }

    void D3D12UIRenderer::Destroy(uint32_t tex)
    {
        if (tex == 0 || tex >= m_textures.size()) return;
        Texture& t = m_textures[tex];
        if (!t.occupied) return;

        if (m_ctx) m_ctx->waitIdle();

        t.resource.Reset();
        t.occupied = false;
        releaseSlot(tex);
    }

    uint32_t D3D12UIRenderer::allocSlot()
    {
        if (!m_freeSrvSlots.empty()) {
            const UINT slot = m_freeSrvSlots.back();
            m_freeSrvSlots.pop_back();
            return slot;
        }
        if (m_srvNextSlot >= kMaxTextures) return UINT_MAX;
        return m_srvNextSlot++;
    }

    void D3D12UIRenderer::releaseSlot(uint32_t handle)
    {
        if (handle == 0) return;
        const uint32_t slot = handle - 1;
        if (slot < kMaxTextures) m_freeSrvSlots.push_back(slot);
    }

    UINT D3D12UIRenderer::srvSlotFor(uint32_t handle) const
    {
        if (handle == 0) return 0;
        if (handle >= m_textures.size()) return 0;
        const Texture& t = m_textures[handle];
        if (!t.occupied) return 0;
        return t.srvSlot;
    }

    void D3D12UIRenderer::runOneShot(const std::function<void(ID3D12GraphicsCommandList*)>& fn)
    {
        ID3D12Device* device = m_ctx->device();

        Microsoft::WRL::ComPtr<ID3D12CommandAllocator> alloc;
        if (FAILED(device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc)))) {
            std::cerr << "[DxUI] runOneShot: CreateCommandAllocator failed\n";
            return;
        }

        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> list;
        if (FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            alloc.Get(), nullptr, IID_PPV_ARGS(&list)))) {
            std::cerr << "[DxUI] runOneShot: CreateCommandList failed\n";
            return;
        }

        fn(list.Get());
        list->Close();

        ID3D12CommandList* lists[] = { list.Get() };
        m_ctx->commandQueue()->ExecuteCommandLists(1, lists);
        m_ctx->waitIdle();
    }

    int D3D12UIRenderer::Draw(const Kit::FrameCollector& fc,
                              const D3D12Context::FrameInfo& frame)
    {
        if (!m_pipeline || !m_ctx) return 0;
        if (frame.frameSlot >= m_frames.size()) {
            std::cerr << "[DxUI] Draw: frameSlot out of range ("
                      << frame.frameSlot << " >= " << m_frames.size() << ")\n";
            return 0;
        }

        const auto& verts = fc.Vertices();
        const auto& idxs = fc.Indices();
        const auto& batches = fc.Batches();
        if (verts.empty() || idxs.empty() || batches.empty()) return 0;

        const UINT64 vertBytes = verts.size() * sizeof(Kit::UIVertex);
        const UINT64 idxBytes = idxs.size() * sizeof(uint32_t);

        FrameBuffers& fb = m_frames[frame.frameSlot];
        if (!ensureCapacity(fb, vertBytes, idxBytes)) return 0;

        std::memcpy(fb.vboMapped, verts.data(), static_cast<size_t>(vertBytes));
        std::memcpy(fb.iboMapped, idxs.data(), static_cast<size_t>(idxBytes));

        ID3D12GraphicsCommandList* cmd = frame.cmd;
        const UINT32 W = frame.width;
        const UINT32 H = frame.height;
        if (W == 0 || H == 0) return 0;

        cmd->SetPipelineState(m_pipeline.Get());
        cmd->SetGraphicsRootSignature(m_rootSignature.Get());

        ID3D12DescriptorHeap* heaps[] = { m_srvHeap.Get(), m_samplerHeap.Get() };
        cmd->SetDescriptorHeaps(_countof(heaps), heaps);

        {
            D3D12_GPU_DESCRIPTOR_HANDLE gpu =
                m_samplerHeap->GetGPUDescriptorHandleForHeapStart();
            cmd->SetGraphicsRootDescriptorTable(kRootParamSamplerTable, gpu);
        }

        D3D12_VERTEX_BUFFER_VIEW vbv{};
        vbv.BufferLocation = fb.vboGpu;
        vbv.SizeInBytes = static_cast<UINT>(vertBytes);
        vbv.StrideInBytes = sizeof(Kit::UIVertex);
        cmd->IASetVertexBuffers(0, 1, &vbv);

        D3D12_INDEX_BUFFER_VIEW ibv{};
        ibv.BufferLocation = fb.iboGpu;
        ibv.SizeInBytes = static_cast<UINT>(idxBytes);
        ibv.Format = DXGI_FORMAT_R32_UINT;
        cmd->IASetIndexBuffer(&ibv);

        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        RootConstants rc{};
        {
            const float w = static_cast<float>(W);
            const float h = static_cast<float>(H);
            std::memset(rc.proj, 0, sizeof(rc.proj));
            rc.proj[0] = 2.0f / w;
            rc.proj[5] = -2.0f / h;
            rc.proj[10] = 1.0f;
            rc.proj[12] = -1.0f;
            rc.proj[13] = 1.0f;
            rc.proj[15] = 1.0f;
        }

        int draws = 0;
        for (const auto& b : batches) {
            if (b.indexCount == 0) continue;

            rc.isTex = (b.texture != 0) ? 1 : 0;
            rc.isColor = (b.isColor ? 1 : 0);
            cmd->SetGraphicsRoot32BitConstants(
                kRootParamConstants,
                sizeof(RootConstants) / sizeof(uint32_t),
                &rc, 0);

            const UINT slot = (b.texture != 0) ? srvSlotFor(b.texture) : srvSlotFor(m_dummyTex);
            D3D12_GPU_DESCRIPTOR_HANDLE gpu = m_srvHeap->GetGPUDescriptorHandleForHeapStart();
            gpu.ptr += static_cast<UINT64>(slot) * m_srvDescriptorSize;
            cmd->SetGraphicsRootDescriptorTable(kRootParamSrvTable, gpu);

            cmd->DrawIndexedInstanced(b.indexCount, 1, b.indexOffset, 0, 0);
            ++draws;
        }

        return draws;
    }

}
