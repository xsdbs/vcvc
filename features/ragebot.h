#pragma once
#include "../sdk/game.h"
#include "lagcomp.h"
#include <memory>
#include <vector>
namespace Prediction { class Scope; }

namespace Ragebot
{
    inline bool HitboxAllowed(int group,int mode) { return mode == 1 ? group == 1 : mode == 2 ? group == 2 || group == 3 : group >= 1 && group <= 3; }
    const char* Status();
    std::vector<size_t> CandidateOrder(const std::vector<int>& groups);
    void UpdateRecords();
    void Clear();
    std::vector<int> SelectTargets(const std::vector<int>& eligible,int limit,int& nextTarget,bool rotateFull = false);
    std::vector<SDK::Vector> AimPoints(const SDK::Capsule& capsule,const SDK::Vector& eye);
    float TargetScore(float damage,int health,float fov,float distance,float age);
    bool CanShoot(SDK::Entity* local,SDK::Entity* weapon,float time);
    float RequiredDamage(int health);
    void ApplyAim(SDK::UserCmd& command,const SDK::Vector& angles,bool silent);
    bool Run(SDK::UserCmd& command,Prediction::Scope* prediction = nullptr);
}
