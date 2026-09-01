#include "stdafx.h"
#include "OrderWatch.h"
#include "PawnAI_Common.h"
#include "../runtime/Runtime.h"
#include "../runtime/MemProbe.h"
#include "../runtime/MonsterTempo.h"
#include "../CombatBus.h"
#include <stdio.h>
#include <math.h>

namespace PawnAI {
namespace OrderWatch {

using Runtime::Mem::Rd;
using Runtime::Mem::RdPtr;
using Runtime::Mem::WrSafe;
using Runtime::Mem::NameOfLiveObjectSafe;

static bool      s_enabled = true;
static Stats     s_stats = {};
static bool      s_inOrder = false;
static DWORD     s_orderStartMs = 0;
static OrderType s_activeOrder = ORDER_NONE;
static DWORD     s_orderTimestampMs = 0;
static DWORD     s_orderDurationMs = 6000; // 6 секунд затухания
static uintptr_t s_focusTarget = 0;
static char      s_focusTargetKind[32] = {};

static float Dist3D(float ax, float ay, float az, float bx, float by, float bz)
{
    float dx = ax - bx, dy = ay - by, dz = az - bz;
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

static uintptr_t FindTargetInPlayerAim(float ax, float ay, float az, char* outKind, int kindCap)
{
    if (outKind && kindCap > 0) outKind[0] = 0;
    const WorldReport w = CombatBus::Instance().LastWorld();
    uintptr_t bestBody = 0;
    float bestDist = 1e9f;

    for (int i = 0; i < w.count; ++i) {
        const WorldPresence& u = w.units[i];
        if (!u.ptr || !u.kind || !Runtime::KindIsEnemy(u.kind)) continue;
        if (strstr(u.actName, "Die") || strstr(u.actName, "Dead")) continue;

        float dx = u.x - ax, dy = u.y - ay, dz = u.z - az;
        float d = sqrtf(dx * dx + dy * dy + dz * dz) / 100.0f;

        // Ближайший враг в активном секторе боя (< 35 м)
        if (d <= 35.0f && d < bestDist) {
            bestDist = d;
            bestBody = u.ptr;
            if (outKind && kindCap > 0) {
                lstrcpynA(outKind, u.kind, kindCap);
            }
        }
    }
    return bestBody;
}

void Init()
{
    memset(&s_stats, 0, sizeof(s_stats));
    s_inOrder = false;
    s_orderStartMs = 0;
    s_activeOrder = ORDER_NONE;
    s_orderTimestampMs = 0;
    s_focusTarget = 0;
    s_focusTargetKind[0] = 0;
    s_enabled = true;

    logFile << "OrderWatch: tactical D-pad command system initialized (Come/Go/Help with smooth decay)" << std::endl;
}

void Shutdown()
{
    s_activeOrder = ORDER_NONE;
    s_inOrder = false;
}

void SetEnabled(bool on)
{
    s_enabled = on;
    if (!on) Shutdown();
}

Stats GetStats()
{
    Stats st = s_stats;
    st.enabled = s_enabled;
    st.activeOrder = s_activeOrder;
    const DWORD now = GetTickCount();
    st.orderRemainingMs = (s_activeOrder != ORDER_NONE && (now - s_orderTimestampMs < s_orderDurationMs))
        ? (s_orderDurationMs - (now - s_orderTimestampMs)) : 0;
    st.focusTargetBody = s_focusTarget;
    lstrcpynA(st.focusTargetKind, s_focusTargetKind, sizeof(st.focusTargetKind));
    return st;
}

void GetDelta(const float* base, float* delta)
{
    if (!s_enabled || s_activeOrder == ORDER_NONE || !delta) return;

    const DWORD now = GetTickCount();
    if (now - s_orderTimestampMs >= s_orderDurationMs) {
        s_activeOrder = ORDER_NONE;
        return;
    }

    float alpha = 1.0f - (float)(now - s_orderTimestampMs) / (float)s_orderDurationMs;
    if (alpha <= 0.0f) {
        s_activeOrder = ORDER_NONE;
        return;
    }

    if (s_activeOrder == ORDER_COME) {
        delta[I_GUARDIAN] += 350.0f * alpha;
    } else if (s_activeOrder == ORDER_GO) {
        delta[I_SCATHER] += 300.0f * alpha;
        delta[I_PIONEER] += 200.0f * alpha;
    } else if (s_activeOrder == ORDER_HELP) {
        delta[I_MEDICANT] += 300.0f * alpha;
        delta[I_UTILITARIAN] += 200.0f * alpha;
    }
}

void Tick()
{
    if (!s_enabled) return;

    const uintptr_t arisen = Runtime::ArisenBody();
    if (!arisen) return;

    // Читаем текущее действие Аризена
    char arisenAct[48] = {};
    uintptr_t actObj = 0;
    if (!RdPtr((void*)(arisen + 0x2DC8), &actObj) || !actObj) return;
    if (!NameOfLiveObjectSafe((const void*)actObj, arisenAct, sizeof(arisenAct)) || !arisenAct[0]) return;

    const bool isOrderAct = (strstr(arisenAct, "Order") != nullptr || !strcmp(arisenAct, "cPlActCMCOrder"));
    const DWORD now = GetTickCount();

    if (isOrderAct && !s_inOrder) {
        // === ВХОД В КОМАНДУ D-PAD ===
        s_inOrder = true;
        s_orderStartMs = now;
        s_orderTimestampMs = now;
        ++s_stats.orderCount;
        s_stats.lastOrderTimeMs = now;
        lstrcpynA(s_stats.lastOrderAct, arisenAct, sizeof(s_stats.lastOrderAct));

        // Распознаем тип команды (по клавишам F1/F2/F3 или D-pad)
        OrderType detectedOrder = ORDER_COME;
        const char* orderName = "Come! (Rally)";

        if (GetAsyncKeyState(VK_F1) & 0x8000) {
            detectedOrder = ORDER_GO;
            orderName = "Go! (Assault Focus)";
        } else if (GetAsyncKeyState(VK_F2) & 0x8000) {
            detectedOrder = ORDER_HELP;
            orderName = "Help! (Defensive Peel)";
        } else if (GetAsyncKeyState(VK_F3) & 0x8000) {
            detectedOrder = ORDER_COME;
            orderName = "Come! (Rally)";
        } else {
            // Определение по цели планировщика главной пешки
            int32_t mainCode = -1;
            Runtime::PawnPriorityCode(&mainCode);
            if (mainCode == 32) {
                detectedOrder = ORDER_HELP;
                orderName = "Help! (Precaution)";
            } else if (mainCode == 54 || mainCode == 57 || mainCode == 10 || mainCode == 2) {
                detectedOrder = ORDER_GO;
                orderName = "Go! (Attack)";
            } else {
                detectedOrder = ORDER_COME;
                orderName = "Come! (Follow)";
            }
        }

        s_activeOrder = detectedOrder;
        lstrcpynA(s_stats.lastOrderName, orderName, sizeof(s_stats.lastOrderName));

        // Координаты Аризена
        float ax = 0, ay = 0, az = 0;
        Runtime::GetArisenWorldPos(&ax, &ay, &az);

        logFile << "\n========================================================" << std::endl;
        logFile << "TacticalOrders: >>> COMMAND [" << orderName << "] EXECUTED by Arisen! <<<" << std::endl;

        // Моторная реализация команд:
        if (detectedOrder == ORDER_GO) {
            // «Вперед!»: находим цель в прицеле и пиним штурмовым пешкам
            s_focusTarget = FindTargetInPlayerAim(ax, ay, az, s_focusTargetKind, sizeof(s_focusTargetKind));
            if (s_focusTarget) {
                const int nPawns = Runtime::PawnBodyCount();
                for (int p = 0; p < nPawns; ++p) {
                    bool isMain = false;
                    uintptr_t b = Runtime::PawnBodyAt(p, &isMain);
                    if (!b) continue;
                    // Пиним штурмовикам цель и даем рывок
                    WrSafe((void*)(b + 0x2EB8), &s_focusTarget, sizeof(uintptr_t));
                    WrSafe((void*)(b + 0x14E0), &s_focusTarget, sizeof(uintptr_t));
                    Runtime::Tempo::SetOverride(b, 1.25f, 1.15f, 3000);
                }
                char fl[256];
                sprintf_s(fl, "  Tactical Target -> Focused on 0x%08X (%s) with sprint haste!",
                          (unsigned)s_focusTarget, s_focusTargetKind[0] ? s_focusTargetKind : "enemy");
                logFile << fl << std::endl;
            }
        } else if (detectedOrder == ORDER_COME) {
            // «Ко мне!»: собираем всех отставших пешек на спринте вокруг Аризена
            const int nPawns = Runtime::PawnBodyCount();
            for (int p = 0; p < nPawns; ++p) {
                bool isMain = false;
                uintptr_t b = Runtime::PawnBodyAt(p, &isMain);
                if (!b) continue;

                float px = 0, py = 0, pz = 0;
                if (Rd((const void*)(b + 0x40), &px, 4) &&
                    Rd((const void*)(b + 0x44), &py, 4) &&
                    Rd((const void*)(b + 0x48), &pz, 4)) {
                    float d = Dist3D(px, py, pz, ax, ay, az) / 100.0f;
                    if (d > 6.0f) {
                        Runtime::Tempo::SetOverride(b, 1.30f, 1.0f, 3000);
                    }
                }
            }
            logFile << "  Tactical Rally -> Spring sprint activated for rallying pawns!" << std::endl;
        }

        // Опрашиваем состояние всех 3 пешек
        const int nPawns = Runtime::PawnBodyCount();
        for (int p = 0; p < nPawns && p < 3; ++p) {
            bool isMain = false;
            const uintptr_t pawnBody = Runtime::PawnBodyAt(p, &isMain);
            if (!pawnBody) continue;

            const char* roleName = isMain ? "MainPawn" : (p == 1 ? "Hired1" : "Hired2");

            char pAct[48] = {};
            Runtime::ReadLiveAct(pawnBody, pAct, sizeof(pAct));
            lstrcpynA(s_stats.lastPawnAct[p], pAct[0] ? pAct : "?", sizeof(s_stats.lastPawnAct[p]));

            float px = 0, py = 0, pz = 0;
            float distM = 0.0f;
            if (Rd((const void*)(pawnBody + 0x40), &px, 4) &&
                Rd((const void*)(pawnBody + 0x44), &py, 4) &&
                Rd((const void*)(pawnBody + 0x48), &pz, 4)) {
                distM = Dist3D(px, py, pz, ax, ay, az) / 100.0f;
            }
            s_stats.lastPawnDist[p] = distM;

            int32_t gCode = -1;
            char gName[32] = "-";
            char ifaces[96] = "-";
            if (isMain) {
                if (Runtime::PawnPriorityCode(&gCode) && gCode >= 0) {
                    Runtime::PawnGoalName(gCode, gName, sizeof(gName));
                    Runtime::PawnPlanInterfaces(gCode, ifaces, sizeof(ifaces));
                }
            }
            s_stats.lastPawnGoalCode[p] = gCode;
            lstrcpynA(s_stats.lastPawnGoalName[p], gName, sizeof(s_stats.lastPawnGoalName[p]));

            char pawnLine[320];
            sprintf_s(pawnLine, "  [%s] dist=%.1fm | act=%s | goal=%d (\"%s\")",
                      roleName, distM, pAct[0] ? pAct : "?", gCode, gName);
            logFile << pawnLine << std::endl;
        }

        logFile << "  Cognitive Impulse: +350 weight boost applied with 6s smooth decay" << std::endl;
        logFile << "========================================================\n" << std::endl;
    }
    else if (!isOrderAct && s_inOrder) {
        const DWORD dur = s_orderStartMs ? (now - s_orderStartMs) : 0;
        char l[160];
        sprintf_s(l, "TacticalOrders: Order phase completed (duration=%ums, decay running)", (unsigned)dur);
        logFile << l << std::endl;
        s_inOrder = false;
    }
}

} // namespace OrderWatch
} // namespace PawnAI
