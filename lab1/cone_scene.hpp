#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <DirectXMath.h>

using Microsoft::WRL::ComPtr;

class D3D12Core;

class ConeScene {
public:
    bool Init(D3D12Core& core);
    void Render(D3D12Core& core, D3D12_CPU_DESCRIPTOR_HANDLE rtv);

    void SetSpinSpeed(float s) { spinSpeed_ = s; }
    float SpinSpeed() const { return spinSpeed_; }

private:
    ComPtr<ID3D12RootSignature> rootSig;
    ComPtr<ID3D12PipelineState>  pso;
    ComPtr<ID3D12Resource> vb, ib, camCB, instBuf;
    ComPtr<ID3D12DescriptorHeap> srvHeap;
    D3D12_VERTEX_BUFFER_VIEW vbv{};
    D3D12_INDEX_BUFFER_VIEW  ibv{};
    D3D12_GPU_DESCRIPTOR_HANDLE srvGpu{};
    UINT indexCount = 0;

    float angle_ = 0.0f;
    float spinSpeed_ = 1.2f;

    DirectX::XMMATRIX proj_ = DirectX::XMMatrixIdentity();
    float baseAspect_ = 16.0f / 9.0f;
};
