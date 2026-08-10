#!/usr/bin/env python3
"""
ARCtool Helper — автоматизация распаковки/запаковки .arc для DDDA

Положи arctool.exe и этот скрипт в одну папку.
Запускай из командной строки.

Требования: Python 3.6+, arctool.exe, pc-dd.bat (от FluffyQuack)

Использование:
  python arctool_helper.py unpack game_main.arc       # распаковать с -xfs
  python arctool_helper.py pack game_main             # запаковать
  python arctool_helper.py unpack-all nativePC/rom    # распаковать все .arc рекурсивно
  python arctool_helper.py create-nightmare           # создать nightmare_game_main.arc
  python arctool_helper.py edit-ai game_main          # открыть AI-файлы для редактирования
"""

import os, sys, shutil, subprocess, argparse, glob
from pathlib import Path

# ═══════════════════════════════════════════════════════════
# КОНФИГУРАЦИЯ
# ═══════════════════════════════════════════════════════════

SCRIPT_DIR = Path(__file__).parent.resolve()
ARCTOOL_EXE = SCRIPT_DIR / "ARCtool.exe"

# Флаги ARCtool для DDDA PC
ARCTOOL_FLAGS_XFS = "-xfs -dd -texRE6 -alwayscomp -pc -txt -v 7"
ARCTOOL_FLAGS_LOT = "-lot -dd -texRE6 -alwayscomp -pc -txt -v 7"
ARCTOOL_FLAGS_ALL = "-tex -xfs -lot -gmd -dd -texRE6 -alwayscomp -pc -txt -v 7"

# ═══════════════════════════════════════════════════════════
# 🔥 ФАЙЛЫ AI — что редактировать для оверхола пешек
# ═══════════════════════════════════════════════════════════

# Файлы в game_main/AI/AIPlayerActionParameter/
# (нужна верификация — запусти unpack и проверь что реально есть)
AI_ACTION_PARAM_FILES = [
    "AIPlActParamBow.AIPlActParam",
    "AIPlActParamSword.AIPlActParam",
    "AIPlActParamDagger.AIPlActParam",
    "AIPlActParamStaff.AIPlActParam",
    "AIPlActParamShield.AIPlActParam",
    "AIPlActParamLongsword.AIPlActParam",
    "AIPlActParamWarhammer.AIPlActParam",
    "AIPlActParamMagickShield.AIPlActParam",
    "AIPlActParamLongbow.AIPlActParam",
    "AIPlActParamMagickBow.AIPlActParam",
]

# Файлы в game_main/param/pl/other/
PLAYER_PARAM_FILES = [
    "PlAbilityParam.ablparam.xml",
    "PlJumpParam.pjp.xml",
]

# Файлы в game_main/param/pl/level/
LEVEL_FILES = [
    "LvFighter.lvl.xml",
    "LvStrider.lvl.xml",
    "LvWizard.lvl.xml",
    "LvPaladin.lvl.xml",
    "LvTrickster.lvl.xml",
    "LvHunter.lvl.xml",
    "LvWarrior.lvl.xml",
    "LvFighterM.lvl.xml",
    "LvStriderM.lvl.xml",
]

# ═══════════════════════════════════════════════════════════
# ФУНКЦИИ
# ═══════════════════════════════════════════════════════════

def find_arctool():
    """Ищет ARCtool.exe и pc-dd.bat"""
    if ARCTOOL_EXE.exists():
        return ARCTOOL_EXE
    # Попробовать найти рядом со скриптом
    for p in SCRIPT_DIR.glob("ARCtool*"):
        if p.suffix == ".exe":
            return p
    print("❌ ARCtool.exe не найден! Скачай: https://www.fluffyquack.com/tools/ARCtool.rar")
    sys.exit(1)


