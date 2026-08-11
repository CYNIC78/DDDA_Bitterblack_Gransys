/**
 * BestiaryData.h — FULL BESTIARY & WILDLIFE DATA FOR DDDA
 *
 * Sources:
 *   types.tsv   — Atvaark/DragonsDogma.Research (uEmXXXX -> groupId / vtable)
 *   bestiary.py — chrispurnell/pawn-knowledge (FlagID -> bestiaryId -> mStudyIdx)
 */

#pragma once
#include <stdint.h>
#include <cstring>

struct EnemyEntry {
    int         bestiaryId;       // 0..71 from bestiary.py (-1 for wildlife)
    uint8_t     groupId;          // targetBase[0x2D] from damage hook or types.tsv
    int         mStudyIdx;        // index in mStudyFlag[322] (-1 if none)
    const char* name;             // enemy name
    const char* family;           // enemy family
    const char* uEmName;          // MT Framework factory name
    uint32_t    vtableRVA;        // virtual table RVA
};

static const EnemyEntry g_bestiary[] = {
    {  0, 0x05,   4, "Goblins", "Goblin", "uEm0100", 0x11A0474 },
    {  1, 0x06,   4, "Hobgoblins", "Goblin", "uEm0101", 0x11A2DE4 },
    {  2, 0x07,   5, "Grimgoblins", "Goblin", "uEm0102", 0x11A3104 },
    {  3, 0x08,   8, "Wolves", "Wolf", "uEm0200", 0x11A55F0 },
    {  4, 0x09,   8, "Direwolves", "Wolf", "uEm0201", 0x11A5908 },
    {  5, 0x0A,   9, "Hellhounds", "Wolf", "uEm0202", 0x11A5C20 },
    {  6, 0x1B,  12, "Skeletons", "Skeleton", "uEm0500", 0x11AE3B4 },
    {  7, 0x1C,  12, "Skeleton Knights", "Skeleton", "uEm0501", 0x11AE6D4 },
    {  8, 0x1D,  13, "Skeleton Lords", "Skeleton", "uEm0502", 0x11AE9F4 },
    {  9, 0x1E,  32, "Skeleton Mages", "Skeleton", "uEm0503", 0x11AED14 },
    { 10, 0x1F,  32, "Skeleton Sorcerers", "Skeleton", "uEm0504", 0x11AF034 },
    { 11, 0x0F,  16, "Saurians", "Saurian", "uEm0400", 0x11A9ED4 },
    { 12, 0x12,  16, "Sulfur Saurians", "Saurian", "uEm0401", 0x11A9EDC },
    { 13, 0x15,  17, "Geo Saurians", "Saurian", "uEm0402", 0x11A9EE4 },
    { 14, 0x18,  17, "Saurian Sages", "Saurian", "uEm0403", 0x11A9EEC },
    { 15, 0x20,  20, "Undead", "Undead", "uEm0600", 0x11B1D4C },
    { 16, 0x21,  20, "Stout Undead", "Undead", "uEm0601", 0x11B2074 },
    { 17, 0x22,  21, "Undead Warriors", "Undead", "uEm0602", 0x11B239C },
    { 18, 0x23,  21, "Giant Undead", "Undead", "uEm0603", 0x11B26C4 },
    { 19, 0x25,  24, "Harpies", "Harpy", "uEm0700", 0x11B4204 },
    { 20, 0x26,  24, "Snow Harpies", "Harpy", "uEm0701", 0x11B483C },
    { 21, 0x27,  25, "Succubi", "Harpy", "uEm0702", 0x11B4B5C },
    { 22, 0x2C,  25, "Gargoyles", "Gargoyle", "uEm0900", 0x11B59A8 },
    { 23, 0x37,  28, "Phantoms", "Ghost", "uEm1200", 0x11B74AC },
    { 24, 0x38,  28, "Phantasms", "Ghost", "uEm1201", 0x11B77CC },
    { 25, 0x37,  29, "Specters", "Ghost", "uEm1200", 0x11B74AC },
    { 26, 0xE0,  80, "Hostile Soldiers", "Human", "uHumanEnemy", 0x11EB494 },
    { 27, 0xE0,  80, "Hostile Bandits", "Human", "uHumanEnemy", 0x11EB494 },
    { 28, 0xB7,  81, "Enemy Wizard", "Human", "uEm9100", 0x116661C },
    { 29, 0x39,  40, "Cyclopes", "Cyclops", "uEm2000", 0x11B8DF4 },
    { 30, 0x3A,  36, "Ogres", "Ogre", "uEm2001", 0x11B9124 },
    { 31, 0x3F,  44, "Golems", "Golem", "uEm5000", 0x11BC400 },
    { 32, 0xAD,  44, "Metal Golems", "Golem", "uEm5001", 0x11BD574 },
    { 33, 0x4A,  48, "Chimeras", "Chimera", "uEm5200", 0x11C0C58 },
    { 34, 0x52,  48, "Gorechimeras", "Chimera", "uEm5301", 0x11C3F6C },
    { 35, 0x54,  52, "Hydras", "Hydra", "uEm5400", 0x11C6CB4 },
    { 36, 0x55,  52, "Archydras", "Hydra", "uEm5401", 0x11C6FDC },
    { 37, 0x5A,  56, "Griffins", "Griffin", "uEm5800", 0x11CF280 },
    { 38, 0x5B,  56, "Cockatrices", "Cockatrice", "uEm5801", 0x11D1438 },
    { 39, 0x5C,  60, "Evil Eyes", "EvilEye", "uEm5900", 0x11D2C74 },
    { 40, 0xB4,  60, "Vile Eyes", "EvilEye", "uEm5906", 0x11D3F64 },
    { 41, 0x5F,  68, "Wights", "Wight", "uEm6000", 0x11D51E8 },
    { 42, 0x60,  68, "Liches", "Lich", "uEm6001", 0x11D5508 },
    { 43, 0x61,  72, "The Dragon", "Dragon", "uEm8000", 0x11D6410 },
    { 44, 0x61,  72, "The Ur-Dragon", "Dragon", "uEm8000", 0x11D6410 },
    { 45, 0x62,  76, "Drakes", "Dragon", "uEm8100", 0x11D6A10 },
    { 46, 0x63,  76, "Wyrms", "Dragon", "uEm8200", 0x11D7070 },
    { 47, 0x64,  77, "Wyverns", "Dragon", "uEm8201", 0x11D7418 },
    { 48, 0x73,  64, "The Seneschal", "Boss", "uEm8900", 0x11D9DE0 },
    { 49, 0xE0,  84, "Enemy Person", "Human", "uHumanEnemy", 0x11EB494 },
    { 50, 0x8E,  88, "Greater Goblins", "BBI-Goblin", "uEm0103", 0x11A3424 },
    { 52, 0x90,  10, "Wargs", "BBI-Wolf", "uEm0203", 0x11A5F38 },
    { 53, 0xA0,  92, "Skeleton Brutes", "BBI-Skeleton", "uEm0505", 0x11AF354 },
    { 54, 0xA1,  92, "Golden Knights", "BBI-Skeleton", "uEm0506", 0x11AF674 },
    { 55, 0xA2,  93, "Silver Knights", "BBI-Skeleton", "uEm0507", 0x11AF994 },
    { 56, 0xBA,  93, "Living Armor", "BBI-Armor", "uEm7000", 0x1164A34 },
    { 57, 0x92,  96, "Pyre Saurians", "BBI-Saurian", "uEm0404", 0x11AA20C },
    { 58, 0xA3, 100, "Poisoned Undead", "BBI-Undead", "uEm0604", 0x11B29EC },
    { 59, 0xA4, 100, "Banshees", "BBI-Undead", "uEm0605", 0x11B2D14 },
    { 60, 0x40, 101, "Eliminators", "BBI-Undead", "uEm5500", 0x11C9FA4 },
    { 61, 0xA5, 104, "Strigoi", "BBI-Harpy", "uEm0703", 0x11B4E7C },
    { 62, 0xA5, 104, "Sirens", "BBI-Harpy", "uEm0703", 0x11B4E7C },
    { 63, 0x38,  30, "Wraiths", "BBI-Ghost", "uEm1201", 0x11B77CC },
    { 64, 0xAF,  41, "Gorecyclopes", "BBI-Cyclops", "uEm5500C", 0x1163914 },
    { 65, 0x44,  37, "Elder Ogres", "BBI-Ogre", "uEm5501", 0x11CD780 },
    { 66, 0xB1,  62, "Gazers", "BBI-EvilEye", "uEm5903", 0x11D35EC },
    { 67, 0xB2,  62, "Maneaters", "BBI-Misc", "uEm5904", 0x11D3914 },
    { 68, 0xB5,  69, "Dark Bishops", "BBI-Wight", "uEm6002", 0x11D5828 },
    { 69, 0xB6,  70, "Death", "BBI-Boss", "uEm6003", 0x11D5830 },
    { 70, 0x65,  77, "Cursed Dragons", "BBI-Dragon", "uEm8300", 0x11D7978 },
    { 71, 0xBC, 108, "Daimon", "BBI-Boss", "uEm7002", 0x1165ED0 },

    // Wildlife / Animals (uEm8500 - uEm8700)
    { -1, 0x69,  -1, "Deer / Stag", "Wildlife", "uEm8500", 0x11D8110 },
    { -1, 0x6A,  -1, "Doe", "Wildlife", "uEm8501", 0x11D8428 },
    { -1, 0x6B,  -1, "Hare / Rabbit", "Wildlife", "uEm8600", 0x11D8B68 },
    { -1, 0x6C,  -1, "Snake / Critter", "Wildlife", "uEm8601", 0x11D8E80 },
    { -1, 0x6D,  -1, "Bat / Crow", "Wildlife", "uEm8602", 0x11D9198 },
    { -1, 0x71,  -1, "Ox / Boar", "Wildlife", "uEm8700", 0x11D9860 },

    { -1, 0x00, -1, nullptr, nullptr, nullptr, 0 }
};

