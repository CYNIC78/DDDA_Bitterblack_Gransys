// Runtime::Tempo — вариативность темпа передвижения монстров. См. MonsterTempo.h.

#include "stdafx.h"
#include "RuntimeInternal.h"
#include "MonsterTempo.h"
#include "../TypeAtlas.Generated.h"

// Границы кода игры — из dinput8.cpp, там же, где их берёт FindSignature.
extern BYTE *codeBase, *codeEnd;

namespace Runtime {
namespace Tempo {

// Поиск места для хука.
//
// ПОЧЕМУ НЕ ПРОСТО FindSignature. Сигнатуры здесь короткие (5-6 байт) — это
// одиночные SSE-инструкции. `movss xmm6,[esp+30]` настолько типовая, что в
// двадцатимегабайтном образе почти обязана встретиться не раз (так и вышло:
// в Build 70.0 хук обычного движения не встал именно поэтому). Промахнуться
// мимо кода движения — гарантированный краш, поэтому:
//
//   одно совпадение   -> берём его;
//   несколько         -> оставляем те, рядом с которыми стоит КОНТЕКСТ —
//                        код, который может быть только здесь;
//   всё ещё неясно    -> не ставим хук вовсе.
//
// Контекст надёжнее адреса. Build 70.1 пробовал разрешать неоднозначность
// по RVA из статьи (0x44D030) — не сработало: у нас 317 совпадений и ни
// одного по этому адресу, то есть сборка exe другая. Адреса привязаны к
// версии, смысл кода — нет.
//
// Для кода применения движения контекст такой: сразу за чтением смещений
// идут записи в координаты тела.
//     movss [esi+0x40], xmm6   ->  F3 0F 11 76 40
//     movss [esi+0x44], xmm6   ->  F3 0F 11 76 44
//     movss [esi+0x48], xmm6   ->  F3 0F 11 76 48
// Посторонняя «movss xmm6,[esp+30]» таких соседей иметь не может.

// Есть ли образец в окне после позиции p.
static bool HasNear(BYTE* p, size_t window, const BYTE* pat, size_t plen)
{
    if (p + window + plen >= codeEnd) window = (size_t)(codeEnd - p - plen - 1);
    for (size_t off = 0; off < window; ++off) {
        size_t i = 0;
        for (; i < plen; ++i)
            if (pat[i] != 0xCC && p[off + i] != pat[i]) break;
        if (i == plen) return true;
    }
    return false;
}

// Сколько из трёх записей координат стоят рядом с кандидатом.
static int MovementContextScore(BYTE* p)
{
    static const BYTE storeX[] = { 0xF3, 0x0F, 0x11, 0x76, 0x40 };
    static const BYTE storeY[] = { 0xF3, 0x0F, 0x11, 0x76, 0x44 };
    static const BYTE storeZ[] = { 0xF3, 0x0F, 0x11, 0x76, 0x48 };
    const size_t kWindow = 192;   // «несколько инструкций ниже» по статье
    int score = 0;
    if (HasNear(p, kWindow, storeX, sizeof(storeX))) ++score;
    if (HasNear(p, kWindow, storeY, sizeof(storeY))) ++score;
    if (HasNear(p, kWindow, storeZ, sizeof(storeZ))) ++score;
    return score;
}

static BYTE* ResolveSite(const char* name, const BYTE* sig, size_t len,
                         bool needContext, int* outCount)
{
    *outCount = 0;
    if (!codeBase || !codeEnd || codeEnd <= codeBase) return nullptr;

    BYTE* first = nullptr;
    BYTE* best = nullptr;
    int   bestScore = 0;
    int   bestCount = 0;

    for (BYTE* p = codeBase; p + len < codeEnd; ++p) {
        size_t i = 0;
        for (; i < len; ++i)
            if (sig[i] != 0xCC && p[i] != sig[i]) break;
        if (i != len) continue;

        ++(*outCount);
        if (!first) first = p;
        if (!needContext) continue;

        const int score = MovementContextScore(p);
        if (score > bestScore) { bestScore = score; best = p; bestCount = 1; }
        else if (score == bestScore && score > 0) ++bestCount;
    }

    if (*outCount == 1) {
        logFile << "MonsterTempo: " << name << " resolved uniquely, rva 0x"
                << std::hex << (uint32_t)((uintptr_t)first - Mem::g_base)
                << std::dec << std::endl;
        return first;
    }

    logFile << "MonsterTempo: " << name << " matches " << *outCount;
    if (needContext) {
        logFile << ", best context " << bestScore << "/3"
                << ", such sites " << bestCount;
        if (best)
            logFile << ", rva 0x" << std::hex
                    << (uint32_t)((uintptr_t)best - Mem::g_base) << std::dec;
    }
    logFile << std::endl;

    // Берём место только если контекст полный и оно единственное такое.
    if (needContext && bestScore == 3 && bestCount == 1) return best;

    logFile << "MonsterTempo: " << name
            << " - cannot identify uniquely, hook not installed" << std::endl;
    return nullptr;
}

// ---------------------------------------------------------------------------
// Таблица «тело → множитель».
//
// Хук горячий: он срабатывает на КАЖДУЮ движущуюся сущность каждый кадр.
// Поэтому внутри него нет ни вызовов, ни резолвов — только сравнения по
// готовой таблице. Таблицу наполняет продуктовый тик раз в 150 мс, когда
// список актёров и так пересобирается.
//
// Множитель хранится битами float: хук грузит его прямо в xmm через movd,
// без преобразований.
// ---------------------------------------------------------------------------
struct TempoEntry {
    uintptr_t body;
    uint32_t  bits;   // биты float-множителя
};

static const int kMaxTracked = 32;   // столько же, сколько актёров в g_act

// extern "C" — чтобы имена были доступны из inline-asm без искажения.
extern "C" {
    TempoEntry g_tempoTable[kMaxTracked] = {};
    int        g_tempoCount = 0;      // 0 = хукам делать нечего
}

// --- настройки -------------------------------------------------------------
static bool  g_enabled = false;
// Диапазон множителей задаётся парой мин-макс, а не одним «разбросом».
// Так можно делать смещённые распределения: 1.00…1.25 = все чуть быстрее
// ванили, 0.85…1.05 = крадущаяся стая, 0.80…1.30 = полный хаос.
static float g_factorLo = 0.85f;
static float g_factorHi = 1.15f;
static bool  g_hookWalk   = true;
static bool  g_hookSprint = true;

// Жёсткие границы. Ini их не переопределяет: анимация не меняется, поэтому
// за этими пределами появляется заметное проскальзывание стоп, а крупный
// шаг за кадр начинает пробивать коллизию.
static const float kFactorMin = 0.75f;
static const float kFactorMax = 1.30f;

// ---------------------------------------------------------------------------
// ТЕМП АНИМАЦИИ — вторая, независимая ручка.
//
// НАЙДЕНО 19.08.2026. Диф по торпору показал два поля тела, которые стояли
// ровно на 0.5 и под замедлением стали ровно 0.25. Дальше тестер вручную
// прошёл все 32 кандидата записью и выделил РЯД ИЗ ПЯТИ подряд идущих
// множителей: 0x0EE4, 0x0EE8, 0x0EEC, 0x0EF0, 0x0EF4. В норме каждый равен
// 1.0; запись 0.5 в ЛЮБОЙ из них замедляет существо. Остальные кандидаты
// не дали ничего.
//
// Почему пять: скорее всего это скорости отдельных слоёв воспроизведения
// (база, верх корпуса, аддитивные). Пишем во все — тогда результат не
// зависит от того, какой слой сейчас ведущий.
//
// ПОЧЕМУ УМНОЖАЕМ, А НЕ ЗАДАЁМ. Движок сам меняет эти поля: торпор ставит
// 0.5, наверняка есть и другие механики. Если писать абсолютное значение,
// мы затрём чужую логику и получим гоблина, которого не берёт замедление.
// Поэтому каждый кадр читаем поле, считаем его «базой» движка, если оно
// изменилось не нами, и пишем база * множитель. Наша правка становится
// коэффициентом поверх игры, а не вместо неё.
static const uint32_t kAnimRateOff0 = 0x0EE4;   // начало ряда
static const int      kAnimRateCount = 5;       // 0x0EE4…0x0EF4, подряд

// Пределы жёстче, чем у передвижения: темп анимации тянет за собой окна
// уязвимости и хитбоксы оружия, слишком быстрый замах читать нечем.
static const float kAnimMin = 0.70f;
static const float kAnimMax = 1.40f;

// Область применения. Множитель воспроизведения — ГЛОБАЛЬНЫЙ для существа:
// он тянет за собой всё, что анимировано, включая ходьбу и повороты, а не
// только атаки. Поэтому область задаётся отдельно:
//   kScopeAll    — весь набор анимаций (по умолчанию);
//   kScopeAttack — только пока текущее действие является атакой. Тогда
//                  локомоция остаётся ванильной, и ручка «атака» честно
//                  отделена от ручки «передвижение».
enum AnimScope { kScopeAll = 0, kScopeAttack = 1 };

// ---------------------------------------------------------------------------
// СЛОЙ ПЕРЕОПРЕДЕЛЕНИЙ — шов для будущего контроллера мутаций.
//
// Этот модуль умеет ровно одно: применять к особи два множителя —
// передвижения и темпа анимации. КАКИМИ они должны быть, он не решает.
// Базовый слой (разброс от адреса тела) — это просто «мутация по
// умолчанию», чтобы стая не была одинаковой.
//
// Всё остальное — ярость, эскалация, реакция на смерть сородича, память
// энкаунтеров — живёт в отдельном модуле и приходит сюда через
// SetOverride(). Множители СКЛАДЫВАЮТСЯ УМНОЖЕНИЕМ: база × переопределение,
// затем жёсткий зажим. Так контроллер задаёт «этот вдвое злее обычного»,
// не зная и не ломая базовый разброс.
struct Override {
    uintptr_t body;
    float     loco;      // множитель поверх базового (1.0 = не трогать)
    float     atk;
    DWORD     until;     // 0 = бессрочно, иначе MsNow() истечения
};
static const int kMaxOverrides = 16;
static Override  g_ovr[kMaxOverrides] = {};
static int       g_nOvr = 0;

static void OverrideFor(uintptr_t body, float* loco, float* atk)
{
    *loco = 1.0f; *atk = 1.0f;
    for (int i = 0; i < g_nOvr; ++i) {
        if (g_ovr[i].body != body) continue;
        *loco = g_ovr[i].loco;
        *atk  = g_ovr[i].atk;
        return;
    }
}

// Просрочки снимаем на обновлении таблицы: точности в 150 мс контроллеру
// хватает, а покадровая проверка была бы работой ради работы.
static void OverridesExpire()
{
    const DWORD now = MsNow();
    int w = 0;
    for (int i = 0; i < g_nOvr; ++i) {
        if (g_ovr[i].until && now > g_ovr[i].until) continue;
        if (w != i) g_ovr[w] = g_ovr[i];
        ++w;
    }
    g_nOvr = w;
}

// ---------------------------------------------------------------------------
// Классификация действия: атака или нет.
//
// Кэшируем по VTABLE объекта действия, а не по объекту: объектов за бой
// сотни, а классов действий у вида два десятка. Резолв имени через DTI
// случается один раз на класс за сессию.
struct ActClass { uintptr_t vt; bool isAttack; };
static ActClass g_actClass[64] = {};
static int      g_nActClass = 0;

static bool ActIsAttackByVt(uintptr_t act, uintptr_t vt)
{
    if (!act || !vt) return false;

    for (int i = 0; i < g_nActClass; ++i)
        if (g_actClass[i].vt == vt) return g_actClass[i].isAttack;

    char nm[48] = {};
    bool isAtk = false;
    bool known = false;
    if (Mem::NameOfLiveObjectSafe((const void*)act, nm, sizeof(nm)) && nm[0]) {
        // СУДИМ ПО ТАБЛИЦЕ, А НЕ ПО ПОДСТРОКАМ.
        //
        // В ActMap.Generated.h лежат все 812 действий 35 видов с
        // категориями. Подстроки годились для гоблина и разваливались на
        // остальных: у крупных монстров в именах атак нет слова Attack
        // вовсе (AtkBite, ClawCyclone, Trample), а у магов — MgcMissile.
        // Плюс таблица отличает «пинаю» от «пинают меня»: ChargeKicked
        // это damage, а не attack.
        const ActMap::Act* a = ActMap::FindByName(nm);
        known = (a != 0);
        isAtk = ActMap::NameIsAttack(nm);
        if (g_nActClass < 64) {
            char l[180];
            sprintf_s(l, "Tempo: act class %s -> %s%s", nm,
                      isAtk ? "ATTACK" : "other",
                      known ? "" : " (NOT IN ActMap - treated as non-attack)");
            logFile << l << std::endl;
        }
    }
    if (g_nActClass < 64) {
        g_actClass[g_nActClass].vt = vt;
        g_actClass[g_nActClass].isAttack = isAtk;
        ++g_nActClass;
    }
    return isAtk;
}

// --- наблюдение за спринтом -------------------------------------------------
// Кто прошёл через хук спринта последним. Пишется из asm, читается тиком.
static volatile uintptr_t g_lastSprintBody = 0;
static SprintStats g_sprint = {};

// Цена работы: измеряем, а не заявляем.
static uint32_t g_animLastUs = 0, g_animMaxUs = 0, g_animAvgUs = 0, g_animTicks = 0;

struct AnimTimer {
    LARGE_INTEGER beg;
    AnimTimer() { QueryPerformanceCounter(&beg); }
    ~AnimTimer()
    {
        static LARGE_INTEGER freq = {};
        if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
        if (freq.QuadPart <= 0) return;
        LARGE_INTEGER end; QueryPerformanceCounter(&end);
        const uint32_t us = (uint32_t)(((end.QuadPart - beg.QuadPart) * 1000000LL)
                                       / freq.QuadPart);
        g_animLastUs = us;
        if (us > g_animMaxUs) g_animMaxUs = us;
        g_animAvgUs = g_animTicks ? (uint32_t)((g_animAvgUs * 7 + us) / 8) : us;
        ++g_animTicks;
    }
};

// ---------------------------------------------------------------------------
// ДОПУСК ВИДА.
//
// Ряд множителей найден и подтверждён на гоблине. Смещение 0x0EE4 лежит
// глубоко в общей для всех существ части тела, поэтому у других видов он
// СКОРЕЕ ВСЕГО тот же — но «скорее всего» не основание, чтобы писать
// float в чужую память. Тела разного размера: uEm0100 29632, uEm0200
// 29888, uEm0500 29408.
//
// Поэтому перед первой записью вид проходит допуск:
//   1. размер класса из TypeAtlas должен покрывать 0x0EF8;
//   2. все пять полей должны выглядеть скоростями (0.05…8.0);
//   3. хотя бы одно должно равняться ровно 1.0 — так выглядит покой.
// Результат печатается в лог один раз на вид: получается список
// проверенных видов прямо из игры, без ручного перебора.
struct SpeciesVerdict { char kind[24]; bool allowed; };
static SpeciesVerdict s_species[24] = {};
static int            s_nSpecies = 0;

static bool SpeciesAllowed(const char* kind, uintptr_t body)
{
    if (!kind || !kind[0]) return false;
    for (int i = 0; i < s_nSpecies; ++i)
        if (!strcmp(s_species[i].kind, kind)) return s_species[i].allowed;

    bool ok = true;
    const char* why = "ok";

    const TypeAtlas::Info* ti = TypeAtlas::FindByName(kind);
    if (ti && ti->size && ti->size < kAnimRateOff0 + kAnimRateCount * 4) {
        ok = false; why = "body smaller than the rate row";
    }

    float cur[kAnimRateCount] = {};
    if (ok && !Mem::Rd((void*)(body + kAnimRateOff0), cur, sizeof(cur))) {
        ok = false; why = "rate row unreadable";
    }
    if (ok) {
        bool anyOne = false;
        for (int k = 0; k < kAnimRateCount; ++k) {
            const float v = cur[k];
            if (!(v == v) || v < 0.05f || v > 8.0f) { ok = false; why = "values do not look like rates"; break; }
            if (v == 1.0f) anyOne = true;
        }
        if (ok && !anyOne) { ok = false; why = "no field at exactly 1.0"; }
    }

    if (s_nSpecies < 24) {
        lstrcpynA(s_species[s_nSpecies].kind, kind, sizeof(s_species[s_nSpecies].kind));
        s_species[s_nSpecies].allowed = ok;
        ++s_nSpecies;
    }
    char l[220];
    sprintf_s(l, "Tempo: species %s %s (%s) rates %.2f %.2f %.2f %.2f %.2f",
              kind, ok ? "ACCEPTED" : "rejected", why,
              cur[0], cur[1], cur[2], cur[3], cur[4]);
    logFile << l << std::endl;
    return ok;
}

struct AnimTrack {
    uintptr_t body;
    float     factor;
    float     base[kAnimRateCount];   // последнее значение, пришедшее от движка
    float     mine[kAnimRateCount];   // что записали мы
    bool      init;
    // СЛЕДИМ ЗА VTABLE, А НЕ ЗА УКАЗАТЕЛЕМ ОБЪЕКТА.
    //
    // Здесь была ошибка, из-за которой ограничение «только атаки» не
    // работало вовсе. Объекты действий берутся из банка (cActBank на
    // +0x2DC0), то есть переиспользуются: один и тот же адрес держит то
    // атаку, то ходьбу. Проверка «указатель не менялся — значит действие
    // то же» после первой же атаки залипала на вердикте ATTACK, и
    // множитель применялся навсегда. Снаружи это выглядело ровно так,
    // как сказал тестер: «ускоряет вообще всё».
    //
    // Vtable у разных классов действий разная, поэтому подмену в том же
    // адресе она ловит.
    uintptr_t actVt;                  // vtable действия, для которого решали
    bool      actIsAttack;
    char      kind[16];               // для списка в панели
};
static AnimTrack g_animTrack[kMaxTracked] = {};
static int       g_animCount = 0;
static bool      g_animEnabled = false;
static float     g_animLo = 0.90f;
static float     g_animHi = 1.15f;
static float     g_animMinSeen = 1.0f;
static float     g_animMaxSeen = 1.0f;
static int       g_animScope = kScopeAll;

// СВЯЗКА ХАРАКТЕРА.
//
// Полная независимость двух ручек даёт четыре характера, но тестер
// заметил, что она же даёт несуразицу: гоблин-спринтер с ванильным
// замахом читается как «быстро прибежал и застыл». Связка задаёт, в
// какой мере темп атаки следует за скоростью передвижения ОСОБИ:
//   0.0 — полностью независимые ручки (четыре характера);
//   1.0 — кто быстро бегает, тот быстро и бьёт (цельное существо);
//   между — смесь.
static float     g_animCoupling = 0.0f;

// СЧЁТЧИК ЧУЖИХ ЗАПИСЕЙ.
//
// Утверждение «движок переписывает эти поля каждый кадр» было моей
// догадкой, а не измерением. Теперь считаем: сколько раз мы обнаружили
// в поле значение, которого туда не писали. Ноль за бой = движок эти
// поля не трогает и покадровое удержание избыточно; много = трогает,
// и удержание обязательно.
static uint32_t  g_animEngineWrites = 0;
static uint32_t  g_animOurWrites = 0;

// --- состояние -------------------------------------------------------------
static LPBYTE pHkWalk = nullptr, oHkWalk = nullptr;
static LPBYTE pHkSprint = nullptr, oHkSprint = nullptr;
static int    g_walkMatches = 0, g_sprintMatches = 0;
static float  g_minSeen = 1.0f, g_maxSeen = 1.0f;

// ---------------------------------------------------------------------------
// Хук обычного движения.
//
// Оригинал: movss xmm6,[esp+30]   (F3 0F 10 74 24 30, 6 байт)
//   esi           — тело движущейся сущности
//   [esp+30/34/38] — смещения X/Y/Z за кадр
//
// Мы сдвигаем стек на 48 байт (0x30), поэтому X оказывается в [esp+60],
// а Z в [esp+68]. Y (вертикаль) НЕ трогаем: это падение и прыжки, там
// множитель означал бы изменение гравитации.
// ---------------------------------------------------------------------------
static void __declspec(naked) HMoveWalk()
{
    __asm
    {
        pushfd                          // -4
        push eax                        // -4
        push ecx                        // -4
        push edx                        // -4
        sub  esp, 0x20                  // -32  итого сдвиг 0x30
        movdqu [esp], xmm0
        movdqu [esp + 0x10], xmm1

        mov  ecx, [g_tempoCount]
        test ecx, ecx
        jz   walkRestore
        mov  eax, offset g_tempoTable
    walkScan:
        cmp  [eax], esi
        je   walkFound
        add  eax, 8
        dec  ecx
        jnz  walkScan
        jmp  walkRestore

    walkFound:
        movd  xmm0, dword ptr [eax + 4]
        movss xmm1, dword ptr [esp + 0x60]   // X
        mulss xmm1, xmm0
        movss dword ptr [esp + 0x60], xmm1
        movss xmm1, dword ptr [esp + 0x68]   // Z
        mulss xmm1, xmm0
        movss dword ptr [esp + 0x68], xmm1

    walkRestore:
        movdqu xmm1, [esp + 0x10]
        movdqu xmm0, [esp]
        add  esp, 0x20
        pop  edx
        pop  ecx
        pop  eax
        popfd

        movss xmm6, [esp + 0x30]        // оригинальная инструкция
        jmp  oHkWalk
    }
}

// ---------------------------------------------------------------------------
// Хук спринта.
//
// Оригинал: addss xmm2,[edi+40]   (F3 0F 58 57 40, 5 байт)
//   edi   — тело движущейся сущности
//   xmm2  — смещение X, xmm3 — Y, xmm6 — Z
//
// Здесь смещения лежат в регистрах, а не на стеке. Y (xmm3) снова не трогаем.
// ---------------------------------------------------------------------------
static void __declspec(naked) HMoveSprint()
{
    __asm
    {
        pushfd
        push eax
        push ecx
        sub  esp, 0x10
        movdqu [esp], xmm0

        mov  ecx, [g_tempoCount]
        test ecx, ecx
        jz   sprintRestore
        mov  eax, offset g_tempoTable
    sprintScan:
        cmp  [eax], edi
        je   sprintFound
        add  eax, 8
        dec  ecx
        jnz  sprintScan
        jmp  sprintRestore

    sprintFound:
        movd  xmm0, dword ptr [eax + 4]
        mulss xmm2, xmm0
        mulss xmm6, xmm0

    sprintRestore:
        // НАБЛЮДЕНИЕ ЗА СПРИНТОМ.
        //
        // Тестер заметил, что пешки в бою не спринтят вообще. Проверять
        // это глазами бессмысленно — здесь ровно то место, через которое
        // проходит ЛЮБОЙ спринтующий: игрок, пешка, монстр. Одна запись
        // указателя в глобал стоит ничего, а продуктовый тик потом
        // разберёт, чьё это было тело.
        mov  [g_lastSprintBody], edi
        movdqu xmm0, [esp]
        add  esp, 0x10
        pop  ecx
        pop  eax
        popfd

        addss xmm2, [edi + 0x40]        // оригинальная инструкция
        jmp  oHkSprint
    }
}

// ---------------------------------------------------------------------------
// Множитель конкретного монстра.
//
// Детерминирован от адреса тела — та же схема, что в PickScale. Это важно:
// один и тот же гоблин не должен менять темп на глазах у игрока, иначе
// вместо «непредсказуемого противника» получится дрожащая аномалия.
// Разброс же между особями в стае — ровно то, что ломает выученный ритм.
// ---------------------------------------------------------------------------
// «Место в диапазоне» для этой особи: 0 — самый медленный край, 1 — самый
// быстрый. Отдельно от самого диапазона: так связка ручек не ломает
// заданные границы (см. ниже).
static float HashUnit(uintptr_t body, uint32_t salt)
{
    // ФИНАЛИЗАТОР MURMUR3, а не один раунд умножения.
    //
    // Прежний хеш (одно умножение + два сдвига, младшие 16 бит) плохо
    // разводил РЕАЛЬНЫЕ адреса тел. Замер по логу тестера: тринадцать
    // гоблинов при заданном диапазоне 1.00…1.20 получили 1.01…1.12, то
    // есть верхние 40 % диапазона не использовались вообще. Причина в
    // структуре адресов: базы идут с шагом 0x20000, а хвосты повторяются
    // (0x0060, 0x7470, 0x8D20), и различия сидят в средних битах.
    //
    // Три раунда финализатора размешивают их полностью. Проверка на тех же
    // восьми адресах: покрытие диапазона выросло с 54 % до 83 %.
    uint32_t h = (uint32_t)(body >> 4) ^ salt;
    h ^= h >> 16;
    h *= 0x85EBCA6Bu;
    h ^= h >> 13;
    h *= 0xC2B2AE35u;
    h ^= h >> 16;
    return (float)h / 4294967295.0f;
}

static const uint32_t kSaltLoco = 0x00000000u;   // как было: поведение не меняется
static const uint32_t kSaltAtk  = 0x9E3779B9u;

static float FactorFor(uintptr_t body)
{
    const float unit = HashUnit(body, kSaltLoco);
    float f = g_factorLo + (g_factorHi - g_factorLo) * unit;  // равномерно в диапазоне
    if (f < kFactorMin) f = kFactorMin;
    if (f > kFactorMax) f = kFactorMax;
    return f;
}

// Множитель темпа анимации. Соль отличается от FactorFor намеренно:
// иначе быстрый бегун всегда был бы и быстрым бойцом. Две независимые
// ручки дают четыре характера — увалень, наскок-отскок, засадный, берсерк.
static float AnimFactorFor(uintptr_t body)
{
    // СВЯЗЫВАЕМ МЕСТО В ДИАПАЗОНЕ, А НЕ САМО ЗНАЧЕНИЕ.
    //
    // Первая версия подмешивала к темпу атаки готовый множитель скорости.
    // Результат тестер увидел сразу: при заданных 1.14…1.15 живые значения
    // оказались 1.06…1.14, то есть НИЖЕ минимума. Диапазон переставал
    // что-либо значить, и подобрать параметры стало нельзя.
    //
    // Теперь связка действует на «место в диапазоне»: быстрый бегун
    // оказывается у верхнего края СВОЕГО диапазона темпа атак, а не
    // получает чужое число. Границы, которые задал человек, остаются
    // границами при любой связке.
    float unit = HashUnit(body, kSaltAtk);
    if (g_animCoupling > 0.0f) {
        const float unitLoco = HashUnit(body, kSaltLoco);
        const float c = g_animCoupling;
        unit = unit + (unitLoco - unit) * c;

        // РАСТЯЖКА ПОСЛЕ СМЕШИВАНИЯ.
        //
        // Смесь двух независимых величин жмётся к середине: дисперсия
        // падает в (1-c)^2 + c^2 раз, и при c = 0.5 разброс сужается в
        // полтора раза. В логе тестера это видно прямо: задано 1.05…1.15,
        // фактически 1.05…1.12.
        //
        // Возвращаем масштаб вокруг середины, чтобы заданный диапазон
        // означал то, что написано, при любой связке.
        const float var = (1.0f - c) * (1.0f - c) + c * c;
        if (var > 0.0001f) {
            const float k = 1.0f / sqrtf(var);
            unit = 0.5f + (unit - 0.5f) * k;
            if (unit < 0.0f) unit = 0.0f;
            if (unit > 1.0f) unit = 1.0f;
        }
    }

    float f = g_animLo + (g_animHi - g_animLo) * unit;
    if (f < kAnimMin) f = kAnimMin;
    if (f > kAnimMax) f = kAnimMax;
    return f;
}

// Покадровое удержание темпа анимации.
//
// Зовётся КАЖДЫЙ кадр, а не раз в 150 мс: движок переписывает эти поля
// сам, и редкая запись жила бы один кадр из девяти. Стоимость — одно
// чтение 20 байт на монстра.
void AnimTick()
{
    if (!g_animEnabled || !g_animCount) return;
    AnimTimer timer;
    for (int i = 0; i < g_animCount; ++i) {
        AnimTrack& t = g_animTrack[i];
        if (!t.body) continue;

        float cur[kAnimRateCount];
        if (!Mem::Rd((void*)(t.body + kAnimRateOff0), cur, sizeof(cur))) continue;

        // Действие перечитываем каждый кадр, но имя резолвим только когда
        // сменился сам объект действия: имя стоит поиска по vtable, а
        // указатель — одно чтение.
        float factor = t.factor;
        if (g_animScope == kScopeAttack) {
            // Один указатель за кадр; имя класса — только когда действие
            // сменилось, и то один раз на класс за сессию (кэш по vtable).
            const uintptr_t act = ActObjectOf(t.body);
            uintptr_t vt = 0;
            if (act) Mem::RdPtr((void*)act, &vt);
            if (vt != t.actVt) {
                t.actVt = vt;
                t.actIsAttack = ActIsAttackByVt(act, vt);
            }
            if (!t.actIsAttack) factor = 1.0f;   // вне атаки — ваниль

            // Доказательство, а не обещание: первые применения пишем
            // в лог с именем действия. Если строк нет — гейт не срабатывал,
            // и разговор про «ускоряет всё» становится проверяемым.
            static int s_proof = 0;
            if (t.actIsAttack && s_proof < 12 && factor != 1.0f) {
                ++s_proof;
                char nm[48] = {};
                Mem::NameOfLiveObjectSafe((const void*)act, nm, sizeof(nm));
                char l[200];
                sprintf_s(l, "Tempo: attack scope applied 0x%08X %s x%.3f",
                          (unsigned)t.body, nm[0] ? nm : "?", factor);
                logFile << l << std::endl;
            }
        }

        float want[kAnimRateCount];
        bool  dirty = false;
        for (int k = 0; k < kAnimRateCount; ++k) {
            const float v = cur[k];
            // Мусор и явно не множители пропускаем: тело могло быть
            // переиспользовано движком под другой объект.
            if (!(v == v) || v <= 0.0f || v > 8.0f) { want[k] = v; continue; }

            if (!t.init) t.base[k] = v;
            else if (v != t.mine[k]) { t.base[k] = v; ++g_animEngineWrites; }
            float w = t.base[k] * factor;
            if (w < 0.05f) w = 0.05f;
            if (w > 4.00f) w = 4.00f;
            want[k] = w;
            if (w != v) dirty = true;
        }
        if (dirty) {
            Mem::WrSafe((void*)(t.body + kAnimRateOff0), want, sizeof(want));
            ++g_animOurWrites;
        }
        for (int k = 0; k < kAnimRateCount; ++k) t.mine[k] = want[k];
        t.init = true;
    }
}

void RefreshTable()
{
    // Ряд анимации живёт отдельно от хуков передвижения: его можно
    // включить и при выключенном [monsterTempo] enabled.
    OverridesExpire();

    if (g_animEnabled) {
        AnimTrack fresh[kMaxTracked] = {};
        int m = 0;
        float alo = 99.0f, ahi = 0.0f;
        for (int i = 0; i < g_nAct && m < kMaxTracked; ++i) {
            const uintptr_t body = g_act[i].ptr;
            if (!body) continue;
            if (!KindIsEnemy(g_act[i].kind)) continue;
            if (g_act[i].isDead) continue;
            if (!SpeciesAllowed(g_act[i].kind, body)) continue;

            AnimTrack& t = fresh[m];
            t.body = body;
            {   // итог = база × переопределение контроллера, затем зажим
                float ol = 1.0f, oa = 1.0f;
                OverrideFor(body, &ol, &oa);
                float f = AnimFactorFor(body) * oa;
                if (f < kAnimMin) f = kAnimMin;
                if (f > kAnimMax) f = kAnimMax;
                t.factor = f;
            }
            {   // короткая копия имени вида: указатель из g_act живёт
                // до следующего скана, а список в панели читается позже
                const char* k = g_act[i].kind ? g_act[i].kind : "?";
                int q = 0;
                for (; q < 15 && k[q]; ++q) t.kind[q] = k[q];
                t.kind[q] = 0;
            }
            // Состояние по этому телу переносим: база и наши записи
            // не должны обнуляться каждые 150 мс, иначе движок и мы
            // будем гонять значение по кругу.
            for (int k = 0; k < g_animCount; ++k) {
                if (g_animTrack[k].body != body) continue;
                t.init = g_animTrack[k].init;
                t.actVt = g_animTrack[k].actVt;
                t.actIsAttack = g_animTrack[k].actIsAttack;
                for (int q = 0; q < kAnimRateCount; ++q) {
                    t.base[q] = g_animTrack[k].base[q];
                    t.mine[q] = g_animTrack[k].mine[q];
                }
                break;
            }
            if (!t.init) {
                // Постановка на учёт — событие, которое стоит видеть в логе
                // с временем: именно её задержка выглядела в игре как
                // «гоблин внезапно взбесился посреди боя».
                char l[160];
                sprintf_s(l, "Tempo: anim enrolled 0x%08X %s factor %.3f (t=%u ms)",
                          (unsigned)body, t.kind, t.factor, (unsigned)MsNow());
                logFile << l << std::endl;
            }
            ++m;
            if (t.factor < alo) alo = t.factor;
            if (t.factor > ahi) ahi = t.factor;
        }
        // Счётчик последним — по той же причине, что и у таблицы хуков.
        for (int k = 0; k < m; ++k) g_animTrack[k] = fresh[k];
        g_animCount = m;
        g_animMinSeen = m ? alo : 1.0f;
        g_animMaxSeen = m ? ahi : 1.0f;
    } else {
        g_animCount = 0;
    }

    int n = 0;
    float lo = 99.0f, hi = 0.0f;
    for (int i = 0; g_enabled && i < g_nAct && n < kMaxTracked; ++i) {
        const uintptr_t body = g_act[i].ptr;
        if (!body) continue;
        // Только настоящие враги. У omni фильтр был «не игрок и не пешка»,
        // из-за чего ускорялись торговцы и союзные NPC. У нас есть DTI,
        // поэтому фильтруем точно.
        if (!KindIsEnemy(g_act[i].kind)) continue;
        if (g_act[i].isDead) continue;

        float ol = 1.0f, oa = 1.0f;
        OverrideFor(body, &ol, &oa);
        float f = FactorFor(body) * ol;
        if (f < kFactorMin) f = kFactorMin;
        if (f > kFactorMax) f = kFactorMax;
        union { float f32; uint32_t u32; } cv;
        cv.f32 = f;

        g_tempoTable[n].body = body;
        g_tempoTable[n].bits = cv.u32;
        ++n;
        if (f < lo) lo = f;
        if (f > hi) hi = f;
    }

    // ТЕЛА ИЗ ПЕРЕОПРЕДЕЛЕНИЙ — даже если это не монстры.
    //
    // Так слой пешек ускоряет пешку тем же примитивом: он просто кладёт
    // переопределение на её тело. Базового разброса тут нет — у пешки
    // не должно быть случайного «характера», ей задают ровно то число,
    // которое решил её слой.
    for (int i = 0; i < g_nOvr && n < kMaxTracked; ++i) {
        const uintptr_t b = g_ovr[i].body;
        if (!b) continue;
        bool dup = false;
        for (int k = 0; k < n; ++k) if (g_tempoTable[k].body == b) { dup = true; break; }
        if (dup) continue;

        float f = g_ovr[i].loco;
        if (f < kFactorMin) f = kFactorMin;
        if (f > kFactorMax) f = kFactorMax;
        union { float f32; uint32_t u32; } cv;
        cv.f32 = f;
        g_tempoTable[n].body = b;
        g_tempoTable[n].bits = cv.u32;
        ++n;
        if (f < lo) lo = f;
        if (f > hi) hi = f;
    }

    // Счётчик пишем ПОСЛЕДНИМ: хук читает его первым и при нуле не трогает
    // таблицу вообще. Так частично заполненная таблица не может быть
    // прочитана хуком с другого потока.
    g_tempoCount = n;
    g_minSeen = n ? lo : 1.0f;
    g_maxSeen = n ? hi : 1.0f;
}

void Init()
{
    g_enabled    = config.getBool("monsterTempo", "enabled", false);
    g_factorLo   = config.getFloat("monsterTempo", "factorMin", 0.85f);
    g_factorHi   = config.getFloat("monsterTempo", "factorMax", 1.15f);
    g_hookWalk   = config.getBool("monsterTempo", "hookWalk", true);
    g_hookSprint = config.getBool("monsterTempo", "hookSprint", true);

    g_animEnabled = config.getBool("monsterTempo", "animEnabled", false);
    // ПО УМОЛЧАНИЮ — ТОЛЬКО АТАКИ.
    //
    // Множитель воспроизведения глобален для существа, поэтому без этого
    // ограничения он ускоряет и ходьбу — а скоростью ходьбы уже управляет
    // хук передвижения. Две ручки крутили бы одно и то же, и разделить
    // «быстро ходит / медленно бьёт» стало бы невозможно.
    g_animScope   = config.getBool("monsterTempo", "animAttacksOnly", true)
                  ? kScopeAttack : kScopeAll;
    g_animLo      = config.getFloat("monsterTempo", "animFactorMin", 0.90f);
    g_animHi      = config.getFloat("monsterTempo", "animFactorMax", 1.15f);
    if (g_animLo < kAnimMin) g_animLo = kAnimMin;
    if (g_animHi > kAnimMax) g_animHi = kAnimMax;
    if (g_animLo > g_animHi) { const float s2 = g_animLo; g_animLo = g_animHi; g_animHi = s2; }
    g_animCoupling = config.getFloat("monsterTempo", "animCoupling", 0.0f);
    if (g_animCoupling < 0.0f) g_animCoupling = 0.0f;
    if (g_animCoupling > 1.0f) g_animCoupling = 1.0f;

    // ДЕЙСТВУЮЩИЕ настройки — в лог одной строкой.
    //
    // Повод: тестер сообщил «ускоряется вообще всё» при том, что в
    // репозитории animAttacksOnly = on. Разница между «что в коде»,
    // «что в поставляемом ini» и «что в ini у тестера» стоила итерации.
    // Больше не гадаем: печатаем то, с чем модуль реально стартовал.
    {
        char l[220];
        sprintf_s(l, "Tempo: effective config: loco %s %.2f..%.2f | anim %s %.2f..%.2f"
                     " scope=%s coupling=%.2f",
                  g_enabled ? "on" : "off", g_factorLo, g_factorHi,
                  g_animEnabled ? "on" : "off", g_animLo, g_animHi,
                  (g_animScope == kScopeAttack) ? "ATTACKS-ONLY" : "everything",
                  g_animCoupling);
        logFile << l << std::endl;
    }

    // Границы жёсткие независимо от ini, плюс защита от перевёрнутого
    // диапазона: min больше max — типовая опечатка в конфиге.
    if (g_factorLo < kFactorMin) g_factorLo = kFactorMin;
    if (g_factorHi > kFactorMax) g_factorHi = kFactorMax;
    if (g_factorLo > g_factorHi) { const float s = g_factorLo; g_factorLo = g_factorHi; g_factorHi = s; }

    // Хуки передвижения нужны не только монстрам. Слой пешек ускоряет
    // пешку тем же примитивом (docs/PAWN_SPRINT_RECON.md, вариант В),
    // поэтому ставим их, если этого хочет ЛЮБОЙ потребитель.
    const bool wantHooks = g_enabled
                         || config.getBool("pawnHaste", "enabled", false);
    if (!wantHooks) {
        logFile << "MonsterTempo: disabled" << std::endl;
        return;
    }

    // Сигнатуры коротки, поэтому сначала считаем совпадения по всему образу.
    // Ставим хук ТОЛЬКО при единственном совпадении: промахнуться мимо
    // нужной инструкции в коде движения — это гарантированный краш.
    BYTE sigWalk[]   = { 0xF3, 0x0F, 0x10, 0x74, 0x24, 0x30 };  // movss xmm6,[esp+30]
    BYTE sigSprint[] = { 0xF3, 0x0F, 0x58, 0x57, 0x40 };        // addss xmm2,[edi+40]

    BYTE* siteSprint = ResolveSite("sprint", sigSprint, sizeof(sigSprint),
                                   false, &g_sprintMatches);
    BYTE* siteWalk   = ResolveSite("walk", sigWalk, sizeof(sigWalk),
                                   true, &g_walkMatches);

    if (g_hookWalk && siteWalk) {
        pHkWalk = siteWalk;
        Hooks::CreateHook("TempoWalk", pHkWalk, HMoveWalk, (LPVOID*)&oHkWalk, true);
        oHkWalk = pHkWalk + 6;
    }
    if (g_hookSprint && siteSprint) {
        pHkSprint = siteSprint;
        Hooks::CreateHook("TempoSprint", pHkSprint, HMoveSprint, (LPVOID*)&oHkSprint, true);
        oHkSprint = pHkSprint + 5;
    }

    logFile << "MonsterTempo: range " << g_factorLo << ".." << g_factorHi
            << " walk=" << (pHkWalk ? 1 : 0) << "/" << g_walkMatches << " matches"
            << " sprint=" << (pHkSprint ? 1 : 0) << "/" << g_sprintMatches << " matches"
            << std::endl;

    if (g_walkMatches != 1 || g_sprintMatches != 1) {
        logFile << "MonsterTempo: ambiguous signature - hook not installed."
                << " The game runs vanilla." << std::endl;
    }
}

void Shutdown()
{
    // Снимаем множители до отцепления хуков: если кто-то из монстров
    // движется прямо сейчас, он доедет с ванильной скоростью.
    g_tempoCount = 0;
    if (pHkWalk)   Hooks::SwitchHook("TempoWalk", pHkWalk, false);
    if (pHkSprint) Hooks::SwitchHook("TempoSprint", pHkSprint, false);
}

void SetRange(float lo, float hi)
{
    if (lo < kFactorMin) lo = kFactorMin;
    if (hi > kFactorMax) hi = kFactorMax;
    if (lo > hi) { const float s = lo; lo = hi; hi = s; }
    g_factorLo = lo;
    g_factorHi = hi;
    RefreshTable();   // не ждём следующего тика: правка видна сразу
}

void GetRange(float* lo, float* hi)
{
    if (lo) *lo = g_factorLo;
    if (hi) *hi = g_factorHi;
}

Status GetStatus()
{
    Status s;
    s.enabled       = g_enabled;
    s.walkHooked    = pHkWalk != nullptr;
    s.sprintHooked  = pHkSprint != nullptr;
    s.walkMatches   = g_walkMatches;
    s.sprintMatches = g_sprintMatches;
    s.tracked       = g_tempoCount;
    s.minFactor     = g_minSeen;
    s.maxFactor     = g_maxSeen;
    s.animEnabled   = g_animEnabled;
    s.animTracked   = g_animCount;
    s.animMin       = g_animMinSeen;
    s.animMax       = g_animMaxSeen;
    s.animAttacksOnly = (g_animScope == kScopeAttack);
    s.animEngineWrites = g_animEngineWrites;
    s.animOurWrites    = g_animOurWrites;
    s.animCoupling     = g_animCoupling;
    return s;
}

void SetAnimRange(float lo, float hi)
{
    if (lo > hi) { const float t = lo; lo = hi; hi = t; }
    if (lo < kAnimMin) lo = kAnimMin;
    if (hi > kAnimMax) hi = kAnimMax;
    g_animLo = lo;
    g_animHi = hi;
}

void GetAnimRange(float* lo, float* hi)
{
    if (lo) *lo = g_animLo;
    if (hi) *hi = g_animHi;
}

void SetAnimEnabled(bool on)
{
    if (g_animEnabled == on) return;
    g_animEnabled = on;
    if (!on) {
        // Возвращаем движку его значения, иначе монстры останутся с нашим
        // коэффициентом до перезагрузки карты.
        for (int i = 0; i < g_animCount; ++i) {
            AnimTrack& t = g_animTrack[i];
            if (!t.body || !t.init) continue;
            Mem::WrSafe((void*)(t.body + kAnimRateOff0), t.base, sizeof(t.base));
        }
        g_animCount = 0;
    }
}

bool GetAnimEnabled() { return g_animEnabled; }

void SetAnimAttacksOnly(bool on)
{
    if ((g_animScope == kScopeAttack) == on) return;
    // При смене области возвращаем движку базу: иначе монстр, пойманный
    // на переходе, останется с нашим коэффициентом вне атаки.
    for (int i = 0; i < g_animCount; ++i) {
        AnimTrack& t = g_animTrack[i];
        if (!t.body || !t.init) continue;
        Mem::WrSafe((void*)(t.body + kAnimRateOff0), t.base, sizeof(t.base));
        t.init = false;
    }
    g_animScope = on ? kScopeAttack : kScopeAll;
}

void AnimResetCounters() { g_animEngineWrites = 0; g_animOurWrites = 0; }

void  SetAnimCoupling(float v)
{
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    g_animCoupling = v;
}
float GetAnimCoupling() { return g_animCoupling; }

// --- API для контроллера мутаций --------------------------------------------
void SetOverride(uintptr_t body, float loco, float atk, uint32_t ttlMs)
{
    if (!body) return;
    // Санитария: контроллер не должен уметь выйти за пределы системы.
    if (!(loco == loco) || loco < 0.25f || loco > 4.0f) loco = 1.0f;
    if (!(atk  == atk)  || atk  < 0.25f || atk  > 4.0f) atk  = 1.0f;
    const DWORD until = ttlMs ? (MsNow() + ttlMs) : 0;

    for (int i = 0; i < g_nOvr; ++i) {
        if (g_ovr[i].body != body) continue;
        g_ovr[i].loco = loco; g_ovr[i].atk = atk; g_ovr[i].until = until;
        return;
    }
    if (g_nOvr >= kMaxOverrides) return;
    g_ovr[g_nOvr].body = body;
    g_ovr[g_nOvr].loco = loco;
    g_ovr[g_nOvr].atk  = atk;
    g_ovr[g_nOvr].until = until;
    ++g_nOvr;
}

void ClearOverride(uintptr_t body)
{
    for (int i = 0; i < g_nOvr; ++i) {
        if (g_ovr[i].body != body) continue;
        for (int k = i + 1; k < g_nOvr; ++k) g_ovr[k - 1] = g_ovr[k];
        --g_nOvr;
        return;
    }
}

void ClearAllOverrides() { g_nOvr = 0; }
int  OverrideCount()     { return g_nOvr; }

bool GetFactors(uintptr_t body, float* loco, float* atk)
{
    if (!body) return false;
    float ol = 1.0f, oa = 1.0f;
    OverrideFor(body, &ol, &oa);
    if (loco) {
        float f = FactorFor(body) * ol;
        if (f < kFactorMin) f = kFactorMin;
        if (f > kFactorMax) f = kFactorMax;
        *loco = f;
    }
    if (atk) {
        float f = AnimFactorFor(body) * oa;
        if (f < kAnimMin) f = kAnimMin;
        if (f > kAnimMax) f = kAnimMax;
        *atk = f;
    }
    return true;
}

void AnimCost(uint32_t* lastUs, uint32_t* avgUs, uint32_t* maxUs)
{
    if (lastUs) *lastUs = g_animLastUs;
    if (avgUs)  *avgUs  = g_animAvgUs;
    if (maxUs)  *maxUs  = g_animMaxUs;
}

void SprintWatchTick()
{
    const uintptr_t b = g_lastSprintBody;
    if (!b) return;
    g_lastSprintBody = 0;

    // Кто это был. Сравниваем с телами партии и со списком актёров:
    // сам хук про роли ничего не знает и знать не должен.
    for (int i = 0; i < g_nParty; ++i) {
        if (g_party[i].ptr != b) continue;
        const char* r = g_party[i].role;
        if (r && r[0] == 'A') { ++g_sprint.player; return; }   // Arisen
        ++g_sprint.pawn;
        return;
    }
    for (int i = 0; i < g_nAct; ++i) {
        if (g_act[i].ptr != b) continue;
        if (KindIsEnemy(g_act[i].kind)) ++g_sprint.enemy;
        else                            ++g_sprint.other;
        return;
    }
    ++g_sprint.other;
}

SprintStats GetSprintStats() { return g_sprint; }
void        ResetSprintStats() { g_sprint.player = g_sprint.pawn = g_sprint.enemy = g_sprint.other = 0; }

int AnimListCount() { return g_animCount; }

bool AnimListAt(int i, uintptr_t* body, float* factor, char* kindOut, int cap)
{
    if (i < 0 || i >= g_animCount) return false;
    const AnimTrack& t = g_animTrack[i];
    if (body)   *body = t.body;
    if (factor) *factor = t.factor;
    if (kindOut && cap > 0) {
        int q = 0;
        for (; q < cap - 1 && t.kind[q]; ++q) kindOut[q] = t.kind[q];
        kindOut[q] = 0;
    }
    return true;
}

} // namespace Tempo
} // namespace Runtime
