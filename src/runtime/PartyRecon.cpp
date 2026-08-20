// Runtime::Party — поиск и разбор тел партии (uPlayer/uCmc), роли, позиции
// Аризена и главной пешки. Продуктовый слой: позиции читает доктрина
// и Camera Plus, а не исследование.

#include "stdafx.h"
#include "RuntimeInternal.h"
#include "../TypeAtlas.Generated.h"
#include <stdio.h>

namespace Runtime {

bool PartyStartsWith(const char* s, const char* prefix)
{
    if (!s || !prefix) return false;
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

bool PartyRelevantName(const char* n)
{
    if (!n || !n[0]) return false;
    return PartyStartsWith(n, "cPlAct")
        || PartyStartsWith(n, "cCmc")
        || PartyStartsWith(n, "uCmc")
        || PartyStartsWith(n, "uPawn")
        || PartyStartsWith(n, "cAIPlayer")
        || PartyStartsWith(n, "rAIPlayer")
        || strstr(n, "ActionManager") != nullptr
        || strstr(n, "ActBank") != nullptr
        || strstr(n, "Motion") != nullptr
        || strstr(n, "Status") != nullptr
        || strstr(n, "Stamina") != nullptr
        || strstr(n, "Health") != nullptr
        || strstr(n, "AICtrl") != nullptr
        || strstr(n, "Think") != nullptr;
}

bool PartyBlockHasPtr(const BYTE* data, uint32_t bytes, uintptr_t want)
{
    if (!data || !want || bytes < 4) return false;
    for (uint32_t off = 0; off + 4 <= bytes; off += 4)
        if (*(const uint32_t*)(data + off) == (uint32_t)want) return true;
    return false;
}

void PartyNoteValueHit(PartyBodyDump& P, uint32_t containerOff,
                              const char* container, uint32_t valueOff,
                              const char* label, const char* encoding)
{
    if (P.nValueHit >= kPartyMaxValueHits) return;
    PartyValueHit& H = P.valueHit[P.nValueHit++];
    memset(&H, 0, sizeof(H));
    H.containerOff = containerOff;
    H.valueOff = valueOff;
    lstrcpynA(H.container, container ? container : "?", sizeof(H.container));
    lstrcpynA(H.label, label ? label : "?", sizeof(H.label));
    lstrcpynA(H.encoding, encoding ? encoding : "?", sizeof(H.encoding));
}

void PartyScanKnownValues(PartyBodyDump& P, const BYTE* data, uint32_t bytes,
                                 const char* container, uint32_t containerOff)
{
    if (!data || bytes < 4) return;
    for (uint32_t off = 0; off + 4 <= bytes; off += 4) {
        uint32_t raw = *(const uint32_t*)(data + off);
        for (int k = 0; k < kPartyKnownValueCount; ++k) {
            if (raw == (uint32_t)kPartyKnownValues[k].value)
                PartyNoteValueHit(P, containerOff, container, off,
                                  kPartyKnownValues[k].label, "i32");
            float fv = (float)kPartyKnownValues[k].value;
            uint32_t fraw = 0;
            memcpy(&fraw, &fv, sizeof(fraw));
            if (raw == fraw)
                PartyNoteValueHit(P, containerOff, container, off,
                                  kPartyKnownValues[k].label, "f32");
        }
    }
}

void PartyRememberNearType(const char* name, uintptr_t vt, uintptr_t sample)
{
    if (!name || (!strstr(name, "Player") && !strstr(name, "Pawn")
               && !strstr(name, "Cmc"))) return;
    for (int i = 0; i < g_nPartyNear; ++i)
        if (g_partyNear[i].vt == vt) return;
    if (g_nPartyNear >= kPartyMaxNearTypes) return;
    PartyNearType& N = g_partyNear[g_nPartyNear++];
    memset(&N, 0, sizeof(N));
    N.vt = vt;
    N.sample = sample;
    lstrcpynA(N.name, name, sizeof(N.name));
}

bool PartyRuntimeProbeName(const char* name)
{
    if (!name || !name[0]) return false;
    return strstr(name, "Stamina") != nullptr
        || strstr(name, "Health") != nullptr
        || !strcmp(name, "rStatusParam");
}

int PartyRuntimeProbePriority(const char* name)
{
    if (!name) return 0;
    if (!strcmp(name, "cPlStamina")) return 4;
    if (strstr(name, "Health")) return 3;
    if (name[0] == 'c' && strstr(name, "Stamina")) return 2;
    if (!strcmp(name, "rStatusParam")) return 1;
    return 0; // rPlStamina and other rule resources
}

void PartyAddRuntimeProbe(uintptr_t obj, uintptr_t vt, const char* name)
{
    if (!obj || !vt || !PartyRuntimeProbeName(name)) return;
    for (int i = 0; i < g_nPartyRuntime; ++i)
        if (g_partyRuntime[i].ptr == obj) return;

    int slot = g_nPartyRuntime;
    if (slot >= kPartyMaxRuntimeProbes) {
        int weakest = 0;
        for (int i = 1; i < g_nPartyRuntime; ++i)
            if (PartyRuntimeProbePriority(g_partyRuntime[i].name)
                < PartyRuntimeProbePriority(g_partyRuntime[weakest].name))
                weakest = i;
        if (PartyRuntimeProbePriority(name)
            <= PartyRuntimeProbePriority(g_partyRuntime[weakest].name)) return;
        slot = weakest;
    }

    BYTE head[kPartyRuntimeProbeBytes];
    if (!Rd((void*)obj, head, sizeof(head))) return;

    PartyRuntimeProbe& R = g_partyRuntime[slot];
    memset(&R, 0, sizeof(R));
    R.ptr = obj;
    R.vt = vt;
    lstrcpynA(R.name, name, sizeof(R.name));
    memcpy(R.head, head, sizeof(head));
    R.headOk = true;
    if (slot == g_nPartyRuntime) ++g_nPartyRuntime;
}

bool PawnAiRelevantName(const char* name)
{
    if (!name || !name[0]) return false;

    // Build 48 is a semantic-linking census. Hundreds of inline runtime
    // PlanCtrl/PlanResult/GoalInfoParam objects are derivable from the planner
    // root and previously exhausted the 1024 cap after reload. Keep only the
    // roots plus compact resources needed to link priority code -> GOAP.
    if (PartyStartsWith(name, "cCmc")) return true;
    if (!strcmp(name, "cAIGoalPlanning")) return true;
    if (!strcmp(name, "rAIGoalPlanning")) return true;
    if (!strcmp(name, "rAIPriorityThink")
        || !strcmp(name, "cAIPriorityThink")
        || !strcmp(name, "rAIPriorityThink::cPrioParam")
        || !strcmp(name, "rAIPriorityThink::cOrderValue")
        || !strcmp(name, "rAIPriorityThink::cCodeParam")
        || !strcmp(name, "rAIPlayerActionParameter")
        || !strcmp(name, "cAICheckSituationCmc")
        || !strcmp(name, "cAIActionInterfaceCmc"))
        return true;
    return false;
}

void PartyAddPawnAiCandidate(uintptr_t obj, uintptr_t vt, const char* name)
{
    if (!obj || !vt || !PawnAiRelevantName(name)) return;
    for (int i = 0; i < g_nPawnAi; ++i)
        if (g_pawnAi[i].ptr == obj) return;
    if (g_nPawnAi >= kPawnAiMaxCandidates) return;

    const TypeAtlas::Info* info = TypeAtlas::FindByName(name);
    PawnAiCandidate& A = g_pawnAi[g_nPawnAi++];
    memset(&A, 0, sizeof(A));
    A.ptr = obj;
    A.vt = vt;
    A.typeSize = info ? info->size : 0;
    lstrcpynA(A.name, name, sizeof(A.name));
}

int PartyPriorityLiveSlot(uintptr_t prioParam)
{
    if (!prioParam) return -1;

    // СНАЧАЛА ЖИВЫЕ ВЁДРА ГЛАВНОЙ ПЕШКИ, ПОТОМ CENSUS.
    //
    // В логе 75.26 фикс писал `slot=-1` при каждом применении: номер ведра
    // искался по списку `g_pawnAi`, который наполняет полный census, а он
    // в сессии не отработал. Между тем ровно те же вёдра доступны прямым
    // путём от тела пешки — им и пользуемся.
    //
    // Номер ведра здесь не украшение: это ЕДИНСТВЕННЫЙ способ увидеть, на
    // сколько наша правка реально подвинула строку.
    {
        const uintptr_t pawn = MainPawnBody();
        uintptr_t ctrl = 0;
        if (pawn && RdPtr((void*)(pawn + 0x2E64), &ctrl) && ctrl) {
            char nm[48] = {};
            if (!NameOfLiveObject(ctrl, nm, sizeof(nm)) || strcmp(nm, "cAICtrl") != 0)
                ctrl = 0;
        }
        if (!ctrl && pawn) ctrl = FindChildByClass(pawn, kPartyBodySize, "cAICtrl", 0);
        const uintptr_t think = ctrl
            ? FindChildByClass(ctrl, 704, "cAIPriorityThink", 0) : 0;
        if (think) {
            for (int slot = 0; slot < 48; ++slot) {
                uint32_t D[5] = {};
                if (!Rd((void*)(think + 0x38u + (uint32_t)slot * 0x14u), D, sizeof(D)))
                    continue;
                if (!D[1] || D[1] > 16u || D[2] > 16u || D[1] > D[2]
                    || !LooksHeap(D[4])) continue;
                uintptr_t entries[16] = {};
                if (!Rd((void*)(uintptr_t)D[4], entries, D[1] * sizeof(uintptr_t)))
                    continue;
                for (uint32_t n = 0; n < D[1]; ++n)
                    if (entries[n] == prioParam) return slot;
            }
        }
    }

    // Build 57.2: искать во ВСЕХ экземплярах cAIPriorityThink, а не только
    // в первом. Раньше брался первый и выходили — если у пешки несколько
    // приоритетных корней (или первый — чужая пешка), slot давал -1.
    for (int i = 0; i < g_nPawnAi; ++i) {
        PawnAiCandidate& root = g_pawnAi[i];
        if (strcmp(root.name, "cAIPriorityThink")) continue;

        for (int slot = 0; slot < 48; ++slot) {
            const uintptr_t field = root.ptr + 0x38u + (uint32_t)slot * 0x14u;
            uint32_t before[5] = {};
            uint32_t after[5] = {};
            uintptr_t entries[16] = {};
            if (!Rd((void*)field, before, sizeof(before))) continue;
            if (before[1] > 16u || before[2] > 16u || before[1] > before[2]) continue;
            if (before[1] && (!LooksHeap(before[4])
                || !Rd((void*)(uintptr_t)before[4], entries,
                    before[1] * sizeof(uintptr_t))))
                continue;
            if (!Rd((void*)field, after, sizeof(after))
                || memcmp(before, after, sizeof(before)) != 0)
                continue;
            for (uint32_t n = 0; n < before[1]; ++n)
                if (entries[n] == prioParam) return slot;
        }
    }
    return -1;
}

PartyVtClass* PartyClassifyVt(uintptr_t vt, uintptr_t sample)
{
    if (!vt || !InImage(vt)) return nullptr;
    uint32_t idx = (uint32_t)(((vt >> 4) ^ (vt >> 16)) & (kPartyVtCacheSize - 1));
    for (int probe = 0; probe < kPartyVtCacheSize; ++probe) {
        PartyVtClass& C = g_partyVtCache[(idx + probe) & (kPartyVtCacheSize - 1)];
        if (C.vt == vt) return &C;
        if (C.vt != 0) continue;

        C.vt = vt;
        C.kind = PVK_OTHER;
        C.name[0] = 0;
        ++g_partyVtChecked;

        char name[64] = {};
        if (NameOfLiveObject(sample, name, sizeof(name)) && name[0]) {
            ++g_partyVtNamed;
            lstrcpynA(C.name, name, sizeof(C.name));
            if (!strcmp(name, "uPlayer") || !strcmp(name, "uCmc"))
                C.kind = PVK_PARTY_BODY;
            else if (!strcmp(name, "sPawnManager"))
                C.kind = PVK_PAWN_MANAGER;
            PartyRememberNearType(name, vt, sample);
        }
        return &C;
    }
    return nullptr;
}

void PartyAddBodyCandidate(uintptr_t obj, uintptr_t wantVt, const char* dtiName)
{
    if (!obj || !dtiName || g_nParty >= kPartyMaxBodies) return;
    uint32_t bodySize = 0;
    if (!strcmp(dtiName, "uPlayer")) bodySize = kPartyBodySize;
    else if (!strcmp(dtiName, "uCmc")) bodySize = kCmcBodySize;
    else return;

    for (int i = 0; i < g_nParty; ++i)
        if (g_party[i].ptr == obj) return;

    uintptr_t vt = 0;
    if (!RdPtr((void*)obj, &vt) || vt != wantVt) return;
    BYTE tail = 0;
    if (!Rd((void*)(obj + bodySize - 1), &tail, 1)) return;

    PartyBodyDump& P = g_party[g_nParty++];
    memset(&P, 0, sizeof(P));
    P.ptr = obj;
    P.vt = vt;
    P.bodySize = bodySize;
    lstrcpynA(P.dti, dtiName, sizeof(P.dti));
}

void PartyAddPawnManagerCandidate(uintptr_t obj, uintptr_t wantVt)
{
    if (!obj || g_nPartyPawnMgr >= 8) return;
    for (int i = 0; i < g_nPartyPawnMgr; ++i)
        if (g_partyPawnMgr[i] == obj) return;
    uintptr_t vt = 0;
    if (!RdPtr((void*)obj, &vt) || vt != wantVt) return;
    BYTE tail = 0;
    if (!Rd((void*)(obj + kPawnManagerSize - 1), &tail, 1)) return;
    g_partyPawnMgr[g_nPartyPawnMgr++] = obj;
}

// Build 60: partyOnly=true — ранний выход, как только найдены ОБА тела
// (uPlayer и uCmc). Позиции нужны лишь от этих двух; полный проход до
// 0x7FFF0000 ради priority/targetSel при позиционном трекинге не нужен и
// давал ~1.5 с на каждую итерацию. При partyOnly не собираем runtime/pawnAi/
// targetSel (они для профиля/аудита) — только тела.
// Сколько пешек ожидать в скане. Ставится обходом актёров, который и так
// идёт каждый тик и видит живые тела дёшево.
//
// ЗАЧЕМ ЭТО ЧИСЛО. Ранний выход из скана памяти раньше был «нашли одного
// uPlayer и одного uCmc — хватит». С наёмными это неверно, а «искать
// всегда всех» означало бы полный проход по всей приватной памяти на
// каждом пересканировании. Ожидаемое число даёт и правильность, и
// короткий скан: остановились ровно тогда, когда нашли всех, кого видно
// в мире.
//
// ОБЪЯВЛЕНО ЗДЕСЬ, А НЕ РЯДОМ С ПОТРЕБИТЕЛЕМ. Первая версия положила
// определение возле тика партии — то есть на триста строк НИЖЕ скана,
// который его читает. MSVC ответил C2065; g++-харнесс промолчал, потому
// что PartyRecon.cpp он не компилирует вовсе. Правило простое: файловый
// статик живёт выше первого использования, а не там, где о нём удобнее
// рассказать.
static volatile LONG g_partyExpectPawns = 1;

// --- СОСТАВ ПАРТИИ ПО ЗАПИСЯМ ПЕРСОНАЖЕЙ ------------------------------------
//
// ЧУЖОЙ МОД ПОДСКАЗАЛ ПРАВИЛЬНЫЙ СЛОЙ. `ddda-dinput8` показывает статы
// всей партии и не ищет живые тела ВООБЩЕ: он читает записи персонажей по
// фиксированным смещениям —
//
//     игрок        pBase + 0xA7000
//     своя пешка   + 0x7F0
//     наёмная 1    + 0x7F0 + 0x1660
//     наёмная 2    + 0x7F0 + 0x1660 * 2
//
// Наш разбор искал ТЕЛА в куче и на наёмных спотыкался: перекличка
// упорно показывала одну пешку. Причина в том, что и обнаружение, и
// ранний выход из скана опирались на список живых актёров, а он
// ограничен 32 записями и в лагере забивается гоблинами.
//
// Записи же лежат по фиксированным адресам, читаются четырьмя чтениями и
// не зависят ни от куч, ни от порядка обхода. Это надёжный источник
// СОСТАВА. Тела по-прежнему нужны (ИИ живёт в них), но теперь мы знаем,
// сколько их искать и как отличить своих от наёмных: тело содержит
// указатель на свою запись.
static uintptr_t PartyRecordAddr(int idx)
{
    if (!pBase || !*pBase || idx < 0 || idx > 2) return 0;
    return (uintptr_t)(*pBase) + 0xA7000 + 0x7F0 + (uintptr_t)idx * 0x1660;
}

// Запись живая и осмысленная? Проверяем тем же набором, что и панель
// склонностей: вокация 1..9, уровень 1..250, максимальное HP в пределах.
static bool PartyRecordValid(int idx, int* vocOut, int* lvlOut)
{
    if (vocOut) *vocOut = 0;
    if (lvlOut) *lvlOut = 0;
    const uintptr_t rec = PartyRecordAddr(idx);
    if (!rec) return false;
    int32_t voc = 0;
    uint16_t lvl = 0;
    float maxHp = 0.0f;
    if (!Rd((void*)(rec + 0x6E0), &voc, 4)) return false;
    if (!Rd((void*)(rec + 0xDD0), &lvl, 2)) return false;
    if (!Rd((void*)(rec + 0x970), &maxHp, 4)) return false;
    if (voc < 1 || voc > 9) return false;
    if (lvl == 0 || lvl > 250) return false;
    if (!(maxHp > 0.0f) || maxHp > 200000.0f) return false;
    if (vocOut) *vocOut = (int)voc;
    if (lvlOut) *lvlOut = (int)lvl;
    return true;
}

int PartyRecordPawnCount()
{
    int n = 0;
    for (int i = 0; i <= 2; ++i)
        if (PartyRecordValid(i, 0, 0)) ++n;
    return n;
}

// Полная карточка по индексу записи: вокация, уровень и найденное для неё
// живое тело (0, если тело ещё не найдено). Нужна перекличке, чтобы
// показывать состав ДАЖЕ когда скан тел не справился.
bool PartyRecordInfo(int idx, int* vocOut, int* lvlOut, uintptr_t* bodyOut)
{
    if (bodyOut) *bodyOut = 0;
    if (!PartyRecordValid(idx, vocOut, lvlOut)) return false;
    if (bodyOut) {
        for (int i = 0; i < g_nParty; ++i)
            if (!strcmp(g_party[i].dti, "uCmc") && g_party[i].pawnRecordIdx == idx) {
                *bodyOut = g_party[i].ptr;
                break;
            }
    }
    return true;
}

void PartySetExpectedPawns(int n)
{
    if (n < 1) n = 1;
    if (n > 4) n = 4;
    InterlockedExchange(&g_partyExpectPawns, (LONG)n);
}

void PartyFindBodies(bool partyOnly)
{
    g_nParty = 0;
    g_partyRawCandidates = 0;
    int nCmcFound = 0;
    g_nPartyPawnMgr = 0;
    g_partyVtChecked = 0;
    g_partyVtNamed = 0;
    g_nPartyNear = 0;
    g_nPartyRuntime = 0;
    g_nPawnAi = 0;
    memset(g_partyRuntime, 0, sizeof(g_partyRuntime));
    memset(g_pawnAi, 0, sizeof(g_pawnAi));
    memset(g_partyVtCache, 0, sizeof(g_partyVtCache));
    if (!g_base) return;

    bool havePlayer = false;
    DWORD t0 = MsNow();
    uintptr_t addr = 0x00010000u;
    MEMORY_BASIC_INFORMATION mbi;
    memset(&mbi, 0, sizeof(mbi));
    bool done = false;
    while (addr < 0x7FFF0000u && !done) {
        SIZE_T got = VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi));
        if (!got) break;
        uintptr_t start = (uintptr_t)mbi.BaseAddress;
        uintptr_t end = start + mbi.RegionSize;
        if (end <= addr) break;

        DWORD prot = mbi.Protect & 0xFF;
        bool readable = prot == PAGE_READONLY || prot == PAGE_READWRITE
                     || prot == PAGE_WRITECOPY || prot == PAGE_EXECUTE_READ
                     || prot == PAGE_EXECUTE_READWRITE;
        bool scan = mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE
                 && readable && !(mbi.Protect & PAGE_GUARD);
        if (scan) {
            __try {
                const uint32_t* p = (const uint32_t*)start;
                uint32_t n = (uint32_t)((end - start) / 4);
                for (uint32_t i = 0; i < n; ++i) {
                    uintptr_t obj = start + (uintptr_t)i * 4;
                    uintptr_t vt = p[i];
                    if (!InImage(vt) || !LooksLikeVtable(vt)) continue;

                    // No hardcoded instance-vtable. Classify each distinct
                    // genuine vtable once by asking the live object for its DTI name.
                    PartyVtClass* C = PartyClassifyVt(vt, obj);
                    if (!C) continue;
                    if (C->kind == PVK_PARTY_BODY) {
                        PartyAddBodyCandidate(obj, vt, C->name);
                        if (partyOnly) {
                            // РАННИЙ ВЫХОД ПЕРЕСТАЁТ РАБОТАТЬ, КАК ТОЛЬКО
                            // В ПАРТИИ ПОЯВЛЯЮТСЯ НАЁМНЫЕ ПЕШКИ.
                            //
                            // Наёмная пешка — тоже `uCmc`. Условие «нашли
                            // одного uPlayer и одного uCmc — выходим»
                            // останавливало скан на ПЕРВОМ попавшемся теле
                            // пешки в порядке памяти. С одной пешкой это
                            // всегда была главная; с наёмными это
                            // подбрасывание монетки, и дальше весь продукт
                            // (ускорение, доктрина, DashWatch) адресовал бы
                            // чужую пешку.
                            //
                            // Теперь ждём ВСЕХ: партия — это Восставший
                            // плюс до трёх пешек. Скан всё так же короткий,
                            // потому что тела лежат рядом.
                            if (!strcmp(C->name, "uPlayer")) havePlayer = true;
                            else if (!strcmp(C->name, "uCmc")) ++nCmcFound;
                            const int want = (int)InterlockedCompareExchange(
                                &g_partyExpectPawns, 0, 0);
                            if (havePlayer && nCmcFound >= want) { done = true; break; }
                            continue; // не собираем pawnAi/targetSel в fast-режиме
                        }
                    } else if (C->kind == PVK_PAWN_MANAGER) {
                        PartyAddPawnManagerCandidate(obj, vt);
                        if (partyOnly) continue;
                    }
                    if (!partyOnly && C->name[0]) {
                        PartyAddRuntimeProbe(obj, vt, C->name);
                        PartyAddPawnAiCandidate(obj, vt, C->name);
                    }
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
        }
        addr = end;
    }
    g_partyRawCandidates = g_nParty;
    g_partyFindMs = MsNow() - t0;
}

void PartyInspectBody(PartyBodyDump& P)
{
    uintptr_t ptr = P.ptr;
    uintptr_t vt = P.vt;
    uint32_t bodySize = P.bodySize;
    char dti[40];
    lstrcpynA(dti, P.dti, sizeof(dti));
    memset(&P, 0, sizeof(P));
    P.ptr = ptr;
    P.vt = vt;
    P.bodySize = bodySize;
    lstrcpynA(P.dti, dti, sizeof(P.dti));
    P.bodyOk = P.bodySize > 0 && P.bodySize <= sizeof(P.body)
            && Rd((void*)P.ptr, P.body, P.bodySize);
    if (!P.bodyOk) return;

    uintptr_t playerRecord = 0;
    uintptr_t mainPawnRecord = 0;
    if (pBase && *pBase) {
        playerRecord = (uintptr_t)(*pBase + 0xA7000);
        mainPawnRecord = playerRecord + 0x7F0;
    }
    P.playerRecordRef = PartyBlockHasPtr(P.body, P.bodySize, playerRecord);
    P.mainPawnRecordRef = PartyBlockHasPtr(P.body, P.bodySize, mainPawnRecord);

    // Чья это пешка: тело несёт указатель на СВОЮ запись персонажа.
    P.pawnRecordIdx = -1;
    for (int r = 0; r <= 2; ++r) {
        const uintptr_t rec = PartyRecordAddr(r);
        if (rec && PartyBlockHasPtr(P.body, P.bodySize, rec)) { P.pawnRecordIdx = r; break; }
    }

    // ОБРАТНОЕ СОПОСТАВЛЕНИЕ ПО УКАЗАТЕЛЮ. Замер 75.10: ни одно тело не
    // содержало указателя на свою запись. Пробуем обратную связь —
    // указатель на ТЕЛО внутри записи.
    if (P.pawnRecordIdx < 0) {
        static BYTE rec[0x1660];
        for (int r = 0; r <= 2; ++r) {
            const uintptr_t ra = PartyRecordAddr(r);
            if (!ra || !Rd((void*)ra, rec, sizeof(rec))) continue;
            if (!PartyBlockHasPtr(rec, sizeof(rec), P.ptr)) continue;
            P.pawnRecordIdx = r;
            break;
        }
    }

    // ИСКАЛИ НЕ ТАМ: ссылка может лежать в ПОДОБЪЕКТЕ, а не в теле.
    //
    // `PartyBlockHasPtr(P.body, ...)` просматривает только сам блок тела
    // (0x58E0 байт). Но данные пешки живут в `cCmcInfo` — отдельном
    // объекте на 5728 байт, который телу лишь принадлежит. Указатель на
    // запись персонажа естественнее искать именно там.
    if (P.pawnRecordIdx < 0) {
        const uintptr_t info = FindChildByClass(P.ptr, P.bodySize, "cCmcInfo", 0);
        if (info) {
            static BYTE blob[5728];
            if (Rd((void*)info, blob, sizeof(blob))) {
                for (int r = 0; r <= 2; ++r) {
                    const uintptr_t ra = PartyRecordAddr(r);
                    if (ra && PartyBlockHasPtr(blob, sizeof(blob), ra)) {
                        P.pawnRecordIdx = r;
                        break;
                    }
                }
            }
        }
    }

    // СОПОСТАВЛЕНИЕ ПО ЗДОРОВЬЮ. Замер 75.11: указателя нет НИ В ОДНУ
    // сторону — тело и запись связаны как-то иначе (индексом, таблицей,
    // чем угодно). Но связь можно установить по данным: у записи есть
    // максимальное HP (+0x970), и это же число живёт в теле, в зеркале
    // `cCmcInfo`.
    //
    // Метод честен ровно настолько, насколько уникально число. Поэтому
    // совпадение принимается ТОЛЬКО если запись такая одна: две пешки с
    // одинаковым максимумом HP оставят обеих неопознанными, и это лучше,
    // чем уверенно назвать не того.
    if (P.pawnRecordIdx < 0 && P.bodyOk) {
        int candidate = -1, hits = 0;
        for (int r = 0; r <= 2; ++r) {
            const uintptr_t ra = PartyRecordAddr(r);
            if (!ra) continue;
            float maxHp = 0.0f;
            if (!Rd((void*)(ra + 0x970), &maxHp, 4)) continue;
            if (!(maxHp > 0.0f)) continue;

            // Уникальна ли эта величина среди записей партии.
            int sameValue = 0;
            for (int q = 0; q <= 2; ++q) {
                const uintptr_t qa = PartyRecordAddr(q);
                if (!qa) continue;
                float v = 0.0f;
                if (Rd((void*)(qa + 0x970), &v, 4) && v == maxHp) ++sameValue;
            }
            if (sameValue != 1) continue;

            // Одного числа мало: максимум HP может совпасть случайно.
            // Требуем ПАРУ — максимум HP и максимум выносливости (+0x97C)
            // рядом друг с другом, в пределах 0x40 байт. Такая пара
            // случайно не встречается.
            float maxSt = 0.0f;
            if (!Rd((void*)(ra + 0x97C), &maxSt, 4) || !(maxSt > 0.0f)) continue;

            for (uint32_t off = 0; off + 4 <= P.bodySize; off += 4) {
                float v = 0.0f;
                memcpy(&v, P.body + off, 4);
                if (v != maxHp) continue;

                bool pair = false;
                const uint32_t lo = (off > 0x40) ? off - 0x40 : 0;
                const uint32_t hi = (off + 0x40 + 4 <= P.bodySize) ? off + 0x40 : P.bodySize - 4;
                for (uint32_t q = lo; q + 4 <= hi && !pair; q += 4) {
                    float w = 0.0f;
                    memcpy(&w, P.body + q, 4);
                    if (w == maxSt) pair = true;
                }
                if (!pair) continue;
                candidate = r;
                ++hits;
                break;
            }
        }
        if (hits == 1) P.pawnRecordIdx = candidate;
    }
    PartyScanKnownValues(P, P.body, P.bodySize, P.dti, 0);

    int bestActScore = -1;
    for (uint32_t off = 0x100; off + 4 <= P.bodySize; off += 4) {
        uintptr_t child = *(uint32_t*)(P.body + off);
        if (!LooksHeap(child)) continue;
        uintptr_t childVt = 0;
        if (!RdPtr((void*)child, &childVt) || !LooksLikeVtable(childVt)) continue;

        char name[48] = {};
        if (!NameOfLiveObject(child, name, sizeof(name)) || !PartyRelevantName(name)) continue;
        if (P.nChild >= kPartyMaxChildren) continue;

        PartyChildDump& C = P.child[P.nChild++];
        memset(&C, 0, sizeof(C));
        C.off = off;
        C.ptr = child;
        C.vt = childVt;
        lstrcpynA(C.name, name, sizeof(C.name));
        C.headOk = Rd((void*)child, C.head, sizeof(C.head));
        C.ownerRef = C.headOk && PartyBlockHasPtr(C.head, sizeof(C.head), P.ptr);

        if (!strcmp(C.name, "uPawnIntel")) P.hasPawnIntel = true;
        if (C.headOk) {
            if (PartyBlockHasPtr(C.head, sizeof(C.head), playerRecord)) P.playerRecordRef = true;
            if (PartyBlockHasPtr(C.head, sizeof(C.head), mainPawnRecord)) P.mainPawnRecordRef = true;
            PartyScanKnownValues(P, C.head, sizeof(C.head), C.name, C.off);
        }

        // Player actions are cPlAct*. Pawn/controller actions are cCmc*.
        // Both normally point back to the owning body. Prefer that evidence
        // over parameter/check-table objects with a similar prefix.
        bool plAct = PartyStartsWith(C.name, "cPlAct");
        bool cmcAct = PartyStartsWith(C.name, "cCmc");
        if ((plAct || cmcAct)
            && !strstr(C.name, "Param") && !strstr(C.name, "CheckTbl")) {
            int score = (plAct ? 20 : 10) + (C.ownerRef ? 100 : 0);
            if (score > bestActScore) {
                bestActScore = score;
                P.actOff = C.off;
                P.actPtr = C.ptr;
                P.actOwnerRef = C.ownerRef;
                lstrcpynA(P.actName, C.name, sizeof(P.actName));
            }
        }
    }
}

void PartyMarkPawnManagerRefs()
{
    for (int i = 0; i < g_nParty; ++i) g_party[i].pawnManagerRef = false;
    for (int m = 0; m < g_nPartyPawnMgr; ++m) {
        static BYTE mgr[kPawnManagerSize];
        if (!Rd((void*)g_partyPawnMgr[m], mgr, sizeof(mgr))) continue;
        for (int i = 0; i < g_nParty; ++i)
            if (PartyBlockHasPtr(mgr, sizeof(mgr), g_party[i].ptr))
                g_party[i].pawnManagerRef = true;
    }
}

int PartyCountValueHits(const PartyBodyDump& P, const char* prefix)
{
    int n = 0;
    size_t len = prefix ? strlen(prefix) : 0;
    if (!len) return 0;
    for (int i = 0; i < P.nValueHit; ++i)
        if (!strncmp(P.valueHit[i].label, prefix, len)) ++n;
    return n;
}

void PartySelectWorkingPair()
{
    if (g_nParty <= 2) return;

    int arisen = -1, pawn = -1;
    int bestArisen = 0, bestPawn = 0;
    for (int i = 0; i < g_nParty; ++i) {
        PartyBodyDump& P = g_party[i];
        bool pawnEvidence = P.mainPawnRecordRef || P.pawnManagerRef || P.hasPawnIntel;

        int a = !strcmp(P.dti, "uPlayer") ? 500 : 0;
        if (P.playerRecordRef) a += 2000;
        a += PartyCountValueHits(P, "player_") * 20;
        if (pawnEvidence) a -= 1000;
        if (a > bestArisen) { bestArisen = a; arisen = i; }

        int p = !strcmp(P.dti, "uCmc") ? 1 : 0;
        if (P.mainPawnRecordRef) p += 2000;
        if (P.pawnManagerRef) p += 2000;
        if (P.hasPawnIntel) p += 2000;
        p += PartyCountValueHits(P, "pawn_") * 20;
        if (p > bestPawn) { bestPawn = p; pawn = i; }
    }

    // A body cannot fill both roles. If that happened, keep the stronger role
    // and look for the next-best distinct candidate.
    if (arisen >= 0 && pawn == arisen) {
        pawn = -1; bestPawn = 0;
        for (int i = 0; i < g_nParty; ++i) {
            if (i == arisen) continue;
            PartyBodyDump& P = g_party[i];
            int p = !strcmp(P.dti, "uCmc") ? 1 : 0;
            if (P.mainPawnRecordRef) p += 2000;
            if (P.pawnManagerRef) p += 2000;
            if (P.hasPawnIntel) p += 2000;
            p += PartyCountValueHits(P, "pawn_") * 20;
            if (p > bestPawn) { bestPawn = p; pawn = i; }
        }
    }

    int keep = 0;
    if (arisen >= 0) g_partyChosen[keep++] = g_party[arisen];
    if (pawn >= 0 && pawn != arisen && keep < 2) g_partyChosen[keep++] = g_party[pawn];
    if (!keep) return;
    for (int i = 0; i < keep; ++i) g_party[i] = g_partyChosen[i];
    g_nParty = keep;
}

void PartyAssignRoles()
{
    int pawn = -1, arisen = -1;
    for (int i = 0; i < g_nParty; ++i) {
        bool cmcBody = !strcmp(g_party[i].dti, "uCmc");
        bool playerBody = !strcmp(g_party[i].dti, "uPlayer");
        bool pawnEvidence = cmcBody
                         || g_party[i].mainPawnRecordRef
                         || g_party[i].pawnManagerRef
                         || g_party[i].hasPawnIntel;
        if (pawnEvidence && pawn < 0) pawn = i;
        if ((playerBody || g_party[i].playerRecordRef) && !pawnEvidence && arisen < 0)
            arisen = i;
    }
    if (g_nParty == 2) {
        if (pawn >= 0 && arisen < 0) arisen = 1 - pawn;
        if (arisen >= 0 && pawn < 0) pawn = 1 - arisen;
    }
    for (int i = 0; i < g_nParty; ++i) {
        if (i == arisen) lstrcpynA(g_party[i].role, "Arisen", sizeof(g_party[i].role));
        else if (i == pawn) lstrcpynA(g_party[i].role, "Main Pawn", sizeof(g_party[i].role));
        else sprintf_s(g_party[i].role, sizeof(g_party[i].role), "Candidate %c", 'A' + i);
    }
}

bool PartyCandidatesStillValid()
{
    if (g_nParty <= 0) return false;
    for (int i = 0; i < g_nParty; ++i) {
        uintptr_t vt = 0;
        if (!RdPtr((void*)g_party[i].ptr, &vt) || vt != g_party[i].vt) return false;
        char name[40] = {};
        if (!NameOfLiveObject(g_party[i].ptr, name, sizeof(name))
            || strcmp(name, g_party[i].dti))
            return false;
    }
    return true;
}

// Читает позиции из уже разрешённых тел (дешёво, без census).
// Вызывается каждый тик и после каждого PartyAssignRoles.
void PartyReadPositions()
{
    g_arisenPosOk = false;
    g_pawnPosOk = false;
    if (g_nParty <= 0) return;
    int arisen = -1, pawn = -1;
    for (int i = 0; i < g_nParty; ++i) {
        if (!strcmp(g_party[i].role, "Arisen")) arisen = i;
        else if (!strcmp(g_party[i].role, "Main Pawn")) pawn = i;
    }
    float x = 0, y = 0, z = 0;
    // (0,0,0) считается sentinel «нет позиции»: мировые координаты DDDA —
    // тысячи, никогда не нулевые в реальной точке.
    if (arisen >= 0
        && Rd((void*)(g_party[arisen].ptr + 0x40), &x, 4)
        && Rd((void*)(g_party[arisen].ptr + 0x44), &y, 4)
        && Rd((void*)(g_party[arisen].ptr + 0x48), &z, 4)
        && !(x == 0.0f && y == 0.0f && z == 0.0f)) {
        g_arisenPosX = x; g_arisenPosY = y; g_arisenPosZ = z;
        g_arisenPosOk = true;
    } else {
        g_arisenPosX = g_arisenPosY = g_arisenPosZ = 0;
    }
    if (pawn >= 0
        && Rd((void*)(g_party[pawn].ptr + 0x40), &x, 4)
        && Rd((void*)(g_party[pawn].ptr + 0x44), &y, 4)
        && Rd((void*)(g_party[pawn].ptr + 0x48), &z, 4)
        && !(x == 0.0f && y == 0.0f && z == 0.0f)) {
        g_pawnPosX = x; g_pawnPosY = y; g_pawnPosZ = z;
        g_pawnPosOk = true;
        g_pawnPosWasOk = true;
        // Build 62: боевая цель пешки (uCmc+0x2EB8, SOURCE_OF_TRUTH §4).
        uintptr_t tgt = 0;
        if (RdPtr((void*)(g_party[pawn].ptr + 0x2EB8), &tgt))
            g_pawnCombatTarget = tgt;
    } else {
        g_pawnPosX = g_pawnPosY = g_pawnPosZ = 0;
        // Диагностика: только при ПЕРЕХОДЕ в сбой (было ок → стало ок-нет),
        // плюс редко (раз в 60с), чтобы не спамить лог каждые 3 секунды.
        DWORD now = MsNow();
        if ((g_pawnPosWasOk || now - g_pawnPosLastFailLog >= 60000u)) {
            g_pawnPosLastFailLog = now;
            logFile << "PartyPositions: pawn read FAILED nParty=" << g_nParty
                    << " pawnIdx=" << pawn << std::endl;
            for (int i = 0; i < g_nParty; ++i) {
                logFile << "  [" << i << "] role='" << g_party[i].role
                        << "' dti='" << g_party[i].dti << "' body=0x" << std::hex
                        << g_party[i].ptr << std::dec << std::endl;
            }
        }
        g_pawnPosWasOk = false;
    }
}

// ЗАПРОС НА ПЕРЕСКАН СОСТАВА.
//
// ПОЧЕМУ ОН ПОНАДОБИЛСЯ. Разбор партии останавливался, как только найдены
// Восставший и главная пешка: `complete -> return`, и больше ни одного
// скана до смены мира. Логика была верной ровно до тех пор, пока партия
// не могла меняться в течение сессии. Наёмные пешки приходят из Рифта
// посреди игры — и не появлялись в разборе НИКОГДА: кнопка переклички
// честно показала «одна пешка» при полной партии из трёх.
//
// Полный скан памяти на каждый тик недопустим, поэтому пересканируем по
// запросу: его выставляет тот, кто увидел в мире тело пешки, которого нет
// в нашем списке (см. WorldScan). Троттл — не чаще раза в пять секунд.
static volatile LONG g_partyRescanWanted = 0;
static DWORD         g_partyRescanLast   = 0;

// ЗАМЕР ЗАДЕРЖКИ, А НЕ РАССУЖДЕНИЕ О НЕЙ.
//
// Совет со стороны: «повесь хук на функцию найма, тогда задержка станет
// нулевой». Прежде чем менять надёжное на элегантное, надо знать, ЧТО
// именно мы меняем. Поэтому засекаем момент, когда в мире впервые
// замечено незнакомое тело пешки, и момент, когда список реально
// обновился. Число решит, есть ли тут задача.
static DWORD g_partyChangeSeenMs = 0;
static int   g_partyLastPawnCount = -1;

void PartyRequestRescan()
{
    if (!InterlockedCompareExchange(&g_partyRescanWanted, 0, 0))
        g_partyChangeSeenMs = MsNow();
    InterlockedExchange(&g_partyRescanWanted, 1);
}

// Пешек в текущем списке. Дубль публичной PawnBodyCount(), но внутренний:
// публичный API не должен вызываться из середины разбора.
static int PartyPawnCountLocal()
{
    int n = 0;
    for (int i = 0; i < g_nParty; ++i)
        if (!strcmp(g_party[i].dti, "uCmc")) ++n;
    return n;
}

// Вызывается после каждого пересбора списка.
static void PartyReportCompositionChange()
{
    const int n = PartyPawnCountLocal();
    if (n == g_partyLastPawnCount) return;
    const int was = g_partyLastPawnCount;
    g_partyLastPawnCount = n;
    if (was < 0) return;                     // первый разбор — не событие
    char l[200];
    const DWORD lat = g_partyChangeSeenMs ? (MsNow() - g_partyChangeSeenMs) : 0;
    sprintf_s(l, "PartyRecon: pawn count %d -> %d, list updated %u ms after the"
                 " new body was first seen", was, n, (unsigned)lat);
    logFile << l << std::endl;
    g_partyChangeSeenMs = 0;
}



static bool PartyRescanDue()
{
    if (!InterlockedCompareExchange(&g_partyRescanWanted, 0, 0)) return false;
    const DWORD now = MsNow();
    if (g_partyRescanLast && now - g_partyRescanLast < 5000) return false;
    g_partyRescanLast = now;
    InterlockedExchange(&g_partyRescanWanted, 0);
    return true;
}

// Есть ли это тело уже в списке партии. Нужно и здесь, и в WorldScan.
// Принять тело в список партии, минуя скан памяти.
//
// Источник — обход живых объектов: он идёт по связному списку и видит
// соседей, среди которых и лежат тела партии. Скан памяти для этой задачи
// оказался ненадёжным (лог 75.10: записи говорят «три пешки», скан находит
// одно тело), поэтому право пополнять список получил тот, кто видит их
// каждый тик.
void PartyAdoptBody(uintptr_t body, const char* dti)
{
    if (!body || !dti || g_nParty >= kPartyMaxBodies) return;
    for (int i = 0; i < g_nParty; ++i)
        if (g_party[i].ptr == body) return;

    uintptr_t vt = 0;
    if (!RdPtr((void*)body, &vt) || !LooksLikeVtable(vt)) return;
    char nm[40] = {};
    if (!NameOfLiveObject(body, nm, sizeof(nm)) || strcmp(nm, dti) != 0) return;

    PartyBodyDump& P = g_party[g_nParty++];
    memset(&P, 0, sizeof(P));
    P.ptr = body;
    P.vt = vt;
    P.bodySize = !strcmp(dti, "uPlayer") ? kPartyBodySize : kCmcBodySize;
    lstrcpynA(P.dti, dti, sizeof(P.dti));
    P.pawnRecordIdx = -1;
    PartyInspectBody(P);
    PartyAssignRoles();

    char l[180];
    sprintf_s(l, "PartyRecon: adopted %s body 0x%08X from the live list"
                 " (record #%d)", dti, (unsigned)body, P.pawnRecordIdx);
    logFile << l << std::endl;
}

bool PartyHasBody(uintptr_t body)
{
    for (int i = 0; i < g_nParty; ++i)
        if (g_party[i].ptr == body) return true;
    return false;
}

void PartyPositionsTick()
{
    int arisen = -1, pawn = -1;
    for (int i = 0; i < g_nParty; ++i) {
        if (!strcmp(g_party[i].role, "Arisen")) arisen = i;
        else if (!strcmp(g_party[i].role, "Main Pawn")) pawn = i;
    }
    bool complete = (arisen >= 0 && pawn >= 0);

    if (g_nParty > 0) {
        if (!PartyCandidatesStillValid()) {
            // Тело стало невалидным (пересоздано в бою/воскрешении) без смены
            // мира. Немедленный пере-резолв, без троттла.
            g_partyPosLastDiscover = 0;
            if (InterlockedCompareExchange(&g_partyBusy, 1, 0) == 0) {
                PartyFindBodies(true);
                for (int i = 0; i < g_nParty; ++i) PartyInspectBody(g_party[i]);
                PartyMarkPawnManagerRefs();
                PartySelectWorkingPair();
                PartyAssignRoles();
                PartyReportCompositionChange();
                InterlockedExchange(&g_partyBusy, 0);
            }
            if (g_nParty > 0) PartyReadPositions();
            return;
        }
        PartyReadPositions(); // тела валидны — читаем (даже если неполный набор)

        // СОСТАВ СВЕРЯЕМ С ЗАПИСЯМИ, А НЕ СО СПИСКОМ АКТЁРОВ.
        //
        // Прежняя версия ждала, пока обход актёров заметит незнакомое
        // тело. Обход ограничен 32 записями и в лагере занят гоблинами —
        // перекличка так и показывала одну пешку при полной партии.
        // Записи персонажей лежат по фиксированным адресам, читаются
        // четырьмя чтениями и врать не могут.
        {
            const int wantPawns = PartyRecordPawnCount();
            if (wantPawns > 0) PartySetExpectedPawns(wantPawns);
            int havePawns = 0;
            for (int i = 0; i < g_nParty; ++i)
                if (!strcmp(g_party[i].dti, "uCmc")) ++havePawns;

            // ОТСТУПАЕМ, ЕСЛИ ПЕРЕСКАН НЕ ПОМОГАЕТ.
            //
            // В логе 75.9 строка «party composition changed - rescanning»
            // повторилась несколько десятков раз подряд: записи говорят
            // «три пешки», скан находит меньше, и запрос выставлялся
            // заново каждые пять секунд. Это не поиск, это цикл.
            //
            // Правило: если после пересканирования число тел не выросло,
            // ещё две попытки — и замолкаем до реальной смены состава
            // (изменения числа записей).
            static int  s_failedRescans = 0;
            static int  s_lastWant = -1;
            if (wantPawns != s_lastWant) { s_lastWant = wantPawns; s_failedRescans = 0; }
            if (havePawns < wantPawns) {
                if (s_failedRescans < 3) {
                    ++s_failedRescans;
                    PartyRequestRescan();
                } else if (s_failedRescans == 3) {
                    ++s_failedRescans;
                    char l[200];
                    sprintf_s(l, "PartyRecon: records show %d pawns, the body scan"
                                 " finds %d - giving up until the party changes"
                                 " (probes will read the bodies we do have)",
                              wantPawns, havePawns);
                    logFile << l << std::endl;
                }
            } else {
                s_failedRescans = 0;
            }
        }

        // Состав мог измениться: наняли или уволили пешку. Тела старых при
        // этом остаются валидными, поэтому проверка живости молчит — нужен
        // явный запрос.
        if (complete && !PartyRescanDue()) return;
        if (complete) {
            logFile << "PartyRecon: party composition changed - rescanning"
                    << std::endl;
        }
    }

    // Нужен (до)резолв: бэкофф по числу попыток.
    DWORD now = MsNow();
    DWORD wait = (g_partyPosAttempts == 0) ? 0u
               : (g_partyPosAttempts == 1) ? 2000u
               : (g_partyPosAttempts == 2) ? 8000u : 20000u;
    if (g_partyPosLastDiscover && now - g_partyPosLastDiscover < wait) return;
    g_partyPosLastDiscover = now;
    ++g_partyPosAttempts;
    if (InterlockedCompareExchange(&g_partyBusy, 1, 0) != 0) return;
    PartyFindBodies(true);
    for (int i = 0; i < g_nParty; ++i) PartyInspectBody(g_party[i]);
    PartyMarkPawnManagerRefs();
    PartySelectWorkingPair();
    PartyAssignRoles();
    PartyReportCompositionChange();
    InterlockedExchange(&g_partyBusy, 0);
    if (g_nParty > 0) PartyReadPositions();
}

void PartyCapture(bool forceFind)
{
    if (InterlockedCompareExchange(&g_partyBusy, 1, 0) != 0) return;
    if (!InWorld()) {
        lstrcpynA(g_partyStatus, "load a save first", sizeof(g_partyStatus));
        InterlockedExchange(&g_partyBusy, 0);
        return;
    }

    if (forceFind || !PartyCandidatesStillValid()) PartyFindBodies();
    for (int i = 0; i < g_nParty; ++i) PartyInspectBody(g_party[i]);
    PartyMarkPawnManagerRefs();
    PartySelectWorkingPair();
    PartyAssignRoles();
    PartyReadPositions();

    ++g_partySeq;
    // Build 69: продукт не зовёт research напрямую. DevTools подписывается
    // на этот хук и сам решает, писать ли дампы.
    if (g_research.onSnapshotEarly) g_research.onSnapshotEarly();

    if (g_nParty <= 0) {
        sprintf_s(g_partyStatus, sizeof(g_partyStatus),
            "no uPlayer/uCmc yet; discovery %03d saved (%d/%d named vtables)",
            g_partySeq, g_partyVtNamed, g_partyVtChecked);
        logFile << "PartyRecon: dynamic DTI scan found no uPlayer/uCmc body"
                << " vtChecked=" << g_partyVtChecked
                << " vtNamed=" << g_partyVtNamed
                << " nearTypes=" << g_nPartyNear
                << " file=" << g_partyLastFile << std::endl;
        for (int i = 0; i < g_nPartyNear; ++i)
            logFile << "  near " << g_partyNear[i].name << " vt=0x" << std::hex
                    << g_partyNear[i].vt << " sample=0x" << g_partyNear[i].sample
                    << std::dec << std::endl;
        InterlockedExchange(&g_partyBusy, 0);
        return;
    }

    if (g_research.onSnapshotFull) g_research.onSnapshotFull();

    logFile << "PartyRecon: snapshot " << g_partySeq << " bodies=" << g_nParty
            << " rawCandidates=" << g_partyRawCandidates
            << " vtChecked=" << g_partyVtChecked << " vtNamed=" << g_partyVtNamed
            << " file=" << g_partyLastFile << std::endl;
    for (int i = 0; i < g_nParty; ++i) {
        logFile << "  " << g_party[i].role << " body=0x" << std::hex << g_party[i].ptr
                << " act@+0x" << g_party[i].actOff << " -> " << g_party[i].actName
                << std::dec << " children=" << g_party[i].nChild
                << " knownValueHits=" << g_party[i].nValueHit
                << " pawnIntel=" << (g_party[i].hasPawnIntel ? 1 : 0)
                << " pawnMgr=" << (g_party[i].pawnManagerRef ? 1 : 0) << std::endl;
    }
    // Build 40 snapshots the upper AI graph on demand. The old dense CSV
    // remains available via '-' but is no longer started automatically.
    InterlockedExchange(&g_partyBusy, 0);
}

void PartyHotkeyTick()
{
    // '-' switches persistent sidecar profiles transactionally.
    // The legacy dense trace remains file-only but has no hotkey in this build.
    PartyPriorityProfileHotkeyTick();
    // Build 69: исследовательские тики — через хук, а не прямым вызовом.
    if (g_research.onTick) g_research.onTick();

    static bool wasDown = false;
    // Physical '=' key beside Backspace (VK_OEM_PLUS without Shift).
    bool down = (GetAsyncKeyState(VK_OEM_PLUS) & 0x8000) != 0;
    if (down && !wasDown) PartyCapture(false);
    wasDown = down;
}

// Build 56.2 — Guardian doctrine anchor/pawn world positions.
// Читает +0x40/+0x44/+0x48 из уже разрешённых тел (PartyReadPositions).
bool GetArisenWorldPos(float* x, float* y, float* z)
{
    if (!g_arisenPosOk) return false;
    if (x) *x = g_arisenPosX;
    if (y) *y = g_arisenPosY;
    if (z) *z = g_arisenPosZ;
    return true;
}

// СНАЧАЛА ПО DTI, ПОТОМ ПО РОЛИ.
//
// Роль присваивается позже самого разбора, и в логе тестера видно окно,
// где тела уже найдены, а роли ещё пустые:
//
//     [0] role='' dti='uPlayer' body=0x10d60060
//     [1] role='' dti='uCmc'    body=0x10d65ac0
//
// Поиск по роли в этот момент возвращал ноль, панель писала «pawn
// unresolved», а следующее нажатие приходилось уже на другое состояние.
// Имя класса от игры доступно сразу и не зависит от нашего этапа разбора.
uintptr_t ArisenBody()
{
    for (int i = 0; i < g_nParty; ++i)
        if (!strcmp(g_party[i].dti, "uPlayer")) return g_party[i].ptr;
    for (int i = 0; i < g_nParty; ++i)
        if (!strcmp(g_party[i].role, "Arisen")) return g_party[i].ptr;
    return 0;
}

// ГЛАВНАЯ ПЕШКА ОПОЗНАЁТСЯ ПО ЗАПИСИ ПЕРСОНАЖА, А НЕ ПО ПОРЯДКУ В ПАМЯТИ.
//
// Раньше здесь стояло «первое тело класса uCmc». Пока пешка одна, это
// верно всегда. Наёмные пешки — тоже `uCmc`, и порядок обхода памяти
// решал бы, кого мы считаем своей: ускорение, доктрина и все замеры
// молча уехали бы на чужую пешку.
//
// Надёжный признак у нас уже собирается: тело главной пешки ссылается на
// её запись персонажа (`Arisen record + 0x7F0`, SOURCE_OF_TRUTH §1) —
// поле `mainPawnRecordRef`. Порядок предпочтений:
//   1. тело с признаком записи главной пешки;
//   2. тело с уже присвоенной ролью «Main Pawn»;
//   3. единственный `uCmc` в партии (наёмных нет — двусмысленности нет);
//   4. первый `uCmc` + ПРЕДУПРЕЖДЕНИЕ в лог: дальше числа могут врать.
uintptr_t MainPawnBody()
{
    for (int i = 0; i < g_nParty; ++i)
        if (!strcmp(g_party[i].dti, "uCmc") && g_party[i].mainPawnRecordRef)
            return g_party[i].ptr;

    for (int i = 0; i < g_nParty; ++i)
        if (!strcmp(g_party[i].role, "Main Pawn")) return g_party[i].ptr;

    int nCmc = 0, first = -1;
    for (int i = 0; i < g_nParty; ++i)
        if (!strcmp(g_party[i].dti, "uCmc")) { ++nCmc; if (first < 0) first = i; }
    if (first < 0) return 0;
    if (nCmc == 1) return g_party[first].ptr;

    static int s_warned = 0;
    if (s_warned < 3) {
        ++s_warned;
        char l[200];
        sprintf_s(l, "PartyRecon: %d uCmc bodies and none carries the main-pawn"
                     " record - falling back to the first one (0x%08X). Numbers"
                     " taken from it may belong to a hired pawn.",
                  nCmc, (unsigned)g_party[first].ptr);
        logFile << l << std::endl;
    }
    return g_party[first].ptr;
}

// --- наёмные пешки: перечисление (read-only) --------------------------------
//
// Продукту они пока не нужны, но замер по ним нужен уже сейчас: пока мы не
// знаем, общий ли у пешек ресурс приоритетов, нельзя ответить на вопрос
// «наши правки весов действуют на чужих пешек или нет».
int PawnBodyCount()
{
    int n = 0;
    for (int i = 0; i < g_nParty; ++i)
        if (!strcmp(g_party[i].dti, "uCmc")) ++n;
    return n;
}

uintptr_t PawnBodyAt(int idx, bool* isMainOut)
{
    if (isMainOut) *isMainOut = false;
    // Порядок стабильный: сначала своя пешка (запись 0), затем наёмные по
    // номеру записи. Порядок обхода памяти для этого не годится — он
    // меняется от запуска к запуску.
    int seen = 0;
    for (int rec = 0; rec <= 2; ++rec) {
        for (int i = 0; i < g_nParty; ++i) {
            if (strcmp(g_party[i].dti, "uCmc")) continue;
            if (g_party[i].pawnRecordIdx != rec) continue;
            if (seen++ != idx) continue;
            if (isMainOut) *isMainOut = (rec == 0);
            return g_party[i].ptr;
        }
    }
    // Тела без опознанной записи — в конец, чтобы не потерялись совсем.
    for (int i = 0; i < g_nParty; ++i) {
        if (strcmp(g_party[i].dti, "uCmc")) continue;
        if (g_party[i].pawnRecordIdx >= 0) continue;
        if (seen++ != idx) continue;
        if (isMainOut) *isMainOut = g_party[i].mainPawnRecordRef
                                 || !strcmp(g_party[i].role, "Main Pawn");
        return g_party[i].ptr;
    }
    return 0;
}

bool GetMainPawnWorldPos(float* x, float* y, float* z)
{
    if (!g_pawnPosOk) return false;
    if (x) *x = g_pawnPosX;
    if (y) *y = g_pawnPosY;
    if (z) *z = g_pawnPosZ;
    return true;
}

} // namespace Runtime