#define BESTIARY_COUNT 72

// Поиск врага по groupId (из damage-хука)
inline const EnemyEntry* FindEnemyByGid(uint8_t gid) {
    for (auto* e = g_bestiary; e->name; e++)
        if (e->groupId == gid) return e;
    return nullptr;
}

// Поиск врага по bestiaryId
inline const EnemyEntry* FindEnemyById(int bid) {
    for (auto* e = g_bestiary; e->name; e++)
        if (e->bestiaryId == bid) return e;
    return nullptr;
}

// Поиск врага по mStudyIdx
inline const EnemyEntry* FindEnemyByMSIdx(int mIdx) {
    if (mIdx < 0) return nullptr;
    for (auto* e = g_bestiary; e->name; e++)
        if (e->mStudyIdx == mIdx) return e;
    return nullptr;
}

// Поиск врага по vtableRVA
inline const EnemyEntry* FindEnemyByVTable(uint32_t vtRVA) {
    if (vtRVA == 0) return nullptr;
    for (auto* e = g_bestiary; e->name; e++)
        if (e->vtableRVA == vtRVA) return e;
    return nullptr;
}

// Категория врага для TacticalSwitch (0=small, 1=medium, 2=large, 3=flying, 4=mage, 5=boss)
inline int GetEnemyCategory(uint8_t gid) {
    if (gid == 0xE0) return 1; // Human medium
    const EnemyEntry* e = FindEnemyByGid(gid);
    if (!e || !e->family) return 1;
    const char* f = e->family;
    if (strstr(f, "Wildlife")) return 0; // Wildlife is harmless/small
    if (strstr(f, "Goblin") || strstr(f, "Wolf") || strstr(f, "Ghost"))   return 0;
    if (strstr(f, "Skeleton") || strstr(f, "Saurian") || strstr(f, "Undead") || strstr(f, "Human")) return 1;
    if (strstr(f, "Cyclops") || strstr(f, "Ogre") || strstr(f, "Golem") || strstr(f, "Chimera")) return 2;
    if (strstr(f, "Harpy") || strstr(f, "Griffin") || strstr(f, "Cockatrice") || strstr(f, "EvilEye")) return 3;
    if (strstr(f, "Wight") || strstr(f, "Lich"))   return 4;
    if (strstr(f, "Dragon") || strstr(f, "Hydra") || strstr(f, "Boss") || strstr(f, "Armor"))  return 5;
    return 1;
}
