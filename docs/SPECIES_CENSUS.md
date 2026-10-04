# Перепись видов: имя, карточка, вердикт

Автогенерация: `python3 tools/species_census.py --md docs/SPECIES_CENSUS.md`.

Числа — из карточек игры (`resources/extracted_assets/em/enemies.zip`,
`charparam/em/*_cmn.prp`, поля 0x0C64/0x0C6C/0x0C74/0x0C7C — те же, что берёт боевой код).
Имена — из наших словарей: `src/BestiaryData.h` (72 вида), `resources/fluffy_em.txt`
(снимок FluffyQuack), `docs/ARC_MAP.txt` (карта архивов). Колонка «источник» говорит,
откуда именно взялось имя (одно и то же имя из двух словарей — приоритет у бестиария).

«Вердикт» — что тюнер делает с ПОЛЯМИ вида (политика 85.64, `SpeciesFieldModeOf`
в `src/runtime/EnemyFileBase.h`): значение 0 — поле не пишем (в карточке это «нет такого
поля», а не «ноль урона»); значение >= 9000 — иммунитет, поле не перемножаем; остальное
пишется как обычно. Вид при этом остаётся в работе: правило «вид с нулём или с маркером
выбрасываем целиком» отменено в 85.64.

Проверка: `python3 tools/species_census.py --check` — сверяет политику в
сгенерированном заголовке с карточками и с этим документом (шаг гейта 2i6/10).

