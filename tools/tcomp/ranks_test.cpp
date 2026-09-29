// 85.42: поведение РАНГОВ (ступени вместо равномерного коридора), ими же
//
// Что проверяем и почему именно это:
//   1) выключенный вид = сегодняшнее поведение (нет ключа -> нет ступеней);
//   2) допущенный вид без секции [ranks] получает встроенные числа пилота;
//   3) мусор в [ladder] починяется: перевёрнутая полоса меняется местами,
//      атака ниже ванили поднимается до 1.0, нулевые веса заменяются встроенными;
//   4) ступень детерминирована по адресу (в бою не мигает), но полоса размера
//      и вес ступени соблюдаются на большой выборке адресов;
//   5) непричастный вид ступеней не получает.
#ifndef DDDA_TEMPO_PORTABLE_FIXTURE
#define DDDA_TEMPO_PORTABLE_FIXTURE
#endif
#include "tempo_stdafx.h"
#include "../../src/runtime/MonsterTempo.cpp"

#include <assert.h>
#include <cmath>
#include <cstring>
#include <iostream>
#include <string>

BYTE* codeBase = 0;
BYTE* codeEnd = 0;
DWORD g_tempoTestNow = 0;
IniConfigStub config;

namespace Runtime {
ActorDump g_act[32] = {};
int g_nAct = 0;
bool KindIsEnemy(const char* kind) { return kind && kind[0] == 'u'; }
namespace Mem {
bool Rd(const void*, void*, size_t) { return false; }
bool WrSafe(void*, const void*, size_t) { return false; }
bool RegionOk(uintptr_t, size_t) { return true; }
bool NameOfLiveObject(uintptr_t, char* out, int cap)
{
    if (!out || cap <= 0) return false;
    const char* k = "uEm0100";
    int i = 0;
    for (; k[i] && i < cap - 1; ++i) out[i] = k[i];
    out[i] = 0;
    return true;
}
} // namespace Mem
} // namespace Runtime

// ── читатель ini для фикстуры: маленькая таблица ключей ────────────────────
struct FakeIni : Runtime::Tempo::RanksIniReader {
    struct Row { const char* sec; const char* key; float f; bool b; bool isBool; };
    enum { kMax = 24 };
    Row rows[kMax];
    int n = 0;

    void SetFloat(const char* sec, const char* key, float v)
    {
        if (n >= kMax) return;
        rows[n].sec = sec; rows[n].key = key; rows[n].f = v;
        rows[n].b = false; rows[n].isBool = false; ++n;
    }
    void SetBool(const char* sec, const char* key, bool v)
    {
        if (n >= kMax) return;
        rows[n].sec = sec; rows[n].key = key; rows[n].f = 0.0f;
        rows[n].b = v; rows[n].isBool = true; ++n;
    }
    float Float(const char* sec, const char* key, float defValue)
    {
        for (int i = 0; i < n; ++i)
            if (!std::strcmp(rows[i].sec, sec) && !std::strcmp(rows[i].key, key)
                && !rows[i].isBool)
                return rows[i].f;
        return defValue;
    }
    bool Bool(const char* sec, const char* key, bool defValue)
    {
        for (int i = 0; i < n; ++i)
            if (!std::strcmp(rows[i].sec, sec) && !std::strcmp(rows[i].key, key)
                && rows[i].isBool)
                return rows[i].b;
        return defValue;
    }
};

using namespace Runtime::Tempo;

static void TestFlagGates()
{
    // Без ключа: пилотный вид (гоблин) включён по умолчанию — иначе бэкфилл
    // дописал бы в живой ini "off" и пилот не включился бы. Прочие виды — нет.
    FakeIni ini;
    RanksNumbers off = RanksFromIni(ini, "uEm0100");
    assert(off.enabled);
    RanksNumbers other = RanksFromIni(ini, "uEm0300");
    assert(!other.enabled);
    RegisterRanks("uEm0300", other);
    int st = -1; float sz = 0, at = 0;
    assert(!RankPickFor("uEm0300", 0x1000, &st, &sz, &at));   // выключено

    // 85.41: вид ВНЕ списка не включается никакой ини. В поле 85.40 у волка
    // в живом ini стояло ranks = on — и волки получили полосу роста и
    // множители атаки, хотя размер волка трогать нельзя.
    FakeIni wolfOn;
    wolfOn.SetBool("species.uEm0200", "ranks", true);
    RanksNumbers wolf = RanksFromIni(wolfOn, "uEm0200");
    assert(!wolf.enabled);                       // ключ не имеет силы
    RegisterRanks("uEm0200", wolf);
    assert(!RankPickFor("uEm0200", 0x777, &st, &sz, &at));
    assert(GetRanks("uEm0200", &wolf) == false);

    // Явный off в ini выключает и гоблина.
    FakeIni offIni;
    offIni.SetBool("species.uEm0100", "ranks", false);
    RanksNumbers gobOff = RanksFromIni(offIni, "uEm0100");
    assert(!gobOff.enabled);
    RegisterRanks("uEm0100", gobOff);
    assert(!RankPickFor("uEm0100", 0x1000, &st, &sz, &at));
    assert(!GetRanks("uEm0100", &gobOff));
    assert(!RankPickFor("uEm0200", 0x1000, &st, &sz, &at));   // вида нет вовсе

    FakeIni on;
    on.SetBool("species.uEm0100", "ladder", true);
    RanksNumbers nums = RanksFromIni(on, "uEm0100");
    assert(nums.enabled);
    // встроенные числа пилота: вес положительный, полосы не вывернуты, атака >= 1
    float sum = 0;
    for (int i = 0; i < kRankSteps; ++i) {
        assert(nums.step[i].weight > 0.0f);
        assert(nums.step[i].sizeMin <= nums.step[i].sizeMax);
        assert(nums.step[i].atk >= 1.0f);
        sum += nums.step[i].weight;
    }
    assert(std::fabs(sum - 1.0f) < 0.001f);
    RegisterRanks("uEm0100", nums);
    assert(GetRanks("uEm0100", &nums));
}

