# Source of Truth — Оффсеты, используемые в коде

**Назначение:** Один файл с каноническими оффсетами и источником каждого.
При любом сомнении — открыть этот файл, проверить источник, потом править.

**Правило:** Если оффсет здесь не указан — его нельзя использовать в коде.

---

## Pawn inclinations (инклинации пешки)

**Источник истины:** Cheat Engine Lua script пользователя, подтверждён 2026-08-15.

```lua
-- Из скрипта CE:
local inc = pBase + 0xA7000 + 0x7F0 + 0x96C + 0x1224
for i, n in ipairs(incNames) do
  print(string.format('  %s = %.2f', n, readFloat(inc + (i-1)*0xC)))
end
```

**Канонические значения в коде (`src/pawnai/PawnAI_Common.h`):**

```cpp
#define PLAYER_BASE         0xA7000    // к скрипту: pBase + 0xA7000
#define PAWN_OFFSET         0x7F0      // к скрипту: pBase + 0xA7000 + 0x7F0
#define INCL_OFFSET         (0x96C + 0x1224)  // = 0x1B90, от тела пешки
#define INCL_STRIDE         0xC        // = 12 байт между инклинациями
```

**Что было сломано раньше:**

| Дата | Что попробовали | Почему не сработало | Кто исправил |
|---|---|---|---|
| 2026-08-15 | `INCL_OFFSET = 0x96C + 0x1224 = 0x1B90` (исходное) | работало с самого начала | оригинал от пользователя |
| 2026-08-15 | `INCL_OFFSET = 0x178`, stride 4 | неправильное место, агент догадался | сломал агент |
| 2026-08-15 | `INCL_OFFSET = 0x1B90`, stride 0xC — восстановлено | совпало со скриптом CE | пользователь |
| 2026-08-15 | memcpy всех 10 float одним блоком | при stride=0xC memcpy читает padding вместо значений | сломал агент |
| 2026-08-15 | `reinterpret_cast<uintptr_t>` для арифметики указателя | MSVC не делает неявное преобразование BYTE*→uintptr_t | починка |
| 2026-08-15 | `__try`/`__except` вокруг каждого адреса | SEH на одном битом адресе раньше валил весь массив | починка |

**Метод верификации (как проверить, не сломались ли оффсеты):**
1. Открыть игру, загрузить сейв с пешкой, у которой известны инклинации
2. F12 → Pawn AI → Inclinations Diagnostic → "Self-Test: write 900 / read back"
3. Должно показать "before=XXX after=900 OK"
4. Если FAILED — НЕ менять оффсеты наугад. Спросить скрипт CE у пользователя.

**Если оффсеты снова перестали работать:**
- CAPCOM патчил DDDA? (нет, последний патч 2016)
- Сменился ID пешки? (проверить через `*pBase + 0xDD0` — level)
- Изменился GameVersion? (проверить `pBase` адрес в логе)

---

## Персонажная запись `pBase` (ГГ и главная пешка)

**Базы:** ГГ = `*pBase + 0xA7000`; главная пешка = запись ГГ `+0x7F0`. Это не live body `uPlayer/uCmc`.

| Оффсет от character record | Размер | Поле | Источник |
|---|---|---|---|
| +0x6E0 | int32 | Vocation (1=Fighter, 2=Strider, 3=Mage, ...) | CE script |
| +0x868 | 24 B | Equipped Skills (3× int32) | CE script |
| +0x8D0 | 24 B | Augments (6× int32) | CE script |
| +0x96C | float | **current HP** | Build 39: изменилось при уроне |
| +0x970 | float | **max HP** | Build 39: 498 / 505 стабильно |
| +0x974 | float | второй HP/recoverable параметр | Build 39: меняется при уроне; имя уточняется |
| +0x978 | float | **current stamina** | Build 39: Dash/умения/восстановление |
| +0x97C... | float | Strength/Defense/Magic/... | CE script; точные границы требуют отдельной сверки |
| +0x96C+0x28 | int32 | XP | CE script |
| +0x96C+0x2C | int32 | XP to next level | CE script |
| +0xDD0 | uint16 | Level | CE script |
| +0x1616 | 322 B | mStudyFlag (bestiary knowledge bits) | архивный код PawnAI |
| +0x1B90 | 10×12 B | Inclinations array (Scather..Acquisitor + Skill Use) | CE script 2026-08-15 |

---

## Global offsets (глобалы процесса)

**Источник:** Cielos Steam-table (для архитектуры dinput8), всё ещё работают в GOG.

