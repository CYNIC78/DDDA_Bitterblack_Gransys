# Инструкция по сборке в Visual Studio

## Что нужно сделать перед открытием проекта

### 1. Скопировать MinHook и ImGui из референс-репозитория

У нас уже есть склонированный репозиторий `/home/user/ddda-dinput8/`.

**На твоём Windows-компьютере** (где установлен Visual Studio):

1. Склонируй оба репозитория или скопируй папки:
   ```
   git clone https://github.com/kubik-jaroslav/ddda-dinput8.git
   ```

2. Создай папку проекта и скопируй туда:
   - Папку `src/` (все .cpp/.h файлы из нашего проекта)
   - Папки `MinHook/` и `ImGui/` из `ddda-dinput8/`
   - Файлы `dinput8.def`, `ddda_ai_overhaul.ini`
   - Файл проекта `.vcxproj` (создам ниже схему)

3. Также нужны будут файлы из `ddda-dinput8/`:
   - `stdafx.cpp`, `stdafx.h` (прекомпилируемый заголовок)
   - `iniConfig.cpp`, `iniConfig.h` (INI-парсер)
   - `d3d9.cpp`, `d3d9.h` (D3D9 хук для ImGui)
   - `InGameUI.cpp`, `InGameUI.h` (UI-фреймворк)
   - `resource.h`, `steam_api.h`

### 2. Настройка проекта в Visual Studio

#### Конфигурация проекта (.vcxproj):
- **Configuration Type**: Dynamic Library (.dll)
- **Platform**: Win32 (x86) — игра 32-битная!
- **Character Set**: Multi-Byte
- **Platform Toolset**: v141 (VS 2017) или новее
- **Output Name**: `dinput8`

#### Include Directories:
```
$(DXSDK_DIR)Include
$(ProjectDir)ImGui
$(ProjectDir)MinHook
$(ProjectDir)src
```

#### Library Directories:
```
$(DXSDK_DIR)Lib\x86
$(ProjectDir)MinHook
```

#### Additional Dependencies:
```
d3dx9.lib
d3d9.lib
libMinHook-x86-v140-mt.lib  (для Release)
```

#### Preprocessor Definitions:
```
WIN32
NDEBUG  (для Release)
_WINDOWS
_USRDLL
```

#### Module Definition File:
```
dinput8.def
```

### 3. Настройка Post-Build Event (автокопирование в папку игры)

```
xcopy /y /d "$(TargetPath)" "C:\Program Files (x86)\Steam\steamapps\common\DDDA\"
xcopy /y /d "$(ProjectDir)ddda_ai_overhaul.ini" "C:\Program Files (x86)\Steam\steamapps\common\DDDA\"
```

### 4. Порядок файлов в проекте

```
Source Files:
  dinput8.cpp
  PawnAI.cpp
  EnemyAI.cpp
  d3d9.cpp
  InGameUI.cpp
  iniConfig.cpp
  stdafx.cpp
  ImGui/imgui.cpp
  ImGui/imgui_draw.cpp
  ImGui/imgui_impl_dx9.cpp

Header Files:
  dinput8.h
  PawnAI.h
  EnemyAI.h
  d3d9.h
  InGameUI.h
  iniConfig.h
  stdafx.h
  resource.h
  steam_api.h
  MinHook/MinHook.h
  ImGui/imgui.h
  ImGui/imgui_impl_dx9.h
  ImGui/imgui_internal.h
  ImGui/imconfig.h
  ImGui/stb_rect_pack.h
  ImGui/stb_textedit.h
  ImGui/stb_truetype.h

Resource Files:
  dinput8.def (Module Definition File)

Libraries:
  MinHook/libMinHook-x86-v140-mt.lib     (Release)
  MinHook/libMinHook-x86-v140-mtd.lib    (Debug)
```

### 5. Сборка и тестирование

1. Выбери **Release | Win32**
2. Build → Build Solution (Ctrl+Shift+B)
3. Если настроен Post-Build Event, DLL и INI автоматически скопируются в папку игры
4. Запусти игру, нажми F12 — должен появиться UI с вкладками "Pawn AI Overhaul" и "Enemy AI Overhaul"

## Что уже работает

- ✅ Архитектура прокси-DLL (взята из dinput8)
- ✅ Поиск сигнатур pBase и pWorld
- ✅ MinHook инициализация
- ✅ ImGui оверлей с меню
- ✅ PawnAI: чтение/запись инклинаций
- ✅ PawnAI: пресеты поведения
- ✅ EnemyAI: заготовка модуля

## Что нужно доработать

### Ближайшие шаги (Фаза 1):
1. **Скопировать недостающие файлы из dinput8**: stdafx, iniConfig, d3d9, InGameUI, steam_api.h
2. **Настроить .vcxproj** (я создам шаблон)
3. **Скомпилировать и проверить** что DLL загружается
4. **Проверить работу ImGui** (F12 должен показывать меню)
5. **Проверить чтение/запись инклинаций** через UI

### Фаза 2 — Поиск AI-адресов:
1. Запустить игру с Cheat Engine
2. Найти параметры поведения врагов (агрессия, дистанция, лимит атакующих)
3. Задокументировать сигнатуры
4. Добавить хуки в EnemyAI.cpp

### Фаза 3 — Анализ .arc файлов:
1. Скачать ARCtool
2. Распаковать game_main.arc с флагом -xfs
3. Найти AI-параметры в XML
4. Документировать структуру
