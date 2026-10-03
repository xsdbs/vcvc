#include "hitfeedback.h"
#include <algorithm>
#include <cmath>

namespace
{
    std::vector<HitFeedback::Hit> hits;
}
float HitFeedback::Hit::DisplayDamage(float now) const
{
    const float fraction = std::clamp((now-changed)/.25f,0.f,1.f);
    // Smoothly count from the currently displayed value to the new total.
    return from+(damage-from)*(1-(1-fraction)*(1-fraction));
}
void HitFeedback::Clear()
{
    hits.clear();
}
const std::vector<HitFeedback::Hit>& HitFeedback::Active(float now)
{
    std::erase_if(hits,[&](const Hit& hit) { return now < hit.last || now-hit.last >= 1.5f; });
    return hits;
}
void HitFeedback::Add(int victim,SDK::Entity* player,const SDK::Vector& position,int damage,float now)
{
    if (victim < 1 || victim > 64 || !player || damage <= 0 || damage > 10000 || !std::isfinite(now)
        || !std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z)) return;
    Active(now);
    for (Hit& hit : hits) {
        if (hit.victim != victim || hit.player != player || now-hit.last > 1.f) continue;
        hit.from = hit.DisplayDamage(now);
        hit.damage = std::min(hit.damage+damage,1000000);
        hit.changed = hit.last = now;
        hit.position = position;
        return;
    }
    if (hits.size() >= 32) hits.erase(hits.begin());
    hits.push_back({victim,player,position,damage,0,now,now,now});
}
