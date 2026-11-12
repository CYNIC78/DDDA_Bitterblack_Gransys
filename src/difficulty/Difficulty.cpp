/**
 * Difficulty — слой сложности, линия 87. Подробности — в Difficulty.h.
 *
 * Сборка 87.0: фундамент слоя.
 *   1. Детекция сборки игры по TimeDateStamp (Steam / GOG).
 *   2. Фикс капа 9 999 макс. HP у людей-врагов: операнд инструкции, который
 *      ссылается на ванильную константу 9999.0, перенаправляется на наш
 *      живой глобал. Патч ОДИН раз, значение крутится в рантайме.
 *   3. Разведзонд воронки использования предметов: хук функции применения
 *      предмета в режиме чистого лога. Поведение игры не меняется. Данные
 *      нужны билду 87.4 (гейт кулдауна).
 *      Ревизия 1 (87.01): писала первый вызов с каждого адреса возврата.
 *      Ревизия 2 (87.02): пишет КАЖДЫЙ вызов (с лимитом 300) вместе с
 *      this-указателем и первыми аргументами — чтобы сопоставить вызовы
 *      с действиями игрока и отделить игрока от пешек.
 *
 * Сборка 87.1: хард-режим без подачек с мешочками. Ванильный хард-режим
 * конвертирует мешочки из врагов в Giant Pouch и сыпет лишний мешочек
 * при смерти врага; обе подачки убираются одно-/шестибайтовыми патчами
 * (ключи `removeGiantPouchConversion`, `removeExtraPouch`; по умолчанию
 * ВЫКЛЮЧЕНЫ — живая проверка возможна только в хард-режиме, приёмка по
 * байт-верификации патч-хелпера).
 *
 * Сборка 87.2: хард-режим без двойного опыта (ключ `removeDoubleExp`,
 * по умолчанию ВЫКЛЮЧЕН). Донор ставил 5-байтовый E9-хук в свою DLL ради
 * рантайм-переключателя; у нас переключатель — сам ini-ключ, поэтому
 * делаем статический эквивалент: на развилке JE +0x50 заменяется на
 * JMP +0x50 (1 байт), бонусная ветка х2 становится недостижимой, обычный
 * режим не задет. Направление веток подтверждено детуром донора _hook_exp.
 *
 * Сборка 87.3: фикс таблицы опыта для уровней 51–100 (ключ
 * `fixLevel51Curve`, по умолчанию ВКЛЮЧЁН — действует на всех
 * сложностях). Ванильный множитель x4 на этом отрезке заменён на x13
 * (между x11 предыдущего диапазона и x15 следующего): до 100-го уровня
 * нужно примерно на 80% больше опыта, до 50-го не меняется ничего.
 * Механизм повторён с донора: подмена указателя загрузчика таблицы
 * обёрткой, которая после загрузки правит запись уровня 51 в памяти.
 * Установка в Init (= DllMain PROCESS_ATTACH, как у донора), значение
 * ячейки перед подменой сверяется с известным оригиналом.
 *
 * Сборка 87.35: подготовка гейта кулдауна/блока расходников (87.4).
 * Зонд, ревизия 3: первые 40 применений дополнительно пишут дамп полей
 * аргумента-предмета (первые 0x40 байт и, если по +4 указатель, 0x60
 * байт по нему) — по скрипт-прогону с известными предметами это даст
 * раскладку категории/типа/лечения именно в нашей точке (воронке), а не
 * в донорской точке меню. Поведение игры не меняется.
 *
 * Сборка 87.44 (фикс тоста): блок 87.43 работал, но тост никто не
 * видел — он был зарегистрирован через InGameUIAdd, а такие колбэки
 * рисуются ТОЛЬКО внутри открытой F12-панели. В бою панель закрыта, и
 * блок без объяснения ощущался как «игра сломана». Регистрация
 * перенесена на InGameUIAddWindow (вызывается каждый кадр в
 * onEndScene, независимо от панели — тот же механизм, что
 * AnimProbeAlwaysTick в DevTools). Заодно тост чуть заметнее: 1800 мс,
 * фейд последние 500 мс.
 *
 * Сборка 87.43 (ХОТФИКС КРАША 87.42): блок-путь делал `ret 8` — и падал
 * при блоке хоткея в бою. Криминалистика лога: наш `ret` высадил
 * процессор на адрес из кучи (0x06AD7290, DEP, «не в одном модуле»),
 * регистры эпилога хоткей-функции оказались мусором — недочистили 16
 * байт стека. Виновник найден в дизассемблере донора: воронка берёт не
 * два стековых аргумента, а ШЕСТЬ, эпилог у неё `ret 0x18` — донорский
 * `_hook_use` на том же пути в своей ветке блока делает ровно
 * `ret 0x18`. Блок-путь исправлен на `ret 24` (0x18), eax=1 как у
 * донора; установка теперь дополнительно сканирует тело воронки на
 * `C2 18 00` и печатает улики в лог (решение — по донору+криминалистике).
 *
 * Сборка 87.42: ГЕЙТ ВКЛЮЧЁН ПО-НАСТОЯЩЕМУ. Разведка 87.41 дала три
 * факта: (1) краш 87.4 устранён возвратом полевого пути пропуска;
 * (2) гейт без ложных срабатываний опознал все боевые применения
 * расходников Возникшего; (3) все четыре точки вызова воронки НЕ чистят
 * стек после `call` — воронка сама делает `ret 8`, а хоткей-вызыватель
 * сразу после вызова затирает al, то есть блок с возвратом 0 безопасен.
 * Теперь в бою применение расходника Возникшим (меню или хоткей) не
 * доходит до оригинальной функции: предмет не тратится, показывается
 * тост. Пешки и лечение пешки не трогаются, вне боя всё как обычно.
 *
 * Сборка 87.41 (ХОТФИКС КРАША 87.4): краш случился ВНЕ боя, на пути
 * пропуска — детур 87.4 навязал оригиналу свою конвенцию очистки стека
 * (вызов через C-обёртку + `ret 8`), ничем не подтверждённую. Путь
 * ПРОХОДА возвращён к полевому варианту 87.02–87.38 (прозрачный `jmp`
 * в трамплин), поэтому обычные применения снова безопасны. Путь БЛОКА в
 * этой сборке — разведка: решение «заблокировал бы» пишется в журнал
 * (строки «gate WOULD-BLOCK»), а применение всё равно проходит.
 * Конвенция возврата для будущего блока читается из кода игры: у точек
 * вызова воронки ищется `add esp, xx` сразу после `call` (строки
 * «gate cleanup probe»). Реальный блок включим в следующей сборке.
 *
 * Сборка 87.4: ГЕЙТ РАСХОДНИКОВ В БОЮ. Возникший в бою больше не может
 * применить расходник (категория 2 — лечилки, ягоды и т.п.) ни через
 * меню, ни через хоткей: обе дороги сходятся в воронку, и гейт стоит на
 * ней. Пешки не трогаются (их применения приходят с телом пешки),
 * лечение ПЕШКИ игроком тоже остаётся возможным — осознанная тактика.
 * Детекция боя — платформенный IsInCombat (урон + боевые действия
 * врагов + цель пешки, с окном выхода), тело Возникшего резолвится
 * динамически через Aggro::ResolveMemberBody, категория предмета — по
 * подтверждённой полем формуле ([запись+0x38] & 0x1F == 2). Все
 * проверки фейл-опен: любая неуверенность разрешает применение.
 * Детур воронки перестал быть прозрачным «пробросом в трамплин»: теперь
 * он идёт через UseGate (журнал -> гейт -> оригинал или блок) и
 * возвращает результат вызывающему по конвенции thiscall (ret 8).
 * Блок = оригинал не вызывается вообще (предмет не тратится), тост в
 * ImGui-оверлее. Ключ ini: [difficulty] combatUseGate = block | off.
 * Фонарь и инструменты гейт не трогает (другая категория); периапты
 * уточняются отдельно — в поле ещё не применялись.
 *
 * Сборка 87.38: фикс таблицы опыта ЗАРАБОТАЛ на GOG. Дампы 87.37
 * показали, что на GOG-сборке поля записи лежат зеркально относительно
 * донорского обхода (множитель при +4, уровень при +8, а не наоборот) —
 * донорский поиск «уровень при +4» здесь никогда не срабатывал. Обход
 * теперь пробует обе раскладки и правит подтверждённое поле-множитель
 * (4.0 -> 13.0 у записи уровня 51); в логе появляется строка [OK] с
 * именем найденной раскладки. Разведочный дамп остаётся на случай, если
 * раскладка когда-нибудь снова не совпадёт.
 *
 * Сборка 87.37: разведка таблицы опыта, ревизия 4. В 87.35/87.36 запись
 * уровня 51 в таблице не нашлась, а распечатка показала необычную
 * раскладку записей. Теперь каждый вызов загрузчика пишет в журнал строку
 * с параметрами таблицы и (первые 8 вызовов) дамп всех записей по 8
 * двойных слов, плюс попытку прочитать указатели-имена как строки; в
 * конце сессии — итог (сколько раз загрузчик вызван, применился ли фикс).
 * Дамп предмета расширен до 0x80 байт записи, чтобы захватить поле
 * лечения при +0x70, которое видит донор.
 *
 * Сборка 87.36 (ХОТФИКС КРАША 87.35): краш «при спавне монстров» —
 * класс отложенной порчи из docs/CRASH_HISTORY (волны 1–2): голые
 * SEH-чтения по недоверенным указателям могут попасть в страницу-сторож
 * чужого стека, «съедают» её, и поток падает позже и в другом месте.
 * Все чтения слоя переведены на фундамент: RegionOk (VirtualQuery,
 * память не трогает) + Rd, каждый указатель перед розыском проходит
 * LooksHeap. Плюс диагностика раскладки таблицы опыта: в 87.35 запись
 * уровня 51 не нашлась (count=13) — теперь при таком исходе в лог
 * печатаются заголовок и первые записи таблицы.
 *
 * Адреса — из реверса мода HardPlus 1.3 (кредит в README), таблица и
 * происхождение — в `docs/DIFFICULTY_ADDRESSES.md`.
 */
