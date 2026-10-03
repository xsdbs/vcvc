#pragma once
#include <filesystem>
namespace Config
{
    struct Settings
    {
        bool menuOpened = true;
        bool RagebotEnable = false;
        bool RagebotAutoFire = false;
        bool DoubleTap = false;
        bool HideShots = false;
        bool ExploitIndicators = true;
        bool RagebotSilent = false;
        bool RagebotAutoScope = false;
        int RagebotHitboxes = 0;
        int RagebotPriority = 0;
        int RagebotTargetsPerTick = 2;
        bool RagebotPerformance = true;
        int RagebotBudgetMs = 2;
        int RagebotResolverPerTick = 2;
        bool RagebotMultipoint = false;
        bool RagebotCorrection = false;
        bool RagebotSafePoints = false;
        bool RagebotPrediction = false;
        bool RagebotBruteforce = false;
        bool RagebotMatchRecords = false;
        bool RagebotLethalBody = false;
        int RagebotBacktrackMs = 200;
        int RagebotPointScale = 60;
        bool RagebotAutoStop = false;
        bool RagebotStopEarly = false;
        bool RagebotStopBetweenShots = false;
        int RagebotHitchance = 50;
        int RagebotMinDamage = 30;
        bool AntiaimEnable = false;
        bool AntiaimDesync = true;
        bool AntiaimAvoidOverlap = true;
        int AntiaimDesyncAmount = 58;
        int AntiaimDesyncSide = 2;
        int AntiaimYawAdd = 0;
        int AntiaimPitch = 0;
        int AntiaimFakeLagLimit = 0;
        bool AntiaimAtTargets = false;
        int AntiaimJitterRange = 45;
        int AntiaimSpinSpeed = 180;
        bool VisEnableEnemy = false;
        bool VisNameEnemy = true;
        bool VisBoxEnemy = true;
        bool VisHealthEnemy = true;
        bool VisSkeletonEnemy = false;
        bool VisWeapTextEnemy = true;
        bool VisAmmoEnemy = true;
        bool VisFlagsEnemy = false;
        bool VisDistanceEnemy = false;
        bool VisWorldHitmarker = false;
        bool VisWorldDamage = false;
        bool VisWatermark = true;
        bool RemovePostProcessing = false;
        bool RemoveScopeOverlay = false;
        bool RemoveSecondZoom = false;
        bool RemoveScopeZoom = false;
        bool Remove3dSky = false;
        bool RemoveFog = false;
        bool RemoveShadows = false;
        bool RemoveBloom = false;
        bool MiscQuickstop = false;
        bool MiscBunnyhop = false;
        bool MiscAutoStrafe = false;
        bool MiscAirDuck = false;
        bool MiscThirdperson = false;
        int MiscThirdpersonDistance = 120;
    };
    inline Settings settings;
    bool Save(const std::filesystem::path& path);
    bool Load(const std::filesystem::path& path);
}