| Символ | Оффсет от pBase | Поле | Источник |
|---|---|---|---|
| `+0xDD0` | — | Level (uint16) | Cielos |
| `+0x994` | — | XP (int32) | Cielos |
| `+0x6E0` | — | Vocation | Cielos |
| `+0xB8780` | — | Weather (int: 0=clear, 1=cloudy, 2=foggy, 3=volcanic) | Cielos |
| `+0xB33A8` | — | Post-game flag | Cielos |
| `+0xA7A18` | — | Gold (int32) | CE script пользователя |
| `+0xA7A1C` | — | RC (int32) | CE script пользователя |
| `+0xA7A14` | — | DP (int32) | CE script пользователя |

---

## Enemy struct fields (поля тел врагов)

**Источник:** Многодневная работа (2026-08-12...2026-08-14), задокументирована в `docs/FIELD_MAP.md` (HUNT 18, 23, 24, 25, 28). Воспроизведена из дампов гоблина `uEm0100` и аналогичных `uEm*`.

**Body size:** ~29 КБ (29632 для uEm0100).

### Универсальные поля (все `uEm*`, включая гоблина)

| Оффсет | Тип | Поле | Где используется | Источник |
|---|---|---|---|---|
| `+0x2D` | byte | `gid` / `typeId` (BestiaryData) | `CombatIntel::IsValidEnemyCharacter`, `EnemyTuner::TickOneBody` | HUNT 18-25, дампы 19-25 |
| `+0x40` | float | World X (Грансис) | `DevTools::DumpActorsFrom` | HUNT 18, dump19 |
| `+0x44` | float | World Y | то же | HUNT 18 |
| `+0x48` | float | World Z | то же | HUNT 18 |
| `+0x60` | float | Scale Width (множитель) | `EnemyTuner::ApplyScale`, `ForceScale` | тест 09 (билд 21) |
| `+0x64` | float | Scale Height | то же | тест 09 |
| `+0x68` | float | Scale Depth | то же | тест 09 |
| `+0x0C` | ptr | Doubly-linked list next (живые персонажи) | `DevTools::DumpActorsFrom` | HUNT 24 |
| `+0x10` | ptr | Doubly-linked list prev | то же | HUNT 24 |
| `+0x2DC0` | ptr | cActBank (банк действий FSM) | `DevTools::ScanActSlot` | MUTATION_ARCHITECTURE |
| `+0x2DC8` | ptr | Current Act (текущее FSM-состояние) | `DevTools::ScanActSlot`, `ReadLiveAct` | MUTATION_ARCHITECTURE, dump19-25 |
| `+0x2E64` | ptr | cAICtrl (704 B — принятие решений) | (только диагностика) | MUTATION_ARCHITECTURE |
| `+0x5870` | 320 B | cCharParamEnemy (только для гоблина!) | `EnemyTuner::ReadCharParam` | тест 09, signature 500/800/1200 |
| `+0x73C0` | — | Size of body (для проверки) | `DevTools::DumpActorsFrom` | HUNT 24 |
| `+0x6150` | ptr | Parent vt (sub-object у гоблина) | `DevTools::DumpActorsFrom` | HUNT 18 |

### Идентификаторы классов (DTI addresses)

**Источник:** `docs/TYPE_ATLAS.md` (4405 фабрик MT Framework из TSV), верифицировано HUNT 9, 12, 14, 19, 24.

