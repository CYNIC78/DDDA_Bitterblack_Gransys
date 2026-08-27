/**
 * BestiaryData.h — ПОЛНЫЙ БЕСТИАРИЙ DDDA
 *
 * Источники:
 *   types.tsv       — Atvaark/DragonsDogma.Research (uEmXXXX → groupId / vtable)
 *   bestiary.py     — chrispurnell/pawn-knowledge (FlagID → bestiaryId → mStudyIdx)
 */

#pragma once
#include <stdint.h>
#include <cstring>

struct EnemyEntry {
    int         bestiaryId;       // 0..71 из bestiary.py
    uint8_t     groupId;          // targetBase[0x2D] из damage-хука или types.tsv
    int         mStudyIdx;        // индекс в mStudyFlag[322]
    const char* name;             // название врага
    const char* family;           // семейство
    const char* uEmName;          // имя фабрики MT Framework
    uint32_t    vtableRVA;        // RVA виртуальной таблицы
};

static const EnemyEntry g_bestiary[] = {
    {  0, 0x05,   4, "Goblins", "Goblin", "uEm0100", 0x11A0474 },
    {  1, 0x06,   4, "Hobgoblins", "Goblin", "uEm0101", 0x11A2DE4 },
    {  2, 0x07,   5, "Grimgoblins", "Goblin", "uEm0102", 0x11A3104 },
    {  3, 0x08,   8, "Wolves", "Wolf", "uEm0200", 0x11A55F0 },
    {  4, 0x09,   8, "Direwolves", "Wolf", "uEm0201", 0x11A5908 },
    {  5, 0x0A,   9, "Hellhounds", "Wolf", "uEm0202", 0x11A5C20 },
    {  6, 0x39,  12, "Skeletons", "Skeleton", "uEm2000", 0x11B8DF4 },
    {  7, 0x3A,  12, "Skeleton Knights", "Skeleton", "uEm2001", 0x11B9124 },
    {  8, 0x3B,  13, "Skeleton Lords", "Skeleton", "uEm2002", 0x11B9454 },
    {  9, 0x3D,  32, "Skeleton Mages", "Skeleton", "uEm2100", 0x11BA774 },
    { 10, 0x3E,  32, "Skeleton Sorcerers", "Skeleton", "uEm2101", 0x11BAAA4 },
    { 11, 0x0F,  16, "Saurians", "Saurian", "uEm0400", 0x11A9ED4 },
    { 12, 0x12,  16, "Sulfur Saurians", "Saurian", "uEm0401", 0x11A9EDC },
    { 13, 0x15,  17, "Geo Saurians", "Saurian", "uEm0402", 0x11A9EE4 },
    { 14, 0x18,  17, "Saurian Sages", "Saurian", "uEm0403", 0x11A9EEC },
    { 15, 0x1B,  20, "Undead", "Undead", "uEm0500", 0x11AE3B4 },
    { 16, 0x1C,  20, "Stout Undead", "Undead", "uEm0501", 0x11AE6D4 },
    { 17, 0x1D,  21, "Undead Warriors", "Undead", "uEm0502", 0x11AE9F4 },
    { 18, 0x1E,  21, "Giant Undead", "Undead", "uEm0503", 0x11AED14 },
    { 19, 0x20,  24, "Harpies", "Harpy", "uEm0600", 0x11B1D4C },
    { 20, 0x21,  24, "Snow Harpies", "Harpy", "uEm0601", 0x11B2074 },
    { 21, 0x22,  25, "Succubi", "Harpy", "uEm0602", 0x11B239C },
    { 22, 0x23,  25, "Gargoyles", "Harpy", "uEm0603", 0x11B26C4 },
    { 23, 0x25,  28, "Phantoms", "Ghost", "uEm0700", 0x11B4204 },
    { 24, 0x26,  28, "Phantasms", "Ghost", "uEm0701", 0x11B483C },
    { 25, 0x27,  29, "Specters", "Ghost", "uEm0702", 0x11B4B5C },
    { 26, 0xE0,  80, "Hostile Soldiers", "Human", "uHumanEnemy", 0x11EB494 },
    { 27, 0xE0,  80, "Hostile Bandits", "Human", "uHumanEnemy", 0x11EB494 },
    { 28, 0xB7,  81, "Enemy Wizard", "Human", "uEm9100", 0x116661C },
    { 29, 0x3F,  40, "Cyclopes", "Cyclops", "uEm5000", 0x11BC400 },
    { 30, 0x2C,  36, "Ogres", "Ogre", "uEm0900", 0x11B59A8 },
    { 31, 0x47,  44, "Golems", "Golem", "uEm5100", 0x11BEBAC },
    { 32, 0x48,  44, "Metal Golems", "Golem", "uEm5101", 0x11BEECC },
    { 33, 0x4A,  48, "Chimeras", "Chimera", "uEm5200", 0x11C0C58 },
    { 34, 0x4D,  48, "Gorechimeras", "Chimera", "uEm5201", 0x11C1908 },
    { 35, 0x50,  52, "Hydras", "Hydra", "uEm5300", 0x11C3C14 },
    { 36, 0x52,  52, "Archydras", "Hydra", "uEm5301", 0x11C3F6C },
    { 37, 0x54,  56, "Griffins", "Griffin", "uEm5400", 0x11C6CB4 },
    { 38, 0x55,  56, "Cockatrices", "Griffin", "uEm5401", 0x11C6FDC },
    { 39, 0x40,  60, "Evil Eyes", "EvilEye", "uEm5500", 0x11C9FA4 },
    { 40, 0x44,  60, "Vile Eyes", "EvilEye", "uEm5501", 0x11CD780 },
    { 41, 0x5F,  68, "Wights", "Wight", "uEm6000", 0x11D51E8 },
    { 42, 0x60,  68, "Liches", "Lich", "uEm6001", 0x11D5508 },
    { 43, 0x5A,  72, "The Dragon", "Dragon", "uEm5800", 0x11CF280 },
    { 44, 0x5B,  72, "The Ur-Dragon", "Dragon", "uEm5801", 0x11D1438 },
    { 45, 0x5C,  76, "Drakes", "Dragon", "uEm5900", 0x11D2C74 },
    { 46, 0x5D,  76, "Wyrms", "Dragon", "uEm5901", 0x11D2F9C },
    { 47, 0x5E,  77, "Wyverns", "Dragon", "uEm5902", 0x11D32C4 },
    { 48, 0x73,  64, "The Seneschal", "Boss", "uEm8900", 0x11D9DE0 },
    { 49, 0xE0,  84, "Enemy Person", "Human", "uHumanEnemy", 0x11EB494 },
    { 50, 0x8E,  88, "Greater Goblins", "BBI-Goblin", "uEm0103", 0x11A3424 },
    { 52, 0x90,  10, "Wargs", "BBI-Wolf", "uEm0203", 0x11A5F38 },
    { 53, 0x3C,  92, "Skeleton Brutes", "BBI-Skeleton", "uEm2003", 0x11B9784 },
    { 54, 0xA9,  92, "Golden Knights", "BBI-Skeleton", "uEm2004", 0x11B9AB4 },
    { 55, 0xAA,  93, "Silver Knights", "BBI-Skeleton", "uEm2005", 0x11B9DE4 },
    { 56, 0xAB,  93, "Living Armor", "BBI-Armor", "uEm2006", 0x11BA114 },
    { 57, 0x92,  96, "Pyre Saurians", "BBI-Saurian", "uEm0404", 0x11AA20C },
    { 58, 0x1F, 100, "Poisoned Undead", "BBI-Undead", "uEm0504", 0x11AF034 },
    { 59, 0xA0, 100, "Banshees", "BBI-Undead", "uEm0505", 0x11AF354 },
    { 60, 0xA1, 101, "Eliminators", "BBI-Undead", "uEm0506", 0x11AF674 },
    { 61, 0xA3, 104, "Strigoi", "BBI-Harpy", "uEm0604", 0x11B29EC },
    { 62, 0xA4, 104, "Sirens", "BBI-Harpy", "uEm0605", 0x11B2D14 },
    { 63, 0xA5,  30, "Wraiths", "BBI-Ghost", "uEm0703", 0x11B4E7C },
    { 64, 0xAD,  41, "Gorecyclopes", "BBI-Cyclops", "uEm5001", 0x11BD574 },
    { 65, 0xA6,  37, "Elder Ogres", "BBI-Ogre", "uEm0901", 0x11B5CC0 },
    { 66, 0xAE,  62, "Gazers", "BBI-EvilEye", "uEm5502", 0x11C9FAC },
    { 67, 0x42,  62, "Maneaters", "BBI-Misc", "uEm5500_00", 0x11CAE10 },
    { 68, 0xB5,  69, "Dark Bishops", "BBI-Wight", "uEm6002", 0x11D5828 },
    { 69, 0xB6,  70, "Death", "BBI-Boss", "uEm6003", 0x11D5830 },
    { 70, 0xB4,  77, "Cursed Dragons", "BBI-Dragon", "uEm5906", 0x11D3F64 },
    { 71, 0xBA, 108, "Daimon", "BBI-Boss", "uEm7000", 0x1164A34 },

    // Wildlife / Animals (uEm8000, uEm8500 - uEm8700)
    { -1, 0x69,  -1, "Deer / Stag", "Wildlife", "uEm8500", 0x11D8110 },
    { -1, 0x6A,  -1, "Doe", "Wildlife", "uEm8501", 0x11D8428 },
    { -1, 0x6B,  -1, "Hare / Rabbit", "Wildlife", "uEm8600", 0x11D8B68 },
    { -1, 0x6C,  -1, "Snake / Critter", "Wildlife", "uEm8601", 0x11D8E80 },
    { -1, 0x6D,  -1, "Bat / Crow", "Wildlife", "uEm8602", 0x11D9198 },
    { -1, 0x71,  -1, "Ox / Boar", "Wildlife", "uEm8700", 0x11D9860 },
    { -1, 0x61,  -1, "Camp Critter", "Wildlife", "uEm8000", 0x11D6410 },
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

// Поиск по имени класса ("uEm0100"), полученному через DTI.
// ЗАЧЕМ. FindEnemyByVTable сравнивает ФАБРИЧНЫЕ vtable из types.tsv,
// а живой объект несёт instance-vtable. Совпадений не бывает — та же
// мина, что убила ActMap::FindByVt. Имя класса же читается у самой игры
// и всегда точное, поэтому это единственный надёжный мост
// «живой объект -> запись бестиария».
inline const EnemyEntry* FindEnemyByUEmName(const char* uEm) {
    if (!uEm || !uEm[0]) return nullptr;
    for (const EnemyEntry* e = g_bestiary;
         e < g_bestiary + sizeof(g_bestiary) / sizeof(g_bestiary[0]); ++e) {
        if (e->uEmName && strcmp(e->uEmName, uEm) == 0) return e;
    }
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
