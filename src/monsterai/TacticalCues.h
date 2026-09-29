#pragma once
// Small, data-driven tactical cue matcher used by Monster Director.
//
// A rule maps exact live actions to a tactical response. The matcher owns no
// Aggro/Tempo state and performs no writes. It only resolves roles: which exact
// party body caused the cue, which exact same-kind monster is involved, which
// response tier is requested, and for how long the evidence may be leased.
// New proved restraint families are added as table rows, not Director branches.

#include <stdint.h>

namespace MonsterAI {

enum TacticalSituationId {
    TACTICAL_SITUATION_NONE = 0,
    TACTICAL_SITUATION_PACK_LIFT_RESCUE = 1,
    TACTICAL_SITUATION_PACK_GRAB_ALERT = 2,
    TACTICAL_SITUATION_PACK_GROUND_PIN_ALARM = 3,
    TACTICAL_SITUATION_GOBLIN_GRAB_ALERT = 4,
    TACTICAL_SITUATION_HOB_GRAB_ALERT = 5,
    TACTICAL_SITUATION_GOB_HORN_ALERT = 6,
    TACTICAL_SITUATION_HOB_HORN_ALERT = 7,
    TACTICAL_SITUATION_WOLF_HOWL_ALERT = 8,
    TACTICAL_SITUATION_SAURIAN_HOWL_ALERT = 9,
    TACTICAL_SITUATION_PLAYER_CHANT_HARASS = 10,
    // 85.28: «встреча у тела павшей пешки». Единственная ситуация, чей триггер
    // — не действие, а СОСТОЯНИЕ (пешка лежит) плюс расстояние от игрока до
    // тела. Правило матчится отдельной ветвью (MatchFallenGuard): цель — всегда
    // Аризен, признак — лежащая рядом пешка, исполнители — монстры этого вида
    // у тела. Ярости не даёт: это готовность, а не бешенство.
    TACTICAL_SITUATION_FALLEN_GUARD = 11,
    // 85.30: «добивание лежащей пешки». Триггер — тоже состояние, но ДРУГОЕ:
    // пешка лежит, НО В СОЗНАНИИ (сбита с ног). Замысел владельца: не дать ей
    // встать и выключить из боя, поэтому ЦЕЛЬ здесь — САМА ПЕШКА, а не игрок.
    // Ветвь матчера — MatchPawnFinish.
    TACTICAL_SITUATION_PAWN_FINISH = 12
};

// ALERT and ALARM keep distinct Aggro bundles and evidence leases.
//
// Urgency is the tempo knob ONLY: it says how far the envelope travels from the
// body's stable roll toward its rage endpoint (level = urgency), and it is what
// the Director logs. The Aggro bundle strength comes from the response tier
// above, NOT from urgency — so magnitude can be tuned per event without
// touching aggro behavior.
//
// Градиент (2026-09-25): вплотную увиденное — 1.0 (полная ярость); услышанный
// зов с 10-14 м — 0.65 (подтянулись, но не в бешенстве); чужой каст — 0.55.
// Прежняя редакция держала 1.0 у всех событий, из-за чего один рог разгонял
// всю округу до потолка.
enum TacticalResponseLevel {
    TACTICAL_RESPONSE_NONE = 0,
    TACTICAL_RESPONSE_ALERT = 1,
    TACTICAL_RESPONSE_ALARM = 2
};

struct TacticalPartyActor {
    int       slot;
    uintptr_t body;
    const char* act;
    bool      positionValid;
    float     x, y, z;
    int       vocation;
    // 85.29: вердикт наблюдателя PartyStatus («пешка лежит»), подтверждённый
    // живым полем строками `PS: Hired2 DOWNED act=...`. Новые поля идут В
    // ХВОСТ структуры: у неё есть аггрегатные инициализаторы, и вставка в
    // середину молча переставила бы смысл флагов (эти грабли уже были при
    // добавлении четвёртого флага в таблицу правил).
    bool      downedValid;
    bool      downedRevivable;
    // 85.30: лежит, но В СОЗНАНИИ (сбита с ног) — адресат другой: такую
    // добивают монстры, а не встречают игрока, который придёт её поднимать.
    bool      downedAwake;
};

struct TacticalMonsterActor {
    uintptr_t body;
    const char* kind;
    const char* act;
    bool      positionValid;
    float     x, y, z;
};

struct TacticalMatch {
    int       situation;
    const char* name;
    const char* policyReason;
    int       priority;
    int       response;
    float     urgency;
    int       targetSlot;
    uintptr_t targetBody;
    uintptr_t evidenceBody;
    const char* targetAct;
    const char* evidenceAct;
    float     pairDistanceM;
    uint32_t  maxLeaseMs;
    bool      excludeEvidenceBody;
    const char* responderKind;
    // 85.25: сколько пар было в радиусе, когда событие допущено. Для событий
    // с запасным выбором «ближайший» это >1 — видно прямо в логе.
    int       pairsConsidered;
};

// Diagnostics are transition-logged by Monster Director. Counts expose why a
// rule did not admit without producing frame-by-frame telemetry.
// 85.29: почему встреча у тела не состоялась.
//
// Механизм молчал в поле ДВАЖДЫ, и оба раза по логу нельзя было понять, какое
// из условий не сошлось: неудачный матч не оставляет следов. Теперь матчер
// заполняет причину и числа, а Директор печатает их одной строкой.
struct TacticalFallenDiag {
    int         situation;     // какую ситуацию объясняем (11 или 12)
    const char* reason;        // 0 = замечаний нет
    int         pawnSlot;
    uintptr_t   pawnBody;
    const char* pawnAct;
    bool        pawnDowned;
    bool        pawnPosValid;
    bool        arisenPosValid;
    float       approachM;     // Аризен -> тело, метры (даже если далеко)
    float       nearestKindM;  // ближайший монстр нужного вида -> тело, метры
    int         monstersOfKind;
    int         monstersTotal;
    float       pawnX, pawnY, pawnZ;
    float       arisenX, arisenY, arisenZ;
};

// 85.33: ВТОРОЕ СОБЫТИЕ, которое тоже подошло прямо сейчас.
//
// Владелец: «если у директора будет возможность отдавать параллельные приказы,
// а математика сама распределит, кто какой приказ будет выполнять, — это
// прогресс. Пачка сможет реагировать на более чем одно событие».
//
// Сканер и раньше видел только ОДНО событие — лучший вес. Теперь он отдаёт
// список: главное плюс то, что случилось одновременно. Жизненным циклом
// (аренда, продолжение, освобождение) по-прежнему владеет только главное
// событие; второе пересчитывается каждый скан, потому что скан идёт каждые
// 150 мс и «что ещё происходит» — это состояние мира, а не обязательство.
struct TacticalAltEvent {
    int         situation;
    const char* name;
    int         priority;
    int         response;          // ALERT/ALARM тира, как у главного события
    float       urgency;           // ярость (у второго приказа темп НЕ арендуется)
    float       score;             // вес: ранг × близость
    int         targetSlot;        // член партии, к которому тянет событие
    uintptr_t   targetBody;
    const char* responderKind;     // вид-исполнитель события
    float       pairDistanceM;     // близость события (то же число, что в логе)
    bool        occupiesSameTarget; // тянет к тому же члену, что и главное
};

#define TACTICAL_MAX_ALTS 2

struct TacticalScan {
    bool      matched;
    int       situation;
    const char* name;
    int       response;
    int       targetCandidates;
    int       evidenceCandidates;
    int       pairCandidates;
    int       positionRejected;
    int       firstTargetSlot;
    uintptr_t firstTargetBody;
    uintptr_t firstEvidenceBody;
    const char* firstTargetAct;
    const char* firstEvidenceAct;
    float     nearestDistanceM;
    TacticalMatch match;
    // 85.32: ЧЕМ выбрано событие и кого оно обошло.
    //
    // Владелец (2026-09-26): «те, кто ближе к тому или иному событию, — для них
    // это событие важнее; ближе бежать — выше шансы на успех». Значит, старый
    // закон «старший ранг всегда прав» заменён на ВЕС: ранг × близость. В лог
    // уходит не только победитель, но и тот, кто проиграл по близости, — иначе
    // решение невозможно проверить в поле.
    float     chosenScore;        // вес победителя
    float     chosenDistanceM;    // его близость (метры)
    int       outrankedSituation; // событие с большим рангом, но худшим весом
    float     outrankedScore;
    float     outrankedDistanceM;
    // 85.33: одновременные события (кроме главного).
    int       altCount;
    TacticalAltEvent alts[TACTICAL_MAX_ALTS];
    // 85.29/85.30: у КАЖДОГО механизма свой канал диагностики — иначе отказ
    // одного затирал бы отказ другого, и проверка «молчит именно добивание»
    // превращалась бы в «кто-то из двоих объяснился».
    TacticalFallenDiag fallen;   // 11: встреча у тела (цель — игрок)
    TacticalFallenDiag finish;   // 12: добивание (цель — лежащая пешка)
};

// Continuation never admits a new pair. It only proves that the two exact
// bodies admitted by a prior strict unique-spatial match still satisfy their
// recipe. Distance and unrelated candidates become diagnostic after admission.
struct TacticalContinuation {
    bool  targetBodyPresent;
    bool  targetActionMatched;
    bool  evidenceBodyPresent;
    bool  evidenceKindMatched;
    bool  evidenceActionMatched;
    bool  distanceValid;
    float distanceM;
};

// ВАЖНО (85.23): вид жертвы передаётся ЯВНО (expectedKind). Раньше правило
// искалось по одному id ситуации, а у PLAYER-CHANT-HARASS таких правил четыре —
// по одному на вид. Поиск всегда возвращал первое (гоблинское), жертва-волк не
// совпадала по виду, и событие мигало: допуск -> victim-species-changed ->
// повторный допуск, 28 циклов за бой. Ожидаемый вид берётся из ДОПУЩЕННОГО
// правила (Director хранит его в s_tactical.responderKind).
// expectedKind == 0 сохраняет прежнее поведение (правило по id).
void InspectTacticalContinuation(int situation, uintptr_t targetBody,
                                 uintptr_t evidenceBody, const char* expectedKind,
                                 const TacticalPartyActor* party, int partyCount,
                                 const TacticalMonsterActor* monsters, int monsterCount,
                                 TacticalContinuation* out);

void ScanTacticalSituations(const TacticalPartyActor* party, int partyCount,
                            const TacticalMonsterActor* monsters, int monsterCount,
                            TacticalScan* out);

// 85.25: запасной выбор «ближайший» для семейства «услышал каст».
//
// ЗАЧЕМ. Правило требует РОВНО ОДНУ пару «кастующий ↔ монстр» в радиусе.
// В поле это оказалось невыполнимым: рядом всегда больше одного монстра, и
// событие отказывало в 86 случаях из 100 (36 отказов против 4 срабатываний,
// поле 2026-09-25, кап 5). Все отказы — reason=pairs-ambiguous при 6–8 монстрах
// в 12 м. Требование уникальности осмысленно там, где она ДОКАЗЫВАЕТ контакт
// (хват, прижим, подъём жертвы). У «услышал каст» доказывать нечего: важно
// лишь, что монстр этого вида рядом, — поэтому берём ближайшего вместо отказа.
//
// Действует только на правила, помеченные nearestPairFallback (четыре правила
// PLAYER-CHANT-HARASS). Хваты и прижимы не тронуты.
void SetNearestPairFallback(bool on);

// 85.28: радиус встречи у тела павшей пешки, метры. Расстояние считается от
// АРИЗЕНА до тела (а не от монстра): включается, когда игрок подходит к телу
// настолько, что монстры успевают развернуться и прийти в точку — спринт в
// DDDA очень быстрый, поэтому 10 по умолчанию, а не 3.
// 0 = механизм выключен (матчер молчит). Ставится Директором из ini.
void SetFallenGuardRadius(float meters);

// 85.30: выключатель добивания лежащей пешки (ключ `pawnFinish`). Живёт рядом с
// resetом радиуса по той же причине: ключ читает Директор, состояние остаётся
// локальным для файла.
void SetPawnFinishEnabled(bool on);
const char* TacticalSituationName(int situation);
const char* TacticalResponseName(int response);
const char* TacticalSituationPolicyReason(int situation);

} // namespace MonsterAI