| emID | имя | семейство | класс в exe | atk | def | matk | mdef | вердикт | источник имени |
|---|---|---|---|---:|---:|---:|---:|---|---|
| em0100 | Goblins | Goblin | uEm0100 (gid 5) | 250 | 75 | 80 | 75 | в работе | BestiaryData.h |
| em0101 | Hobgoblins | Goblin | uEm0101 (gid 6) | 410 | 140 | 100 | 120 | в работе | BestiaryData.h |
| em0102 | Grimgoblins | Goblin | uEm0102 (gid 7) | 800 | 240 | 400 | 230 | в работе | BestiaryData.h |
| em0103 | Greater Goblins | BBI-Goblin | uEm0103 (gid 142) | 1450 | 430 | 400 | 380 | в работе | BestiaryData.h |
| em0104 | Goblin Shaman | — | НЕТ КЛАССА | 650 | 280 | 1500 | 1350 | в работе | ARC_MAP.txt |
| em0200 | Wolves | Wolf | uEm0200 (gid 8) | 220 | 60 | 70 | 100 | в работе | BestiaryData.h |
| em0201 | Direwolves | Wolf | uEm0201 (gid 9) | 440 | 150 | 100 | 180 | в работе | BestiaryData.h |
| em0202 | Hellhounds | Wolf | uEm0202 (gid 10) | 800 | 240 | 880 | 300 | в работе | BestiaryData.h |
| em0203 | Wargs | BBI-Wolf | uEm0203 (gid 144) | 580 | 190 | 300 | 200 | в работе | BestiaryData.h |
| em0204 | Garms | BBI-Wolf | uEm0204 (gid 145) | 3900 | 830 | 1800 | 770 | в работе | BestiaryData.h |
| em0400 | Saurians | Saurian | uEm0400 (gid 15) | 440 | 130 | 200 | 60 | в работе | BestiaryData.h |
| em0401 | Sulfur Saurians | Saurian | uEm0401 (gid 18) | 600 | 180 | 250 | 90 | в работе | BestiaryData.h |
| em0402 | Geo Saurians | Saurian | uEm0402 (gid 21) | 900 | 270 | 100 | 130 | в работе | BestiaryData.h |
| em0403 | Saurian Sages | Saurian | uEm0403 (gid 24) | 1100 | 350 | 650 | 280 | в работе | BestiaryData.h |
| em0404 | Pyre Saurians | BBI-Saurian | uEm0404 (gid 146) | 1750 | 630 | 1900 | 680 | в работе | BestiaryData.h |
| em0405 | Giant Saurian | — | uEm0405 (gid 147) | 1650 | 450 | 400 | 150 | в работе | ARC_MAP.txt |
| em0406 | Giant Sulfur Saurian | — | uEm0406 (gid 148) | 1750 | 490 | 250 | 350 | в работе | ARC_MAP.txt |
| em0407 | Giant Geo Saurian | — | uEm0407 (gid 149) | 2400 | 780 | 500 | 260 | в работе | ARC_MAP.txt |
| em0408 | Giant Saurian Sage | — | uEm0408 (gid 150) | 2400 | 680 | 1500 | 950 | в работе | ARC_MAP.txt |
| em0500 | Undead | Undead | uEm0500 (gid 27) | 330 | 105 | 10 | 95 | в работе | BestiaryData.h |
| em0501 | Undead — второе тело, без строки в бестиарии | — | uEm0501 (gid 28) | 330 | 100 | 10 | 90 | в работе | ARC_MAP.txt |
| em0502 | Stout Undead | Undead | uEm0502 (gid 29) | 360 | 100 | 10 | 90 | в работе | BestiaryData.h |
| em0503 | Undead Warriors | Undead | uEm0503 (gid 30) | 400 | 130 | 10 | 130 | в работе | BestiaryData.h |
| em0504 | Giant Undead | Undead | uEm0504 (gid 31) | 800 | 270 | 10 | 270 | в работе | BestiaryData.h |
| em0505 | Poisoned Undead | BBI-Undead | uEm0505 (gid 160) | 1450 | 370 | 400 | 440 | в работе | BestiaryData.h |
| em0506 | Banshees | BBI-Undead | uEm0506 (gid 161) | 850 | 300 | 1900 | 1600 | в работе | BestiaryData.h |
| em0507 | Eliminators | BBI-Undead | uEm0507 (gid 162) | 3500 | 950 | 150 | 390 | в работе | BestiaryData.h |
| em0600 | Harpies | Harpy | uEm0600 (gid 32) | 350 | 90 | 160 | 130 | в работе | BestiaryData.h |
| em0601 | Snow Harpies | Harpy | uEm0601 (gid 33) | 520 | 100 | 450 | 200 | в работе | BestiaryData.h |
| em0602 | Succubi | Harpy | uEm0602 (gid 34) | 720 | 220 | 670 | 240 | в работе | BestiaryData.h |
| em0603 | Gargoyles | Harpy | uEm0603 (gid 35) | 810 | 280 | 690 | 250 | в работе | BestiaryData.h |
| em0604 | Strigoi | BBI-Harpy | uEm0604 (gid 163) | 1550 | 680 | 1300 | 750 | в работе | BestiaryData.h |
| em0605 | Sirens | BBI-Harpy | uEm0605 (gid 164) | 950 | 290 | 850 | 650 | в работе | BestiaryData.h |
| em0700 | Phantoms | Ghost | uEm0700 (gid 37) | 1 | 100 | 200 | 150 | в работе | BestiaryData.h |
| em0701 | Phantasms | Ghost | uEm0701 (gid 38) | 1 | 180 | 360 | 200 | в работе | BestiaryData.h |
| em0702 | Specters | Ghost | uEm0702 (gid 39) | 1 | 200 | 400 | 250 | в работе | BestiaryData.h |
| em0703 | Wraiths | BBI-Ghost | uEm0703 (gid 165) | 500 | 700 | 1800 | 650 | в работе | BestiaryData.h |
| em0900 | Ogres | Ogre | uEm0900 (gid 44) | 825 | 210 | 80 | 210 | в работе | BestiaryData.h |
| em0901 | Elder Ogres | BBI-Ogre | uEm0901 (gid 166) | 2100 | 980 | 500 | 530 | в работе | BestiaryData.h |
| em1200 | Scarecrow — красная мишень | — | uEm1200 (gid 55) | 0 | 75 | 0 | 75 | ПОЛИТИКА: не пишем (atk,matk) — не боевые поля | ARC_MAP.txt |
| em1201 | Scarecrow — сине-зелёная мишень | — | uEm1201 (gid 56) | 0 | 75 | 0 | 75 | ПОЛИТИКА: не пишем (atk,matk) — не боевые поля | ARC_MAP.txt |
| em2000 | Skeletons | Skeleton | uEm2000 (gid 57) | 280 | 120 | 200 | 120 | в работе | BestiaryData.h |
| em2001 | Skeleton Knights | Skeleton | uEm2001 (gid 58) | 430 | 140 | 200 | 140 | в работе | BestiaryData.h |
| em2002 | Skeleton Lords | Skeleton | uEm2002 (gid 59) | 1000 | 250 | 200 | 250 | в работе | BestiaryData.h |
| em2003 | Skeleton — второе тело, без строки в бестиарии | — | uEm2003 (gid 60) | 500 | 145 | 200 | 145 | в работе | ARC_MAP.txt |
| em2004 | Skeleton Brutes | BBI-Skeleton | uEm2004 (gid 169) | 1900 | 380 | 200 | 450 | в работе | BestiaryData.h |
| em2005 | Golden Knights | BBI-Skeleton | uEm2005 (gid 170) | 4000 | 400 | 2000 | 600 | в работе | BestiaryData.h |
| em2006 | Silver Knights | BBI-Skeleton | uEm2006 (gid 171) | 850 | 650 | 900 | 700 | в работе | BestiaryData.h |
| em2007 | Living Armor | BBI-Armor | uEm2007 (gid 172) | 3300 | 660 | 1350 | 1500 | в работе | BestiaryData.h |
| em2100 | Skeleton Mages | Skeleton | uEm2100 (gid 61) | 50 | 120 | 400 | 120 | в работе | BestiaryData.h |
| em2101 | Skeleton Sorcerers | Skeleton | uEm2101 (gid 62) | 70 | 135 | 600 | 135 | в работе | BestiaryData.h |
| em5000 | Cyclopes | Cyclops | uEm5000 (gid 63) | 600 | 95 | 50 | 60 | в работе | BestiaryData.h |
| em5001 | Gorecyclopes | BBI-Cyclops | uEm5001 (gid 173) | 2900 | 430 | 500 | 390 | в работе | BestiaryData.h |
| em5100 | Golems | Golem | uEm5100 (gid 71) | 950 | 185 | 650 | 10000 | ПОЛИТИКА: иммунитет (mdef) | BestiaryData.h |
| em5101 | Metal Golems | Golem | uEm5101 (gid 72) | 1100 | 10000 | 900 | 20000 | ПОЛИТИКА: иммунитет (def,mdef) | BestiaryData.h |
| em5200 | Chimeras | Chimera | uEm5200 (gid 74) | 300 | 160 | 750 | 500 | в работе | BestiaryData.h |
| em5201 | Gorechimeras | Chimera | uEm5201 (gid 77) | 500 | 260 | 1200 | 750 | в работе | BestiaryData.h |
| em5300 | Hydras | Hydra | uEm5300 (gid 80) | 2700 | 240 | 1000 | 240 | в работе | BestiaryData.h |
| em5301 | Archydras | Hydra | uEm5301 (gid 82) | 3200 | 300 | 1500 | 300 | в работе | BestiaryData.h |
| em5400 | Griffins | Griffin | uEm5400 (gid 84) | 800 | 240 | 830 | 280 | в работе | BestiaryData.h |
| em5401 | Cockatrices | Griffin | uEm5401 (gid 85) | 1125 | 315 | 1125 | 371 | в работе | BestiaryData.h |
| em5500 | Evil Eyes | EvilEye | uEm5500 (gid 64) | 750 | 200 | 450 | 600 | в работе | BestiaryData.h |
| em5501 | Vile Eyes | EvilEye | uEm5501 (gid 68) | 600 | 250 | 1200 | 650 | в работе | BestiaryData.h |
| em5502 | Gazers | BBI-EvilEye | uEm5502 (gid 174) | 1200 | 340 | 850 | 340 | в работе | BestiaryData.h |
| em5503 | Maneater | — | НЕТ КЛАССА | 3800 | 500 | 2000 | 700 | в работе | ARC_MAP.txt |
| em5800 | The Dragon | Dragon | uEm5800 (gid 90) | 1250 | 280 | 600 | 280 | в работе | BestiaryData.h |
| em5801 | The Ur-Dragon | Dragon | uEm5801 (gid 91) | 3600 | 380 | 1300 | 380 | в работе | BestiaryData.h |
| em5900 | Drakes | Dragon | uEm5900 (gid 92) | 1600 | 400 | 500 | 230 | в работе | BestiaryData.h |
| em5901 | Wyrms | Dragon | uEm5901 (gid 93) | 800 | 270 | 1300 | 800 | в работе | BestiaryData.h |
| em5902 | Wyverns | Dragon | uEm5902 (gid 94) | 1200 | 340 | 750 | 380 | в работе | BestiaryData.h |
| em5903 | Firedrake | — | uEm5903 (gid 177) | 4750 | 1350 | 1600 | 520 | в работе | fluffy_em.txt |
| em5904 | Frostwyrm | — | uEm5904 (gid 178) | 1850 | 470 | 4300 | 1350 | в работе | fluffy_em.txt |
| em5905 | Thunderwyvern | — | uEm5905 (gid 179) | 3200 | 850 | 2500 | 850 | в работе | fluffy_em.txt |
| em5906 | Cursed Dragons | BBI-Dragon | uEm5906 (gid 180) | 5600 | 850 | 2700 | 690 | в работе | BestiaryData.h |
| em6000 | Wights | Wight | uEm6000 (gid 95) | 280 | 230 | 500 | 250 | в работе | BestiaryData.h |
| em6001 | Liches | Lich | uEm6001 (gid 96) | 300 | 260 | 800 | 300 | в работе | BestiaryData.h |
| em6002 | Dark Bishops | BBI-Wight | uEm6002 (gid 181) | 500 | 380 | 2600 | 750 | в работе | BestiaryData.h |
| em6003 | Death | BBI-Boss | uEm6003 (gid 182) | 0 | 666 | 0 | 666 | ПОЛИТИКА: не пишем (atk,matk) — не боевые поля | BestiaryData.h |
| em7000 | Daimon | BBI-Boss | uEm7000 (gid 186) | 4600 | 720 | 3000 | 770 | в работе | BestiaryData.h |
| em7001 | Awakened Daimon (Form 2) | — | uEm7001 (gid 187) | 8500 | 1200 | 5500 | 1200 | в работе | fluffy_em.txt |
| em8000 | Camp Critter | Wildlife | uEm8000 (gid 97) | 10 | 10 | 10 | 10 | в работе | BestiaryData.h |
| em8100 | Ambient prop / effect (HP=1) | — | uEm8100 (gid 98) | 80 | 30 | 70 | 30 | в работе | fluffy_em.txt |
| em8200 | Deer / Stag | Wildlife | uEm8200 (gid 99) | 180 | 55 | 1 | 20 | в работе | BestiaryData.h |
| em8300 | Ox | Wildlife | uEm8300 (gid 101) | 300 | 50 | 10 | 50 | в работе | BestiaryData.h |
| em8500 | Critter (unidentified) | Wildlife | uEm8500 (gid 105) | 1 | 1 | 1 | 1 | в работе | BestiaryData.h |
| em8501 | Large Rat | Wildlife | uEm8501 (gid 106) | 180 | 40 | 1 | 10 | в работе | BestiaryData.h |
| em8600 | Flying critter (unidentified) | Wildlife | uEm8600 (gid 107) | 1 | 1 | 1 | 1 | в работе | BestiaryData.h |
| em8601 | Critter (unidentified) | Wildlife | uEm8601 (gid 108) | 1 | 1 | 1 | 1 | в работе | BestiaryData.h |
| em8700 | Wild Boar | Wildlife | uEm8700 (gid 113) | 120 | 20 | 1 | 10 | в работе | BestiaryData.h |
| em8900 | The Seneschal | Boss | uEm8900 (gid 115) | 100 | 10 | 10 | 10 | в работе | BestiaryData.h |
| em9000 | не опознан | — | uEm9000 (gid 116) | 100 | 30 | 10 | 30 | в работе | ARC_MAP.txt |
| em9100 | Leapworm | — | uEm9100 (gid 183) | 550 | 50 | 10 | 50 | в работе | ARC_MAP.txt |

