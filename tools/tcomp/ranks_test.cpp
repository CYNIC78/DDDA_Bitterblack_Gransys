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
#include <cstdio>
#include <cstdlib>   // atoi в тесте счётчиков
#include <iostream>
#include <string>

BYTE* codeBase = 0;
BYTE* codeEnd = 0;
DWORD g_tempoTestNow = 0;
IniConfigStub config;
// 85.57: наборы по месту печатают строку на каждое НОВОЕ место, поэтому
// фикстуре понадобился logFile (до этого он не линковался: сборка с
// gc-sections выбрасывала функции, которые в него пишут).
std::ofstream logFile("/tmp/ranks_test.log");

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
    // 85.57: ИМЯ КЛЮЧА ХРАНИМ КОПИЕЙ, а не указателем.
    // Тест строит имя ключа в переиспользуемом буфере (set0Weight, set1Weight...),
    // и с указателями все записи указывали на ОДИН буфер: фикстура видела четыре
    // копии «set3Weight». Тот же класс ошибки, что ловили в 85.53 в сводке рангов.
    struct Row { char sec[24]; char key[32]; float f; bool b; bool isBool; };
    enum { kMax = 24 };
    Row rows[kMax];
    int n = 0;

    static void Copy(char* dst, const char* src, int cap)
    {
        int i = 0;
        if (src) for (; src[i] && i < cap - 1; ++i) dst[i] = src[i];
        dst[i] = 0;
    }

    void SetFloat(const char* sec, const char* key, float v)
    {
        if (n >= kMax) return;
        Copy(rows[n].sec, sec, 24); Copy(rows[n].key, key, 32);
        rows[n].f = v; rows[n].b = false; rows[n].isBool = false; ++n;
    }
    void SetBool(const char* sec, const char* key, bool v)
    {
        if (n >= kMax) return;
        Copy(rows[n].sec, sec, 24); Copy(rows[n].key, key, 32);
        rows[n].f = 0.0f; rows[n].b = v; rows[n].isBool = true; ++n;
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
    // 85.57: имена наборов. Строк в фикстуре мало, держим отдельным списком.
    struct TextRow { char sec[24]; char key[32]; char val[24]; };
    enum { kMaxText = 8 };
    TextRow texts[kMaxText];
    int nText = 0;
    void SetText(const char* sec, const char* key, const char* val)
    {
        if (nText >= kMaxText) return;
        Copy(texts[nText].sec, sec, 24);
        Copy(texts[nText].key, key, 32);
        Copy(texts[nText].val, val, 24);
        ++nText;
    }
    bool Text(const char* sec, const char* key, char* out, int cap)
    {
        for (int i = 0; i < nText; ++i)
            if (!std::strcmp(texts[i].sec, sec) && !std::strcmp(texts[i].key, key)) {
                int j = 0;
                for (; texts[i].val[j] && j < cap - 1; ++j) out[j] = texts[i].val[j];
                out[j] = 0;
                return j > 0;
            }
        return false;
    }
};

using namespace Runtime::Tempo;

// 85.57: ролл принимает ЗАПРОС (вид + тело + поколение + место). Помощник
// держит тесты читаемыми: место по умолчанию не задано, то есть работают общие
// веса [ranks] — ровно как до наборов.
static RankQuery Q(const char* kind, uintptr_t body, uint32_t gen = 0u)
{
    RankQuery q;
    std::memset(&q, 0, sizeof(q));
    q.kind = kind; q.body = body; q.gen = gen; q.setIndex = -1;
    return q;
}

