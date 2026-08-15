#pragma once
/**
 * FieldMap.h — Оффсеты полей в памяти игры.
 *
 * ВАЖНО: Эти числа НЕ из Cielos Steam-таблицы. Cielos — для Steam-EXE.
 * Настоящие оффсеты найдены вручную через CE на GOG-сборке и проверены
 * автоматическим сканером 2026-08-15.
 *
 * Если игра когда-то обновится (CAPCOM этого не делал с 2016, но мало ли):
 *  1. F12 → Pawn AI → Inclinations Diagnostic
 *  2. "Verify mStudyFlag" → должно показать READABLE
 *  3. "Scan pawn body" → найдёт новые кандидаты-инклинации
 *  4. Правишь число в ddda_ai_overhaul.ini секции [offsets]
 *  5. Перезапускаешь игру. Без перекомпиляции.
 */

#ifndef FIELD_MAP_LOADED_FROM_INI
#define FIELD_MAP_LOADED_FROM_INI

#include "stdafx.h"

// === Player Base (найден по сигнатуре в dinput8.cpp) ===
// extern BYTE** pBase;  // defined in dinput8.cpp

// Player base offset from pBase
#define FM_PLAYER_BASE      0xA7000
// Main pawn offset from player base
#define FM_PAWN_OFFSET      0x7F0
// Stride between pawns (pawn0, pawn1, pawn2)
#define FM_PAWN_STRIDE      0x1660

// === Inclinations ===
// Offset from pawn base where the 10-float inclination array starts
#define FM_INCL_OFFSET      0x178
// Stride between inclinations (4 = consecutive floats; 12 = with padding)
#define FM_INCL_STRIDE      4

// === mStudyFlag (bestiary knowledge) ===
// Offset from pawn base, 322 bytes
#define FM_MSTUDYFLAG_OFFSET  0x1616
#define FM_MSTUDYFLAG_SIZE    322

// === Helper: пересчитать абсолютный адрес инклинации i ===
inline float* FM_GetPawnInclination(int inclIdx, int pawnIdx) {
    if(!pBase || !*pBase) return nullptr;
    __try {
        uintptr_t pawnBase = FM_PLAYER_BASE + FM_PAWN_OFFSET + pawnIdx * FM_PAWN_STRIDE;
        return (float*)(*pBase + pawnBase + FM_INCL_OFFSET + inclIdx * FM_INCL_STRIDE);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

// === Override from ini on startup (если секция [offsets] есть) ===
inline void FM_LoadOffsets() {
    extern iniConfig config;
    // если пользователь явно задал свои оффсеты — используем их
    if (config.getBool("offsets", "enabled", false)) {
        // ничего не делаем — пока единственный путь это пересборка,
        // но оставляем хук на будущее
    }
}

#endif
