# Разведка: одержимость пешки по нашему желанию (в любой момент)

> Протокол охоты: [`SOURCE_OF_TRUTH.md`](SOURCE_OF_TRUTH.md) §12.
> CATALOG: слот **7** = Possession (`mTimer=180`), слот 6 = Drenched —
> [`generated/STATUS_PARAM.md`](generated/STATUS_PARAM.md).
> Live-массив — на ЗАПИСИ персонажа (SoT §12.1.2 CONFIRMED). Тело — нет.
> Пишет `src/pawnai/Possession.cpp`. Apply 84.34 = thiscall на inline
> `cStatus`, не poke записи. Смещение «статус-байта на теле» сюда не писать.

> Постановка от 24.08.2026: «откопать и подготовить фичу одержимости
> (possession) пешки по нашему желанию в любой момент».
>
> Уточнение постановки (24.08.2026, заказчик): вариант **A** (настоящий
> debilitation), цель — **сначала главная пешка**, потом остальные.
> Сюжетная роль: **первый признак заражения материка** — игрок воскрешает
> павшую в бою пешку, а получает одержимую скверной острова пешку-врага
> прямо в бою. Этап — исследование, кода пока нет.
>
> Статус: apply id=7 CONFIRMED (84.34 лог, 84.35 без воды). Кнопка —
> отладка. Сюжет скверны: `NIGHTMARE.md` (sidecar, не сейв). id≠7 не
> канон. Revive лечит — не вешать.

## 0. Три механики под одним словом «possession»

Разделение не моё — оно есть в самой игре (вики Possession + Steam-обсуждения,
проверено веб-поиском 24.08.2026):

| # | Механика | Кто накладывает | На кого | Что происходит | Как снимается |
|---|---|---|---|---|---|
| **A** | **Possession** (debilitation) | дракониды (Drake/Wyvern/Wyrm), Specter, Daimon | **только пешки** | красные глаза + тёмное облако, пешка бьёт Аризена, тикает HP, мили-урон сильно урезан, ~3 минуты | добить до unconscious, Panacea/Placative Brew/Sobering Wine, Ferrystone; враги её игнорируют |
| B | Ghastly possession (хватка призрака) | Ghost/Phantom/Specter | любой член партии | призрак облепляет голову и **сосёт HP**; это не статус | ударить по захваченному; смерть пешки = уход в Рифт навсегда |
| C | Одержимость **Аризена** | Daimon | игрок | только визуал | само |

Делаем **A**. Полезные для сюжета свойства A (из вики):
«Possessed pawns attack the Arisen, but not other pawns» · «Foes generally
ignore possessed pawns» · «Pawns recovered from Unconsciousness are cured» ·
мили-урон режется почти в ноль, магия и стрелы — нет · «A possessed pawn with
no equipped weapon will stand still».

**Ключевое противоречие с нашей задумкой:** ваниль прямо говорит, что
воскрешение **лечит** одержимость. Наша фича — инверсия: воскрешение её
**ставит**. Значит точку входа ищем на событии воскрешения, а не на статусе.

## 1. ЭРАТТА: карта врагов в `ARC_MAP.txt` неверна

`docs/ARC_MAP.txt` §`nativePC/rom/enemy/` составлен «по логам ARCtool, форумам
и коду dinput8» и **расходится с рантайм-бестиарием** (`src/BestiaryData.h`,
сгенерирован из игровых данных). Это важно: мы запросили не тот архив.

