#pragma once
/**
 * PawnAI_Common.h — Общие оффсеты и хелперы для всех PawnAI-модулей
 * Вынесено из монолита PawnAI.cpp чтобы разгрузить менталку
 *
 * ВАЖНО: все операции записи в память игры защищены SEH.
 * Падение одного модуля не должно ронять другие.
 */
#include "stdafx.h"
#include "../EnemyTypes.Generated.h"
#include "../ModPaths.h"
extern BYTE** pBase;

#define PLAYER_BASE         0xA7000
#define PAWN_OFFSET         0x7F0
#define PAWN_STRIDE         0x1660
// Inclinations: verified from user's CE Lua script (2026-08-15).
//   local inc = pBase + 0xA7000 + 0x7F0 + 0x96C + 0x1224
//   for i, n in ipairs(incNames) do
//     readFloat(inc + (i-1)*0xC)
// From the pawn body: starts at 0x96C + 0x1224 = 0x1B90, stride 0xC.
// КАНОНИЧЕСКИЙ ИСТОЧНИК: docs/SOURCE_OF_TRUTH.md. НЕ МЕНЯТЬ без сверки с ним.
#define INCL_OFFSET         (0x96C + 0x1224)
#define INCL_STRIDE         0xC
#define MSTUDYFLAG_OFFSET   0x1616
#define MSTUDYFLAG_SIZE     322

// SEH-безопасное чтение/запись через игру.
// Вся работа с pBase-памятью должна идти через эти макросы.
#define SEH_TRY             __try {
#define SEH_EXCEPT          } __except(EXCEPTION_EXECUTE_HANDLER) { /* молча — следующий тик догонит */ }

enum InclIdx {
    I_SCATHER = 0, I_MEDICANT, I_MITIGATOR, I_CHALLENGER,
    I_UTILITARIAN, I_GUARDIAN, I_NEXUS, I_PIONEER,
    I_ACQUISITOR, I_SKILL_USE, I_COUNT
};

inline const char* InclName(int i){
    static const char* n[]={"Scather","Medicant","Mitigator","Challenger","Utilitarian","Guardian","Nexus","Pioneer","Acquisitor","Skill Use"};
    return n[i];
}
enum InclCat { CAT_USEFUL, CAT_NEUTRAL, CAT_JUNK, CAT_DOCTRINE };
inline InclCat GetInclCategory(int idx){
    switch(idx){
        case I_SCATHER: case I_MEDICANT: case I_MITIGATOR:
        case I_CHALLENGER: case I_UTILITARIAN: return CAT_USEFUL;
        // Build 55: Guardian/Nexus больше не «мусор», а доктрины защиты.
        // Они выходят из-под кордона; их реализацией займётся GuardianDoctrine
        // (priority-совет с учётом vocation), а не числовой кап.
        case I_GUARDIAN: case I_NEXUS: return CAT_DOCTRINE;
        // Acquisitor — единственная инклинация, бесполезная в бою; ею управляет
        // AcquisitorManager (подавление в бою / временный подъём вне боя).
        case I_ACQUISITOR: return CAT_JUNK;
        default: return CAT_NEUTRAL;
    }
}

// ================= Vocation =================
// Оффсет подтверждён (docs/SOURCE_OF_TRUTH.md, character record +0x6E0, int32).
// Enum значений ПОДТВЕРЖДЁН пользовательской CE-таблицей (2026-08-16).
// ВНИМАНИЕ: 1-based, порядок НЕ совпадает с интуитивным:
//   1=Fighter, 2=Strider, 3=Mage, 4=Mystic Knight, 5=Assassin,
//   6=Magick Archer, 7=Warrior, 8=Ranger, 9=Sorcerer
// Значения 0 и ≥10 не определены.
#define VOCATION_OFFSET 0x6E0

enum VocationId {
    VOC_FIGHTER = 1, VOC_STRIDER = 2, VOC_MAGE = 3, VOC_MYSTIC_KNIGHT = 4,
    VOC_ASSASSIN = 5, VOC_MAGICK_ARCHER = 6, VOC_WARRIOR = 7, VOC_RANGER = 8,
    VOC_SORCERER = 9,
    VOC_UNKNOWN = -1
};

inline const char* VocationName(int v){
    switch(v){
        case VOC_FIGHTER:        return "Fighter";
        case VOC_STRIDER:        return "Strider";
        case VOC_MAGE:           return "Mage";
        case VOC_MYSTIC_KNIGHT:  return "Mystic Knight";
        case VOC_ASSASSIN:       return "Assassin";
        case VOC_MAGICK_ARCHER:  return "Magick Archer";
        case VOC_WARRIOR:        return "Warrior";
        case VOC_RANGER:         return "Ranger";
        case VOC_SORCERER:       return "Sorcerer";
        default:                 return "?";
    }
}

