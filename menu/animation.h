#pragma once
#include <algorithm>
#include <cmath>

namespace Animation
{
    inline float Approach(float value,float target,float seconds,float speed = 14.f)
    {
        if (!std::isfinite(seconds) || seconds <= 0) return value;
        return value+(target-value)*(1-std::exp(-speed*std::min(seconds,.25f)));
    }
    struct Fade
    {
        float value = 1,from = 1,target = 1;
        unsigned long long began = 0;
        float Update(unsigned long long now)
        {
            const float t = std::clamp((now-began)/180.f,0.f,1.f);
            value = from+(target-from)*t*t*(3-2*t);
            return value;
        }
        void Set(float next,unsigned long long now)
        {
            Update(now);
            if (next == target) return;
            from = value; target = next; began = now;
        }
    };
}
