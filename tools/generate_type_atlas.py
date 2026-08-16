#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
generate_type_atlas.py — человекочитаемая карта 4405 фабрик MT Framework.

Читает resources/types.tsv (Atvaark/DragonsDogma.Research).
Пишет:
  docs/generated/TYPE_ATLAS.md    — каталог для людей
  src/TypeAtlas.Generated.h       — runtime Identify (DevTools)

VA в TSV под базу 0x400000. В рантайме: base + (VA - 0x400000).
"""

from __future__ import annotations

import os
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TSV_PATH = os.path.join(ROOT, "resources", "types.tsv")
OUT_MD = os.path.join(ROOT, "docs", "generated", "TYPE_ATLAS.md")
OUT_H = os.path.join(ROOT, "src", "TypeAtlas.Generated.h")
IMAGE_BASE = 0x400000


def rva(va_str: str) -> int:
    return int(va_str, 16) - IMAGE_BASE


def load_types():
    rows = []
    with open(TSV_PATH, encoding="utf-8") as f:
        f.readline()
        for line in f:
            p = line.rstrip("\n").split("\t")
            if len(p) < 10:
                continue
            rows.append(
                {
                    "caller": p[0],
                    "factory": p[1],
                    "name": p[2],
                    "alloc": p[3],
                    "size": int(p[4]) if p[4].isdigit() else 0,
                    "typeid": int(p[5]) if p[5].isdigit() else 0,
                    "reset": p[8],
                    "factory_vt": p[9],
                }
            )
    return rows


# ---------------------------------------------------------------------------
# Категории, которые нам реально нужны как «карта королевства»
# ---------------------------------------------------------------------------

MANAGERS = [
    "sUnit",
    "sUnitExt",
    "sUnitManager",
    "sUnitSearchManager",
    "sSetManager",
    "sEnemyManager",
    "sHumanEnemyManager",
    "sCharacterBaseManager",
    "sPlayerManager",
    "sPawnManager",
    "sNpcManager",
    "sNpcIntelManager",
    "sQuestManager",
    "sItemManager",
    "sLockOnManager",
    "sWeatherManager",
    "sArchiveManager",
    "sResource",
    "sResourceExt",
    "sArea",
    "sAreaExt",
    "sEventManager",
    "sShlManager",
    "sSoundManager",
    "sAICtrlManager",
]

PLAYER_CAM = [
    "uPlayer",
    "uPlayerBase",
    "uPawnIntel",
    "uCamera",
    "uCameraBase",
    "uCameraCtrl",
    "uCameraGame",
    "uCameraAnimation",
    "uCameraLAG1",
    "uCameraLERP",
    "uCameraQuake",
]

SPAWN_CORE = [
    "cLayoutSet",
    "cLayoutSetCharaBase",
    "cLayoutSetDynamic",
    "cLayoutSetEnemy",
    "cLayoutSetEnemyEv",
    "cLayoutSetNpc",
    "cLayoutSetOm",
    "cSetInfoEnemy",
    "cLinkUnit",
    "cLinkUnitEnemy",
    "cThinkMgr",
    "cThinkFSM",
    "cEnemyThink",
    "cCharParamEnemy",
]


def by_name(rows):
    return {t["name"]: t for t in rows}


def md_table(rows, names):
    idx = by_name(rows)
    lines = [
        "| Имя | Size | TypeId | Factory RVA | FactoryVtable RVA |",
        "|---|---:|---:|---|---|",
    ]
    missing = []
    for n in names:
        t = idx.get(n)
        if not t:
            missing.append(n)
            continue
        lines.append(
            f"| `{t['name']}` | {t['size']} | 0x{t['typeid']:02X} | "
            f"`0x{rva(t['factory']):06X}` | `0x{rva(t['factory_vt']):06X}` |"
        )
    return "\n".join(lines), missing


def write_markdown(rows):
    idx = by_name(rows)
    prefixes = defaultdict(int)
    for t in rows:
        n = t["name"]
        if n.startswith("Mt"):
            prefixes["Mt*"] += 1
        else:
            prefixes[n[:1]] += 1

    uem = [t for t in rows if t["name"].startswith("uEm")]
    uem_live = [t for t in uem if t["typeid"] != 0]
    uem_parts = [t for t in uem if t["typeid"] == 0]
    setinfo = [t for t in rows if t["name"].startswith("cSetInfoEnemy")]
    lotmgr = [t for t in rows if "LotMgr" in t["name"] or t["name"].startswith("sSetManager")]

    mgr_tbl, _ = md_table(rows, MANAGERS)
    pc_tbl, _ = md_table(rows, PLAYER_CAM)
    sp_tbl, _ = md_table(rows, SPAWN_CORE)

    def emit_full(title, items):
        out = [
            f"### {title} ({len(items)})",
            "",
            "| Имя | Size | TypeId | Factory RVA | FactoryVt RVA |",
            "|---|---:|---:|---|---|",
        ]
        for t in sorted(items, key=lambda x: (x["typeid"] == 0, x["typeid"], x["name"])):
            out.append(
                f"| `{t['name']}` | {t['size']} | 0x{t['typeid']:02X} | "
                f"`0x{rva(t['factory']):06X}` | `0x{rva(t['factory_vt']):06X}` |"
            )
        out.append("")
        return "\n".join(out)

    # full catalog grouped by first letter / known prefixes
    groups = defaultdict(list)
    for t in rows:
        n = t["name"]
        if n.startswith("uEm"):
            groups["uEm* — враги и их части"].append(t)
        elif n.startswith("cSetInfo"):
            groups["cSetInfo* — дескрипторы постановки"].append(t)
        elif n.startswith("cFSM") or n.startswith("cThink") or n.startswith("cAI"):
            groups["AI / FSM / Think"].append(t)
        elif n.startswith("uCamera") or n.startswith("rFollowCamera") or "Camera" in n:
            groups["Камера"].append(t)
        elif n.startswith("s") and "::" not in n:
            groups["Синглтоны s*"].append(t)
        elif n.startswith("uPl") or n.startswith("uPlayer") or n.startswith("uPawn"):
            groups["Игрок / пешка"].append(t)
        elif n.startswith("uNpc") or n.startswith("uHuman"):
            groups["NPC / human"].append(t)
        elif n.startswith("cLayout") or n.startswith("cLinkUnit") or "Lot" in n:
            groups["Layout / Lot / Link"].append(t)
        elif n.startswith("r") and "::" not in n:
            groups["Ресурсы r*"].append(t)
        elif n.startswith("Mt"):
            groups["Mt* ядро"].append(t)
        else:
            groups["Прочее"].append(t)

    parts = []
    parts.append("# Type Atlas — карта фабрик DDDA (MT Framework)")
    parts.append("")
    parts.append("**Автогенерация.** Не редактировать руками.")
    parts.append("`python tools/generate_type_atlas.py`")
    parts.append("")
    parts.append("Источник: `resources/types.tsv` ← [Atvaark/DragonsDogma.Research](https://github.com/Atvaark/DragonsDogma.Research).")
    parts.append(f"Записей: **{len(rows)}**. Image base TSV: `0x{IMAGE_BASE:X}`.")
    parts.append("Рантайм-адрес: `GetModuleHandle(NULL) + RVA`.")
    parts.append("")
    parts.append("Правила runtime-resolve — в `docs/ARCHITECTURE.md`.")
    parts.append("Разведанные оффсеты полей — в `docs/FIELD_MAP.md`.")
    parts.append("")
    parts.append("## Как пользоваться")
    parts.append("")
    parts.append("```cpp")
    parts.append("#include \"TypeAtlas.Generated.h\"")
    parts.append("uintptr_t base = (uintptr_t)GetModuleHandle(nullptr);")
    parts.append("uint32_t  rva  = *(uintptr_t*)obj - base;        // первый dword объекта")
    parts.append("auto* t = TypeAtlas::FindByFactoryVTable(rva);  // для uEm* это и есть instance vt")
    parts.append("if (!t) t = TypeAtlas::FindByName(\"sEnemyManager\");")
    parts.append("// t->name, t->size, t->typeId")
    parts.append("// указатель на фабрику в .data: (void*)(base + t->factoryRVA)")
    parts.append("```")
    parts.append("")
    parts.append("TypeId ≠ 0 ровно у 110 типов (враги + то, что игра нумерует).")
    parts.append("Остальные опознаются **только** по vtable/имени.")
    parts.append("")
    parts.append("Не путать два vtable:")
    parts.append("- колонка TSV `FactoryVtable` — vtable **фабрики**;")
    parts.append("- `*(void**)liveObject` — vtable **экземпляра**.")
    parts.append("Для живых `uEm*` они эмпирически совпали (поэтому `EnemyTypes.Generated.h`")
    parts.append("и CombatIntel работают). Для `s*` managers адрес всё равно должен быть")
    parts.append("подтверждён runtime-resolve, а не принят из generated catalog как singleton.")
    parts.append("")
    parts.append("## Префиксы")
    parts.append("")
    parts.append("| Префикс | Смысл | Кол-во |")
    parts.append("|---|---|---:|")
    parts.append(f"| `c` | component / param / act | {prefixes.get('c', 0)} |")
    parts.append(f"| `u` | unit (живой объект) | {prefixes.get('u', 0)} |")
    parts.append(f"| `r` | resource | {prefixes.get('r', 0)} |")
    parts.append(f"| `s` | singleton | {prefixes.get('s', 0)} |")
    parts.append(f"| `n` | (сетевое / внутреннее) | {prefixes.get('n', 0)} |")
    parts.append(f"| `a` | application | {prefixes.get('a', 0)} |")
    parts.append(f"| `Mt*` | ядро движка | {prefixes.get('Mt*', 0)} |")
    parts.append("")
    parts.append("## Ключи от королевства — синглтон-менеджеры")
    parts.append("")
    parts.append("Это то, что надо резолвить первым. `sUnit` (1.7 МБ) — главный список юнитов.")
    parts.append("`sSetManager` — лоты/спавн. `sEnemyManager` / `sHumanEnemyManager` — живые враги.")
    parts.append("")
    parts.append(mgr_tbl)
    parts.append("")
    parts.append("## Игрок, пешка, камера")
    parts.append("")
    parts.append("`uPlayer` vtable RVA `0x11E4F34` уже зашит в `CombatIntel::IsPartyMember`.")
    parts.append("")
    parts.append(pc_tbl)
    parts.append("")
    parts.append("## Пайплайн спавна (ядро)")
    parts.append("")
    parts.append("```")
    parts.append("LOT → sSetManager::cLotMgr<cLayoutSetEnemy> → cLayoutSetEnemy")
    parts.append("    → cSetInfoEnemyXXXX → create_uEmXXXX → sEnemyManager / sUnit")
    parts.append("```")
    parts.append("")
    parts.append(sp_tbl)
    parts.append("")
    parts.append("### LotMgr / SaveUnit внутри sSetManager")
    parts.append("")
    parts.append("| Имя | Size | Factory RVA |")
    parts.append("|---|---:|---|")
    for t in sorted(lotmgr, key=lambda x: x["name"]):
        parts.append(f"| `{t['name']}` | {t['size']} | `0x{rva(t['factory']):06X}` |")
    parts.append("")
    parts.append(emit_full("Враги uEm* с TypeId (живые юниты)", uem_live))
    parts.append(emit_full("uEm* без TypeId (части тела, базы, хелперы)", uem_parts))
    parts.append(emit_full("cSetInfoEnemy* (что LOT кладёт в мир)", setinfo))
    parts.append("## Полный каталог по группам")
    parts.append("")
    parts.append("Ниже все 4405 имён. Искать по странице (`Ctrl+F`).")
    parts.append("")
    for title in sorted(groups.keys()):
        parts.append(emit_full(title, groups[title]))

    with open(OUT_MD, "w", encoding="utf-8") as o:
        o.write("\n".join(parts))
        if not parts[-1].endswith("\n"):
            o.write("\n")
    print(f"wrote {OUT_MD} ({len(rows)} types)")


def write_header(rows):
    # Compact atlas for runtime. We keep all rows: DevTools needs names.
    lines = []
    lines.append("// TypeAtlas.Generated.h — AUTOGENERATED from resources/types.tsv")
    lines.append("// python tools/generate_type_atlas.py")
    lines.append("// Не редактировать руками.")
    lines.append("#pragma once")
    lines.append("#include <stdint.h>")
    lines.append("#include <string.h>")
    lines.append("")
    lines.append("namespace TypeAtlas {")
    lines.append("")
    lines.append("struct Info {")
    lines.append("    const char* name;")
    lines.append("    uint32_t    factoryRVA;     // FactoryPointer - 0x400000")
    lines.append("    uint32_t    factoryVtRVA;   // FactoryVtable  - 0x400000")
    lines.append("    uint32_t    size;")
    lines.append("    uint16_t    typeId;         // 0 у большинства; у uEm == groupId == obj[0x2D]")
    lines.append("};")
    lines.append("")
    lines.append(f"static const int kCount = {len(rows)};")
    lines.append("static const Info kTypes[] = {")
    for t in rows:
        name = t["name"].replace("\\", "\\\\").replace('"', '\\"')
        lines.append(
            f'    {{ "{name}", 0x{rva(t["factory"]):06X}, 0x{rva(t["factory_vt"]):06X}, '
            f'{t["size"]}, {t["typeid"]} }},'
        )
    lines.append("};")
    lines.append("")
    lines.append("inline const Info* FindByName(const char* name) {")
    lines.append("    if (!name) return nullptr;")
    lines.append("    for (int i = 0; i < kCount; ++i)")
    lines.append("        if (strcmp(kTypes[i].name, name) == 0) return &kTypes[i];")
    lines.append("    return nullptr;")
    lines.append("}")
    lines.append("")
    lines.append("inline const Info* FindByFactoryVTable(uint32_t rva) {")
    lines.append("    if (!rva) return nullptr;")
    lines.append("    for (int i = 0; i < kCount; ++i)")
    lines.append("        if (kTypes[i].factoryVtRVA == rva) return &kTypes[i];")
    lines.append("    return nullptr;")
    lines.append("}")
    lines.append("")
    lines.append("inline const Info* FindByTypeId(uint16_t id) {")
    lines.append("    if (!id) return nullptr;")
    lines.append("    for (int i = 0; i < kCount; ++i)")
    lines.append("        if (kTypes[i].typeId == id) return &kTypes[i];")
    lines.append("    return nullptr;")
    lines.append("}")
    lines.append("")
    lines.append("} // namespace TypeAtlas")
    lines.append("")
    with open(OUT_H, "w", encoding="utf-8") as o:
        o.write("\n".join(lines))
    print(f"wrote {OUT_H} ({len(rows)} types)")


def main():
    rows = load_types()
    write_markdown(rows)
    write_header(rows)


if __name__ == "__main__":
    main()
