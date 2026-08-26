# DDDA AI Overhaul

Runtime AI platform for **Dragon's Dogma: Dark Arisen** (Steam/GOG, x86).

> Умеем изменить живую политику — делаем LIVE. Игровые архивы используются как каталог, а не как основной способ установки.

**Текущий milestone:** tag `84.34-status-call` — thiscall `cStatus`
после воды. 84.33: рецепт не собрался (`stk8=id`).
(`ids[k]=7`). Ещё: `84.30-party-sheet`
(дамп записи). Ещё: `84.29-saurian-pack`.
(карточка вида без agrо-двери). `84.25-fullbody-scan` — в слот только
полное тело. Devilfire Drake live = `uEm5900`, не каталожный `uEm8100`.
Ещё ранее: `84.23-on-field`. Лог в RAM до выхода. Канон: [`docs/SOURCE_OF_TRUTH.md`](docs/SOURCE_OF_TRUTH.md).

**Предыдущий milestone:** tag `84.21-species-rage` — rage-профили (мин-макс скорости атаки и локомоции при std-rush) теперь поля карточки вида (SpeciesCard); roll берёт из них. Волк: 1.20–1.25 / 1.20–1.26 (сбалансированный, без изменений); гоблин: 1.21–1.24 / 1.32–1.40 (атака быстрее локомоции). Нагрузка нулевая: roll один раз на тело, эндпоинты неизменны.

**Предыдущий milestone:** tag `84.18-card-recon` — универсальный CARDRECON (см. 84.19-доки).

**Предыдущий milestone:** tag `84.16-dual-observe` — два read-only прибора
(GOBCARD дифф карт goblin + PS статусы/revive), записей ноль.

**Предыдущий milestone:** tag `84.15-goblin-grab-hold` — grab **держится**.
`GOBLIN-GRAB-ALERT` принимает `cPlActGrabStart` и `cPlActHagaijime` (не
волчий `Hagaijime4Feet`), lease 4000 ms. Пустые карты `0/0/0/0` на
`+0x2FA0+k*0x28C` будятся в нативное `1/4 att=300 w=1.0` (readback+rollback).
Указатели партии не пишутся. Tempo rage / FOCUS / suppress+fakehit нет.
Ночных волчьих кругов нет — тест на берегу Кассардиса.

**Предыдущий milestone:** tag `84.14-goblin-grab-pin` — первый write exact
`uEm0100`: `GOBLIN-GRAB-ALERT` (GrabStart, ≤2 м, 750 ms) пинит свободных
оппортунистов ALERT pin-only. Tempo rage нет.

**Предыдущий milestone:** tag `84.13-leave-engaged` — Director не срывает волка,
уже дерущегося с другим членом партии (`fC=2` не на марке). Он вяжет свою
цель; свободные и те, кто уже на марке, идут на приказ. Tempo у занятых
остаётся. Карта `fC=2` на самом марке по-прежнему пишется (84.12).

**Предыдущий milestone:** tag `84.12-wolf-combat-card` — штырь `uEm0200` пишет
и боевую карту `f8=1 fC=2` (потолок 500), не только восприятие `fC=4`
(потолок 300). Мёртвые `0/0` и переходное `fC=1` по-прежнему fail-closed.
Вес `+0x14` не пишется. Гоблины observe-only. Identity 84.11 не тронута.

**Предыдущий milestone:** tag `84.11-arisen-record-glue` — живой `uPlayer`
навсегда клеится к записи Arisen. Identity не зависит от вида монстра и не
требует указателя на запись внутри тела. Пустые Hired-слоты больше не
ломают партию из двух.

**Предыдущий milestone:** tag `84.10-goblin-pack-observe` — ночной observe-прибор
exact `uEm0100`. Только чтение: роли, `rabble`/`led`, корона по рогу/ChargeCommand,
`LEADER-FALL` по ванильному бегству. Новых галок F12 нет. Волчий write path
Build 012 не изменён. Протокол: [`docs/GOBLIN_PACK_OBSERVE.md`](docs/GOBLIN_PACK_OBSERVE.md).

**Предыдущий milestone (Director):** Build 012 / tag `84.9-pilot012-urgency-mobilization` —
**интегрированные target + urgency + mobilization**. Каждый допущенный приказ
Director хранит точное тело цели и нормализованную срочность `0..1`; все текущие
emergency-рецепты запрашивают `1.0`. Aggro потребляет цель, сохраняя разную силу
ALERT/ALARM, а Tempo держит ровно одну ограниченную оболочку на каждом exact
свободном responder.

