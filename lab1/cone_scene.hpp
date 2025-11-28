#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <DirectXMath.h>

using Microsoft::WRL::ComPtr;

class D3D12Core;

class ConeScene
{
public:
    bool Init(D3D12Core& core);
    void Render(D3D12Core& core, D3D12_CPU_DESCRIPTOR_HANDLE rtv);

    // Скорость вращения конусов
    void  SetSpinSpeed(float s) { spinSpeed_ = s; }
    float SpinSpeed() const { return spinSpeed_; }

    // Управление камерой
    void MoveCameraLocal(float dx, float dy, float dz);
    void RotateCamera(float dYaw, float dPitch);
    void ResetCamera();

    // Управление светом из UI
    void SetAmbientStrength(float s) { ambientStrength_ = s; }
    void SetDirStrength(float s) { dirStrength_ = s; }
    void SetSpotStrength(int index, float s)
    {
        if (index >= 0 && index < 2)
            spot_[index].strength = s;
    }

private:
    ComPtr<ID3D12RootSignature> rootSig;
    ComPtr<ID3D12PipelineState> pso;

    // Геометрия конусов
    ComPtr<ID3D12Resource>     vb;
    ComPtr<ID3D12Resource>     ib;
    D3D12_VERTEX_BUFFER_VIEW   vbv{};
    D3D12_INDEX_BUFFER_VIEW    ibv{};
    UINT                       indexCount = 0;

    // Геометрия пола и маркеров
    ComPtr<ID3D12Resource>     floorVB;
    ComPtr<ID3D12Resource>     floorIB;
    D3D12_VERTEX_BUFFER_VIEW   floorVBV{};
    D3D12_INDEX_BUFFER_VIEW    floorIBV{};
    UINT                       floorIndexCount = 0;

    // Общие буферы
    ComPtr<ID3D12Resource>        camCB;
    ComPtr<ID3D12Resource>        instBuf;
    ComPtr<ID3D12DescriptorHeap>  srvHeap;
    D3D12_GPU_DESCRIPTOR_HANDLE   srvGpu{};

    // Буферы света
    ComPtr<ID3D12Resource> pointLightBuf;
    ComPtr<ID3D12Resource> spotLightBuf;
    UINT                   numPointLights_ = 0;
    UINT                   numSpotLights_ = 0;

    // Параметры прожекторов на CPU
    struct SpotParams
    {
        DirectX::XMFLOAT3 pos;
        float             attK;

        DirectX::XMFLOAT3 dir;
        float             cosInner;

        DirectX::XMFLOAT3 baseColor;
        float             cosOuter;

        float             strength;
        float             _pad[3]; // выравнивание только на CPU
    };

    SpotParams spot_[2]{};

    // Базовые цвета света и множители
    DirectX::XMFLOAT3 ambientBase_{ 0.08f, 0.08f, 0.08f };
    float             ambientStrength_ = 1.0f;

    DirectX::XMFLOAT3 dirColorBase_{ 0.6f, 0.6f, 0.7f };
    float             dirStrength_ = 1.0f;

    // Анимация
    float angle_ = 0.0f;
    float spinSpeed_ = 1.2f;

    // Камера и проекция
    DirectX::XMMATRIX proj_ = DirectX::XMMatrixIdentity();
    float             baseAspect_ = 16.0f / 9.0f;

    DirectX::XMFLOAT3 camPos_{ 0.0f, 0.6f, -3.0f };
    float             camYaw_ = 0.0f;
    float             camPitch_ = 0.0f;
};
