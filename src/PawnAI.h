#pragma once

namespace Hooks
{
    void PawnAI();
    void PawnAI_Shutdown();
}

// Вспомогательные функции
// УДАЛЕНО (75.2): `float* GetPawnInclinations(int)` объявлялась здесь, но
// определения не было ни в одном .cpp — фантом из старого монолита.
// Первое же обращение к ней уронило сборку на LNK2019, хотя линтер писал
// про «нет тела» задолго до. Чтение склонностей — через
// `ReadAllIncl()/WriteAllIncl()` в `pawnai/PawnAI_Common.h`.
