#pragma once
#include "../sdk/game.h"
#include "resolver.h"
#include <memory>
#include <deque>
#include <array>
struct LagRecord_t
{
    SDK::Entity* player = nullptr;
    float simulationTime = 0;
    float duck = 0;
    int flags = 0;
    bool resolved = false;
    bool broken = false;
    int index = 0,chokedTicks = 1;
    unsigned generation = 0;
    float receivedTime = 0;
    SDK::Vector velocity{},eyeAngles{};
    std::array<Resolver::Layer,13> networkLayers{};
    bool layersKnown = false;
    Resolver::Result correction;
    SDK::Vector origin{};
    std::vector<SDK::Capsule> hitboxes;
};

namespace LagComp
{
    using Records = std::deque<std::shared_ptr<LagRecord_t>>;
    struct Timing { float now = 0,lerp = 0,correction = 0,maxUnlag = 0; };
    Timing CurrentTiming(float now);
    bool ValidTime(float simulationTime,const Timing& timing);
    bool Usable(const LagRecord_t& record,SDK::Entity* player,const Timing& timing,int windowMs);
    int ShotTick(float simulationTime,float lerp,float interval);
    const Records& History(int index);
    std::vector<std::shared_ptr<LagRecord_t>> ScanRecords(int index,SDK::Entity* player,const Timing& timing,int windowMs);
    void ResetPlayer(int index);
    void Clear();
    void Update();
    bool LayersChanged(const std::array<Resolver::Layer,13>& before,const std::array<Resolver::Layer,13>& after);
    bool IgnoreSimulationUpdate(const SDK::RecvProxyData* data);
}