## Группы

* **в работе** — 87 видов: все четыре поля в границах, поля пишутся как обычно, файл-база умножается на ступень ранга;
* **не пишем** — 3: em1200, em1201, em6003. Нули в карточке — не «ноль урона», а «нет такого поля»: у Death нет обычной атаки, у em1200/em1201 нет боевых полей вовсе. Тюнер такие поля не пишет — что было в теле, то и остаётся;
* **иммунитет** — 2: em5100, em5101. Круглые тысячи у Голема и Металлического голема — это маркер неуязвимости в самой игре, а не «очень много защиты»: перемножать их нельзя, иначе иммунитет станет бесконечностью. Тюнер оставляет такие поля как есть.

### Виды без карточки

Есть в `docs/ARC_MAP.txt`, но `*_cmn.prp` в архиве нет — в таблицу баз такие виды не попадут, для них работает прежний путь (таблица видов из памяти, затем оценка):

`em5402`, `em5802`, `em7002`, `em8201`, `em8602`, `em9807`

### Карточки с суффиксом варианта — в таблицу НЕ берутся

Список: `em5101` (em5101_00_cmn.prp), `em5200` (em5200_00_cmn.prp, em5200_01_cmn.prp), `em5201` (em5201_00_cmn.prp, em5201_01_cmn.prp), `em5300` (em5300_00_cmn.prp), `em5301` (em5301_00_cmn.prp), `em5500` (em5500_00_cmn.prp, em5500_01_cmn.prp), `em5502` (em5502_00_cmn.prp, em5502_01_cmn.prp), `em5503` (em5503_00_cmn.prp).