#include "stdafx.h"
#include "Difficulty.h"
#include "runtime/LogMemSession.h"   // SessionNote объявлен здесь, а не в LogMem.h
#include "runtime/MemProbe.h"        // фундамент: RegionOk / Rd / LooksHeap
#include "runtime/AggroWatch.h"      // ResolveMemberBody — кто есть кто в партии
#include "CombatIntel.h"             // IsInCombat
#include "MinHook/MinHook.h"

namespace {

// ---------------------------------------------------------------------------
// Детекция сборки
// ---------------------------------------------------------------------------

enum BuildKind { BUILD_UNKNOWN = 0, BUILD_STEAM = 1, BUILD_GOG = 2 };

BYTE*     g_exeBase = nullptr;
BuildKind g_build   = BUILD_UNKNOWN;

// Адреса донорской таблицы даны для ImageBase 0x400000. Пересчитываем от
// реального базового адреса — он стандартный, но считать от фактического
// дешевле и честнее.
BYTE* Va(DWORD steamVa, DWORD gogVa)
{
    DWORD donor = (g_build == BUILD_GOG) ? gogVa : steamVa;
    return g_exeBase + (donor - 0x400000);
}

BuildKind DetectBuild(DWORD& stampOut)
{
    BYTE* base = (BYTE*)GetModuleHandle(nullptr);
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    stampOut = nt->FileHeader.TimeDateStamp;
    if (stampOut == 0x5A314C31) return BUILD_STEAM; // оригинальный Steam-экзешник
    if (stampOut == 0x5936ABAB) return BUILD_GOG;   // релиз GOG
    return BUILD_UNKNOWN;
}

// ---------------------------------------------------------------------------
// Патч-хелпер: верификация -> запись -> кэш инструкций. Идемпотентен.
// Три исхода в логе: [OK] / [OK] (уже применено) / [FAIL] (байты не те —
// запись НЕ производится, в логе фактические байты для разбора).
// ---------------------------------------------------------------------------

bool PatchBytes(LPCSTR what, BYTE* site,
                const BYTE* expect1, const BYTE* expect2,
                const BYTE* newBytes, int len)
{
    if (memcmp(site, newBytes, len) == 0)
    {
        logFile << "Difficulty: [OK]    " << what << " (already applied)" << std::endl;
        return true;
    }
    bool match = (memcmp(site, expect1, len) == 0) ||
                 (expect2 != nullptr && memcmp(site, expect2, len) == 0);
    if (!match)
    {
        logFile << "Difficulty: [FAIL]  " << what
                << ": unexpected bytes at " << site << ", untouched:";
        for (int i = 0; i < len && i < 16; i++)
        {
            char b[8];
            sprintf_s(b, " %02X", site[i]);
            logFile << b;
        }
        logFile << std::endl;
        return false;
    }
    DWORD oldProt = 0;
    if (!VirtualProtect(site, len, PAGE_EXECUTE_READWRITE, &oldProt))
    {
        logFile << "Difficulty: [FAIL]  " << what << ": VirtualProtect denied" << std::endl;
        return false;
    }
    memcpy(site, newBytes, len);
    VirtualProtect(site, len, oldProt, &oldProt);
    FlushInstructionCache(GetCurrentProcess(), site, len);
    logFile << "Difficulty: [OK]    " << what << std::endl;
    return true;
}

// ---------------------------------------------------------------------------
// 1. Фикс капа 9 999 макс. HP у людей-врагов
// ---------------------------------------------------------------------------
// Ваниль: инструкция ссылается на константу 9999.0 в данных игры, и любой
// человек-враг с большим запасом молча обрезается. Перенаправляем операнд
// (4 байта = абсолютный адрес константы внутри инструкции) на наш флоат.
// Ожидаемые значения операнда — адреса ванильной константы (для Steam их
// два известных варианта, для GOG один) — и есть верификация.

float g_humanHpCap = 99999.0f; // живой глобал: цель перенаправленного операнда

// Постоянное хранилище для 4-байтовых значений патча (нельзя брать адрес
// стекового локального — патч живёт вечно).
DWORD g_capOperandNew;  // = (DWORD)&g_humanHpCap

bool ApplyHumanHpCap()
{
    float cap = config.getFloat("difficulty", "humanMaxHpCap", 99999.0f);
    if (cap <= 0.0f)
    {
        logFile << "Difficulty: [SKIP]  human HP cap: disabled in ini (vanilla)" << std::endl;
        return true;
    }
    g_humanHpCap = cap;
    g_capOperandNew = (DWORD)&g_humanHpCap;

    // Таблица: CAP_SITE / CAP_CONST / CAP_CONST_ALT.
    const DWORD capSiteSteam = 0x00782369, capSiteGog = 0x0077F9D9;
    const DWORD capConSteam  = 0x0161CA24, capConGog  = 0x015FD534;
    const DWORD capAltSteam  = 0x01467D90, capAltGog  = 0x015FD534;

    static DWORD exp1, exp2;
    exp1 = (g_build == BUILD_GOG) ? capConGog : capConSteam;
    exp2 = (g_build == BUILD_GOG) ? capAltGog : capAltSteam;

    BYTE* site = Va(capSiteSteam, capSiteGog);
    bool ok = PatchBytes("human HP cap operand", site,
                         (BYTE*)&exp1, (BYTE*)&exp2,
                         (BYTE*)&g_capOperandNew, 4);
    if (ok)
        logFile << "Difficulty: human HP cap value = " << (int)cap << std::endl;
    return ok;
}

// ---------------------------------------------------------------------------
// 2. Хард-режим: убираем «плюшки» с мешочками (87.1)
// ---------------------------------------------------------------------------
// Ванильный хард-режим раздаёт две подачки, компенсирующие вес: мешочки из
// врагов превращаются в Giant Pouch, а при смерти врага падает лишний
// мешочек. ХардПлюс (автор GonorreaNuclear, кредит в README) обе подачки
// убирает; мы воспроизводим это байт-в-байт по его таблице, через наш
// верифицирующий патч-хелпер. Оба патча по умолчанию ВЫКЛЮЧЕНЫ: проверить
// их вживую можно только в хард-режиме, приёмка — по байт-верификации.

bool ApplyPouchPatches()
{
    // (а) Мешочки больше не конвертируются в Giant Pouch: JE -> JMP.
    if (config.getBool("difficulty", "removeGiantPouchConversion", false))
    {
        static const BYTE expG = 0x74, newG = 0xEB;
        PatchBytes("hard mode: no Giant Pouch conversion",
                   Va(0x00461084, 0x004609F4),
                   &expG, nullptr, &newG, 1);
    }
    else
    {
        logFile << "Difficulty: [SKIP]  giant pouch conversion: disabled in ini"
                << std::endl;
    }

    // (б) Лишний мешочек при смерти врага: условный переход заменяется
    // безусловным (в обход блока выдачи) + NOP на хвосте. 6 байт.
    if (config.getBool("difficulty", "removeExtraPouch", false))
    {
        static const BYTE expE[6] = { 0x0F, 0x84, 0x5B, 0x01, 0x00, 0x00 };
        static const BYTE newE[6] = { 0xE9, 0x5C, 0x01, 0x00, 0x00, 0x90 };
        PatchBytes("hard mode: no extra pouch drop",
                   Va(0x0088CD22, 0x0088A752),
                   expE, nullptr, newE, 6);
    }
    else
    {
        logFile << "Difficulty: [SKIP]  extra pouch drop: disabled in ini"
                << std::endl;
    }
    return true; // строки [FAIL] уже в логе; на запуск слоя это не влияет
}

// ---------------------------------------------------------------------------
// 3. Хард-режим: убираем двойной опыт (87.2)
// ---------------------------------------------------------------------------
// Ванильный хард-режим даёт двойной опыт — подачка, которая позволяет
// «проскочить» сложность. На развилке стоит условный переход: взявший его
// получает опыт БЕЗ бонуса (так ходит обычный режим), а хард-режим падает
// сквозь него в бонусную ветку (х2). Это подтверждается детуром донора:
// его хук первым делом перевыставляет тот же JE на «чистый» путь и лишь
// потом решает, пускать ли хард в бонусную ветку.
// Нам рантайм-переключатель не нужен (у нас сам ini-ключ — переключатель),
// поэтому вместо донорского 5-байтового E9-хука в чужую DLL делаем
// статический эквивалент на 1 байт: JE -> JMP на тот же «чистый» путь.
// Бонусная ветка становится недостижимой; обычный режим не задет (он и так
// всегда прыгал); байты, куда могли бы прыгнуть другие ветки, не трогаем.
//
// Раскладка сайта подтверждена верифицирующим буфером самого донора
// (5 байт: 74 50 8B 15 + младший байт адреса GAME_STATE): сразу за
// двухбайтовой веткой идёт `mov edx, [GAME_STATE]` (6 байт), а за ним —
// начало бонусного пути. Взявший ветку «чистый» путь эту загрузку
// пропускает и в ней не нуждается, поэтому прыжок через неё безопасен.

bool ApplyNoDoubleExpPatch()
{
    if (!config.getBool("difficulty", "removeDoubleExp", false))
    {
        logFile << "Difficulty: [SKIP]  double exp removal: disabled in ini"
                << std::endl;
        return true;
    }
    // Ваниль: 74 50 (JE +0x50, цель = «опыт без бонуса»).
    // Станет:  EB 50 (JMP +0x50, туда же, но безусловно).
    static const BYTE expX[2] = { 0x74, 0x50 };
    static const BYTE newX[2] = { 0xEB, 0x50 };
    PatchBytes("hard mode: no double experience",
               Va(0x004434AA, 0x00442F5A),
               expX, nullptr, newX, 2);
    return true;
}

// ---------------------------------------------------------------------------
// 4. Разведзонд воронки использования предметов (данные для билда 87.4)
// ---------------------------------------------------------------------------
// Хукаем саму функцию применения (оригинальная цель вызова из меню, таблица
// USE_ORIG), а не один из её вызывателей: тогда через нас пройдут ВСЕ пути —
// меню, хоткеи и всё остальное. Детур прозрачный для любой конвенции вызова:
// регистры сохраняются, управление передаётся в трамплин МинХука.

// Ревизия 2 (87.02). Ревизия 1 писала только ПЕРВЫЙ вызов с каждого адреса
// возврата — поэтому из лога 87.01 нельзя понять, какой адрес сколько раз
// вызывался и какое действие игрока за ним стояло. Ревизия 2 пишет КАЖДЫЙ
// вызов (с номером по порядку и жёстким лимитом) вместе с this-указателем
// и первыми двумя стековыми аргументами. По номерам вызовов мы сопоставим
// адреса с действиями («лечилка из инвентаря», «хоткей», «пешка сама
// съела») и решим, кого гейтить в билде 87.4. Поведение игры не меняется.
//
// Ревизия 3 (87.35). Для гейта 87.4 нужно уметь по второму аргументу
// воронки понимать, ЧТО за предмет применяется (категория расходника,
// фонарь/метательное, количество лечения). Донорские смещения сняты с его
// точки в меню, а наша точка — сама воронка, поэтому раскладку полей
// подтверждаем своим дампом: первые 40 применений дополнительно пишут
// первые 0x40 байт аргумента-предмета и (если по +4 лежит указатель)
// первые 0x60 байт того, на что он показывает. Это покрывает обе
// гипотезы: «аргумент = сам предмет» и «аргумент = обёртка, предмет
// внутри». Поведение игры по-прежнему не меняется.

// Правило фундамента (см. docs/CRASH_HISTORY, волны 1–2): чужую память
// НЕЛЬЗЯ «прощупывать» голым SEH-чтением по недоверенным указателям.
// Попадание такого чтения в страницу-сторож (PAGE_GUARD) чужого стека
// «съедает» сторожа, и поток падает ПОСЛЕ и В ДРУГОМ МЕСТЕ — например,
// на спавне монстров, когда стек растёт под рекурсией загрузчика.
// Поэтому здесь любой доступ идёт через фундамент: сначала VirtualQuery
// (RegionOk — саму память не трогает, сторожа остаются целы), потом
// чтение через Rd; любой указатель, за которым мы идём, сначала проходит
// LooksHeap. К крашу билда 87.35 привело нарушение этого правила:
// чтения шли по цепочке неподтверждённых указателей таблицы опыта.
static bool DiffRead(uintptr_t addr, void* buf, size_t n)
{
    if (!addr || !buf || !n) return false;
    if (!Runtime::Mem::RegionOk(addr, n)) return false;
    return Runtime::Mem::Rd((const void*)addr, buf, n);
}

const LONG kDumpCap = 40;   // дамп предмета — только для первых применений

static void DumpDwords(const char* what, LONG seq, DWORD addr, int dwords)
{
    DWORD buf[32];
    int n = (dwords > 32) ? 32 : dwords;
    if (!DiffRead(addr, buf, n * 4))
    {
        char line[120];
        sprintf_s(line, "Difficulty: item-probe seq=%d %s @0x%08X unreadable",
                  (int)seq, what, addr);
        logFile << line << std::endl;
        return;
    }
    char line[512];
    int off = sprintf_s(line, "Difficulty: item-probe seq=%d %s @0x%08X:",
                        (int)seq, what, addr);
    for (int i = 0; i < n && off > 0; i++)
    {
        int w = sprintf_s(line + off, sizeof(line) - off, " %08X", buf[i]);
        if (w < 0) break;
        off += w;
    }
    logFile << line << std::endl;
}

static void ProbeItemFields(LONG seq, DWORD a1)
{
    if (!a1 || !Runtime::Mem::LooksHeap((uintptr_t)a1)) return;
    DumpDwords("ctx", seq, a1, 16);           // первые 0x40 байт аргумента
    DWORD inner = 0;
    if (DiffRead((uintptr_t)a1 + 4, &inner, 4) &&
        Runtime::Mem::LooksHeap((uintptr_t)inner))
    {
        // 32 двойных слова = 0x80 байт: захватываем поле лечения, которое
        // у донора лежит при +0x70 записи предмета.
        DumpDwords("ctx+4", seq, inner, 32);  // первые 0x80 байт по [arg+4]
    }
}

volatile LONG g_probeCalls = 0;
const  LONG   kProbeCap = 300;   // защита от раздувания лога

// Счётчик уникальных адресов возврата — только для итоговой строки в
// Shutdown, чтобы разбирать лог было проще.
DWORD g_probeUnique[64];
int   g_probeUniqueCount = 0;

// ---------------------------------------------------------------------------
// Гейт расходников в бою (87.4). Политика по умолчанию — жёсткий блок:
// Возникший в бою не может применить расходник (категория 2) ни через
// меню, ни через хоткей — потому что обе дороги сходятся в эту воронку.
// Пешки НЕ трогаются: их применения приходят с a0 = тело пешки, а гейт
// реагирует только на a0 = тело Возникшего. Лечение ПЕШКИ игроком тоже
// проходит (там a0 = пешка) — это осознанно сохранённая тактика.
// Все проверки фейл-ОПЕН: не сумели определить бой/тело/категорию —
// применение разрешается. Игра не должна страдать от нашей неуверенности.

int   g_gateMode = 0;        // 0 = off, 1 = block
volatile LONG g_gateBlocks = 0;
const  LONG   kGateLogCap = 30;
DWORD g_toastUntilMs = 0;    // до какого момента показывать тост

// Тост рисуется через общий ImGui-оверлей платформы. Шрифт 1.48 — только
// ASCII, поэтому текст английский.
// ВАЖНО (87.44): тост обязан быть виден ВСЕГДА, а не только при
// открытой F12-панели. Поэтому регистрация — через InGameUIAddWindow
// (колбэки вызываются каждый кадр в onEndScene), а не через
// InGameUIAdd (эти рисуются только внутри панели, когда она открыта —
// в 87.42/43 тост из-за этого никто ни разу не увидел).
static void RenderGateToast(bool /*getsInput*/)
{
    DWORD now = GetTickCount();
    if (!g_toastUntilMs || now >= g_toastUntilMs) return;
    float alpha = 1.0f;
    DWORD rem = g_toastUntilMs - now;
    if (rem < 500) alpha = rem / 500.0f;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.06f, 0.06f, 0.06f, 0.75f * alpha));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.62f, 0.26f, alpha));
    ImGui::SetNextWindowPos(ImVec2(24, ImGui::GetIO().DisplaySize.y - 96));
    ImGui::Begin("##difficultyGateToast", nullptr, ImVec2(0, 0), 1.0f,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                 ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize |
                 ImGuiWindowFlags_NoFocusOnAppearing |
                 ImGuiWindowFlags_NoBringToFrontOnFocus);
    // 87.6: текст в тоне игры, без технической кухни. Первая строка —
    // само предупреждение, вторая (тусклее) — что делать. Оба персонажа
    // говорят по-английски, и шрифт оверлея только ASCII.
    ImGui::Text("Too dangerous to use items mid-battle!");
    ImGui::TextColored(ImVec4(0.72f, 0.60f, 0.44f, alpha),
                       "Leave combat to use consumables.");
    ImGui::End();
    ImGui::PopStyleColor(2);
}

