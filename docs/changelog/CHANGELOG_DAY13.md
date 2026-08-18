# День 13 — 18.08.2026: Build 61 — охота за code 4 и code 66

## Контекст

Раскрываем семантику двух нераскрытых Guardian-кодов:
- code 4  — «wait/follow», Guardian primary +3 (поощряется);
- code 66 — «battle response», Guardian primary -4 (сильно штрафуется).

Оба статически «unmatched» (PlanCtrl пуст без активации).

## Что сделано

- Сгенерирован `GoapInterfaceMap.inl` — таблица InterfaceID → имя GOAP-ресурса
  (127 записей) из pawn_ai_catalog.json. Позволяет резолвить .gop имя в C++.
- Прицельный ловец `GuardianIntentHuntTick()` — всегда в фоне (read-only):
  при selected code == 4 или 66 снимает exact cPlAct, target, packed и
  PlanCtrl links → InterfaceID → .gop имя.
- Статус «Intent hunt: ...» в панели Guardian Doctrine + полная строка в лог.

## Следующий шаг

Поймать оба кода в бою (code 4 — при высоком Guardian, code 66 — при низком
Guardian + высоком Scather), закрепить семантику, затем решить, как их
использовать в доктрине (code 4 = «держать позицию» — вероятно не трогаем;
code 66 = «боевой ответ» — кандидат на снятие штрафа при перехвате).
