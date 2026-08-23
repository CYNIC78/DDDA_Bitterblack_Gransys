#pragma once
/**
 * Runtime::Tempo — вариативность темпа передвижения монстров.
 *
 * ЗАЧЕМ. Игрок побеждает гоблина не потому, что тот слаб, а потому что за
 * десять боёв выучил его ритм: за сколько тот добежит, когда замахнётся.
 * Мы не трогаем ни урон, ни HP — мы ломаем предсказуемость. Каждый монстр
 * получает свой множитель скорости передвижения, поэтому в одной стае
 * гоблины подходят вразнобой и дистанцию выучить нельзя.
 *
 * КАК. Движок применяет покадровое смещение к координатам тела
 * (`+0x40/+0x44/+0x48`). Мы перехватываем момент ДО применения и умножаем
 * горизонтальные составляющие. Анимация при этом не меняется — меняется
 * только скорость перемещения тушки.
 *
 * ВАЖНО ПРО ДВА ПУТИ. Основной хук (смещения на стеке) — это общая
 * локомоция: через него проходят dash/track, run и walk. Отдельная
 * sprint-сигнатура (смещения в регистрах) остаётся самостоятельным путём,
 * но её попадание не требуется для доказательства общей локомоции.
 *
 * ВТОРОЙ КАНАЛ. Темп атак меняется отдельно через ряд множителей
 * воспроизведения анимации. Обычная мутация сохраняет два независимых
 * индивидуальных коэффициента. Director не умножает их повторно: одна
 * ограниченная оболочка ведёт точное тело от стабильной пары L0/A0 к его
 * неизменным личным rage-endpoint L1/A1 и затем отпускает обратно.
 *
 * ГРАНИЦЫ. Передвижение жёстко зажато в 0.75…1.30, а анимация — в
 * 0.70…1.40 независимо от ini. Итоговая композиция всегда повторно
 * проходит species-safe clamp.
 */

#include <stdint.h>

