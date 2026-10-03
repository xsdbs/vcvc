#pragma once
#include "game.h"
namespace SDK
{
    constexpr unsigned ShotHull = 0x0600400b;
    constexpr unsigned ShotMask = ShotHull | 0x40000000;
    constexpr unsigned HitboxContents = 0x40000000;
    struct alignas(16) AlignedVector { float x,y,z,w; };
    struct Ray
    {
        AlignedVector start,delta,startOffset{},extents{};
        const Matrix3x4* transform = nullptr; bool isRay = true,swept = true;
        Ray(const Vector& from,const Vector& to)
        {
            const Vector difference = to-from;
            start = {from.x,from.y,from.z,0};
            delta = {difference.x,difference.y,difference.z,0};
            swept = difference.LengthSquared() != 0;
        }
    };
    static_assert(sizeof(Ray) == 0x50);
    struct Plane { Vector normal; float distance; char type,signBits; char padding[2]; };
    struct TraceSurface { const char* name; short properties; unsigned short flags; };
    struct Trace
    {
        Vector start,end; Plane plane; float fraction = 1; int contents; unsigned short displacement;
        bool allSolid,startSolid; float leftSolid; TraceSurface surface; int hitgroup;
        short physicsBone; unsigned short worldSurface; Entity* entity; int hitbox;
        bool Hit() const { return fraction < 1 || allSolid || startSolid; }
    };
    static_assert(sizeof(Trace) == 0x54);
    class TraceFilter
    {
    public:
        Entity* skip;
        explicit TraceFilter(Entity* local) : skip(local) {}
        virtual bool ShouldHitEntity(void* object,int) { return object != skip; }
        virtual int GetTraceType() const { return 0; }
    };
    struct SurfaceData
    {
        std::byte padding[0x50]; float maxSpeed,jump,penetration,damage; unsigned short material;
    };
    inline Trace TraceLine(const Vector& from,const Vector& to,TraceFilter& filter,unsigned mask = ShotMask)
    {
        Ray ray(from,to); Trace trace{};
        Method<void(__thiscall*)(void*,const Ray&,unsigned,TraceFilter*,Trace*)>(engineTrace,5)(engineTrace,ray,mask,&filter,&trace);
        return trace;
    }
    inline int Contents(const Vector& position,unsigned mask = ShotMask)
    {
        return Method<int(__thiscall*)(void*,const Vector&,int,void**)>(engineTrace,0)(engineTrace,position,mask,nullptr);
    }
    inline SurfaceData* Material(short properties)
    {
        return Method<SurfaceData*(__thiscall*)(void*,int)>(physics,5)(physics,properties);
    }
}