def run_arctool(flags: str, target: Path):
    """Запускает ARCtool с указанными флагами"""
    arctool = find_arctool()
    cmd = f'"{arctool}" {flags} "{target}"'
    print(f"▶ {cmd}")
    result = subprocess.run(cmd, shell=True, cwd=str(SCRIPT_DIR),
                          capture_output=True, text=True)
    if result.returncode != 0:
        print(f"⚠ Код возврата: {result.returncode}")
    if result.stdout:
        print(result.stdout[-500:])  # Последние 500 символов
    return result.returncode == 0


def unpack(arc_path: str, with_xfs: bool = True, with_lot: bool = False):
    """Распаковать .arc файл"""
    target = Path(arc_path).resolve()
    if not target.exists():
        print(f"❌ Файл не найден: {target}")
        return False

    flags = ARCTOOL_FLAGS_XFS if with_xfs else "-dd -texRE6 -alwayscomp -pc -txt -v 7"
    if with_lot:
        flags = ARCTOOL_FLAGS_LOT

    backup = target.with_suffix(".arc.backup")
    if not backup.exists():
        shutil.copy2(target, backup)
        print(f"💾 Бэкап: {backup}")

    return run_arctool(flags, target)


def pack(folder_path: str):
    """Запаковать папку в .arc"""
    target = Path(folder_path).resolve()
    if not target.is_dir():
        print(f"❌ Папка не найдена: {target}")
        return False
    return run_arctool(ARCTOOL_FLAGS_XFS, target)


def unpack_all(root_path: str):
    """Рекурсивно распаковать все .arc файлы"""
    root = Path(root_path).resolve()
    count = 0
    for arc in root.rglob("*.arc"):
        print(f"\n{'='*60}")
        print(f"📦 {arc.relative_to(root)}")
        if unpack(str(arc)):
            count += 1
    print(f"\n✅ Распаковано: {count} архивов")


def create_nightmare_arc(game_main_dir: str):
    """
    Создать nightmare_game_main.arc из распакованного game_main.
    Заменяет спавны Gransys на BBI-варианты в LOT-файлах.
    
    ТРЕБУЕТСЯ: предварительно распакованный game_main с флагом -lot
    И запускать скрипт ИЗ ПАПКИ где лежит папка game_main/
    """
    target = Path(game_main_dir).resolve()
    if not target.is_dir():
        print(f"❌ Папка не найдена: {target}")
        return False

    # Таблица замен: Gransys ID → BBI ID
    # Имена файлов из em.txt FluffyQuack
    replacements = {
        # Goblin → Greater Goblin
        "Em0000": "Em2100",
        # Hobgoblin → Greater Hobgoblin
        "Em0100": "Em2110",
        # Wolf → Warg
        "Em0200": "Em3100",
        # Dire Wolf → Garm
        "Em0210": "Em3200",
        # Harpy → Siren
        "Em0300": "Em3300",
        # Snow Harpy → Siren
        "Em0310": "Em3300",
        # Saurian → Giant Saurian / Eliminator
        "Em0400": "Em3400",
        # Undead → Banshee
        "Em0500": "Em3500",
        # Cyclops → Gorecyclops
        "Em0600": "Em4000",
        # Chimera → Gorechimera
        "Em0700": "Em4100",
        # Ogre → Elder Ogre
        "Em0800": "Em4200",
        # Drake → Cursed Dragon
        "Em1000": "Em5000",
    }

    # Ищем LOT-файлы в stage100
    stage100 = target / "scr" / "st100" / "etc"
    modified = 0
    if stage100.exists():
        for lot_file in stage100.rglob("*.lot.txt"):
            content = lot_file.read_text(encoding="utf-8", errors="replace")
            original = content
            for old_id, new_id in replacements.items():
                content = content.replace(old_id, new_id)
            if content != original:
                lot_file.write_text(content, encoding="utf-8")
                modified += 1
                print(f"  🔄 {lot_file.name}: {sum(1 for o,n in replacements.items() if o in original)} замен")

    print(f"\n✅ Модифицировано LOT-файлов: {modified}")
    print(f"📦 Теперь запакуй папку '{target.name}' через: python arctool_helper.py pack {target.name}")
    print(f"   Затем переименуй полученный .arc в nightmare_game_main.arc")
    return True


