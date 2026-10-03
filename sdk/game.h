#pragma once
#include "math.h"
#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <string>

namespace SDK
{
    template<typename Function>
    Function Method(void* object,unsigned index) { return (Function)(*(void***)object)[index]; }

    struct RecvProp;
    struct RecvTable
    {
        RecvProp* properties; int count; void* decoder; const char* name; bool initialized,inList;
    };
    struct RecvProp
    {
        const char* name; int type,flags,stringSize; bool insideArray;
        const void* extra; RecvProp* arrayProp; void* arrayLength; void* proxy; void* tableProxy;
        RecvTable* table; int offset,stride,elements; const char* parentName;
    };
    struct DataVariant
    {
        union { float floating; int integer; void* pointer; float vector[3]; std::int64_t integer64; };
        int type;
    };
    struct RecvProxyData { const RecvProp* property; DataVariant value; int element,objectId; };
    static_assert(offsetof(RecvProxyData,value) == 8);
    using RecvProxy = void(__cdecl*)(const RecvProxyData*,void*,void*);
    inline RecvProp* simulationProperty = nullptr;
    struct ClientClass { void* create; void* createEvent; const char* name; RecvTable* table; ClientClass* next; int id; };
    struct Globals
    {
        float realTime; int frameCount; float absoluteFrameTime,absoluteFrameStartTime,curTime,frameTime;
        int maxClients,tickCount; float interval,interpolation;
    };
    struct UserCmd
    {
        void* vtable; int commandNumber,tickCount; Vector viewAngles,aimDirection;
        float forwardMove,sideMove,upMove; int buttons; std::uint8_t impulse; int weaponSelect,weaponSubtype,randomSeed;
        short mouseX,mouseY; bool predicted; Vector headAngles,headOffset;
    };
    static_assert(sizeof(UserCmd) == 0x64);
    static_assert(offsetof(UserCmd,buttons) == 0x30);
    static_assert(offsetof(Globals,interval) == 0x20);
    static_assert(sizeof(RecvProp) == 0x3c);

    struct PlayerInfo
    {
        std::uint64_t version,xuid; char name[128]; int userId; char steamId[33];
        unsigned friendsId; char friendsName[128]; bool fake,hltv; unsigned customFiles[4]; std::uint8_t filesDownloaded;
    };
    struct WeaponData
    {
        std::byte pad0[0x14]; int maxClip; std::byte pad1[0x70];
        const char* hudName; const char* weaponName; std::byte pad2[0x38]; int type; std::byte pad3[0x10];
        float cycleTime,cycleTimeAlt; std::byte padCycle[0xc];
        int damage; float headMultiplier,armorRatio; int bullets; float penetration; std::byte pad4[8]; float range,rangeModifier;
    };
    static_assert(offsetof(WeaponData,damage) == 0xf0);
    static_assert(offsetof(WeaponData,type) == 0xc8);
    static_assert(offsetof(WeaponData,cycleTime) == 0xdc);
    static_assert(offsetof(WeaponData,penetration) == 0x100);
    static_assert(offsetof(WeaponData,range) == 0x10c);
    struct StudioHitbox
    {
        int bone,group; Vector minimum,maximum; int nameOffset; Vector orientation; float radius; std::byte padding[16];
    };
    static_assert(sizeof(StudioHitbox) == 0x44);
    struct StudioBone
    {
        int nameOffset,parent;
        std::byte padding[0x98]; int flags; std::byte tail[0x34];
    };
    static_assert(sizeof(StudioBone) == 0xd8);
    static_assert(offsetof(StudioBone,flags) == 0xa0);
    struct BoneSegment { Vector from,to; };
    struct HitboxSet
    {
        int nameOffset,count,offset;
        const StudioHitbox* Hitbox(int index) const
        {
            return index >= 0 && index < count ? (const StudioHitbox*)((const BYTE*)this+offset)+index : nullptr;
        }
    };
    struct StudioHeader
    {
        int id,version,checksum; char name[64]; int length;
        Vector eye,illumination,hullMin,hullMax,viewMin,viewMax;
        int flags,bones,boneOffset,boneControllers,boneControllerOffset,hitboxSets,hitboxSetOffset;
        const HitboxSet* Set(int index) const
        {
            return index >= 0 && index < hitboxSets ? (const HitboxSet*)((const BYTE*)this+hitboxSetOffset)+index : nullptr;
        }
    };
    static_assert(offsetof(StudioHeader,hitboxSets) == 0xac);
    struct Capsule
    {
        Vector a,b; float radius{}; int group{};
        Vector Center() const { return (a+b)*.5f; }
        bool Intersects(const Vector& start,const Vector& end) const
        {
            return SegmentDistanceSquared(start,end,a,b) <= radius*radius;
        }
    };

