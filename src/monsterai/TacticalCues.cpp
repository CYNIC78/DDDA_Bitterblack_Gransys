// Data-only tactical cue matching. No writes, hooks, configuration, or UI.
#include "stdafx.h"
#include "TacticalCues.h"
#include <math.h>
#include <string.h>

namespace MonsterAI {
namespace {

// Literal lift/carry remains distinct from body-weight ground restraint.
static const char* const kPackLiftRescueHolderActs[] = {
    "cPlActLiftBeginSmall",
    "cPlActLiftGeneric",
    "cPlActLiftRun",
    "cPlActLiftWalk",
    "cPlActLiftJump"
};
static const char* const kPackLiftRescueVictimActs[] = {
    "cEm0200Lifted"
};

// Live Build 010 evidence established the two ground-restraint phases:
// GrabStart is an attempt/precursor; Hagaijime4Feet is the acting pawn's
// sustained successful body-weight pin. They are independent action signals
// with different response strengths, not a hard-coded temporal state machine.
static const char* const kPackGrabAlertActs[] = {
    "cPlActGrabStart"
};
static const char* const kPackGroundPinAlarmActs[] = {
    "cPlActHagaijime4Feet"
};
// Night-shore log 23: the pawn leaves GrabStart for cPlActHagaijime
// (not the wolf 4-feet pin). Same opportunist ALERT, longer hold.
static const char* const kGoblinGrabAlertActs[] = {
    "cPlActGrabStart",
    "cPlActHagaijime"
};

// Proactive caller acts: callers are vulnerable while blowing horn or howling.
static const char* const kGoblinHornCallerActs[] = {
    "cEm0100ActHornReinforce",
    "cEm0100ActHornTensionUp"
};
static const char* const kWolfHowlCallerActs[] = {
    "cEm0200Howling"
};
static const char* const kSaurianHowlCallerActs[] = {
    "cEm0400ActFriendHowl"
};

// Player chanting / casting acts: long incantation stances vulnerable to harass.
static const char* const kPlayerCasterActs[] = {
    "cPlActWpnWandBase",
    "cPlActWpnMagicBow",
    "cPlActWpnMagicShieldBase",
    "cPlActWpnSwordCstmMadouBase",
    "cPlActWpnDaggerCstmMadouBase"
};

// 85.29 ГЛАВНЫЕ ГРАБЛИ ЭТОГО МЕХАНИЗМА.
//
// Мир DDDA измеряется В САНТИМЕТРАХ: 1 м = 100 единиц. Все расстояния этого
// файла, проверенные полем, делят разницу координат на 100 (см. DistanceM) — и
// именно поэтому они сходятся с игрой: хват 0.49 м, рог 7.17 м, прижим 1.08 м.
// Партия читается тем же масштабом: GuardianDoctrine печатает `pawn-Arisen 7.9m`
// как raw/100. Ручное вычитание БЕЗ деления даёт ошибку в 100 раз: «10 м»
// превращаются в 10 см, а «15 м» — в 1500 м. Ровно на этом встреча у тела
// молчала в поле дважды, проходя при этом фикстуру: в фикстуре числа придуманы
// мной — в метрах, то есть в неверных единицах, и такая фикстура соглашается с
// любой ошибкой масштаба.
static const float kWorldUnitsPerMeter = 100.0f;

// 85.28: акты ЛЕЖАЩЕЙ пешки.
//
// ВНИМАНИЕ, ГРАБЛИ, на которые я уже наступил. Список имён в
// PartyRecon::CombatDownedActionHint — это КАНДИДАТЫ («их смысл намеренно не
// подтверждён»), и РЕАЛЬНОГО акта поля в нём НЕТ: в логах 85.24–85.27 падение
// пешки всегда выглядит как `PS: Hired2 DOWNED act=cPlActDmgCrumbleDead`, а
// такого имени в списке кандидатов не было. Правило, собранное по списку
// кандидатов, молчит в поле и при этом выглядит рабочим в тесте с выдуманным
// актом. Поэтому здесь ПЕРВЫМИ идут акты, подтверждённые живым полем.
//
// Акт Аризена cPlReviveCMC сюда НЕ входит: это действие поднимающего, а не
// признак падения.
// 85.30: ДВА РАЗНЫХ СОСТОЯНИЯ, два разных списка.
//
//   kPawnOutActs       — лежит БЕЗ СОЗНАНИЯ, ждёт подъёма. Сюда же переходные
//                        акты цикла, чтобы событие не мигало между кадрами.
//   kPawnKnockdownActs — лежит, НО В СОЗНАНИИ (сбита с ног). По полю 85.29
//                        (три срабатывания: одно настоящее падение и два
//                        сбивания с ног) это разные события для директора.
static const char* kPawnOutActs[] = {
    // подтверждено полем (строки `PS: <пешка> DOWNED act=...`):
    "cPlActDmgCrumbleDead", "cPlActCmcNeardeath", "cPlActCmcDead",
    // переходные акты того же цикла (падение -> подъём):
    "cPlActCmcReturn",
    // список наблюдателя (не подтверждён полем, но безвреден как расширение):
    "cPlActDead", "cPlActDmgDownDead"
};
static const char* kPawnKnockdownActs[] = {
    // подтверждено полем (`PS: <пешка> KNOCKDOWN act=cPlActDmgDown`):
    "cPlActDmgDown",
    // список наблюдателя:
    "cPlActDmgDownDamage"
};

struct TacticalRule {
    int       situation;
    const char* name;
    const char* policyReason;
    int       priority;
    int       response;
    float     urgency;
    const char* monsterKind;
    const char* const* targetActs;
    int       targetActCount;
    // Empty evidence action set means any current action of the exact kind.
    // This is used when the party action itself proves the interaction while
    // spatial uniqueness resolves the exact restrained body for exclusion.
    const char* const* evidenceActs;
    int       evidenceActCount;
    bool      requireGloballyUniqueEvidence;
    float     maxPairDistanceM;
    uint32_t  maxLeaseMs;
    bool      excludeEvidenceBody;
    bool      arisenOnly;
    bool      casterVocationOnly;
    // 85.28: ветвь «встреча у тела павшей пешки». Признак правила не действие
    // цели, а СОСТОЯНИЕ другого члена партии: пешка лежит рядом с Аризеном.
    // Поэтому такое правило не может выражаться через targetActs цели и идёт
    // отдельной ветвью матчера (MatchFallenGuard). Стоит ПЕРЕД
    // nearestPairFallback: хвост существующих строк дополнен ещё одним false.
    bool      fallenPawnCue;
    // 85.25: см. SetNearestPairFallback в заголовке. Поле стоит ПОСЛЕДНИМ,
    // поэтому существующие строки таблицы остаются валидными без правок —
    // значение по умолчанию false, и включается оно только там, где нужно.
    bool      nearestPairFallback;
};

// 85.25: включается ключом [monsterAI] chantNearest; по умолчанию выключено
// ЗДЕСЬ и включается Директором при инициализации — так решение остаётся
// настраиваемым в поле без пересборки.
static bool s_nearestPairFallback = false;

// 85.28: см. SetFallenGuardRadius в заголовке.
static float s_fallenGuardRadius = 10.0f;
// 85.30: добивание лежащей пешки в сознании. Включено по умолчанию: это прямое
// указание владельца по полю 85.29. Ключ `pawnFinish = 0` выключает без сборки.
static bool  s_pawnFinishEnabled = true;


static bool IsCasterVocation(int v)
{
    // 3 = Mage, 9 = Sorcerer (pure blue)
    // 4 = Mystic Knight, 6 = Magick Archer (hybrid blue)
    return v == 3 || v == 9 || v == 4 || v == 6;
}

// Adding another proved species/restraint is one row plus its action array.
// Aggro write admission remains independently species-specific downstream.
static const TacticalRule kRules[] = {
    {
        TACTICAL_SITUATION_PACK_GROUND_PIN_ALARM,
        "PACK-GROUND-PIN-ALARM",
        "tactical-ground-pin-alarm",
        200,
        TACTICAL_RESPONSE_ALARM,
        1.0f,
        "uEm0200",
        kPackGroundPinAlarmActs,
        (int)(sizeof(kPackGroundPinAlarmActs) /
              sizeof(kPackGroundPinAlarmActs[0])),
        0, 0,
        false,
        2.00f,
        4000,
        true,
        false, false, false, false
    },
    {
        TACTICAL_SITUATION_PACK_LIFT_RESCUE,
        "PACK-LIFT-RESCUE",
        "tactical-pack-lift-rescue",
        150,
        TACTICAL_RESPONSE_ALARM,
        1.0f,
        "uEm0200",
        kPackLiftRescueHolderActs,
        (int)(sizeof(kPackLiftRescueHolderActs) /
              sizeof(kPackLiftRescueHolderActs[0])),
        kPackLiftRescueVictimActs,
        (int)(sizeof(kPackLiftRescueVictimActs) /
              sizeof(kPackLiftRescueVictimActs[0])),
        true,
        2.50f,
        2500,
        true,
        false, false, false, false
    },
    {
        TACTICAL_SITUATION_PACK_GRAB_ALERT,
        "PACK-GRAB-ALERT",
        "tactical-grab-alert",
        100,
        TACTICAL_RESPONSE_ALERT,
        1.0f,
        "uEm0200",
        kPackGrabAlertActs,
        (int)(sizeof(kPackGrabAlertActs) / sizeof(kPackGrabAlertActs[0])),
        0, 0,
        false,
        2.00f,
        750,
        true,
        false, false, false, false
    },
    {
        TACTICAL_SITUATION_GOBLIN_GRAB_ALERT,
        "GOBLIN-GRAB-ALERT",
        "tactical-goblin-grab-alert",
        90,
        TACTICAL_RESPONSE_ALERT,
        1.0f,
        "uEm0100",
        kGoblinGrabAlertActs,
        (int)(sizeof(kGoblinGrabAlertActs) / sizeof(kGoblinGrabAlertActs[0])),
        0, 0,
        false,
        2.00f,
        4000,
        true,
        false, false, false, false
    },
    {
        TACTICAL_SITUATION_HOB_GRAB_ALERT,
        "HOB-GRAB-ALERT",
        "tactical-hob-grab-alert",
        88,
        TACTICAL_RESPONSE_ALERT,
        1.0f,
        "uEm0101",
        kGoblinGrabAlertActs,
        (int)(sizeof(kGoblinGrabAlertActs) / sizeof(kGoblinGrabAlertActs[0])),
        0, 0,
        false,
        2.00f,
        4000,
        true,
        false, false, false, false
    },
    {
        TACTICAL_SITUATION_GOB_HORN_ALERT,
        "GOB-HORN-ALERT",
        "tactical-gob-horn-alert",
        85,
        TACTICAL_RESPONSE_ALERT,
        0.65f,
        "uEm0100",
        0, 0,
        kGoblinHornCallerActs,
        (int)(sizeof(kGoblinHornCallerActs) / sizeof(kGoblinHornCallerActs[0])),
        false,
        12.00f,
        4500,
        true,
        false, false, false, false
    },
    {
        TACTICAL_SITUATION_HOB_HORN_ALERT,
        "HOB-HORN-ALERT",
        "tactical-hob-horn-alert",
        84,
        TACTICAL_RESPONSE_ALERT,
        0.65f,
        "uEm0101",
        0, 0,
        kGoblinHornCallerActs,
        (int)(sizeof(kGoblinHornCallerActs) / sizeof(kGoblinHornCallerActs[0])),
        false,
        12.00f,
        4500,
        true,
        false, false, false, false
    },
    {
        TACTICAL_SITUATION_WOLF_HOWL_ALERT,
        "WOLF-HOWL-ALERT",
        "tactical-wolf-howl-alert",
        80,
        TACTICAL_RESPONSE_ALERT,
        0.65f,
        "uEm0200",
        0, 0,
        kWolfHowlCallerActs,
        (int)(sizeof(kWolfHowlCallerActs) / sizeof(kWolfHowlCallerActs[0])),
        false,
        14.00f,
        3500,
        true,
        false, false, false, false
    },
    {
        TACTICAL_SITUATION_SAURIAN_HOWL_ALERT,
        "SAURIAN-HOWL-ALERT",
        "tactical-saurian-howl-alert",
        78,
        TACTICAL_RESPONSE_ALERT,
        0.65f,
        "uEm0400",
        0, 0,
        kSaurianHowlCallerActs,
        (int)(sizeof(kSaurianHowlCallerActs) / sizeof(kSaurianHowlCallerActs[0])),
        false,
        10.00f,
        4000,
        true,
        false, false, false, false
    },
    {
        TACTICAL_SITUATION_PLAYER_CHANT_HARASS,
        "PLAYER-CHANT-HARASS",
        "tactical-player-chant-harass",
        75,
        TACTICAL_RESPONSE_ALERT,
        0.55f,
        "uEm0100",
        kPlayerCasterActs,
        (int)(sizeof(kPlayerCasterActs) / sizeof(kPlayerCasterActs[0])),
        0, 0,
        false,
        12.00f,
        3500,
        false,
        true, true, false, true
    },
    {
        TACTICAL_SITUATION_PLAYER_CHANT_HARASS,
        "PLAYER-CHANT-HARASS",
        "tactical-player-chant-harass",
        75,
        TACTICAL_RESPONSE_ALERT,
        0.55f,
        "uEm0200",
        kPlayerCasterActs,
        (int)(sizeof(kPlayerCasterActs) / sizeof(kPlayerCasterActs[0])),
        0, 0,
        false,
        14.00f,
        3500,
        false,
        true, true, false, true
    },
    {
        TACTICAL_SITUATION_PLAYER_CHANT_HARASS,
        "PLAYER-CHANT-HARASS",
        "tactical-player-chant-harass",
        74,
        TACTICAL_RESPONSE_ALERT,
        0.55f,
        "uEm0101",
        kPlayerCasterActs,
        (int)(sizeof(kPlayerCasterActs) / sizeof(kPlayerCasterActs[0])),
        0, 0,
        false,
        12.00f,
        3500,
        false,
        true, true, false, true
    },
    {
        TACTICAL_SITUATION_PLAYER_CHANT_HARASS,
        "PLAYER-CHANT-HARASS",
        "tactical-player-chant-harass",
        73,
        TACTICAL_RESPONSE_ALERT,
        0.55f,
        "uEm0400",
        kPlayerCasterActs,
        (int)(sizeof(kPlayerCasterActs) / sizeof(kPlayerCasterActs[0])),
        0, 0,
        false,
        10.00f,
        3500,
        false,
        true, true, false, true
    },
    // ---- 85.28: ВСТРЕЧА У ТЕЛА ПАВШЕЙ ПЕШКИ ---------------------------------
    // Замысел владельца: монстры, стоящие у тела павшей пешки, ДОЛЖНЫ
    // развернуться на игрока, который идёт её поднимать, — «готовятся встретить
    // игрока, пытающегося воскресить пешку и восстановить баланс сил».
    //
    // Почему событие, а не перестановка выбора цели. В 85.27 механизм был
    // встроен в выбор цели (PackMark). Поле показало две вещи: во-первых, та
    // ветка не исполняется без волков (бой был гоблинский, ноль строк решений),
    // во-вторых — и это главное — ВЫБОР ЦЕЛИ В ИГРЕ НИЧЕГО НЕ ДЕЛАЕТ: его читает
    // только панель F12. Реально разворачивает монстров Aggro::DirectorFocusSet,
    // и зовёт его лишь ветка тактических приказов. Поэтому встреча оформлена
    // ОДНИМ ИЗ СОБЫТИЙ: так она получает вывод в агро, аренду, отбор
    // отвечающих и освобождение по таймауту — тем же путём, что рог и хват.
    //
    // Ярости почти не даём (0.70 против 1.0 у «вплотную увиденного»): это
    // готовность и подход, а не бешенство. Тир ответа ALERT, не ALARM.
    //
    // maxPairDistanceM здесь — расстояние от МОНСТРА до ТЕЛА (кого считать
    // «толпой у тела»); радиус подхода игрока задаётся ключом ini и живёт в
    // s_fallenGuardRadius.
    {
        TACTICAL_SITUATION_FALLEN_GUARD,
        "FALLEN-GUARD",
        "tactical-fallen-guard",
        60,
        TACTICAL_RESPONSE_ALERT,
        0.70f,
        "uEm0100",
        kPawnOutActs,
        (int)(sizeof(kPawnOutActs) / sizeof(kPawnOutActs[0])),
        0, 0,
        false,
        15.00f,
        4000,
        false,
        true, false, true, false
    },
    {
        TACTICAL_SITUATION_FALLEN_GUARD,
        "FALLEN-GUARD",
        "tactical-fallen-guard",
        59,
        TACTICAL_RESPONSE_ALERT,
        0.70f,
        "uEm0101",
        kPawnOutActs,
        (int)(sizeof(kPawnOutActs) / sizeof(kPawnOutActs[0])),
        0, 0,
        false,
        15.00f,
        4000,
        false,
        true, false, true, false
    },
    {
        TACTICAL_SITUATION_FALLEN_GUARD,
        "FALLEN-GUARD",
        "tactical-fallen-guard",
        58,
        TACTICAL_RESPONSE_ALERT,
        0.70f,
        "uEm0200",
        kPawnOutActs,
        (int)(sizeof(kPawnOutActs) / sizeof(kPawnOutActs[0])),
        0, 0,
        false,
        15.00f,
        4000,
        false,
        true, false, true, false
    },
    {
        TACTICAL_SITUATION_FALLEN_GUARD,
        "FALLEN-GUARD",
        "tactical-fallen-guard",
        57,
        TACTICAL_RESPONSE_ALERT,
        0.70f,
        "uEm0400",
        kPawnOutActs,
        (int)(sizeof(kPawnOutActs) / sizeof(kPawnOutActs[0])),
        0, 0,
        false,
        15.00f,
        4000,
        false,
        true, false, true, false
    },
    {
        TACTICAL_SITUATION_PAWN_FINISH,
        "PAWN-FINISH",
        "tactical-pawn-finish",
        70,
        TACTICAL_RESPONSE_ALERT,
        0.85f,
        "uEm0100",
        kPawnKnockdownActs,
        (int)(sizeof(kPawnKnockdownActs) / sizeof(kPawnKnockdownActs[0])),
        0, 0,
        false,
        10.00f,
        4000,
        false,
        false, false, false, false
    },
    {
        TACTICAL_SITUATION_PAWN_FINISH,
        "PAWN-FINISH",
        "tactical-pawn-finish",
        69,
        TACTICAL_RESPONSE_ALERT,
        0.85f,
        "uEm0101",
        kPawnKnockdownActs,
        (int)(sizeof(kPawnKnockdownActs) / sizeof(kPawnKnockdownActs[0])),
        0, 0,
        false,
        10.00f,
        4000,
        false,
        false, false, false, false
    },
    {
        TACTICAL_SITUATION_PAWN_FINISH,
        "PAWN-FINISH",
        "tactical-pawn-finish",
        68,
        TACTICAL_RESPONSE_ALERT,
        0.85f,
        "uEm0200",
        kPawnKnockdownActs,
        (int)(sizeof(kPawnKnockdownActs) / sizeof(kPawnKnockdownActs[0])),
        0, 0,
        false,
        10.00f,
        4000,
        false,
        false, false, false, false
    },
    {
        TACTICAL_SITUATION_PAWN_FINISH,
        "PAWN-FINISH",
        "tactical-pawn-finish",
        67,
        TACTICAL_RESPONSE_ALERT,
        0.85f,
        "uEm0400",
        kPawnKnockdownActs,
        (int)(sizeof(kPawnKnockdownActs) / sizeof(kPawnKnockdownActs[0])),
        0, 0,
        false,
        10.00f,
        4000,
        false,
        false, false, false, false
    },

};

static bool ExactAction(const char* act, const char* const* accepted, int count)
{
    if (!act || !act[0] || !accepted || count <= 0) return false;
    for (int i = 0; i < count; ++i)
        if (!strcmp(act, accepted[i])) return true;
    return false;
}

static bool EvidenceAction(const char* act, const TacticalRule& rule)
{
    if (!act || !act[0]) return false;
    if (rule.evidenceActCount <= 0) return true;
    return ExactAction(act, rule.evidenceActs, rule.evidenceActCount);
}

static bool ExactKind(const char* kind, const char* expected)
{
    return kind && expected && !strcmp(kind, expected);
}

static bool DistanceM(const TacticalPartyActor& p, const TacticalMonsterActor& m,
                      float* out)
{
    if (out) *out = -1.0f;
    if (!p.positionValid || !m.positionValid) return false;
    const float dx = p.x - m.x;
    const float dy = p.y - m.y;
    const float dz = p.z - m.z;
    const float d = sqrtf(dx * dx + dy * dy + dz * dz) / 100.0f;
    if (!(d == d) || d < 0.0f || d > 100000.0f) return false;
    if (out) *out = d;
    return true;
}

static void InitScan(TacticalScan* out)
{
    memset(out, 0, sizeof(*out));
    out->situation = TACTICAL_SITUATION_NONE;
    out->name = "NONE";
    out->response = TACTICAL_RESPONSE_NONE;
    out->firstTargetSlot = -1;
    out->nearestDistanceM = -1.0f;
    out->match.situation = TACTICAL_SITUATION_NONE;
    out->match.response = TACTICAL_RESPONSE_NONE;
    out->match.targetSlot = -1;
    out->match.pairDistanceM = -1.0f;
}

static const TacticalRule* FindRule(int situation)
{
    for (int i = 0; i < (int)(sizeof(kRules) / sizeof(kRules[0])); ++i)
        if (kRules[i].situation == situation) return &kRules[i];
    return 0;
}

} // namespace

const char* TacticalSituationName(int situation)
{
    const TacticalRule* rule = FindRule(situation);
    return rule ? rule->name : "NONE";
}

const char* TacticalSituationPolicyReason(int situation)
{
    const TacticalRule* rule = FindRule(situation);
    return rule ? rule->policyReason : "tactical-none";
}

const char* TacticalResponseName(int response)
{
    if (response == TACTICAL_RESPONSE_ALERT) return "ALERT";
    if (response == TACTICAL_RESPONSE_ALARM) return "ALARM";
    return "NONE";
}

// 85.28: расстояние между двумя ЧЛЕНАМИ ПАРТИИ. Существующий DistanceM
// считает пару «партия <-> монстр», а встрече нужен «Аризен <-> тело пешки».
static bool PartyDistanceM(const TacticalPartyActor& a,
                           const TacticalPartyActor& b, float* outM)
{
    if (!a.positionValid || !b.positionValid) return false;
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    // 85.29: делим на масштаб мира (сантиметры). Без деления радиус 10 «метров»
    // работал как 10 сантиметров и не срабатывал никогда.
    if (outM) *outM = sqrtf(dx * dx + dy * dy + dz * dz) / kWorldUnitsPerMeter;
    return true;
}

// 85.28: ветвь «встреча у тела павшей пешки».
//
// ЦЕЛЬ всегда Аризен (это его встретят), ПРИЗНАК — лежащая рядом пешка,
// ИСПОЛНИТЕЛИ — монстры вида у ТЕЛА. Существующие ветви матчера так не умеют:
// у них признак — действие самой цели (каст, хват), а тут признак — состояние
// ДРУГОГО члена партии плюс расстояние. Поэтому отдельная функция в том же
// файле: она возвращает TacticalMatch с тем же контрактом, и дальше событие
// идёт общим путём — допуск, аренда, вывод в агро, освобождение по таймауту.
// 85.29: пешка «лежит»? Два источника, в порядке надёжности.
//
// Первый — вердикт наблюдателя PartyStatus, подтверждённый живым полем
// (строки `PS: Hired2 DOWNED act=...`). Он не зависит от имён актов, а имена
// уже один раз подвели: список кандидатов в PartyRecon не содержал реального
// акта поля.
// Второй — запасной: имена актов из kFallenPawnActs.
// Ни один из них не отличает падение от «сбит с ног» (KNOCKDOWN) — и не
// должен: для толпы у тела и то и другое значит «пешка на земле».
static bool PawnIsOut(const TacticalPartyActor& a, const TacticalRule& rule)
{
    // Вердикт наблюдателя: лежит без сознания (neardeath) — ждёт подъёма.
    if (a.downedValid && !a.downedAwake) return true;
    return ExactAction(a.act, rule.targetActs, rule.targetActCount);
}

static bool PawnOnGroundAwake(const TacticalPartyActor& a, const TacticalRule& rule)
{
    // Вердикт наблюдателя: лежит, но в сознании (сбита с ног) — то, что
    // владелец назвал «пешка на земле, но не без сознания».
    if (a.downedAwake) return true;
    return ExactAction(a.act, rule.targetActs, rule.targetActCount);
}

// 85.31: пешку НЕСУТ на руках. Поле 85.29 показало, что в этом состоянии тело
// отдаёт координаты (0,0,0), и наш лог печатал по ним «подход 340 м» — то есть
// выдуманное число вместо честного «позиции нет». Нести можно только того, кто
// лежит, поэтому для встречи это не «на подходе», а «пешки на земле нет».
static bool ActIsCarry(const char* act)
{
    return act && strstr(act, "Lift") != 0;
}

// Позиция (0,0,0) — не место в мире, а «координат нет»: живые тела в поле
// отдают настоящие числа (тысячи и десятки тысяч сантиметров).
static bool PositionIsUnusable(float x, float y, float z)
{
    return x == 0.0f && y == 0.0f && z == 0.0f;
}

static void ClearFallenDiag(TacticalFallenDiag& d, int situation)
{
    d.situation = situation;
    d.reason = 0;
    d.pawnSlot = -1;   // «не найдена»: 0 значил бы Аризена
    d.approachM = -1.0f;
    d.nearestKindM = -1.0f;
}

static void MatchFallenGuard(const TacticalRule& rule,
                             const TacticalPartyActor* party, int partyCount,
                             const TacticalMonsterActor* monsters, int monsterCount,
                             TacticalScan* diag)
{
    TacticalFallenDiag& d = diag->fallen;
    ClearFallenDiag(d, rule.situation);
    d.monstersTotal = monsterCount;
    // 85.31: вид считаем СРАЗУ, до всех отказов. Раньше счёт шёл после проверки
    // подхода, и в логе появлялось «monstersOfKind=0» в бою, где монстры этого
    // вида были — читалось как «их нет», а значило «мы не считали».
    for (int m = 0; m < monsterCount; ++m)
        if (monsters[m].body && ExactKind(monsters[m].kind, rule.monsterKind))
            ++d.monstersOfKind;
    if (s_fallenGuardRadius <= 0.0001f) {   // ключ 0 = механизм выключен
        d.reason = "mechanism-off";
        return;
    }

    // Цель — Аризен. Без его позиции встреча бессмысленна: расстояние считать
    // нечем, а угадывать нельзя.
    const TacticalPartyActor* arisen = 0;
    for (int p = 0; p < partyCount; ++p) {
        if (party[p].slot != 0 || !party[p].body) continue;
        arisen = &party[p];
        break;
    }
    const bool arisenPosValid = arisen && arisen->positionValid;
    if (arisen) {
        d.arisenPosValid = arisenPosValid;
        d.arisenX = arisen->x; d.arisenY = arisen->y; d.arisenZ = arisen->z;
    }

    // Признак: лежащая пешка. Если их несколько — ближайшая к игроку.
    const TacticalPartyActor* pawn = 0;
    float pawnDist = -1.0f;
    bool pawnSeenWithoutPos = false;
    bool carried = false;
    for (int p = 0; p < partyCount; ++p) {
        const TacticalPartyActor& cand = party[p];
        if (cand.slot == 0 || !cand.body) continue;
        if (!PawnIsOut(cand, rule)) continue;
        if (ActIsCarry(cand.act)) {   // 85.31: пешку несут — на земле её нет
            if (!d.pawnBody) {
                d.pawnSlot = cand.slot;
                d.pawnBody = cand.body;
                d.pawnAct = cand.act;
                d.pawnDowned = true;
            }
            carried = true;
            continue;
        }
        if (!d.pawnBody) {
            d.pawnSlot = cand.slot;
            d.pawnBody = cand.body;
            d.pawnAct = cand.act;
            d.pawnDowned = true;
        }
        float dist = -1.0f;
        if (!arisenPosValid || !PartyDistanceM(*arisen, cand, &dist)) {
            pawnSeenWithoutPos = true;
            continue;
        }
        if (pawnDist < 0.0f || dist < pawnDist) { pawn = &cand; pawnDist = dist; }
    }
    if (!pawn && carried) { d.reason = "pawn-carried"; return; }
    if (!pawn && pawnSeenWithoutPos) {
        d.reason = arisenPosValid ? "pawn-position-unavailable"
                                  : "arisen-position-unavailable";
        return;
    }
    if (!pawn) { d.reason = "no-pawn-down"; return; }

    d.pawnPosValid = true;
    d.pawnX = pawn->x; d.pawnY = pawn->y; d.pawnZ = pawn->z;
    if (PositionIsUnusable(pawn->x, pawn->y, pawn->z)) {
        d.pawnPosValid = false;
        d.reason = "pawn-position-unavailable";
        return;
    }
    d.approachM = pawnDist;   // главное число: подход ИГРОКА к телу, метры
    if (pawnDist > s_fallenGuardRadius) {
        d.reason = "approach-too-far";
        return;
    }
    if (diag->targetCandidates == 0) {
        diag->targetCandidates = 1;
        diag->firstTargetSlot = arisen->slot;
        diag->firstTargetBody = arisen->body;
        diag->firstTargetAct = arisen->act;
    }

    // Исполнители: монстры этого вида у ТЕЛА (не у игрока — у тела стоит толпа).
    //
    // 85.29: расстояние здесь считается тем же масштабом мира. Раньше тут
    // вычитались координаты монстра (сантиметры) и пешки (тоже сантиметры) без
    // деления — значит «15 м» означали 1500 м, и подходящий исполнитель не
    // находился НИКОГДА, даже когда гоблины стояли на теле.
    const TacticalMonsterActor* nearest = 0;
    float nearestD = -1.0f;
    for (int m = 0; m < monsterCount; ++m) {
        const TacticalMonsterActor& evidence = monsters[m];
        if (!evidence.body) continue;
        if (!ExactKind(evidence.kind, rule.monsterKind)) continue;
        if (!EvidenceAction(evidence.act, rule)) continue;
        if (!evidence.positionValid) { ++diag->positionRejected; continue; }
        const float dx = pawn->x - evidence.x;
        const float dy = pawn->y - evidence.y;
        const float dz = pawn->z - evidence.z;
        const float dd = sqrtf(dx * dx + dy * dy + dz * dz) / kWorldUnitsPerMeter;
        if (d.nearestKindM < 0.0f || dd < d.nearestKindM) d.nearestKindM = dd;
        if (dd > rule.maxPairDistanceM) continue;
        ++diag->evidenceCandidates;
        ++diag->pairCandidates;
        if (!diag->firstEvidenceBody) {
            diag->firstEvidenceBody = evidence.body;
            diag->firstEvidenceAct = evidence.act;
        }
        if (nearestD < 0.0f || dd < nearestD) { nearest = &evidence; nearestD = dd; }
    }
    if (!nearest) { d.reason = "no-mob-at-body"; return; }

    diag->match.situation = rule.situation;
    diag->match.name = rule.name;
    diag->match.policyReason = rule.policyReason;
    diag->match.priority = rule.priority;
    diag->match.response = rule.response;
    diag->match.urgency = rule.urgency;
    diag->match.targetSlot = arisen->slot;
    diag->match.targetBody = arisen->body;
    diag->match.evidenceBody = nearest->body;
    // targetAct — акт Аризена (в логе это holderAct): он бежит, стоит, жмёт E,
    // и по нему видно, что игрок действительно рядом, а не телепортировался.
    diag->match.targetAct = arisen->act;
    diag->match.evidenceAct = nearest->act;
    // distance в логе — это ПОДХОД ИГРОКА к телу: главное число механизма.
    diag->match.pairDistanceM = pawnDist;
    diag->match.maxLeaseMs = rule.maxLeaseMs;
    diag->match.excludeEvidenceBody = rule.excludeEvidenceBody;
    diag->match.responderKind = rule.monsterKind;
    diag->match.pairsConsidered = diag->pairCandidates;
}

// 85.30: ДОБИВАНИЕ ЛЕЖАЩЕЙ ПЕШКИ (в сознании).
//
// Отличие от встречи у тела — в АДРЕСАТЕ. Там целью был игрок, пришедший
// поднимать товарища; здесь цель — САМА ЛЕЖАЩАЯ ПЕШКА. Владелец, поле 85.29:
// «пешка на земле, но не без сознания = таргет для окружающих монстров! Не на
// игрока надо ломиться, а добивать лежащую пешку, чтобы выключить её из боя, не
// дать ей подняться».
//
// Поэтому радиус от игрока здесь НЕ участвует вовсе: работает близость
// ОКРУЖАЮЩИХ монстров к пешке (maxPairDistanceM). Радиус ключа
// fallenGuardRadius относится только к встрече у тела.
static void MatchPawnFinish(const TacticalRule& rule,
                            const TacticalPartyActor* party, int partyCount,
                            const TacticalMonsterActor* monsters, int monsterCount,
                            TacticalScan* diag)
{
    TacticalFallenDiag& d = diag->finish;
    ClearFallenDiag(d, rule.situation);
    if (!s_pawnFinishEnabled) {   // ключ 0 = механизм выключен
        d.reason = "mechanism-off";
        return;
    }
    d.monstersTotal = monsterCount;
    for (int m = 0; m < monsterCount; ++m)
        if (monsters[m].body && ExactKind(monsters[m].kind, rule.monsterKind))
            ++d.monstersOfKind;

    // Пешка, которую добивают: лежит в сознании, ближайший монстр этого вида —
    // самый близкий. Если таких пешек несколько, берём ту, к которой монстры
    // стоят плотнее всего: это и есть «окружающие».
    const TacticalPartyActor* victimPawn = 0;
    const TacticalMonsterActor* nearest = 0;
    float bestMobDist = -1.0f;
    bool sawGroundPawn = false;
    for (int p = 0; p < partyCount; ++p) {
        const TacticalPartyActor& cand = party[p];
        if (cand.slot == 0 || !cand.body) continue;          // Аризен не цель
        if (!PawnOnGroundAwake(cand, rule)) continue;
        sawGroundPawn = true;
        if (!d.pawnBody) {
            d.pawnSlot = cand.slot;
            d.pawnBody = cand.body;
            d.pawnAct = cand.act;
            d.pawnDowned = true;
        }
        if (!cand.positionValid) continue;
        d.pawnPosValid = true;
        d.pawnX = cand.x; d.pawnY = cand.y; d.pawnZ = cand.z;
        if (PositionIsUnusable(cand.x, cand.y, cand.z)) {
            d.pawnPosValid = false;
            continue;   // позиции нет — считать нечего, и выдумывать нельзя
        }
        for (int m = 0; m < monsterCount; ++m) {
            const TacticalMonsterActor& mob = monsters[m];
            if (!mob.body) continue;
            if (!ExactKind(mob.kind, rule.monsterKind)) continue;
            if (!EvidenceAction(mob.act, rule)) continue;
            if (!mob.positionValid) { ++diag->positionRejected; continue; }
            const float dx = cand.x - mob.x, dy = cand.y - mob.y, dz = cand.z - mob.z;
            const float dist = sqrtf(dx * dx + dy * dy + dz * dz)
                             / kWorldUnitsPerMeter;
            if (d.nearestKindM < 0.0f || dist < d.nearestKindM)
                d.nearestKindM = dist;
            if (dist > rule.maxPairDistanceM) continue;
            if (bestMobDist < 0.0f || dist < bestMobDist) {
                bestMobDist = dist;
                victimPawn = &cand;
                nearest = &mob;
            }
        }
    }
    if (!sawGroundPawn) { d.reason = "no-pawn-on-ground-awake"; return; }
    // Вид монстра считаем только по правилу с наибольшим числом подходящих —
    // иначе отказ «своего» вида затирал бы отказ вида, который в бою есть.
    if (!victimPawn || !nearest) {
        d.reason = d.pawnPosValid ? "no-mob-at-pawn" : "pawn-position-unavailable";
        return;
    }

    diag->targetCandidates = 1;
    diag->firstTargetSlot = victimPawn->slot;
    diag->firstTargetBody = victimPawn->body;
    diag->firstTargetAct = victimPawn->act;
    ++diag->evidenceCandidates;
    ++diag->pairCandidates;
    diag->firstEvidenceBody = nearest->body;
    diag->firstEvidenceAct = nearest->act;
    d.nearestKindM = bestMobDist;

    diag->match.situation = rule.situation;
    diag->match.name = rule.name;
    diag->match.policyReason = rule.policyReason;
    diag->match.priority = rule.priority;
    diag->match.response = rule.response;
    diag->match.urgency = rule.urgency;
    diag->match.targetSlot = victimPawn->slot;      // ЦЕЛЬ — ПЕШКА, не игрок
    diag->match.targetBody = victimPawn->body;
    diag->match.evidenceBody = nearest->body;
    diag->match.targetAct = victimPawn->act;
    diag->match.evidenceAct = nearest->act;
    diag->match.pairDistanceM = bestMobDist;        // монстр -> пешка, метры
    diag->match.maxLeaseMs = rule.maxLeaseMs;
    diag->match.excludeEvidenceBody = rule.excludeEvidenceBody;
    diag->match.responderKind = rule.monsterKind;
    diag->match.pairsConsidered = diag->pairCandidates;
}

void InspectTacticalContinuation(int situation, uintptr_t targetBody,
                                 uintptr_t evidenceBody, const char* expectedKind,
                                 const TacticalPartyActor* party, int partyCount,
                                 const TacticalMonsterActor* monsters, int monsterCount,
                                 TacticalContinuation* out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->distanceM = -1.0f;
    const TacticalRule* rule = FindRule(situation);
    if (!rule || !targetBody || !evidenceBody || !party || partyCount <= 0
        || !monsters || monsterCount <= 0) return;

    const TacticalPartyActor* target = 0;
    const TacticalMonsterActor* evidence = 0;
    for (int i = 0; i < partyCount; ++i) {
        if (party[i].body != targetBody) continue;
        out->targetBodyPresent = true;
        if (rule->situation == TACTICAL_SITUATION_PAWN_FINISH) {
            // 85.30: цель — САМА ПЕШКА. Рецепт держится, пока она лежит в
            // сознании: встала — цель ушла; ушла в neardeath — задача
            // выполнена (лежащую без сознания не добиваем).
            out->targetActionMatched = PawnOnGroundAwake(party[i], *rule);
            if (out->targetActionMatched) target = &party[i];
            break;
        }
        if (rule->fallenPawnCue) {
            // 85.28: у встречи цель — Аризен, а рецепт — «пешка всё ещё лежит
            // рядом». Собственный акт Аризена тут ни при чём (он бежит, потом
            // жмёт E, потом дерётся), и требовать от него совпадения с
            // kFallenPawnActs нельзя: событие снималось бы в тот же такт.
            if (party[i].slot == 0) {
                out->targetActionMatched = true;
                target = &party[i];
            }
            break;
        }
        if (rule->arisenOnly && party[i].slot != 0) continue;
        if (rule->casterVocationOnly && !IsCasterVocation(party[i].vocation)) continue;
        if (rule->targetActCount <= 0
            || ExactAction(party[i].act, rule->targetActs, rule->targetActCount)) {
            out->targetActionMatched = true;
            target = &party[i];
        }
        break;
    }

    // 85.28: пешка поднялась или игрок отошёл — событие кончилось. Проверяем
    // ровно то же условие, что и при допуске: иначе встреча висела бы вечно.
    if (rule->fallenPawnCue && target) {
        bool pawnStillDown = false;
        for (int i = 0; i < partyCount && !pawnStillDown; ++i) {
            const TacticalPartyActor& cand = party[i];
            if (cand.slot == 0 || !cand.body) continue;
            // 85.29: тот же предикат, что и при допуске. Иначе событие падало бы
            // на первом же такте после входа: допуск по вердикту наблюдателя, а
            // освобождение по имени акта, которого в списке нет.
            if (!PawnIsOut(cand, *rule)) continue;
            float d = -1.0f;
            if (!PartyDistanceM(*target, cand, &d)) continue;
            if (d <= s_fallenGuardRadius) pawnStillDown = true;
        }
        if (!pawnStillDown) out->targetActionMatched = false;
    }
    // Вид допущенного правила приоритетнее «первого правила с таким id»:
    // у PLAYER-CHANT-HARASS видов четыре, и поиск по id всегда давал гоблина.
    const char* wantKind = (expectedKind && expectedKind[0]) ? expectedKind
                                                            : rule->monsterKind;
    for (int i = 0; i < monsterCount; ++i) {
        if (monsters[i].body != evidenceBody) continue;
        out->evidenceBodyPresent = true;
        out->evidenceKindMatched = ExactKind(monsters[i].kind, wantKind);
        out->evidenceActionMatched = out->evidenceKindMatched
                                  && EvidenceAction(monsters[i].act, *rule);
        evidence = &monsters[i];
        break;
    }
    if (target && evidence)
        out->distanceValid = DistanceM(*target, *evidence, &out->distanceM);
}

void ScanTacticalSituations(const TacticalPartyActor* party, int partyCount,
                            const TacticalMonsterActor* monsters, int monsterCount,
                            TacticalScan* out)
{
    if (!out) return;
    InitScan(out);
    if (!party || partyCount <= 0 || !monsters || monsterCount <= 0) return;

    TacticalScan bestDiagnostic;
    TacticalScan bestMatchScan;
    InitScan(&bestDiagnostic);
    InitScan(&bestMatchScan);
    // 85.29: диагностика встречи не участвует в отборе лучшего правила — она
    // нужна даже тогда, когда правило проиграло и в лог ничего не попало.
    TacticalFallenDiag fallenCarry;
    memset(&fallenCarry, 0, sizeof(fallenCarry));
    fallenCarry.approachM = -1.0f;
    fallenCarry.nearestKindM = -1.0f;
    fallenCarry.pawnSlot = -1;
    bool fallenHave = false;
    TacticalFallenDiag finishCarry;
    memset(&finishCarry, 0, sizeof(finishCarry));
    finishCarry.approachM = -1.0f;
    finishCarry.nearestKindM = -1.0f;
    finishCarry.pawnSlot = -1;
    bool finishHave = false;
    int bestDiagnosticPriority = -1;
    int bestMatchPriority = -1;

    for (int r = 0; r < (int)(sizeof(kRules) / sizeof(kRules[0])); ++r) {
        const TacticalRule& rule = kRules[r];
        TacticalScan diag;
        InitScan(&diag);
        diag.situation = rule.situation;
        diag.name = rule.name;
        diag.response = rule.response;

        const bool proactiveCaller = (rule.targetActCount <= 0);

        if (rule.situation == TACTICAL_SITUATION_PAWN_FINISH) {
            // Ветвь выбирается по НОМЕРУ ситуации, а не новым флагом в строке
            // таблицы: добавление поля в структуру уже один раз молча
            // переставило смысл флагов в тринадцати строках. Номер ситуации —
            // такое же штатное поле, и его видно прямо в строке.
            MatchPawnFinish(rule, party, partyCount, monsters, monsterCount, &diag);
        } else if (rule.fallenPawnCue) {
            MatchFallenGuard(rule, party, partyCount, monsters, monsterCount, &diag);
        } else if (proactiveCaller) {
            for (int m = 0; m < monsterCount; ++m) {
                const TacticalMonsterActor& evidence = monsters[m];
                if (!evidence.body || !ExactKind(evidence.kind, rule.monsterKind)
                    || !EvidenceAction(evidence.act, rule))
                    continue;
                ++diag.evidenceCandidates;
                if (!diag.firstEvidenceBody) {
                    diag.firstEvidenceBody = evidence.body;
                    diag.firstEvidenceAct = evidence.act;
                }
            }

            if (diag.evidenceCandidates > 0) {
                float bestDist = -1.0f;
                int bestTargetIdx = -1;
                uintptr_t bestCallerBody = 0;
                const char* bestCallerAct = 0;

                for (int m = 0; m < monsterCount; ++m) {
                    const TacticalMonsterActor& evidence = monsters[m];
                    if (!evidence.body || !ExactKind(evidence.kind, rule.monsterKind)
                        || !EvidenceAction(evidence.act, rule))
                        continue;

                    for (int p = 0; p < partyCount; ++p) {
                        const TacticalPartyActor& target = party[p];
                        if (!target.body) continue;
                        float distance = -1.0f;
                        if (!DistanceM(target, evidence, &distance)) {
                            ++diag.positionRejected;
                            continue;
                        }
                        if (diag.nearestDistanceM < 0.0f || distance < diag.nearestDistanceM)
                            diag.nearestDistanceM = distance;
                        if (distance > rule.maxPairDistanceM) continue;

                        if (bestDist < 0.0f || distance < bestDist) {
                            bestDist = distance;
                            bestTargetIdx = p;
                            bestCallerBody = evidence.body;
                            bestCallerAct = evidence.act;
                        }
                    }
                }

                if (bestTargetIdx >= 0) {
                    const TacticalPartyActor& target = party[bestTargetIdx];
                    diag.targetCandidates = 1;
                    diag.pairCandidates = 1;
                    diag.firstTargetSlot = target.slot;
                    diag.firstTargetBody = target.body;
                    diag.firstTargetAct = target.act;

                    diag.match.situation = rule.situation;
                    diag.match.name = rule.name;
                    diag.match.policyReason = rule.policyReason;
                    diag.match.priority = rule.priority;
                    diag.match.response = rule.response;
                    diag.match.urgency = rule.urgency;
                    diag.match.targetSlot = target.slot;
                    diag.match.targetBody = target.body;
                    diag.match.evidenceBody = bestCallerBody;
                    diag.match.targetAct = target.act;
                    diag.match.evidenceAct = bestCallerAct;
                    diag.match.pairDistanceM = bestDist;
                    diag.match.maxLeaseMs = rule.maxLeaseMs;
                    diag.match.excludeEvidenceBody = rule.excludeEvidenceBody;
                    diag.match.responderKind = rule.monsterKind;
                }
            }
        } else {
            for (int p = 0; p < partyCount; ++p) {
                const TacticalPartyActor& target = party[p];
                if (!target.body) continue;
                if (rule.arisenOnly && target.slot != 0) continue;
                if (rule.casterVocationOnly && !IsCasterVocation(target.vocation)) continue;
                if (!ExactAction(target.act, rule.targetActs, rule.targetActCount))
                    continue;
                ++diag.targetCandidates;
                if (!diag.firstTargetBody) {
                    diag.firstTargetSlot = target.slot;
                    diag.firstTargetBody = target.body;
                    diag.firstTargetAct = target.act;
                }
            }

            for (int m = 0; m < monsterCount; ++m) {
                const TacticalMonsterActor& evidence = monsters[m];
                if (!evidence.body || !ExactKind(evidence.kind, rule.monsterKind)
                    || !EvidenceAction(evidence.act, rule))
                    continue;
                ++diag.evidenceCandidates;
                if (!diag.firstEvidenceBody) {
                    diag.firstEvidenceBody = evidence.body;
                    diag.firstEvidenceAct = evidence.act;
                }
            }

            // The party actor must always be unique. For action-proved restraint,
            // many same-kind monsters may exist in the fight; exactly one may be
            // spatially correlated with the acting pawn. Literal lift additionally
            // keeps its stricter globally unique victim-action requirement.
            for (int p = 0; p < partyCount; ++p) {
                const TacticalPartyActor& target = party[p];
                if (!target.body) continue;
                if (rule.arisenOnly && target.slot != 0) continue;
                if (rule.casterVocationOnly && !IsCasterVocation(target.vocation)) continue;
                if (!ExactAction(target.act, rule.targetActs, rule.targetActCount))
                    continue;

                for (int m = 0; m < monsterCount; ++m) {
                    const TacticalMonsterActor& evidence = monsters[m];
                    if (!evidence.body || !ExactKind(evidence.kind, rule.monsterKind)
                        || !EvidenceAction(evidence.act, rule))
                        continue;

                    float distance = -1.0f;
                    if (!DistanceM(target, evidence, &distance)) {
                        ++diag.positionRejected;
                        continue;
                    }
                    if (diag.nearestDistanceM < 0.0f || distance < diag.nearestDistanceM)
                        diag.nearestDistanceM = distance;
                    if (distance > rule.maxPairDistanceM) continue;
                    ++diag.pairCandidates;

                    const bool evidenceIdentityAllowed =
                        !rule.requireGloballyUniqueEvidence
                        || diag.evidenceCandidates == 1;
                    // 85.25: раньше здесь заполнялась ПОСЛЕДНЯЯ подходящая пара
                    // (перезапись в цикле). Теперь — ближайшая: это осмысленно и
                    // для строгих правил (там пара ровно одна), и обязательно для
                    // запасного выбора, где именно ближайший и должен побеждать.
                    const bool nearer = (diag.match.situation
                                            == TACTICAL_SITUATION_NONE)
                                     || (distance < diag.match.pairDistanceM);
                    if (diag.targetCandidates == 1 && evidenceIdentityAllowed && nearer) {
                        diag.match.situation = rule.situation;
                        diag.match.name = rule.name;
                        diag.match.policyReason = rule.policyReason;
                        diag.match.priority = rule.priority;
                        diag.match.response = rule.response;
                        diag.match.urgency = rule.urgency;
                        diag.match.targetSlot = target.slot;
                        diag.match.targetBody = target.body;
                        diag.match.evidenceBody = evidence.body;
                        diag.match.targetAct = target.act;
                        diag.match.evidenceAct = evidence.act;
                        diag.match.pairDistanceM = distance;
                        diag.match.maxLeaseMs = rule.maxLeaseMs;
                        diag.match.excludeEvidenceBody = rule.excludeEvidenceBody;
                        diag.match.responderKind = rule.monsterKind;
                    }
                }
            }
        }

        // 85.29/85.30: причина нужна одна, а правил у каждого механизма
        // четыре (по видам монстров). Держим диагностику того правила, которое
        // дошло дальше всех: состоявшееся событие важнее отказа, а отказ вида,
        // который в бою РЕАЛЬНО есть, важнее отказа вида, которого нет вовсе.
        if (rule.fallenPawnCue
            || rule.situation == TACTICAL_SITUATION_PAWN_FINISH) {
            const bool isFinish =
                rule.situation == TACTICAL_SITUATION_PAWN_FINISH;
            const TacticalFallenDiag& f = isFinish ? diag.finish : diag.fallen;
            TacticalFallenDiag& carry = isFinish ? finishCarry : fallenCarry;
            bool& have = isFinish ? finishHave : fallenHave;
            // Приоритет: состоявшееся событие (reason == 0) важнее отказа, а
            // из отказов важнее тот вид, который в бою РЕАЛЬНО есть.
            const bool thisClean = (f.reason == 0);
            const bool carryClean = have && carry.reason == 0;
            const bool better = (thisClean && !carryClean)
                             || (!thisClean && !carryClean
                                 && f.monstersOfKind > carry.monstersOfKind);
            if (!have || better) {
                carry = f;
                have = true;
            }
        }

        // A holder action alone is not a diagnostic. Wolf GrabStart must not
        // hide the goblin row when no uEm0200 is present (log 23 PARTIAL).
        if (diag.targetCandidates > 0 && diag.evidenceCandidates > 0
            && rule.priority > bestDiagnosticPriority) {
            bestDiagnostic = diag;
            bestDiagnosticPriority = rule.priority;
        }

        const bool evidenceIdentityAllowed =
            !rule.requireGloballyUniqueEvidence || diag.evidenceCandidates == 1;
        // 85.25: строгое «ровно одна пара» остаётся законом для хватов и
        // прижимов — там уникальность ДОКАЗЫВАЕТ, кого именно держат. Для
        // семейства «услышал каст» (nearestPairFallback) пара может быть и не
        // одна: берём ближайшую, потому что доказывать тут нечего.
        const bool pairsAdmissible =
            (diag.pairCandidates == 1)
            // 85.28: у встречи «несколько монстров у тела» — норма, а не
            // неоднозначность: уникальность пары доказывала бы контакт, а здесь
            // доказывать нечего. Стоит отдельным условием, а не через
            // nearestPairFallback: тот висит на ключе chantNearest, и выключение
            // каста не должно ломать встречу.
            || (rule.fallenPawnCue && diag.pairCandidates > 1)
            // 85.30: у добивания «окружающие монстры» — это и есть суть: их
            // всегда больше одного.
            || (rule.situation == TACTICAL_SITUATION_PAWN_FINISH
                && diag.pairCandidates > 1)
            || (rule.nearestPairFallback && s_nearestPairFallback
                && diag.pairCandidates > 1);
        const bool correlatedPair = diag.targetCandidates == 1
                                 && evidenceIdentityAllowed
                                 && pairsAdmissible
                                 && diag.match.situation
                                    != TACTICAL_SITUATION_NONE;
        if (correlatedPair && rule.priority > bestMatchPriority) {
            diag.match.pairsConsidered = diag.pairCandidates;
            bestMatchScan = diag;
            bestMatchPriority = rule.priority;
        }
    }

    if (bestMatchPriority >= 0) {
        *out = bestMatchScan;
        out->matched = true;
    } else if (bestDiagnosticPriority >= 0) {
        *out = bestDiagnostic;
        out->matched = false;
    }
    out->fallen = fallenCarry;
    out->finish = finishCarry;
}

// 85.25: сеттер живёт в пространстве MonsterAI, а состояние — в анонимном:
// так ключ виден из Директора, а выключатель остаётся локальным для файла.
void SetNearestPairFallback(bool on) { s_nearestPairFallback = on; }

// 85.28: сеттер живёт рядом с chant-овским по той же причине — ключ читает
// Директор, состояние остаётся локальным для файла.
void SetPawnFinishEnabled(bool on) { s_pawnFinishEnabled = on; }

void SetFallenGuardRadius(float meters)
{
    if (!(meters == meters) || meters < 0.0f) meters = 10.0f;   // NaN из ini
    s_fallenGuardRadius = meters;
}

} // namespace MonsterAI