| Тип | Factory vt RVA | Instance vt VA (наблюдение) | DTI address | Где используется | Источник |
|---|---|---|---|---|---|
| `uEm0100` (goblin) | `0x11A0474` | **`0x015852A8`** | `0x01977BA0` | `BestiaryData.h`, `DevTools::WatchAdd` | HUNT 9, 14, 18, 23, 24 |
| `uEm0900` (gargoyle) | `0x11B59A8` | **`0x015B5A80`** | `0x0197B348` | `DevTools::WatchAdd` | HUNT 22, 23, 25 |
| `uNpc` | (TSV) | **`0x015D2618`** | `0x01984DE4` | `DevTools::WatchAdd`, `BestiaryData` | HUNT 19 |
| `uPlayer` (Воскресший) | `0x11E4F34` | **`0x015C9D70`** | `0x019846E8` | PartyRecon, `CombatIntel::IsPartyMember` | build 37, DTI + Run/Dash/Wait |
| `uCmc` (главная пешка) | `0x11E42CC` | **`0x015C9108`** | — | PartyRecon | build 37, DTI + три снимка |
| `uPlayerBase` | `0x11CEF40` | (shared stub) | — | `CombatIntel::IsPartyMember` | Cielos |
| `uHumanEnemy` | `0x11EB494` | — | — | `BestiaryData`, `CombatIntel` | Cielos |
| `uEm8000` (лагерные «зайцы») | `0x11D6410` | **`0x015BB278`** | `0x01981868` | `DevTools`, kind classification | HUNT 19, 28 |
| `uEm8500` (deer) | `0x11D8110` | `0x015BCF78` | `0x01981B40` | `DevTools` | HUNT 28, 29 |
| `uEm8600` (hare, каталог) | `0x11D8B68` | `0x015BD9D0` | `0x01981BE8` | `DevTools` (важно: НЕ на цепи у гоблинов) | HUNT 29 |
| `uPawnIntel` (компонент пешки) | `0x117852C` | — | — | `CombatIntel::IsPartyMember` | Cielos |
| `sPawnManager` | `0x115ADB4` | `0x0155ADA4` | — | (диагностика) | HUNT 13 |
| `sEnemyManager` | `0x1157B54` | — | — | `TypeAtlas` | Cielos |

**Поправка 2026-08-15 по `uPlayer`:** старое `0x015EFD38` отвергнуто как резолвер. Build 37 через динамическое DTI подтвердил текущие живые классы: ГГ = `uPlayer` с vt `0x015C9D70`, главная пешка = `uCmc` с vt `0x015C9108`. В коде всё равно следует резолвить по DTI/менеджеру, а не полагаться на абсолютную vtable.

### Живой action/FSM layout игрока и главной пешки

**Источник:** build 37, три снимка одного запуска (`Run`, `Dash`, `Wait`). Полная расшифровка: `docs/PLAYER_PAWN_WORK/PLAYER_PAWN_IN_MEMORY.md`.

| Оффсет от live body | Тип | Поле | Проверка |
|---:|---|---|---|
| `+0x2DC0` | ptr | `cActionManager::cActBank` | DTI на `uPlayer` и `uCmc` |
| `+0x2DC8` | ptr | текущий `cPlAct*` | Wait/Walk/Run/Jump/Land/Dash/Damage/Weapon/Lift |
| `+0x2DD4` | uint32 | packed current action-code | Build 39: Wait=0, Walk=1, Run=2, Dash=5, Jump=8 |
| `+0x2DE8` | ptr | дубликат указателя current Act | совпал во всех трёх снимках |
| `+0x2E64` | ptr | `cAICtrl` | DTI; у пешки обратная ссылка на body |

Текущий Act — стабильный буфер (placement new): адрес не меняется, меняется vtable. У ГГ наблюдались `cPlActWait` (`0x015C128C`), `cPlActRun` (`0x015C131C`), `cPlActDash` (`0x015C1364`) и состояния прыжка/приземления/урона. Build 39 подтвердил `+0x2DD4` на обоих телах; поля `+0x4AE8/+0x32D8/+0x1C94/+0x4B14` не являются sprint flags.

### Файловый каталог AI пешки

**Источник:** `resources/extracted_assets/pawnAI/`, разбор 2026-08-15.

| Файл/набор | Подтверждённый runtime-мост | Результат |
|---|---|---|
| `AI/PrioThink/cmc.prt` | `rAIPriorityThink` (1064) → `cAIPriorityThink` (1020) | 85 priority entries |
| `AI/Goap/Cmc/*.gop` | GOAP InterfaceID → будущий live `cCmc*` bridge | 68 goals, 167 interfaces |
| `AIPlayerActionParameter/*` | `rAIPlayerActionParameter` (120) → `cAIPlayerActionParameter` (112) | 9 tables, 352 action rows |
| `Sensor/Cmc/cmc_normal.sn2` | sensor index в priority entries | 9 sensor profiles |

Dagger action-code из Build 39 связывается с индексом таблицы: `0x01050003` → Dagger row 3 (`AtckLandL`), `0x0105002A` → Dagger row 42 (`Hyakuretsu`). Полный отчёт: `docs/PLAYER_PAWN_WORK/PAWN_AI_ASSET_RESULT.md`.

### Live priority bridge — Build 40

