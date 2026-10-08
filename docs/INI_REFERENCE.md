# INI_REFERENCE — все ключи ини (генерируется)

> Автогенерация: `python3 tools/ini_reference.py --md docs/INI_REFERENCE.md`.
> Руками не править: правка теряется при следующей генерации. Описания берутся
> из комментариев самих ини — то есть из единственного места, где они гарантированно
> рядом с ключом. Шаг гейта `10f/10` следит, чтобы ни один ключ не пропал
> из справочника (значит, и из комментариев).

**Живой** = перечитывается на ходу (сторож времени файла). Остальное читается
при загрузке DLL. Правило целиком — `docs/INI_CHEATSHEET.md`.

## Карта файлов

| Файл | Что это | Ключей | Секций |
|---|---|---:|---:|
| `ddda_entities.ini` | боевые статы, восприятие, размеры (LIVE, 500 мс) | 22 | 11 |
| `ddda_ai_overhaul.ini` | главный конфиг модуля | 219 | 24 |
| `ddda_ai_overhaul.default.ini` | тот же главный конфиг в поставке (в зипе) | 219 | 24 |
| `ddda_pawn_ai_profiles.ini` | профили приоритетов пешек (прибор, не применяется автоматически) | 41 | 7 |
| | **всего** | **501** | |

---

## `ddda_entities.ini`

боевые статы, восприятие, размеры (LIVE, 500 мс). Ключей: 22.

### `[global]` — живой (500 мс)

| Ключ | Значение в файле | Что делает (из комментария рядом) |
|---|---|---|
| `schema` | `1` | schema — версия формата. Не менять. Нужна, чтобы будущие версии мода понимали старые файлы и не ломали чужие настройки. |
| `enabled` | `on` | enabled — рубильник всего слоя мутаций. off = чистая ваниль. |
| `allowWrites` | `on` | allowWrites — РАЗРЕШИТЬ ЗАПИСЬ В ПАМЯТЬ ИГРЫ. off = мод только читает и показывает значения, ничего не меняя. on = мутации применяются к живым… |

### `[default]` — живой (500 мс)

| Ключ | Значение в файле | Что делает (из комментария рядом) |
|---|---|---|
| `sightRadius` | `0` | --- ЗРЕНИЕ И СЛУХ --------------------------------------------------------- ВНИМАНИЕ (86.00): ЭТИ ТРИ КЛЮЧА ЧИТАЮТСЯ, НО ПОКА НИЧЕМ НЕ ПРИМЕНЯЮТСЯ.… |
| `sightAngle` | `0` | — |
| `hearRadius` | `0` | — |
| `scaleMin` | `1.0` | --- РАЗМЕР ОСОБИ --------------------------------------------------------- Множитель масштаба. 1.0 = ваниль. ВАЖНО: это КОЭФФИЦИЕНТ, а не итоговый… |
| `scaleMax` | `1.0` | — |
| `scaleJitter` | `0.0` | scaleJitter — НЕУНИФОРМНОСТЬ. Движок держит ширину/высоту/глубину раздельно, поэтому особи могут отличаться телосложением, а не только ростом. 0 =… |
| `leashScale` | `1.0` | --- ПОВОДОК И ВОЗВРАТ ДОМОЙ ---------------------------------------------- leashScale — РАБОТАЕТ. Множитель двух таймеров возврата в cCharParamEnemy:… |
| `returnSpeed` | `1.30` | — |
| `returnArmor` | `on` | — |
| `returnArmorMult` | `4.0` | — |
| `attackMult` | `1.12` | --- БОЕВЫЕ СТАТЫ (audit 2026-09-21 §8) ----------------------------------- Почему легко как sorcerer без апгрейдов: мод менял только… |
| `defenseMult` | `1.0` | — |
| `magickAttackMult` | `1.12` | — |
| `magickDefenseMult` | `1.0` | — |
| `adrenalineAtk` | `1.0` | --- АДРЕНАЛИН: ВСПЛЕСК СИЛЫ АТАКИ ПО ПРИКАЗУ (85.34) ---------------------- ЗАЧЕМ. Статичный урон предсказуем: игрок выучивает, сколько ударов он… |
| `adrenalineMagick` | `1.0` | — |
| `enabled` | `on` | enabled — выключить мутации для уровня/вида, оставив остальных. |

### `[class.small]` — живой (500 мс)

_(ключей нет — секция-заглушка)_

### `[class.large]` — живой (500 мс)

_(ключей нет — секция-заглушка)_

### `[class.boss]` — живой (500 мс)

| Ключ | Значение в файле | Что делает (из комментария рядом) |
|---|---|---|
| `attackMult` | `1.0` | Боссы: у них скриптовые фазы. Сила удара НЕ поднята — фазы скриптовые, а разница «босс стал внезапно бить на 12% сильнее» проверяется только полем.… |
| `magickAttackMult` | `1.0` | — |

### `[class.other]` — живой (500 мс)

_(ключей нет — секция-заглушка)_

### `[em0100]` — живой (500 мс)

_(ключей нет — секция-заглушка)_

### `[human]` — живой (500 мс)

_(ключей нет — секция-заглушка)_

### `[em0101]` — живой (500 мс)

_(ключей нет — секция-заглушка)_

### `[em0102]` — живой (500 мс)

_(ключей нет — секция-заглушка)_

### `[em0103]` — живой (500 мс)

_(ключей нет — секция-заглушка)_

---

## `ddda_ai_overhaul.ini`

главный конфиг модуля. Ключей: 219.

### `[main]` — на загрузке

| Ключ | Значение в файле | Что делает (из комментария рядом) |
|---|---|---|
| `loadLibrary` | `` | Load additional library - for chaining with other dinput8 mods - set to the original dinput8 mod's dll name if you want both |

### `[hotkeys]` — на загрузке

| Ключ | Значение в файле | Что делает (из комментария рядом) |
|---|---|---|
| `enabled` | `on` | Hotkeys — REQUIRED for F12 overlay to work! - 'enabled' - must be ON - 'keyUI' - F12 key code (0x7B = F12) - 'menuPause' - delay in ms for menu… |
| `keyUI` | `0x7B` | — |
| `menuPause` | `500` | — |

### `[pawnAI]` — на загрузке

