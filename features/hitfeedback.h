#pragma once
#include "../sdk/game.h"
#include <vector>

namespace HitFeedback
{
    struct Hit
    {
        int victim = 0;
        SDK::Entity* player = nullptr;
        SDK::Vector position{};
        int damage = 0;
        float from = 0,changed = 0,last = 0,born = 0;
        float DisplayDamage(float now) const;
    };
    void Clear();
    void Add(int victim,SDK::Entity* player,const SDK::Vector& position,int damage,float now);
    const std::vector<Hit>& Active(float now);
}