namespace Runtime {
namespace Tempo {

// Поднимает хуки. Вызывается из Runtime::Init(). Если сигнатуры не нашлись
// или нашлись неоднозначно — модуль молча остаётся выключенным, игра
// работает как ванильная.
void Init();

// Снимает все правки. Вызывается из Runtime::Shutdown().
void Shutdown();

// Живое управление общей локомоцией. Хуки остаются установленными как
// безопасный no-op при пустой таблице, поэтому OFF можно переключить в ON
// без перезапуска и после старта с сохранённым выключенным состоянием.
void SetEnabled(bool on);

// Пересчёт таблицы «тело → множитель». Зовётся продуктовым тиком раз в
// 150 мс: хук читает готовую таблицу и не делает ни одного резолва.
void RefreshTable();

// --- состояние для UI -----------------------------------------------------
struct Status {
    bool     enabled;        // включён ли режим в конфиге
    bool     walkHooked;     // legacy name: общий dash/run/walk хук установлен
    bool     sprintHooked;   // отдельный хук спринта установлен
    int      walkMatches;    // сколько мест подошло под общую сигнатуру
    int      sprintMatches;
    int      tracked;        // сколько монстров сейчас в таблице
    float    minFactor;      // фактический разброс в текущей таблице
    float    maxFactor;
    // --- вторая ручка: темп анимации ---
    bool     animEnabled;
    bool     animAttacksOnly;   // область: только атаки или весь набор
    int      animTracked;
    float    animMin;
    float    animMax;
    // Измерение вместо догадки: сколько раз движок перебил нашу запись
    // и сколько раз писали мы. Ноль в первом — движок эти поля не трогает.
    uint32_t animEngineWrites;
    uint32_t animOurWrites;
    // Сколько раз дорожка тела из переопределения снималась с возвратом
    // исходных значений. Считается молча после пятого раза, поэтому
    // число должно быть видно в панели, а не только в логе.
    uint32_t animRestores;
    float    animCoupling;      // 0 = ручки независимы, 1 = темп следует за скоростью
};
Status GetStatus();

// Build 012 mobilization readiness gate. All four facts are reported separately
// so the Director can log the exact fail-closed reason. Director admission only
// permits attack-scoped animation mutation; `everything` is rejected.
struct DirectorReadiness {
    bool movementEnabled;
    bool generalHookInstalled;
    bool animationEnabled;
    bool attacksOnly;
};
DirectorReadiness GetDirectorReadiness();
bool DirectorReady(const char** reasonOut);

// One non-stacking mobilization envelope per exact Director responder.
// `stable*` is the immutable deterministic baseline assigned to this body;
// `rage*` is its personal exact-kind endpoint. Repeated commands refresh the
// same row and maximize `level`; a transitional live factor is never sampled.
struct DirectorMobilizationReceipt {
    uintptr_t body;
    float urgency;
    float level;
    float stableLoco;
    float stableAnim;
    float rageLoco;
    float rageAnim;
    float effectiveLoco;
    float effectiveAnim;
    bool holding;
    bool decaying;
};

bool AdmitDirectorMobilization(uintptr_t body, const char* exactKind,
                               float urgency, uint32_t ttlMs,
                               DirectorMobilizationReceipt* receipt,
                               const char** reasonOut);
void ReleaseDirectorMobilization(uintptr_t body);       // ordinary decay
void HardResetDirectorMobilization(uintptr_t body);     // unsafe immediate reset
void HardResetAllDirectorMobilization();                // disable/shutdown/rollback
int  DirectorMobilizationCount();

// Живая настройка диапазона из UI: применяется со следующим обновлением
// таблицы, то есть в пределах 150 мс. Значения зажимаются в 0.75…1.30.
void  SetRange(float lo, float hi);
void  GetRange(float* lo, float* hi);

// --- темп анимации ---------------------------------------------------------
// Вторая ручка, независимая от передвижения. Найдена дифом по торпору:
// в теле лежит ряд из пяти множителей скорости воспроизведения
// (+0x0EE4…+0x0EF4), в норме 1.0, под торпором 0.5.
//
// Пишем МУЛЬТИПЛИКАТИВНО: читаем значение движка и умножаем на свой
// коэффициент. Поэтому торпор, захват и прочие механики продолжают
// работать — мы меняем контекст, а не отменяем чужие решения.
//
// AnimTick зовётся каждый кадр из WorldScan_Tick: движок переписывает
// эти поля сам, и запись раз в 150 мс жила бы один кадр из девяти.
void  AnimTick();
void  SetAnimRange(float lo, float hi);
void  GetAnimRange(float* lo, float* hi);
void  SetAnimEnabled(bool on);
bool  GetAnimEnabled();

// Область применения. Множитель воспроизведения глобален для существа:
// он ускоряет и ходьбу, и повороты, и атаки. attacksOnly = true применяет
// его только пока текущее действие — атака, и тогда локомоция остаётся
// ванильной.
void  SetAnimAttacksOnly(bool on);
void  AnimResetCounters();

// Связка характера: в какой мере темп атаки следует за скоростью
// передвижения этой же особи. 0 — независимо (четыре характера),
// 1 — кто быстро бегает, тот быстро и бьёт (цельное существо).
void  SetAnimCoupling(float v);
float GetAnimCoupling();

// Кто сейчас на учёте и с каким множителем. Нужен, чтобы перед боем
// видеть раскладку, а не гадать, почему «взбесился только один».
int   AnimListCount();
bool  AnimListAt(int i, uintptr_t* body, float* factor, char* kindOut, int cap);

// Цена работы в микросекундах на кадр. Измерение, а не обещание.
void  AnimCost(uint32_t* lastUs, uint32_t* avgUs, uint32_t* maxUs);

// --- наблюдение за спринтом -------------------------------------------------
// Через хук спринта проходит ЛЮБОЙ спринтующий: игрок, пешка, монстр.
// Считаем, чьи тела там появляются. Нужно, чтобы проверить наблюдение
// «пешки в бою не спринтят вовсе» замером, а не впечатлением.
struct SprintStats { uint32_t player, pawn, enemy, other; };
void        SprintWatchTick();       // зовётся каждый кадр из WorldScan_Tick
SprintStats GetSprintStats();
void        ResetSprintStats();

// Одна строка агрегатов в лог. Первый GENERAL(dash/run/walk) hit и первый
// необязательный SPRINT hit печатаются автоматически продуктовым тиком.
void        DumpLocomotionDiagnostics(const char* reason);

// ============================================================================
// ШОВ ДЛЯ КОНТРОЛЛЕРА МУТАЦИЙ
// ============================================================================
//
// Этот модуль — ПРИМИТИВ. Он держит у особи два множителя — передвижения
// и темпа анимации — и две независимые ограниченные надстройки. Универсальный
// override остаётся мультипликативным. Director использует отдельную
// stable→personal-rage оболочку, но не знает тактику, цель или фазы боя.
//
// Базовый разброс от адреса тела — это «мутация по умолчанию», чтобы стая
// не была одинаковой. Универсальное поведение сверху приходит отсюда:
//
//     Tempo::SetOverride(body, 1.0f, 1.35f, 6000);   // взбесился на 6 секунд
//     Tempo::SetOverride(body, 0.8f, 0.8f, 0);       // ранен, бессрочно
//     Tempo::ClearOverride(body);                     // отпустило
//
// Множители СКЛАДЫВАЮТСЯ УМНОЖЕНИЕМ: итог = база × переопределение, затем
// жёсткий зажим (0.75…1.30 для передвижения, 0.70…1.40 для анимации).
// Контроллер говорит «этот вдвое злее обычного», не зная и не ломая
// базовый разброс — и не может вывести систему за безопасные пределы.
//
// ttlMs = 0 означает «до явной отмены». Просрочки снимаются на обновлении
// таблицы, то есть в пределах 150 мс.
// Returns false only when body is invalid or the bounded override table is full.
// Existing entries update in place. Controllers can therefore fail closed
// instead of assuming that a synchronized policy was installed.
bool  SetOverride(uintptr_t body, float loco, float atk, uint32_t ttlMs);

// --- ряд множителей анимации у НЕ-монстра ------------------------------------
//
// ЗАЧЕМ. Ускорение пешки (`[pawnHaste]`) двигает тушку, не трогая
// анимацию, — отсюда проскальзывание стоп. У монстров же обе ручки есть:
// ряд множителей воспроизведения `+0x0EE4…+0x0EF4` найден и работает.
// Вопрос тестера ровно тот, который стоит задать: а у тела пешки этот ряд
// есть? Если есть — «подделка» превращается в честный быстрый бег.
//
// Проверка ТОЛЬКО ЧИТАЕТ и печатает вердикт один раз на метку:
//   значения всех пяти полей, покрывает ли размер класса ряд, похожи ли
//   числа на множители и есть ли ровно 1.0 (так выглядит покой).
// Ничего не пишет и ничего не включает.
bool AnimRowProbe(uintptr_t body, const char* label);

// Разрешить ряду анимации применяться к телам, у которых стоит активное
// переопределение, даже если это не монстр (пешка). Гейт для таких тел
// строже: все пять полей должны быть ровно 1.0. Отдельно от `animEnabled`,
// потому что это другой потребитель и другой риск.
//
// При снятии переопределения исходные значения ВОЗВРАЩАЮТСЯ: пешка не
// должна остаться с ускоренной анимацией после того, как рывок кончился.
void  SetAnimForOverrides(bool on);
bool  GetAnimForOverrides();

// Наблюдение за рядом у одного тела: пять чисел рядом с текущим
// действием, по изменению, N миллисекунд. Только чтение.
//
// Нужно, потому что первое поле ряда оказалось ЖИВЫМ: у стоящей пешки
// 1.000, у бегущей 1.060. Какое поле за что отвечает — вопрос к
// наблюдению, а не к рассуждению.
void  AnimRowWatchStart(uintptr_t body, uint32_t ms);
bool  AnimRowWatchActive();
void  ClearOverride(uintptr_t body);
void  ClearAllOverrides();
int   OverrideCount();

// Итоговые множители особи после композиции. Контроллеру — чтобы решать,
// UI — чтобы показывать.
bool  GetFactors(uintptr_t body, float* loco, float* atk);

} // namespace Tempo
} // namespace Runtime