`m=0` — неизменная в рамках приказа личная стабильная пара locomotion/attack,
`m=1` — детерминированный personal rage endpoint того же тела (`uEm0200`:
locomotion `1.20..1.25`, attack-only animation `1.20..1.26`). При сигнале
responder быстро попадает в `m=1`, удерживается там, затем после обычного конца
свидетельства линейно возвращается за 1400 ms. Повторные сигналы только
refresh/maximize одну оболочку: не умножают boost, не снимают baseline с
переходного кадра и не ratchet endpoints. Небезопасная потеря identity, species,
readiness, topology/world state, timeout, rollback, disable или shutdown делает
немедленный hard reset.

Build 011 cue model сохранён: `GrabStart` остаётся слабым превентивным ALERT,
`Hagaijime4Feet` — независимо достаточным ground-pin ALARM, а literal lift —
отдельным рецептом. Exact pinned `uEm0200` исключается; после строгого допуска
continuation приклеен к исходной exact holder/victim паре. Новых F12 controls
нет. Absolute-current-HP PackMark, Guardian, принятый стабильный Tempo profile,
HP/damage/stagger/immunity/inclinations и native combat вне приказа не изменены.
Подробно: [`docs/SOURCE_OF_TRUTH.md`](docs/SOURCE_OF_TRUTH.md) §8; прототип — `docs/archive/MONSTER_TARGETING_PROTOTYPE.md`.

## Что уже работает

### Main Pawn runtime AI

- dynamic `uPlayer/uCmc` discovery через DTI;
- current action/FSM observation;
- 83 pawn AI resources в offline catalog;
- 85 priority rows и 48 live buckets;
- personality/order modifiers;
- generalized persistent profiles из 0..48 exact rules;
- multi-rule transaction, readback, convergence и rollback;
- автоматическое повторное применение после загрузки мира;
- planner current code и indexed `PlanCtrl`;
- 352 weapon/action eligibility rows в generated CSV.

Priority profiles не запускают действие насильно. Они меняют штатный порядок намерений; GOAP, eligibility и FSM сохраняют право отказаться от физически невозможного действия.

### Monster runtime AI — темп движений

Две независимые ручки на каждую особь, обе с жёсткими пределами:

| Ручка | Что меняет | Как | Пределы |
|---|---|---|---|
| скорость передвижения | как быстро монстр сближается и уклоняется | общий asm-hook dash/run/walk + отдельный optional sprint path | 0.75…1.30 |
| темп атаки | замах, удар, восстановление | запись в ряд из пяти множителей воспроизведения (`тело +0x0EE4…+0x0EF4`) | 0.70…1.40 |

- множитель у каждой особи **свой**, детерминирован от адреса тела: стая
  сближается вразнобой, выученный ритм боя не работает;
- темп атаки применяется **только во время атак** (класс действия берётся
  из `ActMap`: 873 состояния, 36 emId-групп, 216 атак), поэтому ходьба
  остаётся ванильной и две ручки не дублируют друг друга;
- пять live-validated legacy wolf actions (`AttackRun`, `Bite`,
  `ContinueBite`, `JumpBite`, `DownBite`) внесены явным allowlist без опасного расширения
  общего regex; proof-лог печатается на входе в mapped attack, а не каждый кадр;
- `animCoupling` связывает ручки: 0 — четыре характера (увалень,
  наскок-отскок, засадный, берсерк), 1 — цельные существа;
- правки **мультипликативные**: торпор и захват продолжают работать поверх;
- пресеты Vanilla / Ragged / Sprinters / Predators;
- цена измеряется в панели: единицы микросекунд на кадр;
- допуск вида перед первой записью, чтобы система переносилась на другие
  виды без риска (`docs/SPECIES_ROLLOUT.md`).

Урон и HP не трогаются вообще: меняется ритм боя, а не числа.

### Monster director

`src/monsterai/MonsterDirector.{h,cpp}` — сторона монстров, симметричная
оркестратору пешек. Обе стороны читают **одну шину** (`CombatBus`). Стратегия
сохраняет принятую momentary absolute-HP priority и hysteresis-held `PackMark`:
`isolation` определяет NONE/BIAS/FOCUS-WINDOW, а `targetDepth` остаётся
диагностикой.

