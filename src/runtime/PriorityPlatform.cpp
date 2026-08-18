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

bool PartyPriorityProfileResolveRule(PartyPriorityProfileRule& R)
{
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
            const bool allowDiscover = now - g_priorityProfileWorldSince >= 5000u
                && (!g_priorityProfileLastDiscover
                    || now - g_priorityProfileLastDiscover >= 30000u);
            if (allowDiscover) {
                g_priorityProfileLastDiscover = now;
                if (PartyPriorityProfileAutoDiscover())
                    PartyPriorityProfileApplyAll();
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

void GuardianFixSetTarget(int32_t desiredAddS32)
{
    GuardianFixInitOnce();
    g_guardianFixRule.desiredAddS32 = desiredAddS32;
    // desired == vanilla (-3) → откат; иначе — активен.
    g_guardianFixArmed = (desiredAddS32 != g_guardianFixRule.expectedAddS32);
}

bool GuardianFixIsApplied()
{
    return g_guardianFixApplied;
}

const char* GuardianFixStatus()
{
    return g_guardianFixStatus;
}

// Build 58: единый tick с градиентом. Целевое значение = desired (если armed)
// или expected (rollback). Каждый тик читаем текущее, при расхождении —
// write + readback (verify) + откат к прежнему при неудаче. Поддерживает
// плавную смену desired (градиент: -3 → 0 → +2 по дистанции угрозы).
void GuardianFixTick()
{
    GuardianFixInitOnce();
    PartyPriorityProfileRule& R = g_guardianFixRule;

    if (g_guardianFixArmed && !R.resolved) {
        if (!PartyPriorityProfileResolveRule(R)) {
            sprintf_s(g_guardianFixStatus, sizeof(g_guardianFixStatus),
                "Guardian fix: ARMED, rule not resolved (census pending)");
            return;
        }
    }
    if (!R.resolved) {
        g_guardianFixApplied = false;
        sprintf_s(g_guardianFixStatus, sizeof(g_guardianFixStatus),
            "Guardian fix: disabled (not resolved)");
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
            "Guardian fix: rule lost");
        return;
    }
    R.currentAddS32 = cur;

    if (cur == target) {
        g_guardianFixApplied = g_guardianFixArmed;
        if (g_guardianFixArmed) {
            int slot = PartyPriorityLiveSlot(R.prioPtr);
            sprintf_s(g_guardianFixStatus, sizeof(g_guardianFixStatus),
                "Guardian fix: APPLIED (code54 = %d) slot=%d writes=%d",
                target, slot, g_guardianFixWrites);
        } else {
            sprintf_s(g_guardianFixStatus, sizeof(g_guardianFixStatus),
                "Guardian fix: vanilla (rolled back x%d)", g_guardianFixRollbacks);
        }
        return;
    }

    // Переход к целевому значению: write + readback (verify), при неудаче —
    // вернуть прежнее.
    if (!WrSafe((void*)(R.rulePtr + 0x04), &target, 4)) {
        sprintf_s(g_guardianFixStatus, sizeof(g_guardianFixStatus),
            "Guardian fix: write FAILED");
        return;
    }
    int32_t verify = 0;
    if (!Rd((void*)(R.rulePtr + 0x04), &verify, 4) || verify != target) {
        WrSafe((void*)(R.rulePtr + 0x04), &cur, 4);
        sprintf_s(g_guardianFixStatus, sizeof(g_guardianFixStatus),
            "Guardian fix: verify FAILED");
        return;
    }
    R.currentAddS32 = verify;
    if (g_guardianFixArmed) {
        ++g_guardianFixWrites;
        int slot = PartyPriorityLiveSlot(R.prioPtr);
        sprintf_s(g_guardianFixStatus, sizeof(g_guardianFixStatus),
            "Guardian fix: APPLIED (code54 = %d) slot=%d writes=%d",
            target, slot, g_guardianFixWrites);
    } else {
        ++g_guardianFixRollbacks;
        sprintf_s(g_guardianFixStatus, sizeof(g_guardianFixStatus),
            "Guardian fix: vanilla (rolled back x%d)", g_guardianFixRollbacks);
    }
    g_guardianFixApplied = g_guardianFixArmed;
}

} // namespace Runtime
