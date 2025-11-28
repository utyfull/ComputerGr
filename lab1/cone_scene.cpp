#include "cone_scene.hpp"
#include "d3d12_core.hpp"
#include "cone_mesh.hpp"
#include "dx_util.hpp"

#include <directx/d3dx12.h>
#include <d3dcompiler.h>
#include <wincodec.h>

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "windowscodecs.lib")

#include <chrono>
#include <vector>
#include <cstring>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

// ---------------- CPU-версии структур из HLSL ----------------

struct CameraCBData
{
    XMFLOAT4X4 viewProj;

    XMFLOAT3   camPos;
    float      _pad0;

    XMFLOAT3   ambientColor;
    float      _pad1;

    XMFLOAT3   dirLightDir;
    float      _pad2;

    XMFLOAT3   dirLightColor;
    float      _pad3;
};

struct MaterialCPU
{
    XMFLOAT3 albedo;
    float    shininess;

    XMFLOAT3 specColor;
    float    _pad;
};

struct InstanceDataCPU
{
    XMFLOAT4X4 world;
    XMFLOAT4   tag;
    MaterialCPU mat;
};

struct PointLightCPU
{
    XMFLOAT3 pos;
    float    attK;

    XMFLOAT3 color;
    float    _pad;
};

struct SpotLightCPU
{
    XMFLOAT3 pos;
    float    attK;

    XMFLOAT3 dir;
    float    cosInner;

    XMFLOAT3 color;
    float    cosOuter;
};

// ---------------- Загрузка RGBA8-текстуры через WIC ----------------

static bool LoadTextureWIC(
    const wchar_t* filename,
    std::vector<uint8_t>& pixels,
    UINT& width,
    UINT& height)
{
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    ComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(
        CLSID_WICImagingFactory2, nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory))))
        return false;

    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(factory->CreateDecoderFromFilename(
        filename, nullptr, GENERIC_READ,
        WICDecodeMetadataCacheOnLoad, &decoder)))
        return false;

    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, &frame)))
        return false;

    if (FAILED(frame->GetSize(&width, &height)))
        return false;

    ComPtr<IWICFormatConverter> converter;
    if (FAILED(factory->CreateFormatConverter(&converter)))
        return false;

    if (FAILED(converter->Initialize(
        frame.Get(),
        GUID_WICPixelFormat32bppRGBA,
        WICBitmapDitherTypeNone,
        nullptr,
        0.0f,
        WICBitmapPaletteTypeCustom)))
        return false;

    pixels.resize(static_cast<size_t>(width) * height * 4);
    const UINT stride = width * 4;
    const UINT size = stride * height;

    if (FAILED(converter->CopyPixels(
        nullptr, stride, size, pixels.data())))
        return false;

    return true;
}

// ---------------- Инициализация сцены ----------------

