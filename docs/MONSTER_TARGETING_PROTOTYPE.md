# Monster Director — Build 012 urgency + mobilization

**Build tag:** `84.9-pilot012-urgency-mobilization`  
**Статус:** source pilot; exact `uEm0200`; default OFF; новых F12 controls нет.

## 1. Что изменилось

Build 012 — первая интегрированная итерация приказа Director:

```text
exact target body + normalized urgency
        ├─ Aggro: кому отвечать
        └─ Tempo: как мобилизовать exact свободных responders
```

Каждый реально исполняемый приказ — emergency. Поле `urgency` имеет строгий
диапазон `0..1`; все текущие strategic/tactical команды задают `1.0`. Невалидное
значение не зажимается молча, а отклоняет policy fail-closed.

Прежняя политика удалена полностью:

- ALERT больше не остаётся без Tempo;
- ALARM больше не использует generic `loco ×1.03 / attack ×0.96`;
- Director не вызывает `Tempo::SetOverride()`/`ClearOverride()`.

Вместо этого Tempo владеет одной специальной bounded mobilization row на каждое
точное тело свободного responder.

## 2. Модель mobilization

Для каждого допущенного тела:

- `m=0`: личная стабильная пара `L0/A0`, назначенная обычной Tempo mutation;
- `m=1`: личный deterministic rage endpoint `L1/A1` для exact species;
- `L(m) = L0 + (L1-L0)×m`;
- `A(m) = A0 + (A1-A0)×m`.

Для pilot species `uEm0200`:

| Канал | Stable profile по умолчанию | Personal rage endpoint |
|---|---:|---:|
| dash/track/run/walk locomotion | `1.05..1.20` | `1.20..1.25` |
| attacks-only animation | `1.05..1.15` | `1.20..1.26` |

Точные endpoints детерминированы отдельно для каждого body. Поэтому даже при
`m=1` стая не превращается в одинаковые клоны. Admission требует `L0<L1` и
`A0<A1`; несовместимый пользовательский stable profile отклоняется, а не
ratchet-ит endpoint.

Сейчас emergency urgency `1.0` быстро поднимает responder до `m=1` на policy
actuation и удерживает там, пока приказ допущен. Ordinary release запускает
линейный decay до `m=0` за **1400 ms**. Служебный TTL 600 ms защищает от
зависшего приказа, если явный refresh/release перестал приходить.

### Нератчетирующие инварианты

- не больше одной Director row на exact body;
- повторный/перекрывающийся приказ только refresh-ит TTL и максимизирует `m`;
- `L0/A0/L1/A1` после admission неизменны;
- transitional live factor никогда не становится новой baseline;
- boost не умножается сам на себя;
- переход ALERT→ALARM может переиспользовать ту же row без скачка endpoint.

## 3. Композиция Tempo

Для enemy body оба канала вычисляются в фиксированном порядке:

```text
immutable stable baseline
→ Director stable-to-rage envelope
→ independent generic override
→ final species-safe clamp
```

Generic override остаётся отдельной старой ручкой. Director release/reset не
очищает generic rows. Override-only party bodies сохраняют прежний путь и не
получают enemy mobilization.

Границы финального слоя остаются безопасными: locomotion `0.75..1.30`, animation
`0.70..1.40`; rage endpoints `uEm0200` лежат внутри них.

## 4. ALERT и ALARM остаются разными

Оба response tier запрашивают full mobilization `m=1`, потому что оба являются
экстренными командами. Тактическое отличие не исчезает:

| Response | Aggro bundle | Evidence lease | Tempo |
|---|---|---:|---|
| `PACK-GRAB-ALERT` | exact target pin only; без suppress/fake-hit | 750 ms | full urgency для free responders |
| `PACK-GROUND-PIN-ALARM` | pin + suppress + bounded fake-hit | 4000 ms | full urgency для free responders |
| `PACK-LIFT-RESCUE` | pin + suppress + bounded fake-hit | 2500 ms | full urgency для free responders |
| strategic FOCUS-WINDOW | ALARM-strength target response | strategic intent lifecycle | full urgency для free responders |

Aggro потребляет exact party target. Tempo не знает target и получает только
exact responder body, exact kind, urgency и TTL.

