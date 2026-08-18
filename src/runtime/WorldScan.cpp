// Runtime::World — обход списка актёров, классификация врагов и публикация
// WorldReport. Работает ВСЕГДА: детектор боя и доктрина Guardian зависят
// от этого тика, поэтому он не имеет права гейтиться флагом devtools.

#include "stdafx.h"
#include "RuntimeInternal.h"
#include "../ActMap.Generated.h"
#include "../CombatBus.h"

namespace Runtime {

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

// Безобидная живность: не атакует, не участвует в оценке опасности.
//
// uEm8000 — те самые «лагерные зайцы» из дампов. Их шестеро вокруг
// стоянки, и они прибавляли +6 к счётчику врагов на пустом месте.
// Важно: uEm8000 НЕ Григори (см. FIELD_MAP: «не маппить 0x61 -> Hare,
// сломаем Григори») — это отдельный вид с gid 0x61.
bool KindIsHarmless(const char* kind)
{
    if (!kind) return false;
    return strcmp(kind, "uEm8000") == 0     // лагерная живность
        || strcmp(kind, "uEm8600") == 0;    // Hare, заяц
}

// Враг: существо, представляющее угрозу.
//
// ВАЖНО: враги бывают не только uEm*. Бандиты и солдаты — это
// uHumanEnemy (29696 B), ветка uNpc -> uHumanEnemy. Пока фильтр смотрел
// только на "uEm", люди были невидимы и для счётчика, и для мутаций.
bool KindIsEnemy(const char* kind)
{
    if (!KindIsCreature(kind)) return false;
    return !KindIsHarmless(kind);
}

int KindCategory(const char* kind)
{
    // Tactical category from LIVE kind, not gid. 0x61 must never become boss.
    if (!kind) return -1;
    if (!strcmp(kind, "uEm0100") || !strcmp(kind, "uEm0101")) return 0;
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
    for (int i = 0; i < g_nAct && w.count < 32; ++i) {
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
        if (KindIsEnemy(g_act[i].kind)) {
            w.enemyCount++;
            if (p.inCombatAction) w.enemyCombatCount++;
        }
        else if (KindIsHarmless(g_act[i].kind)) w.critterCount++;
        if (g_act[i].kind && (!strcmp(g_act[i].kind, "uEm0100")
            || !strcmp(g_act[i].kind, "uEm0101")))
            w.goblinCount++;
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
// Проверка по подстроке "Die"/"Dead" покрывает все виды сразу: имена
// состояний единообразны у всех 35 видов (812 состояний в ActMap).
// ВНИМАНИЕ на форму имени. Первая версия проверяла только префикс сразу
// после "Act" — и пропускала 6 состояний из 812, где Die стоит в середине:
//     cEm5000ActDownDie      cEm8600ActFlyDie
//     cEm9100ActGroundDie    cEm0100ActDmgPoisonDie
// Поэтому ищем "Die"/"Dead" где угодно в имени состояния.
//
// Ложных срабатываний нет: слов с этими буквосочетаниями, кроме смерти,
// среди 812 состояний не встречается (проверено перебором таблицы).
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

void DumpActorsFrom(uintptr_t* seed, int ns)
{
    g_nAct = 0;
    if (!seed || ns <= 0) return;
    if (ns > 32) ns = 32;
    for (int s = 0; s < ns && g_nAct < 32; ++s) {
        uintptr_t p = seed[s];
        if (!p || !LooksHeap(p)) continue;
        int have = 0;
        for (int k = 0; k < g_nAct; ++k) if (g_act[k].ptr == p) { have = 1; break; }
        if (have) continue;
        uintptr_t vt = 0;
        if (!RdPtr((void*)p, &vt) || !LooksLikeVtable(vt)) continue;
        ActorDump& A = g_act[g_nAct];
        memset(&A, 0, sizeof(A));
        A.ptr = p;
        A.vt = vt;
        BYTE gidb = 0;
        if (Rd((void*)(p + 0x2D), &gidb, 1)) A.gid = gidb;
        Rd((void*)(p + 0x40), &A.x, 4);
        Rd((void*)(p + 0x44), &A.y, 4);
        Rd((void*)(p + 0x48), &A.z, 4);
        RdPtr((void*)(p + 0x0C), &A.next);
        RdPtr((void*)(p + 0x10), &A.prev);
        if (A.vt == kGoblinInst)
            A.subOk = RdPtr((void*)(p + 0x6150), &A.subVt);
        BYTE probe = 0;
        A.fat29 = Rd((void*)(p + 0x73BF), &probe, 1);
        { BYTE st = 0; if (Rd((void*)(p + 0x14), &st, 1)) A.st14 = st; }
        A.win5bOk = Rd((void*)(p + 0x5BD0), A.win5b, 16);
        A.win60Ok = Rd((void*)(p + 0x6000), A.win60, 64);
        ScanActSlot(A);

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
        if (A.vt == kGoblinInst)      A.kind = "uEm0100";
        else if (A.vt == kNpcInst)    A.kind = "uNpc";
        else if (A.vt == kEm8000Inst) A.kind = "uEm8000";
        else if (A.vt == kHareInst)   A.kind = "uEm8600";
        else {
            if (NameOfLiveObject(p, A.kindBuf, sizeof(A.kindBuf)) && A.kindBuf[0])
                A.kind = A.kindBuf;
            else if (A.vt == kUnk84Inst) A.kind = "u?84";
            else A.kind = "?";
        }
        g_nAct++;
        if (A.next && LooksHeap(A.next) && ns < 32) {
            int d = 0;
            for (int k = 0; k < ns; ++k) if (seed[k] == A.next) { d = 1; break; }
            if (!d) seed[ns++] = A.next;
        }
        if (A.prev && LooksHeap(A.prev) && ns < 32) {
            int d = 0;
            for (int k = 0; k < ns; ++k) if (seed[k] == A.prev) { d = 1; break; }
            if (!d) seed[ns++] = A.prev;
        }
    }
}

void RewalkActors()
{
    uintptr_t seed[32];
    int ns = 0;
    for (int i = 0; i < g_nAct && ns < 32; ++i)
        if (g_act[i].ptr) seed[ns++] = g_act[i].ptr;
    if (!ns) return;
    DumpActorsFrom(seed, ns);
    PublishWorldFromActors();
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
// у DTI: uEm* и uHumanEnemy — наши.
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
    if (nm[0] == 'u' && nm[1] == 'E' && nm[2] == 'm') return true;
    return strcmp(nm, "uHumanEnemy") == 0;
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
        if (k && !strcmp(k, kind)) return g_act[i].ptr;
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
            PartyPriorityProfileRestoreAll("world unload");
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
        }
        g_wasInWorld = false;
        return;
    }
    g_wasInWorld = true;

    // Build 56.2: Guardian doctrine anchor/pawn positions (throttled discover + cheap read).
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
    if (g_nAct)
        RewalkActors();
    // Always poll the hot ring. Empty: 8MB/tick. Have list: 4MB, merge new camps.
    // Build 69.4: адаптивный бюджет поллинга.
    //
    // Полный обход горячего кольца нужен, когда список пуст — ищем первый
    // лагерь и торопимся. Когда актёры уже есть, новых соседей подхватывает
    // RewalkActors по связному списку, а поллинг ищет лишь СОВСЕМ ДРУГОЙ
    // список. Это можно делать меньшими порциями: g_pollAddr сохраняется
    // между тиками, поэтому кольцо всё равно обходится целиком — просто
    // за большее число тиков. Ничего не пропускается, снижается только
    // скорость обхода.
    static int fruitless = 0;              // подряд тиков без новой находки
    uint32_t budget;
    if (!g_nAct) { budget = 0x800000u; fruitless = 0; }
    else          budget = 0x400000u >> (fruitless < 3 ? fruitless : 3);
    g_pollBudget = budget;

    uintptr_t s = PollSeedSlice(budget);
    if (!s) { if (fruitless < 3) ++fruitless; return; }
    int have = 0;
    for (int i = 0; i < g_nAct; ++i)
        if (g_act[i].ptr == s) { have = 1; break; }
    if (have) { if (fruitless < 3) ++fruitless; return; }
    fruitless = 0;                         // нашли новое — снова во весь опор
    uintptr_t seed[32];
    int ns = 0;
    seed[ns++] = s;
    for (int i = 0; i < g_nAct && ns < 32; ++i)
        if (g_act[i].ptr) seed[ns++] = g_act[i].ptr;
    DumpActorsFrom(seed, ns);
    PublishWorldFromActors();
}

} // namespace Runtime
