#pragma once
#include <windows.h>
#include <functional>

namespace dwin {

    class Window {
    public:
        using MsgHandler = std::function<LRESULT(HWND, UINT, WPARAM, LPARAM)>;
        virtual ~Window();

        HWND hwnd() const noexcept { return h_; }
        bool create(const wchar_t* className,
            const wchar_t* title,
            DWORD style, DWORD exStyle,
            int x, int y, int w, int h,
            HWND parent = nullptr,
            void* createParam = nullptr);

        void destroy() noexcept;
        void place(int x, int y, int w, int h, bool repaint = true) noexcept;

    protected:
        virtual LRESULT onMessage(HWND, UINT, WPARAM, LPARAM);
        static ATOM RegisterOnce(const wchar_t* className);
        static LRESULT CALLBACK WndProcThunk(HWND, UINT, WPARAM, LPARAM);

        HWND h_{};
        MsgHandler proc_{};
    };

    class App {
    public:
        int run(const std::function<bool()>& tick);
    };

    class MainWindow : public Window {
    public:
        std::function<void(int, int)> onResize;
    protected:
        LRESULT onMessage(HWND, UINT, WPARAM, LPARAM) override;
    };

    class Viewport : public Window {
    public:
        std::function<void(int, int)> onResize;
    protected:
        LRESULT onMessage(HWND, UINT, WPARAM, LPARAM) override;
    };

    class Slider : public Window {
    public:
        struct State { float min = 0.f, max = 1.f, value = 0.f; } st;
        std::function<void(float)> onChange;
    protected:
        LRESULT onMessage(HWND, UINT, WPARAM, LPARAM) override;
    };

    class CheckBox {
    public:
        bool create(HWND parent, int x, int y, int w, int h, const wchar_t* text);
        HWND hwnd() const noexcept { return h_; }
        bool checked() const noexcept;
        void setChecked(bool v) noexcept;
    private:
        HWND h_{};
    };

} // namespace dwin
