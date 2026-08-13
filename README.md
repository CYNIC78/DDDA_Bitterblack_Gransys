# DDDA AI Overhaul

[![Game](https://img.shields.io/badge/Game-Dragon's%20Dogma%20Dark%20Arisen-blue)](https://store.steampowered.com/app/367500)
[![Platform](https://img.shields.io/badge/Platform-Windows%20(Steam%2FGOG)-lightgrey)]()
[![License](https://img.shields.io/badge/License-MIT-green)]()

**AI Overhaul для Dragon's Dogma: Dark Arisen** — улучшение интеллекта пешек и монстров без читов.

Основан на архитектуре [ddda-dinput8](https://github.com/kubik-jaroslav/ddda-dinput8), но **не является чит-модом**. Все изменения направлены только на геймплейные улучшения AI и удобство игры.

Мод работает на Steam и GoG версиях игры. Поддерживает цепочку с оригинальным динпут8 через `loadLibrary`.

---

## ⌨️ Горячие клавиши

| Клавиша | Действие |
|---|---|
| **F12** | Открыть меню настройки |
| **F4** | Тактическая камера (toggle) |
| **Стрелки + PgUp/PgDn** | Ручное перемещение камеры (в режиме F4) |
| **Num 0** | Пауза игры (toggle) |
| **Num + / Num -** | Ускорить/замедлить скорость паузы |

---

## 📦 Установка

### Быстрая установка (готовый DLL)

1. Скачай последний релиз со [страницы Releases](https://github.com/YOUR_USER/ddda-ai-overhaul/releases)
2. Скопируй **оба файла** в корневую папку игры (где `DDDA.exe`):
   - `dinput8.dll`
   - `ddda_ai_overhaul.ini`
3. Если у тебя уже установлен другой `dinput8.dll` — переименуй его в `dinput8_OLD.dll`
4. Запусти игру
5. Нажми **F12** для открытия меню настройки

### Сборка из исходников

См. [BUILD_INSTRUCTIONS_RU.md](BUILD_INSTRUCTIONS_RU.md)

---

## 🧠 Возможности

### Pawn AI Overhaul (v2.0)

| Функция | Описание |
|---|---|
| **Санитарный кордон** | «Мусорные» инклинации (Guardian, Nexus, Acquisitor) автоматически ограничиваются. Порог — динамический: 3-я по величине полезная инклинация. Никаких хардкод-чисел |
| **Smart Utilitarian** | Вес Utilitarian адаптируется под реальные знания пешки о врагах. В бою сверяется с `mStudyFlag` — если враг незнаком, вес перенаправляется в Scather + Challenger |
| **Пресеты** | 6 готовых профилей поведения: Boss Killer, Crowd Control, Tactical Support, Ranged Hunter, Explorer, Balanced |
| **Плавные переходы** | Инклинации меняются мягко, без резких скачков, сохраняя «естественность» AI |

### Combat Intel (боевая разведка)

| Функция | Описание |
|---|---|
| **Damage-хуки** | Перехватывает ВСЕ события урона (3 сигнатуры: игрок, пешка, другое) |
| **Ring buffer** | Отслеживает какие враги находятся в бою прямо сейчас |
| **Сверка с mStudyFlag** | Для каждого типа врага проверяет — знает ли его пешка |

### 📷 Camera Plus — Тактическая камера + Пауза

| Функция | Описание |
|---|---|
| **Тактическая камера (F4)** | Камера застывает в пространстве, но продолжает следить за ГГ. Идеально для скриншотов и тактического обзора |
| **Ручной полёт** | Стрелки + PgUp/PgDn перемещают камеру в любом направлении |
| **Пауза игры (Num0)** | Мгновенная заморозка игрового мира. Num+/Num- регулируют скорость (0.00001 = пауза, 0.1 = slow-mo) |
| **Безопасный toggle** | Все хоткеи на MinHook и GetAsyncKeyState — не конфликтуют с F12, Alt+F4 работает штатно |

### Enemy AI Overhaul (в работе)

Живые параметры think врагов (агрессия, цели, веса) — слой поверх ванили, без замены `.arc` игрока.

### Nightmare: Bitterblack Gransys (в работе)

После убийства Деймона Грансис становится «большим Островом» — ночь, погода и политика замены монстров в **рантайме**. Файлы игры не подменяются. То, чего движок физически не умеет создать в зоне, подключается отдельно и только по флагу модуля.

---

## 🎮 Как пользоваться

1. Запусти игру, загрузи сейв
2. Нажми **F12** — откроется меню настройки
3. Вкладки:
   - **Pawn AI Overhaul v2.0** — все настройки инклинаций и санитарного кордона
   - **Combat Intel** — просмотр активных врагов в бою
   - **Camera Plus** — тактическая камера, free fly, пауза
   - **Enemy AI Overhaul** — ум врагов (в работе)
   - **Nightmare: Bitterblack Gransys** — Кошмар, выключен по умолчанию

Все настройки сохраняются в `ddda_ai_overhaul.ini` автоматически.

---

## ⚙️ Конфигурация

Файл `ddda_ai_overhaul.ini` — все настройки с комментариями:

```ini
[pawnAI]
enabled = on          # мастер-выключатель
presetsEnabled = on   # использовать пресеты инклинаций
sanitary = on         # санитарный кордон
smartUtil = on        # Smart Utilitarian
preset = 5            # пресет (0-5)
smooth = 0.1          # плавность переходов

[camera]
freeCam = off          # тактическая камера (F4)
detach = off           # отвязка камеры
freeFly = off          # ручной полёт
flySpeed = 2.0         # скорость стрелок
flySpeedZ = 2.0        # скорость PgUp/PgDn
pause = off            # пауза (Num0)
pauseSpeed = 0.0001    # скорость паузы

[combatIntel]
enabled = on          # боевая разведка
timeout = 5           # секунд до выхода врага из боя

[enemyAI]
enabled = on          # ум врагов (рантайм, когда поля найдены)

[nightmare]
enabled = off         # Кошмар (выключен по умолчанию)

[inGameUI]
enabled = on          # оверлей F12
```

---

## 🔧 Как это работает (технически)

Мод использует технику **прокси-DLL**: игра загружает `dinput8.dll` для DirectInput, а мы подменяем её своей DLL, которая:

1. Экспортирует `DirectInput8Create` и пробрасывает вызов в настоящую системную DLL
2. При загрузке ищет в памяти игры сигнатуры для нахождения ключевых структур данных (`pBase`, `pWorld`)
3. Через [MinHook](https://github.com/TsudaKageyu/minhook) перехватывает нужные функции
4. [Dear ImGui](https://github.com/ocornut/imgui) рендерит оверлей поверх игры (F12)

### Документированные оффсеты

```
pBase → *(сигнатура + 2)
├── +0xA7000: База данных персонажа
│   ├── +0x7F0: Главная пешка
│   │   ├── +0x96C+0x1224: Инклинации (9×float, шаг 0xC)
│   │   └── +0x1616: mStudyFlag (322 байта)
│   ├── +0x7F0+0x1660: Пешка 1
│   └── +0x7F0+0x1660×2: Пешка 2
├── +0xB8780: Погода
└── +0xB33A8: Флаг пост-игры
```

---

## 📊 Дорожная карта

Живой север: [docs/ROADMAP.md](docs/ROADMAP.md). Коротко:

```
✅ Нерв пешки: пресеты, кордон, Smart Utilitarian, Tactical Switch, шина
✅ Камера + пауза
🟡 Глаза (TypeAtlas сгенерен, сканер мира ещё нет)
🔜 Политика мира (Nightmare: ночь/погода LIVE, замена через хук emId)
🔜 Ум врага (живые поля think, не своп .arc)
📋 PACK только если движок отказывается создать объект
```

Мы не чит и не пак файлов. Что умеем в рантайме — делаем в рантайме.

---

## ⚠️ Предупреждения

- **Инклинации через dinput8**: установка ВСЕХ инклинаций в 1000 ломает AI пешки. Наш мод использует градиентные значения.
- **Совместимость с другими dinput8-модами**: можно использовать через `loadLibrary` в `[main]` секции .ini
- **Онлайн**: изменение статов пешки может привести к бану; изменение AI-параметров через `.arc` — безопасно

---

## 📂 Структура проекта

```
ddda-ai-overhaul/
├── src/                    # Исходный код модулей
│   ├── dinput8.cpp         # Точка входа DLL
│   ├── PawnAI.cpp/.h       # AI пешек
│   ├── EnemyAI.cpp/.h      # AI врагов
│   ├── CombatIntel.cpp/.h  # Боевая разведка
│   ├── CameraPlus.cpp/.h   # Тактическая камера + пауза
│   └── Nightmare.cpp/.h    # Bitterblack Gransys
├── d3d9.cpp/h              # D3D9 хук (ImGui)
├── InGameUI.cpp/h          # UI-фреймворк
├── iniConfig.cpp/h         # INI-парсер
├── Hotkeys.cpp/h           # Горячие клавиши
├── stdafx.cpp/h            # Прекомпилируемый заголовок
├── dinput8.def             # Экспорты DLL
├── MinHook/                # Библиотека MinHook
├── ImGui/                  # Dear ImGui
├── ddda_ai_overhaul.ini    # Конфигурация
├── ddda-ai-overhaul.vcxproj # Проект VS
├── arctool_helper.py       # Python-автоматизатор ARCtool
├── ARC_MAP.txt             # Карта файлов game_main.arc
├── RESEARCH.md             # Технический анализ
└── BUILD_INSTRUCTIONS_RU.md # Инструкция по сборке
```

---

## 🙏 Благодарности

- **kubik-jaroslav** — автор [ddda-dinput8](https://github.com/kubik-jaroslav/ddda-dinput8)
- **Cielos** — автор Cheat Engine таблицы с адресами
- **Lefein (Lefein_Noel)** — автор World Difficulty, пионер AI-моддинга DDDA
- **FluffyQuack** — ARCtool
- **TsudaKageyu** — MinHook
- **ocornut** — Dear ImGui
- **arena.ai** — LLM model as coding assistant
- **Gemini** — LLM model by Google for researches

## 📄 Лицензия

MIT — делайте что хотите, но упоминайте авторов.
