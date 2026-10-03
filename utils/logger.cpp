#include "logger.h"
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <cstring>
#include <cstdio>

namespace
{
    std::mutex mutex;
    HANDLE console = INVALID_HANDLE_VALUE;
    bool ownsConsole = false;
    HANDLE previousInput = nullptr,previousOutput = nullptr,previousError = nullptr;
    BOOL WINAPI ConsoleControl(DWORD event)
    {
        return event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT;
    }
}
bool Logger::OpenConsole(bool visible)
{
    std::lock_guard<std::mutex> guard(mutex);
    if (console != INVALID_HANDLE_VALUE) return true;
    if (!GetConsoleWindow()) {
        previousInput = GetStdHandle(STD_INPUT_HANDLE);
        previousOutput = GetStdHandle(STD_OUTPUT_HANDLE);
        previousError = GetStdHandle(STD_ERROR_HANDLE);
        if (!AllocConsole()) return false;
        ownsConsole = true;
        SetConsoleCtrlHandler(ConsoleControl,TRUE);
        SetConsoleTitleW(L"havoc - live debug log");
        SetConsoleOutputCP(CP_UTF8);
        DWORD mode = 0;
        HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
        if (GetConsoleMode(input,&mode)) SetConsoleMode(input,(mode|ENABLE_EXTENDED_FLAGS)&~ENABLE_QUICK_EDIT_MODE);
        if (HWND window = GetConsoleWindow()) {
            // Closing a process console can terminate the host game.
            if (HMENU menu = GetSystemMenu(window,FALSE)) DeleteMenu(menu,SC_CLOSE,MF_BYCOMMAND);
            if (!visible) ShowWindow(window,SW_HIDE);
        }
    }
    console = CreateFileW(L"CONOUT$",GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
    return console != INVALID_HANDLE_VALUE;
}
void Logger::CloseConsole()
{
    std::lock_guard<std::mutex> guard(mutex);
    if (console != INVALID_HANDLE_VALUE) { CloseHandle(console); console = INVALID_HANDLE_VALUE; }
    if (ownsConsole) {
        SetConsoleCtrlHandler(ConsoleControl,FALSE);
        FreeConsole();
        SetStdHandle(STD_INPUT_HANDLE,previousInput);
        SetStdHandle(STD_OUTPUT_HANDLE,previousOutput);
        SetStdHandle(STD_ERROR_HANDLE,previousError);
        ownsConsole = false;
    }
}

void Logger::Write(const char* message)
{
    if (!message) return;
    std::lock_guard<std::mutex> guard(mutex);
    SYSTEMTIME time{};
    GetLocalTime(&time);
    char prefix[96];
    std::snprintf(prefix,sizeof(prefix),"[%02u:%02u:%02u.%03u] [havoc pid=%lu] ",
        time.wHour,time.wMinute,time.wSecond,time.wMilliseconds,GetCurrentProcessId());
    const std::string line = std::string(prefix)+message+"\r\n";
    OutputDebugStringA(line.c_str());
    if (console != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(console,line.data(),static_cast<DWORD>(line.size()),&written,nullptr);
    }
    HMODULE module = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        (LPCSTR)&Logger::Write,&module)) return;
    wchar_t path[MAX_PATH]{};
    if (!GetModuleFileNameW(module,path,MAX_PATH)) return;
    char executable[MAX_PATH]{};
    GetModuleFileNameA(nullptr,executable,MAX_PATH);
    const char* name = std::strrchr(executable,'\\');
    const bool game = _stricmp(name ? name+1 : executable,"csgo.exe") == 0;
    std::ofstream file(std::filesystem::path(path).parent_path() / (game ? "havoc.log" : "havoc-tests.log"),std::ios::app);
    file << line;
}