## 5. Сохранённая таксономия и cue table

Три механики не смешиваются:

| Класс | Смысл | Текущий recipe |
|---|---|---|
| ground pin | пешка прижимает волка/завра к земле весом тела | `GrabStart` ALERT; `Hagaijime4Feet` ALARM |
| standing restraint | пешка стоит сзади и держит, например, гоблина под руками | будущая отдельная species recipe |
| literal lift/carry | монстра действительно подняли/несут | `cPlActLift* + cEm0200Lifted` ALARM |

`src/monsterai/TacticalCues.cpp` остаётся universal table-driven matcher:

| Rule | Target action | Evidence | Priority | Urgency | Max lease |
|---|---|---|---:|---:|---:|
| `PACK-GROUND-PIN-ALARM` | exact `cPlActHagaijime4Feet` | exact `uEm0200`, одна spatial pair ≤2.0 m | 200 | 1.0 | 4000 ms |
| `PACK-LIFT-RESCUE` | exact `cPlActLift*` | globally unique exact `cEm0200Lifted`, pair ≤2.5 m | 150 | 1.0 | 2500 ms |
| `PACK-GRAB-ALERT` | exact `cPlActGrabStart` | exact `uEm0200`, одна spatial pair ≤2.0 m | 100 | 1.0 | 750 ms |

`Hagaijime4Feet` — independently sufficient action acting pawn: предшествующий
`GrabStart` не требуется. Ground pin никогда не называется `Lifted`; perfect
block, `cEmActDmgRestraint` и голосовая реплика admission не нужны.

Первичный admission остаётся строгим и требует одной допустимой spatial pair.
После него continuation проверяет **те же exact holder/victim bodies и recipe**.
Появление unrelated duplicate candidate не может украсть или преждевременно
снять уже допущенную пару. Lease исходного event всё равно ограничивает hold.

## 6. Exact responder ownership

Exact paired/restrained `uEm0200` исключается до actuation и не получает ни
Aggro mutation, ни Tempo envelope. Остальные живые exact same-kind bodies —
свободные responders.

Director хранит отдельно:

- точный target slot/body;
- situation/response/urgency;
- exact excluded body и topology signature;
- полный responder set;
- exact Tempo-owned body set;
- диагностические диапазоны `L0/A0/L1/A1`.

Если paired wolf — единственный, policy заканчивается
`wolf-pack-no-free-responder` без gameplay write.

## 7. Release: decay или hard reset

### Ordinary completion → decay

- strategic intent естественно вернулся в NONE/BIAS;
- exact tactical recipe/evidence нормально закончился;
- безопасный command transition при неизменной identity/topology.

Только текущие owned rows переходят в 1400-ms decay. Aggro target lease сразу
снимается.

### Unsafe condition → immediate hard reset

- party identity/body/species/readiness failure;
- потеря responder/exclusion/event topology;
- stale/missing world state;
- hard tactical timeout;
- partial admission или downstream Aggro rejection/rollback;
- Tempo disable, attack-animation disable или смена attacks-only→everything;
- Director/actuator disable;
- shutdown.

Hard reset очищает все Director rows, включая ранее decaying, но не generic
Tempo overrides. Это исключает stale world state и перенос ownership между
несвязанными телами/приказами.

## 8. Fail-closed gates

До gameplay write обязательны:

- свежий WorldReport (не старше 450 ms);
- exact четыре party records и четыре уникальных body;
- совпадение snapshot body с независимым fixed-slot Aggro bridge;
- exact party action и exact admitted target body;
- exact `strcmp(kind, "uEm0200")`, не prefix;
- строгая первичная unique spatial pair;
- наличие хотя бы одного exact свободного responder;
- bounded topology, urgency, TTL и evidence lease;
- Tempo movement ON, general locomotion hook, animation ON, attacks-only scope;
- прежний default-off `wolfActuator` consent;
- downstream Aggro row shape/native/readback/rollback safeguards.

## 9. Что сохранено

Без изменения gameplay semantics остаются:

