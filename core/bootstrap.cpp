#include "bootstrap.h"
#include "runtime.h"
#include "../utils/logger.h"
#include <atomic>
#include <cstring>

namespace
{
    HANDLE worker = nullptr;
    DWORD workerId = 0;
    std::atomic<bool> stopping{false};
    DWORD WINAPI InitializeThread(void*)
    {
        if (!Logger::OpenConsole()) Logger::Write("Could not open live debug console; file logging remains active");
        Logger::Write("CS:GO DLL loaded; waiting for DirectX 9");
        for (int attempt = 0; attempt < 300 && !stopping.load(); ++attempt) {
            if (GetModuleHandleA("shaderapidx9.dll") && HavocInitialize())
                return 0;
            Sleep(100);
        }
        if (!stopping.load()) Logger::Write("Game menu initialization failed; check pattern/hook diagnostics above");
        return 1;
    }
}
bool Bootstrap::IsGameProcess()
{
    char path[MAX_PATH]{};
    GetModuleFileNameA(nullptr,path,MAX_PATH);
    const char* name = std::strrchr(path,'\\');
    return _stricmp(name ? name+1 : path,"csgo.exe") == 0;
}
void Bootstrap::Start()
{
    stopping = false;
    worker = CreateThread(nullptr,0,InitializeThread,nullptr,0,&workerId);
}
void Bootstrap::Stop()
{
    stopping = true;
    if (worker && GetCurrentThreadId() != workerId) {
        WaitForSingleObject(worker,INFINITE);
        CloseHandle(worker);
        worker = nullptr;
    }
}
