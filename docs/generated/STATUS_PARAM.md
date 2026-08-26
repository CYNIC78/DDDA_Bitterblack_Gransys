# CATALOG: `param/status/*.statusparam`

Сгенерировано разбором XFS (`tools/xfs_tree_dump.py`), 2026-08-25.
40 слотов, индекс = `ListStatus` / live status id.

`mTimer` — длительность в секундах. Семантика `mCategory` / `mParam*`
не канонизирована (флаги применения + сила эффекта).

**Слот 7 — главный кандидат Possession (pawn debilitation):**

```text
id=7  timer=180.0  param0=0.2  param1=0.35  category=6657 (0x1A01)
player == enemy  (байт-в-байт по полям)
```

180 с = 3 мин — совпадает с вики Possession. В `Hooks::ListStatus`
слот пустой: читкод баффа игрока его не предлагает (Аризен не одержим).

Слот 6 Drenched: timer 90 с, category 260, идентичен player/enemy.

| id | ListStatus | timer P | timer E | p0 P/E | notes |
|---:|---|---:|---:|---|---|
| 0 | Poison | 150 | 180 | 20 / 5 | DIFF player/enemy |
| 1 | — | 30 | 30 | 0.5 | |
| 2 | Blindness | 60 | 30 | 0 | DIFF |
| 3 | Silence | 60 | 60 | 0 | |
| 4 | — | 15 | 30 | 1.5 | DIFF; кандидат Torpor? |
| 5 | — | 90 | 90 | 2.0 | |
| 6 | Drenched | 90 | 90 | 0.5 | якорь охоты |
| **7** | **— / Possession?** | **180** | **180** | **0.2** | **кандидат; p1=0.35** |
| 8 | — | 90 | 90 | 0 | |
| 9 | — | 90 | 90 | 0.6 | |
| 10 | Caught Fire | 15 | 15 | 12 / 33 | DIFF |
| 11 | — | 15 | 15 | 2.0 | |
| 12 | Petrifaction | 10000 | 10000 | 40 / 10 | practically permanent |
| 13 | — | 10000 | 10000 | 180 | |
| 14–17 | stat lowered | 90 | 90 | 0.7/1.3 | overwrite |
| 18–21 | stat boosted | 60 | 60 | 1.2/0.8 | overwrite |
| 22–26 | enchants | 1 / 60 | | weapon vs enemy DIFF |
| 33 | Stamina Boosted | 300 | 300 | 0.5 | |
| 34 | Impervious | 90 | 90 | 0 | |
| 35 | Weal | 300 | 30 | 2.0 | DIFF |
| 36 | Prosperity | 300 | 30 | 2.0 | DIFF |

Это CATALOG. Live-массив на **записи** — SoT §12.1.2 (CONFIRMED 84.30).
Possession: `ids[k]=7`, timer≈5306 (177 с), p0=0.2, p1=0.35, count=1; после снятия — пусто.
