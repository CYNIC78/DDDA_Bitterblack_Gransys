# Ночной observe-прибор гоблинов (`84.10`) + grab hold (`84.15`)

`PackObserve` только читает. Director 84.15 пишет Aggro ALERT pin-only на
exact `uEm0100` при `GrabStart`/`cPlActHagaijime` (lease 4000 ms) и будит
пустые карты `0/0` на `+0x2FA0`. Tempo rage / FOCUS-WINDOW / suppress+fakehit
гоблину не даются. Новых галок F12 нет. Волчий pack path не копируется.

## Что смотрит прибор

Exact full-body `uEm0100` (размер 29632). Префикс `uEm0100_*` и соседи
`uEm0101` / `uEm0102` в стаю не входят: один `SKIP` / один `MIXED`.

Две композиции одной встречи, не два вида:

| pack | правило |
|---|---|
| `rabble` | n ≤ 4 и не было Horn / ChargeCommand / IgnoreLeader |
| `led` | n ≥ 5 **или** любой из трёх сигналов; липнет до `PACK-GONE` |

Лидер не назначается по scale `+0x60/64/68`. Кандидат — последнее тело,
которое ещё видно после `ChargeCommand` (иначе после `Horn*`).
`IgnoreLeader` доказывает, что король есть, но сам короной не является.
Один раз за встречу, если один scaleH ≥ 1.12, а остальные < 1.08 —
`SCALE-HINT` (не `leader=`).

Смерть короля: тело кандидата исчезло + ≥2 `EscapeStart`/`Scared` за 2 с
→ `LEADER-FALL`. Прибор **не** выдаёт flee и не пишет в тела.

## Как тестировать ночью на берегу Кассардиса

1. Собрать Release\|Win32, сверить первую строку лога:
   `MOD_BUILD_TAG 84.15-goblin-grab-hold`.
2. Для observe достаточно Director off. Для grab hold включить прежние
   `enable monster director` и `enable Director actuator (WRITES)`.
   Observe тикает после `WorldScan` даже при выключенном pawn AI master.
3. Не трогать HP игрока, не ослаблять гоблинов, не бафать партию,
   не менять Guardian из-за длины боя.
4. День, трио 3–4 без рога — отрицательный контроль: `composition=rabble`,
   `leader=none`.
5. Ночь, большая береговая стая — положительный: `composition=led`.
   Пешка хватает гоблина (`GrabStart` → `cPlActHagaijime`). В логе:
   `situation ENGAGED name=GOBLIN-GRAB-ALERT response=ALERT`,
   `policy ENGAGED ... tempoOwned=0`, `Aggro: DIRECTOR ALERT ... pin-only`,
   `Aggro: DIRECTOR goblin-card-wake ... 0/0 -> 1/4 att=300 w=1.0` и
   `writes` > 0 (не только lease). Волков искать не нужно.
6. Оценить **только лог** и строку статуса. Кнопка
   `snapshot to log` дописывает `PackObserve dump`.

## Строки лога

`ADMIT` `JOIN` `LEAVE` `PACK` `HORN` `CHARGE` `IGNORE-LEADER`
`LEADER-CAND` `LEADER-LOST` `FLEE` `LEADER-FALL` `PACK-GONE`
`HEARTBEAT` (15 с) `SCALE-HINT` `SKIP` `MIXED`

С 85.39 (смерть и компоненты — только лог):

`DEATH` — тело исчезло из кадра, живя вблизи (< 60 м) и меньше 0.7 с назад,
либо было видно действие смерти. Строка называет вид, последнее действие,
размер, дистанцию и `nearComp` — ближайшую деталь вида. Это то, чем убийство
привязывается к телу: `DEATH @0x… kind=uEm0100 lastAct=cEmActDmgShrink
conf=likely hint=shrink mates=2 nearComp=uEm0100_3 compDist=0.3m`.
`LEAVE` остался для уходов из вида (`why=out-of-view`).

`COMPONENT-GONE` — вид-компонент пропал из кадра, с последним ближайшим телом.
`SKIP` теперь несёт `nearest=` и `dist=` (к какому телу привязана деталь).
`ROLE` теперь печатает и `flee`, и `insub` — побег виден без выбранного лидера.
`PackObserve dump` — в конце каждой встречи: состав пачки, лидер, счётчики
(`horn` `charge` `ignore` `fall` `shield` `caller` `flee`) и по каждому телу
роль, действие, размер, дистанция. В сборках до 85.39 вызывался только вручную.

Статус (одна `TextDisabled` строка, без галки):

```
PackObserve: idle (exact uEm0100, read-only)
PackObserve: 7 uEm0100 led leader=horn shield=1 caller=1 flee=0
```

## Что сознательно не сделано

- Tempo L1/A1 / FOCUS-WINDOW / suppress+fakehit на `uEm0100`;
- копирование волчьих чисел;
- превращение дневных троек в организованную стаю;
- новые checkbox / hunt UI;
- чтение `cGroupParam` / `cSetInfoEnemy0100`.
