#include <Windows.h>
#include "core/bootstrap.h"

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH && Bootstrap::IsGameProcess())
        Bootstrap::Start();
    return TRUE;
}
