#pragma once
#include "../sdk/game.h"
#include <array>

namespace Resolver
{
    // Legacy client reset code initializes -58/+58 at 0x334/0x338; version is at 0x344.
    inline constexpr size_t StateSize = 0x348;
    inline constexpr int MinimumYawOffset = 0x334,MaximumYawOffset = 0x338;
    struct Layer
    {
        float animationTime,fadeOut; void* studio; int source,destination,order,sequence;
        float previousCycle,weight,weightDelta,playbackRate,cycle; void* owner; int invalidate;
    };
    static_assert(sizeof(Layer) == 0x38);
    struct Variant
    {
        std::array<std::byte,StateSize> state{};
        std::array<Layer,13> layers{};
        std::array<float,24> poses{};
        std::vector<SDK::Capsule> hitboxes;
        float feetYaw = 0;
        bool valid = false;
    };
    using SetVector = void(__thiscall*)(void*,const SDK::Vector&);
    using Invalidate = void(__thiscall*)(void*);
    inline SetVector setOrigin = nullptr,setAngles = nullptr,setVelocity = nullptr;
    inline Invalidate invalidateBones = nullptr;
    struct Result
    {
        std::array<Variant,3> variants;
        int selected = 0;
        float confidence = 0;
        int ticks = 1;
        std::array<Layer,13> networkLayers{};
        SDK::Vector eyeAngles{},velocity{};
        float duck = 0;
        int flags = 0;
        bool shot = false;
        float simulationTime = 0,lowerBodyYaw = 0;
        float lastMoveTime = 0,lastMoveYaw = 0;
        bool hasLastMove = false;
        enum class Method { None, Playback, LowerBody, Matched, Bruteforce, LastMove } method = Method::None;
    };
    bool MatchRecord(Result& current,const Result& candidate,unsigned blacklist);
    bool SolveMove(Result& current,const Result* previous,unsigned blacklist = 0);
    bool SolveStand(Result& current,const Result* previous,unsigned blacklist = 0);
    int ReplayTicks(float delta,float interval);
    float InterpolateYaw(float previous,float current,float fraction);
    void BruteForce(Result& result,unsigned blacklist);
    bool Available();
    Result Reconstruct(SDK::Entity* player,float previousTime,const SDK::Vector& previousOrigin,
        float previousDuck,const Result* previous,unsigned blacklist = 0);
    bool SafePoint(const Result& result,size_t hitbox,const SDK::Vector& eye,const SDK::Vector& point,float range);
    bool SafeRay(const Result& result,size_t hitbox,const SDK::Vector& eye,const SDK::Vector& end);
}