Причина: ключ вида в логе разбирается как `uEm<цифры>`, и суффиксная карточка может принадлежать под-телу составного врага (диск Металлического голема, части Химеры и Гидры) или неиспользуемому варианту: у `em5503_00` нет и класса в exe (`uEm5503` в `types.tsv` отсутствует). Если такой вид однажды появится в поле, вернёмся к этому списку.

### Виды без имени в наших словарях

Нет.


## Что это значит

1. **Нули — это НЕ живность.** У всей живности карточки с настоящими, хоть и крошечными
   числами: олень `1/1/1/1`, заяц `1/1/1/1`, кабан `120/20/1/10`, лагерная мелочь
   `10/10/10/10`. Ноль стоит ровно у трёх видов: `em1200` и `em1201` (gid 55/56, имени нет
   ни в одном словаре; у обоих def/mdef 75 при нулевых атаке и магии — похоже на не-бойца,
   пугало/мишень) и `em6003` = **Death** (босс Bitterblack: `0/666/0/666`). У Death нет
   обычной атаки — он убивает механикой, поэтому в карточке ноль, а не «мало».
2. **«Огромный урон» — это не 5100.** У `em5100` (Golem) `10000` стоит в *магической
   защите*, у `em5101` (Metal Golem) — `10000` в физической и `20000` в магической: это
   маркеры иммунитета (обычный голем не берётся магией, металлический — физикой), а не
   статы. Атака у них обычная боссовая: 950 и 1100.
