#include "menu.h"
#include "../features/exploits.h"
#include "../features/performance.h"
#include "animation.h"
#include "font_data.h"
#include "../core/config.h"
#include "../features/ragebot.h"
#include "../features/shots.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_dx9.h"
#include <windowsx.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_map>

namespace {
    ImGuiContext* context = nullptr;
    IDirect3DDevice9* renderDevice = nullptr;
    ImFont* brandFont = nullptr;
    int tab = 0,ragePage = 0,visualPage = 0,profile = 0;
    Animation::Fade menuFade,watermarkFade{0,0,0,0};
    float smoothedFps = 60,pageProgress = 1,navPosition = 0;
    int previousPage = -1,panelNumber = 0;
    ULONGLONG previousFrame = 0;
    ImVec2 menuPosition{70,35};
    const char* profileStatus = "Ready";
    struct Motion { float hover = 0,active = 0,value = 0,popup = 0; bool initialized = false; };
    std::unordered_map<ImGuiID,Motion> motions;
    float Ease(float value,float target,float speed = 16) {
        return Animation::Approach(value,target,ImGui::GetIO().DeltaTime,speed);
    }
    ImU32 Grey(float value,float opacity = 1) {
        return ImGui::GetColorU32(ImVec4(value/255,value/255,value/255,opacity));
    }
    void Context() {
        if (context) { ImGui::SetCurrentContext(context); return; }
        IMGUI_CHECKVERSION(); context = ImGui::CreateContext();
        auto& io = ImGui::GetIO(); io.IniFilename = nullptr; io.LogFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.BackendPlatformName = "havoc_win32_messages";
        const int keys[] = {VK_TAB,VK_LEFT,VK_RIGHT,VK_UP,VK_DOWN,VK_PRIOR,VK_NEXT,VK_HOME,VK_END,VK_INSERT,VK_DELETE,VK_BACK,VK_SPACE,VK_RETURN,VK_ESCAPE,VK_RETURN,'A','C','V','X','Y','Z'};
        static_assert(sizeof(keys)/sizeof(keys[0]) == ImGuiKey_COUNT);
        for (int i = 0; i < ImGuiKey_COUNT; ++i) io.KeyMap[i] = keys[i];
        ImFontConfig config{}; config.FontDataOwnedByAtlas = false;
        config.OversampleH = 2; config.OversampleV = 1; config.PixelSnapH = true;
        io.FontDefault = io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(menuFontData),sizeof(menuFontData),13,&config,io.Fonts->GetGlyphRangesCyrillic());
        brandFont = io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(menuFontData),sizeof(menuFontData),21,&config);
        io.Fonts->Build(); ImGui::StyleColorsDark();
        auto& style = ImGui::GetStyle();
        style.WindowPadding = {0,0}; style.FramePadding = {8,6}; style.ItemSpacing = {8,3};
        style.WindowRounding = 9; style.ChildRounding = 6; style.FrameRounding = 4;
        style.PopupRounding = 5; style.PopupBorderSize = 1;
        style.ScrollbarSize = 5; style.ScrollbarRounding = 3; style.WindowBorderSize = 0;
        style.Colors[ImGuiCol_WindowBg] = ImVec4(.13f,.13f,.13f,1);
        style.Colors[ImGuiCol_ChildBg] = ImVec4(0,0,0,0);
        style.Colors[ImGuiCol_PopupBg] = ImVec4(.17f,.17f,.17f,.99f);
        style.Colors[ImGuiCol_Border] = ImVec4(.27f,.27f,.27f,1);
        style.Colors[ImGuiCol_Text] = ImVec4(.84f,.84f,.84f,1);
        style.Colors[ImGuiCol_TextDisabled] = ImVec4(.48f,.48f,.48f,1);
        style.Colors[ImGuiCol_FrameBg] = ImVec4(.12f,.12f,.12f,1);
        style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(.22f,.22f,.22f,1);
        style.Colors[ImGuiCol_FrameBgActive] = ImVec4(.27f,.27f,.27f,1);
        style.Colors[ImGuiCol_CheckMark] = style.Colors[ImGuiCol_SliderGrab] = ImVec4(.8f,.8f,.8f,1);
        style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(.95f,.95f,.95f,1);
        style.Colors[ImGuiCol_Button] = style.Colors[ImGuiCol_Header] = ImVec4(.21f,.21f,.21f,1);
        style.Colors[ImGuiCol_ButtonHovered] = style.Colors[ImGuiCol_HeaderHovered] = ImVec4(.28f,.28f,.28f,1);
        style.Colors[ImGuiCol_ButtonActive] = style.Colors[ImGuiCol_HeaderActive] = ImVec4(.33f,.33f,.33f,1);
        style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0,0,0,0);
        style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(.29f,.29f,.29f,.8f);
        style.Colors[ImGuiCol_ScrollbarGrabHovered] = style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(.45f,.45f,.45f,1);
        style.Colors[ImGuiCol_NavHighlight] = ImVec4(.8f,.8f,.8f,.6f);
    }
    bool Hit(const char* name,ImVec2 size,ImRect& bounds,Motion*& motion) {
        auto* window = ImGui::GetCurrentWindow();
        const auto id = window->GetID(name);
        bounds = ImRect(window->DC.CursorPos,ImVec2(window->DC.CursorPos.x+size.x,window->DC.CursorPos.y+size.y));
        ImGui::ItemSize(bounds); motion = &motions[id];
        if (!ImGui::ItemAdd(bounds,id)) return false;
        bool hovered = false,held = false;
        const bool pressed = ImGui::ButtonBehavior(bounds,id,&hovered,&held);
        motion->hover = Ease(motion->hover,hovered || ImGui::IsItemFocused() ? 1.f : 0.f);
        ImGui::RenderNavHighlight(bounds,id);
        return pressed;
    }
    void Check(const char* name,bool& value) {
        ImRect b; Motion* m;
        if (Hit(name,{ImGui::GetContentRegionAvail().x,20},b,m)) value = !value;
        if (!m->initialized) { m->active = value ? 1.f : 0.f; m->initialized = true; }
        m->active = Ease(m->active,value ? 1.f : 0.f);
        auto* draw = ImGui::GetWindowDrawList();
        draw->AddText({b.Min.x,b.Min.y+4},Grey(177+53*m->hover),name);
        const ImVec2 a{b.Max.x-29,b.Min.y+5},z{b.Max.x,b.Min.y+18};
        draw->AddRectFilled(a,z,Grey(59+57*m->active+7*m->hover),7);
        draw->AddCircleFilled({a.x+6.5f+16*m->active,a.y+6.5f},4.5f,Grey(142+96*m->active),16);
    }
    bool Button(const char* name,float height = 30,bool selected = false) {
        ImRect b; Motion* m;
        const bool pressed = Hit(name,{ImGui::GetContentRegionAvail().x,height},b,m);
        m->active = Ease(m->active,selected ? 1.f : 0.f);
        auto* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(b.Min,b.Max,Grey(48+12*m->hover+12*m->active),4);
        draw->AddRect(b.Min,b.Max,Grey(66+24*m->hover),4);
        const auto text = ImGui::CalcTextSize(name);
        draw->AddText({b.Min.x+10,b.Min.y+(height-text.y)*.5f},Grey(185+50*m->hover),name);
        return pressed;
    }
    void Slider(const char* name,int& value,int minimum,int maximum) {
        auto* window = ImGui::GetCurrentWindow(); if (window->SkipItems) return;
        auto& g = *ImGui::GetCurrentContext(); const auto id = window->GetID(name);
        const auto p = ImGui::GetCursorScreenPos(); const float width = ImGui::GetContentRegionAvail().x;
        const ImRect total(p,{p.x+width,p.y+42}),track({p.x,p.y+21},{p.x+width,p.y+39});
        ImGui::ItemSize(total); if (!ImGui::ItemAdd(total,id,&track)) return;
        const bool hover = ImGui::ItemHoverable(track,id);
        if ((hover && g.IO.MouseClicked[0]) || g.NavActivateId == id) {
            ImGui::SetActiveID(id,window); ImGui::SetFocusID(id,window); ImGui::FocusWindow(window);
            g.ActiveIdUsingNavDirMask |= (1 << ImGuiDir_Left)|(1 << ImGuiDir_Right);
        }
        ImRect grab;
        if (ImGui::SliderBehavior(track,id,ImGuiDataType_S32,&value,&minimum,&maximum,"%d",ImGuiSliderFlags_NoInput,&grab)) ImGui::MarkItemEdited(id);
        auto& m = motions[id]; const float target = (std::clamp(value,minimum,maximum)-minimum)/float(maximum-minimum);
        if (!m.initialized) { m.value = target; m.initialized = true; }
        m.value = Ease(m.value,target,22); m.hover = Ease(m.hover,hover || g.ActiveId == id ? 1.f : 0.f);
        char number[24]; std::snprintf(number,sizeof(number),"%d",value);
        auto* draw = window->DrawList;
        draw->AddText(p,Grey(190+35*m.hover),name);
        draw->AddText({p.x+width-ImGui::CalcTextSize(number).x,p.y},Grey(225),number);
        const float x = p.x+5+(width-10)*m.value,y = p.y+29;
        draw->AddRectFilled({p.x,y-2},{p.x+width,y+2},Grey(62),2);
        draw->AddRectFilled({p.x,y-2},{x,y+2},Grey(169+35*m.hover),2);
        draw->AddCircleFilled({x,y},8,Grey(220,.07f*m.hover),20);
        draw->AddCircleFilled({x,y},3.5f+m.hover,Grey(224),20);
        ImGui::RenderNavHighlight(track,id);
    }
    void Choice(const char* name,int& value,const char* values) {
        int count = 0; const char* options[32]{};
        for (const char* option = values; *option && count < 32; option += std::strlen(option)+1) options[count++] = option;
        if (!count) return; value = std::clamp(value,0,count-1);
        ImGui::PushID(name); ImGui::TextUnformatted(name);
        ImRect b; Motion* m; const bool pressed = Hit("##choice",{ImGui::GetContentRegionAvail().x,27},b,m);
        if (pressed) { ImGui::OpenPopup("##options"); m->popup = 0; }
        const bool opened = ImGui::IsPopupOpen("##options");
        m->active = Ease(m->active,opened ? 1.f : 0.f);
        auto* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(b.Min,b.Max,Grey(43+9*m->hover),4);
        draw->AddRect(b.Min,b.Max,Grey(66+32*m->active+12*m->hover),4);
        draw->AddText({b.Min.x+9,b.Min.y+7},Grey(195+30*m->hover),options[value]);
        const float x = b.Max.x-14,y = b.Min.y+13,flip = 1-2*m->active;
        draw->AddLine({x-3,y-2*flip},{x,y+2*flip},Grey(170),1);
        draw->AddLine({x,y+2*flip},{x+3,y-2*flip},Grey(170),1);
        ImGui::SetNextWindowPos({b.Min.x,b.Max.y+4}); ImGui::SetNextWindowSize({b.GetWidth(),0});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(5,5));
        if (ImGui::BeginPopup("##options",ImGuiWindowFlags_NoMove)) {
            m->popup = Ease(m->popup,1,22);
            // Fade the entire popup, including its background and option text.
            auto* popupDraw = ImGui::GetWindowDrawList();
            for (int i = 0; i < count; ++i) {
                ImGui::PushID(i);
                if (Button(options[i],27,value == i)) { value = i; ImGui::CloseCurrentPopup(); }
                ImGui::PopID();
            }
            for (auto& vertex : popupDraw->VtxBuffer) {
                const ImU32 opacity = static_cast<ImU32>(((vertex.col>>IM_COL32_A_SHIFT)&255)*m->popup);
                vertex.col = (vertex.col&~IM_COL32_A_MASK)|(opacity<<IM_COL32_A_SHIFT);
            }
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar(); ImGui::PopID(); ImGui::Dummy({0,2});
    }
    template<typename F> void Panel(const char* name,float x,float y,float width,float height,F contents) {
        const float progress = std::clamp(pageProgress-panelNumber++*.055f,0.f,1.f);
        const float opacity = progress*progress*(3-2*progress);
        y += (1-opacity)*7;
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha,ImGui::GetStyle().Alpha*opacity);
        const auto p = ImGui::GetWindowPos(); auto* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled({p.x+x,p.y+y},{p.x+x+width,p.y+y+height},Grey(39),6);
        draw->AddRect({p.x+x,p.y+y},{p.x+x+width,p.y+y+height},Grey(56),6);
        draw->AddText({p.x+x+14,p.y+y+12},Grey(227),name);
        draw->AddLine({p.x+x+14,p.y+y+31},{p.x+x+width-14,p.y+y+31},Grey(53));
        ImGui::SetCursorPos({x+14,y+40});
        ImGui::BeginChild(name,{width-28,height-50},false,ImGuiWindowFlags_NoBackground);
        contents(); ImGui::EndChild(); ImGui::PopStyleVar();
    }
    bool Navigation(const char* label,ImVec2 position,ImVec2 size,bool selected) {
        ImGui::SetCursorPos(position); ImRect b; Motion* m;
        const bool pressed = Hit(label,size,b,m); m->active = Ease(m->active,selected ? 1.f : 0.f);
        auto* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(b.Min,b.Max,Grey(66,.15f*m->hover),4);
        draw->AddText({b.Min.x+12,b.Min.y+(size.y-13)*.5f},Grey(132+97*m->active+20*m->hover),label);
        return pressed;
    }
    void Layout(float alpha) {
        auto& s = Config::settings;
        ImGui::SetNextWindowPos(menuPosition,ImGuiCond_Once); ImGui::SetNextWindowSize({740,510});
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha,alpha);
        ImGui::Begin("##havoc",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoBackground|ImGuiWindowFlags_NoSavedSettings|(s.menuOpened ? 0 : ImGuiWindowFlags_NoInputs));
        menuPosition = ImGui::GetWindowPos(); const auto p = menuPosition; auto* draw = ImGui::GetWindowDrawList();
        for (int i = 5; i > 0; --i) draw->AddRect({p.x-i,p.y-i},{p.x+740+i,p.y+510+i},Grey(0,.035f),9.f+i,0,2);
        draw->AddRectFilled(p,{p.x+740,p.y+510},Grey(32),9);
        draw->AddRectFilled({p.x+1,p.y+1},{p.x+739,p.y+57},Grey(42),8,ImDrawFlags_RoundCornersTop);
        draw->AddRect(p,{p.x+740,p.y+510},Grey(65),9);
        draw->AddLine({p.x,p.y+57},{p.x+740,p.y+57},Grey(57));
        draw->AddLine({p.x+136,p.y+57},{p.x+136,p.y+481},Grey(50));
        draw->AddText({p.x+20,p.y+16},Grey(225),"CONTROL CENTER");
        draw->AddText({p.x+20,p.y+34},Grey(130),"Precision. In every detail.");
        const ImVec2 logo{p.x+650,p.y+16};
        const float pulse = .7f+.3f*std::sin(static_cast<float>(ImGui::GetTime())*.9f);
        for (int ring = 3; ring >= 1; --ring)
            for (int angle = 0; angle < 8; ++angle) {
                const float a = angle*3.14159265f*.25f;
                draw->AddText(brandFont,21,{logo.x+std::cos(a)*ring,logo.y+std::sin(a)*ring},Grey(230,.025f*pulse),"havoc");
            }
        draw->AddText(brandFont,21,logo,Grey(237),"havoc");
        const char* tabs[] = {"Rage","Visuals","Misc","Config"};
        navPosition = Ease(navPosition,static_cast<float>(tab),17);
        draw->AddRectFilled({p.x+12,p.y+82+navPosition*44},{p.x+124,p.y+118+navPosition*44},Grey(49),5);
        draw->AddRectFilled({p.x+12,p.y+92+navPosition*44},{p.x+14,p.y+108+navPosition*44},Grey(207),1);
        for (int i = 0; i < 4; ++i) if (Navigation(tabs[i],{18,82+i*44.f},{106,36},tab == i)) tab = i;
        draw->AddText({p.x+24,p.y+441},Grey(110),"INSERT");
        draw->AddText({p.x+24,p.y+458},Grey(143),"Toggle menu");
        const char* rageTabs[] = {"Aimbot","Anti-aim","Resolver","Performance"};
        const char* visualTabs[] = {"Players","World","Removals"};
        const char** sections = tab == 0 ? rageTabs : visualTabs;
        int& page = tab == 0 ? ragePage : visualPage;
        if (tab < 2) {
            float x = 152;
            for (int i = 0; i < (tab == 0 ? 4 : 3); ++i) {
                const float width = ImGui::CalcTextSize(sections[i]).x+26;
                if (Navigation(sections[i],{x,66},{width,27},page == i)) page = i;
                const float selected = motions[ImGui::GetID(sections[i])].active;
                draw->AddLine({p.x+x+12,p.y+95},{p.x+x+width-12,p.y+95},Grey(203,selected),1.5f);
                x += width+8;
            }
        } else draw->AddText({p.x+166,p.y+76},Grey(185),tab == 2 ? "Movement & camera" : "Local profiles");
        const int pageKey = tab*10+(tab < 2 ? page : 0);
        if (previousPage != pageKey) { if (previousPage >= 0) pageProgress = 0; previousPage = pageKey; }
        pageProgress = std::min(1.2f,pageProgress+ImGui::GetIO().DeltaTime*5.5f); panelNumber = 0;
        ImGui::PushID(pageKey);
        if (tab == 0 && ragePage == 0) {
            Panel("Aimbot",152,108,278,363,[&] {
                Check("Enable ragebot",s.RagebotEnable); Check("Automatic fire",s.RagebotAutoFire);
                Check("Silent aim",s.RagebotSilent); Check("Auto scope",s.RagebotAutoScope);
                ImGui::Dummy({0,5}); Slider("Minimum hit chance",s.RagebotHitchance,0,100);
                Slider("Minimum damage",s.RagebotMinDamage,0,130);
                Check("Multipoint",s.RagebotMultipoint); Slider("Point scale",s.RagebotPointScale,0,90);
            });
            Panel("Targeting",444,108,278,185,[&] {
                Choice("Priority",s.RagebotPriority,"Damage\0Crosshair distance\0World distance\0Lowest health\0");
                Choice("Hitboxes",s.RagebotHitboxes,"Head + torso\0Head only\0Torso only\0");
                Check("Lethal body priority",s.RagebotLethalBody);
            });
            Panel("Firing & movement",444,305,278,166,[&] {
                Check("Auto stop",s.RagebotAutoStop); Check("Stop early",s.RagebotStopEarly);
                Check("Stop between shots",s.RagebotStopBetweenShots);
                Check("Double tap",s.DoubleTap); Check("Hide shots",s.HideShots);
            });
        } else if (tab == 0 && ragePage == 1) {
            Panel("Angles",152,108,278,363,[&] {
                Check("Enable anti aim",s.AntiaimEnable); Check("At targets",s.AntiaimAtTargets);
                Choice("Yaw mode",s.AntiaimYawAdd,"Backward\0Jitter\0Rotate\0Spin\0Random\0");
                Choice("Pitch",s.AntiaimPitch,"Off\0Down\0Up\0Zero\0");
                Slider("Jitter / rotate range",s.AntiaimJitterRange,0,180); Slider("Spin speed",s.AntiaimSpinSpeed,0,720);
            });
            Panel("Desync & packets",444,108,278,363,[&] {
                Check("Desync",s.AntiaimDesync); Check("Avoid overlap",s.AntiaimAvoidOverlap);
                Choice("Side",s.AntiaimDesyncSide,"Left\0Right\0Alternate\0");
                Slider("Desync amount",s.AntiaimDesyncAmount,0,58); Slider("Choke ticks",s.AntiaimFakeLagLimit,0,14);
                ImGui::TextDisabled("0 uses one choke tick.");
            });
        } else if (tab == 0 && ragePage == 2) {
            Panel("Reconstruction",152,108,278,363,[&] {
                Check("Anti-aim correction",s.RagebotCorrection); Check("Require safe points",s.RagebotSafePoints);
                Check("Engine prediction",s.RagebotPrediction); Slider("Backtrack window (ms)",s.RagebotBacktrackMs,0,1000);
            });
            Panel("Shot feedback",444,108,278,363,[&] {
                Check("Miss-driven brute force",s.RagebotBruteforce); Check("Match recent records",s.RagebotMatchRecords);
                ImGui::Spacing(); ImGui::TextDisabled("Shots  %d",Shots::statistics.fired); ImGui::TextDisabled("Hits    %d",Shots::statistics.hits);
            });
        } else if (tab == 0) {
            Panel("Scan limits",152,108,278,363,[&] {
                Check("Limit expensive work",s.RagebotPerformance);
                Slider("Targets per tick",s.RagebotTargetsPerTick,0,16);
                Slider("Phase budget (ms)",s.RagebotBudgetMs,1,8);
                Slider("Resolver players / update",s.RagebotResolverPerTick,1,16);
                ImGui::TextWrapped(s.RagebotPerformance ? "With work limits: up to 4 targets; 0 uses 2." : "0 scans all eligible enemies.");
            });
            Panel("Frame timings",444,108,278,363,[&] {
                const auto& times = Performance::samples;
                ImGui::Text("Target scan     %.2f ms",times[0].mean); ImGui::Text("Lag records     %.2f ms",times[1].mean);
                ImGui::Text("Prediction      %.2f ms",times[2].mean); ImGui::Text("Burst           %.2f ms",times[3].mean);
                ImGui::Spacing(); ImGui::TextWrapped("Lower budgets reduce scan coverage. Recharge can briefly pause movement.");
            });
        } else if (tab == 1 && visualPage == 0) {
            Panel("Enemy visuals",152,108,278,363,[&] {
                Check("Enabled",s.VisEnableEnemy); Check("Name",s.VisNameEnemy); Check("Box",s.VisBoxEnemy);
                Check("Health",s.VisHealthEnemy); Check("Weapon text",s.VisWeapTextEnemy); Check("Ammo bar",s.VisAmmoEnemy);
            });
            Panel("Details",444,108,278,363,[&] {
                Check("Skeleton",s.VisSkeletonEnemy); Check("Status flags",s.VisFlagsEnemy); Check("Distance",s.VisDistanceEnemy);
            });
        } else if (tab == 1 && visualPage == 1) {
            Panel("Hit feedback",152,108,278,363,[&] { Check("World hitmarker",s.VisWorldHitmarker); Check("World damage counter",s.VisWorldDamage); });
            Panel("Interface",444,108,278,363,[&] { Check("Watermark",s.VisWatermark); Check("Exploit indicators",s.ExploitIndicators); });
        } else if (tab == 1) {
            Panel("World removals",152,108,278,363,[&] {
                Check("Post processing",s.RemovePostProcessing); Check("Bloom",s.RemoveBloom);
                Check("Fog",s.RemoveFog); Check("Shadows",s.RemoveShadows); Check("3D skybox",s.Remove3dSky);
            });
            Panel("Scope removals",444,108,278,363,[&] {
                Check("Scope overlay",s.RemoveScopeOverlay); Check("Second scope zoom",s.RemoveSecondZoom); Check("All scope zoom",s.RemoveScopeZoom);
            });
        } else if (tab == 2) {
            Panel("Movement",152,108,278,363,[&] { Check("Bunny hop",s.MiscBunnyhop); Check("Auto strafe",s.MiscAutoStrafe); Check("Quick stop",s.MiscQuickstop); Check("Duck in air",s.MiscAirDuck); });
            Panel("Camera",444,108,278,363,[&] { Check("Third person",s.MiscThirdperson); Slider("Camera distance",s.MiscThirdpersonDistance,30,200); });
        } else {
            const char* paths[] = {"havoc.cfg","havoc-profile-2.cfg","havoc-profile-3.cfg"};
            Panel("Configuration",152,108,278,363,[&] {
                Choice("Selected profile",profile,"Default\0Profile 2\0Profile 3\0"); ImGui::Spacing();
                if (Button("Save selected profile")) profileStatus = Config::Save(paths[profile]) ? "Profile saved" : "Save failed";
                if (Button("Load selected profile")) profileStatus = Config::Load(paths[profile]) ? "Profile loaded" : "Load failed";
                if (Button("Restore defaults")) { s = Config::Settings{}; s.menuOpened = true; profileStatus = "Defaults restored"; }
            });
            Panel("Profile details",444,108,278,363,[&] { ImGui::TextUnformatted(paths[profile]); ImGui::TextDisabled("Saved locally"); ImGui::Spacing(); ImGui::TextUnformatted(profileStatus); });
        }
        ImGui::PopID();
        draw->AddLine({p.x,p.y+481},{p.x+740,p.y+481},Grey(53));
        draw->PushClipRect({p.x+16,p.y+484},{p.x+600,p.y+508},true);
        draw->AddText({p.x+18,p.y+491},Grey(143),tab == 3 ? profileStatus : Ragebot::Status()); draw->PopClipRect();
        draw->AddText({p.x+636,p.y+491},Grey(122),"LOCAL / X86");
        ImGui::End(); ImGui::PopStyleVar();
    }

    void ExploitIndicators(float height)
    {
        const auto& s = Config::settings;
        if (!s.ExploitIndicators || (!s.DoubleTap && !s.HideShots)) return;
        auto* draw = ImGui::GetForegroundDrawList();
        float y = height*.58f;
        auto indicator = [&](const char* label,Exploits::Mode mode,bool suppressed) {
            const auto& state = Exploits::state;
            const bool ready = Exploits::available && !suppressed && state.mode == mode && state.Ready();
            const char* status = !Exploits::available ? "unavailable" : suppressed ? "standby"
                : state.mode != mode || state.paused ? "paused" : state.shifting || state.pending ? "shifting"
                : ready ? "ready" : state.capacity == 0 ? "server limit" : "charging";
            char text[64]; std::snprintf(text,sizeof(text),"%s  |  %s",label,status);
            const float width = ImGui::CalcTextSize(text).x+24;
            draw->AddRectFilled({20,y},{20+width,y+31},IM_COL32(33,33,33,235));
            draw->AddText({30,y+6},ready ? IM_COL32(235,235,235,255) : IM_COL32(170,170,170,255),text);
            const float charge = !suppressed && state.mode == mode && state.capacity > 0
                ? state.credit/static_cast<float>(state.capacity) : 0.f;
            draw->AddLine({20,y+30},{20+width,y+30},IM_COL32(55,55,55,255));
            draw->AddLine({20,y+30},{20+width*charge,y+30},IM_COL32(190,190,190,255),2.f);
            y += 38;
        };
        if (s.DoubleTap) indicator("DT",Exploits::Mode::DoubleTap,false);
        if (s.HideShots) indicator("HS",Exploits::Mode::HideShots,s.DoubleTap);
    }
    void Watermark(float width,float delta,float latency) {
        watermarkFade.Set(Config::settings.VisWatermark ? 1.f : 0.f,GetTickCount64());
        const float alpha = watermarkFade.Update(GetTickCount64());
        if (alpha <= .001f || width < 300) return;
        smoothedFps = Animation::Approach(smoothedFps,1/std::max(delta,.001f),delta,2);
        char text[96]{};
        std::snprintf(text,sizeof(text),"havoc  |  %d fps  |  %d ms",static_cast<int>(smoothedFps+.5f),std::isfinite(latency) ? static_cast<int>(std::clamp(latency,0.f,9999.f)+.5f) : 0);
        const ImVec2 size = ImGui::CalcTextSize(text); const float x = width-size.x-36,y = 16-(1-alpha)*6;
        auto draw = ImGui::GetForegroundDrawList();
        draw->AddRectFilled({x+2,y+2},{width-14,y+29},IM_COL32(0,0,0,static_cast<int>(70*alpha)));
        draw->AddRectFilled({x,y},{width-16,y+27},IM_COL32(33,33,33,static_cast<int>(245*alpha)));
        draw->AddLine({x,y+26},{width-16,y+26},IM_COL32(175,175,175,static_cast<int>(255*alpha)));
        draw->AddText({x+10,y+6},IM_COL32(200,200,200,static_cast<int>(255*alpha)),text);
    }
}
bool Menu::Visible() { return Config::settings.menuOpened || menuFade.Update(GetTickCount64()) > .001f || Config::settings.VisWatermark || watermarkFade.Update(GetTickCount64()) > .001f || (Config::settings.ExploitIndicators && (Config::settings.DoubleTap || Config::settings.HideShots)); }
bool Menu::BuildFrame(int width,int height,float delta,float latency) {
    Context(); auto& io = ImGui::GetIO(); io.DisplaySize = {static_cast<float>(width),static_cast<float>(height)}; io.DeltaTime = std::clamp(delta,.001f,.25f);
    io.KeyCtrl = io.KeysDown[VK_CONTROL]; io.KeyShift = io.KeysDown[VK_SHIFT]; io.KeyAlt = io.KeysDown[VK_MENU]; io.MouseDrawCursor = Config::settings.menuOpened;
    ImGui::NewFrame(); menuFade.Set(Config::settings.menuOpened ? 1.f : 0.f,GetTickCount64());
    const float alpha = menuFade.Update(GetTickCount64()); if (alpha > .001f) Layout(1);
    Watermark(static_cast<float>(width),io.DeltaTime,latency); ExploitIndicators(static_cast<float>(height)); ImGui::Render();
    // Custom reference widgets use literal colors. Fade their final vertices together.
    for (int i = 0; i < ImGui::GetDrawData()->CmdListsCount; ++i) {
        auto* list = ImGui::GetDrawData()->CmdLists[i];
        if (list->_OwnerName && (std::strcmp(list->_OwnerName,"##Foreground") == 0 || std::strcmp(list->_OwnerName,"##Background") == 0)) continue;
        for (auto& vertex : list->VtxBuffer) {
            const auto opacity = static_cast<ImU32>(((vertex.col>>IM_COL32_A_SHIFT)&255)*alpha);
            vertex.col = (vertex.col&~IM_COL32_A_MASK)|(opacity<<IM_COL32_A_SHIFT);
        }
    }
    return ImGui::GetDrawData()->TotalVtxCount > 0;
}
bool Menu::HandleMessage(UINT message,WPARAM wParam,LPARAM lParam) {
    if (message == WM_KEYDOWN && wParam == VK_INSERT && !(lParam&(1L<<30))) {
        Config::settings.menuOpened = !Config::settings.menuOpened; menuFade.Set(Config::settings.menuOpened ? 1.f : 0.f,GetTickCount64());
        if (context && !Config::settings.menuOpened) { ImGui::SetCurrentContext(context); auto& io = ImGui::GetIO(); std::memset(io.MouseDown,0,sizeof(io.MouseDown)); std::memset(io.KeysDown,0,sizeof(io.KeysDown)); ImGui::ClearActiveID(); }
        return true;
    }
    if (context && (message == WM_KILLFOCUS || message == WM_CAPTURECHANGED)) { ImGui::SetCurrentContext(context); auto& io = ImGui::GetIO(); std::memset(io.MouseDown,0,sizeof(io.MouseDown)); std::memset(io.KeysDown,0,sizeof(io.KeysDown)); }
    if (!Config::settings.menuOpened) return false;
    Context(); auto& io = ImGui::GetIO();
    if (message == WM_MOUSEMOVE || message == WM_LBUTTONDOWN || message == WM_LBUTTONUP || message == WM_RBUTTONDOWN || message == WM_RBUTTONUP) io.MousePos = {static_cast<float>(GET_X_LPARAM(lParam)),static_cast<float>(GET_Y_LPARAM(lParam))};
    switch (message) {
    case WM_LBUTTONDOWN: io.MouseDown[0] = true; return true;
    case WM_LBUTTONUP: io.MouseDown[0] = false; return true;
    case WM_RBUTTONDOWN: io.MouseDown[1] = true; return true;
    case WM_RBUTTONUP: io.MouseDown[1] = false; return true;
    case WM_MOUSEMOVE: return true;
    case WM_MOUSEWHEEL: io.MouseWheel += GET_WHEEL_DELTA_WPARAM(wParam)/static_cast<float>(WHEEL_DELTA); return true;
    case WM_KEYDOWN: case WM_SYSKEYDOWN: if (wParam < 256) io.KeysDown[wParam] = true; return true;
    case WM_KEYUP: case WM_SYSKEYUP: if (wParam < 256) io.KeysDown[wParam] = false; return true;
    case WM_CHAR: io.AddInputCharacterUTF16(static_cast<ImWchar16>(wParam)); return true;
    default: return false;
    }
}
void Menu::DeviceLost() { if (context && renderDevice) { ImGui::SetCurrentContext(context); ImGui_ImplDX9_InvalidateDeviceObjects(); } }
void Menu::Release() {
    if (!context) return; ImGui::SetCurrentContext(context); if (renderDevice) ImGui_ImplDX9_Shutdown();
    renderDevice = nullptr; ImGui::DestroyContext(context); context = nullptr; brandFont = nullptr; previousFrame = 0;
    motions.clear(); previousPage = -1; pageProgress = 1; navPosition = static_cast<float>(tab);
}
bool Menu::Render(IDirect3DDevice9* device,float latency) {
    if (!device || !Visible()) return false; Context();
    if (renderDevice != device) { if (renderDevice) ImGui_ImplDX9_Shutdown(); if (!ImGui_ImplDX9_Init(device)) return false; renderDevice = device; }
    ImGui_ImplDX9_NewFrame(); D3DVIEWPORT9 viewport{}; if (FAILED(device->GetViewport(&viewport))) return false;
    const auto now = GetTickCount64(); const float delta = previousFrame ? (now-previousFrame)/1000.f : 1/60.f; previousFrame = now;
    if (!BuildFrame(viewport.Width,viewport.Height,delta,latency)) return false;
    ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData()); return true;
}
void __cdecl HavocRenderMenu(IDirect3DDevice9* device) { Menu::Render(device); }
bool __cdecl HavocMenuMessage(UINT message,WPARAM wParam,LPARAM lParam) { return Menu::HandleMessage(message,wParam,lParam); }
void __cdecl HavocMenuDeviceLost() { Menu::DeviceLost(); }
