# Source of Truth

Канонические runtime-контракты DDDA AI Overhaul. Если поле/связь отсутствует здесь или в `FIELD_MAP.md`, использовать её в продуктовом коде нельзя без нового подтверждения.

**Milestone:** Build 74.0 (`priority-rows`); последняя канонизация раскладки — Build 73.27

**Платформа:** x86, Release/Win32, Steam/GOG через сигнатуры и DTI; transient heap addresses не канонизируются.

## 1. Глобальные якоря и character records

```text
pBase  = результат сигнатуры в DDDA.exe
Arisen record    = *pBase + 0xA7000
Main Pawn record = Arisen record + 0x7F0
```

Character record — save/gameplay data, не live scene body `uPlayer/uCmc`.

Vocation enum (1-based, подтверждён CE-таблицей 2026-08-16):

```text
1=Fighter 2=Strider 3=Mage 4=Mystic Knight 5=Assassin
6=Magick Archer 7=Warrior 8=Ranger 9=Sorcerer
```

| Offset record | Type | Field |
|---:|---|---|
| `+0x6E0` | int32 | vocation (1-based enum) |
| `+0x868` | 6×int32 | equipped skills (main weapon 3 + secondary weapon 3) |
| `+0x8D0` | 6×int32 | augments |
| `+0x96C` | float | current HP |
| `+0x970` | float | max HP |
| `+0x974` | float | recoverable/secondary HP |
| `+0x978` | float | current stamina |
| `+0x97C` | float | max stamina |
| `+0x980` | float | recoverable/secondary stamina |
| `+0x984` | float | Strength |
| `+0x988` | float | Defense |
| `+0x98C` | float | Magick |
| `+0x990` | float | Magick Defense |
| `+0x994` | int32 | XP |
| `+0x998` | int32 | XP to next level |
| `+0xDD0` | uint16 | level |
| `+0x1616` | 322 B | `mStudyFlag` bestiary knowledge |
| `+0x1B90` | 10×12 B | inclination values (9 inclinations + skill-use slot) |

Inclination stride — `0x0C`; нельзя читать значения плотным `float[]`.

Account economy (абсолютные, от `pBase`):

| Offset | Type | Field |
|---:|---|---|
| `+0xA7A14` | int32 | Discipline Points (DP) |
| `+0xA7A18` | int32 | Gold |
| `+0xA7A1C` | int32 | Rift Crystals (RC) |

## 2. Live bodies и action/FSM

Main Pawn определяется динамически как `uCmc`, Arisen как `uPlayer`; hardcoded absolute vtable не является production resolver.

| Offset live body | Type | Field |
|---:|---|---|
| `+0x2DC0` | ptr | `cActionManager::cActBank` |
| `+0x2DC8` | ptr | current `cPlAct*`/enemy Act object |
| `+0x2DD4` | uint32 | packed current action code (player/main pawn подтверждены) |
| `+0x2DE8` | ptr | duplicate current Act pointer (player/main pawn observations) |
| `+0x2E64` | ptr | `cAICtrl` |
| `+0x40/+0x44/+0x48` | float | world XYZ у live units (единицы НЕ метры — ~сантиметры, см. §2 ниже) |

Current Act использует стабильный placement-new buffer: меняется vtable/состояние, а не обязательно адрес. Прямая подмена `+0x2DC8` не является безопасным AI control.

Подтверждённые packed codes main pawn: Wait `0`, Walk `1`, Run `2`, Dash `5`, Jump `8`.

### 2.1 Масштаб мировых координат

`+0x40/+0x44/+0x48` — это **не метры**. Единицы мира ≈ сантиметры (~100 единиц/метр). Косвенные доказательства:

- `AIPlActParam` — дальности действий `500..4000`: как сантиметры это 5–40 м (осмысленно для удара/лука), как метры — абсурд;
- гоблин-сенсор `em0100A.sn2` зрение `1500`: ~15 м агро-радиуса (осмысленно), не 1500 м;
- live `pawn→Arisen ≈ 440` при следовании вплотную: ~4.4 м (нормальная дистанция следования), не 440 м.

