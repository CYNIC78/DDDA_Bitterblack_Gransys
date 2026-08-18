#pragma once
/**
 * Runtime — внутренний контракт продуктового слоя.
 *
 * Здесь живут типы, состояние и объявления функций, которые совместно
 * используют WorldScan / PartyRecon / PriorityPlatform. Заголовок внутренний:
 * его включают модули рантайма и DevTools (исследование смотрит на продукт,
 * но продукт на исследование — только через Runtime::g_research).
 *
 * Порядок объявлений сохранён исходным (как было в DevTools.cpp): типы
 * зависят друг от друга и от констант размеров.
 *
 * Правило: продуктовый код НЕ вызывает функции DevTools напрямую. Точка
 * расширения ровно одна — ResearchHooks ниже.
 */

#include <windows.h>
#include <stdint.h>
#include "MemProbe.h"
// Заголовок самодостаточен: ActAt() возвращает ActMap::Act*, поэтому
// таблица подключается здесь, а не порядком include в потребителе.
#include "../ActMap.Generated.h"

namespace Runtime {

// Фундамент лежит во вложенном Runtime::Mem, а вложенное пространство имён
// изнутри объемлющего невидимо. Директива открывает Rd/RdPtr/InWorld/... всем
// модулям рантайма без поимённой квалификации.
using namespace Mem;

static const uintptr_t kGoblinInst   = 0x015852A8u;
static const uintptr_t kNpcInst      = 0x015D2618u;
static const uintptr_t kEm8000Inst   = 0x015BB278u;
struct ActorDump {
    uintptr_t   ptr, vt, next, prev, subVt;
    uint8_t     gid, st14;
    float       x, y, z;
    bool        fat29, subOk, win5bOk, win60Ok;
    const char* kind;
    BYTE        win5b[16];
    BYTE        win60[64];
    // Zip 32 — ActScan: current-action pointer inside the 29KB body.
    uint32_t    actOff;      // offset where the Act* was found
    uintptr_t   actPtr;      // the action object
    uint32_t    actVtRva;    // its vtable RVA
    const char* actName;     // "ThreatHowl" / "Die" / ...
    const char* actCat;      // "taunt" / "death" / ...
    int         actHits;     // how many ActMap-matching ptrs in the body
    uint32_t    actOff2;     // second candidate (previous/queued action)
    const char* actName2;
    // Zip 33 — raw mode: vtable-bearing objects in the body, NO ActMap filter.
    // ActMap holds factory vtables; live objects carry instance vtables.
    // These are the real ones, harvested so we can build the bridge.
    uint32_t    rawOff[40];
    uint32_t    rawVt[40];
    uint32_t    rawPtr[40];   // Zip 35: object address — embedded vs heap
    char        rawName[40][40];   // Zip 34: real class name read from DTI
    int         nRaw;
    // Билд 29 — живое состояние через DTI, а НЕ через ActMap.
    // ActMap.Generated.h хранит factory vtable: runtime-сравнение с живым
    // объектом дало 0 совпадений. Поэтому имя
    // состояния спрашиваем у самой игры: obj -> vtable -> GetDTI -> DTI+4.
    char        liveAct[48];  // "cEm0100ActDie", "cEm0100ActWait", ...
    bool        isDead;       // состояние смерти: ActDie / ActDeadBody
    // Имя вида, прочитанное через DTI. kind указывает либо сюда, либо на
    // строковую константу для заранее известных vtable.
    char        kindBuf[40];
};
extern ActorDump g_act[32];
extern uintptr_t g_pawnCombatTarget;
extern int g_nAct;
extern uintptr_t g_pollAddr;
extern uintptr_t g_lastBand;
static const uintptr_t kUnk84Inst = 0x015D1D30u;
static const uintptr_t kHareInst  = 0x015BD9D0u;
static const uintptr_t kHotLo     = 0x10000000u;
static const uintptr_t kHotHi     = 0x18000000u;
static const uint32_t kActSlot = 0x2DC8;
extern uint32_t g_actSlotOff;
extern bool g_actFullScan;
static const uint32_t kPartyBodySize       = 0x5A10;
static const uint32_t kCmcBodySize         = 0x58E0;
static const uint32_t kPawnManagerSize     = 5512;
static const int      kPartyMaxBodies      = 24;
static const int      kPartyMaxChildren    = 96;
static const int      kPartyMaxValueHits   = 96;
static const int      kPartyVtCacheSize    = 8192;
static const int      kPartyMaxNearTypes   = 32;
static const int      kPartyMaxRuntimeProbes = 24;
static const int      kPartyRuntimeProbeBytes = 32;
static const int      kPawnAiMaxCandidates = 1024;
struct PartyKnownValue {
    const char* label;
    int32_t     value;
};
static const PartyKnownValue kPartyKnownValues[] = {
    { "player_hp_current", 331 },
    { "player_hp_max",     498 },
    { "player_stamina",    600 },
    { "pawn_hp_current",   327 },
    { "pawn_hp_max",       505 },
    { "pawn_stamina",      595 }
};
static const int kPartyKnownValueCount = sizeof(kPartyKnownValues) / sizeof(kPartyKnownValues[0]);
enum PartyVtKind { PVK_OTHER = 0, PVK_PARTY_BODY, PVK_PAWN_MANAGER };
struct PartyVtClass {
    uintptr_t vt;
    uint8_t   kind;
    char      name[64];
};
struct PartyNearType {
    uintptr_t vt;
    uintptr_t sample;
    char      name[64];
};
struct PartyRuntimeProbe {
    uintptr_t ptr;
    uintptr_t vt;
    char      name[40];
    BYTE      head[kPartyRuntimeProbeBytes];
    bool      headOk;
};
struct PawnAiCandidate {
    uintptr_t ptr;
    uintptr_t vt;
    uint32_t  typeSize;
    char      name[64];
};
// Сколько байт «головы» дочернего объекта снимаем в дамп.
static const int kPartyChildHeadSize = 384;

struct PartyChildDump {
    uint32_t  off;
    uintptr_t ptr;
    uintptr_t vt;
    char      name[48];
    BYTE      head[kPartyChildHeadSize];
    bool      headOk;
    bool      ownerRef;
};
struct PartyValueHit {
    uint32_t containerOff; // 0 for uPlayer body; body slot for a child object
    uint32_t valueOff;     // offset inside body/child snapshot
    char     container[48];
    char     label[24];
    char     encoding[4];  // i32 / f32
};
struct PartyBodyDump {
    uintptr_t ptr;
    uintptr_t vt;
    uint32_t  bodySize;
    char      dti[40];
    char      role[24];
    bool      playerRecordRef;
    bool      mainPawnRecordRef;
    bool      pawnManagerRef;
    bool      hasPawnIntel;
    BYTE      body[kPartyBodySize];
    bool      bodyOk;
    PartyChildDump child[kPartyMaxChildren];
    int       nChild;
    PartyValueHit valueHit[kPartyMaxValueHits];
    int       nValueHit;
    uint32_t  actOff;
    uintptr_t actPtr;
    char      actName[48];
    bool      actOwnerRef;
};
extern PartyBodyDump g_party[kPartyMaxBodies];
extern PartyBodyDump g_partyChosen[2];
extern int g_nParty;
extern int g_partyRawCandidates;
extern uintptr_t g_partyPawnMgr[8];
extern int g_nPartyPawnMgr;
extern PartyVtClass g_partyVtCache[kPartyVtCacheSize];
extern int g_partyVtChecked;
extern int g_partyVtNamed;
extern PartyNearType g_partyNear[kPartyMaxNearTypes];
extern int g_nPartyNear;
extern PartyRuntimeProbe g_partyRuntime[kPartyMaxRuntimeProbes];
extern int g_nPartyRuntime;
extern PawnAiCandidate g_pawnAi[kPawnAiMaxCandidates];
extern int g_nPawnAi;
extern int g_partySeq;
extern DWORD g_partyFindMs;
extern volatile LONG  g_partyBusy;
extern char g_partyStatus[192];
extern char g_partyLastFile[MAX_PATH];
static const int kPriorityProfileMaxRules = 48;
struct PartyPriorityProfileRule {
    uint32_t sensor;
    uint32_t code;
    uint32_t category;
    uint32_t objectId;
    uint32_t extra;
    uint32_t ruleIndex;
    int32_t  expectedAddS32;
    int32_t  desiredAddS32;
    uint32_t expectedAddF32Bits;
    uint32_t expectedBreak;
    uint32_t expectedCheckCount;
    int32_t  expectedSlot; // -1 = memory verification only

