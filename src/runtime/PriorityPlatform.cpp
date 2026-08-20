// Runtime::Priority — транзакционные priority-профили и Guardian-фикс.
// Правило слоя: validate -> write -> readback -> convergence -> rollback.
// Никаких «попробуем и посмотрим» — только подтверждённые кортежи правил.

#include "stdafx.h"
#include "RuntimeInternal.h"
#include "../ModPaths.h"
#include <stdio.h>
#include <stdlib.h>

namespace Runtime {

const char* PartyPriorityProfilePath()
{
    return ModPaths::File("ddda_pawn_ai_profiles.ini", 7);
}

uint32_t PartyPriorityProfileHash(const void* data, size_t bytes, uint32_t h)
{
    const BYTE* p = (const BYTE*)data;
    for (size_t i = 0; i < bytes; ++i) { h ^= p[i]; h *= 16777619u; }
    return h;
}

int PartyPriorityProfileGetInt(
    const char* section, const char* key, int fallback)
{
    char def[24] = {};
    char value[24] = {};
    sprintf_s(def, sizeof(def), "%d", fallback);
    GetPrivateProfileStringA(section, key, def, value, sizeof(value),
        PartyPriorityProfilePath());
    return (int)strtol(value, nullptr, 0);
}

bool PartyPriorityProfileNameOk(const char* name)
{
    if (!name || !name[0]) return false;
    for (int i = 0; name[i]; ++i) {
        const char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
            || (c >= '0' && c <= '9') || c == '_' || c == '-'))
            return false;
    }
    return true;
}

