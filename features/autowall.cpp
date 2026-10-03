#include "autowall.h"
#include <algorithm>
#include <cmath>

namespace
{
    class ShotFilter : public SDK::TraceFilter
    {
        SDK::Entity* target;
    public:
        ShotFilter(SDK::Entity* local,SDK::Entity* enemy) : TraceFilter(local),target(enemy) {}
        bool ShouldHitEntity(void* entity,int mask) override
        {
            return entity != target && TraceFilter::ShouldHitEntity(entity,mask);
        }
    };
    bool Exit(const SDK::Trace& entry,const SDK::Vector& direction,ShotFilter& filter,SDK::Trace& exit)
    {
        const int initial = SDK::Contents(entry.end);
        for (float distance = 4; distance <= 92; distance += 4) {
            const SDK::Vector point = entry.end+direction*distance;
            const int contents = SDK::Contents(point);
            if ((contents & SDK::ShotHull) && (!(contents & SDK::HitboxContents) || contents == initial)) continue;
            exit = SDK::TraceLine(point,point-direction*4,filter);
            if (exit.startSolid && (exit.surface.flags & 0x8000)) {
                exit = SDK::TraceLine(point,entry.end,filter);
                if (exit.Hit() && !exit.startSolid) return true;
            } else if (exit.Hit() && !exit.startSolid) {
                const bool enterNoDraw = (entry.surface.flags & 0x80) != 0;
                const bool exitNoDraw = (exit.surface.flags & 0x80) != 0;
                if (!exitNoDraw || enterNoDraw) return true;
            }
        }
        return false;
    }
}
float Autowall::RangeDamage(float damage,float modifier,float distance)
{
    if (!std::isfinite(damage) || !std::isfinite(modifier) || !std::isfinite(distance)
        || damage <= 0 || modifier <= 0 || modifier > 1 || distance < 0) return 0;
    return damage*std::pow(modifier,distance*.002f);
}
float Autowall::PenetrationLoss(float damage,float weaponPenetration,float thickness,float modifier,float lossFraction)
{
    if (weaponPenetration <= 0 || modifier <= 0) return damage;
    const float inverse = 1.f/modifier;
    return std::max(0.f,damage*lossFraction+std::max(0.f,3.f/weaponPenetration*1.25f)*inverse*3.f
        +thickness*thickness*inverse/24.f);
}
float Autowall::ArmorDamage(float damage,float armorRatio,int armor,bool helmet,bool heavy,int group,
    float headMultiplier,float headScale,float bodyScale)
{
    if (heavy) headScale *= .5f;
    if (group == 1) damage *= headMultiplier*headScale;
    else if (group == 3) damage *= 1.25f*bodyScale;
    else if (group == 6 || group == 7) damage *= .75f*bodyScale;
    else damage *= bodyScale;
    const bool armored = group == 1 ? helmet || heavy : group >= 2 && group <= 5;
    if (armor <= 0 || !armored) return damage;
    float ratio = armorRatio*.5f,bonus = .5f,heavyBonus = 1;
    if (heavy) { damage *= .85f; ratio *= .5f; bonus = heavyBonus = .33f; }
    float health = damage*ratio;
    if ((damage-health)*bonus*heavyBonus > armor) health = damage-armor/bonus;
    return health;
}
float Autowall::DamagePoint(SDK::Entity* local,SDK::Entity* target,const SDK::WeaponData& weapon,
    const SDK::Vector& from,const SDK::Capsule& hitbox,const SDK::Vector& destination,ShotResult* result)
{
    if (result) *result = {};
    if (!local || !target || !SDK::engineTrace || !SDK::physics || weapon.damage <= 0) return 0;
    const float targetDistance = (destination-from).Length();
    if (!std::isfinite(targetDistance) || !std::isfinite(weapon.range) || targetDistance <= 0 || targetDistance > weapon.range) return 0;
    const SDK::Vector direction = (destination-from).Normalized();
    SDK::Vector source = from;
    float damage = static_cast<float>(weapon.damage),traveled = 0;
    ShotFilter filter(local,target);
    for (int penetrations = 0; penetrations <= 4; ++penetrations) {
        const float remaining = (destination-source).Dot(direction);
        if (remaining < 0) return 0;
        SDK::Trace entry = SDK::TraceLine(source,destination,filter);
        const float segment = remaining*entry.fraction;
        traveled += segment;
        damage = RangeDamage(damage,weapon.rangeModifier,segment);
        if (damage < 1) return 0;
        if (!entry.Hit()) {
            const bool ct = target->m_iTeamNum() == 3;
            const float finalDamage = ArmorDamage(damage,weapon.armorRatio,target->m_ArmorValue(),target->m_bHasHelmet(),
                target->m_bHasHeavyArmor(),hitbox.group,weapon.headMultiplier,
                SDK::Cvar(ct ? "mp_damage_scale_ct_head" : "mp_damage_scale_t_head",1),
                SDK::Cvar(ct ? "mp_damage_scale_ct_body" : "mp_damage_scale_t_body",1));
            if (result) *result = {finalDamage,traveled,penetrations,true};
            return finalDamage;
        }
        if (penetrations == 4 || weapon.penetration <= 0 || traveled > 3000 || entry.startSolid) return 0;
        SDK::SurfaceData* enter = SDK::Material(entry.surface.properties);
        if (!enter || enter->penetration < .1f || enter->material == 'F') return 0;
        SDK::Trace exit{};
        if (!Exit(entry,direction,filter,exit)) return 0;
        SDK::SurfaceData* leave = SDK::Material(exit.surface.properties);
        if (!leave) return 0;
        float modifier = (enter->penetration+leave->penetration)*.5f,loss = .16f;
        if (enter->material == 'Y' || enter->material == 'G') { modifier = 3; loss = .05f; }
        else if ((entry.contents & 8) || (entry.surface.flags & 0x80)) modifier = 1;
        if (enter->material == leave->material) {
            if (enter->material == 'W' || enter->material == 'U') modifier = 3;
            else if (enter->material == 'L') modifier = 2;
        }
        const float thickness = (exit.end-entry.end).Length();
        if (!std::isfinite(thickness) || traveled+thickness > targetDistance || traveled+thickness > weapon.range) return 0;
        damage -= PenetrationLoss(damage,weapon.penetration,thickness,modifier,loss);
        if (damage < 1) return 0;
        traveled += thickness;
        damage = RangeDamage(damage,weapon.rangeModifier,thickness);
        source = exit.end;
    }
    return 0;
}

float Autowall::Damage(SDK::Entity* local,SDK::Entity* target,const SDK::WeaponData& weapon,
    const SDK::Vector& from,const SDK::Capsule& hitbox)
{
    return DamagePoint(local,target,weapon,from,hitbox,hitbox.Center());
}
