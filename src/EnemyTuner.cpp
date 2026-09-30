#include "stdafx.h"
#include "runtime/Runtime.h"
#include "runtime/MemProbe.h"
#include "runtime/MonsterTempo.h"
#include "EnemyTuner.h"
#include "runtime/MonsterTempo.h"   // 85.34: оболочка адреналина (сила атаки)
#include "EntityConfig.h"
#include "monsterai/SpeciesCard.h"
#include "TypeAtlas.Generated.h"
#include <math.h>   // 85.57: floorf для ячейки места

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

// 85.34: одна строка «всплеск не применяется, слой мутаций выключен» на эпизод.
static bool s_spikeBlockedLogged = false;

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
static const uint32_t kFldAttack         = 0x00C; // 物理攻撃力 (Physical Attack)
static const uint32_t kFldDefense        = 0x010; // 物理防御力 (Physical Defense)
static const uint32_t kFldMagickAttack   = 0x014; // 魔法攻撃力 (Magick Attack)
static const uint32_t kFldMagickDefense  = 0x018; // 魔法防御力 (Magick Defense)

// 85.44: КРЕПОСТЬ — сопротивления дебилитациям и выносливости к сбиванию.
// Смещения и имена взяты из карты CharParamEnemy.Generated.h (не из головы):
// 0x38 fire 0x3C ice 0x40 thunder ... 0x54..0xA0 сопротивления наложениям,
// 0xE0 отшатывание, 0xE4 сбивание с ног. Элементные сопротивления (fire/ice/…)
// мы НЕ двигаем — это урон, а не дебилитация; огонь должен кусаться как кусался.
struct ResField { uint32_t off; const char* name; };
static const ResField kResDebil[] = {
    { 0x054, "pois"  }, { 0x058, "torp"  }, { 0x05C, "blnd"  }, { 0x060, "slp"   },
    { 0x064, "tar"   }, { 0x068, "drnch" }, { 0x06C, "poss"  }, { 0x070, "sil"   },
    { 0x074, "stfl"  }, { 0x078, "curs"  },
    // ПОРЯДОК ЭТОЙ ПАРЫ В ПАМЯТИ ОТЛИЧАЕТСЯ ОТ ПОРЯДКА В ФАЙЛЕ ИГРЫ.
    // В файле em0100_cmn.prp: 耐延焼 (горение) = 300, 耐氷漬け (заморозка) = 800.
    // Живое чтение по этим смещениям дало наоборот: 0x7C = 800, 0x80 = 300,
    // при том что остальные 18 полей совпали с файлом ровно. Значит в самом
    // движке пара лежит в другом порядке, и метки ставим ПО ПАМЯТИ, а не по
    // файлу. Полевое подтверждение: гоблины в 85.42/85.44 загорались от костров
    // легко — это низкое горение (300), а не 800.
    { 0x07C, "froz"  }, { 0x080, "burn"  },
    { 0x084, "thst"  }, { 0x088, "hlyx"  }, { 0x08C, "drkx"  }, { 0x090, "petr"  },
    { 0x094, "atkd"  }, { 0x098, "defd"  }, { 0x09C, "matkd" }, { 0x0A0, "mdefd" },
};
static const int kResDebilCount = (int)(sizeof(kResDebil) / sizeof(kResDebil[0]));

// ЭЛЕМЕНТНЫЙ УРОН — ДРУГАЯ СЕМЬЯ ПОЛЕЙ, и её мы НЕ трогаем. Это множители
// получаемого урона: у гоблина 耐魔 (тьма) = 0.6, у волка 耐炎 1.1 / 耐氷 0.85,
// у сауриана 耐炎 0.6 / 耐氷 2.5. Здесь «меньше = крепче», в отличие от
// сопротивлений дебилитациям. Печатаем их в лог только как ЯКОРЯ карты: два
// неединичных значения (тьма 0.6 у гоблина, у других видов свои) подтверждают,
// что смещения прочитаны верно.
struct ElemField { uint32_t off; const char* name; };
static const ElemField kElemAnchors[] = {
    { 0x038, "fire" }, { 0x03C, "ice"  }, { 0x040, "thun" },
    { 0x044, "holy" }, { 0x048, "dark" }, { 0x04C, "slash" }, { 0x050, "strike" },
};
static const int kElemAnchorCount = (int)(sizeof(kElemAnchors) / sizeof(kElemAnchors[0]));
// Сколько раз пробовать прочитать крепость, прежде чем приговорить тело.
// Попытки бесплатны (это 22 чтения), а ошибка дорогая: в поле 85.45 первая пачка
// после загрузки зоны отдала не-числа и была отвергнута НАВСЕГДА — живая пачка
// осталась без крепости, хотя подкрепления в том же бою читались нормально.
static const int kResMaxTries = 60;
static const uint32_t kFldHumanHp = 0x0DC;   // 人間敵 HP — третий якорь (у гоблина 1000)
static const uint32_t kFldFlinch  = 0x0E0;   // human_flinch_endur (у гоблина 100)
static const uint32_t kFldKdown   = 0x0E4;   // human_knockdown_endur (у гоблина 100)
static const uint32_t kFldReturnActivate = 0x100; // リターンテリトリー発動タイム
static const uint32_t kFldReturnDuration = 0x104; // リターンテリトリー継続タイム
static const uint32_t kFldScale          = 0x12C; // スケール値

// Сигнатура блока.
//
// 1. Поле +0x120 движок пересчитывает сам: в файле 1.0, в рантайме 0.0.
// Урок: в сигнатуру годятся только поля, которые игра НЕ трогает.
// Берём три дистанции переключения камер — они статичны и образуют
// характерную возрастающую тройку 500/800/1200, случайно такое не встретится.
static bool LooksLikeCharParam(uintptr_t base)
{
    if (!base) return false;
    if (!Runtime::Mem::RegionOk(base, 0x140)) return false;
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
    if (!body || bodySize < 0x200) return 0;
    if (bodySize > 35000) bodySize = 35000;
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
    // 85.26: какому ВИДУ принадлежит эта запись. Адрес тела — не личность:
    // игра переиспользует освободившиеся слоты под другой вид (в поле 85.25
    // видели, как гоблин занял слот волка и унаследовал его ванильные статы).
    // Ноль = вид неизвестен (старая запись), тогда ничего не сбрасываем.
    uint16_t species;
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
    // DDON Sanctuary (броня и скорость на возврате):
    float baseDef, baseMDef;
    bool  haveDef;
    bool  inReturnArmor;
    // Боевые статы (audit 2026-09-21 §8): ваниль + текущее после mult*roll
    float baseAtk, baseDefC, baseMAtk, baseMDefC;
    float curAtk, curDefC, curMAtk, curMDefC; // последнее применённое (с mult+roll)
    bool  haveCombat;
    int   combatLogged;
    float combatRollAtk, combatRollDef, combatRollMAtk, combatRollMDef; // 0.9..1.1 per body
    int   spikeLogged;   // 85.34: одна строка на эпизод всплеска (хвост — append)
    int   rankLogged;  // 85.40: одна строка про ступень на особь
    int   rankCounted; // 85.43: ранг уже попал в сводку сессии (отдельно от печати)

    // 85.56: СТУПЕНЬ ВЫДАЁТСЯ ОДИН РАЗ И ЗАМОРАЖИВАЕТСЯ.
    //
    // Раньше ступень пересчитывалась каждый тик из весов ini. Пока веса читались
    // только при запуске, это было безобидно; с живым чтением [ranks] правка
    // посреди боя переобула бы ЖИВОГО монстра — поехали бы рост, множитель
    // атаки и крепость. Теперь всё, что относится к ступени, берётся в первый
    // тик жизни особи и живёт в записи: ролл снаружи влияет только на тех, кто
    // появится ПОСЛЕ правки.
    //
    // gen — номер жильца адреса. Растёт, когда слот заняло другое тело: ступень
    // привязана к существу, а не к адресу (поле 85.55).
    uint32_t gen;
    int   rankStep;        // -1 = ступень не выдавали (нулевая ступень = новичок!)
    float rankSize;        // рост по ступени (если ступень управляет ростом)
    float rankAtk;
    float rankResist;
    float rankStand;
    bool  rankUseScale;    // решено В МОМЕНТ ВЫДАЧИ: ступень задаёт рост или нет

    // 85.57: МЕСТО. Набор пачки выбирается по ячейке карты, и позицию читаем
    // ОДИН раз, при выдаче ступени: тело может уйти в другой конец карты, а
    // ступень и набор остаются теми, что выпали здесь.
    int   setIndex;        // -1 = наборов нет (работают веса [ranks])
    float setX, setZ;      // позиция в момент выдачи — нужна соседям для наследования
    bool  haveSetPos;
    bool  rankCapped;      // мини-босс понижен пределом «один на место»

    // 85.44: крепость ранга. Ванильные значения читаем ОДИН раз (иначе после
    // нашей же правки прочитаем её же и будем делить вечно), затем каждый тик
    // сверяем текущее с желаемым — как со статами и размером.
    float baseRes[kResDebilCount];
    float baseFlinch, baseKdown;
    bool  haveRes;
    bool  resRejected;   // карта не сложилась после всех попыток -> особь не трогаем
    int   resTries;      // сколько раз пробовали прочитать (поле 85.45: первая попытка
                         // после загрузки зоны может застать блок незаполненным)
    int   resLogged;
    int   resApplied;

    // Кэш смещения cCharParamEnemy в теле: ищем ровно один раз на особь,
    // чтобы не гонять 29-КБ перебор памяти каждый тик для не-гоблинов.
    uint32_t charParamOff;
    bool     charParamSearched;
};
static const int kMaxTouched = 128;
static Touched s_touched[kMaxTouched];
static int     s_nTouched = 0;
// 85.56: поколения жильцов. Ноль — «поколения нет» (старое поведение ролла),
// поэтому первый жилец получает 1.
static uint32_t s_bodyGenSeq = 0;

