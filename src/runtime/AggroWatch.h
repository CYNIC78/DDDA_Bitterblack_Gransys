#pragma once
/**
 * Runtime::Aggro — прибор «на кого смотрит пачка».
 *
 * Observer path reads only. Historical manual PIN/FOCUS research controls
 * can write, but are gated by explicit [aggro] watch and are never used by
 * Monster Director's quiet observer demand.
 *
 * ЗАЧЕМ. У режиссёра монстров есть только темп — это физика. Чтобы дать
 * ему тактический рычаг (вектор атаки пачки), надо сначала научиться
 * ЧИТАТЬ, кого особь держит целью. Сейчас мы этого не умеем вообще:
 * у пешки цель есть (`uCmc+0x2EB8`), у врага — ничего.
 *
 * ГЛАВНАЯ ИДЕЯ ПРИБОРА. В теле врага несколько слотов ссылаются на членов
 * партии, и это НЕ все цели: часть — постоянные связи (мир, ближайший
 * актёр, владелец сцены). Известный пример из FIX_RULES §6 — гоблин:
 *
 *      слот +0x2B98:  CALM -> uPlayer      AGGRO -> uCmc
 *
 * Отличить связь от цели по одному кадру нельзя. Поэтому прибор не гадает
 * и не хардкодит оффсет, а измеряет ДВИЖЕНИЕ:
 *
 *      цель — это слот, который МЕНЯЕТ члена партии.
 *      постоянная связь — слот, который стоит.
 *
 * Слот с наибольшим числом смен и есть кандидат в цель. Оффсет мы не
 * назначаем, а получаем как результат замера — и печатаем его (FIX_RULES
 * §6 п.4: фактическое смещение печатать в лог).
 *
 * ЦЕНА. Полный обход тела — только при первом появлении особи и раз в
 * kRediscoverMs. В обычном тике перечитываются ТОЛЬКО найденные слоты:
 * несколько четырёхбайтовых чтений на особь. Полный census в активном бою
 * запрещён (FIX_RULES §5, урок 1) — здесь его и нет: обход ограничен
 * телом уже известного врага.
 *
 * ЧТО С НИМ ДЕЛАТЬ. Замер Stamina Hammer по docs/AGGRO_RECON.md §7:
 * MarkEvent("hammer ON") -> бой -> MarkEvent("hammer OFF") -> бой.
 * Сравнить доли членов партии в подвижном слоте. Если статический вес
 * существует, доля носителя молота вырастет.
 */

#include <stdint.h>

namespace Runtime {
namespace Aggro {

enum {
    kMaxRows     = 32,  // особей под наблюдением
    kMaxSlots    = 12,  // target slots + forced goblin-family roster (4 cards)
    kMaxParty    = 4,   // Аризен + до трёх пешек
    kMemberSlots = 5    // + графа «неразрешённая пешка», см. MEMBER_OTHERPAWN
};

// Кто именно лежит в слоте.
enum Member {
    MEMBER_NONE      = -1,
    MEMBER_ARISEN    = 0,
    MEMBER_MAIN      = 1,
    MEMBER_HIRED1    = 2,
    MEMBER_HIRED2    = 3,

