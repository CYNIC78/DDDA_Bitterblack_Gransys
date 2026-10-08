// Рантайм-проверка чисел вида из ini (85.36).
//
// ЗАЧЕМ. Владелец правит потолки разгона гоблина/хоба в ddda_ai_overhaul.ini.
// Цена ошибки высока и НЕ видна: если потолок опустить ниже базового диапазона,
// приказ директора отобьётся на каждом теле, и в поле это выглядит как «монстры
// перестали реагировать», а не как «в ini опечатка». Здесь проверяется, что:
//   * ключа нет            -> число из карточки (старый ini = прежнее поведение);
//   * мусор в ключе        -> число из карточки, с пометкой;
//   * перевёрнутый диапазон -> чинится;
//   * выход за пределы движка -> зажимается (бег 0.75..1.30, замах 0.70..1.40);
//   * потолок ниже базового -> ПОДНИМАЕТСЯ до базового (иначе молчаливый отказ);
//   * низ == верх          -> расширяется (иначе профиль не регистрируется);
//   * tempoRage = 0        -> вид честно выключается, а не молча.
//
// КАК. Настоящий iniConfig тянет WinAPI профилей, которых в песочнице нет,
// поэтому подменяем его словарём «секция|ключ» -> строка. Проверяется боевой
// src/monsterai/SpeciesTuning.cpp — он собирается как есть, без правок.
#include "director_stdafx.h"
#include <map>
#include <string>
#include <vector>
#include <utility>
#include <cstdio>
#include <cstdlib>
#include <cassert>
using std::string;

typedef char CHAR;
#define INVALID_FILE_ATTRIBUTES ((DWORD)0xFFFFFFFFu)
#define GENERIC_WRITE 0x40000000u
#define CREATE_NEW    1u
inline DWORD  GetFileAttributesA(const char*) { return INVALID_FILE_ATTRIBUTES; }
inline int    WriteFile(void*, const void*, DWORD, DWORD*, void*) { return 0; }
struct WIN32_FILE_ATTRIBUTE_DATA {
    DWORD dwFileAttributes;
    FILETIME ftCreationTime, ftLastAccessTime, ftLastWriteTime;
    DWORD nFileSizeHigh, nFileSizeLow;
};
#define GetFileExInfoStandard 0u
inline int GetFileAttributesExA(const char*, int, WIN32_FILE_ATTRIBUTE_DATA*) { return 0; }

#include "iniConfig.h"

std::ofstream logFile("/tmp/species_tuning_test.log");

// ---------------------------------------------------------------- двойник ini ---
static std::map<string, string> g_ini;
static void IniSet(const char* section, const char* key, const char* value)
{
    g_ini[string(section) + "|" + key] = value;
}
// Что было бы дописано в файл: (секция|ключ, значение).
static std::vector<std::pair<string, string> > g_backfill;
// g_cfg объявлен ниже (ему нужен уже определённый iniConfig), поэтому
// autoBackfill здесь не сбрасывается: это делает сам тест, которому важно
// состояние переключателя.
static void IniClear() { g_ini.clear(); g_backfill.clear(); }

iniConfig::iniConfig(LPCSTR fileName) : fileName(fileName) {}

// 86.03, ПОСЛЕ ПОЛЯ: двойник обязан вести себя как настоящий iniConfig, иначе
// тест не ловит ошибки дописки. Настоящий getFloat при отсутствии ключа
// возвращает дефолт И дописывает ключ в файл (iniConfig.cpp:168, sprintf "%g").
// Прежний двойник только возвращал дефолт — и ровно поэтому первый вариант
// шага D дописал владельцу в ini 16 ключей со значением nan, а гейт был
// зелёный. Лог дописок ниже позволяет это проверить.
float iniConfig::getFloat(LPCSTR section, LPCSTR key, float defValue)
{
    const string k = string(section) + "|" + key;
    const std::map<string, string>::const_iterator it = g_ini.find(k);
    if (it != g_ini.end()) return (float)atof(it->second.c_str());
    if (autoBackfill) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%g", defValue);   // NaN -> "nan", как в студии
        g_ini[k] = buf;
        g_backfill.push_back(std::make_pair(k, string(buf)));
    }
    return defValue;
}
bool iniConfig::getBool(LPCSTR section, LPCSTR key, bool defValue)
{
    std::map<string, string>::const_iterator it = g_ini.find(string(section) + "|" + key);
    if (it == g_ini.end()) return defValue;
    return atoi(it->second.c_str()) != 0;
}
int    iniConfig::getInt(LPCSTR, LPCSTR, int d) { return d; }
string iniConfig::getStr(LPCSTR, LPCSTR, string d) { return d; }
double iniConfig::getDouble(LPCSTR, LPCSTR, double d) { return d; }
void iniConfig::setFloat(LPCSTR, LPCSTR, float) const {}
void iniConfig::setBool(LPCSTR, LPCSTR, bool) const {}
void iniConfig::setInt(LPCSTR, LPCSTR, int) const {}
void iniConfig::setStr(LPCSTR, LPCSTR, string) const {}
void iniConfig::removeKey(LPCSTR, LPCSTR) const {}
unsigned int iniConfig::getUInt(LPCSTR, LPCSTR, unsigned int d) { return d; }
void iniConfig::setUInt(LPCSTR, LPCSTR, unsigned int, bool) const {}
void iniConfig::setDouble(LPCSTR, LPCSTR, double) const {}