Отдельный `TacticalCues` — маленький универсальный table-driven matcher, а не
wolf-условие внутри Director. Таблица сохраняет три независимые рецепта:

- `cPlActGrabStart` / `cPlActHagaijime` + exact `uEm0100` →
  `GOBLIN-GRAB-ALERT`, priority 90, pin + goblin-fakehit (без suppress),
  **std-rush Tempo** (`SpeciesCard` 84.21: loco `1.21..1.24`, anim `1.32..1.40`),
  lease до 4000 ms; пустая карта `0/0` на `+0x2FA0` будится в `1/4`;
- `cPlActGrabStart` → `PACK-GRAB-ALERT`, priority 100, pin-only focus, lease до
  750 ms;
- `cPlActHagaijime4Feet` → `PACK-GROUND-PIN-ALARM`, priority 200, полный alarm,
  lease до 4000 ms;
- `cPlActLift* + cEm0200Lifted` → отдельный `PACK-LIFT-RESCUE`, priority 150,
  lease до 2500 ms.

Ground pin — это прижатие волка/завра весом тела к земле, не lift. Для ground
rules exact party action доказывает holder role, exact kind и одна уникальная
пространственная пара не дальше 2 m определяют pinned body. Literal lift
сохраняет более строгую глобальную уникальность victim action. Сильный rule
не зависит от precursor: `Hagaijime4Feet` допускается напрямую. Priority
гарантирует, что ALARM вытесняет ALERT в ближайшем 150 ms scan.

Тактический intent временно обгоняет стратегический PackMark, но не стирает его
память. Приказ переносит exact target body и urgency через policy. Aggro отвечает
только за цель: ALERT арендует pin row, ALARM — pin+suppress+fake-hit. Tempo
отдельно мобилизует всех exact свободных responders до личных rage endpoints;
оба response tier запрашивают `urgency=1.0`, но сохраняют разные Aggro bundle и
lease. Exact restrained body не получает ни Aggro write, ни Director envelope.

Одна bounded Tempo row на body держит immutable `L0/A0/L1/A1`. Refresh только
продлевает TTL/максимизирует уровень. Ordinary command/evidence completion
переводит текущих owners в 1400 ms decay. Любая небезопасная потеря или rollback
вызывает `HardResetAllDirectorMobilization()`; generic overrides не очищаются.
Композиция: stable baseline → Director envelope → generic override → final clamp.

Обе существующие галки `[monsterAI] enabled` и `wolfActuator` по умолчанию
выключены; новых элементов F12 нет. Occupied-on-field identity (84.23: слот без тела = рифт, не авария;
пустой Hired и rifted Main/Hired пропускаются; Аризен без тела — нет), свежесть world snapshot, exact
`uEm0200`, уникальность body/pair, bounded lease, downstream readback и
rollback продолжают fail closed. Для Arisen одного имени DTI `uPlayer`
недостаточно само по себе: claim берёт pointer evidence (тело / первые `0x7F0`
записи / полный `cPlayerInfo`) или, если живой `uPlayer` ровно один, валидную
запись Arisen плюс читаемые XYZ. Ноль или два+ `uPlayer` остаются unresolved
без fallback по адресу, live-list или порядку скана.

#### Полезная диагностика интегрированной policy

Автоматический лог рассчитан на редкие snapshots, а не на отслеживание быстрых
чисел вручную:

- `policy ENGAGED` — доказательство реально принятой команды: exact `targetBody`,
  `response=ALERT|ALARM`, `urgency`, exact `excluded`, количество `responders` и
  `tempoOwned`, а также неизменные диапазоны endpoints `L0/A0/L1/A1`;
- `policy RELEASED ... mobilization=DECAY` — обычный конец приказа;
  `HARD-RESET` — небезопасная потеря topology/identity/readiness, timeout,
  rollback, disable или shutdown;
- `policy FAIL-CLOSED ... HARD-RESET-ONCE` печатается один раз на длительный
  неактивный unsafe episode. Повторные NONE/BIAS/identity evaluations без owned
  state не изображают новые release transitions. `policy RECOVERED` сообщает
  первую успешную команду и число `coalesced` повторов;
