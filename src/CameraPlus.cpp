/**
 * CameraPlus.cpp — Tactical Camera + Pause (v2.1)
 *
 * F4 = Тактическая камера: застывает в пространстве, следит за ГГ.
 *      Стрелки/PgUp/PgDn — двигать камеру вручную.
 * Num0 = Пауза, Num+/Num- = скорость.
 *
 * Все хоткеи через GetAsyncKeyState в FlyThread.
 * Alt+F4 работает нормально (Alt блокирует F4).
 */

#include "stdafx.h"
#include "CameraPlus.h"
#include <windows.h>

// ═══════════════════════════════════
// СОСТОЯНИЕ
// ═══════════════════════════════════

static BYTE* g_camPos    = nullptr;
static BYTE* g_camOrient = nullptr;

static bool  g_freeCam  = false;
static bool  g_freeFly  = false;
static float g_flySpd   = 2.0f;
static float g_flySpdZ  = 2.0f;

static bool  g_paused   = false;
static float g_pauseSpd = 0.0001f;
static BYTE* g_pSpeedObj = nullptr;

static HANDLE g_flyThread = nullptr;
static volatile bool g_flyThreadStop = false;

// ═══════════════════════════════════
// ХУК 1: ЗАХВАТ КАМЕРЫ (ESI → позиция)
// ═══════════════════════════════════

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

// ═══════════════════════════════════
// ХУК 2: ЗАХВАТ КАМЕРЫ (ESI → ориентация)
// ═══════════════════════════════════

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

// ═══════════════════════════════════
// ХУК 3: DETACH (всегда активен, решение внутри по флагу)
// ═══════════════════════════════════

static LPBYTE pHkDetach, oHkDetach;

void __declspec(naked) HDetach()
{
    __asm
    {
        cmp byte ptr [g_freeCam], 1
        je skipUpdate

        movss xmm0, [ecx + 0x20]
        addss xmm0, [ecx + 0x10]
        movss [ecx + 0x10], xmm0
        movss xmm0, [ecx + 0x24]
        addss xmm0, [ecx + 0x14]
        movss [ecx + 0x14], xmm0
        movss xmm0, [ecx + 0x28]
        addss xmm0, [ecx + 0x18]
        movss [ecx + 0x18], xmm0

    skipUpdate:
        ret 4
    }
}

// ═══════════════════════════════════
// ХУК 4: ПАУЗА
// ═══════════════════════════════════

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

// ═══════════════════════════════════
// ПОТОК: ХОТКЕИ + FLY
// ═══════════════════════════════════

static DWORD WINAPI FlyThread(LPVOID)
{
    bool f4w = false, n0w = false, plw = false, mnw = false;

    while (!g_flyThreadStop)
    {
        Sleep(5);

        // F4 (Alt блокирует — Alt+F4 работает!)
        bool f4 = (GetAsyncKeyState(VK_F4) & 0x8000)
               && !(GetAsyncKeyState(VK_MENU) & 0x8000);
        if (f4 && !f4w) {
            g_freeCam = !g_freeCam;
            g_freeFly = g_freeCam;
        }
        f4w = f4;

        // Num0 — пауза
        bool n0 = GetAsyncKeyState(VK_NUMPAD0) & 0x8000;
        if (n0 && !n0w) {
            g_paused = !g_paused;
            if (!g_paused && g_pSpeedObj)
                *(float*)(g_pSpeedObj + 0x24) = 1.0f;
        }
        n0w = n0;

        // Num+ — быстрее
        bool pl = GetAsyncKeyState(VK_ADD) & 0x8000;
        if (pl && !plw) {
            float s = g_pauseSpd * 2.0f;
            if (s > 1.0f) s = 1.0f;
            g_pauseSpd = s;
        }
        plw = pl;

        // Num- — медленнее
        bool mn = GetAsyncKeyState(VK_SUBTRACT) & 0x8000;
        if (mn && !mnw) {
            float s = g_pauseSpd / 2.0f;
            if (s < 0.00001f) s = 0.00001f;
            g_pauseSpd = s;
        }
        mnw = mn;

        // FLY
        if (!g_freeFly || !g_freeCam) continue;
        if (!g_camPos || !g_camOrient) continue;
        if ((DWORD)g_camPos < 0x10000 || (DWORD)g_camOrient < 0x10000) continue;

        float& X = *(float*)(g_camPos + 0x10);
        float& Z = *(float*)(g_camPos + 0x14);
        float& Y = *(float*)(g_camPos + 0x18);
        float  s = *(float*)(g_camOrient + 0xB0);
        float  c = *(float*)(g_camOrient + 0x110);

        if (GetAsyncKeyState(VK_UP)    & 0x8000) { X += s * g_flySpd; Y -= c * g_flySpd; }
        if (GetAsyncKeyState(VK_DOWN)  & 0x8000) { X -= s * g_flySpd; Y += c * g_flySpd; }
        if (GetAsyncKeyState(VK_LEFT)  & 0x8000) { X -= c * g_flySpd; Y -= s * g_flySpd; }
        if (GetAsyncKeyState(VK_RIGHT) & 0x8000) { X += c * g_flySpd; Y += s * g_flySpd; }
        if (GetAsyncKeyState(VK_PRIOR) & 0x8000) { Z += g_flySpdZ; }
        if (GetAsyncKeyState(VK_NEXT)  & 0x8000) { Z -= g_flySpdZ; }
    }
    return 0;
}

// ═══════════════════════════════════
// UI
// ═══════════════════════════════════

