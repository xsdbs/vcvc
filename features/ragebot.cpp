#include "ragebot.h"
#include "autowall.h"
#include "extras.h"
#include "prediction.h"
#include "shots.h"
#include "performance.h"
#include "../core/config.h"
#include "../utils/logger.h"
#include <array>
#include <deque>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{
    char lastStatus[256] = "Idle";
    struct Seed { float a,b,c,d,cosB,sinB,cosD,sinD; };
    std::array<Seed,256> seeds;
    bool seedsReady = false;
    int nextTarget = 1,lastScanCommand = -1,commandStart = 1;
    bool Blocked(const char* reason)
    {
        std::snprintf(lastStatus,sizeof(lastStatus),"%s",reason);
        static ULONGLONG last = 0;
        const ULONGLONG now = GetTickCount64();
        if (now-last >= 3000) {
            Logger::Write(reason);
            last = now;
        }
        return false;
    }
    bool CreateSeeds()
    {
        using RandomSeed = void(__cdecl*)(int);
        using RandomFloat = float(__cdecl*)(float,float);
        HMODULE module = GetModuleHandleA("vstdlib.dll");
        RandomSeed seed = (RandomSeed)GetProcAddress(module,"RandomSeed");
        RandomFloat random = (RandomFloat)GetProcAddress(module,"RandomFloat");
        if (!seed || !random) return false;
        for (int i = 0; i < 256; ++i) {
            seed(i+1);
            seeds[i] = {random(0,1),random(0,2*SDK::Pi),random(0,1),random(0,2*SDK::Pi)};
            auto& sample = seeds[i];
            sample.cosB = std::cos(sample.b); sample.sinB = std::sin(sample.b);
            sample.cosD = std::cos(sample.d); sample.sinD = std::sin(sample.d);
        }
        return true;
    }
    bool Hitchance(SDK::Entity* weapon,const SDK::WeaponData& data,const SDK::Vector& eye,
        const SDK::Vector& bulletAngles,const SDK::Capsule& capsule,const Resolver::Result* safe,size_t hitbox)
    {
        const int chance = Config::settings.RagebotHitchance;
        if (chance == 0) return true;
        if (!seedsReady) seedsReady = CreateSeeds();
        if (!seedsReady) return false;
        const float inaccuracy = weapon->Inaccuracy(),spread = weapon->Spread();
        const int item = weapon->m_iItemDefinitionIndex();
        const float recoil = weapon->m_flRecoilIndex();
        const int needed = static_cast<int>(std::ceil(chance*256.f/100.f));
        SDK::Vector forward,right,up;
        SDK::AngleVectors(bulletAngles,forward,right,up);
        int hits = 0;
        for (int i = 0; i < 256; ++i) {
            float a = seeds[i].a,c = seeds[i].c;
            if (item == 28 && recoil < 3) {
                for (int j = 3; j > recoil; --j) { a *= a; c *= c; }
                a = 1-a; c = 1-c;
            }
            const float offsetX = seeds[i].cosB*a*inaccuracy+seeds[i].cosD*c*spread;
            const float offsetY = seeds[i].sinB*a*inaccuracy+seeds[i].sinD*c*spread;
            const SDK::Vector direction = (forward+right*offsetX+up*offsetY).Normalized();
            const SDK::Vector end = eye+direction*data.range;
            if (capsule.Intersects(eye,end) && (!safe || Resolver::SafeRay(*safe,hitbox,eye,end))) ++hits;
            if (hits >= needed) return true;
            if (hits+255-i < needed) return false;
        }
        return false;
    }
}
const char* Ragebot::Status() { return lastStatus; }
std::vector<size_t> Ragebot::CandidateOrder(const std::vector<int>& groups)
{
    std::vector<size_t> order; order.reserve(groups.size());
    std::array<bool,8> seen{};
    for (size_t i = 0; i < groups.size(); ++i) {
        const int group = std::clamp(groups[i],0,7);
        if (!seen[group]) { seen[group] = true; order.push_back(i); }
    }
    for (size_t i = 0; i < groups.size(); ++i)
        if (std::find(order.begin(),order.end(),i) == order.end()) order.push_back(i);
    return order;
}
std::vector<SDK::Vector> Ragebot::AimPoints(const SDK::Capsule& capsule,const SDK::Vector& eye)
{
    const SDK::Vector center = capsule.Center();
    std::vector<SDK::Vector> result{center};
    if (!Config::settings.RagebotMultipoint || Config::settings.RagebotPointScale == 0) return result;
    SDK::Vector forward,right,up;
    SDK::AngleVectors(SDK::AngleTo(eye,center),forward,right,up);
    const float offset = capsule.radius*Config::settings.RagebotPointScale/100.f;
    result.push_back(center+right*offset); result.push_back(center-right*offset);
    result.push_back(center+up*offset); result.push_back(center-up*offset);
    return result;
}
float Ragebot::TargetScore(float damage,int health,float fov,float distance,float age)
{
    const float base = std::min(damage,static_cast<float>(health))*10-fov-age*10;
    switch (Config::settings.RagebotPriority) {
    case 1: return -fov*10000+std::min(damage,static_cast<float>(health))-age;
    case 2: return -distance*10000+std::min(damage,static_cast<float>(health))-age;
    case 3: return -static_cast<float>(health)*10000+std::min(damage,static_cast<float>(health))-age;
    default: return base;
    }
}
void Ragebot::Clear() { LagComp::Clear(); nextTarget = commandStart = 1; lastScanCommand = -1; }
std::vector<int> Ragebot::SelectTargets(const std::vector<int>& eligible,int limit,int& next,bool rotateFull)
{
    if (eligible.empty()) return {};
    if (limit <= 0 || limit >= static_cast<int>(eligible.size())) {
        if (!rotateFull) { next = 1; return eligible; }
        limit = static_cast<int>(eligible.size());
    }
    const auto found = std::lower_bound(eligible.begin(),eligible.end(),next);
    const size_t start = found == eligible.end() ? 0 : found-eligible.begin();
    std::vector<int> selected;
    selected.reserve(limit);
    for (int i = 0; i < limit; ++i) selected.push_back(eligible[(start+i)%eligible.size()]);
    next = eligible[(start+limit)%eligible.size()];
    return selected;
}
void Ragebot::UpdateRecords()
{
    if (!Config::settings.RagebotEnable || !SDK::InGame()) { Clear(); return; }
    LagComp::Update();
}
bool Ragebot::CanShoot(SDK::Entity* local,SDK::Entity* weapon,float time)
{
    return local && weapon && std::isfinite(time) && weapon->m_iClip1() > 0
        && local->m_flNextAttack() <= time && weapon->m_flNextPrimaryAttack() <= time;
}
float Ragebot::RequiredDamage(int health)
{
    return static_cast<float>(std::clamp(Config::settings.RagebotMinDamage,1,std::max(1,health)));
}
void Ragebot::ApplyAim(SDK::UserCmd& command,const SDK::Vector& angles,bool silent)
{
    SDK::Vector visible{};
    if (silent) SDK::Method<void(__thiscall*)(void*,SDK::Vector&)>(SDK::engine,18)(SDK::engine,visible);
    const SDK::Vector normalized = SDK::NormalizeAngles(angles);
    SDK::FixMovement(command.viewAngles.y,normalized.y,command.forwardMove,command.sideMove);
    command.viewAngles = normalized;
    // Keep the actual engine camera independent of the outgoing firing angle.
    if (!silent) visible = normalized;
    SDK::Method<void(__thiscall*)(void*,SDK::Vector&)>(SDK::engine,19)(SDK::engine,visible);
}
bool Ragebot::Run(SDK::UserCmd& command,Prediction::Scope* prediction)
{
    if (!Config::settings.RagebotEnable || Config::settings.menuOpened || !SDK::InGame()) return false;
    SDK::Entity* local = SDK::Local();
    if (!local || local->IsDead()) return false;
    SDK::Entity* weapon = local->Weapon();
    if (!weapon) return Blocked("Ragebot blocked: no active weapon");
    const SDK::WeaponData* data = weapon->Data();
    if (!data || data->type < 1 || data->type > 6 || data->type == 4 || weapon->m_iItemDefinitionIndex() == 64) return Blocked("Ragebot blocked: unsupported weapon");
    const float now = Prediction::commandTime >= 0 ? Prediction::commandTime : local->m_nTickBase()*SDK::globals->interval;
    if (weapon->m_iClip1() <= 0) return Blocked("Ragebot blocked: empty clip");
    const bool canShoot = CanShoot(local,weapon,now);
    if (!(command.buttons & 1) && !Config::settings.RagebotAutoFire) return false;
    const float timeUntilShot = std::max(local->m_flNextAttack(),weapon->m_flNextPrimaryAttack())-now;
    const int moveType = local->Field<unsigned char>(SDK::offsets.moveType);
    const bool shouldStop = Config::settings.RagebotAutoStop && (local->Field<int>(SDK::offsets.flags)&1)
        && !(command.buttons&2) && moveType != 8 && moveType != 9
        && (canShoot || Config::settings.RagebotStopBetweenShots
            || (Config::settings.RagebotStopEarly && timeUntilShot <= .2f));
    if (!canShoot && !shouldStop) return Blocked("Ragebot blocked: attack cooldown");
    Performance::Measure measured(Performance::Scan);
    const LagComp::Timing timing = LagComp::CurrentTiming(now);
    SDK::Vector eye = local->Eye();
    const SDK::Vector scanEye = eye;
    const bool performance = Config::settings.RagebotPerformance;
    Performance::Budget scanBudget(performance,Config::settings.RagebotBudgetMs,96);
    SDK::Capsule bestCapsule{};
    SDK::Vector bestPoint{};
    std::shared_ptr<LagRecord_t> bestRecord;
    int bestIndex = 0;
    float bestScore = -INFINITY;
    struct Candidate {
        std::shared_ptr<LagRecord_t> record;
        SDK::Capsule capsule; SDK::Vector point;
        int index; size_t hitbox; float score;
    };
    std::vector<Candidate> candidates;
    candidates.reserve(17);
    int recordsSeen = 0,validRecords = 0,points = 0,hitboxesSeen = 0,hitboxRejected = 0,safeRejected = 0;
    float highestDamage = 0;
    const int maximum = std::min(SDK::MaxClients(),64);
    std::vector<int> eligible;
    eligible.reserve(maximum > 0 ? maximum : 0);
    for (int index = 1; index <= maximum; ++index) {
        SDK::Entity* player = SDK::Player(index);
        if (!player || player == local || !player->IsPlayer() || player->IsDormant() || player->IsDead()
            || player->m_iTeamNum() == local->m_iTeamNum() || player->m_bGunGameImmunity()) continue;
        const auto& history = LagComp::History(index);
        if (std::any_of(history.begin(),history.end(),[&](const auto& record) {
            return LagComp::Usable(*record,player,timing,Config::settings.RagebotBacktrackMs);
        })) eligible.push_back(index);
    }
    if (command.commandNumber < lastScanCommand) nextTarget = 1;
    if (command.commandNumber == lastScanCommand) nextTarget = commandStart;
    else commandStart = nextTarget;
    lastScanCommand = command.commandNumber;
    const int targetLimit = performance ? (Config::settings.RagebotTargetsPerTick == 0 ? 2 : std::min(Config::settings.RagebotTargetsPerTick,4)) : Config::settings.RagebotTargetsPerTick;
    bool exhausted = false;
    for (int index : SelectTargets(eligible,targetLimit,nextTarget,performance)) {
        if (exhausted) break;
        SDK::Entity* player = SDK::Player(index);
        int scannedRecords = 0;
        for (const std::shared_ptr<LagRecord_t>& record : LagComp::ScanRecords(index,player,timing,Config::settings.RagebotBacktrackMs)) {
            if (exhausted || (performance && scannedRecords >= 3)) break;
            ++recordsSeen;
            if (record->resolved
                && (Shots::Blacklist(index,player)&(1u<<record->correction.selected))) continue;
            if (!LagComp::Usable(*record,player,timing,Config::settings.RagebotBacktrackMs)) continue;
            if (Config::settings.RagebotSafePoints && !std::all_of(record->correction.variants.begin(),record->correction.variants.end(),
                [](const Resolver::Variant& variant) { return variant.valid; })) continue;
            ++validRecords;
            ++scannedRecords;
            for (size_t hitboxIndex = 0; hitboxIndex < record->hitboxes.size(); ++hitboxIndex) {
                const SDK::Capsule& capsule = record->hitboxes[hitboxIndex];
                ++hitboxesSeen;
                if (!HitboxAllowed(capsule.group,Config::settings.RagebotHitboxes)) { ++hitboxRejected; continue; }
                for (const SDK::Vector& point : AimPoints(capsule,eye)) {
                    if (Config::settings.RagebotSafePoints
                        && !Resolver::SafePoint(record->correction,hitboxIndex,eye,point,data->range)) { ++safeRejected; continue; }
                    // Reserve work for final checks after autostop moves the eye.
                    if (!scanBudget.Available(4,.8) || !scanBudget.Consume()) { exhausted = true; nextTarget = index%maximum+1; break; }
                    const float damage = Autowall::DamagePoint(local,player,*data,eye,capsule,point);
                    ++points;
                    highestDamage = std::max(highestDamage,damage);
                    if (damage < RequiredDamage(player->m_iHealth())) continue;
                    const SDK::Vector angles = SDK::AngleTo(eye,point);
                    const float fov = std::hypot(angles.x-command.viewAngles.x,std::remainder(angles.y-command.viewAngles.y,360.f));
                    const float bodyBonus = Config::settings.RagebotLethalBody && capsule.group != 1
                        && damage >= player->m_iHealth() ? 1000000.f : 0.f;
                    const float score = TargetScore(damage,player->m_iHealth(),fov,(point-eye).Length(),now-record->simulationTime)+bodyBonus;
                    if (candidates.size() < 16 || score > candidates.back().score
                        || std::none_of(candidates.begin(),candidates.end(),[&](const Candidate& c) { return c.capsule.group == capsule.group; })) {
                        const auto duplicate = std::find_if(candidates.begin(),candidates.end(),[&](const Candidate& candidate) {
                            return candidate.index == index && candidate.capsule.group == capsule.group
                                && (!Config::settings.RagebotSafePoints || (candidate.record == record && candidate.hitbox == hitboxIndex))
                                && std::fabs(candidate.capsule.radius-capsule.radius) < .001f
                                && (candidate.capsule.a-capsule.a).LengthSquared() < .0001f
                                && (candidate.capsule.b-capsule.b).LengthSquared() < .0001f
                                && (candidate.point-point).LengthSquared() < .0001f;
                        });
                        if (duplicate != candidates.end() && duplicate->score >= score) continue;
                        if (duplicate != candidates.end()) candidates.erase(duplicate);
                        const auto place = std::find_if(candidates.begin(),candidates.end(),
                            [&](const Candidate& candidate) { return score > candidate.score; });
                        candidates.insert(place,{record,capsule,point,index,hitboxIndex,score});
                        if (candidates.size() > 16) {
                            // Retain the best option for each hitgroup even when
                            // many head multipoints have higher nominal damage.
                            std::array<int,8> counts{};
                            for (const auto& candidate : candidates) ++counts[std::clamp(candidate.capsule.group,0,7)];
                            for (size_t i = candidates.size(); i-- > 0;)
                                if (counts[std::clamp(candidates[i].capsule.group,0,7)] > 1) { candidates.erase(candidates.begin()+i); break; }
                        }
                    }
                    if (score > bestScore) {
                        bestScore = score; bestCapsule = capsule; bestPoint = point; bestRecord = record; bestIndex = index;
                    }
                    if (!canShoot && shouldStop) { exhausted = true; break; }
                }
                if (exhausted) break;
            }
        }
    }
    if (!bestRecord) {
        char diagnostic[256];
        std::snprintf(diagnostic,sizeof(diagnostic),"Ragebot blocked: records=%d valid=%d boxes=%d filtered=%d safeRejected=%d points=%d maxDamage=%.1f minDamage=%d",
            recordsSeen,validRecords,hitboxesSeen,hitboxRejected,safeRejected,points,highestDamage,Config::settings.RagebotMinDamage);
        return Blocked(diagnostic);
    }
    if (shouldStop) {
        Extras::Stop(command,local->m_vecVelocity());
        if (prediction && prediction->Repredict(command)) {
            eye = local->Eye();
        }
    }
    if (!canShoot) return Blocked("Ragebot blocked: attack cooldown");
    if (Config::settings.RagebotAutoScope && data->type == 5 && !local->Field<bool>(SDK::offsets.scoped)) {
        command.buttons &= ~1;
        command.buttons |= 2048;
        return false;
    }
    // A high-damage head point that fails hitchance must not suppress a valid
    // torso point or another target. Bound expensive spread checks to 16 candidates.
    bool foundCandidate = false;
    const bool eyeChanged = (eye-scanEye).LengthSquared() > 0;
    int hitchanceTests = 0;
    std::vector<int> groups; groups.reserve(candidates.size());
    for (const auto& candidate : candidates) groups.push_back(candidate.capsule.group);
    for (size_t candidateIndex : CandidateOrder(groups)) {
        const Candidate& candidate = candidates[candidateIndex];
        if (performance && hitchanceTests >= 4) break;
        if (!LagComp::Usable(*candidate.record,candidate.record->player,timing,Config::settings.RagebotBacktrackMs)) continue;
        if (eyeChanged && Config::settings.RagebotSafePoints && !Resolver::SafePoint(candidate.record->correction,
            candidate.hitbox,eye,candidate.point,data->range)) continue;
        // Initial scan already validated this exact ray. Only rescan if stopping changed the eye.
        if (eyeChanged) {
            if (!scanBudget.Consume()) break;
            if (Autowall::DamagePoint(local,candidate.record->player,*data,eye,candidate.capsule,candidate.point)
                < RequiredDamage(candidate.record->player->m_iHealth())) continue;
        }
        ++hitchanceTests;
        if (!Hitchance(weapon,*data,eye,SDK::AngleTo(eye,candidate.point),candidate.capsule,
            Config::settings.RagebotSafePoints ? &candidate.record->correction : nullptr,candidate.hitbox)) continue;
        bestRecord = candidate.record; bestPoint = candidate.point;
        bestCapsule = candidate.capsule; bestIndex = candidate.index;
        foundCandidate = true; break;
    }
    if (!foundCandidate) return Blocked("Ragebot blocked: no candidate met final damage, safe-point and hitchance checks");
    if (!LagComp::Usable(*bestRecord,bestRecord->player,timing,Config::settings.RagebotBacktrackMs)) return false;
    const int shotTick = LagComp::ShotTick(bestRecord->simulationTime,timing.lerp,SDK::globals->interval);
    if (shotTick < 0) return false;
    const SDK::Vector bulletAngles = SDK::AngleTo(eye,bestPoint);
    const SDK::Vector angles = SDK::NormalizeAngles(bulletAngles-local->m_aimPunchAngle()*SDK::Cvar("weapon_recoil_scale",2));
    ApplyAim(command,angles,Config::settings.RagebotSilent);
    command.tickCount = shotTick;
    if (Config::settings.RagebotAutoFire) command.buttons |= 1;
    Shots::Add(bestIndex,bestRecord,bestCapsule,eye,command.commandNumber);
    std::snprintf(lastStatus,sizeof(lastStatus),"Firing at player %d / tick %d",bestIndex,shotTick);
    static bool logged = false;
    if (!logged) { Logger::Write("Ragebot produced first firing command with selected record"); logged = true; }
    return true;
}