// Что за предмет в a1. Возвращает категорию ([запись+0x38] & 0x1F) или
// -1, если прочитать не удалось (тогда гейт не срабатывает — фейл-опен).
// Заодно отдаёт ID предмета для журнала.
static int GateItemCategory(DWORD a1, DWORD* itemIdOut)
{
    if (itemIdOut) *itemIdOut = 0;
    if (!a1 || !Runtime::Mem::LooksHeap((uintptr_t)a1)) return -1;

    if (itemIdOut)
    {
        DWORD slot = 0;
        if (DiffRead((uintptr_t)a1 + 0x10, &slot, 4))
            *itemIdOut = slot & 0xFFFF;          // ID в младших 16 битах
    }

    DWORD rec = 0;
    if (!DiffRead((uintptr_t)a1 + 4, &rec, 4)) return -1;
    if (!rec || !Runtime::Mem::LooksHeap((uintptr_t)rec)) return -1;
    DWORD catField = 0;
    if (!DiffRead((uintptr_t)rec + 0x38, &catField, 4)) return -1;
    return (int)(catField & 0x1F);
}

// Должен ли гейт заблокировать ЭТО применение. Фейл-опен на каждом шаге.
static bool GateShouldBlock(DWORD a0, DWORD a1, DWORD* itemIdOut)
{
    if (g_gateMode != 1) return false;
    if (!Runtime::Mem::InWorld()) return false;      // титульник/загрузка
    if (!IsInCombat()) return false;                 // вне боя — всё можно

    // Только Возникший. Тело резолвится платформой динамически (адреса
    // тел живут до перезагрузки мира). Не отрезолвилось — не мешаем.
    uintptr_t arisen = 0;
    if (!Runtime::Aggro::ResolveMemberBody(Runtime::Aggro::MEMBER_ARISEN,
                                           &arisen) || !arisen)
        return false;
    if ((uintptr_t)a0 != arisen) return false;       // пешка/цель-пешка

    // Категория 2 = расходник (подтверждено полем на трёх предметах).
    // Фонарь/инструменты — другая категория, их гейт не трогает.
    return GateItemCategory(a1, itemIdOut) == 2;
}

