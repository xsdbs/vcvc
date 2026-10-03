#include "esp.h"
#include "hitfeedback.h"
#include "../sdk/game.h"
#include "../menu/surface.h"
#include "../core/config.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace
{
    unsigned long font = 0;
    void* fontSurface = nullptr;
    void Label(int center,int y,const char* text,DWORD color,unsigned long selected = 0)
    {
        if (!selected) selected = font;
        std::wstring wide;
        const int length = MultiByteToWideChar(CP_UTF8,0,text,-1,nullptr,0);
        if (length <= 0) return;
        wide.resize(length);
        MultiByteToWideChar(CP_UTF8,0,text,-1,wide.data(),length);
        wide.resize(length-1);
        int width = 0,height = 0;
        Surface::Method<void(__thiscall*)(void*,unsigned long,const wchar_t*,int&,int&)>(Surface::instance,79)(
            Surface::instance,selected,wide.c_str(),width,height);
        Surface::Text(center-width/2+1,y+1,text,color&0xff000000,selected);
        Surface::Text(center-width/2,y,text,color,selected);
    }
    void Outline(int x,int y,int w,int h,DWORD color)
    {
        Surface::Rectangle(x,y,w,1,color); Surface::Rectangle(x,y+h-1,w,1,color);
        Surface::Rectangle(x,y,1,h,color); Surface::Rectangle(x+w-1,y,1,h,color);
    }
}
void ESP::Render()
{
    if (!SDK::InGame() || !Surface::instance) { HitFeedback::Clear(); return; }
    SDK::Entity* local = SDK::Local();
    if (!local) return;
    if (fontSurface != Surface::instance) { fontSurface = Surface::instance; font = 0; }
    if (!font) font = Surface::CreateFont();
    if (!font) return;
    const Config::Settings& settings = Config::settings;
    const float now = SDK::globals->realTime;
    for (const HitFeedback::Hit& hit : HitFeedback::Active(now)) {
        SDK::Vector screen{};
        if (!SDK::WorldToScreen(hit.position,screen)) continue;
        const float fade = std::clamp((1.5f-(now-hit.last))/.5f,0.f,1.f);
        const DWORD color = (static_cast<DWORD>(fade*255)<<24)|0x00eeeeee;
        const int centerX = static_cast<int>(screen.x),centerY = static_cast<int>(screen.y);
        if (settings.VisWorldHitmarker) {
            for (int offset = 3; offset <= 8; ++offset) for (int side : {-1,1}) {
                Surface::Rectangle(centerX+offset,centerY+side*offset,1,1,color);
                Surface::Rectangle(centerX-offset,centerY+side*offset,1,1,color);
            }
        }
        if (settings.VisWorldDamage) {
            char number[24];
            std::snprintf(number,sizeof(number),"%d",static_cast<int>(std::round(hit.DisplayDamage(now))));
            Label(centerX,centerY-25-static_cast<int>(std::min(now-hit.born,1.5f)*12),number,color);
        }
    }
    if (!settings.VisEnableEnemy) return;
    const int maximum = SDK::MaxClients();
    for (int index = 1; index <= maximum; ++index) {
        SDK::Entity* player = SDK::Player(index);
        if (!player || player == local || !player->IsPlayer() || player->IsDormant() || player->IsDead()
            || player->m_iTeamNum() == local->m_iTeamNum()) continue;
        SDK::Vector minimum{},maximumBounds{};
        player->RenderBounds(minimum,maximumBounds);
        const SDK::Vector origin = player->RenderOrigin();
        float left = 1e9f,top = 1e9f,right = -1e9f,bottom = -1e9f;
        bool projected = true;
        for (int corner = 0; corner < 8; ++corner) {
            SDK::Vector screen{};
            const SDK::Vector point = origin+SDK::Vector{
                (corner&1) ? maximumBounds.x : minimum.x,(corner&2) ? maximumBounds.y : minimum.y,
                (corner&4) ? maximumBounds.z : minimum.z};
            if (!SDK::WorldToScreen(point,screen)) { projected = false; break; }
            left = std::min(left,screen.x); right = std::max(right,screen.x);
            top = std::min(top,screen.y); bottom = std::max(bottom,screen.y);
        }
        if (!projected) continue;
        const int x = static_cast<int>(left),y = static_cast<int>(top);
        const int width = static_cast<int>(right-left),height = static_cast<int>(bottom-top);
        if (width <= 0 || height <= 0) continue;
        if (settings.VisSkeletonEnemy) {
            for (const auto& segment : player->Skeleton()) {
                SDK::Vector from{},to{};
                if (!SDK::WorldToScreen(segment.from,from) || !SDK::WorldToScreen(segment.to,to)) continue;
                Surface::Line(static_cast<int>(from.x),static_cast<int>(from.y),
                    static_cast<int>(to.x),static_cast<int>(to.y),0xffeeeeee);
            }
        }
        if (settings.VisBoxEnemy) {
            Outline(x-1,y-1,width+2,height+2,0xff000000);
            Outline(x,y,width,height,0xffeeeeee);
            if (width > 2 && height > 2) Outline(x+1,y+1,width-2,height-2,0xff000000);
        }
        if (settings.VisNameEnemy) {
            SDK::PlayerInfo info{};
            if (SDK::PlayerDetails(index,info)) {
                info.name[127] = 0;
                Label(x+width/2,y-17,info.name,0xffeeeeee);
            }
        }
        if (settings.VisHealthEnemy) {
            const int health = std::clamp(player->m_iHealth(),0,100);
            const int fill = height*health/100;
            Surface::Rectangle(x-6,y-1,4,height+2,0xff000000);
            Surface::Rectangle(x-5,y+height-fill,2,fill,0xff73cd82);
            if (health < 100) {
                char number[12];
                std::snprintf(number,sizeof(number),"%d",player->m_iHealth());
                Surface::Text(x-27,y+height-fill-6,number,0xffeeeeee,font);
            }
        }
        SDK::Entity* weapon = player->Weapon();
        if (settings.VisFlagsEnemy) {
            int flagY = y;
            auto flag = [&](const char* text) { Surface::Text(x+width+5,flagY,text,0xffdddddd,font); flagY += 14; };
            if (player->m_ArmorValue() > 0) flag(player->m_bHasHelmet() ? "HK" : "K");
            if (player->Field<bool>(SDK::offsets.scoped)) flag("ZOOM");
            if (player->m_bGunGameImmunity()) flag("IMMUNE");
            if (!(player->Field<int>(SDK::offsets.flags)&1)) flag("AIR");
        }
        if (settings.VisDistanceEnemy) {
            char distance[24];
            std::snprintf(distance,sizeof(distance),"%.0f m",(origin-local->m_vecOrigin()).Length()*.0254f);
            Label(x+width/2,y+height+(settings.VisWeapTextEnemy && weapon ? 20 : 5),distance,0xffbcbcc5);
        }
        if (settings.VisWeapTextEnemy && weapon)
            Label(x+width/2,y+height+5,SDK::WeaponName(weapon),0xffbcbcc5);
        if (settings.VisAmmoEnemy && weapon) {
            const SDK::WeaponData* data = weapon->Data();
            if (data && data->maxClip > 0) {
                const float fraction = std::clamp(weapon->m_iClip1()/static_cast<float>(data->maxClip),0.f,1.f);
                Surface::Rectangle(x-1,y+height+1,width+2,4,0xff000000);
                Surface::Rectangle(x,y+height+2,static_cast<int>(width*fraction),2,0xff7f9cdf);
            }
        }
    }
    static bool logged = false;
    if (!logged) { logged = true; }
}