Точный фактор (100 vs иное) уточняется touch-тестом. В коде доктрины дистанции считаются в raw world-units и переводятся в метры через `worldUnitsPerMeter` (ini `[pawnAI]`, по умолчанию 100.0).

## 3. Main Pawn priority pipeline

### 3.1 Fast main-pawn decision chain (Build 48)

```text
uCmc + 0x2E64 -> cAICtrl
cAICtrl + 0x04 -> same uCmc (owner back-reference)
cAICtrl + 0x68 -> cAIGoalPlanning
cAICtrl + 0x70 -> cAIPriorityThink
cAIGoalPlanning + 0x04 -> same cAICtrl
cAIPriorityThink + 0x04 -> same cAICtrl
```

Цепочка совпала в Wait/Follow/Combat. После разрешения live `uCmc` глобальный AI heap census для planner/priority roots не требуется.

### 3.2 Resource/runtime roots

```text
cAIPriorityThink + 0x08 -> rAIPriorityThink (cmc.prt)
cAIGoalPlanning + 0x08 -> default rAIGoalPlanning (observed Wait resource)
```

`rAIGoalPlanning` содержит inline ASCII resource path по `+0x08`. Runtime manifest Build 48 дал 69 уникальных GOAP resources: все 68 импортированных файлов плюс `WpnBowAtk2`.


`rAIPriorityThink` содержит 85 `cPrioParam` rows. `cAIPriorityThink` материализует 48 runtime buckets (`QUEST`, `PL_Party`, `Situ_Personal`, `Enemy`, `Wait_Follow`, `Etc`; по 8 slots).

### 3.2.1 Ресурс приоритетов ОБЩИЙ на всех пешек (Build 75.6, live, 3 пешки)

Перекличка партии при полном составе (своя Страйдерша + наёмные Файтер и Маг):

```text
roster: 3 pawns, 1 distinct priority resource
```

Каждая пешка имеет **свой** `cAIPriorityThink` (инстанс с 48 рантайм-вёдрами), но `think + 0x08` у всех троих указывает на **один и тот же** `rAIPriorityThink` (`AI\PrioThink\cmc`).

Следствия, которые надо держать в голове при любой правке весов:

1. **правка `cCodeParam::AddS32` действует на ВСЮ партию**, включая наёмных пешек. Это область действия, а не ошибка, но она должна быть заявлена;
2. **различие поведения между пешками даёт не ресурс, а проверки склонностей**: `cCodeParam` несёт check `{inclId, rank}` (§3.5.2), и одно и то же правило применяется только к той пешке, у которой совпала склонность и её ранг. То есть общий набор правил + личные склонности = персональное поведение;
3. **адресная правка «этой пешке так, а той иначе» на слое ресурса невозможна.** Для неё пришлось бы работать с рантайм-вёдрами конкретного инстанса `cAIPriorityThink`.

### 3.3 `cPrioParam`

| Offset | Type | Field |
|---:|---|---|
| `+0x04` | uint32 | Sensor |
| `+0x08` | uint32 | Code |
| `+0x0C` | uint32 | Category |
| `+0x10` | uint32 | Object ID |
| `+0x14` | uint32 | Extra |
| `+0x18..+0x28` | cArray | personality `cCodeParam**` |
| `+0x2C..+0x3C` | cArray | order `cOrderValue**` |

### 3.4 Runtime bucket descriptor

```text
cAIPriorityThink + 0x38 + slot*0x14:
  +0x00 cArray vtable
  +0x04 count
  +0x08 capacity
  +0x0C flags
  +0x10 cPrioParam** mpArray
```

Только первые `count` pointers действительны. Шаг allocator `0x90` не является размером score object; trailing memory не анализируется как score.

### 3.5 Personality/order rules

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

### 3.5.1 Guardian-family modifiers (подтверждено Build 57 audit, read-only)