| Объект/оффсет | Поле | Проверка |
|---|---|---|
| `cAIGoalPlanning + 0x17C` | current priority code | Wait=`0`, Follow=`1`, Combat Dagger=`54` |
| `planner + 0x190 + code*0x110` | соответствующий `cPlanCtrl` | DTI и изменение выбранных slots 0/1/54 |
| `cAIPriorityThink + 0x08` | ptr на `rAIPriorityThink` | Build 41, одна стабильная root-пара |
| `cAIPriorityThink` pointer fields | transient score/rank nodes | Build 41: target addresses идут сериями с шагом `0x90`; layout открыт в Build 42 |
| `rAIPriorityThink::cPrioParam + 0x04` | Sensor | совпало с `cmc.prt` |
| `cPrioParam + 0x08` | Code | 85/85 runtime entries совпали с каталогом |
| `cPrioParam + 0x0C/+0x10/+0x14` | Category / OBJECT_ID / Extra | совпало с XFS layout |
| `cCmcInfo + 0x14B8 + id*0x0C` | live personality `{state,id,value}` | 9 ID и динамические значения TacticalSwitch |
| `cCmc* + 0x258` | compiled action parameter (6 ranges + 3 attrs) | Dagger rows 3/4, Bow rows 0/20 exact |
| `cCmcInfo + 0x29C` | current HP mirror | изменилось при уроне |
| `cCmcInfo + 0x2A4` | recoverable/second HP mirror | изменилось при уроне; имя уточняется |

Полный разбор: `docs/PLAYER_PAWN_WORK/PAWN_AI_LIVE_BRIDGE_RESULT_40.md` и `docs/PLAYER_PAWN_WORK/PAWN_AI_SCORE_BRIDGE_RESULT_41.md`.

**Ловушка:** `gid 0x61` в каталоге = Dragon, но **у гоблинов в лагере стоят `uEm8000` с тем же `+0x2D=0x61`**. Это **зайцы в чужой шкуре**, не Григори. Не маппить `0x61` → Hare в `BestiaryData.h` — сломаем настоящего дракона. Используется как **kind classification** в `DevTools::KindIsEnemy` — отделять от `uEm8600`/`uEm8000` через DTI-имя (`NameOfLiveObjectSafe`).

### Запись главной пешки в `pBase` (не live `uCmc`)

Главная пешка использует тот же character-record layout из таблицы выше, начиная с `*pBase + 0xA7000 + 0x7F0`. Это не смещения живого сценического тела `uCmc`.

### Жизненный цикл

**Источник:** 2026-08-13, dumps 18-24.

- Тела живых персонажей лежат в двусвязном списке через `+0x0C` (next) и `+0x10` (prev).
- HP внутри 29 КБ пока НЕ НАЙДЕН (FieldMap HP — открытый тикет).
- Смерть = переход в состояние `cEm0100ActDie` или `cEm0100ActDeadBody` (читается через `+0x2DC8` → vtable → `ActMap::FindByVt`).
- Труп остаётся в списке живых, пока движок не выгрузит (гистерезис выгрузки: LOS + сотни метров).

### Метод верификации (если что-то перестало работать)

1. **Pawn inclinations:** F12 → Pawn AI → Inclinations Diagnostic → "Self-Test: write 900 / read back" → должен показать "OK".
2. **Enemy `gid`:** DevTools → HUNT → должен найти `uEm0100` с `+0x2D=0x05` рядом с гоблинами.
3. **Enemy XYZ:** HUNT в бою покажет `xyz` поля в json dump.
4. **Enemy instance vt:** В HUNT json, раздел `actors[]`, поле `vt` = 0x015852A8 для гоблина.
5. **Если что-то не совпадает:** НЕ угадывать. Спросить у пользователя CE script или проверить `docs/FIELD_MAP.md` HUNT-секцию.

---

## pBase signature (как находим сам указатель)

**Источник:** Cielos Steam-table, всё ещё работает.

```cpp
BYTE sigBase[] = { 0x8B, 0x15, ?, ?, ?, ?, 0x33, 0xDB, 0x8B, 0xF8 };
// pBase = *(BYTE***)(&sigBase_result + 2)
```

Если эта сигнатура перестанет находиться — проверить GameVersion DDDA.

---

## Когда и как обновлять

1. **Только если:** модуль окончательно перестал работать после проверки всех других возможных причин.
2. **Как:** запросить у пользователя CE script или JSON dump с верифицированными значениями.
3. **После обновления:** обновить таблицу выше, поставить дату, указать источник.

**Никогда:**
- Не менять оффсеты на основе догадок.
- Не применять оффсеты от одного источника (Steam Cielos) к другому (GOG) без проверки.
- Не добавлять новые оффсеты в код без добавления их сюда.
