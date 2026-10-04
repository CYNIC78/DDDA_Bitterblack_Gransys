// -*- 85.63: ФИКСТУРА ОКНА ТИШИНЫ НА ОБЪЯВЛЕНИЕ ЦЕЛИ.
//
// ЧТО ПРОВЕРЯЕМ. У окна тишины (DoctrineAnnounce.h) четыре свойства, и каждое
// ломается молча, а в поле выглядит как «доктрина перестала брать цель»:
//   1) первое объявление тела — всегда проходит;
//   2) то же тело внутри окна — молчит (это и есть цель правки: в поле 85.60
//      половина объявлений была качелями между двумя гоблинами в упор);
//   3) другое тело — проходит сразу (новое решение не глушим);
//   4) окно кончилось — тело можно объявлять снова (иначе долгий бой с одним
//      врагом останется вообще без строк).
// Плюс границы: тело 0 (цель без тела) не глушится никогда, а память кольца
// ограничена четырьмя телами — пятое вытесняет самое старое, и это записано в
// коде как ожидаемое поведение, а не как сюрприз.
#include "shim/windows.h"
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <iostream>

#include "../../src/pawnai/DoctrineAnnounce.h"

using PawnAI::AnnounceThrottle;

int main()
{
    // 1) первое объявление и тишина внутри окна
    {
        AnnounceThrottle th;
        assert(th.Allow(0x1000, 1000));
        assert(!th.Allow(0x1000, 1500));
        assert(!th.Allow(0x1000, 3999));
        // 4) окно кончилось
        assert(th.Allow(0x1000, 4000));
        std::cout << "  same body muted inside window, allowed after: ok" << std::endl;
    }
    // 3) другое тело проходит сразу
    {
        AnnounceThrottle th;
        assert(th.Allow(0x1000, 1000));
        assert(th.Allow(0x2000, 1001));
        assert(th.Allow(0x3000, 1002));
        assert(!th.Allow(0x1000, 1003));   // а вернулись к первому — тишина
        std::cout << "  new body passes, ping-pong muted: ok" << std::endl;
    }
    // тело 0 — не цель, а «нет тела»: глушить нечего
    {
        AnnounceThrottle th;
        assert(th.Allow(0, 1000));
        assert(th.Allow(0, 1001));
        std::cout << "  body 0 never muted: ok" << std::endl;
    }
    // кольцо на четыре тела: пятое вытесняет самое старое
    {
        AnnounceThrottle th;
        for (uintptr_t b = 1; b <= 4; ++b) assert(th.Allow(b * 0x1000, 1000));
        assert(!th.Allow(0x1000, 1100));   // ещё в памяти
        assert(th.Allow(0x5000, 1100));    // вытесняет 0x1000
        assert(th.Allow(0x1000, 1101));    // вытесненного можно объявить снова
        std::cout << "  ring of 4, oldest evicted: ok" << std::endl;
    }
    std::cout << "announce throttle: PASS" << std::endl;
    return 0;
}
