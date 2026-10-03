#pragma once
#include "../sdk/game.h"
#include <array>
#include <atomic>

namespace RenderPose
{
    using SetupBones = bool(__thiscall*)(void*,SDK::Matrix3x4*,int,int,float);
    using Attachments = void(__thiscall*)(void*,void*);
    inline Attachments attachments = nullptr;
    inline thread_local unsigned bypassDepth = 0;
    inline thread_local unsigned visualReadDepth = 0;
    inline std::atomic<bool> rendering{false};
    struct Read
    {
        Read() { ++visualReadDepth; }
        ~Read() { --visualReadDepth; }
        Read(const Read&) = delete;
        Read& operator=(const Read&) = delete;
    };
    struct Bypass
    {
        Bypass() { ++bypassDepth; }
        ~Bypass() { --bypassDepth; }
        Bypass(const Bypass&) = delete;
        Bypass& operator=(const Bypass&) = delete;
    };
    struct Pose
    {
        std::array<SDK::Matrix3x4,256> matrices{};
        SDK::Vector origin{};
        const void* model = nullptr;
        float simulationTime = 0;
        int count = 0;
        bool Store(const void* identity,float simulation,const SDK::Vector& source,
            const SDK::Matrix3x4* bones,int boneCount);
        bool Translate(const SDK::Vector& destination,SDK::Matrix3x4* output,int capacity) const;
    };
    void Refresh();
    void Clear();
    bool Setup(void* renderable,SDK::Matrix3x4* output,int capacity,int mask,float time,SetupBones original);
}