#include "../../src/monsterai/SpeciesTuning.cpp"

// ------------------------------------------------------------ адаптер-читатель ---
// Свой экземпляр конфига, а не глобальный `config`: в этом фикстуре глобальное
// имя занято двойником директора (IniConfigStub из director_stdafx.h), и
// настоящий iniConfig под тем же именем не объявить.
static iniConfig g_cfg("species_test.ini");

struct Reader : MonsterAI::BackfillingIniReader {
    float Float(const char* section, const char* key, float defValue) override {
        return g_cfg.getFloat(section, key, defValue);
    }
    bool Bool(const char* section, const char* key, bool defValue) override {
        return g_cfg.getBool(section, key, defValue);
    }
    bool AutoBackfill() const override { return g_cfg.autoBackfill; }
    void SetAutoBackfill(bool on) override { g_cfg.autoBackfill = on; }
};

// 86.03, ПОСЛЕ ПОЛЯ: двойник iniConfig обязан вести себя как настоящий, иначе
// тест не ловит ошибки дописки. Настоящий iniConfig::getFloat при отсутствии
// ключа возвращает дефолт И дописывает ключ в файл (iniConfig.cpp:168,
// sprintf "%g"). Прежний двойник просто возвращал дефолт — и ровно поэтому
// первый вариант шага D дописал владельцу в ini 16 ключей со значением nan, а
// гейт был зелёный.



// Базовый диапазон «как в поле»: [monsterTempo] factorMin/Max, animFactorMin/Max.
static const float kBaseLocoMin = 1.05f, kBaseLocoMax = 1.20f;
static const float kBaseAnimMin = 1.05f, kBaseAnimMax = 1.15f;

static const MonsterAI::SpeciesCard* Goblin()
{
    const MonsterAI::SpeciesCard* c = MonsterAI::FindSpeciesCard("uEm0100");
    assert(c);
    return c;
}
static const MonsterAI::SpeciesCard* Hob()
{
    const MonsterAI::SpeciesCard* c = MonsterAI::FindSpeciesCard("uEm0101");
    assert(c);
    return c;
}

static MonsterAI::SpeciesTempoNumbers Load(const MonsterAI::SpeciesCard* card)
{
    Reader r;
    return MonsterAI::SpeciesTempoFromIni(r, *card, kBaseLocoMin, kBaseLocoMax,
                                          kBaseAnimMin, kBaseAnimMax);
}

// 85.96: как это зовёт директор — базой ВИДА из карточки, а не общей.
static MonsterAI::SpeciesTempoNumbers LoadWithOwnBase(const MonsterAI::SpeciesCard* card)
{
    Reader r;
    return MonsterAI::SpeciesTempoFromIni(r, *card, card->locoLo, card->locoHi,
                                          card->animLo, card->animHi);
}

static bool Near(float a, float b) { return (a > b ? a - b : b - a) < 0.001f; }

// «a заметно ниже b» — с допуском на представление float (1.15f + 0.05f не
// равно 1.20f, и голое >= на таких числах врёт).
static bool Below(float a, float b) { return a < b - 0.0005f; }