    // СЛЕПОЕ ПЯТНО, ОБЪЯВЛЕННОЕ ВСЛУХ.
    //
    // Разбор партии сверяется с записями персонажей и сам просит
    // пересканировать при найме, но у него есть штатная капитуляция:
    // «records show N pawns, the body scan finds M - giving up». Пока
    // тело наёмной пешки не найдено, указатель на неё для нас просто
    // «не член партии» — и переход цели на эту пешку выглядел бы как
    // «цель потеряна». Это ровно тот сорт чистого на вид вранья, за
    // которым мы охотимся весь проект.
    //
    // Поэтому такой указатель опознаётся по имени класса (uCmc/uPl*) и
    // получает отдельную графу. Прибор обязан показывать свой предел, а
    // не выдавать красивые неверные числа.
    MEMBER_OTHERPAWN = 4
};

// Замер 76.1 показал, что слоты бывают трёх сортов, и мешать их в одну
// кучу нельзя: счётчик доли, суммировавший всё подряд, дал Аризену 21510
// против 5000 у остальных, тогда как честный расчёт по времени удержания
// даёт почти поровну. Врал именно статичный ростер.
enum SlotKind {
    SLOT_TARGET = 0,   // подвижный слот — кандидат в цель
    SLOT_ROSTER = 1    // запись массива известных актёров (см. Roster*)
};

struct Slot {
    uint32_t off;        // смещение в теле врага, где нашли ссылку
    int      member;     // кто там сейчас (Member)
    uint32_t switches;   // сколько раз слот сменил члена партии
    uint32_t holdMs;     // сколько держится текущий
    uint32_t sinceMs;    // отметка времени последней смены
    int      kind;       // SlotKind
};

// CARDWATCH (79.0, §18 AGGRO_RECON): помнит, что напечатали про карточку,
// чтобы лить строки только при ИЗМЕНЕНИИ, а не каждый тик. Двенадцать
// кандидатов в поля карточки — см. kCardFields в .cpp (замер 78.0).
struct CardWatchState {
    uint32_t lastVal[12];
    float    lastDist;
    uint32_t lastMs;
    int      lastMember;
    bool     have;
};

struct Row {
    uintptr_t body;
    char      kind[16];      // uEm0100, ...
    char      act[40];       // текущее действие по DTI
    int       nSlots;
    Slot      slot[kMaxSlots];
    int       best;          // индекс самого подвижного слота, -1 если все стоят
    uint32_t  lastScanMs;

    // МАССИВ КАРТОЧЕК ИЗВЕСТНЫХ АКТЁРОВ (замер 76.1, §15.2 AGGRO_RECON).
    //
    //     база +0x2FA0, шаг 0x28C = 652 байта, ровно 4 записи у uEm0200
    //
    // Число записей совпало с максимумом партии, а шаг подтверждён
    // арифметически. 652 байта на карточку — это не флаг, а подробная
    // запись: место есть под дистанцию, видимость, время контакта и вес.
    // Именно здесь обязан лежать статический член формулы ненависти.
    //
    // Оффсеты НЕ зашиты: и база, и шаг вычисляются из найденных слотов
    // как арифметическая прогрессия. У другого вида они будут другими.
    uint32_t  rosterBase;
    uint32_t  rosterStride;
    int       rosterCount;

    // Кого эта особь держит целью сейчас (по самому липкому подвижному
    // слоту). Нужно, чтобы считать СХОЖДЕНИЕ стаи — см. Converge*.
    int       targetMember;

    // CARDWATCH (79.0): состояние непрерывного слежения за карточками
    // ЭТОЙ особи. См. CardWatchState и комментарий в .cpp (§18).
    CardWatchState card[kMaxParty];
};

// СХОЖДЕНИЕ СТАИ (заявка тестера 21.08).
//
// «Если волк повалит пешку или игрока, ближайшие присоединяются, включается
// QTE. Насчитывал пять-шесть особей на лежачем.»
//
// Это ломает вывод §15.4 («стая не догпайлит»): равномерные 25/24/23/20%
// были замерены на СТОЯЧЕЙ партии. Схождение — состояние-зависимое, и на
// глаз его считать нельзя: пять волков на лежачем длятся пару секунд.
//
// Прибор считает сам: сколько особей одновременно смотрят на одного члена
// партии, и какое действие у самого этого члена в тот момент. Тогда связка
// «повалили -> сбежались» становится числом, а не впечатлением.
struct Converge {
    int      member;        // на кого сошлись
    int      count;         // сколько особей
    int      peak;          // рекорд с последнего MARK
    char     memberAct[40]; // действие ЖЕРТВЫ: тут и ждём Down/Hold/Hikizuri

