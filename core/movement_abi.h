#pragma once
// Legacy engine CL_Move receives its float in XMM0 and finalTick in CL.
// These bridges deliberately do not use a guessed cdecl/vectorcall prototype.
namespace MovementABI
{
    inline void* original = nullptr;
    using Dispatch = void(__stdcall*)(float,bool);
    inline Dispatch dispatch = nullptr;
    inline void Invoke(float sample,bool finalTick)
    {
        __asm {
            movss xmm0, sample
            mov cl, finalTick
            call dword ptr [original]
        }
    }
    __declspec(naked) inline void Hook()
    {
        __asm {
            push ebp
            mov ebp, esp
            movzx eax, cl
            push eax
            sub esp, 4
            movss dword ptr [esp], xmm0
            call dword ptr [dispatch]
            pop ebp
            ret
        }
    }
    using ClientDispatch = void(__stdcall*)(void*,int,float,bool,bool*);
    inline ClientDispatch clientDispatch = nullptr;
    __declspec(naked) inline void ClientHook()
    {
        __asm {
            push ebp
            mov ebp, esp
            push ebx
            lea eax, [esp]
            push eax
            push dword ptr [ebp+16]
            push dword ptr [ebp+12]
            push dword ptr [ebp+8]
            push ecx
            call dword ptr [clientDispatch]
            pop ebx
            pop ebp
            ret 12
        }
    }
}
