// Direct regression for PartyRecon's fixed-record-backed Arisen identity.
#include "director_stdafx.h"
// Локально держим только то, чего нет в shim/windows.h: PAGE_*/MEM_PRIVATE
// шим уже объявляет, а дубли роняли сборку под -Werror (redefinition).
#define __except(x) catch(...)
#define EXCEPTION_EXECUTE_HANDLER 1
#define VK_OEM_PLUS 0xBB
// Interlocked* и GetAsyncKeyState уже объявлены в shim/windows.h; локальные
// копии давали redefinition под -Werror и фикстура не собиралась вовсе.

#include "../../src/runtime/PartyRecon.cpp"
#include <assert.h>
#include <iostream>

namespace Runtime {
PartyBodyDump g_party[kPartyMaxBodies];
PartyBodyDump g_partyChosen[kPartyExactSlots];
int g_nParty = 0;
}

static void ResetCandidates()
{
    memset(Runtime::g_party, 0, sizeof(Runtime::g_party));
    memset(Runtime::g_partyChosen, 0, sizeof(Runtime::g_partyChosen));
    Runtime::g_nParty = 0;
}

static void AddPlayer(uintptr_t body, bool recordBacked)
{
    Runtime::PartyBodyDump& p = Runtime::g_party[Runtime::g_nParty++];
    memset(&p, 0, sizeof(p));
    p.ptr = body;
    p.bodyOk = true;
    p.playerRecordRef = recordBacked;
    p.pawnRecordIdx = -1;
    strcpy(p.dti, "uPlayer");
}

static void AddPawn(uintptr_t body, int record)
{
    Runtime::PartyBodyDump& p = Runtime::g_party[Runtime::g_nParty++];
    memset(&p, 0, sizeof(p));
    p.ptr = body;
    p.bodyOk = true;
    p.pawnRecordIdx = record;
    strcpy(p.dti, "uCmc");
}

int main()
{
    using namespace Runtime;

    // A transient extra class-valid uPlayer must not compete with the one body
    // carrying the exact fixed player-record pointer. Discovery order is inert.
    ResetCandidates();
    AddPlayer(0x1000u, false);
    AddPlayer(0x2000u, true);
    PartyAssignRoles();
    assert(ArisenBody() == 0x2000u);
    assert(strncmp(g_party[0].role, "Unresolved", 10) == 0);
    assert(strcmp(g_party[1].role, "Arisen") == 0);

    // Selection can retain all four exact fixed slots. This is also a bound
    // regression for the historical two-element chosen scratch array.
    AddPawn(0x3000u, 0);
    AddPawn(0x4000u, 1);
    AddPawn(0x5000u, 2);
    PartySelectWorkingPair();
    PartyAssignRoles();
    assert(g_nParty == kPartyExactSlots);
    assert(ArisenBody() == 0x2000u);
    assert(strcmp(g_party[0].role, "Arisen") == 0);
    assert(strcmp(g_party[1].role, "Main Pawn") == 0);
    assert(strcmp(g_party[2].role, "Hired Pawn 1") == 0);
    assert(strcmp(g_party[3].role, "Hired Pawn 2") == 0);

    // Zero record-backed claims fail closed even when DTI is unique.
    ResetCandidates();
    AddPlayer(0x6000u, false);
    PartyAssignRoles();
    assert(ArisenBody() == 0);
    assert(strncmp(g_party[0].role, "Unresolved", 10) == 0);

    // Multiple exact record-backed claims are anomalous and also fail closed;
    // there is deliberately no first/last/heap-order tie breaker.
    ResetCandidates();
    AddPlayer(0x7000u, true);
    AddPlayer(0x8000u, true);
    AddPawn(0x9000u, 0);
    AddPawn(0xA000u, 1);
    AddPawn(0xB000u, 2);
    PartySelectWorkingPair();
    // Selection must preserve the complete anomaly instead of pruning both
    // claimants and allowing one to be re-adopted first later.
    assert(g_nParty == 5);
    PartyAssignRoles();
    assert(ArisenBody() == 0);
    assert(strncmp(g_party[0].role, "Unresolved", 10) == 0);
    assert(strncmp(g_party[1].role, "Unresolved", 10) == 0);

    std::cout << "PartyRecon Build012 Arisen identity: PASS "
                 "(unique fixed-record claim; duplicate/zero fail closed)\n";
    return 0;
}
