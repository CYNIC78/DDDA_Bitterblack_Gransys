# DDDA AI Overhaul — Project Hub

## 📂 Что здесь лежит

```
DDDA_AI_Overhaul/          ← Мастер-папка всего проекта
├── src/                   ← Исходный код модулей (C++)
│   ├── dinput8.cpp        ← Точка входа DLL
│   ├── PawnAI.cpp/.h      ← AI пешек: санитарный кордон, Smart Util, пресеты
│   ├── CombatIntel.cpp/.h ← Боевая разведка: damage-хуки, ring buffer, бестиарий
│   ├── CameraPlus.cpp/.h  ← Тактическая камера + пауза + Free Fly (F4)
│   ├── EnemyAI.cpp/.h     ← ум врага (заготовка; LIVE, не .arc)
│   ├── Nightmare.cpp/.h   ← Bitterblack Gransys (заготовка; политика мира)
│   ├── TargetLock.cpp/.h  ← Авто-aim (ЭКСПЕРИМЕНТАЛЬНЫЙ, отключён)
│   └── BestiaryData.h     ← 72 типа врагов из types.tsv + bestiary.py
├── docs/                  ← Документация
│   ├── README.md          ← Пользовательский README
│   ├── RESEARCH.md        ← Технический анализ архитектуры
│   ├── BUILD_INSTRUCTIONS_RU.md ← Инструкция по сборке
│   ├── ARC_MAP.txt        ← Карта файлов game_main.arc
│   ├── ROADMAP.md         ← север: LIVE / CATALOG / PACK, фазы 0–5
│   ├── DEVTOOLS_VISION.md ← глаза (TypeAtlas + сканер + консоль)
│   ├── TYPE_ATLAS.md      ← 4405 фабрик MT Framework (генератор)
│   ├── FIELD_MAP.md       ← подтверждённые оффсеты полей
│   └── ASSET_FORMATS.md   ← спецификация файлов em0100.arc (XFS вскрыт)
├── resources/             ← Данные реверс-инжиниринга
│   ├── types.tsv          ← Atvaark/DragonsDogma.Research: uEm→groupId
│   ├── bestiary.py.txt    ← chrispurnell/pawn-knowledge: FlagID→bestiaryId
│   └── scanner_player_yaw.CT.txt ← Cheat Engine сканер Yaw
├── tools/                 ← Вспомогательные инструменты
│   ├── arctool_helper.py  ← Python-автоматизатор ARCtool
│   ├── xfs_dump.py        ← парсер XFS: схема + значения (.prp/.shl/.ltg)
│   ├── rst_dump.py        ← парсер .rst: maxHP + пороги нокдауна/флинча
│   ├── lmt_dump.py        ← парсер .lmt: мотионы, кадры, длительность
│   ├── generate_act_map.py ← 812 FSM-состояний врагов из TypeAtlas
│   └── find_type.py       ← поиск рычагов в атласе по смыслу/размеру
├── MinHook/               ← MinHook (x86 hooking library)
├── ImGui/                 ← Dear ImGui (UI overlay)
├── ddda-ai-overhaul.vcxproj ← Проект Visual Studio (Release Win32)
├── ddda_ai_overhaul.ini   ← Конфиг мода
├── dinput8.def            ← Экспорты DLL
├── .gitignore             ← Исключения для git
└── LICENSE                ← MIT
```

## 🧭 Север проекта

Мы — эмерджентный **рантайм**-мод. Диск — аптека, не игра.

- [docs/ROADMAP.md](docs/ROADMAP.md) — кто мы, полки LIVE / CATALOG / PACK, фазы 0–5, что не делаем
- [docs/DEVTOOLS_VISION.md](docs/DEVTOOLS_VISION.md) — глаза: атлас, сканер, консоль
- [docs/TYPE_ATLAS.md](docs/TYPE_ATLAS.md) — 4405 фабрик
- [docs/FIELD_MAP.md](docs/FIELD_MAP.md) — только подтверждённые поля
- [docs/ASSET_FORMATS.md](docs/ASSET_FORMATS.md) — формат XFS: 72 поля гоблина с японскими именами Capcom, сигнатуры для скана

