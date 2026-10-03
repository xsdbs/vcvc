#include "prediction.h"
#include "performance.h"
#include "exploits.h"
#include "resolver.h"
#include "../core/config.h"
#include "../utils/logger.h"
#include <cstring>
#include <array>
#include <cstdio>

namespace
{
    SDK::Entity* previousPlayer = nullptr;
    int previousNumber = 0,tickbase = 0;
    void ReportFailure(const char* reason)
    {
        static ULONGLONG last = 0;
        static bool reported = false;
        const auto now = GetTickCount64();
        if (reported && now-last < 5000) return;
        reported = true; last = now;
        Logger::Write(reason);
    }
    bool Readable(const void* address,size_t length)
    {
        auto begin = reinterpret_cast<std::uintptr_t>(address);
        if (!begin || length > UINTPTR_MAX-begin) return false;
        const auto end = begin+length;
        while (begin < end) {
            MEMORY_BASIC_INFORMATION region{};
            if (!VirtualQuery(reinterpret_cast<void*>(begin),&region,sizeof(region))
                || region.State != MEM_COMMIT || (region.Protect&(PAGE_GUARD|PAGE_NOACCESS))) return false;
            const auto next = reinterpret_cast<std::uintptr_t>(region.BaseAddress)+region.RegionSize;
            if (next <= begin) return false;
            begin = next;
        }
        return true;
    }
}
bool Prediction::ValidMap(const DataMap* map)
{
    return Readable(map,sizeof(DataMap)) && map->count >= 0 && map->count <= 1024
        && (map->count == 0 || Readable(map->fields,map->count*sizeof(TypeDescription)));
}
int Prediction::FindField(DataMap* map,const char* name,int parent,int depth)
{
    if (!name || depth >= 32 || !ValidMap(map)) return -1;
    for (int i = 0; i < map->count; ++i) {
        const TypeDescription& field = map->fields[i];
        if (field.offset < 0 || field.offset > 0x10000-parent) continue;
        if (field.name && Readable(field.name,128) && std::strncmp(field.name,name,128) == 0) return parent+field.offset;
        if (field.type == 10 && field.embedded && !(field.flags&32)) {
            const int found = FindField(field.embedded,name,parent+field.offset,depth+1);
            if (found >= 0) return found;
        }
    }
    return FindField(map->base,name,parent,depth+1);
}
bool Prediction::Available()
{
    return SDK::prediction && SDK::gameMovement && SDK::moveHelper && SDK::predictionSeed
        && SDK::predictionPlayer && Resolver::setOrigin && Resolver::setVelocity && Resolver::setAngles && hashCommand;
}
void Prediction::Reset()
{
    previousPlayer = nullptr; previousNumber = tickbase = 0; commandTime = -1;
}
int Prediction::CommandTick(SDK::Entity* player,int number,int base)
{
    // Never retain pointers into the engine's reusable command buffer.
    const int delta = number-previousNumber;
    if (player != previousPlayer || previousNumber == 0 || delta < 0 || delta > 16
        || base > tickbase+16 || base < tickbase-16) tickbase = base;
    else if (delta > 0) tickbase = std::max(base,tickbase+delta);
    previousPlayer = player;
    previousNumber = number;
    return tickbase;
}
void Prediction::Scope::Save(int offset,int size)
{
    if (offset < 0 || size <= 0 || size > 4096 || offset+size > 0x10000) return;
    SavedField field{offset,std::vector<std::byte>(size)};
    std::memcpy(field.bytes.data(),(BYTE*)local+offset,size);
    saved.push_back(std::move(field));
}
bool Prediction::Scope::SaveMap(DataMap* map,int parent,int depth)
{
    if (!map) return true;
    if (depth >= 32 || !ValidMap(map)) {
        ReportFailure("Engine prediction unavailable: unreadable or cyclic prediction datamap"); return false;
    }
    if (!SaveMap(map->base,parent,depth+1)) return false;
    for (int i = 0; i < map->count; ++i) {
        const TypeDescription& field = map->fields[i];
        // Void, function and input entries describe metadata, not player storage.
        // Their offset is allowed to be a sentinel. Embedded pointers are not inline objects.
        if (field.type == 0 || field.type == 11 || field.type == 20 || field.type == 21
            || (field.type == 10 && (field.flags&32))) continue;
        auto invalid = [&]() {
            char message[192]{};
            std::snprintf(message,sizeof(message),
                "Engine prediction unavailable: field snapshot rejected (depth=%d index=%d type=%d offset=%d bytes=%d parent=%d)",
                depth,i,field.type,field.offset,field.bytes,parent);
            ReportFailure(message); return false;
        };
        if (parent < 0 || parent > 0x10000 || field.offset < 0 || field.offset > 0x10000-parent) return invalid();
        if (field.type == 10) {
            if (!field.embedded) return invalid();
            if (!SaveMap(field.embedded,parent+field.offset,depth+1)) return false;
        }
        else {
            if (field.type < 0 || field.type > 28 || field.bytes <= 0 || field.bytes > 4096
                || field.bytes > 0x10000-parent-field.offset
                || !Readable((BYTE*)local+parent+field.offset,field.bytes)) return invalid();
            Save(parent+field.offset,field.bytes);
        }
    }
    return true;
}
Prediction::Scope::Scope(SDK::Entity* player,SDK::UserCmd& command)
{
    if (!Config::settings.RagebotEnable || !Config::settings.RagebotPrediction || !SDK::globals || !std::isfinite(SDK::globals->interval)
        || SDK::globals->interval <= 0 || !player || player->IsDead() || !Available()) return;
    if (Config::settings.RagebotPerformance) {
        const float now = CommandTick(player,command.commandNumber,player->m_nTickBase())*SDK::globals->interval;
        if (Exploits::state.shifting && !Exploits::state.AllowAttack(now)) return;
        auto* weapon = player->Weapon();
        if (!weapon || (!(command.buttons&1) && !Config::settings.RagebotAutoFire)) return;
        const float cooldown = std::max(player->m_flNextAttack(),weapon->m_flNextPrimaryAttack())-now;
        if (cooldown > 0 && (!Config::settings.RagebotAutoStop
            || (!Config::settings.RagebotStopBetweenShots && (!Config::settings.RagebotStopEarly || cooldown > .2f)))) return;
    }
    Performance::Measure measured(Performance::Prediction);
    visibleAngles = command.viewAngles;
    local = player;
    DataMap* map = SDK::Method<DataMap*(__thiscall*)(void*)>(local,17)(local);
    if (!ValidMap(map)) { ReportFailure("Engine prediction unavailable: invalid player prediction datamap"); return; }
    absVelocityOffset = FindField(map,"m_vecAbsVelocity");
    if (absVelocityOffset < 0) { ReportFailure("Engine prediction unavailable: absolute velocity datamap field missing"); return; }
    if (!SaveMap(map)) return;
    // Include the networked state used by targeting even when absent from a datamap.
    Save(SDK::offsets.origin,12); Save(SDK::offsets.velocity,12); Save(SDK::offsets.viewOffset,12);
    Save(SDK::offsets.flags,4); Save(SDK::offsets.tickbase,4); Save(SDK::offsets.duck,4);
    Save(SDK::offsets.punch,12); Save(SDK::offsets.eyeAngles,12);
    Save(SDK::offsets.currentCommand,4);
    SDK::Entity* weapon = player->Weapon();
    float accuracyBackup = weapon ? weapon->Field<float>(SDK::offsets.accuracyPenalty) : 0;
    globals = *SDK::globals;
    absOrigin = SDK::Method<const SDK::Vector&(__thiscall*)(void*)>(local,10)(local);
    absAngles = SDK::Method<const SDK::Vector&(__thiscall*)(void*)>(local,11)(local);
    absVelocity = local->Field<SDK::Vector>(absVelocityOffset);
    inPrediction = *(bool*)((BYTE*)SDK::prediction+8);
    firstTime = *(bool*)((BYTE*)SDK::prediction+0x18);
    seed = *SDK::predictionSeed; predictingPlayer = *SDK::predictionPlayer;
    tickbase = CommandTick(local,command.commandNumber,local->m_nTickBase());
    commandTime = tickbase*SDK::globals->interval;
    SDK::globals->curTime = commandTime; SDK::globals->frameTime = SDK::globals->interval; SDK::globals->tickCount = tickbase;
    *(bool*)((BYTE*)SDK::prediction+8) = true;
    *(bool*)((BYTE*)SDK::prediction+0x18) = false;
    *SDK::predictionSeed = hashCommand(command.commandNumber)&0x7fffffff;
    *SDK::predictionPlayer = local;
    local->Field<SDK::UserCmd*>(SDK::offsets.currentCommand) = &command;
    SDK::Method<void(__thiscall*)(void*,SDK::Entity*)>(SDK::moveHelper,1)(SDK::moveHelper,local);
    SDK::Method<void(__thiscall*)(void*,SDK::Entity*)>(SDK::gameMovement,3)(SDK::gameMovement,local);
    weaponBackup = weapon; savedAccuracy = accuracyBackup;
    Simulate(command);
    active = true;
    static bool logged = false;
    if (!logged) { Logger::Write("Engine movement prediction active for ragebot command"); logged = true; }
}
void Prediction::Scope::Simulate(SDK::UserCmd& command)
{
    alignas(16) std::array<std::byte,0x200> moveData{};
    SDK::Method<void(__thiscall*)(void*,SDK::Vector&)>(SDK::prediction,13)(SDK::prediction,command.viewAngles);
    SDK::Method<void(__thiscall*)(void*,SDK::Entity*,SDK::UserCmd*,void*,void*)>(SDK::prediction,20)(
        SDK::prediction,local,&command,SDK::moveHelper,moveData.data());
    SDK::Method<void(__thiscall*)(void*,SDK::Entity*,void*)>(SDK::gameMovement,1)(SDK::gameMovement,local,moveData.data());
    SDK::Method<void(__thiscall*)(void*,SDK::Entity*,SDK::UserCmd*,void*)>(SDK::prediction,21)(
        SDK::prediction,local,&command,moveData.data());
    if (weaponBackup) SDK::Method<void(__thiscall*)(void*)>(weaponBackup,484)(weaponBackup);
}
void Prediction::Scope::RestorePlayer()
{
    Resolver::setOrigin(local,absOrigin); Resolver::setAngles(local,absAngles); Resolver::setVelocity(local,absVelocity);
    for (const SavedField& field : saved) std::memcpy((BYTE*)local+field.offset,field.bytes.data(),field.bytes.size());
    if (weaponBackup) weaponBackup->Field<float>(SDK::offsets.accuracyPenalty) = savedAccuracy;
}
bool Prediction::Scope::Repredict(SDK::UserCmd& command)
{
    if (!active) return false;
    RestorePlayer();
    SDK::globals->curTime = commandTime; SDK::globals->frameTime = SDK::globals->interval; SDK::globals->tickCount = tickbase;
    local->Field<SDK::UserCmd*>(SDK::offsets.currentCommand) = &command;
    SDK::Method<void(__thiscall*)(void*)>(SDK::gameMovement,2)(SDK::gameMovement);
    Simulate(command);
    return true;
}
Prediction::Scope::~Scope()
{
    if (!active) return;
    SDK::Method<void(__thiscall*)(void*,SDK::Entity*)>(SDK::gameMovement,4)(SDK::gameMovement,local);
    SDK::Method<void(__thiscall*)(void*,SDK::Entity*)>(SDK::moveHelper,1)(SDK::moveHelper,nullptr);

    RestorePlayer();
    SDK::Method<void(__thiscall*)(void*,SDK::Vector&)>(SDK::prediction,13)(SDK::prediction,visibleAngles);
    *(bool*)((BYTE*)SDK::prediction+8) = inPrediction;
    *(bool*)((BYTE*)SDK::prediction+0x18) = firstTime;
    *SDK::predictionSeed = seed; *SDK::predictionPlayer = predictingPlayer;
    *SDK::globals = globals;
    SDK::Method<void(__thiscall*)(void*)>(SDK::gameMovement,2)(SDK::gameMovement);
    commandTime = -1;
}