Runtime дамп live `cPrioParam` для Guardian-кодов (85 rows total, совпадает с каноном). Identity-кортеж — полный tuple `{sensor, code, category, objectId, extra}`; каждое правило — отдельный `cCodeParam` с `AddS32/AddF32/break/checks`:

| code | tuple | personality rules (AddS32) | статус |
|---|---:|---|---|
| `4` | `{0,·,0,0,1}` | `[+3]` break=0 | CONFIRMED (wait/follow бонус) |
| `13` | `{1,·,0,0,1}` | `[-4,-2,-2]` break=1 | CONFIRMED (party relation) |
| `15` | `{0,·,0,0,1}` | `[-2,-2,-2]` break=1 | CONFIRMED (Air) |
| `54` | `{1,·,0,0,1}` | `[-3,-2,+2,+5,-1]` break=0 | CONFIRMED (WpnDaggerAtk) |
| `60` | `{1,·,0,0,1}` | `[-3,-3]` break=0 | CONFIRMED (Em0600Cover) |
| `66` | `{1,·,0,0,1}` | `[-4]` break=0 | CONFIRMED (battle response) |

**Оговорка о полноте (21.08).** Этот аудит был Guardian-scoped: он проверял шесть заранее подозреваемых кодов и печатал количество правил, а не их содержимое. Полная выписка «склонность → какие коды она двигает» по всем 85 строкам появилась только в Build 75.8 (кнопка `Inclination rules`). До неё считать список из шести строк исчерпывающим нельзя.

Важно и то, что уже видно здесь: `code 4` несёт **бонус +3** к wait/follow. Жалобы игроков на Guardian («пешка липнет к игроку и ничего не делает») описывают именно это, а не штраф на атаку — см. `docs/GUARDIAN_REPORTS_MAP.md`.

`code 54` — главный рычаг: rule[0] несёт штраф `-3` (`break=0`, `checks=1`). Это «поводок пассивности» Guardian на WpnDaggerAtk. Бонусы `+2/+5` — другие состояния Guardian (низкая инклинация → бонус к атаке). Build 57.1 снимает именно rule[0] (`-3 → 0`) транзакционно.

### 3.5.1a Поведенческое подтверждение штрафа code 54 (Build 75.24, A/B на живой пешке)

Одна пешка-Страйдер, три боя по ~110 с, менялась только primary-склонность:

| код | Guardian ×2 | Scather |
|---|---:|---:|
| `54 WpnDaggerAtk` | **0 / 0 кадров** | **1612 кадров, 19 входов** |
| `1 Follow` | 1432 / 2784 | 291 |
| `28 StandOff` | 1204 / 1337 | 152 |

12 467 боевых кадров с Guardian не дали ни одного кадра ближней атаки; 6 526 кадров со Scather дали 1 612.

**Приоритет относителен.** У Guardian НЕТ правила на `Follow` (код 1) — единственное действующее правило это `54(-3)`. Следование растёт не потому, что его повысили, а потому, что понизили то, что его обгоняло. Отсюда и жалоба игроков «пешка липнет и не дерётся»: это побочный эффект одного штрафа, а не отдельное поведение.

Практический вывод: рычагом является ровно `code 54 rule[0]`, а `Follow` трогать не нужно.

### 3.5.2 Семантика checks (подтверждено Build 57.2, read-only)

Каждый `cCodeParam` несёт массив checks (по одному на правило здесь). Check-объект:

```text
+0x00  vtable (4 байта)
+0x04  int32  — идентификатор склонности (0-based, СОВПАДАЕТ с нашим InclIdx):
               0=Scather 1=Medicant 2=Mitigator 3=Challenger 4=Utilitarian
               5=Guardian 6=Nexus 7=Pioneer 8=Acquisitor
+0x08  int32  — ранг склонности: 2=primary, 1=secondary, 0=tertiary
```

Доказательство — идеальная корреляция AddS32 ↔ (склонность, ранг) в code 54:

