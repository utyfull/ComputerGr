#include "cone_scene.hpp"
#include "d3d12_core.hpp"
#include "cone_mesh.hpp"
#include "dx_util.hpp"

#include <directx/d3dx12.h>
#include <d3dcompiler.h>
#pragma comment(lib, "d3dcompiler.lib")

#include <chrono>
#include <vector>
#include <cstring>

using namespace DirectX;

// ===== CPU-версия CameraCB (должна совпадать с HLSL CameraCB) =====

struct CameraCBData {
    XMFLOAT4X4 viewProj;

    XMFLOAT3 camPos;
    float    _pad0;

    XMFLOAT3 ambientColor;
    float    _pad1;

    XMFLOAT3 dirLightDir;
    float    _pad2;

    XMFLOAT3 dirLightColor;
    float    _pad3;
};

// ===== CPU-версии материала, инстанса и источников света =====

struct MaterialCPU {
    XMFLOAT3 albedo;
    float    shininess;
    XMFLOAT3 specColor;
    float    _pad;
};

struct InstanceDataCPU {
    XMFLOAT4X4 world;
    XMFLOAT4   tag;
    MaterialCPU mat;
};

// ДОЛЖНО совпадать с HLSL PointLight
struct PointLightCPU {
    XMFLOAT3 pos;   float attK;   // 1 / (1 + attK * d^2)
    XMFLOAT3 color; float _pad;
};

// ДОЛЖНО совпадать с HLSL SpotLight
struct SpotLightCPU {
    XMFLOAT3 pos;      float attK;
    XMFLOAT3 dir;      float cosInner;
    XMFLOAT3 color;    float cosOuter;
};