// 85.96: БАЗОВЫЙ ТЕМП ВИДА — проводка карточка -> FactorFor/AnimFactorFor.
//
// Зачем это проверяется. FactorFor/AnimFactorFor берут спокойный темп именно
// отсюда, поэтому проверить надо ровно то правило, из-за которого правка и
// затеяна: база берётся из КАРТОЧКИ вида, а общий [monsterTempo] её СДВИГАЕТ.
// Функция чистая (карточка + четыре числа), поэтому живёт в SpeciesTuning.h.
static void TestSpeciesBaseRangeWiring()
{
    const float gLocoLo = 1.05f, gLocoHi = 1.20f;   // поставочная ручка
    const float gAnimLo = 1.05f, gAnimHi = 1.15f;
    float ll, lh, al, ah;

    // 1. Вида без карточки нет -> глобальный диапазон целиком (87 видов из 91).
    MonsterAI::SpeciesBaseRangeFor(0, gLocoLo, gLocoHi, gAnimLo, gAnimHi,
                                   &ll, &lh, &al, &ah);
    assert(Near(ll, gLocoLo) && Near(lh, gLocoHi));
    assert(Near(al, gAnimLo) && Near(ah, gAnimHi));

    // 2. Ручка в поставочном положении: числа карточки проходят КАК НАПИСАНЫ.
    //    Это главное обещание правки — shipped-конфиг не удивляет.
    for (int i = 0; i < MonsterAI::SpeciesCardCount(); ++i) {
        const MonsterAI::SpeciesCard& c = MonsterAI::kSpeciesCards[i];
        MonsterAI::SpeciesBaseRangeFor(&c, gLocoLo, gLocoHi, gAnimLo, gAnimHi,
                                       &ll, &lh, &al, &ah);
        assert(Near(ll, c.locoLo) && Near(lh, c.locoHi));
        assert(Near(al, c.animLo) && Near(ah, c.animHi));
    }

    // 3. ЖИВОЙ ОБЩИЙ РЫЧАГ ДЕЙСТВУЕТ И НА ВИДЫ С КАРТОЧКОЙ. Первый вариант
    //    правки брал пару из карточки как есть — и SetRange(0.80, 0.90) волка
    //    не замедлял вовсе: четыре вида с карточками выпадали из общей ручки.
    //    Сдвиг на ту же величину, что и ручка (отсчёт от нижней границы).
    {
        const MonsterAI::SpeciesCard* wolf = MonsterAI::FindSpeciesCard("uEm0200");
        assert(wolf);
        MonsterAI::SpeciesBaseRangeFor(wolf, 0.80f, 0.90f, 0.80f, 0.90f,
                                       &ll, &lh, &al, &ah);
        // Пол 0.80, а верх 0.95, а не 0.90: сдвиг сохраняет ПОЛОСУ КАРТОЧКИ
        // (у волка 0.15), а ручка здесь сузила разброс до 0.10. Так и задумано —
        // вид рождён с разбросом 0.15, и «опустить темп всей игре» не должно
        // превращать пачку волков в одинаковых. anim у волка ровно 0.10, поэтому
        // там верх совпадает с ручкой.
        assert(Near(ll, 0.80f) && Near(lh, 0.95f));
        assert(Near(al, 0.80f) && Near(ah, 0.90f));

        // сдвиг ВВЕРХ тоже работает и не ломает разброс внутри вида
        MonsterAI::SpeciesBaseRangeFor(wolf, 1.25f, 1.40f, 1.20f, 1.30f,
                                       &ll, &lh, &al, &ah);
        assert(Near(ll, wolf->locoLo + 0.20f));   // пол 1.05 -> 1.25
        assert(Near(lh - ll, wolf->locoHi - wolf->locoLo));   // разброс цел
    }

    // 4. ХОБ: содержательная часть правки. Спокойный бег 1.02..1.15, а не
    //    1.05..1.20 — верх срезан, «перемотки» нет. Замах 1.03..1.10 против
    //    гоблиньих 1.05..1.15: крупнее = чуть медленнее, но не вялый (85.98).
    {
        const MonsterAI::SpeciesCard* hob = MonsterAI::FindSpeciesCard("uEm0101");
        const MonsterAI::SpeciesCard* gob = MonsterAI::FindSpeciesCard("uEm0100");
        assert(hob && gob);
        assert(Near(hob->locoHi, 1.15f) && hob->locoHi < gLocoHi);
        assert(hob->animHi < gob->animHi);
        // Скорость замаха — показатель сложности монстра. Низ базы обязан быть
        // выше 1.02: при 1.00 (85.96) хобы в поле легли в 1.015..1.062 и
        // выглядели замедленными. Проверка именно на нижнюю границу — только
        // animHi < gob->animHi прошёл бы и при 1.00.
        assert(hob->animLo >= 1.02f);
        assert(Near(hob->animLo, 1.03f) && Near(hob->animHi, 1.10f));
        assert(Near(gob->locoHi, gLocoHi));            // гоблин не задет

        // 5. ДОПУСК ПРИКАЗА ЖИВ. Ролл один на базу и ярость, поэтому правило
        //    по краям: rageLo >= baseLo и rageHi >= baseHi. У хоба запас есть
        //    по обоим краям (1.17..1.20 против базы 1.02..1.15).
        assert(hob->rageLocoLo > hob->locoLo);
        assert(hob->rageLocoHi > hob->locoHi);
        assert(hob->rageAnimLo > hob->animLo);
        assert(hob->rageAnimHi > hob->animHi);
    }

    // 6. Мусорная карточка (перевёрнутая пара) не даёт телу мусорный диапазон.
    {
        MonsterAI::SpeciesCard bad = MonsterAI::kSpeciesCards[0];
        bad.locoLo = 1.20f; bad.locoHi = 1.05f;
        MonsterAI::SpeciesBaseRangeFor(&bad, gLocoLo, gLocoHi, gAnimLo, gAnimHi,
                                       &ll, &lh, &al, &ah);
        assert(Near(ll, gLocoLo) && Near(lh, gLocoHi));
    }

    printf("  species base range: card wins, global handle still shifts it,"
           " hob slower, admission alive\n");
}

