#pragma once
#include <windows.h>
#include <cstdio>

inline void LogHr(const wchar_t* step, HRESULT hr) {
    wchar_t buf[256];
    swprintf(buf, 256, L"%s failed. hr=0x%08X", step, (unsigned)hr);
    OutputDebugStringW(buf); OutputDebugStringW(L"\n");
    MessageBoxW(nullptr, buf, L"D3D12", MB_OK | MB_ICONERROR);
}
#define CHECK_HR_R(STEP, EXPR, RET) do{ HRESULT _hr=(EXPR); if(FAILED(_hr)){LogHr(L##STEP,_hr); return RET;} }while(0)
#define CHECK_HR(STEP, EXPR)        CHECK_HR_R(STEP, EXPR, false)
#define CHECK_HR_V(STEP, EXPR)      CHECK_HR_R(STEP, EXPR, (void)0)

inline constexpr float PI = 3.14159265358979323846f;
