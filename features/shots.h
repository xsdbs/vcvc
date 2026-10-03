#pragma once
#include "lagcomp.h"

namespace Shots
{
    struct Statistics { int fired = 0,hits = 0,resolverMisses = 0,spreadMisses = 0,blocked = 0; };
    inline Statistics statistics;
    void Clear();
    void ClearPlayer(int index);
    void Add(int index,const std::shared_ptr<LagRecord_t>& record,const SDK::Capsule& capsule,const SDK::Vector& eye,int commandNumber = 0);
    void Event(void* event);
    void Process();
    unsigned Blacklist(int index,SDK::Entity* player);
}
