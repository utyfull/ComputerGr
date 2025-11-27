#include <windows.h>
#include <windowsx.h>
#include <string>
#include <unordered_map>
#include <algorithm>
#include "dwin.hpp"

namespace dwin {

    Window::~Window() { destroy(); }

    ATOM Window::RegisterOnce(const wchar_t* className) {
        static std::unordered_map<std::wstring, ATOM> cache;
        if (auto it = cache.find(className); it != cache.end()) return it->second;

        WNDCLASSEXW wc{ sizeof(WNDCLASSEXW) };
        wc.lpfnWndProc = &Window::WndProcThunk;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = className;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr; // не закрашиваем фон, убираем белые Ђпростыниї
        ATOM a = RegisterClassExW(&wc);
        if (a) cache.emplace(className, a);
        return a;
    }

    bool Window::create(const wchar_t* className,
        const wchar_t* title,
        DWORD style, DWORD exStyle,
        int x, int y, int w, int h,
        HWND parent, void* createParam) {
        if (!RegisterOnce(className)) return false;
        proc_ = [this](HWND hh, UINT mm, WPARAM ww, LPARAM ll) { return this->onMessage(hh, mm, ww, ll); };
        h_ = CreateWindowExW(exStyle, className, title, style,
            x, y, w, h, parent, nullptr,
            GetModuleHandleW(nullptr), this ? this : createParam);
        return h_ != nullptr;
    }

    void Window::destroy() noexcept { if (h_) { DestroyWindow(h_); h_ = nullptr; } }
    void Window::place(int x, int y, int w, int h, bool repaint) noexcept { if (h_) MoveWindow(h_, x, y, w, h, repaint); }
    LRESULT Window::onMessage(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(h, m, w, l); }

    LRESULT CALLBACK Window::WndProcThunk(HWND h, UINT m, WPARAM w, LPARAM l) {
        if (m == WM_NCCREATE) {
            auto cs = reinterpret_cast<CREATESTRUCTW*>(l);
            SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
            if (auto* self = reinterpret_cast<Window*>(cs->lpCreateParams)) self->h_ = h;
        }
        auto* self = reinterpret_cast<Window*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (!self) return DefWindowProcW(h, m, w, l);
        return self->proc_ ? self->proc_(h, m, w, l) : self->onMessage(h, m, w, l);
    }

    int App::run(const std::function<bool()>& tick) {
        MSG msg{};
        for (;;) {
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) return (int)msg.wParam;
                TranslateMessage(&msg); DispatchMessageW(&msg);
            }
            if (tick && !tick()) PostQuitMessage(0);
        }
    }

    LRESULT MainWindow::onMessage(HWND h, UINT m, WPARAM, LPARAM l) {
        switch (m) {
        case WM_GETMINMAXINFO: {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(l);
            mmi->ptMinTrackSize.x = 400; mmi->ptMinTrackSize.y = 300; return 0;
        }
        case WM_ERASEBKGND: return 1; // фон не стираем Ч дети закроют область
        case WM_PAINT: { // тЄмный фон клиента
            PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
            RECT rc; GetClientRect(h, &rc);
            HBRUSH bg = CreateSolidBrush(RGB(24, 24, 24));
            FillRect(dc, &rc, bg); DeleteObject(bg);
            EndPaint(h, &ps); return 0;
        }
        case WM_CANCELMODE: ReleaseCapture(); return 0;
        case WM_SETCURSOR:
            if (LOWORD(l) == HTCLIENT) { SetCursor(LoadCursor(nullptr, IDC_ARROW)); return TRUE; }
            return DefWindowProcW(h, m, 0, l);
        case WM_SIZE: if (onResize) onResize(LOWORD(l), HIWORD(l)); return 0;
        case WM_CLOSE: DestroyWindow(h); return 0;
        case WM_DESTROY: PostQuitMessage(0); return 0;
        }
        return DefWindowProcW(h, m, 0, l);
    }

    LRESULT Viewport::onMessage(HWND h, UINT m, WPARAM, LPARAM l) {
        switch (m) {
        case WM_SIZE: if (onResize) onResize(LOWORD(l), HIWORD(l)); return 0;
        case WM_ERASEBKGND: return 1;
        }
        return DefWindowProcW(h, m, 0, l);
    }

    LRESULT Slider::onMessage(HWND h, UINT m, WPARAM w, LPARAM l) {
        switch (m) {
        case WM_NCCREATE: SetWindowLongW(h, GWL_STYLE, WS_CHILD | WS_VISIBLE); return TRUE;
        case WM_LBUTTONDOWN: SetCapture(h); [[fallthrough]];
        case WM_MOUSEMOVE:
            if (w & MK_LBUTTON) {
                RECT rc; GetClientRect(h, &rc);
                int W = std::max<int>(1, (int)(rc.right - rc.left));
                int x = std::clamp((int)GET_X_LPARAM(l), 0, W);
                float t = (float)x / (float)W;
                st.value = st.min + t * (st.max - st.min);
                if (onChange) onChange(st.value);
                InvalidateRect(h, nullptr, FALSE);
            }
            return 0;
        case WM_LBUTTONUP:
        case WM_CAPTURECHANGED:
        case WM_CANCELMODE:
            ReleaseCapture(); return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
            RECT rc; GetClientRect(h, &rc);
            HBRUSH bg = CreateSolidBrush(RGB(45, 45, 45)); FillRect(dc, &rc, bg); DeleteObject(bg);
            float t = (st.max > st.min) ? (st.value - st.min) / (st.max - st.min) : 0.f; t = std::clamp(t, 0.f, 1.f);
            RECT fr = rc; fr.right = rc.left + (LONG)((rc.right - rc.left) * t);
            HBRUSH fg = CreateSolidBrush(RGB(90, 160, 255)); FillRect(dc, &fr, fg); DeleteObject(fg);
            int x = fr.right; RECT kb{ x - 4, rc.top, x + 4, rc.bottom };
            HBRUSH hk = CreateSolidBrush(RGB(230, 230, 230)); FillRect(dc, &kb, hk); DeleteObject(hk);
            HPEN pen = CreatePen(PS_SOLID, 1, RGB(200, 200, 200));
            auto op = SelectObject(dc, pen); auto ob = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
            Rectangle(dc, rc.left, rc.top, rc.right, rc.bottom);
            SelectObject(dc, op); SelectObject(dc, ob); DeleteObject(pen);
            EndPaint(h, &ps); return 0;
        }
        }
        return DefWindowProcW(h, m, w, l);
    }

    bool CheckBox::create(HWND parent, int x, int y, int w, int h, const wchar_t* text) {
        h_ = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_CLIPSIBLINGS,
            x, y, w, h, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
        return h_ != nullptr;
    }
    bool CheckBox::checked() const noexcept { return SendMessageW(h_, BM_GETCHECK, 0, 0) == BST_CHECKED; }
    void CheckBox::setChecked(bool v) noexcept { SendMessageW(h_, BM_SETCHECK, v ? BST_CHECKED : BST_UNCHECKED, 0); }

} // namespace dwin
