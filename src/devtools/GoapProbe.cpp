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

// Интересен ли класс для нашей задачи: обход печатает только то, что
// относится к планированию, иначе лог тонет в служебных объектах.
static bool Interesting(const char* nm)
{
    return strstr(nm, "Goal") || strstr(nm, "Plan") || strstr(nm, "Cmc")
        || strstr(nm, "Action") || strstr(nm, "AI") || strstr(nm, "Think");
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

// --- КТО МОЖЕТ ВЫБРАТЬ КОД 84 -----------------------------------------------
//
// Замер закрыл развилку протокола: коды рывка 84/85 не выбираются НИКОГДА
// (0 кадров, 0 входов за 118 с, из них 2618 сэмплов в бою). Дальше
// вопрос переезжает на уровень выше — в приоритетное мышление.
//
// Раскладка подтверждена и записана в SOURCE_OF_TRUTH:
//   cAIPriorityThink + 0x38 + slot*0x14 — дескриптор «ведра» (48 штук):
//       +0x00 vtable cArray, +0x04 count, +0x08 capacity, +0x0C flags,
//       +0x10 cPrioParam** — только первые count указателей действительны;
//   cPrioParam: +0x04 Sensor, +0x08 Code, +0x0C Category, +0x10 ObjectID,
//       +0x14 Extra, +0x18.. cArray правил личности (cCodeParam),
//       +0x2C.. cArray правил порядка (cOrderValue).
//
// СВОЙ СЧЁТ У ПРИБОРА. Канон говорит: строк ровно 85. Дамп считает
// найденные сам и печатает сравнение — если чисел не 85, врёт прибор,
// а не игра. Это правило стоило нам трёх итераций, и оно теперь
// обязательное.
static void DumpOnePrioRow(uintptr_t row, int slot, int idx, int* dashRows)
{
    uint32_t V[16] = {};
    if (!Runtime::Mem::Rd((void*)row, V, sizeof(V))) {
        logFile << "      (row unreadable)" << std::endl;
        return;
    }
    const uint32_t sensor   = V[1];         // +0x04
    const uint32_t code     = V[2];         // +0x08
    const uint32_t category = V[3];         // +0x0C
    const uint32_t objectId = V[4];         // +0x10
    const uint32_t extra    = V[5];         // +0x14
    const uint32_t nPers    = V[7];         // +0x1C count правил личности
    const uint32_t nOrder   = V[12];        // +0x30 count правил порядка

    const bool isDash = (code == 84u || code == 85u);
    if (isDash && dashRows) ++(*dashRows);

    char l[240];
    sprintf_s(l, "      slot %2d [%d] 0x%08X  code %3u  %-22s"
                 " tuple{s=%u,cat=%u,obj=%u,extra=%u}  rules %u/%u%s",
              slot, idx, (unsigned)row, code,
              GoalOfCode((int)code), sensor, category, objectId, extra,
              nPers, nOrder, isDash ? "   <<< DASH ROW" : "");
    logFile << l << std::endl;
}

void DumpPriorityRows()
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

    // Таблица имён нужна, чтобы рядом с кодом стояла цель, а не голое
    // число. Строится от планировщика — он же и проверка живости AI.
    const uintptr_t planner = ResolvePlanner(pawn, 0, 0);
    if (planner) BuildGoalTable(planner);

    // cAICtrl -> cAIPriorityThink, оба по ИМЕНИ КЛАССА.
    uintptr_t ctrl = 0;
    if (Runtime::Mem::RdPtr((void*)(pawn + 0x2E64), &ctrl) && ctrl) {
        char nm[48] = {};
        if (!Runtime::Mem::NameOfLiveObject(ctrl, nm, sizeof(nm))
            || strcmp(nm, "cAICtrl") != 0) ctrl = 0;
    }
    if (!ctrl) ctrl = Runtime::FindChildByClass(pawn, kPawnBodyBytes, "cAICtrl", 0);
    const uintptr_t think = ctrl
        ? Runtime::FindChildByClass(ctrl, 704, "cAIPriorityThink", 0) : 0;
    if (!think) {
        lstrcpynA(s_status, "goap: cAIPriorityThink not found", sizeof(s_status));
        logFile << "GoapProbe: " << s_status << std::endl;
        return;
    }

    logFile << "GoapProbe: === priority rows (who is allowed to pick a code) ==="
            << std::endl;
    {
        char l[200];
        sprintf_s(l, "  cAIPriorityThink 0x%08X, 48 buckets at +0x38 stride 0x14",
                  (unsigned)think);
        logFile << l << std::endl;
    }

    int rows = 0, dashRows = 0, badRows = 0, usedBuckets = 0;
    bool codeSeen[kMaxCode];
    memset(codeSeen, 0, sizeof(codeSeen));

    for (int slot = 0; slot < 48; ++slot) {
        const uint32_t descOff = 0x38u + (uint32_t)slot * 0x14u;
        uint32_t D[5] = {};
        if (!Runtime::Mem::Rd((void*)(think + descOff), D, sizeof(D))) continue;
        const uint32_t count = D[1], capacity = D[2];
        const uintptr_t arr  = D[4];
        if (!count) continue;
        // ГРАНИЦА БЫЛА СЛИШКОМ УЗКОЙ — И ПРИБОР СРАЗУ ЭТО ПОКАЗАЛ.
        //
        // Первый прогон: «rows found 76 (canon 85: MISMATCH), buckets 32 of
        // 48, unreadable entries 1». Ровно одно отвергнутое ведро и ровно
        // девять недостающих строк — то есть отвергли не мусор, а живое
        // ведро с count > 8. Канон говорит «по 8 слотов» про типичное
        // ведро, а ёмкость массива бывает 16 (SOURCE_OF_TRUTH §3.4
        // фиксирует именно capacity).
        //
        // Пока строк не 85, вердикт про код 84 — предварительный: в
        // отвергнутом ведре могло лежать что угодно. Поэтому граница
        // поднята до 16, а отвергнутый дескриптор печатается целиком:
        // «пропустил» обязано быть видно, а не подразумеваться.
        if (count > 16u || capacity > 16u || count > capacity
            || !Runtime::Mem::LooksHeap(arr)) {
            ++badRows;
            char b[200];
            sprintf_s(b, "      slot %2d REJECTED descriptor:"
                         " vt 0x%08X count %u cap %u flags %u array 0x%08X",
                      slot, D[0], D[1], D[2], D[3], D[4]);
            logFile << b << std::endl;
            continue;
        }
        uintptr_t ptrs[16] = {};
        if (!Runtime::Mem::Rd((void*)arr, ptrs, count * sizeof(uintptr_t))) {
            ++badRows;
            char b[160];
            sprintf_s(b, "      slot %2d payload unreadable at 0x%08X (count %u)",
                      slot, (unsigned)arr, count);
            logFile << b << std::endl;
            continue;
        }
        ++usedBuckets;

        for (uint32_t n = 0; n < count; ++n) {
            const uintptr_t row = ptrs[n];
            if (!Runtime::Mem::LooksHeap(row)) { ++badRows; continue; }
            char nm[64] = {};
            if (!Runtime::Mem::NameOfLiveObject(row, nm, sizeof(nm))
                || strcmp(nm, "rAIPriorityThink::cPrioParam") != 0) {
                ++badRows;
                continue;
            }
            uint32_t code = 0;
            if (Runtime::Mem::Rd((void*)(row + 0x08), &code, 4)
                && code < (uint32_t)kMaxCode)
                codeSeen[code] = true;
            DumpOnePrioRow(row, slot, (int)n, &dashRows);
            ++rows;
        }
    }

    // СОБСТВЕННЫЙ СЧЁТ ПРИБОРА. Канон: 85 строк, 48 вёдер.
    char l[240];
    sprintf_s(l, "  rows found %d (canon 85: %s), buckets with rows %d of 48,"
                 " unreadable entries %d",
              rows, (rows == 85) ? "MATCH" : "MISMATCH - suspect the probe first",
              usedBuckets, badRows);
    logFile << l << std::endl;

    // Какие коды из таблицы целей не имеют НИ ОДНОЙ строки приоритета:
    // такую цель приоритетное мышление выбрать не может в принципе.
    int orphans = 0;
    logFile << "  loaded goals with NO priority row (unreachable by cmc.prt):"
            << std::endl;
    for (int c = 0; c < kMaxCode; ++c) {
        if (!s_goalName[c][0] || codeSeen[c]) continue;
        ++orphans;
        sprintf_s(l, "      code %3d  %s", c, s_goalName[c]);
        logFile << l << std::endl;
    }
    if (!orphans) logFile << "      (none - every loaded goal has a row)" << std::endl;

    // ГЛАВНАЯ СТРОКА. Ради неё всё и писалось.
    //
    // ФОРМУЛИРОВКА ИСПРАВЛЕНА ПОСЛЕ ПЕРВОГО ЖЕ ПРОГОНА. Сначала здесь
    // стояло «нет строки -> cmc.prt не может выбрать рывок». Это оказалось
    // сильнее, чем позволяют факты: в списке «целей без строки» стоит
    // код 76 GotoOm, а гистограмма предыдущего замера дала ему 794 кадра,
    // 398 из них в бою. Значит отсутствие строки приоритета НЕ означает,
    // что код не может быть выбран, — существует второй путь номинации.
    // Прибор обязан говорить то, что измерил, и не больше.
    sprintf_s(l, "  DASH ROWS (code 84/85): %d -> %s", dashRows,
              dashRows ? "rows EXIST - the priority layer nominates dash, so weights ARE the lever"
                       : "no row: the priority layer does not nominate dash"
                         " (NOTE: codes without a row still get selected - GotoOm 76 did)");
    logFile << l << std::endl;
    if (rows != 85)
        logFile << "  VERDICT IS PROVISIONAL: rows != 85, some bucket was skipped"
                   " - fix the probe and re-run before believing the line above."
                << std::endl;

    sprintf_s(s_status, "goap: %d prio rows, %d dash rows, %d orphan goals",
              rows, dashRows, orphans);
}

