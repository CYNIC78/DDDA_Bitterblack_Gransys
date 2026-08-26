# PS — статусы партии + downed/revive observer (`84.16`, опрос детей — `84.17`)

`docs/POSSESSION_RECON.md` дала кандидатов статусной подсистемы
(`cStatus` 152 B, `cStatus::cStatWork` 32 B, `uCharacterBase::StatusEffect`
20 B, `cEffectStatus` 40 B, `cEffectStatusManager` 32 B) и план живых
замеров. `src/runtime/PartyRecon.cpp` уже держит зарезервированный канал
`statusMask`/`statusValid` («Zero bits do NOT mean healthy») и
`downedValid`/`downedRevivable` («не угадываются, наружу valid=false»).

`Runtime::PartyStatus` — read-only прибор, закрывающий оба канала
наблюдением. Записей ноль, новых галок F12 нет. Тик в продуктовой ленте
под своим SEH, до гейта gameplay — работает при выключенном Director.

## Два канала

### A. Статусные блоки (якорный поиск + дельта)

По каждому телу партии (Arisen + 3 пешки, тела — только из
`ArisenBody()`/`PartyRecordInfo()`, никаких догадок по порядку):

1. **Discovery** — `FindChildByClass` по имени класса. Полный обход тела
   дорог (DTI-имя на каждый 4-байтовый указатель), поэтому:
   - один класс за проход, троттл **3 с** (та же цена, что у резолвера
     планировщика пешки, и не чаще);
   - после находки — только точечное чтение блока (152/32 B) и
     верификация живости указателя;
   - ушёл указатель → блок возвращается в поиск, не гадаем.
2. **Дельта** — блок, изменившийся с прошлого снимка, печатается
   `PS: <slot> cStatus @ptr +0xNN a->b ...` (до 24 полей + счётчик,
   троттл 200 мс на блок). Первое чтение — тихая базовая линия.
3. **Искали только два класса на уровне тела**: `cStatus` (главный
   подозреваемый) и `cEffectStatusManager`. Вложенные
   (`cStatus::cStatWork`, `uCharacterBase::StatusEffect`, `cEffectStatus`)
   НЕ ищутся перебором всего тела: следующий билд расковыряет сам найденный
   `cStatus` — 152 B = 38 указателей, это точечный обход, а не census
   (FIX_RULES §5.1).

Строки:

```text
PS: MainPawn track @0x... (read-only status + downed/revive observer)
PS: MainPawn cStatus found @0x... off=+0x... size=152
PS: MainPawn cStatus @0x... +0x24 0->1  +0x30 0.000->5.000
PS: MainPawn hb: cStatus=found mgr=scanning downed=0 everRevived=0
```

### 84.17: discovery + опрос детей одним проходом

Лог 24: `cStatus`/`cEffectStatusManager` **не найдены** ни на одном из
4 тел за ~5 минут (15+ discovery-проходов). Подтвердился риск: блок
статусов, вероятно, не «ребёнок тела» по указателю. Поэтому discovery
теперь сам печатает карту детей: тот же проход (та же цена —
указатель каждые 4 байта + DTI-имя, кэш имён по vtable) собирает
классоподобные имена (c*/r*/s*/u*), до 12 уникальных, со смещениями:

```text
PS: MainPawn children (scan cStatus): cAICtrl@+0x2E64 cActionManager@+0x... ...
```

Троттл 15 с на тело; опрос идёт, пока не найден хотя бы один блок
(после `allFound` — ни обходов, ни строк). Дальнейший поиск блока
статусов — по этой карте: по имени, по сигнатуре констант, по
известному якорю (вражеский аналог `rStatusParam` живёт в теле
гоблина на `+0x2710` — STATUS_EFFECTS_RECON §3).

### B. Downed/revive FSM (84.24: с тела, которое упало)

Пешки ГГ не воскрешают. `cPlReviveCMC` — акт **Аризена** «я поднимаю
пешку». На теле пешки его нет (атлас: отдельного `cPlActCmcRevive`
тоже нет). Состояние падения/подъёма читается с live-акта **этого** тела.