bool ConeScene::Init(D3D12Core& core) {
    ID3D12Device* device = core.Dev();

    // ---------- Root signature: b0 (камера), t0..t2 (SRV: instances + lights), b1 (ObjectCB как root-constants) ----------
    CD3DX12_ROOT_PARAMETER rp[3];

    // b0: CameraCB
    rp[0].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);

    // t0..t2: gInstances, gPointLights, gSpotLights
    CD3DX12_DESCRIPTOR_RANGE range;
    range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 0); // count = 3, baseShaderRegister = 0
    rp[1].InitAsDescriptorTable(1, &range, D3D12_SHADER_VISIBILITY_ALL);

    // b1: ObjectCB (gBaseInstance, gNumPointLights, gNumSpotLights) через root-constants
    rp[2].InitAsConstants(3, 1, 0, D3D12_SHADER_VISIBILITY_ALL); // 3 x uint, register(b1)

    D3D12_ROOT_SIGNATURE_DESC rs{};
    rs.NumParameters = _countof(rp);
    rs.pParameters = rp;
    rs.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    Microsoft::WRL::ComPtr<ID3DBlob> rsBlob, rsErr;
    CHECK_HR("SerializeRootSignature",
        D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1,
            &rsBlob, &rsErr));
    CHECK_HR("CreateRootSignature",
        device->CreateRootSignature(0,
            rsBlob->GetBufferPointer(),
            rsBlob->GetBufferSize(),
            IID_PPV_ARGS(&rootSig)));

    // ---------- Шейдеры ----------
    Microsoft::WRL::ComPtr<ID3DBlob> vs, ps;
    CHECK_HR("Read VS", D3DReadFileToBlob(L"shaders/ConesVS.cso", &vs));
    CHECK_HR("Read PS", D3DReadFileToBlob(L"shaders/ConesPS.cso", &ps));

    // ---------- Input layout ----------
    D3D12_INPUT_ELEMENT_DESC il[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    // ---------- PSO ----------
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
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT; // совпадает с depth-буфером
    psoDesc.SampleDesc.Count = 1;

    CHECK_HR("CreateGraphicsPipelineState",
        device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso)));

    // ---------- Геометрия конусов ----------
    std::vector<ConeVertex> verts;
    std::vector<uint32_t>   idx;
    BuildConeMesh(128, verts, idx);

    auto heapUpload = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    auto vbDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(ConeVertex) * verts.size());
    auto ibDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(uint32_t) * idx.size());

    CHECK_HR("Create VB",
        device->CreateCommittedResource(
            &heapUpload, D3D12_HEAP_FLAG_NONE, &vbDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&vb)));

    CHECK_HR("Create IB",
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
        (UINT)(sizeof(ConeVertex) * verts.size()),
        (UINT)sizeof(ConeVertex)
    };
    ibv = {
        ib->GetGPUVirtualAddress(),
        (UINT)(sizeof(uint32_t) * idx.size()),
        DXGI_FORMAT_R32_UINT
    };
    indexCount = (UINT)idx.size();

    // ---------- Геометрия пола / маркеров (квадрат в XZ) ----------
    std::vector<ConeVertex> floorVerts;
    std::vector<uint32_t>   floorIdx;

    {
        ConeVertex v0{ -1.0f, 0.0f, -1.0f,  0.0f, 1.0f, 0.0f };
        ConeVertex v1{ 1.0f, 0.0f, -1.0f,  0.0f, 1.0f, 0.0f };
        ConeVertex v2{ 1.0f, 0.0f,  1.0f,  0.0f, 1.0f, 0.0f };
        ConeVertex v3{ -1.0f, 0.0f,  1.0f,  0.0f, 1.0f, 0.0f };

        floorVerts = { v0, v1, v2, v3 };
        floorIdx = { 0, 1, 2, 0, 2, 3 };
    }

    auto floorVBDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(ConeVertex) * floorVerts.size());
    auto floorIBDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(uint32_t) * floorIdx.size());

    CHECK_HR("Create Floor VB",
        device->CreateCommittedResource(
            &heapUpload, D3D12_HEAP_FLAG_NONE, &floorVBDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&floorVB)));

    CHECK_HR("Create Floor IB",
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
        (UINT)(sizeof(ConeVertex) * floorVerts.size()),
        (UINT)sizeof(ConeVertex)
    };
    floorIBV = {
        floorIB->GetGPUVirtualAddress(),
        (UINT)(sizeof(uint32_t) * floorIdx.size()),
        DXGI_FORMAT_R32_UINT
    };
    floorIndexCount = (UINT)floorIdx.size();

    // ---------- Camera CB ----------
    auto cbDesc = CD3DX12_RESOURCE_DESC::Buffer(256);
    CHECK_HR("Create CameraCB",
        device->CreateCommittedResource(
            &heapUpload, D3D12_HEAP_FLAG_NONE, &cbDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&camCB)));

    // ---------- InstanceData buffer: 3 конуса + пол + 3 маркера = 7 ----------
    auto instDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(InstanceDataCPU) * 7);
    CHECK_HR("Create InstBuf",
        device->CreateCommittedResource(
            &heapUpload, D3D12_HEAP_FLAG_NONE, &instDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&instBuf)));

    // ---------- Буферы света (shader-storage) ----------
    numPointLights_ = 0;
    numSpotLights_ = 2;

    auto pointBufDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(PointLightCPU) * 1);
    CHECK_HR("Create PointLightBuf",
        device->CreateCommittedResource(
            &heapUpload, D3D12_HEAP_FLAG_NONE, &pointBufDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&pointLightBuf)));

    // Dummy-запись
    {
        PointLightCPU dummy{};
        void* p = nullptr;
        pointLightBuf->Map(0, nullptr, &p);
        std::memcpy(p, &dummy, sizeof(dummy));
        pointLightBuf->Unmap(0, nullptr);
    }

    auto spotBufDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(SpotLightCPU) * numSpotLights_);
    CHECK_HR("Create SpotLightBuf",
        device->CreateCommittedResource(
            &heapUpload, D3D12_HEAP_FLAG_NONE, &spotBufDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&spotLightBuf)));

    // Инициализируем параметры прожекторов (CPU-структура spot_)
    {
        float innerDeg = 12.0f;
        float outerDeg = 18.0f;
        float cosInner = cosf(XMConvertToRadians(innerDeg));
        float cosOuter = cosf(XMConvertToRadians(outerDeg));

        XMVECTOR dir = XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f);
        XMFLOAT3 dir3;
        XMStoreFloat3(&dir3, dir);

        // левый (ярче)
        spot_[0].pos = XMFLOAT3(-3.0f, 2.0f, -1.0f);
        spot_[0].attK = 0.25f;
        spot_[0].dir = dir3;
        spot_[0].cosInner = cosInner;
        spot_[0].baseColor = XMFLOAT3(1.0f, 0.95f, 0.8f);
        spot_[0].cosOuter = cosOuter;
        spot_[0].strength = 1.2f; // начальная «яркость»

        // правый (слабее)
        spot_[1].pos = XMFLOAT3(3.0f, 2.5f, 1.0f);
        spot_[1].attK = 0.25f;
        spot_[1].dir = dir3;
        spot_[1].cosInner = cosInner;
        spot_[1].baseColor = XMFLOAT3(0.8f, 0.9f, 1.0f);
        spot_[1].cosOuter = cosOuter;
        spot_[1].strength = 0.5f;
    }

    // ---------- SRV heap: instances (t0) + point lights (t1) + spot lights (t2) ----------
    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.NumDescriptors = 3;
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    CHECK_HR("Create SRV heap",
        device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&srvHeap)));
    srvGpu = srvHeap->GetGPUDescriptorHandleForHeapStart();

    UINT srvInc = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_CPU_DESCRIPTOR_HANDLE hCPU = srvHeap->GetCPUDescriptorHandleForHeapStart();

    // t0: gInstances
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = DXGI_FORMAT_UNKNOWN;
        srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Buffer.FirstElement = 0;
        srv.Buffer.NumElements = 7;
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

    // ---------- Проекция и камера ----------
    baseAspect_ = (core.Height() > 0)
        ? float(core.Width()) / float(core.Height())
        : (16.0f / 9.0f);
    float vfov = XMConvertToRadians(60.0f);
    proj_ = XMMatrixPerspectiveFovLH(vfov, baseAspect_, 0.1f, 100.0f);

    ResetCamera();
    return true;
}