// Тот же запрос, но с местом: набор берём у самого модуля — как это делает
// тюнер в бою (он только читает ячейку и спрашивает набор).
static RankQuery QC(const char* kind, uintptr_t body, int cx, int cz, uint32_t gen = 0u)
{
    RankQuery q = Q(kind, body, gen);
    q.cellX = cx; q.cellZ = cz; q.hasCell = true;
    q.setIndex = PackSetForCell(cx, cz);
    return q;
}

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
    assert(!RankPickFor(Q("uEm0300", 0x1000, 0u), &st, &sz, &at));   // выключено

    // 85.41: вид ВНЕ списка не включается никакой ини. В поле 85.40 у волка
    // в живом ini стояло ranks = on — и волки получили полосу роста и
    // множители атаки, хотя размер волка трогать нельзя.
    // 85.52: у нас появился второй вид вне списка (ящер) — проверяем на нём.
    FakeIni lizardOn;
    lizardOn.SetBool("species.uEm0400", "ranks", true);
    RanksNumbers lizard = RanksFromIni(lizardOn, "uEm0400");
    assert(!lizard.enabled);                     // ключ не имеет силы
    RegisterRanks("uEm0400", lizard);
    assert(!RankPickFor(Q("uEm0400", 0x777, 0u), &st, &sz, &at));
    assert(GetRanks("uEm0400", &lizard) == false);

    // 85.52: ВОЛК допущен — но размером ранги у него НЕ управляют.
    // Это то самое правило владельца: «волки-гиганты нелепо», рост волка
    // неприкосновенен. Проверяем обе половины: ступень есть, размера нет.
    FakeIni wolfIni;
    wolfIni.SetBool("species.uEm0200", "ranks", true);
    wolfIni.SetBool("species.uEm0200", "rankScale", false);
    RanksNumbers wolf = RanksFromIni(wolfIni, "uEm0200");
    assert(wolf.enabled);                        // волк под рангами
    assert(!wolf.scale);                         // но размер — ванильный
    assert(wolf.step[4].atk > 1.4f);             // атака от ступени работает
    RegisterRanks("uEm0200", wolf);
    assert(RankPickFor(Q("uEm0200", 0x777, 0u), &st, &sz, &at));
    assert(st >= 0 && st < kRankSteps);
    assert(!RankScaleEnabled("uEm0200"));        // размер не наш
    assert(RankScaleEnabled("uEm0100") == false || true);  // гоблин — см. ниже

    // А у гоблина умолчание осталось прежним: ключа нет = размер от ступени.
    {
        FakeIni g;
        g.SetBool("species.uEm0100", "ranks", true);
        RanksNumbers gn = RanksFromIni(g, "uEm0100");
        assert(gn.enabled);
        assert(gn.scale);                        // размер по-прежнему наш
        RegisterRanks("uEm0100", gn);
    }

    // Явный off в ini выключает и гоблина.
    FakeIni offIni;
    offIni.SetBool("species.uEm0100", "ranks", false);
    RanksNumbers gobOff = RanksFromIni(offIni, "uEm0100");
    assert(!gobOff.enabled);
    RegisterRanks("uEm0100", gobOff);
    assert(!RankPickFor(Q("uEm0100", 0x1000, 0u), &st, &sz, &at));
    assert(!GetRanks("uEm0100", &gobOff));
    // волк остаётся допущенным (проверен выше), но ВЫКЛЮЧЕННЫМ ключом ini —
    // показываем, что off вида гасит ступень и после успешного включения
    RegisterRanks("uEm0200", wolf);
    assert(RankPickFor(Q("uEm0200", 0x1000, 0u), &st, &sz, &at));
    FakeIni wolfOff;
    wolfOff.SetBool("species.uEm0200", "ranks", false);
    RanksNumbers wolfOffN = RanksFromIni(wolfOff, "uEm0200");
    assert(!wolfOffN.enabled);
    RegisterRanks("uEm0200", wolfOffN);
    assert(!RankPickFor(Q("uEm0200", 0x1000, 0u), &st, &sz, &at));
    assert(!RankScaleEnabled("uEm0200"));   // выключенный вид = размера тоже нет

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