void __cdecl UseProbeLog(DWORD thisPtr, DWORD retAddr, DWORD a0, DWORD a1)
{
    LONG seq = InterlockedIncrement(&g_probeCalls);

    bool known = false;
    for (int i = 0; i < g_probeUniqueCount; i++)
        if (g_probeUnique[i] == retAddr) { known = true; break; }
    if (!known && g_probeUniqueCount < 64)
        g_probeUnique[g_probeUniqueCount++] = retAddr;

    if (seq == kProbeCap + 1)
    {
        logFile << "Difficulty: use-probe cap reached (" << kProbeCap
                << "), further calls only counted" << std::endl;
        return;
    }
    if (seq > kProbeCap) return;
    char line[200];
    sprintf_s(line, "Difficulty: use-call seq=%d ret=0x%08X this=0x%08X"
                    " a0=0x%08X a1=0x%08X",
              (int)seq, retAddr, thisPtr, a0, a1);
    logFile << line << std::endl;

    if (seq <= kDumpCap) ProbeItemFields(seq, a1);
}

LPVOID g_useTrampoline = nullptr;

// 87.41. Разбор краша 87.4: краш случился ВНЕ боя, то есть на пути
// ПРОПУСКА, и в логе не было ни одной строки «use-call». Виноват новый
// детур: в 87.4 он звал оригинал через C-обёртку и сам делал `ret 8` —
// навязывал конвенцию очистки стека, ничем не подтверждённую (в 87.02–
// 87.38 путь прохода был прозрачным `jmp` в трамплин, и это работало
// восемь сборок). Теперь:
//  - путь ПРОХОДА возвращён к полевому варианту: восстановление
//    регистров и прозрачный `jmp` в трамплин;
//  - путь БЛОКА в этой сборке — РАЗВЕДКА: решение «заблокировал бы»
//    пишется в журнал, а вызов всё равно пропускается в игру. Реальный
//    блок включим, когда подтвердим конвенцию возврата;
//  - конвенция возврата читается из кода игры: у четырёх известных
//    точек вызова воронки смотрим первые байты ПОСЛЕ адреса возврата.
//    `83 C4 xx` (add esp, xx) означает, что стек чистит ВЫЗЫВАЮЩИЙ.
//    Если такой инструкции нет, чистит вызываемый. СКОЛЬКО чистит
//    вызываемый — отдельный вопрос: 87.42 показал, что не 8, а 0x18
//    (шесть стековых аргументов) — см. комментарий у g_blockRetN.
//    Байты читаются через фундамент (RegionOk+Rd) и печатаются в лог.
// СКОЛЬКО байт чистит блок-путь. Доказано в 87.43 (см. комментарии к
// детуру): воронка берёт ШЕСТЬ стековых аргументов и эпилог у неё
// `ret 0x18` (24 байта), а не два аргумента, как мы думали в 87.41/42.
// Двойное подтверждение: (1) донор HardPlus хукал ровно этот путь и его
// ветка блока делает `ret 0x18` (disasm _hook_use, метка `blocked`);
// (2) криминалистика краша 87.42 — наш `ret 8` недочистил 16 байт,
// эпилог хоткей-функции прочитал мусор и улетел исполнять кучу.
DWORD g_blockRetN = 24;  // 0x18: шесть стековых аргументов воронки

