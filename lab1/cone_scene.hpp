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

    void SetSpinSpeed(float s) { spinSpeed_ = s; }
    float SpinSpeed() const { return spinSpeed_; }

    void MoveCameraLocal(float dx, float dy, float dz);
    void RotateCamera(float dYaw, float dPitch);
    void ResetCamera();

    void SetAmbientStrength(float s) { ambientStrength_ = s; }
    void SetDirStrength(float s) { dirStrength_ = s; }
    void SetSpotStrength(int index, float s)
    {
        if (index >= 0 && index < 2)
            spot_[index].strength = s;
    }

    // смещения направления света по X/Y/Z (в мировых координатах относительно центра сцены)
    void SetDirOffsetX(float v) { dirOffsetX_ = v; }
    void SetDirOffsetY(float v) { dirOffsetY_ = v; }
    void SetDirOffsetZ(float v) { dirOffsetZ_ = v; }

private:
    static constexpr UINT ShadowMapSize = 4096;

    ComPtr<ID3D12RootSignature> rootSig;
    ComPtr<ID3D12PipelineState> pso;        // основной PSO
    ComPtr<ID3D12PipelineState> shadowPso;  // depth-only PSO для shadow map

    // конусы
    ComPtr<ID3D12Resource> vb;
    ComPtr<ID3D12Resource> ib;
    D3D12_VERTEX_BUFFER_VIEW vbv{};
    D3D12_INDEX_BUFFER_VIEW  ibv{};
    UINT indexCount = 0;

    // пол / маркеры
    ComPtr<ID3D12Resource> floorVB;
    ComPtr<ID3D12Resource> floorIB;
    D3D12_VERTEX_BUFFER_VIEW floorVBV{};
    D3D12_INDEX_BUFFER_VIEW  floorIBV{};
    UINT floorIndexCount = 0;

    // куб (общая геометрия для обоих кубов)
    ComPtr<ID3D12Resource> cubeVB;
    ComPtr<ID3D12Resource> cubeIB;
    D3D12_VERTEX_BUFFER_VIEW cubeVBV{};
    D3D12_INDEX_BUFFER_VIEW  cubeIBV{};
    UINT cubeIndexCount = 0;

    // общие буферы
    ComPtr<ID3D12Resource> camCB;
    ComPtr<ID3D12Resource> instBuf;
    ComPtr<ID3D12DescriptorHeap> srvHeap;
    D3D12_GPU_DESCRIPTOR_HANDLE srvGpu{};

    // буферы света
    ComPtr<ID3D12Resource> pointLightBuf;
    ComPtr<ID3D12Resource> spotLightBuf;
    UINT numPointLights_ = 0;
    UINT numSpotLights_ = 0;

    // текстуры (два материала для кубов)
    ComPtr<ID3D12Resource> diffuseTex0;
    ComPtr<ID3D12Resource> diffuseTex1;

    struct SpotParams
    {
        DirectX::XMFLOAT3 pos;       float attK;
        DirectX::XMFLOAT3 dir;       float cosInner;
        DirectX::XMFLOAT3 baseColor; float cosOuter;
        float strength;              float _pad[3];
    };
    SpotParams spot_[2]{};

    DirectX::XMFLOAT3 ambientBase_{ 0.08f, 0.08f, 0.08f };
    float ambientStrength_ = 1.0f;

    DirectX::XMFLOAT3 dirColorBase_{ 0.6f, 0.6f, 0.7f };
    float dirStrength_ = 1.0f;

    // смещения для направленного света (управляются слайдерами)
    float dirOffsetX_ = 0.0f;
    float dirOffsetY_ = 0.0f;
    float dirOffsetZ_ = 0.0f;

    float angle_ = 0.0f;
    float spinSpeed_ = 1.2f;

    DirectX::XMMATRIX proj_ = DirectX::XMMatrixIdentity();
    float baseAspect_ = 16.0f / 9.0f;

    DirectX::XMFLOAT3 camPos_{ 0.0f, 0.6f, -3.0f };
    float             camYaw_ = 0.0f;
    float             camPitch_ = 0.0f;

    // ресурсы для shadow map
    ComPtr<ID3D12Resource>       shadowMap;
    ComPtr<ID3D12DescriptorHeap> shadowDSVHeap;
    D3D12_CPU_DESCRIPTOR_HANDLE  shadowDSV{};
    D3D12_GPU_DESCRIPTOR_HANDLE  shadowSRV{};
};