// ---------- управление камерой ----------

void ConeScene::MoveCameraLocal(float dx, float dy, float dz) {
    XMVECTOR delta = XMVectorSet(dx, dy, dz, 0.0f);
    XMMATRIX rot = XMMatrixRotationRollPitchYaw(camPitch_, camYaw_, 0.0f);
    delta = XMVector3Transform(delta, rot);

    XMVECTOR pos = XMLoadFloat3(&camPos_);
    pos = XMVectorAdd(pos, delta);
    XMStoreFloat3(&camPos_, pos);
}

void ConeScene::RotateCamera(float dYaw, float dPitch) {
    camYaw_ += dYaw;
    camPitch_ += dPitch;

    const float limit = XM_PIDIV2 - 0.01f;
    if (camPitch_ > limit) camPitch_ = limit;
    if (camPitch_ < -limit) camPitch_ = -limit;
}

void ConeScene::ResetCamera() {
    camPos_ = XMFLOAT3{ 0.0f, 1.0f, -4.0f };
    camYaw_ = 0.0f;
    camPitch_ = XMConvertToRadians(-15.0f);
}

// ---------- Render ----------

void ConeScene::Render(D3D12Core& core, D3D12_CPU_DESCRIPTOR_HANDLE rtv) {
    auto* list = core.CL();

    // ---------- View / Proj ----------
    XMMATRIX camRot = XMMatrixRotationRollPitchYaw(camPitch_, camYaw_, 0.0f);
    XMMATRIX camTrans = XMMatrixTranslation(camPos_.x, camPos_.y, camPos_.z);
    XMMATRIX camWorld = camRot * camTrans;
    XMMATRIX view = XMMatrixInverse(nullptr, camWorld);

    // Псевдо-позиция направленного источника для маркера
    XMFLOAT3 dirLightPos{ 0.0f, 3.0f, 0.0f };

    // ---------- Camera CB ----------
    CameraCBData cbd{};
    XMStoreFloat4x4(&cbd.viewProj, XMMatrixTranspose(view * proj_));
    cbd.camPos = camPos_;

    // ambient с множителем
    cbd.ambientColor = XMFLOAT3(
        ambientBase_.x * ambientStrength_,
        ambientBase_.y * ambientStrength_,
        ambientBase_.z * ambientStrength_);

    // Направленный свет сверху-справа
    {
        XMVECTOR d = XMVector3Normalize(XMVectorSet(-0.4f, -1.0f, -0.3f, 0.0f));
        XMStoreFloat3(&cbd.dirLightDir, d);

        XMFLOAT3 dirCol(
            dirColorBase_.x * dirStrength_,
            dirColorBase_.y * dirStrength_,
            dirColorBase_.z * dirStrength_);
        cbd.dirLightColor = dirCol;

        // Позиция маркера — вдоль -d
        XMVECTOR pos = XMVectorScale(-d, 8.0f);
        XMStoreFloat3(&dirLightPos, pos);
    }

    {
        void* pCam = nullptr;
        camCB->Map(0, nullptr, &pCam);
        std::memcpy(pCam, &cbd, sizeof(cbd));
        camCB->Unmap(0, nullptr);
    }

    // ---------- Обновляем буфер прожекторов из CPU-параметров ----------
    {
        SpotLightCPU gpu[2]{};

        for (UINT k = 0; k < numSpotLights_; ++k) {
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

    // ---------- viewport crop под baseAspect_ ----------
    int VW = core.Width();
    int VH = core.Height();
    float currentAspect = (VH > 0) ? float(VW) / float(VH) : baseAspect_;
    int bx = 0, by = 0, bw = VW, bh = VH;
    if (currentAspect > baseAspect_) {
        bw = int(VH * baseAspect_);
        bx = (VW - bw) / 2;
    }
    else if (currentAspect < baseAspect_) {
        bh = int(VW / baseAspect_);
        by = (VH - bh) / 2;
    }
    const D3D12_VIEWPORT vpFull = core.Viewport();
    D3D12_RECT sc{ bx, by, bx + bw, by + bh };

    // ---------- Время / вращение конусов ----------
    using Clock = std::chrono::steady_clock;
    static Clock::time_point prev = Clock::now();
    Clock::time_point now = Clock::now();
    float dt = std::chrono::duration<float>(now - prev).count();
    prev = now;
    angle_ += dt * spinSpeed_;

    // ---------- InstanceData на кадр ----------
    InstanceDataCPU inst[7];

    auto xm2worldT = [](const XMMATRIX& M, XMFLOAT4X4& dst) {
        XMStoreFloat4x4(&dst, XMMatrixTranspose(M));
        };

    // Материалы
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

    // Конусы
    XMFLOAT3 s{ 0.12f, 0.30f, 0.12f };
    float y = 0.0f;
    float xL = -0.9f, xR = +0.9f, z = -0.5f;

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

    // Пол
    XMMATRIX wf = XMMatrixScaling(20.0f, 1.0f, 20.0f) *
        XMMatrixTranslation(0.0f, -0.3f, 0.0f);
    xm2worldT(wf, inst[3].world);
    inst[3].tag = XMFLOAT4(1.0f, 0.0f, 0.0f, 0.0f);
    inst[3].mat = floorMat;

    // Маркер направленного света
    XMMATRIX wDir =
        XMMatrixScaling(0.3f, 1.0f, 0.3f) *
        XMMatrixTranslation(dirLightPos.x, dirLightPos.y, dirLightPos.z);
    xm2worldT(wDir, inst[4].world);
    inst[4].tag = XMFLOAT4(2.0f, 0.0f, 0.0f, 0.0f);
    inst[4].mat = markerMat;

    // Маркеры прожекторов — позиции берём из spot_[]
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

    {
        void* pInst = nullptr;
        instBuf->Map(0, nullptr, &pInst);
        std::memcpy(pInst, inst, sizeof(inst));
        instBuf->Unmap(0, nullptr);
    }

    // ---------- Биндинг и отрисовка ----------
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

    uint32_t objCB[3];
    objCB[1] = numPointLights_; // gNumPointLights
    objCB[2] = numSpotLights_;  // gNumSpotLights

    // Пол + 3 маркера (instances 3..6)
    list->IASetVertexBuffers(0, 1, &floorVBV);
    list->IASetIndexBuffer(&floorIBV);
    objCB[0] = 3u; // gBaseInstance
    list->SetGraphicsRoot32BitConstants(2, 3, objCB, 0);
    list->DrawIndexedInstanced(floorIndexCount, 4, 0, 0, 0);

    // Конусы (instances 0..2)
    list->IASetVertexBuffers(0, 1, &vbv);
    list->IASetIndexBuffer(&ibv);
    objCB[0] = 0u; // gBaseInstance
    list->SetGraphicsRoot32BitConstants(2, 3, objCB, 0);
    list->DrawIndexedInstanced(indexCount, 3, 0, 0, 0);
}
