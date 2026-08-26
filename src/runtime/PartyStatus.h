#pragma once
// Runtime::PartyStatus — read-only прибор: статусы партии + downed/revive.
//
// 84.16 (dual-observe). Полный замысел и протокол — docs/PARTY_STATUS_OBSERVE.md.
//
// ЗАЧЕМ. PackMark директора сегодня видит только HP (MOMENT-HP). Чтобы
// «выбрать наиболее слабую цель», ему нужны ослабленные состояния партии:
// restraint, сон, в первую очередь possession (пешка становится врагом
// игрока и перестаёт быть членом партии для целей).
//
// ГРАНИЦА 84.16: ТОЛЬКО НАБЛЮДЕНИЕ.
//   1. Статусные блоки на телах партии — поиск по имени класса-якоря
//      (cStatus 152 B, cEffectStatusManager 32 B) + дельта-дампы блока.
//      Полный обход тела — только discovery-проходы с троттлом 3 с
//      (census запрещён, FIX_RULES §5.1); после находки — только точечные
//      чтения. Вложенные объекты (cStatus::cStatWork,
//      uCharacterBase::StatusEffect, cEffectStatus) по телу НЕ ищутся:
//      следующий билд расковыряет сам блок cStatus — 152 B = 38 указателей,
//      это точечный обход, а не перебор.
//   2. FSM падения/подъёма ЧИТАЕТСЯ С ТЕЛА, КОТОРОЕ УПАЛО (84.24/84.25).
//      CrumbleDead = neardeath. Arisen DEAD снимает leftover downed.
//      Пешка: CmcNeardeath/CmcDead → RAISED (обычный акт) / RIFTED (CmcReturn).
//      Нокдаун DmgDown → DmgStandUp — не succor. cPlReviveCMC на пешке
//      игнорируется (это акт Аризена). Аризен: RAISE=cPlReviveCMC, не жертва.
//      downedRevivable — только пешка, и только после RAISED с neardeath.
//
// ЗАПИСЕЙ НЕТ. Новых галок F12 нет. Лог (строки PS:) — основной интерфейс.
// statusMask/statusValid снапшота остаются false: 84.16 ни одно поле блока
// ещё не маппит на именованный статус — это задача possession-замера.

#include <stdint.h>

namespace Runtime {

struct PartyCombatMember;

namespace PartyStatus {

void Init();
void Shutdown();
// Зовётся из продуктового тика под своим SEH. Работает и при выключенном
// Director (прецедент PackObserve: read-only ночной прибор).
void Tick();

// Заполняет подтверждённые наблюдением поля члена снапшота партии.
// Дёшево: только кэшированное состояние FSM, без чтений памяти.
void FillMemberStatus(uintptr_t body, int slot, PartyCombatMember& M);

// Ручной дамп (snapshot to log): FSM + найденные cStatus + SHEET
// (запись персонажа + тело, hex). Та же кнопка Director.
void DumpSnapshot();

// Строка статуса устройства (ASCII).
const char* Status();

} // namespace PartyStatus
} // namespace Runtime
