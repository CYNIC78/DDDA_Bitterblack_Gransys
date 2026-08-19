# Карта слоёв `src/devtools/DevTools.cpp`

> Сгенерировано `tools/analyze_devtools_layers.py`. Руками не править.

Файл: **5079 строк**, разобрано **112 функций** верхнего уровня (**4628 строк** в телах), **107 файловых статиков**.

## Слои

| Слой | Что это | Функций | Строк |
|---|---|---:|---:|
| `PRODUCT-API` | зовётся продуктовыми модулями напрямую → `src/runtime/` | 0 | 0 |
| `PRODUCT-DEP` | нужна продукту транзитивно → `src/runtime/` | 0 | 0 |
| `PASSENGER` | research-дамп в продуктовом тике → место разреза, в runtime не едет | 0 | 0 |
| `PROBE-API` | research-кнопка в продуктовом UI → вырезать из `PawnAI.cpp` | 0 | 0 |
| `PROBE-DEP` | живёт только ради проб → уйдёт вместе с ними | 0 | 0 |
| `RESEARCH` | пробы/аудиты/дампы → под `researchDump` или под нож | 30 | 2064 |
| `DEVTOOLS` | инфраструктура DevTools → остаётся в `src/devtools/` | 82 | 2564 |

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
| 128–144 | 17 | `DEVTOOLS` | `IsRepeatTag` | — | 0 |
| 152–163 | 12 | `DEVTOOLS` | `KindName` | — | 0 |
| 163–187 | 25 | `RESEARCH` | `ProbeType` | — | 0 |
| 187–189 | 3 | `DEVTOOLS` | `DevTools::ModuleBase` | — | 0 |
| 189–190 | 2 | `DEVTOOLS` | `DevTools::Rebase` | — | 0 |
| 192–202 | 11 | `DEVTOOLS` | `DevTools::Identify` | — | 0 |
| 228–233 | 6 | `DEVTOOLS` | `IsSharedStub` | — | 0 |
| 233–240 | 8 | `DEVTOOLS` | `AddHit` | — | 0 |
| 240–312 | 73 | `DEVTOOLS` | `ScanImage` | `AddHit`, `IsSharedStub` | 4 |
| 312–338 | 27 | `RESEARCH` | `WriteScanJson` | — | 3 |
| 350–360 | 11 | `DEVTOOLS` | `WatchAdd` | — | 2 |
| 360–381 | 22 | `DEVTOOLS` | `KindForName` | — | 0 |
| 381–409 | 29 | `DEVTOOLS` | `BuildWatch` | `KindForName`, `WatchAdd` | 1 |
| 409–416 | 8 | `DEVTOOLS` | `WatchVt` | — | 2 |
| 423–443 | 21 | `DEVTOOLS` | `NameOf` | `KindForName`, `WatchVt` | 0 |
| 443–445 | 3 | `DEVTOOLS` | `IsCharKind` | — | 0 |
| 445–450 | 6 | `DEVTOOLS` | `IsHopKind` | — | 0 |
| 693–701 | 9 | `DEVTOOLS` | `HistAdd` | — | 2 |
| 701–708 | 8 | `DEVTOOLS` | `Seen` | — | 2 |
| 708–728 | 21 | `DEVTOOLS` | `AddLive` | `IsCharKind` | 2 |
| 728–738 | 11 | `DEVTOOLS` | `AddDPtr` | — | 2 |
| 738–749 | 12 | `DEVTOOLS` | `ScanObjOf` | — | 2 |
| 749–758 | 10 | `DEVTOOLS` | `BytesInImage` | — | 0 |
| 760–772 | 13 | `DEVTOOLS` | `Consider` | `AddDPtr`, `AddLive`, `HistAdd`, `Hop`, `IsHopKind` +2 | 0 |
| 772–790 | 19 | `DEVTOOLS` | `Hop` | `Consider`, `NameOf` | 0 |
| 790–850 | 61 | `DEVTOOLS` | `ScanEmbedded` | `AddDPtr`, `Consider`, `HistAdd`, `NameOf` | 4 |
| 850–873 | 24 | `DEVTOOLS` | `ScanRegionPtrs` | `Consider`, `WatchVt` | 0 |
| 873–879 | 7 | `RESEARCH` | `DumpHeader` | — | 2 |
| 879–942 | 64 | `RESEARCH` | `HuntHeapSingletons` | `Consider`, `HistAdd`, `IsSharedStub` | 2 |
| 942–956 | 15 | `DEVTOOLS` | `FollowJmp` | — | 0 |
| 956–973 | 18 | `DEVTOOLS` | `CreateFromFactory` | `FollowJmp` | 0 |
| 973–988 | 16 | `DEVTOOLS` | `ConsiderImm` | — | 0 |
| 988–999 | 12 | `DEVTOOLS` | `IsMovToRegPtr` | — | 0 |
| 999–1049 | 51 | `DEVTOOLS` | `DeriveOne` | `ConsiderImm`, `CreateFromFactory`, `IsMovToRegPtr` | 0 |
| 1049–1090 | 42 | `DEVTOOLS` | `ScanFuncForInst` | `ConsiderImm`, `IsMovToRegPtr` | 0 |
| 1090–1130 | 41 | `DEVTOOLS` | `DeriveInstanceVts` | `BuildWatch`, `DeriveOne`, `WatchAdd` | 4 |
| 1130–1144 | 15 | `DEVTOOLS` | `LooksAsciiPtr` | — | 0 |
| 1144–1160 | 17 | `DEVTOOLS` | `IsForeignFactory` | — | 2 |
| 1160–1309 | 150 | `DEVTOOLS` | `RescueFactory` | `CreateFromFactory`, `IsForeignFactory`, `LooksAsciiPtr` | 0 |
| 1309–1325 | 17 | `RESEARCH` | `CensusAdd` | — | 2 |
| 1325–1337 | 13 | `DEVTOOLS` | `NameVt` | `WatchVt` | 2 |
| 1337–1345 | 9 | `DEVTOOLS` | `CountMgrName` | — | 2 |
| 1345–1356 | 12 | `RESEARCH` | `HuntAddKey` | — | 4 |
| 1356–1376 | 21 | `RESEARCH` | `BuildHuntTable` | `BuildWatch`, `HuntAddKey` | 6 |
| 1376–1389 | 14 | `RESEARCH` | `HuntLookup` | — | 4 |
| 1389–1400 | 12 | `DEVTOOLS` | `AddHeapMgr` | `HistAdd` | 2 |
| 1400–1407 | 8 | `DEVTOOLS` | `CountGidOf` | — | 2 |
| 1407–1413 | 7 | `DEVTOOLS` | `CountGidVtOf` | — | 2 |
| 1413–1418 | 6 | `RESEARCH` | `CensusN` | — | 2 |
| 1418–1426 | 9 | `DEVTOOLS` | `SameVtNearby` | — | 0 |
| 1426–1493 | 68 | `DEVTOOLS` | `TryGidScout` | `CensusN`, `CountGidOf`, `CountGidVtOf`, `IsBannedInst`, `IsRepeatTag` +1 | 4 |
| 1503–1511 | 9 | `DEVTOOLS` | `FindDti` | — | 2 |
| 1511–1604 | 94 | `DEVTOOLS` | `ScanDti` | — | 2 |
| 1604–1612 | 9 | `DEVTOOLS` | `IsBannedInst` | — | 0 |
| 1612–1626 | 15 | `DEVTOOLS` | `AddCand` | `IsBannedInst` | 0 |
| 1626–1645 | 20 | `DEVTOOLS` | `HarvestFunc` | `AddCand`, `ScanFuncForInst` | 0 |
| 1645–1655 | 11 | `DEVTOOLS` | `CountCandInst` | — | 2 |
| 1655–1664 | 10 | `DEVTOOLS` | `GoldForName` | — | 0 |
| 1664–1677 | 14 | `DEVTOOLS` | `NearAnyDtiVt` | — | 2 |
| 1677–1702 | 26 | `DEVTOOLS` | `PickUniqueDti` | `CountCandInst`, `GoldForName`, `IsBannedInst`, `NearAnyDtiVt` | 2 |
| 1702–1742 | 41 | `DEVTOOLS` | `EnrichDti` | `HarvestFunc`, `PickUniqueDti` | 2 |
| 1742–1761 | 20 | `DEVTOOLS` | `ApplyDtiToFacts` | `FindDti` | 2 |
| 1761–1789 | 29 | `DEVTOOLS` | `ScanDtiLinks` | `LooksAsciiPtr` | 4 |
| 1789–1831 | 43 | `DEVTOOLS` | `WalkDtiTree` | `LooksAsciiPtr` | 4 |
| 1831–1853 | 23 | `DEVTOOLS` | `AddLead` | `NameOf` | 2 |
| 1853–1869 | 17 | `DEVTOOLS` | `TryBackActor` | `AddLead`, `IsBannedInst`, `NearAnyDtiVt` | 0 |
| 1869–1895 | 27 | `DEVTOOLS` | `CollectLeads` | `AddLead`, `TryBackActor` | 5 |
| 1895–1945 | 51 | `DEVTOOLS` | `ScanNearFactory` | `IsBannedInst` | 2 |
| 1945–1953 | 9 | `DEVTOOLS` | `InInstBand` | — | 0 |
| 1953–2033 | 81 | `DEVTOOLS` | `ScanTextWrites` | `InInstBand`, `IsBannedInst`, `IsMovToRegPtr` | 2 |
| 2033–2053 | 21 | `RESEARCH` | `DumpGidNodes` | `AddLead` | 4 |
| 2053–2088 | 36 | `RESEARCH` | `DumpGoldCtors` | `FindDti` | 2 |
| 2184–2296 | 113 | `RESEARCH` | `PartyWriteJson` | — | 1 |
| 2296–2303 | 8 | `DEVTOOLS` | `PartyRoleBody` | — | 0 |
| 2303–2311 | 9 | `DEVTOOLS` | `PartyFindPtrOffset` | — | 0 |
| 2311–2321 | 11 | `DEVTOOLS` | `PartyHashBytes` | — | 0 |
| 2321–2331 | 11 | `DEVTOOLS` | `PartyMainCmcInfo` | — | 0 |
| 2331–3068 | 738 | `RESEARCH` | `PartyWriteAiBridgeJson` | `PartyFindPtrOffset`, `PartyHashBytes`, `PartyMainCmcInfo`, `PartyRoleBody` | 4 |
| 3068–3079 | 12 | `RESEARCH` | `PartyTraceStop` | — | 3 |
| 3079–3122 | 44 | `RESEARCH` | `PartyTraceStart` | `PartyRoleBody`, `PartyTraceStop` | 5 |
| 3122–3137 | 16 | `RESEARCH` | `PartyTraceBody` | — | 0 |
| 3137–3145 | 9 | `RESEARCH` | `PartyTraceRecord` | — | 0 |
| 3145–3174 | 30 | `RESEARCH` | `PartyTraceTick` | `PartyRoleBody`, `PartyTraceBody`, `PartyTraceRecord` | 3 |
| 3174–3208 | 35 | `DEVTOOLS` | `PartyCollectIntentLinks` | — | 0 |
| 3208–3219 | 12 | `RESEARCH` | `PartyIntentTraceStop` | — | 2 |
| 3219–3243 | 25 | `RESEARCH` | `PartyIntentTraceStart` | `PartyIntentTraceStop` | 10 |
| 3243–3307 | 65 | `RESEARCH` | `PartyIntentTraceTick` | `PartyCollectIntentLinks`, `PartyIntentTraceStop`, `PartyRoleBody` | 12 |
| 3327–3344 | 18 | `RESEARCH` | `ProbeSidecars` | — | 2 |
| 3344–3354 | 11 | `RESEARCH` | `DumpActors` | — | 2 |
| 3354–3371 | 18 | `DEVTOOLS` | `DevTools::FirstEnemyBody` | — | 1 |
| 3371–3384 | 14 | `DEVTOOLS` | `DevTools::DeadCount` | — | 1 |
| 3384–3393 | 10 | `DEVTOOLS` | `DevTools::EnemyActAt` | — | 1 |
| 3393–3421 | 29 | `RESEARCH` | `DumpFactoryHeads` | `RescueFactory` | 2 |
| 3421–3441 | 21 | `DEVTOOLS` | `HopHeapMgrs` | `AddDPtr`, `AddLive`, `NameOf`, `TryGidScout` | 2 |
| 3441–3521 | 81 | `RESEARCH` | `HeapHunt` | `AddHeapMgr`, `AddLive`, `BuildHuntTable`, `CensusAdd`, `CountMgrName` +3 | 5 |
| 3521–3535 | 15 | `DEVTOOLS` | `NoteHolder` | — | 2 |
| 3535–3554 | 20 | `DEVTOOLS` | `ScanForLive` | `NoteHolder` | 3 |
| 3554–3590 | 37 | `DEVTOOLS` | `FindHolders` | `ScanForLive` | 9 |
| 3590–3600 | 11 | `RESEARCH` | `DumpWindow` | — | 3 |
| 3600–3918 | 319 | `RESEARCH` | `WriteDumpJson` | `NameVt` | 64 |
| 3918–3955 | 38 | `DEVTOOLS` | `FilterFakeLives` | — | 4 |
| 3955–4096 | 142 | `RESEARCH` | `HuntLive` | `AddLead`, `ApplyDtiToFacts`, `BuildWatch`, `BytesInImage`, `CollectLeads` +24 | 20 |
| 4096–4215 | 120 | `RESEARCH` | `DumpAnatomy` | `BuildWatch`, `BytesInImage`, `Consider`, `DumpHeader`, `DumpWindow` +7 | 48 |
| 4223–4241 | 19 | `RESEARCH` | `HexDump` | — | 0 |
| 4241–4247 | 7 | `DEVTOOLS` | `SetInspect` | — | 2 |
| 4247–5006 | 760 | `DEVTOOLS` | `RenderDevToolsUI` | `DeadCount`, `DumpAnatomy`, `HexDump`, `HuntLive`, `KindName` +7 | 72 |
| 5006–5015 | 10 | `DEVTOOLS` | `ResearchOnSnapshotEarly` | `PartyWriteJson` | 1 |
| 5015–5021 | 7 | `DEVTOOLS` | `ResearchOnSnapshotFull` | `PartyIntentTraceStart`, `PartyWriteAiBridgeJson` | 2 |
| 5021–5027 | 7 | `DEVTOOLS` | `ResearchOnTick` | `PartyIntentTraceTick`, `PartyTraceTick` | 0 |
| 5027–5032 | 6 | `DEVTOOLS` | `ResearchOnWorldUnload` | `PartyIntentTraceStop` | 0 |
| 5032–5071 | 40 | `DEVTOOLS` | `Hooks::DevTools` | `BuildWatch` | 3 |
| 5071–5078 | 8 | `DEVTOOLS` | `Hooks::DevTools_Shutdown` | `PartyIntentTraceStop` | 0 |

## Предлагаемая раскладка `src/runtime/`

Автогруппировка продуктового кода по именам. Это заготовка шагов 1–3 `REFACTOR_TASK.md`, а не догма — спорные функции видно в таблице ниже.

| Файл | Функций | Строк | Что внутри |
|---|---:|---:|---|
