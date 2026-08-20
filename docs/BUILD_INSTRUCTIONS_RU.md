# Сборка DDDA AI Overhaul (Release | Win32)

Единственный канонический документ по сборке. Раньше информация была размазана по
`BUILD_THIS.txt`, `!ИНСТРУКЦИЯ_ПО_СБОРКЕ.txt`, `README_MODULAR.txt` и этому файлу —
теперь всё здесь.

---

## 0. Что нужно установить

- **Visual Studio 2017 / 2019 / 2022** (Community подойдёт).
  При установке обязательна нагрузка **«Разработка классических приложений на C++»**
  (Desktop development with C++).
- **DirectX SDK (June 2010)** — *опционально*. Проект ссылается на `$(DXSDK_DIR)`,
  но современный Windows SDK обычно закрывает потребности. См. раздел «Если ругается на d3dx9.h».

Ничего копировать из сторонних репозиториев больше **не нужно**: `MinHook/`, `ImGui/`,
`stdafx`, `iniConfig`, `d3d9`, `InGameUI` уже лежат в дереве проекта.

---

## 1. Сборка

1. Открыть `ddda-ai-overhaul.sln` (или `.vcxproj`) в Visual Studio.
   Если VS предложит **Retarget Projects** — согласиться, это нормально.
2. Вверху выбрать конфигурацию: **Release** и платформу **Win32**.
   Игра 32-битная — `x64` не собирать, `Debug` для игры не нужен.
3. **Build → Build Solution** (`Ctrl+Shift+B`).
4. Успех выглядит так:

   ```text
   ========== Build: 1 succeeded, 0 failed ==========
   ```

   Результат: `Release\dinput8.dll`.

---

## 2. Установка в игру

Скопировать в папку с `DDDA.exe`:

| Откуда | Куда |
|---|---|
| `Release\dinput8.dll` | корень игры |
| `ddda_ai_overhaul.default.ini` | НЕ копировать. Справочник ключей; рабочий ini мод создаёт и пополняет сам |
| `ddda_entities.ini` | корень игры |

### Если в игре уже стоит другой `dinput8.dll`

Две DLL с одним именем в папке не уживаются. Правильный путь — цепочка загрузки:

1. Переименовать старый мод в `dinput8_OLD.dll`.
2. Положить наш `dinput8.dll`.
3. В `ddda_ai_overhaul.ini` прописать:

   ```ini
   loadLibrary = dinput8_OLD.dll
   ```

Тогда наш мод — главный, а старый подгружается как дополнительная библиотека.

---

## 3. Проверка после запуска

1. Запустить игру, нажать **F12** — должна открыться панель мода с вкладками
   (Pawn AI Overhaul, Combat Intel, Enemy AI Overhaul, Nightmare, Camera Plus).
2. **Live Inclinations** — раскрыть; должны показываться реальные значения инклинаций
   главной пешки. Видны числа → оффсеты живы.
3. **Combat Intel** — включить галку, ударить врага; в Damage Ring Buffer появится
   запись с `group=XX`.
4. **Camera Plus** — `F4`, стрелки + PgUp/PgDn, слайдер party cam (Аризен ↔ пешка).

DevTools по умолчанию **выключен** (`[devtools] enabled = off`) — он стоит 150-мс обхода
и обычному игроку не нужен. Для разработки включается в `ddda_ai_overhaul.ini`.

---

## 4. Настройки проекта (справка, если .vcxproj пересоздаётся с нуля)

| Параметр | Значение |
|---|---|
| Configuration Type | Dynamic Library (.dll) |
| Platform | Win32 (x86) |
| Character Set | Multi-Byte |
| Platform Toolset | v141 (VS 2017) или новее |
| Output Name | `dinput8` |
| Module Definition File | `dinput8.def` |

**Include Directories**

```text
$(DXSDK_DIR)Include
$(ProjectDir)ImGui
$(ProjectDir)MinHook
$(ProjectDir)src
```

**Library Directories**

```text
$(DXSDK_DIR)Lib\x86
$(ProjectDir)MinHook
```

**Additional Dependencies**

```text
d3dx9.lib
d3d9.lib
libMinHook-x86-v140-mt.lib    (Release)
libMinHook-x86-v140-mtd.lib   (Debug)
```

**Preprocessor Definitions**: `WIN32`, `NDEBUG` (Release), `_WINDOWS`, `_USRDLL`.

### Post-Build Event (автокопирование в игру, опционально)

```text
xcopy /y /d "$(TargetPath)" "C:\Program Files (x86)\Steam\steamapps\common\DDDA\"
```

> **НИКОГДА не копируйте ini этим шагом.**
>
> Раньше вторая строка была такой:
> `xcopy /y /d "$(ProjectDir)ddda_ai_overhaul.ini" "...\DDDA\"` — и она
> затирала настройки тестера **при каждой сборке**. Отсюда и жалоба «мне
> каждый новый тест выставлять настройки заново».
>
> Ini теперь вообще не едет со сборкой: в репозитории лежит
> `ddda_ai_overhaul.default.ini` — справочник со всеми ключами и
> комментариями. Он называется иначе и ничего не затирает.
>
> Рабочий `ddda_ai_overhaul.ini` в папке игры мод ведёт сам: недостающие
> ключи дописываются с умолчаниями при запуске (см. `iniConfig.cpp`, блок
> «ДОСЫЛКА НЕДОСТАЮЩИХ КЛЮЧЕЙ»), а выставленные значения не трогаются
> никогда.
>
> **Если этот шаг уже прописан в вашем проекте — удалите вторую строку.**

---

## 5. Частые проблемы

**Ошибки вида «LPDIRECT3DDEVICE9: необъявленный идентификатор» в `D3D9Hook.h`**
Каталог проекта попал в пути поиска раньше DirectX SDK, и `#include <d3d9.h>`
нашёл заголовок проекта вместо системного. Проверь порядок в
Properties → C/C++ → Additional Include Directories: пути SDK должны идти
первыми. По этой же причине хук называется `D3D9Hook.h`, а не `d3d9.h` —
не переименовывай обратно.

**«Cannot open include file: d3dx9.h»**
DirectX SDK не установлен. Либо поставить June 2010 SDK с сайта Microsoft, либо убрать
`$(DXSDK_DIR)Include` из Include Directories и положиться на Windows SDK.

**Линкер не находит MinHook**
Проверить, что `MinHook/*.lib` реально в дереве. В `.gitignore` есть общий запрет `*.lib`,
но для MinHook сделано исключение (`!MinHook/*.lib`) — библиотеки должны быть в репозитории.

**Собралось, но в игре ничего нет**
- Проверить, что скопирована именно `Release\Win32` сборка;
- проверить конфликт с другим `dinput8.dll` (см. раздел 2);
- посмотреть `ddda_ai_overhaul.log` в папке игры.

**Steam или GOG?**
Оффсеты `pBase`, структуры данных и сигнатуры одинаковые. Работает на обеих.

---

## 6. Что прислать, если не собирается

1. Текст ошибок из окна **Output** Visual Studio.
2. `ddda_ai_overhaul.log` из папки игры (если DLL всё же загрузилась).
3. Скриншот панели F12 (если панель не появляется).