// 86.03, шаг D: база темпа вида как ключ ini.
//
// ГЛАВНОЕ ПРАВИЛО, которое проверяется здесь: записанный ключ вида АБСОЛЮТЕН —
// общий сдвиг [monsterTempo] к нему уже не применяется. Иначе у числа было бы
// два рычага сразу, и предсказать результат в голове стало бы нельзя.
static void TestSpeciesBaseIniKeys()
{
    struct Out { float llo, lhi, alo, ahi; };
    // Как это зовёт продукт: общий диапазон из [monsterTempo], база — от него.
    Out base(const MonsterAI::SpeciesCard* c, float glo, float ghi,
             float gao, float gah);
    const MonsterAI::SpeciesCard* gob = Goblin();
    const MonsterAI::SpeciesCard* hob = Hob();

    // 1. КЛЮЧЕЙ НЕТ + общий рычаг в поставочном положении (1.05/1.05).
    //    Прежнее поведение: карточка как написана.
    IniClear();
    {
        Reader r; Out o;
        MonsterAI::SpeciesBaseRangeEffective(r, gob, 1.05f, 1.20f, 1.05f, 1.15f,
                                             &o.llo, &o.lhi, &o.alo, &o.ahi);
        assert(Near(o.llo, gob->locoLo) && Near(o.lhi, gob->locoHi));
        assert(Near(o.alo, gob->animLo) && Near(o.ahi, gob->animHi));
    }

    // 2. КЛЮЧЕЙ НЕТ + общий рычаг опущен на 0.25. Прежнее поведение: вид едет
    //    вниз, ширина полосы сохраняется (ручка владельца обязана работать).
    IniClear();
    {
        Reader r; Out o;
        MonsterAI::SpeciesBaseRangeEffective(r, gob, 0.80f, 1.20f, 0.80f, 1.15f,
                                             &o.llo, &o.lhi, &o.alo, &o.ahi);
        assert(Near(o.llo, gob->locoLo - 0.25f) && Near(o.lhi, gob->locoHi - 0.25f));
        assert(Near(o.alo, gob->animLo - 0.25f) && Near(o.ahi, gob->animHi - 0.25f));
    }

    // 3. ГЛАВНОЕ. Ключ записан — он АБСОЛЮТЕН, общий сдвиг НЕ применяется.
    //    Сдвиг тот же -0.25, что в п.2: без этого правила гоблин уехал бы на
    //    0.75..0.85, то есть у числа было бы два рычага сразу.
    IniClear();
    IniSet("species.uEm0100", "baseLocoMin", "1.00");
    IniSet("species.uEm0100", "baseLocoMax", "1.10");
    IniSet("species.uEm0100", "baseAnimMin", "1.02");
    IniSet("species.uEm0100", "baseAnimMax", "1.12");
    {
        Reader r; Out o;
        MonsterAI::SpeciesBaseRangeEffective(r, gob, 0.80f, 1.20f, 0.80f, 1.15f,
                                             &o.llo, &o.lhi, &o.alo, &o.ahi);
        assert(Near(o.llo, 1.00f) && Near(o.lhi, 1.10f));
        assert(Near(o.alo, 1.02f) && Near(o.ahi, 1.12f));
    }

    // 4. Ключи независимы: вписан только верх замаха. Низ обязан остаться
    //    КАРТОЧНЫМ (а не сдвинутым), иначе половина пары молча уехала бы за
    //    общей ручкой, и владелец не понял бы, откуда взялось число.
    IniClear();
    IniSet("species.uEm0101", "baseAnimMax", "1.20");
    {
        Reader r; Out o;
        MonsterAI::SpeciesBaseRangeEffective(r, hob, 0.80f, 1.20f, 0.80f, 1.15f,
                                             &o.llo, &o.lhi, &o.alo, &o.ahi);
        assert(Near(o.alo, hob->animLo));            // карточка, не сдвиг
        assert(Near(o.ahi, 1.20f));                  // ключ
        assert(Near(o.llo, hob->locoLo - 0.25f));    // пару бега ключ не трогал
        assert(Near(o.lhi, hob->locoHi - 0.25f));
    }

    // 5. Мусор и переворот чинятся тем же зажимом, что и ярость: за пределами
    //    loco 0.75..1.30 / anim 0.70..1.40 движок рассинхронизирует хитбокс.
    IniClear();
    IniSet("species.uEm0101", "baseLocoMin", "0.10");   // ниже пола движка
    IniSet("species.uEm0101", "baseLocoMax", "2.00");   // выше потолка
    {
        Reader r; Out o;
        MonsterAI::SpeciesBaseRangeEffective(r, hob, 1.05f, 1.20f, 1.05f, 1.15f,
                                             &o.llo, &o.lhi, &o.alo, &o.ahi);
        assert(Near(o.llo, 0.75f) && Near(o.lhi, 1.30f));
    }
    IniClear();
    IniSet("species.uEm0101", "baseLocoMin", "1.20");   // перевёрнутая пара
    IniSet("species.uEm0101", "baseLocoMax", "1.00");
    {
        Reader r; Out o;
        MonsterAI::SpeciesBaseRangeEffective(r, hob, 1.05f, 1.20f, 1.05f, 1.15f,
                                             &o.llo, &o.lhi, &o.alo, &o.ahi);
        assert(o.llo < o.lhi);                         // обмен, а не мусор
        assert(Near(o.llo, 1.00f) && Near(o.lhi, 1.20f));
    }

    // 6. Вид БЕЗ карточки (87 из 91): ключей у него нет, общий диапазон как
    //    есть — шаг D не должен задеть остальных.
    IniClear();
    {
        Reader r; Out o;
        MonsterAI::SpeciesBaseRangeEffective(r, 0, 0.80f, 0.95f, 0.85f, 0.99f,
                                             &o.llo, &o.lhi, &o.alo, &o.ahi);
        assert(Near(o.llo, 0.80f) && Near(o.lhi, 0.95f));
        assert(Near(o.alo, 0.85f) && Near(o.ahi, 0.99f));
    }

    // 7. ДОПУСК. Боевое правило (MonsterTempo.cpp, AdmitDirectorMobilization):
    //    `rageLoco >= stableLoco && rageAnim >= stableAnim` по ОДНОМУ роллу
    //    тела, поэтому разница линейна по роллу и достаточно сравнить КРАЯ
    //    попарно: rageLo >= baseLo и rageHi >= baseHi. Строгий знак (> ) стоил
    //    трёх билдов: у гоблина rageLocoLo 1.15 при базе 1.05..1.20, и верх
    //    совпадает 1.20 == 1.20.
    //
    //    Сравнивать rageLo с ВЕРХОМ базы нельзя — это другая величина, и такая
    //    проверка отсеяла бы законные карточки.
    IniClear();
    for (int i = 0; i < MonsterAI::SpeciesCardCount(); ++i) {
        const MonsterAI::SpeciesCard* c = &MonsterAI::kSpeciesCards[i];
        if (!c->tempoRage) continue;
        Reader r; Out o;
        MonsterAI::SpeciesBaseRangeEffective(r, c, 1.05f, 1.20f, 1.05f, 1.15f,
                                             &o.llo, &o.lhi, &o.alo, &o.ahi);
        assert(!Below(c->rageLocoLo, o.llo));   // низ ярости не ниже низа базы
        assert(!Below(c->rageLocoHi, o.lhi));   // верх ярости не ниже верха базы
        assert(!Below(c->rageAnimLo, o.alo));
        assert(!Below(c->rageAnimHi, o.ahi));
    }
}

