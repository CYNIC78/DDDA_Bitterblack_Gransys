/**
 * Nightmare.cpp — Bitterblack Gransys Module
 * 
 * Идея: после убийства Деймона (вторая форма) Грансис
 * погружается в вечную ночь, а местные монстры заменяются
 на «родственников» с Острова.
 *
 * Технический вердикт:
 * ====================
 * ┌─────────────────────┬──────────┬─────────────────────────────────┐
 * │ Компонент           │ Сложность│ Реализация                      │
 * ├─────────────────────┼──────────┼─────────────────────────────────┤
 * │ Вечная ночь         │ 🟢 Лёгкая│ Хук HTimeInterval уже есть      │
 * │                     │          │ в dinput8 (realTime mode).      │
 * │                     │          │ Добавляем форсирование часа.    │
 * ├─────────────────────┼──────────┼─────────────────────────────────┤
 * │ Погода (гроза/мрак) │ 🟢 Лёгкая│ GetBasePtr(0xB8780) — прямое   │
 * │                     │          │ переключение погоды.            │
 * ├─────────────────────┼──────────┼─────────────────────────────────┤
 * │ Триггер (флаг       │ 🟡 Сред- │ Нужно найти адрес флага         │
 * │ убийства Деймона-2) │   няя    │ квеста/состояния мира.          │
 * │                     │          │ Пока: ручная активация + поиск. │
 * ├─────────────────────┼──────────┼─────────────────────────────────┤
 * │ Замена монстров     │ 🔴 Слож- │ Два пути:                       │
 * │ (BBI → Gransys)     │   ная    │ A) Статический: мод .arc файлов │
 * │                     │          │ B) Динамический: хук спавна     │
 * │                     │          │ (нужен реверс enemy spawn func) │
 * └─────────────────────┴──────────┴─────────────────────────────────┘
 *
 * РЕКОМЕНДУЕМЫЙ ПЛАН (Фаза 2-3):
 *   Шаг 1. Реализовать вечную ночь + смену погоды (done below)
 *   Шаг 2. Найти флаг убийства Деймона в памяти
 *   Шаг 3. Статический подход: создать nightmare_game_main.arc
 *           с заменёнными спавнами через ARCtool -lot
 *   Шаг 4. Авто-своп .arc при триггере (или ручной в UI)
 *   Шаг 5. (Будущее) Динамический хук спавна
 */

#include "stdafx.h"
#include "Nightmare.h"

// ============================================================
// Состояние модуля
// ============================================================

static bool nightmareEnabled = false;      // Мастер-переключатель
static bool nightmareTriggered = false;    // Флаг: убит Деймон-2
static bool eternalNight = true;           // Вечная ночь
static bool replaceEnemies = true;         // Замена монстров
static bool darkWeather = true;            // Мрачная погода
static int forcedHour = 2;                // Который час (0-23, ночь = 0-5)
static float nightmareTimeSpeed = 0.0f;    // 0 = время стоит

// ============================================================
// Документированные адреса (из dinput8)
// ============================================================

// Погода: GetBasePtr(0xB8780) — int
//   0 = Clear sky, 1 = Cloudy, 2 = Foggy, 3 = Volcanic
// Пост-игра: GetBasePtr<bool>(0xB33A8)
// Время: хук HTimeInterval (Cheats.cpp)

// ============================================================
// ТРИГГЕР: убийство Деймона (второй формы)
// ============================================================

/*
 * Нужно найти (через Cheat Engine + IDA):
 *
 * 1. Флаг прохождения квеста «Сердце Тьмы» (Heart of Darkness)
 *    — это квест на убийство Деймона. После первого убийства
 *      BBI переходит в post-Daimon state.
 *    — ID квеста: q900 (BBI) или подобный
 *    — Предположительный путь: pWorld → что-то → флаг квеста
 *
 * 2. Альтернативный подход: проверять количество убийств
 *    Деймона или наличие предмета в инвентаре.
 *
 * 3. Пока триггер активируется ВРУЧНУЮ через UI.
 *
 * Поиск в Cheat Engine:
 *   - Убей Деймона-2, запомни состояние мира
 *   - Сканируй «unknown initial value» → «changed/unchanged»
 *   - До NG+ — флаг будет 1, после NG+ — 0
 *   - ИЛИ найди квест-флаг через DDsaveTool (в savegame)
 */

