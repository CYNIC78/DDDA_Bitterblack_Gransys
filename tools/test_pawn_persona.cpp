// Execute production PawnPersona through the Linux shim.
#include "tcomp/persona_t.cpp"
#include <cassert>
#include <cstdio>

using namespace PawnAI::Persona;

static int failures = 0;

static void Check(bool ok, const char* what)
{
    if (!ok) { ++failures; printf("  FAIL: %s\n", what); }
    else     { printf("  ok  : %s\n", what); }
}

// Order: Scather Medicant Mitigator Challenger Utilitarian Guardian Nexus
//        Pioneer Acquisitor
static void Stack(const float v[9], int r[9])
{
    RanksOf(v, r);
}

int main()
{
    printf("PawnPersona: role from the pawn's own inclination stack\n");

    // 1. Ранги: три верхних получают 2/1/0, остальные остаются вне стека.
    {
        const float v[9] = { 900, 200, 150, 300, 250, 850, 120, 180, 100 };
        int r[9]; Stack(v, r);
        Check(r[0] == RANK_FIRST,  "Scather 900 -> primary");
        Check(r[5] == RANK_SECOND, "Guardian 850 -> secondary");
        Check(r[3] == RANK_THIRD,  "Challenger 300 -> tertiary");
        Check(r[6] == RANK_OUT,    "Nexus 120 -> out of stack");
        Check(r[8] == RANK_OUT,    "Acquisitor 100 -> out of stack");
    }

    // 2. Ползунки главной пешки из ini: Guardian даже не в стеке -> не годен.
    {
        const float v[9] = { 750, 400, 500, 700, 750, 350, 350, 400, 250 };
        int r[9]; Stack(v, r);
        int rank = 0; float val = 0;
        const Kind k = Of(v, &rank, &val);
        Check(r[5] == RANK_OUT, "repo sliders: Guardian out of stack");
        Check(!Eligible(rank, val, 1, 350.0f),
              "repo sliders: main pawn is NOT a guardian candidate");
        (void)k;
    }

    // 3. Ранг первее веса: Guardian-первичная 600 против вторичной 700.
    //    Здесь они в разных пешках, поэтому сравниваем сам ранг напрямую.
    {
        const float v[9] = { 300, 200, 150, 400, 250, 600, 120, 180, 100 };
        int r[9]; Stack(v, r);
        Check(r[5] == RANK_FIRST && Eligible(r[5], v[5], 1, 350.0f),
              "Guardian 600 primary -> eligible");
    }

    // 4. Персона: Nexus-первичная перевешивает Guardian-вторичную по рангу.
    {
        const float v[9] = { 300, 200, 150, 100, 250, 700, 900, 180, 100 };
        int rank = 0; float val = 0;
        const Kind k = Of(v, &rank, &val);
        Check(k == PERSONA_NEXUS, "Nexus primary beats Guardian secondary");
        Check(rank == RANK_FIRST, "persona rank is Nexus' own rank");
    }

    // 5. Полное равенство Guardian и Nexus -> Guardian (решение тестера).
    {
        const float v[9] = { 300, 200, 150, 100, 250, 500, 500, 180, 100 };
        const Kind k = Of(v);
        Check(k == PERSONA_GUARDIAN, "G == N (rank and value) -> Guardian");
    }

    // 6. Порог по рангу: третичная склонность доктрину не включает.
    {
        // 900 Scather, 800 Medicant, 600 Guardian -> Guardian третья.
        const float v[9] = { 900, 800, 100, 100, 100, 600, 120, 100, 100 };
        int rank = 0; float val = 0;
        (void)Of(v, &rank, &val);
        Check(rank == RANK_THIRD, "Guardian 600 behind 900 and 800 -> tertiary");
        Check(!Eligible(rank, val, 1, 350.0f), "tertiary is below minRank=1");
        Check(Eligible(rank, val, 0, 350.0f),  "tertiary passes only if minRank=0");
    }

    // 7. Порог по весу.
    {
        const float v[9] = { 300, 200, 150, 100, 250, 349, 120, 180, 100 };
        int rank = 0; float val = 0;
        (void)Of(v, &rank, &val);
        Check(rank == RANK_FIRST, "Guardian 349 is still primary by rank");
        Check(!Eligible(rank, val, 1, 350.0f), "but below minIncl=350");
    }

    // 8. Все девять равны — без падений и NaN; тай-брейк детерминирован.
    {
        const float v[9] = { 500, 500, 500, 500, 500, 500, 500, 500, 500 };
        int r[9]; Stack(v, r);
        int primary = -1;
        for (int i = 0; i < 9; ++i) if (r[i] == RANK_FIRST) primary = i;
        Check(primary == 0, "all equal: lowest id wins (deterministic)");
        const Kind k = Of(v);
        Check(k == PERSONA_GUARDIAN, "all equal -> Guardian by tie-break");
    }

    // 9. Нулевой указатель не роняет.
    {
        int rank = 12345; float val = 12345.0f;
        const Kind k = Of(0, &rank, &val);
        Check(k == PERSONA_NONE && rank == RANK_OUT && val == 0.0f,
              "null values -> PERSONA_NONE, no crash");
    }

    printf(failures ? "Pawn persona: FAIL (%d)\n" : "Pawn persona: PASS\n", failures);
    return failures ? 1 : 0;
}