// 86.03, ПОСЛЕ ПОЛЯ: дописка ключа обязана нести ЧИСЛО КАРТОЧКИ, а не NaN.
//
// Поле 86.03 показало 16 строк «Config: added missing key [species.uEmXXXX]
// baseLocoMin = nan». Причина: зонд «записан ли ключ?» передавал дефолтом NaN,
// а настоящий iniConfig::getFloat дописывает в файл ИМЕННО ДЕФОЛТ
// (iniConfig.cpp:168, sprintf "%g"). Ключи появлялись, но мусором.
//
// Прежний двойник iniConfig в этом тесте автодописку не воспроизводил, поэтому
// гейт был зелёный. Теперь воспроизводит (см. getFloat выше), и проверка ниже
// ловит ровно эту ошибку.
static void TestSpeciesBaseBackfillValue()
{
    IniClear();
    g_cfg.autoBackfill = true;

    const MonsterAI::SpeciesCard* hob = Hob();
    float llo, lhi, alo, ahi;
    Reader r;
    MonsterAI::SpeciesBaseRangeEffective(r, hob, 1.05f, 1.20f, 1.05f, 1.15f,
                                         &llo, &lhi, &alo, &ahi);

    // Поведение в этот запуск: ключей нет, значит карточка + общий сдвиг.
    assert(Near(llo, hob->locoLo) && Near(lhi, hob->locoHi));
    assert(Near(alo, hob->animLo) && Near(ahi, hob->animHi));

    // ГЛАВНОЕ: в файл ушли четыре ключа, и каждый — с числом карточки.
    assert(g_backfill.size() == 4);
    for (size_t i = 0; i < g_backfill.size(); ++i) {
        const std::string& v = g_backfill[i].second;
        assert(v != "nan");
        assert(v != "-nan");
        assert(v != "nan(ind)");
        assert(!v.empty());
        const float parsed = (float)atof(v.c_str());
        assert(parsed == parsed);                 // не NaN
        assert(parsed >= 0.70f && parsed <= 1.40f);
    }

    // Значения — дословно карточка хоба, потому что общий рычаг в поставочном
    // положении (сдвиг 0). Владелец видит в ini те же числа, что в карточке.
    const std::map<std::string, std::string>& m = g_ini;
    assert(m.find("species.uEm0101|baseLocoMin") != m.end());
    assert(Near((float)atof(m.find("species.uEm0101|baseLocoMin")->second.c_str()), hob->locoLo));
    assert(Near((float)atof(m.find("species.uEm0101|baseAnimMax")->second.c_str()), hob->animHi));

    // И после дописки ключи стали АБСОЛЮТНЫМИ: общий сдвиг к ним больше не
    // применяется. Это прямое следствие правила шага D, и его стоит видеть.
    {
        Reader r2;
        MonsterAI::SpeciesBaseRangeEffective(r2, hob, 0.80f, 1.20f, 0.80f, 1.15f,
                                             &llo, &lhi, &alo, &ahi);
        assert(Near(llo, hob->locoLo));           // НЕ сдвинулось на -0.25
        assert(Near(ahi, hob->animHi));
    }

    g_cfg.autoBackfill = true;
}