| `ARC_MAP.txt` (старый) | `BestiaryData.h` (84.38 верифицировано) |
|---|---|
| `em0100.arc` Hobgoblin | `uEm0100` Goblin, `uEm0101` Hobgoblin, `uEm0102` Grimgoblin |
| `em0500.arc` Skeleton | `uEm0500` Undead (Zombies), `uEm0506` Eliminator |
| `em0600.arc` Undead | `uEm0600` Harpy, `uEm0603` Gargoyle |
| `em0700.arc` Harpy | `uEm0700` Phantom, `uEm0703` Wraith |
| `em0900.arc` Gargoyle | `uEm0900` Ogre, `uEm0901` Elder Ogre |
| `em2000.arc` Cyclops | `uEm2000` Skeleton, `uEm2001` Knight, `uEm2006` Living Armor |
| `em5000.arc` Golem | `uEm5000` Cyclops, `uEm5001` Gorecyclops |
| `em5100.arc` Chimera | `uEm5100` Golem, `uEm5101` Metal Golem |
| `em5300.arc` / `em5400.arc` | `uEm5300` Hydra, `uEm5400` Griffin, `uEm5401` Cockatrice |
| `em5500.arc` Eliminator | `uEm5500` Evil Eye, `uEm5501` Vile Eye, `uEm5502` Gazer, `uEm5500_00` Maneater |
| `em5800.arc` Drake | `uEm5800` The Dragon (Grigori), `uEm5801` Ur-Dragon |
| `em5900.arc` Evil Eye | `uEm5900` Drake, `uEm5901` Wyrm, `uEm5902` Wyvern, `uEm5906` Cursed Dragon |
| `em6000.arc`–`em6003.arc` | `uEm6000` Wight, `uEm6001` Lich, `uEm6002` Dark Bishop, `uEm6003` Death |
| `em7000.arc`–`em7002.arc` | `uEm7000` Daimon Form 1, `uEm7001` Daimon Form 2 |
| `em8000.arc` The Dragon | `uEm8000` Camp Critter / Wildlife (лагерная живность) |

Верифицированный список (84.38, DTI + types.tsv + листинги ассетов):

```text
em0100 Goblin/Hobgoblin/Grimgoblin · em0103 Greater Goblin · em0200 Wolf · em0203 Warg
em0400 Saurian · em0500 Undead · em0506 Eliminator · em0600 Harpy · em0603 Gargoyle
em0700 Phantom/Specter · em0900 Ogre · em0901 Elder Ogre · em2000 Skeleton
em2001 Skeleton Knight · em2006 Living Armor · em5000 Cyclops · em5001 Gorecyclops
em5100 Golem · em5101 Metal Golem · em5200 Chimera · em5300 Hydra · em5400 Griffin
em5500 Evil Eye · em5501 Vile Eye · em5502 Gazer · em5500_00 Maneater
em5800 The Dragon (Grigori) · em5801 The Ur-Dragon
em5900 Drake · em5901 Wyrm · em5902 Wyvern · em5906 Cursed Dragon
em6000 Wight · em6001 Lich · em6002 Dark Bishop · em6003 Death
em7000 Daimon (Form 1) · em7001 Awakened Daimon (Form 2)
em8000 Camp Critter / Wildlife · em8600 Hare / Rabbit · em8900 The Seneschal
```

## 2. Главное исправление: `cEmWightActPossesion` — это НЕ про пешку

Первая версия этого документа назвала его «самым коротким путём». Это неверно.

Бестиарий (`resources/bestiary.py`, строки 75 и 558-559) кладёт записи
`4756/4757` в набор **Cursed Dragons**:

```python
75: [ 'Cursed Dragons', 70, [ 4751, 4752, 4753, 4754, 4755, 4714, 4716,
                               4717, 4718, 4756, 4757, 4758, 4759 ] ],
...
4756: [ 105, 10, 'Observe the Dark Bishop revive it' ],
4757: [ 106, 10, 'Observe the Dark Bishop possess it' ],
4758: [ 107, 20, 'Observe items rotting' ],
```

«Observe the Dark Bishop possess **it**» — `it` это Cursed Dragon, не пешка.
Классы Wight-семейства это подтверждают:

```text
cEmWightActPossesion            132 B  RVA 0xF64E4A  vt 0x15D5E84
cEmWightMagicSummon             ...    у Dark Bishop есть призыв
cEmWightMagicSummonDragon       212 B  RVA 0xF64F0A  vt 0x15D5F6C
cEmWightActOneKillAtk           224 B  RVA 0xF64D0A  vt 0x15D5D1C
```

То есть Dark Bishop (uEm6002) **оживляет и подчиняет своего Cursed Dragon**
(uEm8300). У `cEm8300` ровно те действия, которые этому соответствуют:
`ActQuestWait`, `ActQuestDown`, `ActStartLoopEnd`, `ActionTargetAttack`,
`ActionTargetWalk`, `ActBlowAndDown` — «послушная» машина, а не свободный враг.

**Вывод:** `cEmWightActPossesion` — готовый образец того, **как в движке
выглядит «подчинить существо»**, но цель у него — монстр, а не `uCmc`.
Для нашей фичи он полезен как аналогия и как источник эффекта, но рычаг не он.

