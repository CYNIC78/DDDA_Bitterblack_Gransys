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
# Полный верифицированный маппинг 2026-08-27 на базе DTI, TypeAtlas экшенов,
# типов types.tsv и листингов 29 архивов:
#   - Дракониды (59XX): 5900 Drake, 5901 Wyrm, 5902 Wyvern, 5906 Cursed Dragon
#   - Дракон (58XX): 5800 Grigori, 5801 Ur-Dragon
#   - Скелеты (20XX/21XX): 2000 Skeleton, 2001 Knight, 2002 Lord, 2003 Brute,
#                          2004 Golden, 2005 Silver, 2006 Living Armor, 2100 Mage, 2101 Sorcerer
#   - Нежить (05XX): 0500 Undead, 0501 Stout, 0502 Warrior, 0503 Giant,
#                    0504 Poisoned, 0505 Banshee, 0506 Eliminator
#   - Гарпии (06XX): 0600 Harpy, 0601 Snow Harpy, 0602 Succubus, 0603 Gargoyle, 0604 Strigoi, 0605 Siren
#   - Призраки (07XX): 0700 Phantom, 0701 Phantasm, 0702 Specter, 0703 Wraith
#   - Огры (09XX): 0900 Ogre, 0901 Elder Ogre
#   - Глаза/Мизери (55XX): 5500 Evil Eye, 5501 Vile Eye, 5502 Gazer, 5500_00 Maneater
#   - BBI Боссы (60XX/70XX): 6002 Dark Bishop, 6003 Death, 7000 Daimon Form 1, 7001 Form 2
BESTIARY_UEM_MAP = {
    0: ("uEm0100", "Goblin"),       # Goblins
    1: ("uEm0101", "Goblin"),       # Hobgoblins
    2: ("uEm0102", "Goblin"),       # Grimgoblins
    3: ("uEm0200", "Wolf"),         # Wolves
    4: ("uEm0201", "Wolf"),         # Direwolves
    5: ("uEm0202", "Wolf"),         # Hellhounds
    6: ("uEm2000", "Skeleton"),     # Skeletons
    7: ("uEm2001", "Skeleton"),     # Skeleton Knights
    8: ("uEm2002", "Skeleton"),     # Skeleton Lords
    9: ("uEm2100", "Skeleton"),     # Skeleton Mages
    10: ("uEm2101", "Skeleton"),    # Skeleton Sorcerers
    11: ("uEm0400", "Saurian"),     # Saurians
    12: ("uEm0401", "Saurian"),     # Sulfur Saurians
    13: ("uEm0402", "Saurian"),     # Geo Saurians
    14: ("uEm0403", "Saurian"),     # Saurian Sages
    15: ("uEm0500", "Undead"),      # Undead (Zombies)
    16: ("uEm0501", "Undead"),      # Stout Undead
    17: ("uEm0502", "Undead"),      # Undead Warriors
    18: ("uEm0503", "Undead"),      # Giant Undead
    19: ("uEm0600", "Harpy"),       # Harpies
    20: ("uEm0601", "Harpy"),       # Snow Harpies
    21: ("uEm0602", "Harpy"),       # Succubi
    22: ("uEm0603", "Harpy"),       # Gargoyles
    23: ("uEm0700", "Ghost"),       # Phantoms
    24: ("uEm0701", "Ghost"),       # Phantasms
    25: ("uEm0702", "Ghost"),       # Specters
    26: ("uHumanEnemy", "Human"),   # Hostile Soldiers
    27: ("uHumanEnemy", "Human"),   # Hostile Bandits
    28: ("uEm9100", "Human"),       # Enemy Wizard
    29: ("uEm5000", "Cyclops"),     # Cyclopes
    30: ("uEm0900", "Ogre"),        # Ogres
    31: ("uEm5100", "Golem"),       # Golems
    32: ("uEm5101", "Golem"),       # Metal Golems
    33: ("uEm5200", "Chimera"),     # Chimeras
    34: ("uEm5201", "Chimera"),     # Gorechimeras
    35: ("uEm5300", "Hydra"),       # Hydras
    36: ("uEm5301", "Hydra"),       # Archydras
    37: ("uEm5400", "Griffin"),     # Griffins
    38: ("uEm5401", "Griffin"),     # Cockatrices
    39: ("uEm5500", "EvilEye"),     # Evil Eyes
    40: ("uEm5501", "EvilEye"),     # Vile Eyes
    41: ("uEm6000", "Wight"),       # Wights
    42: ("uEm6001", "Lich"),        # Liches
    43: ("uEm5800", "Dragon"),      # The Dragon (Grigori)
    44: ("uEm5801", "Dragon"),      # The Ur-Dragon
    45: ("uEm5900", "Dragon"),      # Drakes
    46: ("uEm5901", "Dragon"),      # Wyrms
    47: ("uEm5902", "Dragon"),      # Wyverns
    48: ("uEm8900", "Boss"),        # The Seneschal
    49: ("uHumanEnemy", "Human"),   # Enemy Person
    50: ("uEm0103", "BBI-Goblin"),  # Greater Goblins / Goblin Shamans
    52: ("uEm0203", "BBI-Wolf"),    # Wargs / Garm
    53: ("uEm2003", "BBI-Skeleton"),# Skeleton Brutes
    54: ("uEm2004", "BBI-Skeleton"),# Golden Knights
    55: ("uEm2005", "BBI-Skeleton"),# Silver Knights
    56: ("uEm2006", "BBI-Armor"),   # Living Armor
    57: ("uEm0404", "BBI-Saurian"), # Pyre Saurians
    58: ("uEm0504", "BBI-Undead"),  # Poisoned Undead
    59: ("uEm0505", "BBI-Undead"),  # Banshees
    60: ("uEm0506", "BBI-Undead"),  # Eliminators
    61: ("uEm0604", "BBI-Harpy"),   # Strigoi
    62: ("uEm0605", "BBI-Harpy"),   # Sirens
    63: ("uEm0703", "BBI-Ghost"),   # Wraiths
    64: ("uEm5001", "BBI-Cyclops"), # Gorecyclopes
    65: ("uEm0901", "BBI-Ogre"),    # Elder Ogres
    66: ("uEm5502", "BBI-EvilEye"), # Gazers
    67: ("uEm5500_00", "BBI-Misc"), # Maneaters
    68: ("uEm6002", "BBI-Wight"),   # Dark Bishops
    69: ("uEm6003", "BBI-Boss"),    # Death
    70: ("uEm5906", "BBI-Dragon"),  # Cursed Dragons
    71: ("uEm7000", "BBI-Boss"),    # Daimon
}