// Грубая классификация «стиля боя» для доктрин. DDDA-вокации гибридны,
// поэтому это первая прикидка; позже уточним по action-eligibility.
enum VocationClass { VCL_MELEE, VCL_RANGED, VCL_CASTER, VCL_HYBRID, VCL_UNKNOWN };

inline VocationClass VocationClassOf(int v){
    switch(v){
        case VOC_FIGHTER: case VOC_WARRIOR: case VOC_MYSTIC_KNIGHT: return VCL_MELEE;
        case VOC_MAGE: case VOC_SORCERER: return VCL_CASTER;
        case VOC_RANGER: case VOC_MAGICK_ARCHER: return VCL_RANGED;
        case VOC_STRIDER: case VOC_ASSASSIN: return VCL_HYBRID;
        default: return VCL_UNKNOWN;
    }
}

inline const char* VocationClassName(VocationClass c){
    switch(c){
        case VCL_MELEE:  return "Melee";
        case VCL_RANGED: return "Ranged";
        case VCL_CASTER: return "Caster";
        case VCL_HYBRID: return "Hybrid";
        default:         return "?";
    }
}

// Чтение вокации из character record. recordBase = *pBase + PLAYER_BASE
// (для главной пешки — + PLAYER_BASE + PAWN_OFFSET).
inline int ReadVocation(uintptr_t recordBase){
    if(!recordBase) return VOC_UNKNOWN;
    SEH_TRY
        return *(int32_t*)(recordBase + VOCATION_OFFSET);
    SEH_EXCEPT
    return VOC_UNKNOWN;
}

struct InclPreset {
    const char* name;
    const char* desc;
    float v[I_COUNT];
};

// Проверка активного геймплея (защита от записи во время загрузки/меню)
inline bool IsInActiveGameplay() {
    if (!pBase || !*pBase) return false;
    SEH_TRY
        BYTE* pPlayer = *pBase + PLAYER_BASE;
        UINT16 level = *(UINT16*)(pPlayer + 0xDD0);
        if (level == 0) return false;
        float maxHp = *(float*)(pPlayer + 0x96C + 4);
        if (maxHp <= 0.0f || maxHp > 200000.0f) return false;
    SEH_EXCEPT
    return true;
}

// Доступ к памяти — единственный кто трогает pBase напрямую
inline float* GetPawnInclination(int inclIdx, int pawnIdx=0){
    if(!pBase || !*pBase) return nullptr;
    SEH_TRY
        int pawnBase = PLAYER_BASE + PAWN_OFFSET + pawnIdx * PAWN_STRIDE;
        return (float*)(*pBase + pawnBase + INCL_OFFSET + inclIdx * INCL_STRIDE);
    SEH_EXCEPT
    return nullptr;
}

// Чтение всех инклинаций: каждый адрес под собственным SEH.
// Если один адрес битый — только ОН становится 500,
// остальные читаются нормально. Иначе один плохой
// адрес (например Skill Use на i*0xC=0x6C) валит ВСЕ бары.
inline void ReadAllIncl(float* out, int pawnIdx=0){
    if(!pBase || !*pBase){
        for(int i=0;i<I_COUNT;i++) out[i]=500.f;
        return;
    }
    int pawnBase = PLAYER_BASE + PAWN_OFFSET + pawnIdx * PAWN_STRIDE;
    uintptr_t inclBase = reinterpret_cast<uintptr_t>(*pBase) + pawnBase + INCL_OFFSET;
    for(int i=0;i<I_COUNT;i++){
        __try {
            out[i] = *(float*)(inclBase + i * INCL_STRIDE);
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            out[i] = 500.f;
        }
    }
}

inline void WriteAllIncl(const float* v, int pawnIdx=0){
    if(!pBase || !*pBase) return;
    int pawnBase = PLAYER_BASE + PAWN_OFFSET + pawnIdx * PAWN_STRIDE;
    uintptr_t inclBase = reinterpret_cast<uintptr_t>(*pBase) + pawnBase + INCL_OFFSET;
    for(int i=0;i<I_COUNT;i++){
        __try {
            *(float*)(inclBase + i * INCL_STRIDE) = v[i];
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            /* этот адрес не пишется, остальные работают */
        }
    }
}

inline int CountKnownEnemies(){
    if(!pBase || !*pBase) return 0;
    SEH_TRY
        BYTE* study = *pBase + PLAYER_BASE + PAWN_OFFSET + MSTUDYFLAG_OFFSET;
        int k=0; for(int i=0;i<MSTUDYFLAG_SIZE;i++) if(study[i]!=0) k++; return k;
    SEH_EXCEPT
    return 0;
}