#pragma once
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>
#include <directx/d3dx12.h>
#include "dx_util.hpp"

using Microsoft::WRL::ComPtr;

class D3D12Core {
public:
    /// Инициализирует устройство, очередь, список, свопчейн и синхронизацию для окна hwnd с заданным размером.
    bool Init(HWND hwnd, UINT width, UINT height);

    /// Подготавливает кадр: ресетит аллокатор и список, задает RTV/DSV и возвращает текущий дескриптор RTV.
    D3D12_CPU_DESCRIPTOR_HANDLE BeginFrame();

    /// Завершает кадр: закрывает список, отправляет в очередь и презентует свопчейн.
    void EndFrame();

    /// Блокирует CPU до завершения всех команд, отправленных на GPU.
    void WaitGPU();

    /// Освобождает ресурсы и корректно закрывает объекты синхронизации.
    ~D3D12Core();

    /// Возвращает устройство D3D12.
    ID3D12Device* Dev() const { return device.Get(); }

    /// Возвращает текущий графический командный список.
    ID3D12GraphicsCommandList* CL() const { return list.Get(); }

    /// Возвращает текущий аллокатор команд.
    ID3D12CommandAllocator* Alloc() const { return alloc.Get(); }

    /// Возвращает ширину текущего рендер-таргета.
    UINT Width() const { return width; }

    /// Возвращает высоту текущего рендер-таргета.
    UINT Height() const { return height; }

    /// Возвращает активный вьюпорт.
    const D3D12_VIEWPORT& Viewport() const { return viewport; }

    /// Возвращает активный прямоугольник отсечения.
    const D3D12_RECT& Scissor() const { return scissor; }

    /// Возвращает инкремент дескрипторов RTV для текущего устройства.
    UINT RTVInc() const { return rtvInc; }

private:
    bool CreateSwapchainAndRTVs(HWND hwnd);
    bool CreateDepthResources(UINT w, UINT h);

    UINT width = 0, height = 0;
    UINT frameIndex = 0, rtvInc = 0;

    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<IDXGISwapChain3> swap;

    ComPtr<ID3D12DescriptorHeap> rtvHeap;
    ComPtr<ID3D12Resource> buffers[2];

    // depth-buffer
    ComPtr<ID3D12DescriptorHeap> dsvHeap;
    ComPtr<ID3D12Resource> depth;
    D3D12_CPU_DESCRIPTOR_HANDLE dsv{};

    ComPtr<ID3D12CommandAllocator> alloc;
    ComPtr<ID3D12GraphicsCommandList> list;

    ComPtr<ID3D12Fence> fence;
    UINT64 fenceValue = 0;
    HANDLE fenceEvent = nullptr;

    D3D12_VIEWPORT viewport{};
    D3D12_RECT scissor{};
};
