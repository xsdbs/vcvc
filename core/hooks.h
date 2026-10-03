#pragma once
#include <Windows.h>
#include <d3d9.h>
namespace Hooks
{
    bool Setup(IDirect3DDevice9* device);
    bool SetupGame();
    void Restore();
}
extern "C" {
    __declspec(dllexport) bool __cdecl HavocAttachDevice(IDirect3DDevice9* device);
    __declspec(dllexport) void __cdecl HavocDetachDevice();
}
