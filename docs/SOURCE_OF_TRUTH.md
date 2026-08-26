# Source of Truth

Канонические runtime-контракты DDDA AI Overhaul / Bitterblack Gransys.
Если поле, связь или политика отсутствует здесь — **использовать в продуктовом
коде нельзя** без нового живого подтверждения и записи в этот файл.

**Milestone:** Build `84.37-xmm-params` (`src/BuildTag.h`).
Тегов в git remote **нет** (0 tags). Единственный надёжный идентификатор
сборки — первая строка лога / F12: `MOD_BUILD_TAG`.

**Платформа:** x86, Release/Win32, Steam/GOG. Резолв через сигнатуры и DTI.
Transient heap addresses и VA vtable одного запуска **не канонизируются**.

**Как читать статусы строк**

| Метка | Значение |
|---|---|
| `CONFIRMED` | чтение и/или запись + readback; можно использовать |
| `OBSERVED` | живое чтение, семантика или допуск вида ещё узкий |
| `UNVALIDATED` | гипотеза / протокол охоты; писать нельзя |
| `DEPRECATED` | утверждение в другом документе или старом абзаце **ложно относительно 84.21** |

---

## 0. Реестр DEPRECATED (аудит 2026-08-25)

Старые md не удаляются: в них история охоты. Они **не источник правды**.
Ниже — логические противоречия «документ ↔ код 84.21».

### 0.1 Milestone / хаб

| Документ | Утверждает | Код 84.21 | Вердикт |
|---|---|---|---|
| этот файл (до аудита) | Build 74.0 / раскладка 73.27 | `MOD_BUILD_TAG "84.21-species-rage"` | **DEPRECATED header** |
| `PROJECT_HUB.md` | Build 84.18 `card-recon`; ветка `work/player-main-pawn-recon` | HEAD `5cc8ea5` «goblins rush system»; ветка `main` | **DEPRECATED milestone** |
| `CHANGELOG.md` «Текущий milestone» | 84.18 | 84.21 | **DEPRECATED** |
| `docs/ROADMAP.md` | Stable 47 / Active 63 | перенесён в `docs/archive/` | **ARCHIVED** |
| `docs/README.md` | ссылки на `GUARDIAN_VOCATION_MATRIX.md`, `GUARDIAN_LEASH_MATRIX.md`, `REFACTOR_TASK.md` | файлов в дереве нет | **битые ссылки** |

### 0.2 CombatBus / картина мира

| Документ | Утверждает | Код | Вердикт |
|---|---|---|---|
| `ARCHITECTURE.md` §3.1 | «точный live current-target главной пешки ещё не закреплён» | `uCmc+0x2EB8` CONFIRMED (этот файл §4); `WorldReport.pawnEngaged` | **DEPRECATED** |
| `ARCHITECTURE.md` §7 | «monster decision bridge» открыт | `MonsterAI::Director` + `TacticalCues` + `SpeciesCard` | **DEPRECATED граница платформы** |
| `MONSTER_AI_ARCHITECTURE.md` | сборка `73.4`; режиссёр — наблюдатель; все `uEm*` равны; HP нет; политика = `Tempo::SetOverride` | actuator `wolfActuator`; exact-kind; PackMark по record HP; envelope `AdmitDirectorMobilization` | **DEPRECATED целиком как контракт** (оставить как замысел) |
| `CombatBus.h` / `WorldScan` | `dominantCategory` только prefix `uEm0100`/`uEm0101` → `0` | `SpeciesCard` / Director — **exact** `strcmp`, без `_0/_3` | не баг, но **два разных предиката «гоблин»** — см. §8.3 |

### 0.3 Rage-профили и допуск вида

| Документ | Утверждает | `SpeciesCard.h` + `MonsterDirector.cpp` + `MonsterTempo.cpp` | Вердикт |
|---|---|---|---|
| `SPECIES_ROLLOUT.md` §9 | `uEm0100`: observe=да, **tempoRage=нет**, aggro=pin-only | `tempoRage=true`, rage loco `1.21..1.24`, anim `1.32..1.40`; grab-lease = **std-rush** | **DEPRECATED §9** |
| `TEMPO_SYSTEM.md` §5 | «в Build 012 допущен только exact `uEm0200`» | профили регистрирует `Director::Init` из карточек; гоблин admitted | **DEPRECATED §5** |
| `README.md` блок Director | `GOBLIN-GRAB-ALERT` «pin-only, **без Tempo**» | `PolicyResponderKind(GOBLIN_GRAB)` → `uEm0100`; `wantTempo = card->tempoRage` | **DEPRECATED абзац** |
| `CHANGELOG` 84.14 | `SpeciesCard uEm0100 tempoRage=false` | 84.20/84.21 перевернули | исторический факт 84.14; **не текущая политика** |
| `MonsterDirector.cpp` шапка / Init-лог | «immutable-**uEm0200**-endpoints»; «Build 012» | `RegisterRageProfile` для всех `tempoRage` карточек | комментарий **отстаёт от 84.21** |
| `MONSTER_AI_ARCHITECTURE.md` §5–6 | «ярость агонии» ждёт HP тела монстра | PackMark считает **HP записей партии**, не HP `uEm*` | замысел не реализован; HP врага по-прежнему **не найдено** |

### 0.4 Identity / классификация

| Документ / код | Утверждает | Конфликт |
|---|---|---|
| `BestiaryData.h` | `uEm8000` = The Dragon / Ur-Dragon, gid `0x61` | `WorldScan::KindIsHarmless` и `CombatIntel` считают **весь** `uEm8000` живностью |
| `POSSESSION_RECON.md` §1 / `ARC_MAP.txt` | `em1000` = Drake | в атласе нет `uEm1000`. Live Devilfire = **`uEm5900` / `em5900.arc`** (§8.5), не каталожный `uEm8100` |
| `BestiaryData.h` / `generate_bestiary.py` | `uEm5900` = «Evil Eyes» | ручной `BESTIARY_UEM_MAP` bid 39. Live + `em5900.arc` + FluffyQuack `em.txt` = **Drake**. Подпись в `.h` не identity |
| `SOURCE_OF_TRUTH` (старый) §2 | `kPartyBodySize` = 23056 | `RuntimeInternal.h`: `kPartyBodySize=0x5A10` (uPlayer), `kCmcBodySize=0x58E0` (uCmc=22752) — **так и есть** |
| `WorldScan.cpp` `kPawnBodyBytes` (до 84.22) | сканировал uCmc как 23056 | 84.22: `kCmcBodySize` (`0x58E0`). `PartyStatus` `0x5A40` ещё открыт — §11.4 |
| `PartyStatus.cpp` `kPartyBodyBytes = 0x5A40` | discovery 23104 B на любое тело партии | больше обоих TypeAtlas sizes; тот же класс дыры |

---

## 1. Глобальные якоря и character records