- `PartyRecon: adopted uPlayer ... via unique-live-uPlayer` (или
  `player-record-pointer` / `record-body-pointer` / `info-record-pointer`)
  доказывает точный Arisen claim; `unresolved: no player-record-pointer` —
  безопасно проигнорированный class-only candidate (0 или 2+ `uPlayer`);
- Aggro summary сохраняет bundle (`pin-only` для ALERT,
  `pin+suppress+fakehit` для ALARM), `left` (занятые чужой картой `fC=2`),
  writes и rollback count.

`writes` — накопительный счётчик подтверждённых Tempo/Aggro операций, не уровень
ускорения. При refresh он закономерно растёт пропорционально числу responders;
отсутствие ratchet доказывают стабильные `L0/A0/L1/A1`, а не маленькое значение
`writes`. Live replay двух wolf fights подтвердил exact exclusion, ALERT→ALARM,
9→8 hard-reset/re-admission, no-spatial rejection, ordinary decay, timeout reset
и нулевые Aggro rollbacks; обнаруженный class-only `uPlayer` outage закрыт
fixed-record gate и отдельным duplicate-claim regression.

### Pawn tempo compensation (`[pawnHaste]`)

У пешек в бою **нет спринта**: планировщик коды рывка (`84 DashFollow`,
`85 DashFollowSt500`) не выбирает никогда, а строк приоритета у них нет ни
одной из 85. Все пойманные рывки приходят под кодом `1 Follow`.

Раз дыру в мобильности мы усугубили сами, ускорив монстров, — модуль её и
компенсирует:

| ключ | что делает |
|---|---|
| `matchMonsterTempo` | множитель равен множителю самого быстрого врага рядом; монстры ванильные — компенсации нет вовсе |
| `animCouple` | вместе с перемещением ускоряется **анимация бега**: поле `[0]` ряда `+0x0EE4` (покой 1.000, шаг 1.060, бег 1.150) |
| `requireWeaponDrawn` | признак боя у самой пешки (`cPlActHoldWeaponMv`, семейство `cPlActWpn*`) ИЛИ подтверждение детектора — враги за стеной не триггерят |

Область записи узкая: только состояния передвижения (включая перенос
`cPlActLift*`), только поле `[0]`, исходные значения возвращаются при
снятии. Проверено арифметикой в живом логе: `1.150 × 1.07 = 1.234` в беге
при `1.000` в замахе. Подробно — `docs/PAWN_SPRINT_RECON.md` §30-37.

### Caster AI range (`[errata] wandRange`)

Пешка-маг/чародей: живой `cCmc* +0x258`, RangeMax **1–10 м → 15 м** (как лук).
Все `uCmc`. Касты включая Anodyne/Levin; **IceWalk** (Frigor) короткий.
Игрок, снаряд, StandOff, мили — нет. Галка в F12 / `wandRange = on`.
Документ: [`docs/WAND_RANGE.md`](docs/WAND_RANGE.md).

### Existing modules

- Pawn inclination modules: presets, Acquisitor Manager, Smart Utilitarian, Tactical Switch, Guardian Doctrine;
- CombatIntel/CombatBus и bestiary mapping;
- Camera Plus: tactical free camera with optional player tracking, pause, party cam slider: Arisen <-> main pawn;
- WorldScan/PartyRecon/PriorityPlatform — продуктовый рантайм (`src/runtime/`);
- TypeAtlas/SCAN/DUMP/HUNT — исследовательская платформа (`src/devtools/`, отключаемая);
- experimental EnemyTuner.

## Важные ограничения

Проект — development milestone, а не законченный пользовательский AI overhaul.

Что уже работает (Guardian Doctrine):
- трекинг игрока/пешки/врагов + диспозиция боя;
- ролевая матрица (вокация пешки × вокация игрока): Protector/Assault/Adaptive/…;
- градиентная зона телохранителя (preempt/melee) со снятием Guardian-штрафа
  на кинжалы (code 54) транзакционно;
- трёхсигнальный детектор боя (урон + боевые действия врагов + цель пешки).

Ещё не завершены:

- **настоящий спринт пешек** — точка входа моторной команды не найдена;
  идентификатор `0xAD` из `.gop` больше не нужен (ручка уровнем выше — код
  цели), но запускать действие мы по-прежнему не умеем, только наблюдать.
  Открытые зацепки: пешки-проводники в квесте сопровождения бегут спринтом
  (единственный воспроизводимый случай) и цепочка интерфейсов внутри цели
  `Follow`;
