// GoapProbe — разведка планировщика пешки. См. GoapProbe.h.

#include "stdafx.h"
#include "GoapProbe.h"
#include "../runtime/Runtime.h"        // публичный API: MainPawnBody, FindChildByClass
#include "../runtime/RuntimeInternal.h" // Mem::Rd / RdPtr / NameOfLiveObject
#include "../TypeAtlas.Generated.h"     // размеры классов для границ обхода
#include "../CombatIntel.h"              // IsInCombat() - гистограмма делится на бой/покой
#include <math.h>                        // sqrtf() - дистанция поводка


extern BYTE** pBase;   // из dinput8.cpp — база записей персонажей

// Только одна функция из доктрины, поэтому объявляем её здесь, а не тянем
// весь `GuardianDoctrine.h`: он приводит за собой `PawnAI_Common.h` с
// SEH-макросами, которых нет в g++-харнессе.
namespace PawnAI { bool GuardianLeverIsActive(); bool GuardianDoctrineOwnsRule(); }

namespace GoapProbe {

static char      s_status[192] = "goap: idle";
static bool      s_tableReady = false;   // таблица имён целей построена
static uintptr_t s_planner = 0;          // кэш планировщика выбранной пешки

// --- ЦЕЛЬ ПРОБЫ: КАКАЯ ИМЕННО ПЕШКА -----------------------------------------
//
// Все дампы читали `MainPawnBody()`, то есть свою пешку. Пока пешка была
// одна, это и было определением «пешки». Сейчас в партии трое, и главный
// интерес сместился: тестер смотрит на наёмного Файтера-Guardian, а
// прибор упрямо показывает Страйдершу.
//
// Хуже того, вывод получался бы ложным: набор ЦЕЛЕЙ у планировщика
// зависит от вокации (у нашей пешки слот кода 4 пуст), и «code 4 пустой»
// на Страйдерше ничего не говорит о Файтере.
//
// 0 — своя пешка, 1..2 — наёмные (порядок по записям персонажей).
static int s_probePawn = 0;

void SetProbePawn(int idx)
{
    if (idx < 0) idx = 0;
    if (idx > 2) idx = 2;
    s_probePawn = idx;
    s_planner = 0;          // цель сменилась — кэш планировщика недействителен
    s_tableReady = false;
}
int GetProbePawn() { return s_probePawn; }

// СВЕРКА ЛИЧНОСТИ ПЕРЕД ЧТЕНИЕМ (75.37).
//
// Лог 19: тестер полтора часа дрался Страйдершей, а гистограмма пришла с
// чужого тела — `WpnWandAtk 3853 кадра`, `Medicant 660 / Pioneer 561`.
// То есть мерили кастера. `probe target` стоял на «own pawn», но
// `PawnBodyAt(0)` вернул чужое тело: разбор партии иногда промахивается,
// это давняя болячка (`record body 0x00000000`).
//
// Прибор это ЗАМЕТИЛ — напечатал «BODY DISAGREES WITH THE RECORD» — но
// всё равно посчитал и выдал числа. Замечать мало. Раз у нас есть два
// независимых источника склонностей (запись персонажа и живое тело), надо
// не жаловаться, а ИСКАТЬ тело, у которого они сходятся.
//
// Склонности — хороший отпечаток: девять чисел, у разных пешек разные.
// Сверяем по первичной склонности и по её величине.
static bool InclFingerprintOk(uintptr_t body, int idx)
{
    if (!body || !pBase || !*pBase) return false;
    const uintptr_t rec = (uintptr_t)(*pBase) + 0xA7000 + 0x7F0
                        + (uintptr_t)idx * 0x1660;
    float want[9] = {}, live[9] = {};
    for (int i = 0; i < 9; ++i)
        if (!Runtime::Mem::Rd((void*)(rec + 0x1B90 + i * 0x0C), &want[i], 4))
            return false;
    if (!Runtime::PawnInclinationsLive(body, live)) return false;
    int a = 0, b = 0;
    for (int i = 1; i < 9; ++i) {
        if (want[i] > want[a]) a = i;
        if (live[i] > live[b]) b = i;
    }
    if (a != b) return false;
    const float d = (want[a] > live[a]) ? (want[a] - live[a]) : (live[a] - want[a]);
    return d <= 200.0f;      // склонности дрейфуют, но не на сотни за бой
}

// Тело выбранной пешки, ПРОВЕРЕННОЕ. Если разбор партии промахнулся,
// перебираем остальные места партии и берём то, чей отпечаток сошёлся.
// О подмене говорим вслух — один раз на находку.
static uintptr_t s_probeVerified = 0;
static int       s_probeVerifiedFor = -1;

static uintptr_t ProbeBody()
{
    bool isMain = false;
    uintptr_t b = Runtime::PawnBodyAt(s_probePawn, &isMain);
    if (!b) b = Runtime::MainPawnBody();

    // Уже проверенное тело того же места партии — берём без работы.
    if (s_probeVerified && s_probeVerifiedFor == s_probePawn
        && InclFingerprintOk(s_probeVerified, s_probePawn))
        return s_probeVerified;

    if (b && InclFingerprintOk(b, s_probePawn)) {
        s_probeVerified = b;
        s_probeVerifiedFor = s_probePawn;
        return b;
    }

    for (int i = 0; i < 3; ++i) {
        bool m = false;
        const uintptr_t c = Runtime::PawnBodyAt(i, &m);
        if (!c || c == b) continue;
        if (!InclFingerprintOk(c, s_probePawn)) continue;
        char l[220];
        sprintf_s(l, "GoapProbe: probe target CORRECTED - the party list gave"
                     " body 0x%08X for slot %d, but the inclination fingerprint"
                     " matches body 0x%08X (party slot %d). Reading the latter.",
                  (unsigned)b, s_probePawn, (unsigned)c, i);
        logFile << l << std::endl;
        s_probeVerified = c;
        s_probeVerifiedFor = s_probePawn;
        s_planner = 0;
        s_tableReady = false;
        return c;
    }

    // Не сошлось ни одно тело. Возвращаем что есть, но метку ставим:
    // ProbeLabel() и гистограмма обязаны сказать об этом первыми.
    s_probeVerified = 0;
    s_probeVerifiedFor = -1;
    return b;
}

// Сошёлся ли отпечаток у того тела, которое мы читаем прямо сейчас.
static bool ProbeIdentityOk() { return s_probeVerified != 0; }

static const char* ProbeLabel()
{
    static char buf[110];
    int voc = 0, lvl = 0;
    uintptr_t body = 0;
    static const char* kV[] = { "?", "Fighter", "Strider", "Mage", "Mystic Knight",
                                "Assassin", "Magick Archer", "Warrior", "Ranger",
                                "Sorcerer" };
    // ЛОВУШКА, ПОЙМАННАЯ В ЛОГЕ 75.9: подпись бралась из ЗАПИСИ, а дамп
    // читал ТЕЛО. Два дампа подряд («hired Fighter» и «own Strider»)
    // напечатали один и тот же адрес планировщика — то есть подпись
    // сменилась, а тело нет. Прибор обязан показывать оба конца: кого
    // назвали и что реально прочитали.
    const uintptr_t used = ProbeBody();
    if (Runtime::PartyRecordInfo(s_probePawn, &voc, &lvl, &body))
        sprintf_s(buf, "%s pawn (%s lvl %d) | record body 0x%08X | READING body 0x%08X%s",
                  s_probePawn ? "hired" : "own",
                  (voc >= 1 && voc <= 9) ? kV[voc] : "?", lvl,
                  (unsigned)body, (unsigned)used,
                  (!ProbeIdentityOk())
                      ? "  <<< IDENTITY NOT CONFIRMED"
                      : ((body && used && body != used)
                             ? "  <<< body differs from the record, but the"
                               " inclination fingerprint matches"
                             : ""));
    else
        sprintf_s(buf, "pawn #%d (no record) | READING body 0x%08X",
                  s_probePawn, (unsigned)used);
    return buf;
}
static bool      s_watch = false;
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
// s_tableReady объявлен выше, рядом с выбором цели пробы: он им сбрасывается.
// Тот же урок, что и с C2065 накануне — статик живёт выше первого
// использования, а не там, где о нём удобнее рассказывать.

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
    const uintptr_t pawn = ProbeBody();
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

