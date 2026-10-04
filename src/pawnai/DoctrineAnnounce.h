#pragma once
/**
 * 85.63: НЕ ОБЪЯВЛЯТЬ ОДНУ И ТУ ЖЕ ЦЕЛЬ ДВАЖДЫ ПОДРЯД.
 *
 * ЗАЧЕМ. Строка «PROACTIVE TARGET -> …» печатается на смену цели. В поле 85.60
 * их было 28 за сессию (11 у Guardian, 17 у Nexus), и почти половина — качели
 * между двумя гоблинами в упор: цель меняется на каждом тике, потому что тела
 * встают то ближе, то дальше. Для разбора это шум: решение не менялось, менялось
 * только чей нос ближе. При этом строка нужна — она показывает, что доктрина
 * действительно забирает цель и на каком классе угрозы.
 *
 * ЧТО ДЕЛАЕМ. Запоминаем последние тела, о которых объявляли, и молчим, если то
 * же тело объявляют раньше чем через kCooldownMs. Молчание считается, и уходит в
 * сводку сессии («muted»): видно, сколько шума свернули, — то есть подавленное
 * не теряется.
 *
 * ЧЕГО НЕ ДЕЛАЕМ. Не трогаем саму цель, latch и темп: throttle — только про
 * печать. Поведение доктрины остаётся ровно тем же (см. Guardian/Nexus).
 */
#include <stdint.h>
#include <windows.h>
#include <string.h>

namespace PawnAI {

struct AnnounceThrottle {
    static const int   kSlots     = 4;      // помним четыре последних тела
    static const DWORD kCooldownMs = 3000;  // окно тишины на тело

    struct Rec { uintptr_t body; DWORD ms; };

    Rec rec[kSlots];
    int next;

    AnnounceThrottle() : next(0) { memset(rec, 0, sizeof(rec)); }

    // true — объявлять; false — то же тело уже объявляли в окне тишины.
    bool Allow(uintptr_t body, DWORD now)
    {
        if (!body) return true;              // цель без тела объявляем всегда
        for (int i = 0; i < kSlots; ++i)
            if (rec[i].body == body && DWORD(now - rec[i].ms) < kCooldownMs)
                return false;
        rec[next].body = body;
        rec[next].ms   = now;
        next = (next + 1) % kSlots;
        return true;
    }
};

} // namespace PawnAI
