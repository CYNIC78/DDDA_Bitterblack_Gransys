# Карта слоёв `src/devtools/DevTools.cpp`

> Сгенерировано `tools/analyze_devtools_layers.py`. Руками не править.

Файл: **5172 строк**, разобрано **112 функций** верхнего уровня (**4720 строк** в телах), **107 файловых статиков**.

## Слои

| Слой | Что это | Функций | Строк |
|---|---|---:|---:|
| `PRODUCT-API` | зовётся продуктовыми модулями напрямую → `src/runtime/` | 0 | 0 |
| `PRODUCT-DEP` | нужна продукту транзитивно → `src/runtime/` | 0 | 0 |
| `PASSENGER` | research-дамп в продуктовом тике → место разреза, в runtime не едет | 0 | 0 |
| `PROBE-API` | research-кнопка в продуктовом UI → вырезать из `PawnAI.cpp` | 0 | 0 |
| `PROBE-DEP` | живёт только ради проб → уйдёт вместе с ними | 0 | 0 |
| `RESEARCH` | пробы/аудиты/дампы → под `researchDump` или под нож | 30 | 2064 |
| `DEVTOOLS` | инфраструктура DevTools → остаётся в `src/devtools/` | 82 | 2656 |

> **Слои чистые.** Продуктового кода в `DevTools.cpp` нет: продукт живёт в `src/runtime/` и работает при `[devtools] enabled = off`. Этот файл — исследовательский инструмент, его можно отключить целиком.

**Исчезает вместе с пробами: 0 строк** (0 функций) — по таблице судьбы проб в `REFACTOR_TASK.md` §4.

## Пассажиры: research-код в продуктовом тике

Эти функции физически лежат в продуктовом пути вызовов, но по смыслу — исследовательские дампы (их зовут под `g_researchDump` / `g_intentTrace`). При распиле разрез идёт ПО НИМ: в `src/runtime/` они не едут, остаются в DevTools и вызываются из runtime через узкий хук.

| Функция | Строки | Размер | Кто зовёт из продуктового пути |
|---|---|---:|---|

## Research-кнопки в продуктовом UI (нарушение слоёв)

`src/PawnAI.cpp` рисует кнопки проб прямо в продуктовой панели. Из-за этого продукт формально зависит от research-кода. Вырезать первым делом.

| `DevTools::` | Строки | Зовёт | Судьба |
|---|---|---|---|

## Точки входа продукта

| `DevTools::` | Строки | Кто зовёт |
|---|---|---|

## Общие статики (главный тормоз распила)

Нет — распил чистый.

## Полный разбор по функциям

