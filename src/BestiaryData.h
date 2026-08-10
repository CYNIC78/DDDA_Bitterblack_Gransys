/**
 * BestiaryData.h — ПОЛНЫЙ БЕСТИАРИЙ DDDA
 *
 * Источники:
 *   types.tsv       — Atvaark/DragonsDogma.Research (uEmXXXX → groupId)
 *   bestiary.py     — chrispurnell/pawn-knowledge (FlagID → bestiaryId)
 *   CT-проба        — наши тесты (mStudyIdx в памяти)
 *
 * ТРИ СЛОЯ МАППИНГА:
 *   enemyGroupId (байт из targetBase[0x2D] DamageLog)
 *       ↕ types.tsv column 6
 *   uEmXXXX (внутреннее имя врага в MT Framework)
 *       ↕ bestiary.py
 *   bestiaryId (0-71: Goblin=0, Wolf=3, Skeleton=6...)
 *       ↕ наши тесты
 *   mStudyIdx (индекс в mStudyFlag[322] памяти пешки)
 *
 * ВАЖНО: Goblin/HobGoblin/GrimGoblin делят ОБЩИЙ groupId=5!
 *        Skeleton/Knight/Lord/Mage/Sorcerer — разные bestiaryId.
 */

#pragma once

struct EnemyEntry {
    int   bestiaryId;       // 0..71 из bestiary.py
    BYTE  groupId;          // targetBase[0x2D] из damage-хука
    int   mStudyIdx;        // индекс в mStudyFlag[322] (-1 = неизвестно)
    const char* name;       // название врага
    const char* family;     // семейство (для группировки в UI)
};

// ============================================================
// ПОЛНАЯ ТАБЛИЦА: 72 врага из bestiary.py + groupId из types.tsv
// ============================================================

static EnemyEntry g_bestiary[] = {
    // bestiaryId, groupId, mStudyIdx, name,               family
    {  0, 0x05,  4, "Goblins",              "Goblin"      }, // ✅ всё проверено
    {  1, 0x05, -1, "Hobgoblins",           "Goblin"      }, // groupId общий с Goblin
    {  2, 0x05, -1, "Grimgoblins",          "Goblin"      }, // groupId общий с Goblin
    {  3, 0x08,  3, "Wolves",               "Wolf"        }, // ✅ всё проверено
    {  4, 0x08, -1, "Direwolves",           "Wolf"        },
    {  5, 0x08, -1, "Hellhounds",           "Wolf"        },
    {  6, 0x1B,  8, "Skeletons",            "Skeleton"    }, // ✅ mIdx=8, gid из types.tsv uEm0500=27→0x1B
    {  7, 0x1C, -1, "Skeleton Knights",     "Skeleton"    }, // uEm0501=28
    {  8, 0x1D, -1, "Skeleton Lords",       "Skeleton"    }, // uEm0502=29
    {  9, 0x1E, -1, "Skeleton Mages",       "Skeleton"    }, // uEm0503=30
    { 10, 0x1F, -1, "Skeleton Sorcerers",   "Skeleton"    }, // uEm0504=31
    { 11, 0x0F, 11, "Saurians",             "Saurian"     }, // ✅ всё проверено
    { 12, 0x12, -1, "Sulfur Saurians",      "Saurian"     }, // uEm0401=18
    { 13, 0x15, -1, "Geo Saurians",         "Saurian"     }, // uEm0402=21
    { 14, 0x18, -1, "Saurian Sages",        "Saurian"     }, // uEm0403=24
    { 15, 0x20, 16, "Undead",               "Undead"      }, // ✅ mIdx=16, gid≈32(0x20) из patterns
    { 16, 0x21, -1, "Stout Undead",         "Undead"      },
    { 17, 0x22, -1, "Undead Warriors",      "Undead"      },
    { 18, 0x23, -1, "Giant Undead",         "Undead"      },
    { 19, 0x25, 19, "Harpies",              "Harpy"       }, // ✅ mIdx=19, gid≈37(0x25) uEm0700
    { 20, 0x26, -1, "Snow Harpies",         "Harpy"       }, // uEm0701=38
    { 21, 0x27, -1, "Succubi",              "Harpy"       }, // uEm0702=39
    { 22, 0xFF, -1, "Gargoyles",            "Gargoyle"    },
    { 23, 0xFF, -1, "Phantoms",             "Ghost"       },
    { 24, 0xFF, -1, "Phantasms",            "Ghost"       },
    { 25, 0xFF, -1, "Specters",             "Ghost"       },
    { 26, 0xFF, -1, "Hostile Soldiers",     "Human"       },
    { 27, 0xFF, -1, "Hostile Bandits",      "Human"       },
    { 28, 0xFF, -1, "Enemy Wizard",         "Human"       },
    { 29, 0xFF, -1, "Cyclopes",             "Cyclops"     },
    { 30, 0xFF, -1, "Ogres",                "Ogre"        },
    { 31, 0xFF, -1, "Golems",               "Golem"       },
    { 32, 0xFF, -1, "Metal Golems",         "Golem"       },
    { 33, 0xFF, -1, "Chimeras",             "Chimera"     },
    { 34, 0xFF, -1, "Gorechimeras",         "Chimera"     },
    { 35, 0xFF, -1, "Hydras",               "Hydra"       },
    { 36, 0xFF, -1, "Archydras",            "Hydra"       },
    { 37, 0xFF, -1, "Griffins",             "Griffin"     },
    { 38, 0xFF, -1, "Cockatrices",          "Cockatrice"  },
    { 39, 0xFF, -1, "Evil Eyes",            "EvilEye"     },
    { 40, 0xFF, -1, "Vile Eyes",            "EvilEye"     },
    { 41, 0xFF, -1, "Wights",               "Wight"       },
    { 42, 0xFF, -1, "Liches",               "Lich"        },
    { 43, 0xFF, -1, "The Dragon (Grigori)", "Dragon"      },
    { 44, 0xFF, -1, "The Ur-Dragon",        "Dragon"      },
    { 45, 0xFF, -1, "Drakes",               "Dragon"      },
    { 46, 0xFF, -1, "Wyrms",                "Dragon"      },
    { 47, 0xFF, -1, "Wyverns",              "Dragon"      },
    { 48, 0xFF, -1, "The Seneschal",        "Boss"        },
    { 49, 0xFF, -1, "Enemy Person",         "Human"       },

    // === Bitterblack Isle (50-71) ===
    { 50, 0xFF, -1, "Greater Goblins",      "BBI-Goblin"  },
    { 51, 0xFF, -1, "Goblin Shamans",       "BBI-Goblin"  },
    { 52, 0xFF, -1, "Wargs / Garm",         "BBI-Wolf"    },
    { 53, 0xFF, -1, "Skeleton Brutes",      "BBI-Skeleton"},
    { 54, 0xFF, -1, "Golden Knights",       "BBI-Skeleton"},
    { 55, 0xFF, -1, "Silver Knights",       "BBI-Skeleton"},
    { 56, 0xFF, -1, "Living Armor",         "BBI-Armor"   },
    { 57, 0xFF, -1, "Pyre Saurians",        "BBI-Saurian" },
    { 58, 0xFF, -1, "Poisoned Undead",      "BBI-Undead"  },
    { 59, 0xFF, -1, "Banshees",             "BBI-Undead"  },
    { 60, 0xFF, -1, "Eliminators",          "BBI-Undead"  },
    { 61, 0xFF, -1, "Strigoi",              "BBI-Harpy"   },
    { 62, 0xFF, -1, "Sirens",               "BBI-Harpy"   },
    { 63, 0xFF, -1, "Wraiths",              "BBI-Ghost"   },
    { 64, 0xFF, -1, "Gorecyclopes",         "BBI-Cyclops" },
    { 65, 0xFF, -1, "Elder Ogres",          "BBI-Ogre"    },
    { 66, 0xFF, -1, "Gazers",               "BBI-EvilEye" },
    { 67, 0xFF, -1, "Maneaters",            "BBI-Misc"    },
    { 68, 0xFF, -1, "Dark Bishops",         "BBI-Wight"   },
    { 69, 0xFF, -1, "Death",                "BBI-Boss"    },
    { 70, 0xFF, -1, "Cursed Dragons",       "BBI-Dragon"  },
    { 71, 0xFF, -1, "Daimon",               "BBI-Boss"    },

    { -1, 0x00, -1, nullptr, nullptr }  // терминатор
};