| AddS32 | check | смысл |
|---|---:|---|
| -3 | (Guardian, primary) | Guardian главный → штраф кинжалы |
| -2 | (Guardian, secondary) | Guardian второй → штраф |
| +2 | (Scather, secondary) | Scather второй → бонус |
| +5 | (Scather, primary) | Scather главный → бонус |
| -1 | (Medicant, tertiary) | Medicant → лёгкий штраф |

Следовательно: агрессивные склонности дают БОНУС к атаке, оборонительные —
ШТРАФ. Это штатный «поводок», который мы переписываем.

### 3.5.3 Лук (code 57) НЕ имеет personality-модификаторов

`code 57 (WpnBowAtk2)` — `personality=0`. У лука НЕТ склонностных штрафов/бонусов.
Следствие (подтверждено тестом 57.1): пешка-гибрид с Guardian primary при
снятом даггер-штрафе вытаскивает кинжалы, но в момент атаки берёт лук —
потому что лук не штрафуется, а враг дальше даггер-радиуса (GOAP/eligibility
честно выбирают дистанционное оружие). Фикс должен быть distance-aware:
поощрять кинжалы только при угрозе в даггер-радиусе, иначе лук работает.

### 3.6 Persistent profiles

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
| observed codes | Build 53: 18 valid selected codes plus `0xFFFFFFFF`; full aggregate in `pawn_intent_trace_53.json` |
| GOAP resources | 68 goals / 167 interfaces в pawn catalog |

Build 48 runtime GOAP manifest: 69 unique `rAIGoalPlanning` paths, включая runtime-only `WpnBowAtk2`. Default pointer planner root всегда указывал на `Wait` и не равен selected plan.

Build 50 подтвердил compiled link:

```text
cGoalPlanningNode + 0x04 -> rAIGoalPlanning::ActionInterfaceParam
ActionInterfaceParam + 0x04 = InterfaceID
ActionInterfaceParam + 0x08 -> cCmc action interface
```

Primary static PlanCtrl identities, подтверждённые Build 52:

- code `0`: Wait ID 0;
- code `1`: Follow ID 1;
- code `35`: VictoryPose ID 117;
- code `36`: ItemThrow ID 158;
- code `54`: `WpnDaggerAtk` IDs 9/124/132/133/10; exact action `cPlActWpnDaggerAtckLandL`;
- code `57`: runtime-only `WpnBowAtk2` IDs 12/107; exact action `cPlActWpnBow`;
- code `58`: `DmgUkemi` ID 149.

Selected runtime graphs могут одновременно нести transient/auxiliary links (например Follow/Jump или ItemThrow/Follow); их нельзя смешивать с primary slot identity.

Build 52 прошёл все 91 planner slots за один snapshot: 56 slots имеют direct GOAP links, из 70 codes, используемых `cmc.prt`, семантически mapped 42. Build 53 подтвердил стабильность карты 56/91 и впервые поймал live planner-only slots `74 = EscapeNotice2` и `76 = GotoOm`. Следовательно, selected runtime code использует весь диапазон `0..90`, даже если часть slots отсутствует в parsed `cmc.prt` rows. Не применять формулу `PlanCtrl` к `0xFFFFFFFF`.

### 4.0 Код приоритета = слот загруженной цели (Build 73.27, CONFIRMED)

Массив загруженных целей идёт от `planner + 0x08` с шагом 4; ресурс — `rAIGoalPlanning`, путь лежит строкой по `+0x08` самого ресурса.

```text
code = (slotOffset - 8) / 4
PlanCtrl(code) = planner + 0x190 + code * 0x110
```

Пять сходящихся проверок: `1=Follow` (live, Build 40), `15=Air`, `54=WpnDaggerAtk`, `60=Em0600Cover` (все CONFIRMED по Guardian-таблице), плюс структурная — старший загруженный код `90` против ёмкости массива `PlanCtrl` `(25264-0x190)/0x110 = 91` слот (коды 0..90, хвост 112 B).

