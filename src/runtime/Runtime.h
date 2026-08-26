#pragma once
/**
 * Runtime — продуктовый слой мода. Точка входа, которая работает ВСЕГДА,
 * независимо от [devtools] enabled и от того, поднялся ли ImGui-оверлей.
 *
 * ЗАЧЕМ ЭТОТ МОДУЛЬ СУЩЕСТВУЕТ (Build 69, разделение слоёв):
 * до этого продукт (детектор боя, доктрина Guardian, позиции партии) жил
 * внутри DevTools.cpp и гейтился исследовательским флагом. Выключение
 * DevTools ломало игровые фичи, а инициализация вообще не выполнялась —
 * Hooks::DevTools() выходил раньше BuildWatch().
 *
 * Правило слоя: в Runtime нет ни одной строки, которая существует ради
 * исследования. Исследование живёт в src/devtools/ и вызывается из рантайма
 * через узкие хуки, а не наоборот.
 *
 * Состав:
 *   Runtime::Mem       — доступ к памяти, секции образа, DTI-имена (фундамент)
 *   Runtime::World     — обход актёров, враги, публикация WorldReport
 *   Runtime::Party     — uPlayer/uCmc, тела партии, позиции
 *   Runtime::Priority  — транзакционные priority-профили, Guardian-фикс
 */

#include <stdint.h>

namespace Runtime {

// Поднимает продуктовый слой. Вызывается из InitHooks() ДО инициализации UI:
// продукт не должен зависеть от того, удалось ли создать ImGui-оверлей.
void Init();

// Сколько раз CRT сообщил о неверном параметре (переполнение буфера в
// sprintf_s и подобное). По умолчанию такое ЗАВЕРШАЕТ процесс; мы ставим
// свой обработчик, который считает и не валит игру. Ненулевое значение —
// повод искать испорченный вывод, но не повод для вылета.
uint32_t CrtInvalidParamCount();

// Останавливает продуктовый слой: откат всех транзакционных правок.
// Вызывается при выгрузке DLL. Без ожиданий и join'ов.
void Shutdown();

// ---------------------------------------------------------------------------
// Публичный API продуктового слоя. Это всё, что модулям поведения нужно знать
// о рантайме. Раньше эти функции жили в namespace DevTools — и продукт
// формально зависел от исследовательского модуля.
// ---------------------------------------------------------------------------

// Продуктовый тик: обход актёров, детектор боя, позиции, профили.
// Вызывается из PawnAI::Tick каждые ~150 мс. Работает ВСЕГДА.
void WorldScan_Tick();

// --- Мир и враги ---------------------------------------------------------
// Сколько живых врагов сейчас в списке.
int       EnemyCount();
// Перебор врагов: idx 0..EnemyCount()-1. Возвращает тело и (опц.) имя класса
// («uEm0100»). 0, если такого индекса нет.
uintptr_t EnemyBodyAt(int idx, const char** kindOut);
// Сырой список живых объектов (без фильтра «существо»): нужен, чтобы
// видеть в нём тела партии — фильтрованный доступ их прячет.
int       ActorCount();
uintptr_t ActorAt(int idx, const char** kindOut);
// Тело первого врага заданного вида. 0, если такого вида в мире нет.
uintptr_t FirstBodyOfKind(const char* kind);
// Враг ли объект этого вида (не заяц, не NPC).
bool      KindIsEnemy(const char* kind);
// Объект текущего действия существа. 0, если не резолвится.
uintptr_t ActObjectOf(uintptr_t body);

// --- Позиции партии ------------------------------------------------------
// +0x40/+0x44/+0x48 (SOURCE_OF_TRUTH §2). false, если тела не резолвлены.
bool GetArisenWorldPos(float* x, float* y, float* z);
bool GetMainPawnWorldPos(float* x, float* y, float* z);
// Тело главной пешки. 0, если партия ещё не разрешена. Нужно слою пешек,
// чтобы адресовать примитивы (например, множитель передвижения).
uintptr_t MainPawnBody();
uintptr_t ArisenBody();

// --- пешки партии целиком (главная + наёмные) -------------------------------
//
// Наёмная пешка — тоже тело класса `uCmc`, поэтому «первый uCmc» больше не
// является определением главной. Здесь компактно перечисляются только тела,
// которые однозначно связаны с fixed records 0..2, в порядке этих records.
// Unresolved/duplicate тела исключены; поэтому ordinal `idx` нельзя считать
// номером record slot. Для адресации конкретного record использовать
// PartyRecordInfo().
//
// Продуктовые модули по-прежнему работают с главной пешкой. Наёмные пешки
// принадлежат другому игроку: их запись персонажа (вокация, склонности,
// скиллы, аугменты) НЕ наша зона — только рантайм-советы.
int       PawnBodyCount();
uintptr_t PawnBodyAt(int idx, bool* isMainOut);

// Состав партии изменился (наняли/уволили пешку) — попросить пересканировать.
// Разбор не сканирует память заново, пока найденные тела живы; наёмная
// пешка приходит посреди игры и иначе не появляется в списке никогда.
void      PartyRequestRescan();

// Сколько пешек в партии ПО ЗАПИСЯМ персонажей (своя + наёмные, 1..3).
// Записи лежат по фиксированным смещениям от pBase и доступны всегда —
// в отличие от живых тел, которые ещё надо найти в куче.
int       PartyRecordPawnCount();
// Карточка пешки по номеру записи (0 — своя, 1..2 — наёмные): вокация,
// уровень и живое тело, если оно уже найдено.
bool      PartyRecordInfo(int idx, int* vocOut, int* lvlOut, uintptr_t* bodyOut);
// Сколько живых uCmc претендуют на fixed record 0..2.
// 0 = слот пуст на поле (рифт / ещё не появилась); 1 = exact; ≥2 = дыра identity.
int       PartyRecordBodyClaimCount(int idx);
// Имя класса текущего действия существа через DTI (+0x2DC8). Работает и
// для тел партии: у Аризена там cPlAct*, у пешки — тоже.
bool ReadLiveAct(uintptr_t body, char* out, int cap);

// --- Узкий read-only combat snapshot партии ------------------------------
//
// Поля записи — подтверждённая карта SOURCE_OF_TRUTH/FIELD_MAP. Живое тело
// и действие прикрепляются только если PartyRecon уверенно связал их с
// записью. Неизвестная семантика объявлена явно: statusValid/downedValid
// остаются false до живой валидации. downedHint означает лишь совпадение
// сырого имени action с исследуемым кандидатом и НИКОГДА не участвует в
// расчёте цели.
enum PartyCombatSlot {
    PARTY_ARISEN = 0,
    PARTY_MAIN   = 1,
    PARTY_HIRED1 = 2,
    PARTY_HIRED2 = 3,
    PARTY_COMBAT_SLOTS = 4
};

struct PartyCombatMember {
    int       slot;              // PartyCombatSlot
    int       pawnRecordIdx;     // -1 Arisen, иначе 0..2
    uintptr_t record;
    uintptr_t body;