```text
pBase  = результат сигнатуры в DDDA.exe
Arisen record    = *pBase + 0xA7000
Main Pawn record = Arisen record + 0x7F0
Hired 1          = Arisen record + 0x7F0 + 0x1660
Hired 2          = Arisen record + 0x7F0 + 0x1660 * 2
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
| `+0x984` | float | Strength (CORE, не loadout total) |
| `+0x988` | float | Defense |
| `+0x98C` | float | Magick |
| `+0x990` | float | Magick Defense |
| `+0x994` | int32 | XP |
| `+0x998` | int32 | XP to next level |
| `+0xDD0` | uint16 | level |
| `+0x1616` | 322 B | `mStudyFlag` bestiary knowledge |
| `+0x1B90` | 10×12 B | inclination values (9 + skill-use); stride `0x0C` |

Inclination stride — `0x0C`; нельзя читать значения плотным `float[]`.

Account economy (абсолютные, от `pBase`):

| Offset | Type | Field |
|---:|---|---|
| `+0xA7A14` | int32 | Discipline Points (DP) |
| `+0xA7A18` | int32 | Gold |
| `+0xA7A1C` | int32 | Rift Crystals (RC) |

`InWorld()` / `IsInActiveGameplay()`: `level != 0` и `maxHp ∈ (0, 200000]`.
Титул / загрузка → false.

---

## 2. Live bodies и action/FSM

Main Pawn определяется динамически как `uCmc`, Arisen как `uPlayer`.
Hardcoded absolute vtable **не** production resolver.

TypeAtlas sizes (CONFIRMED):

| DTI | size | константа |
|---|---:|---|
| `uPlayer` | 23056 (`0x5A10`) | `kPartyBodySize` |
| `uCmc` | 22752 (`0x58E0`) | `kCmcBodySize` |
| `uEm0100` | 29632 (`0x73C0`) | `SpeciesCard` |
| `uEm0200` | 29888 | `SpeciesCard` |
| `uEm5900` Devilfire Drake | 31920 | TypeAtlas size; live DTI+gid §8.5 |
| `uEm8100` (каталожная строка «Drake») | 29280 | TypeAtlas; **не** Devilfire; подпись — guess генератора |
| `uEm8000` | 29296 | TypeAtlas; **классификация спорная** — §8.4 |
| `uEm8600` Hare | 29328 | harmless |

### 2.1 Arisen identity (Build 84.11, CONFIRMED)

Live `uPlayer` клеится к fixed record `*pBase+0xA7000` без обязательного
указателя на запись в наружном теле. Порядок доказательств:

1. pointer в теле / child graph;
2. обратный указатель в первых `0x7F0` байтах записи (не заходить в Main Pawn record);
3. полный `cPlayerInfo` (2032 B = `0x7F0`) содержит указатель на запись;
4. иначе ровно один живой `uPlayer` + валидный `CombatRecordCore` + читаемые XYZ ≠ 0.

Ноль или два+ `uPlayer` — **fail-closed**. HP записи (`+0x96C`) лежит за
пределами `0x7F0` и в `cPlayerInfo` не ищется.

Pawn record ↔ body (Build 007+): unique match
`cCmcInfo+0x29C` (HP mirror) == `record+0x96C`, два стабильных снимка.
Ambiguous / conflict → unresolved. Порядок скана / live-list **не** identity.

Hired: пустой record — не дыра партии (occupied-exact, не exact-4).

| Offset live body | Type | Field |
|---:|---|---|
| `+0x0C / +0x10` | ptr | live-list next/prev (`uEm*`) |
| `+0x2D` | byte | gid/type; **DTI name обязателен** (коллизия `0x61`) |
| `+0x40/+0x44/+0x48` | float | world XYZ |
| `+0x60/+0x64/+0x68` | float | body scale W/H/D (EnemyTuner) |
| `+0x2DC0` | ptr | `cActionManager::cActBank` |
| `+0x2DC8` | ptr | current `cPlAct*` / enemy Act |
| `+0x2DD4` | uint32 | packed current action code (player/main pawn) |
| `+0x2DE8` | ptr | duplicate current Act |
| `+0x2E64` | ptr | `cAICtrl` (проверять DTI-имя; слот плавает) |
| `+0x2EB8` | ptr | primary planner/combat target |
| `+0x3DEC` | ptr | `cCmcInfo*` (pawn; проверять имя) |

Current Act использует стабильный placement-new buffer: меняется
vtable/состояние, не обязательно адрес. Прямая подмена `+0x2DC8`
не является безопасным AI control.

Подтверждённые packed codes main pawn: Wait `0`, Walk `1`, Run `2`, Dash `5`, Jump `8`.

### 2.2 Масштаб мировых координат

`+0x40/+0x44/+0x48` — **не метры**. Единицы мира ≈ сантиметры (~100 единиц/метр).

- `AIPlActParam` дальности `500..4000` → 5–40 м;
- гоблин-сенсор `em0100A.sn2` зрение `1500` → ~15 м;
- live `pawn→Arisen ≈ 440` при следовании → ~4.4 м.

В коде: raw world-units / `worldUnitsPerMeter` (ini `[pawnAI]`, default 100.0).
Director/TacticalCues делят на `100.0f` напрямую.

---

## 3. Main Pawn priority pipeline

### 3.1 Fast decision chain (Build 48, CONFIRMED)

```text
uCmc + 0x2E64 -> cAICtrl          // имя класса обязательно
cAICtrl + 0x04 -> same uCmc
cAICtrl + 0x68 -> cAIGoalPlanning
cAICtrl + 0x70 -> cAIPriorityThink
cAIGoalPlanning + 0x04 -> same cAICtrl
cAIPriorityThink + 0x04 -> same cAICtrl
```

После разрешения live `uCmc` глобальный AI heap census не требуется.

### 3.2 Resource roots

```text
cAIPriorityThink + 0x08 -> rAIPriorityThink (cmc.prt)
cAIGoalPlanning + 0x08 -> default rAIGoalPlanning (Wait)
```

`rAIPriorityThink`: 85 `cPrioParam` rows. Runtime: 48 buckets
(`QUEST`, `PL_Party`, `Situ_Personal`, `Enemy`, `Wait_Follow`, `Etc` × 8).

Ресурс **общий на всех пешек** (Build 75.6): три `cAIPriorityThink`,
один `rAIPriorityThink`. Правка `AddS32` действует на всю партию.
Различие поведения — checks `{inclId, rank}`, не отдельный ресурс.

### 3.3 `cPrioParam` / `cCodeParam` / buckets

См. `FIELD_MAP.md` §4–5. Кратко:

- `cPrioParam`: Sensor/Code/Category/ObjectId/Extra + personality/order cArrays;
- `cCodeParam` (104 B): `AddS32 +0x04`, `AddF32 +0x08`, `BreakAfterApply +0x0C`, checks;
- bucket: `cAIPriorityThink + 0x38 + slot*0x14`; валидны только `[0, count)`.

`AddS32` = integer bucket displacement (code 45: base 36, −1 → 35, −2 → 34).

Check-объект: `+0x04` InclIdx 0..8, `+0x08` rank 2=primary / 1=secondary / 0=tertiary.

```text
0=Scather 1=Medicant 2=Mitigator 3=Challenger 4=Utilitarian
5=Guardian 6=Nexus 7=Pioneer 8=Acquisitor
```

Guardian code 54 rule[0] `AddS32=-3` (primary) — доказанный «поводок пассивности».
Code 57 `WpnBowAtk2` personality=0.

Sidecar: `DDDA_AI_Overhaul/ddda_pawn_ai_profiles.ini` schema v2.
Контракт: validate all → apply all → readback → convergence → rollback.

---

## 4. Planner / GOAP / target

```text
code = (slotOffset - 8) / 4
PlanCtrl(code) = planner + 0x190 + code * 0x110
planner + 0x17C = selected code; 0xFFFFFFFF = промежуток, НЕ код
```

Ёмкость: коды `0..90`. Не применять `PlanCtrl` к `0xFFFFFFFF`.

`uCmc+0x2EB8` — primary planning/combat target (Build 53: 335/747 rows,
9 unique bodies). Может жить в near-death при code `0xFFFFFFFF`.
`+0x4B28` — secondary/previous (OBSERVED). `+0x14E0` — look-at (OBSERVED).

Оружейные коды: `52 Sword`, `53 GSword`, `54 Dagger`, `55 Wand`,
`56 Shield`, `57 Bow`. Рывок: `84 DashFollow`, `85 DashFollowSt500` —
**строк приоритета нет**; живой рывок идёт под code `1 Follow`.

Поводок follow — **не** в priority-слое. Пороги в `cCmcFollow +0x118..+0x130`
(CONFIRMED read). Роль-зависимый поводок через intent, не запись float.

`AIPlActParam` range tuple в `cCmc* +0x258..+0x278` — eligibility, не hitbox.
WandRange errata: RangeMax кастера 15 м (`docs/WAND_RANGE.md`).

---

## 5. `cCmcInfo`

| Offset | Field |
|---:|---|
| `+0x29C` | current HP mirror (= record `+0x96C`) |
| `+0x2A4` | recoverable HP mirror |
| `+0x14B8 + id*0x0C` | `{state,id,float}` × 9 inclinations |
| `+0x0288/+0x028C/+0x0290` | `rHumanEdit/rBodyEdit/rFaceEdit` |

Current target pointer внутри `cCmcInfo` не подтверждён.

---

## 6. CombatBus (CONFIRMED контракт шины)

Два независимых канала. Hit **не** топчет presence.

```text
CombatIntel.Publish(CombatReport)     // ~150 мс, SRWLOCK exclusive + listeners
WorldScan.PublishWorld(WorldReport)   // presence; listeners НЕ зовутся
```

`CombatReport`: удары, distinct types, knowledge, `enemies[8]`.
`dominantCategory` **никогда** не берётся из `GetEnemyCategory(0x61)`.

`WorldReport` (32 `WorldPresence`):

| Поле | Смысл |
|---|---|
| `units[].ptr/vt/gid/kind/xyz` | кто и где |
| `units[].inCombatAction` | live Act ∈ боевому эвристическому списку |
| `units[].actName[40]` | **копия** DTI-имени Act, не указатель |
| `enemyCount / enemyCombatCount / deadCount / critterCount` | состав |
| `goblinCount` | prefix `uEm0100*` **или** `uEm0101*` |
| `dominantCategory` | max `KindCategory`; только small-goblin = 0, иначе −1 |
| `pawnEngaged` | `uCmc+0x2EB8 != 0` |
| `timestampMs` | свежесть; Director: stale > **450 мс** → пустой view |

Трупы (`ActDie`/`ActDeadBody` anywhere in name) в `units[]` не входят.
Смерть — FSM-имя, не HP и не `+0x14`.

Классификация **по DTI-имени**, не по gid:

```text
KindIsCreature = uEm* || uHumanEnemy
KindIsHarmless = uEm8000 || uEm8600     // см. конфликт §8.4
KindIsEnemy    = creature && !harmless
```

---

## 7. Enemy runtime + Tempo primitive

- `+0x2D` gid — коллизии; DTI обязателен.
- Смерть = `Die`/`Dead` в имени Act.
- Универсального current HP внутри `uEm*` **нет**.
- Ряд анимации `+0x0EE4…+0x0EF4` (5×float). Покой: хотя бы одно поле `== 1.0`.
  Пишем **мультипликативно** (торпор/захват живут). Атаки — `ActMap::NameIsAttack`.
- Допуск вида перед первой anim-записью: TypeAtlas size ≥ `0x0EF8`,
  все 5 ∈ (0.05, 8.0), есть ровно `1.0`. Иначе skip + лог.
- Локомоция: хук `movss xmm6,[esp+30]` + контекст трёх store `esi+40/44/48`
  (сигнатура не уникальна: ~317 hits). Sprint-хук опционален.
- Hard clamp: loco `0.75..1.30`, anim `0.70..1.40`.
- Композиция: `stable baseline → Director envelope → generic SetOverride → clamp`.

`uEm0200` roster head (CONFIRMED):

| Off | Field |
|---:|---|
| `+0x00` | party body ptr |
| `+0x08` | live flag `1` |
| `+0x0C` | mode `4` perception / `2` combat |
| `+0x10` | attention (pin 300 if fC=4, 500 if fC=2) |
| `+0x14` | weight 1.0 / 0.2 — **не писать** |

Dead `0/0` и transitional `fC=1` — fail-closed.

`uEm0100` roster (OBSERVED, тот же base/stride `+0x2FA0` / `0x28C` × 4):

| Head | Смысл |
|---|---|
| `f8&1==1`, `fC=4` | perception, потолок 300 |
| `f8&1==1`, `fC=5` | combat, потолок 484 (native max OBSERVED) |
| `0/0/0/0` | пустая оболочка; Director может wake → `1/4 att=300 w=1.0` |
| иначе | fail-closed |

Старшие биты `f8` у гоблина — константа карты, писать нельзя.
Block B fakehit: `+0x274` (младший бит), `+0x27C` value; `+0x278/+0x280` не трогать.

`cCharParamEnemy` (320 B) у гоблина: две копии `+0x5870` / `+0x59B0`.
Рантайм-раскладка ≠ файл (+8 в первой зоне). Сопротивления (файл+8):

```text
+0x54 res_poison
+0x58 res_TORPOR
+0x64 res_tarred
+0x68 res_drenched
+0x6C res_possession     // сосед Drenched, но это РЕЗИСТ, не live-бит
```

Это **пороги накопления**, не байт наложенного статуса. Писать их ≠ наложить Possession.

---

## 8. Monster Director + SpeciesCard (84.21)

### 8.1 Политика

Режиссёр **не пишет в память игры**. Только примитивы:
`Tempo::Admit/Release/HardReset*` и `Aggro::DirectorFocusSet`.

Тик 150 мс. PackMark (стратегия) — 500 мс, hysteresis 2500 мс,
switch margin 20%, BIAS isolation 20%, FOCUS 100%.
Счёт: `huntScore = highestAbsHP / currentAbsHP` по **on-field** членам
(`recordValid && bodyValid && body && currentHp > 0`). Запись в Разломе
жива, но слот **не** в охоте. Downed на земле — on-field (тело есть).
Occupancy (on-field vs rifted) входит в topology hash; указатель тела — нет.
maxHP, vocation, skills, status, downed-флаг в формуле **игнорируются**.

Тактика (`TacticalCues`) временно обгоняет PackMark, не стирая его.

| Cue | kind | response | lease | Aggro | Tempo 84.21 |
|---|---|---|---:|---|---|
| `PACK-GROUND-PIN-ALARM` (`Hagaijime4Feet`) | exact `uEm0200` | ALARM | 4000 | pin+suppress+fakehit | rage envelope |
| `PACK-LIFT-RESCUE` | exact `uEm0200` | ALARM | 2500 | то же | rage envelope |
| `PACK-GRAB-ALERT` (`GrabStart`) | exact `uEm0200` | ALERT | 750 | pin-only | rage envelope |
| `GOBLIN-GRAB-ALERT` (`GrabStart`\|`Hagaijime`) | exact `uEm0100` | ALERT | 4000 | pin + goblin-fakehit, **без suppress** | **std-rush** (`tempoRage`) |
| `HOB-GRAB-ALERT` (те же акты) | exact `uEm0101` | ALERT | 4000 | pin + goblin-family fakehit/wake на `2FA0/28C` (live 84.26: голова как у гоблина, не wolf `flag==1`) | std-rush |
| PackMark weakest-HP | `uEm0200` / `uEm0101` / `uEm0400` (не смешивать стаи) | FOCUS | — | ALARM | rage envelope |
| — | exact `uEm0400` Saurian | — | — | **без граба** | wolf-lite 1.20–1.22 / 1.20–1.23 |

Допуск пары: unique holder act + unique spatial pair ≤ 2.0 м (lift 2.5 м).
Exact restrained body исключается из responders.

Галки: `[monsterAI] enabled`, `[monsterAI] wolfActuator` — default **off**.

### 8.2 SpeciesCard — единственный словарь допуска write

Exact `strcmp` DTI. Prefix / subtype **не** наследуют профиль.

```text
uEm0200  size=29888  observe tempoRage aggroWrite
         rageLoco 1.20..1.25   rageAnim 1.20..1.26
