# Source of Truth

Канонические runtime-контракты DDDA AI Overhaul. Если поле/связь отсутствует здесь или в `FIELD_MAP.md`, использовать её в продуктовом коде нельзя без нового подтверждения.

**Milestone:** Build 47 (`54e0573`)

**Платформа:** x86, Release/Win32, Steam/GOG через сигнатуры и DTI; transient heap addresses не канонизируются.

## 1. Глобальные якоря и character records

```text
pBase  = результат сигнатуры в DDDA.exe
Arisen record    = *pBase + 0xA7000
Main Pawn record = Arisen record + 0x7F0
```

Character record — save/gameplay data, не live scene body `uPlayer/uCmc`.

| Offset record | Type | Field |
|---:|---|---|
| `+0x6E0` | int32 | vocation |
| `+0x868` | 3×int32 | equipped skills |
| `+0x8D0` | 6×int32 | augments |
| `+0x96C` | float | current HP |
| `+0x970` | float | max HP |
| `+0x974` | float | recoverable/secondary HP |
| `+0x978` | float | current stamina |
| `+0x994` | int32 | XP |
| `+0xDD0` | uint16 | level |
| `+0x1616` | 322 B | `mStudyFlag` bestiary knowledge |
| `+0x1B90` | 10×12 B | inclination values (9 inclinations + skill-use slot) |

Inclination stride — `0x0C`; нельзя читать значения плотным `float[]`.

## 2. Live bodies и action/FSM

Main Pawn определяется динамически как `uCmc`, Arisen как `uPlayer`; hardcoded absolute vtable не является production resolver.

| Offset live body | Type | Field |
|---:|---|---|
| `+0x2DC0` | ptr | `cActionManager::cActBank` |
| `+0x2DC8` | ptr | current `cPlAct*`/enemy Act object |
| `+0x2DD4` | uint32 | packed current action code (player/main pawn подтверждены) |
| `+0x2DE8` | ptr | duplicate current Act pointer (player/main pawn observations) |
| `+0x2E64` | ptr | `cAICtrl` |
| `+0x40/+0x44/+0x48` | float | world XYZ у live units |

Current Act использует стабильный placement-new buffer: меняется vtable/состояние, а не обязательно адрес. Прямая подмена `+0x2DC8` не является безопасным AI control.

Подтверждённые packed codes main pawn: Wait `0`, Walk `1`, Run `2`, Dash `5`, Jump `8`.

## 3. Main Pawn priority pipeline

### 3.1 Resource/runtime roots

```text
cAIPriorityThink + 0x08 -> rAIPriorityThink (cmc.prt)
```

`rAIPriorityThink` содержит 85 `cPrioParam` rows. `cAIPriorityThink` материализует 48 runtime buckets (`QUEST`, `PL_Party`, `Situ_Personal`, `Enemy`, `Wait_Follow`, `Etc`; по 8 slots).

### 3.2 `cPrioParam`

| Offset | Type | Field |
|---:|---|---|
| `+0x04` | uint32 | Sensor |
| `+0x08` | uint32 | Code |
| `+0x0C` | uint32 | Category |
| `+0x10` | uint32 | Object ID |
| `+0x14` | uint32 | Extra |
| `+0x18..+0x28` | cArray | personality `cCodeParam**` |
| `+0x2C..+0x3C` | cArray | order `cOrderValue**` |

### 3.3 Runtime bucket descriptor

```text
cAIPriorityThink + 0x38 + slot*0x14:
  +0x00 cArray vtable
  +0x04 count
  +0x08 capacity
  +0x0C flags
  +0x10 cPrioParam** mpArray
```

Только первые `count` pointers действительны. Шаг allocator `0x90` не является размером score object; trailing memory не анализируется как score.

### 3.4 Personality/order rules

`rAIPriorityThink::cCodeParam` (104 B):

| Offset | Field |
|---:|---|
| `+0x04` | int32 `AddS32` |
| `+0x08` | float `AddF32` |
| `+0x0C` | `BreakAfterApply` |
| `+0x10..+0x20` | checks cArray |

`rAIPriorityThink::cOrderValue` (12 B): `+0x04 Value`, `+0x08 Type`.

Все 48 personality rules и 41 order rules совпали с `cmc.prt` без validation errors.

`AddS32` доказан как integer bucket displacement:

```text
code 45 base slot 36
AddS32 -1 -> slot 35
AddS32 -2 -> slot 34
```

