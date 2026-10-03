#pragma once
extern "C" {
    __declspec(dllexport) bool __cdecl HavocInitialize();
    __declspec(dllexport) void __cdecl HavocShutdown();
    __declspec(dllexport) const char* __cdecl HavocVersion();
}