uEm0100  size=29632  observe tempoRage aggroWrite
         rageLoco 1.21..1.24   rageAnim 1.32..1.40
uEm0101  size=29632  observe tempoRage aggroWrite
         rageLoco 1.21..1.23   rageAnim 1.24..1.32
         // hob: grab=goblin, PackMark=wolf; slower than small goblin
uEm0400  size=29568  observe tempoRage aggroWrite
         rageLoco 1.20..1.22   rageAnim 1.20..1.23
         // saurian: PackMark only, NO grab; live f8&1 + fC 4/2
```

`Director::Init` зовёт `Tempo::RegisterRageProfile` для каждой `tempoRage` строки.
Tempo не зависит от `monsterai`; волк имеет встроенный fallback для unit-тестов.

Admission reject, если `!(rageLoco > stableLoco && rageAnim > stableAnim)`
(`director-mobilization-baseline-outside-profile`) → Director hard-reset
частичной policy.

Roll детерминирован от адреса тела (murmur3). Повторный Admit refresh/maximize
одной envelope; endpoint не двигается. Decay 1400 мс. TTL приказа 600 мс
(fail-safe; нормальный release явный).

### 8.3 Два предиката «гоблин» (не смешивать)

| Предикат | Где | Правило |
|---|---|---|
| presence / TacticalSwitch category | `WorldScan::KindCategory`, `goblinCount` | prefix `uEm0100*` / `uEm0101*` |
| write / rage / cue | `SpeciesCard`, `CollectEligibleResponders`, `TacticalCues` | exact `"uEm0100"` |

`uEm0100_0` / `_3` — детали (typeId 0). До 84.25 они попадали в `g_act` и
на шину; 84.25 их не кладёт. Prefix-предикат остаётся для полных
`uEm0100`/`uEm0101`. Director rage/aggro — exact `\"uEm0100\"` / `\"uEm0101\"` / `\"uEm0200\"`.
84.28: hob pin идёт гоблинским семейством карт (live 84.26).

