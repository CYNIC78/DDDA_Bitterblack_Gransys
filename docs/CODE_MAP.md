# CODE_MAP — кто за что отвечает (генерируется)

> Автогенерация: `python3 tools/code_map.py --md docs/CODE_MAP.md`.
> Роль файла берётся из его собственной шапки — то есть карта не может разойтись
> с кодом: она и есть комментарии кода, собранные в одно место. Шаг гейта `10f/10`
> требует, чтобы каждый `.cpp` из `src/` был в этой карте.

**Колонка «включают»** — сколько файлов проекта делают `#include` на этот файл.
Большое число означает «менять осторожно»: правка задевает много мест.

## Продуктовый слой (`src/runtime/`) — работает всегда, без DevTools

| Файл | Строк | Включают | Роль (из шапки файла) |
|---|---:|---:|---|
| `runtime/AggroWatch.cpp` | 2732 | 0 | Runtime::Aggro — прибор «на кого смотрит пачка». См. AggroWatch.h. |
| `runtime/AggroWatch.h` | 339 | 7 | Runtime::Aggro — прибор «на кого смотрит пачка». |
| `runtime/EnemyFileBase.h` | 205 | 1 | EnemyFileBase.h — АВТОГЕНЕРАЦИЯ, не править руками. |
| `runtime/LogMem.cpp` | 473 | 0 | LogMem.cpp — журнал в оперативке с фоновым сбросом на диск: строки копятся в буфере (8 МБ, дальше тихий truncate), а std::endl больше не бьёт по диску на каждой строке. При сбое буфер сливается из обработчика — без повторной кучи. |
| `runtime/LogMem.h` | 33 | 3 | Лог в оперативке с непрерывным фоновым сбросом на диск и защитой при краше. std::endl больше не трогает диск (только перевод строки в буфер). Писатели с тика пешек и с кадра F12 сериализуются критической секцией. |
| `runtime/LogMemSession.h` | 38 | 11 | 85.63: ПОЛЕВОЙ ПАКЕТ — сводки сессии одним блоком. |
| `runtime/MemProbe.cpp` | 350 | 0 | Runtime::Mem — реализация. См. MemProbe.h. |
| `runtime/MemProbe.h` | 126 | 16 | Runtime::Mem — фундамент рантайма: безопасное чтение/запись чужой памяти, границы секций образа игры и резолв имени живого объекта через DTI. |
| `runtime/MonsterTempo.cpp` | 2719 | 0 | Runtime::Tempo — вариативность темпа передвижения монстров. См. MonsterTempo.h. |
| `runtime/MonsterTempo.h` | 462 | 15 | Runtime::Tempo — вариативность темпа передвижения монстров. |
| `runtime/PartyRecon.cpp` | 1642 | 0 | Runtime::Party — поиск и разбор тел партии (uPlayer/uCmc), роли, позиции Аризена и главной пешки. Продуктовый слой: позиции читает доктрина и Camera Plus, а не исследование. |
| `runtime/PartyStatus.cpp` | 749 | 0 | Runtime::PartyStatus — read-only прибор: статусы партии + downed/revive. См. PartyStatus.h и docs/PARTY_STATUS_OBSERVE.md. |
| `runtime/PartyStatus.h` | 58 | 5 | Runtime::PartyStatus — read-only прибор: статусы партии + downed/revive. |
| `runtime/PriorityPlatform.cpp` | 1077 | 0 | Runtime::Priority — транзакционные priority-профили и Guardian-фикс. Правило слоя: validate -> write -> readback -> convergence -> rollback. Никаких «попробуем и посмотрим» — только подтверждённые кортежи правил. |
| `runtime/Runtime.cpp` | 71 | 0 | Runtime — сборка продуктового слоя. См. Runtime.h. |
| `runtime/Runtime.h` | 305 | 21 | Runtime — продуктовый слой мода. Точка входа, которая работает ВСЕГДА, независимо от [devtools] enabled и от того, поднялся ли ImGui-оверлей. |
| `runtime/RuntimeInternal.h` | 624 | 11 | Runtime — внутренний контракт продуктового слоя. |
| `runtime/RuntimeState.cpp` | 85 | 0 | Runtime — состояние продуктового слоя. |
| `runtime/WorldScan.cpp` | 1222 | 0 | Runtime::World — обход списка актёров, классификация врагов и публикация WorldReport. Работает ВСЕГДА: детектор боя и доктрина Guardian зависят от этого тика, поэтому он не имеет права гейтиться флагом devtools. |

