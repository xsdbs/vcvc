#include "runtime.h"
#include "hooks.h"
#include "bootstrap.h"
#include "../utils/logger.h"
#include <mutex>
namespace
{
    bool initialized = false;
    std::mutex lifecycleMutex;
}
bool __cdecl HavocInitialize()
{
    std::lock_guard<std::mutex> guard(lifecycleMutex);
    if (initialized) return true;
    if (Bootstrap::IsGameProcess()) Logger::OpenConsole();
    if (Bootstrap::IsGameProcess() && !Hooks::SetupGame()) return false;
    Logger::Write("havoc initialized");
    initialized = true;
    return true;
}
void __cdecl HavocShutdown()
{
    Bootstrap::Stop();
    std::lock_guard<std::mutex> guard(lifecycleMutex);
    Hooks::Restore();
    if (initialized) Logger::Write("havoc stopped");
    initialized = false;
    Logger::CloseConsole();
}
const char* __cdecl HavocVersion()
{
    return "havoc 0.1.0";
}
