#include <windows.h>
#include <windowsx.h>
#include <algorithm>
#include <cstdio>

#include <dwin.hpp>
#include "d3d12_core.hpp"
#include "cone_scene.hpp"

using namespace dwin;

static ConeScene* gScene = nullptr;
static HWND gMain = nullptr;

static void UpdateTitle() {
    if (!gScene || !gMain) return;
    wchar_t b[128];
    swprintf(b, 128, L"D3D12 Cone | speed=%.2f", gScene->SpinSpeed());
    SetWindowTextW(gMain, b);
}

static const int SPLIT = 6;
static const int MIN_PANEL = 48;
static int dockL = 240, dockR = 240, dockT = 72, dockB = 0;

enum class DragKind { None, Left, Right, Top, Bottom };
static DragKind gDrag = DragKind::None;
static POINT gDragStart{};
static int gStartL, gStartR, gStartT, gStartB;

static LRESULT CALLBACK PanelProc(HWND h, UINT m, WPARAM, LPARAM) {
    if (m == WM_ERASEBKGND) return 1;
    if (m == WM_PAINT) {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        HBRUSH bg = CreateSolidBrush(RGB(32, 32, 32)); FillRect(dc, &rc, bg); DeleteObject(bg);
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(220, 220, 220));
        auto op = SelectObject(dc, pen); auto ob = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
        Rectangle(dc, rc.left, rc.top, rc.right, rc.bottom);
        SelectObject(dc, op); SelectObject(dc, ob); DeleteObject(pen);
        EndPaint(h, &ps); return 0;
    }
    return DefWindowProcW(h, m, 0, 0);
}