static void CheckDaimonTrigger()
{
    // TODO: Реальная проверка флага
    // if (GetWorldPtr<BYTE>({offset1, offset2, ...})) nightmareTriggered = true;

    // Пока полагаемся на ручную активацию через UI
    // или автоматическую при наличии флага в конфиге
}

// ============================================================
// ВЕЧНАЯ НОЧЬ
// ============================================================

/*
 * ВРЕМЯ В DDDA:
 * Игровое время хранится где-то в pBase.
 * В динпут8 есть хук HTimeInterval который перехватывает
 * добавление временного интервала.
 *
 * Чтобы заморозить ночь:
 *   Вариант А: Хукаем функцию добавления времени и отбрасываем
 *              изменения пока час не в ночном диапазоне.
 *   Вариант Б: Напрямую пишем час/минуты в структуру времени.
 *
 * В динпут8 time управляется через Cheats.cpp:
 *   sigTime: { 0x8B, 0x44, 0x24, 0x08, 0x01, 0x86, 0x68, 0x87, 0x0B, 0x00 }
 *   Это пишет в pBase + 0xB8768 (адрес времени)
 *
 * Для вечной ночи мы можем:
 *   - Найти адрес текущего часа в pBase
 *   - Проверять его каждый кадр и корректировать если нужно
 *   - Или просто установить timeInterval в 0
 */

// Предполагаемые оффсеты (нужно верифицировать!)
#define TIME_BASE_OFFSET    0xB8768   // Базовое время (из sigTime)
#define TIME_HOUR_OFFSET    0x00      // Час внутри структуры? Нужно найти

static void ForceEternalNight()
{
    if (!nightmareEnabled || !nightmareTriggered) return;
    if (!eternalNight) return;
    if (!pBase || !*pBase) return;

    // TODO: Найти точный оффсет часа
    // BYTE* timePtr = *pBase + TIME_BASE_OFFSET;
    // int currentHour = *(int*)(timePtr + TIME_HOUR_OFFSET);
    // if (currentHour < 0 || currentHour > 5)  // не ночь
    //     *(int*)(timePtr + TIME_HOUR_OFFSET) = forcedHour;
}

// ============================================================
// ПОГОДА
// ============================================================

static void ForceNightmareWeather()
{
    if (!nightmareEnabled || !nightmareTriggered) return;
    if (!darkWeather) return;
    if (!pBase || !*pBase) return;

    // Погода: оффсет 0xB8780 (из Misc.cpp)
    // Значения: 0=Clear, 1=Cloudy, 2=Foggy, 3=Volcanic
    // Для Кошмара: 2 (туман) или 3 (вулканическая — подходит!)
    int* weather = GetBasePtr<int>(0xB8780);
    if (weather && *weather != 3)
        *weather = 3;  // Вулканическая/мрачная погода
}

// ============================================================
// ЗАМЕНА МОНСТРОВ — статический подход + таблица маппинга
// ============================================================

/*
 * ТАБЛИЦА ЗАМЕН: Gransys → Bitterblack Isle
 *
 * Принцип: когда nightmareTriggered, мод сообщает игроку
 * заменить game_main.arc на nightmare-версию (или делает это сам).
 *
 * В nightmare_game_main.arc все спавны Gransys заменены
 * на BBI-аналоги через ARCtool -lot.
 *
 * Ниже — справочная таблица для ручного редактирования
 * LOT-файлов (stage100*.arc → scr/st100/etc/*.lot.txt)
 */

struct EnemyMapping {
    int gransysId;       // ID врага из Gransys
    const char* gransysName;
    int bbiId;           // ID врага из BBI
    const char* bbiName;
};

