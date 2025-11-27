#include "cone_scene.hpp"
#include "d3d12_core.hpp"
#include "cone_mesh.hpp"
#include "dx_util.hpp"

#include <directx/d3dx12.h>
#include <d3dcompiler.h>
#pragma comment(lib, "d3dcompiler.lib")

#include <chrono>
#include <cstring>
#include <vector>

using namespace DirectX;

bool ConeScene::Init(D3D12Core& core) {
    ID3D12Device* device = core.Dev();

    CD3DX12_ROOT_PARAMETER rp[2];
    rp[0].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
    CD3DX12_DESCRIPTOR_RANGE range; range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    rp[1].InitAsDescriptorTable(1, &range, D3D12_SHADER_VISIBILITY_ALL);

    D3D12_ROOT_SIGNATURE_DESC rs{};
    rs.NumParameters = _countof(rp); rs.pParameters = rp;
    rs.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    Microsoft::WRL::ComPtr<ID3DBlob> rsBlob, rsErr;
    CHECK_HR("SerializeRootSignature",
        D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsErr));
    CHECK_HR("CreateRootSignature",
        device->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(),
            IID_PPV_ARGS(&rootSig)));

    Microsoft::WRL::ComPtr<ID3DBlob> vs, ps;
    CHECK_HR("Read VS", D3DReadFileToBlob(L"shaders/ConesVS.cso", &vs));
    CHECK_HR("Read PS", D3DReadFileToBlob(L"shaders/ConesPS.cso", &ps));

    D3D12_INPUT_ELEMENT_DESC il[] = {
        {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"NORMAL",  0,DXGI_FORMAT_R32G32B32_FLOAT,0,12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
    };
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.InputLayout = { il, _countof(il) };
    psoDesc.pRootSignature = rootSig.Get();
    psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    { D3D12_DEPTH_STENCIL_DESC ds{}; ds.DepthEnable = FALSE; ds.StencilEnable = FALSE; psoDesc.DepthStencilState = ds; }
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
    psoDesc.SampleDesc.Count = 1;
    CHECK_HR("CreateGraphicsPipelineState",
        device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso)));

    std::vector<ConeVertex> verts;
    std::vector<uint32_t>   idx;
    BuildConeMesh(128, verts, idx);

    auto heapUpload = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    auto vbDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(ConeVertex) * verts.size());
    auto ibDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(uint32_t) * idx.size());

    CHECK_HR("Create VB", device->CreateCommittedResource(&heapUpload, D3D12_HEAP_FLAG_NONE, &vbDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&vb)));
    CHECK_HR("Create IB", device->CreateCommittedResource(&heapUpload, D3D12_HEAP_FLAG_NONE, &ibDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&ib)));

    void* p = nullptr;
    vb->Map(0, nullptr, &p); memcpy(p, verts.data(), sizeof(ConeVertex) * verts.size()); vb->Unmap(0, nullptr);
    ib->Map(0, nullptr, &p); memcpy(p, idx.data(), sizeof(uint32_t) * idx.size()); ib->Unmap(0, nullptr);

    vbv = { vb->GetGPUVirtualAddress(), (UINT)(sizeof(ConeVertex) * verts.size()), (UINT)sizeof(ConeVertex) };
    ibv = { ib->GetGPUVirtualAddress(), (UINT)(sizeof(uint32_t) * idx.size()), DXGI_FORMAT_R32_UINT };
    indexCount = (UINT)idx.size();

    D3D12_DESCRIPTOR_HEAP_DESC hd{}; hd.NumDescriptors = 1;
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    CHECK_HR("Create SRV heap", device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&srvHeap)));
    srvGpu = srvHeap->GetGPUDescriptorHandleForHeapStart();

    auto cbDesc = CD3DX12_RESOURCE_DESC::Buffer(256);
    CHECK_HR("Create CameraCB", device->CreateCommittedResource(&heapUpload, D3D12_HEAP_FLAG_NONE, &cbDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&camCB)));

    struct InstanceData { float m[16]; };
    auto xm2arrT = [](const XMMATRIX& M, float* out16) {
        XMFLOAT4X4 t; XMStoreFloat4x4(&t, XMMatrixTranspose(M)); memcpy(out16, &t, sizeof(t));
        };
    InstanceData inst[3];

    XMFLOAT3 s0{ 0.08f,0.22f,0.08f }, s1{ 0.14f,0.35f,0.14f }, s2{ 0.10f,0.28f,0.10f };
    float xL = -0.9f, xR = +0.9f, z = -0.5f, y2 = +0.45f;
    XMMATRIX w0 = XMMatrixScaling(s0.x, s0.y, s0.z) * XMMatrixTranslation(xL, 0.0f, z);
    XMMATRIX w1 = XMMatrixScaling(s1.x, s1.y, s1.z) * XMMatrixTranslation(xR, 0.0f, z);
    XMMATRIX w2 = XMMatrixScaling(s2.x, s2.y, s2.z) * XMMatrixTranslation(0.0f, y2, z);
    xm2arrT(w0, inst[0].m); xm2arrT(w1, inst[1].m); xm2arrT(w2, inst[2].m);

    auto instDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(inst));
    CHECK_HR("Create InstBuf", device->CreateCommittedResource(&heapUpload, D3D12_HEAP_FLAG_NONE, &instDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&instBuf)));
    void* p2 = nullptr; instBuf->Map(0, nullptr, &p2); memcpy(p2, inst, sizeof(inst)); instBuf->Unmap(0, nullptr);

    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = DXGI_FORMAT_UNKNOWN; srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Buffer.FirstElement = 0; srv.Buffer.NumElements = 3; srv.Buffer.StructureByteStride = sizeof(InstanceData);
    device->CreateShaderResourceView(instBuf.Get(), &srv, srvHeap->GetCPUDescriptorHandleForHeapStart());

    // фиксируем базовый аспект и проекцию
    baseAspect_ = (core.Height() > 0) ? float(core.Width()) / float(core.Height()) : (16.0f / 9.0f);
    float vfov = XMConvertToRadians(60.0f);
    proj_ = XMMatrixPerspectiveFovLH(vfov, baseAspect_, 0.1f, 100.0f);

    return true;
}

