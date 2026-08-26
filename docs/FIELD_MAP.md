# Field Map

Компактная карта подтверждённых runtime-полей. Источники и ограничения — в [`SOURCE_OF_TRUTH.md`](SOURCE_OF_TRUTH.md). История HUNT/TEST хранится в Git, а не в этом файле.

Статусы:

- ✅ подтверждено чтением/изменением и используется;
- 🔎 подтверждено чтением, semantic name уточняется;
- 🧪 исследовательское/видоспецифичное.

## 1. Process anchors

| Anchor | Contract | Status |
|---|---|---|
| `pBase` | signature-resolved gameplay/save base | ✅ |
| `pWorld` | signature-resolved world pointer | ✅ |
| `*pBase + 0xA7000` | Arisen character record | ✅ |
| `Arisen record + 0x7F0` | Main Pawn character record | ✅ |
| `cPlayerInfo` | 2032 B (`0x7F0`); child of live `uPlayer`; scanned whole for Arisen record ptr | ✅ |
| `*pBase + 0xA7A14` | Discipline Points (DP) | ✅ |
| `*pBase + 0xA7A18` | Gold | ✅ |
| `*pBase + 0xA7A1C` | Rift Crystals (RC) | ✅ |

## 2. Character record (Arisen/Main Pawn)

Vocation enum (1-based, confirmed via user CE table 2026-08-16):

```text
1 = Fighter         2 = Strider        3 = Mage
4 = Mystic Knight   5 = Assassin       6 = Magick Archer
7 = Warrior         8 = Ranger         9 = Sorcerer
0 / ≥10 = unknown
```

| Offset | Type | Field | Status |
|---:|---|---|---|
| `+0x6E0` | int32 | vocation (1-based enum above) | ✅ |
| `+0x868` | 6×int32 | equipped skills (main weapon 3 + secondary weapon 3) | ✅ |
| `+0x8D0` | 6×int32 | augments | ✅ |
| `+0x96C` | float | current HP | ✅ |
| `+0x970` | float | max HP | ✅ |
| `+0x974` | float | recoverable/secondary HP | 🔎 |
| `+0x978` | float | current stamina | ✅ |
| `+0x97C` | float | max stamina | ✅ |
| `+0x980` | float | recoverable/secondary stamina | 🔎 |
| `+0x984` | float | Strength | ✅ |
| `+0x988` | float | Defense | ✅ |
| `+0x98C` | float | Magick | ✅ |
| `+0x990` | float | Magick Defense | ✅ |
| `+0x994` | int32 | XP | ✅ |
| `+0x998` | int32 | XP to next level | ✅ |
| `+0xDD0` | uint16 | level | ✅ |
| `+0x1616` | 322 B | bestiary `mStudyFlag` | ✅ |
| `+0x1B90 + id*0x0C` | float in 12-B row | inclinations/skill-use values | ✅ |

Reference test save: Arisen HP `331/498`, stamina `600`; Main Pawn HP `327/505`, stamina `595`.

## 3. Live unit body (`uPlayer/uCmc/uEm*` observations)

| Offset | Type | Field | Scope | Status |
|---:|---|---|---|---|
| `+0x0C` | ptr | live-list next | observed `uEm*` | ✅ |
| `+0x10` | ptr | live-list prev | observed `uEm*` | ✅ |
| `+0x2D` | byte | gid/type byte | DTI name still required | ✅ |
| `+0x40/+0x44/+0x48` | float | world XYZ (НЕ метры — ~сантиметры, ~100/m) | live units | ✅ |
| `+0x60/+0x64/+0x68` | float | body scale W/H/D | EnemyTuner-observed | ✅ |
| `+0x2DC0` | ptr | Act bank | player/pawn/enemy observations | ✅ |
| `+0x2DC8` | ptr | current Act object | player/pawn/enemy observations | ✅ |
| `+0x2DD4` | uint32 | packed action code | player/main pawn | ✅ |
| `+0x2DE8` | ptr | duplicate current Act | player/main pawn | 🔎 |
| `+0x2E64` | ptr | `cAICtrl` | player/pawn/enemy observations | ✅ |