```text
пешка:
  * -> CmcNeardeath|CmcDead|DmgDownDead     DOWNED   (ждёт succor)
  neardeath -> CmcReturn                    RIFTED
  neardeath -> обычный акт                  RAISED   (тело встало)
  * -> DmgDown|DmgDownDamage                KNOCKDOWN (не succor)
  knockdown -> StandUp/обычный              KNOCKDOWN-END
  cPlReviveCMC на пешке                     игнор

Аризен:
  cPlReviveCMC                              RAISE (поднимает пешку, не DOWNED)
  cPlActDead                                DEAD  (не succor-жертва)
  DmgDown                                   KNOCKDOWN, как у пешки
```

Заполнение снапшота (`FillMemberStatus`, только кэш FSM, без чтений):

- `downedValid` — тело в neardeath или knockdown, свежесть ≤5 с;
- `downedRevivable` — пешка, сейчас neardeath, и на этом теле уже был
  `RAISED`. Аризен всегда false.

`statusMask`/`statusValid` **остаются 0/false**: 84.16 ни одно поле
блока ещё не маппит на именованный статус — маппинг появится, когда
possession-замер даст именованное поле (POSSESSION_RECON план, шаг 4).

## Как тестировать

**84.30:** лагерь. F12 → Enemy AI Overhaul → Monster director →
`snapshot to log`. В логе `PS: SHEET` / `PS: REC` / `PS: BODY`.
Яд (фласка/стрела) на пешку → второй snapshot. Дрейк/вода не нужны.

1. Тег `MOD_BUILD_TAG 84.30-party-sheet`; Director можно не включать.
2. **На берегу (сразу доступно):** ночной бой, пешка хватает гоблина
   (`GrabStart`/`Hagaijime`) — рестрейнт форсирует discovery-проход;
   в логе `PS: MainPawn cStatus found ...` (или `scanning` ещё 1–2
   прохода) и дельты, если блок реагирует на захват. Пешка падает
   (HP=0) и её поднимает игрок: `DOWNED` → `RAISED` на пешке,
   `RAISE act=cPlReviveCMC` на Аризене. Таймер без подъёма: `RIFTED`.
3. `snapshot to log` (кнопка Director) печатает FSM + `PS: SHEET`
   (запись + тело hex). `cStatus not-found` на детях тела — ожидаемо.
4. **Possession (отдельная сессия, Грейтволл/Даймон):** драконид
   хватает пешку -> одержимость. Снимать A/B: `snapshot to log` ДО
   захвата, ВО ВРЕМЯ (красные глаза) и ПОСЛЕ (Panacea/добивание).
   Поле, которое живёт ровно в это окно, — possession-бит; он и есть
   вход в `statusMask` и в учёт директора.

## Честные риски

- `cStatus` может оказаться не ребёнком тела, а встроенным блоком
  (тогда discovery не найдёт и будет честно сканировать вечно,
  1 проход/3 с — цену держим, но следим за лог-трафиком). Альтернатива
  на этот случай: сигнатурный поиск по характерным константам блока.
- Блок может пересоздаваться под/после эффекта — это штатно
  обрабатывается (re-discover), но дельты между «до» и «после»
  пересоздания не связываются: для possession A/B важны полные
  `PS:`-дамп из снапшота, а не только дельты.

## Следующий билд (84.17+, по результату)

1. Раскладка `cStatus` из `snapshot to log` + `GOBCARD`-материала:
   именованные биты статусов -> `statusMask` (read-only маппинг).
2. Учёт в `ScoreParty()` директора (категория (3), только после
   наблюдательного подтверждения): possession -> член **исключается из
   набора целей** (он враг партии, ванильные монстры его игнорируют),
   остальные перегруппировываются; sleep/restraint/slow -> бонус
   уязвимости к `huntScore` (веса по таблице, по умолчанию 0).