## Сторона монстров (`src/monsterai/`) — директор, карточки видов, приборы

| Файл | Строк | Включают | Роль (из шапки файла) |
|---|---:|---:|---|
| `monsterai/MonsterDirector.cpp` | 2569 | 0 | MonsterAI::Director — Build 84 / session Build 012. |
| `monsterai/MonsterDirector.h` | 105 | 4 | Monster Director — Build 012 (targeted urgency + responder mobilization). |
| `monsterai/PackObserve.cpp` | 1133 | 0 | PackObserve — read-only night instrument for exact uEm0100. Wolf 012 write path is not referenced. No F12 controls. Transition log only. |
| `monsterai/PackObserve.h` | 51 | 4 | PackObserve — ночной observe-прибор exact uEm0100. |
| `monsterai/SpeciesCard.h` | 91 | 4 | SpeciesCard — словарь допуска по ТОЧНОМУ DTI-имени. |
| `monsterai/SpeciesTuning.cpp` | 130 | 0 | RegisterRageProfile отказывает профилю, у которого низ не меньше верха. Строгий зазор держим здесь: вписав 1.25/1.25, владелец получил бы МОЛЧАЛИВО отключённый разгон вида. const float kMinGap = 0.01f; |
| `monsterai/SpeciesTuning.h` | 48 | 2 | SpeciesTuning — потолки разгона вида в ddda_ai_overhaul.ini (85.36). |
| `monsterai/TacticalCues.cpp` | 1444 | 0 | Data-only tactical cue matching. No writes, hooks, configuration, or UI. #include "stdafx.h" #include "TacticalCues.h" #include <math.h> #include <string.h> |
| `monsterai/TacticalCues.h` | 254 | 2 | Small, data-driven tactical cue matcher used by Monster Director. |

## Сторона пешек (`src/pawnai/`) — доктрины, ускорение, Possession

