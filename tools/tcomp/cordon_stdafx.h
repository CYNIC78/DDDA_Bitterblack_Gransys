#pragma once
// Шим для модулей слоя пешек.
//
// g++ не понимает структурную обработку исключений MSVC, а PawnAI_Common.h
// на ней построен. Подменяем __try/__except обычным try/catch: для
// проверки СИНТАКСИСА этого достаточно, семантика SEH здесь не важна.
#define __try try
#define __except(x) catch(...)

#include "director_stdafx.h"
#include <stdint.h>

extern BYTE** pBase;
