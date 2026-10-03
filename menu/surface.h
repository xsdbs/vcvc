#pragma once
#include <Windows.h>
#include <string>

namespace Surface
{
    inline void* instance = nullptr;
    template<typename Function>
    Function Method(void* object, unsigned index)
    {
        return (Function)(*(void***)object)[index];
    }
    inline void Color(DWORD color)
    {
        Method<void(__thiscall*)(void*,int,int,int,int)>(instance,15)(instance,
            (color>>16)&255,(color>>8)&255,color&255,(color>>24)&255);
    }
    inline void Rectangle(int x, int y, int w, int h, DWORD color)
    {
        Color(color);
        Method<void(__thiscall*)(void*,int,int,int,int)>(instance,16)(instance,x,y,x+w,y+h);
    }
    inline void Line(int x,int y,int endX,int endY,DWORD color)
    {
        Color(color);
        Method<void(__thiscall*)(void*,int,int,int,int)>(instance,19)(instance,x,y,endX,endY);
    }
    inline constexpr const char* fontFace = "Verdana";
    inline unsigned long CreateFont(const char* face = fontFace,int size = 12,int weight = 400,int flags = 16)
    {
        const unsigned long font = Method<unsigned long(__thiscall*)(void*)>(instance,71)(instance);
        const bool success = Method<bool(__thiscall*)(void*,unsigned long,const char*,int,int,int,int,int,int,int)>(instance,72)(
            instance,font,face,size,weight,0,0,flags,0,0);
        return success ? font : 0;
    }
    inline void Text(int x, int y, const char* text, DWORD color, unsigned long font)
    {
        std::wstring wide;
        const int length = MultiByteToWideChar(CP_UTF8,0,text,-1,nullptr,0);
        if (length <= 0) return;
        wide.resize(length);
        MultiByteToWideChar(CP_UTF8,0,text,-1,wide.data(),length);
        wide.resize(length-1);
        Method<void(__thiscall*)(void*,unsigned long)>(instance,23)(instance,font);
        Method<void(__thiscall*)(void*,int,int,int,int)>(instance,25)(instance,
            (color>>16)&255,(color>>8)&255,color&255,(color>>24)&255);
        Method<void(__thiscall*)(void*,int,int)>(instance,26)(instance,x,y);
        Method<void(__thiscall*)(void*,const wchar_t*,int,int)>(instance,28)(instance,wide.c_str(),static_cast<int>(wide.size()),0);
    }
    inline void UnlockCursor()
    {
        Method<void(__thiscall*)(void*)>(instance,66)(instance);
    }
}
