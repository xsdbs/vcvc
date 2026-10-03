#pragma once
#include "../sdk/game.h"
#include "../sdk/trace.h"
#include "../core/config.h"
#include "desync.h"

namespace Extras
{
    inline void AirStrafe(SDK::UserCmd& command,const SDK::Vector& velocity)
    {
        const float speed = std::hypot(velocity.x,velocity.y);
        if (!std::isfinite(speed) || !std::isfinite(command.viewAngles.y) || (command.buttons&131072)) return;
        // Preserve the player's intended WASD direction rather than always steering along velocity.
        float forward = command.forwardMove,side = command.sideMove;
        if (std::fabs(forward)+std::fabs(side) < 1) {
            forward = static_cast<float>((!!(command.buttons&8))-(!!(command.buttons&16)));
            side = static_cast<float>((!!(command.buttons&1024))-(!!(command.buttons&512)));
        }
        float wishYaw = command.viewAngles.y;
        if (std::fabs(forward)+std::fabs(side) > 0) wishYaw += std::atan2(-side,forward)*180/SDK::Pi;
        float accelerationYaw = wishYaw;
        if (speed >= 5) {
            const float velocityYaw = std::atan2(velocity.y,velocity.x)*180/SDK::Pi;
            const float interval = SDK::globals ? SDK::globals->interval : 1.f/64;
            const float acceleration = std::max(0.f,SDK::Cvar("sv_airaccelerate",12))*450*std::clamp(interval,.001f,.1f);
            const float ideal = std::acos(std::clamp((30.f-acceleration)/speed,0.f,1.f))*180/SDK::Pi;
            const float difference = std::remainder(wishYaw-velocityYaw,360.f);
            const float turn = std::fabs(difference) > 1 ? (difference > 0 ? 1.f : -1.f)
                : command.mouseX != 0 ? (command.mouseX > 0 ? -1.f : 1.f) : (command.commandNumber&1 ? 1.f : -1.f);
            accelerationYaw = velocityYaw+turn*ideal;
        }
        command.forwardMove = 450; command.sideMove = 0;
        SDK::FixMovement(accelerationYaw,command.viewAngles.y,command.forwardMove,command.sideMove);
    }
    inline void Stop(SDK::UserCmd& command,const SDK::Vector& velocity)
    {
        const float speed = std::hypot(velocity.x,velocity.y);
        if (speed < 2) { command.forwardMove = command.sideMove = 0; return; }
        const float angle = command.viewAngles.y*SDK::Pi/180;
        const float scale = std::min(speed,450.f)/speed;
        command.forwardMove = -(std::cos(angle)*velocity.x+std::sin(angle)*velocity.y)*scale;
        command.sideMove = (-std::sin(angle)*velocity.x+std::cos(angle)*velocity.y)*scale;
    }
    inline void Movement(SDK::UserCmd& command,int flags,int moveType,const SDK::Vector& velocity)
    {
        if (moveType == 8 || moveType == 9 || moveType == 10) return;
        if (Config::settings.MiscQuickstop && (flags&1) && !(command.buttons&(8|16|512|1024|2))
            && std::fabs(command.forwardMove) < 1 && std::fabs(command.sideMove) < 1) Stop(command,velocity);
        if (Config::settings.MiscBunnyhop && !(flags&1)) command.buttons &= ~2;
        if (Config::settings.MiscAirDuck && !(flags&1)) command.buttons |= 4;
        if (!Config::settings.MiscAutoStrafe || (flags&1)) return;
        AirStrafe(command,velocity);
    }
    inline bool AntiAim(SDK::UserCmd& command,bool canShoot = true,const Desync::Context* context = nullptr)
    {
        if (!Config::settings.AntiaimEnable || !SDK::globals || !std::isfinite(command.viewAngles.y)
            || (command.buttons&(32|2048)) || (canShoot && (command.buttons&1))) return false;
        const float previousYaw = command.viewAngles.y;
        const int yawStep = context && context->available && context->maximum > 0 && context->limit > 0
            && Config::settings.AntiaimDesync && Config::settings.AntiaimDesyncAmount > 0
            ? Desync::state.cycles : command.commandNumber;
        const double time = static_cast<double>(command.commandNumber)*SDK::globals->interval;
        if (Config::settings.AntiaimAtTargets && SDK::InGame()) {
            SDK::Entity* local = SDK::Local();
            float bestDistance = INFINITY;
            if (local) for (int index = 1; index <= std::min(SDK::MaxClients(),64); ++index) {
                SDK::Entity* target = SDK::Player(index);
                if (!target || target == local || !target->IsPlayer() || target->IsDead() || target->IsDormant()
                    || target->m_iTeamNum() == local->m_iTeamNum() || target->m_bGunGameImmunity()) continue;
                const float distance = (target->m_vecOrigin()-local->m_vecOrigin()).LengthSquared();
                if (distance < bestDistance) {
                    bestDistance = distance;
                    command.viewAngles.y = SDK::AngleTo(local->Eye(),target->Eye()).y;
                }
            }
        }
        switch (Config::settings.AntiaimYawAdd) {
        case 0: command.viewAngles.y += 180; break;
        case 1: command.viewAngles.y += 180+((yawStep&1) ? 1.f : -1.f)*Config::settings.AntiaimJitterRange; break;
        case 2: command.viewAngles.y += 180+static_cast<float>(std::sin(time*3))*Config::settings.AntiaimJitterRange; break;
        case 3: command.viewAngles.y += static_cast<float>(std::remainder(time*Config::settings.AntiaimSpinSpeed,360.0)); break;
        case 4: command.viewAngles.y = static_cast<float>((static_cast<unsigned>(command.commandNumber)*1664525u+1013904223u)%360); break;
        }
        if (Config::settings.AntiaimPitch == 1) command.viewAngles.x = 89;
        else if (Config::settings.AntiaimPitch == 2) command.viewAngles.x = -89;
        else if (Config::settings.AntiaimPitch == 3) command.viewAngles.x = 0;
        if (context && context->available && Config::settings.AntiaimDesync && context->maximum > 0) {
            const auto plan = Desync::state.Apply(command.viewAngles.y,command.commandNumber,context->choked,context->limit,
                std::min(static_cast<float>(Config::settings.AntiaimDesyncAmount),context->maximum),
                Config::settings.AntiaimDesyncSide,Config::settings.AntiaimAvoidOverlap);
            if (plan.active) { command.viewAngles.y = plan.yaw; Desync::packetOverride = plan.send ? 1 : 0; }
        } else if (context) Desync::state.Reset();
        command.viewAngles = SDK::NormalizeAngles(command.viewAngles);
        SDK::FixMovement(previousYaw,command.viewAngles.y,command.forwardMove,command.sideMove);
        return true;
    }
    inline SDK::Vector commandAngles{};
    inline bool anglesValid = false;
    inline void SyncModelAngles()
    {
        if (!anglesValid || !Config::settings.MiscThirdperson || !SDK::prediction || !SDK::InGame()) return;
        SDK::Entity* local = SDK::Local();
        if (!local || local->IsDead()) { anglesValid = false; return; }
        SDK::Vector angles = commandAngles;
        SDK::Method<void(__thiscall*)(void*,SDK::Vector&)>(SDK::prediction,13)(SDK::prediction,angles);
    }
    struct ViewSetup
    {
        std::byte padding[0xb0]; float fov,viewmodelFov; SDK::Vector origin,angles;
    };
    static_assert(offsetof(ViewSetup,origin) == 0xb8);
    inline void* cameraInput = nullptr;
    inline bool cameraOwned = false;
    inline bool previousThirdperson = false;
    inline SDK::Vector previousOffset{};
    inline void ResetCamera()
    {
        if (!cameraOwned || !cameraInput) return;
        *(bool*)((BYTE*)cameraInput+0xa9) = previousThirdperson;
        *(SDK::Vector*)((BYTE*)cameraInput+0xac) = previousOffset;
        cameraOwned = false;
    }
    inline bool PrepareCamera()
    {
        SDK::Entity* local = SDK::InGame() ? SDK::Local() : nullptr;
        if (!cameraInput || !SDK::engineTrace || !Config::settings.MiscThirdperson || !local || local->IsDead()) {
            ResetCamera(); return false;
        }
        if (!cameraOwned) {
            previousThirdperson = *(bool*)((BYTE*)cameraInput+0xa9);
            previousOffset = *(SDK::Vector*)((BYTE*)cameraInput+0xac);
            cameraOwned = true;
        }
        *(bool*)((BYTE*)cameraInput+0xa9) = true;
        // OverrideView positions the camera after the engine's view calculation.
        SDK::Vector visible{};
        SDK::Method<void(__thiscall*)(void*,SDK::Vector&)>(SDK::engine,18)(SDK::engine,visible);
        *(SDK::Vector*)((BYTE*)cameraInput+0xac) = {visible.x,visible.y,0};
        return true;
    }
    inline void Camera(ViewSetup& view)
    {
        SDK::Entity* local = SDK::Local();
        if (!local || local->IsDead() || !cameraOwned || !SDK::engineTrace) return;
        SDK::Vector visible{};
        SDK::Method<void(__thiscall*)(void*,SDK::Vector&)>(SDK::engine,18)(SDK::engine,visible);
        view.angles = SDK::NormalizeAngles(visible);
        SDK::Vector forward,right,up;
        SDK::AngleVectors(view.angles,forward,right,up);
        const SDK::Vector eye = view.origin;
        const float distance = static_cast<float>(std::clamp(Config::settings.MiscThirdpersonDistance,30,200));
        const SDK::Vector desired = eye-forward*distance;
        SDK::TraceFilter filter(local);
        SDK::Ray ray(eye,desired);
        ray.extents = {12,12,12,0}; ray.isRay = false;
        SDK::Trace trace{};
        SDK::Method<void(__thiscall*)(void*,const SDK::Ray&,unsigned,SDK::TraceFilter*,SDK::Trace*)>(SDK::engineTrace,5)(
            SDK::engineTrace,ray,0x200400b,&filter,&trace);
        const float fraction = trace.startSolid || trace.allSolid ? 0.f : std::clamp(trace.fraction-.02f,0.f,1.f);
        view.origin = eye+(desired-eye)*fraction;
        *(SDK::Vector*)((BYTE*)cameraInput+0xac) = {view.angles.x,view.angles.y,distance*fraction};
    }
}
