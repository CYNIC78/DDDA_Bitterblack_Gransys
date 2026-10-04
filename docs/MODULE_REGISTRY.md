# MODULE_REGISTRY — реестр модулей (генерируется)

> Сгенерировано `python3 tools/module_registry.py --md docs/MODULE_REGISTRY.md`.
> Смысл: у каждого файла кода есть машинно проверенная точка подключения — либо
> входящий вызов, либо строка в списке исключений с причиной. Шаг сборки `10f/10`
> следит, чтобы реестр не разошёлся с кодом.

Подключение модуля — три ручных шага (`ARCHITECTURE.md` §5.1 и §8): `#include` →
вызов в цепочке/оркестраторе → строка в `.vcxproj`. Реестр показывает, что второй
шаг у каждого файла есть; первый и третий проверяются машиной в том же прогоне.

## Сводка по слоям

| Слой | Файлов кода |
|---|---|
| `src/(корень)` | 28 |
| `src/audio` | 2 |
| `src/devtools` | 9 |
| `src/monsterai` | 9 |
| `src/pawnai` | 32 |
| `src/runtime` | 19 |

## Точки подключения

| Файл | Подключён через |
|---|---|
| `CameraPlus.cpp` | **точка входа** `dinput8.cpp`; вызывается из `TargetLock.cpp` |
| `CombatIntel.cpp` | **цепочка** `UpdatePawnAI`: `CombatIntel_Tick()`; **точка входа** `dinput8.cpp`; вызывается из `PawnAI.cpp`; вызывается из `devtools/DevTools.cpp`; вызывается из `devtools/GoapProbe.cpp`; и ещё 4 .cpp |
| `EnemyAI.cpp` | **точка входа** `dinput8.cpp` |
| `EnemyTuner.cpp` | **цепочка** `UpdatePawnAI`: `EnemyTuner::Tick()`; вызывается из `PawnAI.cpp`; вызывается из `devtools/AnimProbe.cpp`; вызывается из `devtools/DevTools.cpp`; и ещё 12 .cpp |
| `EntityConfig.cpp` | **цепочка** `UpdatePawnAI`: `EntityCfg::Tick()`; **точка входа** `dinput8.cpp`; вызывается из `EnemyTuner.cpp`; вызывается из `PawnAI.cpp`; вызывается из `monsterai/MonsterDirector.cpp` |
| `Nightmare.cpp` | **точка входа** `dinput8.cpp` |
| `PawnAI.cpp` | **цепочка** `UpdatePawnAI`: `HiredInclSelfTestTick()`, `ProductWorldUnload()`; **точка входа** `dinput8.cpp`; вызывается из `CombatIntel.cpp`; вызывается из `Nightmare.cpp`; вызывается из `devtools/GoapProbe.cpp`; и ещё 16 .cpp |
| `TargetLock.cpp` | **точка входа** `dinput8.cpp` |
| `audio/AudioRedirect.cpp` | **точка входа** `dinput8.cpp`; вызывается из `CameraPlus.cpp`; вызывается из `EntityConfig.cpp`; вызывается из `PawnAI.cpp`; и ещё 3 .cpp |
| `devtools/AnimProbe.cpp` | вызывается из `devtools/DevTools.cpp`; вызывается из `pawnai/NexusDoctrine.cpp`; вызывается из `pawnai/WandRange.cpp`; и ещё 1 .cpp |
| `devtools/DevTools.cpp` | **точка входа** `dinput8.cpp`; вызывается из `CombatIntel.cpp`; вызывается из `EnemyAI.cpp`; вызывается из `EnemyTuner.cpp`; и ещё 9 .cpp |
| `devtools/GoapProbe.cpp` | вызывается из `devtools/DevTools.cpp`; вызывается из `monsterai/PackObserve.cpp`; вызывается из `pawnai/GuardianDoctrine.cpp`; и ещё 1 .cpp |
| `dinput8.cpp` | вызывается из `CameraPlus.cpp`; вызывается из `CombatIntel.cpp`; вызывается из `EnemyAI.cpp`; и ещё 20 .cpp |
| `monsterai/MonsterDirector.cpp` | **цепочка** `UpdatePawnAI`: `MonsterAI::Tick()`, `MonsterAI::OnWorldUnload()`; **точка входа** `dinput8.cpp`; вызывается из `EnemyAI.cpp`; вызывается из `EnemyTuner.cpp`; вызывается из `PawnAI.cpp`; и ещё 3 .cpp |
| `monsterai/PackObserve.cpp` | **цепочка** `UpdatePawnAI`: `MonsterAI::PackObserveTick()`; вызывается из `EnemyAI.cpp`; вызывается из `PawnAI.cpp`; вызывается из `monsterai/MonsterDirector.cpp`; и ещё 1 .cpp |
| `monsterai/SpeciesTuning.cpp` | вызывается из `monsterai/MonsterDirector.cpp` |
| `monsterai/TacticalCues.cpp` | вызывается из `monsterai/MonsterDirector.cpp` |
| `pawnai/AcquisitorManager.cpp` | **оркестратор**: поле `AcquisitorManager` в `PawnAI_BusOrchestrator.h`; вызывается из `PawnAI.cpp`; вызывается из `pawnai/TacticalSwitch.cpp` |
| `pawnai/DashWatch.cpp` | **цепочка** `UpdatePawnAI`: `PawnAI::DashWatch::Tick()`; вызывается из `PawnAI.cpp`; вызывается из `devtools/GoapProbe.cpp`; вызывается из `runtime/PartyRecon.cpp` |
| `pawnai/GuardianDoctrine.cpp` | **цепочка** `UpdatePawnAI`: `PawnAI::GuardianDoctrineTick()`; вызывается из `PawnAI.cpp`; вызывается из `devtools/GoapProbe.cpp`; вызывается из `monsterai/TacticalCues.cpp`; и ещё 1 .cpp |
| `pawnai/NexusDoctrine.cpp` | **цепочка** `UpdatePawnAI`: `PawnAI::Nexus::Tick()`; вызывается из `PawnAI.cpp`; вызывается из `devtools/GoapProbe.cpp`; вызывается из `pawnai/GuardianDoctrine.cpp`; и ещё 5 .cpp |
| `pawnai/OrderWatch.cpp` | **цепочка** `UpdatePawnAI`: `PawnAI::OrderWatch::Tick()`; вызывается из `PawnAI.cpp`; вызывается из `pawnai/AcquisitorManager.cpp`; вызывается из `pawnai/GuardianDoctrine.cpp`; и ещё 5 .cpp |
| `pawnai/PartyRescueProtocol.cpp` | **цепочка** `UpdatePawnAI`: `PawnAI::Rescue::Tick()`; вызывается из `PawnAI.cpp`; вызывается из `pawnai/GuardianDoctrine.cpp`; вызывается из `pawnai/NexusDoctrine.cpp`; и ещё 1 .cpp |
| `pawnai/PawnHaste.cpp` | **цепочка** `UpdatePawnAI`: `PawnAI::Haste::Tick()`; вызывается из `EnemyAI.cpp`; вызывается из `PawnAI.cpp`; вызывается из `runtime/AggroWatch.cpp`; и ещё 1 .cpp |
| `pawnai/PawnPersona.cpp` | вызывается из `devtools/DevTools.cpp`; вызывается из `pawnai/GuardianDoctrine.cpp`; вызывается из `pawnai/NexusDoctrine.cpp` |
| `pawnai/Possession.cpp` | **цепочка** `UpdatePawnAI`: `PawnAI::Possession::Tick()`; вызывается из `EnemyTuner.cpp`; вызывается из `Nightmare.cpp`; вызывается из `PawnAI.cpp`; и ещё 1 .cpp |
| `pawnai/PresetManager.cpp` | **оркестратор**: поле `PresetManager` в `PawnAI_BusOrchestrator.h`; вызывается из `PawnAI.cpp` |
| `pawnai/SmartUtilitarian.cpp` | **оркестратор**: поле `SmartUtilitarian` в `PawnAI_BusOrchestrator.h`; вызывается из `pawnai/TacticalSwitch.cpp` |
| `pawnai/TacticalSwitch.cpp` | **оркестратор**: поле `TacticalSwitch` в `PawnAI_BusOrchestrator.h`; вызывается из `CombatIntel.cpp`; вызывается из `runtime/WorldScan.cpp` |
| `pawnai/VocationCordon.cpp` | **оркестратор**: поле `VocationCordon` в `PawnAI_BusOrchestrator.h` |
| `pawnai/WandRange.cpp` | **цепочка** `UpdatePawnAI`: `PawnAI::WandRange::Tick()`; вызывается из `PawnAI.cpp`; вызывается из `pawnai/Possession.cpp`; вызывается из `runtime/WorldScan.cpp` |
| `runtime/AggroWatch.cpp` | **цепочка** `UpdatePawnAI`: `Runtime::Aggro::CardReconTick()`, `Runtime::Aggro::Tick()`; **точка входа** `dinput8.cpp`; вызывается из `EnemyAI.cpp`; вызывается из `EnemyTuner.cpp`; вызывается из `PawnAI.cpp`; и ещё 5 .cpp |
| `runtime/LogMem.cpp` | **точка входа** `dinput8.cpp`; вызывается из `CameraPlus.cpp`; вызывается из `CombatIntel.cpp`; вызывается из `EnemyAI.cpp`; и ещё 27 .cpp |
| `runtime/MemProbe.cpp` | вызывается из `CombatIntel.cpp`; вызывается из `EnemyTuner.cpp`; вызывается из `PawnAI.cpp`; и ещё 18 .cpp |
| `runtime/MonsterTempo.cpp` | **точка входа** `dinput8.cpp`; вызывается из `EnemyAI.cpp`; вызывается из `EnemyTuner.cpp`; вызывается из `PawnAI.cpp`; и ещё 11 .cpp |
| `runtime/PartyRecon.cpp` | вызывается из `CameraPlus.cpp`; вызывается из `PawnAI.cpp`; вызывается из `devtools/AnimProbe.cpp`; и ещё 17 .cpp |
| `runtime/PartyStatus.cpp` | **цепочка** `UpdatePawnAI`: `Runtime::PartyStatus::Tick()`; **точка входа** `dinput8.cpp`; вызывается из `PawnAI.cpp`; вызывается из `monsterai/MonsterDirector.cpp`; вызывается из `monsterai/TacticalCues.cpp`; и ещё 1 .cpp |
| `runtime/PriorityPlatform.cpp` | **цепочка** `UpdatePawnAI`: `Runtime::ErrataTick()`; вызывается из `PawnAI.cpp`; вызывается из `devtools/DevTools.cpp`; вызывается из `devtools/GoapProbe.cpp`; и ещё 4 .cpp |
| `runtime/Runtime.cpp` | **точка входа** `dinput8.cpp`; вызывается из `CameraPlus.cpp`; вызывается из `CombatIntel.cpp`; вызывается из `EnemyAI.cpp`; и ещё 23 .cpp |
| `runtime/RuntimeState.cpp` | вызывается из `PawnAI.cpp`; вызывается из `devtools/DevTools.cpp` |
| `runtime/WorldScan.cpp` | **цепочка** `UpdatePawnAI`: `Runtime::WorldScan_Tick()`; вызывается из `CombatIntel.cpp`; вызывается из `EnemyAI.cpp`; вызывается из `EnemyTuner.cpp`; и ещё 20 .cpp |

Цепочка `UpdatePawnAI()`: 21 вызовов, все разрешаются в определения.

Файлы, которых нет в сборке намеренно:

* `src/MonsterCards.Generated.h` — каталог карт (84.68): справочник, код его не включает, читается инструментами