| Файл | Строк | Включают | Роль (из шапки файла) |
|---|---:|---:|---|
| `pawnai/AcquisitorManager.cpp` | 80 | 0 | AcquisitorManager.cpp — мягкий менеджер инклинации Acquisitor: единственной, которая в бою вредит (пешка «пылесосит» вместо боя). |
| `pawnai/AcquisitorManager.h` | 53 | 2 | AcquisitorManager — мягкий менеджер Acquisitor, единственной инклинации, бесполезной в бою. |
| `pawnai/DashWatch.cpp` | 150 | 0 | PawnAI::DashWatch — наблюдатель за рывками пешки. См. DashWatch.h. |
| `pawnai/DashWatch.h` | 87 | 2 | PawnAI::DashWatch — наблюдатель за рывками пешки. |
| `pawnai/DoctrineAnnounce.h` | 52 | 2 | 85.63: НЕ ОБЪЯВЛЯТЬ ОДНУ И ТУ ЖЕ ЦЕЛЬ ДВАЖДЫ ПОДРЯД. |
| `pawnai/GuardianDoctrine.cpp` | 1254 | 0 | 85.63: СЧЁТЧИКИ СЕССИИ. Лежат на уровне файла, а не в состоянии слота: слот сбрасывается при выходе из боя и при выгрузке мира, а сессия — нет. Нужны для сводки в полевом пакете: сколько раз объявляли цель и сколько объявлений свернули окном тишины — чтобы подавленный шум был виден числом, а не на слово. static int s_probesStarted = 0; static int s_probesCompleted = 0; static int s_targetAnnounces = 0; |
| `pawnai/GuardianDoctrine.h` | 299 | 3 | Guardian doctrine: production decision + runtime adapter. Decide() is read-only; GuardianDoctrineTick() writes combat target/gaze and requests a tempo override. observeOnly describes the report, NOT a global write gate. Nexus has its own module, not this class's old draft branch. 85.09/85.10 changes and remaining limits: docs/GUARDIAN_HARDENING.md. Navigation, shield skill selection and actual attacks remain engine-owned. |
| `pawnai/NexusDoctrine.cpp` | 461 | 0 | 85.63: СЧЁТЧИКИ СЕССИИ — вне состояния слота (Nexus::Shutdown() его обнуляет, а сводка печатается после выгрузки модулей). Как у Guardian: сколько замеров начато/завершено и сколько объявлений цели свернуло окно тишины. static int s_probesStarted = 0; static int s_probesCompleted = 0; static int s_targetAnnounces = 0; static int s_targetMuted = 0; |
| `pawnai/NexusDoctrine.h` | 40 | 2 | Nexus: sticky partner + temporary emergency cover. Build 85.08 contract and limitations: docs/NEXUS_DOCTRINE.md. Melee executors only; no movement, gaze or tempo override ownership. |
| `pawnai/NexusPolicy.h` | 68 | 1 | Pure decisions: no heap dereference, no game writes. Portable regression tests. namespace PawnAI { namespace NexusPolicy { inline bool BetterThreat(bool critical, float distance, uintptr_t body, bool bestCritical, float bestDistance, uintptr_t bestBody) { if (!body || !(distance >= 0.0f) || distance > 12.0f) return false; if (!bestBody) return true; if (critical != bestCritical) return critical; return distance < bestDistance || (distance == bestDistance && body < bestBody); |
| `pawnai/OrderWatch.cpp` | 295 | 0 | OrderWatch.cpp — тактические приказы D-Pad / F1-F3 (Ко мне! / Вперёд! / Помогите!): распознаёт приказ, ищет врага в секторе взгляда игрока (до 35 м по WorldReport из CombatBus) и ведёт его с плавным затуханием, отдавая ускорение через Runtime::Tempo::SetOverride. |
| `pawnai/OrderWatch.h` | 60 | 4 | PawnAI::OrderWatch — тактическая обработка команд D-Pad / F1-F3 (Ко мне! / Вперед! / Помогите!). |
| `pawnai/PartyRescueProtocol.cpp` | 226 | 0 | PartyRescueProtocol.cpp — общепартийный протокол спасения: пешку или самого Восставшего схватили — остальные обязаны бросить свои занятия и идти освобождать. Ловит кризис по act каптора, держит тело, вид, причину и время старта; на время кризиса пинит каптора в боевую цель всем пешкам и поднимает им темп (Runtime::Tempo::SetOverride), по выходу — снимает оверрайды. |
| `pawnai/PartyRescueProtocol.h` | 41 | 4 | PartyRescueProtocol.h — Общепартийный протокол экстренного спасения Аризена. |
| `pawnai/PawnAI_BusOrchestrator.h` | 131 | 1 | PawnAI_BusOrchestrator — тонкий оркестратор. Новая модель весов (v3.0). |
| `pawnai/PawnAI_Common.h` | 204 | 14 | PawnAI_Common.h — Общие оффсеты и хелперы для всех PawnAI-модулей Вынесено из монолита PawnAI.cpp чтобы разгрузить менталку |
| `pawnai/PawnHaste.cpp` | 398 | 0 | PawnAI::Haste — рывок пешки в бою. См. PawnHaste.h. |
| `pawnai/PawnHaste.h` | 77 | 2 | PawnAI::Haste — рывок пешки в бою через множитель передвижения. |
| `pawnai/PawnPersona.cpp` | 83 | 0 | PawnPersona — см. PawnPersona.h. Чистая логика, без памяти игры. |
| `pawnai/PawnPersona.h` | 66 | 3 | PawnPersona — роль пешки из её СОБСТВЕННОГО стека склонностей. |
| `pawnai/Possession.cpp` | 753 | 0 | PawnAI::Possession — primitive. См. Possession.h и SoT §12.1.2. #include "stdafx.h" #include "Possession.h" #include "PawnAI_Common.h" #include "../runtime/Runtime.h" #include "../runtime/MemProbe.h" #include "../runtime/LogMemSession.h" // 85.64: сводки — в полевой пакет |
| `pawnai/Possession.h` | 52 | 2 | PawnAI::Possession — primitive слоя E + watch слоя C (SoT §12.1.2). |
| `pawnai/PresetManager.cpp` | 107 | 0 | НИКАКИХ записей при старте. LoadPreset(5) вызывает SaveConfig() и раньше перезаписывал пользовательский [customAnchor] на Balanced ДО чтения INI. Значения anchor[] уже инициализированы Balanced в .h; LoadConfig читает сохранённый выбор, не изменяя файл. LoadConfig(); } |
| `pawnai/PresetManager.h` | 49 | 2 | PresetManager — якоря (ползунки) и пресеты-снапшоты. |
| `pawnai/SmartUtilitarian.cpp` | 58 | 0 | SmartUtilitarian.cpp — модуль-слушатель шины: тренер (CombatIntel) кричит в мегафон, а модуль возвращает delta[] — поправку к целевым весам по изученности врагов (mStudyFlag через types.tsv). |
| `pawnai/SmartUtilitarian.h` | 32 | 2 | SmartUtilitarian — модуль-слушатель шины. Тренер (CombatIntel) кричит в мегафон, а этот модуль возвращает delta[] — поправку к целевым весам. |
| `pawnai/TacticalSwitch.cpp` | 80 | 0 | TacticalSwitch.cpp — ситуативная поправка к базе: категория врага (small/medium/large/flying/mage/boss) → дельта целевых весов. Одна из трёх поправок оркестратора рядом со SmartUtilitarian и AcquisitorManager. |
| `pawnai/TacticalSwitch.h` | 31 | 2 | TacticalSwitch — фаза 1.6, новая модель. |
| `pawnai/VocationCordon.cpp` | 73 | 0 | PawnAI::VocationCordon — вокационный кордон Guardian. См. VocationCordon.h. |
| `pawnai/VocationCordon.h` | 62 | 2 | PawnAI::VocationCordon — вокационный кордон Guardian. |
| `pawnai/WandRange.cpp` | 943 | 0 | PawnAI::WandRange — см. WandRange.h. |
| `pawnai/WandRange.h` | 64 | 2 | PawnAI::WandRange — эррата дальности посоха у пешки (слой B) + CasterWatch. |

## Исследование (`src/devtools/`) — не существует в релизном поведении

| Файл | Строк | Включают | Роль (из шапки файла) |
|---|---:|---:|---|
| `devtools/AnimProbe.cpp` | 1858 | 0 | AnimProbe — поиск часов анимации. См. AnimProbe.h. |
| `devtools/AnimProbe.h` | 136 | 2 | AnimProbe — поиск множителя темпа анимации у существа. ИССЛЕДОВАТЕЛЬСКИЙ инструмент. |
| `devtools/DevTools.cpp` | 5155 | 0 | DevTools.cpp — TypeAtlas + vtable scan + sUnit anatomy + heap hunt |
| `devtools/DevTools.h` | 46 | 2 | DevTools — ИССЛЕДОВАТЕЛЬСКИЙ слой мода. Продуктом не является. |
| `devtools/GoapProbe.cpp` | 3105 | 0 | GoapProbe — разведка планировщика пешки. См. GoapProbe.h. |
| `devtools/GoapProbe.h` | 222 | 2 | GoapProbe — разведка планировщика пешки. ИССЛЕДОВАТЕЛЬСКИЙ инструмент. |

## Корень `src/` — точки входа, UI, шины

| Файл | Строк | Включают | Роль (из шапки файла) |
|---|---:|---:|---|
| `BestiaryData.h` | 173 | 1 | BestiaryData.h — ПОЛНЫЙ БЕСТИАРИЙ DDDA |
| `BuildTag.h` | 14 | 3 | Метка версии сборки. |
| `CameraPlus.cpp` | 516 | 0 | CameraPlus.cpp — Tactical Camera + Pause + Disable Auto-Correction (v2.2) |
| `CameraPlus.h` | 15 | 2 | CameraPlus.h — тактическая камера (v2.2): тактический обзор, пауза кадра и отключение авто-доворота камеры. Состояние и реализация — CameraPlus.cpp; здесь только две точки жизненного цикла. |
| `CombatBus.h` | 168 | 21 | CombatBus.h — ШИНА (мегафон тренера) |
| `CombatIntel.cpp` | 885 | 0 | Имя класса цели через DTI ("uHumanEnemy", "uEm0100"). Пустая строка, если владельца удара опознать не удалось. |
| `CombatIntel.h` | 19 | 5 | CombatIntel.h — «тренер» боя: сам читает бой и публикует его в CombatBus. Поведение не решает — это работа модулей пешек через оркестратор. |
| `DefaultEntitiesIni.h` | 299 | 1 | AUTOGENERATED from ddda_entities.ini by tools/gen_default_ini.py НЕ РЕДАКТИРОВАТЬ РУКАМИ: правьте ddda_entities.ini и перегенерируйте. |
| `EnemyAI.cpp` | 511 | 0 | EnemyAI.cpp — ум врага (орган, не пак файлов) |
| `EnemyAI.h` | 14 | 2 | EnemyAI.h — контракт оболочки для стороны монстров: единственная точка входа Hooks::EnemyAI(). Жизнь монстров не здесь: режиссёр, ранги, прибор пачки и тактические подсказки живут в src/monsterai/ и зовутся из цепочки PawnAI. |
| `EnemyTuner.cpp` | 2643 | 0 | Первый шаг применения конфига: РАЗВЕДКА, а не запись. |
| `EnemyTuner.h` | 121 | 4 | EnemyTuner — применение EntityCfg к живым врагам. |
| `EntityConfig.cpp` | 346 | 0 | Реализация трёхуровневого конфига с hot-reload. |
| `EntityConfig.h` | 130 | 4 | EntityConfig — настройки поведения сущностей из ddda_entities.ini. |
| `ModPaths.h` | 64 | 6 | ModPaths — все файлы мода в своей папке, а не в корне игры. |
| `Nightmare.cpp` | 362 | 0 | Nightmare.cpp — Bitterblack Gransys Module |
| `Nightmare.h` | 14 | 2 | Nightmare.h — контракт модуля Nightmare (Bitterblack Gransys): вечная ночь, погода, замена монстров и триггер гибели Деймона. Замысел, слои и вердикт — docs/NIGHTMARE.md; здесь только точка входа Hooks::Nightmare(). |
| `PawnAI.cpp` | 1369 | 0 | PawnAI.cpp — Pawn AI Overhaul Orchestrator & Custom Anchors Modules: Acquisitor Manager, Smart Utilitarian, Custom Anchors, Tactical Switch |
| `PawnAI.h` | 15 | 2 | Вспомогательные функции УДАЛЕНО (75.2): `float* GetPawnInclinations(int)` объявлялась здесь, но определения не было ни в одном .cpp — фантом из старого монолита. Первое же обращение к ней уронило сборку на LNK2019, хотя линтер писал про «нет тела» задолго до. Чтение склонностей — через `ReadAllIncl()/WriteAllIncl()` в `pawnai/PawnAI_Common.h`. |
| `TargetLock.cpp` | 228 | 0 | TargetLock.cpp — Auto-aim: доворачивает ГГ по вектору камеры при атаке |
| `TargetLock.h` | 15 | 2 | TargetLock.h — автоприцел: в момент атаки доворачивает Восставшего по вектору камеры (реализация — TargetLock.cpp, свои рабочие потоки). |
| `audio/AudioRedirect.cpp` | 1451 | 0 | AudioRedirect — музыкальный слой 0 (docs/AUDIO_MUSIC_RECON.md). |
| `audio/AudioRedirect.h` | 32 | 2 | AudioRedirect — музыкальный слой 0 (docs/AUDIO_MUSIC_RECON.md). |
| `dinput8.cpp` | 241 | 0 | DDDA AI Overhaul Mod Основан на архитектуре ddda-dinput8 by kubik-jaroslav Цель: улучшение AI пешек и монстров без читов |

---

## Файлы без шапки (кандидаты на комментарий)

Нет — каждый файл объясняет себя.

Файлов в карте: 90 (без `*.Generated.h`: те генерируются и описаны в шапках).
