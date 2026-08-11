#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
generate_bestiary.py — Генератор метаданных бестиария и типов врагов для DDDA AI Overhaul
Читает:
  - resources/types.tsv (Atvaark/DragonsDogma.Research: 4406 фабрик, vtable, RVA, size, groupId)
  - resources/bestiary.py (chrispurnell/pawn-knowledge: 72 врага, flagID, bestiaryId)
Генерирует:
  - src/EnemyTypes.Generated.h (полный список типов MT Framework для рантайм VTable/GroupId lookup)
  - src/Bestiary.Generated.h   (полный бестиарий с 72 типами, mStudyIdx и groupId)
"""

import os, re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TSV_PATH = os.path.join(ROOT, "resources", "types.tsv")
BPY_PATH = os.path.join(ROOT, "resources", "bestiary.py")
OUT_ENEMY_TYPES = os.path.join(ROOT, "src", "EnemyTypes.Generated.h")
OUT_BESTIARY_GEN = os.path.join(ROOT, "src", "Bestiary.Generated.h")
OUT_BESTIARY_DATA = os.path.join(ROOT, "src", "BestiaryData.h")

def load_types_tsv():
    """Читает types.tsv и извлекает все типы uEm и ключевые менеджеры"""
    types_map = {}
    custom_entries = {
        "uHumanEnemy": (0xE0, "0x15eb494", "0x199fd14", "0x136ceba", "29696"),
        "sHumanEnemyManager": (0xE1, "0x1559bd4", "0x1987028", "0x13278ea", "2412"),
    }
    for name, v in custom_entries.items():
        types_map[name] = v

    with open(TSV_PATH, encoding="utf-8") as f:
        f.readline()  # header
        for line in f:
            p = line.strip().split("\t")
            if len(p) < 10:
                continue
            name = p[2]
            gid = int(p[5])  # col 5 = groupId
            vtable = p[9]
            factory = p[1]
            caller = p[0]
            size = p[4]

            if name in custom_entries:
                continue
            if name.startswith("uEm") and gid != 0:
                types_map[name] = (gid, vtable, factory, caller, size)

    return types_map

def load_bestiary_py():
    """Читает bestiary.py и извлекает список врагов с их базовыми FlagID"""
    with open(BPY_PATH, encoding="utf-8") as f:
        content = f.read()

    matches = re.findall(r"\[\s*['\"]([^'\"]+)['\"]\s*,\s*(\d+)\s*,\s*\[([^\]]+)\]", content)
    bestiary = []
    seen_bid = set()
    for name, bid_str, flag_list in matches:
        bid = int(bid_str)
        fids = [int(x.strip()) for x in flag_list.split(",") if x.strip()]
        base_flag = fids[0]
        # Вычисление mStudyIdx: смещение битового флага относительно базы 4128
        m_idx = (base_flag - 4128) // 8
        bestiary.append({
            "name": name,
            "bestiaryId": bid,
            "baseFlag": base_flag,
            "mStudyIdx": m_idx,
        })
    return bestiary

# Маппинг bestiaryId -> (uEmName, Family)
BESTIARY_UEM_MAP = {
    0: ("uEm0100", "Goblin"),
    1: ("uEm0101", "Goblin"),
    2: ("uEm0102", "Goblin"),
    3: ("uEm0200", "Wolf"),
    4: ("uEm0201", "Wolf"),
    5: ("uEm0202", "Wolf"),
    6: ("uEm0500", "Skeleton"),
    7: ("uEm0501", "Skeleton"),
    8: ("uEm0502", "Skeleton"),
    9: ("uEm0503", "Skeleton"),
    10: ("uEm0504", "Skeleton"),
    11: ("uEm0400", "Saurian"),
    12: ("uEm0401", "Saurian"),
    13: ("uEm0402", "Saurian"),
    14: ("uEm0403", "Saurian"),
    15: ("uEm0600", "Undead"),
    16: ("uEm0601", "Undead"),
    17: ("uEm0602", "Undead"),
    18: ("uEm0603", "Undead"),
    19: ("uEm0700", "Harpy"),
    20: ("uEm0701", "Harpy"),
    21: ("uEm0702", "Harpy"),
    22: ("uEm0900", "Gargoyle"),
    23: ("uEm1200", "Ghost"),
    24: ("uEm1201", "Ghost"),
    25: ("uEm1200", "Ghost"),          # Specters
    26: ("uHumanEnemy", "Human"),       # Hostile Soldiers
    27: ("uHumanEnemy", "Human"),       # Hostile Bandits
    28: ("uEm9100", "Human"),           # Enemy Wizard
    29: ("uEm2000", "Cyclops"),
    30: ("uEm2001", "Ogre"),
    31: ("uEm5000", "Golem"),
    32: ("uEm5001", "Golem"),           # Metal Golems
    33: ("uEm5200", "Chimera"),
    34: ("uEm5301", "Chimera"),         # Gorechimeras
    35: ("uEm5400", "Hydra"),
    36: ("uEm5401", "Hydra"),           # Archydras
    37: ("uEm5800", "Griffin"),
    38: ("uEm5801", "Cockatrice"),
    39: ("uEm5900", "EvilEye"),
    40: ("uEm5906", "EvilEye"),         # Vile Eyes
    41: ("uEm6000", "Wight"),
    42: ("uEm6001", "Lich"),
    43: ("uEm8000", "Dragon"),          # The Dragon (Grigori)
    44: ("uEm8000", "Dragon"),          # The Ur-Dragon
    45: ("uEm8100", "Dragon"),          # Drakes
    46: ("uEm8200", "Dragon"),          # Wyrms
    47: ("uEm8201", "Dragon"),          # Wyverns
    48: ("uEm8900", "Boss"),            # The Seneschal
    49: ("uHumanEnemy", "Human"),       # Enemy Person
    50: ("uEm0103", "BBI-Goblin"),      # Greater Goblins / Goblin Shamans
    52: ("uEm0203", "BBI-Wolf"),        # Wargs / Garm
    53: ("uEm0505", "BBI-Skeleton"),    # Skeleton Brutes
    54: ("uEm0506", "BBI-Skeleton"),    # Golden Knights
    55: ("uEm0507", "BBI-Skeleton"),    # Silver Knights
    56: ("uEm7000", "BBI-Armor"),       # Living Armor
    57: ("uEm0404", "BBI-Saurian"),     # Pyre Saurians
    58: ("uEm0604", "BBI-Undead"),      # Poisoned Undead
    59: ("uEm0605", "BBI-Undead"),      # Banshees
    60: ("uEm5500", "BBI-Undead"),      # Eliminators
    61: ("uEm0703", "BBI-Harpy"),       # Strigoi
    62: ("uEm0703", "BBI-Harpy"),       # Sirens
    63: ("uEm1201", "BBI-Ghost"),       # Wraiths
    64: ("uEm5500C", "BBI-Cyclops"),    # Gorecyclopes
    65: ("uEm5501", "BBI-Ogre"),        # Elder Ogres
    66: ("uEm5903", "BBI-EvilEye"),     # Gazers
    67: ("uEm5904", "BBI-Misc"),        # Maneaters
    68: ("uEm6002", "BBI-Wight"),       # Dark Bishops
    69: ("uEm6003", "BBI-Boss"),        # Death
    70: ("uEm8300", "BBI-Dragon"),      # Cursed Dragons
    71: ("uEm7002", "BBI-Boss"),        # Daimon
}

def generate_enemy_types_header(types_map):
    sorted_entries = sorted(types_map.items(), key=lambda kv: kv[1][0])
    with open(OUT_ENEMY_TYPES, "w", encoding="utf-8") as o:
        o.write("// EnemyTypes.Generated.h — AUTOGENERATED from resources/types.tsv\n")
        o.write("// Не редактировать вручную! Перегенерить: python tools/generate_bestiary.py\n")
        o.write("// Источник: Atvaark/DragonsDogma.Research types.tsv\n")
        o.write("#pragma once\n#include <stdint.h>\n\n")
        o.write("struct EnemyTypeInfo {\n")
        o.write("    const char* uEmName;   // FactoryName (uEmXXXX)\n")
        o.write("    uint8_t     groupId;   // types.tsv col 5 == targetBase[0x2D] в damage-хуке\n")
        o.write("    uint32_t    vtableRVA; // FactoryVtable - 0x400000 (для сравнения *(void**)enemyPtr)\n")
        o.write("    uint32_t    factoryRVA;// FactoryPointer - 0x400000\n")
        o.write("    uint32_t    size;      // Размер объекта\n")
        o.write("};\n\n")
        o.write("inline uint32_t RVA(uint32_t absAddr){ return absAddr - 0x400000; }\n\n")
        o.write("static const EnemyTypeInfo g_enemyTypes[] = {\n")
        for name, (gid, vt, fp, caller, size) in sorted_entries:
            vt_rva = int(vt, 16) - 0x400000
            fp_rva = int(fp, 16) - 0x400000
            o.write(f'    {{"{name}", 0x{gid:02X}, 0x{vt_rva:06X}, 0x{fp_rva:06X}, {size}}}, // vt={vt} fp={fp}\n')
        o.write("    {nullptr, 0, 0, 0, 0}\n};\n\n")
        o.write("// Быстрый поиск по groupId (для CombatIntel)\n")
        o.write("inline const EnemyTypeInfo* FindByGroupId(uint8_t gid){\n")
        o.write("    for(auto *e=g_enemyTypes; e->uEmName; ++e) if(e->groupId==gid) return e;\n")
        o.write("    return nullptr;\n}\n")
        o.write("// Точный поиск по VTable (для различения подтипов с общим groupId)\n")
        o.write("inline const EnemyTypeInfo* FindByVTable(uint32_t vtableRVA){\n")
        o.write("    for(auto *e=g_enemyTypes; e->uEmName; ++e) if(e->vtableRVA==vtableRVA) return e;\n")
        o.write("    return nullptr;\n}\n")

    print(f"✅ Сгенерирован {OUT_ENEMY_TYPES} ({len(sorted_entries)} типов)")

def generate_bestiary_headers(types_map, bestiary_list):
    # Уникализация по bestiaryId
    unique_bestiary = {}
    for entry in bestiary_list:
        bid = entry["bestiaryId"]
        if bid not in unique_bestiary:
            unique_bestiary[bid] = entry

    # Генерируем BestiaryData.h
    with open(OUT_BESTIARY_DATA, "w", encoding="utf-8") as o:
        o.write("/**\n * BestiaryData.h — ПОЛНЫЙ БЕСТИАРИЙ DDDA\n *\n")
        o.write(" * Источники:\n")
        o.write(" *   types.tsv       — Atvaark/DragonsDogma.Research (uEmXXXX → groupId / vtable)\n")
        o.write(" *   bestiary.py     — chrispurnell/pawn-knowledge (FlagID → bestiaryId → mStudyIdx)\n")
        o.write(" */\n\n#pragma once\n#include <stdint.h>\n#include <cstring>\n\n")
        o.write("struct EnemyEntry {\n")
        o.write("    int         bestiaryId;       // 0..71 из bestiary.py\n")
        o.write("    uint8_t     groupId;          // targetBase[0x2D] из damage-хука или types.tsv\n")
        o.write("    int         mStudyIdx;        // индекс в mStudyFlag[322]\n")
        o.write("    const char* name;             // название врага\n")
        o.write("    const char* family;           // семейство\n")
        o.write("    const char* uEmName;          // имя фабрики MT Framework\n")
        o.write("    uint32_t    vtableRVA;        // RVA виртуальной таблицы\n")
        o.write("};\n\n")
        o.write("static const EnemyEntry g_bestiary[] = {\n")

        for bid in sorted(unique_bestiary.keys()):
            item = unique_bestiary[bid]
            name = item["name"]
            midx = item["mStudyIdx"]
            uem_tuple = BESTIARY_UEM_MAP.get(bid, ("uEm0100", "Unknown"))
            uem = uem_tuple[0]
            family = uem_tuple[1]

            t_info = types_map.get(uem, (0xFF, "0x400000", "", "", ""))
            gid = t_info[0]
            vt_rva = int(t_info[1], 16) - 0x400000

            o.write(f'    {{ {bid:>2}, 0x{gid:02X}, {midx:>3}, "{name}", "{family}", "{uem}", 0x{vt_rva:06X} }},\n')

        o.write("    { -1, 0x00, -1, nullptr, nullptr, nullptr, 0 }\n};\n\n")
        o.write("#define BESTIARY_COUNT 72\n\n")
        o.write("// Поиск врага по groupId (из damage-хука)\n")
        o.write("inline const EnemyEntry* FindEnemyByGid(uint8_t gid) {\n")
        o.write("    for (auto* e = g_bestiary; e->name; e++)\n")
        o.write("        if (e->groupId == gid) return e;\n")
        o.write("    return nullptr;\n}\n\n")
        o.write("// Поиск врага по bestiaryId\n")
        o.write("inline const EnemyEntry* FindEnemyById(int bid) {\n")
        o.write("    for (auto* e = g_bestiary; e->name; e++)\n")
        o.write("        if (e->bestiaryId == bid) return e;\n")
        o.write("    return nullptr;\n}\n\n")
        o.write("// Поиск врага по mStudyIdx\n")
        o.write("inline const EnemyEntry* FindEnemyByMSIdx(int mIdx) {\n")
        o.write("    if (mIdx < 0) return nullptr;\n")
        o.write("    for (auto* e = g_bestiary; e->name; e++)\n")
        o.write("        if (e->mStudyIdx == mIdx) return e;\n")
        o.write("    return nullptr;\n}\n\n")
        o.write("// Поиск врага по vtableRVA\n")
        o.write("inline const EnemyEntry* FindEnemyByVTable(uint32_t vtRVA) {\n")
        o.write("    if (vtRVA == 0) return nullptr;\n")
        o.write("    for (auto* e = g_bestiary; e->name; e++)\n")
        o.write("        if (e->vtableRVA == vtRVA) return e;\n")
        o.write("    return nullptr;\n}\n\n")
        o.write("// Категория врага для TacticalSwitch (0=small, 1=medium, 2=large, 3=flying, 4=mage, 5=boss)\n")
        o.write("inline int GetEnemyCategory(uint8_t gid) {\n")
        o.write("    if (gid == 0xE0) return 1; // Human medium\n")
        o.write("    const EnemyEntry* e = FindEnemyByGid(gid);\n")
        o.write("    if (!e || !e->family) return 1;\n")
        o.write("    const char* f = e->family;\n")
        o.write('    if (strstr(f, "Goblin") || strstr(f, "Wolf") || strstr(f, "Ghost"))   return 0;\n')
        o.write('    if (strstr(f, "Skeleton") || strstr(f, "Saurian") || strstr(f, "Undead") || strstr(f, "Human")) return 1;\n')
        o.write('    if (strstr(f, "Cyclops") || strstr(f, "Ogre") || strstr(f, "Golem") || strstr(f, "Chimera")) return 2;\n')
        o.write('    if (strstr(f, "Harpy") || strstr(f, "Griffin") || strstr(f, "Cockatrice") || strstr(f, "EvilEye")) return 3;\n')
        o.write('    if (strstr(f, "Wight") || strstr(f, "Lich"))   return 4;\n')
        o.write('    if (strstr(f, "Dragon") || strstr(f, "Hydra") || strstr(f, "Boss") || strstr(f, "Armor"))  return 5;\n')
        o.write("    return 1;\n}\n")

    print(f"✅ Сгенерирован {OUT_BESTIARY_DATA} ({len(unique_bestiary)} бестиариев)")

    # Также копируем/обновляем Bestiary.Generated.h
    with open(OUT_BESTIARY_GEN, "w", encoding="utf-8") as o:
        o.write("// Bestiary.Generated.h — AUTOGENERATED from resources/types.tsv and resources/bestiary.py\n")
        o.write("#pragma once\n#include \"BestiaryData.h\"\n")

    print(f"✅ Сгенерирован {OUT_BESTIARY_GEN}")

def main():
    print("🚀 Генерация метаданных DDDA AI Overhaul из types.tsv и bestiary.py...")
    types_map = load_types_tsv()
    bestiary_list = load_bestiary_py()
    generate_enemy_types_header(types_map)
    generate_bestiary_headers(types_map, bestiary_list)
    print("🎉 Все метаданные успешно синхронизированы!")

if __name__ == "__main__":
    main()
