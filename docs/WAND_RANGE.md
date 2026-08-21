# Wand AI range — эррата кастера (Build 75.56)

Лёгкая заплатка: пешка-маг / чародей получает **луковую дальность eligibility** (15 м). Игрока не трогаем.

Референс — DDON, где чародеям расширили радиус каста. Здесь синглплеер: у Восставшего есть руки, глаза и мозг; у пешки нет спринта в точку каста и нет брони. Синие первые под раздачу — пусть стоят в арьергарде.

## Что это не есть

- Не длина снаряда / `.shl`. Каст с 15 м в молоко = снаряд короче круга AI.
- Не StandOff и не Goto.
- Не игрок.
- Не мили (кинжал, меч, Stinger) и не лук (у него уже 5–15 м).
- Не `cCmcIceWalk` (Frigor-аура: идти в стаю — скилл).

Только поле **«могу ли начать это действие»** на живом `cCmc* + 0x258` (шестёрка RangeMinXZ / RangeMaxXZ / … / EnableMaxXZ; 100 ≈ 1 м).

## Ваниль → патч

| Действие | live cCmc | ваниль | 75.56 |
|---|---|---|---|
| Magick Bolt / комбо | `cCmcMagicUserCombo` | 5–10 м | **15 м** |
| Levin / туча | `cCmcLightningCloud` | 5–10 м | **15 м** |
| Anodyne / сигил | `cCmcHealing` | коротко | **15 м** |
| Halidom / круги | `cCmcCure`, `*Circle` | коротко | **15 м** |
| прочие касты (FireBall, IceMissle, WandDX*, Enchant*, …) | max 1–10 м | как в файле | **15 м** |
| Frigor-аура | `cCmcIceWalk` | 0–5 м | **0–5 м** |
| лук | `cCmcBowAttack` / `BowRensa` | 5–15 м | как было |
| мили | Slash / Dagger / … | 0–1.5 м | как было |

Живой бой 75.56: MagickUserCombo и LightningCloud APPLIED многократно; IceWalk только в `seen`. Субъективно: задняя линия, меньше панических прыжков, отход джогом на пару шагов и речардж.

## Как включить

F12 → Pawn AI → Guardian Doctrine → **Caster AI range -> 15 m (pawns)**

```ini
[errata]
wandRange = on
```

По умолчанию **off** (ваниль). Откат: галка OFF / меню / выгрузка DLL.

## Резолв (как Guardian / Nexus)

Не census тела.

```text
каждая uCmc (своя и наёмные)
  -> cAICtrl (имя класса, не голый оффсет)
     -> cAIActionInterfaceCtrl
        -> текущий cCmc* +0x258
     -> PlanCtrl code 55 (WpnWandAtk)
```

Пишем только если имя `cCmc*` кастерское и RangeMax в 1–10 м. Validate → write → readback → rollback.

Код: `src/pawnai/WandRange.cpp`. Тест-шпаргалка: `docs/TEST_WAND_RANGE.md`.