Build 46 доказал transaction для codes 45/46 одновременно: slot 34 `[46,45]`, slot 35 `[47]`; vanilla — slot 35 `[47,46,45]`.

### 3.5 Persistent profiles

Runtime sidecar:

```text
DDDA_AI_Overhaul/ddda_pawn_ai_profiles.ini
```

Schema v2 поддерживает 0..48 entries с identity:

```text
sensor/code/category/objectId/extra/ruleIndex
```

Контракт: validate all → apply all → readback → convergence → rollback. Адреса после перезапуска разрешаются заново. Build 45 подтвердил persistence между процессами; Build 46 — multi-rule transaction.

**Ограничение:** `cPrioParam` resource может быть общим для нескольких pawn instances. Main-pawn-only тесты подтверждены, но per-instance isolation и однозначная main root association ещё открыты.

## 4. Planner / GOAP

| Contract | Status |
|---|---|
| `cAIGoalPlanning + 0x17C` | current priority code; `0xFFFFFFFF` = no selected priority |
| `planner + 0x190 + code*0x110` | indexed `cPlanCtrl` для валидного code |
| observed codes | Wait `0` или no-priority, Follow `1`, Dagger combat `54` |
| GOAP resources | 68 goals / 167 interfaces в pawn catalog |

GOAP semantic mapping и patch framework ещё не завершены. Не применять формулу `PlanCtrl` к `0xFFFFFFFF`.

## 5. Action eligibility

Девять `rAIPlayerActionParameter` resources содержат 352 rows; runtime census видел 353 `cAIPlayerActionParameter` objects (вероятный extra/default object).

Compiled tuple в `cCmc*`:

| Offset | Field |
|---:|---|
| `+0x258..+0x26C` | 6 range floats |
| `+0x270` | ElementAttr |
| `+0x274` | AtkAttr |
| `+0x278` | UseAttr |

CSV `PLAYER_PAWN_WORK/generated/AIPlActParam*.csv` — AI target/use ranges и eligibility data, не доказанные physical damage hitboxes. Runtime mutation A/B ещё не выполнен.

## 6. `cCmcInfo`

| Offset | Field |
|---:|---|
| `+0x29C` | current HP mirror |
| `+0x2A4` | recoverable/secondary HP mirror |
| `+0x14B8 + id*0x0C` | `{state,id,float value}` для 9 inclinations |
| `+0x0288/+0x028C/+0x0290` | `rHumanEdit/rBodyEdit/rFaceEdit` |
| `+0x07EC` | `cPlLoadManager` |
| `+0x1658` | `cPwnMsgLoadManager` |

Current target pointer не подтверждён.

## 7. Enemy runtime contracts

- `uEm* +0x2D`: observed gid/type byte; DTI name обязателен для kind classification из-за коллизий (`uEm8000` может носить `0x61`).
- `+0x0C/+0x10`: observed doubly-linked live-unit list.
- `+0x40/+0x44/+0x48`: world XYZ.
- `+0x2DC8`: current enemy Act; смерть определяется `Die/DeadBody`, не угадыванием HP.
- `uEm0100` body size `0x73C0`; видоспецифичные offsets нельзя переносить на все `uEm*`.
- Goblin `cCharParamEnemy` найден по signature; observed copies около `+0x5870/+0x59B0`, size `0x140`.
- Body scale channels `+0x60/+0x64/+0x68` и charparam scale `+0x12C` используются только через проверяемый EnemyTuner contract.

## 8. Что не является подтверждённым

- live current target main pawn;
- универсальный enemy current HP offset внутри body;
- main-pawn-specific priority root при нескольких roots;
- семантика всех priority codes;
- physical hitbox из `AIPlActParam` ranges;
- безопасные generalized GOAP/action-eligibility writes;
- monster priority/planner bridge.

## 9. Канонические артефакты

- `FIELD_MAP.md` — таблицы offsets;
- `PLAYER_PAWN_WORK/` — детальный pawn vertical slice;
- `PLAYER_PAWN_WORK/generated/pawn_ai_catalog.json` — 83 pawn AI resources;
- `PLAYER_PAWN_WORK/generated/pawn_ai_profile_46_evidence.json` — compact transaction evidence;
- `generated/TYPE_ATLAS.md` / `src/TypeAtlas.Generated.h` — generated type catalog;
- `ASSET_FORMATS.md` — static resource formats.