### 8.5 Devilfire «Дрейк» — live класс `uEm5900` (OBSERVED 84.25)

Наклейка «Evil Eyes» на `uEm5900` была ошибкой `BESTIARY_UEM_MAP`.
Снято 2026-08-26: `uEm5900` = Drake (live + Fluffy `em5900` + `em5900Dragon.prp`).

Живой спавн Devilfire Grove (три сессии, 84.24–84.25; DUMP `actors[]` +
три MD snapshot):

| Канал | Значение |
|---|---|
| DTI name | `uEm5900` |
| `+0x2D` gid | `0x5C` (types.tsv groupId 92) |
| TypeAtlas size | 31920 |
| Act хвата | `cEm5800Catch` / пешка `cEm5800CatchDamagePL` |
| Прочие live Act | `cEm5800HabatakiAttack`, `Bite`, `BackBreath`, `Run`, `Turn` |
| Ваниль | значок Possession + пешка бьёт Аризена из лука (тестер) |
| Архив | `em5900.arc` → `em5900_enemy_act_param.eap` (ниже) |

Оба независимых канала (имя DTI и байт gid) совпали. Это **не** «сканер
принял глаз за дракона» и не деталь. Каталожная строка `uEm8100` на этом
спавне **не появляется**. Циклопа (`uEm2000` / `0x39`) в списках не было:
тело ~120 м — `uEm5000` gid `0x3F` + `cEm5000ActCommon` (голем за кадром).

Гоблин/волк совпадают с таблицей, потому что у них один класс на спавн
и угаданный маппинг попал. У драконидов несколько словарей сразу.

`cEm5800*` — семейство актов дракона (breath / catch / tower / habataki).
Своих `cEm5900*` в атласе **нет**. `cEm8100*` — пять мелких актов
(`HoverCeilingMove` …), не хват Devilfire.

Писать Tempo/Director в это тело **нельзя** (нет `SpeciesCard`).
Охота Possession якорится на `uCmc` в окне значка / лука по Аризену
и на это тело как источник, не на имя `uEm8100`.

### 8.6 Приёмка вида — без туризма

Протокол: `docs/ENEMY_INTAKE.md`. Стол: `tools/enemy_intake.py`.
Разновидность семьи (скелет/рыцарь/лорд) **не** требует отдельного боя.
`SpeciesCard` / Tempo / Director не заполняются из лога. Допущены только
`uEm0100`, `uEm0101`, `uEm0200` и `uEm0400`.

#### 8.5.1 `em5900.arc` EAP — CATALOG, 2026-08-26 (OBSERVED)

Файл заказчика `em5900_enemy_act_param.eap` из `em5900.arc`:

```text
magic EAP_   ver 14
67 записей × 232 B  (старт 0x15C; тот же контейнер, что em0100 = 92×232)
строк нет: ни Catch, ни Drake, ни Eye, ни Possess, ни uEm/cEm
XFS нет — это таблица чисел (дальности / флаги / motion-id), не имена FSM
```

Что из этого следует:

- архив на диске **совпал** с live DTI (`em5900` ↔ `uEm5900`);
- EAP **не спорит** с `cEm5800Catch` — имён классов в нём и не бывает;
- FluffyQuack `em.txt` (`http://fluffyquack.com/DD/txt/em.txt`):
  `em5900 - Drake`, `em5800 - Dragon`, `em5500 - Evil eye`, `em8100 - ?`;
- полный remap `BestiaryData.h` **не делать** из одного совпадения:
  остальные bid→uEm в той же таблице тоже ручные. Identity Devilfire
  закрыта (live DTI+gid, имя архива, FluffyQuack, charparam ниже).

#### 8.5.2 Три словаря (не склеивать)

| Словарь | Ключ | Что отвечает | Источник |
|---|---|---|---|
| TypeAtlas / `types.tsv` | `uEmXXXX` | имя C++ класса, size, gid | дамп exe |
| Fluffy `resources/fluffy_em.txt` | `emXXXX.arc` | кто лежит **в файле на диске** | FluffyQuack |
| `BESTIARY_UEM_MAP` | encyclopedia bid | человеческая наклейка | **ручная догадка, не identity** |

Флаффи **не** подставлять в `BestiaryData.h` / `strcmp` / gid.
Harpy: архив `em0600`, DTI `uEm0700`. Cyclops: архив `em5000`, DTI `uEm2000`.
Гоблин/волк/Devilfire Drake (`em5900`↔`uEm5900`) совпали — исключение, не правило.
Какой `.arc` открывать — Флаффи. Кто живой в памяти — DTI.

