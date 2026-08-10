/**
 * TargetLock.cpp — Auto-aim: доворачивает ГГ по вектору камеры при атаке
 *
 * КАК РАБОТАЕТ:
 *   1. Хукаем Yaw write (DDDA.exe+697579: fstp [esi+0x14])
 *   2. Хукаем камеру для захвата sin/cos (те же сигнатуры что CameraPlus)
 *   3. Alt+X — захват физики ГГ (один раз за сессию)
 *   4. При атаке (ЛКМ/ПКМ) пишем направление камеры в [playerPhys+0x14]
 *
 * Хоткеи:
 *   Alt+X — захватить физику ГГ
 *   F5    — toggle автонаведения
 */

#include "stdafx.h"
#include "TargetLock.h"
#include <windows.h>

// ═══════════════════════════════════════
// СОСТОЯНИЕ
// ═══════════════════════════════════════

static BYTE* g_playerPhys  = nullptr;  // ESI физики ГГ (Alt+X)
static BYTE* g_camOrient   = nullptr;  // из хука cameraBase2
static float g_lastYaw     = 0.0f;
static bool  g_autoAimOn   = false;
static bool  g_physReady   = false;

static volatile bool g_threadStop = false;
static HANDLE g_atkThread = nullptr;
static HANDLE g_hkThread  = nullptr;

// ═══════════════════════════════════════
// ХУК 1: Yaw write (fstp [esi+0x14])
// ═══════════════════════════════════════

static LPBYTE pYawHook, oYawHook;

void __declspec(naked) HYawWrite()
{
    __asm
    {
        cmp [g_playerPhys], 0
        jne check_phys
        mov [g_playerPhys], esi
    check_phys:
        cmp esi, [g_playerPhys]
        jne do_orig

        fstp dword ptr [esi + 0x14]
        mov eax, [esi + 0x14]
        mov [g_lastYaw], eax
        jmp done

    do_orig:
        fstp dword ptr [esi + 0x14]
    done:
        jmp oYawHook
    }
}

// ═══════════════════════════════════════
// ХУК 2: Захват ориентации камеры
// ═══════════════════════════════════════

static LPBYTE pCamHook2, oCamHook2;

void __declspec(naked) HCamOrient()
{
    __asm
    {
        mov [g_camOrient], esi
        movss xmm1, [esi + 0x114]
        jmp oCamHook2
    }
}

// ═══════════════════════════════════════
// АВТОНАВЕДЕНИЕ
// ═══════════════════════════════════════

void ApplyAutoAim()
{
    if (!g_autoAimOn || !g_physReady || !g_playerPhys || !g_camOrient) return;

    float sn = *(float*)(g_camOrient + 0xB0);
    float cs = *(float*)(g_camOrient + 0x110);
    float yaw = atan2f(sn, cs);

    *(float*)(g_playerPhys + 0x14) = yaw;
}

// ═══════════════════════════════════════
// ЗАХВАТ ФИЗИКИ ГГ
// ═══════════════════════════════════════

void CapturePhys()
{
    if (g_playerPhys && (DWORD)g_playerPhys > 0x01000000)
    {
        g_physReady = true;
        logFile << "TargetLock: physics at 0x" << std::hex
                << (DWORD)g_playerPhys << std::dec << std::endl;
    }
    else
    {
        logFile << "TargetLock: not ready, move and retry" << std::endl;
    }
}

void ToggleAutoAim()
{
    g_autoAimOn = !g_autoAimOn;
    logFile << "TargetLock: auto-aim " << (g_autoAimOn ? "ON" : "OFF") << std::endl;
}

// ═══════════════════════════════════════
// ПОТОКИ
// ═══════════════════════════════════════

static DWORD WINAPI AttackWatcher(LPVOID)
{
    bool lmb_was = false;
    while (!g_threadStop)
    {
        Sleep(5);
        bool lmb = GetAsyncKeyState(VK_LBUTTON) & 0x8000;
        bool rmb = GetAsyncKeyState(VK_RBUTTON) & 0x8000;
        if ((lmb && !lmb_was) || rmb) ApplyAutoAim();
        lmb_was = lmb;
    }
    return 0;
}

