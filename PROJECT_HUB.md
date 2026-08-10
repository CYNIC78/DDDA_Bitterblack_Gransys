# DDDA AI Overhaul — Project Hub

## 📂 Что здесь лежит

```
DDDA_AI_Overhaul/          ← Мастер-папка всего проекта
├── src/                   ← Исходный код модулей (C++)
│   ├── dinput8.cpp        ← Точка входа DLL
│   ├── PawnAI.cpp/.h      ← AI пешек: санитарный кордон, Smart Util, пресеты
│   ├── CombatIntel.cpp/.h ← Боевая разведка: damage-хуки, ring buffer, бестиарий
│   ├── CameraPlus.cpp/.h  ← Тактическая камера + пауза + Free Fly (F4)
│   ├── EnemyAI.cpp/.h     ← Статический .arc редактор (заготовка)
│   ├── Nightmare.cpp/.h   ← Bitterblack Gransys (заготовка)
│   ├── TargetLock.cpp/.h  ← Авто-aim (ЭКСПЕРИМЕНТАЛЬНЫЙ, отключён)
│   └── BestiaryData.h     ← 72 типа врагов из types.tsv + bestiary.py
├── docs/                  ← Документация
│   ├── README.md          ← Пользовательский README
│   ├── RESEARCH.md        ← Технический анализ архитектуры
│   ├── BUILD_INSTRUCTIONS_RU.md ← Инструкция по сборке
│   └── ARC_MAP.txt        ← Карта файлов game_main.arc
├── resources/             ← Данные реверс-инжиниринга
│   ├── types.tsv          ← Atvaark/DragonsDogma.Research: uEm→groupId
│   ├── bestiary.py.txt    ← chrispurnell/pawn-knowledge: FlagID→bestiaryId
│   └── scanner_player_yaw.CT.txt ← Cheat Engine сканер Yaw
├── tools/                 ← Вспомогательные инструменты
│   └── arctool_helper.py  ← Python-автоматизатор ARCtool
├── MinHook/               ← MinHook (x86 hooking library)
├── ImGui/                 ← Dear ImGui (UI overlay)
├── ddda-ai-overhaul.vcxproj ← Проект Visual Studio (Release Win32)
├── ddda_ai_overhaul.ini   ← Конфиг мода
├── dinput8.def            ← Экспорты DLL
├── .gitignore             ← Исключения для git
└── LICENSE                ← MIT
```

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
| PawnAI v2.1 | ✅ | Санитарный кордон + Smart Utilitarian + 6 пресетов + Tactical Switch |
| CombatIntel v2.1 | ✅ | 3 damage-хука, ring buffer, бестиарий 72 врага |
| CameraPlus v2.2 | ✅ | Тактическая камера F4 + Free Fly + Пауза + Disable Auto-Correction |
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