Do not treat offsets `+0x4AE8/+0x32D8/+0x1C94/+0x4B14` as sprint flags; hypothesis rejected.

## 4. Main Pawn priority resource

### `rAIPriorityThink::cPrioParam` (64 B)

| Offset | Type | Field |
|---:|---|---|
| `+0x00` | ptr | vtable |
| `+0x04` | uint32 | Sensor |
| `+0x08` | uint32 | Code |
| `+0x0C` | uint32 | Category |
| `+0x10` | uint32 | Object ID |
| `+0x14` | uint32 | Extra |
| `+0x18` | ptr | personality cArray vtable |
| `+0x1C` | uint32 | personality count |
| `+0x20` | uint32 | personality capacity |
| `+0x24` | uint32 | personality flags |
| `+0x28` | ptr | `cCodeParam**` |
| `+0x2C` | ptr | order cArray vtable |
| `+0x30` | uint32 | order count |
| `+0x34` | uint32 | order capacity |
| `+0x38` | uint32 | order flags |
| `+0x3C` | ptr | `cOrderValue**` |

### `rAIPriorityThink::cCodeParam` (104 B)

| Offset | Type | Field |
|---:|---|---|
| `+0x04` | int32 | AddS32 |
| `+0x08` | float | AddF32 |
| `+0x0C` | uint32 | BreakAfterApply |
| `+0x10` | ptr | checks cArray vtable |
| `+0x14` | uint32 | check count |
| `+0x18` | uint32 | check capacity |
| `+0x1C` | uint32 | check flags |
| `+0x20` | ptr | check pointer array |

### `rAIPriorityThink::cOrderValue` (12 B)

| Offset | Type | Field |
|---:|---|---|
| `+0x04` | uint32 | Value |
| `+0x08` | uint32 | Type |

## 5. `cAIPriorityThink` (1020 B)

| Offset | Field | Status |
|---:|---|---|
| `+0x08` | `rAIPriorityThink*` | ✅ |
| `+0x38 + slot*0x14` | 48 cArray bucket descriptors | ✅ |
| descriptor `+0x04` | count | ✅ |
| descriptor `+0x08` | capacity | ✅ |
| descriptor `+0x0C` | flags | ✅ |
| descriptor `+0x10` | `cPrioParam**` | ✅ |

Only entries `[0,count)` are valid. Allocator spacing/trailing storage is not a score node.

## 6. Main Pawn fast decision chain

| Object/offset | Field | Status |
|---|---|---|
| `uCmc + 0x2E64` | `cAICtrl*` | ✅ |
| `cAICtrl + 0x04` | owner `uCmc*` | ✅ |
| `cAICtrl + 0x68` | `cAIGoalPlanning*` | ✅ |
| `cAICtrl + 0x70` | `cAIPriorityThink*` | ✅ |
| `cAIGoalPlanning + 0x04` | owner `cAICtrl*` | ✅ |
| `cAIPriorityThink + 0x04` | owner `cAICtrl*` | ✅ |
| `uCmc + 0x2EB8` | primary planner/combat target; may persist while planner code is `0xFFFFFFFF` | ✅ 335 Build 53 rows, 9 target bodies; retained through near-death/revival |
| `uCmc + 0x4B28` | secondary/previous/lock target | 🔎 may equal current or retain prior Wolf |
| `uCmc + 0x14E0` | late/context-specific look-at/lock reference | 🔎 appears in later bow snapshot |

## 7. Planner

| Object/offset | Field | Status |
|---|---|---|
| `cAIGoalPlanning + 0x17C` | current priority code / `0xFFFFFFFF` none | ✅ |
| `planner + 0x190 + code*0x110` | indexed `cPlanCtrl` | ✅ for valid code |

Build 52 traversed all 91 slots: 56 have direct GOAP links and 42/70 codes used by `cmc.prt` have exact resource identities. Build 53 then selected planner-only codes `74 = EscapeNotice2` and `76 = GotoOm` during live play, so runtime semantics cover the full `0..90` planner range, not only `cmc.prt` rows. Canonical maps: `PLAYER_PAWN_WORK/generated/pawn_planner_semantics.{json,csv}` and `pawn_priority_semantics.{json,csv}`.