- **уклонение для вокаций без переката** (Маг/Чародей/Варриор) — рычаг
  найден и ждёт замера на подходящей вокации: коды `32 Precaution`,
  `28 StandOff`, `73 EscapeNotice1`, `58 DmgUkemi`, `2 Jump` имеют строки
  приоритета, то есть доступны транзакционному `AddS32`;
- **текущее HP существа** — смещение не найдено; без него не сделать
  политики вроде «ярости агонии» (протокол поиска описан в
  `docs/SOURCE_OF_TRUTH.md` §13);
- **человеческие враги** — оружие, вокации и аугменты образуют отдельную
  систему; темп на них намеренно не переносится;
- поводок (follow-дистанция) — роль-зависимый, НЕ найден (см. GUARDIAN_LEASH_MATRIX.md);
- semantic names всех priority codes (code 4/66 в техдолге);
- Файтер/Варриор (код меча/двуручника) — ждёт смены вокации;
- Nexus doctrine (anchor = выбранная пешка);
- monster priority/planner bridge.

## Сборка

1. Открыть `ddda-ai-overhaul.sln` или `.vcxproj` в Visual Studio.
2. Выбрать **Release | Win32**.
3. Build Solution.
4. Скопировать `dinput8.dll` в папку с `DDDA.exe`. **Ini копировать не нужно** —
   мод создаст `ddda_ai_overhaul.ini` сам и допишет новые ключи при обновлении,
   не трогая уже выставленные значения. Файл `ddda_ai_overhaul.default.ini` в
   репозитории — только справочник.

> **Сверяйте тег.** Первая строка лога печатает `MOD_BUILD_TAG` — это
> единственный надёжный способ убедиться, что игра запустила именно ту
> DLL, которую вы собрали. За сессию 20.08 два замера были сняты со
> старой библиотекой, и оба раза мы едва не начали чинить несуществующее.

Подробно (требования, установка в игру, разбор ошибок): [`docs/BUILD_INSTRUCTIONS_RU.md`](docs/BUILD_INSTRUCTIONS_RU.md).

Если уже используется другой `dinput8.dll`, его можно загрузить цепочкой через `loadLibrary` в ini.

## Основные файлы данных

```text
DDDA_AI_Overhaul/ddda_pawn_ai_profiles.ini
```

Priority sidecar schema v2 идентифицирует rule без transient address:

```text
sensor / code / category / objectId / extra / ruleIndex
```

Профиль проверяет все expected fields до записи и применяется транзакционно.

## Hotkeys

| Key | Action |
|---|---|
| F12 | открыть UI |
| F4 | Camera Plus |
| стрелки + PgUp/PgDn | free-fly camera |
| Num 0 | pause toggle |
| `-` | profile switch в исследовательских билдах |
| `=` / `+` | pawn AI snapshot в исследовательских билдах |

**F9 модом не используется** — у пользователя это сохранение игры.

## Слои

```text
src/runtime/    продукт: работает всегда, не зависит от DevTools
src/devtools/   исследование: выключается целиком через [devtools] enabled = off
```

Продукт не вызывает research напрямую — только через `Runtime::ResearchHooks`.
Инвариант проверяется `tools/analyze_devtools_layers.py` (код возврата 1 при нарушении).

## Проверки перед сборкой

MSVC есть не у всех, а каждая ошибка компиляции стоит целой итерации
(сборка → запуск игры → лог). Поэтому проверки запускаются локально:

```bash
python3 tools/check_link_sanity.py          # «линкер бедняка»: 8+ видов проверок
python3 tools/analyze_devtools_layers.py    # страж слоёв, код возврата 1 при регрессе
bash    tools/test_monster_director_hp_only.sh   # Build 012 urgency/mobilization lifecycle
bash    tools/test_build004_contracts.sh         # retained identity/ownership contracts
bash    tools/test_build005_locomotion_proof.sh # retained hook-side locomotion receipts
bash    tools/test_build008_qol.sh                # profile/toggle/bounded-log contracts
bash    tools/test_act_map_build004.sh           # determinism + five wolf attacks
python3 tools/check_cpp_literals.py               # malformed C++ string/char literals
bash    tools/test_packobserve.sh                 # goblin card + PackObserve + grab pin
bash    tools/syntax_check.sh                    # g++ modules + ASCII UI
```

