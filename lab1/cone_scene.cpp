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

// структура должна совпадать с CameraCB в HLSL
struct CameraCBData {
    DirectX::XMFLOAT4X4 viewProj;

    DirectX::XMFLOAT3 camPos;
    float             _pad0;

    DirectX::XMFLOAT3 ambientColor;
    float             _pad1;

    DirectX::XMFLOAT3 dirLightDir;
    float             _pad2;

    DirectX::XMFLOAT3 dirLightColor;
    float             _pad3;

    DirectX::XMFLOAT4 spotPosRange[2];
    DirectX::XMFLOAT4 spotDirInner[2];
    DirectX::XMFLOAT4 spotColorOuter[2];
};

bool ConeScene::Init(D3D12Core& core) {
    ID3D12Device* device = core.Dev();

    // ---------- Root signature: b0 (камера+свет), t0 (instances), b1 (baseInstance) ----------
    CD3DX12_ROOT_PARAMETER rp[3];
    rp[0].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL); // b0
    CD3DX12_DESCRIPTOR_RANGE range;
    range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);                 // t0
    rp[1].InitAsDescriptorTable(1, &range, D3D12_SHADER_VISIBILITY_ALL);
    rp[2].InitAsConstants(1, 1, 0, D3D12_SHADER_VISIBILITY_ALL);       // b1: gBaseInstance

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

    {
        D3D12_DEPTH_STENCIL_DESC ds{};
        ds.DepthEnable = TRUE;
        ds.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        ds.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        ds.StencilEnable = FALSE;
        psoDesc.DepthStencilState = ds;
    }

    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT; // если в core нет DSV — можно поставить UNKNOWN
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

    // ---------- SRV heap для InstanceData ----------
    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.NumDescriptors = 1;
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    CHECK_HR("Create SRV heap",
        device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&srvHeap)));
    srvGpu = srvHeap->GetGPUDescriptorHandleForHeapStart();

    // ---------- Camera CB (256 байт) ----------
    auto cbDesc = CD3DX12_RESOURCE_DESC::Buffer(256);
    CHECK_HR("Create CameraCB",
        device->CreateCommittedResource(
            &heapUpload, D3D12_HEAP_FLAG_NONE, &cbDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&camCB)));

    // ---------- InstanceData: 3 конуса + пол + 3 маркера = 7 ----------
    struct InstanceDataCPU { float m[16]; float tag[4]; };

    InstanceDataCPU inst[7]{};

    auto xm2arrT = [](const DirectX::XMMATRIX& M, float* out16) {
        DirectX::XMFLOAT4X4 t;
        DirectX::XMStoreFloat4x4(&t, DirectX::XMMatrixTranspose(M));
        std::memcpy(out16, &t, sizeof(t));
        };

    DirectX::XMFLOAT3 s{ 0.12f, 0.30f, 0.12f };
    float y = 0.0f;
    float xL = -0.9f, xR = +0.9f, z = -0.5f;

    DirectX::XMMATRIX w0 = DirectX::XMMatrixScaling(s.x, s.y, s.z) *
        DirectX::XMMatrixTranslation(xL, y, z);
    DirectX::XMMATRIX w1 = DirectX::XMMatrixScaling(s.x, s.y, s.z) *
        DirectX::XMMatrixTranslation(xR, y, z);
    DirectX::XMMATRIX w2 = DirectX::XMMatrixScaling(s.x, s.y, s.z) *
        DirectX::XMMatrixTranslation(0.0f, y, z);

    xm2arrT(w0, inst[0].m); inst[0].tag[0] = 0.0f; // конусы
    xm2arrT(w1, inst[1].m); inst[1].tag[0] = 0.0f;
    xm2arrT(w2, inst[2].m); inst[2].tag[0] = 0.0f;

    DirectX::XMMATRIX wf = DirectX::XMMatrixScaling(20.0f, 1.0f, 20.0f) *
        DirectX::XMMatrixTranslation(0.0f, -0.3f, 0.0f);
    xm2arrT(wf, inst[3].m); inst[3].tag[0] = 1.0f; // пол

    // маркеры инициализируем чем-нибудь, всё равно будут переписаны в Render
    xm2arrT(DirectX::XMMatrixIdentity(), inst[4].m); inst[4].tag[0] = 2.0f;
    xm2arrT(DirectX::XMMatrixIdentity(), inst[5].m); inst[5].tag[0] = 3.0f;
    xm2arrT(DirectX::XMMatrixIdentity(), inst[6].m); inst[6].tag[0] = 4.0f;

    auto instDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(inst));
    CHECK_HR("Create InstBuf",
        device->CreateCommittedResource(
            &heapUpload, D3D12_HEAP_FLAG_NONE, &instDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&instBuf)));

    {
        void* p2 = nullptr;
        instBuf->Map(0, nullptr, &p2);
        std::memcpy(p2, inst, sizeof(inst));
        instBuf->Unmap(0, nullptr);
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = DXGI_FORMAT_UNKNOWN;
    srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Buffer.FirstElement = 0;
    srv.Buffer.NumElements = 7;
    srv.Buffer.StructureByteStride = sizeof(InstanceDataCPU);

    device->CreateShaderResourceView(
        instBuf.Get(), &srv, srvHeap->GetCPUDescriptorHandleForHeapStart());

    // ---------- Проекция и камера ----------
    baseAspect_ = (core.Height() > 0)
        ? float(core.Width()) / float(core.Height())
        : (16.0f / 9.0f);
    float vfov = DirectX::XMConvertToRadians(60.0f);
    proj_ = DirectX::XMMatrixPerspectiveFovLH(vfov, baseAspect_, 0.1f, 100.0f);

    ResetCamera();
    return true;
}

