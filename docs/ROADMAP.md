# Roadmap DDDA AI Overhaul

**Stable milestone:** Build 47 — `pawn-ai-platform-milestone`

**Active vertical:** Build 63 — Guardian Doctrine (ролевая матрица, градиентная зона, детектор боя)

**Дата состояния:** 2026-08-18

## 1. Что уже является платформой

### Главная пешка

- live `uPlayer/uCmc` и current `cPlAct*`;
- character record HP/stamina/inclinations;
- 83 pawn AI resources в каноническом каталоге;
- 85 priority rows → 48 live buckets;
- exact `cCodeParam AddS32` и `cOrderValue` layouts;
- generalized persistent profile из 0..48 rules;
- exact tuple resolution, multi-rule transaction, readback, convergence, rollback;
- автоматическое повторное применение после загрузки мира;
- planner current code и `PlanCtrl` indexing;
- 352 weapon/action eligibility rows в CSV и live compiled parameters.

### Общая runtime-инфраструктура

- TypeAtlas: 4405 типов;
- DTI naming живых объектов;
- WorldScan и enemy body/action discovery;
- CombatBus/CombatIntel;
- Bestiary mapping;
- CameraPlus;
- EnemyTuner experimental mutations;
- отдельная папка `PLAYER_PAWN_WORK` с текущим вертикальным срезом.

## 2. Ближайший порядок работы

### P0 — привести знания к рабочей форме

- [x] удалить завершённые protocol/test документы;
- [x] оставить канонические документы и Git history как архив;
- [x] зафиксировать Build 47;
- [x] синхронизировать пользовательский README и Project Hub с Build 47;

### P1 — закончить priority platform

- [ ] Связать priority codes с именами намерений/GOAP: 42/70 `cmc.prt` codes mapped; runtime map extended to all 91 slots and Build 53 selected planner-only `74/76`.
- [x] Реализовать и проверить read-only main-pawn fast path: 570.7 s, 747 rows, readable intent, exact `cPlAct`, packed code, `uCmc+0x2EB8` target and PlanCtrl links.
- [x] Убрать диагностический census cap: Build 48 — 450/1024 candidates.
- [x] Закрепить main-pawn root association: `uCmc+2E64 → cAICtrl+68/+70`.
- [x] Закрыть live current target: `uCmc+0x2EB8` — primary planning/combat target; Build 53 подтвердил 9 bodies и retention во время near-death.
- [ ] Построить семантические profile entries вместо чисел `code45/46`.
- [ ] Создать первые продуктовые профили только после mapping, без угадывания.

### P2 — action eligibility

1. Связать 352 `AIPlActParam*` rows с runtime objects и конкретными действиями.
2. Разделить AI use/target ranges и реальные damage hitboxes.
3. Выполнить один rollback-safe range A/B.
4. Обобщить mutation в sidecar schema.
5. Использовать для выбора подходящего оружия/приёма, а не изменения урона.

### P3 — GOAP patches

1. Составить semantic map plans/premises/effects.
2. Найти доказанные тупики после правильных target/priority.
3. Патчить только конкретные ошибки планирования.
4. Применять при загрузке с manifest/hash/rollback, не динамически каждый тик.

### P4 — monster decision bridge

1. Найти enemy equivalents sensors/target/priority/planner.
2. Повторить verified resource → runtime → sidecar pipeline.
3. Использовать CombatBus как контекст.
4. Сохранять видовую идентичность; не подменять FSM состояниями вручную.

## 3. Более дальние продуктовые направления

- Active Guardian и другие inclination-aware роли;
- интеллектуальный healer/support;
- реакция на boss knockdown и pawn callouts через CombatBus;
- target scoring вокруг Arisen;
- Nightmare как runtime world policy;
- persistent world memory в отдельном save-keyed sidecar;
- PACK только для отсутствующих LOT/GPL/resources.

## 4. Неподвижные ограничения

- Сейчас исследуем только главную пешку; hired pawns не входят в acceptance scope.
- Не записываем transient pointers в профили.
- Не используем F9 — это сохранение пользователя.
- Не меняем `DDDA.sav`.
- Не подменяем `game_main.arc` как основной способ настройки.
- Не объявляем поля подтверждёнными без runtime A/B/readback.
- Current Act не является безопасной точкой прямого командования.

## 5. Definition of Done для новой AI-фичи

```text
offline identity mapped
+ live object resolved without absolute heap address
+ expected fields validated
+ mutation read back
+ observable downstream result
+ rollback tested
+ sidecar schema documented
+ vanilla fallback works
```

Если отсутствует хотя бы один пункт, это исследовательский probe, а не продуктовая фича.
