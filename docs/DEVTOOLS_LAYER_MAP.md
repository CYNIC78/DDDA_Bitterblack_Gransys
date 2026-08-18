# Карта слоёв `src/devtools/DevTools.cpp`

> Сгенерировано `tools/analyze_devtools_layers.py`. Руками не править.

Файл: **4882 строк**, разобрано **112 функций** верхнего уровня (**4432 строк** в телах), **107 файловых статиков**.

## Слои

| Слой | Что это | Функций | Строк |
|---|---|---:|---:|
| `PRODUCT-API` | зовётся продуктовыми модулями напрямую → `src/runtime/` | 0 | 0 |
| `PRODUCT-DEP` | нужна продукту транзитивно → `src/runtime/` | 0 | 0 |
| `PASSENGER` | research-дамп в продуктовом тике → место разреза, в runtime не едет | 0 | 0 |
| `PROBE-API` | research-кнопка в продуктовом UI → вырезать из `PawnAI.cpp` | 0 | 0 |
| `PROBE-DEP` | живёт только ради проб → уйдёт вместе с ними | 0 | 0 |
| `RESEARCH` | пробы/аудиты/дампы → под `researchDump` или под нож | 30 | 2064 |
| `DEVTOOLS` | инфраструктура DevTools → остаётся в `src/devtools/` | 82 | 2368 |

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
| 127–143 | 17 | `DEVTOOLS` | `IsRepeatTag` | — | 0 |
| 151–162 | 12 | `DEVTOOLS` | `KindName` | — | 0 |
| 162–186 | 25 | `RESEARCH` | `ProbeType` | — | 0 |
| 186–188 | 3 | `DEVTOOLS` | `DevTools::ModuleBase` | — | 0 |
| 188–189 | 2 | `DEVTOOLS` | `DevTools::Rebase` | — | 0 |
| 191–201 | 11 | `DEVTOOLS` | `DevTools::Identify` | — | 0 |
| 227–232 | 6 | `DEVTOOLS` | `IsSharedStub` | — | 0 |
| 232–239 | 8 | `DEVTOOLS` | `AddHit` | — | 0 |
| 239–311 | 73 | `DEVTOOLS` | `ScanImage` | `AddHit`, `IsSharedStub` | 4 |
| 311–337 | 27 | `RESEARCH` | `WriteScanJson` | — | 3 |
| 349–359 | 11 | `DEVTOOLS` | `WatchAdd` | — | 2 |
| 359–380 | 22 | `DEVTOOLS` | `KindForName` | — | 0 |
| 380–408 | 29 | `DEVTOOLS` | `BuildWatch` | `KindForName`, `WatchAdd` | 1 |
| 408–415 | 8 | `DEVTOOLS` | `WatchVt` | — | 2 |
| 422–442 | 21 | `DEVTOOLS` | `NameOf` | `KindForName`, `WatchVt` | 0 |
| 442–444 | 3 | `DEVTOOLS` | `IsCharKind` | — | 0 |
| 444–449 | 6 | `DEVTOOLS` | `IsHopKind` | — | 0 |
| 692–700 | 9 | `DEVTOOLS` | `HistAdd` | — | 2 |
| 700–707 | 8 | `DEVTOOLS` | `Seen` | — | 2 |
| 707–727 | 21 | `DEVTOOLS` | `AddLive` | `IsCharKind` | 2 |
| 727–737 | 11 | `DEVTOOLS` | `AddDPtr` | — | 2 |
| 737–748 | 12 | `DEVTOOLS` | `ScanObjOf` | — | 2 |
| 748–757 | 10 | `DEVTOOLS` | `BytesInImage` | — | 0 |
| 759–771 | 13 | `DEVTOOLS` | `Consider` | `AddDPtr`, `AddLive`, `HistAdd`, `Hop`, `IsHopKind` +2 | 0 |
| 771–789 | 19 | `DEVTOOLS` | `Hop` | `Consider`, `NameOf` | 0 |
| 789–849 | 61 | `DEVTOOLS` | `ScanEmbedded` | `AddDPtr`, `Consider`, `HistAdd`, `NameOf` | 4 |
| 849–872 | 24 | `DEVTOOLS` | `ScanRegionPtrs` | `Consider`, `WatchVt` | 0 |
| 872–878 | 7 | `RESEARCH` | `DumpHeader` | — | 2 |
| 878–941 | 64 | `RESEARCH` | `HuntHeapSingletons` | `Consider`, `HistAdd`, `IsSharedStub` | 2 |
| 941–955 | 15 | `DEVTOOLS` | `FollowJmp` | — | 0 |
| 955–972 | 18 | `DEVTOOLS` | `CreateFromFactory` | `FollowJmp` | 0 |
| 972–987 | 16 | `DEVTOOLS` | `ConsiderImm` | — | 0 |
| 987–998 | 12 | `DEVTOOLS` | `IsMovToRegPtr` | — | 0 |
| 998–1048 | 51 | `DEVTOOLS` | `DeriveOne` | `ConsiderImm`, `CreateFromFactory`, `IsMovToRegPtr` | 0 |
| 1048–1089 | 42 | `DEVTOOLS` | `ScanFuncForInst` | `ConsiderImm`, `IsMovToRegPtr` | 0 |
| 1089–1129 | 41 | `DEVTOOLS` | `DeriveInstanceVts` | `BuildWatch`, `DeriveOne`, `WatchAdd` | 4 |
| 1129–1143 | 15 | `DEVTOOLS` | `LooksAsciiPtr` | — | 0 |
| 1143–1159 | 17 | `DEVTOOLS` | `IsForeignFactory` | — | 2 |
| 1159–1308 | 150 | `DEVTOOLS` | `RescueFactory` | `CreateFromFactory`, `IsForeignFactory`, `LooksAsciiPtr` | 0 |
| 1308–1324 | 17 | `RESEARCH` | `CensusAdd` | — | 2 |
| 1324–1336 | 13 | `DEVTOOLS` | `NameVt` | `WatchVt` | 2 |
| 1336–1344 | 9 | `DEVTOOLS` | `CountMgrName` | — | 2 |
| 1344–1355 | 12 | `RESEARCH` | `HuntAddKey` | — | 4 |
| 1355–1375 | 21 | `RESEARCH` | `BuildHuntTable` | `BuildWatch`, `HuntAddKey` | 6 |
| 1375–1388 | 14 | `RESEARCH` | `HuntLookup` | — | 4 |
| 1388–1399 | 12 | `DEVTOOLS` | `AddHeapMgr` | `HistAdd` | 2 |
| 1399–1406 | 8 | `DEVTOOLS` | `CountGidOf` | — | 2 |
| 1406–1412 | 7 | `DEVTOOLS` | `CountGidVtOf` | — | 2 |
| 1412–1417 | 6 | `RESEARCH` | `CensusN` | — | 2 |
| 1417–1425 | 9 | `DEVTOOLS` | `SameVtNearby` | — | 0 |
| 1425–1492 | 68 | `DEVTOOLS` | `TryGidScout` | `CensusN`, `CountGidOf`, `CountGidVtOf`, `IsBannedInst`, `IsRepeatTag` +1 | 4 |
| 1502–1510 | 9 | `DEVTOOLS` | `FindDti` | — | 2 |
| 1510–1603 | 94 | `DEVTOOLS` | `ScanDti` | — | 2 |
| 1603–1611 | 9 | `DEVTOOLS` | `IsBannedInst` | — | 0 |
| 1611–1625 | 15 | `DEVTOOLS` | `AddCand` | `IsBannedInst` | 0 |
| 1625–1644 | 20 | `DEVTOOLS` | `HarvestFunc` | `AddCand`, `ScanFuncForInst` | 0 |
| 1644–1654 | 11 | `DEVTOOLS` | `CountCandInst` | — | 2 |
| 1654–1663 | 10 | `DEVTOOLS` | `GoldForName` | — | 0 |
| 1663–1676 | 14 | `DEVTOOLS` | `NearAnyDtiVt` | — | 2 |
| 1676–1701 | 26 | `DEVTOOLS` | `PickUniqueDti` | `CountCandInst`, `GoldForName`, `IsBannedInst`, `NearAnyDtiVt` | 2 |
| 1701–1741 | 41 | `DEVTOOLS` | `EnrichDti` | `HarvestFunc`, `PickUniqueDti` | 2 |
| 1741–1760 | 20 | `DEVTOOLS` | `ApplyDtiToFacts` | `FindDti` | 2 |
| 1760–1788 | 29 | `DEVTOOLS` | `ScanDtiLinks` | `LooksAsciiPtr` | 4 |
| 1788–1830 | 43 | `DEVTOOLS` | `WalkDtiTree` | `LooksAsciiPtr` | 4 |
| 1830–1852 | 23 | `DEVTOOLS` | `AddLead` | `NameOf` | 2 |
| 1852–1868 | 17 | `DEVTOOLS` | `TryBackActor` | `AddLead`, `IsBannedInst`, `NearAnyDtiVt` | 0 |
| 1868–1894 | 27 | `DEVTOOLS` | `CollectLeads` | `AddLead`, `TryBackActor` | 5 |
| 1894–1944 | 51 | `DEVTOOLS` | `ScanNearFactory` | `IsBannedInst` | 2 |
| 1944–1952 | 9 | `DEVTOOLS` | `InInstBand` | — | 0 |
| 1952–2032 | 81 | `DEVTOOLS` | `ScanTextWrites` | `InInstBand`, `IsBannedInst`, `IsMovToRegPtr` | 2 |
| 2032–2052 | 21 | `RESEARCH` | `DumpGidNodes` | `AddLead` | 4 |
| 2052–2087 | 36 | `RESEARCH` | `DumpGoldCtors` | `FindDti` | 2 |
| 2183–2295 | 113 | `RESEARCH` | `PartyWriteJson` | — | 1 |
| 2295–2302 | 8 | `DEVTOOLS` | `PartyRoleBody` | — | 0 |
| 2302–2310 | 9 | `DEVTOOLS` | `PartyFindPtrOffset` | — | 0 |
| 2310–2320 | 11 | `DEVTOOLS` | `PartyHashBytes` | — | 0 |
| 2320–2330 | 11 | `DEVTOOLS` | `PartyMainCmcInfo` | — | 0 |
| 2330–3067 | 738 | `RESEARCH` | `PartyWriteAiBridgeJson` | `PartyFindPtrOffset`, `PartyHashBytes`, `PartyMainCmcInfo`, `PartyRoleBody` | 4 |
| 3067–3078 | 12 | `RESEARCH` | `PartyTraceStop` | — | 3 |
| 3078–3121 | 44 | `RESEARCH` | `PartyTraceStart` | `PartyRoleBody`, `PartyTraceStop` | 5 |
| 3121–3136 | 16 | `RESEARCH` | `PartyTraceBody` | — | 0 |
| 3136–3144 | 9 | `RESEARCH` | `PartyTraceRecord` | — | 0 |
| 3144–3173 | 30 | `RESEARCH` | `PartyTraceTick` | `PartyRoleBody`, `PartyTraceBody`, `PartyTraceRecord` | 3 |
| 3173–3207 | 35 | `DEVTOOLS` | `PartyCollectIntentLinks` | — | 0 |
| 3207–3218 | 12 | `RESEARCH` | `PartyIntentTraceStop` | — | 2 |
| 3218–3242 | 25 | `RESEARCH` | `PartyIntentTraceStart` | `PartyIntentTraceStop` | 10 |
| 3242–3306 | 65 | `RESEARCH` | `PartyIntentTraceTick` | `PartyCollectIntentLinks`, `PartyIntentTraceStop`, `PartyRoleBody` | 12 |
| 3326–3343 | 18 | `RESEARCH` | `ProbeSidecars` | — | 2 |
| 3343–3353 | 11 | `RESEARCH` | `DumpActors` | — | 2 |
| 3353–3370 | 18 | `DEVTOOLS` | `DevTools::FirstEnemyBody` | — | 1 |
| 3370–3383 | 14 | `DEVTOOLS` | `DevTools::DeadCount` | — | 1 |
| 3383–3392 | 10 | `DEVTOOLS` | `DevTools::EnemyActAt` | — | 1 |
| 3392–3420 | 29 | `RESEARCH` | `DumpFactoryHeads` | `RescueFactory` | 2 |
| 3420–3440 | 21 | `DEVTOOLS` | `HopHeapMgrs` | `AddDPtr`, `AddLive`, `NameOf`, `TryGidScout` | 2 |
| 3440–3520 | 81 | `RESEARCH` | `HeapHunt` | `AddHeapMgr`, `AddLive`, `BuildHuntTable`, `CensusAdd`, `CountMgrName` +3 | 5 |
| 3520–3534 | 15 | `DEVTOOLS` | `NoteHolder` | — | 2 |
| 3534–3553 | 20 | `DEVTOOLS` | `ScanForLive` | `NoteHolder` | 3 |
| 3553–3589 | 37 | `DEVTOOLS` | `FindHolders` | `ScanForLive` | 9 |
| 3589–3599 | 11 | `RESEARCH` | `DumpWindow` | — | 3 |
| 3599–3917 | 319 | `RESEARCH` | `WriteDumpJson` | `NameVt` | 64 |
| 3917–3954 | 38 | `DEVTOOLS` | `FilterFakeLives` | — | 4 |
| 3954–4095 | 142 | `RESEARCH` | `HuntLive` | `AddLead`, `ApplyDtiToFacts`, `BuildWatch`, `BytesInImage`, `CollectLeads` +24 | 20 |
| 4095–4214 | 120 | `RESEARCH` | `DumpAnatomy` | `BuildWatch`, `BytesInImage`, `Consider`, `DumpHeader`, `DumpWindow` +7 | 48 |
| 4222–4240 | 19 | `RESEARCH` | `HexDump` | — | 0 |
| 4240–4246 | 7 | `DEVTOOLS` | `SetInspect` | — | 2 |
| 4246–4809 | 564 | `DEVTOOLS` | `RenderDevToolsUI` | `DeadCount`, `DumpAnatomy`, `HexDump`, `HuntLive`, `KindName` +7 | 72 |
| 4809–4818 | 10 | `DEVTOOLS` | `ResearchOnSnapshotEarly` | `PartyWriteJson` | 1 |
| 4818–4824 | 7 | `DEVTOOLS` | `ResearchOnSnapshotFull` | `PartyIntentTraceStart`, `PartyWriteAiBridgeJson` | 2 |
| 4824–4830 | 7 | `DEVTOOLS` | `ResearchOnTick` | `PartyIntentTraceTick`, `PartyTraceTick` | 0 |
| 4830–4835 | 6 | `DEVTOOLS` | `ResearchOnWorldUnload` | `PartyIntentTraceStop` | 0 |
| 4835–4874 | 40 | `DEVTOOLS` | `Hooks::DevTools` | `BuildWatch` | 3 |
| 4874–4881 | 8 | `DEVTOOLS` | `Hooks::DevTools_Shutdown` | `PartyIntentTraceStop` | 0 |

## Предлагаемая раскладка `src/runtime/`

Автогруппировка продуктового кода по именам. Это заготовка шагов 1–3 `REFACTOR_TASK.md`, а не догма — спорные функции видно в таблице ниже.

| Файл | Функций | Строк | Что внутри |
|---|---:|---:|---|
