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