bool ConeScene::Init(D3D12Core& core)
{
    ID3D12Device* device = core.Dev();

    // Root signature: b0, t0..t3, b1
    CD3DX12_ROOT_PARAMETER rp[3];

    rp[0].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);

    CD3DX12_DESCRIPTOR_RANGE range;
    range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 0);  // t0..t3
    rp[1].InitAsDescriptorTable(1, &range, D3D12_SHADER_VISIBILITY_ALL);

    rp[2].InitAsConstants(3, 1, 0, D3D12_SHADER_VISIBILITY_ALL);

    // Статический сэмплер для текстуры
    D3D12_STATIC_SAMPLER_DESC staticSampler{};
    staticSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    staticSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSampler.MipLODBias = 0.0f;
    staticSampler.MaxAnisotropy = 1;
    staticSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    staticSampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
    staticSampler.MinLOD = 0.0f;
    staticSampler.MaxLOD = D3D12_FLOAT32_MAX;
    staticSampler.ShaderRegister = 0;
    staticSampler.RegisterSpace = 0;
    staticSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rs{};
    rs.NumParameters = _countof(rp);
    rs.pParameters = rp;
    rs.NumStaticSamplers = 1;
    rs.pStaticSamplers = &staticSampler;
    rs.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> rsBlob;
    ComPtr<ID3DBlob> rsErr;

    CHECK_HR(
        "SerializeRootSignature",
        D3D12SerializeRootSignature(
            &rs, D3D_ROOT_SIGNATURE_VERSION_1,
            &rsBlob, &rsErr));

    CHECK_HR(
        "CreateRootSignature",
        device->CreateRootSignature(
            0,
            rsBlob->GetBufferPointer(),
            rsBlob->GetBufferSize(),
            IID_PPV_ARGS(&rootSig)));

    // Шейдеры
    ComPtr<ID3DBlob> vs;
    ComPtr<ID3DBlob> ps;
    CHECK_HR("Read VS", D3DReadFileToBlob(L"shaders/ConesVS.cso", &vs));
    CHECK_HR("Read PS", D3DReadFileToBlob(L"shaders/ConesPS.cso", &ps));

    // Input layout
    D3D12_INPUT_ELEMENT_DESC il[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    // PSO
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.InputLayout = { il, _countof(il) };
    psoDesc.pRootSignature = rootSig.Get();
    psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

    D3D12_DEPTH_STENCIL_DESC ds{};
    ds.DepthEnable = TRUE;
    ds.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    ds.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    ds.StencilEnable = FALSE;
    psoDesc.DepthStencilState = ds;

    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleDesc.Count = 1;

    CHECK_HR(
        "CreateGraphicsPipelineState",
        device->CreateGraphicsPipelineState(
            &psoDesc, IID_PPV_ARGS(&pso)));

    // ---------------- геометрия конусов ----------------
    std::vector<ConeVertex> verts;
    std::vector<uint32_t>   idx;
    BuildConeMesh(128, verts, idx);

    auto heapUpload = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    auto vbDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(ConeVertex) * verts.size());
    auto ibDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(uint32_t) * idx.size());

    CHECK_HR(
        "Create VB",
        device->CreateCommittedResource(
            &heapUpload, D3D12_HEAP_FLAG_NONE, &vbDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&vb)));

    CHECK_HR(
        "Create IB",
        device->CreateCommittedResource(
            &heapUpload, D3D12_HEAP_FLAG_NONE, &ibDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&ib)));

    {
        void* p = nullptr;
        vb->Map(0, nullptr, &p);
        std::memcpy(p, verts.data(), sizeof(ConeVertex) * verts.size());
        vb->Unmap(0, nullptr);

        ib->Map(0, nullptr, &p);
        std::memcpy(p, idx.data(), sizeof(uint32_t) * idx.size());
        ib->Unmap(0, nullptr);
    }

    vbv = {
        vb->GetGPUVirtualAddress(),
        static_cast<UINT>(sizeof(ConeVertex) * verts.size()),
        static_cast<UINT>(sizeof(ConeVertex))
    };

    ibv = {
        ib->GetGPUVirtualAddress(),
        static_cast<UINT>(sizeof(uint32_t) * idx.size()),
        DXGI_FORMAT_R32_UINT
    };

    indexCount = static_cast<UINT>(idx.size());

    // ---------------- геометрия пола / маркеров ----------------
    std::vector<ConeVertex> floorVerts;
    std::vector<uint32_t>   floorIdx;

    {
        ConeVertex v0{ -1.0f, 0.0f, -1.0f,  0.0f, 1.0f, 0.0f, 0.0f, 0.0f };
        ConeVertex v1{ 1.0f, 0.0f, -1.0f,  0.0f, 1.0f, 0.0f, 1.0f, 0.0f };
        ConeVertex v2{ 1.0f, 0.0f,  1.0f,  0.0f, 1.0f, 0.0f, 1.0f, 1.0f };
        ConeVertex v3{ -1.0f, 0.0f,  1.0f,  0.0f, 1.0f, 0.0f, 0.0f, 1.0f };

        floorVerts = { v0, v1, v2, v3 };
        floorIdx = { 0, 1, 2, 0, 2, 3 };
    }

    auto floorVBDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(ConeVertex) * floorVerts.size());
    auto floorIBDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(uint32_t) * floorIdx.size());

    CHECK_HR(
        "Create Floor VB",
        device->CreateCommittedResource(
            &heapUpload, D3D12_HEAP_FLAG_NONE, &floorVBDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&floorVB)));

    CHECK_HR(
        "Create Floor IB",
        device->CreateCommittedResource(
            &heapUpload, D3D12_HEAP_FLAG_NONE, &floorIBDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&floorIB)));

    {
        void* pf = nullptr;
        floorVB->Map(0, nullptr, &pf);
        std::memcpy(pf, floorVerts.data(), sizeof(ConeVertex) * floorVerts.size());
        floorVB->Unmap(0, nullptr);

        floorIB->Map(0, nullptr, &pf);
        std::memcpy(pf, floorIdx.data(), sizeof(uint32_t) * floorIdx.size());
        floorIB->Unmap(0, nullptr);
    }

    floorVBV = {
        floorVB->GetGPUVirtualAddress(),
        static_cast<UINT>(sizeof(ConeVertex) * floorVerts.size()),
        static_cast<UINT>(sizeof(ConeVertex))
    };

    floorIBV = {
        floorIB->GetGPUVirtualAddress(),
        static_cast<UINT>(sizeof(uint32_t) * floorIdx.size()),
        DXGI_FORMAT_R32_UINT
    };

    floorIndexCount = static_cast<UINT>(floorIdx.size());

    // ---------------- геометрия куба ----------------
    std::vector<ConeVertex> cubeVerts;
    std::vector<uint32_t>   cubeIdx;

    {
        // front
        cubeVerts.push_back({ -0.5f, -0.5f,  0.5f,  0, 0,  1, 0, 1 });
        cubeVerts.push_back({ 0.5f, -0.5f,  0.5f,  0, 0,  1, 1, 1 });
        cubeVerts.push_back({ 0.5f,  0.5f,  0.5f,  0, 0,  1, 1, 0 });
        cubeVerts.push_back({ -0.5f,  0.5f,  0.5f,  0, 0,  1, 0, 0 });

        // back
        cubeVerts.push_back({ 0.5f, -0.5f, -0.5f,  0, 0, -1, 0, 1 });
        cubeVerts.push_back({ -0.5f, -0.5f, -0.5f,  0, 0, -1, 1, 1 });
        cubeVerts.push_back({ -0.5f,  0.5f, -0.5f,  0, 0, -1, 1, 0 });
        cubeVerts.push_back({ 0.5f,  0.5f, -0.5f,  0, 0, -1, 0, 0 });

        // left
        cubeVerts.push_back({ -0.5f, -0.5f, -0.5f, -1, 0, 0, 0, 1 });
        cubeVerts.push_back({ -0.5f, -0.5f,  0.5f, -1, 0, 0, 1, 1 });
        cubeVerts.push_back({ -0.5f,  0.5f,  0.5f, -1, 0, 0, 1, 0 });
        cubeVerts.push_back({ -0.5f,  0.5f, -0.5f, -1, 0, 0, 0, 0 });

        // right
        cubeVerts.push_back({ 0.5f, -0.5f,  0.5f,  1, 0, 0, 0, 1 });
        cubeVerts.push_back({ 0.5f, -0.5f, -0.5f,  1, 0, 0, 1, 1 });
        cubeVerts.push_back({ 0.5f,  0.5f, -0.5f,  1, 0, 0, 1, 0 });
        cubeVerts.push_back({ 0.5f,  0.5f,  0.5f,  1, 0, 0, 0, 0 });

        // bottom
        cubeVerts.push_back({ -0.5f, -0.5f, -0.5f,  0,-1, 0, 0, 1 });
        cubeVerts.push_back({ 0.5f, -0.5f, -0.5f,  0,-1, 0, 1, 1 });
        cubeVerts.push_back({ 0.5f, -0.5f,  0.5f,  0,-1, 0, 1, 0 });
        cubeVerts.push_back({ -0.5f, -0.5f,  0.5f,  0,-1, 0, 0, 0 });

        // top
        cubeVerts.push_back({ -0.5f,  0.5f,  0.5f,  0, 1, 0, 0, 1 });
        cubeVerts.push_back({ 0.5f,  0.5f,  0.5f,  0, 1, 0, 1, 1 });
        cubeVerts.push_back({ 0.5f,  0.5f, -0.5f,  0, 1, 0, 1, 0 });
        cubeVerts.push_back({ -0.5f,  0.5f, -0.5f,  0, 1, 0, 0, 0 });

        auto pushFace = [&](uint32_t i0, uint32_t i1, uint32_t i2, uint32_t i3)
            {
                cubeIdx.push_back(i0);
                cubeIdx.push_back(i1);
                cubeIdx.push_back(i2);
                cubeIdx.push_back(i0);
                cubeIdx.push_back(i2);
                cubeIdx.push_back(i3);
            };

        pushFace(0, 1, 2, 3);
        pushFace(4, 5, 6, 7);
        pushFace(8, 9, 10, 11);
        pushFace(12, 13, 14, 15);
        pushFace(16, 17, 18, 19);
        pushFace(20, 21, 22, 23);
    }

    auto cubeVBDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(ConeVertex) * cubeVerts.size());
    auto cubeIBDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(uint32_t) * cubeIdx.size());

    CHECK_HR(
        "Create Cube VB",
        device->CreateCommittedResource(
            &heapUpload, D3D12_HEAP_FLAG_NONE, &cubeVBDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&cubeVB)));

    CHECK_HR(
        "Create Cube IB",
        device->CreateCommittedResource(
            &heapUpload, D3D12_HEAP_FLAG_NONE, &cubeIBDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&cubeIB)));

    {
        void* p = nullptr;
        cubeVB->Map(0, nullptr, &p);
        std::memcpy(p, cubeVerts.data(), sizeof(ConeVertex) * cubeVerts.size());
        cubeVB->Unmap(0, nullptr);

        cubeIB->Map(0, nullptr, &p);
        std::memcpy(p, cubeIdx.data(), sizeof(uint32_t) * cubeIdx.size());
        cubeIB->Unmap(0, nullptr);
    }

    cubeVBV = {
        cubeVB->GetGPUVirtualAddress(),
        static_cast<UINT>(sizeof(ConeVertex) * cubeVerts.size()),
        static_cast<UINT>(sizeof(ConeVertex))
    };

    cubeIBV = {
        cubeIB->GetGPUVirtualAddress(),
        static_cast<UINT>(sizeof(uint32_t) * cubeIdx.size()),
        DXGI_FORMAT_R32_UINT
    };

    cubeIndexCount = static_cast<UINT>(cubeIdx.size());

    // Camera CB
    auto cbDesc = CD3DX12_RESOURCE_DESC::Buffer(256);

    CHECK_HR(
        "Create CameraCB",
        device->CreateCommittedResource(
            &heapUpload, D3D12_HEAP_FLAG_NONE, &cbDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&camCB)));

    // InstanceData: 3 конуса + пол + 3 маркера + куб
    auto instDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(InstanceDataCPU) * 8);

    CHECK_HR(
        "Create InstBuf",
        device->CreateCommittedResource(
            &heapUpload, D3D12_HEAP_FLAG_NONE, &instDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&instBuf)));

    // Буферы света
    numPointLights_ = 0;
    numSpotLights_ = 2;

    auto pointBufDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(PointLightCPU) * 1);

    CHECK_HR(
        "Create PointLightBuf",
        device->CreateCommittedResource(
            &heapUpload, D3D12_HEAP_FLAG_NONE, &pointBufDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&pointLightBuf)));

    {
        PointLightCPU dummy{};
        void* p = nullptr;
        pointLightBuf->Map(0, nullptr, &p);
        std::memcpy(p, &dummy, sizeof(dummy));
        pointLightBuf->Unmap(0, nullptr);
    }

    auto spotBufDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(SpotLightCPU) * numSpotLights_);

    CHECK_HR(
        "Create SpotLightBuf",
        device->CreateCommittedResource(
            &heapUpload, D3D12_HEAP_FLAG_NONE, &spotBufDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&spotLightBuf)));

    {
        float innerDeg = 12.0f;
        float outerDeg = 18.0f;
        float cosInner = cosf(XMConvertToRadians(innerDeg));
        float cosOuter = cosf(XMConvertToRadians(outerDeg));

        XMVECTOR dir = XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f);
        XMFLOAT3 dir3{};
        XMStoreFloat3(&dir3, dir);

        spot_[0].pos = XMFLOAT3(-3.0f, 2.0f, -1.0f);
        spot_[0].attK = 0.25f;
        spot_[0].dir = dir3;
        spot_[0].cosInner = cosInner;
        spot_[0].baseColor = XMFLOAT3(1.0f, 0.95f, 0.8f);
        spot_[0].cosOuter = cosOuter;
        spot_[0].strength = 1.2f;

        spot_[1].pos = XMFLOAT3(3.0f, 2.5f, 1.0f);
        spot_[1].attK = 0.25f;
        spot_[1].dir = dir3;
        spot_[1].cosInner = cosInner;
        spot_[1].baseColor = XMFLOAT3(0.8f, 0.9f, 1.0f);
        spot_[1].cosOuter = cosOuter;
        spot_[1].strength = 0.5f;
    }

    // --------- текстура для куба: DEFAULT + upload + UpdateSubresources ---------
    {
        std::vector<uint8_t> texData;
        UINT texW = 0;
        UINT texH = 0;

        if (!LoadTextureWIC(
            L"C:/Users/pinch/cg_labs/ComputerGr/lab1/assets/K6614gtn_big_poster_ds.jpg",
            texData, texW, texH))
        {
            texW = texH = 1;
            texData.assign(4, 0);
            texData[0] = 255;
            texData[2] = 255;
        }

        auto texDesc = CD3DX12_RESOURCE_DESC::Tex2D(
            DXGI_FORMAT_R8G8B8A8_UNORM,
            texW,
            texH,
            1, 1);

        auto heapDefault = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

        CHECK_HR(
            "Create Texture",
            device->CreateCommittedResource(
                &heapDefault,
                D3D12_HEAP_FLAG_NONE,
                &texDesc,
                D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr,
                IID_PPV_ARGS(&diffuseTex)));

        UINT64 uploadSize =
            GetRequiredIntermediateSize(diffuseTex.Get(), 0, 1);

        auto heapUploadTex = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        auto uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);

        ComPtr<ID3D12Resource> texUpload;

        CHECK_HR(
            "Create Texture Upload",
            device->CreateCommittedResource(
                &heapUploadTex,
                D3D12_HEAP_FLAG_NONE,
                &uploadDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(&texUpload)));

        D3D12_SUBRESOURCE_DATA sub{};
        sub.pData = texData.data();
        sub.RowPitch = texW * 4;
        sub.SlicePitch = sub.RowPitch * texH;

        ComPtr<ID3D12CommandAllocator>    alloc;
        ComPtr<ID3D12GraphicsCommandList> cmd;

        CHECK_HR(
            "Create Alloc",
            device->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                IID_PPV_ARGS(&alloc)));

        CHECK_HR(
            "Create CmdList",
            device->CreateCommandList(
                0,
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                alloc.Get(),
                nullptr,
                IID_PPV_ARGS(&cmd)));

        UpdateSubresources(
            cmd.Get(),
            diffuseTex.Get(),
            texUpload.Get(),
            0, 0, 1,
            &sub);

        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            diffuseTex.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        cmd->ResourceBarrier(1, &barrier);
        cmd->Close();

        ID3D12CommandQueue* q = core.Queue();
        ID3D12CommandList* lists[] = { cmd.Get() };
        q->ExecuteCommandLists(1, lists);

        ComPtr<ID3D12Fence> fence;
        UINT64 fenceValue = 1;

        CHECK_HR(
            "Create Fence",
            device->CreateFence(
                0,
                D3D12_FENCE_FLAG_NONE,
                IID_PPV_ARGS(&fence)));

        HANDLE evt = CreateEvent(nullptr, FALSE, FALSE, nullptr);

        q->Signal(fence.Get(), fenceValue);
        if (fence->GetCompletedValue() < fenceValue)
        {
            fence->SetEventOnCompletion(fenceValue, evt);
            WaitForSingleObject(evt, INFINITE);
        }

        CloseHandle(evt);
    }

    // --------- SRV heap: instances (t0), point (t1), spot (t2), texture (t3) ---------
    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.NumDescriptors = 4;
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    CHECK_HR(
        "Create SRV heap",
        device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&srvHeap)));

    srvGpu = srvHeap->GetGPUDescriptorHandleForHeapStart();

    UINT srvInc = device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_CPU_DESCRIPTOR_HANDLE hCPU =
        srvHeap->GetCPUDescriptorHandleForHeapStart();

    // t0: gInstances
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = DXGI_FORMAT_UNKNOWN;
        srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Buffer.FirstElement = 0;
        srv.Buffer.NumElements = 8;
        srv.Buffer.StructureByteStride = sizeof(InstanceDataCPU);

        device->CreateShaderResourceView(
            instBuf.Get(), &srv, hCPU);
    }

    // t1: gPointLights
    hCPU.ptr += srvInc;
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = DXGI_FORMAT_UNKNOWN;
        srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Buffer.FirstElement = 0;
        srv.Buffer.NumElements = 1;
        srv.Buffer.StructureByteStride = sizeof(PointLightCPU);

        device->CreateShaderResourceView(
            pointLightBuf.Get(), &srv, hCPU);
    }

    // t2: gSpotLights
    hCPU.ptr += srvInc;
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = DXGI_FORMAT_UNKNOWN;
        srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Buffer.FirstElement = 0;
        srv.Buffer.NumElements = numSpotLights_;
        srv.Buffer.StructureByteStride = sizeof(SpotLightCPU);

        device->CreateShaderResourceView(
            spotLightBuf.Get(), &srv, hCPU);
    }

    // t3: текстура
    hCPU.ptr += srvInc;
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels = 1;
        srv.Texture2D.MostDetailedMip = 0;
        srv.Texture2D.ResourceMinLODClamp = 0.0f;

        device->CreateShaderResourceView(
            diffuseTex.Get(), &srv, hCPU);
    }

    // --------- проекция / камера ---------
    baseAspect_ = (core.Height() > 0)
        ? static_cast<float>(core.Width()) / static_cast<float>(core.Height())
        : (16.0f / 9.0f);

    float vfov = XMConvertToRadians(60.0f);
    proj_ = XMMatrixPerspectiveFovLH(
        vfov, baseAspect_, 0.1f, 100.0f);

    ResetCamera();
    return true;
}

