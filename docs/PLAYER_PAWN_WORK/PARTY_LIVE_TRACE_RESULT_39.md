# Party live trace — результат Build 39

**Дата:** 2026-08-15  
**Сборка:** `39-party-live-trace-hotkeys`  
**Файл:** `ddda_party_live_001.csv`  
**Длина:** 63.328 с, 395 валидных замеров

## Итог

Один контролируемый проход подтвердил общий FSM игрока и главной пешки, сырой action-code `body + 0x2DD4`, а также динамические HP/stamina в персонажных записях `pBase`.

## 1. Пойманные действия

### Воскресший (`uPlayer`)

В порядке теста:

- `cPlActWait`;
- `cPlActRun`;
- `cPlActWalk`;
- `cPlActJumpBegin`;
- `cPlActJump`;
- `cPlActLand`;
- `cPlActDashBegin`;
- `cPlActDash`;
- `cPlActRunEnd`;
- `cPlActDmgNormalB`.

Урон совпал по времени с переходом в `cPlActDmgNormalB` и падением current HP.

### Главная пешка (`uCmc`)

Помимо `Wait/Walk/Run/Dash`, автономно зарегистрированы:

- `cPlActWpnBow`;
- `cPlActWpnDaggerAtckLandL`;
- `cPlActWpnDaggerCstmHyakuretsu`;
- `cPlActLiftBeginSmallItem`;
- `cPlActLiftRun`;
- `cPlActLiftWalk`;
- `cPlActLiftGeneric`;
- `cPlActHoldWeaponMv`.

То есть один и тот же `body + 0x2DC8` действительно сообщает не только locomotion, но и прыжки, получение урона, оружейные действия и поднятие предметов.

## 2. `body + 0x2DD4` — подтверждённый action-code

Поле менялось синхронно с DTI-именем текущего Act у обоих тел.

| `cPlAct*` | `+0x2DD4` |
|---|---:|
| `Wait` | `0` |
| `Walk` | `1` |
| `Run` | `2` |
| `RunEnd` | `3` |
| `Dash` | `5` |
| `JumpBegin` | `6` |
| `Jump` | `8` |
| `Land` | `14` |
| `DmgNormalB` | `19` |
| `DashBegin` | `125` (`0x7D`) |
| `LiftGeneric` | `97` / `99` |
| `LiftWalk` | `101` |
| `LiftRun` | `102` |
| `LiftBeginSmallItem` | `219` |
| `WpnBow` | `0x02060000` |
| `WpnDaggerAtckLandL` | `0x01050003` |
| `WpnDaggerCstmHyakuretsu` | `0x0105002A` |

Это не простой enum только для бега: оружейные значения упакованы, а некоторые DTI-классы имеют варианты кода. Надёжная семантика по-прежнему берётся из DTI current Act; `+0x2DD4` годится как дешёвый код текущего action/варианта.

## 3. Отвергнутые «sprint flags»

Поля `+0x4AE8`, `+0x32D8`, `+0x1C94` и `+0x4B14` менялись внутри одинаковых действий и принимали значения во время Wait/Run/боевых состояний, несовместимые с гипотезой «это флаг спринта».

Их нельзя использовать как sprint boolean. Спринт определяется однозначно через `cPlActDash` или action-code `5` в `+0x2DD4`.

## 4. Динамические HP и stamina в `pBase`

Одинаковый layout подтверждён для:

```text
player record    = *pBase + 0xA7000
main pawn record = player record + 0x7F0
```

| Record offset | Тип | Значение | Доказательство |
|---:|---|---|---|
| `+0x96C` | float | current HP | изменилось ровно при получении урона |
| `+0x970` | float | max HP | стабильно 498 / 505 |
| `+0x974` | float | второй HP/recoverable-health параметр | изменился только при уроне; точная семантика ещё не названа |
| `+0x978` | float | current stamina | плавный расход на Dash и восстановление; затраты умений пешки |

### Воскресший

- current HP: `331.2682 -> 211.9500` в 47.422 с;
- max HP: `498.0`;
- current stamina: `600.0 -> 499.4998` во время Dash;
- восстановление до `600.0` к 42.063 с.

### Главная пешка

- current HP: `327.8011 -> 238.4970` в 47.578 с;
- max HP: `505.0`;
- current stamina: полный уровень `595.0`;
- расход до `495.0` при оружейном действии и до `476.2` при `cPlActWpnDaggerCstmHyakuretsu`;
- восстановление обратно до `595.0`.

Интерфейс игры округляет внутренние float HP до показанных пользователю целых 331/327.

Отдельное постоянное поле max stamina этим окном не доказано. Значения 600/595 подтверждены как полные уровни динамического `+0x978`.

## 5. Объекты `cPlStamina`

DTI census нашёл 24 объекта `cPlStamina` с общей live vtable `0x015FA500`. Их первые 32 байта не изменились, хотя stamina обоих персонажей расходилась и восстанавливалась.

Следовательно, эти первые 32 байта не являются текущим числовым запасом stamina. Для текущего значения использовать подтверждённый record offset `+0x978`.