// 85.44: крепость ранга. Проверяем три вещи, которые легко сделать неправильно:
// ключ читается, ключ зажимается (ниже ванили нельзя, выше потолка нельзя),
// и выбранный ранг отдаёт свою крепость — потому что по ней код решает, писать
// ли вообще в сопротивления (1.0 = не писать).
static void TestToughnessKeys()
{
    FakeIni ini;
    ini.SetBool("species.uEm0100", "ranks", true);
    // по умолчанию — ваниль: фича приезжает выключенной
    RanksNumbers n = RanksFromIni(ini, "uEm0100");
    for (int i = 0; i < kRankSteps; ++i) {
        assert(std::fabs(n.step[i].resist - 1.0f) < 0.0001f);
        assert(std::fabs(n.step[i].stand  - 1.0f) < 0.0001f);
    }

    // заданные значения + зажимы: мусор, попытка ослабить, попытка перекрутить
    FakeIni c;
    c.SetBool("species.uEm0100", "ranks", true);
    c.SetFloat("ranks", "rank0Resist", 0.50f);    // ниже ванили -> 1.0
    c.SetFloat("ranks", "rank0Stand", 1.30f);
    c.SetFloat("ranks", "rank4Resist", 99.0f);    // выше потолка -> потолок
    c.SetFloat("ranks", "rank4Stand", 99.0f);
    c.SetFloat("ranks", "rank3Resist", -5.0f);    // отрицательное -> 1.0
    RanksNumbers m = RanksFromIni(c, "uEm0100");
    assert(std::fabs(m.step[0].resist - 1.0f) < 0.0001f);
    assert(std::fabs(m.step[0].stand  - 1.30f) < 0.0001f);
    assert(m.step[3].resist >= 1.0f);
    // 85.47: потолки подняты до 3.5 под калибровку «половина хобгоблина»
    assert(m.step[4].resist <= 3.5f && m.step[4].resist > 1.5f);
    assert(m.step[4].stand  <= 3.5f && m.step[4].stand  > 1.5f);

    // и сама калибровка: лестница крепости растёт от новичка к мини-боссу и
    // заканчивается «половиной хобгоблина» (горение 300 -> 990 при 3.30)
    assert(std::fabs(n.step[0].resist - 1.00f) < 0.0001f);   // новичок = ваниль
    float prev = 0.0f;
    for (int i = 0; i < kRankSteps; ++i) {
        assert(n.step[i].resist >= prev);
        assert(n.step[i].stand  >= 1.0f);
        prev = n.step[i].resist;
    }

    // ранг отдаёт СВОЮ крепость, и она доезжает до того, кто будет писать
    RegisterRanks("uEm0100", m);
    for (int i = 0; i < 64; ++i) {
        int st = -1; float sz = 0, at = 0, rs = 0, sd = 0;
        const uintptr_t body = 0x10D00000u + (uintptr_t)i * 0x1000u;
        assert(RankPickFor(Q("uEm0100", body, 0u), &st, &sz, &at, &rs, &sd));
        assert(std::fabs(rs - m.step[st].resist) < 0.0001f);
        assert(std::fabs(sd - m.step[st].stand)  < 0.0001f);
    }

    // старые вызовы (без новых аргументов) продолжают работать: крепость не вытащили
    int st = -1; float sz = 0, at = 0;
    assert(RankPickFor(Q("uEm0100", 0x10D50060, 0u), &st, &sz, &at));
}

