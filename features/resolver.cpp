#include "resolver.h"
#include "prediction.h"
#include "render_pose.h"
#include "../utils/logger.h"
#include <cstring>
#include <limits>

namespace
{
    template<typename T> T& State(void* state,int offset) { return *(T*)((BYTE*)state+offset); }
    void Capture(Resolver::Variant& variant,void* state,Resolver::Layer* layers,SDK::Entity* player)
    {
        std::memcpy(variant.state.data(),state,variant.state.size());
        std::memcpy(variant.layers.data(),layers,sizeof(variant.layers));
        std::memcpy(variant.poses.data(),(BYTE*)player+SDK::offsets.poses,sizeof(variant.poses));
        variant.feetYaw = State<float>(state,0x80);
        variant.valid = true;
    }
    void Restore(const Resolver::Variant& variant,void* state,Resolver::Layer* layers,SDK::Entity* player)
    {
        std::memcpy(state,variant.state.data(),variant.state.size());
        std::memcpy(layers,variant.layers.data(),sizeof(variant.layers));
        std::memcpy((BYTE*)player+SDK::offsets.poses,variant.poses.data(),sizeof(variant.poses));
    }
}
bool Resolver::Available()
{
    return setOrigin && setAngles && setVelocity && invalidateBones;
}
int Resolver::ReplayTicks(float delta,float interval)
{
    if (!std::isfinite(delta) || !std::isfinite(interval) || delta <= 0 || interval <= 0) return 0;
    const double ticks = static_cast<double>(delta)/interval;
    const double rounded = std::floor(ticks+.5);
    // Allow float timestamp rounding, without inventing fractional simulation ticks.
    if (rounded < 1 || rounded > 16 || std::fabs(ticks-rounded) > .1) return 0;
    return static_cast<int>(rounded);
}
float Resolver::InterpolateYaw(float previous,float current,float fraction)
{
    if (!std::isfinite(previous) || !std::isfinite(current) || !std::isfinite(fraction)) return NAN;
    const float from = std::remainder(previous,360.f),to = std::remainder(current,360.f);
    return std::remainder(from+std::remainder(to-from,360.f)*std::clamp(fraction,0.f,1.f),360.f);
}
bool Resolver::SolveMove(Result& current,const Result* previous,unsigned blacklist)
{
    if (!previous || current.shot || !(current.flags&1) || !(previous->flags&1)
        || !std::isfinite(std::hypot(current.velocity.x,current.velocity.y))
        || std::hypot(current.velocity.x,current.velocity.y) < 40) return false;
    const Layer& moving = current.networkLayers[6];
    const Layer& prior = previous->networkLayers[6];
    constexpr float tolerance = .001f;
    if (!std::isfinite(moving.playbackRate) || moving.playbackRate <= 0
        || !std::isfinite(moving.weight) || !std::isfinite(prior.weight)
        || !std::isfinite(current.networkLayers[12].weight)
        || moving.weight < 0 || moving.weight > 1
        || current.networkLayers[12].weight < 0 || current.networkLayers[12].weight > 1
        || moving.sequence != prior.sequence || std::fabs(moving.weight-prior.weight) > tolerance) return false;
    float best = INFINITY,second = INFINITY;
    int selected = -1;
    for (int side = 0; side < 3; ++side) {
        const auto& variant = current.variants[side];
        if (!variant.valid || (blacklist&(1u<<side)) || variant.layers[6].sequence != moving.sequence) continue;
        const float rate = variant.layers[6].playbackRate;
        const float moveWeight = variant.layers[6].weight,leanWeight = variant.layers[12].weight;
        // Lean weight measures acceleration, not yaw. Use it to reject inconsistent replays.
        if (!std::isfinite(rate) || rate < 0 || !std::isfinite(moveWeight) || !std::isfinite(leanWeight)
            || std::fabs(moveWeight-moving.weight) > .05f
            || std::fabs(leanWeight-current.networkLayers[12].weight) > .05f) continue;
        const float error = std::fabs(rate-moving.playbackRate);
        if (error < best) { second = best; best = error; selected = side; }
        else second = std::min(second,error);
    }
    if (selected < 0 || best > tolerance || !std::isfinite(second) || second-best <= tolerance) return false;
    current.selected = selected;
    current.confidence = std::clamp((second-best)/std::max(second,tolerance),0.f,1.f);
    current.method = Result::Method::Playback;
    return true;
}
bool Resolver::SolveStand(Result& current,const Result* previous,unsigned blacklist)
{
    const float speed = std::hypot(current.velocity.x,current.velocity.y);
    if (current.shot || !(current.flags&1) || !std::isfinite(speed) || speed > 1
        || !std::isfinite(current.lowerBodyYaw) || !std::isfinite(current.simulationTime)) return false;
    auto select = [&](float yaw,float confidence,Result::Method method) {
        float best = INFINITY,second = INFINITY;
        int selected = -1;
        for (int side = 0; side < 3; ++side) {
            if (!current.variants[side].valid || (blacklist&(1u<<side))) continue;
            const float feet = current.variants[side].feetYaw;
            if (!std::isfinite(feet)) continue;
            const float error = std::fabs(std::remainder(feet-yaw,360.f));
            if (error < best) { second = best; best = error; selected = side; }
            else second = std::min(second,error);
        }
        if (selected < 0 || best > 10 || !std::isfinite(second) || second-best <= 10) return false;
        current.selected = selected;
        current.confidence = confidence;
        current.method = method;
        return true;
    };
    const float elapsed = previous ? current.simulationTime-previous->simulationTime : 0;
    const bool continuous = previous && !previous->shot && (previous->flags&1)
        && elapsed > 0 && elapsed <= .25f && std::isfinite(previous->lowerBodyYaw);
    if (continuous && std::fabs(std::remainder(current.lowerBodyYaw-previous->lowerBodyYaw,360.f)) > 1
        && select(current.lowerBodyYaw,.55f,Result::Method::LowerBody)) return true;
    const float age = current.simulationTime-current.lastMoveTime;
    if (continuous && current.hasLastMove && std::isfinite(current.lastMoveYaw)
        && age >= 0 && age <= .22f
        && std::isfinite(current.eyeAngles.y) && std::isfinite(previous->eyeAngles.y)
        && std::fabs(std::remainder(current.eyeAngles.y-previous->eyeAngles.y,360.f)) <= 15
        && select(current.lastMoveYaw,.35f,Result::Method::LastMove)) return true;
    // An unchanged LBY alone does not establish the current standing orientation.
    return false;
}
Resolver::Result Resolver::Reconstruct(SDK::Entity* player,float previousTime,const SDK::Vector& previousOrigin,
    float previousDuck,const Result* previous,unsigned blacklist)
{
    Result result{};
    RenderPose::Bypass historicalAnimation;
    auto unavailable = [&](const char* reason) {
        static ULONGLONG last = 0;
        const ULONGLONG now = GetTickCount64();
        if (now-last >= 5000) { Logger::Write(reason); last = now; }
        return result;
    };
    if (!player || !SDK::globals || !Available() || SDK::offsets.scoped < 0x14
        || !std::isfinite(SDK::globals->interval) || SDK::globals->interval <= 0)
        return unavailable("Resolver unavailable: engine helpers or globals missing");
    void* state = player->Field<void*>(SDK::offsets.scoped-0x14);
    Layer* layers = player->Field<Layer*>(0x2990);
    if (!state || !layers || State<void*>(state,0x60) != player)
        return unavailable("Resolver unavailable: animation state/layers do not match this entity; required safe points will reject shots");
    const float minimumYaw = State<float>(state,MinimumYawOffset),maximumYaw = State<float>(state,MaximumYawOffset);
    if (!std::isfinite(minimumYaw) || !std::isfinite(maximumYaw) || minimumYaw > 0 || maximumYaw < 0
        || minimumYaw < -180 || maximumYaw > 180)
        return unavailable("Resolver unavailable: animation yaw limits invalid");
    Prediction::DataMap* map = SDK::Method<Prediction::DataMap*(__thiscall*)(void*)>(player,17)(player);
    const int absVelocityOffset = Prediction::FindField(map,"m_vecAbsVelocity");
    const int eflagsOffset = Prediction::FindField(map,"m_iEFlags");
    const int effectsOffset = Prediction::FindField(map,"m_fEffects");
    if (absVelocityOffset < 0 || eflagsOffset < 0 || effectsOffset < 0)
        return unavailable("Resolver unavailable: prediction datamap fields missing; required safe points will reject shots");
    const SDK::Vector absVelocity = player->Field<SDK::Vector>(absVelocityOffset);
    const int eflags = player->Field<int>(eflagsOffset),effects = player->Field<int>(effectsOffset);
    Variant backup{};
    SDK::RenderBonesGuard renderBones(player);
    if (!renderBones.Valid()) return unavailable("Resolver unavailable: render bone cache snapshot invalid");
    Capture(backup,state,layers,player);
    const SDK::Globals globalsBackup = *SDK::globals;
    const SDK::Vector origin = player->m_vecOrigin(),velocity = player->m_vecVelocity();
    const SDK::Vector absOrigin = SDK::Method<const SDK::Vector&(__thiscall*)(void*)>(player,10)(player);
    const SDK::Vector absAngles = SDK::Method<const SDK::Vector&(__thiscall*)(void*)>(player,11)(player);
    const SDK::Vector eyes = player->Field<SDK::Vector>(SDK::offsets.eyeAngles);
    const float duck = player->Field<float>(SDK::offsets.duck),time = player->m_flSimulationTime();
    const bool clientAnimation = player->Field<bool>(SDK::offsets.clientAnimation);
    const int flags = player->Field<int>(SDK::offsets.flags);
    if (!std::isfinite(eyes.x) || !std::isfinite(eyes.y) || !std::isfinite(time))
        return unavailable("Resolver unavailable: non-finite network angles or simulation time");
    const float delta = time-previousTime;
    const int replayTicks = ReplayTicks(delta,SDK::globals->interval);
    const float displacement = (origin-previousOrigin).LengthSquared();
    if (!replayTicks || !std::isfinite(displacement) || displacement > 4096
        || (previous && (!std::isfinite(previous->eyeAngles.y) || !std::isfinite(previous->duck)
            || std::none_of(previous->variants.begin(),previous->variants.end(),[](const Variant& v) { return v.valid; })))) previous = nullptr;
    result.ticks = previous ? replayTicks : 1;
    const SDK::Vector animationVelocity = previous && delta > 0 && delta <= .25f
        ? (origin-previousOrigin)*(1.f/delta) : velocity;
    result.networkLayers = backup.layers;
    result.eyeAngles = eyes; result.velocity = animationVelocity;
    result.duck = duck; result.flags = flags;
    result.simulationTime = time;
    result.lowerBodyYaw = player->Field<float>(SDK::offsets.lowerBodyYaw);
    SDK::Entity* weapon = player->Weapon();
    float shotTime = NAN;
    result.shot = SDK::lastShotOffset < 0 || !weapon;
    if (!result.shot) {
        shotTime = weapon->Field<float>(SDK::lastShotOffset);
        result.shot = !std::isfinite(shotTime) || (shotTime > previousTime && shotTime <= time);
    }
    for (int side = 0; side < 3; ++side) {
        const Variant& start = previous && previous->variants[side].valid ? previous->variants[side] : backup;
        Restore(start,state,layers,player);
        State<float>(state,0x6c) = time-result.ticks*SDK::globals->interval;
        for (int tick = 1; tick <= result.ticks; ++tick) {
            const float fraction = tick/static_cast<float>(result.ticks);
            const float tickTime = time-(result.ticks-tick)*SDK::globals->interval;
            SDK::globals->curTime = tickTime;
            SDK::globals->frameTime = SDK::globals->interval;
            SDK::globals->tickCount = static_cast<int>(std::floor(tickTime/SDK::globals->interval+.5f));
            const SDK::Vector tickOrigin = previous ? previousOrigin+(origin-previousOrigin)*fraction : origin;
            player->m_vecOrigin() = tickOrigin;
            player->m_vecVelocity() = animationVelocity;
            player->Field<float>(SDK::offsets.duck) = previous ? previousDuck+(duck-previousDuck)*fraction : duck;
            SDK::Vector tickEyes = eyes;
            if (previous && result.ticks > 1) {
                if (!result.shot) tickEyes.y = InterpolateYaw(previous->eyeAngles.y,eyes.y,fraction);
                else if (std::isfinite(shotTime) && tickTime < shotTime) tickEyes.y = previous->eyeAngles.y;
            }
            player->Field<SDK::Vector>(SDK::offsets.eyeAngles) = tickEyes;
            player->Field<int>(SDK::offsets.flags) = previous && tick < result.ticks ? previous->flags : flags;
            setOrigin(player,tickOrigin); setVelocity(player,animationVelocity);
            State<int>(state,0x70) = SDK::globals->frameCount-1;
            if (side != 0) State<float>(state,0x80) = std::remainder(tickEyes.y+(side == 1 ? minimumYaw : maximumYaw),360.f);
            player->Field<bool>(SDK::offsets.clientAnimation) = true;
            SDK::Method<void(__thiscall*)(void*)>(player,224)(player);
        }
        result.variants[side].feetYaw = State<float>(state,0x80);
        setAngles(player,{0,result.variants[side].feetYaw,0});
        invalidateBones(player);
        result.variants[side].hitboxes = player->Hitboxes();
        Capture(result.variants[side],state,layers,player);
        result.variants[side].valid = !result.variants[side].hitboxes.empty();
    }
    SolveMove(result,previous,blacklist);
    if (previous && !previous->shot && !result.shot && (previous->flags&1) && (flags&1)
        && std::isfinite(eyes.y) && std::isfinite(previous->eyeAngles.y)
        && std::isfinite(result.lowerBodyYaw) && std::isfinite(previous->lowerBodyYaw)
        && std::fabs(std::remainder(eyes.y-previous->eyeAngles.y,360.f)) <= 15
        && std::fabs(std::remainder(result.lowerBodyYaw-previous->lowerBodyYaw,360.f)) <= 1
        && time-previous->lastMoveTime >= 0 && time-previous->lastMoveTime <= .22f) {
        result.hasLastMove = previous->hasLastMove;
        result.lastMoveTime = previous->lastMoveTime; result.lastMoveYaw = previous->lastMoveYaw;
    }
    if (result.method == Result::Method::Playback && result.confidence >= .5f) {
        result.hasLastMove = true; result.lastMoveTime = time;
        result.lastMoveYaw = result.variants[result.selected].feetYaw;
    }
    SolveStand(result,previous,blacklist);
    Restore(backup,state,layers,player);
    player->m_vecOrigin() = origin; player->m_vecVelocity() = velocity;
    player->Field<float>(SDK::offsets.duck) = duck;
    player->Field<SDK::Vector>(SDK::offsets.eyeAngles) = eyes;
    player->Field<bool>(SDK::offsets.clientAnimation) = clientAnimation;
    player->Field<int>(SDK::offsets.flags) = flags;
    setOrigin(player,absOrigin); setAngles(player,absAngles); setVelocity(player,absVelocity);
    player->Field<int>(eflagsOffset) = eflags;
    player->Field<int>(effectsOffset) = effects;
    *SDK::globals = globalsBackup;
    static bool logged = false;
    if (!logged && result.variants[1].valid && result.variants[2].valid) {
        Logger::Write("Resolver generated networked, left and right animation/bone hypotheses"); logged = true;
    }
    return result;
}
bool Resolver::MatchRecord(Result& current,const Result& candidate,unsigned blacklist)
{
    auto finiteVector = [](const SDK::Vector& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); };
    const float age = current.simulationTime-candidate.simulationTime;
    if (!std::isfinite(age) || age < 0 || age > .2f || !std::isfinite(candidate.confidence)
        || !std::isfinite(current.confidence) || !std::isfinite(current.duck) || !std::isfinite(candidate.duck)
        || !finiteVector(current.velocity) || !finiteVector(candidate.velocity)
        || !finiteVector(current.eyeAngles) || !finiteVector(candidate.eyeAngles)
        || !std::isfinite(current.lowerBodyYaw) || !std::isfinite(candidate.lowerBodyYaw)
        || std::fabs(std::remainder(current.lowerBodyYaw-candidate.lowerBodyYaw,360.f)) > 15) return false;
    // Transfer a side only from strong direct evidence in the same movement state.
    // Matched and brute-force guesses cannot reinforce themselves through history.
    if (current.shot || candidate.shot || current.confidence >= .2f || candidate.confidence < .5f
        || (candidate.method != Result::Method::Playback && candidate.method != Result::Method::LowerBody)
        || candidate.selected < 0 || candidate.selected >= 3
        || !candidate.variants[candidate.selected].valid
        || (blacklist&(1u<<candidate.selected)) || !current.variants[candidate.selected].valid
        || ((current.flags^candidate.flags)&1) || std::fabs(current.duck-candidate.duck) > .05f
        || (current.velocity-candidate.velocity).LengthSquared() > 400
        || std::fabs(std::remainder(current.eyeAngles.y-candidate.eyeAngles.y,360.f)) > 10) return false;
    for (int index : {6,7,12}) {
        const Layer& a = current.networkLayers[index];
        const Layer& b = candidate.networkLayers[index];
        if (a.sequence != b.sequence || !std::isfinite(a.playbackRate) || !std::isfinite(b.playbackRate)
            || !std::isfinite(a.weight) || !std::isfinite(b.weight)
            || std::fabs(a.playbackRate-b.playbackRate) > .005f || std::fabs(a.weight-b.weight) > .05f) return false;
    }
    current.selected = candidate.selected;
    current.confidence = std::min(candidate.confidence,.5f);
    current.method = Result::Method::Matched;
    return true;
}
void Resolver::BruteForce(Result& result,unsigned blacklist)
{
    if (std::isfinite(result.confidence) && result.confidence >= .2f && result.selected >= 0 && result.selected < 3
        && result.variants[result.selected].valid && !(blacklist&(1u<<result.selected))) return;
    result.confidence = 0;
    // Confirmed trajectory misses exclude a side; spread and obstruction never do.
    for (int side : {0,1,2}) {
        if (!result.variants[side].valid || (blacklist&(1u<<side))) continue;
        result.selected = side;
        result.confidence = .2f;
        result.method = Result::Method::Bruteforce;
        return;
    }
}
bool Resolver::SafePoint(const Result& result,size_t hitbox,const SDK::Vector& eye,const SDK::Vector& point,float range)
{
    const float distance = (point-eye).LengthSquared();
    if (!std::isfinite(range) || range <= 0 || !std::isfinite(distance) || distance < 1e-8f) return false;
    const SDK::Vector end = eye+(point-eye).Normalized()*range;
    return SafeRay(result,hitbox,eye,end);
}
bool Resolver::SafeRay(const Result& result,size_t hitbox,const SDK::Vector& eye,const SDK::Vector& end)
{
    if (!std::isfinite(eye.x) || !std::isfinite(eye.y) || !std::isfinite(eye.z)
        || !std::isfinite(end.x) || !std::isfinite(end.y) || !std::isfinite(end.z)) return false;
    for (const Variant& variant : result.variants) {
        if (!variant.valid || hitbox >= variant.hitboxes.size() || !variant.hitboxes[hitbox].Intersects(eye,end)) return false;
    }
    return true;
}