| Ключ | Значение в файле | Что делает (из комментария рядом) |
|---|---|---|
| `enabled` | `on` | Pawn AI Overhaul — модель весов v3.0 - 'enabled' - master switch - 'presetsEnabled' - use anchors (sliders). OFF = weights freeze where they are -… |
| `presetsEnabled` | `on` | — |
| `acquisitor` | `on` | — |
| `acquisitorCombatFloor` | `100.0` | — |
| `acquisitorLootBoost` | `650.0` | — |
| `acquisitorBoostWindowMs` | `20000` | — |
| `acquisitorReturnMs` | `4000` | — |
| `worldUnitsPerMeter` | `100.0` | - 'worldUnitsPerMeter' - scale of world coordinates (Guardian doctrine). DDDA world units are ~centimeters, so 100.0 = 1 m. Evidence: AIPlActParam… |
| `guardianFix` | `off` | - 'guardianFix' - Build 57.1: dynamic Guardian fix. OFF by default (vanilla). When ON, lifts Guardian -3 penalty on WpnDaggerAtk (code 54)… |
| `guardianMeleeRadius` | `6.0` | — |
| `guardianPreemptRadius` | `10.0` | — |
| `guardianDaggerBiasMelee` | `2` | — |
| `guardianDaggerBiasPreempt` | `0` | — |
| `guardianMinRank` | `1` | - 'guardianMinRank' - 85.12: минимальный РАНГ склонности Guardian, при котором пешка считается гвардианом и получает доктрину. 2 = первичная, 1 =… |
| `guardianMinIncl` | `350.0` | — |
| `guardianTelemetryMs` | `0` | — |
| `guardianProbeLog` | `on` | guardianProbeLog: event-only WAKE/INTERCEPT START+RESULT, no periodic spam. off = no probe logs; doctrine unchanged. |
| `nexusProbeLog` | `on` | nexusProbeLog: Nexus START/RESULT once per window (~2 lines / 10.5 s), without per-tick sampling; off = no probe, doctrine remains active. |
| `smartUtil` | `on` | — |
| `tactical` | `on` | — |
| `lastPreset` | `5` | — |
| `smooth` | `0.10` | — |
| `vocationCordon` | `on` | --- Вокационный кордон Guardian -------------------------------------- Guardian как доктрина телохранителя осмысленна только у ближнего боя. У… |
| `cordonGuardianCapRanged` | `300` | — |
| `cordonPioneerFloorRanged` | `650` | — |
| `cordonGuardianCapHybrid` | `550` | — |
| `cordonPioneerFloorHybrid` | `550` | — |

### `[customAnchor]` — на загрузке

| Ключ | Значение в файле | Что делает (из комментария рядом) |
|---|---|---|
| `scather` | `750.0` | Inclination anchors (0 - 1000) — THE current target. Always live in F12. Loading a preset copies its values here. Dragging a slider writes here. |
| `medicant` | `400.0` | — |
| `mitigator` | `500.0` | — |
| `challenger` | `700.0` | — |
| `utilitarian` | `750.0` | — |
| `guardian` | `350.0` | — |
| `nexus` | `350.0` | — |
| `pioneer` | `400.0` | — |
| `acquisitor` | `250.0` | — |
| `skillUse` | `700.0` | — |

### `[targetLock]` — на загрузке