// ---------------- управление камерой ----------------

void ConeScene::MoveCameraLocal(float dx, float dy, float dz)
{
    XMVECTOR delta = XMVectorSet(dx, dy, dz, 0.0f);
    XMMATRIX rot = XMMatrixRotationRollPitchYaw(camPitch_, camYaw_, 0.0f);
    delta = XMVector3Transform(delta, rot);

    XMVECTOR pos = XMLoadFloat3(&camPos_);
    pos = XMVectorAdd(pos, delta);
    XMStoreFloat3(&camPos_, pos);
}

void ConeScene::RotateCamera(float dYaw, float dPitch)
{
    camYaw_ += dYaw;
    camPitch_ += dPitch;

    const float limit = XM_PIDIV2 - 0.01f;
    if (camPitch_ > limit) camPitch_ = limit;
    if (camPitch_ < -limit) camPitch_ = -limit;
}

void ConeScene::ResetCamera()
{
    camPos_ = XMFLOAT3{ 0.0f, 1.0f, -4.0f };
    camYaw_ = 0.0f;
    camPitch_ = XMConvertToRadians(-15.0f);
}

// ---------------- отрисовка кадра ----------------

void ConeScene::Render(D3D12Core& core, D3D12_CPU_DESCRIPTOR_HANDLE rtv)
{
    ID3D12GraphicsCommandList* list = core.CL();

    // View / World камеры
    XMMATRIX camRot = XMMatrixRotationRollPitchYaw(camPitch_, camYaw_, 0.0f);
    XMMATRIX camTrans = XMMatrixTranslation(camPos_.x, camPos_.y, camPos_.z);
    XMMATRIX camWorld = camRot * camTrans;
    XMMATRIX view = XMMatrixInverse(nullptr, camWorld);

    XMFLOAT3 dirLightPos{ 0.0f, 3.0f, 0.0f };

    // CameraCB
    CameraCBData cbd{};
    XMStoreFloat4x4(&cbd.viewProj, XMMatrixTranspose(view * proj_));
    cbd.camPos = camPos_;

    cbd.ambientColor = XMFLOAT3(
        ambientBase_.x * ambientStrength_,
        ambientBase_.y * ambientStrength_,
        ambientBase_.z * ambientStrength_);

    {
        XMVECTOR d = XMVector3Normalize(
            XMVectorSet(-0.4f, -1.0f, -0.3f, 0.0f));
        XMStoreFloat3(&cbd.dirLightDir, d);

        XMFLOAT3 dirCol(
            dirColorBase_.x * dirStrength_,
            dirColorBase_.y * dirStrength_,
            dirColorBase_.z * dirStrength_);
        cbd.dirLightColor = dirCol;

        XMVECTOR pos = XMVectorScale(-d, 8.0f);
        XMStoreFloat3(&dirLightPos, pos);
    }

    {
        void* pCam = nullptr;
        camCB->Map(0, nullptr, &pCam);
        std::memcpy(pCam, &cbd, sizeof(cbd));
        camCB->Unmap(0, nullptr);
    }

    // Обновляем прожекторы
    {
        SpotLightCPU gpu[2]{};

        for (UINT k = 0; k < numSpotLights_; ++k)
        {
            gpu[k].pos = spot_[k].pos;
            gpu[k].attK = spot_[k].attK;
            gpu[k].dir = spot_[k].dir;
            gpu[k].cosInner = spot_[k].cosInner;
            gpu[k].color = spot_[k].baseColor;
            gpu[k].cosOuter = spot_[k].cosOuter;

            gpu[k].color.x *= spot_[k].strength;
            gpu[k].color.y *= spot_[k].strength;
            gpu[k].color.z *= spot_[k].strength;
        }

        void* p = nullptr;
        spotLightBuf->Map(0, nullptr, &p);
        std::memcpy(p, gpu, sizeof(SpotLightCPU) * numSpotLights_);
        spotLightBuf->Unmap(0, nullptr);
    }

    // viewport crop под baseAspect_
    int VW = core.Width();
    int VH = core.Height();
    float currentAspect = (VH > 0)
        ? static_cast<float>(VW) / static_cast<float>(VH)
        : baseAspect_;

    int bx = 0, by = 0, bw = VW, bh = VH;
    if (currentAspect > baseAspect_)
    {
        bw = static_cast<int>(VH * baseAspect_);
        bx = (VW - bw) / 2;
    }
    else if (currentAspect < baseAspect_)
    {
        bh = static_cast<int>(VW / baseAspect_);
        by = (VH - bh) / 2;
    }

    const D3D12_VIEWPORT vpFull = core.Viewport();
    D3D12_RECT sc{ bx, by, bx + bw, by + bh };

    // время / анимация конусов
    using Clock = std::chrono::steady_clock;
    static Clock::time_point prev = Clock::now();
    Clock::time_point now = Clock::now();
    float dt = std::chrono::duration<float>(now - prev).count();
    prev = now;

    angle_ += dt * spinSpeed_;

    // InstanceData
    InstanceDataCPU inst[8];

    auto xm2worldT = [](const XMMATRIX& M, XMFLOAT4X4& dst)
        {
            XMStoreFloat4x4(&dst, XMMatrixTranspose(M));
        };

    MaterialCPU coneMat{};
    coneMat.albedo = XMFLOAT3(0.93f, 0.35f, 0.15f);
    coneMat.shininess = 32.0f;
    coneMat.specColor = XMFLOAT3(1.0f, 1.0f, 1.0f);

    MaterialCPU floorMat{};
    floorMat.albedo = XMFLOAT3(0.85f, 0.85f, 0.85f);
    floorMat.shininess = 16.0f;
    floorMat.specColor = XMFLOAT3(0.2f, 0.2f, 0.2f);

    MaterialCPU markerMat{};
    markerMat.albedo = XMFLOAT3(1.0f, 1.0f, 1.0f);
    markerMat.shininess = 1.0f;
    markerMat.specColor = XMFLOAT3(0.0f, 0.0f, 0.0f);

    MaterialCPU cubeMat{};
    cubeMat.albedo = XMFLOAT3(1.0f, 1.0f, 1.0f);
    cubeMat.shininess = 32.0f;
    cubeMat.specColor = XMFLOAT3(1.0f, 1.0f, 1.0f);

    // конусы
    XMFLOAT3 s{ 0.12f, 0.30f, 0.12f };
    float y = 0.0f;
    float xL = -0.9f;
    float xR = 0.9f;
    float z = -0.5f;

    XMMATRIX rot = XMMatrixRotationY(angle_);

    XMMATRIX w0 = XMMatrixScaling(s.x, s.y, s.z) *
        rot *
        XMMatrixTranslation(xL, y, z);
    XMMATRIX w1 = XMMatrixScaling(s.x, s.y, s.z) *
        rot *
        XMMatrixTranslation(xR, y, z);
    XMMATRIX w2 = XMMatrixScaling(s.x, s.y, s.z) *
        rot *
        XMMatrixTranslation(0.0f, y, z);

    xm2worldT(w0, inst[0].world);
    inst[0].tag = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);
    inst[0].mat = coneMat;

    xm2worldT(w1, inst[1].world);
    inst[1].tag = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);
    inst[1].mat = coneMat;

    xm2worldT(w2, inst[2].world);
    inst[2].tag = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);
    inst[2].mat = coneMat;

    // пол
    XMMATRIX wf = XMMatrixScaling(20.0f, 1.0f, 20.0f) *
        XMMatrixTranslation(0.0f, -0.3f, 0.0f);
    xm2worldT(wf, inst[3].world);
    inst[3].tag = XMFLOAT4(1.0f, 0.0f, 0.0f, 0.0f);
    inst[3].mat = floorMat;

    // маркер направленного света
    XMMATRIX wDir =
        XMMatrixScaling(0.3f, 1.0f, 0.3f) *
        XMMatrixTranslation(dirLightPos.x, dirLightPos.y, dirLightPos.z);
    xm2worldT(wDir, inst[4].world);
    inst[4].tag = XMFLOAT4(2.0f, 0.0f, 0.0f, 0.0f);
    inst[4].mat = markerMat;

    // маркеры прожекторов
    XMMATRIX wS0 =
        XMMatrixScaling(0.25f, 1.0f, 0.25f) *
        XMMatrixTranslation(spot_[0].pos.x, spot_[0].pos.y, spot_[0].pos.z);
    xm2worldT(wS0, inst[5].world);
    inst[5].tag = XMFLOAT4(3.0f, 0.0f, 0.0f, 0.0f);
    inst[5].mat = markerMat;

    XMMATRIX wS1 =
        XMMatrixScaling(0.25f, 1.0f, 0.25f) *
        XMMatrixTranslation(spot_[1].pos.x, spot_[1].pos.y, spot_[1].pos.z);
    xm2worldT(wS1, inst[6].world);
    inst[6].tag = XMFLOAT4(4.0f, 0.0f, 0.0f, 0.0f);
    inst[6].mat = markerMat;

    // куб с текстурой
    XMMATRIX wCube =
        XMMatrixScaling(0.8f, 0.8f, 0.8f) *
        XMMatrixTranslation(0.0f, 0.8f, 1.5f);
    xm2worldT(wCube, inst[7].world);
    inst[7].tag = XMFLOAT4(10.0f, 0.0f, 0.0f, 0.0f); // просто другой tag
    inst[7].mat = cubeMat;

    {
        void* pInst = nullptr;
        instBuf->Map(0, nullptr, &pInst);
        std::memcpy(pInst, inst, sizeof(inst));
        instBuf->Unmap(0, nullptr);
    }

    // биндинг и отрисовка
    ID3D12DescriptorHeap* heaps[] = { srvHeap.Get() };
    list->SetDescriptorHeaps(1, heaps);
    list->SetGraphicsRootSignature(rootSig.Get());
    list->SetPipelineState(pso.Get());
    list->RSSetViewports(1, &vpFull);
    list->RSSetScissorRects(1, &sc);
    list->SetGraphicsRootConstantBufferView(0, camCB->GetGPUVirtualAddress());
    list->SetGraphicsRootDescriptorTable(1, srvGpu);

    const float clear[4] = { 0.2f, 0.2f, 0.2f, 1.0f };
    list->ClearRenderTargetView(rtv, clear, 0, nullptr);

    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    uint32_t objCB[3]{};
    objCB[1] = numPointLights_;
    objCB[2] = numSpotLights_;

    // пол + маркеры (instances 3..6)
    list->IASetVertexBuffers(0, 1, &floorVBV);
    list->IASetIndexBuffer(&floorIBV);
    objCB[0] = 3u;
    list->SetGraphicsRoot32BitConstants(2, 3, objCB, 0);
    list->DrawIndexedInstanced(floorIndexCount, 4, 0, 0, 0);

    // конусы (0..2)
    list->IASetVertexBuffers(0, 1, &vbv);
    list->IASetIndexBuffer(&ibv);
    objCB[0] = 0u;
    list->SetGraphicsRoot32BitConstants(2, 3, objCB, 0);
    list->DrawIndexedInstanced(indexCount, 3, 0, 0, 0);

    // куб (7)
    list->IASetVertexBuffers(0, 1, &cubeVBV);
    list->IASetIndexBuffer(&cubeIBV);
    objCB[0] = 7u;
    list->SetGraphicsRoot32BitConstants(2, 3, objCB, 0);
    list->DrawIndexedInstanced(cubeIndexCount, 1, 0, 0, 0);
}