void ConeScene::Render(D3D12Core& core, D3D12_CPU_DESCRIPTOR_HANDLE rtv) {
    auto* list = core.CL();

    // View * Proj (Proj фиксирован по базовому аспекту)
    XMMATRIX view = XMMatrixLookAtLH(
        XMVectorSet(0.0f, 0.6f, -3.0f, 1.0f),
        XMVectorSet(0.0f, 0.3f, 0.0f, 1.0f),
        XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
    XMMATRIX vpT = XMMatrixTranspose(view * proj_);
    void* pCam = nullptr; camCB->Map(0, nullptr, &pCam); memcpy(pCam, &vpT, sizeof(vpT)); camCB->Unmap(0, nullptr);

    // вписанный прямоугольник под baseAspect_: viewport полный, crop ножницами
    int VW = core.Width(), VH = core.Height();
    float currentAspect = (VH > 0) ? float(VW) / float(VH) : baseAspect_;
    int bx = 0, by = 0, bw = VW, bh = VH;
    if (currentAspect > baseAspect_) { bw = int(VH * baseAspect_); bx = (VW - bw) / 2; }
    else if (currentAspect < baseAspect_) { bh = int(VW / baseAspect_); by = (VH - bh) / 2; }
    const D3D12_VIEWPORT vpFull = core.Viewport();
    D3D12_RECT sc{ bx, by, bx + bw, by + bh };

    // время и вращение
    using clock = std::chrono::steady_clock;
    static auto prev = clock::now();
    auto now = clock::now();
    float dt = std::chrono::duration<float>(now - prev).count(); prev = now;
    angle_ += dt * spinSpeed_;

    struct InstanceData { float m[16]; };
    auto xm2arrT = [](const XMMATRIX& M, float* out16) {
        XMFLOAT4X4 t; XMStoreFloat4x4(&t, XMMatrixTranspose(M)); memcpy(out16, &t, sizeof(t));
        };

    XMFLOAT3 s0{ 0.08f,0.22f,0.08f }, s1{ 0.14f,0.35f,0.14f }, s2{ 0.10f,0.28f,0.10f };
    float xL = -0.9f, xR = +0.9f, z = -0.5f, y2 = +0.45f;
    XMMATRIX w0 = XMMatrixScaling(s0.x, s0.y, s0.z) * XMMatrixRotationY(angle_) * XMMatrixTranslation(xL, 0.0f, z);
    XMMATRIX w1 = XMMatrixScaling(s1.x, s1.y, s1.z) * XMMatrixRotationY(-angle_ * 0.8f) * XMMatrixTranslation(xR, 0.0f, z);
    XMMATRIX w2 = XMMatrixScaling(s2.x, s2.y, s2.z) * XMMatrixRotationY(angle_ * 1.5f) * XMMatrixTranslation(0.0f, y2, z);

    InstanceData inst[3]; xm2arrT(w0, inst[0].m); xm2arrT(w1, inst[1].m); xm2arrT(w2, inst[2].m);
    void* pInst = nullptr; instBuf->Map(0, nullptr, &pInst); memcpy(pInst, inst, sizeof(inst)); instBuf->Unmap(0, nullptr);

    ID3D12DescriptorHeap* heaps[] = { srvHeap.Get() }; list->SetDescriptorHeaps(1, heaps);
    list->SetGraphicsRootSignature(rootSig.Get()); list->SetPipelineState(pso.Get());
    list->RSSetViewports(1, &vpFull);
    list->RSSetScissorRects(1, &sc);
    list->SetGraphicsRootConstantBufferView(0, camCB->GetGPUVirtualAddress());
    list->SetGraphicsRootDescriptorTable(1, srvGpu);

    const float clear[4] = { 0.2f,0.2f,0.2f,1.0f };
    list->ClearRenderTargetView(rtv, clear, 0, nullptr);

    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->IASetVertexBuffers(0, 1, &vbv);
    list->IASetIndexBuffer(&ibv);
    list->DrawIndexedInstanced(indexCount, 3, 0, 0, 0);
}