`syntax_check.sh` компилирует `AnimProbe`, `MonsterDirector` и `PawnHaste`
под g++ со шимом `windows.h`, проверяет UI-блок на настоящем `imgui.h` 1.48
и ищет не-ASCII в строках интерфейса (дефолтный шрифт рисует их как `?`).

## Архитектура AI

```text
Sensors / target selection
  → Priority policy
  → selected intent
  → GOAP planner
  → action eligibility
  → FSM/current Act
  → motion/collision/damage
```

Основной продуктовый слой — priority. GOAP рассматривается как место точечных статических исправлений, FSM — как хирургический уровень, а не способ прямого командования.

## Документация

| Документ | Назначение |
|---|---|
| [`docs/VISION.md`](docs/VISION.md) | замысел: три слоя и философия проекта |
| [`docs/TEMPO_SYSTEM.md`](docs/TEMPO_SYSTEM.md) | система темпа: примитив, две ручки, связка, пресеты, замеры |
| [`docs/SPECIES_ROLLOUT.md`](docs/SPECIES_ROLLOUT.md) | перенос темпа на остальные виды: допуск вида, классификация атак |
| [`docs/GOBLIN_PACK_OBSERVE.md`](docs/GOBLIN_PACK_OBSERVE.md) | ночной observe-прибор exact `uEm0100`: протокол берега |
| [`docs/PAWN_SPRINT_RECON.md`](docs/PAWN_SPRINT_RECON.md) | трек спринта/уклонения: коды целей, приоритетные строки, компенсация темпа |
| [`docs/WAND_RANGE.md`](docs/WAND_RANGE.md) | **эррата посоха пешки**: 15 м eligibility, не игрок |
| [`docs/PAWN_IDLE_RECON.md`](docs/PAWN_IDLE_RECON.md) | разнообразие простоя вне боя: почему пул НПЦ закрыт и что взамен |
| [`docs/HIRED_PAWNS_SCOPE.md`](docs/HIRED_PAWNS_SCOPE.md) | наёмные пешки: что можно, что нельзя, и замер общего ресурса приоритетов |
| [`docs/generated/STATUS_PARAM.md`](docs/generated/STATUS_PARAM.md) | 40 слотов статусов; слот 7 = Possession (CATALOG) |
| [`docs/archive/`](docs/archive/) | замороженные дневники охоты и старые контракты |
| [`docs/ANATOMY_EM0100.md`](docs/ANATOMY_EM0100.md) | анатомия гоблина — справочник для модеров |
| [`docs/README.md`](docs/README.md) | индекс документации |
| [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) | архитектура платформы |
| [`docs/SOURCE_OF_TRUTH.md`](docs/SOURCE_OF_TRUTH.md) §13 | открытые пробелы |
| [`docs/SOURCE_OF_TRUTH.md`](docs/SOURCE_OF_TRUTH.md) | подтверждённые контракты |
| [`docs/FIELD_MAP.md`](docs/FIELD_MAP.md) | offsets |
| [`docs/ASSET_FORMATS.md`](docs/ASSET_FORMATS.md) | игровые resource formats |
| [`docs/PLAYER_PAWN_WORK/`](docs/PLAYER_PAWN_WORK/) | Main Pawn vertical slice |
| [`CHANGELOG.md`](CHANGELOG.md) | мастер-индекс по дням (детали — `docs/changelog/`) |

Завершённые `TEST_*`, protocol и промежуточные result-документы удалены из текущего дерева; история остаётся в Git.

## Принципы безопасности

- не сохранять heap pointers;
- не менять `DDDA.sav`;
- не угадывать offsets;
- validate → write → readback → convergence → rollback;
- unknown version/object → vanilla fallback;
- `game_main.arc` repack — только резервный PACK-путь.

## Credits

- [Arena.ai](https://arena.ai) — ИИ-ассистент и лид-программист runtime-платформы;
- kubik-jaroslav — ddda-dinput8 architecture;
- Cielos — Cheat Engine research;
- Atvaark — DragonsDogma.Research/types;
- chrispurnell — pawn knowledge data;
- Lefein — World Difficulty and DDDA AI modding groundwork;
- FluffyQuack — ARCtool;
- TsudaKageyu — MinHook;
- ocornut — Dear ImGui.

MIT License.