void RenderCameraUI()
{
    if (!ImGui::CollapsingHeader("Camera Plus"))
        return;
    ImGui::PushID("CamPlus");

    if (ImGui::Checkbox("Tactical Camera (F4)", &g_freeCam))
        config.setBool("camera", "freeCam", g_freeCam);
    ImGui::TextWrapped("Freezes in place, tracks player. Arrows to reposition.");

    if (ImGui::Checkbox("Manual Fly (arrows/PgUp/PgDn)", &g_freeFly))
        config.setBool("camera", "freeFly", g_freeFly);

    ImGui::PushItemWidth(150.0f);
    if (ImGui::SliderFloat("Speed XY", &g_flySpd, 0.5f, 30.0f, "%.1f"))
        config.setFloat("camera", "flySpeed", g_flySpd);
    if (ImGui::SliderFloat("Speed Z", &g_flySpdZ, 0.5f, 30.0f, "%.1f"))
        config.setFloat("camera", "flySpeedZ", g_flySpdZ);
    ImGui::PopItemWidth();

    if (g_camPos)
        ImGui::Text("X=%.0f Z=%.0f Y=%.0f",
            *(float*)(g_camPos + 0x10), *(float*)(g_camPos + 0x14), *(float*)(g_camPos + 0x18));

    ImGui::Separator();

    if (ImGui::Checkbox("Pause (Num0)", &g_paused)) {
        config.setBool("camera", "pause", g_paused);
        if (!g_paused && g_pSpeedObj)
            *(float*)(g_pSpeedObj + 0x24) = 1.0f;
    }
    ImGui::PushItemWidth(200.0f);
    ImGui::SliderFloat("Speed", &g_pauseSpd, 0.00001f, 1.0f, "%.5f", 3.0f);
    ImGui::PopItemWidth();
    if (g_paused)
        ImGui::TextColored(ImVec4(1, 0.5f, 0.3f, 1), "PAUSED");

    ImGui::PopID();
}

// ═══════════════════════════════════
// INIT
// ═══════════════════════════════════

void Hooks::CameraPlus()
{
    g_freeCam  = config.getBool("camera", "freeCam", false);
    g_freeFly  = config.getBool("camera", "freeFly", false);
    g_flySpd   = config.getFloat("camera", "flySpeed", 2.0f);
    g_flySpdZ  = config.getFloat("camera", "flySpeedZ", 2.0f);
    g_paused   = config.getBool("camera", "pause", false);
    g_pauseSpd = config.getFloat("camera", "pauseSpeed", 0.0001f);

    // --- Cam1 ---
    BYTE s1[] = {
        0xF3, 0x0F, 0x10, 0x6E, 0x14, 0x0F, 0xC6, 0xED, 0x00,
        0xF3, 0x0F, 0x11, 0x84, 0x24, 0xCC, 0x00, 0x00, 0x00
    };
    if (Hooks::FindSignature("Cam1", s1, &pHkCam1)) {
        Hooks::CreateHook("Cam1", pHkCam1, HCam1, (LPVOID*)&oHkCam1, true);
        oHkCam1 = pHkCam1 + 5;
    }

    // --- Cam2 ---
    BYTE s2[] = {
        0xF3, 0x0F, 0x10, 0x8E, 0x14, 0x01, 0x00, 0x00,
        0xF3, 0x0F, 0x10, 0x86, 0x10, 0x01, 0x00, 0x00,
        0xF3, 0x0F, 0x10, 0xA6, 0x50, 0x01, 0x00, 0x00
    };
    if (Hooks::FindSignature("Cam2", s2, &pHkCam2)) {
        Hooks::CreateHook("Cam2", pHkCam2, HCam2, (LPVOID*)&oHkCam2, true);
        oHkCam2 = pHkCam2 + 8;
    }

    // --- Detach (всегда активен) ---
    BYTE sd[] = {
        0xF3, 0x0F, 0x10, 0x41, 0x20, 0xF3, 0x0F, 0x58, 0x41, 0x10,
        0xF3, 0x0F, 0x11, 0x41, 0x10, 0xF3, 0x0F, 0x10, 0x41, 0x24,
        0xF3, 0x0F, 0x58, 0x41, 0x14, 0xF3, 0x0F, 0x11, 0x41, 0x14,
        0xF3, 0x0F, 0x10, 0x41, 0x28, 0xF3, 0x0F, 0x58, 0x41, 0x18,
        0xF3, 0x0F, 0x11, 0x41, 0x18, 0xC2, 0x04, 0x00
    };
    bool okD = Hooks::FindSignature("Detach", sd, &pHkDetach);
    if (okD)
        Hooks::CreateHook("Detach", pHkDetach, HDetach, (LPVOID*)&oHkDetach, true);

    // --- Speed ---
    BYTE ss[] = { 0xF3, 0x0F, 0x10, 0x4A, 0x24, 0xF3, 0x0F, 0x59, 0x49, 0x70 };
    if (Hooks::FindSignature("Speed", ss, &pHkSpeed)) {
        Hooks::CreateHook("Speed", pHkSpeed, HSpeed, (LPVOID*)&oHkSpeed, true);
        oHkSpeed = pHkSpeed + 5;
    }

    // --- Поток ---
    g_flyThreadStop = false;
    g_flyThread = CreateThread(0, 0, FlyThread, 0, 0, 0);

    InGameUIAdd(RenderCameraUI);

    logFile << "CameraPlus v2.1: detach=" << (okD ? 1 : 0)
            << " speed=" << (pHkSpeed ? 1 : 0)
            << " freeCam=" << g_freeCam << " pause=" << g_paused << std::endl;
}

void Hooks::CameraPlusShutdown()
{
    g_flyThreadStop = true;
    if (g_flyThread) {
        WaitForSingleObject(g_flyThread, 100);
        CloseHandle(g_flyThread);
        g_flyThread = nullptr;
    }
    if (g_paused && g_pSpeedObj)
        *(float*)(g_pSpeedObj + 0x24) = 1.0f;
}
