// Runtime::World — обход списка актёров, классификация врагов и публикация
// WorldReport. Работает ВСЕГДА: детектор боя и доктрина Guardian зависят
// от этого тика, поэтому он не имеет права гейтиться флагом devtools.

#include "stdafx.h"
#include "RuntimeInternal.h"
#include "MonsterTempo.h"
#include "AggroWatch.h"
#include "LogMemSession.h"   // 86.10: сводка таблицы актёров — в полевой пакет
#include "../ActMap.Generated.h"
#include "../CombatBus.h"

namespace Runtime {

// 85.24: сколько держим ворота записи закрытыми после возвращения мира.
static const DWORD kWorldSettleMs = 2500;

// --- профилировка тика (см. RuntimeInternal.h) ------------------------------
static uint32_t g_scanLastUs = 0;
static uint32_t g_scanAvgUs  = 0;
static uint32_t g_scanMaxUs  = 0;
static uint32_t g_scanTicks  = 0;
static uint32_t g_pollBudget = 0;   // байт памяти, просмотренных за тик

ScanStats ScanGetStats()
{
    ScanStats s;
    s.lastUs = g_scanLastUs;
    s.avgUs  = g_scanAvgUs;
    s.maxUs  = g_scanMaxUs;
    s.ticks  = g_scanTicks;
    s.actors = g_nAct;
    s.pollKb = g_pollBudget / 1024;
    return s;
}

void ScanResetStats()
{
    g_scanLastUs = g_scanAvgUs = g_scanMaxUs = g_scanTicks = 0;
    NameCacheReset();
}

// Замер снимается в деструкторе: в тяжёлой части тика есть ранние return,
// и обычная пара «начало-конец» половину выходов бы потеряла.
namespace {
struct ScanTimer {
    LARGE_INTEGER beg;
    ScanTimer() { QueryPerformanceCounter(&beg); }
    ~ScanTimer()
    {
        static LARGE_INTEGER freq = {};
        if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
        if (freq.QuadPart <= 0) return;
        LARGE_INTEGER end; QueryPerformanceCounter(&end);
        const uint32_t us = (uint32_t)(((end.QuadPart - beg.QuadPart) * 1000000LL)
                                       / freq.QuadPart);
        g_scanLastUs = us;
        if (us > g_scanMaxUs) g_scanMaxUs = us;
        // скользящее среднее 1/8: сглаживает выбросы, но реагирует за секунду
        g_scanAvgUs = g_scanTicks ? (uint32_t)((g_scanAvgUs * 7 + us) / 8) : us;
        ++g_scanTicks;
    }
};
} // namespace

// Существо, которым мы вправе управлять (мутации размера и т.п.).
//
// Сюда входят и мирные животные: заяц — тоже uEm*, и масштабировать его
// можно. Это НЕ значит, что он враг.
bool KindIsCreature(const char* kind)
{
    if (!kind) return false;
    if (kind[0] == 'u' && kind[1] == 'E' && kind[2] == 'm') return true;
    return strcmp(kind, "uHumanEnemy") == 0;
}

// 86.12: КТО ВООБЩЕ ЗАНИМАЕТ СЛОТ ТАБЛИЦЫ АКТЁРОВ — белый список.
//
// Причина в поле, а не в вкусе: 86.10 показал, что таблицу на 94 % заполняют
// объекты карты (uOmObj11000=32 uOmSwingInstancing=32 … против uEm0101=8), и
// директор за бой ни разу не увидел больше восьми хобов при двадцати. 86.11
// отсек семейство «uO» — и на его место встало следующее (uFmSwingBase=46,
// uStageSplitMdl=25, uSkyGrass=5, uStageLowMdl=4). Чёрный список здесь не
// работает в принципе: движок всегда найдёт, чем занять 80 слотов.
//
// Что остаётся: существа (KindIsCreature: uEm*, uHumanEnemy — враги И мирная
// живность, она нужна тем же потребителям) и партия: uPlayer/uCmc/uNpc ищут в
// снимке мира подписчики шины (CombatIntel.cpp:191, WandRange.cpp:513-514).
// Безымянное «?» и «u?84» не остаются: KindIsEnemy их и так не принимает, то
// есть слот они занимали впустую.
static bool KindBelongsInActorTable(const char* kind)
{
    if (KindIsCreature(kind)) return true;
    if (!kind) return false;
    return !strcmp(kind, "uPlayer") || !strcmp(kind, "uCmc")
        || !strcmp(kind, "uNpc");
}

// ── 85.64: МИРНАЯ ЖИВНОСТЬ — ЯВНЫЙ СПИСОК ───────────────────────────────────
//
// Было два захардкоженных имени (лагерная мелочь и заяц), и остальная живность
// шла по разряду ВРАГОВ: перепись видов (docs/SPECIES_CENSUS.md) показала, что
// олень, лань, змея, мышь/ворона и кабан считались угрозой в тактических
// счётчиках, в дистанциях рывка пешек, в агрессии и в допуске пачек.
//
// Теперь список явный, десять видов (номера и имена — из наших словарей).
// 85.65: сюда же переехали олень/лань/вол (em8200/em8201/em8300). Раньше они
// числились «частями дракона», но числа карточек говорят другое: HP 1100/1000/
// 2200 (rst) при карточках 180/55/1/20 и 300/50/10/50 — это обычная живность,
// а не деталь чужой модели (см. docs/SPECIES_HUNT.md). Карточки у остальных
// тоже настоящие, просто крошечные: у неопознанной мелочи 1/1/1/1, у крысы
// 180/40/1/10, у кабана 120/20/1/10.
//
// ВАЖНО: uEm8000 — те самые «лагерные зайцы» из дампов, их шестеро вокруг стоянки,
// и они прибавляли +6 к счётчику врагов на пустом месте. И это НЕ Григори
// (см. FIELD_MAP: «не маппить 0x61 -> Hare, сломаем Григори») — отдельный вид.
//
// Сравнение по префиксу с проверкой границы: в поле 19.08 в одной стае жили
// uEm0100, uEm0100_0 и uEm0100_3 — варианты того же вида.
static const char* const kHarmlessKinds[] = {
    "uEm8000",   // лагерная мелочь (Camp Critter)
    "uEm8200",   // олень (Deer / Stag; 85.65: было в «частях»)
    "uEm8201",   // лань (Doe; 85.65: было в «частях»)
    "uEm8300",   // вол (Ox; 85.65: было в «частях»)
    "uEm8500",   // мелочь, вид не опознан (карточка 1/1/1/1)
    "uEm8501",   // крыса (Large Rat)
    "uEm8600",   // летающая мелочь, вид не опознан (действия Fly*)
    "uEm8601",   // мелочь, вид не опознан
    "uEm8602",   // летающая мелочь, вид не опознан (карточки в архиве нет)
    "uEm8700",   // кабан (Wild Boar)
};

// Вид, у которого имя совпадает с шаблоном или продолжается подчёркиванием
// (uEm8500, uEm8500_00) — то есть варианты того же вида.
static bool KindHasPrefix(const char* kind, const char* base)
{
    const size_t n = strlen(base);
    if (strncmp(kind, base, n) != 0) return false;
    return kind[n] == 0 || kind[n] == '_';
}

bool KindIsHarmless(const char* kind)
{
    if (!kind) return false;
    for (int i = 0; i < (int)(sizeof(kHarmlessKinds) / sizeof(kHarmlessKinds[0])); ++i)
        if (KindHasPrefix(kind, kHarmlessKinds[i])) return true;
    return false;
}

// ── 85.64: ЧАСТИ СОСТАВНЫХ ВРАГОВ И ПРЕДМЕТЫ ОКРУЖЕНИЯ ───────────────────────
//
// Это не самостоятельные враги: повешенное на потолке (em8100) и «грудь
// Даймона» (em7002). Дракон и Даймон — отдельные виды (em5800…, em7000/7001),
// и по ним всё работает как работало.
// Считать деталь врагом — значит и в счётчике угрозы, и в дистанциях пешек
// учитывать то, что пешка всё равно не атакует.
//
// 85.65: список СТАЛ КОРОЧЕ. em8200/em8201/em8300 ушли в живность — они олень,
// лань и вол, а не части дракона (так думалось раньше по ярлыку в словаре).
// Остались два: em8100 (1 HP, висит у потолка, ActHoverCeilingMove/HoverNearAtk —
// на самостоятельное тело не похоже, вид не опознан) и em7002 (голова на груди
// Даймона, действия ActRimAttack/ModelExpression — элемент составного босса).
static const char* const kStructuralKinds[] = {
    "uEm8100",   // висит у потолка, 1 HP, вид не опознан (не самостоятельный враг)
    "uEm7002",   // Даймон: драконья голова на груди (форма 2)
};

bool KindIsStructural(const char* kind)
{
    if (!kind) return false;
    for (int i = 0; i < (int)(sizeof(kStructuralKinds) / sizeof(kStructuralKinds[0])); ++i)
        if (KindHasPrefix(kind, kStructuralKinds[i])) return true;
    return false;
}

// Враг: существо, представляющее угрозу.
//
// ВАЖНО: враги бывают не только uEm*. Бандиты и солдаты — это
// uHumanEnemy (29696 B), ветка uNpc -> uHumanEnemy. Пока фильтр смотрел
// только на "uEm", люди были невидимы и для счётчика, и для мутаций.
bool KindIsEnemy(const char* kind)
{
    if (!KindIsCreature(kind)) return false;
    if (KindIsHarmless(kind)) return false;    // живность: не угроза (85.64)
    if (KindIsStructural(kind)) return false;  // деталь составного врага (85.64)
    return true;
}

int KindCategory(const char* kind)
{
    // Tactical category from LIVE kind, not gid. 0x61 must never become boss.
    if (!kind) return -1;
    // ВАРИАНТЫ ВИДА. Скан 19.08 показал в одной стае три разных класса:
    // uEm0100, uEm0100_0, uEm0100_3. Точное сравнение имени пропускало
    // две трети стаи, и категория для пешек (TacticalSwitch) считалась
    // по неполному составу. Сравниваем по префиксу.
    if (!strncmp(kind, "uEm0100", 7) || !strncmp(kind, "uEm0101", 7)) return 0;
    return -1;
}

void PublishWorldFromActors()
{
    if (g_nAct && g_act[0].ptr)
        g_lastBand = g_act[0].ptr & ~0xFFFFFu;
    WorldReport w{};
    w.timestampMs = MsNow();
    w.dominantCategory = -1;
    int best = -1;
    // 86.09: ПРЕДЕЛ — ЁМКОСТЬ ПРИЁМНИКА, А НЕ РАЗМЕР СПИСКА АКТЁРОВ.
    //
    // В 86.08 я поднял эту границу на kMaxAct (80) вместе с остальными
    // тридцать-двумя — и это был краш игры. `w.units` объявлен как
    // `WorldPresence units[32]` в CombatBus.h, то есть запись
    // `w.units[w.count]` при w.count = 32..79 лила ЗА КОНЕЦ структуры
    // WorldReport (WorldPresence ~80 байт, перелив до ~3.8 КБ).
    //
    // Проявилось ровно так, как должно было: на первом бою всё нормально
    // (актёров меньше 32), краш — когда подошёл ко второй точке спавна и
    // список перевалил за 32.
    //
    // Ёмкость берём из самого массива через sizeof, а не литералом: тогда
    // граница не может разойтись с приёмником ни при каком kMaxAct.
    const int kUnitsCap = (int)(sizeof(w.units) / sizeof(w.units[0]));
    for (int i = 0; i < g_nAct && w.count < kUnitsCap; ++i) {
        if (!g_act[i].ptr) continue;
        // Труп — не участник боя. Без этого счётчик в PawnAI показывает
        // "1 враг" над свежим трупом, пока движок не выгрузит тело.
        // Одной чистки EnemyCount() в DevTools мало: PawnAI берёт числа
        // отсюда, через шину CombatBus, — это вторая дорога для тех же данных.
        if (g_act[i].isDead) { w.deadCount++; continue; }
        WorldPresence& p = w.units[w.count];
        p.ptr = g_act[i].ptr;
        p.vt = (uint32_t)g_act[i].vt;
        p.gid = g_act[i].gid;
        p.kind = g_act[i].kind ? g_act[i].kind : "?";
        p.x = g_act[i].x;
        p.y = g_act[i].y;
        p.z = g_act[i].z;
        p.fromScan = true;
        // Build 62: боевое действие врага (по live Act, не по урону).
        p.inCombatAction = KindIsEnemy(g_act[i].kind)
            && EnemyActNameIsCombat(g_act[i].liveAct);
        lstrcpynA(p.actName, g_act[i].liveAct[0] ? g_act[i].liveAct : "",
                  sizeof(p.actName));
        if (KindIsEnemy(g_act[i].kind)) {
            w.enemyCount++;
            if (p.inCombatAction) w.enemyCombatCount++;
        }
        else if (KindIsHarmless(g_act[i].kind)) w.critterCount++;
        if (g_act[i].kind && (!strncmp(g_act[i].kind, "uEm0100", 7)
            || !strncmp(g_act[i].kind, "uEm0101", 7)))
            w.goblinCount++;   // вместе с вариантами uEm0100_0 / uEm0100_3
        int cat = KindCategory(g_act[i].kind);
        if (cat > best) best = cat;
        w.count++;
    }
    w.dominantCategory = best;
    // Build 62: пешка выбрала боевую цель (читается в PartyReadPositions).
    w.pawnEngaged = (g_pawnCombatTarget != 0);
    CombatBus::Instance().PublishWorld(w);
}

// Живое состояние существа: имя класса текущего Act, прочитанное у игры.
//
// ЗАЧЕМ ОТДЕЛЬНАЯ ФУНКЦИЯ, А НЕ ActMap: таблица ActMap.Generated.h содержит
// factory vtable, у живого объекта instance vtable, единого сдвига нет
// (гоблин 0x1B1CC, заяц 0x1B198). Сравнение всегда даёт промах, поэтому
// в старых дампах у всех actName = "-". Имя берём через DTI — тем же
// способом, каким опознаём uEm0100.
//
// Возвращает true, если имя прочитано.
// Указатель на объект текущего действия. Раньше его читал только
// ReadLiveAct и тут же выбрасывал, оставляя имя. Для разбора таймингов
// нужен сам объект: у действий вроде cEmActWalk всего ~116 байт, и это
// перебираемо, в отличие от 29 КБ тела.
bool ReadSafe(uintptr_t addr, void* out, uint32_t bytes)
{
    if (!addr || !out || !bytes) return false;
    return Rd((void*)addr, out, bytes);
}

bool ReadPtrSafe(uintptr_t addr, uintptr_t* out)
{
    if (!addr || !out) return false;
    return RdPtr((void*)addr, out);
}

uintptr_t FindChildByClass(uintptr_t body, uint32_t bodyBytes,
                           const char* className, uint32_t* offOut)
{
    if (offOut) *offOut = 0;
    if (!body || !className || !className[0]) return 0;
    if (bodyBytes < 8 || bodyBytes > 0x20000) return 0;

    for (uint32_t off = 0; off + 4 <= bodyBytes; off += 4) {
        uintptr_t cand = 0;
        if (!RdPtr((void*)(body + off), &cand)) continue;
        if (!LooksHeap(cand) || cand == body) continue;
        char nm[48] = {};
        if (!NameOfLiveObject(cand, nm, sizeof(nm)) || !nm[0]) continue;
        if (strcmp(nm, className) != 0) continue;
        if (offOut) *offOut = off;
        return cand;
    }
    return 0;
}

// --- Планировщик пешки: код приоритета и имя цели (только чтение) -----------
//
// Разрешение — ПО ИМЕНИ КЛАССА. Смещение cAICtrl +0x2E64 у тел партии
// наблюдалось не раз, но слоты тела переиспользуются между состояниями
// (FIX_RULES §6: планировщик уже «пропадал» с зашитого cAICtrl+0x68),
// поэтому известное смещение — только подсказка, а решает имя.
//
// Кэш на одно тело: полный перебор двух объектов по именам стоит дорого,
// а зовут это каждые 150 мс. Смена тела или выгрузка мира кэш сбрасывают.
// TypeAtlas uCmc = 22752. Не путать с uPlayer (kPartyBodySize = 0x5A10):
// обход «с запасом» читает 304 B за хвостом и может подцепить чужой heap.
static const uint32_t kPawnBodyBytes  = kCmcBodySize;
static const uint32_t kAICtrlBytes    = 704;      // cAICtrl
static const uint32_t kGoalSlot0      = 0x08;     // массив загруженных целей
static const int32_t  kMaxGoalCode    = 91;       // коды 0..90

// Кэш на три тела (своя + две наёмных). Раньше он был на одно: пока мы
// смотрели только за главной пешкой, этого хватало. Доктрины теперь работают
// с каждой пешкой, и телеметрия обязана уметь показать, КАКУЮ цель выбрал
// планировщик именно файтера, а не главной.
struct PlannerCache {
    uintptr_t body;
    uintptr_t ptr;
    DWORD     tryMs;
};
static PlannerCache g_plannerCache[3] = {};

// Гасим все три записи сразу. Вызов — при выходе из мира: тела пешек
// освобождены, держать указатели нельзя, иначе чтение пойдёт по чужой памяти.
static void ResetPawnPlannerCache()
{
    for (int i = 0; i < 3; ++i) memset(&g_plannerCache[i], 0, sizeof(g_plannerCache[i]));
}

static uintptr_t ResolvePawnPlannerFor(uintptr_t body)
{
    if (!InWorld() || !body) {
        // Вышли из мира (титры, загрузка, смена зоны) — все три записи больше
        // недействительны.
        ResetPawnPlannerCache();
        return 0;
    }

    const DWORD now = MsNow();

    PlannerCache* c = 0;
    for (int i = 0; i < 3; ++i)
        if (g_plannerCache[i].body == body) { c = &g_plannerCache[i]; break; }
    if (!c) {
        // Вытесняем запись, которую не трогали дольше всех.
        c = &g_plannerCache[0];
        for (int i = 1; i < 3; ++i)
            if (g_plannerCache[i].tryMs < c->tryMs) c = &g_plannerCache[i];
        memset(c, 0, sizeof(*c));
        c->body = body;
    }

    if (c->ptr) return c->ptr;
    // Не искать чаще раза в две секунды: перебор по именам классов —
    // дорогая работа, а тик пешек частый.
    if (c->tryMs && now - c->tryMs < 2000) return 0;
    c->tryMs = now;

    // Шаг 1: cAICtrl. Сначала по известному смещению, но С ПРОВЕРКОЙ ИМЕНИ.
    uintptr_t ctrl = 0;
    if (RdPtr((void*)(body + 0x2E64), &ctrl) && LooksHeap(ctrl)) {
        char nm[48] = {};
        if (!NameOfLiveObject(ctrl, nm, sizeof(nm)) || strcmp(nm, "cAICtrl") != 0)
            ctrl = 0;
    } else {
        ctrl = 0;
    }
    if (!ctrl) ctrl = FindChildByClass(body, kPawnBodyBytes, "cAICtrl", 0);
    if (!ctrl) return 0;

    // Шаг 2: планировщик внутри cAICtrl — тоже по имени.
    c->ptr = FindChildByClass(ctrl, kAICtrlBytes, "cAIGoalPlanning", 0);
    return c->ptr;
}

static uintptr_t ResolvePawnPlanner()
{
    return ResolvePawnPlannerFor(MainPawnBody());
}

bool PawnPriorityCodeFor(uintptr_t body, int32_t* codeOut)
{
    if (codeOut) *codeOut = -1;
    if (!body) return false;
    const uintptr_t planner = ResolvePawnPlannerFor(body);
    if (!planner) return false;
    int32_t code = -1;
    if (!Rd((void*)(planner + 0x17C), &code, 4)) return false;
    if (codeOut) *codeOut = code;
    return true;
}

bool PawnPriorityCode(int32_t* codeOut)
{
    return PawnPriorityCodeFor(MainPawnBody(), codeOut);
}

bool PawnGoalNameFor(uintptr_t body, int32_t code, char* out, int cap)
{
    if (!out || cap < 2) return false;
    out[0] = 0;
    if (code < 0 || code >= kMaxGoalCode) return false;
    if (!body) return false;
    const uintptr_t planner = ResolvePawnPlannerFor(body);
    if (!planner) return false;

    uintptr_t res = 0;
    if (!RdPtr((void*)(planner + kGoalSlot0 + (uint32_t)code * 4), &res)) return false;
    if (!LooksHeap(res)) return false;
    char nm[48] = {};
    if (!NameOfLiveObject(res, nm, sizeof(nm)) || strcmp(nm, "rAIGoalPlanning") != 0)
        return false;

    // У ресурса по +0x08 лежит путь строкой (подтверждено на rPlStamina и
    // на всех 69 целях планировщика).
    char path[96] = {};
    if (!Rd((void*)(res + 0x08), path, sizeof(path) - 1)) return false;
    int n = 0;
    for (; n < (int)sizeof(path) - 1 && path[n]; ++n) {
        const unsigned char c = (unsigned char)path[n];
        if (c < 0x20 || c > 0x7E) break;
    }
    path[n] = 0;
    if (n <= 3) return false;

    const char* tail = path;
    for (const char* p = path; *p; ++p) if (*p == '\\' || *p == '/') tail = p + 1;
    lstrcpynA(out, tail, cap);
    return out[0] != 0;
}

bool PawnGoalName(int32_t code, char* out, int cap)
{
    return PawnGoalNameFor(MainPawnBody(), code, out, cap);
}

// Моторные интерфейсы внутри живого блока плана.
//
// PlanCtrl(code) = planner + 0x190 + code*0x110 (SOURCE_OF_TRUTH §4.0).
//
// ПЕРВАЯ ВЕРСИЯ ВЕРНУЛА «(none found)» И БЫЛА ПРАВА ПО-СВОЕМУ.
// Она смотрела на глубину 1: сам указатель и то, на что он указывает.
// А документированная цепочка длиннее (SOURCE_OF_TRUTH §5.1):
//
//     PlanCtrl -> узел плана -> +0x04 -> ActionInterfaceParam -> +0x08 -> cCmc*
//
// То есть до моторной команды три разыменования, а не одно. Ответ
// «не нашёл» при поиске на неверной глубине — ровно тот сорт вранья, за
// которым мы охотимся весь трек, поэтому теперь функция не только ищет
// глубже, но и ДОКЛАДЫВАЕТ, где именно искала: в строку дописывается
// «(depth 3, N nodes)». Предел инструмента виден в его выводе.
//
// Обход ограничен: 64 узла, по 0x40 байт на узел, глубина 3. Это дёшево
// и вызывается по событию, а не каждый кадр.
bool PawnPlanInterfaces(int32_t code, char* out, int cap)
{
    if (!out || cap < 2) return false;
    out[0] = 0;
    if (code < 0 || code >= kMaxGoalCode) return false;
    const uintptr_t planner = ResolvePawnPlanner();
    if (!planner) return false;

    const uintptr_t block = planner + 0x190 + (uint32_t)code * 0x110;
    if (!RegionOk(block, 0x110)) return false;

    struct QNode { uintptr_t addr; uint32_t bytes; int depth; };
    QNode q[64];
    int nq = 0, head = 0;
    q[nq].addr = block; q[nq].bytes = 0x110; q[nq].depth = 0; ++nq;

    char seen[6][40];
    int found = 0, written = 0, visited = 0;
    memset(seen, 0, sizeof(seen));

    while (head < nq && found < 6) {
        const QNode cur = q[head++];
        ++visited;
        if (!RegionOk(cur.addr, cur.bytes)) continue;

        for (uint32_t off = 0; off + 4 <= cur.bytes; off += 4) {
            uintptr_t p = 0;
            if (!RdPtr((void*)(cur.addr + off), &p)) continue;
            if (!LooksHeap(p) || p == cur.addr) continue;

            char nm[48] = {};
            const bool named = NameOfLiveObject(p, nm, sizeof(nm)) && nm[0];

            if (named && (strncmp(nm, "cCmc", 4) == 0 || strstr(nm, "ActionInterface"))) {
                bool dup = false;
                for (int i = 0; i < found; ++i)
                    if (!strcmp(seen[i], nm)) { dup = true; break; }
                if (dup) continue;
                lstrcpynA(seen[found], nm, sizeof(seen[found]));
                ++found;
                if (written && written < cap - 2) { out[written++] = ','; out[written++] = ' '; }
                for (const char* z = nm; *z && written < cap - 1; ++z) out[written++] = *z;
                out[written] = 0;
                if (found >= 6) break;
                continue;   // нашли команду — глубже по этой ветке не идём
            }

            // Не команда — кандидат на промежуточный узел цепочки.
            // Живые тела и списки актёров в очередь не берём: там чужая
            // подсистема, и мы уже обжигались на соседях по списку.
            if (named && (strncmp(nm, "uPl", 3) == 0 || strncmp(nm, "uCmc", 4) == 0
                       || strncmp(nm, "uEm", 3) == 0 || strncmp(nm, "uNpc", 4) == 0))
                continue;
            if (cur.depth >= 3 || nq >= 64) continue;
            q[nq].addr = p; q[nq].bytes = 0x40; q[nq].depth = cur.depth + 1; ++nq;
        }
    }

    // Предел поиска — частью ответа, а не примечанием в чужой голове.
    char tail[48];
    wsprintfA(tail, "%s(depth 3, %d nodes)", found ? "  " : "", visited);
    for (const char* z = tail; *z && written < cap - 1; ++z) out[written++] = *z;
    out[written] = 0;
    return found > 0;
}

// --- ЖИВЫЕ СКЛОННОСТИ ИЗ ТЕЛА (cCmcInfo) ------------------------------------
//
// ПОВОД. Тестер выставил наёмному Файтеру Scather 1000 и всё остальное в
// ноль нашими ползунками, отвоевал бой — и увидел в игровом профиле
// пешки прежнее: Guardian primary, Utilitarian secondary. То есть запись
// в запись персонажа игру НЕ УБЕДИЛА.
//
// А в SOURCE_OF_TRUTH §6 уже записан второй адрес тех же девяти чисел:
// `cCmcInfo + 0x14B8 + id*0x0C` — тройка `{state, id, value}` на каждую
// склонность, прямо в живом теле. Для главной пешки оба места сходятся
// (её запись — часть сохранения), а для наёмных, похоже, работает именно
// тело.
//
// Пока не доказано, какое из мест игра действительно читает, продуктовый
// код не пишет НИ В ОДНО из них у наёмных: сначала читаем оба и
// сравниваем.
bool PawnInclinationsLive(uintptr_t body, float* out9)
{
    if (!body || !out9) return false;
    for (int i = 0; i < 9; ++i) out9[i] = -1.0f;

    const uintptr_t info = FindChildByClass(body, 0x58E0, "cCmcInfo", 0);
    if (!info) return false;

    int found = 0;
    for (int k = 0; k < 9; ++k) {
        const uintptr_t t = info + 0x14B8 + (uintptr_t)k * 0x0C;
        int32_t state = 0, id = 0;
        float value = 0.0f;
        if (!Rd((void*)(t + 0x00), &state, 4)) continue;
        if (!Rd((void*)(t + 0x04), &id, 4)) continue;
        if (!Rd((void*)(t + 0x08), &value, 4)) continue;
        if (id < 0 || id > 9) continue;
        if (!(value >= 0.0f && value <= 1000.0f)) continue;
        if (id < 9) { out9[id] = value; ++found; }
    }
    return found >= 5;   // половина и больше — раскладка та
}

// Записать склонность в ЖИВОЕ ТЕЛО. Пара к PawnInclinationsLive().
//
// Запись персонажа мы пишем так же, как это делает рабочий мод
// `ddda-dinput8` (тот же адрес `0xA7000 + offset + 0x96C + 0x1224`), но
// профиль наёмной пешки после этого показывал прежнее. Второй адрес тех
// же чисел живёт в теле, и если движок читает поведение оттуда, писать
// надо в оба места. Что именно окажется авторитетным — покажет игра.
bool PawnSetInclinationLive(uintptr_t body, int id, float value)
{
    if (!body || id < 0 || id > 8) return false;
    if (!(value >= 0.0f && value <= 1000.0f)) return false;
    const uintptr_t info = FindChildByClass(body, 0x58E0, "cCmcInfo", 0);
    if (!info) return false;

    for (int k = 0; k < 9; ++k) {
        const uintptr_t t = info + 0x14B8 + (uintptr_t)k * 0x0C;
        int32_t slotId = 0;
        if (!Rd((void*)(t + 0x04), &slotId, 4)) continue;
        if (slotId != id) continue;
        if (!WrSafe((void*)(t + 0x08), &value, 4)) return false;
        float back = -1.0f;
        return Rd((void*)(t + 0x08), &back, 4) && back == value;
    }
    return false;
}

uintptr_t ActObjectOf(uintptr_t body)
{
    if (!body) return 0;
    uintptr_t act = 0;
    if (!RdPtr((void*)(body + kActSlot), &act)) return 0;
    return LooksHeap(act) ? act : 0;
}

bool ReadLiveAct(uintptr_t body, char* out, int cap)
{
    if (!out || cap < 2) return false;
    out[0] = 0;
    if (!body) return false;

    // +0x2DC8 — текущее действие. Подтверждено дампами 14.08.
    uintptr_t act = 0;
    if (!RdPtr((void*)(body + kActSlot), &act)) return false;
    if (!LooksHeap(act)) return false;

    return NameOfLiveObjectSafe((const void*)act, out, cap) != nullptr;
}

// Смерть определяется СОСТОЯНИЕМ, а не флагом.
//
// Флага смерти в теле мы не нашли: гипотеза "+0x14 == 0x12" опровергнута —
// это же значение стоит на живых (дампы 19-22). См. docs/FIELD_MAP.md,
// раздел "Не фильтровать World по +14 / +4C / +FC".
//
// Зато у Capcom смерть — это штатное состояние FSM:
//     cEm0100ActDie        — умирает
//     cEm0100ActDeadBody   — труп
//     cEm0100ActDieBurn / cEm0100ActDieIce — частные случаи
// Проверка по подстроке "Die"/"Dead" покрывает виды сразу: текущий ActMap
// содержит 873 состояния в 36 emId-группах.
// ВНИМАНИЕ на форму имени. Первая версия проверяла только префикс сразу
// после "Act" — и пропускала 6 состояний исходной 812-row карты, где Die стоит в середине:
//     cEm5000ActDownDie      cEm8600ActFlyDie
//     cEm9100ActGroundDie    cEm0100ActDmgPoisonDie
// Поэтому ищем "Die"/"Dead" где угодно в имени состояния.
//
// Ложных срабатываний нет: слов с этими буквосочетаниями, кроме смерти,
// среди текущих 872 состояний не встречается (проверено перебором таблицы).
// "Dive"/"Damage"/"Down" не совпадают — у них другие буквы.
bool ActNameIsDeath(const char* actName)
{
    if (!actName || !actName[0]) return false;
    // Отрезаем префикс класса: интересует только часть после "Act".
    const char* p = strstr(actName, "Act");
    const char* s = p ? p + 3 : actName;
    return strstr(s, "Die") != nullptr || strstr(s, "Dead") != nullptr;
}

// Build 62 — враг в боевом действии? По DTI-имени live Act (не по урону).
// Консервативно: только однозначно боевые состояния. Локомоция (Walk/Run)
// НЕ считается боем — это может быть патруль, а ложный «бой» хуже пропуска
// (пропуск ловится другими сигналами: урон и цель пешки).
bool EnemyActNameIsCombat(const char* actName)
{
    if (!actName || !actName[0]) return false;
    if (ActNameIsDeath(actName)) return false; // смерть — не бой
    static const char* kCombat[] = {
        "Atk",      // Atck*/атаки (покрывает и "Atck")
        "Dmg",      // получает урон
        "Guard",    // блокирует
        "Eva",      // уклоняется
        "Dash",     // боевой рывок
        "Charge",   // заряд/разгон атаки
        "Howl",     // агро-вой
        "Provoke",  // провокация
        "Roar",     // рёв
        "Escape",   // бегство из боя (контекст боя)
        "Bite",     // укус
        "Grab",     // захват
        "Stomp",    // топот
        "Tail",     // хвост (атака)
        "Breath",   // дыхание (дракон)
        "Fire",     // огонь
        "Shot",     // выстрел
        "Swing",    // замах
    };
    for (size_t i = 0; i < sizeof(kCombat) / sizeof(kCombat[0]); ++i)
        if (strstr(actName, kCombat[i])) return true;
    return false;
}

const ActMap::Act* ActAt(uintptr_t body, uint32_t off, uintptr_t* outPtr, uint32_t* outRva)
{
    uintptr_t cand = 0;
    if (!RdPtr((void*)(body + off), &cand)) return nullptr;
    if (!LooksHeap(cand)) return nullptr;
    uintptr_t vt = 0;
    if (!RdPtr((void*)cand, &vt)) return nullptr;
    if (!InImage(vt)) return nullptr;
    uint32_t rva = (uint32_t)(vt - g_base);
    const ActMap::Act* a = ActMap::FindByVt(rva);
    if (!a) return nullptr;
    if (outPtr) *outPtr = cand;
    if (outRva) *outRva = rva;
    return a;
}

void ScanActSlot(ActorDump& A)
{
    A.actOff = 0; A.actPtr = 0; A.actVtRva = 0;
    A.actName = 0; A.actCat = 0; A.actHits = 0;
    A.actOff2 = 0; A.actName2 = 0;
    A.nRaw = 0;

    // Живое имя состояния и признак смерти (билд 29).
    A.liveAct[0] = 0;
    A.isDead     = false;
    if (ReadLiveAct(A.ptr, A.liveAct, sizeof(A.liveAct)))
        A.isDead = ActNameIsDeath(A.liveAct);
    if (!g_base) return;

    // Build 69.5 — ГЛАВНАЯ ЦЕНА СКАНА БЫЛА ЗДЕСЬ.
    //
    // Ниже идёт полный поиск слота действия: копия 29 КБ тела и 7400 итераций
    // с защищёнными чтениями на каждого кандидата. Задумано это как операция
    // HUNT (см. комментарий в DevTools.cpp), но условие было написано так,
    // что полный поиск запускался ВСЕГДА, пока смещение неизвестно. А у
    // обычного игрока оно неизвестно всегда: HUNT никто не нажимает.
    // Результат: каждый тик, на каждого актёра — исследовательский обход
    // всего тела. Это и были те самые ~3 мс при трёх актёрах.
    //
    // Продукту полный поиск не нужен вовсе: детектор боя работает по
    // A.liveAct, прочитанному выше через DTI. Поля actName/actCat/nRaw
    // читают только дампы DevTools.
    if (!g_actFullScan) {
        if (g_actSlotOff) {
            uintptr_t ptr = 0; uint32_t rva = 0;
            if (const ActMap::Act* a = ActAt(A.ptr, g_actSlotOff, &ptr, &rva)) {
                A.actOff = g_actSlotOff; A.actPtr = ptr; A.actVtRva = rva;
                A.actName = a->name; A.actCat = a->category; A.actHits = 1;
            }
        }
        return;   // смещение неизвестно — просто не ищем, а не сканируем тело
    }

    // Full search. Copy the body first: 8 guarded reads instead of thousands.
    static BYTE  buf[0x7400];
    static bool  ok[0x7400 / 0x1000 + 1];
    const uint32_t kEnd = 0x7400, kChunk = 0x1000;
    for (uint32_t c = 0, off = 0; off < kEnd; ++c, off += kChunk) {
        uint32_t n = (off + kChunk <= kEnd) ? kChunk : (kEnd - off);
        ok[c] = Rd((void*)(A.ptr + off), buf + off, n);
    }

    for (uint32_t off = 0x100; off + 4 <= kEnd; off += 4) {
        if (!ok[off / kChunk]) continue;
        uintptr_t cand = *(uintptr_t*)(buf + off);
        if (!LooksHeap(cand)) continue;
        uintptr_t vt = 0;
        if (!RdPtr((void*)cand, &vt) || !InImage(vt)) continue;
        uint32_t rva = (uint32_t)(vt - g_base);

        // Zip 33: harvest every real vtable-bearing object, unfiltered.
        // LooksLikeVtable = lives in .rdata and its first two slots point
        // into .text — that is a genuine C++ object, Act or not.
        if (A.nRaw < 40 && LooksLikeVtable(vt)) {
            int dup = 0;
            for (int r = 0; r < A.nRaw; ++r)
                if (A.rawVt[r] == (uint32_t)vt) { dup = 1; break; }
            if (!dup) {
                A.rawOff[A.nRaw] = off;
                A.rawVt[A.nRaw]  = (uint32_t)vt;
                A.rawPtr[A.nRaw] = (uint32_t)cand;
                // Zip 34: ask the object its own name. Atlas not involved.
                if (!NameOfLiveObject(cand, A.rawName[A.nRaw], 40))
                    A.rawName[A.nRaw][0] = 0;
                A.nRaw++;
            }
        }

        const ActMap::Act* a = ActMap::FindByVt(rva);
        if (!a) continue;

        A.actHits++;
        if (!A.actPtr) {
            A.actOff = off; A.actPtr = cand; A.actVtRva = rva;
            A.actName = a->name; A.actCat = a->category;
        } else if (!A.actOff2) {
            A.actOff2 = off; A.actName2 = a->name;
        }
    }
    if (A.actOff) g_actSlotOff = A.actOff;   // remember for the cheap path
}

// 86.08: переполнение таблицы актёров сообщается ОДИН раз за сессию, а не на
// каждое отброшенное тело — при 64 врагах это были бы сотни строк за бой.
static bool s_actOverflowLogged = false;

// 86.10: ЧЕМ ЗАНЯТА ТАБЛИЦА АКТЁРОВ.
//
// Поле 86.09: «actor table FULL at 80» сработало в ВАНИЛЬНОЙ сессии, где врагов
// было 23 (Tempo: rank summary total=23) плюс партия из четырёх. Значит 80
// слотов занимают не только враги, и поднимать kMaxAct дальше вслепую нельзя:
// 86.08 показал, чем кончается поднятие границы без понимания того, что именно
// она ограничивает. Сначала состав — он виден из того же прохода бесплатно.
//
// Гистограмма живёт один проход (сбрасывается в DumpActorsFrom) и печатается
// вместе с первым переполнением. Счётчики сессии живут до выгрузки и уходят
// одной строкой в полевой пакет: по ним видно, был ли переполнением один такт
// на загрузке или таблица полна весь бой.
struct KindTally { char name[40]; int n; };
static const int kKindTallyCap = 32;
static KindTally s_tally[kKindTallyCap];
static int  s_nTally        = 0;
static int  s_tallyOther    = 0;   // 86.11: тела, чей вид не влез в гистограмму
static int  s_tallyDropped  = 0;   // отброшено в этом проходе
static int  s_actScanPasses = 0;   // сколько проходов сделано за сессию
static int  s_actFullPasses = 0;   // сколько из них уперлись в потолок
static int  s_actDroppedTotal = 0;
static int  s_actDroppedWorst = 0;
static int  s_nonActorSkipped = 0; // 86.12: кто не пущен в таблицу (не актёр)
static int  s_blindTicks      = 0; // 86.12: такты «партия известна, актёров нет»
static int  s_pollFinds       = 0; // 86.14: сколько раз поллинг дал новое семя

// 86.12: нужен ли обход на этом такте. Семена берутся из таблицы актёров И из
// тел партии (RewalkActors), поэтому пустая таблица при известной партии — не
// повод молчать. Отдельной функцией — чтобы условие можно было проверить
// фикстурой: именно его поломка ослепила сессию 86.12.
static bool RewalkNeeded() { return g_nAct > 0 || g_nParty > 0; }

// 86.14: ИЩЕМ, ПОКА НЕ НАШЛИ ХОТЬ ОДНО СУЩЕСТВО.
//
// Поле 86.13 (после починки обхода): passes=1858, blindTicks=0 — обход жив, но
// maxActors=4 (одна партия), nonActors=8 и directorWrites=0 при настоящей
// драке. Арифметика простая: горячее кольцо 0x10000000..0x18000000 — это
// 128 МБ, а лёгкий срез 256 КБ раз в 2 с покрывает его за 17 минут. Сессия
// шла 6.6 минуты, то есть поллинг не прошёл и половины кольца.
//
// Раньше это не мешало: тяжёлый режим (8 МБ/тик, кольцо за 2.4 с) выключался
// при появлении партии, но найденное за первые секунды тело обрастало
// декорациями, и обход жил на этом субстрате. Белый список 86.12 субстрат
// убрал — значит искать обязан поллинг, и останавливать его по принципу
// «таблица не пуста» больше нельзя: партия в таблице есть всегда.
static bool SearchingForActors()
{
    for (int i = 0; i < g_nAct; ++i)
        if (g_act[i].ptr && KindIsCreature(g_act[i].kind)) return false;
    return true;
}

static void TallyReset() { s_nTally = 0; s_tallyOther = 0; s_tallyDropped = 0; }

static void TallyAdd(const char* kind)
{
    if (!kind || !kind[0]) kind = "?";
    for (int i = 0; i < s_nTally; ++i)
        if (!strcmp(s_tally[i].name, kind)) { ++s_tally[i].n; return; }
    // 86.11: ячеек не хватило — тело всё равно посчитано. В поле 86.10
    // гистограмма на 16 ячеек молча потеряла хвост: в показанных восьми видах
    // было 105 тел, а в проходе 162 (80 в таблице + 82 отброшено). Состав
    // обязан сходиться с итогом, иначе по нему нельзя принимать решение.
    if (s_nTally >= kKindTallyCap) { ++s_tallyOther; return; }
    lstrcpynA(s_tally[s_nTally].name, kind, sizeof(s_tally[s_nTally].name));
    s_tally[s_nTally].n = 1;
    ++s_nTally;
}

// Состав прохода: виды по убыванию числа тел, не больше восьми. Сортировка
// выбором по копии порядка — ячеек 32, дороже некуда, и это строка на одно
// переполнение за сессию.
static void TallyComposition(char* out, int cap)
{
    if (!out || cap <= 0) return;
    out[0] = 0;
    int order[kKindTallyCap];
    int nOrd = 0;
    for (int i = 0; i < s_nTally && nOrd < kKindTallyCap; ++i) order[nOrd++] = i;
    for (int a = 0; a + 1 < nOrd; ++a) {
        int best = a;
        for (int b = a + 1; b < nOrd; ++b)
            if (s_tally[order[b]].n > s_tally[order[best]].n) best = b;
        if (best != a) { const int t = order[a]; order[a] = order[best]; order[best] = t; }
    }
    const int shown = nOrd < 8 ? nOrd : 8;
    int used = snprintf(out, (size_t)cap, "composition");
    for (int i = 0; i < shown && used < cap; ++i)
        used += snprintf(out + used, (size_t)(cap - used), " %s=%d",
                         s_tally[order[i]].name, s_tally[order[i]].n);
    if (nOrd > shown && used < cap)
        used += snprintf(out + used, (size_t)(cap - used), " +%d more kinds",
                         nOrd - shown);
    // 86.11: тела, которым не хватило ячейки гистограммы, — отдельным числом,
    // а не в никуда. Без этого состав расходился с итогом прохода.
    if (s_tallyOther > 0 && used < cap)
        used += snprintf(out + used, (size_t)(cap - used), " other=%d",
                         s_tallyOther);
    if (s_nTally >= kKindTallyCap && used < cap)
        used += snprintf(out + used, (size_t)(cap - used), " (tally full at %d)",
                         kKindTallyCap);
}

// 86.10: итог таблицы актёров — в полевой пакет (см. LogMemSession.h). Строка
// нужна и когда переполнений не было: по «passes» видно, что обход вообще шёл,
// а нули в «fullPasses/dropped*» тогда читаются как «хватало всем», а не как
// «мы ничего не мерили».
// 86.12: «nonActors» — сколько объектов не пущено в таблицу белым списком. По
// нему видно и работу фильтра, и его цену: ноль означал бы, что фильтр не
// встретил ни одного постороннего, а не что он сломался молча.
// «blindTicks» — такты, где партия была известна, а актёров не было вовсе.
// Ноль — норма; большое число значит, что мод не видел поле (86.12: 2257 из
// 2257, passes=4, директор не написал ничего за всю драку).
// «pollFinds» — сколько раз поллинг памяти дал новое семя. Это единственный
// независимый от таблицы источник открытий, поэтому ноль при живом бое значит,
// что поиск не работает (86.13: passes=1858, обход здоров, pollFinds=0,
// maxActors=4 — одна партия).
void ScanSessionSummary()
{
    // 86.12: строка нужна и при НУЛЕ проходов — именно тогда она и важнее всего
    // (слепая сессия). Раньше возвращались молча, и полная слепота читалась бы
    // только по отсутствию строки.
    if (!s_actScanPasses && !s_blindTicks) return;
    char l[220];
    snprintf(l, sizeof(l), "WorldScan: actor table summary cap=%d passes=%d"
             " fullPasses=%d droppedTotal=%d droppedWorst=%d nonActors=%d"
             " blindTicks=%d pollFinds=%d",
             (int)kMaxAct, s_actScanPasses, s_actFullPasses,
             s_actDroppedTotal, s_actDroppedWorst, s_nonActorSkipped,
             s_blindTicks, s_pollFinds);
    LogMem::SessionNote(l);
}

void DumpActorsFrom(uintptr_t* seed, int ns)
{
    g_nAct = 0;
    TallyReset();
    if (!seed || ns <= 0) return;
    ++s_actScanPasses;

    // Обход шире списка: Devilfire 84.24 — 32 слота заняли
    // uEmDragonBase::DragonAttackRange (44 B) и uEm5000_1; Дрейк uEm5900
    // в g_act не попал. Деталь не кладём, но next/prev обязаны остаться
    // в walk — иначе тело, видимое только как сосед компоненты, теряется.
    // 86.08: 192 вместо 96. Обход обязан быть ШИРЕ списка, а список вырос с
    // 32 до kMaxAct=80; прежние 96 дали бы запас в 16 ячеек вместо трёхкратного.
    uintptr_t walk[192];
    const int kWalkCap = (int)(sizeof(walk) / sizeof(walk[0]));
    int nw = 0;
    for (int i = 0; i < ns && nw < kWalkCap; ++i) {
        if (!seed[i] || !LooksHeap(seed[i])) continue;
        int d = 0;
        for (int k = 0; k < nw; ++k) if (walk[k] == seed[i]) { d = 1; break; }
        if (!d) walk[nw++] = seed[i];
    }

    for (int s = 0; s < nw; ++s) {
        uintptr_t p = walk[s];
        if (!p || !LooksHeap(p)) continue;
        int seen = 0;
        for (int k = 0; k < s; ++k) if (walk[k] == p) { seen = 1; break; }
        if (seen) continue;
        uintptr_t vt = 0;
        if (!RdPtr((void*)p, &vt) || !LooksLikeVtable(vt)) continue;

        uintptr_t next = 0, prev = 0;
        RdPtr((void*)(p + 0x0C), &next);
        RdPtr((void*)(p + 0x10), &prev);
        if (next && LooksHeap(next) && nw < kWalkCap) {
            int d = 0;
            for (int k = 0; k < nw; ++k) if (walk[k] == next) { d = 1; break; }
            if (!d) walk[nw++] = next;
        }
        if (prev && LooksHeap(prev) && nw < kWalkCap) {
            int d = 0;
            for (int k = 0; k < nw; ++k) if (walk[k] == prev) { d = 1; break; }
            if (!d) walk[nw++] = prev;
        }

        // Имя вида — у самой игры, через DTI.
        //
        // РАНЬШЕ здесь был список из пяти захардкоженных vtable, и всё,
        // чего в нём нет, получало kind="?" — то есть волки, бандиты и
        // огры не считались никем. Список констант не масштабируется:
        // видов в игре 35+, и каждый пришлось бы ловить вручную.
        //
        // DTI даёт настоящее имя класса любого существа сразу.
        // Известные константы оставлены как быстрый путь: для них имя
        // статическое, без чтения памяти.
        char kindBuf[40] = {};
        const char* kind = 0;
        if (vt == kGoblinInst)      kind = "uEm0100";
        else if (vt == kNpcInst)    kind = "uNpc";
        else if (vt == kEm8000Inst) kind = "uEm8000";
        else if (vt == kHareInst)   kind = "uEm8600";
        else if (NameOfLiveObject(p, kindBuf, sizeof(kindBuf)) && kindBuf[0])
            kind = kindBuf;
        else if (vt == kUnk84Inst)  kind = "u?84";
        else kind = "?";

        // uPlayer/uCmc/uNpc остаются в списке (усыновление партии).
        // Отсекаем только ложные uEm*: базы, вложенные ::X, детали _N.
        if (kind[0] == 'u' && kind[1] == 'E' && kind[2] == 'm'
            && !KindIsLiveEnemyBody(kind))
            continue;
        // 86.12: В ТАБЛИЦЕ — ТОЛЬКО АКТЁРЫ. Белый список вместо чёрного.
        //
        // В 86.11 я отсекал объекты карты по префиксу «uO». Поле 86.11:
        //   worldObjects=86660, droppedTotal 104494 -> 43745, droppedWorst 112 -> 40
        // и таблица ВСЁ РАВНО полна на 1143 проходах из 1214, потому что на место
        // uO* встал следующий слой декораций:
        //   composition uFmSwingBase=46 uStageSplitMdl=25 uEm0101=8 uSkyGrass=5
        //               uStageLowMdl=4 uCmc=3 uEm5000=1 uEm5900=1
        // Отсекать по имени — это игра в whack-a-mole с движком: семейство
        // декораций найдётся всегда. Поэтому правило перевёрнуто: слот занимают
        // только существа (uEm*, uHumanEnemy) и партия (uPlayer/uCmc/uNpc — они
        // нужны подписчикам шины, см. CombatIntel.cpp:191, WandRange.cpp:513).
        // Всё остальное, включая безымянное «?», не нужно ни одному продуктовому
        // потребителю: EnemyCount/EnemyBodyAt и KindIsEnemy(u.kind) их и так не
        // видели, а слоты и семена обхода они съедали.
        if (!KindBelongsInActorTable(kind)) { ++s_nonActorSkipped; continue; }
        TallyAdd(kind);   // 86.10: считаем и тех, кому слота не хватило
        if (g_nAct >= kMaxAct) {
            // 86.08: было молчаливым `continue`, и это ровно тот класс дефекта,
            // который дорого искать: часть врагов просто не существует для
            // директора и агро, а в логе ни строчки. Так мы в 86.04 искали
            // directorWrites=0. Одна строка за сессию, не на каждое тело.
            ++s_tallyDropped;
            continue;
        }

        ActorDump& A = g_act[g_nAct];
        memset(&A, 0, sizeof(A));
        A.ptr = p;
        A.vt = vt;
        A.next = next;
        A.prev = prev;
        BYTE gidb = 0;
        if (Rd((void*)(p + 0x2D), &gidb, 1)) A.gid = gidb;
        Rd((void*)(p + 0x40), &A.x, 4);
        Rd((void*)(p + 0x44), &A.y, 4);
        Rd((void*)(p + 0x48), &A.z, 4);
        if (A.vt == kGoblinInst)
            A.subOk = RdPtr((void*)(p + 0x6150), &A.subVt);
        BYTE probe = 0;
        A.fat29 = Rd((void*)(p + 0x73BF), &probe, 1);
        { BYTE st = 0; if (Rd((void*)(p + 0x14), &st, 1)) A.st14 = st; }
        A.win5bOk = Rd((void*)(p + 0x5BD0), A.win5b, 16);
        A.win60Ok = Rd((void*)(p + 0x6000), A.win60, 64);
        if (kind == kindBuf) {
            lstrcpynA(A.kindBuf, kindBuf, sizeof(A.kindBuf));
            A.kind = A.kindBuf;
        } else {
            A.kind = kind;
        }
        ScanActSlot(A);
        g_nAct++;
    }

    // 86.10: переполнение — в сессионные счётчики всегда, а строка с составом
    // одна на сессию. Состав печатаем ПОСЛЕ прохода, а не в момент отказа:
    // на середине обхода гистограмма была бы неполной и врала бы о причинах.
    if (s_tallyDropped > 0) {
        ++s_actFullPasses;
        s_actDroppedTotal += s_tallyDropped;
        if (s_tallyDropped > s_actDroppedWorst) s_actDroppedWorst = s_tallyDropped;
        if (!s_actOverflowLogged) {
            s_actOverflowLogged = true;
            char comp[400];
            TallyComposition(comp, sizeof(comp));
            logFile << "WorldScan: actor table FULL at " << kMaxAct
                    << " - dropped " << s_tallyDropped
                    << " actor(s) in this pass; while the table stays full they"
                       " are INVISIBLE to director/aggro (raise kMaxAct only if"
                       " this shows up without an enemy-cap mod). "
                    << comp << std::endl;
        }
    }
}

void RewalkActors()
{
    uintptr_t seed[kSeedCap];
    int ns = 0;
    for (int i = 0; i < g_nAct && ns < kSeedCap; ++i)
        if (g_act[i].ptr) seed[ns++] = g_act[i].ptr;

    // ТЕЛА ПАРТИИ КАК СЕМЕНА ОБХОДА.
    //
    // Живой тест темпа анимации показал задержку в несколько секунд:
    // подходишь к лагерю, гоблины уже дерутся — и только потом получают
    // свой множитель. Причина не в записи, а в ОБНАРУЖЕНИИ: пока в списке
    // нет ни одного актёра из этого связного списка, найти его может
    // только поллинг памяти, а он идёт порциями по 0.5-4 МБ за тик.
    //
    // Аризен и главная пешка известны всегда и лежат в том же списке
    // живых объектов. Добавляем их семенами: тогда новый лагерь виден
    // на первом же тике после загрузки, без ожидания поллинга.
    for (int i = 0; i < g_nParty && ns < kSeedCap; ++i) {
        const uintptr_t p = g_party[i].ptr;
        if (!p) continue;
        bool dup = false;
        for (int k = 0; k < ns; ++k) if (seed[k] == p) { dup = true; break; }
        if (!dup) seed[ns++] = p;
    }

    if (!ns) return;
    DumpActorsFrom(seed, ns);
    PublishWorldFromActors();
    Tempo::RefreshTable();   // список изменился — пересобрать множители
}

// Известные vtable — быстрый путь без чтения DTI.
int IsSeedVt(uint32_t val)
{
    return val == (uint32_t)kGoblinInst || val == (uint32_t)kEm8000Inst
        || val == (uint32_t)kNpcInst || val == (uint32_t)kUnk84Inst
        || val == (uint32_t)kHareInst;
}

// Тело существа ли это — по имени класса от самой игры.
//
// ЗАЧЕМ. Раньше поиск в куче принимал только пять захардкоженных vtable
// (гоблин, uEm8000, uNpc, u?84, Hare). Волк, бандит, огр — всё остальное
// не проходило фильтр и НИКОГДА не попадало в список акторов. Поэтому
// «волков система не определяет»: дело не в классификации, их просто
// не находили.
//
// Видов в игре 35+, ловить каждый константой нереально. Спрашиваем имя
// у DTI. 84.25: полное тело (uEmNNNN / uHumanEnemy), не база и не деталь.
//
// Порядок проверок важен для скорости: сначала дешёвые отсечения по
// памяти, только потом разбор vtable. Функция зовётся на каждом
// 8-байтовом слове горячей кучи.
bool LooksLikeCreatureAt(uintptr_t obj, uint32_t vt)
{
    if (!LooksLikeVtable((uintptr_t)vt)) return false;
    // У всех тел существ есть gid на +0x2D и координаты на +0x40.
    BYTE probe = 0;
    if (!Rd((void*)(obj + 0x2D), &probe, 1)) return false;
    float x = 0;
    if (!Rd((void*)(obj + 0x40), &x, 4)) return false;

    char nm[40];
    if (!NameOfLiveObject(obj, nm, sizeof(nm)) || !nm[0]) return false;
    return KindIsLiveEnemyBody(nm);
}

uintptr_t PollSeedSlice(uint32_t budget)
{
    // Hot ring only. dump18-23 actors are 0x10DD..0x114F. Walking to 0x40000000
    // skipped the classic band for ~30s (dump23 pack of 3).
    if (!g_nExec) InitSections();
    if (!budget) budget = 0x800000u;
    const uint32_t kBudget = budget;
    uint32_t used = 0;
    int steps = 0;
    if (g_pollAddr < kHotLo || g_pollAddr >= kHotHi)
        g_pollAddr = kHotLo;
    while (used < kBudget && steps < 64) {
        steps++;
        MEMORY_BASIC_INFORMATION mbi;
        memset(&mbi, 0, sizeof(mbi));
        SIZE_T got = VirtualQuery((LPCVOID)g_pollAddr, &mbi, sizeof(mbi));
        if (!got) { g_pollAddr = kHotLo; break; }
        uintptr_t base = (uintptr_t)mbi.BaseAddress;
        uintptr_t next = base + mbi.RegionSize;
        if (next <= g_pollAddr) { g_pollAddr = kHotLo; break; }
        DWORD prot = mbi.Protect & 0xFF;
        bool readable = prot == PAGE_READONLY || prot == PAGE_READWRITE
                     || prot == PAGE_WRITECOPY || prot == PAGE_EXECUTE_READ
                     || prot == PAGE_EXECUTE_READWRITE;
        bool skip = mbi.State != MEM_COMMIT || mbi.Type != MEM_PRIVATE
                 || !readable || (mbi.Protect & PAGE_GUARD)
                 || next <= kHotLo || base >= kHotHi
                 || (g_base && base < ImageEnd() && next > g_base);
        if (skip) { g_pollAddr = (next >= kHotHi) ? kHotLo : next; continue; }
        uintptr_t lo = g_pollAddr > base ? g_pollAddr : base;
        if (lo < kHotLo) lo = kHotLo;
        uintptr_t hi = next;
        if (hi > kHotHi) hi = kHotHi;
        if (hi <= lo) { g_pollAddr = (next >= kHotHi) ? kHotLo : next; continue; }
        uint32_t span = (uint32_t)(hi - lo);
        if (span > kBudget - used) span = kBudget - used;
        hi = lo + span;
        __try {
            // Build 69.4: кандидаты всегда выровнены на 8 байт, поэтому идём
            // шагом 8, а не проверяем выравнивание на каждом dword'е.
            // Тот же охват памяти, вдвое меньше итераций.
            uintptr_t first = (lo + 7u) & ~(uintptr_t)7u;
            for (uintptr_t obj = first; obj + 4 <= hi; obj += 8) {
                uint32_t val = *(const uint32_t*)obj;
                // Быстрый путь: известная vtable — берём без вопросов.
                // Медленный: спрашиваем DTI, но только если значение
                // вообще похоже на указатель в образ (иначе тратили бы
                // разбор vtable на каждое случайное число в куче).
                if (IsSeedVt(val)) {
                    BYTE probe = 0;
                    if (!Rd((void*)(obj + 0x2D), &probe, 1)) continue;
                } else {
                    if (!InImage((uintptr_t)val)) continue;
                    if (!LooksLikeCreatureAt(obj, val)) continue;
                }
                g_pollAddr = obj + 8;
                return obj;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        used += span;
        g_pollAddr = hi;
        if (g_pollAddr >= kHotHi) { g_pollAddr = kHotLo; break; }
        if (hi < next) break;
    }
    return 0;
}

// Считает ЖИВЫХ врагов. Трупы не в счёт: иначе "рядом 5 врагов" после
// выигранного боя, и любая логика "оценить опасность" врёт.
int EnemyCount()
{
    int n = 0;
    for (int i = 0; i < g_nAct; ++i) {
        if (!g_act[i].ptr) continue;
        if (g_act[i].isDead) continue;
        const char* k = g_act[i].kind;
        if (KindIsEnemy(k)) ++n;
    }
    return n;
}

// Перебор врагов по индексу. Нужен, потому что список разнороден:
// в дампах 6x uEm8000 (лагерные, gid 0x61) + 1x uEm0100 (гоблин).
// Кто пишет параметры вида — обязан идти по списку и смотреть kind.
// ВАЖНО: перебор отдаёт только ЖИВЫХ.
//
// Труп остаётся в мире и в списке движка до выгрузки (это не баг, см.
// "гистерезис выгрузки" в FIELD_MAP). Но для модулей поведения мёртвый
// враг — мусор: мутировать его масштаб или поводок бессмысленно, а в
// счётчике "врагов рядом" он завышает опасность.
// Сырой доступ к списку живых объектов: без фильтра «существо».
//
// `EnemyBodyAt` отсеивает всё, что не существо, — и перекличка, глядя
// через него, не увидела бы ни одной пешки. Для диагностики нужен именно
// сырой список: вопрос стоит «видит ли обход тела партии вообще».
int ActorCount() { return g_nAct; }

uintptr_t ActorAt(int idx, const char** kindOut)
{
    if (kindOut) *kindOut = 0;
    if (idx < 0 || idx >= g_nAct) return 0;
    if (kindOut) *kindOut = g_act[idx].kind;
    return g_act[idx].ptr;
}

uintptr_t EnemyBodyAt(int idx, const char** kindOut)
{
    if (idx < 0) return 0;
    int n = 0;
    for (int i = 0; i < g_nAct; ++i) {
        if (!g_act[i].ptr) continue;
        if (g_act[i].isDead) continue;          // труп — не цель
        const char* k = g_act[i].kind;
        // KindIsCreature, а НЕ KindIsEnemy: заяц не враг, но
        // масштабировать его можно и нужно (разнообразие живности).
        // Угрозу считает EnemyCount(), у него фильтр строже.
        if (!KindIsCreature(k)) continue;
        if (n == idx) {
            if (kindOut) *kindOut = k;
            return g_act[i].ptr;
        }
        ++n;
    }
    return 0;
}

uintptr_t FirstBodyOfKind(const char* kind)
{
    if (!kind) return 0;
    for (int i = 0; i < g_nAct; ++i) {
        if (!g_act[i].ptr) continue;
        if (g_act[i].isDead) continue;          // труп — не цель
        const char* k = g_act[i].kind;
        // Префикс, а не точное имя: uEm0100_0 и uEm0100_3 — тоже гоблины.
        if (k && !strncmp(k, kind, strlen(kind))) return g_act[i].ptr;
    }
    return 0;
}

void WorldScan_Tick()
{
    // Presence only. Engine keeps uEm* on the list and on screen far
    // past the spawn sphere. World=0 means the 29KB body is gone.
    // Do not invent a distance despawn.
    //
    // Build 69 — ГЛАВНАЯ ПРАВКА РЕФАКТОРИНГА.
    // Здесь стояло `if (!g_enabled) return;` — продуктовый тик гейтился
    // исследовательским флагом [devtools] enabled. Выключение панели
    // обрывало детектор боя, доктрину Guardian и позиции партии, то есть
    // ломало игровые фичи у любого, кто не разработчик.
    // Тик продуктовый и работает всегда.
    bool inWorld = InWorld();
    if (!inWorld) {
        // Build 56.7: cleanup только на ПЕРЕХОДЕ в «не в мире», а не каждый тик
        // (раньше RestoreAll логировал «world unload» каждые 150 мс — спам).
        if (g_wasInWorld) {
            if (g_research.onWorldUnload) g_research.onWorldUnload("world unload");
            // 85.24: продуктовый сброс. Живёт отдельно от research-хука, потому
            // что обязан работать и при выключенном DevTools.
            if (g_worldUnloadHooks.onWorldUnload)
                g_worldUnloadHooks.onWorldUnload("world unload");
            PartyPriorityProfileRestoreAll("world unload");
            // Эррата тоже обязана вернуть ваниль: указатели после выгрузки
            // недействительны, а незакрытая правка — это долг.
            ErrataRestoreAll();
            PartyPriorityProfileResetRuntime();
            g_priorityProfileWorldSince = 0;
            g_priorityProfileLastDiscover = 0;
            g_arisenPosOk = false;
            g_pawnPosOk = false;
            g_pawnPosWasOk = true;
            g_partyPosLastDiscover = 0;
            g_partyPosAttempts = 0;
            g_pawnCombatTarget = 0; // Build 62: цель пешки невалидна после выгрузки
            // Сброс тел: старые body-указатели после выгрузки недействительны.
            // Без этого PartyPositionsTick мог залипнуть на старом uPlayer.
            g_nParty = 0;
            // Build 57.1: сброс dynamic fix-правила (указатели устарели).
            g_guardianFixRule.resolved = g_guardianFixRule.applied = false;
            g_guardianFixRule.prioPtr = g_guardianFixRule.rulePtr = 0;
            g_guardianFixApplied = false;

            // P0-1 / 84.22: world-policy тоже fail-closed в ЭТОМ тике.
            // Иначе Director ещё 450 мс считает LastWorld свежим, а naked
            // Tempo-хуки множат координаты по адресам из мёртвой таблицы.
            g_nAct = 0;
            memset(g_act, 0, sizeof(g_act));
            // 85.12: кэш планировщика — на три тела (своя + две наёмных),
            // гасим все. Раньше это были два скаляра на одну главную пешку.
            ResetPawnPlannerCache();
            Tempo::OnWorldUnload();
            {
                WorldReport empty{};
                empty.timestampMs = 0;
                empty.dominantCategory = -1;
                CombatBus::Instance().PublishWorld(empty);
            }
            Aggro::DirectorFocusSet(-1, 0, 0, Aggro::DIRECTOR_RESPONSE_NONE);
            logFile << "WorldScan: FAIL-CLOSED world-unload"
                    << " actors=0 world.ts=0 tempo=drop aggro=release"
                    << std::endl;
        }
        // 85.24: ВОРОТА ЗАПИСИ. Перезагрузка сейва длится дольше одного тика,
        // поэтому окно не одноразовое, а продлевается, пока мира нет. Плюс
        // 2.5 с сверху на «мир вернулся, но движок ещё достраивает тела».
        // Так закрыты все записи — и тех модулей, о которых мы не подумали.
        Mem::BlockWritesFor(kWorldSettleMs);
        g_wasInWorld = false;
        return;
    }
    g_wasInWorld = true;

    // Темп анимации монстров удерживается ПОКАДРОВО: движок переписывает
    // эти поля сам, редкая запись жила бы один кадр из девяти.
    Tempo::AnimTick();
    Tempo::SprintWatchTick();   // кто вообще спринтует: игрок, пешка, монстр

    // Build 56.2: Guardian doctrine anchor/pawn positions (throttled discover + cheap read).
    // СОСТАВ ПАРТИИ МОГ ИЗМЕНИТЬСЯ.
    //
    // Разбор партии не пересканирует, пока найденные тела живы, — иначе
    // полный скан памяти шёл бы каждый тик. Но наёмная пешка приходит
    // посреди игры, и заметить её может только тот, кто и так каждый тик
    // смотрит на живые тела: обход актёров. Если в мире есть `uCmc`,
    // которого нет в списке партии, просим пересканировать (запрос
    // троттлится на стороне разбора).
    // ТЕЛА ПЕШЕК БЕРЁМ ИЗ СПИСКА ЖИВЫХ, А НЕ ИЗ СКАНА ПАМЯТИ.
    //
    // Лог 75.10 расставил точки: записи персонажей показывают три пешки
    // (Страйдер 4, Файтер 5, Маг 5 — уровни и вокации верные), а скан
    // памяти находит ОДНО тело, и все пробы читают его же. То есть
    // сравнение «Файтер против Страйдерши» было сравнением пешки с самой
    // собой.
    //
    // Скан памяти оказался ненадёжным источником: он зависит от раскладки
    // куч и от того, что успело попасть в регионы. Обход же живых объектов
    // идёт по связному списку от известных семян и находит соседей по
    // списку — а пешки лежат именно там.
    //
    // Поэтому теперь тела не «запрашиваются сканом», а ДОБАВЛЯЮТСЯ прямо
    // отсюда: увидели `uCmc`, которого нет в списке партии, — включили.
    {
        int seenPawns = 0;
        for (int i = 0; i < g_nAct; ++i) {
            const char* k = g_act[i].kind;
            if (!k) continue;
            const bool isPawn = (strcmp(k, "uCmc") == 0);
            if (!isPawn && strcmp(k, "uPlayer") != 0) continue;
            if (isPawn) ++seenPawns;
            if (PartyHasBody(g_act[i].ptr)) continue;
            PartyAdoptBody(g_act[i].ptr, k);
        }
        if (seenPawns > 0) PartySetExpectedPawns(seenPawns);
    }

    PartyPositionsTick();

    // Temporary player/pawn probe: '=' takes an AI snapshot. This is
    // intentionally checked before the WorldScan throttle so a deliberate
    // key press is not lost while the Arisen or pawn is sprinting.
    PartyHotkeyTick();

    static DWORD last = 0;
    DWORD now = MsNow();
    if (last && now - last < 150) return;

    // Build 69.2: с этой точки начинается тяжёлая часть тика — её и мерим.
    ScanTimer scanTimer;
    last = now;
    // 86.12: ОБХОД ЖИВЁТ, ПОКА ЖИВ ХОТЬ ОДИН ИСТОЧНИК СЕМЯН — а не только таблица.
    //
    // Было `if (g_nAct)`. Поле 86.12 (белый список актёров): таблица очистилась
    // от декораций — и вместе с ней исчез источник семян, потому что
    // RewalkActors() набирает семена из g_act. Дальше сработала вторая половина
    // ловушки: needUrgentPoll требует ПУСТОЙ партии, а партия была известна,
    // поэтому тяжёлый поллинг тоже не включился. Итог за сессию:
    //   passes=4 nonActors=8 maxActors=0 noWorldTicks=2257 directorWrites=0
    // То есть мод всю драку не видел ни одного монстра. Декорации в таблице были
    // не мусором, а субстратом обхода — выкинуть их, не отвязав семена от
    // таблицы, значит ослепнуть.
    //
    // Партия известна всегда и лежит в том же списке живых объектов (это и есть
    // смысл семян из g_party внутри RewalkActors), поэтому её одной достаточно.
    if (RewalkNeeded())
        RewalkActors();
    // 86.12: слепые окна — 150-мс такты, где партия известна, а актёров нет.
    // Ровно то состояние, в котором 86.12 провёл всю сессию; в логе оно читалось
    // только по косвенным признакам (passes=4 при 2257 тактах).
    if (!g_nAct && g_nParty > 0) ++s_blindTicks;
    // Поллинг горячего кольца:
    // Когда список пуст и семян нет — активный поиск (8 МБ/тик).
    // Когда актёры уже есть или известна партия — RewalkActors обходит всех
    // по связному списку в микросекунды, а поллинг спит и делает редкий
    // лёгкий срез раз в 2 секунды (256 КБ), исключая микрофризы в бою.
    // 86.14: режим поллинга зависит от того, нашли ли мы хоть одно СУЩЕСТВО,
    // а не от того, пуста ли таблица (см. SearchingForActors).
    static DWORD lastPollMs = 0;
    const bool searching = SearchingForActors();
    const DWORD pollEveryMs = searching ? 0 : 2000;
    if (pollEveryMs && lastPollMs && now - lastPollMs < pollEveryMs) return;
    lastPollMs = now;

    // Бюджет: старт до появления партии — прежние 8 МБ/тик; поиск существа —
    // 1 МБ на такт (кольцо 128 МБ обходится за ~19 с, срез стоит ~5 мс); когда
    // существо найдено — прежний лёгкий срез 256 КБ раз в 2 с.
    uint32_t budget;
    if (!g_nAct && !g_nParty) budget = 0x800000u;
    else if (searching)       budget = 0x100000u;
    else                      budget = 0x40000u;
    g_pollBudget = budget;

    // (Счётчик «подряд тиков без находки» убран: он увеличивался, но нигде не
    // читался — режим поллинга от него всё равно не зависел.)
    uintptr_t s = PollSeedSlice(budget);
    if (!s) return;
    int have = 0;
    for (int i = 0; i < g_nAct; ++i)
        if (g_act[i].ptr == s) { have = 1; break; }
    if (have) return;
    ++s_pollFinds;
    uintptr_t seed[kSeedCap];
    int ns = 0;
    seed[ns++] = s;
    for (int i = 0; i < g_nAct && ns < kSeedCap; ++i)
        if (g_act[i].ptr) seed[ns++] = g_act[i].ptr;
    DumpActorsFrom(seed, ns);
    PublishWorldFromActors();
    Tempo::RefreshTable();   // список изменился — пересобрать множители
}

} // namespace Runtime
