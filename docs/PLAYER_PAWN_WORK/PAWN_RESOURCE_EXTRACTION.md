# Какие ресурсы распаковать для AI пешки

**Статус:** выполнено. 83 файла получены из commit `5007796` и импортированы в `resources/extracted_assets/pawnAI/`. Результат разбора: `PAWN_AI_ASSET_RESULT.md`.

## 1. Важная поправка к аналогии с гоблином

У гоблина файлы `.eap/.lmt/.prp/.rst` дали статические параметры, таблицы действий и мотионы. Но текущее живое состояние игра назвала через DTI объекта из общего слота `body + 0x2DC8`.

У пешки этот live-механизм уже найден:

- `uCmc + 0x2DC8 -> cPlAct*` — фактически выполняемое низкоуровневое действие;
- `uCmc + 0x2DD4` — packed action-code;
- `uCmc + 0x3DEC -> cCmcInfo` — pawn-only AI/info object.

В TypeAtlas уже есть:

- **282** класса `cPlAct*` — низкоуровневые движения, урон, оружие, взаимодействия;
- **180** классов `cCmc*` — верхнеуровневые решения пешки: Follow, ItemGet, TreasureBox, Healing, Guard, Climb, конкретные атаки и навыки.

То есть вручную проигрывать все сотни действий ради их имён не требуется. Распаковка нужна для второй половины задачи: получить ванильные веса, условия, дистанции и связь решения `cCmc*` с исполняемым `cPlAct*`.

## 2. Точный файловый аналог AI гоблина

| Гоблин | Игрок/пешка | Runtime-двойник |
|---|---|---|
| `AI/Character/Enemy/*_enemy_act_param.eap` | `AI/AIPlayerActionParameter/*.AIPlActParam` | `rAIPlayerActionParameter` → `cAIPlayerActionParameter` |
| think/FSM policy | `AI/PrioThink/cmc.prt` | `rAIPriorityThink` → `cAIPriorityThink` |
| `e0100_*.lmt` | `motion/pl/.../*.lmt` + weapon archives | текущий `cPlAct*` выбирает motion |
| `uEm* + 0x2DC8` | `uCmc + 0x2DC8` | общий current-action slot |

Особенно важен `cmc.prt`: имя совпадает с живым классом пешки `uCmc`, а TypeAtlas подтверждает ресурс `rAIPriorityThink` размером 1064 байта и runtime-инстанс `cAIPriorityThink` размером 1020 байт.

## 3. Что распаковать сейчас

Нужен один архив из чистой установки:

```text
<папка DDDA>\nativePC\rom\game_main.arc
```

Распаковать **копию** архива свежей версией ARCtool с XFS-конвертацией и сохранением manifest/order:

```text
arctool.exe -xfs -dd -texRE6 -alwayscomp -pc -txt -v 7 game_main.arc
```

Ничего не запаковывать обратно и не подменять в игре.

## 4. Что прислать после распаковки

Не нужен весь распакованный `game_main`. Сложить в один ZIP:

```text
game_main\AI\PrioThink\                       целиком
game_main\AI\AIPlayerActionParameter\         целиком
game_main\param\pl\                           целиком
game_main.arc.txt или game_main.txt             manifest, который создал ARCtool
```

Важно сохранить и исходные бинарные файлы, и созданные ARCtool `.xml/.txt`, если он оставил оба варианта.

Если названия папок немного отличаются, прислать весь `game_main\AI\` — это лучше, чем вручную отфильтровать и потерять соседний ресурс.

## 5. Что пока не нужно

Пока не присылать:

- модели и текстуры;
- весь `motion/pl`;
- `npc000.arc`;
- все weapon archives;
- перепакованный или установленный модифицированный `game_main.arc`.

Сначала разбираются AI policy и action parameters. Мотионы понадобятся только для связи «решение/скилл → номер анимации».

## 6. Второй этап: мотионы текущей пешки

Build 39 показал у тестовой пешки кинжалы и семейство лука. Точный второй архив зависит от её вокации:

```text
Strider: nativePC\rom\wp\w1\dag.arc + nativePC\rom\wp\w2\bow.arc
Ranger:  nativePC\rom\wp\w1\dag.arc + nativePC\rom\wp\w2\bbw.arc
```

Сначала определяется вокация через уже известный character-record `+0x6E0`; угадывать `bow` против `bbw` по одному имени `cPlActWpnBow` не нужно. Общие player motions находятся внутри `game_main\motion\pl\...`; weapon-specific LMT лежат в соответствующих weapon archives. Для первого этапа они не нужны.

## 7. Безопасность ARCtool

Использовать чистую копию и свежий ARCtool. В старых версиях был известен дефект XFS-пересборки `AI\PrioThink\cmc.prt`, после которого пешки могли перестать реагировать. Для нашего исследования нужна только распаковка; изменённый архив в игру не ставится.

## 8. Что будет сделано после получения файлов

1. Прочитать XFS-схемы `cmc.prt` и всех `AIPlActParam*`.
2. Построить статический каталог order/weight/condition/distance.
3. Сопоставить ресурсные классы `rAIPriorityThink` / `rAIPlayerActionParameter` с живыми `cAIPriorityThink` / `cAIPlayerActionParameter`.
4. Найти текущий верхнеуровневый `cCmc*` внутри/через `cCmcInfo`.
5. Использовать файлы как словарь и дефолты, но менять поведение LIVE, без подмены `game_main.arc`.

## 9. Внешняя проверка путей

- ARCtool для DDDA и XFS-конвертация: https://steamcommunity.com/app/367500/discussions/0/451850849186329684/
- Точный лог `AI/PrioThink/cmc.prt` и `AI/AIPlayerActionParameter/AIPlActParamBow.AIPlActParam`, включая предупреждение о старом XFS-конвертере: https://residentevilmodding.boards.net/thread/481/arc-unpacker-repacker-v0-27?page=16
- Пример common player LMT внутри `game_main/motion/pl/...`: https://www.nexusmods.com/dragonsdogma/mods/602
