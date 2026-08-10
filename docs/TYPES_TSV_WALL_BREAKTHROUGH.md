# WALL BREAKTHROUGH: Как работать с `types.tsv` без ограничений

**Статус: СТЕНА ПРОБИТА 10.08.2026 — база для всей системы PawnAI/EnemyAI/Nightmare**

> Проект родился из CheatEngine-сканирования — ручной поиск адресов, `pBase + 0xA7000 + 0x7F0 + 0x96C + 0x1224` с шагом `0xC`. Это была боль.
> Теперь у нас есть `types.tsv` (Atvaark, 4406 фабрик из `DDDA.exe`) + `bestiary.py` (chrispurnell, 72 врага + FlagID) и архитектура **Тренер + Мегафон + Слушатели**.

---

## 1. Что лежит в `types.tsv` — распутали три сосны

Файл — дамп всех `RegisterFactory` вызовов в `DDDA.exe`. Заголовок врёт:

```
Caller  FactoryPointer  FactoryName  Allocator  Size  Unknown  Unknown  Unknown  ResetFactoryFunc  FactoryVtable
```

Правильно **по индексу** (проверили в песочнице):

| # | Значение | Смысл для нашего `dinput8.dll` | Пример `uEm0500` |
|---|---|---|---|
| 0 | `Caller` | Адрес `call` в коде где фабрика регистрируется | `0x134c2ea` |
| 1 | `FactoryPointer` | Глобальный `**pFactory` в `.data` — указатель на менеджер/фабрику | `0x1994e90` |
| 2 | `FactoryName` | Имя MT Framework типа | `uEm0500` = Skeleton |
| 3 | `Allocator` | Аллокатор | `0x1994ff8` |
| 4 | `Size` | `sizeof(класс)` — пригодится для сканирования памяти | `29408` |
| **5** | **`groupId`** | **ГЛАВНОЕ: `targetBase[0x2D]` в damage-хуке!** Совпадает с `EnemyEntry.groupId` | `27` (`0x1B`) |
| 6 | `0` | — | `0` |
| 7 | `0` | — | `0` |
| 8 | `ResetFactoryFunc` | — | `0x138e090` |
| 9 | `FactoryVtable` | **VTable типа — точная идентификация** когда `groupId` общий/нулевой | `0x15ae3b4` |

**Ловушка прошлой сессии:** читали `row['Unknown']` через `DictReader` — получали третью колонку (всегда `0`), думали `gid=0` у всех. Надо `parts[5]` по индексу.

Все адреса — **VA под базу `0x400000`**. В рантайме:
```cpp
uintptr_t base = (uintptr_t)GetModuleHandle(nullptr);
uint32_t  rva  = 0x15ae3b4 - 0x400000; // 0x11AE3B4
uintptr_t real = base + rva;
```

---

## 2. Три уровня использования — от простого к безграничному

### Level 1 — COMPILE-TIME (делаем сейчас, покрывает 95%)

Не таскать 341 KB TSV в релиз! Генерим `src/EnemyTypes.Generated.h` при сборке:

```bash
python tools/generate_bestiary.py  # → 110 записей с gid != 0, уже готово
```

В `CombatIntel.cpp`:
```cpp
#include "EnemyTypes.Generated.h"
BYTE gid = targetBase[0x2D];
auto* info = FindByGroupId(gid); // теперь находит ВСЕ 110 типов, а не 6
```

**Эффект:** `PawnAI Smart Utilitarian` получает реальный `avgKnowledge` (Skeletons `0x1B`, Saurians `0x0F`, Undead `0x20` … до `uEm7002 0xBC` Daimon). Бестиарий в F12 перестаёт показывать `0xFF`.

### Level 2 — VTable-точность (пробивает ограничение `groupId`-коллизий)

Проблема: `Goblin / Hobgoblin / Grimgoblin` — раньше `gid` общий `0x05`, BBI-подтипы `uEm0100_0` вообще `gid=0`. Через `0x2D` их не отличить.

Решение: вторым слоем сравниваем **vtable**:
```cpp
void* vt = *(void**)targetBase; // первый DWORD живого объекта
uint32_t rva = (uintptr_t)vt - base;
auto* precise = FindByVTable(rva); // "uEm0100_0" vs "uEm0100"
if (precise) return precise;
```

Где взять `targetBase`? Он уже есть в `OnDamage(targetBase, dmg)` — это и есть указатель на врага. Никаких новых хуков.