- strategic absolute-current-HP `PackMark` и 2500-ms hysteresis;
- `PackMark` memory во время tactical interrupt;
- direct `Hagaijime4Feet` admission и weak `GrabStart`;
- literal-lift separation и exact responder exclusion;
- universal cue recognition при species-specific write admission;
- Guardian behavior и inclinations;
- stable shipped profile: locomotion `1.05..1.20`, sprint OFF,
  attacks-only animation `1.05..1.15`, coupling `0.00`;
- HP, damage, stagger, immunity и native action eligibility/sequences;
- существующие `[monsterAI] enabled` и `wolfActuator`; новых F12 controls нет.

## 10. Как тестировать в игре

Не нужно следить за быстро меняющимися цифрами. Нужен полный автоматический лог;
`snapshot to log` нажимается редко — только после anomaly/partial или когда нужно
зафиксировать один устойчивый момент.

1. Собрать и установить source Build 012.
2. В первой строке лога проверить exact tag:
   ```text
   84.9-pilot012-urgency-mobilization
   ```
3. Включить прежние `enable monster director` и
   `enable Director actuator (WRITES)`. Новых переключателей искать не нужно.
4. Убедиться, что UI показывает все четыре Tempo readiness как `yes`.
5. В бою с несколькими волками дать любой пешке начать `GrabStart` и, по
   возможности, завершить ground pin.
6. После боя сохранить **полный лог**. Не выписывать вручную fast counters,
   distances или HP.

### Ожидаемый failed GrabStart

```text
situation ENGAGED name=PACK-GRAB-ALERT response=ALERT urgency=1
policy ENGAGED reason=tactical-grab-alert ... urgency=1 ... responders=N tempoOwned=N mobilization=HOLD endpoints{...}
situation RELEASED name=PACK-GRAB-ALERT ... actuation=DECAY
policy RELEASED ... mobilization=DECAY
```

ALARM появляться не обязан, но ALERT теперь **обязан** иметь Tempo ownership для
всех exact free responders. Это главное отличие Build 012 от Build 011.

### Ожидаемый successful ground pin

```text
situation ENGAGED name=PACK-GROUND-PIN-ALARM response=ALARM urgency=1 ... holderAct=cPlActHagaijime4Feet
policy ENGAGED reason=tactical-ground-pin-alarm ... response=ALARM urgency=1 ... responders=N tempoOwned=N mobilization=HOLD endpoints{...}
```

Прямой первый event `PACK-GROUND-PIN-ALARM` нормален: отсутствие sampled
`GrabStart` не является ошибкой. По Aggro summary ALARM должен сохранять сильный
bundle, ALERT — только pin.

### Что проверить редким snapshot

Важны `situation`, `response`, exact `targetBody`, `urgency`, `responders`,
`tempoOwned`, mobilization phase и endpoint ranges. Реальную силу партии не
оцениваем по vocation или неподтверждённым core DEF/ATK.

## 11. Автоматические проверки

```bash
bash tools/test_monster_director_hp_only.sh
bash tools/test_build004_contracts.sh
bash tools/test_build005_locomotion_proof.sh
bash tools/test_build008_qol.sh
bash tools/test_act_map_build004.sh
python3 tools/check_link_sanity.py
python3 tools/analyze_devtools_layers.py
python3 tools/check_cpp_literals.py
bash tools/syntax_check.sh
```

Focused fixtures проверяют target+urgency, full mobilization ALERT/ALARM,
immutable endpoints, non-ratcheting refresh, exact override composition, linear
decay, TTL expiry, hard reset/re-admission, capacity 16, sticky exact-pair continuation и
сохранение стратегического PackMark. Отдельный PartyRecon fixture
проверяет unique fixed-record-backed Arisen, zero/duplicate fail-closed и полный
массив четырёх fixed slots. Интегрированный fixture чередует неактивные
NONE/BIAS/identity reasons и доказывает один `HARD-RESET-ONCE`, отсутствие
ложных RELEASED lines и итоговый `RECOVERED coalesced=N`.

## 12. Ограничение пакета

В среде подготовки нет MSBuild/Visual Studio и MinGW cross-compiler, поэтому
Release Win32 DLL здесь не производится. Build 012 поставляется как source
archive с manifest и SHA-256; продуктовые модули проверяются strict g++ fixtures,
portable syntax shims и статическими preservation contracts.