#### 8.5.3 `em5900` charparam / motion (CATALOG 2026-08-26)

`em5900.rst`: HP **80000** / KD 20000.
`em5900_cmn.prp` (320 B `cCharParamEnemy`):

```text
ATK 1600  DEF 400  MATK 500  MDEF 230  weight 5000  fire 0.2  ice 1.5
耐敵化 (res_possession) = 10000   // резист САМОГО дрейка, не байт на пешке
```

`em5900Dragon.prp` (188 B, hash `0x347379CF`):

```text
CMC掴み右手倍率     5.0     хват пешки
CMC封印行うダメージ 5000    «печать» на CMC (кандидат inflict, не live-байт)
башня / сердце / breath / hover / press / backjump
カースドラゴン復活   1800 фреймов
```

`e5900_at.lmt`: 36/71 слотов, **129–130 костей**.
`e5800_pl.lmt`: 4 слота, 71 кость, 8.37 / 4.03 / 2.37 / 3.03 с (player-interact).
Имён событий LMT-дампер не читает. Слой C на `uCmc` по-прежнему UNVALIDATED.

### 8.4 `uEm8000` / gid `0x61` — ОТКРЫТЫЙ КОНФЛИКТ

- Бестиарий: `uEm8000` = The Dragon / Ur-Dragon, gid `0x61`.
- WorldScan + CombatIntel: class name `uEm8000` = **harmless critter**.
- Зайцы live носят gid `0x61`; настоящий заяц = `uEm8600`.

Пока живой A/B не разведёт «лагерный uEm8000» и Григори, **нельзя**:
маппить gid `0x61` → Dragon; писать Tempo/Director в `uEm8000`;
считать `KindIsHarmless("uEm8000")` доказанным для финала Nightmare.

---

## 9. Aggro write contract (CONFIRMED)

`DirectorFocusSet(slot, expectedBody, excluded, ALERT|ALARM, exactKind)`:

- `exactKind` ∈ {`uEm0200`,`uEm0100`,`uEm0101`,`uEm0400`} иначе reject;
- перед **каждой** записью карты: `ResolveMemberBody(slot) == expectedBody`;
- mismatch → release, no-write;
- occupied combat card на другом члене (`fC=2` волк / `fC=5` гоблин) — leave-engaged;
- validate head → WrSafe → readback → rollback.

Manual PIN остаётся research, `uEm0200` only, не включает product lease.

---

## 10. PartyCombatSnapshot (CONFIRMED read layer)

`Runtime::ReadPartyCombatSnapshot` — единственный вход Director в партию.
Поля записи CONFIRMED. `statusMask/statusValid` = 0/false до A/B Possession.
`downedValid/downedRevivable` — только из `PartyStatus` FSM (84.16+), не из имени Act.
`downedHint` — сырое имя, в скоринг не идёт.

`ReadPartyCombatSnapshot` возвращает true при `recordCount > 0`.
Записи save-layer живут на load screen и в Разломе.

Occupied-exact (84.23) считается **по on-field слотам**:

```text
on-field = recordValid && bodyValid && unique body
rifted   = recordValid && PartyRecordBodyClaimCount==0   // skip, как пустой Hired
empty    = !recordValid                                   // skip Hired
duplicate / Arisen without body                           // FAIL-CLOSED
```

Главная пешка в Разломе (таймер воскрешения / обрыв / камень не трогали)
**не** роняет identity всей партии. Аризен без тела — по-прежнему авария.

---

## 11. Контракт памяти и fail-closed

### 11.1 Что считается безопасным чтением

`Runtime::Mem::Rd` / `WrSafe`: SEH `__try/memcpy`, **без** `IsBadReadPtr`
(PAGE_GUARD side-effect, краш 19.08).
`RegionOk`: `VirtualQuery`, не commited / `PAGE_GUARD|NOACCESS` / не целиком
в одном регионе → false.
`LooksHeap`: aligned, `≥0x01000000`, `<0x80000000`, не image, не page-round `<0x10000000`.
`LooksLikeVtable`: vt в rdata, первые два слота в .text.
DTI: vtable slot `B8 imm32 C3/C2` → DTI card `+4` = ASCII name.
Кэш `vt → name` 4096 слотов (и отрицательный результат).

Продуктовый тик: каждый модуль в своём `__try` (`UpdatePawnAI`).
Naked Tempo-хуки **без** SEH: они только множат смещение, если `esi/edi`
совпал с таблицей. Мусорный body в таблице = запись в чужой объект, не AV.

### 11.2 Когда обязан вызываться `HardResetAllDirectorMobilization`

Код сейчас зовёт его при:

- `Director::Init` / `Shutdown` / `SetEnabled(false)` / `SetActuatorEnabled(false)`;
- `ReleasePolicy(..., hardReset=true)`: identity/topology/species/readiness
  failure, urgency NaN, empty responders, Aggro reject, stale-tactical unsafe;
- `Tempo::Shutdown`, `SetEnabled(false)`, `SetAnimEnabled(false)`,
  `SetAnimAttacksOnly(false)`;
- `Tempo::OnWorldUnload` / `MonsterAI::OnWorldUnload` (84.22, переход
  `inWorld→false`).

Ordinary `decision-none` / `decision-bias` → **decay**, не hard-reset.
Generic `SetOverride` (PawnHaste) hard-reset **не** чистит.

### 11.3 ДЫРЫ fail-closed (P0 — не соответствуют заявленному «мгновенно»)

**P0-1. World unload — CLOSED в 84.22.**

На переходе `inWorld→false` (тот же тик, не 450 мс, не `s_enabled`):

```text
g_nAct = 0; memset(g_act)
PublishWorld(empty, timestamp=0)
Tempo::OnWorldUnload()          // g_tempoCount=0, drop anim, no WrSafe
Aggro::DirectorFocusSet(-1)
MonsterAI::OnWorldUnload()      // оркестратор, всегда; policy HARD-RESET
PackObserve: ts=0 → PACK-GONE   // без debounce
```

`Tempo::OnWorldUnload` **не** зовёт `RefreshTable` и **не** пишет ряд
анимации обратно: тел нет. `WrSafe` в dangling — подозреваемый краш
загрузки. Лог: `WorldScan: FAIL-CLOSED world-unload`.

`InWorld()` — save-layer (записи). Тела могут умереть раньше; это P0-3,
не регресс P0-1. Beach-crash после зачистки лагеря барьер не закрывает.

**P0-2. Кривой указатель ≠ мгновенный HardReset.**

`Rd`/`WrSafe` глотают AV и возвращают false. `AdmitDirectorMobilization`
проверяет `body != 0` и вид, **не** `RegionOk(body)` и не DTI-имя тела.
Повторный Admit по stale ptr обновляет TTL envelope.
`AnimTick` при нечитаемом ряде `continue` — envelope остаётся.

Это fail-soft на чтении, не fail-closed на политике.

**P0-3. Identity records переживают мир.**

`ReadPartyCombatSnapshot` = true на одних записях. Без второго гейта
`ExactPartyIdentity` (тело + Aggro exact) PackMark мог бы жить на load
screen. Write-path закрыт. Advisory PackMark при `s_nView==0` сбрасывается.

### 11.4 DTI-скан: контракт и промахи размеров

`LooksLikeCreatureAt` (84.25): vtable + `+0x2D` readable + `+0x40` float +
`KindIsLiveEnemyBody` — `uHumanEnemy` | `uEm`+цифры | `uEmNNNN_DD`
(голова химеры). **Не** `uEmDragonBase`, `uEm*::*`, `uEm0100_3`,
`uEm5000_1`. Поллинг: `VirtualQuery`, skip `PAGE_GUARD`, `__try` по slice.

