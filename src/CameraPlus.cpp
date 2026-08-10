/**
 * CameraPlus.cpp — Tactical Camera + Pause + Disable Auto-Correction (v2.2)
 */

#include "stdafx.h"
#include "CameraPlus.h"
#include <windows.h>

static BYTE* g_camPos    = nullptr;
static BYTE* g_camOrient = nullptr;
static bool  g_freeCam  = false;
static bool  g_freeFly  = false;
static float g_flySpd   = 2.0f;
static float g_flySpdZ  = 2.0f;
static bool  g_paused   = false;
static float g_pauseSpd = 0.0001f;
static BYTE* g_pSpeedObj = nullptr;
static bool  g_noAutoCorrect = false;
static HANDLE g_flyThread = nullptr;
static volatile bool g_flyThreadStop = false;

// Кастомные хоткеи (из .ini)
static int g_hkFreeCam = VK_F4;
static int g_hkPause   = VK_NUMPAD0;
static int g_hkSpeedUp = VK_ADD;
static int g_hkSpeedDn = VK_SUBTRACT;

// ═══ HУК 1: ЗАХВАТ КАМЕРЫ ═══

static LPBYTE pHkCam1, oHkCam1;

void __declspec(naked) HCam1()
{
    __asm
    {
        mov [g_camPos], esi
        movss xmm5, [esi + 0x14]
        jmp oHkCam1
    }
}

static LPBYTE pHkCam2, oHkCam2;

void __declspec(naked) HCam2()
{
    __asm
    {
        mov [g_camOrient], esi
        movss xmm1, [esi + 0x114]
        jmp oHkCam2
    }
}

// ═══ HУК 3: DETACH ═══

static LPBYTE pHkDetach, oHkDetach;

void __declspec(naked) HDetach()
{
    __asm
    {
        cmp byte ptr [g_freeCam], 1
        je skipUpdate
        movss xmm0, [ecx + 0x20]
        addss xmm0, [ecx + 0x10]
        movss dword ptr [ecx + 0x10], xmm0
        movss xmm0, [ecx + 0x24]
        addss xmm0, [ecx + 0x14]
        movss dword ptr [ecx + 0x14], xmm0
        movss xmm0, [ecx + 0x28]
        addss xmm0, [ecx + 0x18]
        movss dword ptr [ecx + 0x18], xmm0
    skipUpdate:
        ret 4
    }
}

// ═══ HУК 4: ПАУЗА ═══

static LPBYTE pHkSpeed, oHkSpeed;

void __declspec(naked) HSpeed()
{
    __asm
    {
        mov [g_pSpeedObj], edx
        cmp byte ptr [g_paused], 1
        je doPause
        movss xmm1, [edx + 0x24]
        jmp oHkSpeed
    doPause:
        movss xmm1, [g_pauseSpd]
        movss dword ptr [edx + 0x24], xmm1
        jmp oHkSpeed
    }
}

// ═══ DISABLE AUTO-CORRECTION ═══

static LPBYTE pAutoV, pAutoH;
static BYTE  origAutoV[7], origAutoH[7];

void ApplyAutoCorrect(bool disable)
{
    DWORD old;
    if (pAutoV) {
        VirtualProtect(pAutoV, 7, PAGE_EXECUTE_READWRITE, &old);
        if (disable) {
            pAutoV[0]=0x83; pAutoV[1]=0xFE; pAutoV[2]=0x00;
            pAutoV[3]=0x90; pAutoV[4]=0x90; pAutoV[5]=0x90; pAutoV[6]=0x90;
        } else memcpy(pAutoV, origAutoV, 7);
        VirtualProtect(pAutoV, 7, old, &old);
    }
    if (pAutoH) {
        VirtualProtect(pAutoH, 7, PAGE_EXECUTE_READWRITE, &old);
        if (disable) {
            pAutoH[0]=0x83; pAutoH[1]=0xFA; pAutoH[2]=0x00;
            pAutoH[3]=0x90; pAutoH[4]=0x90; pAutoH[5]=0x90; pAutoH[6]=0x90;
        } else memcpy(pAutoH, origAutoH, 7);
        VirtualProtect(pAutoH, 7, old, &old);
    }
}

// ═══ ПОТОК ═══

