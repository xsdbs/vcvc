#include "shots.h"
#include "hitfeedback.h"
#include "../core/config.h"
#include "../utils/logger.h"
#include <deque>
#include <cstring>
#include <array>

namespace
{
    struct Pending
    {
        int target = 0,commandNumber = 0;
        std::shared_ptr<LagRecord_t> record;
        SDK::Capsule capsule;
        SDK::Vector eye,impact;
        float queued = 0,fired = 0;
        bool confirmed = false,hasImpact = false,hit = false;
    };
    struct PlayerFeedback {
        SDK::Entity* player = nullptr; unsigned mask = 0; float expires = 0;
        SDK::Vector eyes{},velocity{}; float duck = 0,lowerBodyYaw = 0; int flags = 0;
    };
    std::deque<Pending> pending;
    std::array<PlayerFeedback,65> feedback;
    struct Impact { SDK::Vector point,eye; float time; };
    std::deque<Impact> impacts;
    int EventIndex(void* event,const char* key)
    {
        const int userId = SDK::Method<int(__thiscall*)(void*,const char*,int)>(event,6)(event,key,0);
        return SDK::Method<int(__thiscall*)(void*,int)>(SDK::engine,9)(SDK::engine,userId);
    }
}
void Shots::Clear()
{
    pending.clear(); feedback = {}; statistics = {}; impacts.clear(); HitFeedback::Clear();
}
void Shots::ClearPlayer(int index)
{
    if (index < 1 || index > 64) return;
    feedback[index] = {};
    std::erase_if(pending,[&](const Pending& shot) { return shot.target == index; });
}
unsigned Shots::Blacklist(int index,SDK::Entity* player)
{
    if (index < 1 || index > 64) return 0;
    PlayerFeedback& entry = feedback[index];
    if (entry.player != player || SDK::globals->curTime >= entry.expires) entry = {};
    if (entry.player && entry.mask) {
        const auto eyes = player->Field<SDK::Vector>(SDK::offsets.eyeAngles);
        const float duck = player->Field<float>(SDK::offsets.duck);
        const float lby = player->Field<float>(SDK::offsets.lowerBodyYaw);
        const float velocityChange = (player->m_vecVelocity()-entry.velocity).LengthSquared();
        if (!std::isfinite(eyes.y) || !std::isfinite(lby) || !std::isfinite(duck) || !std::isfinite(velocityChange)
            || std::fabs(std::remainder(eyes.y-entry.eyes.y,360.f)) > 35
            || std::fabs(std::remainder(lby-entry.lowerBodyYaw,360.f)) > 35
            || velocityChange > 6400 || std::fabs(duck-entry.duck) > .2f
            || ((player->Field<int>(SDK::offsets.flags)^entry.flags)&1)) entry = {};
    }
    return entry.mask;
}
void Shots::Add(int index,const std::shared_ptr<LagRecord_t>& record,const SDK::Capsule& capsule,const SDK::Vector& eye,int commandNumber)
{
    if (index < 1 || index > 64 || !record) return;
    if (commandNumber > 0 && std::any_of(pending.begin(),pending.end(),[&](const Pending& shot) { return shot.commandNumber == commandNumber; })) return;
    if (pending.size() >= 32) pending.pop_front();
    Pending shot{}; shot.target = index; shot.record = record; shot.capsule = capsule; shot.commandNumber = commandNumber;
    shot.eye = eye; shot.queued = SDK::globals->curTime;
    pending.push_back(std::move(shot));
}
void Shots::Event(void* event)
{
    if (!event || !SDK::ready) return;
    const char* name = SDK::Method<const char*(__thiscall*)(void*)>(event,1)(event);
    if (!name) return;
    if (std::strcmp(name,"round_start") == 0 || std::strcmp(name,"game_newmap") == 0) { LagComp::Clear(); return; }
    if (!SDK::InGame()) return;
    const int local = SDK::Method<int(__thiscall*)(void*)>(SDK::engine,12)(SDK::engine);
    if (std::strcmp(name,"weapon_fire") == 0 && EventIndex(event,"userid") == local) {
        for (Pending& shot : pending) {
            if (!shot.confirmed && SDK::globals->curTime-shot.queued <= SDK::Latency()+.5f) {
                shot.confirmed = true; shot.fired = SDK::globals->curTime; ++statistics.fired; break;
            }
        }
    } else if (std::strcmp(name,"bullet_impact") == 0 && EventIndex(event,"userid") == local) {
        const auto read = SDK::Method<float(__thiscall*)(void*,const char*,float)>(event,8);
        const SDK::Vector point{read(event,"x",0),read(event,"y",0),read(event,"z",0)};
        if (SDK::Entity* player = SDK::Local()) {
            const float now = SDK::globals->realTime;
            while (!impacts.empty() && (now < impacts.front().time || now-impacts.front().time > .5f)) impacts.pop_front();
            if (impacts.size() >= 64) impacts.pop_front();
            impacts.push_back({point,player->Eye(),now});
        }
        // Several confirmations in one packet make impact-to-shot association ambiguous.
        // Do not turn that uncertainty into resolver feedback.
        int eligible = 0;
        for (const Pending& shot : pending)
            if (shot.confirmed && SDK::globals->curTime-shot.fired <= SDK::Latency()+.5f) ++eligible;
        if (eligible != 1) return;
        Pending* candidate = nullptr;
        for (Pending& shot : pending) {
            if (!shot.confirmed || SDK::globals->curTime-shot.fired > SDK::Latency()+.5f) continue;
            if (!shot.hasImpact) { candidate = &shot; break; }
            candidate = &shot;
        }
        if (candidate && (!candidate->hasImpact || (point-candidate->eye).LengthSquared() > (candidate->impact-candidate->eye).LengthSquared())) {
            candidate->impact = point; candidate->hasImpact = true;
        }
    } else if (std::strcmp(name,"player_hurt") == 0 && EventIndex(event,"attacker") == local) {
        const int victim = EventIndex(event,"userid");
        SDK::Vector position{};
        bool hasPosition = false;
        for (Pending& shot : pending) {
            if (shot.confirmed && !shot.hit && shot.target == victim && SDK::globals->curTime-shot.fired <= SDK::Latency()+.5f) {
                position = shot.capsule.Center(); hasPosition = true;
                shot.hit = true; break;
            }
        }
        if (victim == local || (!Config::settings.VisWorldHitmarker && !Config::settings.VisWorldDamage)) return;
        SDK::Entity* player = victim >= 1 && victim <= 64 ? SDK::Player(victim) : nullptr;
        if (!player) return;
        const auto read = SDK::Method<int(__thiscall*)(void*,const char*,int)>(event,6);
        const int damage = read(event,"dmg_health",0),group = read(event,"hitgroup",0);
        if (!hasPosition) {
            position = player->m_vecOrigin()+player->m_vecViewOffset()*.5f;
            for (const SDK::Capsule& capsule : player->Hitboxes()) {
                if (capsule.group == group) { position = capsule.Center(); break; }
            }
        }
        // Impacts may be behind a penetrated target. Project onto their trajectory
        // at the victim's depth instead of drawing the marker on the wall behind it.
        float nearest = 32.f*32.f;
        SDK::Vector hitPosition = position;
        const float now = SDK::globals->realTime;
        for (const Impact& impact : impacts) {
            if (now < impact.time || now-impact.time > .25f) continue;
            const SDK::Vector ray = impact.point-impact.eye;
            const float length = ray.LengthSquared();
            if (length < .001f) continue;
            const SDK::Vector projected = impact.eye+ray*std::clamp((position-impact.eye).Dot(ray)/length,0.f,1.f);
            const float distance = (projected-position).LengthSquared();
            if (distance < nearest) { nearest = distance; hitPosition = projected; }
        }
        HitFeedback::Add(victim,player,hitPosition,damage,now);
    }
}
void Shots::Process()
{
    if (!SDK::InGame()) { Clear(); return; }
    const float now = SDK::globals->curTime;
    for (auto iterator = pending.begin(); iterator != pending.end();) {
        Pending& shot = *iterator;
        if (!shot.confirmed) {
            if (now-shot.queued > SDK::Latency()+1.f) iterator = pending.erase(iterator);
            else ++iterator;
            continue;
        }
        if (now-shot.fired < std::max(.5f,SDK::Latency()+.2f)) { ++iterator; continue; }
        if (shot.hit) {
            ++statistics.hits;
            if (shot.record->resolved && feedback[shot.target].player == shot.record->player)
                feedback[shot.target].mask &= ~(1u<<shot.record->correction.selected);
            Logger::Write("Shot feedback: confirmed hit");
        } else if (shot.hasImpact) {
            const float impactDistance = (shot.impact-shot.eye).Length();
            const float targetDistance = (shot.capsule.Center()-shot.eye).Length();
            if (impactDistance+shot.capsule.radius < targetDistance) {
                ++statistics.blocked; Logger::Write("Shot feedback: impact stopped before target");
            } else if (!shot.capsule.Intersects(shot.eye,shot.impact)) {
                ++statistics.spreadMisses; Logger::Write("Shot feedback: spread/trajectory miss");
            } else if (shot.record->resolved && shot.record->correction.confidence >= .2f
                && SDK::Player(shot.target) == shot.record->player && !shot.record->player->IsDead()) {
                ++statistics.resolverMisses;
                Blacklist(shot.target,shot.record->player);
                PlayerFeedback& entry = feedback[shot.target];
                if (entry.player != shot.record->player) entry = {};
                entry.player = shot.record->player;
                entry.eyes = shot.record->eyeAngles; entry.velocity = shot.record->velocity;
                entry.flags = shot.record->flags; entry.duck = shot.record->duck;
                entry.lowerBodyYaw = shot.record->correction.lowerBodyYaw;
                entry.mask |= 1u<<shot.record->correction.selected;
                entry.expires = now+3;
                if (entry.mask == 7) entry.mask = 0;
                Logger::Write("Shot feedback: trajectory crossed selected hypothesis without hurt; side temporarily excluded");
            } else Logger::Write("Shot feedback: unclassified miss; no resolver side excluded");
        }
        iterator = pending.erase(iterator);
    }
}
