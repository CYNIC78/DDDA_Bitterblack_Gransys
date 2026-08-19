// GoapProbe — разведка планировщика пешки. См. GoapProbe.h.

#include "stdafx.h"
#include "GoapProbe.h"
#include "../runtime/Runtime.h"        // публичный API: MainPawnBody, FindChildByClass
#include "../runtime/RuntimeInternal.h" // Mem::Rd / RdPtr / NameOfLiveObject
#include "../TypeAtlas.Generated.h"     // размеры классов для границ обхода
#include "../CombatIntel.h"              // IsInCombat() - гистограмма делится на бой/покой

namespace GoapProbe {

static char      s_status[192] = "goap: idle";
static bool      s_watch = false;
static uintptr_t s_planner = 0;
static int32_t   s_lastCode = -12345;
static int       s_logged = 0;

// Размер тела пешки (uPlayer/uCmc-класс) — тот же, что использует разбор
// партии. Планировщик заметно больше, поэтому его обходим отдельно.
static const uint32_t kPawnBodyBytes = 0x5A10;
static const uint32_t kPlannerBytes  = 25264;   // sizeof cAIGoalPlanning

// Идентификаторы из файлов .gop: обычное следование и три варианта рывка.
struct DashId { uint32_t id; const char* name; };
static const DashId kDashIds[] = {
    { 0x01, "Follow (walk/jog)" },
    { 0xAD, "DashFollow" },
    { 0xB0, "DashFollowSt500" },
    { 0xB5, "PlEscape" },
    { 0xA6, "EscapeNotice1" },
    { 0xA7, "EscapeNotice2" },
};
static const int kNDashIds = (int)(sizeof(kDashIds) / sizeof(kDashIds[0]));

// Интересен ли класс для нашей задачи.
static bool Interesting(const char* nm)
{
    return strstr(nm, "Goal") || strstr(nm, "Plan") || strstr(nm, "Cmc")
        || strstr(nm, "Action") || strstr(nm, "Stamina") || strstr(nm, "AI")
        || strstr(nm, "Think");
}

// Перечислить живые подобъекты области памяти по именам классов.
// Возвращает, сколько нашли. Дедупликация обязательна: один и тот же
// объект попадается десятками ссылок.
static int MapChildren(uintptr_t base, uint32_t bytes, const char* title,
                       int maxLines)
{
    logFile << "  " << title << ":" << std::endl;
    static uintptr_t seen[256];
    int nSeen = 0, shown = 0;

    for (uint32_t off = 0; off + 4 <= bytes && shown < maxLines; off += 4) {
        uintptr_t cand = 0;
        if (!Runtime::Mem::RdPtr((void*)(base + off), &cand)) continue;
        if (!Runtime::Mem::LooksHeap(cand) || cand == base) continue;

        bool dup = false;
        for (int i = 0; i < nSeen; ++i) if (seen[i] == cand) { dup = true; break; }
        if (dup) continue;
        if (nSeen < 256) seen[nSeen++] = cand;

        char nm[48] = {};
        if (!Runtime::Mem::NameOfLiveObject(cand, nm, sizeof(nm)) || !nm[0]) continue;
        if (!Interesting(nm)) continue;

        char l[180];
        sprintf_s(l, "    +0x%04X -> 0x%08X  %s", (unsigned)off, (unsigned)cand, nm);
        logFile << l << std::endl;
        ++shown;
    }
    if (!shown) logFile << "    (nothing interesting)" << std::endl;
    return shown;
}

// Поиск идентификаторов рывка в области памяти.
static void FindDashIds(uintptr_t base, uint32_t bytes, const char* title)
{
    logFile << "  " << title << ":" << std::endl;
    int hits = 0;
    for (uint32_t off = 0; off + 4 <= bytes && hits < 40; off += 4) {
        uint32_t v = 0;
        if (!Runtime::Mem::Rd((void*)(base + off), &v, 4)) continue;
        for (int k = 0; k < kNDashIds; ++k) {
            if (v != kDashIds[k].id) continue;
            // Единица встречается повсюду, поэтому её показываем только
            // рядом с другими идентификаторами — иначе утонем в шуме.
            if (kDashIds[k].id == 0x01) break;
            char l[180];
            sprintf_s(l, "    +0x%04X = 0x%02X  %s",
                      (unsigned)off, v, kDashIds[k].name);
            logFile << l << std::endl;
            ++hits;
            break;
        }
    }
    if (!hits) logFile << "    (no dash ids here)" << std::endl;
}

// --- обход вглубь -----------------------------------------------------------
//
// Планировщик не висит прямым указателем в теле — первый дамп это
// показал. Значит нужен обход по уровням: тело -> его объекты -> их
// объекты. Границы каждого объекта берём из атласа типов, иначе обход
// вылезет в соседнюю кучу и начнёт показывать фантомы.
struct Node {
    uintptr_t addr;
    uint32_t  size;
    int       depth;
    char      path[160];
};

// СКЛЕЙКА ПУТИ БЕЗ sprintf_s — И ЭТО НЕ ПРИДИРКА.
//
// Вылет на глубоком обходе дал именно sprintf_s: при переполнении буфера
// он не обрезает строку, а вызывает обработчик неверного параметра,
// который по умолчанию ЗАВЕРШАЕТ ПРОЦЕСС. Путь растёт с каждым уровнем
// («body +0x0010 uPlayer +0x2E64 cAICtrl +0x0028 ...»), имена классов
// бывают под сорок символов — и на третьем уровне буфер кончился.
//
// Здесь ручная склейка: она молча обрезает и не может уронить игру.
static void PathCat(char* dst, int cap, const char* parent,
                    uint32_t off, const char* name)
{
    if (cap <= 0) return;
    int n = 0;
    for (const char* p = parent; *p && n < cap - 1; ++p) dst[n++] = *p;
    const char* hex = "0123456789ABCDEF";
    const char* pre = " +0x";
    for (const char* p = pre; *p && n < cap - 1; ++p) dst[n++] = *p;
    for (int shift = 12; shift >= 0 && n < cap - 1; shift -= 4)
        dst[n++] = hex[(off >> shift) & 0xF];
    if (n < cap - 1) dst[n++] = ' ';
    for (const char* p = name; *p && n < cap - 1; ++p) dst[n++] = *p;
    dst[n] = 0;
}

// Сосед по связному списку живых объектов — не потомок.
//
// В теле пешки на +0x0010 лежит указатель на тело Аризена: это next/prev
// того же списка, что у монстров. Обход уходил в другого актёра, тратил
// бюджет и лез в чужие подсистемы.
static bool IsForeignActor(uint32_t off, const char* nm)
{
    if (off == 0x0C || off == 0x10) return true;
    return strncmp(nm, "uPlayer", 7) == 0 || strncmp(nm, "uCmc", 4) == 0
        || strncmp(nm, "uEm", 3) == 0     || strncmp(nm, "uNpc", 4) == 0;
}
// ЁМКОСТЬ ОЧЕРЕДИ — ЧАСТЬ ИЗМЕРЕНИЯ.
//
// Фоновый обход отрапортовал «walk finished, 192 nodes, planner NOT
// found». Ровно 192 — это и был размер очереди. То есть обход не дошёл
// до конца графа, он упёрся в мой массив и молча остановился, а отчёт
// прозвучал как отрицательный результат.
//
// Теперь мест на порядок больше, а переполнение считается и попадает
// в отчёт: «граф больше очереди» и «планировщика нет» — разные ответы.
static const int kMaxNodes = 2048;
static Node s_q[kMaxNodes];
static int  s_nQ = 0;
static int  s_qOverflow = 0;

static uint32_t SizeOfClass(const char* nm)
{
    if (!nm || !nm[0]) return 0;
    const TypeAtlas::Info* i = TypeAtlas::FindByName(nm);
    return i ? i->size : 0;
}

static bool QueuePush(uintptr_t addr, uint32_t size, int depth, const char* path)
{
    for (int i = 0; i < s_nQ; ++i) if (s_q[i].addr == addr) return false;
    if (s_nQ >= kMaxNodes) { ++s_qOverflow; return false; }
    s_q[s_nQ].addr = addr;
    s_q[s_nQ].size = size;
    s_q[s_nQ].depth = depth;
    lstrcpynA(s_q[s_nQ].path, path, sizeof(s_q[s_nQ].path));
    ++s_nQ;
    return true;
}

// Обход вширь. Печатает интересные классы с путём и возвращает адрес
// первого найденного объекта класса want (0 = просто карта).
static uintptr_t Walk(uintptr_t root, uint32_t rootBytes, const char* want,
                      int maxDepth, int maxPrint)
{
    s_nQ = 0;
    s_qOverflow = 0;
    QueuePush(root, rootBytes, 0, "body");
    uintptr_t found = 0;
    int printed = 0;

    // БЮДЖЕТ. Обход без потолка — это сотни тысяч чтений в кадре
    // рендера: игра встаёт колом, а мы вдобавок перебираем чужую память
    // тем дольше, чем больше шансов на неприятность.
    int budget = 120000;

    for (int qi = 0; qi < s_nQ && budget > 0; ++qi) {
        const Node cur = s_q[qi];
        if (cur.depth >= maxDepth) continue;
        const uint32_t lim = (cur.size && cur.size < 0x8000) ? cur.size : 0x1000;

        // Сплошной обход только по проверенному региону: без этого
        // однажды попадём на сторожевую страницу чужого стека.
        if (!Runtime::Mem::RegionOk(cur.addr, lim)) continue;

        for (uint32_t off = 0; off + 4 <= lim && budget > 0; off += 4) {
            --budget;
            uintptr_t cand = 0;
            if (!Runtime::Mem::RdPtr((void*)(cur.addr + off), &cand)) continue;
            if (!Runtime::Mem::LooksHeap(cand) || cand == cur.addr) continue;

            char nm[48] = {};
            if (!Runtime::Mem::NameOfLiveObject(cand, nm, sizeof(nm)) || !nm[0]) continue;

            if (IsForeignActor(off, nm)) continue;

            char path[160];
            PathCat(path, sizeof(path), cur.path, off, nm);
            if (!QueuePush(cand, SizeOfClass(nm), cur.depth + 1, path)) continue;

            if (want && !strcmp(nm, want) && !found) {
                found = cand;
                logFile << "    FOUND " << want << " at 0x" << std::hex
                        << (unsigned)cand << std::dec << "   path: " << path
                        << std::endl;
            }
            if (Interesting(nm) && printed < maxPrint) {
                ++printed;
                // Через поток, а не через sprintf_s: путь заранее не
                // ограничен по длине, а форматирование в буфер на этом
                // и погорело.
                logFile << "    0x" << std::hex << (unsigned)cand << std::dec
                        << "  " << nm << "   " << path << std::endl;
            }
        }
    }
    if (budget <= 0)
        logFile << "    (read budget exhausted - walk stopped early)" << std::endl;
    if (s_qOverflow)
        logFile << "    (queue overflowed: " << s_qOverflow
                << " objects were not visited - result is INCOMPLETE)" << std::endl;
    return found;
}

// ПУТЬ РЕСУРСА.
//
// Первый дамп rPlStamina «числами» дал мусор — а это оказался вовсе не
// массив чисел: по смещению +0x08 лежит строка пути ресурса.
// Расшифровка байт из лога тестера дала `param\pl\stamina\PlStaminaMax`.
//
// Значит два с половиной десятка rPlStamina в теле пешки — это ИМЕНОВАННЫЕ
// правила, и их список сам по себе карта: если среди них найдётся что-то
// вроде PlStaminaDash, вопрос о пороге рывка закроется чтением имени.
static bool ResourcePath(uintptr_t obj, char* out, int cap)
{
    out[0] = 0;
    if (!obj) return false;
    char buf[96] = {};
    if (!Runtime::Mem::Rd((void*)(obj + 0x08), buf, sizeof(buf) - 1)) return false;
    int n = 0;
    for (; n < cap - 1 && buf[n]; ++n) {
        const unsigned char c = (unsigned char)buf[n];
        if (c < 0x20 || c > 0x7E) break;      // не текст — значит не путь
        out[n] = buf[n];
    }
    out[n] = 0;
    return n > 3;
}

// --- ТАБЛИЦА КОДОВ ПРИОРИТЕТА ------------------------------------------------
//
// Массив загруженных целей идёт от planner+0x08 с шагом 4. Номер слота —
// это код приоритета, тот самый, что лежит в planner+0x17C. Обоснование
// и пять сходящихся проверок — в GoapProbe.h.
static const int      kMaxCode        = 91;      // коды 0…90
static const uint32_t kGoalSlotBase   = 0x08;
static const uint32_t kPlanCtrlBase   = 0x190;
static const uint32_t kPlanCtrlStride = 0x110;

static char s_goalName[kMaxCode][40];
static bool s_tableReady = false;

static uint32_t SlotOfCode(int code) { return kGoalSlotBase + (uint32_t)code * 4; }
static uintptr_t PlanCtrlOf(uintptr_t planner, int code)
{
    return planner + kPlanCtrlBase + (uint32_t)code * kPlanCtrlStride;
}

// Хвост пути ресурса: "AI\Goap\Cmc\DashFollow" -> "DashFollow".
static const char* PathTail(const char* path)
{
    const char* tail = path;
    for (const char* p = path; *p; ++p) if (*p == '\\' || *p == '/') tail = p + 1;
    return tail;
}

static void BuildGoalTable(uintptr_t planner)
{
    memset(s_goalName, 0, sizeof(s_goalName));
    s_tableReady = false;
    if (!planner) return;
    for (int code = 0; code < kMaxCode; ++code) {
        uintptr_t res = 0;
        if (!Runtime::Mem::RdPtr((void*)(planner + SlotOfCode(code)), &res)) continue;
        if (!Runtime::Mem::LooksHeap(res)) continue;
        char nm[48] = {};
        if (!Runtime::Mem::NameOfLiveObject(res, nm, sizeof(nm))) continue;
        if (strcmp(nm, "rAIGoalPlanning") != 0) continue;
        char path[96] = {};
        if (!ResourcePath(res, path, sizeof(path))) continue;
        lstrcpynA(s_goalName[code], PathTail(path), sizeof(s_goalName[code]));
    }
    s_tableReady = true;
}

static const char* GoalOfCode(int code)
{
    if (code < 0 || code >= kMaxCode) return "(code out of range)";
    if (!s_tableReady) return "(table not built)";
    return s_goalName[code][0] ? s_goalName[code] : "(slot empty)";
}

// Похоже ли это на настоящее число с плавающей точкой? Нужно, чтобы
// не выдавать за 3.14 случайный указатель: денормали, бесконечности и
// абсурдные величины отсеиваем.
static bool FloatLooksReal(uint32_t bits, float* out)
{
    float f = 0.0f;
    memcpy(&f, &bits, 4);
    if (f != f) return false;                          // NaN
    const float a = (f < 0.0f) ? -f : f;
    if (a != 0.0f && (a < 1.0e-5f || a > 1.0e7f)) return false;
    *out = f;
    return true;
}

// Перечислить все ресурсы заданного класса с их путями.
static void DumpResourcePaths(uintptr_t body, uint32_t bytes, const char* cls)
{
    logFile << "  " << cls << " resources by path:" << std::endl;
    int shown = 0;
    for (uint32_t off = 0; off + 4 <= bytes && shown < 40; off += 4) {
        uintptr_t cand = 0;
        if (!Runtime::Mem::RdPtr((void*)(body + off), &cand)) continue;
        if (!Runtime::Mem::LooksHeap(cand)) continue;
        char nm[48] = {};
        if (!Runtime::Mem::NameOfLiveObject(cand, nm, sizeof(nm)) || strcmp(nm, cls) != 0) continue;
        char path[80] = {};
        if (!ResourcePath(cand, path, sizeof(path))) continue;
        logFile << "    body +0x" << std::hex << off << std::dec
                << "  " << path << std::endl;
        ++shown;
    }
    if (!shown) logFile << "    (none readable)" << std::endl;
}

// Тело живо и это действительно тело? Устаревший указатель после смены
// карты выглядит как обычное число, и обход по нему — путь к вылету.
static bool BodyAlive(uintptr_t body)
{
    if (!body || !Runtime::Mem::LooksHeap(body)) return false;
    if (!Runtime::Mem::RegionOk(body, 0x100)) return false;
    char nm[48] = {};
    if (!Runtime::Mem::NameOfLiveObject(body, nm, sizeof(nm)) || !nm[0]) return false;
    return (strncmp(nm, "uPl", 3) == 0) || (strncmp(nm, "uCmc", 4) == 0)
        || (strncmp(nm, "uCharacter", 10) == 0);
}

void DumpPawnPlanner()
{
    // Первое нажатие сказало «пешка не найдена», второе уронило игру:
    // между ними разбор партии успел подставить устаревший указатель.
    // Теперь проверяем и мир, и тело.
    if (!Runtime::Mem::InWorld()) {
        lstrcpynA(s_status, "goap: not in an active save", sizeof(s_status));
        logFile << "GoapProbe: " << s_status << std::endl;
        return;
    }
    const uintptr_t pawn = Runtime::MainPawnBody();
    if (!pawn) {
        lstrcpynA(s_status, "goap: main pawn not resolved", sizeof(s_status));
        logFile << "GoapProbe: " << s_status << std::endl;
        return;
    }
    if (!BodyAlive(pawn)) {
        sprintf_s(s_status, "goap: pawn body 0x%08X is stale - reopen the panel later",
                  (unsigned)pawn);
        logFile << "GoapProbe: " << s_status << std::endl;
        return;
    }

    logFile << "GoapProbe: === pawn 0x" << std::hex << pawn << std::dec
            << " ===" << std::endl;

    // 1) Карта подобъектов тела. Заодно ответ на вопрос, куда делась
    //    выносливость: cPlStamina прямым указателем не нашлась.
    MapChildren(pawn, kPawnBodyBytes, "pawn body children", 40);

    // 2) Планировщик — обходом вглубь: прямым указателем его в теле нет.
    logFile << "  deep walk (depth 3, class sizes from TypeAtlas):" << std::endl;
    uintptr_t planner = Walk(pawn, kPawnBodyBytes, "cAIGoalPlanning", 3, 60);
    if (!planner) {
        logFile << "  cAIGoalPlanning NOT reachable from the body within depth 3"
                << std::endl;
        // Не тупик: команды могут жить в cCmcInfo, он рядом и большой.
        uint32_t cmcOff = 0;
        const uintptr_t cmc = Runtime::FindChildByClass(pawn, kPawnBodyBytes,
                                                        "cCmcInfo", &cmcOff);
        if (cmc) {
            char l2[160];
            sprintf_s(l2, "  cCmcInfo at body +0x%04X -> 0x%08X (5728 B)",
                      (unsigned)cmcOff, (unsigned)cmc);
            logFile << l2 << std::endl;
            FindDashIds(cmc, 5728, "dash ids inside cCmcInfo");
        }
        // И заодно — что такое rPlStamina, которых в теле два десятка.
        uint32_t stOff = 0;
        const uintptr_t st = Runtime::FindChildByClass(pawn, kPawnBodyBytes,
                                                       "rPlStamina", &stOff);
        if (st) DumpResourcePaths(pawn, kPawnBodyBytes, "rPlStamina");
        FindDashIds(pawn, kPawnBodyBytes, "dash ids inside the pawn body");
        lstrcpynA(s_status, "goap: planner not reachable - see log for cCmcInfo",
                  sizeof(s_status));
        return;
    }
    s_planner = planner;

    char l[200];

    int32_t code = -1;
    if (Runtime::Mem::Rd((void*)(planner + 0x17C), &code, 4)) {
        sprintf_s(l, "  current priority code (+0x17C) = %d", code);
        logFile << l << std::endl;

        // Слот плана этого кода — по формуле из Build 40.
        if (code >= 0 && code < 90) {
            const uintptr_t ctrl = planner + 0x190 + (uint32_t)code * 0x110;
            char nm[48] = {};
            Runtime::Mem::NameOfLiveObject(ctrl, nm, sizeof(nm));
            sprintf_s(l, "  PlanCtrl(%d) = 0x%08X  %s",
                      code, (unsigned)ctrl, nm[0] ? nm : "(no DTI name)");
            logFile << l << std::endl;
        }
    }

    // 3) Карта подобъектов планировщика и поиск идентификаторов рывка.
    MapChildren(planner, kPlannerBytes, "planner children", 40);
    FindDashIds(planner, kPlannerBytes, "dash ids inside the planner");
    FindDashIds(pawn, kPawnBodyBytes, "dash ids inside the pawn body");

    logFile << "  Read-only. Nothing was written." << std::endl;
    sprintf_s(s_status, "goap: dumped planner 0x%08X (code %d) - see log",
              (unsigned)planner, code);
}

// Сравнение с Аризеном.
//
// В теле пешки нашлись два десятка rPlStamina (правила, 120 B) и НИ ОДНОГО
// cPlStamina (живое значение, 16 B). Если у Аризена всё наоборот или есть
// и то и другое — это прямое подтверждение наблюдения тестера: «пешки не
// тратят выносливость». Тогда порог St500 у рывка не проходится не
// потому, что стамины мало, а потому, что её у пешки попросту нет.
void DumpArisenCompare()
{
    const uintptr_t pl = Runtime::ArisenBody();
    if (pl && !BodyAlive(pl)) {
        lstrcpynA(s_status, "goap: Arisen body is stale", sizeof(s_status));
        logFile << "GoapProbe: " << s_status << std::endl;
        return;
    }
    if (!pl) {
        lstrcpynA(s_status, "goap: Arisen not resolved", sizeof(s_status));
        logFile << "GoapProbe: " << s_status << std::endl;
        return;
    }
    logFile << "GoapProbe: === Arisen 0x" << std::hex << pl << std::dec
            << " (compare with the pawn) ===" << std::endl;

    int nStaminaRule = 0, nStaminaLive = 0;
    for (uint32_t off = 0; off + 4 <= kPawnBodyBytes; off += 4) {
        uintptr_t cand = 0;
        if (!Runtime::Mem::RdPtr((void*)(pl + off), &cand)) continue;
        if (!Runtime::Mem::LooksHeap(cand)) continue;
        char nm[48] = {};
        if (!Runtime::Mem::NameOfLiveObject(cand, nm, sizeof(nm)) || !nm[0]) continue;
        if (!strcmp(nm, "rPlStamina")) ++nStaminaRule;
        else if (!strcmp(nm, "cPlStamina")) {
            ++nStaminaLive;
            char l[160];
            sprintf_s(l, "    cPlStamina at +0x%04X -> 0x%08X",
                      (unsigned)off, (unsigned)cand);
            logFile << l << std::endl;
            float f[4] = {};
            if (Runtime::Mem::Rd((void*)cand, f, sizeof(f))) {
                sprintf_s(l, "      values: %.2f %.2f %.2f %.2f", f[0], f[1], f[2], f[3]);
                logFile << l << std::endl;
            }
        }
    }
    char l[200];
    sprintf_s(l, "  Arisen: rPlStamina x%d, cPlStamina x%d", nStaminaRule, nStaminaLive);
    logFile << l << std::endl;
    logFile << "  (pawn had rPlStamina x25+, cPlStamina x0)" << std::endl;
    sprintf_s(s_status, "goap: Arisen has %d live stamina object(s)", nStaminaLive);
}

// --- ФОНОВЫЙ ОБХОД -----------------------------------------------------------
//
// Разовый обход упёрся в потолок: за один кадр глубже трёх уровней идти
// нельзя, а планировщика там нет. Решение — тот же обход, но по кусочку
// в кадр: игра не замечает, а глубина ограничена только терпением.
//
// Ровно так же устроен поллинг мира в продукте: бюджет на тик, состояние
// между тиками. Приём проверенный.
static bool     s_bgOn = false;
static int      s_bgNext = 0;          // следующий узел очереди
static uint32_t s_bgOff = 0;           // смещение внутри узла
static int      s_bgFound = 0;
static uintptr_t s_bgRoot = 0;
static const int kBgDepth = 5;

void StartBackgroundWalk()
{
    const uintptr_t pawn = Runtime::MainPawnBody();
    if (!BodyAlive(pawn)) {
        lstrcpynA(s_status, "goap: pawn body not ready", sizeof(s_status));
        return;
    }
    s_nQ = 0;
    s_qOverflow = 0;
    QueuePush(pawn, kPawnBodyBytes, 0, "body");
    s_bgNext = 0;
    s_bgOff = 0;
    s_bgFound = 0;
    s_bgRoot = pawn;
    s_bgOn = true;
    logFile << "GoapProbe: background walk started (depth " << kBgDepth
            << ", a slice per frame)" << std::endl;
}

void StopBackgroundWalk()
{
    s_bgOn = false;
    lstrcpynA(s_status, "goap: background walk stopped", sizeof(s_status));
}

bool BackgroundWalkActive() { return s_bgOn; }

static void BackgroundStep()
{
    if (!s_bgOn) return;
    if (!Runtime::Mem::InWorld()) { s_bgOn = false; return; }

    int budget = 3000;                  // ~0.1 мс на кадр, незаметно
    while (budget > 0 && s_bgNext < s_nQ) {
        Node& cur = s_q[s_bgNext];
        const uint32_t lim = (cur.size && cur.size < 0x8000) ? cur.size : 0x1000;

        if (cur.depth >= kBgDepth || !Runtime::Mem::RegionOk(cur.addr, lim)) {
            ++s_bgNext; s_bgOff = 0; continue;
        }
        if (s_bgOff + 4 > lim) { ++s_bgNext; s_bgOff = 0; continue; }

        const uint32_t off = s_bgOff;
        s_bgOff += 4;
        --budget;

        uintptr_t cand = 0;
        if (!Runtime::Mem::RdPtr((void*)(cur.addr + off), &cand)) continue;
        if (!Runtime::Mem::LooksHeap(cand) || cand == cur.addr) continue;
        char nm[48] = {};
        if (!Runtime::Mem::NameOfLiveObject(cand, nm, sizeof(nm)) || !nm[0]) continue;
        if (IsForeignActor(off, nm)) continue;

        char path[160];
        PathCat(path, sizeof(path), cur.path, off, nm);
        if (!QueuePush(cand, SizeOfClass(nm), cur.depth + 1, path)) continue;

        if (!strcmp(nm, "cAIGoalPlanning")) {
            s_planner = cand;
            ++s_bgFound;
            logFile << "GoapProbe: FOUND cAIGoalPlanning at 0x" << std::hex
                    << (unsigned)cand << std::dec << "  path: " << path << std::endl;
            s_bgOn = false;
            sprintf_s(s_status, "goap: planner found at 0x%08X", (unsigned)cand);
            return;
        }
        // Всё, что пахнет планированием, тоже в лог: если планировщика
        // нет, соседи подскажут, где искать.
        if (strstr(nm, "Goal") || strstr(nm, "Plan") || strstr(nm, "Think")
            || strstr(nm, "Goap")) {
            logFile << "GoapProbe: " << nm << " at 0x" << std::hex
                    << (unsigned)cand << std::dec << "  path: " << path << std::endl;
        }
    }

    if (s_bgNext >= s_nQ) {
        s_bgOn = false;
        if (s_qOverflow) {
            sprintf_s(s_status, "goap: walk INCOMPLETE - queue full (%d nodes, "
                                "%d more were dropped)", s_nQ, s_qOverflow);
        } else {
            sprintf_s(s_status, "goap: walk finished, %d nodes, planner %s",
                      s_nQ, s_planner ? "found" : "NOT found");
        }
        logFile << "GoapProbe: " << s_status << std::endl;
    } else {
        sprintf_s(s_status, "goap: walking... node %d/%d, depth %d",
                  s_bgNext, s_nQ, s_q[s_bgNext].depth);
    }
}

// --- ПОИСК ПЛАНИРОВЩИКА ПО ИМЕНИ, А НЕ ПО СМЕЩЕНИЮ ---------------------------
//
// Обход нашёл планировщик на `cAICtrl +0x0068`, и я это смещение
// захардкодил. В следующем же запуске он там не оказался — «planner not
// at cAICtrl+0x68 this time».
//
// Это ровно то, от чего предостерегает наш собственный EnemyTuner:
// «объекты ищутся ПО ИМЕНИ через DTI, а не по фиксированному оффсету;
// часть слотов тела переиспользуется под разные объекты в зависимости
// от состояния». Правило было записано ещё в августе — и я его нарушил,
// как только увидел красивое круглое смещение.
//
// Теперь: сам cAICtrl проверяется по имени класса, планировщик ищется
// перебором его слотов, найденное смещение печатается в лог. Если оно
// снова окажется другим — увидим это как факт, а не как отказ.
static uintptr_t ResolvePlanner(uintptr_t pawn, uint32_t* ctrlOffOut,
                                uint32_t* plannerOffOut)
{
    if (ctrlOffOut) *ctrlOffOut = 0;
    if (plannerOffOut) *plannerOffOut = 0;
    if (!pawn) return 0;

    // cAICtrl: сначала по известному месту, но С ПРОВЕРКОЙ ИМЕНИ,
    // иначе — поиском по телу.
    uintptr_t ctrl = 0;
    uint32_t ctrlOff = 0x2E64;
    if (Runtime::Mem::RdPtr((void*)(pawn + 0x2E64), &ctrl) && ctrl) {
        char nm[48] = {};
        if (!Runtime::Mem::NameOfLiveObject(ctrl, nm, sizeof(nm))
            || strcmp(nm, "cAICtrl") != 0) ctrl = 0;
    }
    if (!ctrl) {
        ctrl = Runtime::FindChildByClass(pawn, kPawnBodyBytes, "cAICtrl", &ctrlOff);
        if (ctrl) logFile << "GoapProbe: cAICtrl moved to body +0x" << std::hex
                          << ctrlOff << std::dec << std::endl;
    }
    if (!ctrl) return 0;
    if (ctrlOffOut) *ctrlOffOut = ctrlOff;

    // Планировщик — перебором слотов cAICtrl (704 B), по имени класса.
    uint32_t pOff = 0;
    uintptr_t planner = Runtime::FindChildByClass(ctrl, 704,
                                                  "cAIGoalPlanning", &pOff);

    // Последний рубеж: если и в cAICtrl его нет, идём обходом по телу.
    // Дорого, зато отвечает «нет» только когда его действительно нет.
    if (!planner) {
        logFile << "GoapProbe: not in cAICtrl either - falling back to a walk"
                << std::endl;
        planner = Walk(pawn, kPawnBodyBytes, "cAIGoalPlanning", 4, 0);
    }
    if (planner && plannerOffOut) *plannerOffOut = pOff;
    if (planner && pOff != 0x0068)
        logFile << "GoapProbe: planner is at cAICtrl +0x" << std::hex << pOff
                << std::dec << " this time (not 0x68)" << std::endl;
    return planner;
}

// --- ПУТИ ЗАГРУЖЕННЫХ ЦЕЛЕЙ --------------------------------------------------
//
// Главная находка обхода: планировщик держит МАССИВ ресурсов
// rAIGoalPlanning — это и есть загруженные файлы `.gop`. А у ресурсов, как
// выяснилось на rPlStamina, по смещению +0x08 лежит путь.
//
// Значит вопрос «есть ли у пешки в наборе рывок» решается чтением имён,
// без всякого реверса структуры целей: если среди загруженных окажется
// DashFollow — действие в наборе есть, и разбираться надо с условиями.
// Если нет — набор боевых целей его просто не включает.
void DumpPlannerGoals()
{
    if (!Runtime::Mem::InWorld()) {
        lstrcpynA(s_status, "goap: not in an active save", sizeof(s_status));
        return;
    }
    const uintptr_t pawn = Runtime::MainPawnBody();
    if (!BodyAlive(pawn)) {
        lstrcpynA(s_status, "goap: pawn body not ready", sizeof(s_status));
        return;
    }

    uint32_t ctrlOff = 0, plannerOff = 0;
    const uintptr_t planner = ResolvePlanner(pawn, &ctrlOff, &plannerOff);
    if (!planner) {
        lstrcpynA(s_status, "goap: planner not found inside cAICtrl",
                  sizeof(s_status));
        logFile << "GoapProbe: " << s_status << std::endl;
        return;
    }
    logFile << "GoapProbe: planner at cAICtrl +0x" << std::hex << plannerOff
            << std::dec << std::endl;

    logFile << "GoapProbe: === loaded goals of planner 0x" << std::hex << planner
            << std::dec << " ===" << std::endl;
    logFile << "  code = (slot - 8) / 4  -- the slot index IS the priority code"
            << std::endl;

    BuildGoalTable(planner);

    int n = 0, dash = 0, maxCode = -1;
    for (int code = 0; code < kMaxCode; ++code) {
        if (!s_goalName[code][0]) continue;
        ++n;
        maxCode = code;
        const bool isDash = (strstr(s_goalName[code], "Dash") != 0);
        if (isDash) ++dash;

        // Пометки известных кодов: они и есть доказательство раскладки.
        const char* proof = "";
        if (code == 1)  proof = "   [confirmed live, Build 40]";
        if (code == 15) proof = "   [confirmed, Guardian table]";
        if (code == 54) proof = "   [confirmed, Guardian MAIN lever]";
        if (code == 60) proof = "   [confirmed, Guardian table]";

        char l[200];
        sprintf_s(l, "    code %3d  slot +0x%03X  %-24s%s%s",
                  code, (unsigned)SlotOfCode(code), s_goalName[code],
                  isDash ? "   <<< DASH" : "", proof);
        logFile << l << std::endl;
    }

    logFile << "  total goal resources: " << n << ", dash-related: " << dash
            << ", highest code: " << maxCode << std::endl;

    // СТРУКТУРНАЯ ПРОВЕРКА РАСКЛАДКИ. Если старший загруженный код и
    // ёмкость массива PlanCtrl сходятся — нумерация та же самая.
    {
        const int slots = (int)((kPlannerBytes - kPlanCtrlBase) / kPlanCtrlStride);
        const uint32_t tail = kPlannerBytes - kPlanCtrlBase - (uint32_t)slots * kPlanCtrlStride;
        char l[220];
        sprintf_s(l, "  PlanCtrl array: base 0x%X, stride 0x%X, planner %u B"
                     " -> %d slots (codes 0..%d), %u B tail",
                  (unsigned)kPlanCtrlBase, (unsigned)kPlanCtrlStride,
                  (unsigned)kPlannerBytes, slots, slots - 1, (unsigned)tail);
        logFile << l << std::endl;
        sprintf_s(l, "  highest loaded goal code %d vs highest PlanCtrl slot %d -> %s",
                  maxCode, slots - 1,
                  (maxCode == slots - 1) ? "EXACT MATCH: same numbering"
                                         : "MISMATCH: the mapping needs another look");
        logFile << l << std::endl;

        sprintf_s(l, "  dash codes: DashFollow=%d PlanCtrl 0x%X | DashFollowSt500=%d PlanCtrl 0x%X",
                  84, (unsigned)(kPlanCtrlBase + 84 * kPlanCtrlStride),
                  85, (unsigned)(kPlanCtrlBase + 85 * kPlanCtrlStride));
        logFile << l << std::endl;
    }

    if (!dash)
        logFile << "  DashFollow is NOT among the loaded goals - the combat set"
                << " simply does not contain it." << std::endl;

    // Заодно приоритетное мышление: там лежит cmc.prt, который и решает,
    // какая цель когда берётся.
    uintptr_t ctrl = 0;
    Runtime::Mem::RdPtr((void*)(pawn + 0x2E64), &ctrl);
    uintptr_t think = ctrl ? Runtime::FindChildByClass(ctrl, 704, "cAIPriorityThink", 0) : 0;
    if (think) {
        logFile << "  cAIPriorityThink at 0x" << std::hex << think << std::dec
                << " - resources inside:" << std::endl;
        for (uint32_t off = 0; off + 4 <= 1020; off += 4) {
            uintptr_t res = 0;
            if (!Runtime::Mem::RdPtr((void*)(think + off), &res)) continue;
            if (!Runtime::Mem::LooksHeap(res)) continue;
            char nm[48] = {};
            if (!Runtime::Mem::NameOfLiveObject(res, nm, sizeof(nm)) || nm[0] != 'r') continue;
            char path[96] = {};
            if (!ResourcePath(res, path, sizeof(path))) continue;
            logFile << "    +0x" << std::hex << off << std::dec << "  " << nm
                    << "  " << path << std::endl;
        }
    }

    sprintf_s(s_status, "goap: %d goal resources, %d dash-related - see log", n, dash);
}

// --- PLANCTRL A/B ------------------------------------------------------------
//
// ЭТО ТОТ ДИФФ, КОТОРЫМ ГЛУБОКИЙ ОБХОД ДОЛЖЕН БЫЛ БЫТЬ.
//
// Структурный дифф ресурсов Follow и DashFollow дал 80 расхождений и ноль
// кандидатов. Причина видна в самом логе: обход ушёл в указатель +0x70 и
// начал сравнивать блоки вида {ptr, 0x0EEA0000, ptr, ptr, ..., 0x5000000B,
// 0x46000002}, повторяющиеся с шагом 0x50, где 0x0EEA0000/0x0EEB0000 —
// базы 64-килобайтных сегментов кучи. Это заголовки аллокатора, а не
// содержимое целей. «У A указатели, у B нули» означало только то, что в
// сегменте A лежало больше блоков.
//
// Сравнивать два разобранных ресурса по одинаковым смещениям изначально
// нельзя: у каждого своя область разбора и своя раскладка.
//
// PlanCtrl — другое дело. Это элементы ОДНОГО массива внутри планировщика,
// одного типа и одного размера: смещение k в PlanCtrl(1) и в PlanCtrl(84)
// значит одно и то же поле. Здесь дифф осмыслен по построению.
static void DumpOnePlanCtrl(uintptr_t planner, int code)
{
    const uintptr_t ctrl = PlanCtrlOf(planner, code);
    char l[220];
    sprintf_s(l, "  PlanCtrl(%d) \"%s\" at 0x%08X:",
              code, GoalOfCode(code), (unsigned)ctrl);
    logFile << l << std::endl;

    if (!Runtime::Mem::RegionOk(ctrl, kPlanCtrlStride)) {
        logFile << "    (region not readable)" << std::endl;
        return;
    }
    uint32_t V[kPlanCtrlStride / 4];
    if (!Runtime::Mem::Rd((void*)ctrl, V, kPlanCtrlStride)) {
        logFile << "    (read failed)" << std::endl;
        return;
    }
    for (uint32_t k = 0; k < kPlanCtrlStride / 4; k += 4) {
        sprintf_s(l, "    +0x%03X  %08X %08X %08X %08X",
                  (unsigned)(k * 4), V[k], V[k + 1], V[k + 2], V[k + 3]);
        logFile << l << std::endl;
    }
}

void DumpPlanCtrlAB()
{
    if (!Runtime::Mem::InWorld()) {
        lstrcpynA(s_status, "goap: not in an active save", sizeof(s_status));
        return;
    }
    const uintptr_t pawn = Runtime::MainPawnBody();
    if (!BodyAlive(pawn)) {
        lstrcpynA(s_status, "goap: pawn body not ready", sizeof(s_status));
        return;
    }
    const uintptr_t planner = ResolvePlanner(pawn, 0, 0);
    if (!planner) {
        lstrcpynA(s_status, "goap: planner not found", sizeof(s_status));
        logFile << "GoapProbe: " << s_status << std::endl;
        return;
    }
    BuildGoalTable(planner);

    int32_t cur = -1;
    Runtime::Mem::Rd((void*)(planner + 0x17C), &cur, 4);

    logFile << "GoapProbe: === PlanCtrl A/B: Follow(1) vs DashFollow(84) ==="
            << std::endl;
    {
        char l[200];
        sprintf_s(l, "  planner 0x%08X, current code %d \"%s\"",
                  (unsigned)planner, cur, GoalOfCode(cur));
        logFile << l << std::endl;
    }

    // Сначала сырьё: два блока целиком. Раскладку PlanCtrl мы ещё не
    // знаем, и сырые байты — единственный способ её увидеть.
    DumpOnePlanCtrl(planner, 1);
    DumpOnePlanCtrl(planner, 84);

    // Теперь построчный дифф. Одинаковый тип, одинаковые смещения —
    // расхождение здесь значит «у этих двух целей разное состояние
    // ровно в этом поле».
    const uintptr_t ca = PlanCtrlOf(planner, 1);
    const uintptr_t cb = PlanCtrlOf(planner, 84);
    if (!Runtime::Mem::RegionOk(ca, kPlanCtrlStride)
        || !Runtime::Mem::RegionOk(cb, kPlanCtrlStride)) {
        lstrcpynA(s_status, "goap: PlanCtrl region unreadable", sizeof(s_status));
        return;
    }
    uint32_t A[kPlanCtrlStride / 4], B[kPlanCtrlStride / 4];
    if (!Runtime::Mem::Rd((void*)ca, A, kPlanCtrlStride)
        || !Runtime::Mem::Rd((void*)cb, B, kPlanCtrlStride)) {
        lstrcpynA(s_status, "goap: PlanCtrl read failed", sizeof(s_status));
        return;
    }

    logFile << "  --- differing fields (same type, same offsets: comparable) ---"
            << std::endl;
    int diffs = 0;
    for (uint32_t k = 0; k < kPlanCtrlStride / 4; ++k) {
        if (A[k] == B[k]) continue;
        ++diffs;
        char l[220];
        sprintf_s(l, "    +0x%03X  Follow=0x%08X  DashFollow=0x%08X",
                  (unsigned)(k * 4), A[k], B[k]);
        logFile << l;
        float fa = 0.0f, fb = 0.0f;
        const bool oa = FloatLooksReal(A[k], &fa), ob = FloatLooksReal(B[k], &fb);
        if (oa || ob) {
            char f[64];
            logFile << "   float:";
            if (oa) { sprintf_s(f, " %.4f", fa); logFile << f; } else logFile << " -";
            logFile << " /";
            if (ob) { sprintf_s(f, " %.4f", fb); logFile << f; } else logFile << " -";
        }
        // Маленькие целые в PlanCtrl — самое вероятное место для
        // «включено / кулдаун / счётчик попыток».
        if (A[k] < 0x10000 && B[k] < 0x10000)
            logFile << "   [small ints: " << (unsigned)A[k] << " / "
                    << (unsigned)B[k] << "]";
        logFile << std::endl;
    }
    if (!diffs)
        logFile << "    (identical - the two goals are in the same plan state)"
                << std::endl;

    logFile << "  differing dwords: " << diffs << " of "
            << (kPlanCtrlStride / 4) << ". Read-only." << std::endl;
    sprintf_s(s_status, "goap: PlanCtrl A/B - %d of %u dwords differ",
              diffs, (unsigned)(kPlanCtrlStride / 4));
}

// --- ГИСТОГРАММА КОДОВ -------------------------------------------------------
//
// Слежение за +0x17C логировало КАЖДУЮ смену кода. В бою это сотни строк
// и никакого ответа на вопрос «а 84 вообще бывает».
//
// Гистограмма отвечает числом: сколько кадров планировщик простоял в
// каждом коде, раздельно в бою и в покое. Это же и приёмочный прибор
// будущей правки — «до: код 84 ноль кадров, после: столько-то».
static uint32_t s_hist[kMaxCode];
static uint32_t s_histCombat[kMaxCode];
static uint32_t s_enter[kMaxCode];          // сколько раз ВОШЛИ в код
static uint32_t s_samples = 0, s_samplesCombat = 0;
static DWORD    s_histStart = 0;

void ResetCodeHistogram()
{
    memset(s_hist, 0, sizeof(s_hist));
    memset(s_histCombat, 0, sizeof(s_histCombat));
    memset(s_enter, 0, sizeof(s_enter));
    s_samples = 0;
    s_samplesCombat = 0;
    s_histStart = MsNow();
    lstrcpynA(s_status, "goap: histogram reset", sizeof(s_status));
}

void DumpCodeHistogram()
{
    logFile << "GoapProbe: === priority code histogram ===" << std::endl;
    if (!s_samples) {
        logFile << "  no samples - turn 'code watch' ON first, then play."
                << std::endl;
        lstrcpynA(s_status, "goap: no samples yet - enable code watch",
                  sizeof(s_status));
        return;
    }
    const DWORD secs = s_histStart ? (MsNow() - s_histStart) / 1000 : 0;
    char l[220];
    sprintf_s(l, "  %u samples over %u s, of them %u in combat",
              s_samples, (unsigned)secs, s_samplesCombat);
    logFile << l << std::endl;
    logFile << "  code  goal                      frames   in combat   entries"
            << std::endl;

    // Сортировка выбором: массив 91 элемент, лишний код тут дороже.
    bool done[kMaxCode];
    memset(done, 0, sizeof(done));
    for (int rank = 0; rank < kMaxCode; ++rank) {
        int best = -1;
        for (int c = 0; c < kMaxCode; ++c) {
            if (done[c] || !s_hist[c]) continue;
            if (best < 0 || s_hist[c] > s_hist[best]) best = c;
        }
        if (best < 0) break;
        done[best] = true;
        sprintf_s(l, "   %3d  %-24s %6u  %9u  %8u",
                  best, GoalOfCode(best), s_hist[best], s_histCombat[best],
                  s_enter[best]);
        logFile << l << std::endl;
    }

    // Главный вопрос трека — отдельной строкой, чтобы не искать глазами.
    const uint32_t dashFrames = s_hist[84] + s_hist[85];
    const uint32_t dashCombat = s_histCombat[84] + s_histCombat[85];
    sprintf_s(l, "  DASH CODES 84/85: %u frames total, %u of them in combat, "
                 "%u entries -> %s",
              dashFrames, dashCombat, s_enter[84] + s_enter[85],
              dashFrames ? "the planner DOES pick dash sometimes"
                         : "the planner NEVER picks dash");
    logFile << l << std::endl;
    sprintf_s(s_status, "goap: hist %u samples, dash 84/85 = %u frames (%u in combat)",
              s_samples, dashFrames, dashCombat);
}


// --- ДИФФ ДВУХ ЦЕЛЕЙ (историческое) ------------------------------------------
//
// Ключ к правке. На диске `Follow.gop` и `DashFollow.gop` побайтово
// одинаковы, кроме ОДНОГО числа — идентификатора моторной команды
// (1 против 0xAD). Обе цели загружены в планировщик как ресурсы
// rAIGoalPlanning (160 B каждый).
//
// Значит достаточно сравнить два загруженных ресурса и найти место, где
// они расходятся. Это и будет поле, которым цель выбирает команду —
// четыре байта, которые превращают «идти» в «рвануть».
//
// Только чтение. Ничего не пишем.
static uintptr_t FindGoalByPath(uintptr_t planner, const char* want, char* pathOut, int cap)
{
    for (uint32_t off = 0; off + 4 <= 0x400; off += 4) {
        uintptr_t res = 0;
        if (!Runtime::Mem::RdPtr((void*)(planner + off), &res)) continue;
        if (!Runtime::Mem::LooksHeap(res)) continue;
        char nm[48] = {};
        if (!Runtime::Mem::NameOfLiveObject(res, nm, sizeof(nm))) continue;
        if (strcmp(nm, "rAIGoalPlanning") != 0) continue;
        char path[96] = {};
        if (!ResourcePath(res, path, sizeof(path))) continue;

        // Сравниваем ХВОСТ пути: "Follow" не должен совпасть с "DashFollow".
        const char* tail = path;
        for (const char* p = path; *p; ++p) if (*p == '\\' || *p == '/') tail = p + 1;
        if (strcmp(tail, want) != 0) continue;
        if (pathOut && cap > 0) lstrcpynA(pathOut, path, cap);
        return res;
    }
    return 0;
}

void DumpGoalDiff()
{
    if (!Runtime::Mem::InWorld()) return;
    const uintptr_t pawn = Runtime::MainPawnBody();
    if (!BodyAlive(pawn)) {
        lstrcpynA(s_status, "goap: pawn body not ready", sizeof(s_status));
        return;
    }
    const uintptr_t planner = ResolvePlanner(pawn, 0, 0);
    if (!planner) {
        lstrcpynA(s_status, "goap: planner not found inside cAICtrl", sizeof(s_status));
        logFile << "GoapProbe: " << s_status << std::endl;
        return;
    }

    char pa[96] = {}, pb[96] = {};
    const uintptr_t a = FindGoalByPath(planner, "Follow", pa, sizeof(pa));
    const uintptr_t b = FindGoalByPath(planner, "DashFollow", pb, sizeof(pb));
    if (!a || !b) {
        sprintf_s(s_status, "goap: Follow %s, DashFollow %s",
                  a ? "ok" : "MISSING", b ? "ok" : "MISSING");
        logFile << "GoapProbe: " << s_status << std::endl;
        return;
    }

    logFile << "GoapProbe: === diff Follow vs DashFollow ===" << std::endl;
    logFile << "  A 0x" << std::hex << a << " " << pa << std::endl;
    logFile << "  B 0x" << b << " " << pb << std::dec << std::endl;

    uint32_t A[40] = {}, B[40] = {};
    const uint32_t bytes = 160;               // sizeof rAIGoalPlanning
    if (!Runtime::Mem::Rd((void*)a, A, bytes) || !Runtime::Mem::Rd((void*)b, B, bytes)) {
        lstrcpynA(s_status, "goap: resources unreadable", sizeof(s_status));
        return;
    }

    int diffs = 0;
    for (uint32_t i = 0; i < bytes / 4; ++i) {
        if (A[i] == B[i]) continue;
        ++diffs;
        // Указатель или число? Если оба ведут на живые объекты — покажем
        // их классы: расхождение может быть не в числе, а в структуре.
        char na[48] = {}, nb[48] = {};
        if (Runtime::Mem::LooksHeap(A[i])) Runtime::Mem::NameOfLiveObject((uintptr_t)A[i], na, sizeof(na));
        if (Runtime::Mem::LooksHeap(B[i])) Runtime::Mem::NameOfLiveObject((uintptr_t)B[i], nb, sizeof(nb));
        logFile << "    +0x" << std::hex << (i * 4) << "  A=0x" << A[i]
                << "  B=0x" << B[i] << std::dec;
        if (na[0] || nb[0]) logFile << "   (" << (na[0] ? na : "?") << " / "
                                    << (nb[0] ? nb : "?") << ")";
        logFile << std::endl;
    }
    if (!diffs) logFile << "    (identical - the difference lives deeper)" << std::endl;

    // Ресурс маленький, содержимое целей наверняка за указателем. Если
    // расхождение только в одном указателе — сравним и то, на что он
    // показывает.
    for (uint32_t i = 0; i < bytes / 4; ++i) {
        if (A[i] == B[i]) continue;
        if (!Runtime::Mem::LooksHeap(A[i]) || !Runtime::Mem::LooksHeap(B[i])) continue;
        uint32_t sa[32] = {}, sb[32] = {};
        if (!Runtime::Mem::Rd((void*)(uintptr_t)A[i], sa, sizeof(sa))) continue;
        if (!Runtime::Mem::Rd((void*)(uintptr_t)B[i], sb, sizeof(sb))) continue;
        logFile << "    -- inside the differing pointer at +0x" << std::hex
                << (i * 4) << std::dec << ":" << std::endl;
        for (int k = 0; k < 32; ++k) {
            if (sa[k] == sb[k]) continue;
            logFile << "       +0x" << std::hex << (k * 4) << "  A=0x" << sa[k]
                    << "  B=0x" << sb[k] << std::dec;
            if (sb[k] == 0xAD || sb[k] == 0xB0) logFile << "   <<< DASH COMMAND ID";
            logFile << std::endl;
        }
    }

    sprintf_s(s_status, "goap: %d differing dwords between Follow and DashFollow", diffs);
    logFile << "  Read-only." << std::endl;
}

// --- РЕКУРСИВНЫЙ ДИФФ СТРУКТУР -----------------------------------------------
//
// Плоский дифф двух ресурсов показал главное: содержимое целей лежит НЕ
// в самом ресурсе, а за указателями, и у каждой цели своя область
// разбора (Follow в 0x0ED7xxxx, DashFollow в 0x0ED8xxxx). Смещения внутри
// областей разные, поэтому «сравнить два блока подряд» бессмысленно.
//
// Зато роли полей совпадают: если у обоих в одном и том же месте лежит
// указатель — это один и тот же по смыслу подобъект, и сравнивать надо
// ИХ содержимое. Отсюда обход парами: идём по двум структурам синхронно,
// расходящиеся ЧИСЛА показываем, расходящиеся УКАЗАТЕЛИ раскрываем.
//
// Файлы на диске различаются одним числом. Значит и здесь, спустившись
// достаточно глубоко, мы обязаны увидеть ровно одно осмысленное
// расхождение — идентификатор команды.
struct DiffPair { uintptr_t a, b; int depth; char path[96]; };
static DiffPair s_dp[64];
static int      s_nDp = 0;

static void DiffPush(uintptr_t a, uintptr_t b, int depth, const char* path)
{
    if (s_nDp >= 64) return;
    for (int i = 0; i < s_nDp; ++i)
        if (s_dp[i].a == a && s_dp[i].b == b) return;
    s_dp[s_nDp].a = a; s_dp[s_nDp].b = b; s_dp[s_nDp].depth = depth;
    lstrcpynA(s_dp[s_nDp].path, path, sizeof(s_dp[s_nDp].path));
    ++s_nDp;
}

void DumpGoalDeepDiff()
{
    if (!Runtime::Mem::InWorld()) return;
    const uintptr_t pawn = Runtime::MainPawnBody();
    if (!BodyAlive(pawn)) { lstrcpynA(s_status, "goap: pawn not ready", sizeof(s_status)); return; }
    const uintptr_t planner = ResolvePlanner(pawn, 0, 0);
    if (!planner) {
        lstrcpynA(s_status, "goap: planner not found inside cAICtrl", sizeof(s_status));
        logFile << "GoapProbe: " << s_status << std::endl;
        return;
    }

    const uintptr_t a = FindGoalByPath(planner, "Follow", 0, 0);
    const uintptr_t b = FindGoalByPath(planner, "DashFollow", 0, 0);
    if (!a || !b) { lstrcpynA(s_status, "goap: goals not found", sizeof(s_status)); return; }

    logFile << "GoapProbe: === deep diff Follow vs DashFollow ===" << std::endl;
    s_nDp = 0;
    DiffPush(a, b, 0, "res");

    const int kMaxDepth = 4;
    const uint32_t kNodeBytes = 128;
    int lines = 0, hits = 0, skipped = 0;

    for (int i = 0; i < s_nDp && lines < 80; ++i) {
        const DiffPair cur = s_dp[i];
        if (cur.depth >= kMaxDepth) continue;
        if (!Runtime::Mem::RegionOk(cur.a, kNodeBytes)) continue;
        if (!Runtime::Mem::RegionOk(cur.b, kNodeBytes)) continue;

        uint32_t A[kNodeBytes / 4], B[kNodeBytes / 4];
        if (!Runtime::Mem::Rd((void*)cur.a, A, kNodeBytes)) continue;
        if (!Runtime::Mem::Rd((void*)cur.b, B, kNodeBytes)) continue;

        for (uint32_t k = 0; k < kNodeBytes / 4 && lines < 80; ++k) {
            if (A[k] == B[k]) continue;
            const bool pa = Runtime::Mem::LooksHeap((uintptr_t)A[k]);
            const bool pb = Runtime::Mem::LooksHeap((uintptr_t)B[k]);

            if (pa && pb) {
                // ЗДЕСЬ БЫЛА ДЫРА, ИЗ-ЗА КОТОРОЙ ОБХОД ДАЛ 80 СТРОК МУСОРА.
                //
                // Раньше любая пара указателей раскрывалась. На первом же
                // шаге обход попал в +0x70 — а там лежит не содержимое
                // цели, а служебные блоки аллокатора: повторяющиеся с
                // шагом 0x50 записи {ptr, 0x0EEA0000, ptr, ptr, ...,
                // 0x5000000B, 0x46000002}, где 0x0EEA0000 — база
                // 64-килобайтного сегмента кучи. Дальше обход честно
                // сравнивал два РАЗНЫХ сегмента и печатал их различия.
                //
                // Правило: спускаемся в пару, только если обе стороны —
                // объекты ОДНОГО класса по DTI. Безымянную пару пускаем
                // ровно на один уровень от самого ресурса, дальше нет.
                char ca[48] = {}, cb[48] = {};
                const bool na = Runtime::Mem::NameOfLiveObject((uintptr_t)A[k], ca, sizeof(ca)) && ca[0];
                const bool nb = Runtime::Mem::NameOfLiveObject((uintptr_t)B[k], cb, sizeof(cb)) && cb[0];
                bool ok;
                if (na && nb)       ok = (strcmp(ca, cb) == 0);
                else if (!na && !nb) ok = (cur.depth == 0);
                else                 ok = false;

                if (!ok) {
                    if (skipped < 12) {
                        ++skipped;
                        logFile << "    (skip " << cur.path << " +0x" << std::hex
                                << (k * 4) << std::dec << ": classes "
                                << (na ? ca : "(unnamed)") << " / "
                                << (nb ? cb : "(unnamed)")
                                << " - not a comparable pair)" << std::endl;
                    }
                    continue;
                }
                char np[96];
                PathCat(np, sizeof(np), cur.path, k * 4, na ? ca : "->");
                DiffPush((uintptr_t)A[k], (uintptr_t)B[k], cur.depth + 1, np);
                continue;                       // адреса разные по природе
            }
            // Расхождение в ДАННЫХ — вот это интересно.
            ++lines;
            const bool isDash = (B[k] == 0xAD || B[k] == 0xB0 || A[k] == 0x01);
            if (isDash) ++hits;
            logFile << "    " << cur.path << " +0x" << std::hex << (k * 4)
                    << "  A=0x" << A[k] << "  B=0x" << B[k] << std::dec;
            if (B[k] == 0xAD) logFile << "   <<< DashFollow command id";
            else if (B[k] == 0xB0) logFile << "   <<< DashFollowSt500 id";
            else if (A[k] == 0x01 && B[k] > 0x80 && B[k] < 0x100)
                logFile << "   <<< 1 -> command id, looks like THE field";
            logFile << std::endl;
        }
    }

    logFile << "  pairs walked: " << s_nDp << ", data differences: " << lines
            << ", command-id candidates: " << hits
            << ", pairs skipped as incomparable: " << skipped << std::endl;
    logFile << "  NOTE: superseded by PlanCtrl A/B - the goal slot index is the"
            << " priority code, so the .gop command id is no longer needed."
            << std::endl;
    sprintf_s(s_status, "goap: deep diff - %d data diffs, %d id candidates",
              lines, hits);
}

void ToggleCodeWatch()
{
    s_watch = !s_watch;
    s_lastCode = -12345;
    s_logged = 0;
    if (s_watch) {
        // Включение = начало нового замера. Иначе гистограмма склеит
        // два разных сеанса и её числа будут ни о чём.
        ResetCodeHistogram();
        if (!s_planner) {
            const uintptr_t pawn = Runtime::MainPawnBody();
            if (BodyAlive(pawn)) s_planner = ResolvePlanner(pawn, 0, 0);
        }
        if (s_planner) BuildGoalTable(s_planner);
    }
    logFile << "GoapProbe: code watch " << (s_watch ? "ON (histogram reset)" : "off")
            << std::endl;
}

bool CodeWatchActive() { return s_watch; }

void Tick()
{
    BackgroundStep();
    if (!s_watch) return;
    // Выгрузка мира делает все указатели недействительными.
    if (!Runtime::Mem::InWorld()) { s_planner = 0; s_tableReady = false; return; }
    if (!s_planner) {
        const uintptr_t pawn = Runtime::MainPawnBody();
        if (!BodyAlive(pawn)) return;
        // Ищем не чаще раза в две секунды: обход дорогой, а в кадре
        // рендера дорогое повторять нельзя.
        static DWORD lastTry = 0;
        const DWORD now = MsNow();
        if (lastTry && now - lastTry < 2000) return;
        lastTry = now;
        s_planner = ResolvePlanner(pawn, 0, 0);
        if (!s_planner) return;
        BuildGoalTable(s_planner);
    }

    int32_t code = -1;
    if (!Runtime::Mem::Rd((void*)(s_planner + 0x17C), &code, 4)) return;

    // Таблицу имён строим один раз на найденный планировщик: без неё
    // гистограмма будет столбиком голых чисел.
    if (!s_tableReady) BuildGoalTable(s_planner);

    // СБОР ГИСТОГРАММЫ. Одно чтение на кадр, всё остальное — счётчики.
    // Бой берём у продуктового детектора: «враг в списке» уже один раз
    // подвёл нас в DashWatch, повторять эту ошибку нельзя.
    if (code >= 0 && code < kMaxCode) {
        if (!s_histStart) s_histStart = MsNow();
        const bool fight = IsInCombat();
        ++s_samples;
        ++s_hist[code];
        if (fight) { ++s_samplesCombat; ++s_histCombat[code]; }
        if (code != s_lastCode) ++s_enter[code];
    }

    if (code == s_lastCode) return;
    s_lastCode = code;

    // Что пешка делает в этот момент — чтобы код был не абстрактным
    // числом, а «код 1 при cPlActRun».
    char act[48] = {};
    const uintptr_t pawn = Runtime::MainPawnBody();
    if (pawn) Runtime::ReadLiveAct(pawn, act, sizeof(act));

    sprintf_s(s_status, "goap: code %d \"%s\" (%s)", code, GoalOfCode(code),
              act[0] ? act : "?");
    if (s_logged < 60) {
        ++s_logged;
        logFile << "GoapProbe: " << s_status << std::endl;
    }
}

const char* Status() { return s_status; }

} // namespace GoapProbe
