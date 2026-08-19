// AnimProbe — поиск часов анимации. См. AnimProbe.h.

#include "stdafx.h"
#include "AnimProbe.h"
#include "../runtime/RuntimeInternal.h"
#include "../TypeAtlas.Generated.h"
#include "../CharParamEnemy.Generated.h"
#include <math.h>

namespace AnimProbe {

// Тело гоблина — 0x73C0 байт. Берём с запасом, но кратно 4.
static const uint32_t kBodyBytes  = 0x7400;
static const uint32_t kBodySlots  = kBodyBytes / 4;
// Действия компактные (cEmActWalk ~116 B). 256 байт покрывают их целиком.
static const uint32_t kActBytes   = 256;
static const uint32_t kActSlots   = kActBytes / 4;
// Дочерний объект (модель/контроллер движения). Первый замер показал, что
// в теле только таймеры, поэтому часы ищем в подобъектах.
static const uint32_t kChildBytes = 1024;
static const uint32_t kChildSlots = kChildBytes / 4;
static const uint32_t kTotalSlots = kBodySlots + kActSlots + kChildSlots;

// Статистика по каждому 4-байтному полю. Нас интересует форма изменения,
// а не сами значения: пила опознаётся по «много раз чуть-чуть вверх,
// изредка резко вниз».
struct SlotStat {
    float    minV, maxV;
    float    sumUp;      // сумма положительных приращений
    float    sumDown;    // сумма отрицательных приращений (по модулю)
    uint32_t ups;        // сколько кадров значение росло
    uint32_t downs;      // сколько убывало
    uint32_t resetsDown; // резкие падения — конец цикла у ВОЗРАСТАЮЩЕГО счётчика
    uint32_t resetsUp;   // резкие скачки вверх — конец цикла у ОБРАТНОГО отсчёта
    // Решающий признак. Часы анимации обязаны сбрасываться, когда меняется
    // действие: началась новая анимация — счёт пошёл заново. Таймер живёт
    // своей жизнью и сбрасывается когда угодно. Считаем, сколько сбросов
    // пришлось на смену действия.
    uint32_t resetsAtAct;
};

static SlotStat s_stat[kTotalSlots];   // трактовка байт как float
static SlotStat s_statI[kTotalSlots];  // те же байты как int32
static float    s_prev[kTotalSlots];
static float    s_prevI[kTotalSlots];
static bool     s_havePrev = false;

// Счётчики кадров почти всегда ЦЕЛЫЕ, а целое 21, прочитанное как float,
// равно 2.9e-44 — денормал, который отсеивался фильтром размаха. То есть
// весь float-проход смотрел мимо кадровых счётчиков. Целые до 2^24
// представимы во float точно, поэтому вторую трактовку считаем той же
// машинерией: просто конвертируем int32 -> float перед сравнением.
static void Accumulate(SlotStat* stat, const float* cur, const float* prev, bool nearAct);

static void ToIntView(const float* src, float* dst, uint32_t n)
{
    for (uint32_t i = 0; i < n; ++i) {
        int32_t v;
        memcpy(&v, &src[i], 4);
        dst[i] = (float)v;
    }
}

static bool      s_active = false;
static uintptr_t s_body = 0;
static uintptr_t s_actAtStart = 0;
static int       s_frames = 0;
static int       s_actChanges = 0;
static char      s_status[256] = "AnimProbe: idle";
static char      s_kindBuf[48] = {};
static char      s_actName[48] = {};
static char      s_lastActName[48] = {};
static uint32_t  s_childOff = 0;          // смещение указателя на подобъект
static char      s_childName[48] = {};

// Хронометраж действий: сколько кадров держится каждое.
//
// Идея от пользователя и она сильнее поиска поля: у нас уже есть
// наблюдаемое следствие — имя текущего действия в реальном времени.
// Засекая его длительность, получаем карту «имя -> кадры». В дампе .lmt
// есть длительности, но нет имён, а без имён тайминги бесполезны:
// непонятно, что именно правим. Плюс это метрика для проверки любой
// будущей гипотезы о темпе: записал в поле -> длительность изменилась?
struct ActRecord {
    char     name[48];
    uint32_t times;      // сколько раз наблюдали
    uint32_t frames;     // суммарно кадров рендера
    uint32_t minF, maxF;
};
static ActRecord s_acts[32];
static int       s_nActs = 0;
static int       s_actStartFrame = 0;

// Хронометраж предыдущего замера — чтобы печатать разницу, а не заставлять
// тестера сравнивать два лога глазами.
static ActRecord s_prevActs[32];
static int       s_nPrevActs = 0;
static char      s_verdict[190] = "verdict: no reference run yet";

// ЭТАЛОН — последний замер, в котором мы НИЧЕГО не писали.
//
// Сравнивать с предыдущим замером недостаточно: при делении пополам
// подряд идут несколько изменённых прогонов, и «так же, как в прошлый раз»
// означало бы «эффект сохранился», а нам нужно «эффект есть или нет
// относительно нетронутого гоблина». Отсюда отдельный эталон.
static ActRecord s_baseActs[32];
static int       s_nBaseActs = 0;
static bool      s_runHadWrites = false;   // писали ли во время этого замера

static void NoteActFinished(const char* name, int frames)
{
    if (!name || !name[0] || frames <= 0) return;
    for (int i = 0; i < s_nActs; ++i) {
        if (strcmp(s_acts[i].name, name) != 0) continue;
        ++s_acts[i].times;
        s_acts[i].frames += (uint32_t)frames;
        if ((uint32_t)frames < s_acts[i].minF) s_acts[i].minF = frames;
        if ((uint32_t)frames > s_acts[i].maxF) s_acts[i].maxF = frames;
        return;
    }
    if (s_nActs >= 32) return;
    ActRecord& r = s_acts[s_nActs++];
    lstrcpynA(r.name, name, sizeof(r.name));
    r.times = 1;
    r.frames = (uint32_t)frames;
    r.minF = r.maxF = (uint32_t)frames;
}

// --- проверка записью ------------------------------------------------------
//
// Зона поля: где оно физически лежит. Адрес резолвится КАЖДЫЙ КАДР заново,
// потому что объект действия пересоздаётся вместе со сменой действия, а
// подобъект может быть переприсвоен.
enum Zone { kZoneBody = 0, kZoneAct = 1, kZoneChild = 2 };

// Одно удерживаемое поле. Их может быть несколько: деление пополам пишет
// сразу в половину списка кандидатов.
struct HeldField {
    uint8_t   zone;
    uint32_t  off;
    uintptr_t at;      // адрес, по которому лежит поле сейчас
    float     orig;    // что там было до нас
    bool      origOk;
};
static HeldField s_held[32];
static int       s_nHeld = 0;
static float     s_heldVal = 1.0f;
static char      s_testStatus[192] = "test: off";

// Кандидаты в множитель из последнего разбора либо из сравнения A/B.
struct ConstCand { uint32_t off; float val; uint8_t zone; };
static ConstCand s_const[32];
static int       s_nConst = 0;

// --- деление пополам --------------------------------------------------------
static int   s_bisList[32];      // кандидаты, ещё не исключённые
static int   s_nBisList = 0;
static int   s_bisSet[32];       // поднабор, применённый в текущем раунде
static int   s_nBisSet = 0;
static int   s_bisRound = 0;
static float s_bisVal = 1.5f;
static bool  s_bisOn = false;
static char  s_bisStatus[192] = "bisect: off";

// --- A/B сравнение (подсказка про торпор) -----------------------------------
//
// Торпор замедляет у монстра ВСЁ: и анимацию, и физику. Значит движок уже
// умеет крутить нужную ручку, и она обязана лежать в памяти существа. Мы
// снимаем два слепка одного и того же тела — до и под эффектом — и берём
// поля, которые были НЕПОДВИЖНЫ в обоих слепках, но изменили значение.
// Это не гипотеза, а наблюдение за тем, что дёргает сама игра.
static const int kAbFrames = 60;          // ~1 с записи на слепок

// Слепок берём ШИРЕ обычного замера. Обычный смотрит тело + действие +
// ОДИН выбранный подобъект — если ручка темпа лежит в модели или в
// контроллере движения, мы её просто не увидим. Для A/B такой слепоты
// позволить нельзя: сравнение имеет смысл ровно один раз, пока на монстре
// висит эффект. Поэтому список дочерних объектов фиксируется на слепке A
// и переиспользуется на слепке B — иначе сравнивались бы разные адреса.
static const uint32_t kAbChildren   = 24;
static const uint32_t kAbChildSlots = 256;              // 1 КБ на объект
static const uint32_t kAbSlots      = kTotalSlots + kAbChildren * kAbChildSlots;

struct AbChild { uintptr_t ptr; uint32_t bodyOff; uint32_t slots; char name[40]; };
static AbChild   s_abChild[kAbChildren];
static uint32_t  s_nAbChild = 0;

// ТРИ слепка, а не два.
//
// Первый живой замер показал, почему двух мало: 40 «изменившихся
// неподвижных полей» оказались костями и кватернионами позы. Существо
// просто стояло в двух РАЗНЫХ позах, и в пределах одного слепка каждое
// такое поле было идеально постоянным. Фильтр «неподвижно в обоих» их
// не отсекает по определению.
//
// Лекарство — контрольный слепок: A, потом A2 в том же нормальном
// состоянии, и только потом B под эффектом. Всё, что отличается уже
// между A и A2, — шум позы, и в отчёт оно не попадает.
static const int kAbA = 0, kAbB = 1, kAbCtl = 2;

struct AbMeta {
    char  actStart[48];
    char  actEnd[48];
    float x, y, z;
    bool  dead;
};

static float     s_abMin[3][kAbSlots];
static float     s_abMax[3][kAbSlots];
static bool      s_abHave[3] = { false, false, false };
static AbMeta    s_abMeta[3];
static uintptr_t s_abBody = 0;
static int       s_abWhich = -1;          // -1 = не пишем
static int       s_abFrames = 0;
static char      s_abStatus[192] = "A/B: no captures";

// Код зоны для полей внутри дочерних объектов слепка: kZoneAbBase + номер.
static const uint8_t kZoneAbBase = 8;

// Имя зоны для лога и для кнопок. Для дочернего объекта имя собирается
// в общий буфер, поэтому вызывать его надо по одному разу на строку —
// так оно везде и используется.
static const char* ZoneName(uint8_t z)
{
    if (z >= kZoneAbBase) {
        static char buf[8];
        sprintf_s(buf, "ch%u", (unsigned)(z - kZoneAbBase));
        return buf;
    }
    return (z == kZoneAct) ? "act" : (z == kZoneChild) ? "child" : "body";
}

// Кандидаты последнего обхода — для кнопок в UI.
struct Candidate { uint32_t off; char name[40]; };
static Candidate s_cand[16];
static int       s_nCand = 0;

// Размер класса из атласа типов. Без него обход вылезает за границу объекта
// и показывает соседнюю кучу как «потомков»: cActBank это 36 байт, а читали
// мы от него килобайт — всё, что дальше, принадлежало кому-то другому.
static uint32_t ClassSize(const char* name)
{
    if (!name || !name[0]) return 0;
    const TypeAtlas::Info* i = TypeAtlas::FindByName(name);
    return i ? i->size : 0;
}

static bool ReadFloats(uintptr_t base, float* dst, uint32_t slots)
{
    // Одним защищённым чтением: побайтно было бы тысячи SEH-кадров на кадр.
    return Runtime::Mem::Rd((void*)base, dst, (size_t)slots * 4);
}

// Ближайший к Аризену живой враг нужного вида. Пустой kind = любой враг.
static uintptr_t PickNearestEnemy(const char* kind, char* kindOut, int cap)
{
    float ax = 0, ay = 0, az = 0;
    const bool haveArisen = Runtime::GetArisenWorldPos(&ax, &ay, &az);

    uintptr_t best = 0;
    float bestD2 = 3.0e38f;
    for (int i = 0; i < Runtime::g_nAct; ++i) {
        const Runtime::ActorDump& A = Runtime::g_act[i];
        if (!A.ptr || A.isDead) continue;
        if (!A.kind || !Runtime::KindIsEnemy(A.kind)) continue;
        // По ПРЕФИКСУ: в стае живут uEm0100, uEm0100_0 и uEm0100_3 —
        // это варианты одного вида, и точное сравнение имени пропускало
        // две трети стаи.
        if (kind && kind[0] && strncmp(A.kind, kind, strlen(kind)) != 0) continue;

        // Тело должно быть живым и того класса, за который себя выдаёт:
        // устаревший указатель однажды увёл обход на объекты мира (uOmObj*).
        char nm[48] = {};
        if (!Runtime::Mem::NameOfLiveObject(A.ptr, nm, sizeof(nm))) continue;
        if (strncmp(nm, "uEm", 3) != 0) continue;

        float d2 = 0.0f;
        if (haveArisen) {
            const float dx = A.x - ax, dy = A.y - ay, dz = A.z - az;
            d2 = dx * dx + dy * dy + dz * dz;
        }
        if (d2 < bestD2) {
            bestD2 = d2;
            best = A.ptr;
            if (kindOut) lstrcpynA(kindOut, nm, cap);
        }
    }

    if (best && haveArisen) {
        char l[160];
        sprintf_s(l, "AnimProbe: nearest enemy at %.1f m", sqrtf(bestD2) / 100.0f);
        logFile << l << std::endl;
    }
    return best;
}

// Прочитать весь наблюдаемый набор: тело + объект действия + подобъект.
// Вынесено из Tick, потому что теперь читает ещё и запись слепков A/B.
static bool ReadAll(float* cur)
{
    if (!s_body) return false;
    if (!ReadFloats(s_body, cur, kBodySlots)) return false;

    // Объект действия читаем каждый кадр заново: он меняется вместе
    // с действием, и его смена — сама по себе полезный сигнал.
    memset(cur + kBodySlots, 0, kActSlots * 4);
    const uintptr_t act = Runtime::ActObjectOf(s_body);
    if (act) {
        // Строго в границах класса: за ними начинается чужая память,
        // и её «пилы» не имеют к нашему существу никакого отношения.
        char an[48] = {};
        Runtime::Mem::NameOfLiveObject(act, an, sizeof(an));
        const uint32_t asz = ClassSize(an);
        const uint32_t aw = (asz ? (asz > kActBytes ? kActBytes : asz) : kActBytes) / 4;
        ReadFloats(act, cur + kBodySlots, aw);
    }

    // Дочерний объект: указатель перечитываем каждый кадр, он может
    // пересоздаваться вместе со сменой действия.
    memset(cur + kBodySlots + kActSlots, 0, kChildSlots * 4);
    if (s_childOff && s_childOff + 4 <= kBodyBytes) {
        uintptr_t child = 0;
        if (Runtime::Mem::RdPtr((void*)(s_body + s_childOff), &child)
            && Runtime::Mem::LooksHeap(child)) {
            if (!s_childName[0])
                Runtime::Mem::NameOfLiveObject(child, s_childName, sizeof(s_childName));
            const uint32_t csz = ClassSize(s_childName);
            const uint32_t cw = (csz ? (csz > kChildBytes ? kChildBytes : csz) : kChildBytes) / 4;
            if (!ReadFloats(child, cur + kBodySlots + kActSlots, cw))
                memset(cur + kBodySlots + kActSlots, 0, kChildSlots * 4);
        }
    }
    return true;
}

// --- именованная карта параметров особи --------------------------------------
//
// В теле лежит загруженный em0100_cmn.prp (класс cCharParamEnemy, 320 B),
// причём ДВУМЯ копиями подряд: +0x5870 и +0x59B0. Схема файла известна —
// 72 подписанных поля, — значит эти 640 байт тела перестают быть безымянными.
//
// Для охоты за темпом там сразу три интересных места:
//   +0x0050  res_TORPOR          сопротивление торпору;
//   +0x0120  RESTRAINT_SLOW_LV1  множитель замедления при захвате;
//   +0x0124  LV2, +0x0128 EX     он же для других уровней.
// В файле слоу-рейты равны 1.0, а движок в рантайме их обнуляет (это
// выяснилось в EnemyTuner при подборе сигнатуры) — то есть поля живые.
static uint32_t s_paramOff[4];
static int      s_nParamOff = 0;

static bool NearlyEqF(float a, float b) { const float d = a - b; return (d < 0.5f) && (d > -0.5f); }

// Сигнатура блока — три дистанции переключения камер (500/800/1200) и
// высота мгновенной смерти (1500). Именно эти поля движок не трогает;
// слоу-рейты в сигнатуру брать нельзя, на них уже обжигались.
static bool LooksLikeParamBlock(const float* body, uint32_t off)
{
    if (off + CharParamEnemy::kSize > kBodyBytes) return false;
    const float* b = body + off / 4;
    return NearlyEqF(b[0x0EC / 4], 500.0f)
        && NearlyEqF(b[0x0F4 / 4], 800.0f)
        && NearlyEqF(b[0x0FC / 4], 1200.0f)
        && NearlyEqF(b[0x110 / 4], 1500.0f);
}

// Ищем по слепку тела, а не отдельными чтениями: 7400 позиций по четыре
// чтения каждая — это тридцать тысяч защищённых чтений на ровном месте.
static void FindParamCopies(uintptr_t body)
{
    s_nParamOff = 0;
    static float buf[kBodySlots];
    if (!ReadFloats(body, buf, kBodySlots)) return;
    for (uint32_t off = 0; off + CharParamEnemy::kSize <= kBodyBytes && s_nParamOff < 4; off += 4)
        if (LooksLikeParamBlock(buf, off)) s_paramOff[s_nParamOff++] = off;

    logFile << "AnimProbe: cCharParamEnemy copies: " << s_nParamOff;
    for (int i = 0; i < s_nParamOff; ++i) {
        char l[48];
        sprintf_s(l, "%s+0x%04X", i ? ", " : " at ", (unsigned)s_paramOff[i]);
        logFile << l;
    }
    logFile << std::endl;
}

// Подпись смещения в теле, если оно попало в блок параметров.
// Пустая строка — не попало.
static const char* ParamLabel(uint32_t bodyOff, char* buf, int cap)
{
    buf[0] = 0;
    for (int c = 0; c < s_nParamOff; ++c) {
        const uint32_t base = s_paramOff[c];
        if (bodyOff < base || bodyOff >= base + CharParamEnemy::kSize) continue;
        const uint32_t in = bodyOff - base;
        const char* nm = CharParamEnemy::LabelAt(in);
        if (nm[0]) sprintf_s(buf, cap, "  [param#%d %s]", c, nm);
        else       sprintf_s(buf, cap, "  [param#%d +0x%03X]", c, (unsigned)in);
        return buf;
    }
    return buf;
}

// Адрес поля прямо сейчас. 0 = зона недоступна в этот кадр.
static uintptr_t ResolveZone(uint8_t zone, uint32_t off)
{
    if (!s_body) return 0;
    if (zone == kZoneBody) {
        if (off + 4 > kBodyBytes) return 0;
        return s_body + off;
    }
    if (zone == kZoneAct) {
        const uintptr_t act = Runtime::ActObjectOf(s_body);
        if (!act) return 0;
        // За границей класса действия начинается чужая память.
        char an[48] = {};
        Runtime::Mem::NameOfLiveObject(act, an, sizeof(an));
        const uint32_t asz = ClassSize(an);
        const uint32_t lim = asz ? asz : kActBytes;
        if (off + 4 > lim) return 0;
        return act + off;
    }
    if (zone >= kZoneAbBase) {
        // Поле внутри объекта из списка слепка A/B. Указатель перечитываем
        // из тела: объект мог быть пересоздан движком.
        const uint32_t idx = (uint32_t)(zone - kZoneAbBase);
        if (idx >= s_nAbChild) return 0;
        uintptr_t p = 0;
        if (!Runtime::Mem::RdPtr((void*)(s_body + s_abChild[idx].bodyOff), &p)) return 0;
        if (!Runtime::Mem::LooksHeap(p)) return 0;
        if (off + 4 > s_abChild[idx].slots * 4) return 0;
        return p + off;
    }

    // zone == kZoneChild
    if (!s_childOff || s_childOff + 4 > kBodyBytes) return 0;
    uintptr_t child = 0;
    if (!Runtime::Mem::RdPtr((void*)(s_body + s_childOff), &child)) return 0;
    if (!Runtime::Mem::LooksHeap(child)) return 0;
    if (off + 4 > kChildBytes) return 0;
    return child + off;
}

// Похоже ли исходное значение на множитель темпа.
//
// ЗДЕСЬ БЫЛ БАГ, из-за которого Apply не сработал НИ РАЗУ. Проверка
// «не указатель ли это» звучала как asInt > 0x01000000 — но 1.0f в байтах
// это 0x3F800000 = 1065353216, то есть под неё попадал ЛЮБОЙ нормальный
// float. Инструмент честно отвечал REFUSED на все 24 кандидата подряд.
// Урок: эвристику «похоже на указатель» нельзя применять к полю, которое
// мы сами отобрали как float около единицы.
static bool PlausibleMultiplier(float v, char* why, int cap)
{
    if (!(v == v)) { lstrcpynA(why, "NaN", cap); return false; }
    if (v < 0.05f || v > 16.0f) {
        sprintf_s(why, cap, "%.6g is out of the 0.05..16 range", v);
        return false;
    }
    // Единственная оставшаяся страховка от указателя: если байты значения
    // указывают на ЖИВОЙ объект движка (валидная vtable с DTI), это точно
    // не множитель. Для 1.0 такое совпадение практически невозможно.
    uint32_t raw = 0;
    memcpy(&raw, &v, 4);
    if (Runtime::Mem::LooksHeap((uintptr_t)raw)) {
        char nm[48] = {};
        if (Runtime::Mem::NameOfLiveObject((uintptr_t)raw, nm, sizeof(nm)) && nm[0]) {
            sprintf_s(why, cap, "value points at a live %s", nm);
            return false;
        }
    }
    return true;
}

// Добавить поле в набор удерживаемых. Возвращает false с причиной в why.
static bool HoldAdd(uint8_t zone, uint32_t off, char* why, int cap)
{
    if (s_nHeld >= 32) { lstrcpynA(why, "too many fields held", cap); return false; }
    // Первые 0x100 байт тела — заголовок объекта: vtable по нулевому
    // смещению, указатели связного списка на +0x0C/+0x10. Запись туда
    // роняет игру мгновенно, и один такой вылет уже случился.
    const uint32_t guard = (zone == kZoneBody) ? 0x100u : 0x0Cu;
    if (off < guard) { lstrcpynA(why, "object header (vtable/list)", cap); return false; }

    const uintptr_t at = ResolveZone(zone, off);
    if (!at) { lstrcpynA(why, "zone unavailable right now", cap); return false; }

    float orig = 0.0f;
    if (!Runtime::Mem::Rd((void*)at, &orig, 4)) { lstrcpynA(why, "unreadable", cap); return false; }
    if (!PlausibleMultiplier(orig, why, cap)) return false;

    HeldField& f = s_held[s_nHeld++];
    f.zone = zone; f.off = off; f.at = at; f.orig = orig; f.origOk = true;
    Runtime::Mem::WrSafe((void*)at, &s_heldVal, 4);
    return true;
}

void TestRevert()
{
    for (int i = 0; i < s_nHeld; ++i) {
        HeldField& f = s_held[i];
        if (!f.origOk) continue;
        // Возвращаем значение только если объект тот же самый: объект
        // действия мог умереть и его память уже отдали другому.
        const uintptr_t at = ResolveZone(f.zone, f.off);
        if (at && at == f.at)
            Runtime::Mem::WrSafe((void*)at, &f.orig, 4);
    }
    if (s_nHeld) {
        logFile << "AnimProbe: reverted " << s_nHeld << " field(s)" << std::endl;
        lstrcpynA(s_testStatus, "test: off (reverted)", sizeof(s_testStatus));
    }
    s_nHeld = 0;
}

// Удержание: движок может переписывать поле каждый кадр, разовая запись
// ничего не покажет. Зовётся из Tick.
static void HoldTick()
{
    for (int i = 0; i < s_nHeld; ++i) {
        HeldField& f = s_held[i];
        const uintptr_t at = ResolveZone(f.zone, f.off);
        if (!at) continue;
        if (at != f.at) {
            // Объект пересоздан (типичный случай для зоны act): исходное
            // значение перечитываем у нового объекта.
            float fresh = 0.0f;
            if (!Runtime::Mem::Rd((void*)at, &fresh, 4)) continue;
            char why[64];
            if (!PlausibleMultiplier(fresh, why, sizeof(why))) continue;
            f.at = at; f.orig = fresh;
        }
        float now = 0.0f;
        if (Runtime::Mem::Rd((void*)at, &now, 4) && now != s_heldVal)
            Runtime::Mem::WrSafe((void*)at, &s_heldVal, 4);
    }
}

// Применить значение к набору кандидатов по индексам.
static void ApplyIndices(const int* idx, int n, float value)
{
    TestRevert();
    if (value < 0.1f) value = 0.1f;
    if (value > 4.0f) value = 4.0f;
    s_heldVal = value;

    int ok = 0;
    char why[96] = {};
    char last[96] = {};
    for (int k = 0; k < n; ++k) {
        const int i = idx[k];
        if (i < 0 || i >= s_nConst) continue;
        if (HoldAdd(s_const[i].zone, s_const[i].off, why, sizeof(why))) ++ok;
        else lstrcpynA(last, why, sizeof(last));
    }
    if (ok && s_active) s_runHadWrites = true;
    if (ok)
        sprintf_s(s_testStatus, "test: holding %d field(s) at %.3f", ok, value);
    else
        sprintf_s(s_testStatus, "test: nothing written (%s)", last[0] ? last : "empty set");
    logFile << "AnimProbe: " << s_testStatus << std::endl;
    for (int k = 0; k < s_nHeld; ++k) {
        char l[120];
        sprintf_s(l, "    %-5s +0x%04X  %.4f -> %.4f",
                  ZoneName(s_held[k].zone), (unsigned)s_held[k].off,
                  s_held[k].orig, value);
        logFile << l << std::endl;
    }
}

// --- подозреваемые в темпе ---------------------------------------------------
//
// ПОДТВЕРЖДЕНО ЗАПИСЬЮ 19.08.2026 (build 38).
//
// Диф по торпору дал наводку, а тестер вручную прошёл записью все 32
// кандидата и выделил ряд из ПЯТИ подряд идущих полей: запись 0.5 в любое
// из них замедляет гоблина вдвое, остальные кандидаты не дают ничего.
// В норме каждое поле равно 1.0.
//
// Пять штук — похоже на скорости отдельных слоёв воспроизведения (база,
// верх корпуса, аддитивные). Пишем во все сразу, чтобы не гадать, какой
// слой ведущий в данный момент.
struct Suspect { uint32_t off; const char* note; };
static const Suspect kSuspects[] = {
    { 0x0EE4, "confirmed playback rate" },
    { 0x0EE8, "confirmed playback rate" },
    { 0x0EEC, "confirmed playback rate" },
    { 0x0EF0, "confirmed playback rate" },
    { 0x0EF4, "confirmed playback rate" },
};
static const int kNSuspects = (int)(sizeof(kSuspects) / sizeof(kSuspects[0]));

int      SuspectCount()      { return kNSuspects; }
uint32_t SuspectOffset(int i) { return (i >= 0 && i < kNSuspects) ? kSuspects[i].off : 0; }

void SuspectsApply(float value)
{
    if (!s_body) {
        lstrcpynA(s_testStatus, "test: start the probe first", sizeof(s_testStatus));
        return;
    }
    TestRevert();
    if (value < 0.05f) value = 0.05f;
    if (value > 4.0f)  value = 4.0f;
    s_heldVal = value;

    int ok = 0;
    char why[96] = {}, last[96] = {};
    for (int i = 0; i < kNSuspects; ++i) {
        if (HoldAdd((uint8_t)kZoneBody, kSuspects[i].off, why, sizeof(why))) ++ok;
        else lstrcpynA(last, why, sizeof(last));
    }
    if (ok && s_active) s_runHadWrites = true;
    if (ok) sprintf_s(s_testStatus, "test: %d tempo suspect(s) held at %.3f", ok, value);
    else    sprintf_s(s_testStatus, "test: nothing written (%s)", last[0] ? last : "-");
    logFile << "AnimProbe: " << s_testStatus << std::endl;
    for (int k = 0; k < s_nHeld; ++k) {
        char l[140];
        sprintf_s(l, "    body +0x%04X  %.4f -> %.4f", (unsigned)s_held[k].off,
                  s_held[k].orig, value);
        logFile << l << std::endl;
    }
}

void TestApplyIndex(int i, float value)
{
    if (!s_body) {
        lstrcpynA(s_testStatus, "test: start the probe first", sizeof(s_testStatus));
        return;
    }
    ApplyIndices(&i, 1, value);
}

void TestWrite(uint32_t bodyOffset, float value)
{
    if (!s_body) {
        lstrcpynA(s_testStatus, "test: start the probe first", sizeof(s_testStatus));
        return;
    }
    TestRevert();
    if (value < 0.1f) value = 0.1f;
    if (value > 4.0f) value = 4.0f;
    s_heldVal = value;
    char why[96] = {};
    if (HoldAdd((uint8_t)kZoneBody, bodyOffset, why, sizeof(why)))
        sprintf_s(s_testStatus, "test: body +0x%04X %.4f -> %.4f (holding)",
                  (unsigned)bodyOffset, s_held[0].orig, value);
    else
        sprintf_s(s_testStatus, "test: REFUSED - %s", why);
    logFile << "AnimProbe: " << s_testStatus << std::endl;
}

int         ConstCount()        { return s_nConst; }
uint32_t    ConstOffset(int i)  { return (i >= 0 && i < s_nConst) ? s_const[i].off : 0; }
float       ConstValue(int i)   { return (i >= 0 && i < s_nConst) ? s_const[i].val : 0.0f; }
const char* ConstZone(int i)    { return (i >= 0 && i < s_nConst) ? ZoneName(s_const[i].zone) : ""; }

bool        TestActive()    { return s_nHeld > 0; }
int         TestHeldCount() { return s_nHeld; }
const char* TestStatus()    { return s_testStatus; }

// --- деление пополам --------------------------------------------------------
//
// Двадцать четыре кандидата — это двадцать четыре боя, если проверять их
// по одному. Вместо этого пишем сразу во все и смотрим на длительности
// действий: изменились — множитель в наборе, значит половиним. Пять боёв
// вместо двадцати четырёх, и первый же отвечает, есть ли искомое в списке.
static void BisectApplyCurrentSet()
{
    ApplyIndices(s_bisSet, s_nBisSet, s_bisVal);
    sprintf_s(s_bisStatus, "bisect round %d: %d of %d candidates at %.2f - fight, then Stop",
              s_bisRound, s_nBisSet, s_nBisList, s_bisVal);
    logFile << "AnimProbe: " << s_bisStatus << std::endl;
}

void BisectBegin(float value)
{
    if (!s_nConst) {
        lstrcpynA(s_bisStatus, "bisect: no candidates - run a capture first",
                  sizeof(s_bisStatus));
        return;
    }
    if (!s_body) {
        lstrcpynA(s_bisStatus, "bisect: start the probe first", sizeof(s_bisStatus));
        return;
    }
    s_bisVal = value;
    s_nBisList = 0;
    for (int i = 0; i < s_nConst && s_nBisList < 32; ++i) s_bisList[s_nBisList++] = i;
    s_nBisSet = s_nBisList;
    for (int i = 0; i < s_nBisSet; ++i) s_bisSet[i] = s_bisList[i];
    s_bisRound = 1;
    s_bisOn = true;
    logFile << "AnimProbe: === bisect start: " << s_nBisList
            << " candidates, value " << value << " ===" << std::endl;
    BisectApplyCurrentSet();
}

void BisectReport(bool effect)
{
    if (!s_bisOn) return;

    if (effect) {
        // Темп изменился => виновник внутри применённого поднабора.
        s_nBisList = s_nBisSet;
        for (int i = 0; i < s_nBisSet; ++i) s_bisList[i] = s_bisSet[i];
    } else {
        // Не изменился => виновника в поднаборе нет, выкидываем его.
        int keep[32], n = 0;
        for (int i = 0; i < s_nBisList; ++i) {
            bool inSet = false;
            for (int k = 0; k < s_nBisSet; ++k) if (s_bisSet[k] == s_bisList[i]) { inSet = true; break; }
            if (!inSet) keep[n++] = s_bisList[i];
        }
        s_nBisList = n;
        for (int i = 0; i < n; ++i) s_bisList[i] = keep[i];
    }

    if (s_nBisList == 0) {
        TestRevert();
        s_bisOn = false;
        lstrcpynA(s_bisStatus, "bisect: the multiplier is NOT in this list - "
                               "try the A/B torpor diff", sizeof(s_bisStatus));
        logFile << "AnimProbe: " << s_bisStatus << std::endl;
        return;
    }
    if (s_nBisList == 1) {
        const int w = s_bisList[0];
        s_nBisSet = 1; s_bisSet[0] = w;
        ApplyIndices(s_bisSet, 1, s_bisVal);
        // Режим НЕ выключаем: пусть Start ещё раз положит значение на место,
        // чтобы победителя можно было подтвердить отдельным боем.
        sprintf_s(s_bisStatus, "bisect: WINNER %s +0x%04X (was %.4f) held at %.2f - "
                               "press Start and confirm it once more",
                  ZoneName(s_const[w].zone), (unsigned)s_const[w].off,
                  s_const[w].val, s_bisVal);
        logFile << "AnimProbe: " << s_bisStatus << std::endl;
        return;
    }

    ++s_bisRound;
    const int half = (s_nBisList + 1) / 2;
    s_nBisSet = half;
    for (int i = 0; i < half; ++i) s_bisSet[i] = s_bisList[i];
    BisectApplyCurrentSet();
}

void BisectReset()
{
    TestRevert();
    s_bisOn = false;
    s_nBisList = s_nBisSet = 0;
    s_bisRound = 0;
    lstrcpynA(s_bisStatus, "bisect: off", sizeof(s_bisStatus));
}

bool        BisectActive() { return s_bisOn; }
const char* BisectStatus() { return s_bisStatus; }

// --- A/B сравнение ----------------------------------------------------------

// Собрать список дочерних объектов тела. Делается ОДИН раз, на слепке A:
// оба слепка обязаны смотреть на одни и те же адреса, иначе сравнение
// сравнивает разные объекты.
static void AbBuildChildList(uintptr_t body)
{
    s_nAbChild = 0;
    static uint32_t raw[kBodySlots];
    if (!Runtime::Mem::Rd((void*)body, raw, (size_t)kBodySlots * 4)) return;

    for (uint32_t i = 0; i < kBodySlots && s_nAbChild < kAbChildren; ++i) {
        const uintptr_t cand = (uintptr_t)raw[i];
        if (!Runtime::Mem::LooksHeap(cand) || cand == body) continue;

        char nm[48] = {};
        if (!Runtime::Mem::NameOfLiveObject(cand, nm, sizeof(nm)) || !nm[0]) continue;

        // ЧУЖИЕ АКТЁРЫ. В теле гоблина на +0x2B98 нашёлся указатель на
        // uPlayer, а на +0x322C — на uCmc. Это не потомки, а связи с целью:
        // их поля живут своей жизнью (игрок ходит и крутит камеру), и в
        // отчёт A/B они принесли полтора десятка ложных строк.
        if (strncmp(nm, "uEm", 3) == 0) continue;      // сосед по списку живых
        if (strncmp(nm, "uPlayer", 7) == 0) continue;  // цель, а не потомок
        if (strncmp(nm, "uNpc", 4) == 0) continue;
        if (strncmp(nm, "uPawn", 5) == 0) continue;
        if (strncmp(nm, "uCmc", 4) == 0) continue;     // команда движения цели
        {   // и вообще всё, что числится актёром в скане мира
            bool foreign = false;
            for (int a = 0; a < Runtime::g_nAct; ++a)
                if (Runtime::g_act[a].ptr == cand) { foreign = true; break; }
            if (foreign) continue;
        }

        bool dup = false;
        for (uint32_t k = 0; k < s_nAbChild; ++k)
            if (s_abChild[k].ptr == cand) { dup = true; break; }
        if (dup) continue;

        // Строго в границах класса: за ними чужая куча.
        uint32_t sz = ClassSize(nm);
        if (!sz) sz = 256;
        uint32_t slots = sz / 4;
        if (slots > kAbChildSlots) slots = kAbChildSlots;
        if (slots < 4) continue;

        AbChild& c = s_abChild[s_nAbChild++];
        c.ptr = cand; c.bodyOff = i * 4; c.slots = slots;
        lstrcpynA(c.name, nm, sizeof(c.name));
    }

    logFile << "AnimProbe: A/B watch list = " << s_nAbChild
            << " child objects" << std::endl;
    for (uint32_t k = 0; k < s_nAbChild; ++k) {
        char l[160];
        sprintf_s(l, "    ch%-2u  body +0x%04X -> 0x%08X  %s (%u slots)",
                  (unsigned)k, (unsigned)s_abChild[k].bodyOff,
                  (unsigned)s_abChild[k].ptr, s_abChild[k].name,
                  (unsigned)s_abChild[k].slots);
        logFile << l << std::endl;
    }
}

// Тихий NaN. Константное 0.0f/0.0f компилятор считает делением на ноль
// и ругается на этапе компиляции — берём битовую маску напрямую.
static float QNaN()
{
    const uint32_t bits = 0x7FC00000u;
    float f = 0.0f;
    memcpy(&f, &bits, 4);
    return f;
}

// Полное чтение слепка: тело + действие + подопечный + все дети из списка.
static bool AbReadWide(float* cur)
{
    if (!ReadAll(cur)) return false;
    for (uint32_t k = 0; k < kAbChildren; ++k) {
        float* dst = cur + kTotalSlots + k * kAbChildSlots;
        // Незаполненное помечаем NaN: такие слоты в сравнении отпадают сами.
        const float nan = QNaN();
        for (uint32_t j = 0; j < kAbChildSlots; ++j) dst[j] = nan;
        if (k >= s_nAbChild) continue;
        uintptr_t p = 0;
        if (!Runtime::Mem::RdPtr((void*)(s_body + s_abChild[k].bodyOff), &p)) continue;
        if (p != s_abChild[k].ptr) continue;    // объект подменили — не сравниваем
        Runtime::Mem::Rd((void*)p, dst, (size_t)s_abChild[k].slots * 4);
    }
    return true;
}

// Метаданные слепка: чем занималось существо и где стояло. Нужны, чтобы
// отчёт мог честно сказать «эти два слепка сравнивать нельзя».
static void AbNoteMeta(int w, bool atStart)
{
    AbMeta& m = s_abMeta[w];
    char act[48] = {};
    Runtime::ReadLiveAct(s_body, act, sizeof(act));
    if (atStart) {
        lstrcpynA(m.actStart, act[0] ? act : "?", sizeof(m.actStart));
        m.actEnd[0] = 0;
        m.dead = false;
        m.x = m.y = m.z = 0.0f;
        for (int i = 0; i < Runtime::g_nAct; ++i) {
            if (Runtime::g_act[i].ptr != s_body) continue;
            m.dead = Runtime::g_act[i].isDead;
            m.x = Runtime::g_act[i].x; m.y = Runtime::g_act[i].y; m.z = Runtime::g_act[i].z;
            break;
        }
    } else {
        lstrcpynA(m.actEnd, act[0] ? act : "?", sizeof(m.actEnd));
    }
}

void AbCapture(int which)
{
    if (which < 0 || which > 2) return;
    uintptr_t body = s_body;
    if (which != kAbA && s_abBody) {
        // Второй слепок обязан быть с ТОГО ЖЕ тела, иначе сравнение
        // сравнивает двух разных гоблинов.
        char nm[48] = {};
        if (Runtime::Mem::NameOfLiveObject(s_abBody, nm, sizeof(nm))
            && strncmp(nm, "uEm", 3) == 0)
            body = s_abBody;
    }
    if (!body) body = PickNearestEnemy(nullptr, s_kindBuf, sizeof(s_kindBuf));
    if (!body) {
        lstrcpynA(s_abStatus, "A/B: no target", sizeof(s_abStatus));
        logFile << "AnimProbe: " << s_abStatus << std::endl;
        return;
    }
    if (which != kAbA && !s_abHave[kAbA]) {
        lstrcpynA(s_abStatus, "A/B: capture A first", sizeof(s_abStatus));
        logFile << "AnimProbe: " << s_abStatus << std::endl;
        return;
    }

    s_body = body;
    if (which == kAbA) {
        s_abBody = body;
        s_abHave[0] = s_abHave[1] = s_abHave[2] = false;
        AbBuildChildList(body);
        FindParamCopies(body);
    }
    AbNoteMeta(which, true);
    {   // Слепок во время получения урона бесполезен: поза и таймеры чужие.
        const char* a = s_abMeta[which].actStart;
        if (strstr(a, "Dmg") || strstr(a, "Die") || strstr(a, "Collapse")
            || strstr(a, "Down") || strstr(a, "Blow"))
            logFile << "AnimProbe: WARNING - capture taken during '" << a
                    << "': the target is being hit or knocked down, the pose"
                    << " will not match the other captures" << std::endl;
    }
    if (s_abMeta[which].dead) {
        logFile << "AnimProbe: WARNING - target is dead; a corpse has no tempo"
                << std::endl;
    }

    for (uint32_t i = 0; i < kAbSlots; ++i) {
        s_abMin[which][i] =  3.0e38f;
        s_abMax[which][i] = -3.0e38f;
    }
    s_abWhich  = which;
    s_abFrames = 0;
    sprintf_s(s_abStatus, "A/B: capturing %s on 0x%08X (act %s) ...",
              (which == kAbA) ? "A" : (which == kAbB ? "B" : "A2"),
              (unsigned)body, s_abMeta[which].actStart);
    logFile << "AnimProbe: " << s_abStatus << std::endl;
}

static void AbTick()
{
    static float cur[kAbSlots];
    if (!AbReadWide(cur)) {
        lstrcpynA(s_abStatus, "A/B: target became unreadable", sizeof(s_abStatus));
        s_abWhich = -1;
        return;
    }
    const int w = s_abWhich;
    for (uint32_t i = 0; i < kAbSlots; ++i) {
        const float v = cur[i];
        if (!(v == v)) continue;                 // NaN = слот не читался
        if (v < s_abMin[w][i]) s_abMin[w][i] = v;
        if (v > s_abMax[w][i]) s_abMax[w][i] = v;
    }
    if (++s_abFrames >= kAbFrames) {
        s_abHave[w] = true;
        s_abWhich = -1;
        AbNoteMeta(w, false);
        const char* next =
            (w == kAbA)   ? "Now press A2 for the noise control (same state!)." :
            (w == kAbCtl) ? "Now apply the effect and capture B." :
                            "Press Compare.";
        sprintf_s(s_abStatus, "A/B: capture %s done (%s -> %s). %s",
                  (w == kAbA) ? "A" : (w == kAbB ? "B" : "A2"),
                  s_abMeta[w].actStart, s_abMeta[w].actEnd, next);
        logFile << "AnimProbe: " << s_abStatus << std::endl;
    }
}

// Разложить номер слота обратно в зону и смещение.
static void AbSlotZone(uint32_t i, uint8_t* zone, uint32_t* off, const char** owner)
{
    *owner = "";
    if (i >= kTotalSlots) {
        const uint32_t k = (i - kTotalSlots) / kAbChildSlots;
        *zone = (uint8_t)(kZoneAbBase + k);
        *off  = ((i - kTotalSlots) % kAbChildSlots) * 4;
        if (k < s_nAbChild) *owner = s_abChild[k].name;
        return;
    }
    if (i >= kBodySlots + kActSlots) {
        *zone = (uint8_t)kZoneChild; *off = (i - kBodySlots - kActSlots) * 4; return;
    }
    if (i >= kBodySlots) {
        *zone = (uint8_t)kZoneAct;   *off = (i - kBodySlots) * 4; return;
    }
    *zone = (uint8_t)kZoneBody; *off = i * 4;
}

// Отличается ли слот между слепками x и y (оба должны быть постоянны).
static bool AbSteadyDiffer(uint32_t i, int x, int y)
{
    const float x0 = s_abMin[x][i], x1 = s_abMax[x][i];
    const float y0 = s_abMin[y][i], y1 = s_abMax[y][i];
    if (!(x0 == x0) || !(y0 == y0)) return false;
    if (x0 != x1 || y0 != y1) return true;    // дрожало = не константа = шум
    return x0 != y0;
}

void AbCompare()
{
    if (!s_abHave[kAbA] || !s_abHave[kAbB]) {
        lstrcpynA(s_abStatus, "A/B: need captures A and B", sizeof(s_abStatus));
        return;
    }
    logFile << "AnimProbe: === A/B diff (A = normal, B = affected), body 0x"
            << std::hex << s_abBody << std::dec << " ===" << std::endl;

    // Честные предупреждения ДО таблицы: без них тестер поверит числам,
    // которые сравнивать было нельзя.
    const bool haveCtl = s_abHave[kAbCtl];
    if (!haveCtl) {
        logFile << "  WARNING: no A2 control capture. Every pose value that"
                << " happened to stand still will look like a hit. Capture A,"
                << " then A2 in the SAME state, then B." << std::endl;
    }
    for (int w = 0; w < 3; ++w) {
        if (!s_abHave[w]) continue;
        if (s_abMeta[w].dead)
            logFile << "  WARNING: capture " << (w == kAbA ? "A" : (w == kAbB ? "B" : "A2"))
                    << " was taken on a DEAD target" << std::endl;
    }
    if (strcmp(s_abMeta[kAbA].actStart, s_abMeta[kAbB].actStart) != 0)
        logFile << "  WARNING: different acts in A (" << s_abMeta[kAbA].actStart
                << ") and B (" << s_abMeta[kAbB].actStart
                << ") - poses differ, expect noise" << std::endl;
    {
        const float dx = s_abMeta[kAbA].x - s_abMeta[kAbB].x;
        const float dy = s_abMeta[kAbA].y - s_abMeta[kAbB].y;
        const float dz = s_abMeta[kAbA].z - s_abMeta[kAbB].z;
        const float d = sqrtf(dx * dx + dy * dy + dz * dz) / 100.0f;
        if (d > 1.0f) {
            char l[120];
            sprintf_s(l, "  note: the target moved %.1f m between A and B", d);
            logFile << l << std::endl;
        }
    }

    logFile << "  steady in BOTH captures, different value"
            << (haveCtl ? ", noise rejected by A2:" : ":") << std::endl;

    s_nConst = 0;
    int shown = 0;
    int rejected = 0;
    // Сначала дети и мелкие объекты, потом тело: в 29 КБ тела случайных
    // совпадений больше, а осмысленных кандидатов меньше.
    for (int pass = 0; pass < 2 && shown < 40; ++pass) {
        const uint32_t from = (pass == 0) ? kBodySlots : 0;
        const uint32_t to   = (pass == 0) ? kAbSlots   : kBodySlots;
        for (uint32_t i = from; i < to && shown < 40; ++i) {
            const float a0 = s_abMin[kAbA][i], a1 = s_abMax[kAbA][i];
            const float b0 = s_abMin[kAbB][i], b1 = s_abMax[kAbB][i];
            if (!(a0 == a0) || !(b0 == b0)) continue;    // слот не читался
            if (a0 != a1 || b0 != b1) continue;          // дрожал — не константа
            if (a0 > 3.0e37f || b0 > 3.0e37f) continue;
            if (a0 == b0) continue;                      // не изменился
            // Шум позы: поле уже отличалось между двумя НОРМАЛЬНЫМИ слепками.
            if (haveCtl && AbSteadyDiffer(i, kAbA, kAbCtl)) { ++rejected; continue; }
            // Интересны только правдоподобные множители: если в A стояло
            // значение не похожее на темп, это не наша ручка.
            if (a0 < 0.05f || a0 > 16.0f) continue;

            uint8_t zone = 0; uint32_t off = 0; const char* owner = "";
            AbSlotZone(i, &zone, &off, &owner);

            char lab[64];
            if (zone == (uint8_t)kZoneBody) ParamLabel(off, lab, sizeof(lab));
            else                            lab[0] = 0;

            char l[260];
            sprintf_s(l, "    %-5s +0x%04X   A %.5f -> B %.5f   ratio %.4f%s  %s%s",
                      ZoneName(zone), (unsigned)off, a0, b0,
                      (a0 != 0.0f) ? (b0 / a0) : 0.0f,
                      (a0 == 1.0f) ? "  << was exactly 1.0" : "", owner, lab);
            logFile << l << std::endl;
            ++shown;

            if (s_nConst < 32) {
                s_const[s_nConst].off  = off;
                s_const[s_nConst].val  = a0;
                s_const[s_nConst].zone = zone;
                ++s_nConst;
            }
        }
    }
    if (!shown)
        logFile << "    (none - the knob is not in the watched memory)" << std::endl;

    // Второй список: изменившиеся неподвижные поля, которые НЕ похожи на
    // множитель (сопротивление торпору равно 100, таймер эффекта — секунды).
    // Кандидатами они не становятся, но молча терять их нельзя: по ним
    // видно, что эффект вообще применился к этому телу.
    {
        logFile << "  other steady changes (not multiplier-like, for context):"
                << std::endl;
        int other = 0;
        for (uint32_t i = 0; i < kAbSlots && other < 20; ++i) {
            const float a0 = s_abMin[kAbA][i], a1 = s_abMax[kAbA][i];
            const float b0 = s_abMin[kAbB][i], b1 = s_abMax[kAbB][i];
            if (!(a0 == a0) || !(b0 == b0)) continue;
            if (a0 != a1 || b0 != b1) continue;
            if (a0 > 3.0e37f || b0 > 3.0e37f) continue;
            if (a0 == b0) continue;
            if (haveCtl && AbSteadyDiffer(i, kAbA, kAbCtl)) continue;
            if (a0 >= 0.05f && a0 <= 16.0f) continue;    // уже показали выше
            if (a0 > 1.0e9f || a0 < -1.0e9f) continue;   // мусор и указатели

            uint8_t zone = 0; uint32_t off = 0; const char* owner = "";
            AbSlotZone(i, &zone, &off, &owner);
            char lab[64];
            if (zone == (uint8_t)kZoneBody) ParamLabel(off, lab, sizeof(lab));
            else                            lab[0] = 0;

            // Подсказка под охоту за здоровьем: у гоблина human_hp в
            // cCharParamEnemy равен 1000, значит текущее HP — величина
            // того же порядка, которая УМЕНЬШАЕТСЯ от удара.
            const bool maybeHp = (b0 < a0) && (a0 >= 50.0f) && (a0 <= 100000.0f);

            char l[260];
            sprintf_s(l, "    %-5s +0x%04X   A %.4g -> B %.4g   %s%s%s",
                      ZoneName(zone), (unsigned)off, a0, b0, owner, lab,
                      maybeHp ? "   << dropped: possible HP" : "");
            logFile << l << std::endl;
            ++other;
        }
        if (!other) logFile << "    (none)" << std::endl;
    }
    logFile << "  These are now the candidate list. Equal ratios across several"
            << " fields mean one knob feeding many." << std::endl;

    if (rejected)
        logFile << "  (" << rejected << " field(s) rejected as pose noise by the"
                << " A2 control)" << std::endl;

    sprintf_s(s_abStatus, "A/B: %d candidate(s), %d rejected as pose noise%s",
              shown, rejected, haveCtl ? "" : " (NO A2 control - results are noisy)");
}

// --- именованный дамп параметров особи ---------------------------------------
void DumpCharParams()
{
    uintptr_t body = s_body;
    if (!body) body = PickNearestEnemy(nullptr, s_kindBuf, sizeof(s_kindBuf));
    if (!body) {
        lstrcpynA(s_status, "AnimProbe: no target for char params", sizeof(s_status));
        logFile << s_status << std::endl;
        return;
    }
    FindParamCopies(body);
    if (!s_nParamOff) {
        lstrcpynA(s_status, "AnimProbe: cCharParamEnemy signature not found",
                  sizeof(s_status));
        logFile << s_status << std::endl;
        return;
    }

    static float c0[CharParamEnemy::kSize / 4];
    static float c1[CharParamEnemy::kSize / 4];
    const bool have0 = ReadFloats(body + s_paramOff[0], c0, CharParamEnemy::kSize / 4);
    const bool have1 = (s_nParamOff > 1)
        && ReadFloats(body + s_paramOff[1], c1, CharParamEnemy::kSize / 4);
    if (!have0) {
        lstrcpynA(s_status, "AnimProbe: param block unreadable", sizeof(s_status));
        logFile << s_status << std::endl;
        return;
    }

    logFile << "AnimProbe: === cCharParamEnemy of 0x" << std::hex << body << std::dec
            << " (copy0 +0x" << std::hex << s_paramOff[0];
    if (have1) logFile << ", copy1 +0x" << s_paramOff[1];
    logFile << std::dec << ") ===" << std::endl;

    int diffs = 0;
    for (int i = 0; i < CharParamEnemy::kCount; ++i) {
        const CharParamEnemy::Field& f = CharParamEnemy::kFields[i];
        const uint32_t sl = f.off / 4;
        if (sl >= CharParamEnemy::kSize / 4) continue;

        char v0[40], v1[40];
        struct L {
            static void fmt(char* out, int cap, uint8_t type, float raw)
            {
                if (type == CharParamEnemy::kF32) { sprintf_s(out, cap, "%.4g", raw); return; }
                int32_t iv = 0; memcpy(&iv, &raw, 4);
                if (type == CharParamEnemy::kBool) sprintf_s(out, cap, "%s", (iv & 0xFF) ? "true" : "false");
                else                               sprintf_s(out, cap, "%d", iv);
            }
        };
        L::fmt(v0, sizeof(v0), f.type, c0[sl]);
        v1[0] = 0;
        bool differ = false;
        if (have1) {
            L::fmt(v1, sizeof(v1), f.type, c1[sl]);
            differ = (strcmp(v0, v1) != 0);
            if (differ) ++diffs;
        }

        char l[200];
        sprintf_s(l, "    +0x%04X  body +0x%04X  %-24s %-12s %-12s%s",
                  (unsigned)f.off, (unsigned)(s_paramOff[0] + f.off), f.name,
                  v0, have1 ? v1 : "-", differ ? " << copies differ" : "");
        logFile << l << std::endl;
    }
    logFile << "  copies differ in " << diffs << " field(s). The runtime copy is"
            << " the one the engine reads - watch it under torpor." << std::endl;

    sprintf_s(s_status, "AnimProbe: char params dumped, %d field(s) differ between copies",
              diffs);
}

bool        AbBusy()       { return s_abWhich >= 0; }
bool        AbHave(int w)  { return (w >= 0 && w < 3) ? s_abHave[w] : false; }
const char* AbStatus()     { return s_abStatus; }
void WatchChild(uint32_t bodyOffset)
{
    s_childOff = bodyOffset;
    s_childName[0] = 0;
}

void ScanChildren()
{
    char kb[48] = {};
    uintptr_t body = PickNearestEnemy(nullptr, kb, sizeof(kb));
    if (!body) {
        lstrcpynA(s_status, "AnimProbe: no enemies in the world", sizeof(s_status));
        logFile << s_status << std::endl;
        return;
    }

    static float buf[kBodySlots];
    if (!ReadFloats(body, buf, kBodySlots)) {
        lstrcpynA(s_status, "AnimProbe: body unreadable", sizeof(s_status));
        logFile << s_status << std::endl;
        return;
    }

    logFile << "AnimProbe: === child objects of body 0x" << std::hex << body
            << std::dec << " ===" << std::endl;

    // Интересен ли класс как владелец состояния анимации.
    struct L { static bool interesting(const char* nm) {
        return strstr(nm, "Motion") || strstr(nm, "Model") || strstr(nm, "Anim")
            || strstr(nm, "Lmt") || strstr(nm, "Ctrl") || strstr(nm, "Joint")
            || strstr(nm, "Array") || strstr(nm, "Act");
    }};

    s_nCand = 0;
    const uint32_t* raw = (const uint32_t*)buf;
    int found = 0, deep = 0;

    // Один и тот же объект встречается в теле десятки раз: у гоблина
    // 120 слотов ссылаются на общий rEffectProvider. Без дедупликации
    // они съедают лимит вывода, и до нужных объектов обход не доходит.
    static uintptr_t seen[256];
    static int       seenCnt[256];
    int nSeen = 0;
    for (uint32_t i = 0; i < kBodySlots && found < 120; ++i) {
        const uintptr_t cand = (uintptr_t)raw[i];
        if (!Runtime::Mem::LooksHeap(cand)) continue;
        if (cand == body) continue;                   // ссылка на самого себя
        char nm[48] = {};
        if (!Runtime::Mem::NameOfLiveObject(cand, nm, sizeof(nm)) || !nm[0]) continue;
        // Соседи по связному списку живых объектов — не потомки.
        if (strncmp(nm, "uEm", 3) == 0) continue;

        // Уже показывали этот адрес? Считаем повтор и идём дальше.
        bool dup = false;
        for (int s = 0; s < nSeen; ++s)
            if (seen[s] == cand) { ++seenCnt[s]; dup = true; break; }
        if (dup) continue;
        if (nSeen < 256) { seen[nSeen] = cand; seenCnt[nSeen] = 1; ++nSeen; }

        char line[160];
        const uint32_t sz = ClassSize(nm);
        sprintf_s(line, "    body +0x%04X -> 0x%08X  %s (%u B)",
                  (unsigned)(i * 4), (unsigned)cand, nm, sz);
        logFile << line;
        if (L::interesting(nm)) {
            logFile << "   <- candidate";
            if (s_nCand < 16) {
                s_cand[s_nCand].off = i * 4;
                lstrcpynA(s_cand[s_nCand].name, nm, sizeof(s_cand[s_nCand].name));
                ++s_nCand;
            }
        }
        logFile << std::endl;
        ++found;

        // Второй уровень — строго в границах объекта.
        if (!L::interesting(nm) || deep >= 8) continue;
        const uint32_t csz = ClassSize(nm);
        if (csz < 8) {
            if (csz) logFile << "        (" << csz << " B - no room for children)" << std::endl;
            else     logFile << "        (size unknown, not expanding)" << std::endl;
            continue;
        }
        ++deep;
        const uint32_t words = (csz > 1024 ? 1024 : csz) / 4;
        static uint32_t sub[256];
        if (!Runtime::Mem::Rd((void*)cand, sub, (size_t)words * 4)) continue;
        int shown = 0;
        for (uint32_t k = 0; k < words && shown < 12; ++k) {
            const uintptr_t c2 = (uintptr_t)sub[k];
            if (!Runtime::Mem::LooksHeap(c2) || c2 == cand || c2 == body) continue;
            char nm2[48] = {};
            if (!Runtime::Mem::NameOfLiveObject(c2, nm2, sizeof(nm2)) || !nm2[0]) continue;
            if (strncmp(nm2, "uEm", 3) == 0) continue;
            char l2[180];
            sprintf_s(l2, "        +0x%04X -> 0x%08X  %s",
                      (unsigned)(k * 4), (unsigned)c2, nm2);
            logFile << l2;
            if (L::interesting(nm2)) logFile << "   <- candidate";
            logFile << std::endl;
            ++shown;
        }
    }

    // Отдельно — потомки объекта текущего действия: он может держать
    // ссылку на запрос воспроизведения анимации.
    const uintptr_t act = Runtime::ActObjectOf(body);
    if (act) {
        char an[48] = {};
        Runtime::Mem::NameOfLiveObject(act, an, sizeof(an));
        logFile << "    act 0x" << std::hex << act << std::dec
                << "  " << (an[0] ? an : "?") << std::endl;
        const uint32_t asz = ClassSize(an);
        const uint32_t awords = (asz ? (asz > 256 ? 256 : asz) : 64) / 4;
        logFile << "        (act size " << asz << " B)" << std::endl;
        static uint32_t asub[64];
        if (Runtime::Mem::Rd((void*)act, asub, (size_t)awords * 4)) {
            for (uint32_t k = 0; k < awords; ++k) {
                const uintptr_t c2 = (uintptr_t)asub[k];
                if (!Runtime::Mem::LooksHeap(c2) || c2 == body) continue;
                char nm2[48] = {};
                if (!Runtime::Mem::NameOfLiveObject(c2, nm2, sizeof(nm2)) || !nm2[0]) continue;
                char l2[180];
                sprintf_s(l2, "        act +0x%04X -> 0x%08X  %s",
                          (unsigned)(k * 4), (unsigned)c2, nm2);
                logFile << l2 << std::endl;
            }
        }
    }

    // Сколько слотов ссылались на один и тот же объект — полезно знать:
    // это массивы слотов, а не отдельные подсистемы.
    for (int s = 0; s < nSeen; ++s) {
        if (seenCnt[s] < 3) continue;
        char l[120];
        sprintf_s(l, "    (0x%08X referenced by %d slots)",
                  (unsigned)seen[s], seenCnt[s]);
        logFile << l << std::endl;
    }

    sprintf_s(s_status, "AnimProbe: %d unique children, %d expanded - see log",
              found, deep);
    logFile << "  Use 'watch child' with a body offset, then run the probe."
            << std::endl;
}

void Start(const char* kind)
{
    s_active = false;
    s_body = 0;
    s_frames = 0;
    s_actChanges = 0;
    s_havePrev = false;
    s_nActs = 0;
    s_actStartFrame = 0;
    memset(s_acts, 0, sizeof(s_acts));
    memset(s_stat, 0, sizeof(s_stat));
    memset(s_statI, 0, sizeof(s_statI));
    for (uint32_t i = 0; i < kTotalSlots; ++i) {
        s_stat[i].minV = s_statI[i].minV = 3.0e38f;
        s_stat[i].maxV = s_statI[i].maxV = -3.0e38f;
    }

    // БЛИЖАЙШИЙ к Аризену, а не первый в списке.
    //
    // Первый в списке может стоять в соседней зоне и смеяться, пока ты
    // дерёшься с другим. Именно так и вышло: за 2425 кадров проба поймала
    // только FingerLaugh и HeartyLaugh, а прыжковый удар прошёл мимо —
    // его выполнял другой гоблин.
    uintptr_t body = PickNearestEnemy(kind, s_kindBuf, sizeof(s_kindBuf));

    if (!body) {
        lstrcpynA(s_status, "AnimProbe: no enemies in the world", sizeof(s_status));
        logFile << s_status << std::endl;
        return;
    }

    s_body = body;
    s_runHadWrites = false;
    FindParamCopies(body);        // чтобы смещения в отчёте были подписаны
    s_actAtStart = Runtime::ActObjectOf(body);
    Runtime::ReadLiveAct(body, s_actName, sizeof(s_actName));
    lstrcpynA(s_lastActName, s_actName, sizeof(s_lastActName));
    s_active = true;

    float ax = 0, ay = 0, az = 0, bx = 0, by = 0, bz = 0;
    float dist = -1.0f;
    if (Runtime::GetArisenWorldPos(&ax, &ay, &az)) {
        for (int i = 0; i < Runtime::g_nAct; ++i)
            if (Runtime::g_act[i].ptr == body) { bx = Runtime::g_act[i].x; by = Runtime::g_act[i].y; bz = Runtime::g_act[i].z; }
        const float dx = bx - ax, dy = by - ay, dz = bz - az;
        dist = sqrtf(dx * dx + dy * dy + dz * dz) / 100.0f;
    }
    sprintf_s(s_status, "AnimProbe: recording %s 0x%08X at %.1f m, act=%s",
              s_kindBuf, (unsigned)body, dist, s_actName[0] ? s_actName : "?");
    logFile << s_status << std::endl;
    logFile << "AnimProbe: let the target act, then stop. Two or three full"
            << " animations are needed." << std::endl;

    // Деление пополам продолжается через несколько замеров подряд, а Stop
    // каждый раз откатывает запись (иначе игра осталась бы с нашим
    // значением без присмотра). Поэтому набор текущего раунда возвращаем
    // на место автоматически — руками ничего повторять не надо.
    if (s_bisOn && s_nBisSet) {
        ApplyIndices(s_bisSet, s_nBisSet, s_bisVal);
        if (s_nHeld) s_runHadWrites = true;
        logFile << "AnimProbe: bisect set re-applied for this run" << std::endl;
    }
}

void Tick()
{
    // Запись слепка A/B идёт независимо от обычного замера: она нужна
    // и тогда, когда проба не запущена.
    if (s_abWhich >= 0) { AbTick(); return; }

    if (!s_active || !s_body) return;

    static float cur[kTotalSlots];

    if (!ReadAll(cur)) {
        lstrcpynA(s_status, "AnimProbe: body became unreadable, stopped", sizeof(s_status));
        logFile << s_status << std::endl;
        s_active = false;
        return;
    }

    // Удерживаем испытуемые значения: движок может переписывать поле
    // каждый кадр, и разовая запись ничего не покажет.
    HoldTick();

    char nowAct[48] = {};
    Runtime::ReadLiveAct(s_body, nowAct, sizeof(nowAct));
    if (nowAct[0] && strcmp(nowAct, s_lastActName) != 0) {
        NoteActFinished(s_lastActName, s_frames - s_actStartFrame);
        s_actStartFrame = s_frames;
        ++s_actChanges;
        lstrcpynA(s_lastActName, nowAct, sizeof(s_lastActName));
        // Смена действия = гарантированный конец цикла. Предыдущий кадр
        // сравнивать не с чем, пропускаем его, чтобы не засчитать сброс
        // как «падение значения».
        memcpy(s_prev, cur, sizeof(s_prev));
        ++s_frames;
        return;
    }

    static float curI[kTotalSlots];
    ToIntView(cur, curI, kTotalSlots);

    if (s_havePrev) {
        // «Рядом со сменой действия» — три кадра рендера, полтора кадра
        // анимации: достаточно, чтобы поймать сброс, и мало, чтобы поймать
        // случайный.
        const bool nearAct = (s_frames - s_actStartFrame) <= 3;
        Accumulate(s_stat, cur, s_prev, nearAct);
        Accumulate(s_statI, curI, s_prevI, nearAct);
    }

    memcpy(s_prev, cur, sizeof(s_prev));
    memcpy(s_prevI, curI, sizeof(s_prevI));
    s_havePrev = true;
    ++s_frames;

    if ((s_frames % 30) == 0) {
        sprintf_s(s_status, "AnimProbe: %d frames, %d acts seen, now %s (%d f)",
                  s_frames, s_nActs, s_lastActName[0] ? s_lastActName : "?",
                  s_frames - s_actStartFrame);
    }
}

// Накопление формы изменения по одному набору значений.
static void Accumulate(SlotStat* stat, const float* cur, const float* prev, bool nearAct)
{
        for (uint32_t i = 0; i < kTotalSlots; ++i) {
            const float v = cur[i], p = prev[i];
            // Мусор и денормалы пропускаем: они дают ложные пилы.
            if (!(v == v) || !(p == p)) continue;
            if (v > 1.0e30f || v < -1.0e30f) continue;

            SlotStat& st = stat[i];
            if (v < st.minV) st.minV = v;
            if (v > st.maxV) st.maxV = v;

            const float d = v - p;
            const float span = st.maxV - st.minV;
            // Резкий скачок в любую сторону — это конец цикла, а не изменение.
            // Обратный отсчёт (N -> 0) в первой версии отбрасывался как
            // «убывает»: искали только возрастающие пилы, то есть половину
            // пространства поиска не смотрели вовсе.
            if (d > 0.0f) {
                if (span > 0.0f && d > span * 0.5f) { ++st.resetsUp; if (nearAct) ++st.resetsAtAct; }
                else { ++st.ups; st.sumUp += d; }
            } else if (d < 0.0f) {
                if (span > 0.0f && -d > span * 0.5f) { ++st.resetsDown; if (nearAct) ++st.resetsAtAct; }
                else { ++st.downs; st.sumDown += -d; }
            }
        }
}

void Stop()
{
    s_active = false;
    TestRevert();   // не оставляем игру с нашим значением
    if (s_frames < 30) {
        lstrcpynA(s_status, "AnimProbe: too few frames", sizeof(s_status));
        logFile << s_status << std::endl;
        return;
    }

    logFile << "AnimProbe: === analysis: " << s_frames << " frames, "
            << s_actChanges << " act changes, body 0x" << std::hex << s_body
            << std::dec << " (" << s_kindBuf << ")";
    if (s_childOff)
        logFile << " child +0x" << std::hex << s_childOff << std::dec
                << " (" << (s_childName[0] ? s_childName : "?") << ")";
    logFile << " ===" << std::endl;
    if (s_actChanges < 2)
        logFile << "  WARNING: only " << s_actChanges << " act change(s). The target"
                << " was idle - no combat animations were captured." << std::endl;

    // --- хронометраж действий -------------------------------------------
    if (s_nActs) {
        NoteActFinished(s_lastActName, s_frames - s_actStartFrame);
        logFile << "  act durations (render frames; game animates at 30/s,"
                << " so divide by 2 for animation frames):" << std::endl;
        for (int i = 0; i < s_nActs; ++i) {
            const ActRecord& r = s_acts[i];
            const float avg = (float)r.frames / (float)r.times;
            char l[200];
            sprintf_s(l, "    %-32s x%-3u  avg %6.1f  min %4u  max %4u  "
                         "= %5.2f anim frames (%.2f s)",
                      r.name, r.times, avg, r.minF, r.maxF,
                      avg / 2.0f, avg / 60.0f);
            logFile << l << std::endl;
        }

        // --- сравнение с эталоном ------------------------------------------
        //
        // Сравниваем с ПОСЛЕДНИМ ЗАМЕРОМ БЕЗ ЗАПИСИ, а не с предыдущим.
        // При делении пополам подряд идут несколько изменённых прогонов;
        // «как в прошлый раз» значило бы «эффект сохранился», а вопрос
        // всегда один: есть ли эффект относительно нетронутого гоблина.
        {
            const bool useBase = (s_runHadWrites && s_nBaseActs > 0);
            const ActRecord* ref = useBase ? s_baseActs : s_prevActs;
            const int nRef = useBase ? s_nBaseActs : s_nPrevActs;

            if (nRef) {
                logFile << (useBase ? "  vs baseline (clean run):"
                                    : "  vs previous run:") << std::endl;
                float worst = 0.0f;
                const char* worstName = "";
                int compared = 0;
                for (int i = 0; i < s_nActs; ++i) {
                    for (int j = 0; j < nRef; ++j) {
                        if (strcmp(s_acts[i].name, ref[j].name) != 0) continue;
                        const float now = (float)s_acts[i].frames / (float)s_acts[i].times;
                        const float was = (float)ref[j].frames / (float)ref[j].times;
                        if (was <= 0.0f) break;
                        const float rel = (now - was) / was;
                        char l[210];
                        sprintf_s(l, "    %-32s %6.1f -> %6.1f  (%+.1f%%)%s",
                                  s_acts[i].name, was, now, rel * 100.0f,
                                  (rel < -0.08f) ? "  << FASTER"
                                                 : (rel > 0.08f ? "  << slower" : ""));
                        logFile << l << std::endl;
                        ++compared;
                        const float mag = (rel < 0.0f) ? -rel : rel;
                        const float top = (worst < 0.0f) ? -worst : worst;
                        if (mag > top) { worst = rel; worstName = s_acts[i].name; }
                        break;
                    }
                }
                if (!compared) {
                    lstrcpynA(s_verdict, "verdict: no act seen in both runs - "
                                         "fight the same way both times",
                              sizeof(s_verdict));
                    logFile << "    (no act was seen in both runs)" << std::endl;
                } else {
                    const float mag = (worst < 0.0f) ? -worst : worst;
                    sprintf_s(s_verdict, "verdict vs %s: %s (%s %+.1f%%)",
                              useBase ? "baseline" : "previous",
                              (mag < 0.08f) ? "NO CHANGE"
                                            : (worst < 0.0f ? "FASTER" : "SLOWER"),
                              worstName, worst * 100.0f);
                    logFile << "  " << s_verdict << std::endl;
                }
            } else {
                lstrcpynA(s_verdict, "verdict: this run is the baseline",
                          sizeof(s_verdict));
            }

            // Чистый замер становится новым эталоном.
            if (!s_runHadWrites) {
                memcpy(s_baseActs, s_acts, sizeof(s_acts));
                s_nBaseActs = s_nActs;
                logFile << "  (clean run - stored as the baseline)" << std::endl;
            }
            memcpy(s_prevActs, s_acts, sizeof(s_acts));
            s_nPrevActs = s_nActs;
        }

        // Абсурдная длительность = существо не действовало.
        //
        // В живом замере cEm0100ActNAttack12 «шёл» 2049 кадров (34 с) при
        // четырёх сменах действия на 2546 кадров. Столько не длится ни одна
        // анимация: цель стояла (или лежала), а имя действия осталось от
        // последнего. Такие строки нельзя сравнивать между прогонами.
        for (int i = 0; i < s_nActs; ++i) {
            const float avg = (float)s_acts[i].frames / (float)s_acts[i].times;
            if (avg < 600.0f) continue;
            char l[200];
            sprintf_s(l, "    WARNING: %s held %.0f frames (%.1f s) - the target"
                         " was idle, dead or grabbed; not an animation length",
                      s_acts[i].name, avg, avg / 60.0f);
            logFile << l << std::endl;
        }
        if (s_actChanges < 4)
            logFile << "    WARNING: only " << s_actChanges << " act change(s)"
                    << " - too few to compare runs" << std::endl;

        logFile << "  Compare with the .lmt dump: goblin attacks are"
                << " 15, 19, 20, 21, 27 animation frames." << std::endl;
    }

    // --- КОНСТАНТЫ ОКОЛО 1.0 --------------------------------------------
    //
    // Смена подхода. Двадцать смен действия не дали ни одного счётчика,
    // привязанного к анимации: часов в теле нет. Но множитель темпа —
    // это и НЕ счётчик. Это константа, которая стоит на 1.0 и не меняется,
    // пока её не тронут. Проба показывала только то, что изменялось, то
    // есть искомое поле было невидимо для неё по определению.
    //
    // Здесь наоборот: ищем то, что НЕ менялось ни разу и лежит около
    // единицы. Такие поля — кандидаты в множители скорости воспроизведения.
    // Проверяются они не глазами, а записью: поставить 1.5 и посмотреть,
    // сократилась ли длительность действия (её мы уже умеем мерить).
    {
        logFile << "  CONSTANT ~1.0 (playback rate candidates, never changed):"
                << std::endl;
        int shownC = 0;
        s_nConst = 0;
        // Сначала объект действия и подопечный потомок: они маленькие,
        // и константа там гораздо осмысленнее, чем в 29 КБ тела.
        for (int pass = 0; pass < 2 && shownC < 40; ++pass) {
            const uint32_t from = (pass == 0) ? kBodySlots : 0;
            const uint32_t to   = (pass == 0) ? kTotalSlots : kBodySlots;
            for (uint32_t i = from; i < to && shownC < 40; ++i) {
                const SlotStat& st = s_stat[i];
                if (st.ups || st.downs || st.resetsUp || st.resetsDown) continue;
                const float v = st.minV;
                if (v != st.maxV) continue;
                if (v > 3.0e37f || v < -3.0e37f) continue;   // ни разу не читалось
                // В теле — только ровная единица, иначе список утонет
                // в тысячах случайных констант. В мелких объектах —
                // весь разумный диапазон множителя.
                if (pass == 1) { if (v != 1.0f) continue; }
                else           { if (v < 0.25f || v > 4.0f) continue; }

                // ЕДИНИЦЫ МАТРИЦ. Живой замер выдал сорок кандидатов вида
                // +0x0618, +0x062C, +0x0648, +0x065C, потом ровно то же
                // через 0x140 байт — это массив матриц с шагом 320 B, а
                // «кандидаты» в нём суть диагональные единицы и w-компоненты
                // кватернионов. Опознаются по соседям-нулям.
                {
                    int zeros = 0;
                    for (int d = -3; d <= 3; ++d) {
                        if (!d) continue;
                        const int j = (int)i + d;
                        if (j < 0 || j >= (int)kTotalSlots) continue;
                        const SlotStat& n = s_stat[j];
                        if (n.minV == n.maxV && n.minV == 0.0f) ++zeros;
                    }
                    if (zeros >= 3) continue;   // (1,0,0,0) / (0,0,0,1)
                }

                uint8_t zone = (uint8_t)kZoneBody;
                uint32_t off = i * 4;
                if (i >= kBodySlots + kActSlots) { zone = (uint8_t)kZoneChild; off = (i - kBodySlots - kActSlots) * 4; }
                else if (i >= kBodySlots)        { zone = (uint8_t)kZoneAct;   off = (i - kBodySlots) * 4; }

                char lab[64];
                if (zone == (uint8_t)kZoneBody) ParamLabel(off, lab, sizeof(lab));
                else                            lab[0] = 0;
                char l[230];
                sprintf_s(l, "    %-5s +0x%04X  = %.4f%s", ZoneName(zone), (unsigned)off, v, lab);
                logFile << l << std::endl;
                ++shownC;

                // Запоминаем только то, во что вообще можно писать:
                // заголовок объекта исключён. Раньше в список попадало
                // ТОЛЬКО тело — то есть самые осмысленные кандидаты
                // (мелкие объекты действия и модели) кнопок не получали.
                const uint32_t guard = (zone == kZoneBody) ? 0x100u : 0x0Cu;
                if (s_nConst < 32 && off >= guard) {
                    s_const[s_nConst].off  = off;
                    s_const[s_nConst].val  = v;
                    s_const[s_nConst].zone = zone;
                    ++s_nConst;
                }
            }
        }
        if (!shownC) logFile << "    (none)" << std::endl;
        logFile << "    Test by writing: set 1.5, then measure the act duration."
                << " A playback rate shortens it; anything else does not."
                << std::endl;
    }

    // --- целочисленные счётчики -----------------------------------------
    // Ищем шаг ровно 1 на кадр анимации (=0.5 на кадр рендера) и сброс:
    // так выглядит классический счётчик кадров. Размах = длина анимации.
    {
        logFile << "  INTEGER counters (span = animation length in frames):"
                << std::endl;
        int shownI = 0;
        for (uint32_t i = 0; i < kTotalSlots && shownI < 20; ++i) {
            const SlotStat& st = s_statI[i];
            const float span = st.maxV - st.minV;
            if (span < 3.0f || span > 4000.0f) continue;
            const bool down = (st.downs > st.ups * 3) && (st.resetsUp > 0);
            const bool up   = (st.ups > st.downs * 3) && (st.resetsDown > 0);
            if (!up && !down) continue;
            const uint32_t ticks  = down ? st.downs : st.ups;
            const uint32_t resets = down ? st.resetsUp : st.resetsDown;
            const float    sum    = down ? st.sumDown : st.sumUp;
            if (ticks < 10 || resets < 2) continue;
            const float step = sum / (float)ticks;
            // Кадровый счётчик шагает единицей; всё остальное — не он.
            if (step < 0.8f || step > 1.2f) continue;

            const char* zone = "body";
            uint32_t off = i * 4;
            if (i >= kBodySlots + kActSlots) { zone = "child"; off = (i - kBodySlots - kActSlots) * 4; }
            else if (i >= kBodySlots)        { zone = "act  "; off = (i - kBodySlots) * 4; }

            char l[200];
            const unsigned atAct = st.resetsAtAct;
            sprintf_s(l, "    %s +0x%04X  %s  span %.0f frames (%.2f s)  step %.2f"
                         "  cycles %u  at act change %u  %s",
                      zone, (unsigned)off, down ? "DOWN" : "up  ",
                      span, span / 30.0f, step, resets, atAct,
                      (atAct * 2 >= resets) ? "<< TIED TO ANIMATION" : "(free-running timer)");
            logFile << l << std::endl;
            ++shownI;
        }
        if (!shownI) logFile << "    (none)" << std::endl;
        logFile << "    'at act change' is the decisive test: an animation clock"
                << " restarts when the act changes, a timer does not." << std::endl;
    }

    // Кандидат в часы анимации:
    //   растёт почти всегда, изредка резко сбрасывается, шаг положительный.
    // Период в кадрах = размах / средний шаг. Именно его сверяем с числом
    // кадров из дампа .lmt: совпало — это часы.
    struct Cand { uint32_t slot; float span, step, period; uint32_t ticks, resets, atAct; bool down; };
    Cand best[24];
    int nb = 0;

    for (uint32_t i = 0; i < kTotalSlots; ++i) {
        const SlotStat& st = s_stat[i];
        const float span = st.maxV - st.minV;
        if (span < 0.5f) continue;    // шум: околонулевой размах

        // Пила вверх или пила вниз — берём ту сторону, что доминирует.
        const bool down = (st.downs > st.ups * 3) && (st.resetsUp > 0);
        const bool up   = (st.ups > st.downs * 3) && (st.resetsDown > 0);
        if (!up && !down) continue;

        const uint32_t ticks  = down ? st.downs : st.ups;
        const uint32_t resets = down ? st.resetsUp : st.resetsDown;
        const float    sum    = down ? st.sumDown : st.sumUp;
        if (ticks < 10 || resets == 0) continue;

        const float step = sum / (float)ticks;
        if (step <= 0.0f) continue;
        const float period = span / step;
        if (period < 3.0f || period > 1000.0f) continue;

        Cand c;
        c.slot = i; c.span = span; c.step = step; c.period = period;
        c.ticks = ticks; c.resets = resets; c.atAct = st.resetsAtAct; c.down = down;

        // Держим 24 лучших по числу сбросов: чем больше циклов увидели,
        // тем надёжнее оценка периода.
        if (nb < 24) best[nb++] = c;
        else {
            int worst = 0;
            for (int k = 1; k < nb; ++k)
                if (best[k].resets < best[worst].resets) worst = k;
            if (c.resets > best[worst].resets) best[worst] = c;
        }
    }

    if (!nb) {
        logFile << "  no animation-clock candidates found" << std::endl;
        lstrcpynA(s_status, "AnimProbe: no clock found", sizeof(s_status));
        return;
    }

    logFile << "  candidates (compare period with .lmt frame counts):" << std::endl;
    for (int k = 0; k < nb; ++k) {
        // простая сортировка выводом: сначала с наибольшим числом циклов
        int bi = 0;
        for (int j = 1; j < nb; ++j)
            if (best[j].resets > best[bi].resets) bi = j;
        const Cand c = best[bi];
        best[bi].resets = 0;

        const char* zone = "body";
        uint32_t off = c.slot * 4;
        if (c.slot >= kBodySlots + kActSlots) {
            zone = "child"; off = (c.slot - kBodySlots - kActSlots) * 4;
        } else if (c.slot >= kBodySlots) {
            zone = "act  "; off = (c.slot - kBodySlots) * 4;
        }

        // Ровное время (1.00 с, 3.00 с) — таймер: настоящие длительности
        // из .lmt неровные. А решающее — доля сбросов, пришедшихся на смену
        // действия: часы анимации обязаны стартовать заново вместе с ней.
        const float seconds = c.span / 30.0f;
        const float halfSec = seconds * 2.0f;
        const int   nearest = (int)(halfSec + 0.5f);
        const bool  roundish = (halfSec - nearest < 0.02f) && (nearest - halfSec < 0.02f);
        const bool  tied = c.resets && (c.atAct * 2 >= c.resets);

        char line[220];
        sprintf_s(line,
            "    %s +0x%04X  %s step %.5f  span %.3f (%.2f s)  cycles %u  at act %u  %s",
            zone, (unsigned)off, c.down ? "DOWN" : "up  ",
            c.step, c.span, seconds, c.resets, c.atAct,
            tied ? "<< TIED TO ANIMATION"
                 : (roundish ? "(round time: timer)" : "(free-running)"));
        logFile << line << std::endl;
    }

    logFile << "  How to read: SPAN is the value range in animation frames"
            << " (the game counts 30 per second, we sample per rendered frame,"
            << " hence the typical step of 0.5). Compare SPAN with the .lmt"
            << " frame counts: goblin attacks are 15, 19, 20, 21, 27 frames."
            << " Round durations (1.00 s, 3.00 s) are timers, not animation."
            << std::endl;

    sprintf_s(s_status, "AnimProbe: %d candidates, %d frames - see log",
              nb, s_frames);
}

int         CandidateCount()      { return s_nCand; }
uint32_t    CandidateOffset(int i) { return (i >= 0 && i < s_nCand) ? s_cand[i].off : 0; }
const char* CandidateName(int i)   { return (i >= 0 && i < s_nCand) ? s_cand[i].name : ""; }
uint32_t    WatchedOffset()        { return s_childOff; }

bool Active() { return s_active; }
const char* Status() { return s_status; }
const char* Verdict() { return s_verdict; }

} // namespace AnimProbe
