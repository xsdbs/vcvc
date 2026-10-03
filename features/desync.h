#pragma once
#include <algorithm>
#include <cmath>

namespace Desync
{
    struct Plan { bool active = false,send = true; float yaw = 0,base = 0,amount = 0; };
    struct Controller
    {
        bool cycling = false,hasSent = false;
        int lastCommand = -1,cycles = 0,side = 1;
        float base = 0,amount = 0,sentYaw = 0;
        Plan last{};
        void Reset() { *this = Controller{}; }
        Plan Apply(float requestedBase,int command,int choked,int limit,float requestedAmount,int sideMode,bool avoidOverlap)
        {
            if (!std::isfinite(requestedBase) || !std::isfinite(requestedAmount) || requestedAmount <= 0
                || command <= 0 || choked < 0 || limit < 1) { Reset(); return {}; }
            if (command < lastCommand) Reset();
            if (command == lastCommand) return last;
            if (!cycling || choked == 0) {
                base = std::remainder(requestedBase,360.f);
                amount = std::clamp(requestedAmount,0.f,58.f);
                side = sideMode == 0 ? -1 : sideMode == 1 ? 1 : (cycles&1) ? -1 : 1;
                const float separation = std::min(10.f,amount);
                if (avoidOverlap && hasSent
                    && std::fabs(std::remainder(base+side*2*amount-sentYaw,360.f)) < separation
                    && std::fabs(std::remainder(base-side*2*amount-sentYaw,360.f)) >= separation) side = -side;
                cycling = true;
            }
            const bool send = choked >= std::clamp(limit,1,14);
            last = {true,send,std::remainder(base+(send ? 0.f : side*2*amount),360.f),base,amount};
            lastCommand = command;
            if (send) { sentYaw = base; hasSent = true; cycling = false; ++cycles; }
            return last;
        }
    };
    inline Controller state;
    inline int packetOverride = -1;
    struct Context { int choked = 0,limit = 1; float maximum = 0; bool available = false; };
}