static void DetectFunnelCleanup()
{
    if (g_build != BUILD_GOG)
    {
        logFile << "Difficulty: gate cleanup probe skipped (Steam call sites"
                   " unknown; GOG only for now)" << std::endl;
        return;
    }
    // Известные адреса возврата в воронку (разведка 87.02): меню, хоткей,
    // два пешечных пути. Хватит и одного, но печатаем все — для сверки.
    const DWORD sites[4] = { 0x0066B3E3, 0x00407E3C, 0x005497AA, 0x00ADD987 };
    int callerCleanupVotes = 0;
    for (int i = 0; i < 4; i++)
    {
        BYTE b[6] = { 0, 0, 0, 0, 0, 0 };
        char line[200];
        if (!DiffRead((uintptr_t)sites[i], b, 6))
        {
            sprintf_s(line, "Difficulty: gate cleanup probe @0x%08X unreadable",
                      (unsigned)sites[i]);
            logFile << line << std::endl;
            continue;
        }
        // 83 C4 xx = add esp, xx — вызывающий сам чистит стек.
        bool callerCleanup = (b[0] == 0x83 && b[1] == 0xC4);
        if (callerCleanup) callerCleanupVotes++;
        sprintf_s(line, "Difficulty: gate cleanup probe @0x%08X bytes"
                        " %02X %02X %02X %02X %02X %02X -> %s",
                  (unsigned)sites[i], b[0], b[1], b[2], b[3], b[4], b[5],
                  callerCleanup ? "caller-cleanup" : "callee-cleanup");
        logFile << line << std::endl;
    }
    if (callerCleanupVotes > 0)
    {
        // Никто из четырёх точек не чистит стек сам (иначе бы краш
        // 87.42 выглядел иначе) — если голоса всё же появились, это
        // сенсация, и её надо видеть в логу.
        logFile << "Difficulty: gate cleanup probe [WARN] caller-cleanup"
                   " votes > 0 — contradicts ret 0x18 model, inspect!"
                << std::endl;
    }
    g_blockRetN = 24;   // 0x18 — см. комментарий у объявления

    // Подтверждение из самой воронки: ищем в её теле `C2 18 00`
    // (ret 0x18). Это улика для лога, решение уже принято по донору и
    // по криминалистике 87.42. Сканируем первые 0x1000 байт; если
    // эпилог глубже — так и скажем, на решение это не влияет.
    static BYTE body[0x1000];
    const DWORD funnelSteam = 0x00B77130, funnelGog = 0x00B747F0;
    BYTE* funnel = Va(funnelSteam, funnelGog);
    if (DiffRead((uintptr_t)funnel, body, sizeof body))
    {
        int match18 = -1, hits = 0;
        char cands[220];
        cands[0] = 0;
        for (size_t off = 0; off + 2 < sizeof body; off++)
        {
            if (body[off] != 0xC2 || body[off + 2] != 0x00) continue;
            if (body[off + 1] == 0x18 && match18 < 0) match18 = (int)off;
            if (hits < 6)
            {
                char one[32];
                sprintf_s(one, " +0x%03X:C2%02X00",
                          (unsigned)off, body[off + 1]);
                strcat_s(cands, one);
            }
            hits++;
        }
        char line[320];
        if (match18 >= 0)
            sprintf_s(line, "Difficulty: gate epilogue scan: ret 0x18"
                            " found at funnel+0x%03X -> corroborates"
                            " block N=0x18 (all C2 candidates:%s)",
                      match18, cands);
        else
            sprintf_s(line, "Difficulty: gate epilogue scan: no ret 0x18"
                            " in first 0x1000 bytes (epilogue deeper?)"
                            " block N=0x18 stands per donor+forensics"
                            " (C2 candidates:%s)", cands);
        logFile << line << std::endl;
    }
    else
    {
        logFile << "Difficulty: gate epilogue scan: funnel body unreadable"
                   " (block N=0x18 stands per donor+forensics)" << std::endl;
    }
    logFile << "Difficulty: gate block-path will use ret 0x18 (24 bytes ="
               " 6 stack args; proven by donor _hook_use + 87.42 forensics)"
            << std::endl;
}

