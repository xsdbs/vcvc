#pragma once
#include "extras.h"

namespace Removals
{
    void Update();
    void Restore();
    void View(Extras::ViewSetup& view);
    bool SkipPanel(const char* name);
}
