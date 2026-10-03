#include "hooks.h"
#include "../menu/menu.h"
#include "../utils/logger.h"
#include "config.h"
#include "movement_abi.h"
#include "../features/exploits.h"
#include "../features/performance.h"
#include "../features/render_pose.h"
#include "../sdk/game.h"
#include "../sdk/events.h"
#include "../features/ragebot.h"
#include "../features/esp.h"
#include "../features/extras.h"
#include "../features/prediction.h"
#include "../features/shots.h"
#include "../features/removals.h"
#include "../menu/surface.h"
#include <cstdio>
#include <cstdlib>
#include <MinHook.h>
#include <atomic>
#include <cstring>
#include <mutex>

namespace
{
    using Present = HRESULT(WINAPI*)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
    using Reset = HRESULT(WINAPI*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
    Present originalPresent = nullptr;
    Reset originalReset = nullptr;
    void* presentTarget = nullptr;
    void* resetTarget = nullptr;
    void* paintTarget = nullptr;
    void* cursorTarget = nullptr;
    void* createMoveTarget = nullptr;
    void* movementTarget = nullptr;
    void* clientMoveTarget = nullptr;
    BYTE** movementClientState = nullptr;
    using ClientMove = void(__thiscall*)(void*,int,float,bool);
    ClientMove originalClientMove = nullptr;
    SDK::Entity* exploitWeapon = nullptr;
    void* frameStageTarget = nullptr;
    void* bonesTarget = nullptr;
    RenderPose::SetupBones originalBones = nullptr;
    void* viewTarget = nullptr;
    void* eventTarget = nullptr;
    void* panelTarget = nullptr;
    void* panelInterface = nullptr;
    using PaintTraverse = void(__thiscall*)(void*,unsigned,bool,bool);
    PaintTraverse originalTraverse = nullptr;
    void* simulationTarget = nullptr;
    SDK::RecvProxy originalSimulation = nullptr;
    void* eventManager = nullptr;
    using FireEvent = Events::FireClientSide;
    FireEvent originalEvent = nullptr;
    using OverrideView = void(__thiscall*)(void*,Extras::ViewSetup*);
    OverrideView originalView = nullptr;
    using CreateMove = bool(__thiscall*)(void*,float,SDK::UserCmd*);
    using FrameStage = void(__thiscall*)(void*,int);
    CreateMove originalCreateMove = nullptr;
    FrameStage originalFrameStage = nullptr;
    HWND gameWindow = nullptr;
    WNDPROC originalWindowProc = nullptr;
    IDirect3DDevice9* hookedDevice = nullptr;
    bool minhookReady = false;
    bool cursorReleased = false;
    RECT originalClip{};
    std::recursive_mutex menuMutex;
    std::atomic<unsigned> callbacks{0};

    struct Callback
    {
        Callback() { ++callbacks; }
        ~Callback() { --callbacks; }
    };
    void __fastcall HookTraverse(void* object,void*,unsigned panel,bool forceRepaint,bool allowForce)
    {
        Callback callback;
        {
            std::lock_guard<std::recursive_mutex> guard(menuMutex);
            const char* name = SDK::Method<const char*(__thiscall*)(void*,unsigned)>(object,36)(object,panel);
            if (Removals::SkipPanel(name)) return;
        }
        originalTraverse(object,panel,forceRepaint,allowForce);
    }
    void __cdecl HookSimulation(const SDK::RecvProxyData* data,void* entity,void* output)
    {
        Callback callback;
        if (!data || data->property != SDK::simulationProperty || !LagComp::IgnoreSimulationUpdate(data))
            originalSimulation(data,entity,output);
    }

    using Paint = void(__thiscall*)(void*,int);
    using LockCursor = void(__thiscall*)(void*);
    Paint originalPaint = nullptr;
    LockCursor originalLockCursor = nullptr;
    using Drawing = void(__thiscall*)(void*);
    Drawing startDrawing = nullptr;
    Drawing finishDrawing = nullptr;
    void* engineVGui = nullptr;
    void* gameSurface = nullptr;
    void* inputSystem = nullptr;
    bool gameBackend = false;
    bool inputDisabled = false;


    float LocalDesyncLimit(SDK::Entity* local)
    {
        if (!local || SDK::offsets.scoped < 0x14) return 0;
        auto* state = local->Field<BYTE*>(SDK::offsets.scoped-0x14);
        MEMORY_BASIC_INFORMATION memory{};
        if (!state || !VirtualQuery(state,&memory,sizeof(memory)) || memory.State != MEM_COMMIT
            || (memory.Protect&(PAGE_GUARD|PAGE_NOACCESS))
            || reinterpret_cast<std::uintptr_t>(state)+Resolver::StateSize
                > reinterpret_cast<std::uintptr_t>(memory.BaseAddress)+memory.RegionSize
            || *reinterpret_cast<SDK::Entity**>(state+0x60) != local) return 0;
        auto field = [&](int offset) { return *reinterpret_cast<float*>(state+offset); };
        const float minimum = field(Resolver::MinimumYawOffset),maximum = field(Resolver::MaximumYawOffset);
        const float walk = field(0xf8),crouch = field(0xfc),duck = field(0xa4),transition = field(0x11c);
        if (!std::isfinite(minimum) || !std::isfinite(maximum) || minimum > 0 || maximum < 0
            || minimum < -180 || maximum > 180 || !std::isfinite(walk) || !std::isfinite(crouch)
            || !std::isfinite(duck) || !std::isfinite(transition)) return 0;
        float width = 1+(-.3f*std::clamp(transition,0.f,1.f)-.2f)*std::clamp(walk,0.f,1.f);
        width += std::clamp(duck,0.f,1.f)*std::clamp(crouch,0.f,1.f)*(.5f-width);
        return std::clamp(std::min(-minimum,maximum)*width,0.f,58.f);
    }
    void ExploitCommand(SDK::UserCmd& command,SDK::Entity* local)
    {
        if (!Exploits::available || Exploits::state.paused) return;
        auto& state = Exploits::state;
        SDK::Entity* weapon = local->Weapon();
        const float now = Prediction::commandTime >= 0 ? Prediction::commandTime : local->m_nTickBase()*SDK::globals->interval;
        if (!state.AllowAttack(now)) command.buttons &= ~1;
        const bool firing = (command.buttons&1) && Ragebot::CanShoot(local,weapon,now);
        if (state.shifting) { if (firing) ++state.shots; }
        else if (movementClientState && *movementClientState
            && state.Request(firing,true,*reinterpret_cast<int*>(*movementClientState+0x4d30),GetTickCount64()/1000.)) {
            const auto* data = weapon->Data();
            // The weapon may not have simulated the buffered first shot yet.
            // Its old next-primary time must not authorize another shot one tick later.
            const float cycle = data ? data->cycleTime : NAN;
            state.secondShotAt = std::isfinite(cycle) && cycle >= .01f && cycle <= 4.f ? now+cycle : INFINITY;
        }
    }
    void __stdcall DispatchClientMove(void* object,int sequence,float sample,bool active,bool* sendPacket)
    {
        Callback callback;
        std::lock_guard<std::recursive_mutex> guard(menuMutex);
        Desync::packetOverride = Config::settings.AntiaimEnable ? 1 : -1;
        originalClientMove(object,sequence,sample,active);
        const bool planned = Desync::packetOverride < 0 ? *sendPacket : Desync::packetOverride != 0;
        if (Exploits::available) *sendPacket = Exploits::state.SendPacket(planned);
        Desync::packetOverride = -1;
    }
    void __stdcall DispatchMovement(float sample,bool finalTick)
    {
        Callback callback;
        std::lock_guard<std::recursive_mutex> guard(menuMutex);
        SDK::Entity* local = SDK::InGame() ? SDK::Local() : nullptr;
        SDK::Entity* weapon = local && !local->IsDead() ? local->Weapon() : nullptr;
        const auto* data = weapon ? weapon->Data() : nullptr;
        const bool connected = movementClientState && *movementClientState
            && *reinterpret_cast<int*>(*movementClientState+0x108) == 6
            && !*reinterpret_cast<bool*>(*movementClientState+0x4d48);
        const bool valid = connected && local && !local->IsDead() && data && data->type >= 1 && data->type <= 6
            && weapon->Field<short>(SDK::offsets.itemDefinition) != 64 && !(local->Field<int>(SDK::offsets.flags)&32);
        if (!valid || weapon != exploitWeapon) { Exploits::state.Reset(); Desync::state.Reset(); exploitWeapon = weapon; }
        const auto selected = !valid ? Exploits::Mode::Off : Config::settings.DoubleTap ? Exploits::Mode::DoubleTap
            : Config::settings.HideShots ? Exploits::Mode::HideShots : Exploits::Mode::Off;
        const float serverLimit = SDK::Cvar("sv_maxusrcmdprocessticks",16);
        const int limit = std::isfinite(serverLimit) ? static_cast<int>(std::clamp(serverLimit,1.f,16.f))-2 : 0;
        Exploits::state.Configure(selected,limit);
        Exploits::state.paused = !valid || Config::settings.menuOpened;
        const int choked = connected ? *reinterpret_cast<int*>(*movementClientState+0x4d30) : 0;
        if (choked == 0 && Exploits::state.Recharge(GetTickCount64()/1000.,finalTick)) return;
        MovementABI::Invoke(sample,finalTick);
        Exploits::state.Start();
        if (Exploits::state.shifting) {
            Performance::Measure measured(Performance::Burst);
            while (Exploits::state.shifting) {
                MovementABI::Invoke(0.f,Exploits::state.remaining == 1);
                Exploits::state.Advance();
            }
        }
    }
    bool __fastcall HookCreateMove(void* object,void*,float sample,SDK::UserCmd* command)
    {
        Callback callback;
        const bool result = originalCreateMove(object,sample,command);
        if (!command || command->commandNumber == 0) return result;
        std::lock_guard<std::recursive_mutex> guard(menuMutex);
        static bool logged = false;
        if (!logged) { Logger::Write("Gameplay CreateMove callback reached"); logged = true; }
        SDK::Entity* local = SDK::InGame() ? SDK::Local() : nullptr;
        if (!local || local->IsDead() || Config::settings.menuOpened) { Extras::anglesValid = false; Desync::state.Reset(); Prediction::Reset(); return result; }
        Shots::Process();
        const int moveType = local->Field<unsigned char>(SDK::offsets.moveType);
        if (result) {
            SDK::Vector cameraAngles = command->viewAngles;
            SDK::Method<void(__thiscall*)(void*,SDK::Vector&)>(SDK::engine,19)(SDK::engine,cameraAngles);
        }
        Extras::Movement(*command,local->Field<int>(SDK::offsets.flags),moveType,local->m_vecVelocity());
        Prediction::Scope prediction(local,*command);
        struct CommandClockRestore {
            float saved = Prediction::commandTime;
            ~CommandClockRestore() { Prediction::commandTime = saved; }
        } clockRestore;
        // Command timing is required for shifted weapon cooldowns even when
        // movement prediction is switched off or its datamap is unavailable.
        if (Exploits::available && Exploits::state.mode != Exploits::Mode::Off && Prediction::commandTime < 0)
            Prediction::commandTime = Prediction::CommandTick(local,command->commandNumber,local->m_nTickBase())*SDK::globals->interval;
        const bool allowAttack = Exploits::state.AllowAttack(Prediction::commandTime);
        if (!allowAttack) command->buttons &= ~1;
        if (allowAttack && Ragebot::Run(*command,&prediction)) {
            Desync::state.Reset();
            ExploitCommand(*command,local);
            Extras::commandAngles = command->viewAngles;
            Extras::anglesValid = true;
            return false;
        }
        SDK::Entity* weapon = local->Weapon();
        const SDK::WeaponData* data = weapon ? weapon->Data() : nullptr;
        const float now = Prediction::commandTime >= 0 ? Prediction::commandTime : local->m_nTickBase()*SDK::globals->interval;
        const bool canShoot = Ragebot::CanShoot(local,weapon,now);
        Desync::Context desync;
        desync.available = Exploits::available && movementClientState && *movementClientState
            && !Exploits::state.shifting && !Exploits::state.pending;
        if (desync.available && Config::settings.AntiaimEnable && Config::settings.AntiaimDesync) {
            desync.choked = *reinterpret_cast<int*>(*movementClientState+0x4d30);
            const float serverLimit = SDK::Cvar("sv_maxusrcmdprocessticks",16);
            const int maximum = std::isfinite(serverLimit) ? std::clamp(static_cast<int>(std::clamp(serverLimit,1.f,16.f))-2,0,14) : 0;
            desync.limit = std::min(std::max(1,Config::settings.AntiaimFakeLagLimit),maximum);
            if (Exploits::state.mode != Exploits::Mode::Off) desync.limit = std::min(desync.limit,1);
            desync.maximum = LocalDesyncLimit(local);
        }
        const bool antiAim = data && data->type >= 1 && data->type <= 6 && moveType != 8 && moveType != 9 && moveType != 10
            && !(local->Field<int>(SDK::offsets.flags)&32) && Extras::AntiAim(*command,canShoot,&desync);
        if (!antiAim) Desync::state.Reset();
        ExploitCommand(*command,local);
        Extras::commandAngles = command->viewAngles;
        if (antiAim && Desync::state.last.active) Extras::commandAngles.y = Desync::state.last.base;
        Extras::anglesValid = true;
        if (antiAim) {
            static bool antiAimLogged = false;
            if (!antiAimLogged) { Logger::Write("Anti-aim applied to command; prediction model angles synchronized"); antiAimLogged = true; }
            return false;
        }
        return result;
    }
    bool __fastcall HookBones(void* renderable,void*,SDK::Matrix3x4* output,int capacity,int mask,float time)
    {
        Callback callback;
        return RenderPose::Setup(renderable,output,capacity,mask,time,originalBones);
    }
    void InstallRenderBones()
    {
        if (bonesTarget || !minhookReady || !RenderPose::attachments || !Resolver::invalidateBones) return;
        auto* player = SDK::Local();
        if (!player) return;
        void* target = SDK::Method<void*>(reinterpret_cast<BYTE*>(player)+4,13);
        const auto created = MH_CreateHook(target,reinterpret_cast<void*>(&HookBones),reinterpret_cast<void**>(&originalBones));
        if (created != MH_OK) return;
        if (MH_EnableHook(target) != MH_OK) { MH_RemoveHook(target); originalBones = nullptr; return; }
        bonesTarget = target;
        Logger::Write("Enemy render poses ready: one visual pose per simulation update, interpolated origin, isolated historical bones");
    }
    void __fastcall HookFrameStage(void* object,void*,int stage)
    {
        Callback callback;
        if (stage == 4 || stage == 5) {
            InstallRenderBones();
            RenderPose::Refresh();
        }
        if (stage == 5) RenderPose::rendering = true;
        if (stage == 6) RenderPose::rendering = false;
        if (stage == 5) {
            std::lock_guard<std::recursive_mutex> guard(menuMutex);
            Removals::Update();
            Extras::SyncModelAngles();
        }
        originalFrameStage(object,stage);
        if (stage != 4) return;
        std::lock_guard<std::recursive_mutex> guard(menuMutex);
        Ragebot::UpdateRecords();
        static ULONGLONG lastPerformanceLog = 0;
        const auto now = GetTickCount64();
        if (Config::settings.RagebotEnable && now-lastPerformanceLog >= 5000) {
            char message[256]; const auto& samples = Performance::samples;
            std::snprintf(message,sizeof(message),"Performance ms (average/peak): scan %.2f/%.2f records %.2f/%.2f prediction %.2f/%.2f burst %.2f/%.2f",
                samples[0].mean,samples[0].peak,samples[1].mean,samples[1].peak,
                samples[2].mean,samples[2].peak,samples[3].mean,samples[3].peak);
            Logger::Write(message); lastPerformanceLog = now;
            for (auto& sample : Performance::samples) sample.peak = 0;
        }
    }
    void __fastcall HookView(void* object,void*,Extras::ViewSetup* view)
    {
        Callback callback;
        std::lock_guard<std::recursive_mutex> guard(menuMutex);
        const bool thirdperson = Extras::PrepareCamera();
        originalView(object,view);
        if (view) Removals::View(*view);
        if (thirdperson && view) Extras::Camera(*view);
    }
    bool __fastcall HookEvent(void* object,void*,void* event)
    {
        Callback callback;
        {
            std::lock_guard<std::recursive_mutex> guard(menuMutex);
            Shots::Event(event);
        }
        return originalEvent(object,event);
    }
    void UpdateCursor()
    {
        if (inputSystem && inputDisabled != Config::settings.menuOpened) {
            inputDisabled = Config::settings.menuOpened;
            Surface::Method<void(__thiscall*)(void*,bool)>(inputSystem,11)(inputSystem,!inputDisabled);
        }
        if (Config::settings.menuOpened) {
            if (!cursorReleased) {
                GetClipCursor(&originalClip);
                cursorReleased = true;
            }
            ClipCursor(nullptr);
            SetCursor(LoadCursor(nullptr,IDC_ARROW));
        } else if (cursorReleased) {
            ClipCursor(&originalClip);
            cursorReleased = false;
        }
    }
    void* Capture(const char* moduleName, const char* prefix)
    {
        HMODULE module = GetModuleHandleA(moduleName);
        if (!module) return nullptr;
        using Factory = void*(__cdecl*)(const char*,int*);
        Factory factory = (Factory)GetProcAddress(module,"CreateInterface");
        if (!factory) return nullptr;
        for (int version = 99; version >= 0; --version) {
            char name[80];
            std::snprintf(name,sizeof(name),"%s%03d",prefix,version);
            if (void* object = factory(name,nullptr)) {
                Logger::Write(name);
                return object;
            }
        }
        return nullptr;
    }
    BYTE* FindPattern(HMODULE module, const BYTE* bytes, const char* mask)
    {
        BYTE* image = (BYTE*)module;
        const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)image;
        const IMAGE_NT_HEADERS32* nt = (const IMAGE_NT_HEADERS32*)(image+dos->e_lfanew);
        const IMAGE_SECTION_HEADER* sections = IMAGE_FIRST_SECTION(nt);
        const size_t length = std::strlen(mask);
        for (WORD section = 0; section < nt->FileHeader.NumberOfSections; ++section) {
            if (!(sections[section].Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
            BYTE* begin = image+sections[section].VirtualAddress;
            const DWORD size = sections[section].Misc.VirtualSize;
            for (DWORD offset = 0; offset+length <= size; ++offset) {
                bool match = true;
                for (size_t i = 0; i < length; ++i) {
                    if (mask[i] == '?' || begin[offset+i] == bytes[i]) continue;
                    match = false; break;
                }
                if (match) return begin+offset;
            }
        }
        return nullptr;
    }
    BYTE* PatternText(HMODULE module,const char* pattern)
    {
        std::vector<BYTE> bytes; std::string mask;
        const char* cursor = pattern;
        while (*cursor) {
            if (*cursor == ' ') { ++cursor; continue; }
            if (*cursor == '?') {
                bytes.push_back(0); mask.push_back('?');
                while (*cursor == '?') ++cursor;
            } else {
                char* end = nullptr;
                bytes.push_back(static_cast<BYTE>(std::strtoul(cursor,&end,16))); mask.push_back('x');
                cursor = end;
            }
        }
        return FindPattern(module,bytes.data(),mask.c_str());
    }
    void __fastcall HookPaint(void* object, void*, int mode)
    {
        Callback callback;
        originalPaint(object,mode);
        if (!(mode & 1)) return;
        std::lock_guard<std::recursive_mutex> guard(menuMutex);
        static bool reached = false;
        static bool drew = false;
        if (!reached) { Logger::Write("Game VGUI Paint callback reached"); reached = true; }
        UpdateCursor();
        if (!Config::settings.VisEnableEnemy
            && !Config::settings.VisWorldHitmarker && !Config::settings.VisWorldDamage) return;
        startDrawing(gameSurface);
        Surface::instance = gameSurface;
        ESP::Render();
        finishDrawing(gameSurface);
        if (!drew) {
            Logger::Write("Game ESP rendered through ISurface");
            drew = true;
        }
    }
    void __fastcall HookLockCursor(void* object, void*)
    {
        Callback callback;
        std::lock_guard<std::recursive_mutex> guard(menuMutex);
        if (Config::settings.menuOpened) {
            Surface::instance = gameSurface;
            Surface::UnlockCursor();
            return;
        }
        originalLockCursor(object);
    }
    bool PrepareSurface()
    {
        engineVGui = Capture("engine.dll","VEngineVGui");
        gameSurface = Capture("vguimatsurface.dll","VGUI_Surface");
        inputSystem = Capture("inputsystem.dll","InputSystemVersion");
        if (!engineVGui || !gameSurface || !inputSystem) {
            Logger::Write("VGUI/input interface capture failed");
            return false;
        }
        const BYTE startBytes[] = {0x55,0x8B,0xEC,0x83,0xE4,0xC0,0x83,0xEC,0x38};
        const BYTE finishBytes[] = {0x8B,0x0D,0,0,0,0,0x56,0xC6,0x05};
        HMODULE module = GetModuleHandleA("vguimatsurface.dll");
        startDrawing = (Drawing)FindPattern(module,startBytes,"xxxxxxxxx");
        finishDrawing = (Drawing)FindPattern(module,finishBytes,"xx????xxx");
        if (!startDrawing || !finishDrawing) {
            Logger::Write("Surface StartDrawing/FinishDrawing pattern missing");
            return false;
        }
        Surface::instance = gameSurface;
        gameBackend = true;
        return true;
    }

    LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        Callback callback;
        {
            std::lock_guard<std::recursive_mutex> guard(menuMutex);
            const bool handled = Menu::HandleMessage(message,wParam,lParam);
            UpdateCursor();
            if (message == WM_LBUTTONDOWN && handled) SetCapture(window);
            if (message == WM_LBUTTONUP && GetCapture() == window) ReleaseCapture();
            if (handled) return 0;
            if (Config::settings.menuOpened) {
                if (message == WM_SETCURSOR) { SetCursor(LoadCursor(nullptr,IDC_ARROW)); return TRUE; }
                if (message == WM_INPUT) return DefWindowProcA(window,message,wParam,lParam);
                if ((message >= WM_MOUSEFIRST && message <= WM_MOUSELAST)
                    || (message >= WM_KEYFIRST && message <= WM_KEYLAST))
                    return 0;
            }
        }
        return CallWindowProcA(originalWindowProc,window,message,wParam,lParam);
    }
    bool DrawBackbuffer(IDirect3DDevice9* device)
    {
        IDirect3DSurface9* backbuffer = nullptr;
        if (FAILED(device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&backbuffer))) return false;
        IDirect3DSurface9* savedTargets[4]{};
        D3DCAPS9 caps{};
        device->GetDeviceCaps(&caps);
        const unsigned count = caps.NumSimultaneousRTs < 4 ? caps.NumSimultaneousRTs : 4;
        for (unsigned i = 0; i < count; ++i) device->GetRenderTarget(i,&savedTargets[i]);
        IDirect3DSurface9* savedDepth = nullptr;
        device->GetDepthStencilSurface(&savedDepth);
        D3DVIEWPORT9 savedViewport{};
        device->GetViewport(&savedViewport);
        for (unsigned i = 1; i < count; ++i) device->SetRenderTarget(i,nullptr);
        device->SetDepthStencilSurface(nullptr);
        bool drawn = false;
        if (SUCCEEDED(device->SetRenderTarget(0,backbuffer))) {
            D3DSURFACE_DESC description{};
            backbuffer->GetDesc(&description);
            const D3DVIEWPORT9 viewport = {0,0,description.Width,description.Height,0,1};
            device->SetViewport(&viewport);
            const HRESULT scene = device->BeginScene();
            if (SUCCEEDED(scene) || scene == D3DERR_INVALIDCALL) {
                drawn = Menu::Render(device,SDK::ready ? SDK::Latency()*1000 : 0);
                if (SUCCEEDED(scene)) device->EndScene();
            }
        }
        if (savedTargets[0]) device->SetRenderTarget(0,savedTargets[0]);
        for (unsigned i = 1; i < count; ++i) device->SetRenderTarget(i,savedTargets[i]);
        device->SetDepthStencilSurface(savedDepth);
        device->SetViewport(&savedViewport);
        for (unsigned i = 0; i < count; ++i) if (savedTargets[i]) savedTargets[i]->Release();
        if (savedDepth) savedDepth->Release();
        backbuffer->Release();
        return drawn;
    }
    HRESULT WINAPI HookPresent(IDirect3DDevice9* device, const RECT* source,
        const RECT* destination, HWND overrideWindow, const RGNDATA* dirty)
    {
        Callback callback;
        static bool callbackLogged = false;
        static bool frameLogged = false;
        D3DDEVICE_CREATION_PARAMETERS creation{};
        const bool sameWindow = SUCCEEDED(device->GetCreationParameters(&creation)) && creation.hFocusWindow == gameWindow;
        if (device == hookedDevice || sameWindow) {
            std::lock_guard<std::recursive_mutex> guard(menuMutex);
            if (!callbackLogged) {
                Logger::Write("Present callback reached on game device");
                callbackLogged = true;
            }
            UpdateCursor();
            if (Menu::Visible() && DrawBackbuffer(device) && !frameLogged) {
                Logger::Write("Menu draw succeeded on the displayed backbuffer");
                frameLogged = true;
            }
        }
        return originalPresent(device,source,destination,overrideWindow,dirty);
    }
    HRESULT WINAPI HookReset(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* parameters)
    {
        Callback callback;
        D3DDEVICE_CREATION_PARAMETERS creation{};
        if (device == hookedDevice || (SUCCEEDED(device->GetCreationParameters(&creation)) && creation.hFocusWindow == gameWindow)) {
            std::lock_guard<std::recursive_mutex> guard(menuMutex);
            Menu::DeviceLost();
        Extras::ResetCamera();
        }
        return originalReset(device,parameters);
    }
    bool Check(MH_STATUS result, const char* operation)
    {
        if (result == MH_OK) return true;
        Logger::Write(operation);
        Logger::Write(MH_StatusToString(result));
        return false;
    }
    BYTE* FindDevicePattern(HMODULE module)
    {
        BYTE* image = (BYTE*)module;
        const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)image;
        const IMAGE_NT_HEADERS32* nt = (const IMAGE_NT_HEADERS32*)(image+dos->e_lfanew);
        const IMAGE_SECTION_HEADER* sections = IMAGE_FIRST_SECTION(nt);
        const BYTE bytes[] = {0xA1,0,0,0,0,0x50,0x8B,0x08,0xFF,0x51,0x0C};
        for (WORD section = 0; section < nt->FileHeader.NumberOfSections; ++section) {
            if (!(sections[section].Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
            BYTE* begin = image+sections[section].VirtualAddress;
            const DWORD size = sections[section].Misc.VirtualSize;
            for (DWORD offset = 0; offset+sizeof(bytes) <= size; ++offset) {
                bool match = true;
                for (unsigned i = 0; i < sizeof(bytes); ++i) {
                    if (i >= 1 && i <= 4) continue;
                    if (begin[offset+i] != bytes[i]) { match = false; break; }
                }
                if (match) return begin+offset;
            }
        }
        return nullptr;
    }
}
bool Hooks::SetupGame()
{
    HMODULE module = GetModuleHandleA("shaderapidx9.dll");
    if (!module) { Logger::Write("shaderapidx9.dll is not loaded"); return false; }
    BYTE* match = FindDevicePattern(module);
    if (!match) { Logger::Write("DirectDevice pattern not found: A1 ? ? ? ? 50 8B 08 FF 51 0C"); return false; }
    IDirect3DDevice9** deviceAddress = nullptr;
    std::memcpy(&deviceAddress,match+1,sizeof(deviceAddress));
    if (!deviceAddress || !*deviceAddress) { Logger::Write("DirectDevice is not initialized"); return false; }
    if (!PrepareSurface() || !SDK::Initialize()) return false;
    const BYTE inputBytes[] = {0xB9,0,0,0,0,0xF3,0x0F,0x11,0x04,0x24,0xFF,0x50,0x10};
    if (BYTE* inputMatch = FindPattern(GetModuleHandleA("client.dll"),inputBytes,"x????xxxxxxxx"))
        std::memcpy(&Extras::cameraInput,inputMatch+1,sizeof(void*));
    else Logger::Write("Third-person input pattern unavailable");
    HMODULE clientModule = GetModuleHandleA("client.dll");
    // Unique in the installed legacy engine. Disassembly confirms XMM0/CL
    // arguments, client-state load at +0x1b, and BL packet decision around slot22.
    movementTarget = PatternText(GetModuleHandleA("engine.dll"),"55 8B EC 81 EC 64 01 00 00 53 56 8A F9 F3 0F 11 45 FC 8B 4D 04 57 E8 ? ? ? ? 8B 3D");
    if (movementTarget) std::memcpy(&movementClientState,static_cast<BYTE*>(movementTarget)+0x1d,4);
    else Logger::Write("Double tap / hide shots unavailable: verified CL_Move signature missing");
    Resolver::setOrigin = (Resolver::SetVector)PatternText(clientModule,"55 8B EC 83 E4 F8 51 53 56 57 8B F1 E8");
    Resolver::setAngles = (Resolver::SetVector)PatternText(clientModule,"55 8B EC 83 E4 F8 83 EC 64 53 56 57 8B F1 E8");
    Resolver::setVelocity = (Resolver::SetVector)PatternText(clientModule,"55 8B EC 83 E4 F8 83 EC 0C 53 56 57 8B 7D 08 8B F1 F3");
    Resolver::invalidateBones = (Resolver::Invalidate)PatternText(clientModule,"80 ? ? ? ? ? ? 74 16 A1 ? ? ? ? 48 C7 ? ? ? ? ? ? ? ? ? 89 ? ? ? ? ? C3");
    RenderPose::attachments = (RenderPose::Attachments)PatternText(clientModule,"55 8B EC 83 EC 48 53 8B 5D 08 89 4D F4");
    if (!RenderPose::attachments) Logger::Write("Enemy render pose hook unavailable: attachment helper signature missing");
    Logger::Write(Resolver::Available() ? "Resolver engine helpers ready" : "Resolver engine helper signature missing; correction unavailable");
    if (BYTE* helperMatch = PatternText(clientModule,"8B 0D ? ? ? ? 8B 45 ? 51 8B D4 89 02 8B 01")) {
        void** address = nullptr; std::memcpy(&address,helperMatch+2,4);
        SDK::moveHelper = address ? *address : nullptr;
    }
    if (BYTE* seedMatch = PatternText(clientModule,"8B 0D ? ? ? ? BA ? ? ? ? E8 ? ? ? ? 83 C4 04"))
        std::memcpy(&SDK::predictionSeed,seedMatch+2,4);
    if (BYTE* playerMatch = PatternText(clientModule,"89 35 ? ? ? ? F3 0F 10 48 20"))
        std::memcpy(&SDK::predictionPlayer,playerMatch+2,4);
    Prediction::hashCommand = (Prediction::Hash)PatternText(clientModule,"55 8B EC 83 E4 F8 83 EC 70 6A 58 8D 44 24 1C 89 4C 24 08 6A 00 50");
    Logger::Write(Prediction::Available() ? "Engine prediction helpers ready" : "Engine prediction helper signature missing; prediction unavailable");
    eventManager = Capture("engine.dll","GAMEEVENTSMANAGER");
    return Setup(*deviceAddress);
}
bool Hooks::Setup(IDirect3DDevice9* device)
{
    if (hookedDevice) return hookedDevice == device;
    if (!device) return false;
    D3DDEVICE_CREATION_PARAMETERS creation{};
    if (FAILED(device->GetCreationParameters(&creation)) || !creation.hFocusWindow) {
        Logger::Write("D3D9 focus window lookup failed");
        return false;
    }
    if (!Check(MH_Initialize(),"MH_Initialize failed")) return false;
    minhookReady = true;
    if (gameBackend) {
        if (movementTarget && movementClientState) {
            clientMoveTarget = (*(void***)SDK::client)[22];
            MovementABI::dispatch = &DispatchMovement;
            MovementABI::clientDispatch = &DispatchClientMove;
            if (!Check(MH_CreateHook(movementTarget,(void*)&MovementABI::Hook,&MovementABI::original),"CL_Move hook failed")
                || !Check(MH_CreateHook(clientMoveTarget,(void*)&MovementABI::ClientHook,(void**)&originalClientMove),"Client command packet hook failed")) {
                Restore(); return false;
            }
            Exploits::available = true;
            Logger::Write("Double tap / hide shots: CL_Move and client packet bridges ready");
        }
        paintTarget = (*(void***)engineVGui)[14];
        cursorTarget = (*(void***)gameSurface)[67];
        if (!Check(MH_CreateHook(paintTarget,(void*)&HookPaint,(void**)&originalPaint),"VGUI Paint hook creation failed")
            || !Check(MH_CreateHook(cursorTarget,(void*)&HookLockCursor,(void**)&originalLockCursor),"LockCursor hook creation failed")) {
            Restore(); return false;
        }
        createMoveTarget = (*(void***)SDK::clientMode)[24];
        frameStageTarget = (*(void***)SDK::client)[37];
        viewTarget = (*(void***)SDK::clientMode)[18];
        eventTarget = eventManager ? (*(void***)eventManager)[Events::ClientSideSlot] : nullptr;
        panelInterface = Capture("vgui2.dll","VGUI_Panel");
        panelTarget = panelInterface ? (*(void***)panelInterface)[41] : nullptr;
        if (panelTarget && !Check(MH_CreateHook(panelTarget,(void*)&HookTraverse,(void**)&originalTraverse),"Scope panel hook failed")) { Restore(); return false; }
        simulationTarget = SDK::simulationProperty ? SDK::simulationProperty->proxy : nullptr;
        if (simulationTarget && !Check(MH_CreateHook(simulationTarget,(void*)&HookSimulation,(void**)&originalSimulation),"Simulation-time proxy hook failed")) { Restore(); return false; }
        if (eventTarget && !Check(MH_CreateHook(eventTarget,(void*)&HookEvent,(void**)&originalEvent),"Game event hook failed")) { Restore(); return false; }
        if (!Check(MH_CreateHook(createMoveTarget,(void*)&HookCreateMove,(void**)&originalCreateMove),"ClientMode CreateMove hook failed")
            || !Check(MH_CreateHook(viewTarget,(void*)&HookView,(void**)&originalView),"OverrideView hook failed")
            || !Check(MH_CreateHook(frameStageTarget,(void*)&HookFrameStage,(void**)&originalFrameStage),"FrameStageNotify hook failed")) {
            Restore(); return false;
        }
    }
    {
        void** table = *(void***)device;
        presentTarget = table[17];
        resetTarget = table[16];
        if (!Check(MH_CreateHook(presentTarget,(void*)&HookPresent,(void**)&originalPresent),"Present hook creation failed")
            || !Check(MH_CreateHook(resetTarget,(void*)&HookReset,(void**)&originalReset),"Reset hook creation failed")) {
            Restore(); return false;
        }
    }
    gameWindow = creation.hFocusWindow;
    SetLastError(0);
    originalWindowProc = (WNDPROC)SetWindowLongPtrA(gameWindow,GWLP_WNDPROC,(LONG_PTR)&WindowProc);
    if (!originalWindowProc) {
        Logger::Write("Window procedure installation failed");
        Restore(); return false;
    }
    hookedDevice = device;
    if (Exploits::available && (!Check(MH_QueueEnableHook(movementTarget),"CL_Move queue failed")
        || !Check(MH_QueueEnableHook(clientMoveTarget),"Client command packet queue failed"))) { Restore(); return false; }
    if (gameBackend && (!Check(MH_QueueEnableHook(createMoveTarget),"CreateMove queue failed")
        || !Check(MH_QueueEnableHook(viewTarget),"OverrideView queue failed")
        || !Check(MH_QueueEnableHook(frameStageTarget),"FrameStage queue failed"))) { Restore(); return false; }
    if (eventTarget && !Check(MH_QueueEnableHook(eventTarget),"Game event queue failed")) { Restore(); return false; }
    if (simulationTarget && !Check(MH_QueueEnableHook(simulationTarget),"Simulation-time proxy queue failed")) { Restore(); return false; }
    if (panelTarget && !Check(MH_QueueEnableHook(panelTarget),"Scope panel queue failed")) { Restore(); return false; }
    if (paintTarget && !Check(MH_QueueEnableHook(paintTarget),"Paint queue failed")) { Restore(); return false; }
    if (cursorTarget && !Check(MH_QueueEnableHook(cursorTarget),"Cursor queue failed")) { Restore(); return false; }
    if (!Check(MH_QueueEnableHook(presentTarget),"Present queue failed")
        || !Check(MH_QueueEnableHook(resetTarget),"Reset queue failed")
        || !Check(MH_ApplyQueued(),"Enabling render hooks failed")) {
        Restore(); return false;
    }
    Logger::Write(gameBackend ? "VGUI Paint (14), LockCursor (67), and WndProc installed; Insert toggles menu" : "Present (17), Reset (16), and WndProc installed; Insert toggles menu");
    return true;
}
void Hooks::Restore()
{
    if (minhookReady) {
        if (presentTarget) MH_DisableHook(presentTarget);
        if (resetTarget) MH_DisableHook(resetTarget);
        if (paintTarget) MH_DisableHook(paintTarget);
        if (cursorTarget) MH_DisableHook(cursorTarget);
        if (createMoveTarget) MH_DisableHook(createMoveTarget);
        if (movementTarget) MH_DisableHook(movementTarget);
        if (clientMoveTarget) MH_DisableHook(clientMoveTarget);
        if (frameStageTarget) MH_DisableHook(frameStageTarget);
        if (bonesTarget) MH_DisableHook(bonesTarget);
        if (viewTarget) MH_DisableHook(viewTarget);
        if (eventTarget) MH_DisableHook(eventTarget);
        if (simulationTarget) MH_DisableHook(simulationTarget);
        if (panelTarget) MH_DisableHook(panelTarget);
    }
    if (gameWindow && originalWindowProc)
        SetWindowLongPtrA(gameWindow,GWLP_WNDPROC,(LONG_PTR)originalWindowProc);
    while (callbacks.load() != 0) Sleep(1);
    {
        std::lock_guard<std::recursive_mutex> guard(menuMutex);
        if (cursorReleased) { ClipCursor(&originalClip); cursorReleased = false; }
        if (gameWindow && GetCapture() == gameWindow) ReleaseCapture();
        Menu::Release();
        Removals::Restore();
        RenderPose::Clear();
        Extras::ResetCamera();
        if (inputSystem && inputDisabled) Surface::Method<void(__thiscall*)(void*,bool)>(inputSystem,11)(inputSystem,true);
        inputDisabled = false;
    }
    if (minhookReady) {
        if (presentTarget) MH_RemoveHook(presentTarget);
        if (resetTarget) MH_RemoveHook(resetTarget);
        if (paintTarget) MH_RemoveHook(paintTarget);
        if (cursorTarget) MH_RemoveHook(cursorTarget);
        if (createMoveTarget) MH_RemoveHook(createMoveTarget);
        if (movementTarget) MH_RemoveHook(movementTarget);
        if (clientMoveTarget) MH_RemoveHook(clientMoveTarget);
        if (frameStageTarget) MH_RemoveHook(frameStageTarget);
        if (bonesTarget) MH_RemoveHook(bonesTarget);
        if (viewTarget) MH_RemoveHook(viewTarget);
        if (eventTarget) MH_RemoveHook(eventTarget);
        if (simulationTarget) MH_RemoveHook(simulationTarget);
        if (panelTarget) MH_RemoveHook(panelTarget);
        MH_Uninitialize();
    }
    minhookReady = false;
    hookedDevice = nullptr;
    originalPresent = nullptr;
    originalReset = nullptr;
    presentTarget = nullptr;
    resetTarget = nullptr;
    paintTarget = nullptr; cursorTarget = nullptr;
    gameWindow = nullptr;
    originalWindowProc = nullptr;
    gameBackend = false;
    createMoveTarget = nullptr;
    movementTarget = nullptr; clientMoveTarget = nullptr; movementClientState = nullptr; originalClientMove = nullptr;
    MovementABI::original = nullptr; MovementABI::dispatch = nullptr; MovementABI::clientDispatch = nullptr;
    Exploits::available = false; Exploits::state.Reset(); exploitWeapon = nullptr;
    Desync::state.Reset(); Desync::packetOverride = -1;
    frameStageTarget = nullptr;
    bonesTarget = nullptr; originalBones = nullptr; RenderPose::attachments = nullptr;
    originalCreateMove = nullptr;
    originalFrameStage = nullptr;
    viewTarget = nullptr;
    eventTarget = nullptr; eventManager = nullptr; originalEvent = nullptr;
    originalView = nullptr;
    Extras::cameraInput = nullptr;
    Prediction::Reset();
    Extras::anglesValid = false;
    SDK::prediction = nullptr;
    SDK::ready = false;
    panelTarget = nullptr; panelInterface = nullptr; originalTraverse = nullptr;
    SDK::simulationProperty = nullptr; simulationTarget = nullptr; originalSimulation = nullptr;
    Ragebot::Clear();
    originalPaint = nullptr;
    originalLockCursor = nullptr;
    engineVGui = nullptr;
    gameSurface = nullptr;
    inputSystem = nullptr;
}
bool __cdecl HavocAttachDevice(IDirect3DDevice9* device) { return Hooks::Setup(device); }
void __cdecl HavocDetachDevice() { Hooks::Restore(); }
