# Коды приоритета пешки: слот ресурса цели = код

Сгенерировано `tools/goal_codes_from_log.py` из лога кнопки
`Dump planner GOALS`. Руками не править.

```
code = (slotOffset - 8) / 4
```

## Самопроверка

* целей загружено: 69, старший код: 90
* массив PlanCtrl: (25264 - 0x190) / 0x110 = 91 слотов, коды 0..90
* OK: старший код совпал с ёмкостью массива PlanCtrl
* OK: код 1 = Follow (наблюдали живьём, Build 40)
* OK: код 15 = Air (kGuardianModifiers, CONFIRMED)
* OK: код 54 = WpnDaggerAtk (главный рычаг Guardian, CONFIRMED)
* OK: код 60 = Em0600Cover (kGuardianModifiers, CONFIRMED)
* пустых слотов в диапазоне 0..90: 22 -> 4, 16, 17, 18, 27, 29, 30, 33, 39, 40, 41, 45, 46, 47, 48, 64, 65, 66, 68, 71, 75, 83

## Таблица

| код | слот | цель | пометка |
|----:|------|------|---------|
| 0 | `+0x008` | `Wait` |  |
| 1 | `+0x00C` | `Follow` | подтверждён независимо: наблюдали живьём, Build 40 |
| 2 | `+0x010` | `Jump` |  |
| 3 | `+0x014` | `Climb` |  |
| 5 | `+0x01C` | `PrecautionSub` |  |
| 6 | `+0x020` | `HoldAction` |  |
| 7 | `+0x024` | `Provoke` |  |
| 8 | `+0x028` | `Enchant` |  |
| 9 | `+0x02C` | `Receive` |  |
| 10 | `+0x030` | `Cling` |  |
| 11 | `+0x034` | `Cover` |  |
| 12 | `+0x038` | `HelpAction` |  |
| 13 | `+0x03C` | `Recovery` |  |
| 14 | `+0x040` | `Launch` |  |
| 15 | `+0x044` | `Air` | подтверждён независимо: kGuardianModifiers, CONFIRMED |
| 19 | `+0x054` | `Recover` |  |
| 20 | `+0x058` | `Goto` |  |
| 21 | `+0x05C` | `LadderClimb` |  |
| 22 | `+0x060` | `LadderDismount` |  |
| 23 | `+0x064` | `ClimbAttack` |  |
| 24 | `+0x068` | `ClimbEnd` |  |
| 25 | `+0x06C` | `HoldWait` |  |
| 26 | `+0x070` | `HoldWait4Feet` |  |
| 28 | `+0x078` | `StandOff` |  |
| 31 | `+0x084` | `UseItem` |  |
| 32 | `+0x088` | `Precaution` |  |
| 34 | `+0x090` | `LiftCorpse` |  |
| 35 | `+0x094` | `VictoryPose` |  |
| 36 | `+0x098` | `ItemThrow` |  |
| 37 | `+0x09C` | `BattlelAssist` |  |
| 38 | `+0x0A0` | `PlCons` |  |
| 42 | `+0x0B0` | `WaitInfinity` |  |
| 43 | `+0x0B4` | `OmBreak` |  |
| 44 | `+0x0B8` | `ClimbMove` |  |
| 49 | `+0x0CC` | `WpnOff` |  |
| 50 | `+0x0D0` | `ItemGet` |  |
| 51 | `+0x0D4` | `ItemFind` |  |
| 52 | `+0x0D8` | `WpnSwordAtk` |  |
| 53 | `+0x0DC` | `WpnGSwordAtk` |  |
| 54 | `+0x0E0` | `WpnDaggerAtk` | подтверждён независимо: главный рычаг Guardian, CONFIRMED |
| 55 | `+0x0E4` | `WpnWandAtk` |  |
| 56 | `+0x0E8` | `WpnShieldAtk` |  |
| 57 | `+0x0EC` | `WpnBowAtk2` | из дополнения (`tu2\AI\Goap\Cmc\WpnBowAtk2`) |
| 58 | `+0x0F0` | `DmgUkemi` |  |
| 59 | `+0x0F4` | `DmgLeverGacha` |  |
| 60 | `+0x0F8` | `Em0600Cover` | подтверждён независимо: kGuardianModifiers, CONFIRMED |
| 61 | `+0x0FC` | `Em0700HandlingOff` |  |
| 62 | `+0x100` | `Em5200JustGuard` |  |
| 63 | `+0x104` | `Em5300TaruBaku` |  |
| 67 | `+0x114` | `StatusUp` |  |
| 69 | `+0x11C` | `OpenDoor` |  |
| 70 | `+0x120` | `CarryGoods` |  |
| 72 | `+0x128` | `TreasureBox` |  |
| 73 | `+0x12C` | `EscapeNotice1` |  |
| 74 | `+0x130` | `EscapeNotice2` |  |
| 76 | `+0x138` | `GotoOm` |  |
| 77 | `+0x13C` | `BattleActionEtc` |  |
| 78 | `+0x140` | `BarrelBomb` |  |
| 79 | `+0x144` | `HoldAttack` |  |
| 80 | `+0x148` | `Ballista` |  |
| 81 | `+0x14C` | `DmgCancel` |  |
| 82 | `+0x150` | `DmgAbsorb` |  |
| 84 | `+0x158` | `DashFollow` | **рывок** |
| 85 | `+0x15C` | `DashFollowSt500` | **рывок** |
| 86 | `+0x160` | `JumpSt500` |  |
| 87 | `+0x164` | `TwoPlatoons` |  |
| 88 | `+0x168` | `MultiPlier` |  |
| 89 | `+0x16C` | `PlEscape` |  |
| 90 | `+0x170` | `DmgEscape` |  |