| Строки | Размер | Слой | Функция | Зовёт | Статиков |
|---:|---:|---|---|---|---:|
| 129–145 | 17 | `DEVTOOLS` | `IsRepeatTag` | — | 0 |
| 153–164 | 12 | `DEVTOOLS` | `KindName` | — | 0 |
| 164–188 | 25 | `RESEARCH` | `ProbeType` | — | 0 |
| 188–190 | 3 | `DEVTOOLS` | `DevTools::ModuleBase` | — | 0 |
| 190–191 | 2 | `DEVTOOLS` | `DevTools::Rebase` | — | 0 |
| 193–203 | 11 | `DEVTOOLS` | `DevTools::Identify` | — | 0 |
| 229–234 | 6 | `DEVTOOLS` | `IsSharedStub` | — | 0 |
| 234–241 | 8 | `DEVTOOLS` | `AddHit` | — | 0 |
| 241–313 | 73 | `DEVTOOLS` | `ScanImage` | `AddHit`, `IsSharedStub` | 4 |
| 313–339 | 27 | `RESEARCH` | `WriteScanJson` | — | 3 |
| 351–361 | 11 | `DEVTOOLS` | `WatchAdd` | — | 2 |
| 361–382 | 22 | `DEVTOOLS` | `KindForName` | — | 0 |
| 382–410 | 29 | `DEVTOOLS` | `BuildWatch` | `KindForName`, `WatchAdd` | 1 |
| 410–417 | 8 | `DEVTOOLS` | `WatchVt` | — | 2 |
| 424–444 | 21 | `DEVTOOLS` | `NameOf` | `KindForName`, `WatchVt` | 0 |
| 444–446 | 3 | `DEVTOOLS` | `IsCharKind` | — | 0 |
| 446–451 | 6 | `DEVTOOLS` | `IsHopKind` | — | 0 |
| 694–702 | 9 | `DEVTOOLS` | `HistAdd` | — | 2 |
| 702–709 | 8 | `DEVTOOLS` | `Seen` | — | 2 |
| 709–729 | 21 | `DEVTOOLS` | `AddLive` | `IsCharKind` | 2 |
| 729–739 | 11 | `DEVTOOLS` | `AddDPtr` | — | 2 |
| 739–750 | 12 | `DEVTOOLS` | `ScanObjOf` | — | 2 |
| 750–759 | 10 | `DEVTOOLS` | `BytesInImage` | — | 0 |
| 761–773 | 13 | `DEVTOOLS` | `Consider` | `AddDPtr`, `AddLive`, `HistAdd`, `Hop`, `IsHopKind` +2 | 0 |
| 773–791 | 19 | `DEVTOOLS` | `Hop` | `Consider`, `NameOf` | 0 |
| 791–851 | 61 | `DEVTOOLS` | `ScanEmbedded` | `AddDPtr`, `Consider`, `HistAdd`, `NameOf` | 4 |
| 851–874 | 24 | `DEVTOOLS` | `ScanRegionPtrs` | `Consider`, `WatchVt` | 0 |
| 874–880 | 7 | `RESEARCH` | `DumpHeader` | — | 2 |
| 880–943 | 64 | `RESEARCH` | `HuntHeapSingletons` | `Consider`, `HistAdd`, `IsSharedStub` | 2 |
| 943–957 | 15 | `DEVTOOLS` | `FollowJmp` | — | 0 |
| 957–974 | 18 | `DEVTOOLS` | `CreateFromFactory` | `FollowJmp` | 0 |
| 974–989 | 16 | `DEVTOOLS` | `ConsiderImm` | — | 0 |
| 989–1000 | 12 | `DEVTOOLS` | `IsMovToRegPtr` | — | 0 |
| 1000–1050 | 51 | `DEVTOOLS` | `DeriveOne` | `ConsiderImm`, `CreateFromFactory`, `IsMovToRegPtr` | 0 |
| 1050–1091 | 42 | `DEVTOOLS` | `ScanFuncForInst` | `ConsiderImm`, `IsMovToRegPtr` | 0 |
| 1091–1131 | 41 | `DEVTOOLS` | `DeriveInstanceVts` | `BuildWatch`, `DeriveOne`, `WatchAdd` | 4 |
| 1131–1145 | 15 | `DEVTOOLS` | `LooksAsciiPtr` | — | 0 |
| 1145–1161 | 17 | `DEVTOOLS` | `IsForeignFactory` | — | 2 |
| 1161–1310 | 150 | `DEVTOOLS` | `RescueFactory` | `CreateFromFactory`, `IsForeignFactory`, `LooksAsciiPtr` | 0 |
| 1310–1326 | 17 | `RESEARCH` | `CensusAdd` | — | 2 |
| 1326–1338 | 13 | `DEVTOOLS` | `NameVt` | `WatchVt` | 2 |
| 1338–1346 | 9 | `DEVTOOLS` | `CountMgrName` | — | 2 |
| 1346–1357 | 12 | `RESEARCH` | `HuntAddKey` | — | 4 |
| 1357–1377 | 21 | `RESEARCH` | `BuildHuntTable` | `BuildWatch`, `HuntAddKey` | 6 |
| 1377–1390 | 14 | `RESEARCH` | `HuntLookup` | — | 4 |
| 1390–1401 | 12 | `DEVTOOLS` | `AddHeapMgr` | `HistAdd` | 2 |
| 1401–1408 | 8 | `DEVTOOLS` | `CountGidOf` | — | 2 |
| 1408–1414 | 7 | `DEVTOOLS` | `CountGidVtOf` | — | 2 |
| 1414–1419 | 6 | `RESEARCH` | `CensusN` | — | 2 |
| 1419–1427 | 9 | `DEVTOOLS` | `SameVtNearby` | — | 0 |
| 1427–1494 | 68 | `DEVTOOLS` | `TryGidScout` | `CensusN`, `CountGidOf`, `CountGidVtOf`, `IsBannedInst`, `IsRepeatTag` +1 | 4 |
| 1504–1512 | 9 | `DEVTOOLS` | `FindDti` | — | 2 |
| 1512–1605 | 94 | `DEVTOOLS` | `ScanDti` | — | 2 |
| 1605–1613 | 9 | `DEVTOOLS` | `IsBannedInst` | — | 0 |
| 1613–1627 | 15 | `DEVTOOLS` | `AddCand` | `IsBannedInst` | 0 |
| 1627–1646 | 20 | `DEVTOOLS` | `HarvestFunc` | `AddCand`, `ScanFuncForInst` | 0 |
| 1646–1656 | 11 | `DEVTOOLS` | `CountCandInst` | — | 2 |
| 1656–1665 | 10 | `DEVTOOLS` | `GoldForName` | — | 0 |
| 1665–1678 | 14 | `DEVTOOLS` | `NearAnyDtiVt` | — | 2 |
| 1678–1703 | 26 | `DEVTOOLS` | `PickUniqueDti` | `CountCandInst`, `GoldForName`, `IsBannedInst`, `NearAnyDtiVt` | 2 |
| 1703–1743 | 41 | `DEVTOOLS` | `EnrichDti` | `HarvestFunc`, `PickUniqueDti` | 2 |
| 1743–1762 | 20 | `DEVTOOLS` | `ApplyDtiToFacts` | `FindDti` | 2 |
| 1762–1790 | 29 | `DEVTOOLS` | `ScanDtiLinks` | `LooksAsciiPtr` | 4 |
| 1790–1832 | 43 | `DEVTOOLS` | `WalkDtiTree` | `LooksAsciiPtr` | 4 |
| 1832–1854 | 23 | `DEVTOOLS` | `AddLead` | `NameOf` | 2 |
| 1854–1870 | 17 | `DEVTOOLS` | `TryBackActor` | `AddLead`, `IsBannedInst`, `NearAnyDtiVt` | 0 |
| 1870–1896 | 27 | `DEVTOOLS` | `CollectLeads` | `AddLead`, `TryBackActor` | 5 |
| 1896–1946 | 51 | `DEVTOOLS` | `ScanNearFactory` | `IsBannedInst` | 2 |
| 1946–1954 | 9 | `DEVTOOLS` | `InInstBand` | — | 0 |
| 1954–2034 | 81 | `DEVTOOLS` | `ScanTextWrites` | `InInstBand`, `IsBannedInst`, `IsMovToRegPtr` | 2 |
| 2034–2054 | 21 | `RESEARCH` | `DumpGidNodes` | `AddLead` | 4 |
| 2054–2089 | 36 | `RESEARCH` | `DumpGoldCtors` | `FindDti` | 2 |
| 2185–2297 | 113 | `RESEARCH` | `PartyWriteJson` | — | 1 |
| 2297–2304 | 8 | `DEVTOOLS` | `PartyRoleBody` | — | 0 |
| 2304–2312 | 9 | `DEVTOOLS` | `PartyFindPtrOffset` | — | 0 |
| 2312–2322 | 11 | `DEVTOOLS` | `PartyHashBytes` | — | 0 |
| 2322–2332 | 11 | `DEVTOOLS` | `PartyMainCmcInfo` | — | 0 |
| 2332–3069 | 738 | `RESEARCH` | `PartyWriteAiBridgeJson` | `PartyFindPtrOffset`, `PartyHashBytes`, `PartyMainCmcInfo`, `PartyRoleBody` | 4 |
| 3069–3080 | 12 | `RESEARCH` | `PartyTraceStop` | — | 3 |
| 3080–3123 | 44 | `RESEARCH` | `PartyTraceStart` | `PartyRoleBody`, `PartyTraceStop` | 5 |
| 3123–3138 | 16 | `RESEARCH` | `PartyTraceBody` | — | 0 |
| 3138–3146 | 9 | `RESEARCH` | `PartyTraceRecord` | — | 0 |
| 3146–3175 | 30 | `RESEARCH` | `PartyTraceTick` | `PartyRoleBody`, `PartyTraceBody`, `PartyTraceRecord` | 3 |
| 3175–3209 | 35 | `DEVTOOLS` | `PartyCollectIntentLinks` | — | 0 |
| 3209–3220 | 12 | `RESEARCH` | `PartyIntentTraceStop` | — | 2 |
| 3220–3244 | 25 | `RESEARCH` | `PartyIntentTraceStart` | `PartyIntentTraceStop` | 10 |
| 3244–3308 | 65 | `RESEARCH` | `PartyIntentTraceTick` | `PartyCollectIntentLinks`, `PartyIntentTraceStop`, `PartyRoleBody` | 12 |
| 3328–3345 | 18 | `RESEARCH` | `ProbeSidecars` | — | 2 |
| 3345–3355 | 11 | `RESEARCH` | `DumpActors` | — | 2 |
| 3355–3372 | 18 | `DEVTOOLS` | `DevTools::FirstEnemyBody` | — | 1 |
| 3372–3385 | 14 | `DEVTOOLS` | `DevTools::DeadCount` | — | 1 |
| 3385–3394 | 10 | `DEVTOOLS` | `DevTools::EnemyActAt` | — | 1 |
| 3394–3422 | 29 | `RESEARCH` | `DumpFactoryHeads` | `RescueFactory` | 2 |
| 3422–3442 | 21 | `DEVTOOLS` | `HopHeapMgrs` | `AddDPtr`, `AddLive`, `NameOf`, `TryGidScout` | 2 |
| 3442–3522 | 81 | `RESEARCH` | `HeapHunt` | `AddHeapMgr`, `AddLive`, `BuildHuntTable`, `CensusAdd`, `CountMgrName` +3 | 5 |
| 3522–3536 | 15 | `DEVTOOLS` | `NoteHolder` | — | 2 |
| 3536–3555 | 20 | `DEVTOOLS` | `ScanForLive` | `NoteHolder` | 3 |
| 3555–3591 | 37 | `DEVTOOLS` | `FindHolders` | `ScanForLive` | 9 |
| 3591–3601 | 11 | `RESEARCH` | `DumpWindow` | — | 3 |
| 3601–3919 | 319 | `RESEARCH` | `WriteDumpJson` | `NameVt` | 64 |
| 3919–3956 | 38 | `DEVTOOLS` | `FilterFakeLives` | — | 4 |
| 3956–4097 | 142 | `RESEARCH` | `HuntLive` | `AddLead`, `ApplyDtiToFacts`, `BuildWatch`, `BytesInImage`, `CollectLeads` +24 | 20 |
| 4097–4216 | 120 | `RESEARCH` | `DumpAnatomy` | `BuildWatch`, `BytesInImage`, `Consider`, `DumpHeader`, `DumpWindow` +7 | 48 |
| 4224–4242 | 19 | `RESEARCH` | `HexDump` | — | 0 |
| 4242–4248 | 7 | `DEVTOOLS` | `SetInspect` | — | 2 |
| 4248–5099 | 852 | `DEVTOOLS` | `RenderDevToolsUI` | `DeadCount`, `DumpAnatomy`, `HexDump`, `HuntLive`, `KindName` +7 | 72 |
| 5099–5108 | 10 | `DEVTOOLS` | `ResearchOnSnapshotEarly` | `PartyWriteJson` | 1 |
| 5108–5114 | 7 | `DEVTOOLS` | `ResearchOnSnapshotFull` | `PartyIntentTraceStart`, `PartyWriteAiBridgeJson` | 2 |
| 5114–5120 | 7 | `DEVTOOLS` | `ResearchOnTick` | `PartyIntentTraceTick`, `PartyTraceTick` | 0 |
| 5120–5125 | 6 | `DEVTOOLS` | `ResearchOnWorldUnload` | `PartyIntentTraceStop` | 0 |
| 5125–5164 | 40 | `DEVTOOLS` | `Hooks::DevTools` | `BuildWatch` | 3 |
| 5164–5171 | 8 | `DEVTOOLS` | `Hooks::DevTools_Shutdown` | `PartyIntentTraceStop` | 0 |

## Предлагаемая раскладка `src/runtime/`

Автогруппировка продуктового кода по именам. Это заготовка шагов 1–3 `REFACTOR_TASK.md`, а не догма — спорные функции видно в таблице ниже.

| Файл | Функций | Строк | Что внутри |
|---|---:|---:|---|