## 3. Второе исправление: у драконидов possess-класса в атласе нет

Проверено перебором всех 4405 типов:

| Семейство | Классов действий | Есть possess/grab-pawn? |
|---|---:|---|
| `cEm8000` The Dragon / Ur-Dragon | 4 | нет |
| `cEm8100` (каталог «Drake», не Devilfire) | 5 | нет (HoverCeiling…) |
| `cEm8200` Wyrm | 2 | нет |
| `cEm8201` Wyvern | **0** | нет |
| `cEm8300` Cursed Dragon | 8 | нет |
| `cEm6002` Dark Bishop (Wight-семейство) | 26 | **`cEmWightActPossesion`** |

Плюс: **ни одного** класса `cEm*Act*` с именем, похожим на possess, кроме
Wight-овского. Полный перебор по `Grab|Catch|Hold|Press` даёт захваты у
`em0400/0500/0600/0900/5000/5100/5200/5300/5400/5500/5800/7000`, но у
`em8xxx` — ни одного.

**Что из этого следует.** Одержимость пешки драконидом реализована НЕ
именованным FSM-классом атакующего. Остаются три候选:

1. **motion event** внутри `.lmt` драконида (движок поднимает статус по кадру
   анимации) — якорь архива **`em5900`**, не `em8100`;
2. **сторона жертвы**: у пешки уже есть `cPlActGrabStart` (120 B) —
   наш `TacticalCues` ловит именно его как `PACK-GRAB-ALERT`;
3. **общий слой статусов** (`cStatus` / `uCharacterBase::StatusEffect`), куда
   пишет не класс действия, а хит-коллизия.

Пункт 2 для нас самый практичный: он уже наблюдался живьём и уже имеет
обработчик в `src/monsterai/TacticalCues.cpp`.

## 4. Точка входа под сюжет: воскрешение

Заказчик хочет «воскресил павшую → получил одержимую». В атласе есть полный
набор классов этого пути:

| Класс | Размер | RVA | vtable | Роль |
|---|---:|---|---|---|
| `cPlActCmcNeardeath` | 128 | `0xF6887A` | `0x15DD60C` | пешка лежит (downed) |
| `cPlActCmcDead` | 124 | `0xF688FA`… | `0x11DD69C` | смерть пешки |
| `cPlReviveCMC` | **132** | `0xF6827A` | `0x15DCEA0` | **воскрешение пешки** — наша точка |
| `cNpcActRevive` | 128 | `0xF6CB7A` | `0x15E9EA8` | воскрешение NPC (аналог) |
| `cEventActNeardeath` | 120 | `0xF3F0A7` | `0x1597AB4` | событийная обёртка near-death |
| `cCmcCure` | 672 | `0xF2FDDA` | `0x156F214` | решение пешки «лечить» (Halidom/круги — `WAND_RANGE.md`) |
| `cCmcHealing` | 672 | `0xF2FD9A` | `0x156F1AC` | решение «Anodyne/сигил» |
| `cCmcRecover` | 640 | `0xF30BDA` | `0x157052C` | code 19 `Recover` |

**Это уже наполовину готово в нашем рантайме.** `src/runtime/PartyRecon.cpp`
строки 586-590 держат ровно этот набор имён как кандидатный downed-hint:

```cpp
static const char* kCandidate[] = {
    "cPlActCmcDead", "cPlActDead", "cPlActDmgDown",
    "cPlActDmgDownDamage", "cPlActDmgDownDead", "cPlReviveCMC"
};
```

и честно помечает `downedValid = false` / `downedRevivable = false`
(`src/runtime/Runtime.h` §`PartyCombatMember`), потому что живой валидации
последовательности «пешка упала → воскрешена» ещё не было.

**Первый практический шаг фичи — не статус, а детектор.** Нужен наблюдатель,
который печатает переход `cPlActCmcNeardeath → cPlReviveCMC → первый
обычный act` вместе с `PawnPriorityCode()` и позицией. Это:

- закрывает `downedValid`/`downedRevivable` (технический долг, уже объявлен);
- даёт нам точный кадр, в который надо вклиниться;
- read-only, нулевой риск (`FIX_RULES` §5.1: census запрещён, точечное чтение
  по якорю `uCmc + 0x2DC8` — разрешено).

