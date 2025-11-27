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

    // управление камерой
    void MoveCameraLocal(float dx, float dy, float dz);
    void RotateCamera(float dYaw, float dPitch);
    void ResetCamera();

private:
    ComPtr<ID3D12RootSignature> rootSig;
    ComPtr<ID3D12PipelineState>  pso;

    // конусы
    ComPtr<ID3D12Resource> vb;
    ComPtr<ID3D12Resource> ib;
    D3D12_VERTEX_BUFFER_VIEW vbv{};
    D3D12_INDEX_BUFFER_VIEW  ibv{};
    UINT indexCount = 0;

    // пол
    ComPtr<ID3D12Resource> floorVB;
    ComPtr<ID3D12Resource> floorIB;
    D3D12_VERTEX_BUFFER_VIEW floorVBV{};
    D3D12_INDEX_BUFFER_VIEW  floorIBV{};
    UINT floorIndexCount = 0;

    // буферы
    ComPtr<ID3D12Resource> camCB;
    ComPtr<ID3D12Resource> instBuf;
    ComPtr<ID3D12DescriptorHeap> srvHeap;
    D3D12_GPU_DESCRIPTOR_HANDLE srvGpu{};

    // анимация
    float angle_ = 0.0f;
    float spinSpeed_ = 1.2f;

    // проекция и камера
    DirectX::XMMATRIX proj_ = DirectX::XMMatrixIdentity();
    float baseAspect_ = 16.0f / 9.0f;

    DirectX::XMFLOAT3 camPos_{ 0.0f, 0.6f, -3.0f };
    float camYaw_ = 0.0f;
    float camPitch_ = 0.0f;
};
