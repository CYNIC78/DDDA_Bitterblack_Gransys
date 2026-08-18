# Рефакторинг — задание на следующую сессию

**Цель:** разнести раздутый `DevTools.cpp` (6k+ строк) на слои, вынести продукт
из-под research-флага, вычистить отжившие пробы. Продукт должен работать при
`[devtools] enabled = off`.

## 1. Проблема (зафиксирована)

- `DevTools.cpp` смешал три слоя: продуктовый runtime, исследовательские пробы,
  собственно DevTools (SCAN/DUMP/HUNT/TypeAtlas).
- **Критический баг**: продукт (детектор боя, доктрина, позиции) зависит от
  `WorldScan_Tick`, который гейтится `[devtools] enabled`. Выключи DevTools →
  сломался продукт.
- Накопился зоопарк проб: FollowProbe, LeashAb (багованый rollback, дрейфует),
  IntentHunt, два аудита, target-selection recon.

## 2. Целевая структура

```text
src/runtime/         (НОВЫЙ, продуктовый, всегда включён)
  PartyRecon.*       — uPlayer/uCmc, позиции, боевая цель
  WorldScan.*        — враги, боевые действия, сигналы детектора
  PriorityPlatform.* — транзакционные профили + GuardianFix

src/pawnai/          — поведение (уже есть): GuardianDoctrine, Acquisitor...
src/devtools/        — чисто исследовательское (SCAN/DUMP/HUNT + пробы),
                       гейтится researchDump, компилируется отдельно
```

## 3. Правила (из опыта Build 64)

1. Проба = временный инструмент: вопрос → ответ → SOURCE_OF_TRUTH → удалить.
2. Продукт НЕ зависит от research-флага.
3. Полный census в бою — ЗАПРЕЩЁН (краши); точечные пробы — только по якорям.
4. Тяжёлую работу не делать на render-потоке (ImGui-кнопки = D3D9 present).

## 4. Судьба проб-кнопок (что удалить / что оставить)

| Проба | Судьба | Обоснование |
|---|---|---|
| Audit all inclinations | удалить | ответ в SOURCE_OF_TRUTH §3.5 |
| Probe follow distance | удалить | ответ в §5.2 (поводка как поля нет) |
| Leash A/B | удалить | багованый rollback; гипотеза отвергнута |
| Intent hunt (code 4/66) | держать до поимки | техдолг, потом удалить |
| Target selection audit | удалить | ответ: цель = тело врага |
| Find both + capture | держать (research) | вне пути продукта, за researchDump |

## 5. Шаги (каждый — зелёный билд)

1. **Вынести `WorldScan`** (враги/позиции/сигналы) в `runtime/WorldScan.*`,
   включить всегда (убрать `if(!g_enabled)` из продуктового пути).
2. **Вынести `PartyRecon`** (uPlayer/uCmc + позиции) в `runtime/PartyRecon.*`.
3. **Вынести `PriorityPlatform`** (транзакции + GuardianFix) в `runtime/`.
4. **Пробы** — за researchDump или удалить отжившие (по таблице выше).
5. **GuardianDoctrine/Acquisitor** — проверить, что не зависят от devtools.
6. **Проверка**: `[devtools] enabled=off` → продукт (детектор боя, доктрина,
   позиции) работает, F12-панель продукта жива, исследовательские кнопки скрыты.

## 6. Definition of Done

- Продукт работает при `devtools = off` (протестировано в бою и вне).
- Ни одной пробы на пути продукта.
- `DevTools.cpp` содержит только SCAN/DUMP/HUNT/TypeAtlas + research-пробы.
- Доки (SOURCE_OF_TRUTH/FIX_RULES/CHANGELOG) отражают новую структуру.

## 7. После рефакторинга (следующие задачи)

- Vocation-aware кордон Guardian (гасить для Mage/Sorcerer/Ranger/MagickArcher).
- Strider: частичный кордон + Pioneer-перевес (A/B в бою).
- Pioneer-перевес для дальнобойщиков (грубый вариант через веса).
- (опционально) RE цели Follow для точного контроля позиции заслона/дистанции.