static LRESULT CALLBACK SplitterProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_SETCURSOR:
        if (LOWORD(l) == HTCLIENT) {
            LONG_PTR tag = GetWindowLongPtrW(h, GWLP_USERDATA);
            SetCursor(LoadCursor(nullptr, (tag == 3 || tag == 4) ? IDC_SIZENS : IDC_SIZEWE));
            return TRUE;
        }
        return DefWindowProcW(h, m, w, l);
    case WM_LBUTTONDOWN:
        SetCapture(h);
        gDragStart = { GET_X_LPARAM(l), GET_Y_LPARAM(l) }; ClientToScreen(h, &gDragStart);
        switch (GetWindowLongPtrW(h, GWLP_USERDATA)) {
        case 1: gDrag = DragKind::Left; break; case 2: gDrag = DragKind::Right; break;
        case 3: gDrag = DragKind::Top;  break; case 4: gDrag = DragKind::Bottom; break;
        default: gDrag = DragKind::None; break;
        }
        gStartL = dockL; gStartR = dockR; gStartT = dockT; gStartB = dockB; return 0;
    case WM_MOUSEMOVE:
        if (GetCapture() != h || gDrag == DragKind::None) return 0;
        {
            POINT p{ GET_X_LPARAM(l), GET_Y_LPARAM(l) }; ClientToScreen(h, &p);
            int dx = p.x - gDragStart.x, dy = p.y - gDragStart.y;
            RECT rc; GetClientRect(GetParent(h), &rc);
            int cx = (int)(rc.right - rc.left), cy = (int)(rc.bottom - rc.top);
            switch (gDrag) {
            case DragKind::Left:   dockL = std::clamp(gStartL + dx, MIN_PANEL, std::max(0, cx - dockR - SPLIT - MIN_PANEL)); break;
            case DragKind::Right:  dockR = std::clamp(gStartR - dx, MIN_PANEL, std::max(0, cx - dockL - SPLIT - MIN_PANEL)); break;
            case DragKind::Top:    dockT = std::clamp(gStartT + dy, MIN_PANEL, std::max(0, cy - dockB - SPLIT - MIN_PANEL)); break;
            case DragKind::Bottom: dockB = std::clamp(gStartB - dy, MIN_PANEL, std::max(0, cy - dockT - SPLIT - MIN_PANEL)); break;
            default: break;
            }
            PostMessageW(GetParent(h), WM_SIZE, 0, MAKELPARAM(cx, cy));
        }
        return 0;
    case WM_LBUTTONUP:
    case WM_CAPTURECHANGED:
    case WM_CANCELMODE:
        ReleaseCapture(); gDrag = DragKind::None; SetCursor(LoadCursor(nullptr, IDC_ARROW)); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    constexpr int W = 1600, H = 900;

    MainWindow mainWin;
    if (!mainWin.create(L"DWin.Main", L"D3D12 Cone",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_VISIBLE, 0,
        100, 100, W, H)) return 1;
    gMain = mainWin.hwnd();

    WNDCLASSW wcP{}; wcP.lpfnWndProc = PanelProc; wcP.hInstance = hInst; wcP.lpszClassName = L"DWin.Panel";  RegisterClassW(&wcP);
    WNDCLASSW wcS{}; wcS.lpfnWndProc = SplitterProc; wcS.hInstance = hInst; wcS.lpszClassName = L"DWin.Split"; RegisterClassW(&wcS);

    const DWORD PANEL_STYLE = WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
    const DWORD SPLIT_STYLE = WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS;

    HWND pnlL = CreateWindowExW(0, L"DWin.Panel", L"", PANEL_STYLE, 0, 0, 1, 1, gMain, nullptr, hInst, nullptr);
    HWND pnlR = CreateWindowExW(0, L"DWin.Panel", L"", PANEL_STYLE, 0, 0, 1, 1, gMain, nullptr, hInst, nullptr);
    HWND pnlT = CreateWindowExW(0, L"DWin.Panel", L"", PANEL_STYLE, 0, 0, 1, 1, gMain, nullptr, hInst, nullptr);
    HWND pnlB = CreateWindowExW(0, L"DWin.Panel", L"", PANEL_STYLE, 0, 0, 1, 1, gMain, nullptr, hInst, nullptr);

    HWND splL = CreateWindowExW(0, L"DWin.Split", L"", SPLIT_STYLE, 0, 0, 1, 1, gMain, nullptr, hInst, nullptr); SetWindowLongPtrW(splL, GWLP_USERDATA, 1);
    HWND splR = CreateWindowExW(0, L"DWin.Split", L"", SPLIT_STYLE, 0, 0, 1, 1, gMain, nullptr, hInst, nullptr); SetWindowLongPtrW(splR, GWLP_USERDATA, 2);
    HWND splT = CreateWindowExW(0, L"DWin.Split", L"", SPLIT_STYLE, 0, 0, 1, 1, gMain, nullptr, hInst, nullptr); SetWindowLongPtrW(splT, GWLP_USERDATA, 3);
    HWND splB = CreateWindowExW(0, L"DWin.Split", L"", SPLIT_STYLE, 0, 0, 1, 1, gMain, nullptr, hInst, nullptr); SetWindowLongPtrW(splB, GWLP_USERDATA, 4);

    HWND topBox = CreateWindowExW(0, L"BUTTON", L" Controls ",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | BS_GROUPBOX,
        8, 8, 600, dockT - 16, pnlT, nullptr, hInst, nullptr);

    Slider slider; slider.st = { 0.0f, 6.0f, 1.2f };
    slider.create(L"DWin.Slider", L"", WS_CHILD | WS_VISIBLE, 0, 16, 24, 340, 26, topBox);
    CheckBox pause; pause.create(topBox, 370, 24, 120, 24, L"Pause");

    Viewport vp;
    vp.create(
        L"DWin.Viewport",    // class
        L"",                 // title
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
        0,                   // exStyle
        0, 0,                // x, y
        100, 100,            // w, h
        gMain,               // parent HWND
        &vp                  // createParam = this (или nullptr)
    );

    RECT r0; GetClientRect(gMain, &r0);
    int vw = std::max<int>(1, (int)(r0.right - r0.left) - dockL - dockR - 2 * SPLIT);
    int vh = std::max<int>(1, (int)(r0.bottom - r0.top) - dockT - dockB - 2 * SPLIT);
    D3D12Core core; if (!core.Init(vp.hwnd(), vw, vh)) return 1;

    ConeScene scene; if (!scene.Init(core)) return 1; gScene = &scene;
    scene.SetSpinSpeed(slider.st.value); UpdateTitle();

    auto layout = [&](int cx, int cy) {
        MoveWindow(pnlT, 0, 0, cx, dockT, TRUE);
        MoveWindow(splT, 0, dockT, cx, SPLIT, TRUE);

        MoveWindow(pnlB, 0, cy - dockB, cx, dockB, TRUE);
        MoveWindow(splB, 0, cy - dockB - SPLIT, cx, SPLIT, TRUE);

        int topY = dockT + SPLIT;
        int botY = cy - dockB - SPLIT;
        int midH = std::max<int>(1, botY - topY);

        MoveWindow(pnlL, 0, topY, dockL, midH, TRUE);
        MoveWindow(splL, dockL, topY, SPLIT, midH, TRUE);

        MoveWindow(pnlR, cx - dockR, topY, dockR, midH, TRUE);
        MoveWindow(splR, cx - dockR - SPLIT, topY, SPLIT, midH, TRUE);

        int cx0 = std::max<int>(1, cx - dockL - dockR - 2 * SPLIT);
        int cy0 = std::max<int>(1, midH);
        MoveWindow(vp.hwnd(), dockL + SPLIT, topY, cx0, cy0, TRUE);

        MoveWindow(topBox, 8, 8, std::max<int>(400, cx - 16), dockT - 16, TRUE);
        MoveWindow(slider.hwnd(), 16, 24, std::max<int>(220, cx - 280), 26, TRUE);
        MoveWindow(pause.hwnd(), cx - 180, 24, 120, 24, TRUE);
        };

    mainWin.onResize = [&](int cx, int cy) { layout(cx, cy); };
    slider.onChange = [&](float v) { if (gScene) gScene->SetSpinSpeed(v); UpdateTitle(); };

    RECT rc; GetClientRect(gMain, &rc); layout((int)(rc.right - rc.left), (int)(rc.bottom - rc.top));

    App app;
    return app.run([&]() {
        if (pause.checked()) return true;
        auto rtv = core.BeginFrame();
        scene.Render(core, rtv);
        core.EndFrame();
        return true;
        });
}