    struct Offsets
    {
        int health,life,team,origin,viewOffset,velocity,simulation,hitboxSet,activeWeapon,tickbase,punch,nextAttack;
        int armor,helmet,heavyArmor,immunity,clip,nextPrimary,itemDefinition,recoilIndex;
        int flags,moveType,scoped,poses,eyeAngles,duck,clientAnimation,lowerBodyYaw,currentCommand,accuracyPenalty;
    };
    inline Offsets offsets{};
    class Entity
    {
    public:
        template<typename T> T& Field(int offset) { return *(T*)((BYTE*)this+offset); }
        int& m_iHealth() { return Field<int>(offsets.health); }
        char& m_lifeState() { return Field<char>(offsets.life); }
        int& m_iTeamNum() { return Field<int>(offsets.team); }
        Vector& m_vecOrigin() { return Field<Vector>(offsets.origin); }
        Vector& m_vecViewOffset() { return Field<Vector>(offsets.viewOffset); }
        Vector& m_vecVelocity() { return Field<Vector>(offsets.velocity); }
        Vector& m_aimPunchAngle() { return Field<Vector>(offsets.punch); }
        float& m_flSimulationTime() { return Field<float>(offsets.simulation); }
        int& m_nTickBase() { return Field<int>(offsets.tickbase); }
        float& m_flNextAttack() { return Field<float>(offsets.nextAttack); }
        unsigned& m_hActiveWeapon() { return Field<unsigned>(offsets.activeWeapon); }
        int& m_ArmorValue() { return Field<int>(offsets.armor); }
        bool& m_bHasHelmet() { return Field<bool>(offsets.helmet); }
        bool& m_bHasHeavyArmor() { return Field<bool>(offsets.heavyArmor); }
        bool& m_bGunGameImmunity() { return Field<bool>(offsets.immunity); }
        int& m_iClip1() { return Field<int>(offsets.clip); }
        float& m_flNextPrimaryAttack() { return Field<float>(offsets.nextPrimary); }
        short& m_iItemDefinitionIndex() { return Field<short>(offsets.itemDefinition); }
        float& m_flRecoilIndex() { return Field<float>(offsets.recoilIndex); }
        bool IsDormant()
        {
            void* networkable = (BYTE*)this+8;
            return Method<bool(__thiscall*)(void*)>(networkable,9)(networkable);
        }
        bool IsDead() { return m_lifeState() != 0 || m_iHealth() <= 0; }
        bool IsPlayer()
        {
            void* networkable = (BYTE*)this+8;
            ClientClass* type = Method<ClientClass*(__thiscall*)(void*)>(networkable,2)(networkable);
            return type && std::string(type->name) == "CCSPlayer";
        }
        Vector Eye() { return m_vecOrigin()+m_vecViewOffset(); }
        Entity* Weapon();
        WeaponData* Data() { return Method<WeaponData*(__thiscall*)(void*)>(this,461)(this); }
        float Spread() { return Method<float(__thiscall*)(void*)>(this,453)(this); }
        float Inaccuracy() { return Method<float(__thiscall*)(void*)>(this,483)(this); }
        std::vector<Capsule> Hitboxes(float time = -1);
        std::vector<BoneSegment> Skeleton();
        Vector RenderOrigin()
        {
            void* renderable = (BYTE*)this+4;
            return Method<const Vector&(__thiscall*)(void*)>(renderable,1)(renderable);
        }
        void RenderBounds(Vector& minimum,Vector& maximum)
        {
            void* renderable = (BYTE*)this+4;
            Method<void(__thiscall*)(void*,Vector&,Vector&)>(renderable,17)(renderable,minimum,maximum);
        }
    };
    inline void* client = nullptr;
    class RenderBonesGuard
    {
        Entity* player = nullptr;
        std::vector<Matrix3x4> cache,accessor;
        int count = 0,counter = 0,readableMask = 0,writableMask = 0;
        float setupTime = 0;
        bool ready = false,aliased = false,rebuild = false;
    public:
        explicit RenderBonesGuard(Entity* entity,bool rebuildAfter = false);
        ~RenderBonesGuard();
        bool Valid() const { return ready; }
        RenderBonesGuard(const RenderBonesGuard&) = delete;
        RenderBonesGuard& operator=(const RenderBonesGuard&) = delete;
    };
    inline void* engine = nullptr;
    bool PublishRenderBones(Entity* player,const Matrix3x4* matrices,int count,int mask);
    inline void* entityList = nullptr;
    inline void* engineTrace = nullptr;
    inline void* physics = nullptr;
    inline void* modelInfo = nullptr;
    inline void* cvar = nullptr;
    inline void* clientMode = nullptr;
    inline void* prediction = nullptr;
    inline void* gameMovement = nullptr;
    inline void* moveHelper = nullptr;
    inline int* predictionSeed = nullptr;
    inline Entity** predictionPlayer = nullptr;
    inline int lastShotOffset = -1;
    inline int zoomLevelOffset = -1;
    inline Globals* globals = nullptr;
    inline bool ready = false;
    bool Initialize();
    Entity* Player(int index);
    Entity* Local();
    bool InGame();
    int MaxClients();
    bool PlayerDetails(int index,PlayerInfo& info);
    float Cvar(const char* name,float fallback);
    float LerpTime();
    float Latency();
    float MaxUnlag();
    const char* WeaponName(Entity* weapon);
    bool WorldToScreen(const Vector& point,Vector& screen);
    bool RecordValid(float simulationTime,float now,float correction,float maxUnlag);
    int FindOffset(RecvTable* table,const char* property,int parent = 0);
}
