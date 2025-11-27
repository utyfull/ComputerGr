#include "d3d12_core.hpp"
#include <dxgidebug.h>

bool D3D12Core::Init(HWND hwnd, UINT w, UINT h) {
    width = w; height = h;

#if _DEBUG
    if (ComPtr<ID3D12Debug> dbg; SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dbg))))
        dbg->EnableDebugLayer();
    if (ComPtr<IDXGIInfoQueue> iq; SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&iq)))) {
        iq->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR, TRUE);
        iq->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION, TRUE);
    }
#endif

    ComPtr<IDXGIFactory6> factory;
    CHECK_HR("CreateDXGIFactory1", CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
    HRESULT hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device));
    if (FAILED(hr)) {
        ComPtr<IDXGIAdapter> warp;
        CHECK_HR("EnumWarpAdapter", factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)));
        CHECK_HR("D3D12CreateDevice(WARP)", D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
    }

    D3D12_COMMAND_QUEUE_DESC qd{}; qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    CHECK_HR("CreateCommandQueue", device->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue)));

    CHECK_HR("Create swapchain/RTVs", CreateSwapchainAndRTVs(hwnd));

    CHECK_HR("CreateCommandAllocator", device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc)));
    CHECK_HR("CreateCommandList", device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(), nullptr, IID_PPV_ARGS(&list)));
    CHECK_HR("Initial Close", list->Close());

    CHECK_HR("CreateFence", device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
    fenceValue = 1; fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

    viewport = { 0.f,0.f,(float)width,(float)height,0.f,1.f };
    scissor = { 0,0,(LONG)width,(LONG)height };
    return true;
}

bool D3D12Core::CreateSwapchainAndRTVs(HWND hwnd) {
    ComPtr<IDXGIFactory6> factory;
    CHECK_HR("CreateDXGIFactory1", CreateDXGIFactory1(IID_PPV_ARGS(&factory)));

    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.Width = width; sd.Height = height;
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    ComPtr<IDXGISwapChain1> swap1;
    CHECK_HR("CreateSwapChainForHwnd", factory->CreateSwapChainForHwnd(queue.Get(), hwnd, &sd, nullptr, nullptr, &swap1));
    CHECK_HR("Swap1->As", swap1.As(&swap));
    frameIndex = swap->GetCurrentBackBufferIndex();

    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.NumDescriptors = 2; hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    CHECK_HR("CreateDescriptorHeap RTV", device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&rtvHeap)));
    rtvInc = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    auto h = CD3DX12_CPU_DESCRIPTOR_HANDLE(rtvHeap->GetCPUDescriptorHandleForHeapStart());
    for (UINT i = 0; i < 2; i++) {
        CHECK_HR("Swap->GetBuffer", swap->GetBuffer(i, IID_PPV_ARGS(&buffers[i])));
        device->CreateRenderTargetView(buffers[i].Get(), nullptr, h);
        h.Offset(1, rtvInc);
    }
    return true;
}

D3D12_CPU_DESCRIPTOR_HANDLE D3D12Core::BeginFrame() {
    // сбросы с явной проверкой, без CHECK_HR_V
    HRESULT hr = alloc->Reset();
    if (FAILED(hr)) { LogHr(L"alloc->Reset", hr); return D3D12_CPU_DESCRIPTOR_HANDLE{}; }

    hr = list->Reset(alloc.Get(), nullptr);
    if (FAILED(hr)) { LogHr(L"list->Reset", hr); return D3D12_CPU_DESCRIPTOR_HANDLE{}; }

    // Present -> RT
    auto toRT = CD3DX12_RESOURCE_BARRIER::Transition(
        buffers[frameIndex].Get(),
        D3D12_RESOURCE_STATE_PRESENT,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    list->ResourceBarrier(1, &toRT);

    // целевой RTV
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtv(
        rtvHeap->GetCPUDescriptorHandleForHeapStart(), frameIndex, rtvInc);
    list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    return rtv; 
}

void D3D12Core::EndFrame() {
    // RT -> Present
    auto toPresent = CD3DX12_RESOURCE_BARRIER::Transition(
        buffers[frameIndex].Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PRESENT);
    list->ResourceBarrier(1, &toPresent);

    CHECK_HR_V("list->Close", list->Close());
    ID3D12CommandList* cmds[] = { list.Get() };
    queue->ExecuteCommandLists(1, cmds);
    CHECK_HR_V("Present", swap->Present(1, 0));
    WaitGPU();
    frameIndex = swap->GetCurrentBackBufferIndex();
}

void D3D12Core::WaitGPU() {
    CHECK_HR_V("Queue->Signal", queue->Signal(fence.Get(), fenceValue));
    if (fence->GetCompletedValue() < fenceValue) {
        CHECK_HR_V("Fence->SetEventOnCompletion", fence->SetEventOnCompletion(fenceValue, fenceEvent));
        WaitForSingleObject(fenceEvent, INFINITE);
    }
    ++fenceValue;
}

D3D12Core::~D3D12Core() {
    if (fenceEvent) CloseHandle(fenceEvent);
}