`DumpActorsFrom`: деталь не занимает слот `g_act[32]`, но `next/prev`
(`+0x0C/+0x10`) всё равно идут в walk (ёмкость 96). Иначе Дрейк,
достижимый только как сосед компоненты, теряется. `uPlayer`/`uCmc`/`uNpc`
в списке остаются.

`FindChildByClass(body, bodyBytes, name)`: шаг 4, `LooksHeap`, DTI exact.
`bodyBytes` обязан быть **TypeAtlas size этого класса**, не «с запасом».

Нарушения:

| Зов | bytes | реальный size | Риск |
|---|---:|---:|---|
| `ResolvePawnPlanner` / `kPawnBodyBytes` | `0x58E0` (84.22) | `uCmc 0x58E0` | было 304 B за хвостом; закрыто |
| `PawnInclinationsLive` | `0x58E0` | `uCmc 0x58E0` | OK |
| `PartyStatus` discovery | `0x5A40` | 23056 / 22752 | 48–352 B за хвостом |
| Aggro `kScanBytes=0x6800` | 26624 | волк 29888 / гоблин 29632 | OK (меньше тела) |

Промах за хвостом ловится SEH/`LooksHeap`, но может подцепить чужой
heap-объект как «ребёнка» (ложный `cAICtrl` / ложный `cStatus`).

`PartyAdoptBody`: `LooksLikeVtable` + `NameOfLiveObject == expected DTI`
перед записью в roster. Duplicate claim не прунится в unique (84.11).

---

## 12. Статусы партии и протокол Possession (Nightmare)

### 12.0 Два слоя (не путать)

```text
ЗАПИСЬ  pBase+0xA7000 / +0x7F0+i*0x1660
        лист персонажа: HP, стамина, STR/DEF, уровень, вокация.
        Слой C — рабочий массив статусов здесь (§12.1.2).

ТЕЛО    uPlayer / uCmc
        актёр на сцене: XYZ, Act, анимация, AI. Выполняет команды.
        Heap-детей cStatus нет. 84.33/84.34: cStatus INLINE @ +0x2698
        — путь apply (слой E), не лист записи.
```

`FindChildByClass` видит только указатель на объект с именем. Маска /
массив слотов на записи ему невидимы. 84.30: та же кнопка
`snapshot to log` дампит hex ЗАПИСИ (`PS: REC`) и ТЕЛА (`PS: BODY`).

### 12.1 Что подтверждено

- `PartyStatus` — read-only. `cStatus` / `cEffectStatusManager` как
  **heap-дети тела** не найдены (лог 24, Devilfire 84.25). 84.33:
  `cStatus` inline `uCmc+0x2698`. statusMask не маппится.
- Downed FSM (84.24/84.25): читается с тела, которое упало. Пешка
  `CmcNeardeath|CmcDead|DmgDownDead|DmgCrumbleDead` → `RAISED` (обычный
  акт) / `RIFTED` (`CmcReturn`). Нокдаун `DmgDown` ≠ succor.
  `cPlReviveCMC` — акт Аризена `RAISE`, на пешке игнор. Аризен не
  succor-жертва (`cPlActDead` ≠ `DOWNED`; DEAD снимает leftover
  `downedNow`). `downedValid` — свежий (<5 с) neardeath/knockdown.
  `downedRevivable` — только пешка после `RAISED`, и только пока снова
  neardeath.
- Vanilla: воскрешение **лечит** Possession (84.34: `cPlReviveCMC` →
  пешка нормальная). Сюжет Nightmare **не** вешает apply на этот акт.
  Latch на neardeath + BBI-вектор; Set — на стоячем теле после подъёма.
- Live Devilfire Drake = **`uEm5900` / gid `0x5C` / `em5900.arc`** (§8.5).
  Подпись «Drake» на `uEm8100` в `BestiaryData.h` — guess генератора.
  Possess-класса у `cEm8100*` нет; хват = `cEm5800Catch`.
- `cEmWightActPossesion` подчиняет Cursed Dragon (`uEm8300`), не `uCmc`.
- `ListResist`: `17 Drenched`, `19 Possession` — другой индекс, другой слой.
- `em_wet.esp` — shader UV, не статусный аккумулятор.

### 12.1.1 CATALOG `*.statusparam` — слот 7 = Possession (CONFIRMED identity)

`player.statusparam` и `enemy.statusparam`: 40 слотов, индекс = live status id.
Полная таблица: `docs/generated/STATUS_PARAM.md`.

```text
slot 6 Drenched    timer=90    cat=260    p0=0.5   p1=2.0    player==enemy
slot 7 Possession  timer=180   cat=6657   p0=0.2   p1=0.35   player==enemy
```

Почему это не догадка:

1. **180 с = 3 мин** — вики Possession.
2. Слот **пустой в читкоде баффов игрока** (`ListStatus[7]` был `"7: "`):
   Аризен ванилью не одержим; дебилитация пешек. Подпись в коде теперь
   `7: Possession (pawns)` — этикетка каталога, не live-доказательство.
3. Слоты 6 и 7 **соседние** в том же массиве — якорь Drenched для диффа
   тела остаётся валидным (H2: массив слотов, id=6 → id=7).
4. `mParam0=0.2` согласуется с «мили-урон сильно урезан»; семантика
   `mParam1=0.35` и `mCategory=0x1A01` — UNVALIDATED.

Это **id и параметры ресурса**, не смещение байта на `uCmc`/`uEm8100`.
Писать `statusparam` на диск / в charparam **нельзя** — нужен слой C.

### 12.1.2 LIVE запись персонажа — рабочий массив статусов (CONFIRMED 84.30)

Три снапшота 26.08.2026, `84.30-party-sheet`, лагерь. Контроль: Hired2 сухая
во всех трёх. Мокрые: MainPawn + Hired1. Arisen сухой (изменения в его
дампе `+0x121C…` = перекрытие `MainPawn+0x0A2C`, не свой статус).

```text
character record +0x0A29   u8     «сейчас в воде» (MainPawn 0→1→0; Hired1 не взводился)
character record +0x0A2C   i32    число занятых слотов (0 / 1)
character record +0x0A30   i32[40]  id статуса; пусто = -1 (0xFFFFFFFF)
character record +0x0AD0   f32[40]  таймер, кадры 30 Hz  (90 с ≈ 2700)
character record +0x0B70   f32[40]  param0
character record +0x0C10   f32[40]  param1
```

Четыре параллельных массива, шаг 4, длина 40 = каталог. Индекс массива —
свободный слот работы, **не** id статуса. Id лежит значением:

```text
сухо:  ids[0]=-1  timer=0     p0=0    p1=0    count=0
мокро: ids[0]=6   timer=2669  p0=0.5  p1=2.0  count=1   // 6=Drenched
сухо2: ids[0]=6   timer=659   p0=0.5  p1=2.0  count=1   // ещё тикает (~22 с)
```

Hired1 тот же слот, таймер 2281→270 (вошла в воду раньше). Hired2 — ни
байта. `rStatusParam` общий на партию, к живому слоту не относится.

2669/30 = 89.0 с. Каталог слота 6: timer=90, p0=0.5, p1=2.0. Совпало.

**Possession — тот же массив, CONFIRMED 26.08.2026 (Devilfire, 84.30):**

