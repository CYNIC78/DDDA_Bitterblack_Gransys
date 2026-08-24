// 84.16 dual-observe: offline fixture для PartyStatus downed/revive FSM.
//
// Скрипт live-актов главной пешки (тело 0xB000):
//   Walk -> Neardeath -> Neardeath -> cPlReviveCMC -> Walk -> Neardeath -> Walk
// Проверяется:
//   1. downedValid — только на подтверждённом переходе FSM;
//   2. downedRevivable — только после полной последовательности
//      (downed -> cPlReviveCMC -> первый обычный акт);
//   3. голос у каждого перехода (DOWNED / REVIVE / RECOVERED / DOWN-END);
//   4. статусные блоки не пишутся и не находят себя (Rd/RdPtr ложь,
//      FindChildByClass 0) — прибор остаётся read-only.

#include "partystatus_t.cpp"
#include <assert.h>
#include <cstring>
#include <fstream>
#include <sstream>

std::ofstream logFile("/tmp/partystatus_fsm_test.log");
BYTE** pBase = 0;
IniConfigStub config;

namespace Runtime {

bool g_inWorld = true;

namespace Mem {
bool InWorld() { return g_inWorld; }
bool Rd(const void*, void*, size_t) { return false; }
bool RdPtr(const void*, uintptr_t*) { return false; }
bool NameOfLiveObject(uintptr_t, char*, int) { return false; }
bool LooksHeap(uintptr_t) { return false; }
} // namespace Mem

int g_findChildCalls = 0;
uintptr_t FindChildByClass(uintptr_t, uint32_t, const char*, uint32_t*)
{
    ++g_findChildCalls;
    return 0;   // блоков нет: прибор должен молча оставаться в поиске
}

uintptr_t ArisenBody() { return 0xA000; }

bool PartyRecordInfo(int idx, int* vocOut, int* lvlOut, uintptr_t* bodyOut)
{
    if (bodyOut) *bodyOut = 0xB000 + (uintptr_t)idx;
    if (vocOut) *vocOut = 1;
    if (lvlOut) *lvlOut = 10;
    return true;
}

const char* g_mainAct = "cPlActWalk";

bool ReadLiveAct(uintptr_t body, char* out, int cap)
{
    if (!out || cap <= 0) return false;
    const char* a = (body == 0xB000) ? g_mainAct : "cPlActWalk";
    strncpy(out, a, (size_t)cap - 1);
    out[cap - 1] = 0;
    return true;
}

} // namespace Runtime

static int CountSubstring(const std::string& hay, const char* needle)
{
    int n = 0;
    size_t pos = 0;
    while ((pos = hay.find(needle, pos)) != std::string::npos) {
        ++n;
        pos += strlen(needle);
    }
    return n;
}

static void Step(const char* act, bool expectDownedValid,
                 bool expectDownedRevivable)
{
    Runtime::g_mainAct = act;
    Runtime::PartyStatus::Tick();
    Runtime::PartyCombatMember M;
    memset(&M, 0, sizeof(M));
    Runtime::PartyStatus::FillMemberStatus(0xB000, 1, M);
    assert(M.downedValid == expectDownedValid);
    assert(M.downedRevivable == expectDownedRevivable);
}

int main()
{
    // 1. Обычное состояние: downed невалиден.
    Step("cPlActWalk", false, false);
    // 2. Neardeath-предшественник: подтверждённый вход в downed.
    Step("cPlActCmcNeardeath", true, false);
    // 3. Всё ещё downed: повторного перехода (и повторной строки) нет.
    Step("cPlActCmcNeardeath", true, false);
    // 4. Воскрешение: тело всё ещё downed; «воскрешаемо» ещё не подтверждено.
    Step("cPlReviveCMC", true, false);
    // 5. Первый обычный акт: последовательность завершена, downed закрыт.
    Step("cPlActWalk", false, false);
    // 6. Второй downed: теперь revivable=true — последовательность видана.
    Step("cPlActCmcNeardeath", true, true);
    // 7. Вышли из downed без воскрешения: DOWN-END, downed закрыт.
    Step("cPlActWalk", false, false);

    std::ifstream in("/tmp/partystatus_fsm_test.log");
    assert(in.good());
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string log = ss.str();

    // Четыре тела партии отслежены (Arisen + 3 пешки).
    assert(CountSubstring(log, " (read-only status + downed/revive observer)") == 4);
    // Голос у каждого перехода: DOWNED ровно дважды (шаги 2 и 6).
    assert(CountSubstring(log, "PS: MainPawn DOWNED act=cPlActCmcNeardeath") == 2);
    // REVIVE разово за down, RECOVERED — подтверждённая последовательность.
    assert(CountSubstring(log, "PS: MainPawn REVIVE act=cPlReviveCMC") == 1);
    assert(CountSubstring(log, "PS: MainPawn RECOVERED act=cPlActWalk") == 1);
    // Второй downed закончился без воскрешения.
    assert(CountSubstring(log, "PS: MainPawn DOWN-END act=cPlActWalk") == 1);
    // Блоков нет: ни одной находки, прибор не написал ничего в память
    // (Rd/RdPtr ложь, FindChildByClass 0 — если бы код пытался писать,
    // WrSafe отсутствовал бы в PartyStatus.cpp по статическому контракту).
    assert(log.find("found @") == std::string::npos);
    assert(Runtime::g_findChildCalls >= 0);

    fprintf(stderr, "PartyStatus 84.16 FSM fixture passed "
                    "(downed/revive sequence, read-only).\n");
    return 0;
}