// камера
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
    camPos_ = XMFLOAT3{ 0.0f, 1.0f, -4.0f }; // чуть дальше, чтобы видеть пол
    camYaw_ = 0.0f;
    camPitch_ = XMConvertToRadians(-15.0f);
}

void ConeScene::Render(D3D12Core& core, D3D12_CPU_DESCRIPTOR_HANDLE rtv) {
    auto* list = core.CL();
    using namespace DirectX;

    // ---------- View / Proj ----------
    XMMATRIX camRot = XMMatrixRotationRollPitchYaw(camPitch_, camYaw_, 0.0f);
    XMMATRIX camTrans = XMMatrixTranslation(camPos_.x, camPos_.y, camPos_.z);
    XMMATRIX camWorld = camRot * camTrans;
    XMMATRIX view = XMMatrixInverse(nullptr, camWorld);

    // Псевдо-позиции источников для маркеров
    XMFLOAT3 dirLightPos{ 0.0f, 3.0f, 0.0f };
    XMFLOAT3 spotPos[2]{};

    // ---------- Camera / lights CB ----------
    CameraCBData cbd{};
    XMStoreFloat4x4(&cbd.viewProj, XMMatrixTranspose(view * proj_));
    cbd.camPos = camPos_;
    cbd.ambientColor = XMFLOAT3(0.08f, 0.08f, 0.08f);

    // Направленный свет сверху-справа
    {
        XMVECTOR d = XMVector3Normalize(XMVectorSet(-0.4f, -1.0f, -0.3f, 0.0f));
        XMStoreFloat3(&cbd.dirLightDir, d);
        cbd.dirLightColor = XMFLOAT3(0.6f, 0.6f, 0.7f);

        // Позиция маркера — вдоль -d, подальше от сцены
        XMVECTOR pos = XMVectorScale(-d, 8.0f);
        XMStoreFloat3(&dirLightPos, pos);
    }

    // Прожекторы: два «фонарика» под собой, разнесены
    {
        // узкий луч
        float innerDeg = 12.0f;
        float outerDeg = 18.0f;
        float cosInner = cosf(XMConvertToRadians(innerDeg));
        float cosOuter = cosf(XMConvertToRadians(outerDeg));

        XMVECTOR spotDir = XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f); // строго вниз

        // левый, ярче, ближе к центру
        {
            spotPos[0] = XMFLOAT3(-3.0f, 2.0f, -1.0f);
            cbd.spotPosRange[0] = XMFLOAT4(spotPos[0].x, spotPos[0].y, spotPos[0].z,
                0.25f); // k для 1/(1 + k d^2)

            XMFLOAT3 dir3;
            XMStoreFloat3(&dir3, spotDir);
            cbd.spotDirInner[0] = XMFLOAT4(dir3.x, dir3.y, dir3.z, cosInner);
            cbd.spotColorOuter[0] = XMFLOAT4(1.0f, 0.95f, 0.8f, cosOuter); // базовый цвет (ярче будет в PS)
        }

        // правый, дальше и слабее
        {
            spotPos[1] = XMFLOAT3(3.0f, 2.5f, 1.0f);
            cbd.spotPosRange[1] = XMFLOAT4(spotPos[1].x, spotPos[1].y, spotPos[1].z,
                0.25f);

            XMFLOAT3 dir3;
            XMStoreFloat3(&dir3, spotDir);
            cbd.spotDirInner[1] = XMFLOAT4(dir3.x, dir3.y, dir3.z, cosInner);
            cbd.spotColorOuter[1] = XMFLOAT4(0.8f, 0.9f, 1.0f, cosOuter); // базовый цвет (в PS ещё уменьшаем яркость)
        }
    }

    // Записываем CB
    {
        void* pCam = nullptr;
        camCB->Map(0, nullptr, &pCam);
        std::memcpy(pCam, &cbd, sizeof(cbd));
        camCB->Unmap(0, nullptr);
    }

    // ---------- viewport crop по aspect ----------
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
    using clock = std::chrono::steady_clock;
    static auto prev = clock::now();
    auto now = clock::now();
    float dt = std::chrono::duration<float>(now - prev).count();
    prev = now;
    angle_ += dt * spinSpeed_;

    // ---------- InstanceData на кадр ----------
    struct InstanceDataCPU { float m[16]; float tag[4]; };
    InstanceDataCPU inst[7];

    auto xm2arrT = [](const XMMATRIX& M, float* out16) {
        XMFLOAT4X4 t;
        XMStoreFloat4x4(&t, XMMatrixTranspose(M));
        std::memcpy(out16, &t, sizeof(t));
        };

    // Конусы
    XMFLOAT3 s{ 0.12f, 0.30f, 0.12f };
    float y = 0.0f;
    float xL = -0.9f, xR = +0.9f, z = -0.5f;

    XMMATRIX rot = XMMatrixRotationY(angle_);

    XMMATRIX w0 = XMMatrixScaling(s.x, s.y, s.z) * rot * XMMatrixTranslation(xL, y, z);
    XMMATRIX w1 = XMMatrixScaling(s.x, s.y, s.z) * rot * XMMatrixTranslation(xR, y, z);
    XMMATRIX w2 = XMMatrixScaling(s.x, s.y, s.z) * rot * XMMatrixTranslation(0.0f, y, z);

    xm2arrT(w0, inst[0].m); inst[0].tag[0] = 0.0f; // конусы
    xm2arrT(w1, inst[1].m); inst[1].tag[0] = 0.0f;
    xm2arrT(w2, inst[2].m); inst[2].tag[0] = 0.0f;

    // Пол
    XMMATRIX wf = XMMatrixScaling(20.0f, 1.0f, 20.0f) *
        XMMatrixTranslation(0.0f, -0.3f, 0.0f);
    xm2arrT(wf, inst[3].m); inst[3].tag[0] = 1.0f; // пол

    // Маркер направленного света
    XMMATRIX wDir =
        XMMatrixScaling(0.3f, 1.0f, 0.3f) *
        XMMatrixTranslation(dirLightPos.x, dirLightPos.y, dirLightPos.z);
    xm2arrT(wDir, inst[4].m); inst[4].tag[0] = 2.0f;

    // Маркеры прожекторов
    XMMATRIX wS0 =
        XMMatrixScaling(0.25f, 1.0f, 0.25f) *
        XMMatrixTranslation(spotPos[0].x, spotPos[0].y, spotPos[0].z);
    xm2arrT(wS0, inst[5].m); inst[5].tag[0] = 3.0f;

    XMMATRIX wS1 =
        XMMatrixScaling(0.25f, 1.0f, 0.25f) *
        XMMatrixTranslation(spotPos[1].x, spotPos[1].y, spotPos[1].z);
    xm2arrT(wS1, inst[6].m); inst[6].tag[0] = 4.0f;

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
    // если используется depth-buffer: ClearDepthStencilView(...)

    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Пол + 3 маркера (instances 3..6)
    list->IASetVertexBuffers(0, 1, &floorVBV);
    list->IASetIndexBuffer(&floorIBV);
    list->SetGraphicsRoot32BitConstant(2, 3u, 0); // gBaseInstance = 3
    list->DrawIndexedInstanced(floorIndexCount, 4, 0, 0, 0);

    // Конусы (instances 0..2)
    list->IASetVertexBuffers(0, 1, &vbv);
    list->IASetIndexBuffer(&ibv);
    list->SetGraphicsRoot32BitConstant(2, 0u, 0); // gBaseInstance = 0
    list->DrawIndexedInstanced(indexCount, 3, 0, 0, 0);
}
