#include <windows.h>
#include <windowsx.h>

#include <algorithm>
#include <chrono>

#include <dwin.hpp>
#include "d3d12_core.hpp"
#include "cone_scene.hpp"

using namespace dwin;

static ConeScene* gScene = nullptr;
static HWND       gMain = nullptr;

// Обновление заголовка окна по скорости вращения
static void UpdateTitle()
{
    if (!gScene || !gMain)
        return;

    wchar_t b[128];
    swprintf(b, 128, L"D3D12 Cone | speed=%.2f", gScene->SpinSpeed());
    SetWindowTextW(gMain, b);
}

// Главное окно (R сбрасывает камеру)
class ConeMainWindow : public MainWindow
{
protected:
    LRESULT onMessage(HWND h, UINT m, WPARAM w, LPARAM l) override
    {
        switch (m)
        {
        case WM_KEYDOWN:
            if (gScene)
            {
                if (w == 'R')
                {
                    gScene->ResetCamera();
                    return 0;
                }
            }
            break;
        }

        return MainWindow::onMessage(h, m, w, l);
    }
};

// Вьюпорт: правая кнопка мыши крутит камеру
class ConeViewport : public Viewport
{
public:
    bool  rotating = false;
    POINT last{};

protected:
    LRESULT onMessage(HWND h, UINT m, WPARAM w, LPARAM l) override
    {
        switch (m)
        {
        case WM_RBUTTONDOWN:
            SetCapture(h);
            rotating = true;
            last.x = GET_X_LPARAM(l);
            last.y = GET_Y_LPARAM(l);
            return 0;

        case WM_MOUSEMOVE:
            if (rotating && (w & MK_RBUTTON) && gScene)
            {
                POINT p{ GET_X_LPARAM(l), GET_Y_LPARAM(l) };
                int   dx = p.x - last.x;
                int   dy = p.y - last.y;
                last = p;

                const float sens = 0.005f;
                gScene->RotateCamera(dx * sens, dy * sens);
                return 0;
            }
            break;

        case WM_RBUTTONUP:
        case WM_CAPTURECHANGED:
        case WM_CANCELMODE:
            if (rotating)
            {
                rotating = false;
                ReleaseCapture();
            }
            return 0;
        }

        return Viewport::onMessage(h, m, w, l);
    }
};

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int)
{
    constexpr int W = 1600;
    constexpr int H = 900;
    constexpr int TOP_BAR_H = 150; // высота полосы с контролами

    ConeMainWindow mainWin;
    if (!mainWin.create(
        L"DWin.Main",
        L"D3D12 Cone",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_VISIBLE,
        0,
        100, 100, W, H))
    {
        return 1;
    }

    gMain = mainWin.hwnd();

    // Верхний блок с контролами
    HWND topBox = CreateWindowExW(
        0,
        L"BUTTON",
        L" Controls ",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | BS_GROUPBOX,
        8, 8, W - 16, TOP_BAR_H - 16,
        gMain, nullptr, hInst, nullptr);

    // Подписи
    HWND lblSpeed = CreateWindowExW(
        0,
        L"STATIC",
        L"Spin:",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        16, 24, 80, 20,
        topBox, nullptr, hInst, nullptr);

    HWND lblAmb = CreateWindowExW(
        0,
        L"STATIC",
        L"Ambient:",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        16, 48, 80, 20,
        topBox, nullptr, hInst, nullptr);

    HWND lblDir = CreateWindowExW(
        0,
        L"STATIC",
        L"Directional:",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        16, 72, 80, 20,
        topBox, nullptr, hInst, nullptr);

    HWND lblSpot0 = CreateWindowExW(
        0,
        L"STATIC",
        L"Spot 0:",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        16, 96, 80, 20,
        topBox, nullptr, hInst, nullptr);

    HWND lblSpot1 = CreateWindowExW(
        0,
        L"STATIC",
        L"Spot 1:",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        16, 120, 80, 20,
        topBox, nullptr, hInst, nullptr);

    // Слайдеры
    Slider sliderSpeed;
    sliderSpeed.st = { 0.0f, 6.0f, 1.2f };
    sliderSpeed.create(
        L"DWin.Slider",
        L"",
        WS_CHILD | WS_VISIBLE,
        0,
        90, 24, 220, 20,
        topBox);

    Slider sliderAmbient;
    sliderAmbient.st = { 0.0f, 3.0f, 1.0f };
    sliderAmbient.create(
        L"DWin.Slider",
        L"",
        WS_CHILD | WS_VISIBLE,
        0,
        90, 48, 220, 20,
        topBox);

    Slider sliderDir;
    sliderDir.st = { 0.0f, 3.0f, 1.0f };
    sliderDir.create(
        L"DWin.Slider",
        L"",
        WS_CHILD | WS_VISIBLE,
        0,
        90, 72, 220, 20,
        topBox);

    Slider sliderSpot0;
    sliderSpot0.st = { 0.0f, 3.0f, 1.2f };
    sliderSpot0.create(
        L"DWin.Slider",
        L"",
        WS_CHILD | WS_VISIBLE,
        0,
        90, 96, 220, 20,
        topBox);

    Slider sliderSpot1;
    sliderSpot1.st = { 0.0f, 3.0f, 0.5f };
    sliderSpot1.create(
        L"DWin.Slider",
        L"",
        WS_CHILD | WS_VISIBLE,
        0,
        90, 120, 220, 20,
        topBox);

    CheckBox pause;
    pause.create(topBox, 340, 24, 120, 24, L"Pause");

    // Вьюпорт с D3D12
    ConeViewport vp;
    vp.create(
        L"DWin.Viewport",
        L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
        0,
        0, TOP_BAR_H, W, H - TOP_BAR_H,
        gMain);

    RECT rcClient{};
    GetClientRect(gMain, &rcClient);

    int vw = std::max(1, static_cast<int>(rcClient.right - rcClient.left));
    int vh = std::max(1, static_cast<int>(rcClient.bottom - rcClient.top - TOP_BAR_H));

    D3D12Core core;
    if (!core.Init(vp.hwnd(), vw, vh))
        return 1;

    ConeScene scene;
    if (!scene.Init(core))
        return 1;

    gScene = &scene;

    // Начальные значения
    scene.SetSpinSpeed(sliderSpeed.st.value);
    scene.SetAmbientStrength(sliderAmbient.st.value);
    scene.SetDirStrength(sliderDir.st.value);
    scene.SetSpotStrength(0, sliderSpot0.st.value);
    scene.SetSpotStrength(1, sliderSpot1.st.value);
    UpdateTitle();

    // Лэйаут по ресайзу окна
    mainWin.onResize = [&](int cx, int cy)
        {
            cx = std::max(cx, 1);
            cy = std::max(cy, 1);

            int topH = TOP_BAR_H;
            int vpY = topH;
            int vpH = std::max(1, cy - vpY);

            MoveWindow(topBox, 8, 8, cx - 16, topH - 16, TRUE);

            int boxW = std::max(400, cx - 16);
            int labelX = 16;
            int sliderX = 90;
            int rowH = 20;
            int rowY = 24;
            int sliderW = std::max(200, boxW - sliderX - 80);

            MoveWindow(lblSpeed, labelX, rowY, 70, rowH, TRUE);
            MoveWindow(sliderSpeed.hwnd(), sliderX, rowY, sliderW, rowH, TRUE);
            MoveWindow(pause.hwnd(), boxW - 140, rowY, 120, 24, TRUE);

            rowY += 24;
            MoveWindow(lblAmb, labelX, rowY, 80, rowH, TRUE);
            MoveWindow(sliderAmbient.hwnd(), sliderX, rowY, sliderW, rowH, TRUE);

            rowY += 24;
            MoveWindow(lblDir, labelX, rowY, 80, rowH, TRUE);
            MoveWindow(sliderDir.hwnd(), sliderX, rowY, sliderW, rowH, TRUE);

            rowY += 24;
            MoveWindow(lblSpot0, labelX, rowY, 80, rowH, TRUE);
            MoveWindow(sliderSpot0.hwnd(), sliderX, rowY, sliderW, rowH, TRUE);

            rowY += 24;
            MoveWindow(lblSpot1, labelX, rowY, 80, rowH, TRUE);
            MoveWindow(sliderSpot1.hwnd(), sliderX, rowY, sliderW, rowH, TRUE);

            MoveWindow(vp.hwnd(), 0, vpY, cx, vpH, TRUE);
        };

    // Начальный лэйаут
    mainWin.onResize(
        static_cast<int>(rcClient.right - rcClient.left),
        static_cast<int>(rcClient.bottom - rcClient.top));

    // Callback'и для слайдеров
    sliderSpeed.onChange = [&](float v)
        {
            if (gScene)
                gScene->SetSpinSpeed(v);
            UpdateTitle();
        };

    sliderAmbient.onChange = [&](float v)
        {
            if (gScene)
                gScene->SetAmbientStrength(v);
        };

    sliderDir.onChange = [&](float v)
        {
            if (gScene)
                gScene->SetDirStrength(v);
        };

    sliderSpot0.onChange = [&](float v)
        {
            if (gScene)
                gScene->SetSpotStrength(0, v);
        };

    sliderSpot1.onChange = [&](float v)
        {
            if (gScene)
                gScene->SetSpotStrength(1, v);
        };

    // Главный цикл
    App app;
    return app.run([&]()
        {
            using clock = std::chrono::steady_clock;
            static auto prev = clock::now();

            auto  now = clock::now();
            float dt = std::chrono::duration<float>(now - prev).count();
            prev = now;

            if (!pause.checked() && gScene)
            {
                const float moveSpeed = 2.0f;
                float       dx = 0.0f;
                float       dy = 0.0f;
                float       dz = 0.0f;

                if (GetAsyncKeyState('W') & 0x8000) dz += moveSpeed * dt;
                if (GetAsyncKeyState('S') & 0x8000) dz -= moveSpeed * dt;
                if (GetAsyncKeyState('A') & 0x8000) dx -= moveSpeed * dt;
                if (GetAsyncKeyState('D') & 0x8000) dx += moveSpeed * dt;
                if (GetAsyncKeyState(VK_SPACE) & 0x8000)   dy += moveSpeed * dt;
                if (GetAsyncKeyState(VK_CONTROL) & 0x8000) dy -= moveSpeed * dt;

                if (dx != 0.0f || dy != 0.0f || dz != 0.0f)
                    gScene->MoveCameraLocal(dx, dy, dz);
            }

            auto rtv = core.BeginFrame();
            scene.Render(core, rtv);
            core.EndFrame();
            return true;
        });
}
