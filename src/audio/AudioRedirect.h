#pragma once
/**
 * AudioRedirect — музыкальный слой 0 (docs/AUDIO_MUSIC_RECON.md).
 *
 * Редирект BGM на уровне CreateFile + транзакционный патч STRQ-таблицы
 * в памяти (размер/сэмплы/лупы — см. docs/generated/STQ_FORMAT.md).
 * Слой не трогает AI-слои; состояние — только в нашей папке и ini.
 *
 * Режимы (ddda_music_map.ini, секция [music]):
 *   enabled = 0      ванилa: подмена и патч НЕ выполняются (по умолчанию)
 *   log_requests = 1 R1-прибор: лог всех обращений к *.sngw
 *   probe_stq = 1    R1-пробник: разово ищет STRQ-образы в памяти
 *                    (сработает один раз за запуск на первом bgm-файле)
 */
namespace Audio
{
    // Поднимается из InitHooks() до UI. Хук ставится всегда (дешёвый
    // фильтр по ".sngw"), поведение на запрос — по флагам карты.
    void Init();

    // Bounded-rollback: только несколько guarded 4-байтных записей,
    // без ожиданий/потоков/аллокаций (прецедент DevTools::Shutdown).
    // Вызывается из Unitialize() до MH_Uninitialize.
    void Shutdown();
}