    logFile << "  target: " << ProbeLabel() << std::endl;
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
    const uintptr_t pawn = ProbeBody();
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
    const uintptr_t pawn = ProbeBody();
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
    logFile << "  target: " << ProbeLabel() << std::endl;
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
    const uintptr_t pawn = ProbeBody();
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

// --- ПЕРЕКЛИЧКА ПАРТИИ -------------------------------------------------------
//
// Наёмные пешки того же класса, что и главная. Пока их не было, «первое
// тело uCmc» работало как определение главной пешки; с наёмными это
// подбрасывание монетки. Дамп показывает всех и главное — их корни AI.
void DumpPartyRoster()
{
    if (!Runtime::Mem::InWorld()) {
        lstrcpynA(s_status, "goap: not in an active save", sizeof(s_status));
        return;
    }
    logFile << "GoapProbe: === party roster ===" << std::endl;

    // Просим разбор пересканировать состав: наёмные пешки приходят
    // посреди игры, а полный скан по умолчанию не повторяется, пока
    // прежние тела живы. Запрос троттлится, поэтому нажатие безопасно.
    Runtime::PartyRequestRescan();

    const int n = Runtime::PawnBodyCount();
    char l[260];

    // СНАЧАЛА ЗАПИСИ, ПОТОМ ТЕЛА.
    //
    // Записи персонажей лежат по фиксированным адресам и доступны всегда,
    // даже когда живые тела ещё не найдены. Именно поэтому чужой мод
    // показывает всю партию, ничего не сканируя. Показываем состав по
    // записям — и отдельно, нашлось ли для каждой живое тело.
    const int recPawns = Runtime::PartyRecordPawnCount();
    sprintf_s(l, "  Arisen 0x%08X | pawns by CHARACTER RECORDS: %d | pawn BODIES found: %d",
              (unsigned)Runtime::ArisenBody(), recPawns, n);
    logFile << l << std::endl;
    if (recPawns > n)
        logFile << "  records show more pawns than bodies - the body scan is the"
                   " weak link, not the party" << std::endl;

    // Состав по записям — печатается всегда, даже если тел не нашлось.
    static const char* kVoc[] = { "?", "Fighter", "Strider", "Mage",
                                  "Mystic Knight", "Assassin", "Magick Archer",
                                  "Warrior", "Ranger", "Sorcerer" };
    for (int r = 0; r <= 2; ++r) {
        int voc = 0, lvl = 0;
        uintptr_t body = 0;
        if (!Runtime::PartyRecordInfo(r, &voc, &lvl, &body)) continue;
        sprintf_s(l, "  record #%d (%s): %-14s level %3d | live body %s",
                  r, r == 0 ? "own pawn" : "hired",
                  (voc >= 1 && voc <= 9) ? kVoc[voc] : "?", lvl,
                  body ? "matched" : "NOT matched to any body");
        logFile << l << std::endl;
    }

    // СПИСОК ЖИВЫХ ОБЪЕКТОВ — сырьё, из которого теперь берутся тела.
    // Если тел пешек нет и здесь, значит обход до них не доходит, и это
    // совсем другой разговор, чем «скан памяти не нашёл».
    {
        int shown = 0;
        logFile << "  live actor list (source for adoption):" << std::endl;
        for (int i = 0; i < Runtime::ActorCount() && shown < 32; ++i) {
            const char* kind = 0;
            const uintptr_t b = Runtime::ActorAt(i, &kind);
            if (!b) continue;
            ++shown;
            sprintf_s(l, "      0x%08X  %s", (unsigned)b, kind ? kind : "?");
            logFile << l << std::endl;
        }
        if (!shown) logFile << "      (empty)" << std::endl;
    }

    uintptr_t seenRes[8] = {};
    int nRes = 0;

    for (int i = 0; i < n && i < 8; ++i) {
        bool isMain = false;
        const uintptr_t body = Runtime::PawnBodyAt(i, &isMain);
        if (!body) continue;

        // cAICtrl -> planner / priority think, всё по имени класса.
        uintptr_t ctrl = 0;
        if (Runtime::Mem::RdPtr((void*)(body + 0x2E64), &ctrl) && ctrl) {
            char nm[48] = {};
            if (!Runtime::Mem::NameOfLiveObject(ctrl, nm, sizeof(nm))
                || strcmp(nm, "cAICtrl") != 0) ctrl = 0;
        }
        if (!ctrl) ctrl = Runtime::FindChildByClass(body, kPawnBodyBytes, "cAICtrl", 0);
        const uintptr_t planner = ctrl
            ? Runtime::FindChildByClass(ctrl, 704, "cAIGoalPlanning", 0) : 0;
        const uintptr_t think = ctrl
            ? Runtime::FindChildByClass(ctrl, 704, "cAIPriorityThink", 0) : 0;

        // Ресурс приоритетов: think + 0x08 (SOURCE_OF_TRUTH §3.2).
        uintptr_t res = 0;
        char resPath[80] = {};
        if (think && Runtime::Mem::RdPtr((void*)(think + 0x08), &res)
            && Runtime::Mem::LooksHeap(res))
            ResourcePath(res, resPath, sizeof(resPath));

        int32_t code = -1;
        if (planner) Runtime::Mem::Rd((void*)(planner + 0x17C), &code, 4);
        char act[48] = {};
        Runtime::ReadLiveAct(body, act, sizeof(act));

        sprintf_s(l, "  [%d] %-6s body 0x%08X  planner 0x%08X  think 0x%08X"
                     "  prio res 0x%08X %s",
                  i, isMain ? "MAIN" : "hired", (unsigned)body,
                  (unsigned)planner, (unsigned)think, (unsigned)res,
                  resPath[0] ? resPath : "(no path)");
        logFile << l << std::endl;
        if (planner) BuildGoalTable(planner);
        sprintf_s(l, "        current code %d \"%s\", act %s",
                  code, GoalOfCode(code), act[0] ? act : "?");
        logFile << l << std::endl;

        if (res) {
            bool dup = false;
            for (int k = 0; k < nRes; ++k) if (seenRes[k] == res) dup = true;
            if (!dup && nRes < 8) seenRes[nRes++] = res;
        }
    }

    // --- sPawnManager: ищем поле «сколько пешек» --------------------------
    //
    // ЗАЧЕМ. Сейчас смену состава мы замечаем сравнением списка живых тел
    // со своим — это дёшево, но это всё-таки сравнение. Совет со стороны
    // предлагает хук на функцию найма; хук по неуникальной сигнатуре в
    // чужом коде — риск, который мы уже оплачивали (317 совпадений у
    // сигнатуры передвижения, разные сборки exe).
    //
    // Есть третий путь, дешевле обоих: у игры есть `sPawnManager`, и если
    // в нём лежит счётчик пешек или массив их слотов, то смена состава
    // читается ОДНИМ dword за тик. Это ровно «событие» по цене чтения и
    // без единого хука.
    //
    // Дамп помечает поля, равные числу пешек, и поля, совпадающие с
    // указателями на их тела. Дальше решаем по фактам.
    for (int m = 0; m < Runtime::g_nPartyPawnMgr && m < 4; ++m) {
        const uintptr_t mgr = Runtime::g_partyPawnMgr[m];
        if (!mgr) continue;
        // ОКНО РАСШИРЕНО. Первые 64 dword не дали ничего: ни счётчика, ни
        // указателей на тела. Менеджер размером 5512 байт, значит смотреть
        // надо его целиком, а не первый кусочек.
        static uint32_t V[1378] = {};
        const int nDw = (int)(sizeof(V) / 4);
        if (!Runtime::Mem::Rd((void*)mgr, V, sizeof(V))) continue;
        sprintf_s(l, "  sPawnManager 0x%08X, %d dwords (marked: count / body ptr):",
                  (unsigned)mgr, nDw);
        logFile << l << std::endl;
        for (int k = 0; k < nDw; ++k) {
            const uint32_t v = V[k];
            const char* mark = "";
            if ((int)v == n && n > 0) mark = "   <<< equals the pawn count";
            for (int q = 0; q < n && q < 8; ++q) {
                bool im = false;
                if ((uintptr_t)v == Runtime::PawnBodyAt(q, &im))
                    mark = im ? "   <<< MAIN pawn body" : "   <<< hired pawn body";
            }
            if (!mark[0]) continue;
            sprintf_s(l, "      +0x%03X = 0x%08X%s", k * 4, v, mark);
            logFile << l << std::endl;
        }
    }
    if (!Runtime::g_nPartyPawnMgr)
        logFile << "  sPawnManager not found in this scan" << std::endl;

    // ГЛАВНЫЙ ОТВЕТ ДАМПА.
    if (n <= 1) {
        logFile << "  only one pawn in the list. If the party actually has more,"
                   " the rescan was just requested by this very press - wait a"
                   " second and press again. (Before 75.3 the party was scanned"
                   " once and never again, so hired pawns never appeared.)"
                << std::endl;
    } else if (nRes == 1) {
        logFile << "  PRIORITY RESOURCE IS SHARED: all pawns point at the same"
                   " rAIPriorityThink. Every weight edit we make applies to"
                   " HIRED pawns too - that is scope, not a bug, but it must be"
                   " stated." << std::endl;
    } else {
        sprintf_s(l, "  PRIORITY RESOURCE IS PER-PAWN: %d distinct resources for"
                     " %d pawns - edits can be scoped to our own pawn.", nRes, n);
        logFile << l << std::endl;
    }

    sprintf_s(s_status, "goap: roster - %d pawns, %d distinct priority resources",
              n, nRes);
}

// --- ГИСТОГРАММА ДЕЙСТВИЙ (трек идлов) ---------------------------------------
//
// Заявка: «идлы пешек вне боя привязаны к праймари инклинации, хотелось бы
// разнообразия». Прежде чем что-то менять, нужен список того, что есть:
// сколько РАЗНЫХ состояний пешка показывает в покое и как долго в каждом
// стоит. Гистограмма целей на этот вопрос не отвечает — цель `Wait` одна,
// а видимых поз под ней может быть сколько угодно.
//
// Только чтение: имя класса текущего действия раз в тик.
struct ActBin { char name[48]; uint32_t frames, combat, entries; };
static const int kMaxActBins = 64;
static ActBin  s_actBin[kMaxActBins];
static int     s_nActBins = 0;
static bool    s_actWatch = false;
static char    s_actPrev[48] = {};
static DWORD   s_actStart = 0;
static uint32_t s_actSamples = 0;
static uint32_t s_actOverflow = 0;   // сколько состояний не поместилось

void ToggleActWatch()
{
    s_actWatch = !s_actWatch;
    if (s_actWatch) {
        memset(s_actBin, 0, sizeof(s_actBin));
        s_nActBins = 0;
        s_actPrev[0] = 0;
        s_actSamples = 0;
        s_actOverflow = 0;
        s_actStart = MsNow();
    }
    logFile << "GoapProbe: act watch " << (s_actWatch ? "ON (histogram reset)" : "off")
            << std::endl;
}

bool ActWatchActive() { return s_actWatch; }

static void ActWatchTick()
{
    if (!s_actWatch) return;
    if (!Runtime::Mem::InWorld()) return;
    const uintptr_t pawn = ProbeBody();
    if (!pawn) return;
    char act[48] = {};
    if (!Runtime::ReadLiveAct(pawn, act, sizeof(act)) || !act[0]) return;

    const bool fight = IsInCombat();
    ++s_actSamples;

    int idx = -1;
    for (int i = 0; i < s_nActBins; ++i)
        if (!strcmp(s_actBin[i].name, act)) { idx = i; break; }
    if (idx < 0) {
        if (s_nActBins >= kMaxActBins) { ++s_actOverflow; return; }
        idx = s_nActBins++;
        lstrcpynA(s_actBin[idx].name, act, sizeof(s_actBin[idx].name));
    }
    ++s_actBin[idx].frames;
    if (fight) ++s_actBin[idx].combat;
    if (strcmp(act, s_actPrev)) {
        ++s_actBin[idx].entries;
        lstrcpynA(s_actPrev, act, sizeof(s_actPrev));
    }
}

void DumpActHistogram()
{
    logFile << "GoapProbe: === pawn act histogram (idle track) ===" << std::endl;
    logFile << "  target: " << ProbeLabel() << std::endl;
    if (!s_actSamples) {
        logFile << "  no samples - turn 'act watch' ON first, then stand around."
                << std::endl;
        lstrcpynA(s_status, "goap: no act samples - enable act watch", sizeof(s_status));
        return;
    }
    const DWORD secs = s_actStart ? (MsNow() - s_actStart) / 1000 : 0;
    char l[240];
    sprintf_s(l, "  %u samples over %u s, %d distinct acts%s",
              s_actSamples, (unsigned)secs, s_nActBins,
              s_actOverflow ? " (TABLE FULL - some acts were dropped)" : "");
    logFile << l << std::endl;

    // Склонности рядом с гистограммой: заявка утверждает связь, и она
    // должна проверяться в том же снимке, а не по памяти тестера.
    //
    // ЧИТАЕМ САМИ, А НЕ ЧУЖОЙ ФУНКЦИЕЙ. Первая версия звала
    // `GetPawnInclinations()` из `PawnAI.h` — объявление там есть, а
    // определения нет НИ В ОДНОМ .cpp: сборка легла на LNK2019. Причём
    // наш же линтер писал об этом предупреждением («нет тела?») ещё
    // накануне, и я его пролистал. Предупреждения — тоже показания
    // прибора.
    //
    // Формула канонична (SOURCE_OF_TRUTH §1): запись главной пешки =
    // pBase + 0xA7000 + 0x7F0, склонности от +0x1B90 с шагом 0x0C.
    if (pBase && *pBase) {
        const uintptr_t rec = (uintptr_t)(*pBase) + 0xA7000 + 0x7F0;
        float inc[9] = {};
        bool ok = true;
        for (int i = 0; i < 9 && ok; ++i)
            ok = Runtime::Mem::Rd((void*)(rec + 0x1B90 + i * 0x0C), &inc[i], 4);
        if (ok) {
            sprintf_s(l, "  inclinations: Sca %.0f Med %.0f Mit %.0f Cha %.0f Uti %.0f"
                         " Gua %.0f Nex %.0f Pio %.0f Acq %.0f",
                      inc[0], inc[1], inc[2], inc[3], inc[4], inc[5],
                      inc[6], inc[7], inc[8]);
            logFile << l << std::endl;
        }
    }

    logFile << "  act                                    frames  in combat  entries"
            << std::endl;
    bool done[kMaxActBins];
    memset(done, 0, sizeof(done));
    for (int rank = 0; rank < s_nActBins; ++rank) {
        int best = -1;
        for (int i = 0; i < s_nActBins; ++i) {
            if (done[i]) continue;
            if (best < 0 || s_actBin[i].frames > s_actBin[best].frames) best = i;
        }
        if (best < 0) break;
        done[best] = true;
        sprintf_s(l, "  %-38s %6u  %9u  %7u", s_actBin[best].name,
                  s_actBin[best].frames, s_actBin[best].combat, s_actBin[best].entries);
        logFile << l << std::endl;
    }

    // Отдельно — то, ради чего замер: репертуар ВНЕ боя.
    int idleKinds = 0;
    for (int i = 0; i < s_nActBins; ++i)
        if (s_actBin[i].frames > s_actBin[i].combat) ++idleKinds;
    sprintf_s(l, "  IDLE REPERTOIRE: %d acts seen outside combat."
                 " Re-run after switching the primary inclination and compare.",
              idleKinds);
    logFile << l << std::endl;
    sprintf_s(s_status, "goap: acts %d distinct, %d seen out of combat",
              s_nActBins, idleKinds);
}

// --- КАРТА СКЛОННОСТЕЙ -------------------------------------------------------
//
// Раскладка подтверждена (SOURCE_OF_TRUTH §3.3/§3.5/§3.5.2):
//   cPrioParam  +0x18 cArray правил личности: count +0x1C, mpArray +0x28
//   cCodeParam  +0x04 AddS32, +0x08 AddF32, +0x0C break,
//               +0x10 cArray проверок: count +0x14, mpArray +0x20
//   check       +0x04 id склонности (0-based), +0x08 ранг (2=primary)
static const char* kInclNames[9] = {
    "Scather", "Medicant", "Mitigator", "Challenger", "Utilitarian",
    "Guardian", "Nexus", "Pioneer", "Acquisitor"
};
static const char* InclNameOf(int id)
{
    return (id >= 0 && id < 9) ? kInclNames[id] : "?";
}
static const char* RankNameOf(int r)
{
    return (r == 2) ? "primary" : (r == 1) ? "secondary" : (r == 0) ? "tertiary" : "?";
}

void DumpInclinationRules()
{
    if (!Runtime::Mem::InWorld()) {
        lstrcpynA(s_status, "goap: not in an active save", sizeof(s_status));
        return;
    }
    const uintptr_t pawn = ProbeBody();
    if (!BodyAlive(pawn)) {
        lstrcpynA(s_status, "goap: pawn body not ready", sizeof(s_status));
        return;
    }
    const uintptr_t planner = ResolvePlanner(pawn, 0, 0);
    if (planner) BuildGoalTable(planner);

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

    logFile << "GoapProbe: === inclination rule map (what each inclination moves) ==="
            << std::endl;
    logFile << "  target: " << ProbeLabel() << std::endl;

    // Сводка: по склонности и рангу — список «код(±N)».
    struct Sum { int nRules; int sum; char line[400]; };
    static Sum sum[9][3];
    memset(sum, 0, sizeof(sum));
    int rows = 0, rules = 0, noCheck = 0;
    char l[300];

    for (int slot = 0; slot < 48; ++slot) {
        uint32_t D[5] = {};
        if (!Runtime::Mem::Rd((void*)(think + 0x38u + (uint32_t)slot * 0x14u), D, sizeof(D)))
            continue;
        const uint32_t count = D[1], capacity = D[2];
        const uintptr_t arr = D[4];
        if (!count || count > 16u || capacity > 16u || count > capacity
            || !Runtime::Mem::LooksHeap(arr)) continue;
        uintptr_t ptrs[16] = {};
        if (!Runtime::Mem::Rd((void*)arr, ptrs, count * sizeof(uintptr_t))) continue;

        for (uint32_t n = 0; n < count; ++n) {
            const uintptr_t row = ptrs[n];
            if (!Runtime::Mem::LooksHeap(row)) continue;
            char nm[64] = {};
            if (!Runtime::Mem::NameOfLiveObject(row, nm, sizeof(nm))
                || strcmp(nm, "rAIPriorityThink::cPrioParam") != 0) continue;
            ++rows;

            uint32_t R[16] = {};
            if (!Runtime::Mem::Rd((void*)row, R, sizeof(R))) continue;
            const uint32_t code = R[2];
            const uint32_t nPers = R[7];            // +0x1C
            const uintptr_t persArr = R[10];        // +0x28
            if (!nPers || nPers > 16u || !Runtime::Mem::LooksHeap(persArr)) continue;

            uintptr_t rulePtr[16] = {};
            if (!Runtime::Mem::Rd((void*)persArr, rulePtr, nPers * sizeof(uintptr_t)))
                continue;

            for (uint32_t k = 0; k < nPers; ++k) {
                const uintptr_t rp = rulePtr[k];
                if (!Runtime::Mem::LooksHeap(rp)) continue;
                uint32_t C[10] = {};
                if (!Runtime::Mem::Rd((void*)rp, C, sizeof(C))) continue;
                const int32_t addS32 = (int32_t)C[1];   // +0x04
                const uint32_t brk   = C[3];            // +0x0C
                const uint32_t nChk  = C[5];            // +0x14
                const uintptr_t chkArr = C[8];          // +0x20
                ++rules;

                int inclId = -1, rank = -1;
                if (nChk && nChk <= 8u && Runtime::Mem::LooksHeap(chkArr)) {
                    uintptr_t cp = 0;
                    if (Runtime::Mem::RdPtr((void*)chkArr, &cp)
                        && Runtime::Mem::LooksHeap(cp)) {
                        uint32_t K[3] = {};
                        if (Runtime::Mem::Rd((void*)cp, K, sizeof(K))) {
                            inclId = (int)K[1];
                            rank   = (int)K[2];
                        }
                    }
                }
                if (inclId < 0 || inclId > 8) { ++noCheck; continue; }

                sprintf_s(l, "  code %3u %-20s rule[%u] AddS32 %+d break %u"
                             "  <- %s %s",
                          code, GoalOfCode((int)code), k, addS32, brk,
                          InclNameOf(inclId), RankNameOf(rank));
                logFile << l << std::endl;

                const int rk = (rank >= 0 && rank <= 2) ? rank : 0;
                Sum& S = sum[inclId][rk];
                ++S.nRules;
                S.sum += addS32;
                char piece[48];
                sprintf_s(piece, "%s%u(%+d)", S.line[0] ? " " : "", code, addS32);
                lstrcpynA(S.line + strlen(S.line), piece,
                          (int)(sizeof(S.line) - strlen(S.line)));
            }
        }
    }

    logFile << "  --- summary by inclination ---" << std::endl;
    for (int i = 0; i < 9; ++i) {
        for (int r = 2; r >= 0; --r) {
            if (!sum[i][r].nRules) continue;
            sprintf_s(l, "  %-12s %-9s : %d rules, net %+d   %s",
                      InclNameOf(i), RankNameOf(r), sum[i][r].nRules,
                      sum[i][r].sum, sum[i][r].line);
            logFile << l << std::endl;
        }
    }
    sprintf_s(l, "  rows %d, personality rules %d, of them without a readable check %d",
              rows, rules, noCheck);
    logFile << l << std::endl;
    logFile << "  NOTE: negative AddS32 demotes the code (moves it to a lower"
               " bucket), positive promotes it. Read-only." << std::endl;

    sprintf_s(s_status, "goap: inclination map - %d rules over %d rows", rules, rows);
}

// --- КАРТА ВЁДЕР -------------------------------------------------------------
//
// Раскладка строк по вёдрам — это ПРИМЕНЁННАЯ личность пешки: общий набор
// правил плюс её собственные склонности. Сравнение двух карт показывает
// эффект склонности напрямую, без интерпретаций.
static int  s_bucketSnap[kMaxCode];      // code -> slot, предыдущий снимок
static bool s_bucketSnapValid = false;
static char s_bucketSnapWho[160] = {};

// ИМЕНА ГРУПП ВЁДЕР (75.31).
//
// 48 вёдер — это НЕ плоский список. В ресурсе `AI\PrioThink\cmc.prt` они
// заданы шестью именованными группами по восемь слотов:
//
//   QUEST 00-07 | PL_Party 00-07 | Situ_Personal 00-07
//   Enemy 00-07 | Wait_Follow 00-07 | Etc 00-07
//
// Абсолютный слот = индекс группы * 8 + номер внутри группы. Это и есть
// причина, по которой сдвиг ±5 «не работает»: лук (57) лежит в PL_Party-03
// (абс. 11), кинжалы (54) — в Etc-04 (абс. 44), и внутри своей группы
// кинжалы чужую группу не перепрыгивают.
//
// Прибор обязан печатать группу, а не голый номер: иначе читатель снова
// будет сравнивать 41 с 11, как будто это одна шкала.
static const char* kBucketGroup[6] = {
    "QUEST", "PL_Party", "Situ_Personal", "Enemy", "Wait_Follow", "Etc"
};

static const char* GroupOfSlot(int slot)
{
    if (slot < 0 || slot >= 48) return "?";
    return kBucketGroup[slot / 8];
}

// "Etc-04" — так, как это записано в файле правил.
static void SlotLabel(int slot, char* out, int cap)
{
    if (slot < 0) { lstrcpynA(out, "(nowhere)", cap); return; }
    if (slot >= 48) { sprintf_s(out, cap, "abs%d (OUT OF THE 48)", slot); return; }
    sprintf_s(out, cap, "%s-%02d", GroupOfSlot(slot), slot % 8);
}

void DumpBucketMap()
{
    if (!Runtime::Mem::InWorld()) {
        lstrcpynA(s_status, "goap: not in an active save", sizeof(s_status));
        return;
    }
    const uintptr_t pawn = ProbeBody();
    if (!BodyAlive(pawn)) {
        lstrcpynA(s_status, "goap: pawn body not ready", sizeof(s_status));
        return;
    }
    const uintptr_t planner = ResolvePlanner(pawn, 0, 0);
    if (planner) BuildGoalTable(planner);

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

    int slotOf[kMaxCode];
    for (int i = 0; i < kMaxCode; ++i) slotOf[i] = -1;
    int placed = 0;

    logFile << "GoapProbe: === bucket map (applied personality) ===" << std::endl;
    logFile << "  target: " << ProbeLabel() << std::endl;
    char l[260];
    sprintf_s(l, "  think 0x%08X  |  48 buckets = 6 groups x 8 slots", (unsigned)think);
    logFile << l << std::endl;

    // УСЛОВИЯ ОПЫТА В САМОМ ОТЧЁТЕ. Карта вёдер И ЕСТЬ применённая личность,
    // поэтому склонность, из которой она посчитана, обязана стоять рядом.
    char who[160] = {};
    {
        float live[9] = {};
        if (Runtime::PawnInclinationsLive(pawn, live)) {
            int a = 0, b = -1;
            for (int i = 1; i < 9; ++i) if (live[i] > live[a]) a = i;
            for (int i = 0; i < 9; ++i)
                if (i != a && (b < 0 || live[i] > live[b])) b = i;
            sprintf_s(who, "%s primary %.0f / %s %.0f",
                      InclNameOf(a), live[a], InclNameOf(b), live[b]);
        } else {
            lstrcpynA(who, "inclinations unreadable", sizeof(who));
        }
        sprintf_s(l, "  inclinations (BODY): %s", who);
        logFile << l << std::endl;
    }
    // Состояние нашей записи — тоже условие опыта.
    sprintf_s(l, "  our rule-write: %s | %s",
              Runtime::GuardianFixIsApplied() ? "APPLIED" : "idle",
              Runtime::GuardianFixStatus());
    logFile << l << std::endl;

    // Что планировщик выбрал ПРЯМО СЕЙЧАС — якорь между картой и поведением.
    if (planner) {
        int32_t cur = -1;
        if (Runtime::Mem::Rd((void*)(planner + 0x17C), &cur, 4)) {
            sprintf_s(l, "  selected right now: code %d %s", cur,
                      (cur >= 0 && cur < kMaxCode) ? GoalOfCode((int)cur) : "(none)");
            logFile << l << std::endl;
        }
    }

    // ИМЕНА ЦЕЛЕЙ МОГУТ БЫТЬ НЕ ГОТОВЫ — И ЭТО НАДО СКАЗАТЬ СРАЗУ.
    //
    // В логе 16a карта напечатала «(slot empty)» почти у всех кодов, и
    // выглядело это как «у пешки ничего не загружено». На самом деле не
    // прочиталась таблица имён (21 цель вместо ~69): планировщик в тот
    // момент был не готов. Номера вёдер при этом ЧЕСТНЫЕ — они читаются из
    // cAIPriorityThink и от планировщика не зависят.
    {
        int named = 0;
        for (int c = 0; c < kMaxCode; ++c) if (s_goalName[c][0]) ++named;
        if (named < 40) {
            sprintf_s(l, "  *** GOAL NAMES ARE NOT READY (%d of ~69) - every"
                         " \"(slot empty)\" below is unreliable. The BUCKET NUMBERS"
                         " are still valid. Press 'Goal codes' once, then this"
                         " button again, to get the names.", named);
            logFile << l << std::endl;
        }
    }

    // --- ПРОХОД ПО ВЁДРАМ, ГРУППАМИ ------------------------------------------
    // Печатаем ВСЕ строки каждого ведра, а не одну: раньше slotOf[code]
    // перезаписывался, и ведро с тремя строками выглядело как ведро с одной.
    logFile << "  --- buckets ---" << std::endl;
    for (int slot = 0; slot < 48; ++slot) {
        if (slot % 8 == 0) {
            sprintf_s(l, "   [ group %s : absolute slots %d..%d ]",
                      kBucketGroup[slot / 8], slot, slot + 7);
            logFile << l << std::endl;
        }
        uint32_t D[5] = {};
        if (!Runtime::Mem::Rd((void*)(think + 0x38u + (uint32_t)slot * 0x14u), D, sizeof(D)))
            continue;
        const uint32_t count = D[1], capacity = D[2];
        const uintptr_t arr = D[4];
        if (!count || count > 16u || capacity > 16u || count > capacity
            || !Runtime::Mem::LooksHeap(arr)) continue;
        uintptr_t ptrs[16] = {};
        if (!Runtime::Mem::Rd((void*)arr, ptrs, count * sizeof(uintptr_t))) continue;
        for (uint32_t n = 0; n < count; ++n) {
            if (!Runtime::Mem::LooksHeap(ptrs[n])) continue;
            char nm[64] = {};
            if (!Runtime::Mem::NameOfLiveObject(ptrs[n], nm, sizeof(nm))
                || strcmp(nm, "rAIPriorityThink::cPrioParam") != 0) continue;
            uint32_t code = 0;
            if (!Runtime::Mem::Rd((void*)(ptrs[n] + 0x08), &code, 4)) continue;
            if (code >= (uint32_t)kMaxCode) continue;
            slotOf[code] = slot;
            ++placed;
            char label[32];
            SlotLabel(slot, label, sizeof(label));
            const char* goal = GoalOfCode((int)code);
            sprintf_s(l, "     %-16s abs %2d  code %3u  %-24s%s",
                      label, slot, code, goal,
                      (goal[0] == '(') ? "   <- no goal loaded, this row is dead"
                                       : "");
            logFile << l << std::endl;
        }
    }

    // --- ГЛАВНЫЕ ФИГУРАНТЫ ОТДЕЛЬНОЙ ТАБЛИЧКОЙ -------------------------------
    // 48 строк читать глазами незачем: вопрос трека — четыре кода.
    static const int kWatched[4] = { 54, 57, 1, 28 };
    logFile << "  --- the four codes this track is about ---" << std::endl;
    for (int i = 0; i < 4; ++i) {
        const int c = kWatched[i];
        char label[32];
        SlotLabel(slotOf[c], label, sizeof(label));
        sprintf_s(l, "     code %3d  %-24s %-16s abs %2d",
                  c, GoalOfCode(c), label, slotOf[c]);
        logFile << l << std::endl;
    }

    // Дифф с предыдущим снимком — ради него всё и писалось.
    if (s_bucketSnapValid) {
        logFile << "  --- diff vs previous snapshot (" << s_bucketSnapWho << ") ---"
                << std::endl;
        int diffs = 0;
        for (int c = 0; c < kMaxCode; ++c) {
            if (slotOf[c] == s_bucketSnap[c]) continue;
            ++diffs;
            char was[32], now[32];
            SlotLabel(s_bucketSnap[c], was, sizeof(was));
            SlotLabel(slotOf[c], now, sizeof(now));
            sprintf_s(l, "     code %3d %-20s %-14s -> %-14s  (abs %2d -> %2d, %+d)%s",
                      c, GoalOfCode(c), was, now,
                      s_bucketSnap[c], slotOf[c],
                      (s_bucketSnap[c] >= 0 && slotOf[c] >= 0)
                          ? slotOf[c] - s_bucketSnap[c] : 0,
                      (s_bucketSnap[c] >= 0 && slotOf[c] >= 0
                       && s_bucketSnap[c] / 8 != slotOf[c] / 8)
                          ? "   *** CROSSED INTO ANOTHER GROUP ***" : "");
            logFile << l << std::endl;
        }
        if (!diffs)
            logFile << "     (identical - these two layouts are the same)"
                    << std::endl;
        sprintf_s(l, "  codes differing: %d", diffs);
        logFile << l << std::endl;
    } else {
        logFile << "  (no previous snapshot - change the inclination or the probe"
                   " target and press again to get the diff)" << std::endl;
    }

    for (int c = 0; c < kMaxCode; ++c) s_bucketSnap[c] = slotOf[c];
    s_bucketSnapValid = true;
    sprintf_s(s_bucketSnapWho, "%s | %s", ProbeLabel(), who);

    logFile << "  LIMIT OF THIS INSTRUMENT: it shows WHERE the rows sit, not WHICH"
               " one the planner prefers between two groups. That question is"
               " answered by the bucket sweep, not by this map." << std::endl;

    sprintf_s(s_status, "goap: bucket map - %d codes placed", placed);
}

// --- РАЗВЁРТКА ПО ВЁДРАМ (75.31, ПЕРЕДЕЛАНА в 75.32) ------------------------
//
// ВОПРОС. Кинжалы (code 54) стоят в группе Etc, лук (57) — в PL_Party.
// Правило соревнования МЕЖДУ группами неизвестно. Значит надо не
// рассуждать, а протащить строку кинжалов по всей шкале и в каждом
// положении посчитать долю боевых кадров в ближнем бою.
//
// ПОЧЕМУ ПЕРВАЯ ВЕРСИЯ СОБРАЛА НОЛЬ КАДРОВ (лог 16, разбор).
//
// Она двигала ОДНО зашитое правило — «Guardian primary, AddS32 -3». А у
// пешки в замере было `Utilitarian 800 / Scather 700`, Guardian вообще не
// в тройке. Правило к этой пешке не применяется, писать в него бесполезно,
// и вдобавок оно даже не разрешалось: резолв запускался только когда
// значение отличается от ванильного, а первая ступень как раз ванильная.
// Прибор молчал 600 кадров и напечатал «slot (nowhere)».
//
// Ошибка в устройстве, а не в игре: прибор требовал от пешки конкретной
// личности и не проверял, выполнено ли требование.
//
// КАК СДЕЛАНО ТЕПЕРЬ. У строки code 54 несколько правил личности
// (Scather primary +5, Scather secondary +2, Medicant tertiary -1,
// Guardian primary -3, Guardian secondary -2). Какое из них действует —
// зависит от склонностей ИМЕННО ЭТОЙ пешки. Прибор:
//
//   1) находит строку code 54 в живых вёдрах пешки;
//   2) читает все её правила вместе с проверкой (склонность + ранг);
//   3) читает живые склонности пешки и строит её собственный порядок
//      (первичная / вторичная / третичная);
//   4) выбирает ДЕЙСТВУЮЩЕЕ правило — то, чья проверка совпала;
//   5) дальше работает сервоприводом: у каждой ступени есть ЦЕЛЕВОЙ
//      абсолютный слот, прибор читает живой слот и подкручивает AddS32
//      выбранного правила на разницу, пока не сойдётся. Кадры считаются
//      ТОЛЬКО когда строка реально стоит в целевом слоте.
//
// Пункт 5 — главное отличие. Мы больше не верим, что «-4 значит слот 40»:
// мы смотрим, куда движок положил строку, и правим по факту.
//
// ПРЕДЕЛ ПРИМЕНИМОСТИ (обязателен по FIX_RULES):
//   - меряет ОДНУ пешку — ту, что выбрана в `probe target`;
//   - нужна пешка, у которой есть хоть одно действующее правило на code 54
//     (Scather, Medicant или Guardian в тройке). Иначе строку нечем
//     двигать, и прибор откажется стартовать, назвав причину;
//   - нужны кинжалы в руках, иначе ближнего боя не будет ни в каком слоте;
//   - считаются только боевые кадры и только при сошедшемся слоте;
//   - отвечает «где включается», а НЕ «почему».
//
// Целевые слоты подобраны по началам групп и по слоту самого лука:
//   46 Etc-06 (как есть)  44 Etc-04 (база ресурса)  41 Etc-01  40 Etc-00
//   32 Wait_Follow-00     24 Enemy-00               16 Situ_Personal-00
//   11 PL_Party-03 (ровно там, где лук)
// Первая ступень — ВАНИЛЬНОЕ положение при Guardian (41). С неё начинаем,
// чтобы в одном прогоне была и точка отсчёта, и всё остальное.
static const int kSweepTargets[] = { 41, 46, 40, 32, 24, 11 };
static const int kSweepCount = (int)(sizeof(kSweepTargets) / sizeof(kSweepTargets[0]));

static const int kSweepCode = 54;          // WpnDaggerAtk

static bool     s_sweepOn = false;
static int      s_sweepStep = 0;
static uint32_t s_sweepFramesPerStep = 500;   // боевых кадров на ступень
static uint32_t s_sweepCombat = 0;            // боевых кадров в целевом слоте
static uint32_t s_sweep54 = 0, s_sweep57 = 0, s_sweep1 = 0, s_sweep28 = 0;
static uint32_t s_sweepEnter54 = 0;
static int32_t  s_sweepLastCode = -12345;
static DWORD    s_sweepStepStart = 0;
static DWORD    s_sweepLastServo = 0;
static DWORD    s_sweepLastBeat = 0;
static uint32_t s_sweepNotSettled = 0;        // кадров ждали сходимости
static int      s_sweepSeenSlot = -1;

// ВСЕ правила строки code 54 как КАНДИДАТЫ В РЫЧАГИ.
//
// Лог 16a заставил переделать и это. Пешке выставили `Guardian primary 997`,
// а строка кинжалов осталась в том же ведре 46, что и при
// `Utilitarian / Scather` — дифф карт вёдер: «codes differing: 0».
// То есть раскладка НЕ пересчиталась от смены склонности вживую.
//
// Значит нельзя выбирать рычаг по проверке правила и на этом успокаиваться:
// «действующее по бумаге» правило может не двигать строку вовсе. Прибор
// обязан проверить это САМ — записью и наблюдением, а не рассуждением.
//
// Поэтому держим список всех правил строки. Первым пробуем то, чья проверка
// совпала со склонностями пешки; если строка на него не отзывается —
// говорим об этом вслух, откатываем и берём следующее. Кто реально двигает
// строку, выясняется опытом.
struct SweepCand {
    uintptr_t ptr, vt;
    int32_t   vanilla;
    int       incl, rank, idx;
    bool      active;      // проверка совпала со склонностями пешки
    bool      dead;        // проверено: строку не двигает
};
static SweepCand s_sweepCand[16];
static int       s_sweepCandCount = 0;
static int       s_sweepCandCur = -1;

static uintptr_t s_sweepRule = 0;        // адрес cCodeParam текущего рычага
static uintptr_t s_sweepRuleVt = 0;      // его vtable — сторож живости
static int32_t   s_sweepRuleVanilla = 0; // исходный AddS32 (для отката)
static int       s_sweepRuleIncl = -1, s_sweepRuleRank = -1, s_sweepRuleIdx = -1;
static int       s_sweepBaseSlot = -1;   // слот при исходных значениях
static int       s_sweepExpectSlot = -1; // слот, который следует из суммы активных правил
// Сторож застревания: сколько раз подряд подкрутили рычаг без движения строки.
static int       s_sweepStall = 0;
static int       s_sweepStallSlot = -1;

struct SweepRow {
    int target; int slot; uint32_t combat;
    uint32_t f54, f57, f1, f28, enter54; int32_t add;
};
static SweepRow s_sweepRows[16];
static int      s_sweepRowCount = 0;

// Где строка code 54 лежит ПРЯМО СЕЙЧАС. Читаем каждый раз заново: смысл
// сервопривода в том, чтобы верить факту, а не собственной записи.
static int SweepLiveSlot(uintptr_t think)
{
    if (!think) return -1;
    for (int slot = 0; slot < 48; ++slot) {
        uint32_t D[5] = {};
        if (!Runtime::Mem::Rd((void*)(think + 0x38u + (uint32_t)slot * 0x14u), D, sizeof(D)))
            continue;
        const uint32_t count = D[1], capacity = D[2];
        const uintptr_t arr = D[4];
        if (!count || count > 16u || capacity > 16u || count > capacity
            || !Runtime::Mem::LooksHeap(arr)) continue;
        uintptr_t ptrs[16] = {};
        if (!Runtime::Mem::Rd((void*)arr, ptrs, count * sizeof(uintptr_t))) continue;
        for (uint32_t n = 0; n < count; ++n) {
            if (!Runtime::Mem::LooksHeap(ptrs[n])) continue;
            char nm[64] = {};
            if (!Runtime::Mem::NameOfLiveObject(ptrs[n], nm, sizeof(nm))
                || strcmp(nm, "rAIPriorityThink::cPrioParam") != 0) continue;
            uint32_t code = 0;
            if (!Runtime::Mem::Rd((void*)(ptrs[n] + 0x08), &code, 4)) continue;
            if ((int)code == kSweepCode) return slot;
        }
    }
    return -1;
}

// cAIPriorityThink выбранной пешки.
static uintptr_t SweepThink(uintptr_t pawn)
{
    uintptr_t ctrl = 0;
    if (Runtime::Mem::RdPtr((void*)(pawn + 0x2E64), &ctrl) && ctrl) {
        char nm[48] = {};
        if (!Runtime::Mem::NameOfLiveObject(ctrl, nm, sizeof(nm))
            || strcmp(nm, "cAICtrl") != 0) ctrl = 0;
    }
    if (!ctrl) ctrl = Runtime::FindChildByClass(pawn, kPawnBodyBytes, "cAICtrl", 0);
    return ctrl ? Runtime::FindChildByClass(ctrl, 704, "cAIPriorityThink", 0) : 0;
}

// Строка cPrioParam с кодом 54 в вёдрах этой пешки.
static uintptr_t SweepFindRow(uintptr_t think, int* slotOut)
{
    if (slotOut) *slotOut = -1;
    for (int slot = 0; slot < 48; ++slot) {
        uint32_t D[5] = {};
        if (!Runtime::Mem::Rd((void*)(think + 0x38u + (uint32_t)slot * 0x14u), D, sizeof(D)))
            continue;
        const uint32_t count = D[1], capacity = D[2];
        const uintptr_t arr = D[4];
        if (!count || count > 16u || capacity > 16u || count > capacity
            || !Runtime::Mem::LooksHeap(arr)) continue;
        uintptr_t ptrs[16] = {};
        if (!Runtime::Mem::Rd((void*)arr, ptrs, count * sizeof(uintptr_t))) continue;
        for (uint32_t n = 0; n < count; ++n) {
            if (!Runtime::Mem::LooksHeap(ptrs[n])) continue;
            char nm[64] = {};
            if (!Runtime::Mem::NameOfLiveObject(ptrs[n], nm, sizeof(nm))
                || strcmp(nm, "rAIPriorityThink::cPrioParam") != 0) continue;
            uint32_t code = 0;
            if (!Runtime::Mem::Rd((void*)(ptrs[n] + 0x08), &code, 4)) continue;
            if ((int)code != kSweepCode) continue;
            if (slotOut) *slotOut = slot;
            return ptrs[n];
        }
    }
    return 0;
}

// Собственный порядок склонностей пешки: 0 — первичная, 1 — вторичная,
// 2 — третичная, -1 — вне тройки. Правило личности действует только если
// его проверка совпала и по склонности, и по рангу.
static bool SweepInclRanks(uintptr_t pawn, int* rankOf, float* valOut)
{
    float live[9] = {};
    if (!Runtime::PawnInclinationsLive(pawn, live)) return false;
    for (int i = 0; i < 9; ++i) { rankOf[i] = -1; valOut[i] = live[i]; }
    bool used[9] = {};
    for (int r = 0; r < 3; ++r) {
        int best = -1;
        for (int i = 0; i < 9; ++i) {
            if (used[i]) continue;
            if (best < 0 || live[i] > live[best]) best = i;
        }
        if (best < 0) break;
        used[best] = true;
        rankOf[best] = r;
    }
    return true;
}

// Взять кандидата i в качестве текущего рычага.
static void SweepTakeCand(int i)
{
    SweepCand& C = s_sweepCand[i];
    s_sweepCandCur = i;
    s_sweepRule = C.ptr;
    s_sweepRuleVt = C.vt;
    s_sweepRuleVanilla = C.vanilla;
    s_sweepRuleIncl = C.incl;
    s_sweepRuleRank = C.rank;
    s_sweepRuleIdx = C.idx;
    s_sweepStall = 0;
    s_sweepStallSlot = -1;
    char l[240];
    sprintf_s(l, "  lever: rule[%d] of code 54 (%s %s), vanilla AddS32 %+d%s",
              C.idx, (C.incl >= 0 && C.incl < 9) ? InclNameOf(C.incl) : "?",
              RankNameOf(C.rank), C.vanilla,
              C.active ? "  (matches this pawn's inclinations)"
                       : "  (does NOT match - taken because nothing else moved the row)");
    logFile << l << std::endl;
}

// Найти ДЕЙСТВУЮЩЕЕ правило строки code 54 и запомнить его. Печатает все
// правила строки с пометкой, какое активно: если активного нет, читатель
// сразу видит, почему прибор отказался.
static bool SweepResolveRule(uintptr_t pawn, uintptr_t think)
{
    s_sweepRule = s_sweepRuleVt = 0;
    s_sweepRuleIncl = s_sweepRuleRank = s_sweepRuleIdx = -1;
    s_sweepBaseSlot = -1;
    s_sweepExpectSlot = -1;
    s_sweepCandCount = 0;
    s_sweepCandCur = -1;

    int slot = -1;
    const uintptr_t row = SweepFindRow(think, &slot);
    char l[260];
    if (!row) {
        logFile << "GoapProbe: sweep - no cPrioParam row with code 54 in this"
                   " pawn's buckets" << std::endl;
        return false;
    }
    int rankOf[9]; float val[9];
    if (!SweepInclRanks(pawn, rankOf, val)) {
        logFile << "GoapProbe: sweep - cannot read this pawn's live inclinations"
                << std::endl;
        return false;
    }
    {
        int p = 0, s2 = -1, t = -1;
        for (int i = 0; i < 9; ++i) {
            if (rankOf[i] == 0) p = i;
            else if (rankOf[i] == 1) s2 = i;
            else if (rankOf[i] == 2) t = i;
        }
        sprintf_s(l, "  pawn inclinations: %s %.0f (primary), %s %.0f (secondary),"
                     " %s %.0f (tertiary)",
                  InclNameOf(p), val[p],
                  s2 >= 0 ? InclNameOf(s2) : "-", s2 >= 0 ? val[s2] : 0.0f,
                  t  >= 0 ? InclNameOf(t)  : "-", t  >= 0 ? val[t]  : 0.0f);
        logFile << l << std::endl;
    }

    uint32_t R[16] = {};
    if (!Runtime::Mem::Rd((void*)row, R, sizeof(R))) return false;
    const uint32_t nPers = R[7];            // +0x1C
    const uintptr_t persArr = R[10];        // +0x28
    if (!nPers || nPers > 16u || !Runtime::Mem::LooksHeap(persArr)) {
        logFile << "GoapProbe: sweep - the code 54 row has no personality rules"
                << std::endl;
        return false;
    }
    uintptr_t rulePtr[16] = {};
    if (!Runtime::Mem::Rd((void*)persArr, rulePtr, nPers * sizeof(uintptr_t)))
        return false;

    int32_t activeSum = 0;
    sprintf_s(l, "  code 54 row at 0x%08X, live slot %d, %u personality rules:",
              (unsigned)row, slot, nPers);
    logFile << l << std::endl;

    for (uint32_t k = 0; k < nPers; ++k) {
        const uintptr_t rp = rulePtr[k];
        if (!Runtime::Mem::LooksHeap(rp)) continue;
        uint32_t C[10] = {};
        if (!Runtime::Mem::Rd((void*)rp, C, sizeof(C))) continue;
        const int32_t addS32 = (int32_t)C[1];
        const uint32_t nChk = C[5];
        const uintptr_t chkArr = C[8];
        int inclId = -1, rank = -1;
        if (nChk && nChk <= 8u && Runtime::Mem::LooksHeap(chkArr)) {
            uintptr_t cp = 0;
            if (Runtime::Mem::RdPtr((void*)chkArr, &cp) && Runtime::Mem::LooksHeap(cp)) {
                uint32_t K[3] = {};
                if (Runtime::Mem::Rd((void*)cp, K, sizeof(K))) {
                    inclId = (int)K[1];
                    rank = (int)K[2];
                }
            }
        }
        // РАНГ В ПРОВЕРКЕ ПЕРЕВЁРНУТ ОТНОСИТЕЛЬНО ЗДРАВОГО СМЫСЛА.
        //
        // В поле `チェックする状態` 2 = первичная, 1 = вторичная,
        // 0 = третичная. RankNameOf это знает и печатает правильно, а
        // сравнение писал я — и сравнил «в лоб», со своей нумерацией, где
        // 0 = первичная. Итог в логе 17: у пешки `Guardian 1000`, а прибор
        // объявил действующим правило `Scather primary` и заодно соврал
        // про «stale layout».
        //
        // Спасло то, что прибор не поверил бумаге и проверил рычаг записью:
        // rule[3] строку не сдвинул, прибор сказал об этом вслух и перешёл
        // к rule[0] — настоящему. Правило «проверку выполняет мод»
        // окупилось буквально в первом же прогоне.
        const bool active = (inclId >= 0 && inclId < 9
                             && rank >= 0 && rank <= 2
                             && rankOf[inclId] == (2 - rank));
        sprintf_s(l, "     rule[%u] AddS32 %+d  <- %s %s%s",
                  k, addS32,
                  (inclId >= 0 && inclId < 9) ? InclNameOf(inclId) : "?",
                  RankNameOf(rank), active ? "   *** ACTIVE on this pawn ***" : "");
        logFile << l << std::endl;
        if (active) activeSum += addS32;

        uintptr_t vt = 0;
        if (!Runtime::Mem::RdPtr((void*)rp, &vt)) continue;
        if (s_sweepCandCount >= 16) continue;
        SweepCand& CD = s_sweepCand[s_sweepCandCount++];
        CD.ptr = rp; CD.vt = vt; CD.vanilla = addS32;
        CD.incl = inclId; CD.rank = rank; CD.idx = (int)k;
        CD.active = active; CD.dead = false;
    }

    if (!s_sweepCandCount) {
        logFile << "GoapProbe: sweep REFUSED - the code 54 row has no readable"
                   " personality rules, so there is no lever at all." << std::endl;
        return false;
    }

    // СВЕРКА РАСКЛАДКИ С БУМАГОЙ. Если живой слот не равен базе плюс сумме
    // активных правил, значит раскладка посчитана не по нынешним склонностям.
    // Это надо сказать сразу и громко: именно на этом сгорел лог 16a.
    s_sweepExpectSlot = 44 + activeSum;     // 44 — база строки 54 в cmc.prt
    sprintf_s(l, "  layout check: active rules sum %+d, so the row SHOULD sit at"
                 " slot %d; it actually sits at %d%s",
              activeSum, s_sweepExpectSlot, slot,
              (slot == s_sweepExpectSlot)
                  ? "  - consistent"
                  : "  *** MISMATCH: the bucket layout was NOT recomputed from the"
                    " current inclinations (stale layout) ***");
    logFile << l << std::endl;
    s_sweepBaseSlot = (slot >= 0) ? slot - activeSum : -1;

    // Первым берём активное правило, если оно есть; иначе первое попавшееся.
    s_sweepCandCur = -1;
    for (int i = 0; i < s_sweepCandCount && s_sweepCandCur < 0; ++i)
        if (s_sweepCand[i].active) s_sweepCandCur = i;
    if (s_sweepCandCur < 0) s_sweepCandCur = 0;
    SweepTakeCand(s_sweepCandCur);
    return true;
}

static bool SweepRuleAlive()
{
    if (!s_sweepRule) return false;
    uintptr_t vt = 0;
    return Runtime::Mem::RdPtr((void*)s_sweepRule, &vt) && vt == s_sweepRuleVt;
}

static int32_t SweepReadAdd()
{
    int32_t v = 0;
    Runtime::Mem::Rd((void*)(s_sweepRule + 0x04), &v, 4);
    return v;
}

// validate -> write -> readback. Ничего «попробуем и посмотрим».
static bool SweepWriteAdd(int32_t v)
{
    if (v < -64 || v > 64) return false;
    if (!SweepRuleAlive()) return false;
    const int32_t before = SweepReadAdd();
    if (!Runtime::Mem::WrSafe((void*)(s_sweepRule + 0x04), &v, 4)) return false;
    int32_t back = 0;
    if (!Runtime::Mem::Rd((void*)(s_sweepRule + 0x04), &back, 4) || back != v) {
        Runtime::Mem::WrSafe((void*)(s_sweepRule + 0x04), &before, 4);
        return false;
    }
    return true;
}

// ОТКАТ ВСЕГО, ЧТО ТРОГАЛИ. Кандидатов могло быть несколько: рычаг, который
// не двигал строку, тоже был записан и тоже обязан вернуться к ванили.
static void SweepRestoreRule()
{
    int restored = 0, lost = 0;
    for (int i = 0; i < s_sweepCandCount; ++i) {
        SweepCand& C = s_sweepCand[i];
        if (!C.ptr) continue;
        uintptr_t vt = 0;
        if (!Runtime::Mem::RdPtr((void*)C.ptr, &vt) || vt != C.vt) { ++lost; continue; }
        int32_t cur = 0;
        if (Runtime::Mem::Rd((void*)(C.ptr + 0x04), &cur, 4) && cur == C.vanilla)
            continue;                        // не трогали или уже вернули
        if (Runtime::Mem::WrSafe((void*)(C.ptr + 0x04), &C.vanilla, 4)) {
            ++restored;
            char l[200];
            sprintf_s(l, "GoapProbe: sweep - rule[%d] of code 54 restored to vanilla %+d",
                      C.idx, C.vanilla);
            logFile << l << std::endl;
        }
    }
    if (lost)
        logFile << "GoapProbe: sweep - some rule pointers went stale, nothing to"
                   " restore there (the body was re-created; the resource reloads"
                   " with it)" << std::endl;
    if (!restored && !lost)
        logFile << "GoapProbe: sweep - nothing to roll back, all rules already"
                   " hold their vanilla values" << std::endl;
    s_sweepRule = s_sweepRuleVt = 0;
    s_sweepCandCount = 0;
    s_sweepCandCur = -1;
}

static void SweepResetStep()
{
    s_sweepCombat = 0;
    s_sweep54 = s_sweep57 = s_sweep1 = s_sweep28 = 0;
    s_sweepEnter54 = 0;
    s_sweepLastCode = -12345;
    s_sweepNotSettled = 0;
    s_sweepSeenSlot = -1;
    s_sweepStepStart = MsNow();
    s_sweepLastServo = 0;
    s_sweepLastBeat = MsNow();
}

static void SweepAnnounceStep()
{
    char l[220];
    sprintf_s(l, "GoapProbe: sweep step %d/%d - target slot %d (%s), need %u"
                 " combat frames while the row actually sits there",
              s_sweepStep + 1, kSweepCount, kSweepTargets[s_sweepStep],
              GroupOfSlot(kSweepTargets[s_sweepStep]), s_sweepFramesPerStep);
    logFile << l << std::endl;
}

static void SweepCloseStep()
{
    if (s_sweepRowCount < 16) {
        SweepRow& R = s_sweepRows[s_sweepRowCount++];
        R.target = kSweepTargets[s_sweepStep];
        R.slot = s_sweepSeenSlot;
        R.combat = s_sweepCombat;
        R.f54 = s_sweep54; R.f57 = s_sweep57;
        R.f1 = s_sweep1;   R.f28 = s_sweep28;
        R.enter54 = s_sweepEnter54;
        R.add = s_sweepRule ? SweepReadAdd() : 0;
    }
    char l[260], label[32];
    SlotLabel(s_sweepSeenSlot, label, sizeof(label));
    const unsigned pct = s_sweepCombat ? (unsigned)((s_sweep54 * 100) / s_sweepCombat) : 0;
    sprintf_s(l, "GoapProbe: sweep step %d done - target %d, reached %d %s |"
                 " dagger %u/%u combat frames (%u%%), %u entries",
              s_sweepStep + 1, kSweepTargets[s_sweepStep], s_sweepSeenSlot, label,
              s_sweep54, s_sweepCombat, pct, s_sweepEnter54);
    logFile << l << std::endl;
}

void DumpSweepReport()
{
    logFile << "GoapProbe: === bucket sweep report (code 54 across the scale) ==="
            << std::endl;
    logFile << "  target: " << ProbeLabel() << std::endl;
    char l[300];
    sprintf_s(l, "  %u combat frames per step, %d steps completed, base slot %d",
              s_sweepFramesPerStep, s_sweepRowCount, s_sweepBaseSlot);
    logFile << l << std::endl;
    if (!s_sweepRowCount) {
        logFile << "  no completed steps - the sweep needs actual fighting with"
                   " the row settled in the target slot." << std::endl;
        return;
    }
    logFile << "  want  got  bucket           AddS32   combat  dagger54    %"
               "  entries   bow57  follow1  standoff28" << std::endl;
    int best = 0;
    for (int i = 0; i < s_sweepRowCount; ++i) {
        SweepRow& R = s_sweepRows[i];
        char label[32];
        SlotLabel(R.slot, label, sizeof(label));
        const unsigned pct = R.combat ? (unsigned)((R.f54 * 100) / R.combat) : 0;
        sprintf_s(l, "  %4d %4d  %-16s %+6d  %7u  %8u %4u%%  %7u %7u %8u %11u",
                  R.target, R.slot, label, R.add, R.combat, R.f54, pct,
                  R.enter54, R.f57, R.f1, R.f28);
        logFile << l << std::endl;
        const unsigned bp = s_sweepRows[best].combat
            ? (unsigned)((s_sweepRows[best].f54 * 100) / s_sweepRows[best].combat) : 0;
        if (pct > bp) best = i;
    }
    char label[32];
    SlotLabel(s_sweepRows[best].slot, label, sizeof(label));
    sprintf_s(l, "  BEST: slot %d %s, %u dagger frames of %u combat",
              s_sweepRows[best].slot, label,
              s_sweepRows[best].f54, s_sweepRows[best].combat);
    logFile << l << std::endl;
    logFile << "  LIMIT: this says WHERE the dagger row starts winning, not WHY."
               " Steps were measured in different fights against different enemies,"
               " so treat small differences as noise and only trust a step change."
            << std::endl;
}

void ToggleSlotSweep()
{
    if (s_sweepOn) {
        SweepCloseStep();
        s_sweepOn = false;
        SweepRestoreRule();
        Runtime::GuardianFixHold(false, -3);
        logFile << "GoapProbe: bucket sweep OFF" << std::endl;
        DumpSweepReport();
        lstrcpynA(s_status, "goap: sweep off (report written)", sizeof(s_status));
        return;
    }

    if (!Runtime::Mem::InWorld()) {
        lstrcpynA(s_status, "goap: not in an active save", sizeof(s_status));
        return;
    }
    // Развёртка ПИШЕТ в правило. Если доктрина включена, у рычага два
    // хозяина и оба измерения будут враньём — отказываемся вслух.
    if (PawnAI::GuardianDoctrineOwnsRule()) {
        lstrcpynA(s_status,
                  "goap: turn the Guardian fix checkbox OFF first (two owners)",
                  sizeof(s_status));
        logFile << "GoapProbe: sweep REFUSED - the Guardian doctrine owns the same"
                   " rule. Uncheck the Guardian fix, then start the sweep."
                << std::endl;
        return;
    }
    const uintptr_t pawn = ProbeBody();
    if (!BodyAlive(pawn)) {
        lstrcpynA(s_status, "goap: pawn body not ready", sizeof(s_status));
        return;
    }
    const uintptr_t think = SweepThink(pawn);
    if (!think) {
        lstrcpynA(s_status, "goap: cAIPriorityThink not found", sizeof(s_status));
        logFile << "GoapProbe: sweep REFUSED - cAIPriorityThink not found"
                << std::endl;
        return;
    }

    logFile << "GoapProbe: === bucket sweep ON ===" << std::endl;
    logFile << "  target: " << ProbeLabel() << std::endl;
    if (!SweepResolveRule(pawn, think)) {
        lstrcpynA(s_status, "goap: sweep refused - no active code 54 rule",
                  sizeof(s_status));
        return;
    }

    // Развёртке нужен кэш планировщика, а его ведёт слежение за кодом.
    // Заодно получим обычную гистограмму за тот же прогон — она бесплатна.
    if (!CodeWatchActive()) ToggleCodeWatch();

    // Доктрина зовёт GuardianFixTick каждый кадр; на время замера рычаг
    // объявлен нашим, и её tick становится пустым.
    Runtime::GuardianFixHold(true, -3);

    s_sweepRowCount = 0;
    s_sweepStep = 0;
    s_sweepOn = true;
    SweepResetStep();
    logFile << "  It walks the code 54 row across the 48 buckets and counts how"
               " much of each fight the pawn spends in melee. Keep fighting;"
               " frames count only while the row is settled in the target slot."
            << std::endl;
    SweepAnnounceStep();
    lstrcpynA(s_status, "goap: sweep running - go fight", sizeof(s_status));
}

bool SlotSweepActive() { return s_sweepOn; }

static void SlotSweepTick()
{
    if (!s_sweepOn) return;
    if (!Runtime::Mem::InWorld()) return;
    const uintptr_t pawn = ProbeBody();
    if (!BodyAlive(pawn)) return;
    const uintptr_t think = SweepThink(pawn);
    if (!think) return;
    if (!SweepRuleAlive()) {
        // Тело пересоздано — правило надо искать заново, иначе мы пишем в
        // чужую память и молчим об этом.
        if (!SweepResolveRule(pawn, think)) {
            logFile << "GoapProbe: sweep - lost the rule and cannot re-resolve;"
                       " stopping" << std::endl;
            s_sweepOn = false;
            Runtime::GuardianFixHold(false, -3);
            DumpSweepReport();
            return;
        }
    }

    const int want = kSweepTargets[s_sweepStep];
    const int live = SweepLiveSlot(think);
    if (live >= 0) s_sweepSeenSlot = live;
    const DWORD now = MsNow();

    // СЕРВОПРИВОД. Не «мы записали -4, значит слот 40», а «слот 43, до 40
    // не хватает трёх — подкрутим на три». Раз в 400 мс: раскладка
    // пересчитывается движком не мгновенно.
    if (live != want) {
        ++s_sweepNotSettled;
        if (!s_sweepLastServo || now - s_sweepLastServo > 400) {
            s_sweepLastServo = now;
            if (live >= 0) {
                // СТОРОЖ ЗАСТРЕВАНИЯ. Мы уже писали в этот рычаг, а строка
                // стоит там же — значит рычаг не тот. Молчать нельзя: именно
                // так лог 16a выглядел как «прибор сломан», хотя прибор
                // честно писал в правило, которого игра не слушала.
                if (s_sweepStallSlot == live) ++s_sweepStall;
                else { s_sweepStall = 0; s_sweepStallSlot = live; }

                if (s_sweepStall >= 5) {
                    char l[260];
                    sprintf_s(l, "GoapProbe: sweep - rule[%d] does NOT move the row"
                                 " (5 pushes, still at slot %d). This rule is not the"
                                 " lever for this pawn.", s_sweepRuleIdx, live);
                    logFile << l << std::endl;
                    // Вернуть этому кандидату ваниль и взять следующего.
                    if (SweepRuleAlive())
                        Runtime::Mem::WrSafe((void*)(s_sweepRule + 0x04),
                                             &s_sweepRuleVanilla, 4);
                    if (s_sweepCandCur >= 0 && s_sweepCandCur < s_sweepCandCount)
                        s_sweepCand[s_sweepCandCur].dead = true;
                    int next = -1;
                    for (int i = 0; i < s_sweepCandCount && next < 0; ++i)
                        if (!s_sweepCand[i].dead && s_sweepCand[i].active) next = i;
                    for (int i = 0; i < s_sweepCandCount && next < 0; ++i)
                        if (!s_sweepCand[i].dead) next = i;
                    if (next < 0) {
                        logFile << "GoapProbe: sweep STOPPED - none of the code 54"
                                   " rules moves the row. The bucket layout is not"
                                   " being recomputed from the rules right now."
                                   " THIS IS THE FINDING - send the log."
                                << std::endl;
                        s_sweepOn = false;
                        SweepRestoreRule();
                        Runtime::GuardianFixHold(false, -3);
                        DumpSweepReport();
                        return;
                    }
                    SweepTakeCand(next);
                    return;
                }

                const int32_t cur = SweepReadAdd();
                const int32_t next2 = cur + (int32_t)(want - live);
                if (next2 != cur && !SweepWriteAdd(next2)) {
                    char l[200];
                    sprintf_s(l, "GoapProbe: sweep - write of AddS32 %+d REFUSED"
                                 " (out of range or readback failed)", next2);
                    logFile << l << std::endl;
                }
            }
        }
    } else {
        s_sweepStall = 0;
        s_sweepStallSlot = -1;
    }

    // ПУЛЬС. Прибор обязан говорить на протяжении опыта, а не в конце.
    if (now - s_sweepLastBeat > 10000) {
        s_sweepLastBeat = now;
        char l[240];
        sprintf_s(l, "GoapProbe: sweep step %d/%d - want slot %d, row is at %d,"
                     " lever rule[%d] AddS32 %+d | combat frames %u/%u | waited %u"
                     " frames for the row to settle | %s",
                  s_sweepStep + 1, kSweepCount, want, live, s_sweepRuleIdx,
                  SweepReadAdd(), s_sweepCombat, s_sweepFramesPerStep,
                  s_sweepNotSettled, IsInCombat() ? "in combat" : "out of combat");
        logFile << l << std::endl;
    }

    if (live != want) return;          // не сошлось — не считаем
    if (!s_planner) return;            // планировщик кэширует общий Tick()
    int32_t code = -1;
    if (!Runtime::Mem::Rd((void*)(s_planner + 0x17C), &code, 4)) return;
    if (!IsInCombat()) return;

    ++s_sweepCombat;
    if (code == 54) ++s_sweep54;
    else if (code == 57) ++s_sweep57;
    else if (code == 1)  ++s_sweep1;
    else if (code == 28) ++s_sweep28;
    if (code == 54 && s_sweepLastCode != 54) ++s_sweepEnter54;
    if (code >= 0) s_sweepLastCode = code;

    if (s_sweepCombat < s_sweepFramesPerStep) return;

    SweepCloseStep();
    ++s_sweepStep;
    if (s_sweepStep >= kSweepCount) {
        s_sweepOn = false;
        SweepRestoreRule();
        Runtime::GuardianFixHold(false, -3);
        logFile << "GoapProbe: bucket sweep COMPLETE" << std::endl;
        DumpSweepReport();
        lstrcpynA(s_status, "goap: sweep complete (report written)", sizeof(s_status));
        return;
    }
    SweepResetStep();
    SweepAnnounceStep();
    sprintf_s(s_status, "goap: sweep step %d/%d (target slot %d)",
              s_sweepStep + 1, kSweepCount, kSweepTargets[s_sweepStep]);
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
// Сколько раз за замер планировщик пропадал из-под нас. Ненулевое
// значение объясняет короткую или пустую гистограмму лучше любых догадок.
static uint32_t s_lostPlanner = 0;
// СОСТОЯНИЕ ФИКСА НАДО МЕРИТЬ, А НЕ ПОДСМАТРИВАТЬ В КОНЦЕ.
//
// В шапке печаталось «guardian fix: not applied | vanilla (rolled back
// x6)» — при том что галка стояла и в логе было сорок применений за бой.
// Никакого противоречия: фикс по устройству включается ТОЛЬКО пока угроза
// в зоне, а гистограмма печатается, когда бой уже кончился. Мы смотрели
// на мгновенное состояние и принимали его за итог.
//
// Считаем как всё остальное — по кадрам замера.
// --- ПОВОДОК: НАСКОЛЬКО БЛИЗКО ПЕШКА ДЕРЖИТСЯ К АРИЗЕНУ (75.41) -------------
//
// Вопрос тестера: «весь этот вой на форумах был из-за страйдера, который не
// мог использовать кинжалы?» Разбор показал, что нет: у Guardian есть ещё
// жалоба «липнет ко мне и не даёт кастовать», а в таблице приоритетов НЕТ
// ни одного правила, которое тянуло бы пешку к игроку. Значит липкость
// делает какая-то другая система, которой мы ещё не видели.
//
// Прежде чем её искать, надо доказать, что она вообще есть. Меряем самое
// простое и самое прямое: расстояние «пешка -> Аризен» в бою. Если у
// Guardian оно систематически меньше, чем у той же пешки без Guardian, —
// липкость реальна и её источник стоит искать. Если распределения совпали —
// жалоба была следствием запрета на ближний бой, и она уже вылечена.
//
// Гистограмма, а не среднее: среднее одинаково у «стоит в трёх метрах» и у
// «мечется между нулём и шестью», а это разное поведение.
static const int   kDistBuckets = 8;
static const float kDistEdge[kDistBuckets] = {
    2.0f, 4.0f, 6.0f, 8.0f, 12.0f, 20.0f, 35.0f, 1e9f
};
static const char* kDistName[kDistBuckets] = {
    "0-2 m", "2-4 m", "4-6 m", "6-8 m", "8-12 m", "12-20 m", "20-35 m", "35+ m"
};
static uint32_t s_dist[kDistBuckets];
static uint32_t s_distSamples = 0;
static double   s_distSum = 0.0;
static float    s_distMin = 1e9f, s_distMax = 0.0f;

// ОДНОЙ ДИСТАНЦИИ МАЛО (75.43).
//
// Первый замер (лог 21) дал: Guardian — максимум 10.7 м и НОЛЬ кадров
// дальше 12 м; Scather — до 16.8 м и 9 % времени в полосе 12-20 м.
// Похоже на потолок. Но сравнение нечестное: Scather по своей сути гонится
// за врагами, и «дальше от игрока» у него значит «ближе к врагу», а вовсе
// не «поводок отпустил».
//
// Отделить одно от другого можно только второй меркой: расстоянием до
// ближайшего врага. Тогда каждый боевой кадр раскладывается на четыре
// клетки, и жалоба с форума получает своё число:
//
//     рядом с игроком + врагов рядом нет  = «стоит возле тебя без дела»
//     рядом с игроком + враг рядом        = дерётся у тебя под боком
//     далеко от игрока + враг рядом       = дерётся где-то там (норма)
//     далеко от игрока + врагов нет       = бежит, ищет
//
// Первая клетка — это буквально «having a pawn sitting next to you instead
// of fighting». Её долю мы и меряем.
static const float kNearArisen = 6.0f;   // м
static const float kNearEnemy  = 8.0f;   // м
static uint32_t s_cell[4];               // [near you][near enemy] -> 0..3
static uint32_t s_cellSamples = 0;
static double   s_enemySum = 0.0;
static uint32_t s_enemySamples = 0;
static float    s_lastEnemyDist = -1.0f;
static int      s_enemyPhase = 0;        // считаем врагов не каждый кадр

static uint32_t s_fixApplied = 0;      // сэмплов, когда правка правила была активна
static uint32_t s_leverActive = 0;     // сэмплов, когда поднята склонность

void ResetCodeHistogram()
{
    memset(s_hist, 0, sizeof(s_hist));
    memset(s_histCombat, 0, sizeof(s_histCombat));
    memset(s_enter, 0, sizeof(s_enter));
    s_samples = 0;
    s_samplesCombat = 0;
    s_lastValid = -1;
    s_gapSamples = 0;
    s_lostPlanner = 0;
    s_fixApplied = 0;
    s_leverActive = 0;
    memset(s_dist, 0, sizeof(s_dist));
    memset(s_cell, 0, sizeof(s_cell));
    s_cellSamples = 0;
    s_enemySum = 0.0;
    s_enemySamples = 0;
    s_lastEnemyDist = -1.0f;
    s_enemyPhase = 0;
    s_distSamples = 0;
    s_distSum = 0.0;
    s_distMin = 1e9f;
    s_distMax = 0.0f;
    s_histStart = MsNow();
    lstrcpynA(s_status, "goap: histogram reset", sizeof(s_status));
}

// СВЕРКА РАСКЛАДКИ СО СКЛОННОСТЯМИ (75.44).
//
// Лог 22 принёс невозможное: `Guardian 1000`, эррата ВЫКЛЮЧЕНА, ваниль —
// и 1101 кадр ближнего боя кинжалами. В логах 17a и 21 та же пешка при тех
// же условиях давала РОВНО НОЛЬ на 500 и на 4676 кадрах.
//
// Объяснение у нас уже записано, просто мы не поставили на него прибор:
// раскладка строк по вёдрам считается при СОЗДАНИИ AI и не пересчитывается
// от живой смены склонности (доказано логом 16a). Тестер за сессию несколько
// раз крутил склонности и перезагружался — и в этом прогоне раскладка,
// судя по всему, осталась от прежнего состояния, где Guardian первичным
// не был. То есть замер снят НЕ с той личности, которая написана в шапке.
//
// Отсюда правило: гистограмма обязана печатать, СОВПАДАЕТ ли раскладка с
// нынешними склонностями. Считается это дёшево: где строка code 54 лежит
// сейчас, против того, где ей полагается лежать по сумме действующих
// правил (база 44 из cmc.prt).
static void ReportLayoutConsistency(uintptr_t pawn)
{
    const uintptr_t think = SweepThink(pawn);
    if (!think) {
        logFile << "  layout check: cAIPriorityThink not found - cannot verify"
                << std::endl;
        return;
    }
    int slot = -1;
    const uintptr_t row = SweepFindRow(think, &slot);
    if (!row) {
        logFile << "  layout check: no code 54 row in this pawn's buckets"
                << std::endl;
        return;
    }
    int rankOf[9]; float val[9];
    if (!SweepInclRanks(pawn, rankOf, val)) {
        logFile << "  layout check: inclinations unreadable" << std::endl;
        return;
    }
    uint32_t R[16] = {};
    if (!Runtime::Mem::Rd((void*)row, R, sizeof(R))) return;
    const uint32_t nPers = R[7];
    const uintptr_t persArr = R[10];
    if (!nPers || nPers > 16u || !Runtime::Mem::LooksHeap(persArr)) return;
    uintptr_t rulePtr[16] = {};
    if (!Runtime::Mem::Rd((void*)persArr, rulePtr, nPers * sizeof(uintptr_t))) return;

    int32_t sum = 0;
    for (uint32_t k = 0; k < nPers; ++k) {
        const uintptr_t rp = rulePtr[k];
        if (!Runtime::Mem::LooksHeap(rp)) continue;
        uint32_t C[10] = {};
        if (!Runtime::Mem::Rd((void*)rp, C, sizeof(C))) continue;
        const int32_t addS32 = (int32_t)C[1];
        const uint32_t nChk = C[5];
        const uintptr_t chkArr = C[8];
        int inclId = -1, rank = -1;
        if (nChk && nChk <= 8u && Runtime::Mem::LooksHeap(chkArr)) {
            uintptr_t cp = 0;
            if (Runtime::Mem::RdPtr((void*)chkArr, &cp) && Runtime::Mem::LooksHeap(cp)) {
                uint32_t K[3] = {};
                if (Runtime::Mem::Rd((void*)cp, K, sizeof(K))) {
                    inclId = (int)K[1];
                    rank = (int)K[2];
                }
            }
        }
        // Ранг в проверке перевёрнут: 2 = первичная, 0 = третичная.
        if (inclId >= 0 && inclId < 9 && rank >= 0 && rank <= 2
            && rankOf[inclId] == (2 - rank))
            sum += addS32;
    }
    const int expect = 44 + sum;
    char l[300];
    sprintf_s(l, "  layout check: code 54 sits in slot %d; by the CURRENT"
                 " inclinations it should sit in %d (base 44 %+d)%s",
              slot, expect, sum,
              (slot == expect)
                  ? "  - consistent, this run really is that personality"
                  : "  *** MISMATCH: the bucket layout is STALE - it was built"
                    " when the pawn had OTHER inclinations. THIS RUN DOES NOT"
                    " MEASURE THE PERSONALITY IN THE HEADER. Reload the save"
                    " after changing inclinations. ***");
    logFile << l << std::endl;
}

void DumpCodeHistogram()
{
    logFile << "GoapProbe: === priority code histogram ===" << std::endl;
    logFile << "  target: " << ProbeLabel() << std::endl;

    const uintptr_t pawn = ProbeBody();

    // СОСТОЯНИЕ ФИКСА — ТОЖЕ УСЛОВИЕ ОПЫТА.
    //
    // Три боя A/B прошли с включённой галкой Guardian-фикса, и никто из
    // нас об этом не помнил, потому что в отчёте этого не было. Условия
    // опыта обязаны быть в самом отчёте, все до одного.
    {
        char l0[280];
        const unsigned pct = s_samples ? (unsigned)((s_fixApplied * 100) / s_samples) : 0;
        const unsigned pctL = s_samples ? (unsigned)((s_leverActive * 100) / s_samples) : 0;
        sprintf_s(l0, "  guardian rule-write: active in %u of %u samples (%u%%)"
                      " | inclination lever: active in %u samples (%u%%)",
                  s_fixApplied, s_samples, pct, s_leverActive, pctL);
        logFile << l0 << std::endl;
        sprintf_s(l0, "  state at dump time (NOT the whole run): %s | %s",
                  Runtime::GuardianFixIsApplied() ? "rule-write APPLIED" : "rule-write idle",
                  Runtime::GuardianFixStatus());
        logFile << l0 << std::endl;
    }

    // СКЛОННОСТИ ПРЯМО В ОТЧЁТЕ.
    //
    // Следующий шаг — A/B на ОДНОЙ пешке: Guardian против Scather в двух
    // боях. Два таких отчёта в одном логе неразличимы, если в них не
    // написано, с какой личностью снят каждый. Прибор обязан подписывать
    // условия опыта сам, иначе через час их не восстановит никто.
    if (pBase && *pBase) {
        const uintptr_t rec = (uintptr_t)(*pBase) + 0xA7000 + 0x7F0
                            + (uintptr_t)GetProbePawn() * 0x1660;
        float inc[9] = {};
        bool ok = true;
        for (int i = 0; i < 9 && ok; ++i)
            ok = Runtime::Mem::Rd((void*)(rec + 0x1B90 + i * 0x0C), &inc[i], 4);
        if (ok) {
            // Первичная и вторичная — то, что реально решает.
            int a = 0, b = -1;
            for (int i = 1; i < 9; ++i) if (inc[i] > inc[a]) a = i;
            for (int i = 0; i < 9; ++i) if (i != a && (b < 0 || inc[i] > inc[b])) b = i;
            char l2[240];
            sprintf_s(l2, "  inclinations (RECORD): primary %s %.0f, secondary %s %.0f"
                          "  | Sca %.0f Med %.0f Mit %.0f Cha %.0f Uti %.0f"
                          " Gua %.0f Nex %.0f Pio %.0f Acq %.0f",
                      InclNameOf(a), inc[a], InclNameOf(b), inc[b],
                      inc[0], inc[1], inc[2], inc[3], inc[4], inc[5], inc[6],
                      inc[7], inc[8]);
            logFile << l2 << std::endl;

            // ВТОРОЙ ИСТОЧНИК В ТОЙ ЖЕ ШАПКЕ.
            //
            // Замер 75.16 показал, что запись и игровой профиль могут
            // расходиться. Пока не доказано, кто из них прав, отчёт обязан
            // печатать оба — иначе он снова подпишет опыт не тем условием.
            float live[9] = {};
            if (Runtime::PawnInclinationsLive(pawn, live)) {
                int la = 0, lb = -1;
                for (int i = 1; i < 9; ++i) if (live[i] > live[la]) la = i;
                for (int i = 0; i < 9; ++i)
                    if (i != la && (lb < 0 || live[i] > live[lb])) lb = i;
                sprintf_s(l2, "  inclinations (BODY)  : primary %s %.0f, secondary %s %.0f%s",
                          InclNameOf(la), live[la], InclNameOf(lb), live[lb],
                          (la == a) ? "" : "   <<< DISAGREES WITH THE RECORD");
                logFile << l2 << std::endl;
            } else {
                logFile << "  inclinations (BODY)  : cCmcInfo not readable" << std::endl;
            }
        }
    }
    if (pawn) ReportLayoutConsistency(pawn);

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
    // Слишком короткий прогон сравнивать не с чем — лучше сказать сразу.
    if (secs < 20 || s_samplesCombat < 500)
        logFile << "  *** SHORT RUN - too little combat to compare against"
                   " another one ***" << std::endl;
    if (s_lostPlanner) {
        char w[200];
        sprintf_s(w, "  note: the planner went unreadable %u times during this run"
                     " (body re-created); samples were skipped then",
                  s_lostPlanner);
        logFile << w << std::endl;
    }
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

    // ВОРОТА ЛИЧНОСТИ. Прогон, снятый не с той пешки, не спасает никакая
    // статистика: лог 19 отдал 4543 боевых кадра кастера вместо Страйдерши.
    if (!ProbeIdentityOk()) {
        logFile << "  *** WRONG PAWN - DISCARD THIS RUN ***" << std::endl;
        logFile << "      The body we read does not match the record of the"
                   " selected probe target (its inclinations disagree). The"
                   " party list was rebuilt and no other body matched either."
                   " Re-press 'Party roster', check 'probe target', repeat."
                << std::endl;
    }

    // ВОРОТА ЗДРАВОГО СМЫСЛА.
    //
    // Второй прогон A/B напечатал: «code 0 (slot empty) — 9098 кадров,
    // 1 вход» и был подан как результат. Это не результат: код не менялся
    // ни разу за 151 секунду боя, а имя цели для него — «слот пуст». Так
    // выглядит чтение НЕ ТОГО объекта, а не поведение пешки.
    //
    // Прибор обязан отличать «пешка стояла» от «мы читали мусор» и
    // говорить об этом первым, а не оставлять это читателю.
    {
        int named = 0;
        for (int c = 0; c < kMaxCode; ++c) if (s_goalName[c][0]) ++named;
        const bool tableThin = named < 20;
        int distinct = 0, topCode = -1;
        for (int c = 0; c < kMaxCode; ++c) {
            if (!s_hist[c]) continue;
            ++distinct;
            if (topCode < 0 || s_hist[c] > s_hist[topCode]) topCode = c;
        }
        const bool frozen = (distinct <= 1);
        const bool topUnnamed = (topCode >= 0 && !s_goalName[topCode][0]);
        if (tableThin || frozen || topUnnamed) {
            logFile << "  *** READ TARGET SUSPECT - do not use this run ***"
                    << std::endl;
            char w[240];
            sprintf_s(w, "      goals named %d (expect ~69), distinct codes %d,"
                         " top code %d %s",
                      named, distinct, topCode,
                      topUnnamed ? "has no goal name" : "");
            logFile << w << std::endl;
            logFile << "      Most likely the probe target points at a body that is"
                       " not this pawn's planner (party list was rebuilt between"
                       " runs). Re-press 'Party roster', then repeat."
                    << std::endl;
        }
    }

    // --- ПОВОДОК ------------------------------------------------------------
    if (s_distSamples) {
        logFile << "  --- distance pawn -> Arisen, combat frames only ---"
                << std::endl;
        for (int b = 0; b < kDistBuckets; ++b) {
            const unsigned pct = (unsigned)((s_dist[b] * 100) / s_distSamples);
            char bar[42];
            int n = (int)((pct * 40) / 100);
            if (n > 40) n = 40;
            for (int k = 0; k < n; ++k) bar[k] = '#';
            bar[n] = 0;
            sprintf_s(l, "   %-8s %6u  %3u%%  %s", kDistName[b], s_dist[b], pct, bar);
            logFile << l << std::endl;
        }
        sprintf_s(l, "   samples %u | mean %.1f m | min %.1f m | max %.1f m",
                  s_distSamples, (float)(s_distSum / (double)s_distSamples),
                  s_distMin, s_distMax);
        logFile << l << std::endl;

        // --- ЧЕТЫРЕ КЛЕТКИ: ГДЕ ОНА И ЕСТЬ ЛИ ТАМ КОГО БИТЬ ----------------
        if (s_cellSamples) {
            static const char* kCellName[4] = {
                "far from you,  no enemy near   (running, searching)",
                "far from you,  enemy near      (fighting out there - normal)",
                "NEXT TO YOU,   no enemy near   (standing by you doing nothing)",
                "next to you,   enemy near      (fighting at your side)",
            };
            sprintf_s(l, "   --- what she was doing (near you = under %.0f m,"
                         " enemy near = under %.0f m) ---",
                      kNearArisen, kNearEnemy);
            logFile << l << std::endl;
            for (int c = 0; c < 4; ++c) {
                const unsigned pct = (unsigned)((s_cell[c] * 100) / s_cellSamples);
                sprintf_s(l, "   %-52s %6u  %3u%%", kCellName[c], s_cell[c], pct);
                logFile << l << std::endl;
            }
            const unsigned idlePct =
                (unsigned)((s_cell[2] * 100) / s_cellSamples);
            sprintf_s(l, "   THE FORUM COMPLAINT, AS A NUMBER: %u%% of combat"
                         " spent standing next to you with nothing to fight."
                         " Mean distance to the nearest enemy %.1f m.",
                      idlePct, (float)(s_enemySum / (double)(s_enemySamples ? s_enemySamples : 1)));
            logFile << l << std::endl;
        }
        logFile << "   LIMIT: distance alone does not say WHY she is there."
                   " Compare two runs of the SAME pawn against the SAME kind of"
                   " enemy, one with Guardian primary and one without."
                << std::endl;
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
    // ВЫКЛЮЧЕНИЕ = КОНЕЦ ЗАМЕРА, А ЗНАЧИТ ОТЧЁТ.
    //
    // Тестер провёл бой, выключил слежение и прислал лог — в котором нет
    // гистограммы, потому что её надо было нажать отдельной кнопкой ДО
    // выключения. Замер пропал из-за порядка нажатий, а не из-за игры.
    // Теперь выключение печатает итог само.
    // ПУСТОЙ ПРОГОН ТОЖЕ ДОЛЖЕН БЫТЬ ВИДЕН.
    //
    // Второй замер A/B пропал бесследно: слежение включили и выключили,
    // сэмплов не набралось, и вывода не было вовсе — в логе просто нет
    // второй гистограммы, как будто её и не заказывали. Тестер узнал об
    // этом от меня, а не от прибора.
    if (s_watch) {
        if (s_samples) DumpCodeHistogram();
        else {
            char w[240];
            sprintf_s(w, "GoapProbe: code watch off - NO SAMPLES collected"
                         " (planner lost %u times). The target's planner never"
                         " resolved: the party list was being rebuilt, or the"
                         " watch was on for less than a tick.", s_lostPlanner);
            logFile << w << std::endl;
        }
    }

    // НЕ ПУСКАТЬ В БОЙ БЕЗ ТАБЛИЦЫ ИМЁН (75.43).
    //
    // Прогон B в логе 21 пропал целиком: таблица целей не построилась
    // (`goals named 0`), и вся гистограмма кодов оказалась мусором —
    // «code 0, 3608 кадров, 1 вход». Тестер отдал за это отдельный бой.
    //
    // Прибор обязан отказаться ДО боя, а не признаться после. Дистанция
    // от таблицы не зависит, но код — зависит, и половинчатый замер нам
    // не нужен.
    if (!s_watch) {
        const uintptr_t pb = ProbeBody();
        if (BodyAlive(pb)) {
            uintptr_t pl = ResolvePlanner(pb, 0, 0);
            if (pl) { s_planner = pl; BuildGoalTable(pl); }
        }
        int named = 0;
        for (int c = 0; c < kMaxCode; ++c) if (s_goalName[c][0]) ++named;
        if (named < 40) {
            sprintf_s(s_status, "goap: REFUSED - goal table is empty (%d of ~69)."
                                " Press 'Party roster', then 'Goal codes'.", named);
            logFile << "GoapProbe: code watch REFUSED to start - the goal table"
                       " has " << named << " of ~69 names, so the code histogram"
                       " would be garbage. Press 'Party roster' and 'Goal codes'"
                       " first. (This is what silently ruined run B in log 21.)"
                    << std::endl;
            return;
        }
    }

    s_watch = !s_watch;
    s_lastCode = -12345;
    s_logged = 0;
    if (s_watch) {
        // Включение = начало нового замера. Иначе гистограмма склеит
        // два разных сеанса и её числа будут ни о чём.
        ResetCodeHistogram();
        if (!s_planner) {
            const uintptr_t pawn = ProbeBody();
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
    ActWatchTick();          // трек идлов идёт своим счётом, независимо
    SlotSweepTick();         // развёртка тоже живёт своим счётом
    if (!s_watch) return;
    // Выгрузка мира делает все указатели недействительными.
    if (!Runtime::Mem::InWorld()) { s_planner = 0; s_tableReady = false; return; }
    if (!s_planner) {
        const uintptr_t pawn = ProbeBody();
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
    if (!Runtime::Mem::Rd((void*)(s_planner + 0x17C), &code, 4)) {
        // Планировщик перестал читаться: тело пересоздано (смена локации,
        // пересборка партии). Сбрасываем кэш, чтобы следующий тик нашёл
        // его заново, а не молчал до конца замера.
        s_planner = 0;
        s_tableReady = false;
        ++s_lostPlanner;
        return;
    }

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
        if (Runtime::GuardianFixIsApplied()) ++s_fixApplied;
        if (PawnAI::GuardianLeverIsActive()) ++s_leverActive;

        // Поводок меряем только в бою: вне боя пешка ходит за игроком по
        // другим причинам, и смешивать эти кадры значит смазать ответ.
        if (fight) {
            float ax = 0, ay = 0, az = 0;
            if (Runtime::GetArisenWorldPos(&ax, &ay, &az)) {
                const uintptr_t pb = ProbeBody();
                float px = 0, py = 0, pz = 0;
                if (pb && Runtime::Mem::Rd((void*)(pb + 0x40), &px, 4)
                       && Runtime::Mem::Rd((void*)(pb + 0x44), &py, 4)
                       && Runtime::Mem::Rd((void*)(pb + 0x48), &pz, 4)) {
                    const float dx = px - ax, dy = py - ay, dz = pz - az;
                    // Мир меряется в сантиметрах (см. GuardianDoctrine).
                    const float d = sqrtf(dx * dx + dy * dy + dz * dz) / 100.0f;
                    if (d >= 0.0f && d < 500.0f) {
                        ++s_distSamples;
                        s_distSum += d;
                        if (d < s_distMin) s_distMin = d;
                        if (d > s_distMax) s_distMax = d;
                        for (int b = 0; b < kDistBuckets; ++b)
                            if (d < kDistEdge[b]) { ++s_dist[b]; break; }

                        // Ближайший враг — обход списка акторов, поэтому
                        // раз в 10 кадров; между опросами держим прошлое
                        // значение. Дистанции в бою меняются медленнее.
                        if (--s_enemyPhase <= 0) {
                            s_enemyPhase = 10;
                            // ВРАГИ, А НЕ ВСЕ АКТОРЫ.
                            //
                            // Лог 22: «mean distance to the nearest enemy
                            // 0.0 m» и «enemy near» в 100 % кадров. Причина
                            // тупая: `ActorAt` отдаёт всех, включая саму
                            // пешку, а до себя расстояние ноль. Прибор
                            // измерял пешку и радостно докладывал, что враг
                            // всегда рядом.
                            //
                            // Берём отфильтрованный список врагов и на
                            // всякий случай отбрасываем всё ближе 0.3 м —
                            // это уже не «рядом», это «то же тело».
                            float best = 1e9f;
                            const int n = Runtime::EnemyCount();
                            for (int a = 0; a < n && a < 64; ++a) {
                                const char* kind = 0;
                                const uintptr_t eb = Runtime::EnemyBodyAt(a, &kind);
                                if (!eb || eb == pb) continue;
                                float ex = 0, ey = 0, ez = 0;
                                if (!Runtime::Mem::Rd((void*)(eb + 0x40), &ex, 4)
                                    || !Runtime::Mem::Rd((void*)(eb + 0x44), &ey, 4)
                                    || !Runtime::Mem::Rd((void*)(eb + 0x48), &ez, 4))
                                    continue;
                                const float qx = px - ex, qy = py - ey, qz = pz - ez;
                                const float q = sqrtf(qx * qx + qy * qy + qz * qz) / 100.0f;
                                if (q >= 0.3f && q < 500.0f && q < best) best = q;
                            }
                            s_lastEnemyDist = (best < 1e8f) ? best : -1.0f;
                        }
                        if (s_lastEnemyDist >= 0.0f) {
                            ++s_enemySamples;
                            s_enemySum += s_lastEnemyDist;
                            const int nearYou = (d <= kNearArisen) ? 1 : 0;
                            const int nearFoe = (s_lastEnemyDist <= kNearEnemy) ? 1 : 0;
                            ++s_cell[nearYou * 2 + nearFoe];
                            ++s_cellSamples;
                        }
                    }
                }
            }
        }
    } else {
        ++s_gapSamples;
    }

    if (code == s_lastCode) return;
    const int32_t prev = s_lastCode;
    s_lastCode = code;

    // НЕ ЖЕЧЬ БЮДЖЕТ ЛОГА НА МЕРЦАНИЕ -1.
    //
    // В замере 75.13 бюджет в 60 строк был израсходован ДО боя: пешка
    // стояла в лагере, код мигал «-1 / Wait / -1 / Wait», и к началу драки
    // писать было уже нечего. В логе не осталось ни одной строки про то,
    // ради чего включали слежение.
    //
    // Значение -1 — промежуток между выборами, а не состояние (это уже
    // записано в §26.6). Раз оно не состояние, то и переходы в него и из
    // него событиями не являются.
    if (code < 0 || prev < 0) return;

    // Что пешка делает в этот момент — чтобы код был не абстрактным
    // числом, а «код 1 при cPlActRun».
    char act[48] = {};
    const uintptr_t pawn = ProbeBody();
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