static DWORD WINAPI FlyThread(LPVOID)
{
    bool f4w=false, n0w=false, plw=false, mnw=false;
    while (!g_flyThreadStop) {
        Sleep(5);
        bool f4 = (GetAsyncKeyState(g_hkFreeCam)&0x8000) && !(GetAsyncKeyState(VK_MENU)&0x8000);
        if (f4 && !f4w) { g_freeCam=!g_freeCam; g_freeFly=g_freeCam; }
        f4w=f4;
        bool n0 = GetAsyncKeyState(g_hkPause)&0x8000;
        if (n0 && !n0w) { g_paused=!g_paused; if(!g_paused&&g_pSpeedObj)*(float*)(g_pSpeedObj+0x24)=1.0f; }
        n0w=n0;
        bool pl = GetAsyncKeyState(g_hkSpeedUp)&0x8000;
        if (pl&&!plw) { float s=g_pauseSpd*2.0f; if(s>1.0f)s=1.0f; g_pauseSpd=s; }
        plw=pl;
        bool mn = GetAsyncKeyState(g_hkSpeedDn)&0x8000;
        if (mn&&!mnw) { float s=g_pauseSpd/2.0f; if(s<0.00001f)s=0.00001f; g_pauseSpd=s; }
        mnw=mn;
        if (!g_freeFly||!g_freeCam) continue;
        if (!g_camPos||!g_camOrient) continue;
        if ((DWORD)g_camPos<0x10000||(DWORD)g_camOrient<0x10000) continue;
        float& X=*(float*)(g_camPos+0x10);
        float& Z=*(float*)(g_camPos+0x14);
        float& Y=*(float*)(g_camPos+0x18);
        float s=*(float*)(g_camOrient+0xB0);
        float c=*(float*)(g_camOrient+0x110);
        if (GetAsyncKeyState(VK_UP)   &0x8000) { X+=s*g_flySpd; Y-=c*g_flySpd; }
        if (GetAsyncKeyState(VK_DOWN) &0x8000) { X-=s*g_flySpd; Y+=c*g_flySpd; }
        if (GetAsyncKeyState(VK_LEFT) &0x8000) { X-=c*g_flySpd; Y-=s*g_flySpd; }
        if (GetAsyncKeyState(VK_RIGHT)&0x8000) { X+=c*g_flySpd; Y+=s*g_flySpd; }
        if (GetAsyncKeyState(VK_PRIOR)&0x8000) Z+=g_flySpdZ;
        if (GetAsyncKeyState(VK_NEXT) &0x8000) Z-=g_flySpdZ;
    }
    return 0;
}

// ═══ UI ═══