// Species-level vanilla combat base (audit fix for reload double-mult).
// Per-body base is vulnerable to mid-session reload: body memory already has
// multiplied values (e.g. 512) but Touched is new (haveCombat=false) → we would
// capture 512 as base and double to 1048. Species base is captured once per
// species from first vanilla-looking body and never overwritten.
struct SpeciesCombatBase {
    uint16_t emId = 0xFFFF;
    float atk = 0, defC = 0, mAtk = 0, mDefC = 0;
    bool have = false;
};
static const int kMaxSpeciesBase = 128;
static SpeciesCombatBase s_speciesBase[kMaxSpeciesBase];
static int s_nSpeciesBase = 0;

static SpeciesCombatBase* FindSpeciesBase(uint16_t emId) {
    for (int i=0;i<s_nSpeciesBase;++i) if (s_speciesBase[i].emId==emId) return &s_speciesBase[i];
    return nullptr;
}
static SpeciesCombatBase* RememberSpeciesBase(uint16_t emId, float atk,float defC,float mAtk,float mDefC) {
    auto* exist = FindSpeciesBase(emId);
    if (exist) return exist;
    if (s_nSpeciesBase>=kMaxSpeciesBase) return nullptr;
    auto* s = &s_speciesBase[s_nSpeciesBase++];
    s->emId=emId; s->atk=atk; s->defC=defC; s->mAtk=mAtk; s->mDefC=mDefC; s->have=true;
    return s;
}


// 85.24: сброс на выгрузке мира. См. EnemyTuner.h.
void OnWorldUnload()
{
    const int was = s_nTouched;
    s_nTouched = 0;
    memset(s_touched, 0, sizeof(s_touched));
    // Ручное удержание (кнопка FORCE) держит указатель на тело — тоже чужое.
    s_holdBody = 0;
    logFile << "EnemyTuner: world-unload reset, forgot " << was
            << " bodies (species base table kept=" << s_nSpeciesBase << ")" << std::endl;
}

static void ForgetMissing()
{
    const int n = Runtime::EnemyCount();
    int w = 0;
    for (int i = 0; i < s_nTouched; ++i) {
        bool alive = false;
        for (int k = 0; k < n; ++k) {
            if (Runtime::EnemyBodyAt(k, nullptr) == s_touched[i].body) {
                alive = true;
                break;
            }
        }
        if (!alive) continue;
        if (w != i) s_touched[w] = s_touched[i];
        ++w;
    }
    s_nTouched = w;
}

static Touched* FindTouched(uintptr_t body)
{
    for (int i = 0; i < s_nTouched; ++i)
        if (s_touched[i].body == body) return &s_touched[i];
    return nullptr;
}

static Touched* RememberTouched(uintptr_t body, float scale)
{
    if (s_nTouched >= kMaxTouched) {
        ForgetMissing();
    }
    Touched* t = nullptr;
    if (s_nTouched < kMaxTouched) {
        t = &s_touched[s_nTouched++];
    } else {
        int replaceIdx = -1;
        const int nLive = Runtime::EnemyCount();
        for (int i = 0; i < s_nTouched; ++i) {
            bool found = false;
            for (int k = 0; k < nLive; ++k) {
                if (Runtime::EnemyBodyAt(k, nullptr) == s_touched[i].body) { found = true; break; }
            }
            if (!found) { replaceIdx = i; break; }
        }
        if (replaceIdx >= 0) {
            t = &s_touched[replaceIdx];
        } else {
            return nullptr; // buffer full with live enemies, do not corrupt slot 0
        }
    }
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
    t->baseDef      = 0.0f;
    t->baseMDef     = 0.0f;
    t->haveDef      = false;
    t->inReturnArmor = false;
    t->baseAtk = t->baseDefC = t->baseMAtk = t->baseMDefC = 0.0f;
    t->curAtk = t->curDefC = t->curMAtk = t->curMDefC = 0.0f;
    t->haveCombat = false;
    t->combatLogged = 0;
    // 85.43: эти два счётчика НЕ сбрасывались, и адрес, переиспользованный
    // движком под новое тело того же вида, приносил чужое состояние: строку
    // ранга и строку всплеска новое тело уже не печатало, а сводка сессии
    // недосчитывала его (поле 85.42: 14 гоблинов в бою, в сводке 12).
    // Новая запись обязана быть чистой — как и все поля выше.
    t->spikeLogged = 0;
    t->rankLogged = 0;
    t->rankCounted = 0;
    // 85.56: новая запись = новый жилец. Поколение растёт, ступень сбрасывается
    // именно в -1: ноль — это ЗАКОННАЯ ступень («новичок»), и забыть про это
    // легко (поймано бы в поле как «у новичка чужая ступень»).
    t->gen = ++s_bodyGenSeq;
    t->rankStep = -1;
    t->rankSize = 0.0f;
    t->rankAtk = 1.0f;
    t->rankResist = 1.0f;
    t->rankStand = 1.0f;
    t->rankUseScale = false;
    t->setIndex = -1;          // минус один, а не ноль: ноль — ЗАКОННЫЙ индекс набора
    t->setX = t->setZ = 0.0f;
    t->haveSetPos = false;
    t->rankCapped = false;
    t->haveRes = false;
    t->resRejected = false;
    t->resTries = 0;
    t->resLogged = 0;
    t->resApplied = 0;
    for (int i = 0; i < kResDebilCount; ++i) t->baseRes[i] = 1.0f;
    t->baseFlinch = 0.0f;
    t->baseKdown  = 0.0f;
    t->combatRollAtk = t->combatRollDef = t->combatRollMAtk = t->combatRollMDef = 1.0f;
    t->charParamOff = 0;
    t->charParamSearched = false;
    return t;
}