static DWORD WINAPI HotkeyThread(LPVOID)
{
    bool ax_was = false, f5_was = false;
    while (!g_threadStop)
    {
        Sleep(50);
        bool ax = (GetAsyncKeyState(VK_MENU) & 0x8000)
               && (GetAsyncKeyState('X') & 0x8000);
        if (ax && !ax_was) CapturePhys();
        ax_was = ax;

        bool f5 = (GetAsyncKeyState(VK_DELETE) & 0x8000) || (GetAsyncKeyState(VK_DECIMAL) & 0x8000); // был F5, теперь NumPad Del
        if (f5 && !f5_was) ToggleAutoAim();
        f5_was = f5;
    }
    return 0;
}

// ═══════════════════════════════════════
// UI
// ═══════════════════════════════════════

void RenderTargetUI()
{
    if (!ImGui::CollapsingHeader("Target Lock (Auto-Aim)"))
        return;
    ImGui::PushID("TL");

    if (g_physReady)
        ImGui::TextColored(ImVec4(0.3f,1,0.3f,1), "Physics CAPTURED");
    else
        ImGui::TextColored(ImVec4(1,0.5f,0.3f,1), "Press Alt+X to capture (auto)");

    if (ImGui::Button("Capture (Alt+X)")) CapturePhys();

    ImGui::Separator();

    if (ImGui::Checkbox("Auto-Aim (NumPad Del)", &g_autoAimOn))
        config.setBool("targetLock","autoAim", g_autoAimOn);

    ImGui::TextWrapped("Snaps melee attacks to camera direction. LMB/RMB.");

    if (g_playerPhys)
        ImGui::Text("Phys: 0x%X  Yaw: %.2f", (DWORD)g_playerPhys, g_lastYaw);

    ImGui::PopID();
}

// ═══════════════════════════════════════
// INIT
// ═══════════════════════════════════════

static LPBYTE pYawSig, pCamSig2;

void Hooks::TargetLock()
{
    g_autoAimOn = config.getBool("targetLock","autoAim", false); // хоткей Numpad Del

    // Yaw: D9 5E 14 (fstp [esi+14])
    BYTE sY[] = { 0xD9, 0x5E, 0x14 };
    bool okY = Hooks::FindSignature("Yaw", sY, &pYawSig);
    if (okY) {
        pYawHook = pYawSig;
        Hooks::CreateHook("Yaw", pYawHook, HYawWrite, (LPVOID*)&oYawHook, true);
        oYawHook = pYawHook + 3;
    }

    // Cam2: та же сигнатура что в CameraPlus
    BYTE sC[] = { 0xF3,0x0F,0x10,0x8E,0x14,0x01,0x00,0x00,
                  0xF3,0x0F,0x10,0x86,0x10,0x01,0x00,0x00,
                  0xF3,0x0F,0x10,0xA6,0x50,0x01,0x00,0x00 };
    bool okC = Hooks::FindSignature("Cam2TL", sC, &pCamSig2);
    if (okC) {
        pCamHook2 = pCamSig2;
        Hooks::CreateHook("Cam2TL", pCamHook2, HCamOrient, (LPVOID*)&oCamHook2, true);
        oCamHook2 = pCamHook2 + 8;
    }

    g_threadStop = false;
    g_atkThread = CreateThread(0,0,AttackWatcher,0,0,0);
    g_hkThread  = CreateThread(0,0,HotkeyThread,0,0,0);

    InGameUIAdd(RenderTargetUI);

    logFile << "TargetLock init: yaw=" << okY << " cam=" << okC << std::endl;
}

void Hooks::TargetLockShutdown()
{
    g_threadStop = true;
    if (g_atkThread) { WaitForSingleObject(g_atkThread,200); CloseHandle(g_atkThread); }
    if (g_hkThread)  { WaitForSingleObject(g_hkThread,200);  CloseHandle(g_hkThread); }
}
