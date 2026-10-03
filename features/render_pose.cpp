#include "render_pose.h"
#include "resolver.h"
#include <mutex>
#include <cstring>

namespace
{
    struct Entry { SDK::Entity* player = nullptr; RenderPose::Pose pose; };
    std::array<Entry,65> entries;
    std::mutex mutex;
    bool Finite(const SDK::Vector& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
}
bool RenderPose::Pose::Store(const void* identity,float simulation,const SDK::Vector& source,
    const SDK::Matrix3x4* bones,int boneCount)
{
    count = 0;
    if (!identity || !bones || boneCount < 1 || boneCount > 256 || !std::isfinite(simulation) || !Finite(source)) return false;
    for (int i = 0; i < boneCount; ++i)
        for (const auto& row : bones[i].values)
            for (float value : row) if (!std::isfinite(value)) return false;
    std::memcpy(matrices.data(),bones,boneCount*sizeof(*bones));
    origin = source; model = identity; simulationTime = simulation; count = boneCount;
    return true;
}
bool RenderPose::Pose::Translate(const SDK::Vector& destination,SDK::Matrix3x4* output,int capacity) const
{
    if (!output || count < 1 || capacity < count || !Finite(destination)) return false;
    const SDK::Vector delta = destination-origin;
    // Translate from the pristine pose on every call, never the last translated cache.
    // A teleport invalidates the pose instead of stretching a previous life across the map.
    if (!Finite(delta) || delta.LengthSquared() > 4096) return false;
    for (int i = 0; i < count; ++i) {
        output[i] = matrices[i];
        output[i].values[0][3] += delta.x;
        output[i].values[1][3] += delta.y;
        output[i].values[2][3] += delta.z;
    }
    return true;
}
void RenderPose::Clear()
{
    rendering = false;
    std::lock_guard<std::mutex> lock(mutex);
    for (auto& entry : entries) { entry.player = nullptr; entry.pose.count = 0; }
}
void RenderPose::Refresh()
{
    if (!SDK::InGame() || !SDK::globals) { Clear(); return; }
    auto* local = SDK::Local();
    if (!local) { Clear(); return; }
    const int maximum = std::min(SDK::MaxClients(),64);
    // No engine calls while holding the cache lock: threaded bone jobs can reenter SetupBones.
    for (int i = 1; i <= 64; ++i) {
        auto* player = i <= maximum ? SDK::Player(i) : nullptr;
        if (player && (player == local || !player->IsPlayer() || player->IsDead() || player->IsDormant()
            || player->m_iTeamNum() == local->m_iTeamNum())) player = nullptr;
        std::lock_guard<std::mutex> lock(mutex);
        if (entries[i].player != player || !player) {
            entries[i].player = player; entries[i].pose.count = 0;
        }
    }
}
bool RenderPose::Setup(void* renderable,SDK::Matrix3x4* output,int capacity,int mask,float time,SetupBones original)
{
    if (bypassDepth || (!rendering.load() && !visualReadDepth) || !SDK::globals || !attachments || !Resolver::invalidateBones)
        return original(renderable,output,capacity,mask,time);
    auto* player = reinterpret_cast<SDK::Entity*>(static_cast<BYTE*>(renderable)-4);
    int index = 0;
    Pose pose;
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (int i = 1; i <= 64; ++i) if (entries[i].player == player) { index = i; pose = entries[i].pose; break; }
    }
    if (!index) return original(renderable,output,capacity,mask,time);
    // Engine requests for historical times must not be answered using a visual pose.
    if (time >= 0 && std::fabs(time-SDK::globals->curTime) > SDK::globals->interval*.5f)
        return original(renderable,output,capacity,mask,time);
    const void* model = SDK::Method<const void*(__thiscall*)(void*)>(renderable,8)(renderable);
    const auto* header = model && SDK::modelInfo
        ? SDK::Method<const SDK::StudioHeader*(__thiscall*)(void*,const void*)>(SDK::modelInfo,32)(SDK::modelInfo,model) : nullptr;
    if (!header || header->bones < 1 || header->bones > 256)
        return original(renderable,output,capacity,mask,time);
    if (output && capacity < header->bones) return false;
    const float simulation = player->m_flSimulationTime();
    const SDK::Vector origin = player->RenderOrigin();
    alignas(16) std::array<SDK::Matrix3x4,256> translated{};
    constexpr int allBones = 0xfff00;
    if (mask != -1 && (mask&~allBones)) return original(renderable,output,capacity,mask,time);
    if (pose.count != header->bones || pose.model != model || pose.simulationTime != simulation
        || !pose.Translate(origin,translated.data(),256)) {
        if (!rendering.load()) return original(renderable,output,capacity,mask,time);
        Bypass bypass;
        Resolver::invalidateBones(player);
        // Capture the normal engine visual pose at render time. Resolver hypotheses
        // never enter this cache. Build all LODs once, then reuse between packets.
        if (!original(renderable,translated.data(),256,allBones,time)) return false;
        if (!pose.Store(model,simulation,origin,translated.data(),header->bones)) return false;
        std::lock_guard<std::mutex> lock(mutex);
        if (entries[index].player == player) entries[index].pose = pose;
    }
    // Both the renderer's internal accessor and returned skeleton matrices must
    // see the same translated pose. Never change engine-owned allocation pointers.
    if (!SDK::PublishRenderBones(player,translated.data(),pose.count,allBones))
        return original(renderable,output,capacity,mask,time);
    if (output) std::memcpy(output,translated.data(),pose.count*sizeof(SDK::Matrix3x4));
    if (mask == -1 || (mask&0x200)) {
        // Verified in this client: renderable + 0x294c == entity + 0x2950.
        if (void* studio = player->Field<void*>(0x2950)) {
            Bypass bypass;
            attachments(player,studio);
        }
    }
    return true;
}
