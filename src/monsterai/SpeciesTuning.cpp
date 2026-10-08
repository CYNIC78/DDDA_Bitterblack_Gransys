#include "stdafx.h"
#include "SpeciesTuning.h"
#include <string.h>
#include <stdio.h>

namespace MonsterAI {

namespace {

// Те же пределы, что и в MonsterTempo.cpp: дальше движок рассинхронизирует
// хитбокс (loco 0.75..1.30, anim 0.70..1.40).
const float kLocoClampMin = 0.75f;
const float kLocoClampMax = 1.30f;
const float kAnimClampMin = 0.70f;
const float kAnimClampMax = 1.40f;
// RegisterRageProfile отказывает профилю, у которого низ не меньше верха.
// Строгий зазор держим здесь: вписав 1.25/1.25, владелец получил бы МОЛЧАЛИВО
// отключённый разгон вида.
const float kMinGap = 0.01f;

bool IsBad(float v) { return !(v == v); }   // NaN: ключа нет или в файле мусор

// Тихий NaN. Так же, как в devtools/AnimProbe.cpp: константное 0.0f/0.0f
// компилятор считает делением на ноль и ругается НА ЭТАПЕ КОМПИЛЯЦИИ
// (MSVC C2124), поэтому берём битовую маску напрямую.
float QNaN()
{
    const uint32_t bits = 0x7FC00000u;
    float f = 0.0f;
    memcpy(&f, &bits, 4);
    return f;
}

// Дописать кусок в примечание. fmt — ровно с "%s %.2f %.2f" (имя, было, стало).
void Append(char* buf, int cap, const char* fmt, const char* name,
            float was, float now)
{
    if (!buf || cap <= 0 || !fmt) return;
    char one[140];
    snprintf(one, sizeof(one), fmt, name, was, now);

    int used = (int)strlen(buf);
    if (used && used < cap - 1) { buf[used++] = ' '; buf[used] = 0; }
    for (int i = 0; one[i] && used < cap - 1; ++i) buf[used++] = one[i];
    buf[used] = 0;
}

float Clamp(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// 86.03, шаг D: «ключ вида записан в ini?» — и дописывание.
//
// iniConfig::getFloat при отсутствии ключа возвращает дефолт И дописывает ключ
// в файл (iniConfig.cpp, «ДОСЫЛКА НЕДОСТАЮЩИХ КЛЮЧЕЙ»). Отсюда два требования,
// которые приходится разводить по двум чтениям:
//
//   1. Узнать, записан ли ключ. Для этого дефолтом идёт NaN — значение,
//      которого в файле быть не может; вернулся NaN — ключа нет.
//   2. Дописать в файл ЧИСЛО КАРТОЧКИ, а не NaN. Именно за это отвечает второе
//      чтение: оно идёт уже с нормальным дефолтом, и iniConfig допишет его.
//
// Первая версия шага D читала один раз с дефолтом NaN — и поле 86.03 показало
// результат: 16 строк «Config: added missing key [species.uEmXXXX]
// baseLocoMin = nan». Ключи появлялись в файле, но мусором, который нельзя ни
// прочитать, ни поправить осмысленно. Первое чтение теперь идёт с выключенной
// автодопиской, чтобы зонд вообще ничего не писал в файл.
//
// outPresent сообщает, было ли значение вписано владельцем: по правилу шага D
// записанный ключ абсолютен и общий сдвиг [monsterTempo] к нему не применяется.
float ReadSpeciesBound(BackfillingIniReader& ini, const char* section,
                       const char* key, float cardValue, float clampLo,
                       float clampHi, bool* outPresent)
{
    // 1. Зонд. Автодописка выключена: NaN не должен попасть в файл.
    const bool saved = ini.AutoBackfill();
    ini.SetAutoBackfill(false);
    const float probe = ini.Float(section, key, QNaN());
    ini.SetAutoBackfill(saved);

    const bool present = !IsBad(probe);
    if (outPresent) *outPresent = present;
    if (present) return Clamp(probe, clampLo, clampHi);

    // 2. Ключа нет: второе чтение с дефолтом КАРТОЧКИ. Вот оно и допишет ключ
    //    в файл — так новая опция появляется в ini сама, без ручной правки.
    ini.Float(section, key, cardValue);
    return cardValue;
}

struct Pair {
    float lo, hi;
    float cardLo, cardHi;
    const char* name;
};

void Sanitize(Pair& p, float baseLo, float baseHi, float clampLo, float clampHi,
              char* note, int noteCap)
{
    // 1. Мусор (или ключа нет — тогда тут уже число карточки, и это норма).
    if (IsBad(p.lo)) { const float t = p.lo; p.lo = p.cardLo;
                       Append(note, noteCap, "%s junk(%.2f)->card(%.2f)", p.name, t, p.lo); }
    if (IsBad(p.hi)) { const float t = p.hi; p.hi = p.cardHi;
                       Append(note, noteCap, "%s junk(%.2f)->card(%.2f)", p.name, t, p.hi); }

    // 2. Перевёрнутый диапазон — типовая опечатка в конфиге.
    if (p.lo > p.hi) {
        const float tl = p.lo, th = p.hi;
        p.lo = th; p.hi = tl;
        Append(note, noteCap, "%s reversed(%.2f/%.2f)->fixed", p.name, tl, th);
    }

    // 3. Жёсткие пределы движка.
    const float cl = Clamp(p.lo, clampLo, clampHi);
    const float ch = Clamp(p.hi, clampLo, clampHi);
    if (cl != p.lo) { Append(note, noteCap, "%sMin clamped(%.2f->%.2f)", p.name, p.lo, cl); p.lo = cl; }
    if (ch != p.hi) { Append(note, noteCap, "%sMax clamped(%.2f->%.2f)", p.name, p.hi, ch); p.hi = ch; }

    // 4. БЕЗОПАСНОСТЬ ПРИКАЗА: потолок обязан быть не ниже базового низа и
    //    верха, иначе admit отобьёт тело (baseline-outside-profile).
    if (p.lo < baseLo) { Append(note, noteCap, "%sMin raised(%.2f->%.2f)", p.name, p.lo, baseLo); p.lo = baseLo; }
    if (p.hi < baseHi) { Append(note, noteCap, "%sMax raised(%.2f->%.2f)", p.name, p.hi, baseHi); p.hi = baseHi; }

    // 5. Строгий зазор: низ == верх модуль разгона не принимает вовсе.
    if (p.lo >= p.hi) {
        const float tl = p.lo, th = p.hi;
        const float want = Clamp(p.lo + kMinGap, clampLo, clampHi);
        if (want > p.lo) { p.hi = want; }
        else { p.lo = Clamp(p.hi - kMinGap, clampLo, clampHi); }
        Append(note, noteCap, "%s narrowed(%.2f/%.2f)->fixed", p.name, tl, th);
    }
}

} // namespace

// 86.03, шаг D. База вида: карточка + общий сдвиг, НО ключ вида важнее и
// абсолютно. Правило и его причину см. в объявлении (SpeciesTuning.h).
void SpeciesBaseRangeEffective(BackfillingIniReader& ini, const SpeciesCard* card,
                               float globalLocoLo, float globalLocoHi,
                               float globalAnimLo, float globalAnimHi,
                               float* locoLo, float* locoHi,
                               float* animLo, float* animHi)
{
    // Вид без карточки (87 из 91): общий диапазон как есть, ключей у него нет.
    const bool has = card && card->locoHi > card->locoLo
                           && card->animHi > card->animLo;
    if (!has) {
        SpeciesBaseRangeFor(card, globalLocoLo, globalLocoHi,
                            globalAnimLo, globalAnimHi,
                            locoLo, locoHi, animLo, animHi);
        return;
    }

    char section[64];
    snprintf(section, sizeof(section), "species.%s", card->kind);

    // Правило пары: пока не записан НИ ОДИН ключ — обе границы остаются
    // сдвинутой парой (прежнее поведение, общая ручка работает). Как только
    // записан хотя бы один — пара собирается из карточки, и общая ручка на
    // этот вид больше не действует. Поэтому вписавший только baseLocoMax
    // получает низ из карточки, а не уехавший за общей ручкой.
    const float shiftLlo = card->locoLo + (globalLocoLo - kGlobalLocoRefLo);
    const float shiftLhi = card->locoHi + (globalLocoLo - kGlobalLocoRefLo);
    const float shiftAlo = card->animLo + (globalAnimLo - kGlobalAnimRefLo);
    const float shiftAhi = card->animHi + (globalAnimLo - kGlobalAnimRefLo);

    bool setLlo = false, setLhi = false, setAlo = false, setAhi = false;
    const float vLlo = ReadSpeciesBound(ini, section, "baseLocoMin", card->locoLo,
                                        kLocoClampMin, kLocoClampMax, &setLlo);
    const float vLhi = ReadSpeciesBound(ini, section, "baseLocoMax", card->locoHi,
                                        kLocoClampMin, kLocoClampMax, &setLhi);
    const float vAlo = ReadSpeciesBound(ini, section, "baseAnimMin", card->animLo,
                                        kAnimClampMin, kAnimClampMax, &setAlo);
    const float vAhi = ReadSpeciesBound(ini, section, "baseAnimMax", card->animHi,
                                        kAnimClampMin, kAnimClampMax, &setAhi);

    const bool locoSet = setLlo || setLhi;
    const bool animSet = setAlo || setAhi;
    float llo = locoSet ? card->locoLo : shiftLlo;
    float lhi = locoSet ? card->locoHi : shiftLhi;
    float alo = animSet ? card->animLo : shiftAlo;
    float ahi = animSet ? card->animHi : shiftAhi;
    if (setLlo) llo = vLlo;
    if (setLhi) lhi = vLhi;
    if (setAlo) alo = vAlo;
    if (setAhi) ahi = vAhi;

    // Перевёрнутая пара — типовая опечатка в конфиге; чиним обменом, как в
    // Sanitize у ярости. Равные границы допустимы: «темп вида постоянен» —
    // законное желание, в отличие от ярости, где нужен строгий зазор.
    if (llo > lhi) { const float t = llo; llo = lhi; lhi = t; }
    if (alo > ahi) { const float t = alo; alo = ahi; ahi = t; }

    if (locoLo) *locoLo = llo;
    if (locoHi) *locoHi = lhi;
    if (animLo) *animLo = alo;
    if (animHi) *animHi = ahi;
}

SpeciesTempoNumbers SpeciesTempoFromIni(SpeciesIniReader& ini,
                                        const SpeciesCard& card,
                                        float baseLocoMin, float baseLocoMax,
                                        float baseAnimMin, float baseAnimMax)
{
    SpeciesTempoNumbers out;
    memset(&out, 0, sizeof(out));

    char section[64];
    snprintf(section, sizeof(section), "species.%s", card.kind);

    out.rageEnabled = ini.Bool(section, "tempoRage", card.tempoRage);

    Pair loco;
    loco.name = "rageLoco";
    loco.cardLo = card.rageLocoLo;
    loco.cardHi = card.rageLocoHi;
    loco.lo = ini.Float(section, "rageLocoMin", card.rageLocoLo);
    loco.hi = ini.Float(section, "rageLocoMax", card.rageLocoHi);

    Pair anim;
    anim.name = "rageAnim";
    anim.cardLo = card.rageAnimLo;
    anim.cardHi = card.rageAnimHi;
    anim.lo = ini.Float(section, "rageAnimMin", card.rageAnimLo);
    anim.hi = ini.Float(section, "rageAnimMax", card.rageAnimHi);

    Sanitize(loco, baseLocoMin, baseLocoMax, kLocoClampMin, kLocoClampMax,
             out.note, (int)sizeof(out.note));
    Sanitize(anim, baseAnimMin, baseAnimMax, kAnimClampMin, kAnimClampMax,
             out.note, (int)sizeof(out.note));

    out.rageLocoMin = loco.lo;
    out.rageLocoMax = loco.hi;
    out.rageAnimMin = anim.lo;
    out.rageAnimMax = anim.hi;
    out.sanitized = (out.note[0] != 0);
    return out;
}

} // namespace MonsterAI