Дамп файлов игры:

```bash
python3 tools/xfs_dump.py --values <файл.prp|.shl|.ltg>   # схема + значения (XFS)
python3 tools/rst_dump.py <файл.rst>                      # HP и пороги сбивания
python3 tools/lmt_dump.py <файл.lmt>                      # мотионы: id, кадры, длительность
python3 tools/generate_act_map.py                         # -> src/ActMap.Generated.h
python3 tools/find_type.py --em 0100 --cat taunt          # поиск рычагов в атласе
python3 tools/find_type.py --size 320                     # найти тип по sizeof
```

**ActMap** — 812 FSM-состояний с именами (`cEm0100ActThreatHowl` = таунт гоблина,
`cEm0100ActDie` = смерть). Смерть врага определяется состоянием, а не HP.

Перегенерация атласа: `python tools/generate_type_atlas.py`

## 🔑 Ключевые оффсеты

```
pBase → *(сигнатура + 2)
├── +0xA7000: База данных ГГ
│   ├── +0x7F0: Главная пешка
│   │   ├── +0x96C+0x1224: Инклинации (9×float, шаг 0xC)
│   │   └── +0x1616: mStudyFlag (322 байта)
│   ├── +0x7F0+0x1660: Пешка 1
│   └── +0x7F0+0x1660×2: Пешка 2
├── +0xB8780: Погода
└── +0xB33A8: Флаг пост-игры
```

## 🎮 Горячие клавиши

| Клавиша | Действие |
|---|---|
| F12 | Открыть меню настройки |
| F4 | Тактическая камера (toggle) |
| Стрелки + PgUp/PgDn | Ручное перемещение камеры (в режиме F4) |
| Num 0 | Пауза игры (toggle) |
| Num + / Num - | Ускорить/замедлить скорость паузы |

## 🧠 Модули

| Модуль | Статус | Описание |
|---|---|---|
| PawnAI v2.8 | ✅ | Санитарный кордон + Smart Utilitarian + Custom Anchors (слайдеры-якоря + живые бары) + 6 пресетов + Tactical Switch |
| CombatIntel v2.8 | ✅ | Универсальный хук урона (DDDA+374739), перехват пешек (42 хита), ring buffer, бестиарий 72 врага |
| CameraPlus v2.2 | ✅ | Тактическая камера F4 + Free Fly + Пауза + Disable Auto-Correction (R&D: Camera Distance) |
| EnemyAI | 🔜 | Статический .arc редактор (заготовка) |
| Nightmare | 🔜 | Bitterblack Gransys (заготовка) |
| TargetLock | 📋 | Авто-aim (отключён, нужен правильный Yaw-адрес) |

## 📊 Бестиарий

Источники:
- **Atvaark types.tsv** → uEmXXXX → groupId (из DDDA.exe)
- **chrispurnell bestiary.py** → bestiaryId → FlagID (из DDDA.sav)
- **Наши тесты** → bestiaryId → mStudyIdx (из памяти пешки)

72 врага в `BestiaryData.h`. Подтверждены: Goblin, Wolf, Skeleton, Saurian, Undead, Harpy.

## 🔧 Как пересобрать

1. Открыть `ddda-ai-overhaul.vcxproj` в Visual Studio
2. Выбрать **Release | Win32**
3. **Ctrl+Shift+B**
4. `dinput8.dll` в `Release/`

## 🙏 Благодарности

- **kubik-jaroslav** — ddda-dinput8 (архитектура)
- **Cielos** — Cheat Engine таблица
- **Atvaark** — DragonsDogma.Research (types.tsv)
- **chrispurnell** — pawn-knowledge (bestiary.py)
- **Lefein** — World Difficulty (пионер AI-моддинга)
- **FluffyQuack** — ARCtool
- **TsudaKageyu** — MinHook
- **ocornut** — Dear ImGui