## 5. Кандидаты статусной подсистемы (для варианта A «по-настоящему»)

| Класс | Размер | caller RVA | vtable | Почему |
|---|---:|---|---|---|
| `uCharacterBase` | 12176 | `0xF3FE2A` | `0x159A0F0` | база живого персонажа; `uPlayer` (23056) и `uCmc` (22752) её наследуют |
| `uCharacterBase::StatusEffect` | **20** | `0xF3FE67` | `0x159A10C` | фабрика вызывается **изнутри** `uCharacterBase` — самый прямой кандидат на «один наложенный эффект» |
| `cStatus` | 152 | `0xF3FB4A` | `0x15989D8` | «состояние статусов существа» (`STATUS_EFFECTS_RECON.md` §3) |
| `cStatus::cStatWork` | 32 | `0xF3FB87` | `0x15989F4` | рабочий блок одного статуса |
| `cEffectStatus` / `cEffectStatusManager` | 40 / 32 | `0xF3ED07` / `0xF3ED47` | `0x1597570` / `0x159758C` | наложенные эффекты + менеджер |
| `rStatusParam` / `cStatusParam` | 120 / 28 | `0xF79B27` / `0xF79AE7` | `0x16173C0` / `0x161736C` | ресурс параметров статуса; у гоблина в теле на `+0x2710` |
| `sAIMessageCtrl::cBadStatusMsgCtrl` | 80 | `0xF271A7` | `0x1555DD0` | как статус **сообщается** AI |
| `cPlActStatusRecover` / `cPlActStatusRefresh` | 116 / — | `0xF691F7` | `0x15DE108` | action-сторона статуса у игрока/пешки |
| `cAICheckSituationCmc::cStatusSeqData(s)` | 8 / 36 | — | — | проверка статуса в условиях AI пешки |

Отдельно проверено: **типа «одержимый игрок» не существует** — в атласе только
`uPlayer`, `uPlayerBase`, `uPlayer::cInsuranceWallInfo`. Значит одержимость —
это флаг/статус на существующем теле плюс смена цели AI. Тело подменять не
нужно, и это хорошо: подмена `cPlAct*`/target pointer запрещена (`FIX_RULES` §3).

Что уже есть как **резист** (не рычаг):

- `{ 19, "19: Possession Resistance" }` — `stdafx.cpp:367`, строка UI из
  мастер-списка 24 сопротивлений (чужой dinput8-мод);
- `{ 0x006C, kF32, 4, "res_possession" }` — `src/CharParamEnemy.Generated.h:52`,
  поле `耐敵化` вражеского `.prp`, сгенерировано `tools/prp_to_header.py:67`.

## 6. Пути реализации (переоценены по итогам §2-§4)

| Путь | Суть | Статус после разведки |
|---|---|---|
| **1. Статус** | найти в живом `uCmc` блок статуса и выставить Possession так же, как это делает игра | **основной**. Требует раскладки `cStatus`/`StatusEffect` — только живой A/B |
| **2. Приём существа** | дать Dark Bishop выполнить `cEmWightActPossesion` | **понижен**: цель приёма — монстр, а не пешка (§2). Годится как донор эффекта/звука |
| **3. Точка воскрешения** | детектор `cPlActCmcNeardeath → cPlReviveCMC`, в этот кадр ставим статус | **новый, приоритетный как первый шаг**: read-only, закрывает существующий техдолг |
| 4. Симуляция | цель пешки → Аризен + тик HP (`+0x96C`) без статуса | fallback «хотя бы работает»; нет красных глаз и штатного лечения |
| 5. Архивы | каталог статусов из `game_main.arc` | даёт id и параметры, рычаг не даёт |

Реалистичный порядок: **3 → 1**, при поддержке **5**. Путь 4 держим как
страховку на случай, если статус окажется недостижим без прямого вызова FSM.

## 7. Запрос файлов (исправленный)

Команда репозитория (`tools/arctool_helper.py`):
`arctool.exe -xfs -dd -texRE6 -alwayscomp -pc -txt -v 7 <arc>` — на **копии**.