    uintptr_t prioPtr;
    uintptr_t rulePtr;
    uintptr_t ruleVt;
    bool      resolved;
    bool      applied;
    int32_t   currentAddS32;
    int32_t   liveSlot;
};
extern PartyPriorityProfileRule g_priorityProfileRules[kPriorityProfileMaxRules];
extern int g_nPriorityProfileRules;
extern char g_priorityProfileActive[40];
extern uint32_t g_priorityProfileConfigHash;
extern bool g_priorityProfileLoaded;
extern bool g_priorityProfileFileOk;
extern bool g_priorityProfileApplied;
extern bool g_priorityProfileConverged;
extern int g_priorityProfileWrites;
extern int g_priorityProfileRestores;
extern DWORD g_priorityProfileLastPoll;
extern DWORD g_priorityProfileWorldSince;
extern DWORD g_priorityProfileLastDiscover;
extern char g_priorityProfileStatus[192];
extern bool g_arisenPosOk;
extern bool g_pawnPosOk;
extern bool g_wasInWorld;
extern float g_arisenPosX, g_arisenPosY, g_arisenPosZ;
extern float g_pawnPosX, g_pawnPosY, g_pawnPosZ;
extern DWORD g_pawnPosLastFailLog;
extern bool g_pawnPosWasOk;
extern DWORD g_partyPosLastDiscover;
extern int g_partyPosAttempts;
extern PartyPriorityProfileRule g_guardianFixRule;
extern bool g_guardianFixInit;
extern bool g_guardianFixArmed;
extern bool g_guardianFixApplied;
extern int g_guardianFixWrites;
extern int g_guardianFixRollbacks;
extern char g_guardianFixStatus[160];

// ---------------------------------------------------------------------------
// Профилировка продуктового тика.
//
// Оптимизируем по числам, а не по ощущениям: тик мерится
// QueryPerformanceCounter'ом, наружу отдаются последнее, среднее и худшее
// значение в микросекундах. Само измерение стоит два обращения к счётчику
// на тик (~7 раз в секунду) — на фоне самого тика это шум.
// ---------------------------------------------------------------------------
struct ScanStats {
    uint32_t lastUs;    // длительность последнего тяжёлого тика
    uint32_t avgUs;     // скользящее среднее
    uint32_t maxUs;     // худший тик с момента сброса
    uint32_t ticks;     // сколько тяжёлых тиков посчитано
    int      actors;    // актёров в последнем снимке
    uint32_t pollKb;    // сколько КБ памяти просматривает поллинг за тик
};
ScanStats ScanGetStats();
void      ScanResetStats();

// ---------------------------------------------------------------------------
// Точка подключения исследовательского слоя.
//
// Продукт вызывает эти хуки в фиксированных местах и ничего не знает о том,
// что за ними стоит. DevTools регистрирует свою реализацию при инициализации;
// если DevTools выключен, указатели остаются нулевыми и продукт просто идёт
// дальше. Так research-код больше не может утянуть продукт за собой.
// ---------------------------------------------------------------------------
struct ResearchHooks {
    void (*onSnapshotEarly)();                  // снимок партии снят, тела найдены
    void (*onSnapshotFull)();                   // снимок полный: роли, позиции, AI
    void (*onTick)();                           // такт на pawn-потоке
    void (*onWorldUnload)(const char* reason);  // мир выгружен
};

extern ResearchHooks g_research;
void SetResearchHooks(const ResearchHooks& hooks);

// Существо, которым мы вправе управлять (мутации размера и т.п.).
//
// Сюда входят и мирные животные: заяц — тоже uEm*, и масштабировать его
// можно. Это НЕ значит, что он враг.
bool KindIsCreature(const char* kind);

// Безобидная живность: не атакует, не участвует в оценке опасности.
//
// uEm8000 — те самые «лагерные зайцы» из дампов. Их шестеро вокруг
// стоянки, и они прибавляли +6 к счётчику врагов на пустом месте.
// Важно: uEm8000 НЕ Григори (см. FIELD_MAP: «не маппить 0x61 -> Hare,
// сломаем Григори») — это отдельный вид с gid 0x61.
bool KindIsHarmless(const char* kind);

// Враг: существо, представляющее угрозу.
//
// ВАЖНО: враги бывают не только uEm*. Бандиты и солдаты — это
// uHumanEnemy (29696 B), ветка uNpc -> uHumanEnemy. Пока фильтр смотрел
// только на "uEm", люди были невидимы и для счётчика, и для мутаций.
bool KindIsEnemy(const char* kind);

int KindCategory(const char* kind);

// Собирает WorldReport из снимка актёров и публикует его в CombatBus.
void PublishWorldFromActors();

// Живое состояние существа: имя класса текущего Act, прочитанное у игры.
//
// ЗАЧЕМ ОТДЕЛЬНАЯ ФУНКЦИЯ, А НЕ ActMap: таблица ActMap.Generated.h содержит
// factory vtable, у живого объекта instance vtable, единого сдвига нет
// (гоблин 0x1B1CC, заяц 0x1B198). Сравнение всегда даёт промах, поэтому
// в старых дампах у всех actName = "-". Имя берём через DTI — тем же
// способом, каким опознаём uEm0100.
//
// Возвращает true, если имя прочитано.
bool ReadLiveAct(uintptr_t body, char* out, int cap);

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
bool ActNameIsDeath(const char* actName);

// Build 62 — враг в боевом действии? По DTI-имени live Act (не по урону).
// Консервативно: только однозначно боевые состояния. Локомоция (Walk/Run)
// НЕ считается боем — это может быть патруль, а ложный «бой» хуже пропуска
// (пропуск ловится другими сигналами: урон и цель пешки).
bool EnemyActNameIsCombat(const char* actName);

const ActMap::Act* ActAt(uintptr_t body, uint32_t off, uintptr_t* outPtr, uint32_t* outRva);

bool PartyStartsWith(const char* s, const char* prefix);

bool PartyRelevantName(const char* n);

bool PartyBlockHasPtr(const BYTE* data, uint32_t bytes, uintptr_t want);

void PartyNoteValueHit(PartyBodyDump& P, uint32_t containerOff,
                              const char* container, uint32_t valueOff,
                              const char* label, const char* encoding);

void PartyScanKnownValues(PartyBodyDump& P, const BYTE* data, uint32_t bytes,
                                 const char* container, uint32_t containerOff);

void PartyRememberNearType(const char* name, uintptr_t vt, uintptr_t sample);

bool PartyRuntimeProbeName(const char* name);

int PartyRuntimeProbePriority(const char* name);

void PartyAddRuntimeProbe(uintptr_t obj, uintptr_t vt, const char* name);

bool PawnAiRelevantName(const char* name);

void PartyAddPawnAiCandidate(uintptr_t obj, uintptr_t vt, const char* name);

const char* PartyPriorityProfilePath();

uint32_t PartyPriorityProfileHash(const void* data, size_t bytes, uint32_t h = 2166136261u);

int PartyPriorityProfileGetInt(
    const char* section, const char* key, int fallback);

bool PartyPriorityProfileNameOk(const char* name);

void PartyPriorityProfileEnsureFile();

bool PartyPriorityProfileReadConfig(
    char* activeOut, PartyPriorityProfileRule* rulesOut, int* countOut,
    uint32_t* hashOut);

int PartyPriorityLiveSlot(uintptr_t prioParam);

void PartyPriorityProfileResetRuntime();

bool PartyPriorityProfileResolveRule(PartyPriorityProfileRule& R);

bool PartyPriorityProfileResolveAll();

bool PartyPriorityProfileRestoreAll(const char* reason);

void PartyPriorityProfileUndoWrites(const bool* wrote, int count);

bool PartyPriorityProfileApplyAll();

void PartyPriorityProfileUpdateState();

bool PartyPriorityProfileLoadIfChanged();

void PartyPriorityProfileSetActive(const char* active);

void PartyPriorityProfileTick();

void PartyPriorityProfileToggle();

void PartyPriorityProfileHotkeyTick();

PartyVtClass* PartyClassifyVt(uintptr_t vt, uintptr_t sample);

void PartyAddBodyCandidate(uintptr_t obj, uintptr_t wantVt, const char* dtiName);

void PartyAddPawnManagerCandidate(uintptr_t obj, uintptr_t wantVt);

// Build 60: partyOnly=true — ранний выход, как только найдены ОБА тела
// (uPlayer и uCmc). Позиции нужны лишь от этих двух; полный проход до
// 0x7FFF0000 ради priority/targetSel при позиционном трекинге не нужен и
// давал ~1.5 с на каждую итерацию. При partyOnly не собираем runtime/pawnAi/
// targetSel (они для профиля/аудита) — только тела.
void PartyFindBodies(bool partyOnly = false);

void PartyInspectBody(PartyBodyDump& P);

void PartyMarkPawnManagerRefs();

int PartyCountValueHits(const PartyBodyDump& P, const char* prefix);

void PartySelectWorkingPair();

void PartyAssignRoles();

bool PartyCandidatesStillValid();

// Читает позиции из уже разрешённых тел (дешёво, без census).
// Вызывается каждый тик и после каждого PartyAssignRoles.
void PartyReadPositions();

void PartyPositionsTick();

bool PartyPriorityProfileAutoDiscover();

void PartyCapture(bool forceFind);

void PartyHotkeyTick();

void ScanActSlot(ActorDump& A);

void DumpActorsFrom(uintptr_t* seed, int ns);

void RewalkActors();

// Известные vtable — быстрый путь без чтения DTI.
int IsSeedVt(uint32_t val);

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
bool LooksLikeCreatureAt(uintptr_t obj, uint32_t vt);

uintptr_t PollSeedSlice(uint32_t budget);

// Считает ЖИВЫХ врагов. Трупы не в счёт: иначе "рядом 5 врагов" после
// выигранного боя, и любая логика "оценить опасность" врёт.
int EnemyCount();

// Перебор врагов по индексу. Нужен, потому что список разнороден:
// в дампах 6x uEm8000 (лагерные, gid 0x61) + 1x uEm0100 (гоблин).
// Кто пишет параметры вида — обязан идти по списку и смотреть kind.
// ВАЖНО: перебор отдаёт только ЖИВЫХ.
//
// Труп остаётся в мире и в списке движка до выгрузки (это не баг, см.
// "гистерезис выгрузки" в FIELD_MAP). Но для модулей поведения мёртвый
// враг — мусор: мутировать его масштаб или поводок бессмысленно, а в
// счётчике "врагов рядом" он завышает опасность.
uintptr_t EnemyBodyAt(int idx, const char** kindOut);

uintptr_t FirstBodyOfKind(const char* kind);

// Build 56.2 — Guardian doctrine anchor/pawn world positions.
// Читает +0x40/+0x44/+0x48 из уже разрешённых тел (PartyReadPositions).
bool GetArisenWorldPos(float* x, float* y, float* z);

bool GetMainPawnWorldPos(float* x, float* y, float* z);

void GuardianFixInitOnce();

void GuardianFixSetTarget(int32_t desiredAddS32);

bool GuardianFixIsApplied();

const char* GuardianFixStatus();

// Build 58: единый tick с градиентом. Целевое значение = desired (если armed)
// или expected (rollback). Каждый тик читаем текущее, при расхождении —
// write + readback (verify) + откат к прежнему при неудаче. Поддерживает
// плавную смену desired (градиент: -3 → 0 → +2 по дистанции угрозы).
void GuardianFixTick();

void WorldScan_Tick();

} // namespace Runtime