Полная таблица — `docs/generated/PAWN_GOAL_CODES.md` (пересобирается `tools/goal_codes_from_log.py`, набор целей зависит от вокации пешки). Ключевые коды оружия: `52 WpnSwordAtk`, `53 WpnGSwordAtk`, `54 WpnDaggerAtk`, `55 WpnWandAtk`, `56 WpnShieldAtk`, `57 WpnBowAtk2`; рывок — `84 DashFollow`, `85 DashFollowSt500`.

`planner + 0x17C` — текущий выбранный код. Значение `-1` (`0xFFFFFFFF`) — **промежуток между выборами, а не состояние**: считать его отдельным кодом или применять к нему формулу `PlanCtrl` нельзя.

### 4.1 `cAIGoalPlanning::cPlanCtrl` — раскладка блока (Build 73.27, read-only)

Построчный дифф `PlanCtrl(1)` против `PlanCtrl(84)` (элементы одного массива одного типа, поэтому смещения сопоставимы):

| Offset | Field |
|---:|---|
| `+0x000` | vtable `cPlanCtrl` |
| `+0x004` | cArray #1 (`count`, `capacity`, `flags`, `mpArray` по схеме §3.4) |
| `+0x038` | cArray #2 |
| `+0x04C` | cArray #3 |
| `+0x060` | подобъект (vtable `0x015593F4`), внутри cArray по `+0x068` |
| `+0x0A8..+0x0B4` | 4×`0xFFFFFFFF` — пустые слоты-сентинелы |
| `+0x0C0/+0x0C4/+0x0C8` | float XYZ — **живая точка назначения плана** (меняется между снимками) |
| `+0x0D0` | vtable `0x0155A4A8` |
| `+0x0D4` | ptr на живое тело (кандидат: объект следования) |
| `+0x0DC/+0x0E0` | флаги исполнения |

Три cArray компилированной части присутствуют у ОБЕИХ целей (`count 1`), а всё, что заполняется при исполнении (`+0x060`-подобъект, XYZ, тело, флаги), у никогда не запускавшейся цели — нули. **`PlanCtrl` хранит состояние исполнения, а не условия выбора; править его для «разрешения» цели бессмысленно.**

### Current target

`uCmc+0x2EB8` подтверждён как current combat target:

- zero в Wait/Follow;
- Build 49 переключился Wolf A → Wolf B между combat snapshots;
- Build 51 указывал на тот же Goblin body во время exact dagger и bow actions;
- Build 53: non-null в 335/747 trace rows, 9 уникальных bodies (`uHumanEnemy`, `uEm0100`, `uMultiNpc`);
- при `cPlActCmcNeardeath` code был `0xFFFFFFFF`, но target `uHumanEnemy` сохранялся все 16 heartbeat rows и пережил переход в `cPlActCmcReturn`.

Итого: `+0x2EB8` — primary planning/combat target, но не гарантия активного execution: ссылка может сохраняться в planner-disabled damage/near-death states. `+0x4B28` — secondary/previous/lock candidate: может совпадать с current target или хранить предыдущего Wolf. `+0x14E0` появляется позднее/контекстно и похож на look-at/lock reference.

### 4.2 Target-selection / lock-on слой (Build 59.x, read-only)

Цель пешки (`uCmc+0x2EB8`) — это само **тело врага** (`uHumanEnemy`/`uEm*`), не отдельная карточка. Скоринг «кого бить» живёт в объектах, что ссылаются на тело. Вскрытые типы (все в TypeAtlas):

| Тип | Размер | Роль | Статус |
|---|---:|---|---|
| `rLockOnTarget` | 120 B | РЕСУРС конфига локона. ASCII-путь `param\lockon\m000cmc` (гл. пешка), `...\HumanEnemy`, `...\m000` | ✅ read-only |
| `sLockOnManager` | 56 B | менеджер локона | ✅ |
| `sLockOnManager::cLockOnTarget` | 112 B | живая карточка локона: `+0x38` float радиус (~10.0), `+0x40` owner body ptr (uPlayer/uCmc), в конце world XYZ | ✅ частично |
| `cLockOnTarget` | 80 B | по одной записи на врага (десятки) | 🧪 раскладка не закреплена |
| `sRecognition` | 72 B | менеджер распознавания; float 1.0/2.0/10.0, счётчики, ptr | ✅ частично |
| `sRecognition::cEnemyInfo` | 80 B | карточка врага (в live НЕ инстанцируется как vtable-объект) | 🧪 |

