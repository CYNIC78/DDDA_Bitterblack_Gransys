#include "stdafx.h"
#include "EntityConfig.h"
#include "ModPaths.h"
#include "DefaultEntitiesIni.h"

/**
 * Реализация трёхуровневого конфига с hot-reload.
 *
 * Порядок разрешения значения для вида:
 *      [default] -> [class.<группа>] -> [em<id>]
 * Каждый следующий уровень переопределяет предыдущий, но ТОЛЬКО те ключи,
 * которые в нём реально присутствуют. Отсюда и приём: читаем ключ, передавая
 * дефолтом уже накопленное значение. Нет ключа -> остаётся унаследованное.
 */

namespace EntityCfg {

// ---------------------------------------------------------------- клампы ----
float ClampSpeed(float v) {
    if (!(v == v)) return 1.0f;
    if (v < 0.5f) return 0.5f;
    if (v > 1.5f) return 1.5f;   // выше — рассинхрон хитбокса, проверено
    return v;
}
float ClampAngle(float v) {
    if (!(v == v)) return 0.0f; // 0 = vanilla
    if (v < (float)Limits::kSightAngleMin) return (float)Limits::kSightAngleMin;
    if (v > (float)Limits::kSightAngleMax) return (float)Limits::kSightAngleMax;
    return v;
}
float ClampScale(float v) {
    if (!(v == v)) return 1.0f;
    // Ниже 0.7 модель проваливается в землю, выше 1.4 расходится с хитбоксом.
    // Границы консервативные: сначала убедимся, что вообще работает.
    if (v < 0.7f) return 0.7f;
    if (v > 1.4f) return 1.4f;
    return v;
}
float ClampRadius(float v, float maxv) {
    if (!(v == v)) return 0.0f; // 0 = vanilla
    if (v < 0.0f) return 0.0f;
    if (v > maxv) return maxv;
    return v;
}
float ClampCombat(float v) {
    // NaN protection: hot-reload may read half-written file -> stof throws -> defValue,
    // but also direct NaN from file or corrupted memory. NaN must not reach engine.
    if (!(v == v)) return 1.0f; // NaN -> vanilla
    if (v < 0.5f) return 0.5f;
    if (v > 3.0f) return 3.0f;
    return v;
}

// ------------------------------------------------------------- состояние ----
static const int kMaxEntries = 128;

struct Entry {
    uint16_t emId;
    Tuning   t;
};

static Tuning   s_default;
static Entry    s_entries[kMaxEntries];
static int      s_nEntries = 0;
static bool     s_enabled  = true;
static bool     s_allowWrites = false;
static int      s_reloads  = 0;
static FILETIME s_mtime    = { 0, 0 };
static DWORD    s_lastCheck = 0;

// Группы видов. Гоблины/волки — мелочь, огры/химеры — крупные, драконы — боссы.
// Список сознательно грубый: точность добирается секцией [em<id>].
static const char* ClassOf(uint16_t emId)
{
    switch (emId) {
        case 100: case 101: case 102: case 103:   // гоблины
        case 200: case 201: case 202: case 203: case 204: // волки/саблезубы
        case 900: case 901:                        // гарпии
            return "small";
        case 500: case 501: case 502: case 503: case 504: case 505:
        case 600: case 601: case 602: case 603: case 604: case 605:
            return "large";
        case 5500: case 5501: case 5300:
            return "boss";
        default:
            return "other";
    }
}

static void SetVanilla(Tuning& t)
{
    t.sightRadius = 0.0f;
    t.sightAngle  = 0.0f;
    t.hearRadius  = 0.0f;
    t.leashScale  = 1.0f;
    t.returnSpeed = 1.0f;
    t.returnArmor = true;
    t.returnArmorMult = 4.0f;
    t.scaleMin    = 1.0f;
    t.scaleMax    = 1.0f;
    t.scaleJitter = 0.0f;
    t.attackMult        = 1.0f;
    t.defenseMult       = 1.0f;
    t.magickAttackMult  = 1.0f;
    t.magickDefenseMult = 1.0f;
    t.adrenalineAtk     = 1.0f;
    t.adrenalineMagick  = 1.0f;
    t.enabled     = true;
}

// Прочитать секцию поверх уже накопленного base.
// Ключ отсутствует -> getFloat вернёт переданный дефолт -> значение наследуется.
static void ReadSection(iniConfig& cfg, const char* section, Tuning& t)
{
    t.sightRadius = ClampRadius(cfg.getFloat(section, "sightRadius", t.sightRadius),
                                (float)Limits::kSightRadiusMax);
    t.hearRadius  = ClampRadius(cfg.getFloat(section, "hearRadius",  t.hearRadius),
                                (float)Limits::kHearRadiusMax);

    float ang = cfg.getFloat(section, "sightAngle", t.sightAngle);
    t.sightAngle = (ang <= 0.0f) ? 0.0f : ClampAngle(ang);

    t.scaleMin = ClampScale(cfg.getFloat(section, "scaleMin", t.scaleMin));
    t.scaleMax = ClampScale(cfg.getFloat(section, "scaleMax", t.scaleMax));
    if (t.scaleMax < t.scaleMin) t.scaleMax = t.scaleMin;
    t.scaleJitter = ClampRadius(cfg.getFloat(section, "scaleJitter", t.scaleJitter), 0.35f);

    // Поводок: 0.5..3.0. Верхняя граница не 10 — при таком множителе враг
    // гонится через полкарты и ломает переходы между зонами.
    // Нижняя не 0 — иначе враг разворачивается мгновенно и агро не читается.
    {
        float ls = cfg.getFloat(section, "leashScale", t.leashScale);
        if (ls < 0.5f) ls = 0.5f;
        if (ls > 3.0f) ls = 3.0f;
        t.leashScale = ls;
    }
    t.returnSpeed = ClampSpeed(cfg.getFloat(section, "returnSpeed", t.returnSpeed));
    t.returnArmor = cfg.getBool(section, "returnArmor", t.returnArmor);
    {
        float ram = cfg.getFloat(section, "returnArmorMult", t.returnArmorMult);
        if (ram < 1.0f) ram = 1.0f;
        if (ram > 20.0f) ram = 20.0f;
        t.returnArmorMult = ram;
    }
    t.attackMult        = ClampCombat(cfg.getFloat(section, "attackMult",        t.attackMult));
    t.defenseMult       = ClampCombat(cfg.getFloat(section, "defenseMult",       t.defenseMult));
    t.magickAttackMult  = ClampCombat(cfg.getFloat(section, "magickAttackMult",  t.magickAttackMult));
    t.magickDefenseMult = ClampCombat(cfg.getFloat(section, "magickDefenseMult", t.magickDefenseMult));
    // Адреналин: пол жёстко 1.0 (ваниль — нижний порог), потолок 1.6. Значение
    // вне диапазона зажимается, а не отбрасывается: владелец должен увидеть, что
    // его число не принято целиком, а не гадать, почему всплеска нет.
    {
        float aa = cfg.getFloat(section, "adrenalineAtk", t.adrenalineAtk);
        if (!(aa == aa) || aa < 1.0f) aa = 1.0f;
        if (aa > kAdrenalineMax) aa = kAdrenalineMax;
        t.adrenalineAtk = aa;
        float am = cfg.getFloat(section, "adrenalineMagick", t.adrenalineMagick);
        if (!(am == am) || am < 1.0f) am = 1.0f;
        if (am > kAdrenalineMax) am = kAdrenalineMax;
        t.adrenalineMagick = am;
    }
    t.enabled     = cfg.getBool(section, "enabled",     t.enabled);
}

// ------------------------------------------------------------------ Load ----
// Создать файл с дефолтным содержимым, если его нет.
// Возвращает true, если файл был создан именно сейчас.
static bool EnsureFileExists(const char* path)
{
    DWORD attr = GetFileAttributesA(path);
    if (attr != INVALID_FILE_ATTRIBUTES) return false;   // уже есть — не трогаем

    // CREATE_NEW: если кто-то создал файл между проверкой и записью,
    // мы его не затрём.
    HANDLE h = CreateFileA(path, GENERIC_WRITE, 0, nullptr,
                           CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        logFile << "EntityCfg: cannot create " << path
                << " (err " << GetLastError() << ")" << std::endl;
        return false;
    }

    // UTF-8 BOM — чтобы Блокнот не испортил кириллицу в комментариях
    static const unsigned char kBom[3] = { 0xEF, 0xBB, 0xBF };
    DWORD wrote = 0;
    WriteFile(h, kBom, 3, &wrote, nullptr);

    for (int i = 0; kDefaultEntitiesIni[i] != nullptr; ++i) {
        const char* line = kDefaultEntitiesIni[i];
        WriteFile(h, line, (DWORD)strlen(line), &wrote, nullptr);
    }

    CloseHandle(h);
    logFile << "EntityCfg: created default " << path << std::endl;
    return true;
}

void Load()
{
    const char* path = ModPaths::File("ddda_entities.ini", 1);

    EnsureFileExists(path);

    iniConfig cfg(path);
    // Audit 2026-09-21: entities.ini использует трёхуровневое наследование
    // default -> class.* -> emXXXX. Авто-дописка недостающих ключей (backfill)
    // ломает наследование: при первом чтении [class.small] не имеет attackMult,
    // backfill пишет туда 1, и он переопределяет 2.0 из [default]. Поэтому
    // для этого файла backfill выключен — файл остаётся как создан из
    // DefaultEntitiesIni.h (только [default] с новыми ключами), а отсутствие
    // ключа = наследование, а не запись дефолта.
    cfg.autoBackfill = false;

    s_enabled = cfg.getBool("global", "enabled", true);
    // Запись выключена по умолчанию: читать и смотреть безопасно всегда.
    // 86.03, ПРОВЕРЕНО: поставляемый ddda_entities.ini и вшитый
    // DefaultEntitiesIni.h уже несут allowWrites = on, то есть на чистой
    // установке запись включена и менять дефолт кода не нужно — он срабатывает
    // только если ключ из файла удалён, а там «только смотрю» уместнее.
    s_allowWrites = cfg.getBool("global", "allowWrites", false);

    int schema = cfg.getInt("global", "schema", 1);
    if (schema != 1) {
        logFile << "EntityCfg: unknown schema=" << schema
                << " (expected 1). Reading anyway." << std::endl;
    }

    SetVanilla(s_default);
    ReadSection(cfg, "default", s_default);

    s_nEntries = 0;

    // Секции [em<id>] перебором по известным видам. Их 96, проверка дешёвая
    // и делается только при перезагрузке файла, не в тике.
    static const uint16_t kKnownEm[] = {
        100, 101, 102, 103, 200, 201, 202, 203, 204,
        400, 401, 402, 403, 404, 405, 406, 407, 408,
        500, 501, 502, 503, 504, 505, 506, 507,
        600, 601, 602, 603, 604, 605,
        700, 701, 702, 703, 900, 901,
        1200, 1201, 5000, 5300, 5400, 5500, 5501, 8000, 8600,
        9999   // uHumanEnemy: бандиты, солдаты. Секция [human].
    };
    const int nKnown = sizeof(kKnownEm) / sizeof(kKnownEm[0]);

    for (int i = 0; i < nKnown && s_nEntries < kMaxEntries; ++i) {
        uint16_t id = kKnownEm[i];

        Tuning t = s_default;

        char classSec[32];
        wsprintfA(classSec, "class.%s", ClassOf(id));
        ReadSection(cfg, classSec, t);

        char emSec[16];
        // 9999 = люди-враги (uHumanEnemy): секция [human], а не [em9999].
        if (id == 9999) lstrcpynA(emSec, "human", sizeof(emSec));
        else            wsprintfA(emSec, "em%04u", (unsigned)id);
        ReadSection(cfg, emSec, t);

        s_entries[s_nEntries].emId = id;
        s_entries[s_nEntries].t    = t;
        ++s_nEntries;
    }

    ++s_reloads;
    logFile << "EntityCfg: loaded " << path
            << "  global=" << (s_enabled ? "on" : "off")
            << "  writes=" << (s_allowWrites ? "ON" : "off")
            << "  species=" << s_nEntries
            << "  reload#" << s_reloads << std::endl;
}

// ------------------------------------------------------------------- For ----
const Tuning& For(uint16_t emId)
{
    for (int i = 0; i < s_nEntries; ++i)
        if (s_entries[i].emId == emId) return s_entries[i].t;
    return s_default;
}

bool Enabled()      { return s_enabled; }
bool AllowWrites()  { return s_allowWrites; }
int  ReloadCount(){ return s_reloads; }

void ForceReload(){ Load(); }

// ------------------------------------------------------------------ Tick ----
// 85.34: см. комментарий в заголовке.
bool AnyAdrenalineConfigured()
{
    if (s_default.adrenalineAtk > 1.0f || s_default.adrenalineMagick > 1.0f)
        return true;
    for (int i = 0; i < s_nEntries; ++i) {
        if (s_entries[i].t.adrenalineAtk > 1.0f
            || s_entries[i].t.adrenalineMagick > 1.0f)
            return true;
    }
    return false;
}

void Tick()
{
    DWORD now = MsNow();
    if (s_lastCheck && now - s_lastCheck < 500) return;   // не чаще 2 раз в секунду
    s_lastCheck = now;

    WIN32_FILE_ATTRIBUTE_DATA fad;
    const char* path = ModPaths::File("ddda_entities.ini", 1);
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &fad)) return;

    if (fad.ftLastWriteTime.dwLowDateTime  == s_mtime.dwLowDateTime &&
        fad.ftLastWriteTime.dwHighDateTime == s_mtime.dwHighDateTime)
        return;   // не менялся

    // Первый вызов: просто запоминаем время, файл уже прочитан в Load().
    bool first = (s_mtime.dwLowDateTime == 0 && s_mtime.dwHighDateTime == 0);
    s_mtime = fad.ftLastWriteTime;
    if (first) return;

    // Редакторы сохраняют неатомарно: можно поймать полузаписанный файл.
    // Ждём следующий тик — mtime уже запомнен, но перечитаем через 500 мс,
    // когда запись точно завершится.
    static FILETIME s_pending = { 0, 0 };
    if (s_pending.dwLowDateTime != s_mtime.dwLowDateTime ||
        s_pending.dwHighDateTime != s_mtime.dwHighDateTime) {
        s_pending = s_mtime;
        return;               // пропускаем один цикл — даём файлу дописаться
    }

    Load();
}

} // namespace EntityCfg
