#pragma once
/**
 * PawnAI::VocationCordon — вокационный кордон Guardian.
 *
 * ЗАЧЕМ. Guardian как доктрина телохранителя осмысленна только у ближнего
 * боя. У лучника и кастера активный Guardian вреден: пешка прижимается
 * к Аризену и стреляет из-за его спины, то есть тащит агро прямо на него.
 * Capcom это «предотвратили» штрафами на атаку — и побочно превратили
 * Guardian в пассивный хвост.
 *
 * Мы штрафы сняли (Build 57), и для мили это правильно. Для дальнего боя
 * — нет: получился активный лучник, висящий на плече игрока.
 *
 * ЧТО ДЕЛАЕМ. Гасим Guardian у тех вокаций, которым он не подходит, и
 * добавляем перевес Pioneer, чтобы пешка не липла к якорю.
 *
 *   Fighter / Warrior / Mystic Knight  — не трогаем, Guardian по делу
 *   Strider / Assassin (гибрид)        — частичный кордон
 *   Ranger / Magick Archer             — полный кордон
 *   Mage / Sorcerer                    — полный кордон
 *
 * ПОЧЕМУ ПОТОЛОК, А НЕ ВЫЧИТАНИЕ. Кордон задаёт ПОТОЛОК склонности, а не
 * вычитает фиксированное число. Так он предсказуемо складывается с
 * ползунками игрока: поставил Guardian ниже потолка — кордон молчит.
 *
 * ЧЕСТНОЕ ОГРАНИЧЕНИЕ. Pioneer — грубый рычаг «не липни к якорю», а не
 * управление позицией. Настоящее «встань на фланг, стреляй из тыла»
 * требует реверса цели Follow (отдельная задача, см.
 * docs/GUARDIAN_VOCATION_MATRIX.md). Здесь мы лишь снимаем вред, а не
 * рисуем построение.
 */

#include <stdint.h>

namespace PawnAI {

class VocationCordon {
public:
    void Init();
    void Shutdown() {}

    // Вклад в общую дельту склонностей. Вызывается оркестратором.
    void GetDelta(const float* target, float* delta);

    bool  enabled = true;

    // Потолки и полы. Значения по умолчанию — первая прикидка, доводятся
    // ползунками в панели.
    float guardianCapRanged  = 300.0f;   // лучник, магический лучник, маг, чародей
    float pioneerFloorRanged = 650.0f;
    float guardianCapHybrid  = 550.0f;   // страйдер, ассасин
    float pioneerFloorHybrid = 550.0f;

    // --- для панели ---
    int         lastVocation = -1;
    const char* lastClassName = "?";
    const char* lastAction = "idle";
    float       lastGuardianCap = 0.0f;   // 0 = кордон не применялся
};

} // namespace PawnAI
