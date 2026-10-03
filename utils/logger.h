#pragma once
namespace Logger
{
    bool OpenConsole(bool visible = true);
    void CloseConsole();
    void Write(const char* message);
}