# bestiary.py сводит несколько видов в один bestiaryId: Warg и Garm оба 52,
# Greater Goblins и Goblin Shamans оба 50. Дед-дуп по bid оставляет только
# первый вид слота и молча выбрасывает остальные.
#   * Goblin Shamans сворачиваем в Greater Goblins (тот же uEm0103) — это
#     осознанный выбор: один класс, один архив.
#   * Garm — НЕ вариант Warg: у него свой класс uEm0204, свой gid 0x91
#     и своя mStudyIdx-полоса (4218..4222). Возвращаем его отдельной строкой.
# Ключ — bid слота; значение — список (имя в bestiary.py, имя для вывода,
# uEm-класс, family).
BID_SHARED_EXTRAS = {
    52: [("Garm", "Garms", "uEm0204", "BBI-Wolf")],
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

            # Виды, разделяющие слот bestiaryId с уже выведенным (Warg/Garm).
            if bid in BID_SHARED_EXTRAS:
                for src_name, disp, uem, family in BID_SHARED_EXTRAS[bid]:
                    extra = next((e for e in bestiary_list if e["name"] == src_name), None)
                    if not extra:
                        print("⚠️  %s не найден в bestiary.py — пропуск" % src_name)
                        continue
                    t2 = types_map.get(uem, (0xFF, "0x400000", "", "", ""))
                    gid2 = t2[0]
                    vt2 = int(t2[1], 16) - 0x400000
                    o.write(f'    {{ {extra["bestiaryId"]:>2}, 0x{gid2:02X}, {extra["mStudyIdx"]:>3}, "{disp}", "{family}", "{uem}", 0x{vt2:06X} }},\n')

        # Wildlife / Animals
        wildlife = [
            ("Deer / Stag", "Wildlife", "uEm8500"),
            ("Doe", "Wildlife", "uEm8501"),
            ("Hare / Rabbit", "Wildlife", "uEm8600"),
            ("Snake / Critter", "Wildlife", "uEm8601"),
            ("Bat / Crow", "Wildlife", "uEm8602"),
            ("Ox / Boar", "Wildlife", "uEm8700"),
            ("Camp Critter", "Wildlife", "uEm8000"),
        ]
        o.write("\n    // Wildlife / Animals (uEm8000, uEm8500 - uEm8700)\n")
        for w_name, w_fam, w_uem in wildlife:
            t_info = types_map.get(w_uem, (0xFF, "0x400000", "", "", ""))
            gid = t_info[0]
            vt_rva = int(t_info[1], 16) - 0x400000
            o.write(f'    {{ -1, 0x{gid:02X},  -1, "{w_name}", "{w_fam}", "{w_uem}", 0x{vt_rva:06X} }},\n')

        o.write("    { -1, 0x00, -1, nullptr, nullptr, nullptr, 0 }\n};\n\n")
        total_species = len(unique_bestiary) + sum(len(v) for v in BID_SHARED_EXTRAS.values())
        o.write(f"#define BESTIARY_COUNT {total_species}\n\n")
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
        o.write("// Поиск по имени класса (\"uEm0100\"), полученному через DTI.\n")
        o.write("// ЗАЧЕМ. FindEnemyByVTable сравнивает ФАБРИЧНЫЕ vtable из types.tsv,\n")
        o.write("// а живой объект несёт instance-vtable. Совпадений не бывает — та же\n")
        o.write("// мина, что убила ActMap::FindByVt. Имя класса же читается у самой игры\n")
        o.write("// и всегда точное, поэтому это единственный надёжный мост\n")
        o.write("// «живой объект -> запись бестиария».\n")
        o.write("inline const EnemyEntry* FindEnemyByUEmName(const char* uEm) {\n")
        o.write("    if (!uEm || !uEm[0]) return nullptr;\n")
        o.write("    for (const EnemyEntry* e = g_bestiary;\n")
        o.write("         e < g_bestiary + sizeof(g_bestiary) / sizeof(g_bestiary[0]); ++e) {\n")
        o.write("        if (e->uEmName && strcmp(e->uEmName, uEm) == 0) return e;\n")
        o.write("    }\n")
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
        o.write('    if (strstr(f, \"Wildlife\")) return 0; // Wildlife is harmless/small\n')
        o.write('    if (strstr(f, \"Goblin\") || strstr(f, \"Wolf\") || strstr(f, \"Ghost\"))   return 0;\n')
        o.write('    if (strstr(f, \"Skeleton\") || strstr(f, \"Saurian\") || strstr(f, \"Undead\") || strstr(f, \"Human\")) return 1;\n')
        o.write('    if (strstr(f, \"Cyclops\") || strstr(f, \"Ogre\") || strstr(f, \"Golem\") || strstr(f, \"Chimera\")) return 2;\n')
        o.write('    if (strstr(f, \"Harpy\") || strstr(f, \"Griffin\") || strstr(f, \"Cockatrice\") || strstr(f, \"EvilEye\")) return 3;\n')
        o.write('    if (strstr(f, \"Wight\") || strstr(f, \"Lich\"))   return 4;\n')
        o.write('    if (strstr(f, \"Dragon\") || strstr(f, \"Hydra\") || strstr(f, \"Boss\") || strstr(f, \"Armor\"))  return 5;\n')
        o.write("    return 1;\n}\n")

    total_species = len(unique_bestiary) + sum(len(v) for v in BID_SHARED_EXTRAS.values())
    print(f"✅ Сгенерирован {OUT_BESTIARY_DATA} ({total_species} видов)")

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