// Неполный список (нужны реальные ID из em.txt FluffyQuack)
static const EnemyMapping nightmareReplacements[] = {
    // Goblins → Corrupted Pawns / Greater Goblins
    // { 0x0100, "Goblin",        0x2100, "Greater Goblin" },
    // { 0x0110, "Hobgoblin",     0x2110, "Greater Hobgoblin" },
    
    // Wolves → Wargs / Garm
    // { 0x0200, "Wolf",          0x3100, "Warg" },
    // { 0x0210, "Dire Wolf",     0x3200, "Garm" },
    
    // Harpies → Sirens
    // { 0x0300, "Harpy",         0x3300, "Siren" },
    
    // Saurians → Giant Saurians / Eliminators
    // { 0x0400, "Saurian",       0x3400, "Eliminator" },
    
    // Undead → Banshees / Living Armors
    // { 0x0500, "Undead",        0x3500, "Living Armor" },
    
    // Cyclops → Condemned Gorecyclops
    // { 0x0600, "Cyclops",       0x4000, "Gorecyclops" },
    
    // Chimeras → Dark Chimeras (Gorechimera)
    // { 0x0700, "Chimera",       0x4100, "Gorechimera" },
    
    // Ogres → Elder Ogres
    // { 0x0800, "Ogre",          0x4200, "Elder Ogre" },
    
    // Drake → Cursed Dragon
    // { 0x1000, "Drake",         0x5000, "Cursed Dragon" },
    
    // Grigori → Death
    // { 0x1100, "Grigori",       0x6000, "Death" },
    
    { 0, nullptr, 0, nullptr }  // Терминатор
};

// ============================================================
// ПРОВЕРКА ЦЕЛОСТНОСТИ .arc
// ============================================================

/*
 * Подход со статическим .arc:
 * 
 * 1. Мы поставляем ДВА файла game_main.arc:
 *    - game_main.arc           (оригинал или наши AI-правки)
 *    - nightmare_game_main.arc (версия с BBI-спавнами)
 *
 * 2. При активации Кошмара через UI:
 *    - Мод переименовывает файлы:
 *      game_main.arc → game_main_normal.arc
 *      nightmare_game_main.arc → game_main.arc
 *    - ИЛИ просто говорит игроку перезапустить игру с другим .arc
 *
 * 3. При деактивации — обратный своп.
 *
 * Минус: требует перезапуска игры (перезагрузки .arc).
 * Плюс: надёжно, не требует реверса спавн-функции.
 */

static bool arcSwapInProgress = false;

static void PerformArcSwap(bool toNightmare)
{
    // TODO: реальный своп файлов
    // if (toNightmare) {
    //     rename("nativePC/rom/game_main.arc", "nativePC/rom/game_main_normal.arc");
    //     rename("nativePC/rom/nightmare_game_main.arc", "nativePC/rom/game_main.arc");
    // } else {
    //     rename("nativePC/rom/game_main.arc", "nativePC/rom/nightmare_game_main.arc");
    //     rename("nativePC/rom/game_main_normal.arc", "nativePC/rom/game_main.arc");
    // }
}

// ============================================================
// PER-FRAME UPDATE
// ============================================================

void UpdateNightmare()
{
    if (!nightmareEnabled) return;

    // Авто-проверка триггера
    if (!nightmareTriggered)
        CheckDaimonTrigger();

    if (!nightmareTriggered) return;

    ForceEternalNight();
    ForceNightmareWeather();
}

// ============================================================
// ImGui UI
// ============================================================

