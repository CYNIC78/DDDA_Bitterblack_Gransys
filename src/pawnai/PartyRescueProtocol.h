#pragma once
/**
 * PartyRescueProtocol.h — Общепартийный протокол экстренного спасения Аризена.
 *
 * КОНЦЕПТ:
 * Когда Аризена захватывает, валит или уносит враг:
 *   - Гримгоблин: яростный топот ногами (cEm0100ActDwnAtTramplePl);
 *   - Огр: захват и жевание в кулаке (cEm0500ActCatch* / cEm0500ActEat*);
 *   - Гарпия/Горгулья: унос в воздух для сброса (cEm0600ActCatch* / cEm0900ActCarryCatchPl);
 *   - Циклоп: сжатие в лапе (cEm5000ActCatchPl*);
 *   - Химера/Дрейк/Дракон: захват и прижатие (cEm5200_01ActAtkBiteCatch* / cEm5900Catch);
 *   - Волк: удержание на земле (cEm0200ActDownBite);
 *
 * Вся патия (главная пешка + наёмники) немедленно объявляет EMERGENCY RESCUE:
 *   1. Находит точное тело врага-захватчика (Captor Body).
 *   2. Всем живым пешкам пати записывает captorBody в регистр цели (uCmc + 0x2EB8) и фокус (+0x14E0).
 *   3. Снимает все пассивные склонности (Guardian/Nexus/Acquisitor) и выставляет максимальную атакующую агрессию.
 *   4. Дает пешкам темп-ускорение (std-rush) для немедленного перехвата и сбивания захвата с монстра!
 *
 * Когда Аризен свободен — протокол немедленно и безопасно завершается (Fail-Closed).
 */

#include <stdint.h>
#include <windows.h>

namespace PawnAI {
namespace Rescue {

void Init();
void Shutdown();
void Tick();

bool IsActive();
uintptr_t CurrentCaptorBody();
const char* CurrentCaptorKind();
const char* CurrentCrisisReason();
uint32_t CrisisCount();

} // namespace Rescue
} // namespace PawnAI
