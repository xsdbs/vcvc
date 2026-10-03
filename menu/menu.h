#pragma once
#include <Windows.h>
#include <d3d9.h>

namespace Menu
{
    bool Render(IDirect3DDevice9* device,float latencyMs = 0);
    bool HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    bool BuildFrame(int width,int height,float delta,float latencyMs = 0);
    bool Visible();
    void Release();
    void DeviceLost();
}
extern "C" {
    __declspec(dllexport) void __cdecl HavocRenderMenu(IDirect3DDevice9* device);
    __declspec(dllexport) bool __cdecl HavocMenuMessage(UINT message, WPARAM wParam, LPARAM lParam);
    __declspec(dllexport) void __cdecl HavocMenuDeviceLost();
}