```text
snap1 grab     act=cEm5800CatchDamagePL   count=0  ids[0]=-1
snap2 possess  act=cPlActWpnBow           count=1  ids[0]=7  timer=5306.5  p0=0.2  p1=0.35
snap3 clear    act=cPlActRun              count=0  ids[0]=-1  timer=0      p0=0    p1=0
```

5306.5/30 = 176.9 с. Каталог слота 7: timer=180, p0=0.2, p1=0.35.
Аризен пуст (его `+0x1220` = перекрытие MainPawn `+0x0A30`).
Граб сам статус не ставит — слот пуст, пока нет значка.

Писать: validate → `count` / `ids[k]=7` / timer=5400 / p0=0.2 / p1=0.35 →
readback → WATCH 2.5 с → rollback. Модуль `src/pawnai/Possession.cpp`
пишет **только MainPawn**.

**84.31 live (лагерь, 26.08.2026):** `applied id=7 t=5400 count=1`, затем
`failed why=watch-engine-cleared` до 2.5 с. Значка / AI нет. Poke массива
≠ apply. Движок читает слот и стирает неполный набор.

**84.32 live (лагерь, вода, 26.08.2026):** 225× `BuffApply id=6`
`body=MainPawn uCmc` `ctx=body+0x2698` `t=90 p0=0.5 p1=2.0` (каталог, секунды).
Poke `id=7` в хук не заходил. Значок Мокрота живёт с таймером записи.
Хук @ `0x008343E3`.

**84.33 live:** `cStatus` inline @ `uCmc+0x2698` (CONFIRMED name).
`BuffEnter ecx=cStatus+0x7C stk4=cStatus stk8=6`. Рецепт 84.33 не собрался
(ждали id в edx). Apply не вызывался.

**84.34 live (лагерь, 26.08.2026, `84.34-status-call` 20:25:17) — CONFIRMED apply:**

```text
recipe this=cStatus+0x7C arg0=cStatus arg1=id fn=0x8342f0
vanilla-call id=7 status=0x10d08158 param=0x10d081d4
BuffApply id=7 body=MainPawn uCmc t=180 p0=0.2 p1=0.35   // каталог 1:1, секунды
FindId(rec,7) сразу = −1  count=1  why=vanilla-no-record // наш FSM, не ваниль
тестер: лук по Аризену = Devilfire; бой ~3 мин; HP 505→0 / Аризен 498→237
DOWNED cPlActDmgCrumbleDead → RIFTED cPlActCmcReturn +5578ms
RAISE cPlReviveCMC → пешка нормальная (ванильный cure)
```

Конвенция thiscall **закрыта**. Вода — лагерный замок рецепта, не вектор.
`vanilla-no-record`: чтение записи в том же кадре, что вызов; `count=1`
скорее остаток Drenched (181× id=6 за сессию). Снапшота записи в этом
логе нет — слой C в том же кадре не доказан; слой E + AI тестера — да.
Poke-путь не вызывался.

Revive **не** вешаем: ваниль этим лечит (этот лог).

**84.35 live (лагерь, тестер, без воды) — OBSERVED apply без рецепта:**

Layout thiscall, пешка атакует первой (= Devilfire). Первый заход сейва
может задержать AI до удара — не баг (мир/тело). Повтор и apply после
паузы (запрос висит до гейта) — сразу. Лога 84.35 в репо нет; поведение
совпало с 84.34. Poke не зовём. Чужой status id этим не доказан.

Замысел градуса скверны (sidecar, не сейв): `docs/NIGHTMARE.md`.

**84.37:** на нашем thiscall, если `[possession] customParams=on`, mid-хук
подменяет `xmm0/1/2` (timer/p0/p1) **только при esi=7 и s_inject**.
Дрейк / чужой apply не трогаем. Каталог на диске не пишем. Live ещё нет:
ждать `inject t=` в `BuffApply id=7` и SHEET p0/p1/таймер записи.

### 12.2 Слои, которые нельзя путать

```text
A. res_* в cCharParamEnemy     порог накопления (Drenched @+0x68, Possession @+0x6C)
B. rStatusParam / statusparam  ресурс длительности (секунды в BuffApply)
C. live work-array на ЗАПИСИ   SoT §12.1.2; иконка живёт с этим таймером
D. visual                      иконка, красные глаза, облако
E. cStatus inline uCmc+0x2698  apply path (84.34 thiscall CONFIRMED)
```

dinput8 `HBuffMods` (upstream Cheats.cpp) — **не inflict**. Mid-сайт
после загрузки каталога в xmm: `esi=id`, подмена `xmm0/1/2` =
timer/p0/p1 из `buffModsValues[id]`, гейт `[body+0x3DEC]+8` ≥ 0.
Наш хук 84.32 слушает тот же сайт. Писать xmm можно только когда
игра уже вошла в apply; сами мы входим thiscall-ом (84.34).

### 12.3 Протокол дифференциального поиска (UNVALIDATED)

Цель: байт/поле live Possession. Источник ванили на Devilfire — тело
`uEm5900` gid `0x5C` (§8.5), не каталожный `uEm8100`. Носитель статуса —
**пешка `uCmc`** в окне значка / лука по Аризену.

**Запрещено до закрытия протокола:** `WrSafe` в кандидата; копирование
волчьих/гоблинских оффсетов; обход всего процесса (FIX_RULES §5.1);
подмена `cPlAct*` / target (FIX_RULES §3).

#### Фаза 0 — приборы

1. Тег лога = текущий `MOD_BUILD_TAG` (`84.31-possession`).
2. Director actuator **off**. Tempo на Devilfire-тело не admitted — не писать.
3. `PartyStatus` + `snapshot to log` (84.30: hex ЗАПИСИ + ТЕЛА,
   строки `PS: SHEET` / `PS: REC` / `PS: BODY`). Census только по кнопке
   вне боя. Текущий снапшот больше не слеп к записи.
4. Якорь источника: DTI + gid живого тела (§8.5), live Act ≠ Die.

#### Фаза 1 — поймать Drenched на Дрейке

Контроль шума (как торпор-охота):

```text
A0  дрейк стоит, сухой, не в атаке          DUMP body+дети (DTI names)
A1  тот же покой, +2 с                      DUMP  (фильтр позы)
B   наложить Drenched (вода / заклинание)   DUMP сразу, Act в лог
C   Drenched спал                           DUMP
```

Искать поля, которые:

- стабильны A0=A1;
- меняются A→B;
- возвращаются B→C (или таймер монотонно падает);
- **не** похожи на XYZ/кватернион/аним-тайм (`+0x40..`, ряд `+0xEE4`);
- **не** `res_drenched` float в charparam (слой A).

Ожидаемые формы live Drenched (проверять в этом порядке):

| Гипотеза | Сигнатура B | Следствие для Possession |
|---|---|---|
| H1 битовая маска | один байт/dword, бит N взводится | Possession = соседний бит той же маски |
| H2 массив слотов | stride S, слот id=6 активен (таймер>0) | слот Possession = id±1 в том же массиве |
| H3 `cStatWork` 32 B | новый объект + ptr в родителе | соседний слот массива работ |
| H4 `StatusEffect` 20 B | фабрика внутри `uCharacterBase` | тот же контейнер, другой id |
| H5 только визуал | в теле тишина, меняется shader/efl | live-бит на пешке, не на Дрейке |