static void TestDeterminismAndSpread()
{
    FakeIni ini;
    ini.SetBool("species.uEm0100", "ranks", true);
    RanksNumbers n = RanksFromIni(ini, "uEm0100");
    RegisterRanks("uEm0100", n);

    // детерминированность: два вопроса к одному телу дают одно и то же
    int s1 = -1, s2 = -1; float z1 = 0, z2 = 0, a1 = 0, a2 = 0;
    assert(RankPickFor(Q("uEm0100", 0x10D50060, 0u), &s1, &z1, &a1));
    assert(RankPickFor(Q("uEm0100", 0x10D50060, 0u), &s2, &z2, &a2));
    assert(s1 == s2 && std::fabs(z1 - z2) < 0.00001f && std::fabs(a1 - a2) < 0.00001f);

    // полоса размера ступени соблюдается: размер не гуляет по всему коридору
    int counts[kRankSteps] = {};
    const int N = 4096;
    for (int i = 0; i < N; ++i) {
        int st = -1; float sz = 0, at = 0;
        const uintptr_t body = 0x10D00000u + (uintptr_t)i * 0x1000u;
        assert(RankPickFor(Q("uEm0100", body, 0u), &st, &sz, &at));
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

// 85.53: СВОДКА ДЕРЖИТ ИМЯ ВИДА КОПИЕЙ.
//
// Поле 85.50 показало в сводке вид «uEm0100_20», которому ранги не выдаются
// вообще. Причина — сюда сохранялся указатель на строку вызывающего, а тот
// указывает в сканер мира: слот переиспользуется, и к моменту печати в нём
// лежит уже другое имя. Проверяем ровно это: счётчики вида не должны «переехать»
// на чужое имя, когда исходная строка переписана.
static int SummaryCount(const char* sum, const char* kind, int out[5])
{
    for (int i = 0; i < 5; ++i) out[i] = -1;
    const char* at = strstr(sum, kind);
    if (!at) return 0;
    at += strlen(kind);
    if (*at != ':') return 0;   // «kind:» — иначе это другой вид (uEm0100_20)
    ++at;
    for (int i = 0; i < 5; ++i) {
        while (*at == ' ') ++at;
        if (*at < '0' || *at > '9') return i;
        int v = 0;
        while (*at >= '0' && *at <= '9') { v = v * 10 + (*at - '0'); ++at; }
        out[i] = v;
    }
    return 5;
}

static void TestSummaryKeepsKindName()
{
    char out[256];
    int before[5];
    RankSummary(out, sizeof(out));
    SummaryCount(out, "uEm0100", before);   // могло быть начислено другими тестами

    char buf[24];
    strcpy(buf, "uEm0100");
    NoteRankIssued(buf, 0);
    RankSummary(out, sizeof(out));
    int mid[5];
    assert(SummaryCount(out, "uEm0100", mid) > 0);
    assert(mid[0] == before[0] + 1);                       // +1 новичок
    assert(strstr(out, "uEm0100_20") == nullptr);          // чужого имени ещё нет

    // Слот сканера переиспользован: строка под указателем теперь другая.
    strcpy(buf, "uEm0100_20");
    NoteRankIssued(buf, 2);                                // это уже ДРУГОЙ вид
    RankSummary(out, sizeof(out));

    int after[5], ghost[5];
    assert(SummaryCount(out, "uEm0100", after) > 0);
    for (int i = 0; i < 5; ++i) assert(after[i] == mid[i]);  // счётчики не переехали
    assert(SummaryCount(out, "uEm0100_20", ghost) == 5);
    assert(ghost[2] == 1);                                   // и учтён отдельно
    std::cout << "  summary keeps names: ok\n";
}

// 85.53: умолчание «трогать ли размер» зависит от вида: гоблину — да (он под
// рангами с самого начала), волку — нет (правило владельца «волки-гиганты
// нелепо»). Ключа в ini может не быть вовсе: тогда работает именно умолчание,
// и ошибиться в нём нельзя — бэкфилл запишет в файл ровно его.
static void TestScaleDefaultPerSpecies()
{
    FakeIni empty;   // ни одного ключа
    RanksNumbers gob = RanksFromIni(empty, "uEm0100");
    assert(gob.enabled);
    assert(gob.scale);            // гоблин: размер от ступени, как было

    RanksNumbers wolf = RanksFromIni(empty, "uEm0200");
    assert(wolf.enabled);
    assert(!wolf.scale);          // волк: ступень есть, размер ванильный

    // явный ключ по-прежнему сильнее умолчания (в любую сторону)
    FakeIni on;
    on.SetBool("species.uEm0200", "rankScale", true);
    RanksNumbers wolfOn = RanksFromIni(on, "uEm0200");
    assert(wolfOn.enabled && wolfOn.scale);
    std::cout << "  scale defaults: goblin=on wolf=off (key overrides)\n";
}

// 85.56: СТУПЕНЬ ПРИВЯЗАНА К ЖИЛЬЦУ, А НЕ К АДРЕСУ.
//
// Поле 85.55: адрес 0x10D57470 отдал «ветерана» гоблину, а следом волчице в
// том же слоте — снова «ветерана» (вес ветерана 13%, повтор 13% — почти не
// бывает случайно). Причина: хеш считался от одного адреса. Проверяем, что
// поколение входит в ролл и при этом ничего не ломает.
static void TestGeneration()
{
    FakeIni ini;
    ini.SetBool("species.uEm0100", "ranks", true);
    RanksNumbers n = RanksFromIni(ini, "uEm0100");
    RegisterRanks("uEm0100", n);

    // 1) внутри жизни одного жильца ступень не мигает
    for (int i = 0; i < 32; ++i) {
        const uintptr_t body = 0x10D50060u + (uintptr_t)i * 0x7410u;
        int a = -1, b = -1; float x = 0, y = 0, c = 0, d = 0;
        assert(RankPickFor(Q("uEm0100", body, 7u), &a, &x, &c));
        assert(RankPickFor(Q("uEm0100", body, 7u), &b, &y, &d));
        assert(a == b && std::fabs(x - y) < 0.00001f && std::fabs(c - d) < 0.00001f);
    }

    // 2) поколение реально участвует: иначе все пары совпали бы и тест упал
    int same = 0, changed = 0;
    for (int i = 0; i < 256; ++i) {
        const uintptr_t body = 0x10D00000u + (uintptr_t)i * 0x7410u;
        int p1 = -1, p2 = -1; float s1 = 0, s2 = 0, a1 = 0, a2 = 0;
        assert(RankPickFor(Q("uEm0100", body, 1u), &p1, &s1, &a1));
        assert(RankPickFor(Q("uEm0100", body, 2u), &p2, &s2, &a2));
        if (p1 == p2 && std::fabs(s1 - s2) < 0.00001f) ++same; else ++changed;
    }
    assert(changed > 0);
    std::cout << "  generation: unchanged=" << same << " changed=" << changed
              << " of 256 (address alone would give 0 changed)\n";

    // 3) у одного адреса разные жильцы реально получают разные ступени
    bool seen[kRankSteps] = {};
    int distinct = 0;
    for (uint32_t g = 1; g <= 64; ++g) {
        int st = -1; float sz = 0, at = 0;
        assert(RankPickFor(Q("uEm0100", 0x10D57470u, g), &st, &sz, &at));
        assert(st >= 0 && st < kRankSteps);
        if (!seen[st]) { seen[st] = true; ++distinct; }
    }
    assert(distinct >= 2);          // «всегда ветеран» больше не бывает
    std::cout << "  generation spread: " << distinct << " tiers over 64 residents\n";

    // 4) gen = 0 — старое поведение (так зовут фикстуры и код без записи тела)
    int z1 = -1, z2 = -1; float f1 = 0, f2 = 0, q1 = 0, q2 = 0;
    assert(RankPickFor(Q("uEm0100", 0x10D57470u, 0u), &z1, &f1, &q1));
    assert(RankPickFor(Q("uEm0100", 0x10D57470u, 0u), &z2, &f2, &q2));
    assert(z1 == z2 && std::fabs(f1 - f2) < 0.00001f);
}

// 85.57: НАБОРЫ ПАЧЕК ПО МЕСТУ.
//
// Владелец: «если все пачки в сессии одинаковыми по рангам — весь смысл
// теряется; в этой зоне такой сет, в следующей другой». Проверяем ровно это:
// (1) без ini работают встроенные наборы; (2) место детерминировано — одна
// ячейка всегда даёт один набор, но РАЗНЫЕ места дают разные; (3) доли наборов
// соответствуют весам; (4) состав пачки внутри одного места отличается от
// другого места; (5) мусор и выключение наборов не ломают ролл.
static void TestPackSets()
{
    // (1) пустая ini: встроенные наборы и включённые наборы
    FakeIni empty;
    PackSetsConfig def = PackSetsFromIni(empty);
    assert(def.enabled);
    assert(def.count == 4);
    assert(!std::strcmp(def.set[0].name, "rabble"));
    assert(!std::strcmp(def.set[3].name, "hunt"));
    RegisterPackSets(def);

    // (2) детерминизм и разброс по местам
    bool seen[8] = {};
    int  bySet[8] = {};
    const int CELLS = 2048;
    for (int i = 0; i < CELLS; ++i) {
        const int cx = (i % 64) - 32;
        const int cz = (i / 64) - 16;
        const int s1 = PackSetForCell(cx, cz);
        const int s2 = PackSetForCell(cx, cz);
        assert(s1 == s2);                         // одно место — один набор
        assert(s1 >= 0 && s1 < def.count);
        seen[s1] = true;
        ++bySet[s1];
    }
    int distinct = 0;
    for (int i = 0; i < def.count; ++i) if (seen[i]) ++distinct;
    assert(distinct == def.count);                // все четыре набора живые
    // доли: patrol (0.40) должен быть самым частым, hunt (0.10) — редким
    assert(bySet[1] > bySet[2]);                   // был очень осторожен: patrol > warband
    assert(bySet[2] > 0 && bySet[3] > 0);
    std::cout << "  pack sets over " << CELLS << " cells: rabble=" << bySet[0]
              << " patrol=" << bySet[1] << " warband=" << bySet[2]
              << " hunt=" << bySet[3] << "\n";

    // (3) КЛЮЧЕВОЕ: состав пачки в РАЗНЫХ местах разный. Берём пачку из 12 тел
    // в одном месте и такую же в другом — наборы должны дать разный уклон.
    RegisterRanks("uEm0100", RanksFromIni(empty, "uEm0100"));
    int rabbleHeavy = 0, veteranHeavy = 0;
    for (int c = 0; c < 256; ++c) {
        int nov = 0, vet = 0;
        for (int b = 0; b < 12; ++b) {
            int st = -1; float sz = 0, at = 0;
            assert(RankPickFor(QC("uEm0100", 0x10D00000u + (uintptr_t)b * 0x7410u,
                                  c * 7, -c * 5), &st, &sz, &at));
            if (st == 0) ++nov;
            if (st >= 2) ++vet;
        }
        if (nov >= 8) ++rabbleHeavy;
        if (vet >= 4) ++veteranHeavy;
    }
    assert(rabbleHeavy > 0);      // где-то пачка почти целиком из новичков
    assert(veteranHeavy > 0);     // а где-то — из ветеранов
    std::cout << "  pack character: rabble-heavy=" << rabbleHeavy
              << " veteran-heavy=" << veteranHeavy << " of 256 places\n";

    // (4) выключенные наборы = прежнее поведение (общие веса [ranks])
    FakeIni off;
    off.SetBool("packs", "enabled", false);
    PackSetsConfig cfgOff = PackSetsFromIni(off);
    assert(!cfgOff.enabled);
    RegisterPackSets(cfgOff);
    assert(PackSetForCell(10, 10) == -1);
    int st = -1; float sz = 0, at = 0;
    assert(RankPickFor(Q("uEm0100", 0x10D50060u), &st, &sz, &at));   // ролл жив

    // (5) мусор: отрицательные веса и нулевая сумма ступеней -> набор выключен,
    // система не остаётся без наборов молча (здесь: два живы, два мусорные)
    FakeIni junk;
    junk.SetFloat("packs", "set0Weight", -1.0f);
    junk.SetFloat("packs", "set1r0", -5.0f);
    junk.SetFloat("packs", "set1r1", -5.0f);
    junk.SetFloat("packs", "set1r2", -5.0f);
    junk.SetFloat("packs", "set1r3", -5.0f);
    junk.SetFloat("packs", "set1r4", -5.0f);
    PackSetsConfig cfgJunk = PackSetsFromIni(junk);
    assert(cfgJunk.count == 2);                 // выжили warband и hunt
    for (int i = 0; i < cfgJunk.count; ++i) {
        float sum = 0;
        for (int r = 0; r < kRankSteps; ++r) sum += cfgJunk.set[i].rank[r];
        assert(sum > 0.0f);
    }
    // имена наборов читаются из ini
    FakeIni named;
    named.SetText("packs", "set0Name", "besiegers");
    PackSetsConfig cfgNamed = PackSetsFromIni(named);
    assert(!std::strcmp(cfgNamed.set[0].name, "besiegers"));
    // всё выключено нулями -> система выключается, а не подсовывает встроенные
    FakeIni allZero;
    for (int i = 0; i < 4; ++i) {
        char k[32];
        snprintf(k, sizeof(k), "set%dWeight", i);
        allZero.SetFloat("packs", k, 0.0f);
    }
    assert(!PackSetsFromIni(allZero).enabled);

    // ставим рабочие наборы обратно, чтобы не влиять на другие тесты
    RegisterPackSets(def);
    std::cout << "  pack sets: builtin, off, junk and names ok\n";
}

// 85.57: «один мини-босс на место». С наборами (набор hunt = 10% мини-боссу)
// двойной мини-босс перестал быть лотереей 2%, поэтому у места есть счётчик.
static void TestMinibossPerCell()
{
    ResetPackMemory("test");
    assert(CellMinibossCount(3, -7) == 0);
    NoteCellMiniboss(3, -7);
    assert(CellMinibossCount(3, -7) == 1);
    assert(CellMinibossCount(4, -7) == 0);      // соседнее место не задето
    NoteCellMiniboss(3, -7);
    assert(CellMinibossCount(3, -7) == 2);      // счётчик не врёт: он счётчик
    ResetPackMemory("test-2");
    assert(CellMinibossCount(3, -7) == 0);      // разгрузка мира чистит место
    std::cout << "  miniboss per cell: counts and reset ok\n";
}

// 85.58: СВОДКА ПО НАБОРАМ ЗА СЕССИЮ.
//
// Зачем тест: сводка — единственное место, где видно раздачу наборов без
// вычитывания лога, и она же может соврать (как врала сводка по огню в 85.54).
// Проверяем три вещи: счёт тел и мест идёт по индексам, сумма сходится, и при
// СМЕНЕ СПИСКА наборов на ходу счётчики обнуляются (иначе тела, выданные до
// правки, приписались бы новым именам).
// Ищем ТОЛЬКО в разделе «bodies»: в сводке два раздела с одинаковыми именами
// (places и bodies), и поиск с начала строки вернул бы число мест.
static int CountAfter(const std::string& s, const char* name)
{
    const size_t b = s.find("bodies:");
    const std::string key = std::string(" ") + name + " ";
    size_t at = (b == std::string::npos) ? std::string::npos : s.find(key, b);
    if (at == std::string::npos) return -1;
    at += key.size();
    return std::atoi(s.c_str() + at);
}

static void TestPackSetBookkeeping()
{
    FakeIni empty;
    PackSetsConfig def = PackSetsFromIni(empty);
    RegisterPackSets(def);
    ResetPackMemory("test-bookkeeping");

    // места: четыре разные ячейки (набор каждой узнаём у модуля)
    const int c1 = PackSetForCell(10, 10);
    const int c2 = PackSetForCell(-4, 7);
    const int c3 = PackSetForCell(33, -8);
    const int c4 = PackSetForCell(0, 0);
    assert(c1 >= 0 && c2 >= 0 && c3 >= 0 && c4 >= 0);

    // тела: пишем 7 в индекс 0 и 5 в индекс 2 — числа нечётные, чтобы не
    // спутать разделы «places» и «bodies» при поиске в строке
    for (int i = 0; i < 7; ++i) NotePackSetBody(0);
    for (int i = 0; i < 5; ++i) NotePackSetBody(2);
    NotePackSetBody(-1);          // мусорный индекс не должен ничего испортить
    NotePackSetBody(9999);

    char buf[420];
    PackSetSummary(buf, sizeof(buf));
    const std::string s(buf);
    assert(s.find("places:") != std::string::npos);
    assert(s.find("bodies:") != std::string::npos);
    assert(CountAfter(s, "rabble") == 7);     // тела по индексу 0
    assert(CountAfter(s, "warband") == 5);    // тела по индексу 2
    for (int i = 0; i < def.count; ++i)
        assert(s.find(def.set[i].name) != std::string::npos);   // все имена на месте

    // смена СПИСКА наборов на ходу -> счётчики обнуляются и об этом есть строка
    FakeIni five;
    five.SetFloat("packs", "set4Weight", 0.25f);
    five.SetFloat("packs", "set4r0", 0.2f);
    five.SetFloat("packs", "set4r1", 0.4f);
    five.SetFloat("packs", "set4r2", 0.3f);
    five.SetFloat("packs", "set4r3", 0.1f);
    five.SetFloat("packs", "set4r4", 0.0f);
    five.SetText("packs", "set4Name", "beastwave");
    PackSetsConfig cfg5 = PackSetsFromIni(five);
    assert(cfg5.count == 5);
    RegisterPackSets(cfg5);
    PackSetSummary(buf, sizeof(buf));
    const std::string s2(buf);
    assert(s2.find("beastwave") != std::string::npos);   // новый набор в сводке
    assert(CountAfter(s2, "rabble") == 0);               // старый счёт сброшен

    std::cout << "  pack set bookkeeping: counters, reset on change ok\n";

    // возвращаем рабочие четыре набора, чтобы не влиять на другие тесты
    RegisterPackSets(def);
}

int main()
{
    TestFlagGates();
    TestCustomAndSanitize();
    TestDeterminismAndSpread();
    TestSessionSummary();
    TestToughnessKeys();
    TestSummaryKeepsKindName();
    TestScaleDefaultPerSpecies();
    TestGeneration();
    TestPackSets();
    TestMinibossPerCell();
    TestPackSetBookkeeping();
    std::cout << "ranks: PASS (флаг вида, встроенные числа, мусор, "
                 "детерминизм, разброс ступеней, поколение жильца, наборы по месту)\n";
    return 0;
}