    // Эпизод схождения: пока число особей >= порога, это ОДИН эпизод.
    // 79.0 печатает CONVHOLD каждые 750 мс внутри эпизода и CONVENDED
    // в конце — так догпайл становится таймлайном, а не одиночкой.
    int      episodePeak;   // максимум за ТЕКУЩИЙ эпизод
    uint32_t sinceMs;       // когда эпизод начался (0 = нет эпизода)
    uint32_t lastHoldMs;    // когда печатали последний CONVHOLD
};

void Init();
void Shutdown();

// Зовётся из общего продуктового тика под своим SEH.
void Tick();

// CARDRECON (84.16, универсальный в 84.18): временной дифф карточек
// ЛЮБОГО вида врага. Массив карточек (база/шаг) находит сам — тем же
// методом, которым найден волчий (DiscoverSlots + ClassifySlots);
// refmatch-строка, если раскладка совпала с волчьим референсом.
// Только читает; работает при выключенном Director. docs/GOBLIN_CARD_DIFF_OBSERVE.md.
void CardReconTick();
// Полный дамп карточек отслеженных тел (MARK / snapshot to log).
void CardReconDump();

// Goblin live card mode (84.17, лог 24): fC=4 — восприятие (потолок 300),
// fC=5 — боевой режим (потолок 484, наблюдаемый нативный максимум).
// Флаг +0x08 у гоблина — (константа_карты) | младший_байт, поэтому гейт
// по младшему биту, а не равенству 1 (лог: f8=1149272065 и т.п.).
// Открыто для фикстюр-теста.
bool LiveGoblinCardMode(uint32_t flag, uint32_t mode,
                        float* pinCeiling, float* maxNative);

bool Enabled();
void SetEnabled(bool on);

// Product observer lease. It keeps native target rows current even when the
// verbose research watch is off. The lease alone remains read-only; the
// separate explicit Director target lease is below.
void SetObserverDemand(bool on);
bool ObserverDemanded();

// Deterministic record-slot -> live-body bridge. Pawn labels come from the
// confirmed character-record index (0 Main, 1 Hired1, 2 Hired2), never from
// PawnBodyAt() order. Unresolved means false; callers must not guess.
bool ResolveMemberBody(int member, uintptr_t* bodyOut);
// Same bridge with a stable, slot-specific diagnostic string. Returns one of
// identity-<slot>-exact / record-unavailable / body-unresolved-or-duplicate.
// This is for automatic Director logs; it performs no writes.
const char* ResolveMemberBodyStatus(int member, uintptr_t* bodyOut);

// Product response lease for Monster Director. exactKind is a full DTI name
// (uEm0200 pack, uEm0100 grab, or uEm0101 hob pack/grab). ALERT reapplies only the validated
// attention pin: free same-kind bodies (not the restrained body, and not a
// wolf already in combat with someone else) turn toward the acting pawn
// without fake-hit attack pressure. A wolf chewing another member is left
// occupying them. ALARM uses the already tested pin+suppress+fakehit bundle
// on free wolves and on wolves already fighting the mark. Goblins never get
// that ALARM pack; their only write is ALERT pin-only. The caller supplies
// the exact fixed-slot body; Aggro re-resolves it here and before every write.
// member < 0 releases. The optional exact restrained body receives no Aggro
// mutation. Manual research PIN stays uEm0200-only.
enum DirectorResponse {
    DIRECTOR_RESPONSE_NONE = 0,
    DIRECTOR_RESPONSE_ALERT = 1,
    DIRECTOR_RESPONSE_ALARM = 2
};
bool     DirectorFocusSet(int member, uintptr_t expectedBody,
                          uintptr_t excludedEnemyBody = 0,
                          int response = DIRECTOR_RESPONSE_ALARM,
                          const char* exactKind = "uEm0200");
int      DirectorFocusMember();
int      DirectorResponseLevel();
uint32_t DirectorWriteCount();

// Текущее схождение по каждому члену партии (индекс = Member).
const Converge* ConvergeAt(int member);

int         RowCount();
const Row*  RowAt(int i);
const char* Status();

// Читаемое имя члена партии. Всегда ASCII (правило шрифта ImGui 1.48).
const char* MemberName(int member);

// Отметка момента в логе: «надел молот», «бахнул в щит». Без неё лог
// замера невозможно разрезать на A и B.
void MarkEvent(const char* tag);

// Строка на особь в лог. Ручная кнопка, не автоматика.
void DumpSnapshot();

// ЭТАП 2 БЕЗ МОЛОТА. Выгружает карточки известных актёров и печатает
// ТОЛЬКО те поля, что различаются между членами партии: одинаковые у
// всех к конкретному члену не относятся, различающиеся — покарточные
// величины (дистанция, свежий урон, вес). Это способ найти формулу, не
// дожидаясь десятого уровня и Stamina Hammer.
void DumpRoster();

// CARDWATCH (79.0). Непрерывное слежение за карточками двух особей:
// кандидаты в поля карточки + живая дистанция до члена карточки в ОДНОЙ
// строке. Ручные ростеры (78.0) не развели «дистанция» и «ненависть»:
// между двумя нажатиями шёл бой. Здесь строка сама несёт и то, и другое —
// корреляция видна без экспериментатора у кнопки. Печатает только
// изменения (дельта + сердечник 4 c), полный census не делает.
bool CardWatchOn();
void SetCardWatch(bool on);
int  CardWatchCount(); // сколько особей сейчас под слежением (0..2)

// PIN (80.0, AGGRO_RECON §20) — ПЕРВАЯ МУТАЦИЯ трека.
//
// «Штырь внимания» на члене партии: каждый тик по карточкам выбранного
// члена (только uEm0200 — единственный вид с верифицированной картой)
// перезаписываем поле внимания +0x10 на 300.0 — нативное значение линии
// 300-10d при дистанции 0 м. Движок сам затухает поле (9/тик по замеру),
// поэтому штырь — не патч, а ПЕРЕЗАЯВКА (паттерн WandRange): пишем,
// проверяем readback, не сошлось — откатываем прочитанное.
//
// СБРОС ВСТРОЕН В САМУ МЕХАНИКУ: снятие штыря = просто перестать
// перезаписывать. Собственное затухание движка вернёт поле к нулю за
// несколько секунд. Постоянных состояний нет, .sav не трогаем.
//
// member < 0 — снять. scope: 0 = ближайший к члену волк, 1 = все.
void PinSet(int member, int scope);
int  PinMember(); // текущий заштыренный член (Member) или MEMBER_NONE
int  PinScope();
void PinStats(uint32_t* writesOut, uint32_t* rollbacksOut);

// 81.0: «подавление остальных». Замер 80.0 (§21): штырь работает на
// пак-уровне (доля цели 0.8% -> 24%), но в толпе все карты лежат возле
// геометрической линии (200-280), и +30 над остальными не перебивает
// аргмакс у отдельной особи. Режим-вектор: заштыренному 300, всем
// остальным живым картам той же особи — 0 (нативное «затухшее» значение).
// Тогда аргмакс становится чистым. off по умолчанию.
void PinSuppressSet(bool on);
bool PinSuppressOn();

// 82.0: «фейк-хит». Замер 81.0 (§23): в плотной мурле движок сам пишет
// в +0x10 — система восприятия сбрасывает карту члена на линию 300-10d
// при каждом «повторном увиденнии», и штырь/подавление оказываются в
// войне записей (наши 300 и 0 перебиваются линией). Блок B («свежий
// урон»: 274 флаг, 27c значение) восприниманием НЕ сбрасывается — он
// ставится только по удару и затухает. Значит, пере-заявка
// 274=1 + 27c=value на карточке заштыренного должна прилипнуть даже
// в мурле, если функция цели этот блок читает. off по умолчанию.
void PinFakehitSet(bool on);
bool PinFakehitOn();

// 83.0: FOCUS — продуктовая операция рычага одной кнопкой:
// штырь + подавление + фейк-хит на выбранном члене (scope — текущий).
// Замер 82.0 (§25): связка даёт 75.6% доли прицеливания и догпайл
// на 8 особей. Ручные переключатели (pin/suppress/fakehit) остаются
// для A/B-исследований; любое ручное изменение отключает FOCUS
// (он не должен молчать о том, что больше не является правдой).
void PinFocusSet(int member); // member < 0 — release all (всё выключить)
int  PinFocusMember();        // зафокусированный член или MEMBER_NONE

} // namespace Aggro
} // namespace Runtime
