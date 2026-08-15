# DDDA AI Overhaul

[![Game](https://img.shields.io/badge/Game-Dragon's%20Dogma%20Dark%20Arisen-blue)](https://store.steampowered.com/app/367500)
[![Platform](https://img.shields.io/badge/Platform-Windows%20(Steam%2FGOG)-lightgrey)]()
[![License](https://img.shields.io/badge/License-MIT-green)]()
[![Status](https://img.shields.io/badge/Status-Development-orange)]()

**AI Overhaul для Dragon's Dogma: Dark Arisen** — улучшение интеллекта пешек и монстров без читов.

Основан на архитектуре [ddda-dinput8](https://github.com/kubik-jaroslav/ddda-dinput8), но **не является чит-модом**. Все изменения направлены только на геймплейные улучшения AI и удобство игры.

---

## 🧠 Философия

Мы — **эмерджентный рантайм-мод**. Не чит и не пак файлов.

```
Умеем в рантайме — делаем в рантайме. Диск — аптека, не игра.
```

**Три полки:**
| Полка | Что это | Пример |
|---|---|---|
| **LIVE** | Читаем/пишем процесс. Эмерджентно, обратимо, без рестарта | Инклинации, mStudyFlag, хук урона, погода, пауза, камера |
| **CATALOG** | Мёртвые таблицы, которые LIVE читает как политику | types.tsv → TypeAtlas, bestiary.py → BestiaryData |
| **PACK** | Файлы, без которых движок не умеет инстанциировать объект | sidecar .gpl / LOT для типов, которых нет в зоне |

**Четыре вопроса для любой фичи:**
1. Можем **увидеть** это живым? → Atlas / Scan / хук.
2. Можем **изменить** живым? → запись поля / подмена аргумента.
3. Движок **отказывается создать** объект? → тогда PACK.
4. PACK никогда не включает себя сам. Его включает LIVE-политика.

---

## 🏗 Архитектура

```
┌─────────────────────────────────────────────────────────┐
│  CombatBus  (шина событий — мегафон тренера)             │
│  CombatIntel ──Publish──► CombatBus ──Subscribe──► PawnAI│
│  WorldScan   ──PublishWorld──► (не топчет hit)           │
└──────────────────────────┬──────────────────────────────┘
                           │
┌──────────────────────────▼──────────────────────────────┐
│  PawnAI Orchestrator  (тик 150 мс, модули в SEH)         │
│  ├── SanitaryCordon    — динамический потолок мусора     │
│  ├── SmartUtilitarian  — знание → вес Utilitarian        │
│  ├── PresetManager     — 7 профилей + custom + lerp      │
│  └── TacticalSwitch    — категория врага → пресет        │
└──────────────────────────────────────────────────────────┘
```

### Ключевые принципы

- **Модульность с изоляцией**: каждый модуль работает в своём SEH-блоке.
  Падение одного **не роняет** соседей и не роняет игру.
- **Шина, а не прямой вызов**: модули подписываются на CombatBus,
  а не вызывают друг друга.
- **Глаза, нервы, органы**: TypeAtlas + WorldScan = глаза,
  CombatBus = нервы, PawnAI/EnemyAI/Nightmare = органы.
- **Потокобезопасность**: шина защищена SRWLOCK,
  запись в память — через `__try/__except`.

---

## ✅ Текущий статус модулей

| Модуль | Версия | Статус | Описание |
|---|---|---|---|
| **PawnAI** | v2.8 | ✅ Работает | Санитарный кордон + Smart Utilitarian + Custom Anchors (слайдеры-якоря + живые бары) + 7 пресетов + Tactical Switch |
| **CombatIntel** | v2.8 | ✅ Работает | Универсальный хук урона (3 сигнатуры игрока + 1 универсальный health write), ring buffer 64 слота, debounce суб-тиков, бестиарий 72 врага |
| **CameraPlus** | v2.2 | ✅ Работает | Тактическая камера (F4/MMB) + Free Fly + Пауза + Disable Auto-Correction |
| **EntityConfig** | v1.0 | ✅ Работает | `ddda_entities.ini` — трёхуровневый конфиг ([default]→[class.*]→[emXXXX]) с hot-reload по mtime. Масштаб (+0x60/64/68), поводок (таймеры возврата), разброс на особь |
| **EnemyAI** | stub | 🔜 Заготовка | UI-слайдеры без биндинга. Ждёт `cThinkMgr`/`cCharParamEnemy` |
| **Nightmare** | stub | 🔜 Заготовка | UI + ручной триггер + политика замен. Ждёт час ночи + хук emId |
| **DevTools** | v0.6 | 🔧 Dev only | TypeAtlas (4405 фабрик) + WorldScan + Inspector + Dump/HUNT. Включается `[devtools] enabled = on` |

---

## ⌨️ Горячие клавиши

| Клавиша | Действие |
|---|---|
| **F12** | Открыть меню настройки (ImGui-оверлей) |
| **F4 / MMB** | Тактическая камера (toggle) |
| **Стрелки + PgUp/PgDn** | Ручное перемещение камеры (в режиме F4) |
| **Num 0** | Пауза игры (toggle) |
| **Num + / Num -** | Ускорить/замедлить скорость паузы |
| **F5** | Сохранить игру (штатный сейв) |
| **F9** | Чекпоинт |
| **M / J / U / K** | Быстрое открытие карты/журнала/экипировки/статуса |

*Все хоткеи настраиваются в секции `[hotkeys]` конфига.*

---

## 📦 Установка

### Быстрая установка (готовый DLL)

1. Скачай последний релиз со [страницы Releases](https://github.com/YOUR_USER/ddda-ai-overhaul/releases)
2. Скопируй **файлы** в корневую папку игры (где `DDDA.exe`):
   - `dinput8.dll` — основной мод
   - `ddda_ai_overhaul.ini` — конфиг
   - `ddda_entities.ini` — настройки мутаций врагов (опционально)
3. Если у тебя уже установлен другой `dinput8.dll` — переименуй его в `dinput8_OLD.dll`
   и пропиши в `ddda_ai_overhaul.ini`: `loadLibrary = dinput8_OLD.dll`
4. Запусти игру
5. Нажми **F12** для открытия меню настройки

### Сборка из исходников

```
1. Открыть ddda-ai-overhaul.sln в Visual Studio (2017/2019/2022)
2. Выбрать Release | Win32
3. Ctrl+Shift+B
4. dinput8.dll в Release/
```

Подробная инструкция: [BUILD_INSTRUCTIONS.md](docs/BUILD_INSTRUCTIONS_RU.md)

---

## ⚙️ Конфигурация

### `ddda_ai_overhaul.ini` — настройки мода

```ini
[pawnAI]
enabled = on          # мастер-выключатель AI пешек
presetsEnabled = on   # пресеты инклинаций
sanitary = on         # санитарный кордон (режет мусорные инклинации)
smartUtil = on        # Smart Utilitarian (адаптация под знание)
tactical = on         # авто-смена пресета под категорию врага
preset = 5            # 0-6 (6 = Custom Anchor)
smooth = 0.1          # плавность лерпа (0.01..1.0)

[camera]
freeCam = off         # тактическая камера
freeFly = off         # ручной полёт
flySpeed = 2.0        # скорость перемещения
pause = off           # пауза
pauseSpeed = 0.0001   # скорость паузы
noAutoCorrect = off   # отключить авто-коррекцию камеры

[combatIntel]
enabled = on          # боевая разведка

[nightmare]
enabled = off         # модуль Кошмара (выключен по умолчанию)

[inGameUI]
enabled = on          # оверлей F12

[devtools]
enabled = off         # режим разработчика (только для моддера)
```

### `ddda_entities.ini` — мутации врагов (горячая перезагрузка)

Трёхуровневая система: `[default]` → `[class.small/large/boss]` → `[em0100]`.

```ini
[global]
enabled = on
allowWrites = off      # ВКЛЮЧАТЬ ОСОЗНАННО! По умолчанию только чтение.

[default]
; Множители для всех врагов (1.0 = ванилла)
speedMin = 1.0
speedMax = 1.0
scaleMin = 1.0
scaleMax = 1.0
scaleJitter = 0.0       # неуниформность W/H/D

[class.small]
scaleMin = 0.85
scaleMax = 1.15
scaleJitter = 0.1

[em0100]
sightRadius = 0          # 0 = ваниль (1500)
sightAngle = 90          # расширяем конус 60° → 90°
speedMin = 1.0
speedMax = 1.2
leashScale = 0.8         # короче поводок
```

*Файл создаётся автоматически при первом запуске с комментариями.*
*Изменения подхватываются через ~1 сек после сохранения (hot-reload).*

---

## 🧰 Что под капотом

### Технические детали

- **Прокси-DLL**: `dinput8.dll` — подмена системной DLL с пробросом вызовов
- **MinHook**: перехват функций (x86 detours)
- **Dear ImGui + D3D9**: оверлей поверх игры
- **Сигнатурный поиск**: нахождение `pBase`, `pWorld` и других структур
- **TypeAtlas**: 4405 фабрик MT Framework с именами, RVA, размерами
- **DTI (MT Framework)**: каждый живой объект сам сообщает своё имя класса — 
  идентификация не по захардкоженным vtable, а через систему RTTI движка
- **FieldMap**: 72 поля характеристик врага (XFS-формат Capcom с японскими именами)
- **ActMap**: 812 FSM-состояний с именами (смерть по состоянию, а не по HP)

### Ключевые оффсеты

```
pBase → *(сигнатура + 2)
├── +0xA7000: База данных персонажа
│   ├── +0x7F0: Главная пешка
│   │   ├── +0x96C+0x1224: Инклинации (9×float, шаг 0xC)
│   │   └── +0x1616: mStudyFlag (322 байта)
│   ├── +0x7F0+0x1660: Пешка 1
│   └── +0x7F0+0x1660×2: Пешка 2
├── +0xB8780: Погода (0=ясно, 1=облачно, 2=туман, 3=вулкан)
└── +0xB33A8: Флаг пост-игры

Тело uEm* (29632 байт):
├── +0x40/44/48: XYZ координаты
├── +0x60/64/68: Масштаб W/H/D (живой, каждый кадр)
├── +0x2DC0: cActBank (банк действий)
├── +0x2DC8: Текущий Act (FSM-состояние, стабильный адрес)
├── +0x2E64: cAICtrl (704 B — принятие решений)
├── +0x5870: cCharParamEnemy (таймеры поводка, масштаб)
└── +0x73C0: Размер тела
```

---

## 📚 Документация

| Файл | О чём |
|---|---|
| [PROJECT_HUB.md](PROJECT_HUB.md) | Карта проекта, модули, ключевые оффсеты |
| [ROADMAP.md](docs/ROADMAP.md) | **Живой север** — фазы 0-5, полки, «правило четырёх вопросов» |
| [ARCHITECTURE_VISION.md](docs/ARCHITECTURE_VISION.md) | Философия: 3 слоя, FSM сверху, ini-архитектура |
| [RESEARCH.md](docs/RESEARCH.md) | Технический анализ архитектуры DDDA и dinput8 |
| [MUTATION_ARCHITECTURE.md](docs/MUTATION_ARCHITECTURE.md) | Act-объекты, placement new, cAICtrl, карта врага |
| [DEVTOOLS_VISION.md](docs/DEVTOOLS_VISION.md) | TypeAtlas, WorldScan, DevConsole — глаза проекта |
| [FIELD_MAP.md](docs/FIELD_MAP.md) | Подтверждённые оффсеты и структуры |
| [ASSET_FORMATS.md](docs/ASSET_FORMATS.md) | Формат XFS, sn2-сенсоры, rst, lmt |
| [BUILD_INSTRUCTIONS_RU.md](docs/BUILD_INSTRUCTIONS_RU.md) | Сборка в Visual Studio |

---

## 🧭 Север проекта

Подробно: [ROADMAP.md](docs/ROADMAP.md). Кратко:

```
Фаза 0 — Глаза (бедный REFramework)
  ✅ TypeAtlas (4405 имён)
  ✅ WorldScan (присутствие до удара)
  🔜 Inspector + DevConsole
  🔜 Singleton resolve (sEnemyManager, sUnit)

Фаза 1 — Нервная система пешки
  ✅ Sanitary Cordon, Smart Utilitarian, Presets, Tactical Switch
  🔜 Пешки 1 и 2
  🔜 Политики, не пресеты

Фаза 2 — Политика мира (Nightmare)
  🔜 Ночь + погода в тике
  🔜 Триггер Деймона
  🔜 Хук emId — замена на лету

Фаза 3 — Ум врага (LIVE)
  🔜 cThinkMgr / cCharParamEnemy
  🔜 Поля агрессии, цели, таймеры
  🔜 AIPlActParam — веса из XML в память

Фаза 4 — PACK (только когда движок отказался)
  🔜 sidecar .gpl / LOT для отсутствующих типов

Фаза 5 — Открытый каркас для сообщества
```

---

## ⚠️ Известные ограничения

- **Инклинации через dinput8**: установка ВСЕХ инклинаций в 1000 ломает AI пешки. Наш мод использует градиентные значения.
- **Совместимость с другими dinput8-модами**: можно использовать через `loadLibrary` в `[main]` секции .ini
- **Онлайн**: изменение статов пешки может привести к бану; изменение AI-параметров — безопасно
- **Не альфа**: мод в активной разработке, архитектура стабильна, но код — proof-of-concept.
  Мы строим платформу для рантайм-модификаций, а не готовый продукт.

---

## 🙏 Благодарности

- **kubik-jaroslav** — автор [ddda-dinput8](https://github.com/kubik-jaroslav/ddda-dinput8)
- **Cielos** — Cheat Engine таблица с адресами
- **Atvaark** — [DragonsDogma.Research](https://github.com/Atvaark/DragonsDogma.Research) (types.tsv)
- **chrispurnell** — pawn-knowledge (bestiary.py)
- **Lefein (Lefein_Noel)** — World Difficulty, пионер AI-моддинга DDDA
- **FluffyQuack** — ARCtool
- **TsudaKageyu** — MinHook
- **ocornut** — Dear ImGui

## 📄 Лицензия

MIT — делайте что хотите, но упоминайте авторов.