## 8. `cCmcInfo` and action interface

| Offset | Type | Field | Status |
|---:|---|---|---|
| `+0x29C` | float | current HP mirror | ✅ |
| `+0x2A4` | float | recoverable/secondary HP mirror | 🔎 |
| `+0x14B8 + id*0x0C` | row | inclination `{state,id,value}` | ✅ |
| `+0x0288` | ptr | `rHumanEdit` | ✅ |
| `+0x028C` | ptr | `rBodyEdit` | ✅ |
| `+0x0290` | ptr | `rFaceEdit` | ✅ |
| `+0x07EC` | ptr | `cPlLoadManager` | ✅ |
| `+0x1658` | ptr | `cPwnMsgLoadManager` | ✅ |

`cCmc*` compiled action tuple:

| Offset | Field |
|---:|---|
| `+0x258..+0x26C` | six range floats |
| `+0x270` | ElementAttr |
| `+0x274` | AtkAttr |
| `+0x278` | UseAttr |

## 9. Enemy-specific verified fields

### Goblin `uEm0100`

| Contract | Value | Status |
|---|---:|---|
| body size | `0x73C0` | ✅ |
| charparam signature location | observed near `+0x5870` and `+0x59B0` | 🧪 species-specific |
| charparam size | `0x140` | ✅ |
| charparam return activate | `+0x100` inside block | ✅ |
| charparam return duration | `+0x104` inside block | ✅ |
| charparam scale | `+0x12C` inside block | ✅ |

Offsets of `cCharParamEnemy` inside body must be signature-resolved for other species.

### Wolf `uEm0200` roster card head

| Offset | Type | Field | Status |
|---:|---|---|---|
| `+0x00` | ptr | party body | ✅ |
| `+0x08` | int | live flag (`1`) | ✅ |
| `+0x0C` | int | mode `4` perception / `2` combat | ✅ |
| `+0x10` | float | attention (pin 300 if `fC=4`, 500 if `fC=2`) | ✅ |
| `+0x14` | float | weight 1.0 / 0.2 — do not write | ✅ read-only |

## 10. Status

CATALOG: `param/status/{player,enemy}.statusparam` — 40 slots, index = status id.
Слот **7** = Possession (`mTimer=180`). Слот **6** = Drenched (`mTimer=90`).
Таблица: `generated/STATUS_PARAM.md`.

LIVE на **записи персонажа** (CONFIRMED 84.30, SoT §12.1.2):

| Offset | Type | Field | Status |
|---:|---|---|---|
| `+0x0A29` | u8 | currently in water (0/1) | 🔎 |
| `+0x0A2C` | int32 | occupied work-slot count | ✅ |
| `+0x0A30` | int32[40] | status id; empty = −1 | ✅ |
| `+0x0AD0` | float[40] | remaining frames @ 30 Hz | ✅ |
| `+0x0B70` | float[40] | param0 | ✅ |
| `+0x0C10` | float[40] | param1 | ✅ |

Индекс массива ≠ id. Drenched `ids[k]=6` / Possession `=7`.

Apply (CONFIRMED 84.34/84.35): thiscall `ecx=cStatus+0x7C`, `[esp+4]=cStatus*`,
`[esp+8]=id`. Без воды с 84.35. Каталог id=7 `t=180 p0=0.2 p1=0.35`.
Poke ≠ apply. id≠7 не канон. Revive лечит. Скверна: `NIGHTMARE.md`.

## 11. Open fields

- current HP inside generic enemy body;
- семантика p0/p1 Possession (значения каталога CONFIRMED, смысл нет);
- синхрон слой C в том же кадре, что thiscall;
- live statusMask на теле (массив — на ЗАПИСИ; apply — inline cStatus);
- semantic names/GOAP links for all priority codes;
- proven runtime fields for GOAP patches;
- physical damage hitboxes distinct from AI action ranges.