3. **Настоящий гигант по урону — `em7001` Awakened Daimon (форма 2): 8500 атаки, 5500
   магии.** Он в таблице и работает. Следом: `em5906` Cursed Dragon 5600, `em5903`
   Firedrake 4750, `em7000` Daimon 4600, `em0204` Garm 3900.
4. **Голем, Металлический голем и Death раньше выбрасывались целиком** — так работало
   правило «вид либо в файл-базе, либо нет»: нули и маркеры резали вид. В 85.64 это правило
   заменено пофайловой политикой: иммунные поля (>= 9000) не перемножаются, нулевые (= 0) не
   пишутся, остальные поля вида берутся из файла как обычно. Все три босса теперь идут по
   файл-базе, как и все прочие.
5. **Живность в фильтрах врагов — исправлено в 85.64.** `KindIsEnemy` знал только два имени
   (лагерная мелочь `uEm8000` и заяц `uEm8600`), поэтому олень, лань, змея, мышь/ворона и
   кабан проходили как враги: они попадали в тактические счётчики, в дистанции рывка пешек,
   в агрессию и в допуск пачек. Теперь список мирных видов явный (`WorldScan.cpp`
   `kHarmlessKinds`), а масштаб живности при этом сохраняется намеренно: существо — да,
   угроза — нет.
6. **Части составных врагов — исправлено в 85.64.** `em8100` (ambient prop),
   `em8200`/`em8201` (части драконов), `em8300` (тело Проклятого дракона) и `em7002`
   (драконья голова на груди Даймона) лежат в таблице как обычные виды, но собственными
   телами не являются: теперь они в списке `kStructuralKinds` — их не тюнят, не считают
   угрозой и не берут в допуск пачек. Один список на весь продукт: `Runtime::KindIsStructural`.

## Что осталось из предложений переписи

* **Имена для оставшихся 8 видов** (`em0104`, `em0405`…`em0408`, `em1200`, `em1201`,
  `em5503`, `em9000`): достать из игровых текстов. На бой не влияет — только на читаемость
  лога. Отдельная задача, в 85.64 не входила.
