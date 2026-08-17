# День 8 — 16.08.2026: Build 55.x + 56 (draft) — реабилитация Guardian/Nexus

## Направление

Guardian и Nexus — не «мусор», а доктрины защиты (поводок со штрафами на
некоторые действия). Обе завязаны на мили-вокации, чего Capcom не учитывает —
отсюда маги, летящие в гущу боя, и чародеи на спинах монстров.

План: не глушить их числами (как делал SanitaryCordon), а реализовать правильно
через GuardianDoctrine (priority-совет с учётом vocation), начиная с Guardian.

## Build 55.x — кордон расщеплён, vocation подтверждён

### Кордон расщеплён

- `SanitaryCordon` удалён. Вместо него — `AcquisitorManager`.
- Guardian/Nexus вышли из-под кордона → новая категория `CAT_DOCTRINE`.
- Acquisitor остался единственной управляемой инклинацией:
  - в бою — подавление до `acquisitorCombatFloor` (100);
  - после боя — временный подъём на `acquisitorLootBoost` (180) на окно
    `acquisitorBoostWindowMs` (8 с), «пылесосит» лут;
  - затем — плавный возврат «домой» к ползунку игрока за
    `acquisitorReturnMs` (4 с), дельта → 0;
  - hysteresis ~1.5 с на границе боя.
- 55.2 уточнил спецификацию: дельта больше не висит постоянно вне боя.

### Vocation-фундамент (подтверждён CE-таблицей 2026-08-16)

- Enum 1-based: `1=Fighter 2=Strider 3=Mage 4=Mystic Knight 5=Assassin
  6=Magick Archer 7=Warrior 8=Ranger 9=Sorcerer`.
- `VocationClassOf`: melee/ranged/caster/hybrid.
- В 55.0 enum был неверным (0-based) — исправлено в 55.1.

### Документация пополнена (CE-таблица)

- max stamina `+0x97C`, recoverable stamina `+0x980`;
- Str `+0x984` / Def `+0x988` / Magick `+0x98C` / MgkDef `+0x990`;
- XP-to-next `+0x998`;
- скиллы — 6 слотов (2 оружия × 3);
- экономика: DP `+0xA7A14`, Gold `+0xA7A18`, RC `+0xA7A1C`.

## Build 56.x — GuardianDoctrine core (observe-only)

- Новый модуль `GuardianDoctrine` (h/cpp), ранее `ProtectorDoctrine`:
  - ЭТО улучшение инклинации Guardian (и позже Nexus тем же ядром), НЕ
    отдельная фича «спаси игрока» — та (Critical Response) будет поверх;
  - чистый decision core (память не трогает, тестируемый);
  - вход `GuardianSitRep`, выход `GuardianReport` с `GuardianAdvice[]`;
  - ownership Guardian/Nexus по инклинации; Nexus — заглушка;
  - зона ответственности + hysteresis + dwell;
  - ответ по вокации (Threat Anchor ≠ Movement Anchor):
    melee=Intercept, ranged=RangedHold, caster=Support, hybrid=Adaptive;
  - leash-контроль дистанции от якоря;
  - таблица подтверждённых Guardian-модификаторов (code 4/13/15/54/60/66)
    со статусами CONFIRMED/HYPOTHESIS;
  - главный рычаг A/B: снять штраф -3 с `WpnDaggerAtk` (code 54).
- observe-only: отчёт показывается в единой UI-панели «Guardian Doctrine»,
  записи нет.
- 56.1: переименование Protector→Guardian + чистка UI (убраны спойлеры с
  номерами билдов, секции слиты). Имена инклинаций сохранены как есть.

## Build 56.2 — позиции Аризена и пешки

- Резолвер позиций uPlayer (anchor) и uCmc (pawn):
  - тела берутся из того же census'а, что и priority-платформа;
  - позиции читаются каждый тик из `+0x40/+0x44/+0x48` (SEH-безопасно);
  - census throttled (раз в 10 с, только при невалидных телах).
- Новые accessors `DevTools::GetArisenWorldPos` / `GetMainPawnWorldPos`.
- `BuildGuardianSitRep` заполняет позиции; зона ответственности теперь
  считается по-настоящему.
- UI показывает координаты обеих сторон + угрозы в зоне.
- Помечено как ПЕРВОЕ применение +0x40 для uPlayer/uCmc — требует
  визуального подтверждения в движении.