void PartyPriorityProfileEnsureFile()
{
    const char* path = PartyPriorityProfilePath();
    const bool exists = GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
    if (!exists)
        WritePrivateProfileStringA("profile", "active", "vanilla", path);

    // Schema migration keeps the Build 45 active choice but expands the file
    // into the generalized Build 46 rule-list format.
    const int schema = PartyPriorityProfileGetInt("profile", "schemaVersion", 0);
    if (schema < 2) {
        WritePrivateProfileStringA("profile", "schemaVersion", "2", path);
        WritePrivateProfileStringA("vanilla", "ruleCount", "0", path);
        WritePrivateProfileStringA("research_code45", "ruleCount", "1", path);
        const char* s = "research_code45.rule0";
        WritePrivateProfileStringA(s, "sensor", "0", path);
        WritePrivateProfileStringA(s, "code", "45", path);
        WritePrivateProfileStringA(s, "category", "0", path);
        WritePrivateProfileStringA(s, "objectId", "0", path);
        WritePrivateProfileStringA(s, "extra", "1", path);
        WritePrivateProfileStringA(s, "ruleIndex", "0", path);
        WritePrivateProfileStringA(s, "expectedAddS32", "-1", path);
        WritePrivateProfileStringA(s, "desiredAddS32", "-2", path);
        WritePrivateProfileStringA(s, "expectedAddF32", "0.0", path);
        WritePrivateProfileStringA(s, "expectedBreak", "1", path);
        WritePrivateProfileStringA(s, "expectedCheckCount", "1", path);
        WritePrivateProfileStringA(s, "expectedSlot", "34", path);

        WritePrivateProfileStringA("research_pair45_46", "ruleCount", "2", path);
        const char* p0 = "research_pair45_46.rule0";
        const char* p1 = "research_pair45_46.rule1";
        const char* pairSections[2] = { p0, p1 };
        const char* pairCodes[2] = { "45", "46" };
        for (int i = 0; i < 2; ++i) {
            WritePrivateProfileStringA(pairSections[i], "sensor", "0", path);
            WritePrivateProfileStringA(pairSections[i], "code", pairCodes[i], path);
            WritePrivateProfileStringA(pairSections[i], "category", "0", path);
            WritePrivateProfileStringA(pairSections[i], "objectId", "0", path);
            WritePrivateProfileStringA(pairSections[i], "extra", "1", path);
            WritePrivateProfileStringA(pairSections[i], "ruleIndex", "0", path);
            WritePrivateProfileStringA(pairSections[i], "expectedAddS32", "-1", path);
            WritePrivateProfileStringA(pairSections[i], "desiredAddS32", "-2", path);
            WritePrivateProfileStringA(pairSections[i], "expectedAddF32", "0.0", path);
            WritePrivateProfileStringA(pairSections[i], "expectedBreak", "1", path);
            WritePrivateProfileStringA(pairSections[i], "expectedCheckCount", "1", path);
            WritePrivateProfileStringA(pairSections[i], "expectedSlot", "34", path);
        }
    }
    g_priorityProfileFileOk = GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

bool PartyPriorityProfileReadConfig(
    char* activeOut, PartyPriorityProfileRule* rulesOut, int* countOut,
    uint32_t* hashOut)
{
    if (!activeOut || !rulesOut || !countOut || !hashOut) return false;
    PartyPriorityProfileEnsureFile();
    memset(rulesOut, 0, sizeof(PartyPriorityProfileRule) * kPriorityProfileMaxRules);

    GetPrivateProfileStringA("profile", "active", "vanilla",
        activeOut, 40, PartyPriorityProfilePath());
    if (!PartyPriorityProfileNameOk(activeOut)) return false;

    int count = PartyPriorityProfileGetInt(activeOut, "ruleCount", 0);
    if (count < 0 || count > kPriorityProfileMaxRules) return false;

    uint32_t h = PartyPriorityProfileHash(activeOut, strlen(activeOut) + 1);
    for (int i = 0; i < count; ++i) {
        char section[72] = {};
        sprintf_s(section, sizeof(section), "%s.rule%d", activeOut, i);
        PartyPriorityProfileRule& R = rulesOut[i];
        R.sensor = (uint32_t)PartyPriorityProfileGetInt(section, "sensor", -1);
        R.code = (uint32_t)PartyPriorityProfileGetInt(section, "code", -1);
        R.category = (uint32_t)PartyPriorityProfileGetInt(section, "category", -1);
        R.objectId = (uint32_t)PartyPriorityProfileGetInt(section, "objectId", -1);
        R.extra = (uint32_t)PartyPriorityProfileGetInt(section, "extra", -1);
        R.ruleIndex = (uint32_t)PartyPriorityProfileGetInt(section, "ruleIndex", -1);
        R.expectedAddS32 = PartyPriorityProfileGetInt(section, "expectedAddS32", 9999);
        R.desiredAddS32 = PartyPriorityProfileGetInt(section, "desiredAddS32", 9999);
        R.expectedBreak = (uint32_t)PartyPriorityProfileGetInt(
            section, "expectedBreak", -1);
        R.expectedCheckCount = (uint32_t)PartyPriorityProfileGetInt(
            section, "expectedCheckCount", -1);
        R.expectedSlot = PartyPriorityProfileGetInt(section, "expectedSlot", -1);
        char f32Text[32] = {};
        GetPrivateProfileStringA(section, "expectedAddF32", "0.0",
            f32Text, sizeof(f32Text), PartyPriorityProfilePath());
        const float expectedF32 = (float)atof(f32Text);
        memcpy(&R.expectedAddF32Bits, &expectedF32, 4);
        R.liveSlot = -1;

        if (R.sensor > 1u || R.code > 255u || R.category > 32u
            || R.objectId > 0xFFFFu || R.ruleIndex >= 16u
            || R.expectedAddS32 < -32 || R.expectedAddS32 > 32
            || R.desiredAddS32 < -32 || R.desiredAddS32 > 32
            || R.expectedBreak > 1u || R.expectedCheckCount > 16u
            || R.expectedSlot < -1 || R.expectedSlot >= 48)
            return false;

        for (int j = 0; j < i; ++j) {
            PartyPriorityProfileRule& P = rulesOut[j];
            if (P.sensor == R.sensor && P.code == R.code
                && P.category == R.category && P.objectId == R.objectId
                && P.extra == R.extra && P.ruleIndex == R.ruleIndex)
                return false;
        }
        const size_t configBytes = (const BYTE*)&R.prioPtr - (const BYTE*)&R;
        h = PartyPriorityProfileHash(&R, configBytes, h);
    }
    *countOut = count;
    *hashOut = h;
    return true;
}

void PartyPriorityProfileResetRuntime()
{
    for (int i = 0; i < g_nPriorityProfileRules; ++i) {
        PartyPriorityProfileRule& R = g_priorityProfileRules[i];
        R.prioPtr = R.rulePtr = R.ruleVt = 0;
        R.resolved = R.applied = false;
        R.currentAddS32 = 0;
        R.liveSlot = -1;
    }
    g_priorityProfileApplied = g_nPriorityProfileRules == 0;
    g_priorityProfileConverged = g_nPriorityProfileRules == 0;
}

// РАЗРЕШЕНИЕ ПРАВИЛА ЧЕРЕЗ ЖИВЫЕ ВЁДРА ПЕШКИ (75.26).
//
// ПОЧЕМУ ПОНАДОБИЛОСЬ. Тестер сообщил, что галка Guardian-фикса стояла во
// ВСЕХ трёх боях A/B — а кинжалы при Guardian всё равно ноль. Проверка
// кода объяснила: правило ищется перебором `g_pawnAi`, то есть по
// результату ПОЛНОГО census, который в этих сессиях не отработал ни разу
// (`priority profile auto-discovery found=0` пять раз подряд). Фикс всё
// это время висел в состоянии «ARMED, rule not resolved» и не написал
// ничего.
//
// При этом добраться до той же строки можно дёшево и без census — тем же
// путём, которым ходят пробы: тело -> cAICtrl -> cAIPriorityThink -> 48
// вёдер -> cPrioParam с нужным кодом. Ниже именно этот путь.
static bool ResolveRuleFromLiveBuckets(PartyPriorityProfileRule& R)
{
    const uintptr_t pawn = MainPawnBody();
    if (!pawn) return false;

    uintptr_t ctrl = 0;
    if (RdPtr((void*)(pawn + 0x2E64), &ctrl) && ctrl) {
        char nm[48] = {};
        if (!NameOfLiveObject(ctrl, nm, sizeof(nm)) || strcmp(nm, "cAICtrl") != 0)
            ctrl = 0;
    }
    if (!ctrl) ctrl = FindChildByClass(pawn, kPartyBodySize, "cAICtrl", 0);
    if (!ctrl) return false;
    const uintptr_t think = FindChildByClass(ctrl, 704, "cAIPriorityThink", 0);
    if (!think) return false;

    for (int slot = 0; slot < 48; ++slot) {
        uint32_t D[5] = {};
        if (!Rd((void*)(think + 0x38u + (uint32_t)slot * 0x14u), D, sizeof(D))) continue;
        const uint32_t count = D[1], capacity = D[2];
        const uintptr_t arr = D[4];
        if (!count || count > 16u || capacity > 16u || count > capacity
            || !LooksHeap(arr)) continue;
        uintptr_t ptrs[16] = {};
        if (!Rd((void*)arr, ptrs, count * sizeof(uintptr_t))) continue;

        for (uint32_t n = 0; n < count; ++n) {
            const uintptr_t prio = ptrs[n];
            if (!LooksHeap(prio)) continue;
            char nm[64] = {};
            if (!NameOfLiveObject(prio, nm, sizeof(nm))
                || strcmp(nm, "rAIPriorityThink::cPrioParam") != 0) continue;

            uint32_t raw[16] = {};
            if (!Rd((void*)prio, raw, sizeof(raw))) continue;
            if (raw[1] != R.sensor || raw[2] != R.code || raw[3] != R.category
                || raw[4] != R.objectId || raw[5] != R.extra) continue;
            if (raw[7] > 16u || R.ruleIndex >= raw[7] || !LooksHeap(raw[10])) continue;

            uintptr_t rule = 0;
            if (!RdPtr((void*)(uintptr_t)(raw[10] + R.ruleIndex * 4u), &rule)
                || !LooksHeap(rule)) continue;
            uint32_t cp[9] = {};
            if (!Rd((void*)rule, cp, sizeof(cp)) || !LooksLikeVtable(cp[0])) continue;
            const int32_t current = (int32_t)cp[1];
            if (current != R.expectedAddS32 && current != R.desiredAddS32) continue;

            uintptr_t vt = 0;
            if (!RdPtr((void*)prio, &vt)) continue;
            R.prioPtr = prio;
            R.rulePtr = rule;
            R.ruleVt = cp[0];
            R.currentAddS32 = current;
            R.liveSlot = slot;
            R.resolved = true;
            return true;
        }
    }
    return false;
}

bool PartyPriorityProfileResolveRule(PartyPriorityProfileRule& R)
{
    // Сначала дешёвый путь по живым вёдрам, census — только как запасной.
    if (ResolveRuleFromLiveBuckets(R)) return true;

    for (int i = 0; i < g_nPawnAi; ++i) {
        PawnAiCandidate& A = g_pawnAi[i];
        if (strcmp(A.name, "rAIPriorityThink::cPrioParam")) continue;
        uintptr_t currentPrioVt = 0;
        uint32_t raw[16] = {};
        if (!RdPtr((void*)A.ptr, &currentPrioVt) || currentPrioVt != A.vt
            || !Rd((void*)A.ptr, raw, sizeof(raw)))
            continue;
        if (raw[1] != R.sensor || raw[2] != R.code || raw[3] != R.category
            || raw[4] != R.objectId || raw[5] != R.extra)
            continue;
        if (raw[7] > 16u || raw[8] > 16u || raw[7] > raw[8]
            || R.ruleIndex >= raw[7] || !LooksHeap(raw[10]))
            return false;

        uintptr_t rule = 0;
        if (!RdPtr((void*)(uintptr_t)(raw[10] + R.ruleIndex * 4u), &rule)
            || !LooksHeap(rule))
            return false;
        uint32_t cp[9] = {};
        if (!Rd((void*)rule, cp, sizeof(cp))
            || !LooksLikeVtable(cp[0]) || !LooksLikeVtable(cp[4]))
            return false;
        const int32_t current = (int32_t)cp[1];
        if ((current != R.expectedAddS32 && current != R.desiredAddS32)
            || cp[2] != R.expectedAddF32Bits || cp[3] != R.expectedBreak
            || cp[5] != R.expectedCheckCount || cp[6] > 16u
            || cp[5] > cp[6] || cp[7] != 1u
            || (cp[5] && !LooksHeap(cp[8])))
            return false;

        R.prioPtr = A.ptr;
        R.rulePtr = rule;
        R.ruleVt = cp[0];
        R.currentAddS32 = current;
        R.liveSlot = PartyPriorityLiveSlot(A.ptr);
        R.resolved = true;
        return true;
    }
    return false;
}

bool PartyPriorityProfileResolveAll()
{
    for (int i = 0; i < g_nPriorityProfileRules; ++i)
        if (!PartyPriorityProfileResolveRule(g_priorityProfileRules[i]))
            return false;
    return true;
}

bool PartyPriorityProfileRestoreAll(const char* reason)
{
    // Build 56.7: ничего не применено — нечего и восстанавливать. Без этого
    // вызова на «world unload» логировали впустую (спам в логе).
    if (!g_priorityProfileApplied) return true;

    // Prevalidate every still-live target before changing any of them.
    for (int i = 0; i < g_nPriorityProfileRules; ++i) {
        PartyPriorityProfileRule& R = g_priorityProfileRules[i];
        if (!R.applied || !R.resolved) continue;
        uintptr_t vt = 0;
        int32_t current = 0;
        if (!RdPtr((void*)R.rulePtr, &vt) || vt != R.ruleVt
            || !Rd((void*)(R.rulePtr + 0x04), &current, 4)) {
            R.resolved = R.applied = false; // object is gone; nothing remains to restore
            continue;
        }
        if (current != R.expectedAddS32 && current != R.desiredAddS32) {
            sprintf_s(g_priorityProfileStatus, sizeof(g_priorityProfileStatus),
                "Priority profile: ROLLBACK REFUSED rule %d value=%d", i, current);
            return false;
        }
        R.currentAddS32 = current;
    }

    for (int i = 0; i < g_nPriorityProfileRules; ++i) {
        PartyPriorityProfileRule& R = g_priorityProfileRules[i];
        if (!R.applied || !R.resolved) continue;
        if (R.currentAddS32 == R.desiredAddS32
            && R.desiredAddS32 != R.expectedAddS32) {
            if (!WrSafe((void*)(R.rulePtr + 0x04), &R.expectedAddS32, 4))
                return false;
            int32_t verify = 0;
            if (!Rd((void*)(R.rulePtr + 0x04), &verify, 4)
                || verify != R.expectedAddS32)
                return false;
            ++g_priorityProfileRestores;
        }
        R.currentAddS32 = R.expectedAddS32;
        R.applied = false;
    }
    g_priorityProfileApplied = false;
    g_priorityProfileConverged = false;
    logFile << "PartyRecon: priority profile restored reason="
            << (reason ? reason : "unknown") << std::endl;
    return true;
}

void PartyPriorityProfileUndoWrites(const bool* wrote, int count)
{
    for (int i = 0; i < count; ++i) {
        PartyPriorityProfileRule& R = g_priorityProfileRules[i];
        if (wrote && wrote[i] && R.rulePtr)
            WrSafe((void*)(R.rulePtr + 0x04), &R.expectedAddS32, 4);
        R.currentAddS32 = R.expectedAddS32;
        R.applied = false;
    }
    g_priorityProfileApplied = false;
    g_priorityProfileConverged = false;
}

bool PartyPriorityProfileApplyAll()
{
    if (g_nPriorityProfileRules == 0) {
        g_priorityProfileApplied = g_priorityProfileConverged = true;
        return true;
    }
    if (!PartyPriorityProfileResolveAll()) return false;

    bool wrote[kPriorityProfileMaxRules] = {};
    for (int i = 0; i < g_nPriorityProfileRules; ++i) {
        PartyPriorityProfileRule& R = g_priorityProfileRules[i];
        if (R.currentAddS32 == R.desiredAddS32) {
            R.applied = true;
            continue;
        }
        if (R.currentAddS32 != R.expectedAddS32
            || !WrSafe((void*)(R.rulePtr + 0x04), &R.desiredAddS32, 4)) {
            PartyPriorityProfileUndoWrites(wrote, g_nPriorityProfileRules);
            return false;
        }
        int32_t verify = 0;
        if (!Rd((void*)(R.rulePtr + 0x04), &verify, 4)
            || verify != R.desiredAddS32) {
            WrSafe((void*)(R.rulePtr + 0x04), &R.expectedAddS32, 4);
            PartyPriorityProfileUndoWrites(wrote, g_nPriorityProfileRules);
            return false;
        }
        wrote[i] = true;
        ++g_priorityProfileWrites;
        R.currentAddS32 = verify;
        R.applied = true;
    }
    g_priorityProfileApplied = true;
    return true;
}

void PartyPriorityProfileUpdateState()
{
    if (g_nPriorityProfileRules == 0) {
        g_priorityProfileApplied = g_priorityProfileConverged = true;
        sprintf_s(g_priorityProfileStatus, sizeof(g_priorityProfileStatus),
            "Priority profile: %s, 0 rules (vanilla)", g_priorityProfileActive);
        return;
    }

    int applied = 0;
    int converged = 0;
    for (int i = 0; i < g_nPriorityProfileRules; ++i) {
        PartyPriorityProfileRule& R = g_priorityProfileRules[i];
        if (!R.resolved) continue;
        uintptr_t vt = 0;
        int32_t current = 0;
        if (!RdPtr((void*)R.rulePtr, &vt) || vt != R.ruleVt
            || !Rd((void*)(R.rulePtr + 0x04), &current, 4)) {
            R.resolved = R.applied = false;
            continue;
        }
        R.currentAddS32 = current;
        R.liveSlot = PartyPriorityLiveSlot(R.prioPtr);
        if (current == R.desiredAddS32 && R.applied) ++applied;
        if (R.expectedSlot < 0 || R.liveSlot == R.expectedSlot) ++converged;
    }
    g_priorityProfileApplied = applied == g_nPriorityProfileRules;
    g_priorityProfileConverged = g_priorityProfileApplied
        && converged == g_nPriorityProfileRules;
    sprintf_s(g_priorityProfileStatus, sizeof(g_priorityProfileStatus),
        "Priority profile: %s, rules %d/%d, %s",
        g_priorityProfileActive, applied, g_nPriorityProfileRules,
        g_priorityProfileConverged ? "CONVERGED" : "PENDING");
}

bool PartyPriorityProfileLoadIfChanged()
{
    PartyPriorityProfileRule next[kPriorityProfileMaxRules] = {};
    char active[40] = {};
    int count = 0;
    uint32_t hash = 0;
    if (!PartyPriorityProfileReadConfig(active, next, &count, &hash)) {
        lstrcpynA(g_priorityProfileStatus,
            "Priority profile: INVALID SIDECAR, keeping current profile",
            sizeof(g_priorityProfileStatus));
        return false;
    }
    if (g_priorityProfileLoaded && hash == g_priorityProfileConfigHash) return true;
    if (g_priorityProfileLoaded
        && !PartyPriorityProfileRestoreAll("sidecar switch"))
        return false;

    memset(g_priorityProfileRules, 0, sizeof(g_priorityProfileRules));
    memcpy(g_priorityProfileRules, next, sizeof(next));
    g_nPriorityProfileRules = count;
    lstrcpynA(g_priorityProfileActive, active, sizeof(g_priorityProfileActive));
    g_priorityProfileConfigHash = hash;
    g_priorityProfileLoaded = true;
    PartyPriorityProfileResetRuntime();
    return true;
}

void PartyPriorityProfileSetActive(const char* active)
{
    if (!PartyPriorityProfileNameOk(active)) active = "vanilla";
    PartyPriorityProfileEnsureFile();
    WritePrivateProfileStringA("profile", "active", active,
        PartyPriorityProfilePath());
    g_priorityProfileLastPoll = 0;
}

void PartyPriorityProfileTick()
{
    DWORD now = MsNow();
    if (!g_priorityProfileWorldSince) g_priorityProfileWorldSince = now;
    if (!g_priorityProfileLastPoll || now - g_priorityProfileLastPoll >= 1000u) {
        g_priorityProfileLastPoll = now;
        PartyPriorityProfileLoadIfChanged();
    }

    if (!g_priorityProfileApplied && g_nPriorityProfileRules > 0) {
        if (!PartyPriorityProfileApplyAll()) {
            // ОТСТУПАЕМ ПОСЛЕ ТРЁХ ПУСТЫХ ПОПЫТОК.
            //
            // Лог 75.23 показал цикл: `auto-discovery found=0 candidates=460
            // findMs=33187` — и так пять раз подряд, каждые тридцать секунд
            // по ТРИДЦАТЬ СЕКУНД полного скана памяти. Между этими сканами
            // разбор партии пересобирался, тела пересоздавались, счётчик
            // пешек прыгал `1 -> 0`, а слежение за кодом теряло планировщик
            // и не набирало ни одного сэмпла. Так пропали два замера подряд.
            //
            // Поиск, который трижды ничего не нашёл, не найдёт и в
            // четвёртый: профиль просто не для этой сессии. Замолкаем до
            // смены мира или состава партии.
            static int s_emptyDiscoveries = 0;
            const bool allowDiscover = s_emptyDiscoveries < 3
                && now - g_priorityProfileWorldSince >= 5000u
                && (!g_priorityProfileLastDiscover
                    || now - g_priorityProfileLastDiscover >= 30000u);
            if (allowDiscover) {
                g_priorityProfileLastDiscover = now;
                if (PartyPriorityProfileAutoDiscover()) {
                    s_emptyDiscoveries = 0;
                    PartyPriorityProfileApplyAll();
                } else if (++s_emptyDiscoveries == 3) {
                    logFile << "PartyRecon: priority profile not found three times"
                               " in a row - stopping the search until the world or"
                               " the party changes (it was costing ~30 s of scanning"
                               " every 30 s and breaking live measurements)"
                            << std::endl;
                }
            }
        }
    }
    PartyPriorityProfileUpdateState();
}

void PartyPriorityProfileToggle()
{
    PartyPriorityProfileLoadIfChanged();
    PartyPriorityProfileSetActive(!strcmp(g_priorityProfileActive, "vanilla")
        ? "research_pair45_46" : "vanilla");
    PartyPriorityProfileLoadIfChanged();
    if (g_nPriorityProfileRules > 0) PartyPriorityProfileApplyAll();
    PartyPriorityProfileUpdateState();
}

void PartyPriorityProfileHotkeyTick()
{
    PartyPriorityProfileTick();
    static bool wasDown = false;
    const bool down = (GetAsyncKeyState(VK_OEM_MINUS) & 0x8000) != 0;
    if (down && !wasDown) PartyPriorityProfileToggle();
    wasDown = down;
}

bool PartyPriorityProfileAutoDiscover()
{
    if (InterlockedCompareExchange(&g_partyBusy, 1, 0) != 0) return false;
    PartyFindBodies();
    for (int i = 0; i < g_nParty; ++i) PartyInspectBody(g_party[i]);
    PartyMarkPawnManagerRefs();
    PartySelectWorkingPair();
    PartyAssignRoles();
    PartyReadPositions();
    InterlockedExchange(&g_partyBusy, 0);

    const bool found = PartyPriorityProfileResolveAll();
    logFile << "PartyRecon: priority profile auto-discovery found="
            << (found ? 1 : 0) << " candidates=" << g_nPawnAi
            << " rules=" << g_nPriorityProfileRules
            << " findMs=" << g_partyFindMs << std::endl;
    return found;
}

void GuardianFixInitOnce()
{
    if (g_guardianFixInit) return;
    g_guardianFixInit = true;
    memset(&g_guardianFixRule, 0, sizeof(g_guardianFixRule));
    g_guardianFixRule.sensor     = 1;
    g_guardianFixRule.code       = 54;
    g_guardianFixRule.category   = 0;
    g_guardianFixRule.objectId   = 0;
    g_guardianFixRule.extra      = 1;
    g_guardianFixRule.ruleIndex  = 0;
    g_guardianFixRule.expectedAddS32 = -3;
    g_guardianFixRule.desiredAddS32  = 0;
    g_guardianFixRule.expectedBreak  = 0;
    g_guardianFixRule.expectedCheckCount = 1;
    g_guardianFixRule.expectedSlot = -1; // слот не проверяем (конвергенция по AddS32)
}

// УДЕРЖАНИЕ РЫЧАГА ЗА ПРИБОРОМ (75.31).
//
// Замер «развёртка по вёдрам» держит строку code 54 в заданном ведре
// десятки секунд. Доктрина же каждый тик ставит своё значение — и затирала
// бы удержание в том же кадре. Владение объявляется явно.
static bool    s_fixHeld = false;
static int32_t s_fixHeldValue = -3;

void GuardianFixHold(bool on, int32_t value)
{
    GuardianFixInitOnce();
    s_fixHeld = on;
    s_fixHeldValue = value;
    if (!on) return;
    g_guardianFixRule.desiredAddS32 = value;
    g_guardianFixArmed = (value != g_guardianFixRule.expectedAddS32);
}

bool GuardianFixHeld() { return s_fixHeld; }

void GuardianFixSetTarget(int32_t desiredAddS32)
{
    GuardianFixInitOnce();
    if (s_fixHeld) return;              // рычагом владеет прибор
    g_guardianFixRule.desiredAddS32 = desiredAddS32;
    // desired == vanilla (-3) → откат; иначе — активен.
    g_guardianFixArmed = (desiredAddS32 != g_guardianFixRule.expectedAddS32);
}

bool GuardianFixIsApplied()
{
    return g_guardianFixApplied;
}

// Где строка code 54 лежит ПРЯМО СЕЙЧАС. Прибор развёртки обязан печатать
// не «что мы просили», а «куда движок её положил» — иначе это не измерение,
// а пересказ собственной записи.
int GuardianFixLiveSlot()
{
    if (!g_guardianFixRule.resolved) return -1;
    return PartyPriorityLiveSlot(g_guardianFixRule.prioPtr);
}

bool GuardianFixResolved() { return g_guardianFixRule.resolved; }

const char* GuardianFixStatus()
{
    return g_guardianFixStatus;
}

// Build 58: единый tick с градиентом. Целевое значение = desired (если armed)
// или expected (rollback). Каждый тик читаем текущее, при расхождении —
// write + readback (verify) + откат к прежнему при неудаче. Поддерживает
// плавную смену desired (градиент: -3 → 0 → +2 по дистанции угрозы).
// Печатать состояние фикса при КАЖДОЙ смене.
//
// До 75.26 у единственной продуктовой записи мода не было ни одной строки
// в логе: статус жил только в панели. Три боя A/B прошли с включённой
// галкой, и по логам нельзя было сказать, работал фикс или молчал. Теперь
// можно.
static void GuardianFixLogState()
{
    static char prev[160] = {};
    if (!strcmp(prev, g_guardianFixStatus)) return;
    lstrcpynA(prev, g_guardianFixStatus, sizeof(prev));
    logFile << "GuardianFix: " << g_guardianFixStatus << std::endl;
}

void GuardianFixTick()
{
    GuardianFixInitOnce();
    // РЫЧАГ ОТДАН ПРИБОРУ. Доктрина зовёт этот tick каждый кадр независимо
    // от галки, и он гнал бы строку обратно к -3, пока прибор гонит её к
    // своей цели. Две записи в одно поле в одном кадре — это не измерение.
    if (s_fixHeld) return;
    PartyPriorityProfileRule& R = g_guardianFixRule;

    if (g_guardianFixArmed && !R.resolved) {
        if (!PartyPriorityProfileResolveRule(R)) {
            sprintf_s(g_guardianFixStatus, sizeof(g_guardianFixStatus),
                "ARMED, rule NOT resolved (neither live buckets nor census)");
            GuardianFixLogState();
            return;
        }
    }
    if (!R.resolved) {
        g_guardianFixApplied = false;
        sprintf_s(g_guardianFixStatus, sizeof(g_guardianFixStatus),
            "disabled (rule not resolved)");
        GuardianFixLogState();
        return;
    }

    const int32_t target = g_guardianFixArmed ? R.desiredAddS32 : R.expectedAddS32;
    int32_t cur = 0;
    uintptr_t vt = 0;
    if (!RdPtr((void*)R.rulePtr, &vt) || vt != R.ruleVt
        || !Rd((void*)(R.rulePtr + 0x04), &cur, 4)) {
        R.resolved = R.applied = false;
        g_guardianFixApplied = false;
        sprintf_s(g_guardianFixStatus, sizeof(g_guardianFixStatus),
            "rule lost (body re-created?)");
        GuardianFixLogState();
        return;
    }
    R.currentAddS32 = cur;

    if (cur == target) {
        g_guardianFixApplied = g_guardianFixArmed;
        if (g_guardianFixArmed) {
            int slot = PartyPriorityLiveSlot(R.prioPtr);
            sprintf_s(g_guardianFixStatus, sizeof(g_guardianFixStatus),
                "APPLIED (code54 = %d) slot=%d writes=%d",
                target, slot, g_guardianFixWrites);
        } else {
            sprintf_s(g_guardianFixStatus, sizeof(g_guardianFixStatus),
                "vanilla (rolled back x%d)", g_guardianFixRollbacks);
        }
        GuardianFixLogState();
        return;
    }

    // Переход к целевому значению: write + readback (verify), при неудаче —
    // вернуть прежнее.
    if (!WrSafe((void*)(R.rulePtr + 0x04), &target, 4)) {
        sprintf_s(g_guardianFixStatus, sizeof(g_guardianFixStatus),
            "write FAILED");
        GuardianFixLogState();
        return;
    }
    int32_t verify = 0;
    if (!Rd((void*)(R.rulePtr + 0x04), &verify, 4) || verify != target) {
        WrSafe((void*)(R.rulePtr + 0x04), &cur, 4);
        sprintf_s(g_guardianFixStatus, sizeof(g_guardianFixStatus),
            "verify FAILED");
        GuardianFixLogState();
        return;
    }
    R.currentAddS32 = verify;
    if (g_guardianFixArmed) {
        ++g_guardianFixWrites;
        int slot = PartyPriorityLiveSlot(R.prioPtr);
        sprintf_s(g_guardianFixStatus, sizeof(g_guardianFixStatus),
            "APPLIED (code54 = %d) slot=%d writes=%d",
            target, slot, g_guardianFixWrites);
    } else {
        ++g_guardianFixRollbacks;
        sprintf_s(g_guardianFixStatus, sizeof(g_guardianFixStatus),
            "Guardian fix: vanilla (rolled back x%d)", g_guardianFixRollbacks);
    }
    g_guardianFixApplied = g_guardianFixArmed;
    GuardianFixLogState();
}


// ============================================================================
// РЕЕСТР ЭРРАТ (75.45) — статичные починки сломанных ванильных правил
// ============================================================================
//
// Слой B по `docs/ERRATA_ARCHITECTURE.md`. Не «крутим веса по ситуации», а
// намертво снимаем запреты, которых в дизайне быть не должно.
//
// ПОЧЕМУ РЕЕСТР, А НЕ ЕЩЁ ОДИН ФИКС. Правок будет много: Guardian на
// кинжалы, Nexus на магию, дальше остальные. Каждая — своя строка со своим
// отпечатком, своим счётом и своей причиной. Копировать под каждую отдельный
// модуль значит гарантированно развести пять разных багов.
//
// ПОЧЕМУ ПОИСК ПО ПРОВЕРКЕ, А НЕ ПО НОМЕРУ ПРАВИЛА. У строки `code 54`
// порядок оказался такой: rule[0] Guardian primary, rule[1] Guardian
// secondary, rule[2] Scather secondary, rule[3] Scather primary,
// rule[4] Medicant tertiary. Для `code 55` порядок нам НИКТО не обещал, и
// зашивать «rule[0] — это Nexus» значит однажды переписать чужое правило.
//
// Поэтому ищем по содержанию: перебираем правила строки, читаем их проверку
// (склонность + ранг) и берём то, где проверка совпала И значение равно
// ванильному отпечатку. Не совпало ничего — не пишем и говорим об этом.
//
// РАНГ В ПРОВЕРКЕ ПЕРЕВЁРНУТ: 2 = первичная, 1 = вторичная, 0 = третичная.
// Эту грабину уже проходили в 75.33.
//
// СТОРОЖ. Оставлен как страховка и счётчик: лог 18 и 19 показали, что игра
// наши значения НЕ возвращает (`re-asserts 0` за 250 с). Стоит одно чтение
// четырёх байт на строку за тик; пишем только при расхождении.

enum { INCL_GUARDIAN = 5, INCL_NEXUS = 6 };

struct ErrataEntry {
    const char* id;
    const char* what;        // человекочитаемо, для лога
    uint32_t    code;        // строка приоритета
    int         inclId;      // склонность в проверке правила
    int         rank;        // 0 = первичная, 1 = вторичная (наша нумерация)
    int32_t     vanilla;     // отпечаток: что должно лежать в ванили
    int         group;       // 0 = Guardian/кинжалы, 1 = Nexus/магия
    // рантайм
    uintptr_t   rulePtr, ruleVt, prioPtr;
    bool        resolved;
    int         miss;
};

static ErrataEntry g_errata[] = {
    { "E01_guardian_dagger_primary",   "Guardian primary -> WpnDaggerAtk",
      54, INCL_GUARDIAN, 0, -3, 0, 0, 0, 0, false, 0 },
    { "E02_guardian_dagger_secondary", "Guardian secondary -> WpnDaggerAtk",
      54, INCL_GUARDIAN, 1, -2, 0, 0, 0, 0, false, 0 },
    { "E03_nexus_magic_primary",       "Nexus primary -> WpnWandAtk",
      55, INCL_NEXUS,    0, -3, 1, 0, 0, 0, false, 0 },
    { "E04_nexus_magic_secondary",     "Nexus secondary -> WpnWandAtk",
      55, INCL_NEXUS,    1, -2, 1, 0, 0, 0, false, 0 },
};
static const int kErrataCount = (int)(sizeof(g_errata) / sizeof(g_errata[0]));

// Две группы — две галки в панели, два ключа ini, два независимых счёта.
bool    g_errataDaggerOn  = false;   // группа 0
int32_t g_errataDaggerVal = 0;
bool    g_errataMagicOn   = false;   // группа 1
int32_t g_errataMagicVal  = 0;

static bool  g_errataApplied[2]   = {};
static int   g_errataWrites[2]    = {};
static int   g_errataReasserts[2] = {};
static int   g_errataRollbacks[2] = {};
static int   g_errataUserSets[2]  = {};
static int32_t g_errataPrevWant[2] = { 0x7FFFFFFF, 0x7FFFFFFF };
static DWORD g_errataHeldSince[2] = {};
static DWORD g_errataLastTry      = 0;
static char  g_errataStatus[2][220] = { "errata: off", "errata: off" };

static bool  ErrataGroupOn(int g)  { return g ? g_errataMagicOn  : g_errataDaggerOn; }
static int32_t ErrataGroupVal(int g){ return g ? g_errataMagicVal : g_errataDaggerVal; }

const char* ErrataStatus(int group)
{
    return (group >= 0 && group < 2) ? g_errataStatus[group] : "";
}
bool ErrataIsApplied(int group)
{
    return (group >= 0 && group < 2) ? g_errataApplied[group] : false;
}
int ErrataReasserts(int group)
{
    return (group >= 0 && group < 2) ? g_errataReasserts[group] : 0;
}

static void ErrataLogState(int g)
{
    static char prev[2][220] = {};
    if (!strcmp(prev[g], g_errataStatus[g])) return;
    lstrcpynA(prev[g], g_errataStatus[g], sizeof(prev[g]));
    logFile << "Errata[" << (g ? "nexus-magic" : "dagger-ban") << "]: "
            << g_errataStatus[g] << std::endl;
}

// cAIPriorityThink главной пешки. Дёшево, без census.
static uintptr_t ErrataThink()
{
    const uintptr_t pawn = MainPawnBody();
    if (!pawn) return 0;
    uintptr_t ctrl = 0;
    if (RdPtr((void*)(pawn + 0x2E64), &ctrl) && ctrl) {
        char nm[48] = {};
        if (!NameOfLiveObject(ctrl, nm, sizeof(nm)) || strcmp(nm, "cAICtrl") != 0)
            ctrl = 0;
    }
    if (!ctrl) ctrl = FindChildByClass(pawn, kPartyBodySize, "cAICtrl", 0);
    if (!ctrl) return 0;
    return FindChildByClass(ctrl, 704, "cAIPriorityThink", 0);
}

// Найти правило по СОДЕРЖАНИЮ: строка с нужным кодом, внутри — правило, чья
// проверка совпала со склонностью и рангом, а значение равно ванильному
// отпечатку (или уже нашему desired — тогда это повторное разрешение).
static bool ErrataResolve(ErrataEntry& E, int32_t desired)
{
    const uintptr_t think = ErrataThink();
    if (!think) return false;

    for (int slot = 0; slot < 48; ++slot) {
        uint32_t D[5] = {};
        if (!Rd((void*)(think + 0x38u + (uint32_t)slot * 0x14u), D, sizeof(D))) continue;
        const uint32_t count = D[1], capacity = D[2];
        const uintptr_t arr = D[4];
        if (!count || count > 16u || capacity > 16u || count > capacity
            || !LooksHeap(arr)) continue;
        uintptr_t ptrs[16] = {};
        if (!Rd((void*)arr, ptrs, count * sizeof(uintptr_t))) continue;

        for (uint32_t n = 0; n < count; ++n) {
            const uintptr_t prio = ptrs[n];
            if (!LooksHeap(prio)) continue;
            char nm[64] = {};
            if (!NameOfLiveObject(prio, nm, sizeof(nm))
                || strcmp(nm, "rAIPriorityThink::cPrioParam") != 0) continue;
            uint32_t raw[16] = {};
            if (!Rd((void*)prio, raw, sizeof(raw))) continue;
            if (raw[2] != E.code) continue;
            const uint32_t nPers = raw[7];
            const uintptr_t persArr = raw[10];
            if (!nPers || nPers > 16u || !LooksHeap(persArr)) continue;
            uintptr_t rulePtr[16] = {};
            if (!Rd((void*)persArr, rulePtr, nPers * sizeof(uintptr_t))) continue;

            for (uint32_t k = 0; k < nPers; ++k) {
                const uintptr_t rp = rulePtr[k];
                if (!LooksHeap(rp)) continue;
                uint32_t C[10] = {};
                if (!Rd((void*)rp, C, sizeof(C)) || !LooksLikeVtable(C[0])) continue;
                const int32_t addS32 = (int32_t)C[1];
                const uint32_t nChk = C[5];
                const uintptr_t chkArr = C[8];
                if (!nChk || nChk > 8u || !LooksHeap(chkArr)) continue;
                uintptr_t cp = 0;
                if (!RdPtr((void*)chkArr, &cp) || !LooksHeap(cp)) continue;
                uint32_t K[3] = {};
                if (!Rd((void*)cp, K, sizeof(K))) continue;
                const int inclId = (int)K[1];
                const int rank   = (int)K[2];      // 2 = primary!
                if (inclId != E.inclId) continue;
                if (rank != (2 - E.rank)) continue;
                if (addS32 != E.vanilla && addS32 != desired) {
                    // Проверка совпала, а значение — нет. Это чужая правка
                    // или другой патч игры. Молча писать поверх нельзя.
                    char l[240];
                    sprintf_s(l, "Errata[%s]: FINGERPRINT MISMATCH - expected"
                                 " vanilla %+d, found %+d. NOT writing.",
                              E.id, E.vanilla, addS32);
                    logFile << l << std::endl;
                    return false;
                }
                E.prioPtr = prio;
                E.rulePtr = rp;
                E.ruleVt  = C[0];
                E.resolved = true;
                return true;
            }
        }
    }
    return false;
}

void ErrataRestore(int group)
{
    for (int i = 0; i < kErrataCount; ++i) {
        ErrataEntry& E = g_errata[i];
        if (E.group != group || !E.resolved) continue;
        uintptr_t vt = 0;
        if (RdPtr((void*)E.rulePtr, &vt) && vt == E.ruleVt) {
            const int32_t van = E.vanilla;
            if (WrSafe((void*)(E.rulePtr + 0x04), &van, 4)) ++g_errataRollbacks[group];
        }
        E.resolved = false;
    }
    if (!g_errataApplied[group]) return;
    g_errataApplied[group] = false;
    sprintf_s(g_errataStatus[group], sizeof(g_errataStatus[group]),
              "rolled back to vanilla (x%d)", g_errataRollbacks[group]);
    ErrataLogState(group);
}

void ErrataRestoreAll() { ErrataRestore(0); ErrataRestore(1); }

// Одна строка: разрешить, сверить, при расхождении переписать.
static bool ErrataOne(ErrataEntry& E, int32_t want, DWORD now)
{
    const int g = E.group;
    if (!E.resolved) {
        if (!ErrataResolve(E, want)) { ++E.miss; return false; }
        E.miss = 0;
        char l[220];
        sprintf_s(l, "Errata[%s]: resolved (code %u, %s, vanilla %+d)",
                  E.id, E.code, E.what, E.vanilla);
        logFile << l << std::endl;
    }
    uintptr_t vt = 0;
    int32_t cur = 0;
    if (!RdPtr((void*)E.rulePtr, &vt) || vt != E.ruleVt
        || !Rd((void*)(E.rulePtr + 0x04), &cur, 4)) {
        E.resolved = false;
        return false;
    }
    if (cur == want) return true;

    const bool userChanged = (g_errataPrevWant[g] != 0x7FFFFFFF
                              && g_errataPrevWant[g] != want);
    const bool wasReset = g_errataApplied[g] && !userChanged;

    if (!WrSafe((void*)(E.rulePtr + 0x04), &want, 4)) return false;
    int32_t back = 0;
    if (!Rd((void*)(E.rulePtr + 0x04), &back, 4) || back != want) {
        WrSafe((void*)(E.rulePtr + 0x04), &cur, 4);
        return false;
    }

    char l[260];
    if (userChanged) {
        ++g_errataUserSets[g];
        sprintf_s(l, "Errata[%s]: target changed by hand to %+d (was %+d);"
                     " NOT a re-assert", E.id, want, cur);
        logFile << l << std::endl;
    } else if (wasReset) {
        ++g_errataReasserts[g];
        if (g_errataReasserts[g] <= 10 || (g_errataReasserts[g] % 50) == 0) {
            sprintf_s(l, "Errata[%s]: the game put %+d back; re-applied %+d"
                         " (re-assert #%d)", E.id, cur, want, g_errataReasserts[g]);
            logFile << l << std::endl;
        }
    } else {
        ++g_errataWrites[g];
        sprintf_s(l, "Errata[%s]: applied %+d over vanilla %+d (write #%d)",
                  E.id, want, cur, g_errataWrites[g]);
        logFile << l << std::endl;
    }
    g_errataHeldSince[g] = now;
    return true;
}

static void ErrataTickGroup(int g, DWORD now, bool mayResolve)
{
    if (!ErrataGroupOn(g)) {
        if (g_errataApplied[g]) ErrataRestore(g);
        else {
            lstrcpynA(g_errataStatus[g], "off (vanilla)", sizeof(g_errataStatus[g]));
            ErrataLogState(g);
        }
        return;
    }
    const int32_t want = ErrataGroupVal(g);
    int ok = 0, total = 0;
    for (int i = 0; i < kErrataCount; ++i) {
        if (g_errata[i].group != g) continue;
        ++total;
        if (!g_errata[i].resolved && !mayResolve) continue;
        if (ErrataOne(g_errata[i], want, now)) ++ok;
    }
    g_errataPrevWant[g] = want;
    if (!ok) {
        g_errataApplied[g] = false;
        lstrcpynA(g_errataStatus[g], "rules not resolved yet (no pawn in world?)",
                  sizeof(g_errataStatus[g]));
        ErrataLogState(g);
        return;
    }
    if (!g_errataApplied[g]) {
        g_errataApplied[g] = true;
        g_errataHeldSince[g] = now;
    }
    const unsigned heldS = (now - g_errataHeldSince[g]) / 1000;
    sprintf_s(g_errataStatus[g], sizeof(g_errataStatus[g]),
        "APPLIED to %d of %d ranks (%+d) | held %u s | writes %d, re-asserts %d,"
        " %d hand changes",
        ok, total, want, heldS, g_errataWrites[g], g_errataReasserts[g],
        g_errataUserSets[g]);
    static DWORD lastBeat[2] = {};
    if (!lastBeat[g] || now - lastBeat[g] > 30000) {
        lastBeat[g] = now;
        logFile << "Errata[" << (g ? "nexus-magic" : "dagger-ban") << "]: "
                << g_errataStatus[g] << std::endl;
    }
}

void ErrataTick()
{
    const DWORD now = GetTickCount();

    // Поиск дороже проверки, поэтому его пускаем по расписанию. Отступ
    // после трёх пустых попыток — урок, который в проекте уже был записан.
    bool needResolve = false;
    int worstMiss = 0;
    for (int i = 0; i < kErrataCount; ++i) {
        if (!ErrataGroupOn(g_errata[i].group)) continue;
        if (!g_errata[i].resolved) needResolve = true;
        if (g_errata[i].miss > worstMiss) worstMiss = g_errata[i].miss;
    }
    bool mayResolve = true;
    if (needResolve) {
        const DWORD wait = (worstMiss >= 3) ? 15000u : 500u;
        if (g_errataLastTry && now - g_errataLastTry < wait) mayResolve = false;
        else g_errataLastTry = now;
    }

    ErrataTickGroup(0, now, mayResolve);
    ErrataTickGroup(1, now, mayResolve);
}

} // namespace Runtime
