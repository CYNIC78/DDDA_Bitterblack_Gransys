# DDDA AI Overhaul

Runtime AI platform for **Dragon's Dogma: Dark Arisen** (Steam/GOG, x86).

> Умеем изменить живую политику — делаем LIVE. Игровые архивы используются как каталог, а не как основной способ установки.

---

## 🎯 Текущий Milestone: Build 84.64 (`84.64-tactical-dpad-commands`)

Тактическая система приказов D-Pad («Ко мне!» / «Вперед!» / «Помогите!») с плавным затуханием (6с decay) и пином целей:

1. **Тактическая система приказов (Tactical Command Engine)**:
   - **«Вперед!» (Go! / F1 / D-pad Up)**: мгновенный пин цели в фокусе прицела камеры/лука Аризена (`uCmc + 0x2EB8`) штурмовым пешкам со скоростным рывком (x1.25). Ситуативный импульс: Scather +300, Pioneer +200.
   - **«Ко мне!» (Come! / F3 / D-pad Down)**: мобильный сбор — отставшие пешки (> 6м) на спринте стягиваются к Аризену в защитную коробку. Ситуативный импульс: Guardian +350.
   - **«Помогите!» (Help! / F2 / D-pad Left/Right)**: мобилизация защиты и приоритет снятия дебаффов/одышки. Ситуативный импульс: Medicant +300, Utilitarian +200.
   - **Плавное затухание (Smooth 6s Decay)**: через 6 секунд веса 100% плавно возвращаются к вашим базовым ползункам без единого байта дрифта в `DDDA.sav`.
2. **Синхронная коробка защиты (Dual-Bodyguard Formation)**:
   - **Guardian (Фронт)**: защищает Аризена от ближних атак и фланкирования.
   - **Nexus (Тыл)**: прикрывает Чародейку/Мага (`Backline Protector`) от сбивания кастов (`PLAYER-CHANT-HARASS`).
   - Подтвержден рекордный прирост успешности кастов до **81.4%** (35 из 43 завершено в бою) при 70 перехватах Nexus.
3. **Устранение эффекта "Бенни Хилла" на сабельном раше (`SwMoveAttack`)**:
   - Атака непрерывного бега со взмахами саблей (`cEm0100ActSwMoveAttack`) исключена из ускорения темпа (`factor = 1.0f`).
4. **Гибридный Nuke Gating (Угроза в упор + Одиночный моб)**:
   - Блокировка 15с спеллов при угрозе в упор (< 6м) и на добивании 1 моба.
   - Свобода зачистки сцены (Crowd Wipe) на пачках и боссах.
5. **Оптимизация дистанции HFB (`RangeMinXZ = 4.5 м`)**:
   - Минимальная планка зарядки Focused Bolt установлена на 4.5 метра (`4.5..20 м`).
6. **Общепартийное спасение Аризена (Party Emergency Rescue Protocol)**:
   - Автоматическая мобилизация всех пешек при захвате/нокдауне Аризена.
7. **DDON Return Sanctuary**:
   - 4-кратная броня и x1.30 скорость отхода на спавн у отступающих монстров.

---

## 📜 Хронология ключевых майлстоунов