// --- ИНТЕРФЕЙСЫ ПЛАНА --------------------------------------------------------
//
// Главный факт замера 74.0: все три пойманных рывка (включая боевой)
// случились под кодом 1 «Follow», а строки приоритета у кодов 84/85 нет
// ни одной. Рывок выбирает не приоритетный слой — его выбирает сама цель
// Follow, переключая моторную команду внутри своего плана.
//
// Здесь мы просто СМОТРИМ на эти команды: у кого какие. Сравнение двух
// нажатий (пешка бежит / пешка рвётся) и есть поиск переключателя.
void DumpPlanInterfaces()
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
        return;
    }
    BuildGoalTable(planner);

    int32_t cur = -1;
    Runtime::Mem::Rd((void*)(planner + 0x17C), &cur, 4);
    char act[48] = {};
    Runtime::ReadLiveAct(pawn, act, sizeof(act));

    logFile << "GoapProbe: === plan interfaces (who moves the pawn) ===" << std::endl;
    {
        char l[200];
        sprintf_s(l, "  current code %d \"%s\", current act %s",
                  cur, GoalOfCode(cur), act[0] ? act : "?");
        logFile << l << std::endl;
    }

    const int codes[4] = { 1, 84, 20, cur };
    for (int i = 0; i < 4; ++i) {
        const int c = codes[i];
        if (c < 0 || c >= kMaxCode) continue;
        if (i == 3 && (c == 1 || c == 84 || c == 20)) continue;  // уже показан
        char ifaces[96] = {};
        Runtime::PawnPlanInterfaces(c, ifaces, sizeof(ifaces));
        char l[220];
        sprintf_s(l, "    code %3d  %-18s -> %s", c, GoalOfCode(c),
                  ifaces[0] ? ifaces : "(no cCmc* in this plan block)");
        logFile << l << std::endl;
    }
    logFile << "  Press this twice: once while the pawn jogs, once while it dashes."
               " A name that appears only in the dashing snapshot is the switch."
            << std::endl;
    sprintf_s(s_status, "goap: plan interfaces dumped (code %d)", cur);
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

