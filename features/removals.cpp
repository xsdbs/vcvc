#include "removals.h"
#include <array>
#include <cstring>
#include "../utils/logger.h"

namespace
{
    struct Override
    {
        const char* name;
        float replacement;
        void* variable = nullptr;
        float original = 0;
        bool owned = false,missingLogged = false;
        void Apply(bool enabled)
        {
            if (!enabled) {
                if (owned && variable) SDK::Method<void(__thiscall*)(void*,float)>(variable,15)(variable,original);
                owned = false; variable = nullptr;
                return;
            }
            if (!variable) variable = SDK::Method<void*(__thiscall*)(void*,const char*)>(SDK::cvar,15)(SDK::cvar,name);
            if (!variable) {
                if (!missingLogged) { Logger::Write("Removal unavailable: missing ConVar"); Logger::Write(name); missingLogged = true; }
                return;
            }
            if (!owned) {
                original = SDK::Cvar(name,NAN);
                if (!std::isfinite(original)) return;
                owned = true;
            }
            // Reapply only when the engine changes a value; avoid repeated callbacks.
            if (SDK::Cvar(name,NAN) != replacement)
                SDK::Method<void(__thiscall*)(void*,float)>(variable,15)(variable,replacement);
        }
    };
    std::array<Override,7> overrides{{
        {"mat_postprocess_enable",0},{"r_3dsky",0},{"fog_override",1},
        {"fog_enable",0},{"cl_csm_enabled",0},{"r_shadows",0},{"mat_disable_bloom",1}
    }};
}
void Removals::Restore()
{
    for (auto& setting : overrides) setting.Apply(false);
}
void Removals::Update()
{
    if (!SDK::cvar || !SDK::InGame()) { Restore(); return; }
    const auto& s = Config::settings;
    const bool enabled[] = {s.RemovePostProcessing,s.Remove3dSky,s.RemoveFog,s.RemoveFog,
        s.RemoveShadows,s.RemoveShadows,s.RemoveBloom};
    for (size_t i = 0; i < overrides.size(); ++i) overrides[i].Apply(enabled[i]);
}
bool Removals::SkipPanel(const char* name)
{
    return Config::settings.RemoveScopeOverlay && name && std::strcmp(name,"HudZoom") == 0;
}
void Removals::View(Extras::ViewSetup& view)
{
    if (!SDK::InGame()) return;
    SDK::Entity* local = SDK::Local();
    if (!local || local->IsDead() || !local->Field<bool>(SDK::offsets.scoped)) return;
    if (Config::settings.RemoveScopeZoom) { view.fov = 90; return; }
    if (!Config::settings.RemoveSecondZoom || SDK::zoomLevelOffset < 0) return;
    SDK::Entity* weapon = local->Weapon();
    if (!weapon) return;
    const SDK::WeaponData* data = weapon->Data();
    if (data && data->type == 5 && weapon->Field<int>(SDK::zoomLevelOffset) == 2) view.fov = 40;
}
