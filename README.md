# DDDA AI Overhaul

Runtime AI platform for **Dragon's Dogma: Dark Arisen** (Steam/GOG, x86).

> Умеем изменить живую политику — делаем LIVE. Игровые архивы используются как каталог, а не как основной способ установки.

**Текущий milestone:** Build 73 — **рантайм-управление темпом движений
монстров**. Найден и подтверждён ряд множителей скорости воспроизведения
анимации в теле существа; на его основе собрана система из двух независимых
ручек (скорость передвижения и темп атак) с разбросом по особям, пресетами
и швом для будущего контроллера мутаций. Тем же примитивом залатана дыра на
стороне союзников: у пешек в бою нет спринта вообще.

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
| скорость передвижения | как быстро монстр сближается и уклоняется | два asm-хука на покадровое смещение координат | 0.75…1.30 |
| темп атаки | замах, удар, восстановление | запись в ряд из пяти множителей воспроизведения (`тело +0x0EE4…+0x0EF4`) | 0.70…1.40 |

- множитель у каждой особи **свой**, детерминирован от адреса тела: стая
  сближается вразнобой, выученный ритм боя не работает;
- темп атаки применяется **только во время атак** (класс действия берётся
  из `ActMap`, 812 действий 35 видов), поэтому ходьба остаётся ванильной и
  две ручки не дублируют друг друга;
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
оркестратору пешек. Обе стороны читают **одну шину** (`CombatBus`) и
управляют своими примитивами. Режиссёр не пишет в память игры никогда:
только читает бой и раздаёт указания через `Tempo::SetOverride`.
Пока наблюдатель, гейт `[monsterAI] enabled`.

### Pawn combat haste

У пешек в бою **нет спринта**: разведка GOAP показала, что даш существует
отдельным действием, но привязан к цели Follow. Модуль `[pawnHaste]`
поднимает пешке множитель передвижения, пока она закрывает дистанцию.
Честно названо подделкой: анимация остаётся джогом
(`docs/PAWN_SPRINT_RECON.md`).

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

- **настоящий спринт пешек** — сейчас подделка через множитель скорости;
  цель — найти точку входа моторной команды даша (`cCmc`, id `0xAD`);
- **текущее HP существа** — смещение не найдено; без него не сделать
  политики вроде «ярости агонии» (протокол поиска описан в
  `docs/MONSTER_AI_ARCHITECTURE.md`);
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
4. Скопировать `dinput8.dll` и `ddda_ai_overhaul.ini` в папку с `DDDA.exe`.

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
(сборка → запуск игры → лог). Поэтому три проверки запускаются локально:

```bash
python3 tools/check_link_sanity.py        # «линкер бедняка»: 8+ видов проверок
python3 tools/analyze_devtools_layers.py  # страж слоёв, код возврата 1 при регрессе
sh      tools/syntax_check.sh             # g++ по ключевым модулям + ASCII в UI
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
| [`docs/NEXT_MILESTONE_OPTIONS.md`](docs/NEXT_MILESTONE_OPTIONS.md) | три трека после Build 73: спринт пешек, память места, дышащий мир |
| [`docs/TEMPO_SYSTEM.md`](docs/TEMPO_SYSTEM.md) | система темпа: примитив, две ручки, связка, пресеты, замеры |
| [`docs/MONSTER_AI_ARCHITECTURE.md`](docs/MONSTER_AI_ARCHITECTURE.md) | две стороны и одна шина; контракт режиссёра; замыслы политик |
| [`docs/SPECIES_ROLLOUT.md`](docs/SPECIES_ROLLOUT.md) | перенос темпа на остальные виды: допуск вида, классификация атак |
| [`docs/PAWN_SPRINT_RECON.md`](docs/PAWN_SPRINT_RECON.md) | почему пешки не спринтят и три способа это залатать |
| [`docs/MONSTER_TEMPO_RECON.md`](docs/MONSTER_TEMPO_RECON.md) | история охоты за темпом: что исключили и как нашли |
| [`docs/STATUS_EFFECTS_RECON.md`](docs/STATUS_EFFECTS_RECON.md) | статусы и торпор; карта `cCharParamEnemy` (72 поля) |
| [`docs/TEMPO_HUNT_PROTOCOL_RU.md`](docs/TEMPO_HUNT_PROTOCOL_RU.md) | протокол исследования: что вводить и когда жать |
| [`docs/ANATOMY_EM0100.md`](docs/ANATOMY_EM0100.md) | анатомия гоблина — справочник для модеров |
| [`docs/README.md`](docs/README.md) | индекс документации |
| [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) | архитектура платформы |
| [`docs/ROADMAP.md`](docs/ROADMAP.md) | актуальный план |
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
