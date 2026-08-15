# Zip 32 — ActScan: найти указатель на текущее действие

**Цель одного захода:** найти внутри 29-КБ тела `uEm0100` поле, в котором лежит
указатель на объект текущего действия (`cEm0100Act*`). Это поле даёт **смерть**
(`ActDie` / `ActDeadBody`), **таунт** (`ThreatHowl` / `PartyDance`) и точку
подмены — сразу для всех 35 видов врагов.

Патч чисто диагностический: **ничего не пишет в игру**, только читает и логирует.

---

## 1. Что добавляем

Один новый скан + одна колонка в json. Правки в `src/devtools/DevTools.cpp`.

### 1.1 Подключить ActMap

В начало файла, к остальным `#include`:

```cpp
#include "../ActMap.Generated.h"
```

### 1.2 Расширить `ActorDump`

Найти `struct ActorDump` (около строки 719) и добавить **в конец структуры**,
перед закрывающей `};`:

```cpp
    // Zip 32 — ActScan
    uint32_t    actOff;      // смещение в теле, где найден указатель на Act
    uintptr_t   actPtr;      // сам указатель на объект действия
    uint32_t    actVtRva;    // vtable RVA этого объекта
    const char* actName;     // "ThreatHowl" / "Die" / ...
    const char* actCat;      // "taunt" / "death" / "attack" / ...
    int         actHits;     // сколько всего кандидатов найдено в теле
```

### 1.3 Функция сканирования

Вставить **перед** `static void DumpActorsFrom(...)` (около строки 2380):

```cpp
// Zip 32 — ActScan.
// Идём по телу 29 КБ, читаем каждый dword как указатель. Если он похож на
// кучу и его [0] совпадает с vtable RVA из ActMap — это объект действия.
// Первый найденный кандидат = самый вероятный слот «текущее действие».
static void ScanActSlot(ActorDump& A)
{
    A.actOff = 0; A.actPtr = 0; A.actVtRva = 0;
    A.actName = 0; A.actCat = 0; A.actHits = 0;

    const uintptr_t base = DevTools::ModuleBase();
    if (!base) return;

    // 29 КБ тела. Шаг 4 байта. Пропускаем первые 256 (трансформ, dump25).
    for (uint32_t off = 0x100; off < 0x7400; off += 4) {
        uintptr_t cand = 0;
        if (!RdPtr((void*)(A.ptr + off), &cand)) continue;
        if (!cand || !LooksHeap(cand)) continue;

        uintptr_t vt = 0;
        if (!RdPtr((void*)cand, &vt)) continue;
        if (vt <= base) continue;

        uint32_t rva = (uint32_t)(vt - base);
        const ActMap::Act* a = ActMap::FindByVt(rva);
        if (!a) continue;

        A.actHits++;
        if (!A.actPtr) {                 // запоминаем первый
            A.actOff   = off;
            A.actPtr   = cand;
            A.actVtRva = rva;
            A.actName  = a->name;
            A.actCat   = a->category;
        }
    }
}
```

### 1.4 Вызвать её

В `DumpActorsFrom`, сразу **после** строки
`A.win60Ok = Rd((void*)(p + 0x6000), A.win60, 64);` добавить:

```cpp
        ScanActSlot(A);
```

### 1.5 Вывести в json

Найти `fprintf` дампа акторов (около строки 3038). В конец форматной строки,
**перед** `\"win5b\":\"`, дописать:

```
\"actOff\":\"0x%04X\",\"actPtr\":\"0x%08X\",\"actVt\":\"0x%07X\",\"act\":\"%s\",\"actCat\":\"%s\",\"actHits\":%d,
```

и в список аргументов, перед `g_act[i].fat29 ? ...`, добавить:

```cpp
            g_act[i].actOff, (unsigned)g_act[i].actPtr, g_act[i].actVtRva,
            g_act[i].actName ? g_act[i].actName : "-",
            g_act[i].actCat  ? g_act[i].actCat  : "-",
            g_act[i].actHits,
```

### 1.6 Показать в UI

В таблице акторов (около строки 3704), где печатается `gid`/`st14`, дописать
колонку:

```cpp
        ImGui::SameLine();
        ImGui::TextColored(
            g_act[i].actCat && g_act[i].actCat[0] == 'd'
                ? ImVec4(1.0f, 0.4f, 0.4f, 1.0f)     // death — красным
                : ImVec4(0.6f, 0.9f, 0.6f, 1.0f),
            "act=%s@+%04X (%d)",
            g_act[i].actName ? g_act[i].actName : "-",
            g_act[i].actOff, g_act[i].actHits);
```

---

## 2. Протокол теста в игре

Нужен **один** гоблин и **три** нажатия HUNT. Не отходить от тела.

| шаг | что делать | что записать |
|---|---|---|
| 1 | Подойти к лагерю. Гоблин **не тронут**, стоит/патрулирует. HUNT | `actOff`, `act`, `actCat` |
| 2 | Ударить **один раз**, не убивая. Сразу HUNT | изменился ли `act`? |
| 3 | Дождаться таунта (топанье/вой). HUNT в этот момент | ждём `act=ThreatHowl` или `PartyDance` |
| 4 | Убить. **Не отходить.** HUNT | ждём `act=Die` или `DeadBody` |

Каждый HUNT пишет json — просто сохрани все четыре файла.

### 2.1 Что считать успехом

- **`actOff` одинаков** во всех четырёх снимках → слот найден, это стабильное поле.
- **`act` меняется** по смыслу: `Wait` → `Dmg*` → `ThreatHowl` → `Die`.
- **`actHits` небольшой** (1–3). Если 20+ — фильтр слишком широкий,
  скажи мне число, я сужу.

### 2.2 Если `act` пустой у всех

Значит указатель на действие лежит не прямо в теле, а через промежуточный
объект (например `cThinkMgr`). Тогда следующий шаг — двухуровневый скан.
Не страшно, просто скажи результат.

---

## 3. Почему это безопасно

- патч **только читает**: `RdPtr` / `Rd` уже защищены проверками в проекте;
- `LooksHeap` отсекает мусор до разыменования;
- скан 29 КБ шагом 4 = **7360 итераций** на тело, максимум 32 тела —
  это доли миллисекунды, и только по кнопке HUNT, не в тике 150 мс;
- ничего не пишется в память игры.

---

## 4. Что это даст сразу после успеха

| находка | что открывает |
|---|---|
| `actOff` | смерть без HP — `ActDie` в `CombatIntel` |
| то же поле | таунт виден в реальном времени |
| `actPtr` → `cMotionCtrl` | скорость анимации (§21) |
| работает для 35 видов | не только гоблин |

**Смерть по состоянию закрывает «энкаунтер кончился» и разблокирует полку MEMORY.**
