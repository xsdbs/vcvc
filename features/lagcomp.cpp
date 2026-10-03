#include "lagcomp.h"
#include "shots.h"
#include "../core/config.h"
#include "performance.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
    std::array<LagComp::Records,65> history;
    std::array<unsigned,65> generation{};
    int nextResolver = 1;
}
bool LagComp::IgnoreSimulationUpdate(const SDK::RecvProxyData* data)
{
    return data && data->value.type == 0 && data->value.integer == 0;
}
bool LagComp::LayersChanged(const std::array<Resolver::Layer,13>& before,const std::array<Resolver::Layer,13>& after)
{
    for (size_t i = 0; i < before.size(); ++i) {
        const auto& a = before[i]; const auto& b = after[i];
        if (a.sequence != b.sequence || a.weight != b.weight || a.weightDelta != b.weightDelta
            || a.cycle != b.cycle || a.playbackRate != b.playbackRate) return true;
    }
    return false;
}
const LagComp::Records& LagComp::History(int index)
{
    static const Records empty;
    return index >= 1 && index <= 64 ? history[index] : empty;
}
void LagComp::ResetPlayer(int index)
{
    if (index < 1 || index > 64) return;
    history[index].clear(); ++generation[index]; Shots::ClearPlayer(index);
}
LagComp::Timing LagComp::CurrentTiming(float now)
{
    if (!std::isfinite(SDK::MaxUnlag())) return {now,0,0,0};
    const float maxUnlag = std::clamp(SDK::MaxUnlag(),0.f,1.f);
    const float lerp = SDK::LerpTime();
    return {now,lerp,std::clamp(SDK::Latency()+lerp,0.f,maxUnlag),maxUnlag};
}
bool LagComp::ValidTime(float simulationTime,const Timing& timing)
{
    return SDK::RecordValid(simulationTime,timing.now,timing.correction,timing.maxUnlag)
        && std::isfinite(timing.lerp) && timing.lerp >= 0;
}
bool LagComp::Usable(const LagRecord_t& record,SDK::Entity* player,const Timing& timing,int windowMs)
{
    if (record.index < 1 || record.index > 64 || record.player != player || record.broken || record.chokedTicks > 16
        || record.generation != generation[record.index] || !ValidTime(record.simulationTime,timing)) return false;
    const Records& records = history[record.index];
    if (records.empty()) return false;
    return windowMs == 0 ? records.front().get() == &record
        : windowMs > 0 && timing.now-record.simulationTime <= windowMs/1000.f;
}
std::vector<std::shared_ptr<LagRecord_t>> LagComp::ScanRecords(int index,SDK::Entity* player,const Timing& timing,int windowMs)
{
    std::vector<std::shared_ptr<LagRecord_t>> result;
    std::shared_ptr<LagRecord_t> latest,oldest,distant,turned,standing,strongest,reconstructed;
    float distance = 0,angle = 0,duck = 1,confidence = 0;
    const unsigned blacklist = Shots::Blacklist(index,player);
    for (const auto& record : History(index)) {
        if (!Usable(*record,player,timing,windowMs)
            || (record->resolved && (blacklist&(1u<<record->correction.selected)))) continue;
        if (!latest) latest = record;
        if (!reconstructed && std::all_of(record->correction.variants.begin(),record->correction.variants.end(),
            [](const Resolver::Variant& variant) { return variant.valid; })) reconstructed = record;
        oldest = record;
        const float displacement = (record->origin-latest->origin).LengthSquared();
        const float yaw = std::fabs(std::remainder(record->eyeAngles.y-latest->eyeAngles.y,360.f));
        if (displacement > distance) { distance = displacement; distant = record; }
        if (yaw > angle) { angle = yaw; turned = record; }
        if (record->duck < duck) { duck = record->duck; standing = record; }
        if (record->resolved && record->correction.confidence > confidence) {
            confidence = record->correction.confidence; strongest = record;
        }
    }
    // Bound costly point/penetration scans while retaining materially different
    // positions, angles and poses, rather than scanning duplicate network ticks.
    for (const auto& record : {latest,strongest,reconstructed,distant,turned,standing,oldest})
        if (record && std::find(result.begin(),result.end(),record) == result.end()) result.push_back(record);
    return result;
}
int LagComp::ShotTick(float simulationTime,float lerp,float interval)
{
    if (!std::isfinite(simulationTime) || !std::isfinite(lerp) || !std::isfinite(interval) || interval <= 0) return -1;
    const double tick = std::floor((static_cast<double>(simulationTime)+lerp)/interval+.5);
    return tick >= 0 && tick < 2147483647 ? static_cast<int>(tick) : -1;
}
void LagComp::Clear()
{
    nextResolver = 1;
    for (int index = 1; index <= 64; ++index) { history[index].clear(); ++generation[index]; }
    Shots::Clear();
}
void LagComp::Update()
{
    Performance::Measure measured(Performance::Records);
    if (!SDK::InGame()) { Clear(); return; }
    SDK::Entity* local = SDK::Local();
    if (!local) { Clear(); return; }
    if (!SDK::globals || !std::isfinite(SDK::globals->interval) || SDK::globals->interval <= 0) { Clear(); return; }
    const float maximumAge = std::clamp(SDK::MaxUnlag(),0.f,1.f);
    if (!std::isfinite(maximumAge) || maximumAge <= 0) { Clear(); return; }
    const size_t limit = static_cast<size_t>(std::clamp(std::ceil(maximumAge/SDK::globals->interval)+2.f,2.f,256.f));
    const int maximum = std::min(SDK::MaxClients(),64);
    const bool performance = Config::settings.RagebotPerformance;
    Performance::Budget resolverBudget(performance,Config::settings.RagebotBudgetMs,Config::settings.RagebotResolverPerTick);
    const int start = maximum > 0 ? std::clamp(nextResolver,1,maximum) : 1;
    for (int step = 0; step < maximum; ++step) {
        const int index = (start-1+step)%maximum+1;
        SDK::Entity* player = SDK::Player(index);
        auto& records = history[index];
        if (!player || player == local || !player->IsPlayer() || player->IsDead() || player->IsDormant()
            || player->m_iTeamNum() == local->m_iTeamNum()) { ResetPlayer(index); continue; }
        const float time = player->m_flSimulationTime();
        if (!std::isfinite(time) || !std::isfinite(player->m_vecOrigin().x)
            || !std::isfinite(player->m_vecOrigin().y) || !std::isfinite(player->m_vecOrigin().z)) {
            ResetPlayer(index); continue;
        }
        bool broken = false;
        std::array<Resolver::Layer,13> networkLayers{};
        const auto layers = player->Field<Resolver::Layer*>(0x2990);
        if (layers) std::memcpy(networkLayers.data(),layers,sizeof(networkLayers));
        if (!records.empty()) {
            broken = (player->m_vecOrigin()-records.front()->origin).LengthSquared() > 4096;
            if (records.front()->player != player || time < records.front()->simulationTime
                || (player->m_vecOrigin()-records.front()->origin).LengthSquared() > 4096) {
                ResetPlayer(index);
            }
            else if (time == records.front()->simulationTime) {
                while (!records.empty() && SDK::globals->curTime-records.back()->simulationTime > maximumAge) records.pop_back();
                continue;
            }
        }
        std::shared_ptr<LagRecord_t> record = std::make_shared<LagRecord_t>();
        record->player = player; record->simulationTime = time; record->origin = player->m_vecOrigin();
        record->index = index; record->generation = generation[index]; record->broken = broken;
        record->receivedTime = SDK::globals->curTime;
        record->networkLayers = networkLayers; record->layersKnown = layers != nullptr;
        record->velocity = player->m_vecVelocity(); record->eyeAngles = player->Field<SDK::Vector>(SDK::offsets.eyeAngles);
        if (!records.empty()) {
            const int ticks = Resolver::ReplayTicks(time-records.front()->simulationTime,SDK::globals->interval);
            record->chokedTicks = ticks ? ticks : 17;
            record->broken = record->broken || !ticks;
            if (!ticks) { ResetPlayer(index); record->generation = generation[index]; }
        }
        record->duck = player->Field<float>(SDK::offsets.duck);
        record->flags = player->Field<int>(SDK::offsets.flags);
        SDK::RenderBonesGuard renderBones(player,true);
        if ((Config::settings.RagebotCorrection || Config::settings.RagebotSafePoints) && resolverBudget.Consume()) {
            nextResolver = index%maximum+1;
            // A player can wait several updates for its reconstruction turn.
            // Replay from the last actual hypothesis, not an unbuilt intervening record.
            std::shared_ptr<LagRecord_t> previous;
            for (const auto& candidate : records) {
                if (time-candidate->simulationTime > .25f) break;
                if (!candidate->broken && candidate->correction.variants[0].valid) { previous = candidate; break; }
            }
            record->correction = Resolver::Reconstruct(player,previous ? previous->simulationTime : time,
                previous ? previous->origin : record->origin,previous ? previous->duck : record->duck,
                previous ? &previous->correction : nullptr,Shots::Blacklist(index,player));
            // Reconstruction already built the network hypothesis. Avoid an
            // additional historical SetupBones call before those three sides.
            if (record->correction.variants[0].valid) record->hitboxes = record->correction.variants[0].hitboxes;
            const unsigned blacklist = Shots::Blacklist(index,player);
            if (Config::settings.RagebotMatchRecords && record->correction.confidence < .2f) {
                for (const auto& candidate : records) {
                    if (time-candidate->simulationTime > .2f) break;
                    if (Resolver::MatchRecord(record->correction,candidate->correction,blacklist)) break;
                }
            }
            if (Config::settings.RagebotBruteforce) Resolver::BruteForce(record->correction,blacklist);
            if (Config::settings.RagebotCorrection && record->correction.confidence >= .2f && record->correction.variants[record->correction.selected].valid) {
                record->hitboxes = record->correction.variants[record->correction.selected].hitboxes;
                record->resolved = true;
            }
        }
        if (record->hitboxes.empty()) record->hitboxes = player->Hitboxes(time);
        if (!record->hitboxes.empty()) records.push_front(record);
        while (!records.empty() && SDK::globals->curTime-records.back()->simulationTime > maximumAge) records.pop_back();
        while (records.size() > limit) records.pop_back();
    }
    for (int index = maximum+1; index <= 64; ++index) if (!history[index].empty()) ResetPlayer(index);
}
