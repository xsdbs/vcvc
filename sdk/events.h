#pragma once
namespace Events
{
    // Legacy CS:GO adds AddListenerGlobal before CreateEvent.
    // Slot 8 is FireEvent(event, dontBroadcast); slot 9 takes only event.
    inline constexpr unsigned ClientSideSlot = 9;
    using FireClientSide = bool(__thiscall*)(void*,void*);
}