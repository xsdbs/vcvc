#pragma once
#include "../sdk/game.h"
#include <vector>

namespace Prediction
{
    using Hash = unsigned(__thiscall*)(unsigned);
    inline Hash hashCommand = nullptr;
    inline float commandTime = -1;
    struct TypeDescription;
    struct DataMap
    {
        TypeDescription* fields; int count; const char* name; DataMap* base;
        int packedSize; void* optimized;
    };
    struct TypeDescription
    {
        int type; const char* name; int offset; unsigned short count,flags;
        const char* externalName; void* saveRestore; void* input; DataMap* embedded;
        int bytes; TypeDescription* overrideField; int overrideCount; float tolerance;
        int flatOffset[2]; unsigned short group;
    };
    // Verified against this legacy client's data descriptors, not the older SDK layout.
    static_assert(sizeof(TypeDescription) == 0x3c);
    static_assert(offsetof(TypeDescription,embedded) == 0x1c);
    static_assert(offsetof(TypeDescription,bytes) == 0x20);
    static_assert(sizeof(DataMap) == 0x18);
    bool ValidMap(const DataMap* map);
    int FindField(DataMap* map,const char* name,int parent = 0,int depth = 0);
    bool Available();
    void Reset();
    int CommandTick(SDK::Entity* player,int number,int base);
    class Scope
    {
        struct SavedField { int offset; std::vector<std::byte> bytes; };
        std::vector<SavedField> saved;
        SDK::Entity* local = nullptr;
        SDK::Entity* weaponBackup = nullptr;
        float savedAccuracy = 0;
        SDK::Globals globals{};
        SDK::Vector absOrigin{},absVelocity{},absAngles{};
        SDK::Vector visibleAngles{};
        int absVelocityOffset = -1,seed = -1;
        SDK::Entity* predictingPlayer = nullptr;
        bool active = false,inPrediction = false,firstTime = false;
        void Save(int offset,int size);
        bool SaveMap(DataMap* map,int parent = 0,int depth = 0);
        void Simulate(SDK::UserCmd& command);
        void RestorePlayer();
    public:
        Scope(SDK::Entity* player,SDK::UserCmd& command);
        ~Scope();
        bool Repredict(SDK::UserCmd& command);
        bool Active() const { return active; }
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
    };
}