| Milestone | Тег | Ключевое нововведение |
|---|---|---|
| **84.64** | `84.64-tactical-dpad-commands` | Тактические приказы D-Pad (пин цели, спринт-сбор, 6с decay) |
| **84.62** | `84.62-dual-guardian-nexus-synergy` | Синхронная работа Guardian (Аризен) + Nexus (Чародейка) |
| **84.61** | `84.61-nexus-doctrine-wingman` | Nexus Doctrine: выбор напарника-пешки (защита кастов/штурм) + перехват |
| **84.60** | `84.60-hobgoblin-flail-tempo-tuner` | Калибровка ярости гоблинов + фикс ускорения раша саблей |
| **84.59** | `84.59-caster-proximity-tactics` | Гибридный Nuke Gating (угроза в упор + 1 моб) + HFB от 4.5м |
| **84.58** | `84.58-smart-nukes-chimera-safety` | Фикс безопасности составных боссов (Химера) + строгий Nuke Gating |
| **84.56** | `84.56-caster-tactics-hfb-logger` | Онлайн-трекер Holy Focused Bolt (HFB) + Аналитика кастов спеллов |
| **84.55** | `84.55-pawn-combat-mastery` | Двухуровневый Гвардиан + Спасение Аризена + Умный кастер |
| **84.46** | `84.46-ddon-sanctuary` | Броня и скорость возврата на спавн в стиле DDON |
| **84.45** | `84.45-scan-optimizer` | Кэш `charParamOff`, спящий поллинг, 0-alloc log |
| **84.44** | `84.44-genetic-scale` | Генетический масштаб особей стаи + защита альфа-вожаков Capcom |
| **84.43** | `84.43-true-torpor` | Истинный Торпор (`id=1`, 0.5x) и Debilitation Lab |
| **84.40** | `84.40-caster-harass` | Тактический прессинг Аризена-кастера стаей |
| **84.39** | `84.39-caller-defense` | Проактивная защита горниста гоблинов |
| **84.23** | `84.23-on-field` | Занятость на поле: пропуск rifted/пустых слотов |
| **84.21** | `84.21-species-rage` | Видовые rage-профили в `SpeciesCard` |
| **84.18** | `84.18-card-recon` | Универсальный прибор CARDRECON |
| **84.16** | `84.16-dual-observe` | Приборы GOBCARD + PartyStatus (read-only) |
| **84.15** | `84.15-goblin-grab-hold` | Удержание захвата гоблина `GrabStart`/`Hagaijime` |
| **84.14** | `84.14-goblin-grab-pin` | Пин свободных оппортунистов гоблинов |
| **84.13** | `84.13-leave-engaged` | Сохранение боя у занятых волков вне марка |
| **84.12** | `84.12-wolf-combat-card` | Запись боевой карты `f8=1 fC=2` |
| **84.11** | `84.11-arisen-record-glue` | Пожизненная привязка живого `uPlayer` к записи Arisen |
| **84.10** | `84.10-goblin-pack-observe` | Ночной observe-прибор exact `uEm0100` |
| **84.9** | `84.9-pilot012-urgency-mobilization` | Интегрированные target + urgency + mobilization |

Подробный журнал изменений: [`CHANGELOG.md`](CHANGELOG.md) | Канон: [`docs/SOURCE_OF_TRUTH.md`](docs/SOURCE_OF_TRUTH.md).

---

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

Урон, защита, HP и темп особей в стае балансируются формулами для создания архетипов (увалень, спринтер, берсерк, джаггернаут и т.д.) и их гибридов.

### Генетический масштаб и индивидуальность стаи (Build 84.44)

Масштаб особи (`тело +0x60 / +0x64 / +0x68`) в MT Framework масштабирует скелет,
дальность шага и радиус хитбоксов:

- **Видовые коридоры в `SpeciesCard`**: гоблины `0.93..1.15`, хобгоблины `0.95..1.14`,
  волки `0.94..1.10`, саурианы `0.95..1.12`. Нижний порог защищает от мажущих хитбоксов.
- **Защита альфа-вожаков (Capcom Leader Immunity)**: если ванильный спавн имеет
  `baseScale >= leaderThreshold` (альфа-волки, крупные вожаки), их авторский размер
  сохраняется (`LEADER_PRESERVED`) и не раздувается.
- **Компенсация частоты шага (Cadence Boost)**: особи меньше `1.0` получают автоматический
  бонус частоты шагов в `MonsterTempo` (`cadence = 1.0 / scale`), благодаря чему легковесы
  чаще семенят ногами, не отстают от стаи и ощущаются шустрыми загонщиками.

### Monster director

`src/monsterai/MonsterDirector.{h,cpp}` — сторона монстров, симметричная
оркестратору пешек. Обе стороны читают **одну шину** (`CombatBus`). Стратегия
сохраняет принятую momentary absolute-HP priority и hysteresis-held `PackMark`:
`isolation` определяет NONE/BIAS/FOCUS-WINDOW, а `targetDepth` остаётся
диагностикой.

Отдельный `TacticalCues` — маленький универсальный table-driven matcher, а не
wolf-условие внутри Director. Таблица сохраняет проверенные тактические рецепты:
- `cPlActGrabStart` / `cPlActHagaijime` + exact `uEm0100` → `GOBLIN-GRAB-ALERT`;
- `cPlActGrabStart` → `PACK-GRAB-ALERT`;
- `cPlActHagaijime4Feet` → `PACK-GROUND-PIN-ALARM`;
- `cPlActLift* + cEm0200Lifted` → `PACK-LIFT-RESCUE`;
- `cEm0100ActHorn*` + exact `uEm0100`/`uEm0101` → `GOB-HORN-ALERT` / `HOB-HORN-ALERT`;
- `cEm0200Howling` / `cEm0400ActFriendHowl` → `WOLF-HOWL-ALERT` / `SAURIAN-HOWL-ALERT`;
- `cPlActWpnWandBase` / `MagicBow` / `MagicShieldBase` + Arisen only → `PLAYER-CHANT-HARASS`.

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

### Caster AI range (`[errata] wandRange`)

