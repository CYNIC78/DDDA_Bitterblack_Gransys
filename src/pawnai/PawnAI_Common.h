#pragma once
/**
 * PawnAI_Common.h — Общие оффсеты и хелперы для всех PawnAI-модулей
 * Вынесено из монолита PawnAI.cpp чтобы разгрузить менталку
 */
#include "stdafx.h"
#include "../EnemyTypes.Generated.h"
extern BYTE** pBase;

#define PLAYER_BASE         0xA7000
#define PAWN_OFFSET         0x7F0
#define PAWN_STRIDE         0x1660
#define INCL_OFFSET         (0x96C + 0x1224)
#define INCL_STRIDE         0xC
#define MSTUDYFLAG_OFFSET   0x1616
#define MSTUDYFLAG_SIZE     322

enum InclIdx {
    I_SCATHER = 0, I_MEDICANT, I_MITIGATOR, I_CHALLENGER,
    I_UTILITARIAN, I_GUARDIAN, I_NEXUS, I_PIONEER,
    I_ACQUISITOR, I_SKILL_USE, I_COUNT
};

inline const char* InclName(int i){
    static const char* n[]={"Scather","Medicant","Mitigator","Challenger","Utilitarian","Guardian","Nexus","Pioneer","Acquisitor","Skill Use"};
    return n[i];
}
enum InclCat { CAT_USEFUL, CAT_NEUTRAL, CAT_JUNK };
inline InclCat GetInclCategory(int idx){
    switch(idx){
        case I_SCATHER: case I_MEDICANT: case I_MITIGATOR:
        case I_CHALLENGER: case I_UTILITARIAN: return CAT_USEFUL;
        case I_GUARDIAN: case I_NEXUS: case I_ACQUISITOR: return CAT_JUNK;
        default: return CAT_NEUTRAL;
    }
}

struct InclPreset {
    const char* name;
    const char* desc;
    float v[I_COUNT];
};

// Доступ к памяти — единственный кто трогает pBase напрямую
inline float* GetPawnInclination(int inclIdx, int pawnIdx=0){
    if(!pBase || !*pBase) return nullptr;
    int pawnBase = PLAYER_BASE + PAWN_OFFSET + pawnIdx * PAWN_STRIDE;
    return (float*)(*pBase + pawnBase + INCL_OFFSET + inclIdx * INCL_STRIDE);
}
inline void ReadAllIncl(float* out, int pawnIdx=0){
    for(int i=0;i<I_COUNT;i++){ auto* p=GetPawnInclination(i,pawnIdx); out[i]= p? *p : 500.f; }
}
inline void WriteAllIncl(const float* v, int pawnIdx=0){
    for(int i=0;i<I_COUNT;i++){ auto* p=GetPawnInclination(i,pawnIdx); if(p) *p=v[i]; }
}
inline int CountKnownEnemies(){
    if(!pBase || !*pBase) return 0;
    BYTE* study = *pBase + PLAYER_BASE + PAWN_OFFSET + MSTUDYFLAG_OFFSET;
    int k=0; for(int i=0;i<MSTUDYFLAG_SIZE;i++) if(study[i]!=0) k++; return k;
}