### Level 3 — LIVE SCAN (безграничные решения, будущее)

`FactoryPointer` для `sEnemyManager`, `sHumanEnemyManager`, `cEnemyThink` — это глобальные менеджеры. 
```
realMgr = *(void**)(base + (0x199xxxx - 0x400000))
```
Просканировав его массив, можно без ожидания дамага узнать **кто сейчас в комнате** и сравнить каждый `*(vtable)` с `g_enemyTypes`.

Это открывает:
- **TacticalSwitch** без удара (видишь `Griffin 0x61` → сразу `Ranged Hunter`)
- **Nightmare Bitterblack Gransys** — замена `LotMgr<cLayoutSetEnemy>` на лету (подмена `emLevels`)
- **TargetLock** — точный yaw врага через его объект, а не через `targetBase[0x2D]`
- Сканирование `Size` поля — валидация границ объекта при чтении памяти (анти-краш)

Все три уровня опираются на один `EnemyTypes.Generated.h` — меняется только способ получения указателя.

---

## 3. Архитектура BUS — тренер с мегафоном

Раньше: `CombatIntel` → прямой вызов `PawnAI::SmartUtil()` — спагетти.

Сейчас (это база для всех модулей):

```
[CombatIntel] --Publishes--> [CombatBus] --Subscribe--> [SmartUtilitarian]
                                             |--> [SanitaryCordon]
                                             |--> [TacticalSwitch] (фаза 1.6)
                                             |--> [FutureModule]
```

- **CombatIntel** — единственный кто читает `mStudyFlag` + `types.tsv` + damage-хуки. Формирует `CombatReport` и кричит в `CombatBus::Publish()`.
- **PawnAI-модули** — независимы, каждый `Subscribe([](report){...})` и сам решает как менять инклинации. Добавить модуль = 10 строк, не трогая тренера.
- **types.tsv** — единожды раскладывается в `Generated.h`, дальше все модули работают с готовыми `EnemyTypeInfo`, не зная про TSV.

Пример `SmartUtilitarian` как слушатель:
```cpp
// src/modules/SmartUtilitarian.cpp
int g_busId = CombatBus::Instance().Subscribe([](const CombatReport& r){
    float conf = r.utilitarianConfidence; // уже посчитан тренером через mStudyFlag биты
    // плавный lerp Utilitarian → Scather/Challenger
});
```

Так мы перестаём быть ограниченными: любой новый тип врага из `types.tsv` автоматически появится в `CombatReport.enemies[]` без переписывания `PawnAI`.

---

## 4. Что именно мы добили в этой сессии

- [x] `tools/generate_bestiary.py` — корректно читает `parts[5]` и `parts[9]`, генерит RVA
- [x] `src/EnemyTypes.Generated.h` — 110 типов (от `0x05 Goblin` до `0xBC Daimon v2`), `FindByGroupId` + `FindByVTable`
- [x] `src/CombatBus.h` — шина `CombatReport` / `EnemyContact` с примером подписки
- [ ] `src/BestiaryData.h` → `BestiaryData.Generated.h` (мердж `types.tsv` + `bestiary.py` + `mStudyIdx`) — следующий PR
- [ ] `CombatIntel` публикует в шину, `PawnAI SmartUtil` слушает — черновик в `src/modules/` ниже

После этого `PawnAI` состоит из независимых модулей, а `types.tsv` — фундамент, а не костыль.

---

## 5. ЧитЭнджин → Плавная разработка: что изменилось

| Было (мучение) | Стало (производительно) |
|---|---|
| Ручной CE-скан `Yaw`, `mStudyFlag` каждый патч | `types.tsv` даёт `Size` + `VTable` — валидация без скана |
| Хардкод `0xA7000 + 0x7F0 + 0x1616` только для 6 врагов | 110 типов автоматом, без хардкода |
| Монолит `PawnAI.cpp 407 строк` | Bus + модули по 50 строк каждый |
| Бестиарий `0xFF` заглушки | Полный бестиарий с `uEmName` для UI |

Игра преобразилась именно потому что мы перестали гадать адресами и начали генерить их из исследования Atvaark.

---

**Следующий шаг:** добить `Bestiary.Complete.h` (соединить `groupId` из TSV + `bestiaryId/FlagID` из `bestiary.py` + `mStudyIdx` из наших тестов) и подключить `CombatBus` в `CombatIntel.cpp` / `PawnAI.cpp` как пример. Дальше — рефактор PawnAI на модули по этому образцу.