## Build 56.3 — масштаб мировых координат

- Выяснено: `+0x40` для uPlayer/uCmc ВЕРНЫЙ. Ошибка была в масштабе —
  мир в ~сантиметрах (~100/m), не в метрах. Доказательства зафиксированы
  в SOURCE_OF_TRUTH §2.1 (AIPlActParam 500..4000, гоблин-сенсор 1500,
  pawn→Arisen ~440 = ~4.4 м).
- Доктрина: добавлен `worldUnitsPerMeter` (ini, по умолч. 100.0); радиусы
  в метрах → world-units внутри; дистанции на экран в метрах.
- Починены «Threats in zone: 0» (радиус был 12 см) и «pawn 439 м» (на деле 4.4 м).
- Отбой поездки в Cheat Engine — оффсет не требовался.

## Build 56.4 — фикс позиции пешки

- В 56.3 пешка читалась как (0,0,0) из-за ОБЩЕГО флага резолва для обоих тел.
- Флаги разделены: `g_arisenPosOk` / `g_pawnPosOk`; (0,0,0) — sentinel «нет
  позиции».
- Доктрина при нечитаемой пешке пишет «pawn position UNRESOLVED» и не
  выдумывает дистанцию/совет leash.
- Добавлена rate-limited диагностика в лог (роль/тело пешки при сбое).
- Throttle пере-обнаружения 10с → 5с.

## Build 56.5 — отключены исследовательские дампы

- Флаг `[devtools] researchDump` (по умолчанию off). Гейтит три файла:
  `ddda_party_recon_*.json`, `ddda_pawn_ai_bridge_*.json`,
  `ddda_pawn_intent_trace_*.csv` (trace рос всю сессию).
- Чекбокс «Write research dumps (json/csv)» в панели recon.
- Резолв тел/позиций НЕ затронут — отключены только файлы.

## Build 56.6 — позиции в рилтайме, без скана

- Авто-census убран (PartyPositionsTick больше не гоняет PartyFindBodies по
  таймеру) — источник периодических подвисаний устранён.
- Позиции uPlayer/uCmc берутся из WorldReport (актор-список, публикуется
  каждые 150 мс, уже содержит имена и координаты партии). Дорогой census
  для доктрины больше не нужен; остаётся только fallback.

## Build 56.7 — ленивый census + фикс спама

- В 56.6 позиции брались из WorldReport, но uPlayer/uCmc НЕ входят в
  актор-список (фильтр LooksLikeCreatureAt пропускает только uEm*/uHumanEnemy).
  Вернул census-резолв.
- Census стал ЛЕНИВЫМ и одноразовым: запускается в фоне (pawn-tick поток,
  не фризит рендер) только когда тела не найдены; повтор лишь при сбое
  (троттл 10 с). После — дешёвое чтение каждый тик.
- Cleanup «world unload» теперь только на переходе из мира (не каждый тик);
  RestoreAll не логирует, если нечего восстанавливать. Спам в логе устранён.

## Build 56.8 — census добирает пешку

- Баг: ранний выход в PartyPositionsTick по «есть хоть одно тело» — первый
  фоновый скан ловил только uPlayer (пешка спавнится позже), и census больше
  не перезапускался → пешка вечно ненайденная («pawn read FAILED»).
- Census теперь повторяется (троттл 5 с), пока не найдены ОБЕ роли.
- Позиция Аризена читается и в ожидании; при выгрузке мира g_nParty
  сбрасывается в 0.

## Build 56.9 — anchor развязан от позиции пешки

- Ответ на вопрос «нормально ли, что координаты ГГ не показываются без
  пешки»: НЕТ — это был косяк (общее условие на обе позиции).
- Доктрина теперь требует только anchor (ГГ): зона/совет работают сразу;
  leash-совет включается только при живой позиции пешки.
- UI показывает позицию ГГ + зону всегда, пешку — по готовности
  («pending resolution...»).

## Следующие шаги

1. Resolver позиций uPlayer (anchor) и uCmc (pawn) — единственная дырка
   для рабочей зоны ответственности.
2. Build 56 A/B: применить снятие штрафа code 54 через транзакционный
   priority-профиль (validate→apply→readback→convergence→rollback).
3. Nexus Dedicated Partner (anchor=выбранная пешка, доступ к hired pawns).
4. CriticalResponseDirector — отдельно, поверх доктрин.