// Единая точка решения: журнал -> гейт. Возврат: 0 = пропустить вызов в
// оригинал (детур прозрачно уходит в трамплин, как в 87.02–87.38),
// 1 = блок: оригинал не вызывается, предмет не тратится.
// Конвенция возврата при блоке (87.43): воронка — функция с ШЕСТЬЮ
// стековыми аргументами, эпилог `ret 0x18`. Доказательства: донор
// HardPlus хукал этот же путь, и его ветка блока делает ровно
// `ret 0x18`; краш 87.42 (наш `ret 8` недочистил 16 байт) это
// подтвердил криминалистически. См. детур ниже.
DWORD __cdecl UseGate(DWORD thisPtr, DWORD retAddr, DWORD a0, DWORD a1)
{
    UseProbeLog(thisPtr, retAddr, a0, a1);

    DWORD itemId = 0;
    if (GateShouldBlock(a0, a1, &itemId))
    {
        LONG n = InterlockedIncrement(&g_gateBlocks);
        g_toastUntilMs = GetTickCount() + 1800;
        if (n <= kGateLogCap)
        {
            char line[200];
            sprintf_s(line, "Difficulty: gate BLOCKED use #%d"
                            " item=0x%04X (Arisen, in combat)",
                      (int)n, (unsigned)itemId);
            logFile << line << std::endl;
        }
        return 1;
    }
    return 0;
}

// thiscall-детур (87.43): журнал и решение гейта идут через UseGate.
// Путь прохода — прозрачный `jmp` в трамплин, байт-в-байт как в полевых
// сборках 87.02–87.38: оригинал сам делает свой `ret`, никаких догадок.
// Путь блока: воронка — функция с ШЕСТЬЮ стековыми аргументами, её
// эпилог — `ret 0x18`. Доказано в двух независимых источниках:
//   (1) донор HardPlus хукал этот же путь (патч в точке вызова меню,
//       адрес возврата 0x66B3E3) — его ветка блока `blocked:` делает
//       ровно `ret 0x18`, и мод годами работал с этим у людей;
//   (2) криминалистика краша 87.42: наш `ret 8` недочистил 16 байт,
//       эпилог хоткей-функции прочитал мусор ([ebp]=0, edi/esi/ebx=1)
//       и её финальный `ret` улетел исполнять адрес из кучи
//       (0x06AD7290, DEP-фолт, «не принадлежит ни одному модулю»).
// Оригинальная функция при блоке не вызывается вообще — предмет
// остаётся в сумке. Возвращаем eax=1 — как донор в своей ветке блока;
// оба живых адресата блока (меню и хоткей) значение затирают сразу.
__declspec(naked) void UseProbeDetour()
{
    __asm
    {
        push eax
        push ecx                 // сохранить this
        push edx
        // стек: [edx][ecx][eax][ret][a0..a5] (воронка берёт 6 аргументов)
        mov  eax, [esp + 16]     // a0
        mov  edx, [esp + 20]     // a1
        push edx                 // аргумент 4: a1
        push eax                 // аргумент 3: a0
        mov  eax, [esp + 20]     // ret (после двух пушей)
        push eax                 // аргумент 2: ret
        mov  eax, ecx
        push eax                 // аргумент 1: this
        call UseGate
        add  esp, 16
        test eax, eax
        jnz  GateBlocked
        // Проход: ровно как в 87.02–87.38.
        pop  edx
        pop  ecx
        pop  eax
        jmp  [g_useTrampoline]
GateBlocked:
        // Блок: оригинал не вызывается. Стек сейчас
        // [edx][ecx][eax_saved][ret][a0..a5] — восстанавливаем edx/ecx,
        // слот сохранённого eax выбрасываем, вызывающему возвращаем 1
        // (паритет с донором) и чистим 24 байта = шесть аргументов.
        pop  edx
        pop  ecx
        add  esp, 4
        mov  eax, 1
        ret  24                  // 0x18: см. комментарий над детуром
    }
}