// 85.56: ВЫДАТЬ СТУПЕНЬ РОВНО ОДИН РАЗ НА ЖИЗНЬ ОСОБИ.
//
// Здесь же и счёт в сводку сессии (перенесён из блока печати строки: печать —
// это печать, а факт выдачи — это факт; в 85.43 их уже разводили, и вот почему:
// лимит строк на особь не должен влиять на сводку).
static void EnsureRankIssued(Touched* rec, const char* kind, uintptr_t body)
{
    if (!rec || rec->rankStep >= 0) return;

    // ── МЕСТО (85.57) ─────────────────────────────────────────────────────
    // Позицию читаем только здесь и только пока ступень не выдана. Координаты
    // тела лежат по +0x40/+0x44/+0x48 (универсальные поля, см. ANATOMY_EM0100).
    // Ячейка — из ENCOUNTER_MEMORY_DESIGN §1: floor(x/cell), floor(z/cell),
    // высоту не берём.
    float xyz[3] = { 0.0f, 0.0f, 0.0f };
    const bool hasPos = SafeRead((const void*)(body + 0x40), xyz, 12);
    int cx = 0, cz = 0;
    const float cellCm = 100.0f * Runtime::Tempo::PackCellMeters();
    if (hasPos && cellCm > 0.5f) {
        cx = (int)floorf(xyz[0] / cellCm);
        cz = (int)floorf(xyz[2] / cellCm);
    }
    rec->haveSetPos = hasPos;
    rec->setX = hasPos ? xyz[0] : 0.0f;
    rec->setZ = hasPos ? xyz[2] : 0.0f;

    // НАСЛЕДОВАНИЕ НАБОРА. Пачка — это тела, стоящие рядом, а ячейка карты —
    // прямоугольник: пачка на границе получила бы два разных набора и
    // рассыпалась бы на «сброд + ватагу» в одном бою. Поэтому тело, рядом с
    // которым уже есть ОСОБЬ С НАБОРОМ, берёт её набор. Ячейка нужна только
    // для мест, где мы ещё никого не видели.
    int setIdx = -1;
    if (Runtime::Tempo::PackSetsEnabled() && hasPos) {
        const float inh = Runtime::Tempo::PackInheritMeters() * 100.0f;
        float best = inh * inh + 1.0f;
        for (int i = 0; i < s_nTouched; ++i) {
            const Touched& o = s_touched[i];
            if (&o == rec || o.rankStep < 0 || o.setIndex < 0 || !o.haveSetPos) continue;
            const float dx = o.setX - xyz[0], dz = o.setZ - xyz[2];
            const float d2 = dx * dx + dz * dz;
            if (d2 <= best) { best = d2; setIdx = o.setIndex; }
        }
        if (setIdx < 0) setIdx = Runtime::Tempo::PackSetForCell(cx, cz);
    }
    rec->setIndex = setIdx;

    Runtime::Tempo::RankQuery q;
    memset(&q, 0, sizeof(q));
    q.kind     = kind;
    q.body     = body;
    q.gen      = rec->gen;
    q.setIndex = setIdx;
    q.cellX    = cx;
    q.cellZ    = cz;
    q.hasCell  = hasPos;

    int   step = -1;
    float sz = 0.0f, atk = 1.0f, res = 1.0f, stand = 1.0f;
    if (!Runtime::Tempo::RankPickFor(q, &step, &sz, &atk, &res, &stand))
        return;                           // вид не под лестницей или она выключена

    // ── ПРЕДЕЛ «ОДИН МИНИ-БОСС НА МЕСТО» (85.57) ──────────────────────────
    // До наборов двойной мини-босс был лотереей 2% и почти не встречался. С
    // наборами у места может стоять «охота», где мини-боссу отдано 10% веса, —
    // и два мини-босса в одной пачке стали бы обычным делом. Понижаем особь на
    // ступень (не отменяем): место остаётся опасным, но не «два босса в кустах».
    if (hasPos && step == Runtime::Tempo::kRankSteps - 1) {
        const int limit = Runtime::Tempo::PackMinibossPerPack();
        if (limit > 0 && Runtime::Tempo::CellMinibossCount(cx, cz) >= limit) {
            float s2 = 0.0f, a2 = 1.0f, r2 = 1.0f, st2 = 1.0f;
            if (Runtime::Tempo::RankNumbers(kind, step - 1, &s2, &a2, &r2, &st2)) {
                step = step - 1;
                sz = s2; atk = a2; res = r2; stand = st2;
                rec->rankCapped = true;
                logFile << "EnemyTuner: miniboss capped at cell " << cx << "," << cz
                        << " (limit " << limit << ") -> " << Runtime::Tempo::RankName(step)
                        << " for 0x" << std::hex << (unsigned)body << std::dec
                        << std::endl;
            }
        } else {
            Runtime::Tempo::NoteCellMiniboss(cx, cz);
        }
    }

    rec->rankStep    = step;
    rec->rankSize    = sz;
    rec->rankAtk     = atk;
    rec->rankResist  = res;
    rec->rankStand   = stand;
    rec->rankUseScale = Runtime::Tempo::RankScaleEnabled(kind);
    if (!rec->rankCounted) {
        rec->rankCounted = 1;
        Runtime::Tempo::NoteRankIssued(kind, step);
        // 85.58: и счёт по наборам — иначе в итоге сессии видно только ступени,
        // а «какой сет где стоял» приходится вычитывать из строк ранга.
        if (rec->setIndex >= 0) Runtime::Tempo::NotePackSetBody(rec->setIndex);
    }
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
static int ApplyLeash(uintptr_t body, Touched* rec, float scale, const char* kind)
{
    if (!rec) return 0;
    // Исключаем подчасти боссов (например uEm5200_00 голова козла, uEm5200_01 хвост змеи)
    if (kind && (strstr(kind, "_00") || strstr(kind, "_01") || strstr(kind, "_02") || strstr(kind, "_03")))
        return 0;

    uintptr_t base = 0;
    if (rec->charParamSearched && rec->charParamOff) {
        base = body + rec->charParamOff;
        if (!LooksLikeCharParam(base)) {
            // stale cache (body reallocated or not ready) — re-search
            rec->charParamOff = 0;
            base = 0;
        }
    }
    if (!base) {
        // first time or previous search failed — try to find
        uintptr_t cand = body + kCharParamOff;
        if (LooksLikeCharParam(cand)) {
            base = cand;
            rec->charParamOff = kCharParamOff;
        } else {
            const TypeAtlas::Info* ti = kind ? TypeAtlas::FindByName(kind) : nullptr;
            const uint32_t bSize = (ti && ti->size) ? ti->size : 29000;
            base = FindCharParam(body, bSize);
            rec->charParamOff = base ? (uint32_t)(base - body) : 0;
        }
        rec->charParamSearched = true;
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
    if (wantAct < 1.0f) wantAct = 1.0f;
    if (wantAct > 3600.0f) wantAct = 3600.0f;
    if (wantDur < 1.0f) wantDur = 1.0f;
    if (wantDur > 3600.0f) wantDur = 3600.0f;

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

// -------------------------------------------------------- Combat stats (audit 2026-09-21 §8) ---
// cCharParamEnemy +0x0C/+0x10/+0x14/+0x18 — урон и броня.
// План из аудита: транзакция validate->write->readback->WATCH, как у scale/leash,
// + per-body roll 0.9..1.1 чтобы в паке не было одинаковых мобов.
//
// ВНИМАНИЕ: Sanctuary трогает защиту при возврате. Чтобы не драться за поле,
// ApplyCombatStats пишет боевую базу, а Sanctuary поверх неё умножает на armorMult
// и при выходе восстанавливает боевую, а не ваниль.
static float CombatRoll(uintptr_t body, uint32_t salt)
{
    // детерминированный ролл 0.9..1.1 на особь+стат
    uint32_t h = (uint32_t)(body >> 3) ^ salt;
    h ^= h >> 13; h *= 0x5BD1E995u; h ^= h >> 15;
    float t = (float)(h & 0xFFFF) / 65535.0f; // 0..1
    return 0.9f + t * 0.2f; // 0.9..1.1
}

// 85.34: spikeAtk/spikeMAtk приходят из оболочки Tempo (их задаёт директор).
// Всплеск — ещё один множитель В ТОЙ ЖЕ формуле, поэтому писатель остаётся один:
// ini-множитель, ролл особи и адреналин сходятся в одном значении, а не дерутся
// за поле. Ваниль — нижний порог: всплеск никогда не ниже 1.0.
static float ClampSpike(float v)
{
    if (!(v == v)) return 1.0f;                       // NaN из битой памяти
    if (v < 1.0f) return 1.0f;                        // пол: не хуже ванили
    if (v > EntityCfg::kAdrenalineMax) return EntityCfg::kAdrenalineMax;
    return v;
}

// 85.44: КРЕПОСТЬ РАНГА — сопротивления наложениям и устойчивость к сбиванию.
//
// НАПРАВЛЕНИЕ ПОЛЕЙ — то, что здесь надо знать точно, и здесь я ошибался.
//
// Первая версия (85.44) предполагала, что сопротивления наложениям лежат
// множителем получаемого эффекта («крепче = делить»), как элементный урон. Живое
// чтение в поле 85.44 это опровергло: у гоблина 耐毒 = 1000, 耐延焼 = 300,
// 耐氷漬け = 800, 耐敵化 = 10000 — это ЗАПАСЫ, а не множители. И они растут
// вместе с крепостью вида: у хобгоблина те же поля 3000 / 2000 / 3500, у сауриана
// к яду 10000 (саурианы к яду стойки по лору), у волка к яду всего 600. Умножаем
// число — особь становится крепче. Значит для ВСЕХ 22 полей (20 сопротивлений +
// отшатывание + сбивание) «крепче» = УМНОЖАТЬ, а элементный урон — единственная
// семья, где «крепче» = меньше, и её мы не трогаем вообще.
//
// В ini ключ читается как «во сколько раз крепче» (1.0 = ваниль), поэтому
// направление по-прежнему целиком на совести этого кода, а не настройки.
//
// Опорные числа из файла игры (em0100_cmn.prp и соседние), для калибровки:
//   поле         гоблин  хоб    волк   сауриан  гарпия
//   яд             1000   3000    600    10000     600
//   горение         300   2000    400     1000     500
//   заморозка       800   3500    800      500    1000
//   тьма (урон)     0.6    0.6    1.0      1.0     1.0
//   отшатывание     100    100    100      100     100  (в .prp; у хоба .rst 450)
//   сбивание        100    100    100      100     100  (в .prp; у хоба .rst 650)
// 85.53: ЭТАЛОН ЗАПАСОВ — НА ВИД, А НЕ НА ТЕЛО.
//
// Поле 85.52 показало, чем это кончается. У одного гоблина запасы вышли
// 1822 / 169 / 547, то есть ровно ×1.35 ПОВЕРХ ×1.35. Как: слот тела
// переиспользуется, запись особи вытесняется из таблицы (128 мест на три боя),
// при повторном появлении того же адреса «ванильными» были приняты НАШИ ЖЕ
// умноженные числа, и множитель применился второй раз. Тот же класс ошибки,
// что когда-то у боевых статов, и лечится так же — эталоном на вид.
//
// Правильное поведение: первый носитель вида в сессии задаёт эталон (в этот
// момент там точно ванильные числа), все остальные особи вида берут эталон,
// что бы ни лежало в их слоте. Плюс к этому ниже убран ранний выход для
// новичка: если новичку достался слот с чужими умноженными запасами, они
// теперь приводятся к ванили, а не остаются на нём. «Новичок = ровно ваниль»
// должно быть верно и для грязного слота.
struct SpeciesPoolBase {
    char  kind[24];
    float pools[kResDebilCount];
    float flinch, kdown;
    bool  have;
};
static SpeciesPoolBase s_speciesPools[32];
static int             s_nSpeciesPools = 0;

static SpeciesPoolBase* FindSpeciesPools(const char* kind)
{
    if (!kind || !kind[0]) return nullptr;
    for (int i = 0; i < s_nSpeciesPools; ++i)
        if (!strcmp(s_speciesPools[i].kind, kind)) return &s_speciesPools[i];
    return nullptr;
}

static SpeciesPoolBase* AddSpeciesPools(const char* kind)
{
    if (!kind || !kind[0]) return nullptr;
    if (s_nSpeciesPools >= 32) return nullptr;
    SpeciesPoolBase& sp = s_speciesPools[s_nSpeciesPools++];
    memset(&sp, 0, sizeof(sp));
    lstrcpynA(sp.kind, kind, sizeof(sp.kind));
    return &sp;
}

static int ApplyRankToughness(uintptr_t body, Touched* rec, uintptr_t base,
                              const char* kind, float resist, float stand)
{
    if (!rec || !base) return 0;
    if (rec->resRejected) return 0;

    if (!rec->haveRes) {
        // Санити-гейт: если карта смещений разъедется, мы прочитаем мусор
        // (NaN, отрицательные, гигантские числа). Тогда лучше не трогать вовсе —
        // и сказать об этом в лог, чем писать случайные байты в живого монстра.
        //
        // НО: неудача первой попытки ещё не значит, что карта плохая. В поле 85.45
        // десять гоблинов первой пачки, прочитанные сразу после загрузки зоны,
        // отдали не-числа, а подкрепления в том же бою читались правильно. Поэтому
        // пробуем несколько раз, а в лог несём ПОДРОБНОСТЬ (какое поле и какое
        // значение), чтобы разбор не гадал. Приговор — только после всех попыток.
        bool  ok = true;
        int   badIdx = -1;
        float badVal = 0.0f;
        float sum = 0.0f;
        for (int i = 0; i < kResDebilCount; ++i) {
            float v = 0.0f;
            if (!SafeRead((const void*)(base + kResDebil[i].off), &v, 4)) {
                ok = false; badIdx = i; badVal = 0.0f; break;
            }
            if (!(v == v) || v < 0.0f || v > 10000.0f) {
                ok = false; badIdx = i; badVal = v; break;
            }
            rec->baseRes[i] = v;
            sum += v;
        }
        if (ok) {
            if (!SafeRead((const void*)(base + kFldFlinch), &rec->baseFlinch, 4) ||
                !SafeRead((const void*)(base + kFldKdown),  &rec->baseKdown,  4)) {
                ok = false; badIdx = -2;
            } else if (!(rec->baseFlinch == rec->baseFlinch) || rec->baseFlinch < 0.0f
                       || rec->baseFlinch > 100000.0f) {
                ok = false; badIdx = -2; badVal = rec->baseFlinch;
            } else if (!(rec->baseKdown == rec->baseKdown) || rec->baseKdown < 0.0f
                       || rec->baseKdown > 100000.0f) {
                ok = false; badIdx = -3; badVal = rec->baseKdown;
            }
        }
        if (ok && sum <= 0.0f) { ok = false; badIdx = -4; }  // все нули: блок ещё пуст

        if (!ok) {
            ++rec->resTries;
            if (rec->resTries == 1) {
                char l[240];
                if (badIdx == -4)
                    sprintf_s(l, "resist %s 0x%08X not ready (block still empty) - will retry",
                              kind ? kind : "?", (unsigned)body);
                else
                    sprintf_s(l, "resist %s 0x%08X not ready (field %d val %.2f) - will retry",
                              kind ? kind : "?", (unsigned)body, badIdx, badVal);
                logFile << "EnemyTuner: " << l << std::endl;
            }
            if (rec->resTries < kResMaxTries) return 0;   // попробуем на следующем тике
            rec->haveRes = true;
            rec->resRejected = true;
            char l[240];
            if (badIdx == -4)
                sprintf_s(l, "resist %s 0x%08X map rejected after %d tries"
                             " (still empty) - body left untouched",
                          kind ? kind : "?", (unsigned)body, rec->resTries);
            else
                sprintf_s(l, "resist %s 0x%08X map rejected after %d tries"
                             " (field %d val %.2f) - body left untouched",
                          kind ? kind : "?", (unsigned)body, rec->resTries, badIdx, badVal);
            logFile << "EnemyTuner: " << l << std::endl;
            return 0;
        }
        rec->haveRes = true;

        // 85.53: эталон вида. Первый носитель вида в сессии его задаёт, все
        // последующие берут готовый (см. комментарий у SpeciesPoolBase).
        SpeciesPoolBase* sp = FindSpeciesPools(kind);
        if (!sp) sp = AddSpeciesPools(kind);
        if (sp && !sp->have) {
            for (int i = 0; i < kResDebilCount; ++i) sp->pools[i] = rec->baseRes[i];
            sp->flinch = rec->baseFlinch;
            sp->kdown  = rec->baseKdown;
            sp->have   = true;
            char l[300];
            sprintf_s(l, "resist base %s reference taken (pois %.0f froz %.0f burn %.0f"
                         " flinch %.0f kdown %.0f) - every body of this kind uses it",
                      kind ? kind : "?", sp->pools[0], sp->pools[10], sp->pools[11],
                      sp->flinch, sp->kdown);
            logFile << "EnemyTuner: " << l << std::endl;
        } else if (sp) {
            // Слот мог достаться от прошлого жильца: тогда в теле лежат НЕ
            // ванильные числа. Печатаем один раз на особь — и всё равно
            // работаем от эталона вида.
            bool dirty = false;
            for (int i = 0; i < kResDebilCount; ++i)
                if (!NearlyEq(rec->baseRes[i], sp->pools[i])) { dirty = true; break; }
            if (!dirty && (!NearlyEq(rec->baseFlinch, sp->flinch)
                           || !NearlyEq(rec->baseKdown, sp->kdown)))
                dirty = true;
            if (dirty && rec->resLogged < 1) {
                char l[300];
                sprintf_s(l, "resist %s 0x%08X slot was dirty (read pois %.0f froz %.0f"
                             " burn %.0f) - using species reference instead",
                          kind ? kind : "?", (unsigned)body, rec->baseRes[0],
                          rec->baseRes[10], rec->baseRes[11]);
                logFile << "EnemyTuner: " << l << std::endl;
            }
            for (int i = 0; i < kResDebilCount; ++i) rec->baseRes[i] = sp->pools[i];
            rec->baseFlinch = sp->flinch;
            rec->baseKdown  = sp->kdown;
        }
    }

    // Родные числа игры в лог — один раз на особь. Это и есть цель этой сборки:
    // увидеть настоящие сопротивления гоблина, а не гадать по вики.
    if (rec->resLogged < 1) {
        ++rec->resLogged;
        char line[560];
        int n = 0;
        n += sprintf_s(line + n, sizeof(line) - (size_t)n, "resist %s 0x%08X elem",
                       kind ? kind : "?", (unsigned)body);
        for (int i = 0; i < kElemAnchorCount; ++i) {
            float ev = 0.0f;
            if (SafeRead((const void*)(base + kElemAnchors[i].off), &ev, 4))
                n += sprintf_s(line + n, sizeof(line) - (size_t)n, " %s %.2f",
                               kElemAnchors[i].name, ev);
        }
        float hpv = 0.0f;
        if (SafeRead((const void*)(base + kFldHumanHp), &hpv, 4))
            n += sprintf_s(line + n, sizeof(line) - (size_t)n, " hp %.0f |", hpv);
        else
            n += sprintf_s(line + n, sizeof(line) - (size_t)n, " hp ? |");
        for (int i = 0; i < kResDebilCount && n < (int)sizeof(line) - 60; ++i)
            n += sprintf_s(line + n, sizeof(line) - (size_t)n, " %s %.2f",
                           kResDebil[i].name, rec->baseRes[i]);
        sprintf_s(line + n, sizeof(line) - (size_t)n, " | flinch %.1f kdown %.1f",
                  rec->baseFlinch, rec->baseKdown);
        logFile << "EnemyTuner: " << line << std::endl;
    }

    // Ручек нет — только читаем. Так фича и приезжает: числа видно, бой не тронут.
    //
    // 85.53: РАНЬШЕ ЗДЕСЬ БЫЛ РАННИЙ ВЫХОД для новичка (1.00/1.00). Он верен
    // ровно до тех пор, пока тело заспавнилось в чистый блок. Но слоты
    // переиспользуются, и новичок может получить его от ветерана вместе с его
    // умноженными запасами — тогда «новичок = ваниль» переставало быть правдой.
    // Теперь единица не повод выйти, а повод СВЕРИТЬ: если в теле лежит не
    // эталон, он приводится к ванили. Если лежит эталон — не пишем ничего и в
    // лог не шумим (wrote останется 0).

    int wrote = 0;
    for (int c = 0; c < 2; ++c) {
        uintptr_t b = base + (uintptr_t)c * 0x140;
        if (c && !LooksLikeCharParam(b)) break;
        for (int i = 0; i < kResDebilCount; ++i) {
            // Поля, которые игра уже сделала почти иммунными (одержимость и
            // печать навыков — по 10000), не трогаем: умножать их бессмысленно,
            // а запись ради записи — лишний риск. Порог 5000 отделяет их от
            // рабочих полей (у гоблина всё остальное 300..1000).
            if (rec->baseRes[i] >= 5000.0f) continue;
            const float want = rec->baseRes[i] * resist;     // крепче = больше запас
            float cur = 0.0f;
            if (!SafeRead((const void*)(b + kResDebil[i].off), &cur, 4)) continue;
            if (NearlyEq(cur, want)) continue;
            if (SafeWrite((void*)(b + kResDebil[i].off), &want, 4)) ++wrote;
        }
        const float wantF = rec->baseFlinch * stand;          // крепче = больше запас
        const float wantK = rec->baseKdown  * stand;
        float cur = 0.0f;
        if (SafeRead((const void*)(b + kFldFlinch), &cur, 4) && !NearlyEq(cur, wantF))
            if (SafeWrite((void*)(b + kFldFlinch), &wantF, 4)) ++wrote;
        if (SafeRead((const void*)(b + kFldKdown), &cur, 4) && !NearlyEq(cur, wantK))
            if (SafeWrite((void*)(b + kFldKdown), &wantK, 4)) ++wrote;
    }

    if (wrote) {
        ++rec->resApplied;
        // Логируем первые разы и далее редко: движок может откатывать правку
        // каждый тик, и тогда лог превратится в поток.
        if (rec->resApplied <= 5 || (rec->resApplied % 32) == 0) {
            // Обратное чтение ПОСЛЕ записи. Раньше здесь стояло одно поле с
            // меткой "burn", но по этому смещению (0x07C) лежит ЗАМОРОЗКА —
            // метка врала, и в поле 85.47 строка выглядела так, будто горение
            // умножилось не на своё число. Теперь печатаем обе половины пары
            // плюс яд и оба поля устойчивости: яд/заморозка/горение — это
            // рабочие поля с разными родными числами (1000/800/300 у гоблина),
            // по ним сразу видно, что умножено верно.
            float rbPois = 0.0f, rbFroz = 0.0f, rbBurn = 0.0f, rbFl = 0.0f, rbKd = 0.0f;
            SafeRead((const void*)(base + 0x054), &rbPois, 4);
            SafeRead((const void*)(base + 0x07C), &rbFroz, 4);
            SafeRead((const void*)(base + 0x080), &rbBurn, 4);
            SafeRead((const void*)(base + kFldFlinch), &rbFl, 4);
            SafeRead((const void*)(base + kFldKdown), &rbKd, 4);
            char l[280];
            sprintf_s(l, "resist applied %s 0x%08X res x%.2f stand x%.2f wrote %d"
                         " readback pois %.0f froz %.0f burn %.0f flinch %.0f kdown %.0f"
                         " (run %d)",
                      kind ? kind : "?", (unsigned)body, resist, stand, wrote,
                      rbPois, rbFroz, rbBurn, rbFl, rbKd, rec->resApplied);
            logFile << "EnemyTuner: " << l << std::endl;
        }
    }
    return wrote;
}

static int ApplyCombatStats(uintptr_t body, Touched* rec, const EntityCfg::Tuning& t,
                            const char* kind, float spikeAtk, float spikeMAtk)
{
    if (!rec) return 0;
    if (kind && (strstr(kind, "_00") || strstr(kind, "_01") || strstr(kind, "_02") || strstr(kind, "_03")))
        return 0; // подчасти боссов — не трогаем

    // если все множители ваниль и мы уже применяли — можно пропустить?
    // Нет: нужен per-body roll даже при 1.0, чтобы мобы не были клонами.
    // Поэтому проверяем только что не нули.

    uintptr_t base = 0;
    if (rec->charParamSearched && rec->charParamOff) {
        base = body + rec->charParamOff;
        if (!LooksLikeCharParam(base)) {
            // stale cache (body reallocated or not ready) — re-search
            rec->charParamOff = 0;
            base = 0;
        }
    }
    if (!base) {
        // first time or previous search failed — try to find
        uintptr_t cand = body + kCharParamOff;
        if (LooksLikeCharParam(cand)) {
            base = cand;
            rec->charParamOff = kCharParamOff;
        } else {
            const TypeAtlas::Info* ti = kind ? TypeAtlas::FindByName(kind) : nullptr;
            const uint32_t bSize = (ti && ti->size) ? ti->size : 29000;
            base = FindCharParam(body, bSize);
            rec->charParamOff = base ? (uint32_t)(base - body) : 0;
        }
        rec->charParamSearched = true;
        if (!base) return 0;
    }

    float curAtk=0, curDef=0, curMAtk=0, curMDef=0;
    if (!SafeRead((const void*)(base + kFldAttack), &curAtk, 4)) return 0;
    if (!SafeRead((const void*)(base + kFldDefense), &curDef, 4)) return 0;
    if (!SafeRead((const void*)(base + kFldMagickAttack), &curMAtk, 4)) return 0;
    if (!SafeRead((const void*)(base + kFldMagickDefense), &curMDef, 4)) return 0;

    // Species base lookup (for reload protection)
    uint16_t emIdForBase = 0xFFFF;
    if (kind) {
        // EmIdFromKind is static, need to call via function - we have emId from caller? 
        // We have kind string, parse emId via existing helper EmIdFromKind if available.
        // To avoid forward decl issues, we will try to find species base by scanning for matching base already stored
        // and if not found, we will use cur as candidate for species base if it looks vanilla.
    }

    if (!rec->haveCombat) {
        // санити: ванильные статы обычно 0..50000, не NaN
        if (!(curAtk >= 0.0f && curAtk < 50000.0f)) return 0;
        if (!(curDef >= 0.0f && curDef < 50000.0f)) return 0;
        if (!(curMAtk >= 0.0f && curMAtk < 50000.0f)) return 0;
        if (!(curMDef >= 0.0f && curMDef < 50000.0f)) return 0;

        // детерминированные роллы на особь — считаем ДО выбора базы чтобы
        // иметь roll для проверки уже-умноженного cur.
        rec->combatRollAtk  = CombatRoll(body, 0xA11CE5u);
        rec->combatRollDef  = CombatRoll(body, 0xDEF011u);
        rec->combatRollMAtk = CombatRoll(body, 0x5A7AC4u);
        rec->combatRollMDef = CombatRoll(body, 0xD0A55Eu);

        // Попытка найти species base
        uint16_t emId = 0xFFFF;
        // EmIdFromKind is defined later, but we can parse kind like uEm0100 -> 100
        if (kind && kind[0]=='u' && kind[1]=='E' && kind[2]=='m') {
            // uEm0100 -> 100, uEm0200 -> 200, etc.
            // kind+3 points to digits, may have _20 suffix
            int v=0;
            for (int i=3; kind[i] && kind[i]>='0' && kind[i]<='9' && i<7; ++i) {
                v = v*10 + (kind[i]-'0');
            }
            if (v>0 && v<10000) emId = (uint16_t)v;
        }
        SpeciesCombatBase* spb = (emId!=0xFFFF) ? FindSpeciesBase(emId) : nullptr;
        if (spb && spb->have) {
            // Use species vanilla base, not cur (protects against reload double-mult)
            // If cur is already multiplied (e.g. 512 vs vanilla 250), we will keep want = vanilla*mult*roll = cur, stable.
            rec->baseAtk = spb->atk;
            rec->baseDefC = spb->defC;
            rec->baseMAtk = spb->mAtk;
            rec->baseMDefC = spb->mDefC;
        } else {
            // First time we see this species — cur should be vanilla (game start).
            // If cur looks already multiplied (e.g. >1.8x of what we would expect? we don't know),
            // we try to reverse: if cur / (mult*roll) is plausible, use that as vanilla.
            // Heuristic: if cur > 400 and mult>=1.5, assume cur is already multiplied and recover vanilla.
            float estVanillaAtk = curAtk;
            float estVanillaDef = curDef;
            float multAtk = t.attackMult * rec->combatRollAtk;
            float multDef = t.defenseMult * rec->combatRollDef;
            if (multAtk>1.5f && curAtk>350.0f) {
                float cand = curAtk / multAtk;
                if (cand>=50.0f && cand<1000.0f) estVanillaAtk = cand;
            }
            if (multDef>1.5f && curDef>120.0f) {
                float cand = curDef / multDef;
                if (cand>=10.0f && cand<500.0f) estVanillaDef = cand;
            }
            rec->baseAtk = estVanillaAtk;
            rec->baseDefC = estVanillaDef;
            rec->baseMAtk = curMAtk; // for magick we keep simple for now
            rec->baseMDefC = curMDef;
            // Store as species base for future bodies
            if (emId!=0xFFFF) {
                RememberSpeciesBase(emId, rec->baseAtk, rec->baseDefC, rec->baseMAtk, rec->baseMDefC);
            }
        }
        rec->haveCombat = true;
    }

    // если в Sanctuary — защиту не перезаписываем боевой (её бустит Sanctuary)
    bool inSanct = rec->inReturnArmor;

    // Roll применяется только когда mult != 1.0, чтобы 1.0 оставался ванилью.
    // Если mult == 1.0 — want = base (восстановление ванили).
    float rollAtk  = NearlyEq(t.attackMult, 1.0f)        ? 1.0f : rec->combatRollAtk;
    float rollDef  = NearlyEq(t.defenseMult, 1.0f)       ? 1.0f : rec->combatRollDef;
    float rollMAtk = NearlyEq(t.magickAttackMult, 1.0f)  ? 1.0f : rec->combatRollMAtk;
    float rollMDef = NearlyEq(t.magickDefenseMult, 1.0f) ? 1.0f : rec->combatRollMDef;

    // 85.34: адреналин директора — последний множитель, поверх боевой базы.
    // Порядок намеренный: база вида -> ini вида -> ролл особи -> всплеск приказа.
    const float adrAtk  = ClampSpike(spikeAtk);
    const float adrMAtk = ClampSpike(spikeMAtk);
    if (!rec->spikeLogged && (adrAtk > 1.0001f || adrMAtk > 1.0001f)) {
        rec->spikeLogged = 1;
        char sl[200];
        sprintf_s(sl, "adrenaline attack x%.3f/x%.3f -> %s 0x%08X  atk %.1f -> %.1f  matk %.1f -> %.1f",
                  adrAtk, adrMAtk, kind ? kind : "?", (unsigned)body,
                  rec->baseAtk * t.attackMult * rollAtk,
                  rec->baseAtk * t.attackMult * rollAtk * adrAtk,
                  rec->baseMAtk * t.magickAttackMult * rollMAtk,
                  rec->baseMAtk * t.magickAttackMult * rollMAtk * adrMAtk);
        logFile << "EnemyTuner: " << sl << std::endl;
        lstrcpynA(s_status, sl, sizeof(s_status));
    } else if (rec->spikeLogged && adrAtk <= 1.0001f && adrMAtk <= 1.0001f) {
        rec->spikeLogged = 0;   // всплеск кончился — строка появится снова
    }
    // 85.40: ступень лестницы. Статичный множитель особи, поэтому порядок с
    // адреналином не важен (умножение), но ставим ДО него: «всплеск — последний».
    // 85.56: числа ступени берём ИЗ ЗАПИСИ ТЕЛА (выдано один раз, см. выше).
    EnsureRankIssued(rec, kind, body);
    const bool  ranksOn = (rec->rankStep >= 0);
    const float rankAtk = ranksOn ? rec->rankAtk : 1.0f;
    // 85.41: спецправило «ванильный вожак получает старшую ступень» УБРАНО.
    // Поле 85.40 показало, почему его нельзя оставлять: порог «крупный = вожак»
    // сравнивает ЗАПОМНЕННУЮ базу роста с 1.12, а база после загрузки сейва
    // бывает нашей же прошлой записью (движок откатывает размер). Три гоблина
    // из десяти получили ×1.52 случайно — у одного рост остался 1.12, у двух
    // 0.96, то есть «элита» с ростом новичка. Урок тот же, что и с числами:
    // если признак неотличим — не угадываем, а даём ступень по хешу.
    if (ranksOn && rec->rankLogged < 2) {
        // 85.43: сводка сессии считает выдачу ПРИ НАЗНАЧЕНИИ — это по-прежнему
        // так, просто назначение переехало в EnsureRankIssued. Здесь только
        // строка в лог, и лимит «две на особь» её не касается.
        {
            ++rec->rankLogged;
            char ll[230];
            // 85.52: у видов с rankScale = off размер ступенью НЕ задаётся —
            // печатаем это прямо, иначе в логе «size 1.190» читалось бы как
            // выданный рангом рост, которого на самом деле нет.
            // 85.56: печатаем И ПОКОЛЕНИЕ — по нему видно, что слот сменил
            // жильца (иначе в поле не проверить, что ступень больше не
            // «переезжает» на нового монстра вместе с адресом).
            // 85.57: печатаем НАБОР места — по этой строке видно, какое место
            // какой сет получило («в этой зоне такой набор, в следующей другой»).
            char spart[40], setpart[40], cappart[26];
            if (rec->rankUseScale) sprintf_s(spart, "size %.3f", rec->rankSize);
            else                   lstrcpynA(spart, "size off (vanilla)", sizeof(spart));
            setpart[0] = 0; cappart[0] = 0;
            const char* setn = (rec->setIndex >= 0)
                             ? Runtime::Tempo::PackSetName(rec->setIndex) : nullptr;
            if (setn) sprintf_s(setpart, " set=%s", setn);
            if (rec->rankCapped) lstrcpynA(cappart, " miniboss-capped", sizeof(cappart));
            sprintf_s(ll, "rank %s %s(%d) %s atk x%.2f gen=%u%s%s -> 0x%08X",
                      kind ? kind : "?", Runtime::Tempo::RankName(rec->rankStep),
                      rec->rankStep, spart, rec->rankAtk, rec->gen,
                      setpart, cappart, (unsigned)body);
            logFile << "EnemyTuner: " << ll << std::endl;
            lstrcpynA(s_status, ll, sizeof(s_status));
        }
    }

    // 85.44: крепость ранга. По умолчанию ручки = 1.0, то есть НИЧЕГО не
    // пишется — сборка только читает родные сопротивления и говорит их в лог.
    if (ranksOn) ApplyRankToughness(body, rec, base, kind, rec->rankResist, rec->rankStand);

    float wantAtk  = rec->baseAtk  * t.attackMult        * rollAtk  * adrAtk
                   * (ranksOn ? rankAtk : 1.0f);
    float wantDef  = rec->baseDefC * t.defenseMult       * rollDef;
    float wantMAtk = rec->baseMAtk * t.magickAttackMult  * rollMAtk * adrMAtk;
    float wantMDef = rec->baseMDefC* t.magickDefenseMult * rollMDef;

    // NaN protection — hot-reload может подсунуть NaN из полузаписанного ini
    // или из повреждённой памяти. NaN в charParam = краш движка при расчёте урона.
    if (!(wantAtk==wantAtk)) wantAtk = rec->baseAtk;
    if (!(wantDef==wantDef)) wantDef = rec->baseDefC;
    if (!(wantMAtk==wantMAtk)) wantMAtk = rec->baseMAtk;
    if (!(wantMDef==wantMDef)) wantMDef = rec->baseMDefC;
    if (!(rec->baseAtk==rec->baseAtk) || !(rec->baseDefC==rec->baseDefC) ||
        !(rec->baseMAtk==rec->baseMAtk) || !(rec->baseMDefC==rec->baseMDefC)) {
        return 0; // база битая — не пишем
    }

    // кламп абсолютов чтобы не взорвать баланс
    if (wantAtk < 0.0f) wantAtk = 0.0f; if (wantAtk > 50000.0f) wantAtk = 50000.0f;
    if (wantDef < 0.0f) wantDef = 0.0f; if (wantDef > 50000.0f) wantDef = 50000.0f;
    if (wantMAtk < 0.0f) wantMAtk = 0.0f; if (wantMAtk > 50000.0f) wantMAtk = 50000.0f;
    if (wantMDef < 0.0f) wantMDef = 0.0f; if (wantMDef > 50000.0f) wantMDef = 50000.0f;

    // если всё уже стоит — ничего не делаем (избегаем лишних записей)
    bool needAtk = !NearlyEq(curAtk, wantAtk);
    bool needDef = !inSanct && !NearlyEq(curDef, wantDef);
    bool needMAtk= !NearlyEq(curMAtk, wantMAtk);
    bool needMDef= !inSanct && !NearlyEq(curMDef, wantMDef);
    if (!needAtk && !needDef && !needMAtk && !needMDef) {
        // обновим cur-кэш даже если не писали (на случай если Sanctuary менял защиту)
        rec->curAtk = wantAtk; rec->curMAtk = wantMAtk;
        if (!inSanct) { rec->curDefC = wantDef; rec->curMDefC = wantMDef; }
        return 0;
    }

    int wrote = 0;
    for (int c = 0; c < 2; ++c) {
        uintptr_t b = base + (uintptr_t)c * 0x140;
        if (c && !LooksLikeCharParam(b)) break;
        float cur = 0;
        if (needAtk && SafeRead((const void*)(b + kFldAttack), &cur, 4) && !NearlyEq(cur, wantAtk)) {
            if (SafeWrite((void*)(b + kFldAttack), &wantAtk, 4)) ++wrote;
        }
        if (needDef && SafeRead((const void*)(b + kFldDefense), &cur, 4) && !NearlyEq(cur, wantDef)) {
            if (SafeWrite((void*)(b + kFldDefense), &wantDef, 4)) ++wrote;
        }
        if (needMAtk && SafeRead((const void*)(b + kFldMagickAttack), &cur, 4) && !NearlyEq(cur, wantMAtk)) {
            if (SafeWrite((void*)(b + kFldMagickAttack), &wantMAtk, 4)) ++wrote;
        }
        if (needMDef && SafeRead((const void*)(b + kFldMagickDefense), &cur, 4) && !NearlyEq(cur, wantMDef)) {
            if (SafeWrite((void*)(b + kFldMagickDefense), &wantMDef, 4)) ++wrote;
        }
    }

    // readback WATCH (audit §8): проверяем что держится
    if (wrote) {
        float rbAtk=0, rbDef=0, rbMAtk=0, rbMDef=0;
        bool ok = SafeRead((const void*)(base + kFldAttack), &rbAtk, 4)
               && SafeRead((const void*)(base + kFldDefense), &rbDef, 4)
               && SafeRead((const void*)(base + kFldMagickAttack), &rbMAtk, 4)
               && SafeRead((const void*)(base + kFldMagickDefense), &rbMDef, 4);
        if (!ok || (!inSanct && (!NearlyEq(rbDef, wantDef) || !NearlyEq(rbMDef, wantMDef)))
                || !NearlyEq(rbAtk, wantAtk) || !NearlyEq(rbMAtk, wantMAtk)) {
            // движок откатил — залогируем как WATCH drift
            if (rec->combatLogged < 3) {
                char ll[240];
                sprintf_s(ll, "CombatStats WATCH drift on %s 0x%08X: want (%.1f,%.1f,%.1f,%.1f) got (%.1f,%.1f,%.1f,%.1f)",
                          kind ? kind : "?", (unsigned)body, wantAtk, wantDef, wantMAtk, wantMDef,
                          rbAtk, rbDef, rbMAtk, rbMDef);
                logFile << "EnemyTuner: " << ll << std::endl;
            }
        }
        rec->curAtk = wantAtk; rec->curDefC = wantDef;
        rec->curMAtk = wantMAtk; rec->curMDefC = wantMDef;
        if (rec->combatLogged < 2) {
            ++rec->combatLogged;
            char ll[260];
            sprintf_s(ll, "CombatStats x%.2f/%.2f/%.2f/%.2f (roll %.2f/%.2f/%.2f/%.2f) -> %s 0x%08X  atk %.1f->%.1f def %.1f->%.1f matk %.1f->%.1f mdef %.1f->%.1f",
                      t.attackMult, t.defenseMult, t.magickAttackMult, t.magickDefenseMult,
                      rec->combatRollAtk, rec->combatRollDef, rec->combatRollMAtk, rec->combatRollMDef,
                      kind ? kind : "?", (unsigned)body,
                      rec->baseAtk, wantAtk, rec->baseDefC, wantDef, rec->baseMAtk, wantMAtk, rec->baseMDefC, wantMDef);
            logFile << "EnemyTuner: " << ll << std::endl;
            lstrcpynA(s_status, ll, sizeof(s_status));
        }
    }
    return wrote;
}

// -------------------------------------------------------- DDON Sanctuary ---
//
// В Dragon's Dogma Online монстры при возврате на спавн получали статус
// Sanctuary: многократный буст защиты + ускоренный бег домой, чтобы игроки
// не расстреливали их безнаказанно в спину.
//
// Когда монстр входит в состояние возврата (Escape/Return/Retreat):
//   1. Умножаем физическую (+0x10) и магическую (+0x18) защиту в returnArmorMult раз.
//   2. Ускоряем скорость бега домой через Tempo::SetOverride(returnSpeed).
// Как только монстр выходит из возврата (снова вступает в бой или дошёл до лагеря):
//   - Восстанавливаем ванильные значения защиты и снимаем оверрайд темпа.
static bool IsReturningState(const char* act)
{
    if (!act || !act[0]) return false;
    return strstr(act, "Return") != nullptr
        || strstr(act, "Escape") != nullptr
        || strstr(act, "Retreat") != nullptr
        || !strcmp(act, "cEm0100ActEscapeStart");
}

static int ApplyReturnSanctuary(uintptr_t body, Touched* rec, const EntityCfg::Tuning& t, const char* kind)
{
    if (!rec) return 0;
    // Исключаем подчасти боссов
    if (kind && (strstr(kind, "_00") || strstr(kind, "_01") || strstr(kind, "_02") || strstr(kind, "_03")))
        return 0;

    uintptr_t base = 0;
    if (rec->charParamSearched && rec->charParamOff) {
        base = body + rec->charParamOff;
        if (!LooksLikeCharParam(base)) {
            // stale cache (body reallocated or not ready) — re-search
            rec->charParamOff = 0;
            base = 0;
        }
    }
    if (!base) {
        // first time or previous search failed — try to find
        uintptr_t cand = body + kCharParamOff;
        if (LooksLikeCharParam(cand)) {
            base = cand;
            rec->charParamOff = kCharParamOff;
        } else {
            const TypeAtlas::Info* ti = kind ? TypeAtlas::FindByName(kind) : nullptr;
            const uint32_t bSize = (ti && ti->size) ? ti->size : 29000;
            base = FindCharParam(body, bSize);
            rec->charParamOff = base ? (uint32_t)(base - body) : 0;
        }
        rec->charParamSearched = true;
        if (!base) return 0;
    }

    float curDef = 0, curMDef = 0;
    if (!SafeRead((const void*)(base + kFldDefense), &curDef, 4)) return 0;
    if (!SafeRead((const void*)(base + kFldMagickDefense), &curMDef, 4)) return 0;

    // Запоминаем ванильную базу брони до первого применения
    if (!rec->haveDef && !rec->inReturnArmor) {
        if (curDef >= 0.0f && curDef < 50000.0f && curMDef >= 0.0f && curMDef < 50000.0f) {
            rec->baseDef = curDef;
            rec->baseMDef = curMDef;
            rec->haveDef = true;
        }
    }
    if (!rec->haveDef) return 0;

    if (!t.returnArmor) {
        if (rec->inReturnArmor) {
            // Audit §8 fix: восстанавливаем боевую защиту (с mult+roll), а не ваниль,
            // иначе CombatStats сбрасывался бы при выходе из Sanctuary.
            float restoreDef = rec->haveCombat ? rec->curDefC : rec->baseDef;
            float restoreMDef = rec->haveCombat ? rec->curMDefC : rec->baseMDef;
            for (int c = 0; c < 2; ++c) {
                uintptr_t b = base + (uintptr_t)c * 0x140;
                if (c && !LooksLikeCharParam(b)) break;
                SafeWrite((void*)(b + kFldDefense), &restoreDef, 4);
                SafeWrite((void*)(b + kFldMagickDefense), &restoreMDef, 4);
            }
            Runtime::Tempo::ClearOverride(body);
            rec->inReturnArmor = false;
        }
        return 0;
    }

    char liveAct[48] = {};
    Runtime::ReadLiveAct(body, liveAct, sizeof(liveAct));
    const bool returning = IsReturningState(liveAct);

    float armorMult = t.returnArmorMult;
    if (armorMult < 1.0f) armorMult = 1.0f;
    if (armorMult > 20.0f) armorMult = 20.0f;

    // Audit §8: база для Sanctuary — боевая защита (с mult+roll), если уже есть,
    // иначе ваниль. Так CombatStats и Sanctuary не дерутся за поле.
    float combatDef = rec->haveCombat ? rec->curDefC : rec->baseDef;
    float combatMDef = rec->haveCombat ? rec->curMDefC : rec->baseMDef;
    if (combatDef <= 0.0f) combatDef = rec->baseDef;
    if (combatMDef <= 0.0f) combatMDef = rec->baseMDef;

    float wantDef = returning ? (combatDef * armorMult) : combatDef;
    float wantMDef = returning ? (combatMDef * armorMult) : combatMDef;

    int wrote = 0;
    if (returning && !rec->inReturnArmor) {
        // Вход в Sanctuary: бустим броню + ускоряем отход
        for (int c = 0; c < 2; ++c) {
            uintptr_t b = base + (uintptr_t)c * 0x140;
            if (c && !LooksLikeCharParam(b)) break;
            if (SafeWrite((void*)(b + kFldDefense), &wantDef, 4)) ++wrote;
            if (SafeWrite((void*)(b + kFldMagickDefense), &wantMDef, 4)) ++wrote;
        }
        float speed = t.returnSpeed;
        if (speed < 1.0f) speed = 1.0f;
        if (speed > 1.40f) speed = 1.40f;
        if (speed > 1.01f) {
            Runtime::Tempo::SetOverride(body, speed, 1.0f, 4000);
        }
        rec->inReturnArmor = true;
        char l[200];
        sprintf_s(l, "Sanctuary: ENGAGED on %s 0x%08X (act=%s, def %.1f->%.1f, mdef %.1f->%.1f, speed x%.2f)",
                  kind ? kind : "?", (unsigned)body, liveAct[0] ? liveAct : "?",
                  rec->baseDef, wantDef, rec->baseMDef, wantMDef, speed);
        logFile << "EnemyTuner: " << l << std::endl;
        lstrcpynA(s_status, l, sizeof(s_status));
    } else if (!returning && rec->inReturnArmor) {
        // Выход из Sanctuary: восстанавливаем боевую защиту, не ваниль (audit §8)
        float restoreDef = rec->haveCombat ? rec->curDefC : rec->baseDef;
        float restoreMDef = rec->haveCombat ? rec->curMDefC : rec->baseMDef;
        for (int c = 0; c < 2; ++c) {
            uintptr_t b = base + (uintptr_t)c * 0x140;
            if (c && !LooksLikeCharParam(b)) break;
            if (SafeWrite((void*)(b + kFldDefense), &restoreDef, 4)) ++wrote;
            if (SafeWrite((void*)(b + kFldMagickDefense), &restoreMDef, 4)) ++wrote;
        }
        Runtime::Tempo::ClearOverride(body);
        rec->inReturnArmor = false;
        char l[200];
        sprintf_s(l, "Sanctuary: RELEASED on %s 0x%08X (restored def %.1f, mdef %.1f)",
                  kind ? kind : "?", (unsigned)body, rec->baseDef, rec->baseMDef);
        logFile << "EnemyTuner: " << l << std::endl;
        lstrcpynA(s_status, l, sizeof(s_status));
    }
    return wrote;
}

static void TickOneBody(uintptr_t body, const char* kind)
{
    // P0-2 / hot-reload safety: stale body must not be touched
    if (!body) return;
    if (!Runtime::Mem::RegionOk(body, 0x70)) return; // need at least +0x60..0x68 scale
    // Тело под ручным удержанием (кнопка FORCE) — не трогаем.
    // Иначе тик затирает результат нажатия и мы сами себе создаём "реверты".
    if (s_holdBody && body == s_holdBody) return;

    uint16_t emId = EmIdFromKind(kind);
    if (emId == 0xFFFF) return;

    const EntityCfg::Tuning& t = EntityCfg::For(emId);
    if (!t.enabled) return;

    Touched* rec0 = FindTouched(body);
    if (!rec0) {
        rec0 = RememberTouched(body, 1.0f);
    } else if (rec0->species && rec0->species != emId) {
        // 85.26: ЭТОТ АДРЕС ЗАНЯЛ ДРУГОЙ ВИД. Так бывает постоянно: тело умерло,
        // память освободилась и её отдали новому монстру — в поле 85.25 гоблин
        // встал в слот волка. Все накопленные значения записи принадлежат
        // прошлому жильцу (его ванильные статы, размер, поводок, кэш смещения),
        // и новый вид получал их в наследство: атака не своя, защита не своя.
        // Насколько это опасно, зависит от пары видов: гоблин в слоте волка
        // отделался бы мелочью, а вот если бы слот крупного зверя заняла мелочь
        // (или наоборот), статы разошлись бы в разы.
        //
        // Поэтому: вид сменился — запись не подходит, обнуляем её целиком и
        // читаем ваниль заново, как при первой встрече. Цена — одно лишнее
        // чтение базы на редкий случай, польза — статы всегда от своего вида.
        const uint16_t was = rec0->species;
        memset(rec0, 0, sizeof(*rec0));
        rec0->body = body;
        // 85.56: у адреса новый жилец — новое поколение и никакой ступени.
        // Раньше ступень считалась от одного адреса, и новый монстр получал
        // ступень прежнего (поле 85.55: волчица в слот гоблина — «ветеран»).
        rec0->gen = ++s_bodyGenSeq;
        rec0->rankStep = -1;
        rec0->setIndex = -1;      // у нового жильца своё место
        rec0->rankCapped = false;
        char ls[176];
        sprintf_s(ls, "0x%08X slot reuse: kind uEm%04u -> uEm%04u, body record reset"
                      " (gen %u)",
                  (unsigned)body, (unsigned)was, (unsigned)emId, rec0->gen);
        logFile << "EnemyTuner: " << ls << std::endl;
        lstrcpynA(s_status, ls, sizeof(s_status));
    }
    rec0->species = emId;

    // --- боевые статы: урон/броня с per-body roll (audit §8) ----------
    // Должен идти ДО Sanctuary, чтобы Sanctuary видел curDefC/curMDefC
    // и умножал уже боевую защиту на armorMult.
    //
    // 85.34: здесь же спрашиваем оболочку директора. Если она молчит, всплеск
    // равен 1.0 и блок работает ровно как до этой правки — адреналин ничего не
    // включает сам по себе и не мешает, когда выключен.
    // 85.34: адреналин = УРОВЕНЬ приказа (Tempo: 0..1, ведёт его urgency события)
    // x РАЗМЕР всплеска вида (ddda_entities.ini: adrenalineAtk). Два источника
    // одного числа не смешиваются: сила приказа — в тактике, потолок вида — в
    // карточке вида, рядом с остальными боевыми параметрами и с hot-reload.
    const float adrLevel = Runtime::Tempo::DirectorAdrenalineLevelFor(body);
    float spikeAtk = 1.0f, spikeMAtk = 1.0f;
    if (adrLevel > 0.0f) {
        if (t.adrenalineAtk > 1.0f)
            spikeAtk = 1.0f + (t.adrenalineAtk - 1.0f) * adrLevel;
        if (t.adrenalineMagick > 1.0f)
            spikeMAtk = 1.0f + (t.adrenalineMagick - 1.0f) * adrLevel;
    }
    const bool spikeLive = spikeAtk > 1.0001f || spikeMAtk > 1.0001f;
    bool needCombat = !NearlyEq(t.attackMult, 1.0f) || !NearlyEq(t.defenseMult, 1.0f) ||
                      !NearlyEq(t.magickAttackMult, 1.0f) ||
                      !NearlyEq(t.magickDefenseMult, 1.0f) ||
                      rec0->haveCombat || spikeLive; // после первой встречи держим roll
    if (!needCombat) {
        // 85.40: вид под лестницей обязан получить ступень ДАЖЕ если все
        // множители ровно 1.0 — иначе ступени не работали бы там, где силу
        // решено не трогать глобально. Проба дешёвая и только здесь: пока
        // другой сигнал молчит, то есть как правило один раз на особь.
        Runtime::Tempo::RanksNumbers probe;
        needCombat = Runtime::Tempo::GetRanks(kind, &probe);
    }
    if (needCombat) // после первой встречи продолжаем держать roll
    {
        int nC = ApplyCombatStats(body, rec0, t, kind, spikeAtk, spikeMAtk);
        if (nC > 0) s_writes += nC;
    }

    // --- DDON Sanctuary: броня и скорость при возврате на спавн --------
    ApplyReturnSanctuary(body, rec0, t, kind);

    // --- поводок: независимый блок --------------------------------------
    // МОДУЛЬНОСТЬ: поводок и масштаб не должны зависеть друг от друга.
    // Раньше здесь стоял общий ранний выход по scaleMin/Max == 1.0, и при
    // выключенном масштабе поводок молча не работал бы.
    if (!NearlyEq(t.leashScale, 1.0f)) {
        int n = ApplyLeash(body, rec0, t.leashScale, kind);
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

    // 85.40: лестница. Если вид под ней — размер берётся из полосы ступени,
    // а не из равномерного коридора, и ранний выход «масштаб выключен»
    // (коридор 1.0..1.0) больше не мешает: полосу задаёт ступень.
    // 85.56: ступень НЕ спрашиваем заново — берём ту, что уже выдана этой особи.
    // Обе дороги (боевые статы и рост) идут через один и тот же EnsureRankIssued,
    // поэтому кто первый в тике, тот и бросает; второй читает запись. Иначе
    // живое чтение [ranks] двигало бы рост уже стоящего в бою монстра.
    Touched* rec = rec0;      // запись получена выше (блок поводка)
    if (!rec) return;
    EnsureRankIssued(rec, kind, body);

    // 85.52: ранги могут работать без размера (волк). Если вид просил не
    // трогать рост — идём обычной дорогой: коридор вида для рядовых и
    // сохранение крупных ванильных вожаков.
    const bool  ranksOn   = (rec->rankStep >= 0);
    const bool  rankScale = ranksOn && rec->rankUseScale;
    const float rankSize  = rankScale ? rec->rankSize : 0.0f;

    if (!rankScale && NearlyEq(scaleLo, 1.0f) && NearlyEq(scaleHi, 1.0f)) return;

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

    if (rankScale) {
        // 85.41: лестница ПЕРВИЧНА. Раньше первой стояла ветка «вожак», и тело
        // с запомненной базой >= 1.12 (часто наша же прошлая запись) и размер
        // не получало, и ступень ломало. Теперь под лестницей КАЖДОЕ тело
        // получает полосу своей ступени — без исключений и угадывания.
        wantH = rankSize;
        const float lm = 0.02f;   // комплекция внутри полосы
        uint32_t lh1 = (uint32_t)(body >> 3) * 2654435761u;
        uint32_t lh2 = (uint32_t)(body >> 5) * 2246822519u;
        float lj1 = ((float)((lh1 >> 8) & 0xFFFF) / 65535.0f) * 2.0f - 1.0f;
        float lj2 = ((float)((lh2 >> 8) & 0xFFFF) / 65535.0f) * 2.0f - 1.0f;
        wantW = wantH * (1.0f + lm * lj1);
        wantD = wantH * (1.0f + lm * lj2);
    } else if (isLeader) {
        // Вожак от Capcom — только для видов БЕЗ лестницы (их коридор).
        // Сохраняем его авторский статус и крупный размер, лишь гарантируем
        // верхний предел безопасности (scaleHi + 0.04).
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
            // 85.43: под рангами размер ВСЕГДА от ранга, и подпись говорит про
            // ИСТОЧНИК размера. Раньше здесь первым стоял признак «крупный
            // ванильный» (база >= 1.12), и в поле 85.42 половина пачки (6 из 14)
            // печаталась как LEADER — лог выглядел так, будто вернулось
            // убранное правило вожака, хотя размер шёл от ранга.
            wantH, rec->baseH, rankScale ? "RANK" : (isLeader ? "LEADER" : "GENE"),
            kind ? kind : "?",
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

    ForgetMissing();

    s_tracked = Runtime::EnemyCount();

    if (!EntityCfg::AllowWrites()) {
        // 85.34: честность контракта. Директор может выдать всплеск силы атаки, но
        // без allowWrites этот слой вообще не пишет — сказать об этом обязательно,
        // иначе «я включил, а не работает» превращается в поиск несуществующей
        // поломки. Строка одна на эпизод, не поток.
        const int holding = Runtime::Tempo::DirectorMobilizationCount();
        if (!s_spikeBlockedLogged && holding > 0
            && EntityCfg::AnyAdrenalineConfigured()) {
            s_spikeBlockedLogged = true;
            logFile << "EnemyTuner: adrenaline attack NOT applied: mutations layer off "
                       "(ddda_entities.ini [global] allowWrites=off), bodies on orders "
                    << holding << std::endl;
        } else if (holding == 0) {
            s_spikeBlockedLogged = false;
        }
        return;
    }

    for (int i = 0; ; ++i) {
        const char* kind = nullptr;
        uintptr_t body = Runtime::EnemyBodyAt(i, &kind);
        if (!body) break;
        TickOneBody(body, kind);
    }
}

const char* StatusLine() { return s_status; }

// Тело под ручным удержанием, 0 если удержания нет. Для индикатора в UI.
// 85.50: запасы тела на момент вопроса. Поля те же, что пишет ApplyRankToughness:
// яд 0x054, горение 0x080, сбивание 0x0E4. Читаем ЖИВУЮ память, а не запись,
// чтобы строка смерти показывала правду, даже если движок что-то вернул назад.
// 85.56: что за ступень живёт в этом теле. Отдаём ЗАМОРОЖЕННОЕ — то, по чему
// особь реально живёт, а не то, что получилось бы при нынешних весах.
bool RankIssuedFor(uintptr_t body, int* stepOut, uint32_t* genOut, int* setIndexOut)
{
    Touched* rec = FindTouched(body);
    if (!rec || rec->rankStep < 0) return false;
    if (stepOut)     *stepOut     = rec->rankStep;
    if (genOut)      *genOut      = rec->gen;
    if (setIndexOut) *setIndexOut = rec->setIndex;
    return true;
}

bool PoolsFor(uintptr_t body, float* poisOut, float* kdownOut, float* burnOut)
{
    Touched* rec = FindTouched(body);
    if (!rec || !rec->charParamSearched || !rec->charParamOff) return false;
    const uintptr_t base = body + rec->charParamOff;
    float pois = 0.0f, burn = 0.0f, kd = 0.0f;
    const bool ok = SafeRead((const void*)(base + 0x054), &pois, 4)
                 && SafeRead((const void*)(base + 0x080), &burn, 4)
                 && SafeRead((const void*)(base + kFldKdown), &kd, 4);
    if (!ok) return false;
    if (poisOut)  *poisOut  = pois;
    if (kdownOut) *kdownOut = kd;
    if (burnOut)  *burnOut  = burn;
    return true;
}

uintptr_t HeldBody()  { return s_holdBody; }
float     HeldValue() { return s_holdValue; }
int TrackedCount()       { return s_tracked; }
int WriteCount()         { return s_writes; }

} // namespace EnemyTuner