    bool      recordValid;
    bool      hpValid;            // confirmed current/max HP record reads
    bool      statsValid;         // core STR/DEF/MAG/MDEF reads; not loadout totals
    bool      skillsValid;
    bool      bodyValid;
    bool      positionValid;
    bool      actionValid;

    int       vocation;
    int       level;
    int32_t   equippedSkills[6];
    float     currentHp;
    float     maxHp;             // только диагностика; %HP в targeting нет
    // Подтверждены как CORE stats записи, а не итоговые loadout totals.
    // Build 002 помечает их CORE/UNVALIDATED и не использует в решении.
    float     strength;
    float     defense;
    float     magick;
    float     magickDefense;
    float     x, y, z;
    char      liveAct[48];

    uint64_t  statusMask;
    bool      statusValid;
    bool      downedValid;
    bool      downedRevivable;
    bool      downedHint;        // raw action hint; neutral in scoring
};

struct PartyCombatSnapshot {
    uint32_t          sampledAtMs;
    int               recordCount;
    PartyCombatMember member[PARTY_COMBAT_SLOTS];
};

bool ReadPartyCombatSnapshot(PartyCombatSnapshot* out);
const char* PartyCombatSlotName(int slot);

// --- Планировщик пешки: код текущего приоритета (только чтение) ----------
//
// ЗАЧЕМ ЭТО В ПРОДУКТЕ, А НЕ В ПРОБЕ. Замер 73.27 поймал расхождение двух
// приборов: гистограмма кодов показала ноль выборов рывка (84/85), а
// DashWatch в это же время видел живые состояния cPlActDashBegin — в том
// числе в бою. Пока каждый прибор смотрит в свою сторону, объяснить это
// нечем. Один и тот же момент времени должен быть подписан обоими
// числами сразу, поэтому чтение кода переехало в рантайм: слой пешек не
// имеет права зависеть от исследовательского модуля (FIX_RULES §5).
//
// Цепочка (SOURCE_OF_TRUTH §3.1, разрешение ПО ИМЕНИ КЛАССА, не по
// зашитому оффсету — слоты тела «плавают», см. FIX_RULES §6):
//     body -> cAICtrl -> cAIGoalPlanning -> +0x17C код приоритета
//
// Код -1 (0xFFFFFFFF) — не состояние, а промежуток между выборами;
// вызывающий обязан его отбрасывать, а не считать отдельным кодом.
bool PawnPriorityCode(int32_t* codeOut);

// Имя цели по коду приоритета: слот ресурса цели в планировщике И ЕСТЬ
// код (`code = (slot - 8) / 4`, доказано в PAWN_SPRINT_RECON §25).
// Читает путь загруженного rAIGoalPlanning и возвращает его хвост
// («AI\Goap\Cmc\DashFollow» -> «DashFollow»).
bool PawnGoalName(int32_t code, char* out, int cap);

// Какие моторные интерфейсы (`cCmc*`) вшиты в живой блок плана этой цели.
//
// ЗАЧЕМ. Замер 74.0 дал главный факт трека: ВСЕ пойманные рывки пешки
// случились под кодом приоритета 1 (`Follow`), в том числе рывок в бою.
// Строки приоритета у кодов 84/85 нет вовсе. Значит рывок выбирает не
// приоритетный слой, а сама цель `Follow` — внутри своего плана она
// берёт то `cCmcFollow`, то `cCmcDashFollow`. Рычаг там, и первым делом
// его надо УВИДЕТЬ: как называется интерфейс в момент рывка и в момент
// обычного бега.
//
// Пишет в `out` имена найденных классов через запятую. Обход ограничен
// блоком PlanCtrl (0x110 байт) и одним уровнем разыменования — это
// дёшево и вызывается только по событию, не каждый кадр.
bool PawnPlanInterfaces(int32_t code, char* out, int cap);

// Девять склонностей из ЖИВОГО ТЕЛА (`cCmcInfo + 0x14B8 + id*0x0C`).
//
// Второй адрес тех же чисел, отличный от записи персонажа. Замер 75.16
// показал расхождение: запись в character record наёмной пешки игру не
// убедила — профиль показывал прежние значения. Пока не доказано, какое
// место авторитетно, читаем оба и сравниваем.
bool PawnInclinationsLive(uintptr_t body, float* out9);
// Запись той же склонности в живое тело, с проверкой чтением.
bool PawnSetInclinationLive(uintptr_t body, int id, float value);

// Найти в теле указатель на живой объект заданного класса. Возвращает
// адрес объекта и (опц.) смещение в теле. 0, если не найден.
//
// Нужен всем, кто хочет добраться до подсистемы существа, не зная
// оффсета наизусть: слоты в теле «плавают» между состояниями, а имя
// класса игра сообщает сама через DTI.
uintptr_t FindChildByClass(uintptr_t body, uint32_t bodyBytes,
                           const char* className, uint32_t* offOut);

// Защищённое чтение чужой памяти для модулей поведения. Тонкая обёртка
// над фундаментом (Runtime::Mem): слоям выше незачем включать внутренний
// заголовок рантайма ради двух вызовов.
bool ReadSafe(uintptr_t addr, void* out, uint32_t bytes);
bool ReadPtrSafe(uintptr_t addr, uintptr_t* out);

// --- Guardian-фикс (транзакционная правка правила code 54) ---------------
// Кортеж подтверждён дампом Build 57:
//   code=54 tuple{s=1,cat=0,obj=0,extra=1} rule[0] AddS32=-3 break=0 checks=1
// desired == -3 (vanilla) означает откат.
void  GuardianFixSetTarget(int32_t desiredAddS32);
bool  GuardianFixIsApplied();
const char* GuardianFixStatus();
void  GuardianFixTick();   // доктрина зовёт каждый тик (apply/rollback)

// --- УДЕРЖАНИЕ РЫЧАГА ЗА ПРИБОРОМ (75.31) --------------------------------
// У одного правила не может быть двух хозяев. Доктрина каждый тик ставит
// своё значение (-3 по умолчанию), и любой замер, который хочет подержать
// строку в заданном ведре, был бы затёрт в том же кадре.
//
// Поэтому владение объявляется явно: пока держит прибор, SetTarget от
// доктрины игнорируется. Отпустил — доктрина снова хозяйка.
void  GuardianFixHold(bool on, int32_t value);
bool  GuardianFixHeld();

// Живое ведро строки code 54 (-1, если правило не разрешено) и признак
// разрешённости. Нужны прибору развёртки, чтобы печатать факт, а не намерение.
int   GuardianFixLiveSlot();
bool  GuardianFixResolved();


// --- РЕЕСТР ЭРРАТ (75.45) -------------------------------------------------
// Слой B по docs/ERRATA_ARCHITECTURE.md: статичные починки сломанных правил.
// Две группы, две галки, два независимых счёта:
//   группа 0 - Guardian душит кинжалы (code 54, ранги 1 и 2);
//   группа 1 - Nexus душит магию      (code 55, ранги 1 и 2).
// Правило ищется ПО СОДЕРЖАНИЮ (склонность + ранг в проверке), а не по
// номеру: порядок правил внутри строки нам никто не обещал.
extern bool    g_errataDaggerOn;    // ini [errata] guardianDaggerBan
extern int32_t g_errataDaggerVal;   // ini [errata] guardianDaggerValue
extern bool    g_errataMagicOn;     // ini [errata] nexusMagicBan
extern int32_t g_errataMagicVal;    // ini [errata] nexusMagicValue
void  ErrataTick();
void  ErrataRestore(int group);     // 0 = кинжалы, 1 = магия
void  ErrataRestoreAll();
bool  ErrataIsApplied(int group);
int   ErrataReasserts(int group);
const char* ErrataStatus(int group);

} // namespace Runtime