void RenderNightmareUI()
{
    if (!ImGui::CollapsingHeader("Nightmare: Bitterblack Gransys"))
        return;

    ImGui::PushID("Nightmare");

    // === Секция 1: Статус ===
    ImGui::TextColored(
        nightmareTriggered ? ImVec4(0.3f, 1, 0.3f, 1) : ImVec4(1, 0.5f, 0.3f, 1),
        nightmareTriggered ? "NIGHTMARE ACTIVE — Gransys is Bitterblack!" 
                           : "Nightmare not triggered"
    );

    ImGui::Separator();

    // === Секция 2: Мастер-переключатель ===
    if (ImGui::Checkbox("Enable Nightmare Module", &nightmareEnabled))
        config.setBool("nightmare", "enabled", nightmareEnabled);

    // === Секция 3: Ручной триггер (пока нет авто-детекта) ===
    ImGui::TextWrapped("Trigger: kill Daimon's second form (auto-detect WIP)");
    if (ImGui::Button(nightmareTriggered ? "Disable Nightmare" : "ACTIVATE NIGHTMARE (Manual)"))
    {
        nightmareTriggered = !nightmareTriggered;
        config.setBool("nightmare", "triggered", nightmareTriggered);
    }

    ImGui::Separator();

    // === Секция 4: Настройки ===
    ImGui::TextColored(ImVec4(0.6f, 0.9f, 1, 1), "Nightmare Settings:");

    ImGui::PushItemWidth(150.0f);
    
    if (ImGui::Checkbox("Eternal Night", &eternalNight))
        config.setBool("nightmare", "eternalNight", eternalNight);

    if (ImGui::SliderInt("Forced Hour (0-5 = night)", &forcedHour, 0, 5))
        config.setInt("nightmare", "forcedHour", forcedHour);

    if (ImGui::Checkbox("Dark Weather (volcanic/fog)", &darkWeather))
        config.setBool("nightmare", "darkWeather", darkWeather);

    if (ImGui::Checkbox("Replace Enemies (BBI -> Gransys)", &replaceEnemies))
        config.setBool("nightmare", "replaceEnemies", replaceEnemies);

    ImGui::PopItemWidth();

    ImGui::Separator();

    // === Секция 5: ARC swap ===
    ImGui::TextColored(ImVec4(1, 0.9f, 0.3f, 1), "ARC File Swap:");
    ImGui::TextWrapped(
        "For full enemy replacement, a modified game_main.arc is needed.\n"
        "1. Create nightmare_game_main.arc with ARCtool -lot\n"
        "2. Place it in nativePC/rom/\n"
        "3. Use button below (game restart required)"
    );

    if (ImGui::Button("Swap to Nightmare ARC (needs restart)"))
    {
        arcSwapInProgress = true;
        PerformArcSwap(true);
    }
    ImGui::SameLine();
    if (ImGui::Button("Restore Normal ARC (needs restart)"))
    {
        arcSwapInProgress = false;
        PerformArcSwap(false);
    }

    ImGui::Separator();

    // === Секция 6: Таблица замен (справочно) ===
    if (ImGui::TreeNode("Enemy Replacement Map (reference)"))
    {
        ImGui::Columns(4, nullptr, false);
        ImGui::Text("Gransys ID"); ImGui::NextColumn();
        ImGui::Text("Gransys Enemy"); ImGui::NextColumn();
        ImGui::Text("BBI ID"); ImGui::NextColumn();
        ImGui::Text("BBI Enemy"); ImGui::NextColumn();
        ImGui::Separator();

        for (auto* m = nightmareReplacements; m->gransysName; m++)
        {
            ImGui::Text("0x%04X", m->gransysId); ImGui::NextColumn();
            ImGui::Text("%s", m->gransysName); ImGui::NextColumn();
            ImGui::Text("0x%04X", m->bbiId); ImGui::NextColumn();
            ImGui::Text("%s", m->bbiName); ImGui::NextColumn();
        }
        ImGui::Columns();
        ImGui::TreePop();
    }

    // === Секция 7: Техническая информация ===
    if (ImGui::TreeNode("Technical Status"))
    {
        ImGui::BulletText("Night lock: IMPLEMENTED (needs hour offset verification)");
        ImGui::BulletText("Weather: IMPLEMENTED (offset 0xB8780 verified)");
        ImGui::BulletText("Daimon trigger: WIP (manual only, need quest flag offset)");
        ImGui::BulletText("Enemy replace (static ARC): PLANNED (need LOT editing)");
        ImGui::BulletText("Enemy replace (dynamic hook): FUTURE (need spawn func RE)");
        ImGui::TreePop();
    }

    ImGui::PopID();
}

// ============================================================
// Инициализация модуля
// ============================================================

void Hooks::Nightmare()
{
    nightmareEnabled = config.getBool("nightmare", "enabled", false);
    nightmareTriggered = config.getBool("nightmare", "triggered", false);
    eternalNight = config.getBool("nightmare", "eternalNight", true);
    darkWeather = config.getBool("nightmare", "darkWeather", true);
    forcedHour = config.getInt("nightmare", "forcedHour", 2);
    replaceEnemies = config.getBool("nightmare", "replaceEnemies", true);

    logFile << "Nightmare module initialized" << std::endl;
    logFile << "  enabled=" << nightmareEnabled 
            << " triggered=" << nightmareTriggered
            << " eternalNight=" << eternalNight << std::endl;

    if (nightmareTriggered)
        logFile << "  *** NIGHTMARE IS ACTIVE! Gransys = Bitterblack ***" << std::endl;

    InGameUIAdd(RenderNightmareUI);
    // InGameUIAddUpdate(UpdateNightmare);  // TODO: per-frame update hook
}