Обратных ссылок «карточка → тело врага» в первых байтах этих объектов НЕ найдено — связь, вероятно, от тела врага к карточке или через индекс. Открыто: найти поле threat-скора для бонуса «враги в радиусе Аризена приоритетнее».

Подтверждение по ресурсу (Build 59.x): `param\lockon\m000cmc.ltg` — конфиг локона
главной пешки, `mQuality=2`, `mRadius=10.0` (см. ASSET_FORMATS §8). Радиус 10 м
совпадает с live `cLockOnTarget +0x38` и с Guardian preempt-радиусом.

Build 51 также подписал основные `cAICtrl` children: path/nav traces (`+0x30/+0x34`), sensor (`+0x38`), route (`+0x3C`), action-interface ctrl (`+0x40`), situation (`+0x5C`), message (`+0x60`), grid (`+0x64`), planner (`+0x68`), study (`+0x6C`), priority (`+0x70`), look-at (`+0x74`).

Build 53 подтвердил компактный read-only fast path без heap census: за 570.7 s записано 747 rows, 18 valid selected codes, 33 exact action types и clean DLL-detach stop без ошибок. Semantic intent, selected code, exact `cPlAct`, packed code, primary target `+0x2EB8` и selected PlanCtrl links пишутся только при переходе плюс heartbeat раз в секунду. Compact evidence: `PLAYER_PAWN_WORK/generated/pawn_intent_trace_53.json`.

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

### 5.1 Поводок (follow-дистанция) — найдено (Build 64.7)

В `cCmcFollow` (доступен по цепочке: uCmc+0x2E64 → cAICtrl+0x68 → planner →
PlanCtrl[1] → node+0x04 → ActionInterfaceParam+0x08 → cCmcFollow) есть
**серия порогов follow-дистанции** (float, сантиметры):

```text
+0x118..+0x130  (float idx 70..76):
  [70]=1200  [71]=900  [72]=600  [73]=300  [74]=1500  [75]=1000  [76]=150
```

Интерпретация: лестница дистанций следования — 1500 (15 м) = порог, с которого
пешка начинает догонять; 1200/1000/900/600/300 = ступени сближения; 150 (1.5 м)
= дистанция остановки рядом. Значения СТАТИЧНЫ (не меняются «стоит/бежит» —
это пороги поведения, а не текущая дистанция; текущая снимается отдельно).

`+0x258..+0x26C` (range floats) — НЕ поводок: `71.9` стабильно во всех состояниях
(это, вероятно, диапазоны атаки). Поводок именно в `+0x118..+0x130`.

Это даёт реализацию роль-зависимого поводка (GUARDIAN_LEASH_MATRIX):
Assault — сдвинуть пороги ВНИЗ (жмётся к игроку), Support/RangedHold/Protector —
ВВЕРХ (держится дальше). Требует A/B: поменять один порог и проверить поведение
догона, транзакционно с rollback.

### 5.2 Поводок — НЕ в priority-слое (Build 65.1, полный аудит)

Прочитаны ОБА массива cPrioParam для всех 85 строк: personality (cCodeParam,
AddS32 = веса-штрафы) и order (cOrderValue {+0x04 Value, +0x08 Type}).
Результат: **нигде нет параметра «дистанция следования»**.

- personality: AddS32 = целые смещения (штрафы/бонусы), привязаны к (склонность, ранг).
- order: `Type` = тип команды (9=attack, 10=move/follow, 11=combat-follow...),
  `Value` = небольшое число. Это КЛАСС команды, а не метры.
