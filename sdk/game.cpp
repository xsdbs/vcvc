#include "game.h"
#include "../utils/logger.h"
#include "../features/render_pose.h"
#include <cstdio>
#include <cstring>
#include <limits>

namespace
{
    bool BoneMemory(const void* address,size_t bytes)
    {
        auto begin = reinterpret_cast<std::uintptr_t>(address);
        if (!begin || bytes > UINTPTR_MAX-begin) return false;
        const auto end = begin+bytes;
        while (begin < end) {
            MEMORY_BASIC_INFORMATION region{};
            if (!VirtualQuery(reinterpret_cast<void*>(begin),&region,sizeof(region)) || region.State != MEM_COMMIT
                || (region.Protect&(PAGE_GUARD|PAGE_NOACCESS))
                || !(region.Protect&(PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))) return false;
            const auto next = reinterpret_cast<std::uintptr_t>(region.BaseAddress)+region.RegionSize;
            if (next <= begin) return false;
            begin = next;
        }
        return true;
    }
    SDK::RecvProp* FindProperty(SDK::RecvTable* table,const char* name)
    {
        if (!table) return nullptr;
        for (int i = 0; i < table->count; ++i) {
            auto& property = table->properties[i];
            if (property.name && std::strcmp(property.name,name) == 0) return &property;
            if (auto found = FindProperty(property.table,name)) return found;
        }
        return nullptr;
    }
    void* Capture(const char* moduleName,const char* prefix)
    {
        HMODULE module = GetModuleHandleA(moduleName);
        if (!module) return nullptr;
        using Factory = void*(__cdecl*)(const char*,int*);
        Factory factory = (Factory)GetProcAddress(module,"CreateInterface");
        if (!factory) return nullptr;
        for (int version = 99; version >= 0; --version) {
            char name[80];
            std::snprintf(name,sizeof(name),"%s%03d",prefix,version);
            if (void* object = factory(name,nullptr)) { Logger::Write(name); return object; }
        }
        return nullptr;
    }
}
bool SDK::PublishRenderBones(Entity* player,const Matrix3x4* matrices,int count,int mask)
{
    if (!player || !matrices || count < 1 || count > 256 || !BoneMemory(player,0x2954)
        || player->Field<int>(0x2920) != count) return false;
    auto* cached = player->Field<Matrix3x4*>(0x2914);
    auto* accessed = player->Field<Matrix3x4*>(0x26a8);
    const size_t bytes = count*sizeof(Matrix3x4);
    if (!BoneMemory(cached,bytes) || !BoneMemory(accessed,bytes)) return false;
    std::memcpy(cached,matrices,bytes);
    if (accessed != cached) std::memcpy(accessed,matrices,bytes);
    player->Field<int>(0x26ac) |= mask;
    player->Field<int>(0x26b0) |= mask;
    return true;
}
SDK::RenderBonesGuard::RenderBonesGuard(Entity* entity,bool rebuildAfter) : player(entity),rebuild(rebuildAfter)
{
    if (!entity || !BoneMemory(entity,0x292c)) return;
    // Verified in the installed legacy client: cache-copy displacement 0x2914;
    // invalidation writes setup time 0x2928 and model-bone counter 0x2690.
    count = entity->Field<int>(0x2920);
    if (count < 0 || count > 256) return;
    auto* cached = entity->Field<Matrix3x4*>(0x2914);
    auto* accessed = entity->Field<Matrix3x4*>(0x26a8);
    const size_t bytes = count*sizeof(Matrix3x4);
    if (count && (!BoneMemory(cached,bytes) || !BoneMemory(accessed,bytes))) return;
    if (count) cache.assign(cached,cached+count);
    aliased = cached == accessed;
    if (count && !aliased) accessor.assign(accessed,accessed+count);
    counter = entity->Field<int>(0x2690); setupTime = entity->Field<float>(0x2928);
    readableMask = entity->Field<int>(0x26ac); writableMask = entity->Field<int>(0x26b0);
    ready = true;
}
SDK::RenderBonesGuard::~RenderBonesGuard()
{
    if (!ready) return;
    auto* cached = player->Field<Matrix3x4*>(0x2914);
    auto* accessed = player->Field<Matrix3x4*>(0x26a8);
    const size_t bytes = count*sizeof(Matrix3x4);
    const bool sameSize = player->Field<int>(0x2920) == count;
    const bool restored = sameSize && (!count || ((aliased || accessed != cached) && BoneMemory(cached,bytes) && BoneMemory(accessed,bytes)));
    if (restored && count) {
        std::memcpy(cached,cache.data(),bytes);
        if (accessed != cached) std::memcpy(accessed,aliased ? cache.data() : accessor.data(),bytes);
    }
    // Never restore an old allocation pointer: SetupBones may have grown its vector.
    // A size change leaves no trustworthy old cache, so force a normal render rebuild.
    // Historical capture may run before interpolation. Restoring its old valid
    // counter would allow that network-position cache to survive into rendering.
    const bool reuse = restored && !rebuild;
    player->Field<int>(0x2690) = reuse ? counter : -1;
    player->Field<float>(0x2928) = reuse ? setupTime : -std::numeric_limits<float>::max();
    player->Field<int>(0x26ac) = reuse ? readableMask : 0;
    player->Field<int>(0x26b0) = reuse ? writableMask : 0;
}
int SDK::FindOffset(RecvTable* table,const char* property,int parent)
{
    for (int i = 0; i < table->count; ++i) {
        const RecvProp& prop = table->properties[i];
        if (std::strcmp(prop.name,property) == 0) return parent+prop.offset;
        if (prop.table && prop.table->count > 0) {
            const int result = FindOffset(prop.table,property,parent+prop.offset);
            if (result >= 0) return result;
        }
    }
    return -1;
}
bool SDK::Initialize()
{
    client = Capture("client.dll","VClient");
    engine = Capture("engine.dll","VEngineClient");
    entityList = Capture("client.dll","VClientEntityList");
    engineTrace = Capture("engine.dll","EngineTraceClient");
    physics = Capture("vphysics.dll","VPhysicsSurfaceProps");
    modelInfo = Capture("engine.dll","VModelInfoClient");
    gameMovement = Capture("client.dll","GameMovement");
    prediction = Capture("client.dll","VClientPrediction");
    cvar = Capture("vstdlib.dll","VEngineCvar");
    if (!client || !engine || !entityList || !engineTrace || !physics || !modelInfo || !cvar || !prediction) {
        Logger::Write("Gameplay interface capture failed"); return false;
    }
    ClientClass* classes = Method<ClientClass*(__thiscall*)(void*)>(client,8)(client);
    simulationProperty = nullptr;
    for (ClientClass* current = classes; current && !simulationProperty; current = current->next)
        simulationProperty = FindProperty(current->table,"m_flSimulationTime");
    lastShotOffset = -1;
    for (ClientClass* current = classes; current && lastShotOffset < 0; current = current->next)
        if (current->table) lastShotOffset = FindOffset(current->table,"m_fLastShotTime");
    zoomLevelOffset = -1;
    for (ClientClass* current = classes; current && zoomLevelOffset < 0; current = current->next)
        if (current->table) zoomLevelOffset = FindOffset(current->table,"m_zoomLevel");
    struct Request { int* output; const char* table; const char* property; };
    const Request requests[] = {
        {&offsets.currentCommand,"DT_BasePlayer","m_hViewEntity"},
        {&offsets.accuracyPenalty,"DT_WeaponCSBase","m_fAccuracyPenalty"},
        {&offsets.poses,"DT_BaseAnimating","m_flPoseParameter"},
        {&offsets.eyeAngles,"DT_CSPlayer","m_angEyeAngles[0]"},
        {&offsets.duck,"DT_CSPlayer","m_flDuckAmount"},
        {&offsets.clientAnimation,"DT_BaseAnimating","m_bClientSideAnimation"},
        {&offsets.lowerBodyYaw,"DT_CSPlayer","m_flLowerBodyYawTarget"},
        {&offsets.scoped,"DT_CSPlayer","m_bIsScoped"},
        {&offsets.flags,"DT_BasePlayer","m_fFlags"}, {&offsets.moveType,"DT_BaseEntity","m_nRenderMode"},
        {&offsets.health,"DT_BasePlayer","m_iHealth"}, {&offsets.life,"DT_BasePlayer","m_lifeState"},
        {&offsets.team,"DT_BaseEntity","m_iTeamNum"}, {&offsets.origin,"DT_BaseEntity","m_vecOrigin"},
        {&offsets.viewOffset,"DT_BasePlayer","m_vecViewOffset[0]"}, {&offsets.velocity,"DT_BasePlayer","m_vecVelocity[0]"},
        {&offsets.simulation,"DT_BaseEntity","m_flSimulationTime"}, {&offsets.hitboxSet,"DT_BaseAnimating","m_nHitboxSet"},
        {&offsets.activeWeapon,"DT_BaseCombatCharacter","m_hActiveWeapon"}, {&offsets.tickbase,"DT_BasePlayer","m_nTickBase"},
        {&offsets.punch,"DT_BasePlayer","m_aimPunchAngle"}, {&offsets.nextAttack,"DT_BaseCombatCharacter","m_flNextAttack"},
        {&offsets.armor,"DT_CSPlayer","m_ArmorValue"}, {&offsets.helmet,"DT_CSPlayer","m_bHasHelmet"},
        {&offsets.heavyArmor,"DT_CSPlayer","m_bHasHeavyArmor"}, {&offsets.immunity,"DT_CSPlayer","m_bGunGameImmunity"},
        {&offsets.clip,"DT_BaseCombatWeapon","m_iClip1"}, {&offsets.nextPrimary,"DT_BaseCombatWeapon","m_flNextPrimaryAttack"},
        {&offsets.itemDefinition,"DT_BaseAttributableItem","m_iItemDefinitionIndex"},
        {&offsets.recoilIndex,"DT_WeaponCSBase","m_flRecoilIndex"}
    };
    for (const Request& request : requests) {
        int result = -1;
        for (ClientClass* current = classes; current; current = current->next) {
            if (current->table && std::strcmp(current->table->name,request.table) == 0) {
                result = FindOffset(current->table,request.property);
                break;
            }
        }
        // Some base tables are only reachable through a derived class's RecvTable tree.
        if (result < 0) {
            for (ClientClass* current = classes; current; current = current->next) {
                if (!current->table) continue;
                result = FindOffset(current->table,request.property);
                if (result >= 0) break;
            }
        }
        if (result < 0) { Logger::Write("Missing required netvar"); Logger::Write(request.property); return false; }
        *request.output = result;
    }
    ++offsets.moveType;
    offsets.currentCommand -= 4;
    void* clientModeAddress = nullptr;
    void* globalsAddress = nullptr;
    std::memcpy(&clientModeAddress,(BYTE*)(*(void***)client)[10]+5,sizeof(clientModeAddress));
    std::memcpy(&globalsAddress,(BYTE*)(*(void***)client)[11]+10,sizeof(globalsAddress));
    clientMode = *(void**)clientModeAddress;
    globals = *(Globals**)globalsAddress;
    if (!clientMode || !globals || globals->interval <= 0) {
        Logger::Write("ClientMode/Globals resolution failed"); return false;
    }
    ready = true;
    Logger::Write("Gameplay interfaces and runtime netvars ready");
    return true;
}
SDK::Entity* SDK::Player(int index)
{
    return Method<Entity*(__thiscall*)(void*,int)>(entityList,3)(entityList,index);
}
SDK::Entity* SDK::Local()
{
    return Player(Method<int(__thiscall*)(void*)>(engine,12)(engine));
}
SDK::Entity* SDK::Entity::Weapon()
{
    return Method<Entity*(__thiscall*)(void*,unsigned)>(entityList,4)(entityList,m_hActiveWeapon());
}
bool SDK::InGame()
{
    return ready && Method<bool(__thiscall*)(void*)>(engine,26)(engine);
}
int SDK::MaxClients() { return Method<int(__thiscall*)(void*)>(engine,20)(engine); }
bool SDK::PlayerDetails(int index,PlayerInfo& info)
{
    return Method<bool(__thiscall*)(void*,int,PlayerInfo*)>(engine,8)(engine,index,&info);
}
float SDK::Cvar(const char* name,float fallback)
{
    void* variable = Method<void*(__thiscall*)(void*,const char*)>(cvar,15)(cvar,name);
    if (!variable) return fallback;
    // Legacy CS:GO ConVar stores its parent at 0x1c and an XOR-encoded float at 0x2c.
    void* parent = nullptr;
    std::memcpy(&parent,(BYTE*)variable+0x1c,sizeof(parent));
    if (!parent) return fallback;
    std::uint32_t encoded = 0;
    std::memcpy(&encoded,(BYTE*)parent+0x2c,sizeof(encoded));
    encoded ^= reinterpret_cast<std::uintptr_t>(variable);
    float value = 0;
    std::memcpy(&value,&encoded,sizeof(value));
    return std::isfinite(value) ? value : fallback;
}
float SDK::LerpTime()
{
    float rate = Cvar("cl_updaterate",64);
    const float minimumRate = Cvar("sv_minupdaterate",rate), maximumRate = Cvar("sv_maxupdaterate",rate);
    if (minimumRate > 0 && maximumRate >= minimumRate) rate = std::clamp(rate,minimumRate,maximumRate);
    float ratio = Cvar("cl_interp_ratio",1);
    if (ratio == 0) ratio = 1;
    const float minimum = Cvar("sv_client_min_interp_ratio",-1), maximum = Cvar("sv_client_max_interp_ratio",-1);
    if (minimum >= 0 && maximum >= minimum) ratio = std::clamp(ratio,minimum,maximum);
    return std::max(Cvar("cl_interp",0),ratio/std::max(rate,1.f));
}
float SDK::Latency()
{
    void* channel = Method<void*(__thiscall*)(void*)>(engine,78)(engine);
    if (!channel) return 0;
    const auto latency = Method<float(__thiscall*)(void*,int)>(channel,9);
    return latency(channel,0)+latency(channel,1);
}
float SDK::MaxUnlag() { return Cvar("sv_maxunlag",1); }
bool SDK::RecordValid(float simulationTime,float now,float correction,float maxUnlag)
{
    const float age = now-simulationTime;
    return std::isfinite(simulationTime) && std::isfinite(now) && std::isfinite(correction)
        && std::isfinite(maxUnlag) && maxUnlag > 0
        && age >= -0.001f && age <= maxUnlag && std::fabs(correction-age) <= .2f;
}
const char* SDK::WeaponName(Entity* weapon)
{
    if (!weapon) return "";
    WeaponData* data = weapon->Data();
    if (!data || !data->weaponName) return "";
    return std::strncmp(data->weaponName,"weapon_",7) == 0 ? data->weaponName+7 : data->weaponName;
}
bool SDK::WorldToScreen(const Vector& point,Vector& screen)
{
    int width = 0,height = 0;
    Method<void(__thiscall*)(void*,int&,int&)>(engine,5)(engine,width,height);
    const ViewMatrix& matrix = Method<const ViewMatrix&(__thiscall*)(void*)>(engine,37)(engine);
    return Project(point,matrix,width,height,screen);
}
std::vector<SDK::Capsule> SDK::Entity::Hitboxes(float time)
{
    RenderPose::Bypass historicalBones;
    std::vector<Capsule> result;
    void* renderable = (BYTE*)this+4;
    const void* model = Method<const void*(__thiscall*)(void*)>(renderable,8)(renderable);
    if (!model) return result;
    const StudioHeader* header = Method<const StudioHeader*(__thiscall*)(void*,const void*)>(modelInfo,32)(modelInfo,model);
    if (!header) return result;
    const HitboxSet* set = header->Set(Field<int>(offsets.hitboxSet));
    if (!set) return result;
    alignas(16) Matrix3x4 bones[256]{};
    if (!Method<bool(__thiscall*)(void*,Matrix3x4*,int,int,float)>(renderable,13)(renderable,bones,256,0x100,time >= 0 ? time : globals->curTime)) return result;
    for (int index = 0; index < set->count; ++index) {
        const StudioHitbox* hitbox = set->Hitbox(index);
        if (!hitbox || (hitbox->group != 1 && hitbox->group != 2 && hitbox->group != 3)) continue;
        if (hitbox->bone < 0 || hitbox->bone >= header->bones || hitbox->bone >= 256 || hitbox->radius < 0) continue;
        result.push_back({Transform(hitbox->minimum,bones[hitbox->bone]),Transform(hitbox->maximum,bones[hitbox->bone]),hitbox->radius,hitbox->group});
    }
    return result;
}
std::vector<SDK::BoneSegment> SDK::Entity::Skeleton()
{
    RenderPose::Read visualBones;
    std::vector<BoneSegment> result;
    if (!modelInfo || !globals) return result;
    void* renderable = (BYTE*)this+4;
    const void* model = Method<const void*(__thiscall*)(void*)>(renderable,8)(renderable);
    if (!model) return result;
    const StudioHeader* header = Method<const StudioHeader*(__thiscall*)(void*,const void*)>(modelInfo,32)(modelInfo,model);
    if (!header || header->bones <= 0 || header->bones > 256 || header->length < static_cast<int>(sizeof(StudioHeader))
        || header->length > 16*1024*1024 || header->boneOffset < static_cast<int>(sizeof(StudioHeader))
        || header->boneOffset > header->length
        || header->bones > (header->length-header->boneOffset)/static_cast<int>(sizeof(StudioBone))) return result;
    const auto descriptions = reinterpret_cast<const StudioBone*>((const BYTE*)header+header->boneOffset);
    alignas(16) Matrix3x4 matrices[256]{};
    // SetupBones reuses the engine's current-frame bone cache for this mask.
    if (!Method<bool(__thiscall*)(void*,Matrix3x4*,int,int,float)>(renderable,13)(renderable,matrices,256,0x100,globals->curTime)) return result;
    result.reserve(header->bones);
    auto position = [&](int bone) { return Vector{matrices[bone].values[0][3],matrices[bone].values[1][3],matrices[bone].values[2][3]}; };
    auto finite = [](const Vector& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); };
    for (int bone = 0; bone < header->bones; ++bone) {
        const int parent = descriptions[bone].parent;
        if (!(descriptions[bone].flags&0x100) || parent < 0 || parent >= header->bones || parent == bone) continue;
        const Vector from = position(parent),to = position(bone);
        if (finite(from) && finite(to)) result.push_back({from,to});
    }
    return result;
}
