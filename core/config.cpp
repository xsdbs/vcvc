#include "config.h"
#include <fstream>
#include <Windows.h>
bool Config::Save(const std::filesystem::path& path)
{
    const std::filesystem::path temporary = path.wstring()+L".tmp";
    std::ofstream file(temporary);
    if (!file) return false;
    const Settings& s = settings;
    file << "havoc_config_v13\n"
         << s.menuOpened << ' ' << s.RagebotEnable << ' ' << s.RagebotAutoFire << ' '
         << s.RagebotHitchance << ' ' << s.RagebotMinDamage << ' '
         << s.AntiaimEnable << ' ' << s.AntiaimYawAdd << ' ' << s.AntiaimPitch << ' '
         << s.AntiaimFakeLagLimit << ' ' << s.VisEnableEnemy << ' ' << s.VisNameEnemy << ' '
         << s.VisBoxEnemy << ' ' << s.VisHealthEnemy << ' ' << s.MiscBunnyhop << ' ' << s.MiscAutoStrafe << ' ' << s.VisWeapTextEnemy << ' ' << s.VisAmmoEnemy << ' ' << s.MiscThirdperson << ' ' << s.MiscThirdpersonDistance << ' ' << s.MiscQuickstop << ' ' << s.RagebotAutoStop << ' ' << s.RagebotStopEarly << ' ' << s.RagebotStopBetweenShots << ' ' << s.RagebotSilent << ' ' << s.RagebotAutoScope << ' ' << s.RagebotHitboxes << ' ' << s.RagebotPriority << ' ' << s.RagebotMultipoint << ' ' << s.RagebotPointScale << ' ' << s.RagebotCorrection << ' ' << s.RagebotSafePoints << ' ' << s.RagebotPrediction << ' ' << s.RagebotBacktrackMs << '\n';
    file << s.RagebotBruteforce << ' ' << s.RagebotMatchRecords << ' ' << s.RagebotLethalBody << ' '
         << s.AntiaimAtTargets << ' ' << s.AntiaimJitterRange << ' ' << s.AntiaimSpinSpeed << ' '
         << s.VisFlagsEnemy << ' ' << s.VisDistanceEnemy << ' ' << s.MiscAirDuck << '\n';
    file << s.VisWorldHitmarker << ' ' << s.VisWorldDamage << '\n';
    file << s.VisWatermark << '\n';
    file << s.RemovePostProcessing << ' ' << s.RemoveScopeOverlay << ' ' << s.RemoveSecondZoom << ' '
         << s.RemoveScopeZoom << ' ' << s.Remove3dSky << ' ' << s.RemoveFog << ' '
         << s.RemoveShadows << ' ' << s.RemoveBloom << '\n';
    file << s.VisSkeletonEnemy << '\n';
    file << s.RagebotTargetsPerTick << '\n';
    file << s.DoubleTap << ' ' << s.HideShots << ' ' << s.ExploitIndicators << '\n';
    file << s.RagebotPerformance << ' ' << s.RagebotBudgetMs << ' ' << s.RagebotResolverPerTick << '\n';
    file << s.AntiaimDesync << ' ' << s.AntiaimAvoidOverlap << ' ' << s.AntiaimDesyncAmount << ' ' << s.AntiaimDesyncSide << '\n';
    file.flush();
    const bool written = static_cast<bool>(file);
    file.close();
    if (written && MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) return true;
    std::error_code error;
    std::filesystem::remove(temporary,error);
    return false;
}
bool Config::Load(const std::filesystem::path& path)
{
    std::ifstream file(path);
    std::string version;
    Settings s;
    if (!(file >> version)) return false;
    int revision = 0;
    for (int i = 1; i <= 13; ++i) if (version == "havoc_config_v"+std::to_string(i)) revision = i;
    if (revision == 0) return false;
    if (!(file >> s.menuOpened >> s.RagebotEnable >> s.RagebotAutoFire
        >> s.RagebotHitchance >> s.RagebotMinDamage
        >> s.AntiaimEnable >> s.AntiaimYawAdd >> s.AntiaimPitch
        >> s.AntiaimFakeLagLimit >> s.VisEnableEnemy >> s.VisNameEnemy
        >> s.VisBoxEnemy >> s.VisHealthEnemy >> s.MiscBunnyhop >> s.MiscAutoStrafe))
        return false;
    if (revision >= 2 && !(file >> s.VisWeapTextEnemy >> s.VisAmmoEnemy)) return false;
    if (revision >= 3 && !(file >> s.MiscThirdperson >> s.MiscThirdpersonDistance >> s.MiscQuickstop >> s.RagebotAutoStop >> s.RagebotStopEarly >> s.RagebotStopBetweenShots)) return false;
    if (revision >= 4 && !(file >> s.RagebotSilent >> s.RagebotAutoScope >> s.RagebotHitboxes >> s.RagebotPriority >> s.RagebotMultipoint >> s.RagebotPointScale >> s.RagebotCorrection >> s.RagebotSafePoints >> s.RagebotPrediction >> s.RagebotBacktrackMs)) return false;
    if (revision >= 5 && !(file >> s.RagebotBruteforce >> s.RagebotMatchRecords >> s.RagebotLethalBody
        >> s.AntiaimAtTargets >> s.AntiaimJitterRange >> s.AntiaimSpinSpeed
        >> s.VisFlagsEnemy >> s.VisDistanceEnemy >> s.MiscAirDuck)) return false;
    if (revision >= 6 && !(file >> s.VisWorldHitmarker >> s.VisWorldDamage)) return false;
    if (revision >= 7 && !(file >> s.VisWatermark)) return false;
    if (revision >= 8 && !(file >> s.RemovePostProcessing >> s.RemoveScopeOverlay >> s.RemoveSecondZoom
        >> s.RemoveScopeZoom >> s.Remove3dSky >> s.RemoveFog >> s.RemoveShadows >> s.RemoveBloom)) return false;
    if (revision >= 9 && !(file >> s.VisSkeletonEnemy)) return false;
    if (revision >= 10) { if (!(file >> s.RagebotTargetsPerTick)) return false; }
    else s.RagebotTargetsPerTick = 0;
    if (revision >= 11 && !(file >> s.DoubleTap >> s.HideShots >> s.ExploitIndicators)) return false;
    if (revision >= 12 && !(file >> s.RagebotPerformance >> s.RagebotBudgetMs >> s.RagebotResolverPerTick)) return false;
    if (revision >= 13 && !(file >> s.AntiaimDesync >> s.AntiaimAvoidOverlap >> s.AntiaimDesyncAmount >> s.AntiaimDesyncSide)) return false;
    if (s.AntiaimDesyncAmount < 0 || s.AntiaimDesyncAmount > 58 || s.AntiaimDesyncSide < 0 || s.AntiaimDesyncSide > 2) return false;
    if (s.RagebotBudgetMs < 1 || s.RagebotBudgetMs > 8 || s.RagebotResolverPerTick < 1 || s.RagebotResolverPerTick > 16) return false;
    if (s.RagebotTargetsPerTick < 0 || s.RagebotTargetsPerTick > 16) return false;
    if (s.AntiaimJitterRange < 0 || s.AntiaimJitterRange > 180 || s.AntiaimSpinSpeed < 0 || s.AntiaimSpinSpeed > 720) return false;
    if (s.RagebotBacktrackMs < 0 || s.RagebotBacktrackMs > 1000) return false;
    if (s.RagebotHitboxes < 0 || s.RagebotHitboxes > 2 || s.RagebotPriority < 0 || s.RagebotPriority > 3 || s.RagebotPointScale < 0 || s.RagebotPointScale > 90) return false;
    if (s.MiscThirdpersonDistance < 30 || s.MiscThirdpersonDistance > 200) return false;
    if (s.RagebotHitchance < 0 || s.RagebotHitchance > 100 || s.RagebotMinDamage < 0 || s.RagebotMinDamage > 130
        || s.AntiaimYawAdd < 0 || s.AntiaimYawAdd > 4 || s.AntiaimPitch < 0 || s.AntiaimPitch > 3
        || s.AntiaimFakeLagLimit < 0 || s.AntiaimFakeLagLimit > 14)
        return false;
    settings = s;
    return true;
}