// ЕЩЁ ОДНО ВРАНЬЁ ПРИБОРА, ПОЙМАННОЕ В ЛОГЕ 73.27.
//
// Между выборами планировщик держит в +0x17C значение -1 (0xFFFFFFFF).
// В логе это видно парами: «code -1», «code 0 Wait», «code -1», «code 0».
// Счётчик входов считал ВОЗВРАТ из -1 новым входом, и Wait получил 365
// входов на 365 кадрах — то есть «пешка входила в ожидание каждый кадр».
// Это артефакт, а не поведение.
//
// Теперь -1 не считается состоянием: последним кодом остаётся последний
// ДЕЙСТВИТЕЛЬНЫЙ, а сами промежутки считаются отдельно и печатаются.
static int32_t  s_lastValid = -1;
static uint32_t s_gapSamples = 0;

void ResetCodeHistogram()
{
    memset(s_hist, 0, sizeof(s_hist));
    memset(s_histCombat, 0, sizeof(s_histCombat));
    memset(s_enter, 0, sizeof(s_enter));
    s_samples = 0;
    s_samplesCombat = 0;
    s_lastValid = -1;
    s_gapSamples = 0;
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
    sprintf_s(l, "  %u samples over %u s, of them %u in combat"
                 " (plus %u samples with code -1: the gap between selections,"
                 " not a state)",
              s_samples, (unsigned)secs, s_samplesCombat, s_gapSamples);
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

    // Оговорка о метке боя — рядом с числами, а не в документе.
    // Детектор гаснет не мгновенно, поэтому «хвост» боя (например
    // VictoryPose, которая играет уже после драки) подписывается боевым.
    // Это смещение вверх у боевых столбцов, и его надо знать, читая
    // таблицу.
    logFile << "  NOTE: the combat label has a tail - states that play right"
               " after a fight (VictoryPose) still count as combat."
            << std::endl;
    // И перекрёстная ссылка на второй прибор: ноль здесь не означает
    // «пешка не рвётся». DashWatch считает сами состояния рывка и с
    // 74.0 подписывает каждое кодом приоритета.
    logFile << "  CROSS-CHECK: zero here means the planner never SELECTS dash."
               " Whether the pawn dashes at all is DashWatch's number, and it"
               " now records the priority code at the moment of the dash."
            << std::endl;

    sprintf_s(s_status, "goap: hist %u samples, dash 84/85 = %u frames (%u in combat)",
              s_samples, dashFrames, dashCombat);
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
        // Вход считаем по последнему ДЕЙСТВИТЕЛЬНОМУ коду: возврат из
        // промежуточного -1 в тот же код — не вход (см. комментарий выше).
        if (code != s_lastValid) ++s_enter[code];
        s_lastValid = code;
    } else {
        ++s_gapSamples;
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