`ListStatus[6]=Drenched` + CATALOG §12.1.1 задают **H2 как первый ход**:
слот 6 якорь, слот 7 — Possession. H1 (бит в общей маске) проверять
рядом: если это bitfield, бит 7 соседствует с битом 6.

Окно соседей после поимки якоря `offD`:

```text
байты:   offD-16 .. offD+16
dwords:  offD-32 .. offD+32
если найден массив: слоты [id-2 .. id+2], id(Drenched)=6
```

Не расширять окно, пока якорь не стабилен на двух независимых наложениях.

#### Фаза 2 — наложить Possession, не теряя якорь Drenched

Предпочтительно **то же тело Дрейка не нужно** для ванильного Possession
(он *даёт* статус пешке). Два допустимых пути:

**P-pawn (основной для сюжета):** A/B на `uCmc` Main Pawn.

```text
P0  пешка здорова                         DUMP uCmc + PartyStatus snapshot
P1  Дрейк держит пешку (Grab)             DUMP; Act пешки + Дрейка
P2  красные глаза / облако, бой vs Arisen DUMP
P3  Panacea / Sobering / unconscious      DUMP
```

Дифф P0→P2, обратный P2→P3. Пересечение с окном соседей Drenched
**по смещению относительно известного якоря**, не по абсолютному VA.

Если Drenched-якорь найден на Дрейке в фазе 1 — **перенести смещение**
на `uCmc` только после проверки, что тот же паттерн (маска/stride)
читается на пешке в покое (все нули) и в P2 (бит/слот взведён).
Тела разного размера (`uCmc 22752` vs `uEm8100 29280`): общий префикс
`uCharacterBase` (12176 B) — единственная зона, куда законно переносить
смещение без нового A/B.

**P-forced (только если P-pawn недоступен):** искать на Дрейке поле,
которое взводится на время grab-possess (не статус пешки, а «я наложил»).
Это другой контракт; не называть его Possession byte.

#### Фаза 3 — канонизация

В `FIELD_MAP` + этот файл попадает только если:

1. два независимых A/B;
2. поле живёт ровно в окне эффекта;
3. DTI-имя контейнера записано;
4. readback после *ванильного* наложения совпал;
5. WATCH 2–3 с: движок не затирает мгновенно.

После этого — `statusMask` bit, учёт в `ScoreParty`
(possessed pawn **исключается** из целей; ваниль: враги её игнорируют).
Модуль `src/pawnai/Possession.{h,cpp}` (84.31) пишет слой C на MainPawn.
write-WATCH (шаг 5) — живой тест этого зипа. F12 apply/clear + авто-clear
на unload. Запись: validate → write → readback → WATCH → rollback.

#### Фаза 4 — сюжетный триггер Nightmare (после байта)

**Не** вешать Possession на `cPlReviveCMC`. Ваниль этим лечит; игрок оставит
пешку лежать. Принудительный подъём = подмена `cPlAct*` / HP+FSM — запрещён
(FIX_RULES §3).

```text
в бою + вектор + вход в Neardeath/Dead     → sidecar latch (игра не пишется)
любой подъём (revive / inn / зона)         → latch жив, ещё не Set
первый бой, пешка на ногах                 → Possession::Set (слой C)
верность / нет вектора / !InWorld          → Set отказывает
unload / actuator off                      → Clear
```

`PartyStatus` уже видит `DOWNED` / `REVIVE` / `RECOVERED`. Apply — на
стоячем теле в бою, не на лежачем (иначе ванильный revive сотрёт статус).

`src/Nightmare.cpp` сейчас: UI + сырой write погоды, тик выключен.
Не строить заражение на weather poke.

### 12.4 Почему Дрейк, а не пешка, как первый якорь Drenched

Вода на Дрейке воспроизводима без Greatwall/Daimon. Один вид, один layout.
Пешка нужна, чтобы **подтвердить перенос** смещения в `uCharacterBase`.
Охота «сразу на пешке под Possession» смешивает grab-FSM, downed и статус —
шум, который уже сжёг торпор-дифф (`ActDmgCollapse`).

---

## 13. Что не является подтверждённым

- универсальный enemy current HP;
- семантика `p0/p1` Possession (каталог 0.2/0.35 CONFIRMED на apply;
  «мили режется / лук нет» — вики, не A/B);
- синхрон слой C в том же кадре, что thiscall (84.34: FindId сразу −1);
- семантика всех priority codes; physical hitbox из `AIPlActParam`;
- generalized GOAP writes;
- monster priority/planner bridge (Director — **не** planner);
- безопасный emId-swap / LOT hook для Nightmare;
- hour-offset вечной ночи;
- флаг квеста Daimon-2;
- тождество live `uEm8000` ≡ Григори;
- перенос rage-профиля на любой вид без строки `SpeciesCard`.

---

## 14. Канонические артефакты

- этот файл — единственный продуктовый контракт;
- `FIELD_MAP.md` — компактные таблицы (сверять с §0 при расхождении);
- `src/BuildTag.h` — номер сборки;
- `src/CombatBus.h` — шина;
- `src/monsterai/SpeciesCard.h` — допуск вида + rage ranges;
- `src/monsterai/MonsterDirector.cpp` / `TacticalCues.cpp` — политика;
- `src/runtime/MonsterTempo.cpp` — примитив;
- `src/runtime/AggroWatch.cpp` — карты / pin / fakehit;
- `src/runtime/MemProbe.cpp` — SEH/DTI;
- `src/runtime/PartyRecon.cpp` — identity + snapshot;
- `src/CharParamEnemy.Generated.h` — резисты (не live status);
- `PLAYER_PAWN_WORK/generated/*` — pawn vertical slice;
- `generated/TYPE_ATLAS.md` / `ActMap.Generated.h`;
- `generated/STATUS_PARAM.md` — 40 слотов `*.statusparam`.

Дневники охоты и старые контракты — `docs/archive/` (не удалять).
`POSSESSION_RECON.md` ещё живой (протокол), канон слота — §12.

---

## 15. Nightmare Mode — архитектура на сейчас

```text
замысел: вектор BBI-лут → sidecar latch → приступы id=7 градусом
         + ночь + кузены (не на revive)
факт:    apply id=7 CONFIRMED (84.34 лог, 84.35 без воды)
         Hooks::Nightmare() — UI, тик выключен
         weather 0xB8780 — сырой указатель, не заражение
         latch файла нет; кнопка F12 = отладка
```

Компас скверны: `docs/NIGHTMARE.md`. Канон:

- состояние мода — **sidecar**, не `DDDA.sav`;
- ванильный статус загрузку сейва не переживает — Set после load из latch;
- thiscall передаёт **id**, не p0/p1; каталог при вызове не патчим;
- id≠7 — UNVALIDATED (отдельный A/B);
- **не** вешать Set на `cPlReviveCMC`;
- вектор обязателен; верность = иммунитет; только Main Pawn;
- stage двигает **наш** Clear (длина приступа), не CORE STR/DEF.

Порядок:

1. ~~P0-1~~ 84.22; ~~слой C + apply~~ 84.30–84.35;
2. sidecar latch + детектор BBI-вектора (ещё нет);
3. Set на стоячем теле после подъёма, Clear по stage;
4. night/weather — `WrSafe` + `InWorld` + readback, не `GetBasePtr`;
5. spawn-id CATALOG; нет кузена → vanilla + лог;
6. PACK только если GPL отказывает типу.

Не `.arc` swap. Не ярость Tempo на «заражённых». Заражение = статус +
ванильный AI «бьёт Аризена».