#define BESTIARY_COUNT 72

// ============================================================
// ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ
// ============================================================

// Поиск врага по groupId (из damage-хука)
inline EnemyEntry* FindEnemyByGid(BYTE gid) {
    for (auto* e = g_bestiary; e->name; e++)
        if (e->groupId == gid) return e;
    return nullptr;
}

// Поиск врага по bestiaryId
inline EnemyEntry* FindEnemyById(int bid) {
    for (auto* e = g_bestiary; e->name; e++)
        if (e->bestiaryId == bid) return e;
    return nullptr;
}

// Поиск врага по mStudyIdx
inline EnemyEntry* FindEnemyByMSIdx(int mIdx) {
    if (mIdx < 0) return nullptr;
    for (auto* e = g_bestiary; e->name; e++)
        if (e->mStudyIdx == mIdx) return e;
    return nullptr;
}

// Категория врага для TacticalSwitch (0=small, 1=medium, 2=large, 3=flying, 4=mage, 5=boss)
inline int GetEnemyCategory(BYTE gid) {
    EnemyEntry* e = FindEnemyByGid(gid);
    if (!e || !e->family) return -1;
    // Определяем по семейству
    const char* f = e->family;
    if (strstr(f, "Goblin") || strstr(f, "Wolf") || strstr(f, "Ghost"))   return 0; // small
    if (strstr(f, "Skeleton") || strstr(f, "Saurian") || strstr(f, "Undead") || strstr(f, "Harpy") || strstr(f, "Human")) return 1; // medium
    if (strstr(f, "Cyclops") || strstr(f, "Ogre") || strstr(f, "Golem") || strstr(f, "Chimera")) return 2; // large
    if (strstr(f, "Harpy") || strstr(f, "Griffin") || strstr(f, "Cockatrice") || strstr(f, "EvilEye")) return 3; // flying
    if (strstr(f, "Wight") || strstr(f, "Lich") || strstr(f, "Ghost"))   return 4; // mage
    if (strstr(f, "Dragon") || strstr(f, "Hydra") || strstr(f, "Boss"))  return 5; // boss
    return 1; // default medium
}
