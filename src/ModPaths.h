#pragma once
/**
 * ModPaths — все файлы мода в своей папке, а не в корне игры.
 *
 * ПОЧЕМУ ЭТО НЕ ПРОСТО "ДОБАВИТЬ ПРЕФИКС":
 *
 * Глобальные объекты (logFile, config в dinput8.cpp) конструируются ДО
 * DllMain, на этапе статической инициализации CRT. В этот момент:
 *   - нельзя надёжно вызывать WinAPI сверх минимума;
 *   - каталога ещё нет, его никто не создал.
 * Поэтому путь считается лениво, при первом обращении (Meyers singleton),
 * а каталог создаётся тогда же.
 *
 * ВТОРАЯ ЛОВУШКА: iniConfig хранит LPCSTR — сырой указатель, он НЕ копирует
 * строку. Если передать c_str() от временного std::string, указатель
 * повиснет. Поэтому строки здесь статические и живут до конца процесса.
 *
 * ТРЕТЬЯ: путь берётся от папки EXE (GetModuleFileName(nullptr)), а не от
 * текущего каталога. CWD игра может сменить, и файлы разъедутся.
 */

#include <windows.h>

namespace ModPaths {

// Папка мода: <папка игры>\DDDA_AI_Overhaul
// Создаётся при первом обращении. Возвращает путь БЕЗ слэша на конце.
//
// (Здесь был обратный слэш в конце строки комментария. В C++ это склейка
//  строк: следующая строка молча становилась частью комментария. Обошлось
//  тем, что там тоже был комментарий — но окажись там код, он бы исчез.)
inline const char* Dir()
{
    static char s_dir[MAX_PATH] = { 0 };
    static bool  s_init = false;
    if (!s_init) {
        s_init = true;
        char exe[MAX_PATH] = { 0 };
        DWORD n = GetModuleFileNameA(nullptr, exe, MAX_PATH);
        if (n == 0 || n >= MAX_PATH) { lstrcpynA(s_dir, ".", MAX_PATH); return s_dir; }
        // отрезаем имя exe
        for (int i = (int)n - 1; i >= 0; --i) {
            if (exe[i] == '\\' || exe[i] == '/') { exe[i] = 0; break; }
        }
        wsprintfA(s_dir, "%s\\DDDA_AI_Overhaul", exe);
        // CreateDirectory безопасна, если каталог уже есть
        CreateDirectoryA(s_dir, nullptr);
    }
    return s_dir;
}

// Полный путь к файлу внутри папки мода.
// ВНИМАНИЕ: возвращает указатель на статический буфер, привязанный к slot.
// slot 0..7 — чтобы два разных пути не затирали друг друга.
inline const char* File(const char* name, int slot = 0)
{
    static char s_buf[8][MAX_PATH];
    if (slot < 0 || slot > 7) slot = 0;
    wsprintfA(s_buf[slot], "%s\\%s", Dir(), name);
    return s_buf[slot];
}

} // namespace ModPaths
