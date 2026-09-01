#pragma once
#include <stdint.h>
#include "PawnAI_Common.h"

/**
 * PawnAI::OrderWatch — тактическая обработка команд D-Pad / F1-F3 (Ко мне! / Вперед! / Помогите!).
 *
 * ВОЗМОЖНОСТИ:
 *   1. Мгновенная реакция (0 мс):
 *      - «Ко мне!»: спринтовой рывок к Аризену для всех отставших пешек (> 6м).
 *      - «Вперед!»: считывание прицела Аризена и принудительный пин фокус-цели (uCmc + 0x2EB8).
 *      - «Помогите!»: мобилизация защиты и приоритет снятия дебаффов/одышки.
 *
 *   2. Временный тактический импульс склонностей с затуханием (6 сек decay):
 *      - «Ко мне!»   -> Guardian +350 (с гладким возвратом к базовым ползункам).
 *      - «Вперед!»   -> Scather +300, Pioneer +200.
 *      - «Помогите!» -> Medicant +300, Utilitarian +200.
 *
 *   3. Защита файлов сохранений:
 *      - Никакого постоянного дрифта в DDDA.sav. Характер пешки остается чистым.
 */

namespace PawnAI {
namespace OrderWatch {

enum OrderType {
    ORDER_NONE = 0,
    ORDER_COME,  // F3 / D-pad Down
    ORDER_GO,    // F1 / D-pad Up
    ORDER_HELP   // F2 / D-pad Left/Right
};

void Init();
void Shutdown();
void Tick();

void GetDelta(const float* base, float* delta);

struct Stats {
    bool        enabled;
    uint32_t    orderCount;
    OrderType   activeOrder;
    uint32_t    orderRemainingMs;
    char        lastOrderName[32];
    uint32_t    lastOrderTimeMs;
    char        lastOrderAct[48];
    uintptr_t   focusTargetBody;
    char        focusTargetKind[32];
    int32_t     lastPawnGoalCode[3];
    char        lastPawnGoalName[3][32];
    char        lastPawnAct[3][48];
    float       lastPawnDist[3];
};

Stats GetStats();
void  SetEnabled(bool on);

} // namespace OrderWatch
} // namespace PawnAI