void InstallUseProbe()
{
    // Гейт (87.4): политика из ini. "block" — жёсткий блок расходников
    // в бою (по умолчанию), "off" — воронка только логирует, как раньше.
    string policy = config.getStr("difficulty", "combatUseGate", "block");
    g_gateMode = (policy == "off") ? 0 : 1;

    if (!config.getBool("difficulty", "useProbe", true))
    {
        logFile << "Difficulty: [SKIP]  use-probe/gate: disabled in ini" << std::endl;
        return;
    }
    const DWORD useOrigSteam = 0x00B77130, useOrigGog = 0x00B747F0;
    BYTE* target = Va(useOrigSteam, useOrigGog);
    Hooks::CreateHook("Difficulty use-probe", target, UseProbeDetour,
                      &g_useTrampoline, true);
    // AddWindow, а не Add: тост должен рисоваться каждый кадр сам по
    // себе, а варианты Add живут только внутри открытой F12-панели.
    Hooks::InGameUIAddWindow(RenderGateToast);
    DetectFunnelCleanup();
    logFile << "Difficulty: use-probe rev4 + use-gate rev4 (LIVE;"
            << " policy=" << (g_gateMode == 1 ? "block" : "off")
            << "; block ret 0x18 proven"
            << "; probe cap=" << kProbeCap
            << ", item dump first " << kDumpCap << ")" << std::endl;
}

// ---------------------------------------------------------------------------
// 5. Фикс таблицы опыта для уровней 51–100 (87.3)
// ---------------------------------------------------------------------------
// Ванильная таблица опыта проваливается на уровнях 51–100: множитель
// падает до x4 (при x11 у предыдущего диапазона и x15 у следующего) —
// прокачка на этом отрезке летит слишком быстро. Донор правит таблицу
// так: подменяет УКАЗАТЕЛЬ загрузчика таблицы (ячейка в .data игры,
// которую разыменовывает движок) своей обёрткой; обёртка вызывает
// оригинальный загрузчик, а после возвращения правит в уже загруженной
// таблице запись уровня 51: множитель 4.0 заменяется на 13.0.
// Воспроизводим поведение один-в-один, своими руками:
//   - установку делаем в Init (у нас он, как и у донора, исполняется в
//     DllMain PROCESS_ATTACH — до того, как игра загрузит таблицу);
//   - перед подменой сверяем текущее значение ячейки с известным
//     оригиналом — малейшее несовпадение означает [FAIL] без записи;
//   - обёртка повторяет донорский детур поинструкционно (порядок
//     push/pop проверен на его работающем бинарнике);
//   - правка записи идёт только через проверенные чтения (падение на
//     битой структуре невозможно), и только если множитель в точности
//     равен ванильным 4.0; если уже 13.0 — игра с пропатченным
//     game_main.arc, просто отмечаем это в логе.

void* g_plexpOriginal = nullptr;     // оригинальный загрузчик таблицы
bool  g_fixLevel51    = false;
bool  g_plexpNoted    = false;       // одна итоговая строка на сессию
volatile LONG g_plexpCalls = 0;      // сколько раз загрузчик вызван
const  LONG kPlexpDumpCap = 8;       // полные дампы — только первых 8 раз

// (чтение через фундамент — DiffRead — объявлено в разделе зонда выше)