static void TestCustomAndSanitize()
{
    FakeIni ini;
    ini.SetBool("species.uEm0100", "ranks", true);
    // перевёрнутая полоса ветерана и атака ниже ванили — обе чинятся
    ini.SetFloat("ranks", "rank2SizeMin", 1.20f);
    ini.SetFloat("ranks", "rank2SizeMax", 1.10f);
    ini.SetFloat("ranks", "rank2Atk", 0.40f);
    ini.SetFloat("ranks", "rank0Weight", 0.0f);   // вес 0 допустим
    RanksNumbers n = RanksFromIni(ini, "uEm0100");
    assert(n.enabled);
    assert(n.step[2].sizeMin <= n.step[2].sizeMax);
    assert(n.step[2].sizeMin >= 0.70f && n.step[2].sizeMax <= 1.40f);
    assert(n.step[2].atk >= 1.0f);

    // полный мусор в весах -> встроенные веса
    FakeIni junk;
    junk.SetBool("species.uEm0100", "ranks", true);
    for (int i = 0; i < kRankSteps; ++i) {
        char k[32]; snprintf(k, sizeof(k), "rank%dWeight", i);
        junk.SetFloat("ladder", k, 0.0f);
    }
    RanksNumbers j = RanksFromIni(junk, "uEm0100");
    float sum = 0;
    for (int i = 0; i < kRankSteps; ++i) sum += j.step[i].weight;
    assert(sum > 0.9f);
}

static void TestDeterminismAndSpread()
{
    FakeIni ini;
    ini.SetBool("species.uEm0100", "ranks", true);
    RanksNumbers n = RanksFromIni(ini, "uEm0100");
    RegisterRanks("uEm0100", n);

    // детерминированность: два вопроса к одному телу дают одно и то же
    int s1 = -1, s2 = -1; float z1 = 0, z2 = 0, a1 = 0, a2 = 0;
    assert(RankPickFor("uEm0100", 0x10D50060, &s1, &z1, &a1));
    assert(RankPickFor("uEm0100", 0x10D50060, &s2, &z2, &a2));
    assert(s1 == s2 && std::fabs(z1 - z2) < 0.00001f && std::fabs(a1 - a2) < 0.00001f);

    // полоса размера ступени соблюдается: размер не гуляет по всему коридору
    int counts[kRankSteps] = {};
    const int N = 4096;
    for (int i = 0; i < N; ++i) {
        int st = -1; float sz = 0, at = 0;
        const uintptr_t body = 0x10D00000u + (uintptr_t)i * 0x1000u;
        assert(RankPickFor("uEm0100", body, &st, &sz, &at));
        assert(st >= 0 && st < kRankSteps);
        assert(sz >= n.step[st].sizeMin - 0.0001f && sz <= n.step[st].sizeMax + 0.0001f);
        assert(std::fabs(at - n.step[st].atk) < 0.0001f);
        ++counts[st];
    }
    // ступени действительно разные: рядовых большинство, элита/мини-босс редки
    const int rabble = counts[0] + counts[1];
    assert(rabble > N * 60 / 100);
    assert(counts[2] > 0 && counts[2] < N * 30 / 100);
    assert(counts[3] < N * 12 / 100);
    assert(counts[4] < N * 8 / 100);
    // и хотя бы один мини-босс на четыре тысячи тел найдётся (иначе ступень мертва)
    assert(counts[4] > 0);

    // доступ к конкретной ступени (нужен вожаку: он берёт старшую)
    float vsz = 0, vat = 0;
    assert(RankNumbers("uEm0100", kRankSteps - 1, &vsz, &vat));
    assert(std::fabs(vat - n.step[kRankSteps - 1].atk) < 0.0001f);
    assert(!RankNumbers("uEm0100", kRankSteps, &vsz, &vat));   // за границей
    assert(!RankNumbers("uEm0200", 0, &vsz, &vat));              // вид не в списке

    std::cout << "rank spread: novice=" << counts[0] << " soldier=" << counts[1]
              << " veteran=" << counts[2] << " elite=" << counts[3]
              << " miniboss=" << counts[4] << " of " << N << "\n";
}

static void TestSessionSummary()
{
    // 85.42: сводка «кто заспавнился» — то, что владелец читает в логе.
    NoteRankIssued("uEm0100", 1);
    NoteRankIssued("uEm0100", 1);
    NoteRankIssued("uEm0100", 4);
    NoteRankIssued("uEm0100", 0);
    char buf[220];
    RankSummary(buf, sizeof(buf));
    const std::string s(buf);
    assert(s.find("uEm0100:") != std::string::npos);
    assert(s.find("1 2 0 0 1") != std::string::npos);   // 1 новичок, 2 солдата, 1 мини-босс
}

int main()
{
    TestFlagGates();
    TestCustomAndSanitize();
    TestDeterminismAndSpread();
    TestSessionSummary();
    std::cout << "ranks: PASS (флаг вида, встроенные числа, мусор, "
                 "детерминизм, разброс ступеней)\n";
    return 0;
}