Пешка-маг/чародей: живой `cCmc* +0x258`, RangeMax **1–10 м → 15 м** (как лук).
Все `uCmc`. Касты включая Anodyne/Levin/Focused Bolt; **IceWalk** (Frigor) короткий.
Игрок, снаряд, StandOff, мили — нет. Галка в F12 / `wandRange = on`.

### Existing modules

- Pawn inclination modules: presets, Acquisitor Manager, Smart Utilitarian, Tactical Switch, Guardian Doctrine;
- CombatIntel/CombatBus и bestiary mapping;
- Camera Plus: tactical free camera with optional player tracking, pause, party cam slider: Arisen <-> main pawn;
- WorldScan/PartyRecon/PriorityPlatform — продуктовый рантайм (`src/runtime/`);
- TypeAtlas/SCAN/DUMP/HUNT — исследовательская платформа (`src/devtools/`, отключаемая);
- experimental EnemyTuner.

## Важные ограничения

Проект — development milestone, а не законченный пользовательский AI overhaul.

Что уже работает (Guardian Doctrine & Rescue Protocol):
- трекинг игрока/пешки/врагов + диспозиция боя;
- двухуровневый защитный периметр (Critical Melee 4–6м, Preempt Intercept 6–12м);
- общепартийное спасение Аризена при захватах и нокдаунах;
- ролевая матрица (вокация пешки × вокация игрока): Protector/Assault/Adaptive/…;
- градиентная зона телохранителя (preempt/melee) со снятием Guardian-штрафа транзакционно;
- трёхсигнальный детектор боя (урон + боевые действия врагов + цель пешки).

Ещё не завершены:
- **человеческие враги** — оружие, вокации и аугменты образуют отдельную систему; темп на них намеренно не переносится;
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
> DLL, которую вы собрали.

Подробно (требования, установка в игру, разбор ошибок): [`docs/BUILD_INSTRUCTIONS_RU.md`](docs/BUILD_INSTRUCTIONS_RU.md).

## Hotkeys

| Key | Action |
|---|---|
| F12 | открыть UI |
| F4 | Camera Plus |
| стрелки + PgUp/PgDn | free-fly camera |
| Num 0 | pause toggle |
| `-` | profile switch в исследовательских билдах |
| `=` / `+` | pawn AI snapshot в исследовательских билдах |

## Слои

```text
src/runtime/    продукт: работает всегда, не зависит от DevTools
src/devtools/   исследование: выключается целиком через [devtools] enabled = off
```

Продукт не вызывает research напрямую — только через `Runtime::ResearchHooks`.
Инвариант проверяется `tools/analyze_devtools_layers.py` (код возврата 1 при нарушении).

## Проверки перед сборкой

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

## Документация

| Документ | Назначение |
|---|---|
| [`docs/VISION.md`](docs/VISION.md) | замысел: три слоя и философия проекта |
| [`docs/RESEARCH_BACKLOG_LEASH_SIGHT_PAWN_AGGRO.md`](docs/RESEARCH_BACKLOG_LEASH_SIGHT_PAWN_AGGRO.md) | бэклог: поводки, зрение, боссы и агр |
| [`docs/TEMPO_SYSTEM.md`](docs/TEMPO_SYSTEM.md) | система темпа: примитив, две ручки, связка, пресеты, замеры |
| [`docs/SPECIES_ROLLOUT.md`](docs/SPECIES_ROLLOUT.md) | перенос темпа на остальные виды: допуск вида, классификация атак |
| [`docs/GOBLIN_PACK_OBSERVE.md`](docs/GOBLIN_PACK_OBSERVE.md) | ночной observe-прибор exact `uEm0100`: протокол берега |
| [`docs/PAWN_SPRINT_RECON.md`](docs/PAWN_SPRINT_RECON.md) | трек спринта/уклонения: коды целей, приоритетные строки, компенсация темпа |
| [`docs/WAND_RANGE.md`](docs/WAND_RANGE.md) | **эррата посоха пешки**: 15 м eligibility, не игрок |
| [`docs/ANATOMY_EM0100.md`](docs/ANATOMY_EM0100.md) | анатомия гоблина — справочник для модеров |
| [`docs/README.md`](docs/README.md) | индекс документации |
| [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) | архитектура платформы |
| [`docs/SOURCE_OF_TRUTH.md`](docs/SOURCE_OF_TRUTH.md) §13 | открытые пробелы |
| [`docs/SOURCE_OF_TRUTH.md`](docs/SOURCE_OF_TRUTH.md) | подтверждённые контракты |
| [`docs/FIELD_MAP.md`](docs/FIELD_MAP.md) | offsets |
| [`CHANGELOG.md`](CHANGELOG.md) | мастер-индекс по дням (детали — `docs/changelog/`) |

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