| Приоритет | Что | Зачем |
|---|---|---|
| 🔥 0 | `dir nativePC\rom\enemy\` (просто листинг) | сверить имена архивов с рантайм-id (§1) |
| 🔥 1 | архив Devilfire Drake: **`em5900*`** (EAP уже сверен, §8.5.1) | motion-событие grab-possess (§3, гипотеза 1) |
| 🔥 1 | `game_main.arc` → `param/` всё со `Status`/`Debuff` в имени | id и параметры Possession |
| 🔥 1 | `game_main.arc` → `AI/Character/Pawn/`, `AI/AINpcActionParameter/` | есть ли у пешки строка под одержимое поведение |
| 2 | архив Wight/Lich/Dark Bishop: `em6000*` | разбор `cEmWightActPossesion` как донора эффекта (§2) |
| 2 | архив Cursed Dragon: `em8300*` | как выглядит «подчинённое» существо (`ActQuest*`) |
| 3 | архив Ghost: `em1200*`/`em1201*` | вариант B, если понадобится |
| 3 | `stage450*` (BBI финал) | Daimon + Chamber of Lament, где стоят «одержимые» пешки |

Отдельно: **`DDDA.exe`** — согласован. Он нужен, чтобы разобрать caller-функции
фабрик (`cStatus` `0xF3FB4A`, `StatusEffect` `0xF3FE67`, `cEmWightActPossesion`
`0xF64E4A`, `cPlReviveCMC` `0xF6827A`) статически, до живых замеров.

## 8. План живых замеров (read-only, код — только наблюдатель)

```text
1. наблюдатель воскрешения: uCmc + 0x2DC8 (имя класса через DTI, не оффсет)
   + packed code +0x2DD4 + PawnPriorityCode() + позиция;
   печатать ТОЛЬКО переходы, не каждый кадр;
2. поймать ванильную одержимость (Drake/Wyvern у Greatwall, либо Daimon)
   и снять A/B: дамп uCmc ДО / ВО ВРЕМЯ / ПОСЛЕ (Panacea или добивание);
3. в том же дампе искать по ИМЕНАМ КЛАССОВ: cStatus, cStatus::cStatWork,
   uCharacterBase::StatusEffect, cEffectStatus, rStatusParam
   (Runtime::FindChildByClass уже умеет, FIX_RULES §6);
4. раскладка блока → запись в FIELD_MAP.md, контракт → SOURCE_OF_TRUTH.md;
5. модуль src/pawnai/Possession.{h,cpp} (84.31) пишет слой C на MainPawn.
   write-WATCH в лагере — этот зип. Revive не вешаем.
```

## 9. Риски и границы

- `FIX_RULES.md` §3: нельзя подменять `cPlAct*` и target pointer — путь 2
  упирается именно в это;
- `FIX_RULES.md` §4б: readback ≠ согласие движка → обязателен WATCH 2-3 с и
  «голос» у каждой смены состояния (armed/applied/rolled back/failed);
- `FIX_RULES.md` §5.1: полный census в активной игре запрещён — только
  точечный обход по якорям, каждый под SEH;
- **фича обязана иметь выход.** Одержимость лечится добиванием пешки;
  без явного «снять» (хоткей) и автоотката при выгрузке DLL получим
  инструмент необратимой отправки пешки в Рифт;
- сюжетный сценарий «воскресил → одержима» означает, что триггер висит на
  событии игры, а не на хоткее. Хоткей оставляем как отладочный;
- наёмные пешки: `HIRED_PAWNS_SCOPE.md` — чужой билд не трогаем, рантайм-
  состояние с откатом допустимо (прецедент `HiredInclRestoreAll()`);
- `em1000` / `em5000` / `em6000` / `em7000` из `ARC_MAP.txt` **не заказывать**
  вслепую: имена на диске не сверены с рантайм-id (§1).

## Источники вне репозитория (веб-поиск 24.08.2026)

- `dragonsdogma.fandom.com/wiki/Possession` — разделение A/B/C, эффекты,
  способы лечения, «attack the Arisen, but not other pawns»;
- `dragonsdogma.fandom.com/wiki/Drake` — «Grab (Pawn) → Spec/Status → Causes
  Possession», +33.3% урона дракону во время удержания;
- Steam 367500: «Being possessed is not a status effect» (про B), «Status
  effect Possession comes only from Dragons», лечение Panacea / Sobering Wine.
