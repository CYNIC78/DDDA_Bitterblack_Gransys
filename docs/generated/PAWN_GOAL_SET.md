# Набор целей главной пешки (живой дамп 19.08.2026)

Снято кнопкой `Dump planner GOALS`. Планировщик `cAIGoalPlanning`
(`тело +0x2E64 cAICtrl +0x0068`) держит массив ресурсов
`rAIGoalPlanning` — это загруженные файлы `.gop`. Путь ресурса лежит
по смещению `+0x08` внутри самого ресурса.

Всего загружено **69** целей.

| # | Цель | Смещение в планировщике |
|---:|---|---|
| 1 | `Wait` | `+0x8` |
| 2 | `Follow` | `+0xc` |
| 3 | `Jump` | `+0x10` |
| 4 | `Climb` | `+0x14` |
| 5 | `PrecautionSub` | `+0x1c` |
| 6 | `HoldAction` | `+0x20` |
| 7 | `Provoke` | `+0x24` |
| 8 | `Enchant` | `+0x28` |
| 9 | `Receive` | `+0x2c` |
| 10 | `Cling` | `+0x30` |
| 11 | `Cover` | `+0x34` |
| 12 | `HelpAction` | `+0x38` |
| 13 | `Recovery` | `+0x3c` |
| 14 | `Launch` | `+0x40` |
| 15 | `Air` | `+0x44` |
| 16 | `Recover` | `+0x54` |
| 17 | `Goto` | `+0x58` |
| 18 | `LadderClimb` | `+0x5c` |
| 19 | `LadderDismount` | `+0x60` |
| 20 | `ClimbAttack` | `+0x64` |
| 21 | `ClimbEnd` | `+0x68` |
| 22 | `HoldWait` | `+0x6c` |
| 23 | `HoldWait4Feet` | `+0x70` |
| 24 | `StandOff` | `+0x78` |
| 25 | `UseItem` | `+0x84` |
| 26 | `Precaution` | `+0x88` |
| 27 | `LiftCorpse` | `+0x90` |
| 28 | `VictoryPose` | `+0x94` |
| 29 | `ItemThrow` | `+0x98` |
| 30 | `BattlelAssist` | `+0x9c` |
| 31 | `PlCons` | `+0xa0` |
| 32 | `WaitInfinity` | `+0xb0` |
| 33 | `OmBreak` | `+0xb4` |
| 34 | `ClimbMove` | `+0xb8` |
| 35 | `WpnOff` | `+0xcc` |
| 36 | `ItemGet` | `+0xd0` |
| 37 | `ItemFind` | `+0xd4` |
| 38 | `WpnSwordAtk` | `+0xd8` |
| 39 | `WpnGSwordAtk` | `+0xdc` |
| 40 | `WpnDaggerAtk` | `+0xe0` |
| 41 | `WpnWandAtk` | `+0xe4` |
| 42 | `WpnShieldAtk` | `+0xe8` |
| 43 | `WpnBowAtk2` | `+0xec` |
| 44 | `DmgUkemi` | `+0xf0` |
| 45 | `DmgLeverGacha` | `+0xf4` |
| 46 | `Em0600Cover` | `+0xf8` |
| 47 | `Em0700HandlingOff` | `+0xfc` |
| 48 | `Em5200JustGuard` | `+0x100` |
| 49 | `Em5300TaruBaku` | `+0x104` |
| 50 | `StatusUp` | `+0x114` |
| 51 | `OpenDoor` | `+0x11c` |
| 52 | `CarryGoods` | `+0x120` |
| 53 | `TreasureBox` | `+0x128` |
| 54 | `EscapeNotice1` | `+0x12c` |
| 55 | `EscapeNotice2` | `+0x130` |
| 56 | `GotoOm` | `+0x138` |
| 57 | `BattleActionEtc` | `+0x13c` |
| 58 | `BarrelBomb` | `+0x140` |
| 59 | `HoldAttack` | `+0x144` |
| 60 | `Ballista` | `+0x148` |
| 61 | `DmgCancel` | `+0x14c` |
| 62 | `DmgAbsorb` | `+0x150` |
| 63 | `DashFollow` **<- рывок** | `+0x158` |
| 64 | `DashFollowSt500` **<- рывок** | `+0x15c` |
| 65 | `JumpSt500` | `+0x160` |
| 66 | `TwoPlatoons` | `+0x164` |
| 67 | `MultiPlier` | `+0x168` |
| 68 | `PlEscape` | `+0x16c` |
| 69 | `DmgEscape` | `+0x170` |

## Что из этого следует

- **`DashFollow` и `DashFollowSt500` загружены.** Действие рывка есть в
  наборе пешки; вставлять нечего, вопрос только в условии выбора.
- **Боевые цели**: `Goto`, `StandOff`, `Precaution`, `PrecautionSub`,
  `BattleActionEtc`, `BattlelAssist`, `HoldAttack` и пять `Wpn*Atk`.
- **`WpnBowAtk2` пришла из дополнения** — единственная цель с путём
  `tu2\...`. Значит набор пополнялся патчем, то есть он расширяем.
- **Видоспецифичные цели** под конкретных монстров: `Em0600Cover`,
  `Em0700HandlingOff`, `Em5200JustGuard`, `Em5300TaruBaku`.
- Приоритетное мышление: `cAIPriorityThink +0x08 -> AI\PrioThink\cmc`
  (`cmc.prt`) — именно оно решает, какая цель когда берётся.

## Пропуски в массиве

Смещения идут не подряд: `+0x18`, `+0x48`…`+0x50`, `+0x74`, `+0x7c`…`+0x80`
и другие пусты. Скорее всего это слоты под цели, которых у пешки нет
(вокационные или сюжетные), либо выравнивание групп. Пустой слот —
кандидат на место для новой цели, если до этого дойдёт.