int main()
{
    // 1. Ключей нет вовсе: старый ini обязан вести себя как раньше.
    IniClear();
    {
        const MonsterAI::SpeciesTempoNumbers n = Load(Goblin());
        assert(n.rageEnabled);
        assert(!n.sanitized);
        assert(Near(n.rageLocoMin, 1.15f) && Near(n.rageLocoMax, 1.20f));
        assert(Near(n.rageAnimMin, 1.15f) && Near(n.rageAnimMax, 1.24f));
    }

    // 2. Владелец поднял потолки — применяется как написано.
    IniClear();
    IniSet("species.uEm0100", "rageLocoMin", "1.16");
    IniSet("species.uEm0100", "rageLocoMax", "1.24");
    IniSet("species.uEm0100", "rageAnimMin", "1.20");
    IniSet("species.uEm0100", "rageAnimMax", "1.34");
    {
        const MonsterAI::SpeciesTempoNumbers n = Load(Goblin());
        assert(!n.sanitized);
        assert(Near(n.rageLocoMin, 1.16f) && Near(n.rageLocoMax, 1.24f));
        assert(Near(n.rageAnimMin, 1.20f) && Near(n.rageAnimMax, 1.34f));
    }

    // 3. ГЛАВНОЕ: потолок ниже базового диапазона. Без зажима приказ отбился бы
    //    на каждом теле МОЛЧА (baseline-outside-profile) — это и есть ловушка.
    IniClear();
    IniSet("species.uEm0100", "rageLocoMin", "0.90");
    IniSet("species.uEm0100", "rageLocoMax", "1.02");
    {
        const MonsterAI::SpeciesTempoNumbers n = Load(Goblin());
        assert(n.sanitized);
        assert(n.rageLocoMin >= kBaseLocoMin);
        assert(n.rageLocoMax >= kBaseLocoMax);
        assert(n.rageLocoMax > n.rageLocoMin);
    }

    // 4. Мусор в ключе -> число карточки и пометка в лог.
    IniClear();
    IniSet("species.uEm0100", "rageAnimMax", "abc");
    {
        const MonsterAI::SpeciesTempoNumbers n = Load(Goblin());
        // atof("abc") = 0.0 -> поднимется до базового верха замаха, но не 0.
        assert(n.rageAnimMax >= kBaseAnimMax);
        assert(n.rageAnimMax > n.rageAnimMin);
        assert(n.sanitized);
    }

    // 5. Перевёрнутый диапазон: низ выше верха — чинится, а не отвергается.
    IniClear();
    IniSet("species.uEm0101", "rageLocoMin", "1.28");
    IniSet("species.uEm0101", "rageLocoMax", "1.14");
    {
        const MonsterAI::SpeciesTempoNumbers n = Load(Hob());
        assert(n.rageLocoMin < n.rageLocoMax);
        assert(n.sanitized);
    }

    // 6. Пределы движка: выше него — рассинхрон хитбокса, поэтому зажимается.
    IniClear();
    IniSet("species.uEm0101", "rageLocoMax", "1.90");
    IniSet("species.uEm0101", "rageAnimMax", "1.90");
    {
        const MonsterAI::SpeciesTempoNumbers n = Load(Hob());
        assert(Near(n.rageLocoMax, 1.30f));
        assert(Near(n.rageAnimMax, 1.40f));
        assert(n.sanitized);
    }

    // 7. Низ == верх: RegisterRageProfile такой профиль не примет вовсе,
    //    поэтому зазор расширяется здесь.
    IniClear();
    IniSet("species.uEm0100", "rageLocoMin", "1.25");
    IniSet("species.uEm0100", "rageLocoMax", "1.25");
    {
        const MonsterAI::SpeciesTempoNumbers n = Load(Goblin());
        assert(n.rageLocoMax > n.rageLocoMin);
        assert(n.sanitized);
    }

    // 8. Выключатель вида: tempoRage = 0 — честно, а не через «числа ниже базы».
    IniClear();
    IniSet("species.uEm0101", "tempoRage", "0");
    {
        const MonsterAI::SpeciesTempoNumbers n = Load(Hob());
        assert(!n.rageEnabled);
    }

    // 9. Числа по умолчанию не трогают соседа: ключи читаются ПО ВИДУ.
    IniClear();
    IniSet("species.uEm0100", "rageLocoMax", "1.28");
    {
        const MonsterAI::SpeciesTempoNumbers gob = Load(Goblin());
        const MonsterAI::SpeciesTempoNumbers hob = Load(Hob());
        assert(Near(gob.rageLocoMax, 1.28f));
        assert(Near(hob.rageLocoMax, 1.20f));
    }

    // 10. Все числа, которые поставка даёт по умолчанию, обязаны проходить
    //     зажим БЕЗ правок: иначе первый же запуск печатал бы «raised».
    IniClear();
    for (int i = 0; i < MonsterAI::SpeciesCardCount(); ++i) {
        const MonsterAI::SpeciesTempoNumbers n = Load(&MonsterAI::kSpeciesCards[i]);
        if (!n.rageEnabled) continue;
        assert(!n.sanitized);
        assert(n.rageLocoMin >= kBaseLocoMin && n.rageLocoMax >= kBaseLocoMax);
        assert(n.rageAnimMin >= kBaseAnimMin && n.rageAnimMax >= kBaseAnimMax);
        assert(n.rageLocoMin < n.rageLocoMax);
        assert(n.rageAnimMin < n.rageAnimMax);
    }

    // 11. 85.96: БАЗА СТАЛА СВОЕЙ У ВИДА — и вместе с ней поехал смысл зажима.
    //     Раньше SpeciesTuning сверял потолок ярости с ГЛОБАЛЬНЫМ
    //     [monsterTempo] factorMax (1.20) и поднимал всё, что ниже: карточка
    //     хоба 1.15 превращалась в 1.20, то есть решение вида молча не
    //     применялось (это и поймал прежний ассерт !sanitized). Теперь директор
    //     передаёт базу вида, и правило читается так: полоса ярости обязана
    //     лежать не ниже полосы базы ПО ОБОИМ КРАЯМ — иначе тела на краю
    //     приказ отобьёт (baseline-outside-profile) или, что хуже, замедлит.
    IniClear();
    for (int i = 0; i < MonsterAI::SpeciesCardCount(); ++i) {
        const MonsterAI::SpeciesCard& c = MonsterAI::kSpeciesCards[i];
        if (!c.tempoRage) continue;

        // база вида живая и внутри пределов движка
        assert(c.locoLo < c.locoHi);
        assert(c.animLo < c.animHi);
        assert(c.locoLo >= 0.75f && c.locoHi <= 1.30f);
        assert(c.animLo >= 0.70f && c.animHi <= 1.40f);

        // ГЛАВНОЕ: со своей базой поставочные числа проходят зажим БЕЗ правок.
        // Именно это и было сломано до 85.96: зажим сверялся с общей базой.
        const MonsterAI::SpeciesTempoNumbers n = LoadWithOwnBase(&c);
        if (!n.rageEnabled) continue;
        assert(!n.sanitized);
        assert(n.rageLocoMin < n.rageLocoMax);
        assert(n.rageAnimMin < n.rageAnimMax);

        // ДОПУСК ПРИКАЗА — ПРАВИЛО ПО КРАЯМ (85.99).
        //
        // Ролл у базы и у ярости ОДИН (HashUnit с той же солью), поэтому
        // разница «ярость - база» линейна по роллу. Значит проверять надо не
        // «низ ярости против верха базы», а ОБА КРАЯ:
        //     rageLo >= baseLo   И   rageHi >= baseHi
        // Это и есть канон 25.09 (DOC_CONSISTENCY_2026_09_25.md: «правило
        // слабее: rageLo >= factorMin, rageHi >= factorMax»), который в коде
        // жил как строгое >. Строгий знак отбивал тело с верхним роллом там,
        // где ярость просто РАВНА базе, — а вызывающий код на отказ делает
        // ReleasePolicy() и снимает приказ со всей пачки.
        //
        // Правило осталось нужным: при rageHi < baseHi тело с верхним роллом
        // приказом ЗАМЕДЛЯЕТСЯ, и вот это гейт обязан ловить.
        //
        // Допуск на плавающую точку обязателен: 1.20f - 0.05f = 1.14999997.
        assert(!Below(n.rageLocoMin, c.locoLo));
        assert(!Below(n.rageLocoMax, c.locoHi));
        assert(!Below(n.rageAnimMin, c.animLo));
        assert(!Below(n.rageAnimMax, c.animHi));
        (void)0;
    }

    // 12. ХОБ: ровно тот случай, ради которого базу и разделили.
    //     а) со своей базой 1.02..1.15 карточка действует как написана —
    //        ни одно число не «поднято до безопасного»;
    //     б) хоб медленнее гоблина и в беге, и в замахе (решение владельца:
    //        крупнее = чуть медленнее), а гоблин остался на глобальных числах —
    //        то есть правка задела только хоба;
    //     в) разброс внутри вида сохранён: пачка остаётся пёстрой.
    //
    // Чего здесь НЕТ и почему. Хотелось заодно доказать, что с ОБЩЕЙ базой
    // карточка хоба перебивалась бы зажимом (так и было до 85.96 при ярости
    // 1.10..1.15). После того, как низ ярости подняли до 1.17, с общей базой
    // 1.05..1.20 карточка проходит зажим без правок — то есть по ЧИСЛАМ ЯРОСТИ
    // обе базы теперь дают одно и то же. Разница осталась в другом и более
    // важном месте: ЧТО СЧИТАТЬ БАЗОЙ СПОКОЙНОГО ТЕМПА (1.02..1.15 или
    // 1.05..1.20). Это проверяет не этот файл, а шаг 2i2 (ranks_test), где
    // собирается MonsterTempo.cpp и доступна Runtime::Tempo::SpeciesBaseRangeFor.
    IniClear();
    {
        const MonsterAI::SpeciesCard* gob = Goblin();
        const MonsterAI::SpeciesCard* hob = Hob();
        const MonsterAI::SpeciesTempoNumbers hobOwn = LoadWithOwnBase(hob);
        const MonsterAI::SpeciesTempoNumbers gobOwn = LoadWithOwnBase(gob);

        assert(!hobOwn.sanitized);
        assert(Near(hobOwn.rageLocoMax, hob->rageLocoHi));   // карточка не перебита
        assert(Near(hobOwn.rageLocoMin, hob->rageLocoLo));

        assert(hob->locoHi < gob->locoHi);                    // бег: хоб медленнее
        assert(hob->animHi < gob->animHi);                    // замах: хоб медленнее
        assert(hob->animLo >= 1.02f);                         // но не вялый (85.98)
        assert(Near(gob->locoLo, kBaseLocoMin) && Near(gob->locoHi, kBaseLocoMax));
        assert(Near(gob->animLo, kBaseAnimMin) && Near(gob->animHi, kBaseAnimMax));
        assert(Near(gobOwn.rageLocoMax, gob->rageLocoHi));    // гоблин не задет

        // разброс внутри вида: у хоба по бегу 13 %, у гоблина 14 % — пачка
        // пёстрая, сдвинут уровень, а не сжат диапазон.
        const float hobSpread = hob->locoHi / hob->locoLo;
        const float gobSpread = gob->locoHi / gob->locoLo;
        assert(hobSpread > 1.10f && hobSpread < 1.20f);
        assert(gobSpread > 1.10f && gobSpread < 1.20f);
    }

    TestSpeciesBaseRangeWiring();
    TestSpeciesBaseIniKeys();
    TestSpeciesBaseBackfillValue();
    printf("SpeciesTuning Build 85.36/85.96/86.03: PASS (ключ отсутствует = карточка,"
           " мусор/переворот/пределы/ниже базы чинятся, tempoRage=0 выключает,"
           " база вида своя и ярость не ниже базы по обоим краям)\n");
    return 0;
}