| Ключ | Значение в файле | Что делает (из комментария рядом) |
|---|---|---|
| `autoAim` | `off` | Target Lock — автонаведение мили-атак по камере - 'autoAim' - snap melee attacks to camera direction - Alt+X to capture player physics (once per… |

### `[camera_keys]` — на загрузке

| Ключ | Значение в файле | Что делает (из комментария рядом) |
|---|---|---|
| `freeCam` | `0x04` | Camera Plus + Pawn Cam hotkeys - 'freeCam' - toggle tactical camera (default MMB) - 'pause' - pause toggle (default Num 0) - 'pawnCam' - toggle Pawn… |
| `pause` | `0x60` | — |
| `speedUp` | `0x6B` | — |
| `speedDn` | `0x6D` | — |
| `pawnCam` | `0x61` | — |

### `[camera]` — на загрузке

| Ключ | Значение в файле | Что делает (из комментария рядом) |
|---|---|---|
| `freeCam` | `off` | Camera Plus — свободная камера + пауза - 'freeCam' - enable free camera (F4 toggle) - 'detach' - detach camera from player - 'freeFly' - enable free… |
| `detach` | `off` | — |
| `freeFly` | `off` | — |
| `flySpeed` | `2.0` | — |
| `flySpeedZ` | `2.0` | — |
| `pause` | `off` | — |
| `pauseSpeed` | `0.0001` | — |
| `pawnCam` | `off` | Party Cam — камера между Аризеном и пешкой (позиция blend, взгляд игрока) - 'pawnCam' - enable (toggle via camera_keys.pawnCam, default Num 1) -… |
| `pawnCamBias` | `1.0` | — |
| `pawnCamHeight` | `150.0` | — |
| `pawnCamFollow` | `0.01` | — |
| `pawnCamBiasEase` | `0.20` | — |

### `[combatIntel]` — на загрузке

| Ключ | Значение в файле | Что делает (из комментария рядом) |
|---|---|---|
| `enabled` | `on` | Combat Intel — боевая разведка - 'enabled' - hook ALL damage events, track enemies in combat - 'timeout' - seconds before enemy considered "out of… |
| `timeout` | `5` | — |

### `[enemyAI]` — на загрузке

| Ключ | Значение в файле | Что делает (из комментария рядом) |
|---|---|---|
| `enabled` | `on` | Enemy AI Overhaul - 'enabled' - master switch - 'aggression' - aggression multiplier (1.0 = default, >1 = more aggressive) - 'reactionSpeed' -… |
| `aggression` | `1.3` | — |
| `reactionSpeed` | `1.2` | — |
| `maxAttackers` | `5` | — |
| `smarterTactics` | `on` | — |

### `[nightmare]` — на загрузке

| Ключ | Значение в файле | Что делает (из комментария рядом) |
|---|---|---|
| `enabled` | `off` | Nightmare Module — Bitterblack Gransys After killing Daimon's second form, Gransys transforms: - Eternal night falls upon the land - Weather becomes… |
| `triggered` | `off` | — |
| `eternalNight` | `on` | — |
| `darkWeather` | `on` | — |
| `forcedHour` | `2` | — |
| `replaceEnemies` | `on` | — |

### `[devtools]` — на загрузке

| Ключ | Значение в файле | Что делает (из комментария рядом) |
|---|---|---|
| `enabled` | `off` | Type Atlas / factory probe (F12 → DevTools) DEVELOPER TOOL — players should leave this OFF. Build 69: this flag NO LONGER affects the mod itself.… |
| `autoDump` | `off` | — |
| `researchDump` | `off` | — |

### `[monsterTempo]` — на загрузке

| Ключ | Значение в файле | Что делает (из комментария рядом) |
|---|---|---|
| `enabled` | `on` | Вариативность темпа передвижения монстров. ЗАЧЕМ: игрок побеждает гоблина не потому, что тот слаб, а потому что выучил его ритм. Каждый монстр… |
| `factorMin` | `1.05` | — |
| `factorMax` | `1.20` | — |
| `hookWalk` | `on` | — |
| `hookSprint` | `off` | — |
| `animEnabled` | `on` | ТЕМП АНИМАЦИИ — вторая, независимая ручка (найдена 19.08.2026). В теле существа лежит ряд из пяти множителей скорости воспроизведения… |
| `animAttacksOnly` | `on` | — |
| `animFactorMin` | `1.05` | — |
| `animFactorMax` | `1.15` | — |
| `animCoupling` | `0.00` | СВЯЗКА ХАРАКТЕРА: 0.0 … 1.0. Полная независимость ручек даёт четыре характера, но и несуразицу: гоблин-спринтер с ванильным замахом читается как… |

### `[ranks]` — живой (85.56)

| Ключ | Значение в файле | Что делает (из комментария рядом) |
|---|---|---|
| `rank0Weight` | `0.34` | ВАЖНО ПРО rankNWeight: ЭТО ЗАПАСНОЙ РЫЧАГ, А НЕ ГЛАВНЫЙ. Как только наборы включены ([packs] enabled = on, по умолчанию так), состав пачки задают… |
| `rank0SizeMin` | `0.95` | — |
| `rank0SizeMax` | `1.03` | — |
| `rank0Atk` | `1.00` | — |
| `rank1Weight` | `0.46` | Ранг 2 — солдат: основной боец пачки. |
| `rank1SizeMin` | `1.03` | — |
| `rank1SizeMax` | `1.07` | — |
| `rank1Atk` | `1.09` | — |
| `rank2Weight` | `0.13` | Ранг 3 — ветеран: 1-2 на пачку, читается по размеру. |
| `rank2SizeMin` | `1.07` | — |
| `rank2SizeMax` | `1.12` | — |
| `rank2Atk` | `1.20` | — |
| `rank3Weight` | `0.05` | Ранг 4 — элита: редко, заметно крупнее и злее. |
| `rank3SizeMin` | `1.12` | — |
| `rank3SizeMax` | `1.18` | — |
| `rank3Atk` | `1.35` | — |
| `rank4Weight` | `0.02` | Ранг 5 — мини-босс: 0-1 на пачку, редко. Рост 1.25 при пределе движка 1.40: выше расходится хитбокс, не поднимать. |
| `rank4SizeMin` | `1.18` | — |
| `rank4SizeMax` | `1.25` | — |
| `rank4Atk` | `1.52` | — |
| `rank0Resist` | `1.00` | КРЕПОСТЬ РАНГА (85.47). Ключ = «во сколько раз крепче»: 1.00 = РОВНО ВАНИЛЬ, ниже 1.00 зажимается обратно. Поля в игре — ЗАПАСЫ, поэтому крепче =… |
| `rank0Stand` | `1.00` | — |
| `rank1Resist` | `1.35` | — |
| `rank1Stand` | `1.30` | — |
| `rank2Resist` | `2.00` | — |
| `rank2Stand` | `1.95` | — |
| `rank3Resist` | `2.65` | — |
| `rank3Stand` | `2.60` | — |
| `rank4Resist` | `3.30` | — |
| `rank4Stand` | `3.25` | — |

### `[packs]` — живой (85.57)

| Ключ | Значение в файле | Что делает (из комментария рядом) |
|---|---|---|
| `enabled` | `on` | — |
| `cellMeters` | `40` | Размер ячейки места. 40 м: пачка целиком внутри. Меньше 10 м код не примет. |
| `inheritMeters` | `25` | Радиус наследования набора: тела ближе этого расстояния берут набор соседа. 0 = наследования нет (тогда пачка на границе ячейки может получить два… |
| `minibossPerPack` | `1` | Сколько мини-боссов может быть в одном месте. 0 = без предела. До наборов двойной мини-босс был лотереей 2%; набор hunt отдаёт мини-боссу 10% веса, и… |
| `set0Name` | `rabble` | ВЕСА НАБОРОВ: как часто набор выпадает МЕСТУ (не каждой особи). ВЕСА СТУПЕНЕЙ ВНУТРИ: setN r0..r4 = новичок / солдат / ветеран / элита / мини-босс.… |
| `set0Weight` | `0.30` | — |
| `set0r0` | `8.00` | — |
| `set0r1` | `2.00` | — |
| `set0r2` | `0.00` | — |
| `set0r3` | `0.00` | — |
| `set0r4` | `0.00` | — |
| `set1Name` | `patrol` | Набор 2 — ПАТРУЛЬ: как задумано в [ranks] (средняя пачка игры). |
| `set1Weight` | `0.40` | — |
| `set1r0` | `0.34` | — |
| `set1r1` | `0.46` | — |
| `set1r2` | `0.13` | — |
| `set1r3` | `0.05` | — |
| `set1r4` | `0.02` | — |
| `set2Name` | `warband` | Набор 3 — ВАТАГА: ветераны, редкая элита, без мини-боссов. |
| `set2Weight` | `0.20` | — |
| `set2r0` | `0.10` | — |
| `set2r1` | `0.45` | — |
| `set2r2` | `0.35` | — |
| `set2r3` | `0.10` | — |
| `set2r4` | `0.00` | — |
| `set3Name` | `hunt` | Набор 4 — ОХОТА: элита и мини-босс, рядовых мало. Самое злое место. |
| `set3Weight` | `0.10` | — |
| `set3r0` | `0.05` | — |
| `set3r1` | `0.30` | — |
| `set3r2` | `0.35` | — |
| `set3r3` | `0.20` | — |
| `set3r4` | `0.10` | — |

### `[species.uEm0200]` — на загрузке

| Ключ | Значение в файле | Что делает (из комментария рядом) |
|---|---|---|
| `baseLocoMin` | `1.05` | 86.03, шаг D: БАЗА ТЕМПА ВИДА. Спокойный темп — то, с чем особь живёт без приказа; ярость выше (rageLoco*/rageAnim*) — то, до чего её разгоняет… |
| `baseLocoMax` | `1.20` | — |
| `baseAnimMin` | `1.05` | — |
| `baseAnimMax` | `1.15` | — |
| `ranks` | `on` | ВОЛК: РАНГИ ВКЛЮЧЕНЫ, НО РАЗМЕР НЕ ТРОГАЕМ. rankScale = off — ключ специально для волка: ступень даёт ему АТАКУ и КРЕПОСТЬ, а рост остаётся… |
| `rankScale` | `off` | — |
| `rageLocoMin` | `1.20` | ВОЛК. Проверенный профиль, эталон: бег до 1.25, замах до 1.26. |
| `rageLocoMax` | `1.25` | — |
| `rageAnimMin` | `1.20` | — |
| `rageAnimMax` | `1.26` | — |

### `[species.uEm0100]` — на загрузке

| Ключ | Значение в файле | Что делает (из комментария рядом) |
|---|---|---|
| `baseLocoMin` | `1.05` | 86.03, шаг D: БАЗА ТЕМПА ВИДА. Спокойный темп — то, с чем особь живёт без приказа; ярость выше (rageLoco*/rageAnim*) — то, до чего её разгоняет… |
| `baseLocoMax` | `1.20` | — |
| `baseAnimMin` | `1.05` | — |
| `baseAnimMax` | `1.15` | — |
| `ranks` | `on` | ГОБЛИН: ПИЛОТ ЛЕСТНИЦЫ. Размер = ступень. Выключить: ranks = off |
| `rankScale` | `on` | rankScale = on — гоблину размер выдаётся ступенью (как с самого начала). Ключ показан явно, чтобы у гоблина и волка читалось одинаково: у гоблина on,… |
| `rageLocoMin` | `1.15` | ГОБЛИН. Малый быстрый боец: бьёт быстрее, чем бегает. Потолок бега упирается в верх базового (1.20 = 1.20), поэтому самый шустрый гоблин в стае… |
| `rageLocoMax` | `1.20` | — |
| `rageAnimMin` | `1.15` | — |
| `rageAnimMax` | `1.24` | — |

### `[species.uEm0101]` — на загрузке

| Ключ | Значение в файле | Что делает (из комментария рядом) |
|---|---|---|
| `baseLocoMin` | `1.02` | 86.03, шаг D: БАЗА ТЕМПА ВИДА. Спокойный темп — то, с чем особь живёт без приказа; ярость выше (rageLoco*/rageAnim*) — то, до чего её разгоняет… |
| `baseLocoMax` | `1.15` | — |
| `baseAnimMin` | `1.03` | — |
| `baseAnimMax` | `1.10` | — |
| `ranks` | `on` | ХОБГОБЛИН: ЛЕСТНИЦА РАНГОВ ВКЛЮЧЕНА. 86.06: здесь стояло "off" с подписью «подключатся следующим, если пилот понравится» — с момента, когда… |
| `rageLocoMin` | `1.17` | ХОБГОБЛИН. Тяжёлый бронированный: естественный темп замаха ниже гоблиньего, и это решение вида — не выравнивать. СЕЙЧАС разгон замаха слабо заметен:… |
| `rageLocoMax` | `1.20` | — |
| `rageAnimMin` | `1.10` | — |
| `rageAnimMax` | `1.18` | — |

### `[species.uEm0400]` — на загрузке

| Ключ | Значение в файле | Что делает (из комментария рядом) |
|---|---|---|
| `baseLocoMin` | `1.05` | 86.03, шаг D: БАЗА ТЕМПА ВИДА. Спокойный темп — то, с чем особь живёт без приказа; ярость выше (rageLoco*/rageAnim*) — то, до чего её разгоняет… |
| `baseLocoMax` | `1.20` | — |
| `baseAnimMin` | `1.05` | — |
| `baseAnimMax` | `1.15` | — |
| `ranks` | `off` | ЯЩЕР: ранги выключены. |
| `rageLocoMin` | `1.20` | ЯЩЕР. Сбалансированный: разгон ровный, без выбросов. |
| `rageLocoMax` | `1.22` | — |
| `rageAnimMin` | `1.20` | — |
| `rageAnimMax` | `1.23` | — |

### `[pawnHaste]` — на загрузке

| Ключ | Значение в файле | Что делает (из комментария рядом) |
|---|---|---|
| `enabled` | `on` | Рывок пешки в бою. ПРОБЛЕМА. У пешек в бою нет спринта вообще. Причина найдена в данных: даш существует отдельным действием GOAP, но привязан только… |
| `factor` | `1.20` | — |
| `minDistanceM` | `5.0` | — |
| `maxDistanceM` | `40.0` | — |
| `animCouple` | `on` | — |
| `matchMonsterTempo` | `on` | — |
| `requireWeaponDrawn` | `on` | ПРИЗНАК БОЯ У САМОЙ ПЕШКИ ('requireWeaponDrawn'). «Враг в радиусе» — грубая метка: враги за стеной, которых партия не видит, включали ускорение. У… |

### `[monsterAI]` — на загрузке

| Ключ | Значение в файле | Что делает (из комментария рядом) |
|---|---|---|
| `enabled` | `on` | Build 012 keeps the accepted absolute-HP PackMark and universal table-driven restraint recipes, then carries exact target + normalized urgency… |
| `wolfActuator` | `on` | — |
| `responderMax` | `0` | 'responderMax' — СКОЛЬКО особей получает приказ директора (85.23). 0 (по умолчанию) = все подходящие, как было раньше. N = N ближайших к очагу:… |
| `chantNearest` | `1` | 'chantNearest' — «услышал каст» берёт БЛИЖАЙШЕГО монстра (85.25). Механизм: пешка кастует, монстр этого вида слышит и злится. Раньше правило… |
| `fallenGuardRadius` | `10` | 'fallenGuardRadius' — ВСТРЕЧА У ТЕЛА ПАВШЕЙ ПЕШКИ (85.27; переделано в 85.28). Механизм: упал товарищ, вы идёте его поднимать — и толпа, стоящая у… |
| `pawnFinish` | `1` | 'pawnFinish' — ДОБИВАНИЕ ЛЕЖАЩЕЙ ПЕШКИ (85.30). Пешка сбита с ног, но В СОЗНАНИИ: лежит на земле и встанет сама через секунду. Окружающие монстры… |
| `parallelOrders` | `1` | 'parallelOrders' — ПАРАЛЛЕЛЬНЫЕ ПРИКАЗЫ (85.33, включено с 86.12). ОДНОВРЕМЕННЫЕ события: трубит горнист, и в тот же момент упала пешка. Директор… |

### `[aggro]` — на загрузке

| Ключ | Значение в файле | Что делает (из комментария рядом) |
|---|---|---|
| `watch` | `off` | Прибор «на кого смотрит пачка» (docs/AGGRO_RECON.md, этап 1). Наблюдательный путь только читает. Ручные PIN/FOCUS ниже — отдельные исследовательские… |
| `logEvents` | `off` | Детальные successful per-card readback и shape-census строки — opt-in. При off автоматическими остаются transitions, bounded summaries, unsafe skips,… |
| `cardwatch` | `off` | CARDWATCH (79.0): непрерывное слежение за карточками двух особей — кандидаты в поля карточки И живая дистанция до члена карточки в одной строке.… |
| `pin` | `off` | PIN (80.0): «штырь внимания» — первая мутация трека (AGGRO_RECON §20). Каждый тик переписывает поле внимания +0x10 = 300 (нативное значение линии… |
| `pin_scope` | `nearest` | nearest | all (ближайший к члену волк | вся пачка uEm0200) |
| `pin_suppress` | `off` | off | on (81.0: гасить прочие живые карты той же особи до 0 — тогда аргмакс внимания становится чистым и цель «прилипает»); замер 80.0: без… |
| `pin_fakehit` | `off` | off | on (82.0: фейк-хит — пере-заявка «свежего урона» в блоке B заштыренной карты: 274=1, 27c=значение. Блок B восприниманием не сбрасывается (в… |
| `pin_fakehit_value` | `150` | 1..500 — «свежий урон» на заштыренного (нативные замеры: до ~499) |

### `[inGameUI]` — на загрузке

| Ключ | Значение в файле | Что делает (из комментария рядом) |
|---|---|---|
| `enabled` | `on` | In-game UI overlay (F12) |

### `[errata]` — на загрузке

| Ключ | Значение в файле | Что делает (из комментария рядом) |
|---|---|---|
| `guardianDaggerBan` | `off` | — |
| `guardianDaggerValue` | `0` | — |
| `nexusMagicBan` | `off` | — |
| `nexusMagicValue` | `0` | — |
| `wandRange` | `on` | pawn staff AI range 10 m -> 15 m (casters only, not the player spell) |
| `nukeGating` | `on` | Contextual Spell Management: блокировать 15с касты (Болид, Торнадо, Сейсм) против мелочи |

### `[possession]` — на загрузке

| Ключ | Значение в файле | Что делает (из комментария рядом) |
|---|---|---|
| `enabled` | `off` | off | on (84.37: thiscall id=7. customParams = xmm on OUR apply only. Apply/clear F12. No water. No revive. Unload clears. Not Drake.) |
| `customParams` | `off` | — |
| `timer` | `180.0` | — |
| `param0` | `0.2` | — |
| `param1` | `0.35` | — |

---

## `ddda_ai_overhaul.default.ini`

тот же главный конфиг в поставке (в зипе). Ключей: 219.

### `[main]` — на загрузке

| Ключ | Значение в файле | Что делает (из комментария рядом) |
|---|---|---|
| `loadLibrary` | `` | Load additional library - for chaining with other dinput8 mods - set to the original dinput8 mod's dll name if you want both |

### `[hotkeys]` — на загрузке

| Ключ | Значение в файле | Что делает (из комментария рядом) |
|---|---|---|
| `enabled` | `on` | Hotkeys — REQUIRED for F12 overlay to work! - 'enabled' - must be ON - 'keyUI' - F12 key code (0x7B = F12) - 'menuPause' - delay in ms for menu… |
| `keyUI` | `0x7B` | — |
| `menuPause` | `500` | — |

### `[pawnAI]` — на загрузке

| Ключ | Значение в файле | Что делает (из комментария рядом) |
|---|---|---|
| `enabled` | `on` | Pawn AI Overhaul — модель весов v3.0 - 'enabled' - master switch - 'presetsEnabled' - use anchors (sliders). OFF = weights freeze where they are -… |
| `presetsEnabled` | `on` | — |
| `acquisitor` | `on` | — |
| `acquisitorCombatFloor` | `100.0` | — |
| `acquisitorLootBoost` | `650.0` | — |
| `acquisitorBoostWindowMs` | `20000` | — |
| `acquisitorReturnMs` | `4000` | — |
| `worldUnitsPerMeter` | `100.0` | - 'worldUnitsPerMeter' - scale of world coordinates (Guardian doctrine). DDDA world units are ~centimeters, so 100.0 = 1 m. Evidence: AIPlActParam… |
| `guardianFix` | `off` | - 'guardianFix' - Build 57.1: dynamic Guardian fix. OFF by default (vanilla). When ON, lifts Guardian -3 penalty on WpnDaggerAtk (code 54)… |
| `guardianMeleeRadius` | `6.0` | — |
| `guardianPreemptRadius` | `10.0` | — |
| `guardianDaggerBiasMelee` | `2` | — |
| `guardianDaggerBiasPreempt` | `0` | — |
| `guardianMinRank` | `1` | - 'guardianMinRank' - 85.12: minimum Guardian RANK for a pawn to count as a guardian and receive the doctrine. 2 = primary, 1 = secondary, 0 =… |
| `guardianMinIncl` | `350.0` | — |
| `guardianTelemetryMs` | `0` | — |
| `guardianProbeLog` | `on` | guardianProbeLog: event-only WAKE/INTERCEPT START+RESULT, no periodic spam. off = no probe logs; doctrine unchanged. |
| `nexusProbeLog` | `on` | nexusProbeLog: Nexus START/RESULT once per window (~2 lines / 10.5 s), without per-tick sampling; off = no probe, doctrine remains active. |
| `smartUtil` | `on` | — |
| `tactical` | `on` | — |
| `lastPreset` | `5` | — |
| `smooth` | `0.10` | — |
| `vocationCordon` | `on` | --- Вокационный кордон Guardian -------------------------------------- Guardian как доктрина телохранителя осмысленна только у ближнего боя. У… |
| `cordonGuardianCapRanged` | `300` | — |
| `cordonPioneerFloorRanged` | `650` | — |
| `cordonGuardianCapHybrid` | `550` | — |
| `cordonPioneerFloorHybrid` | `550` | — |

### `[customAnchor]` — на загрузке

| Ключ | Значение в файле | Что делает (из комментария рядом) |
|---|---|---|
| `scather` | `750.0` | Inclination anchors (0 - 1000) — THE current target. Always live in F12. Loading a preset copies its values here. Dragging a slider writes here. |
| `medicant` | `400.0` | — |
| `mitigator` | `500.0` | — |
| `challenger` | `700.0` | — |
| `utilitarian` | `750.0` | — |
| `guardian` | `350.0` | — |
| `nexus` | `350.0` | — |
| `pioneer` | `400.0` | — |
| `acquisitor` | `250.0` | — |
| `skillUse` | `700.0` | — |

### `[targetLock]` — на загрузке

| Ключ | Значение в файле | Что делает (из комментария рядом) |
|---|---|---|
| `autoAim` | `off` | Target Lock — автонаведение мили-атак по камере - 'autoAim' - snap melee attacks to camera direction - Alt+X to capture player physics (once per… |

### `[camera_keys]` — на загрузке

| Ключ | Значение в файле | Что делает (из комментария рядом) |
|---|---|---|
| `freeCam` | `0x04` | Camera Plus + Pawn Cam hotkeys - 'freeCam' - toggle tactical camera (default MMB) - 'pause' - pause toggle (default Num 0) - 'pawnCam' - toggle Pawn… |
| `pause` | `0x60` | — |
| `speedUp` | `0x6B` | — |
| `speedDn` | `0x6D` | — |
| `pawnCam` | `0x61` | — |

### `[camera]` — на загрузке

| Ключ | Значение в файле | Что делает (из комментария рядом) |
|---|---|---|
| `freeCam` | `off` | Camera Plus — свободная камера + пауза - 'freeCam' - enable free camera (F4 toggle) - 'detach' - detach camera from player - 'freeFly' - enable free… |
| `detach` | `off` | — |
| `freeFly` | `off` | — |
| `flySpeed` | `2.0` | — |
| `flySpeedZ` | `2.0` | — |
| `pause` | `off` | — |
| `pauseSpeed` | `0.0001` | — |
| `pawnCam` | `off` | Party Cam — камера между Аризеном и пешкой (позиция blend, взгляд игрока) - 'pawnCam' - enable (toggle via camera_keys.pawnCam, default Num 1) -… |
| `pawnCamBias` | `1.0` | — |
| `pawnCamHeight` | `150.0` | — |
| `pawnCamFollow` | `0.01` | — |
| `pawnCamBiasEase` | `0.20` | — |

### `[combatIntel]` — на загрузке

| Ключ | Значение в файле | Что делает (из комментария рядом) |
|---|---|---|
| `enabled` | `on` | Combat Intel — боевая разведка - 'enabled' - hook ALL damage events, track enemies in combat - 'timeout' - seconds before enemy considered "out of… |
| `timeout` | `5` | — |

### `[enemyAI]` — на загрузке

| Ключ | Значение в файле | Что делает (из комментария рядом) |
|---|---|---|
| `enabled` | `on` | Enemy AI Overhaul - 'enabled' - master switch - 'aggression' - aggression multiplier (1.0 = default, >1 = more aggressive) - 'reactionSpeed' -… |
| `aggression` | `1.3` | — |
| `reactionSpeed` | `1.2` | — |
| `maxAttackers` | `5` | — |
| `smarterTactics` | `on` | — |

### `[nightmare]` — на загрузке

| Ключ | Значение в файле | Что делает (из комментария рядом) |
|---|---|---|
| `enabled` | `off` | Nightmare Module — Bitterblack Gransys After killing Daimon's second form, Gransys transforms: - Eternal night falls upon the land - Weather becomes… |
| `triggered` | `off` | — |
| `eternalNight` | `on` | — |
| `darkWeather` | `on` | — |
| `forcedHour` | `2` | — |
| `replaceEnemies` | `on` | — |

### `[devtools]` — на загрузке

| Ключ | Значение в файле | Что делает (из комментария рядом) |
|---|---|---|
| `enabled` | `off` | Type Atlas / factory probe (F12 → DevTools) DEVELOPER TOOL — players should leave this OFF. Build 69: this flag NO LONGER affects the mod itself.… |
| `autoDump` | `off` | — |
| `researchDump` | `off` | — |

### `[monsterTempo]` — на загрузке

| Ключ | Значение в файле | Что делает (из комментария рядом) |
|---|---|---|
| `enabled` | `on` | Вариативность темпа передвижения монстров. ЗАЧЕМ: игрок побеждает гоблина не потому, что тот слаб, а потому что выучил его ритм. Каждый монстр… |
| `factorMin` | `1.05` | — |
| `factorMax` | `1.20` | — |
| `hookWalk` | `on` | — |
| `hookSprint` | `off` | — |
| `animEnabled` | `on` | ТЕМП АНИМАЦИИ — вторая, независимая ручка (найдена 19.08.2026). В теле существа лежит ряд из пяти множителей скорости воспроизведения… |
| `animAttacksOnly` | `on` | — |
| `animFactorMin` | `1.05` | — |
| `animFactorMax` | `1.15` | — |
| `animCoupling` | `0.00` | СВЯЗКА ХАРАКТЕРА: 0.0 … 1.0. Полная независимость ручек даёт четыре характера, но и несуразицу: гоблин-спринтер с ванильным замахом читается как… |

### `[pawnHaste]` — на загрузке

| Ключ | Значение в файле | Что делает (из комментария рядом) |
|---|---|---|
| `enabled` | `on` | Рывок пешки в бою. ПРОБЛЕМА. У пешек в бою нет спринта вообще. Причина найдена в данных: даш существует отдельным действием GOAP, но привязан только… |
| `factor` | `1.20` | — |
| `minDistanceM` | `5.0` | — |
| `maxDistanceM` | `40.0` | — |
| `animCouple` | `on` | — |
| `matchMonsterTempo` | `on` | — |
| `requireWeaponDrawn` | `on` | ПРИЗНАК БОЯ У САМОЙ ПЕШКИ ('requireWeaponDrawn'). «Враг в радиусе» — грубая метка: враги за стеной, которых партия не видит, включали ускорение. У… |

### `[monsterAI]` — на загрузке

| Ключ | Значение в файле | Что делает (из комментария рядом) |
|---|---|---|
| `enabled` | `on` | Build 012 keeps the accepted absolute-HP PackMark and universal table-driven restraint recipes, then carries exact target + normalized urgency… |
| `wolfActuator` | `on` | — |
| `responderMax` | `0` | 'responderMax' — СКОЛЬКО особей получает приказ директора (85.23). 0 (по умолчанию) = все подходящие, как было раньше. N = N ближайших к очагу:… |
| `chantNearest` | `1` | 'chantNearest' — «услышал каст» берёт БЛИЖАЙШЕГО монстра (85.25). Механизм: пешка кастует, монстр этого вида слышит и злится. Раньше правило… |
| `fallenGuardRadius` | `10` | 'fallenGuardRadius' — ВСТРЕЧА У ТЕЛА ПАВШЕЙ ПЕШКИ (85.27; переделано в 85.28). Механизм: упал товарищ, вы идёте его поднимать — и толпа, стоящая у… |
| `pawnFinish` | `1` | 'pawnFinish' — ДОБИВАНИЕ ЛЕЖАЩЕЙ ПЕШКИ (85.30). Пешка сбита с ног, но В СОЗНАНИИ: лежит на земле и встанет сама через секунду. Окружающие монстры… |
| `parallelOrders` | `1` | 'parallelOrders' — ПАРАЛЛЕЛЬНЫЕ ПРИКАЗЫ (85.33, включено с 86.12). ОДНОВРЕМЕННЫЕ события: трубит горнист, и в тот же момент упала пешка. Директор… |

### `[aggro]` — на загрузке

| Ключ | Значение в файле | Что делает (из комментария рядом) |
|---|---|---|
| `watch` | `off` | Прибор «на кого смотрит пачка» (docs/AGGRO_RECON.md, этап 1). Наблюдательный путь только читает. Ручные PIN/FOCUS ниже — отдельные исследовательские… |
| `logEvents` | `off` | Детальные successful per-card readback и shape-census строки — opt-in. При off автоматическими остаются transitions, bounded summaries, unsafe skips,… |
| `cardwatch` | `off` | CARDWATCH (79.0): непрерывное слежение за карточками двух особей — кандидаты в поля карточки И живая дистанция до члена карточки в одной строке.… |
| `pin` | `off` | PIN (80.0): «штырь внимания» — первая мутация трека (AGGRO_RECON §20). Каждый тик переписывает поле внимания +0x10 = 300 (нативное значение линии… |
| `pin_scope` | `nearest` | nearest | all (ближайший к члену волк | вся пачка uEm0200) |
| `pin_suppress` | `off` | off | on (81.0: гасить прочие живые карты той же особи до 0 — тогда аргмакс внимания становится чистым и цель «прилипает»); замер 80.0: без… |
| `pin_fakehit` | `off` | off | on (82.0: фейк-хит — пере-заявка «свежего урона» в блоке B заштыренной карты: 274=1, 27c=значение. Блок B восприниманием не сбрасывается (в… |
| `pin_fakehit_value` | `150` | 1..500 — «свежий урон» на заштыренного (нативные замеры: до ~499) |

### `[inGameUI]` — на загрузке

| Ключ | Значение в файле | Что делает (из комментария рядом) |
|---|---|---|
| `enabled` | `on` | In-game UI overlay (F12) |

### `[errata]` — на загрузке

| Ключ | Значение в файле | Что делает (из комментария рядом) |
|---|---|---|
| `guardianDaggerBan` | `off` | — |
| `guardianDaggerValue` | `0` | — |
| `nexusMagicBan` | `off` | — |
| `nexusMagicValue` | `0` | — |
| `wandRange` | `on` | pawn staff AI range 10 m -> 15 m (casters only, not the player spell) |
| `nukeGating` | `on` | Contextual Spell Management: блокировать 15с касты (Болид, Торнадо, Сейсм) против мелочи |

### `[possession]` — на загрузке

| Ключ | Значение в файле | Что делает (из комментария рядом) |
|---|---|---|
| `enabled` | `off` | off | on (84.37: thiscall id=7. customParams = xmm on OUR apply only. Apply/clear F12. No water. No revive. Unload clears. Not Drake.) |
| `customParams` | `off` | — |
| `timer` | `180.0` | — |
| `param0` | `0.2` | — |
| `param1` | `0.35` | — |

### `[ranks]` — живой (85.56)

| Ключ | Значение в файле | Что делает (из комментария рядом) |
|---|---|---|
| `rank0Weight` | `0.34` | ВАЖНО ПРО rankNWeight: ЭТО ЗАПАСНОЙ РЫЧАГ, А НЕ ГЛАВНЫЙ. Как только наборы включены ([packs] enabled = on, по умолчанию так), состав пачки задают… |
| `rank0SizeMin` | `0.95` | — |
| `rank0SizeMax` | `1.03` | — |
| `rank0Atk` | `1.00` | — |
| `rank1Weight` | `0.46` | Ранг 2 — солдат: основной боец пачки. |
| `rank1SizeMin` | `1.03` | — |
| `rank1SizeMax` | `1.07` | — |
| `rank1Atk` | `1.09` | — |
| `rank2Weight` | `0.13` | Ранг 3 — ветеран: 1-2 на пачку, читается по размеру. |
| `rank2SizeMin` | `1.07` | — |
| `rank2SizeMax` | `1.12` | — |
| `rank2Atk` | `1.20` | — |
| `rank3Weight` | `0.05` | Ранг 4 — элита: редко, заметно крупнее и злее. |
| `rank3SizeMin` | `1.12` | — |
| `rank3SizeMax` | `1.18` | — |
| `rank3Atk` | `1.35` | — |
| `rank4Weight` | `0.02` | Ранг 5 — мини-босс: 0-1 на пачку, редко. Рост 1.25 при пределе движка 1.40: выше расходится хитбокс, не поднимать. |
| `rank4SizeMin` | `1.18` | — |
| `rank4SizeMax` | `1.25` | — |
| `rank4Atk` | `1.52` | — |
| `rank0Resist` | `1.00` | КРЕПОСТЬ РАНГА (85.47). Ключ = «во сколько раз крепче»: 1.00 = РОВНО ВАНИЛЬ, ниже 1.00 зажимается обратно. Поля в игре — ЗАПАСЫ, поэтому крепче =… |
| `rank0Stand` | `1.00` | — |
| `rank1Resist` | `1.35` | — |
| `rank1Stand` | `1.30` | — |
| `rank2Resist` | `2.00` | — |
| `rank2Stand` | `1.95` | — |
| `rank3Resist` | `2.65` | — |
| `rank3Stand` | `2.60` | — |
| `rank4Resist` | `3.30` | — |
| `rank4Stand` | `3.25` | — |

### `[packs]` — живой (85.57)

| Ключ | Значение в файле | Что делает (из комментария рядом) |
|---|---|---|
| `enabled` | `on` | — |
| `cellMeters` | `40` | Размер ячейки места. 40 м: пачка целиком внутри. Меньше 10 м код не примет. |
| `inheritMeters` | `25` | Радиус наследования набора: тела ближе этого расстояния берут набор соседа. 0 = наследования нет (тогда пачка на границе ячейки может получить два… |
| `minibossPerPack` | `1` | Сколько мини-боссов может быть в одном месте. 0 = без предела. До наборов двойной мини-босс был лотереей 2%; набор hunt отдаёт мини-боссу 10% веса, и… |
| `set0Name` | `rabble` | ВЕСА НАБОРОВ: как часто набор выпадает МЕСТУ (не каждой особи). ВЕСА СТУПЕНЕЙ ВНУТРИ: setN r0..r4 = новичок / солдат / ветеран / элита / мини-босс.… |
| `set0Weight` | `0.30` | — |
| `set0r0` | `8.00` | — |
| `set0r1` | `2.00` | — |
| `set0r2` | `0.00` | — |
| `set0r3` | `0.00` | — |
| `set0r4` | `0.00` | — |
| `set1Name` | `patrol` | Набор 2 — ПАТРУЛЬ: как задумано в [ranks] (средняя пачка игры). |
| `set1Weight` | `0.40` | — |
| `set1r0` | `0.34` | — |
| `set1r1` | `0.46` | — |
| `set1r2` | `0.13` | — |
| `set1r3` | `0.05` | — |
| `set1r4` | `0.02` | — |
| `set2Name` | `warband` | Набор 3 — ВАТАГА: ветераны, редкая элита, без мини-боссов. |
| `set2Weight` | `0.20` | — |
| `set2r0` | `0.10` | — |
| `set2r1` | `0.45` | — |
| `set2r2` | `0.35` | — |
| `set2r3` | `0.10` | — |
| `set2r4` | `0.00` | — |
| `set3Name` | `hunt` | Набор 4 — ОХОТА: элита и мини-босс, рядовых мало. Самое злое место. |
| `set3Weight` | `0.10` | — |
| `set3r0` | `0.05` | — |
| `set3r1` | `0.30` | — |
| `set3r2` | `0.35` | — |
| `set3r3` | `0.20` | — |
| `set3r4` | `0.10` | — |

### `[species.uEm0200]` — на загрузке

| Ключ | Значение в файле | Что делает (из комментария рядом) |
|---|---|---|
| `baseLocoMin` | `1.05` | 86.03, шаг D: БАЗА ТЕМПА ВИДА. Спокойный темп — то, с чем особь живёт без приказа; ярость выше (rageLoco*/rageAnim*) — то, до чего её разгоняет… |
| `baseLocoMax` | `1.20` | — |
| `baseAnimMin` | `1.05` | — |
| `baseAnimMax` | `1.15` | — |
| `ranks` | `on` | ВОЛК: РАНГИ ВКЛЮЧЕНЫ, НО РАЗМЕР НЕ ТРОГАЕМ. rankScale = off — ключ специально для волка: ступень даёт ему АТАКУ и КРЕПОСТЬ, а рост остаётся… |
| `rankScale` | `off` | — |
| `rageLocoMin` | `1.20` | ВОЛК. Проверенный профиль, эталон: бег до 1.25, замах до 1.26. |
| `rageLocoMax` | `1.25` | — |
| `rageAnimMin` | `1.20` | — |
| `rageAnimMax` | `1.26` | — |

### `[species.uEm0100]` — на загрузке

| Ключ | Значение в файле | Что делает (из комментария рядом) |
|---|---|---|
| `baseLocoMin` | `1.05` | 86.03, шаг D: БАЗА ТЕМПА ВИДА. Спокойный темп — то, с чем особь живёт без приказа; ярость выше (rageLoco*/rageAnim*) — то, до чего её разгоняет… |
| `baseLocoMax` | `1.20` | — |
| `baseAnimMin` | `1.05` | — |
| `baseAnimMax` | `1.15` | — |
| `ranks` | `on` | ГОБЛИН: ПИЛОТ ЛЕСТНИЦЫ. Размер = ступень. Выключить: ranks = off |
| `rankScale` | `on` | rankScale = on — гоблину размер выдаётся ступенью (как с самого начала). Ключ показан явно, чтобы у гоблина и волка читалось одинаково: у гоблина on,… |
| `rageLocoMin` | `1.15` | ГОБЛИН. Малый быстрый боец: бьёт быстрее, чем бегает. Потолок бега упирается в верх базового (1.20 = 1.20), поэтому самый шустрый гоблин в стае… |
| `rageLocoMax` | `1.20` | — |
| `rageAnimMin` | `1.15` | — |
| `rageAnimMax` | `1.24` | — |

### `[species.uEm0101]` — на загрузке

| Ключ | Значение в файле | Что делает (из комментария рядом) |
|---|---|---|
| `baseLocoMin` | `1.02` | 86.03, шаг D: БАЗА ТЕМПА ВИДА. Спокойный темп — то, с чем особь живёт без приказа; ярость выше (rageLoco*/rageAnim*) — то, до чего её разгоняет… |
| `baseLocoMax` | `1.15` | — |
| `baseAnimMin` | `1.03` | — |
| `baseAnimMax` | `1.10` | — |
| `ranks` | `on` | ХОБГОБЛИН: ЛЕСТНИЦА РАНГОВ ВКЛЮЧЕНА. 86.06: здесь стояло "off" с подписью «подключатся следующим, если пилот понравится» — с момента, когда… |
| `rageLocoMin` | `1.17` | ХОБГОБЛИН. Тяжёлый бронированный: естественный темп замаха ниже гоблиньего, и это решение вида — не выравнивать. СЕЙЧАС разгон замаха слабо заметен:… |
| `rageLocoMax` | `1.20` | — |
| `rageAnimMin` | `1.10` | — |
| `rageAnimMax` | `1.18` | — |

### `[species.uEm0400]` — на загрузке

| Ключ | Значение в файле | Что делает (из комментария рядом) |
|---|---|---|
| `baseLocoMin` | `1.05` | 86.03, шаг D: БАЗА ТЕМПА ВИДА. Спокойный темп — то, с чем особь живёт без приказа; ярость выше (rageLoco*/rageAnim*) — то, до чего её разгоняет… |
| `baseLocoMax` | `1.20` | — |
| `baseAnimMin` | `1.05` | — |
| `baseAnimMax` | `1.15` | — |
| `ranks` | `off` | ЯЩЕР: ранги выключены. |
| `rageLocoMin` | `1.20` | ЯЩЕР. Сбалансированный: разгон ровный, без выбросов. |
| `rageLocoMax` | `1.22` | — |
| `rageAnimMin` | `1.20` | — |
| `rageAnimMax` | `1.23` | — |

---

## `ddda_pawn_ai_profiles.ini`

профили приоритетов пешек (прибор, не применяется автоматически). Ключей: 41.

### `[profile]` — на загрузке

| Ключ | Значение в файле | Что делает (из комментария рядом) |
|---|---|---|
| `schemaVersion` | `2` | — |
| `active` | `vanilla` | — |

### `[vanilla]` — на загрузке

| Ключ | Значение в файле | Что делает (из комментария рядом) |
|---|---|---|
| `ruleCount` | `0` | — |

### `[research_code45]` — на загрузке

| Ключ | Значение в файле | Что делает (из комментария рядом) |
|---|---|---|
| `ruleCount` | `1` | — |

### `[research_code45.rule0]` — на загрузке

| Ключ | Значение в файле | Что делает (из комментария рядом) |
|---|---|---|
| `sensor` | `0` | — |
| `code` | `45` | — |
| `category` | `0` | — |
| `objectId` | `0` | — |
| `extra` | `1` | — |
| `ruleIndex` | `0` | — |
| `expectedAddS32` | `-1` | — |
| `desiredAddS32` | `-2` | — |
| `expectedAddF32` | `0.0` | — |
| `expectedBreak` | `1` | — |
| `expectedCheckCount` | `1` | — |
| `expectedSlot` | `34` | — |

### `[research_pair45_46]` — на загрузке

| Ключ | Значение в файле | Что делает (из комментария рядом) |
|---|---|---|
| `ruleCount` | `2` | — |

### `[research_pair45_46.rule0]` — на загрузке

| Ключ | Значение в файле | Что делает (из комментария рядом) |
|---|---|---|
| `sensor` | `0` | — |
| `code` | `45` | — |
| `category` | `0` | — |
| `objectId` | `0` | — |
| `extra` | `1` | — |
| `ruleIndex` | `0` | — |
| `expectedAddS32` | `-1` | — |
| `desiredAddS32` | `-2` | — |
| `expectedAddF32` | `0.0` | — |
| `expectedBreak` | `1` | — |
| `expectedCheckCount` | `1` | — |
| `expectedSlot` | `34` | — |

### `[research_pair45_46.rule1]` — на загрузке

| Ключ | Значение в файле | Что делает (из комментария рядом) |
|---|---|---|
| `sensor` | `0` | — |
| `code` | `46` | — |
| `category` | `0` | — |
| `objectId` | `0` | — |
| `extra` | `1` | — |
| `ruleIndex` | `0` | — |
| `expectedAddS32` | `-1` | — |
| `desiredAddS32` | `-2` | — |
| `expectedAddF32` | `0.0` | — |
| `expectedBreak` | `1` | — |
| `expectedCheckCount` | `1` | — |
| `expectedSlot` | `34` | — |
