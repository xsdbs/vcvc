#pragma once
#include "../sdk/trace.h"

namespace Autowall
{
    struct ShotResult { float damage = 0,distance = 0; int penetrations = 0; bool reachedTarget = false; };
    float RangeDamage(float damage,float modifier,float distance);
    float PenetrationLoss(float damage,float weaponPenetration,float thickness,float modifier,float lossFraction);
    float ArmorDamage(float damage,float armorRatio,int armor,bool helmet,bool heavy,int group,float headMultiplier,float headScale,float bodyScale);
    float DamagePoint(SDK::Entity* local,SDK::Entity* target,const SDK::WeaponData& weapon,const SDK::Vector& from,const SDK::Capsule& hitbox,const SDK::Vector& destination,ShotResult* result = nullptr);
    float Damage(SDK::Entity* local,SDK::Entity* target,const SDK::WeaponData& weapon,const SDK::Vector& from,const SDK::Capsule& hitbox);
}
