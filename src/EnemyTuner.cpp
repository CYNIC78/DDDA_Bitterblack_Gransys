#include "stdafx.h"
#include "runtime/Runtime.h"
#include "runtime/MemProbe.h"
#include "EnemyTuner.h"
#include "EntityConfig.h"
#include "monsterai/SpeciesCard.h"

/**
 * Первый шаг применения конфига: РАЗВЕДКА, а не запись.
 *
 * Почему не пишем сразу. Значения зрения (1500 / 60 / 3000) лежат в файле
 * em0100A.sn2. Куда движок кладёт их в памяти — мы ещё НЕ знаем: в дампах
 * cAICtrl этих чисел нет (там 4.0, 3.5, 147.7). Значит они либо глубже 384
 * байт, либо в отдельном объекте сенсора, либо вообще в общей таблице на
 * весь вид, а не на особь.
 *
 * Писать наугад в чужую память нельзя. Поэтому этот модуль сначала ИЩЕТ
 * известные числа и докладывает, где они лежат. Как только адрес подтверждён
 * дампом — включаем запись отдельным флагом.
 *
 * Скорость (cMotionCtrl, темп 1.0..1.5) — та же история: тип известен,
 * живой адрес ещё нет.
 */

namespace EnemyTuner {

static int  s_tracked = 0;
static int  s_writes  = 0;
static char s_status[192] = "idle";

// ---------------------------------------------------------------- helpers ---
static bool SafeRead(const void* src, void* dst, size_t n)
{
    if (!src) return false;
    __try {
        memcpy(dst, src, n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Похоже на указатель в кучу игры (диапазон из FIELD_MAP: 0x10000000..0x18000000
// плюс верхние банды, которые встречались в дампах: 0x49.., 0x4F.., 0x50..).
// ВАЖНО: диапазон 0x40000000..0x60000000 БЫЛ ошибкой. В нём живут обычные
// float: 0x42C80000 = 100.0, 0x447A0000 = 1000.0. Из-за этого сканер принял
// число 100.0 за указатель и выдал 9 ложных "OBJ ... -> 0x42c80000".
// Настоящие кучи в дампах: 0x10xxxxxx..0x11xxxxxx (тела/акты) и
// 0x49xxxxxx..0x50xxxxxx (ресурсы: rShlParamList 0x49DC0720, rStatusParam
// 0x4FF05E70, cGroupParam 0x50AC1920). Сужаем и требуем выравнивания на 4.
static bool LooksHeap(uint32_t v)
{
    if (v & 3u) return false;                       // указатели выровнены
    if (v >= 0x10000000u && v < 0x18000000u) return true;
    if (v >= 0x49000000u && v < 0x52000000u) return true;
    return false;
}

// Значения зрения гоблина из em0100A.sn2 — то, что ищем в памяти.
struct KnownFloat { float v; const char* what; };
static const KnownFloat kGoblinSensor[] = {
    { 1500.0f, "sight/aware radius" },
    {   60.0f, "sight cone angle"   },
    { 3000.0f, "hear radius (far)"  },
    { 2000.0f, "presence radius"    },
    {  150.0f, "melee zone"         },
};
static const int kNSensor = sizeof(kGoblinSensor) / sizeof(kGoblinSensor[0]);

static bool NearlyEq(float a, float b)
{
    float d = a - b;
    if (d < 0) d = -d;
    return d < 0.01f;
}

// ------------------------------------------------------------- TargetBody ---
// Кого исследуем/правим по кнопке.
//
// ЭТО НЕ МЕЛОЧЬ. Раньше все кнопки звали FirstEnemyBody() = первый uEm*
// в списке. У лагеря это стабильно uEm8000 (их шесть), а гоблин uEm0100
// лежит следующим. То есть вся "разведка гоблина" могла сниматься с зайца.
// Теперь цель явная: сначала гоблин, и в лог всегда пишется, кто выбран.
static uintptr_t TargetBody(const char* what, const char** kindOut)
{
    const char* kind = "uEm0100";
    uintptr_t body = Runtime::FirstBodyOfKind("uEm0100");
    if (!body) {
        body = Runtime::EnemyBodyAt(0, &kind);
        if (body)
            logFile << "EnemyTuner: " << what << ": no goblin, using "
                    << (kind ? kind : "?") << std::endl;
    }
    if (body)
        logFile << "EnemyTuner: " << what << ": target " << (kind ? kind : "?")
                << " 0x" << std::hex << body << std::dec << std::endl;
    if (kindOut) *kindOut = kind;
    return body;
}

// --------------------------------------------------------- ScanVisionParams -
// Ищем известные float в теле врага. Тело 29 632 байта — обходим целиком,
// но ТОЛЬКО по явной команде, не в тике.
void ScanVisionParams()
{
    uintptr_t body = TargetBody("ScanVision", nullptr);
    if (!body) {
        lstrcpynA(s_status, "ScanVision: no enemy body (load a save, HUNT first)", sizeof(s_status));
        logFile << "EnemyTuner: " << s_status << std::endl;
        return;
    }

    logFile << "EnemyTuner: scanning body 0x" << std::hex << body << std::dec
            << " for known sensor values" << std::endl;

    const uint32_t kBodySize = 29632;
    int found = 0;

    // 1) прямо в теле
    for (uint32_t off = 0; off + 4 <= kBodySize; off += 4) {
        float f = 0.0f;
        if (!SafeRead((const void*)(body + off), &f, 4)) continue;
        for (int k = 0; k < kNSensor; ++k) {
            if (NearlyEq(f, kGoblinSensor[k].v)) {
                logFile << "  BODY +0x" << std::hex << off << std::dec
                        << "  = " << f << "   (" << kGoblinSensor[k].what << ")"
                        << std::endl;
                ++found;
            }
        }
    }

    // 2) в объектах, на которые тело ссылается (первый уровень, 512 байт каждый)
    for (uint32_t off = 0; off + 4 <= kBodySize; off += 4) {
        uint32_t p = 0;
        if (!SafeRead((const void*)(body + off), &p, 4)) continue;
        if (!LooksHeap(p)) continue;

        char name[64] = { 0 };
        const char* nm = Runtime::Mem::NameOfLiveObjectSafe((const void*)(uintptr_t)p, name, sizeof(name));

        for (uint32_t o2 = 0; o2 + 4 <= 512; o2 += 4) {
            float f = 0.0f;
            if (!SafeRead((const void*)((uintptr_t)p + o2), &f, 4)) break;
            for (int k = 0; k < kNSensor; ++k) {
                if (NearlyEq(f, kGoblinSensor[k].v)) {
                    logFile << "  OBJ  body+0x" << std::hex << off
                            << " -> 0x" << p << " +0x" << o2 << std::dec
                            << "  = " << f
                            << "   [" << (nm ? nm : "?") << "]"
                            << "   (" << kGoblinSensor[k].what << ")"
                            << std::endl;
                    ++found;
                }
            }
        }
    }

    wsprintfA(s_status, "ScanVision: %d hit(s), see ddda_ai_overhaul.log", found);
    logFile << "EnemyTuner: " << s_status << std::endl;
}

// ---------------------------------------------------- ReadCharParamEnemy ----
// Блок cCharParamEnemy (320 B) найден в теле на +0x5870, вторая копия
// на +0x59B0. Опознан сверкой с em0100_cmn.prp: 20 из 22 полей подряд.
// Здесь читаем поля поводка и масштаб — они подтверждены и осмысленны.
//
// ВАЖНО: база подтверждена на ОДНОМ враге (uEm0100). Перед записью надо
// проверить, что она та же у других видов: sizeof тела у них разный
// (uEm0200 29888, uEm0500 29408), значит блок может лежать иначе.
// Поэтому здесь — проверка сигнатуры, а не слепое доверие оффсету.

// ============================ НАСТОЯЩИЙ МАСШТАБ ============================
// Источник: badecho.com "Hacking Dragon's Dogma Part 1/4" (omni).
// В location-структуре существа лежат множители масштаба:
//     +0x60 width, +0x64 height, +0x68 depth   — НЕУНИФОРМНЫЕ, живые.
// Автор пишет: "Changing one of these multipliers immediately updates the
// look of the character" — то есть читаются каждый кадр, не при спавне.
//
// ПОЧЕМУ ЭТО НАШЕ ТЕЛО: в его хуке координаты берутся как [eax+40]:
//     movss xmm0,[eax+40]     ; player location hook
// А в наших дампах у тела uEm0100 xyz лежат ровно на +0x40/44/48.
// Значит "location structure" из статьи == тело uEm*/uPlayer.
// Следовательно масштаб — на body+0x60/0x64/0x68.
//
// cCharParamEnemy +0x12C (スケール値) — это НЕ то поле: параметр ресурса,
// читается при создании модели. Наша запись туда откатывалась (тест 05).
static const uint32_t kScaleW = 0x60;   // ширина
static const uint32_t kScaleH = 0x64;   // высота
static const uint32_t kScaleD = 0x68;   // глубина

static const uint32_t kCharParamOff  = 0x5870;   // база блока в теле uEm0100
static const uint32_t kFldReturnActivate = 0x100; // リターンテリトリー発動タイム
static const uint32_t kFldReturnDuration = 0x104; // リターンテリトリー継続タイム
static const uint32_t kFldScale          = 0x12C; // スケール値

// Сигнатура блока.
//
// ОШИБКА, КОТОРУЮ ЗДЕСЬ ИСПРАВИЛИ: сначала в проверку попало поле +0x120
// (拘束スローレート Lv1). В файле оно 1.0, но движок ОБНУЛЯЕТ его в рантайме
// (как и +0x124). Проверка проваливалась на правильной структуре.
//
// Урок: в сигнатуру годятся только поля, которые игра НЕ трогает.
// Берём три дистанции переключения камер — они статичны и образуют
// характерную возрастающую тройку 500/800/1200, случайно такое не встретится.
static bool LooksLikeCharParam(uintptr_t base)
{
    float cam0 = 0, cam1 = 0, cam2 = 0, death = 0;
    if (!SafeRead((const void*)(base + 0x0EC), &cam0,  4)) return false;
    if (!SafeRead((const void*)(base + 0x0F4), &cam1,  4)) return false;
    if (!SafeRead((const void*)(base + 0x0FC), &cam2,  4)) return false;
    if (!SafeRead((const void*)(base + 0x110), &death, 4)) return false;
    return NearlyEq(cam0, 500.0f) && NearlyEq(cam1, 800.0f)
        && NearlyEq(cam2, 1200.0f) && NearlyEq(death, 1500.0f);
}

// Автопоиск базы: у разных видов тело разного размера (uEm0200 29888,
// uEm0500 29408), поэтому +0x5870 верен только для uEm0100. Сканируем тело
// и ищем сигнатуру. Возвращает 0, если не найдена.
static uintptr_t FindCharParam(uintptr_t body, uint32_t bodySize)
{
    for (uint32_t off = 0; off + 0x140 <= bodySize; off += 4) {
        if (LooksLikeCharParam(body + off)) return body + off;
    }
    return 0;
}

void ReadCharParam()
{
    uintptr_t body = TargetBody("CharParam", nullptr);
    if (!body) {
        lstrcpynA(s_status, "CharParam: no enemy body", sizeof(s_status));
        logFile << "EnemyTuner: " << s_status << std::endl;
        return;
    }

    // Сначала пробуем известный оффсет, затем ищем по сигнатуре.
    uintptr_t base = body + kCharParamOff;
    if (!LooksLikeCharParam(base)) {
        base = FindCharParam(body, 29632);
        if (!base) {
            lstrcpynA(s_status, "CharParam: signature not found in body", sizeof(s_status));
            logFile << "EnemyTuner: " << s_status
                    << " (body 0x" << std::hex << body << std::dec << ")" << std::endl;
            return;
        }
        logFile << "EnemyTuner: charParam found by scan at +0x"
                << std::hex << (uint32_t)(base - body) << std::dec
                << " (not the expected 0x5870)" << std::endl;
    }

    float act = 0, dur = 0, scale = 0;
    SafeRead((const void*)(base + kFldReturnActivate), &act, 4);
    SafeRead((const void*)(base + kFldReturnDuration), &dur, 4);
    SafeRead((const void*)(base + kFldScale), &scale, 4);

    char line[192];
    sprintf_s(line, "CharParam: leash activate=%.1f duration=%.1f scale=%.3f",
              act, dur, scale);
    lstrcpynA(s_status, line, sizeof(s_status));

    logFile << "EnemyTuner: body 0x" << std::hex << body
            << " charParam +0x" << kCharParamOff << std::dec << std::endl;
    logFile << "  return activate (+0x100) = " << act << "   (file: 60.0)" << std::endl;
    logFile << "  return duration (+0x104) = " << dur << "   (file: 450.0)" << std::endl;
    logFile << "  scale           (+0x12C) = " << scale << std::endl;

    // Копий структуры в теле несколько (база и рабочая). Покажем все —
    // писать надо в ту, которую движок реально читает.
    int copies = 0;
    for (uint32_t off = 0; off + 0x140 <= 29632; off += 4) {
        if (!LooksLikeCharParam(body + off)) continue;
        float a = 0, d = 0, sc = 0;
        SafeRead((const void*)(body + off + kFldReturnActivate), &a, 4);
        SafeRead((const void*)(body + off + kFldReturnDuration), &d, 4);
        SafeRead((const void*)(body + off + kFldScale), &sc, 4);
        char cl[160];
        sprintf_s(cl, "  copy #%d at body+0x%04X: activate=%.1f duration=%.1f scale=%.3f",
                  copies, (unsigned)off, a, d, sc);
        logFile << cl << std::endl;
        ++copies;
        if (copies >= 8) break;
    }
    logFile << "  total copies: " << copies << std::endl;
}

// ----------------------------------------------------------------- ЗАПИСЬ ---
// Первая настоящая мутация: размер особи (スケール値, +0x12C).
//
// Почему именно масштаб первым:
//   - виден глазом мгновенно, не надо гадать, сработало ли;
//   - неверное значение не портит логику: это множитель отрисовки/габарита,
//     а не указатель и не счётчик;
//   - поле статично (файл 1.0 == память 1.0), движок его не пересчитывает,
//     значит наша запись не будет затёрта в следующем кадре.
//
// Копий структуры в теле ДВЕ, подряд: +0x5870 и +0x59B0 (шаг 0x140 = sizeof).
// Пишем в обе — иначе движок может прочитать нетронутую.

static bool SafeWrite(void* dst, const void* src, size_t n)
{
    if (!dst) return false;
    __try {
        memcpy(dst, src, n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Помним, кому какой размер выдали.
//
// ИСПРАВЛЕНО: раньше здесь стоял «записал один раз и забыл». Это неверно
// для поля, которое движок может перезаписать при инициализации спавна:
// наша запись откатывалась, а повторно мы уже не пробовали.
// Теперь помним ЖЕЛАЕМОЕ значение и сверяем его каждый тик; если движок
// откатил — пишем снова и считаем откаты.
// Ручное переопределение (кнопка FORCE).
//
// ПОЧЕМУ ЭТО ОБЯЗАТЕЛЬНО. Тик сверяет фактический масштаб с желаемым
// каждые 150 мс и восстанавливает своё. Поэтому кнопка без удержания
// давала эффект длиной в один кадр: гоблин сжимался и мгновенно
// возвращался к размеру из ini. В логе это выглядело как "не работает",
// хотя запись проходила:
//     ForceScale ... after=(0.600,0.600,0.600)   <- кнопка сработала
//     scale 1.233 ... was=0.600                  <- тик вернул своё
//
// Пока стоит удержание, тик это тело не трогает вовсе.
static uintptr_t s_holdBody  = 0;
static float     s_holdValue = 0.0f;

// Счётчик реальных вмешательств движка.
//
// ВАЖНО: раньше сюда попадали наши же нажатия кнопки (тик видел чужое
// значение и считал это откатом движка). Теперь тело под кнопкой
// исключено из тика, поэтому "revert" означает именно то, что написано:
// значение изменил кто-то извне мода.
//
// baseW/baseH/baseD — ВАНИЛЬНЫЙ масштаб особи, снятый при первой встрече.
// Зачем: в тесте 10 выяснилось, что гоблин спавнится с H=1.136, а не 1.000
// (у зайцев ровно 1.000). В em0100_cmn.prp スケール値 = 1.0, значит разброс
// делает сам движок — у Capcom есть штатная вариативность размера особей.
// Если писать наше значение НАПРЯМУЮ, мы стираем этот разброс. Поэтому
// множим: итог = ванильное * наш_коэффициент.
struct Touched {
    uintptr_t body;
    float scale;
    int   reverts;
    int   applies;
    float baseW, baseH, baseD;
    bool  haveBase;
    // Поводок: ванильные таймеры возврата, снятые при первой встрече.
    // Отдельно от масштаба, потому что +0x100 движок пересчитывает сам
    // (в файле 60.0, в памяти 30.0) — тем важнее не потерять исходное.
    float baseLeashAct, baseLeashDur;
    bool  haveLeash;
    int   leashLogged;   // чтобы не залить лог одинаковыми строками
};
static Touched s_touched[32];
static int     s_nTouched = 0;

static Touched* FindTouched(uintptr_t body)
{
    for (int i = 0; i < s_nTouched; ++i)
        if (s_touched[i].body == body) return &s_touched[i];
    return nullptr;
}

static Touched* RememberTouched(uintptr_t body, float scale)
{
    if (s_nTouched >= 32) s_nTouched = 0;   // кольцо: старые особи давно мертвы
    Touched* t = &s_touched[s_nTouched];
    t->body     = body;
    t->scale    = scale;
    t->reverts  = 0;
    t->applies  = 0;
    t->baseW    = 1.0f;
    t->baseH    = 1.0f;
    t->baseD    = 1.0f;
    t->haveBase = false;
    t->baseLeashAct = 0.0f;
    t->baseLeashDur = 0.0f;
    t->haveLeash    = false;
    t->leashLogged  = 0;
    ++s_nTouched;
    return t;
}

// Детерминированный разброс: одна и та же особь получает один и тот же
// размер, даже если мы пересчитаем. Адрес тела как источник.
static float PickScale(uintptr_t body, float lo, float hi)
{
    if (hi <= lo) return lo;
    uint32_t h = (uint32_t)(body >> 4);
    h ^= h >> 13; h *= 0x5BD1E995u; h ^= h >> 15;
    float t = (float)(h & 0xFFFF) / 65535.0f;
    return lo + (hi - lo) * t;
}

// Прочитать текущий масштаб (берём высоту как представителя).
static bool ReadScale(uintptr_t body, float& w, float& h, float& d)
{
    if (!SafeRead((const void*)(body + kScaleW), &w, 4)) return false;
    if (!SafeRead((const void*)(body + kScaleH), &h, 4)) return false;
    if (!SafeRead((const void*)(body + kScaleD), &d, 4)) return false;
    return true;
}

// Масштаб осмыслен только в разумных пределах — заодно это проверка,
// что мы действительно на location-структуре, а не на мусоре.
static bool ScaleLooksSane(float v)
{
    return v > 0.05f && v < 20.0f;
}

// Применить масштаб. Возвращает число записанных полей (0..3).
static int ApplyScale(uintptr_t body, float w, float h, float d)
{
    float cw = 0, ch = 0, cd = 0;
    if (!ReadScale(body, cw, ch, cd)) return 0;
    // не пишем в мусор: у живого существа тут ~1.0
    if (!ScaleLooksSane(cw) || !ScaleLooksSane(ch) || !ScaleLooksSane(cd)) return 0;

    int wrote = 0;
    if (!NearlyEq(cw, w) && SafeWrite((void*)(body + kScaleW), &w, 4)) ++wrote;
    if (!NearlyEq(ch, h) && SafeWrite((void*)(body + kScaleH), &h, 4)) ++wrote;
    if (!NearlyEq(cd, d) && SafeWrite((void*)(body + kScaleD), &d, 4)) ++wrote;
    return wrote;
}


// ------------------------------------------------------------ ForceScale ----
// Диагностика: записать масштаб ПРЯМО СЕЙЧАС по кнопке и сразу перечитать.
// Отвечает на вопрос «движок откатывает или просто не читает поле живьём».
//
// Три исхода:
//   1. прочиталось наше значение и модель изменилась -> поле живое;
//   2. прочиталось наше, модель прежняя -> читается только при спавне;
//   3. прочиталось 1.0 -> движок откатил мгновенно, это не то поле.
void ForceScale(float v)
{
    // ИСПРАВЛЕНО (тест 07): раньше здесь стоял FirstEnemyBody(), который
    // отдавал первого uEm* в списке — а это стабильно uEm8000 (лагерные,
    // их шесть), не гоблин. Кнопка честно писала и честно перечитывала
    // своё значение, только не у того существа.
    //
    // Теперь целимся в гоблина явно, а если его в мире нет — берём первого
    // и ОБЯЗАТЕЛЬНО пишем в лог, кого именно масштабируем.
    const char* kind = nullptr;
    uintptr_t body = TargetBody("ForceScale", &kind);
    if (!body) {
        lstrcpynA(s_status, "ForceScale: no enemy body", sizeof(s_status));
        logFile << "EnemyTuner: " << s_status << std::endl;
        return;
    }

    // Ставим удержание ДО записи: иначе тик успеет вмешаться и мы снова
    // будем измерять собственную работу вместо поведения движка.
    s_holdBody  = body;
    s_holdValue = v;

    float bw = 0, bh = 0, bd = 0;
    ReadScale(body, bw, bh, bd);

    int n = ApplyScale(body, v, v, v);

    float aw = 0, ah = 0, ad = 0;
    ReadScale(body, aw, ah, ad);

    char line[192];
    sprintf_s(line,
        "ForceScale: %s 0x%08X +0x60 before=(%.3f,%.3f,%.3f) wrote=%.3f fields=%d after=(%.3f,%.3f,%.3f) [HOLD]",
        kind ? kind : "?", (unsigned)body, bw, bh, bd, v, n, aw, ah, ad);
    lstrcpynA(s_status, line, sizeof(s_status));
    logFile << "EnemyTuner: " << line << std::endl;

    // Проверяем все три множителя: держится ли запись.
    if (!NearlyEq(aw, v) || !NearlyEq(ah, v) || !NearlyEq(ad, v)) {
        logFile << "  -> value did NOT hold: the engine reverts this field"
                << std::endl;
    } else {
        logFile << "  -> written and held. The tick no longer touches this body"
                << " (HOLD), so any further change comes from the engine."
                << " Press CHECK hold in a couple of seconds."
                << std::endl;
    }
}

// Определена ниже (блок FieldScan) — объявляем заранее.
static void FieldScanTick();

// SampleTick вызывается каждый кадр из UI-колбэка.
// Покадровый сэмплер масштаба удалён: он ответил на свой вопрос
// (тест 10 — масштаб в теле статичен) и полностью перекрыт FieldScan.
// Осталась только диспетчеризация покадровых задач.
void SampleTick()
{
    FieldScanTick();
}

// Нажать через несколько секунд после FORCE.
//
// Пока тело под удержанием, тик его не трогает. Значит любое изменение
// значения за это время сделал ДВИЖОК. Это и есть чистый ответ на вопрос
// "откатывает ли он поле" — без нашего участия.
// ==================== ПОИСК АНИМИРУЕМЫХ ПОЛЕЙ В ТЕЛЕ ======================
//
// Вопрос: если движок анимирует масштаб, где лежит анимируемое значение?
//
// Рассуждение. Если бы движок писал прямо в +0x60/64/68 каждый кадр, наша
// запись затиралась бы мгновенно и гоблин остался бы ванильным. Но размер
// держится (engineReverts ~1 на 5 применений). Значит:
//   +0x60/64/68 — БАЗА, её читают;
//   а где-то рядом лежит РАБОЧЕЕ значение, которое движок пересчитывает.
//
// Это ровно гипотеза «два параметра: статичный и плавающий». Проверяем не
// рассуждением, а перебором: снимаем всё тело как массив float каждый кадр
// и смотрим, какие смещения меняются.
//
// Тело 29632 B = 7408 float. Снимок раз в кадр — это ~30 КБ memcpy,
// на фоне отрисовки незаметно.
static const uint32_t kBodyFloats = 29632 / 4;

static float    s_baseSnap[kBodyFloats];   // значения на старте
static float    s_minSnap[kBodyFloats];
static float    s_maxSnap[kBodyFloats];
static uint16_t s_changeCnt[kBodyFloats];  // сколько раз менялось
// Буфер одного кадра: тело копируется сюда целиком одним чтением.
static float    s_frameBuf[kBodyFloats];
static bool     s_scanning   = false;
static uintptr_t s_scanBody  = 0;
static int      s_scanFrames = 0;

void StartFieldScan()
{
    const char* kind = nullptr;
    uintptr_t body = TargetBody("FieldScan", &kind);
    if (!body) {
        lstrcpynA(s_status, "FieldScan: no target", sizeof(s_status));
        logFile << "EnemyTuner: " << s_status << std::endl;
        return;
    }

    // Стартовый снимок — тоже одним чтением.
    if (!SafeRead((const void*)body, s_frameBuf, kBodyFloats * 4)) {
        lstrcpynA(s_status, "FieldScan: body not fully readable", sizeof(s_status));
        logFile << "EnemyTuner: " << s_status << std::endl;
        return;
    }
    for (uint32_t i = 0; i < kBodyFloats; ++i) {
        float v = s_frameBuf[i];
        s_baseSnap[i]  = v;
        s_minSnap[i]   = v;
        s_maxSnap[i]   = v;
        s_changeCnt[i] = 0;
    }
    s_scanBody   = body;
    s_scanFrames = 0;
    s_scanning   = true;

    char line[160];
    sprintf_s(line, "FieldScan: watching %u fields of %s 0x%08X",
              (unsigned)kBodyFloats, kind ? kind : "?", (unsigned)body);
    lstrcpynA(s_status, line, sizeof(s_status));
    logFile << "EnemyTuner: " << line << std::endl;
}

// Каждый кадр: сравнить всё тело с прошлым снимком.
static void FieldScanTick()
{
    if (!s_scanning || !s_scanBody) return;

    // ОДИН SafeRead на всё тело, а не 7408 штук: внутри SafeRead сидит
    // IsBadReadPtr, вызывать его на каждый float — это тысячи системных
    // проверок за кадр. Копируем блоком, дальше работаем с локальной копией.
    if (!SafeRead((const void*)s_scanBody, s_frameBuf, kBodyFloats * 4)) {
        s_scanning = false;
        logFile << "EnemyTuner: FieldScan: body became unreadable, stopped"
                << std::endl;
        return;
    }

    ++s_scanFrames;
    for (uint32_t i = 0; i < kBodyFloats; ++i) {
        float v = s_frameBuf[i];
        // NaN/мусор пропускаем: сравнения с NaN всегда ложны и портят min/max
        if (!(v == v)) continue;
        if (v < s_minSnap[i]) s_minSnap[i] = v;
        if (v > s_maxSnap[i]) s_maxSnap[i] = v;
        if (!NearlyEq(v, s_baseSnap[i])) {
            if (s_changeCnt[i] < 0xFFFF) ++s_changeCnt[i];
            s_baseSnap[i] = v;
        }
    }
}

void StopFieldScan()
{
    s_scanning = false;
    if (s_scanFrames <= 0) {
        lstrcpynA(s_status, "FieldScan: no frames captured", sizeof(s_status));
        logFile << "EnemyTuner: " << s_status << std::endl;
        return;
    }

    logFile << "EnemyTuner: --- FieldScan: " << s_scanFrames
            << " frames, body 0x" << std::hex << s_scanBody << std::dec
            << " ---" << std::endl;

    // Интересуют поля, которые (а) менялись и (б) похожи на множитель:
    // диапазон 0.05..20. Координаты (тысячи) и таймеры отсеиваются.
    int shown = 0;
    logFile << "  MULTIPLIER-LIKE fields that changed during the run:" << std::endl;
    for (uint32_t i = 0; i < kBodyFloats && shown < 60; ++i) {
        if (!s_changeCnt[i]) continue;
        float lo = s_minSnap[i], hi = s_maxSnap[i];
        if (lo < 0.05f || hi > 20.0f) continue;      // не множитель
        if (NearlyEq(lo, hi)) continue;              // размах нулевой

        char cl[190];
        sprintf_s(cl, "    +0x%04X  %.4f .. %.4f  (span %.4f, changes %u)",
                  (unsigned)(i * 4), lo, hi, hi - lo, (unsigned)s_changeCnt[i]);
        logFile << cl;
        if (i * 4 == kScaleW) logFile << "   <- OUR W";
        if (i * 4 == kScaleH) logFile << "   <- OUR H";
        if (i * 4 == kScaleD) logFile << "   <- OUR D";
        logFile << std::endl;
        ++shown;
    }
    if (!shown) logFile << "    (none - body scale is static)" << std::endl;

    // Отдельно: троек подряд (X,Y,Z), которые меняются — кандидаты на
    // рабочий масштаб или на матрицу трансформации.
    logFile << "  TRIPLES of adjacent changing multipliers (W/H/D candidates):"
            << std::endl;
    int triples = 0;
    for (uint32_t i = 0; i + 2 < kBodyFloats && triples < 20; ++i) {
        bool ok = true;
        for (int k = 0; k < 3; ++k) {
            if (!s_changeCnt[i + k]) { ok = false; break; }
            float lo = s_minSnap[i + k], hi = s_maxSnap[i + k];
            if (lo < 0.05f || hi > 20.0f) { ok = false; break; }
        }
        if (!ok) continue;
        char cl[190];
        sprintf_s(cl, "    +0x%04X  (%.3f..%.3f, %.3f..%.3f, %.3f..%.3f)",
                  (unsigned)(i * 4),
                  s_minSnap[i],     s_maxSnap[i],
                  s_minSnap[i + 1], s_maxSnap[i + 1],
                  s_minSnap[i + 2], s_maxSnap[i + 2]);
        logFile << cl << std::endl;
        ++triples;
        i += 2;
    }
    if (!triples) logFile << "    (none)" << std::endl;

    char line[160];
    sprintf_s(line, "FieldScan: %d frames, %d fields changing - see log",
              s_scanFrames, shown);
    lstrcpynA(s_status, line, sizeof(s_status));
}

// ----------------------------------------------------------- CheckHold ------
// Нажать через несколько секунд после FORCE.
//
// Пока тело под удержанием, тик его не трогает. Значит любое изменение
// значения за это время сделал ДВИЖОК.
void CheckHold()
{
    if (!s_holdBody) {
        lstrcpynA(s_status, "CheckHold: no hold set, press FORCE first",
                  sizeof(s_status));
        logFile << "EnemyTuner: " << s_status << std::endl;
        return;
    }

    float w = 0, h = 0, d = 0;
    if (!ReadScale(s_holdBody, w, h, d)) {
        lstrcpynA(s_status, "CheckHold: body unreadable (died or unloaded)",
                  sizeof(s_status));
        logFile << "EnemyTuner: " << s_status << std::endl;
        s_holdBody = 0;
        return;
    }

    bool held = NearlyEq(w, s_holdValue)
             && NearlyEq(h, s_holdValue)
             && NearlyEq(d, s_holdValue);

    char line[192];
    sprintf_s(line, "CheckHold: 0x%08X want=%.3f now=(%.3f,%.3f,%.3f) -> %s",
              (unsigned)s_holdBody, s_holdValue, w, h, d,
              held ? "HELD" : "CHANGED BY ENGINE");
    lstrcpynA(s_status, line, sizeof(s_status));
    logFile << "EnemyTuner: " << line << std::endl;

    if (held) {
        logFile << "  -> the engine does NOT touch +0x60/64/68. If the model also"
                << " did not change, these bytes do not affect rendering"
                << " for this creature: look for the real multiplier elsewhere."
                << std::endl;
    } else {
        logFile << "  -> the engine rewrites the field itself. The field is live,"
                << " but the game code drives it: needs a hook, not a tick write."
                << std::endl;
    }
}

// ------------------------------------------------------------- DumpHead -----
// Печать первых 0x100 байт тела как float и как hex.
//
// ЗАЧЕМ ИМЕННО ЭТО. Вывод "+0x60 = масштаб" был сделан по аналогии:
// у автора статьи координаты читались как [eax+40], и у нашего тела xyz
// тоже на +0x40 — значит, решили мы, это та же структура.
//
// Но совпадение одного оффсета — слабое доказательство. У движка есть
// отдельный тип uCoord (240 байт). Возможно, "location structure" из
// статьи — это он, а не тело uEm*, и +0x40 там совпало случайно.
//
// Глядя на сырые байты, это видно сразу:
//   - если +0x40/44/48 = координаты (тысячи) и рядом +0x60/64/68 = ~1.0,
//     структура похожа на нужную;
//   - если между ними лежат указатели/мусор — мы в чужой структуре
//     и настоящий масштаб надо искать по указателю на uCoord.
void DumpHead()
{
    const char* kind = nullptr;
    uintptr_t body = TargetBody("DumpHead", &kind);
    if (!body) {
        lstrcpynA(s_status, "DumpHead: no enemy body", sizeof(s_status));
        logFile << "EnemyTuner: " << s_status << std::endl;
        return;
    }

    logFile << "EnemyTuner: body head " << (kind ? kind : "?")
            << " 0x" << std::hex << body << std::dec
            << " (0x00..0x100)" << std::endl;

    for (uint32_t off = 0; off < 0x100; off += 16) {
        uint32_t u[4];
        if (!SafeRead((const void*)(body + off), u, 16)) break;
        float f[4];
        memcpy(f, u, 16);

        char cl[220];
        sprintf_s(cl,
            "  +0x%02X  %08X %08X %08X %08X   | %12.3f %12.3f %12.3f %12.3f",
            off, u[0], u[1], u[2], u[3], f[0], f[1], f[2], f[3]);
        logFile << cl;

        if (off == 0x40) logFile << "   <- xyz?";
        if (off == 0x60) logFile << "   <- scale W/H/D?";
        logFile << std::endl;
    }

    // Ищем в голове указатели на объекты — вдруг настоящая location-структура
    // лежит отдельно, а тело только ссылается на неё.
    logFile << "  -- pointers in the head (uCoord candidates) --" << std::endl;
    int nptr = 0;
    for (uint32_t off = 0; off < 0x100; off += 4) {
        uint32_t v = 0;
        if (!SafeRead((const void*)(body + off), &v, 4)) continue;
        if (!LooksHeap(v)) continue;
        char nm[64] = { 0 };
        const char* n = Runtime::Mem::NameOfLiveObjectSafe((const void*)(uintptr_t)v,
                                                       nm, sizeof(nm));
        char cl[160];
        sprintf_s(cl, "    +0x%02X -> 0x%08X  %s", off, v, n ? n : "(name not resolved)");
        logFile << cl << std::endl;
        ++nptr;
    }
    if (!nptr) logFile << "    (none)" << std::endl;

    char line[160];
    sprintf_s(line, "DumpHead: %s 0x%08X - see log", kind ? kind : "?", (unsigned)body);
    lstrcpynA(s_status, line, sizeof(s_status));
}

// Снять удержание — тик снова управляет этим телом.
void ReleaseHold()
{
    if (s_holdBody) {
        logFile << "EnemyTuner: hold 0x" << std::hex << s_holdBody
                << std::dec << " captured" << std::endl;
    }
    s_holdBody  = 0;
    s_holdValue = 0.0f;
    lstrcpynA(s_status, "HOLD released: tick controls scale again",
              sizeof(s_status));
}

// --------------------------------------------------------- DumpSensorWindow -
// Находки легли парами (угол, радиус) с шагом 0x140 = 320 байт.
// Печатаем окно целиком, чтобы увидеть полную запись сенсора:
// в файле .sn2 запись 0x50 байт = {type, ..., r1, r2, ..., angle}.
void DumpSensorWindow()
{
    uintptr_t body = TargetBody("SensorWindow", nullptr);
    if (!body) {
        lstrcpynA(s_status, "SensorWindow: no enemy body", sizeof(s_status));
        logFile << "EnemyTuner: " << s_status << std::endl;
        return;
    }

    const uint32_t kFrom = 0x5900;
    const uint32_t kTo   = 0x5B80;

    logFile << "EnemyTuner: sensor window body 0x" << std::hex << body
            << " +0x" << kFrom << "..+0x" << kTo << std::dec << std::endl;

    for (uint32_t off = kFrom; off < kTo; off += 16) {
        uint32_t w[4] = { 0, 0, 0, 0 };
        if (!SafeRead((const void*)(body + off), w, 16)) break;

        char line[192];
        wsprintfA(line, "  +0x%04X  %08X %08X %08X %08X",
                  off, w[0], w[1], w[2], w[3]);
        logFile << line;

        // рядом — те же dword как float, чтобы читать глазами
        logFile << "   |";
        for (int i = 0; i < 4; ++i) {
            float f;
            memcpy(&f, &w[i], 4);
            if (f != 0.0f && f > -1e7f && f < 1e7f) {
                char fb[32];
                sprintf_s(fb, " %.4g", f);
                logFile << fb;
            } else {
                logFile << " .";
            }
        }
        logFile << std::endl;
    }
    lstrcpynA(s_status, "SensorWindow: dumped, see log", sizeof(s_status));
}

// "uEm0100" -> 100. Возвращает 0xFFFF, если это не uEm<цифры>.
//
// Зачем: список врагов разнороден. У лагеря шесть uEm8000 и один uEm0100,
// и конфиг вида надо брать по РЕАЛЬНОМУ виду каждого тела, а не по одному
// захардкоженному номеру.
// Люди-враги (бандиты, солдаты) — не uEm*, а uHumanEnemy. Своего числового
// id у них нет, поэтому даём синтетический: в ini секция [human].
// 9999 не пересекается с реальными em-номерами (0100..8600).
static const uint16_t kHumanEnemyId = 9999;

static uint16_t EmIdFromKind(const char* k)
{
    if (k && strcmp(k, "uHumanEnemy") == 0) return kHumanEnemyId;
    if (!k || k[0] != 'u' || k[1] != 'E' || k[2] != 'm') return 0xFFFF;
    uint32_t v = 0;
    int n = 0;
    for (const char* p = k + 3; *p; ++p, ++n) {
        if (*p < '0' || *p > '9') return 0xFFFF;
        v = v * 10 + (uint32_t)(*p - '0');
    }
    if (!n || v > 0xFFFF) return 0xFFFF;
    return (uint16_t)v;
}

// Применить масштаб к одному телу по конфигу его вида.
// Вынесено из Tick, чтобы обходить всех врагов одним циклом.
// --------------------------------------------------------------- поводок ---
//
// "Поводок" в этом движке — не радиус, а ДВА ТАЙМЕРА в cCharParamEnemy:
//     +0x100  リターンテリトリー発動タイム  — через сколько решает вернуться
//     +0x104  リターンテリトリー継続タイム  — сколько длится возврат
//
// leashScale > 1 = враг преследует ДОЛЬШЕ (позже разворачивается домой).
//
// Три отличия от масштаба, из-за которых код осторожнее:
//
// 1. Поле +0x100 движок пересчитывает сам: в файле 60.0, в памяти 30.0.
//    Значит писать туда штатно, но ванильную базу надо снять до записи,
//    иначе следующий тик умножит уже наш результат (та же ловушка
//    самозахвата, что мы ловили с масштабом).
//
// 2. Структура лежит в теле ДВАЖДЫ: +0x5870 и +0x59B0 (шаг 0x140 =
//    sizeof). Какую копию читает движок — неизвестно, поэтому пишем в обе.
//
// 3. Эффект не виден глазом мгновенно: надо отойти и ждать. Поэтому
//    значения пишем в лог — проверять будем по нему, а не "на глаз".
static int ApplyLeash(uintptr_t body, Touched* rec, float scale)
{
    uintptr_t base = body + kCharParamOff;
    if (!LooksLikeCharParam(base)) {
        base = FindCharParam(body, 29632);
        if (!base) return 0;
    }

    float act = 0, dur = 0;
    if (!SafeRead((const void*)(base + kFldReturnActivate), &act, 4)) return 0;
    if (!SafeRead((const void*)(base + kFldReturnDuration), &dur, 4)) return 0;

    // Первая встреча: запоминаем ваниль. Санити-проверка, чтобы не взять
    // за базу мусор из ещё не готового тела.
    if (!rec->haveLeash) {
        if (act <= 0.0f || act > 10000.0f || dur <= 0.0f || dur > 100000.0f)
            return 0;
        rec->baseLeashAct = act;
        rec->baseLeashDur = dur;
        rec->haveLeash    = true;
    }

    float wantAct = rec->baseLeashAct * scale;
    float wantDur = rec->baseLeashDur * scale;

    int wrote = 0;
    // Обе копии структуры: движок может читать любую.
    for (int c = 0; c < 2; ++c) {
        uintptr_t b = base + (uintptr_t)c * 0x140;
        if (c && !LooksLikeCharParam(b)) break;   // второй копии может не быть
        float cur = 0;
        if (SafeRead((const void*)(b + kFldReturnActivate), &cur, 4) &&
            !NearlyEq(cur, wantAct) &&
            SafeWrite((void*)(b + kFldReturnActivate), &wantAct, 4)) ++wrote;
        if (SafeRead((const void*)(b + kFldReturnDuration), &cur, 4) &&
            !NearlyEq(cur, wantDur) &&
            SafeWrite((void*)(b + kFldReturnDuration), &wantDur, 4)) ++wrote;
    }
    return wrote;
}

static void TickOneBody(uintptr_t body, const char* kind)
{
    // Тело под ручным удержанием (кнопка FORCE) — не трогаем.
    // Иначе тик затирает результат нажатия и мы сами себе создаём "реверты".
    if (s_holdBody && body == s_holdBody) return;

    uint16_t emId = EmIdFromKind(kind);
    if (emId == 0xFFFF) return;

    const EntityCfg::Tuning& t = EntityCfg::For(emId);
    if (!t.enabled) return;

    Touched* rec0 = FindTouched(body);
    if (!rec0) rec0 = RememberTouched(body, 1.0f);

    // --- поводок: независимый блок --------------------------------------
    // МОДУЛЬНОСТЬ: поводок и масштаб не должны зависеть друг от друга.
    // Раньше здесь стоял общий ранний выход по scaleMin/Max == 1.0, и при
    // выключенном масштабе поводок молча не работал бы.
    if (!NearlyEq(t.leashScale, 1.0f)) {
        int n = ApplyLeash(body, rec0, t.leashScale);
        if (n > 0) {
            s_writes += n;
            if (rec0->leashLogged < 2) {
                ++rec0->leashLogged;
                char ll[190];
                sprintf_s(ll,
                    "leash x%.2f -> %s 0x%08X  activate %.1f -> %.1f, duration %.1f -> %.1f",
                    t.leashScale, kind ? kind : "?", (unsigned)body,
                    rec0->baseLeashAct, rec0->baseLeashAct * t.leashScale,
                    rec0->baseLeashDur, rec0->baseLeashDur * t.leashScale);
                logFile << "EnemyTuner: " << ll << std::endl;
                lstrcpynA(s_status, ll, sizeof(s_status));
            }
        }
    }

    // --- масштаб (SpeciesCard + EntityCfg) --------------------------------
    const MonsterAI::SpeciesCard* card = MonsterAI::FindSpeciesCard(kind);

    float scaleLo = t.scaleMin;
    float scaleHi = t.scaleMax;
    float jitter = t.scaleJitter;
    float leaderThresh = 1.12f;

    if (card) {
        if (NearlyEq(scaleLo, 1.0f) && NearlyEq(scaleHi, 1.0f)) {
            scaleLo = card->scaleMin;
            scaleHi = card->scaleMax;
            jitter = card->scaleJitter;
            leaderThresh = card->leaderScaleThreshold;
        }
    }

    if (NearlyEq(scaleLo, 1.0f) && NearlyEq(scaleHi, 1.0f)) return;

    Touched* rec = rec0;      // запись уже получена выше (блок поводка)

    // Что сейчас реально лежит в location-структуре?
    float cw = 0, ch = 0, cd = 0;
    if (!ReadScale(body, cw, ch, cd)) return;

    // Первая встреча с особью: запоминаем ВАНИЛЬНЫЙ масштаб.
    // Движок разбрасывает особей по росту сам (гоблин спавнится с 1.136,
    // зайцы с 1.000), и этот разброс надо сохранить, а не затереть.
    if (!rec->haveBase) {
        if (!ScaleLooksSane(cw) || !ScaleLooksSane(ch) || !ScaleLooksSane(cd))
            return;                       // тело ещё не готово, подождём тик

        // ЗАЩИТА ОТ САМОЗАХВАТА. Если мод перезагрузили (или запись уже
        // применялась) — в памяти лежит НАШЕ значение, и принять его за
        // ваниль нельзя: коэффициент начнёт умножаться сам на себя и
        // существо будет расти с каждой перезагрузкой.
        // Ванильный масштаб у Capcom неуниформным не бывает: разброс есть,
        // но W/H/D одной особи равны между собой. Наш jitter их разводит.
        // Значит неравные W/H/D = уже наша работа, базу брать нельзя.
        bool uniform = NearlyEq(cw, ch) && NearlyEq(ch, cd);
        if (!uniform) {
            logFile << "EnemyTuner: 0x" << std::hex << body << std::dec
                    << " scale already non-uniform (" << cw << "," << ch << ","
                    << cd << ") - base not captured, using 1.0"
                    << " (mod reloaded mid-session?)" << std::endl;
            rec->baseW = rec->baseH = rec->baseD = 1.0f;
        } else {
            rec->baseW = cw;
            rec->baseH = ch;
            rec->baseD = cd;
        }
        rec->haveBase = true;
    }

    // Детектор вожака (Capcom Native Alpha / Leader):
    const bool isLeader = (rec->baseH >= leaderThresh);
    float wantW = 1.0f, wantH = 1.0f, wantD = 1.0f;

    if (isLeader) {
        // Вожак от Capcom: сохраняем его авторский статус и крупный размер,
        // лишь гарантируем верхний предел безопасности (scaleHi + 0.04).
        wantH = (rec->baseH > scaleHi + 0.04f) ? (scaleHi + 0.04f) : rec->baseH;
        wantW = wantH;
        wantD = wantH;
    } else {
        // Рядовой член стаи: рассчитываем размер внутри коридора вида
        wantH = PickScale(body, scaleLo, scaleHi);
        if (jitter > 0.001f) {
            uint32_t h1 = (uint32_t)(body >> 3) * 2654435761u;
            uint32_t h2 = (uint32_t)(body >> 5) * 2246822519u;
            float j1 = ((float)((h1 >> 8) & 0xFFFF) / 65535.0f) * 2.0f - 1.0f;
            float j2 = ((float)((h2 >> 8) & 0xFFFF) / 65535.0f) * 2.0f - 1.0f;
            wantW = wantH * (1.0f + jitter * j1);
            wantD = wantH * (1.0f + jitter * j2);
        } else {
            wantW = wantH;
            wantD = wantH;
        }
    }

    float cur = ch;

    if (NearlyEq(cur, wantH)) return;   // держится — ничего не делаем

    // Значение не наше: либо ещё не писали, либо движок откатил.
    bool wasReverted = (rec->applies > 0);
    int n = ApplyScale(body, wantW, wantH, wantD);
    if (n <= 0) return;

    s_writes += n;
    ++rec->applies;
    if (wasReverted) ++rec->reverts;

    // Логируем первые несколько раз и далее раз в 32 применения,
    // чтобы не залить лог, если движок откатывает каждый кадр.
    if (rec->applies <= 5 || (rec->applies % 32) == 0) {
        char line[192];
        sprintf_s(line,
            "scale %.3f (base %.3f %s) -> %s 0x%08X was=%.3f applies=%d engineReverts=%d",
            wantH, rec->baseH, isLeader ? "LEADER" : "GENE", kind ? kind : "?",
            (unsigned)body, cur, rec->applies, rec->reverts);
        lstrcpynA(s_status, line, sizeof(s_status));
        logFile << "EnemyTuner: " << line << std::endl;
    }
}

// --------------------------------------------------------------- ListEnemies
// Диагностика, которой не хватило и стоила двух итераций.
// Печатает ВЕСЬ список врагов: индекс, вид, адрес, текущий масштаб.
// Сразу видно, кто в мире и кому реально уходит запись.
void ListEnemies()
{
    int n = 0;
    logFile << "EnemyTuner: --- live enemy list ---" << std::endl;
    for (int i = 0; ; ++i) {
        const char* kind = nullptr;
        uintptr_t body = Runtime::EnemyBodyAt(i, &kind);
        if (!body) break;
        float w = 0, h = 0, d = 0;
        bool ok = ReadScale(body, w, h, d);
        char cl[160];
        sprintf_s(cl, "  [%d] %-10s 0x%08X  scale=(%.3f, %.3f, %.3f)%s",
                  i, kind ? kind : "?", (unsigned)body, w, h, d,
                  ok ? "" : "  <unreadable>");
        logFile << cl << std::endl;
        ++n;
    }
    if (!n) logFile << "  (empty - load a save and run HUNT)" << std::endl;

    char line[128];
    sprintf_s(line, "ListEnemies: %d enemies, details in log", n);
    lstrcpynA(s_status, line, sizeof(s_status));
}

// ------------------------------------------------------------------- Tick ---
// ИСПРАВЛЕНО (тест 07). Раньше здесь стояло:
//
//     uintptr_t body = DevTools::FirstEnemyBody();
//     const EntityCfg::Tuning& t = EntityCfg::For(100);
//
// Две ошибки в двух строках:
//   1. FirstEnemyBody() = ПЕРВЫЙ uEm* в списке. В дампах это стабильно
//      0x10DD0060 = uEm8000 (лагерные, gid 0x61, их шесть), а гоблин лежит
//      следующим на 0x10DD7320. Мы масштабировали зайца, не гоблина.
//   2. For(100) — конфиг гоблина применялся к тому, кто попался первым.
//
// Симптом был идеально обманчив: запись проходила, reverts=0, значение
// держалось — и ничего не менялось на экране. Поле было верное, тело чужое.
//
// Теперь: обходим ВСЕХ врагов и каждому даём конфиг ЕГО вида.
void Tick()
{
    if (!EntityCfg::Enabled()) { s_tracked = 0; return; }

    s_tracked = Runtime::EnemyCount();

    if (!EntityCfg::AllowWrites()) return;

    for (int i = 0; ; ++i) {
        const char* kind = nullptr;
        uintptr_t body = Runtime::EnemyBodyAt(i, &kind);
        if (!body) break;
        TickOneBody(body, kind);
    }
}

const char* StatusLine() { return s_status; }

// Тело под ручным удержанием, 0 если удержания нет. Для индикатора в UI.
uintptr_t HeldBody()  { return s_holdBody; }
float     HeldValue() { return s_holdValue; }
int TrackedCount()       { return s_tracked; }
int WriteCount()         { return s_writes; }

} // namespace EnemyTuner