// Постобработка загруженной таблицы. Первый аргумент — контекст таблицы
// (позиция ecx при вызове — раскладка повторена с работающего донора),
// второй — возвращаемое значение оригинального загрузчика (не нужен).
//
// Все чтения — через DiffRead (RegionOk + Rd): таблица приходит из-под
// загрузчика, и пока её раскладка не подтверждена полем, мы не имеем
// права ходить по её указателям голым чтением (см. комментарий про
// страницы-сторожа выше). Билд 87.35 крашнулся именно на этом.
void __cdecl PlexpLoaded(DWORD ctx, DWORD /*loaderResult*/)
{
    if (!g_fixLevel51 || !ctx) return;
    if (!Runtime::Mem::LooksHeap((uintptr_t)ctx))
    {
        if (!g_plexpNoted)
        {
            g_plexpNoted = true;
            char line[160];
            sprintf_s(line, "Difficulty: [WARN]  experience table: context"
                            " 0x%08X failed heap-look, skipped", (unsigned)ctx);
            logFile << line << std::endl;
        }
        return;
    }
    uintptr_t b = (uintptr_t)ctx;

    // Заголовок таблицы: 0x14 байт с +0x60; счётчик записей при +0x64,
    // указатель на массив записей при +0x70.
    DWORD header[5];
    if (!DiffRead(b + 0x60, header, 0x14)) return;
    DWORD count = header[1];
    DWORD array = header[4];
    if (!array || count < 2 || count > 64) return;
    if (!Runtime::Mem::LooksHeap((uintptr_t)array)) return;

    DWORD entries[64];
    if (!DiffRead((uintptr_t)array, entries, count * 4)) return;

    // Ревизия 5 (87.38): полевые дампы 87.37 показали, что на GOG-сборке
    // запись таблицы опыта лежит ЗЕРКАЛЬНО относительно донорского
    // обхода: у донора [+4]=уровень (целый), [+8]=множитель (float), а
    // здесь [+4]=множитель (float), [+8]=уровень (целый). Таблица та же
    // самая: 13 записей, границы уровней 200/220/2/6/14/18/25/36/42/51/
    // 101/151/200, множители 1/1/1.3/3/5/8/9/10/11/4/15/20/0 — и у
    // уровня 51 ванильный провал до x4. Поэтому фикс ищет запись в двух
    // раскладках: сначала донорской (для Steam), потом зеркальной
    // (для GOG). Правка всегда идёт в подтверждённое поле-множитель.
    struct Patch { DWORD entry; int mulOff; };
    Patch found = { 0, 0 };
    bool foundAlready = false;

    for (DWORD i = 0; i < count; ++i)
    {
        uintptr_t e = (uintptr_t)entries[i];
        if (!e || !Runtime::Mem::LooksHeap(e)) continue;
        DWORD head[3];
        if (!DiffRead(e, head, 0x0C)) continue;

        // Донорская раскладка: [+4]=уровень, [+8]=множитель.
        if (head[1] == 0x33)
        {
            float mul;
            memcpy(&mul, &head[2], 4);
            if (mul == 4.0f)  { found.entry = (DWORD)e; found.mulOff = 8; break; }
            if (mul == 13.0f) { foundAlready = true; break; }
        }
        // Зеркальная раскладка (найдена в поле на GOG): [+4]=множитель,
        // [+8]=уровень.
        if (head[2] == 0x33)
        {
            float mul;
            memcpy(&mul, &head[1], 4);
            if (mul == 4.0f)  { found.entry = (DWORD)e; found.mulOff = 4; break; }
            if (mul == 13.0f) { foundAlready = true; break; }
        }
    }

    if (found.entry)
    {
        float fixed = 13.0f;
        // Диапазон записи уже проверен (DiffRead выше покрыл 0x0C),
        // куча — страница пишется без снятия защиты, как у донора.
        memcpy((void*)((uintptr_t)found.entry + found.mulOff), &fixed, 4);
        if (!g_plexpNoted)
        {
            g_plexpNoted = true;
            char line[200];
            sprintf_s(line, "Difficulty: [OK]    experience table: levels 51+"
                            " fixed (x4 -> x13, layout=%s)",
                      found.mulOff == 8 ? "donor" : "mirrored-GOG");
            logFile << line << std::endl;
        }
        return;
    }
    if (foundAlready)
    {
        if (!g_plexpNoted)
        {
            g_plexpNoted = true;
            logFile << "Difficulty: [OK]    experience table: already"
                    << " fixed (modified game_main.arc)" << std::endl;
        }
        return;
    }

    // Ревизия 4 (87.37). В 87.35/87.36 запись уровня 51 не нашлась, и
    // распечатка первых четырёх записей показала раскладку, не похожую на
    // донорскую (у записи +4 выглядит как float, а +8 как целый). Чтобы
    // понять, что это за таблица, печатаем ВСЕ записи (до 16, по 8
    // двойных слов каждая), плюс пробуем прочитать первые два указателя
    // как строки-имена: загрузчик таблиц в движке обычно держит имя
    // таблицы, и оно сразу скажет, та ли это таблица вообще.
    //
    // Важно: строка пишется на КАЖДЫЙ вызов загрузчика (не только на
    // первый) — таблица может загружаться несколько раз, и та, что с
    // уровнями, может прийти не первой. Чтобы не раздувать журнал, полные
    // дампы идут только для первых нескольких вызовов.
    LONG callSeq = InterlockedIncrement(&g_plexpCalls);
    if (callSeq > kPlexpDumpCap) return;
    {
        char line[512];
        int off = sprintf_s(line, "Difficulty: exp-table call ctx=0x%08X"
                            " count=%u hdr0=0x%08X array=0x%08X",
                            (unsigned)ctx, (unsigned)count,
                            (unsigned)header[0], (unsigned)array);
        char name[48];
        if (header[0] && Runtime::Mem::ReadCStr((uintptr_t)header[0],
                                                name, sizeof(name))
            && name[0] >= 0x20)
        {
            int w = sprintf_s(line + off, sizeof(line) - off, " hdr0name=%s",
                              name);
            if (w > 0) off += w;
        }
        logFile << line << std::endl;

        DWORD n = (count > 16) ? 16 : count;
        for (DWORD i = 0; i < n; i++)
        {
            DWORD body[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
            uintptr_t e = (uintptr_t)entries[i];
            if (e && Runtime::Mem::LooksHeap(e))
                DiffRead(e, body, 0x20);
            off = sprintf_s(line, "Difficulty: exp-entry %u=0x%08X:",
                            (unsigned)i, (unsigned)e);
            for (int k = 0; k < 8 && off > 0; k++)
            {
                int w = sprintf_s(line + off, sizeof(line) - off, " %08X",
                                  body[k]);
                if (w < 0) break;
                off += w;
            }
            if (e && Runtime::Mem::LooksHeap(e) && body[0] &&
                Runtime::Mem::ReadCStr((uintptr_t)body[0], name,
                                       sizeof(name)) && name[0] >= 0x20)
            {
                int w = sprintf_s(line + off, sizeof(line) - off, "  (%s)",
                                  name);
                if (w > 0) off += w;
            }
            logFile << line << std::endl;
        }
    }
}

// Обёртка загрузчика: повторяет донорский детур поинструкционно.
// Прозрачна для игры: аргумент передаётся оригиналу, возврат оригинала
// возвращается вызывающему, аргумент очищается (ret 4).
__declspec(naked) void PlexpLoadDetour()
{
    __asm
    {
        push ecx
        push [esp + 8]           // аргумент игры — оригинальному загрузчику
        call [g_plexpOriginal]
        pop  ecx
        push eax                 // возврат загрузчика
        push ecx                 // контекст таблицы
        call PlexpLoaded
        add  esp, 4
        pop  eax
        ret  4
    }
}

void InstallPlexpHook()
{
    g_fixLevel51 = config.getBool("difficulty", "fixLevel51Curve", true);
    if (!g_fixLevel51)
    {
        logFile << "Difficulty: [SKIP]  exp curve fix: disabled in ini"
                << std::endl;
        return;
    }
    BYTE** slot = (BYTE**)Va(0x01619750, 0x015FA258);   // PLEXP_SLOT
    BYTE*  orig = Va(0x00CE8C50, 0x00CE5550);           // PLEXP_ORIG

    BYTE* cur = nullptr;
    if (!DiffRead((uintptr_t)slot, &cur, 4))
    {
        logFile << "Difficulty: [FAIL]  exp table hook: slot unreadable"
                << std::endl;
        return;
    }
    if (cur != orig)
    {
        char line[160];
        sprintf_s(line, "Difficulty: [FAIL]  exp table hook: unexpected"
                        " loader pointer %p (expected %p) - untouched",
                  cur, orig);
        logFile << line << std::endl;
        return;
    }

    g_plexpOriginal = cur;
    DWORD oldProt = 0;
    if (!VirtualProtect(slot, 4, PAGE_READWRITE, &oldProt))
    {
        logFile << "Difficulty: [FAIL]  exp table hook: VirtualProtect denied"
                << std::endl;
        return;
    }
    *slot = (BYTE*)PlexpLoadDetour;
    VirtualProtect(slot, 4, oldProt, &oldProt);
    logFile << "Difficulty: [OK]    exp table loader hook installed"
            << " (levels 51+ x4 -> x13)" << std::endl;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Публичный интерфейс слоя
// ---------------------------------------------------------------------------

void Difficulty::Init()
{
    g_exeBase = (BYTE*)GetModuleHandle(nullptr);
    DWORD stamp = 0;
    g_build = DetectBuild(stamp);
    if (g_build == BUILD_UNKNOWN)
    {
        char line[120];
        sprintf_s(line, "Difficulty: [FAIL] unknown DDDA.exe build"
                        " (TimeDateStamp=0x%08X) - layer disabled", stamp);
        logFile << line << std::endl;
        return;
    }
    logFile << "Difficulty: game build = "
            << (g_build == BUILD_STEAM ? "Steam" : "GOG")
            << " (TimeDateStamp=0x" << std::hex << stamp << std::dec << ")"
            << std::endl;

    ApplyHumanHpCap();
    ApplyPouchPatches();
    ApplyNoDoubleExpPatch();
    InstallPlexpHook();
    InstallUseProbe();
}

void Difficulty::Shutdown()
{
    if (g_probeCalls > 0)
    {
        char line[160];
        sprintf_s(line, "Difficulty: use-probe summary calls=%d unique=%d"
                        " (every call listed in the main log as use-call seq=N)",
                  (int)g_probeCalls, g_probeUniqueCount);
        LogMem::SessionNote(line);
    }
    if (g_plexpCalls > 0)
    {
        char line[160];
        sprintf_s(line, "Difficulty: exp-table summary loaderCalls=%d"
                        " fixApplied=%s",
                  (int)g_plexpCalls, g_plexpNoted ? "yes" : "no");
        LogMem::SessionNote(line);
    }
    if (g_gateMode == 1)
    {
        char line[160];
        sprintf_s(line, "Difficulty: use-gate summary policy=block"
                        " blocked=%d (consumable uses denied in combat)",
                  (int)g_gateBlocks);
        LogMem::SessionNote(line);
    }
}