- Pioneer(7) НЕ встречается в personality ни разу — его «пинок» это НЕ дистанция,
  а выбор другого intent (Goto/StandOff/DashFollow вместо Follow).

Вывод: поводок — это compiled-параметр из ресурса (Follow.gop класс 752 B или
его param), общий на всех вокаций (тот самый «статичный на все вокации» из
наблюдения пользователя). Дистанция — СЛЕДСТВИЕ выбора intent, а не записываемое
поле. Роль-зависимый поводок делается через приоритет intent (Follow vs Goto),
а не записью float.

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

### 7.1 Применение движения — два пути (подтверждено Build 70.2, живой A/B)

Покадровое смещение применяется к координатам тела в ДВУХ разных местах.
Хук одного из них даёт «шагом медленно, бегом быстро».

| Путь | Инструкция | Сигнатура | Тело | Где смещения |
|---|---|---|---|---|
| обычное движение | `movss xmm6,[esp+30]` | `F3 0F 10 74 24 30` | `esi` | стек `[esp+30/34/38]` |
| спринт | `addss xmm2,[edi+40]` | `F3 0F 58 57 40` | `edi` | регистры `xmm2/xmm3/xmm6` |

Сигнатура обычного движения **не уникальна**: 317 совпадений в образе.
Опознаётся по контексту — в пределах 192 байт после неё стоят все три
записи координат (`F3 0F 11 76 40/44/48`), и такое место ровно одно.
Адресное разрешение (RVA из внешнего источника) НЕ работает: сборки exe
различаются.

Умножение горизонтальных составляющих смещения (X и Z) меняет скорость
перемещения, **не затрагивая анимации**. Вертикаль (Y) не трогать: это
падение и прыжки.

**Живая проверка (18.08.2026).** Три гоблина, безоружная главная пешка,
одна и та же сцена:

| Множитель | Исход |
|---|---|
| 1.19 | пешку сбивают с ног и не дают подняться, воскрешение не помогает |
| 0.79 | пешка спокойно уворачивается от всех троих |

Движок корректно переваривает изменённые смещения: ни рассинхрона
коллизий, ни провалов сквозь геометрию не наблюдалось.

## 8. Что не является подтверждённым

- универсальный enemy current HP offset внутри body;
- семантика/GOAP links всех priority codes;
- physical hitbox из `AIPlActParam` ranges;
- безопасные generalized GOAP/action-eligibility writes;
- monster priority/planner bridge.

## 9. Канонические артефакты

- `FIELD_MAP.md` — таблицы offsets;
- `PLAYER_PAWN_WORK/` — детальный pawn vertical slice;
- `PLAYER_PAWN_WORK/generated/pawn_ai_catalog.json` — 83 pawn AI resources;
- `PLAYER_PAWN_WORK/generated/pawn_ai_profile_46_evidence.json` — compact transaction evidence;
- `PLAYER_PAWN_WORK/generated/pawn_priority_semantics.{json,csv}` — conservative code map;
- `PLAYER_PAWN_WORK/generated/pawn_ai_semantic_roots_48.json` — fast root/GOAP manifest evidence;
- `PLAYER_PAWN_WORK/generated/pawn_ai_plan_fingerprint_49.json` — compiled planner/target candidates;
- `PLAYER_PAWN_WORK/generated/pawn_ai_plan_links_50.json` — direct PlanCtrl→GOAP interface links;
- `PLAYER_PAWN_WORK/generated/pawn_ai_action_target_51.json` — exact actions, bow/dagger semantics and target evidence;
- `PLAYER_PAWN_WORK/generated/pawn_planner_semantics.{json,csv}` — all 91 slots, 42/70 `cmc.prt` priority codes mapped;
- `PLAYER_PAWN_WORK/generated/pawn_intent_trace_53.json` — compact 747-row aggregate, live codes/actions/targets and lifecycle spans;
- `generated/TYPE_ATLAS.md` / `src/TypeAtlas.Generated.h` — generated type catalog;
- `ASSET_FORMATS.md` — static resource formats.
