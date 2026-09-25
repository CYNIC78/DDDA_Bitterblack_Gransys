// PawnPersona — см. PawnPersona.h. Чистая логика, без памяти игры.

#include "stdafx.h"
#include "PawnPersona.h"

namespace PawnAI {
namespace Persona {

// Девять настоящих склонностей: 0..8 (I_SKILL_USE в стек не входит).
static const int kInclCount = 9;

void RanksOf(const float values[9], int rankOut[9])
{
    for (int i = 0; i < kInclCount; ++i) rankOut[i] = RANK_OUT;
    if (!values || !rankOut) return;

    // Сортировка вставками: девять элементов, сортировать нечего.
    // Порядок: значение по убыванию, при равенстве — меньший id.
    int order[9];
    for (int i = 0; i < kInclCount; ++i) order[i] = i;

    for (int i = 1; i < kInclCount; ++i) {
        const int cur = order[i];
        int j = i - 1;
        while (j >= 0) {
            const int cand = order[j];
            const bool better = (values[cur] > values[cand]) ||
                                (values[cur] == values[cand] && cur < cand);
            if (!better) break;
            order[j + 1] = cand;
            --j;
        }
        order[j + 1] = cur;
    }

    // Три верхних получают 2 / 1 / 0, остальные остаются RANK_OUT.
    for (int place = 0; place < 3; ++place)
        rankOut[order[place]] = RANK_FIRST - place;
}

Kind Of(const float values[9], int* rankOut, float* valueOut)
{
    if (!values) {
        if (rankOut)  *rankOut  = RANK_OUT;
        if (valueOut) *valueOut = 0.0f;
        return PERSONA_NONE;
    }

    int rank[9];
    RanksOf(values, rank);

    const int   rg = rank[I_GUARDIAN], rn = rank[I_NEXUS];
    const float vg = values[I_GUARDIAN], vn = values[I_NEXUS];

    Kind  k;
    int   r;
    float v;

    // Ранг первее веса. Полное равенство — за Guardian (решение тестера:
    // защита важнее связности).
    if (rg > rn || (rg == rn && vg >= vn)) {
        k = PERSONA_GUARDIAN; r = rg; v = vg;
    } else {
        k = PERSONA_NEXUS;    r = rn; v = vn;
    }

    if (rankOut)  *rankOut  = r;
    if (valueOut) *valueOut = v;
    return k;
}

const char* Name(Kind k)
{
    switch (k) {
        case PERSONA_GUARDIAN: return "Guardian";
        case PERSONA_NEXUS:    return "Nexus";
        default:               return "none";
    }
}

} // namespace Persona
} // namespace PawnAI