def edit_ai_files(game_main_dir: str):
    """Показать список AI-файлов для редактирования и открыть в блокноте"""
    target = Path(game_main_dir).resolve()
    if not target.is_dir():
        print(f"❌ Папка не найдена: {target}")
        return False

    print("\n🔥 ===== AI-ФАЙЛЫ ДЛЯ РЕДАКТИРОВАНИЯ =====\n")
    
    # 1. AI Action Parameters
    ai_dir = target / "AI" / "AIPlayerActionParameter"
    if ai_dir.exists():
        print("📂 AI/AIPlayerActionParameter/ (параметры действий пешек):")
        for f in sorted(ai_dir.iterdir()):
            if f.suffix == ".xml":
                print(f"  ✅ {f.name}")
            else:
                print(f"  📄 {f.name}")
    else:
        print("⚠ AI/AIPlayerActionParameter/ не найден. Распакуй с флагом -xfs!")

    # 2. Player params
    print("\n📂 param/pl/other/ (параметры аугментов и способностей):")
    other_dir = target / "param" / "pl" / "other"
    if other_dir.exists():
        for f in sorted(other_dir.glob("*.xml")):
            print(f"  ✅ {f.name}")
    else:
        print("  ⚠ не найден")

    # 3. Level stats
    print("\n📂 param/pl/level/ (статы прокачки):")
    lvl_dir = target / "param" / "pl" / "level"
    if lvl_dir.exists():
        for f in sorted(lvl_dir.glob("*.xml")):
            print(f"  ✅ {f.name}")
    else:
        print("  ⚠ не найден")

    # 4. FSM
    print("\n📂 FSM-файлы (AI-поведение врагов, бинарный формат):")
    for fsm in target.rglob("*.fsm"):
        print(f"  📄 {fsm.relative_to(target)}")
    for fsm in target.rglob("*_AI.*"):
        print(f"  📄 {fsm.relative_to(target)}")

    print("\n💡 Совет: открой .xml файлы в Notepad++ и редактируй.")
    print("   FSM-файлы требуют хекс-редактора и глубокого понимания формата.")
    return True


# ═══════════════════════════════════════════════════════════
# CLI
# ═══════════════════════════════════════════════════════════

def main():
    parser = argparse.ArgumentParser(
        description="ARCtool Helper для DDDA AI Overhaul",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Примеры:
  python arctool_helper.py unpack game_main.arc
  python arctool_helper.py pack game_main
  python arctool_helper.py unpack-all "C:/Steam/steamapps/common/DDDA/nativePC/rom"
  python arctool_helper.py create-nightmare game_main
  python arctool_helper.py edit-ai game_main
        """
    )
    sub = parser.add_subparsers(dest="cmd", required=True)

    sub.add_parser("unpack", help="Распаковать .arc (с -xfs)").add_argument("path")
    sub.add_parser("pack", help="Запаковать папку в .arc").add_argument("path")
    sub.add_parser("unpack-all", help="Рекурсивно распаковать все .arc").add_argument("path")
    p = sub.add_parser("create-nightmare", help="Создать nightmare .arc с BBI-спавнами")
    p.add_argument("path", help="Путь к распакованной папке game_main")
    p = sub.add_parser("edit-ai", help="Показать AI-файлы для редактирования")
    p.add_argument("path", help="Путь к распакованной папке game_main")

    args = parser.parse_args()

    print("🛠  ARCtool Helper for DDDA AI Overhaul")
    print(f"   Папка: {SCRIPT_DIR}")
    print()

    if args.cmd == "unpack":
        unpack(args.path)
    elif args.cmd == "pack":
        pack(args.path)
    elif args.cmd == "unpack-all":
        unpack_all(args.path)
    elif args.cmd == "create-nightmare":
        create_nightmare_arc(args.path)
    elif args.cmd == "edit-ai":
        edit_ai_files(args.path)


if __name__ == "__main__":
    main()
