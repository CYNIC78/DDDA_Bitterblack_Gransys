# День 14 — 18.08.2026: Build 62 — переработка детектора боя

## Контекст

- Code 4 и 66 не пойманы за 15 мин боя — помечены ТЕХДОЛГОМ (нужны особые
  условия; вернёмся позже).
- Задача: переработать детектор начала/конца боя. Старый висел на уроне +
  таймере отсутствия.

## Решение

Трёхсигнальный детектор с гистерезисом (CombatIntel::IsInCombat):
1. урон (кольцо, как раньше);
2. боевые действия врагов (WorldReport.enemyCombatCount) — по DTI-имени live
   Act, новый классификатор EnemyActNameIsCombat (Atk/Dmg/Guard/Eva/Dash/
   Charge/Howl/Provoke/Roar/Escape/Bite/Grab/Stomp/Tail/Breath/Fire/Shot/Swing);
3. цель пешки (WorldReport.pawnEngaged) — uCmc+0x2EB8 != 0, читается в
   PartyReadPositions.

Гистерезис: вход мгновенный, выход через 2.5 с тишины. Локомоция не считается
боем (консервативно против ложных срабатываний).

Изменённые файлы: CombatBus.h (+2 поля WorldPresence/WorldReport),
DevTools.cpp (классификатор, заполнение, чтение цели), CombatIntel.cpp/h
(IsInCombat трёхсигнальный, PublishToBus), PawnAI.cpp (UI-строка сигналов).

## Техдолг

- Раскрытие code 4 (wait/follow, Guardian +3) и code 66 (battle, Guardian -4):
  статически unmatched, PlanCtrl пуст без активации. Ловец Build 61
  (GuardianIntentHunt) оставлен в коде и работает в фоне — поймает при случае.