void RenderCameraUI()
{
    if (!ImGui::CollapsingHeader("Camera Plus")) return;
    ImGui::PushID("CamPlus");

    if (ImGui::Checkbox("Tactical Camera (F4)", &g_freeCam))
        config.setBool("camera","freeCam",g_freeCam);
    ImGui::TextWrapped("Freezes in place, tracks player. Arrows to reposition.");

    if (ImGui::Checkbox("Manual Fly (arrows/PgUp/PgDn)", &g_freeFly))
        config.setBool("camera","freeFly",g_freeFly);

    ImGui::PushItemWidth(150);
    ImGui::SliderFloat("Speed XY", &g_flySpd, 0.5f, 30.0f, "%.1f");
    ImGui::SliderFloat("Speed Z", &g_flySpdZ, 0.5f, 30.0f, "%.1f");
    ImGui::PopItemWidth();

    if (g_camPos) ImGui::Text("X=%.0f Z=%.0f Y=%.0f",
        *(float*)(g_camPos+0x10),*(float*)(g_camPos+0x14),*(float*)(g_camPos+0x18));

    ImGui::Separator();

    ImGui::TextColored(ImVec4(0.6f,0.9f,1,1),"Camera Behavior");
    bool wasAuto = g_noAutoCorrect;
    if (ImGui::Checkbox("Disable Auto-Correction", &g_noAutoCorrect)) {
        config.setBool("camera","noAutoCorrect",g_noAutoCorrect);
        if (wasAuto != g_noAutoCorrect) ApplyAutoCorrect(g_noAutoCorrect);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Camera won't auto-return behind player.\nIn F4 mode creates 'tracking' effect.");
    ImGui::SameLine();
    ImGui::TextDisabled("(dinp8)");

    ImGui::Separator();

    if (ImGui::Checkbox("Pause (Num0)", &g_paused)) {
        config.setBool("camera","pause",g_paused);
        if (!g_paused && g_pSpeedObj) *(float*)(g_pSpeedObj+0x24)=1.0f;
    }
    ImGui::PushItemWidth(200);
    ImGui::SliderFloat("Speed", &g_pauseSpd, 0.00001f, 1.0f, "%.5f", 3.0f);
    ImGui::PopItemWidth();
    if (g_paused) ImGui::TextColored(ImVec4(1,0.5f,0.3f,1),"PAUSED");
    ImGui::PopID();
}

// ═══ INIT ═══

void Hooks::CameraPlus()
{
    g_freeCam  = config.getBool("camera","freeCam",false);
    g_freeFly  = config.getBool("camera","freeFly",false);
    g_flySpd   = config.getFloat("camera","flySpeed",2.0f);
    g_flySpdZ  = config.getFloat("camera","flySpeedZ",2.0f);
    g_paused   = config.getBool("camera","pause",false);
    g_pauseSpd = config.getFloat("camera","pauseSpeed",0.0001f);
    g_noAutoCorrect = config.getBool("camera","noAutoCorrect",false);

    // Кастомные хоткеи из .ini (camera_keys)
    g_hkFreeCam = config.getUInt("camera_keys","freeCam", VK_F4) & 0xFF;
    g_hkPause   = config.getUInt("camera_keys","pause",   VK_NUMPAD0) & 0xFF;
    g_hkSpeedUp = config.getUInt("camera_keys","speedUp", VK_ADD) & 0xFF;
    g_hkSpeedDn = config.getUInt("camera_keys","speedDn", VK_SUBTRACT) & 0xFF;

    BYTE s1[]={0xF3,0x0F,0x10,0x6E,0x14,0x0F,0xC6,0xED,0x00,0xF3,0x0F,0x11,0x84,0x24,0xCC,0x00,0x00,0x00};
    if (Hooks::FindSignature("Cam1",s1,&pHkCam1)) {
        Hooks::CreateHook("Cam1",pHkCam1,HCam1,(LPVOID*)&oHkCam1,true); oHkCam1=pHkCam1+5;
    }

    BYTE s2[]={0xF3,0x0F,0x10,0x8E,0x14,0x01,0x00,0x00,0xF3,0x0F,0x10,0x86,0x10,0x01,0x00,0x00,0xF3,0x0F,0x10,0xA6,0x50,0x01,0x00,0x00};
    if (Hooks::FindSignature("Cam2",s2,&pHkCam2)) {
        Hooks::CreateHook("Cam2",pHkCam2,HCam2,(LPVOID*)&oHkCam2,true); oHkCam2=pHkCam2+8;
    }

    BYTE sd[]={0xF3,0x0F,0x10,0x41,0x20,0xF3,0x0F,0x58,0x41,0x10,0xF3,0x0F,0x11,0x41,0x10,0xF3,0x0F,0x10,0x41,0x24,0xF3,0x0F,0x58,0x41,0x14,0xF3,0x0F,0x11,0x41,0x14,0xF3,0x0F,0x10,0x41,0x28,0xF3,0x0F,0x58,0x41,0x18,0xF3,0x0F,0x11,0x41,0x18,0xC2,0x04,0x00};
    if (Hooks::FindSignature("Detach",sd,&pHkDetach))
        Hooks::CreateHook("Detach",pHkDetach,HDetach,(LPVOID*)&oHkDetach,true);

    BYTE ss[]={0xF3,0x0F,0x10,0x4A,0x24,0xF3,0x0F,0x59,0x49,0x70};
    if (Hooks::FindSignature("Speed",ss,&pHkSpeed)) {
        Hooks::CreateHook("Speed",pHkSpeed,HSpeed,(LPVOID*)&oHkSpeed,true); oHkSpeed=pHkSpeed+5;
    }

    BYTE sigV[]={0x80,0xBE,0xF0,0x02,0x00,0x00,0x00,0x0F,0x85,0xCC,0xCC,0x00,0x00};
    if (Hooks::FindSignature("AutoV",sigV,&pAutoV)) memcpy(origAutoV,pAutoV,7);

    BYTE sigH[]={0x80,0xBA,0xF1,0x02,0x00,0x00,0x00,0x0F,0x85,0xCC,0xCC,0x00,0x00};
    if (Hooks::FindSignature("AutoH",sigH,&pAutoH)) memcpy(origAutoH,pAutoH,7);

    if (g_noAutoCorrect) ApplyAutoCorrect(true);

    g_flyThreadStop=false;
    g_flyThread=CreateThread(0,0,FlyThread,0,0,0);
    InGameUIAdd(RenderCameraUI);

    logFile << "CameraPlus v2.2: detach=" << (pHkDetach?1:0)
            << " speed=" << (pHkSpeed?1:0)
            << " autoV=" << (pAutoV?1:0) << " autoH=" << (pAutoH?1:0)
            << " freeCam=" << g_freeCam << std::endl;
}

void Hooks::CameraPlusShutdown()
{
    g_flyThreadStop=true;
    if (g_flyThread){WaitForSingleObject(g_flyThread,100);CloseHandle(g_flyThread);}
    if (g_paused&&g_pSpeedObj)*(float*)(g_pSpeedObj+0x24)=1.0f;
    if (g_noAutoCorrect) ApplyAutoCorrect(false);
}
