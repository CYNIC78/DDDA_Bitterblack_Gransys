# Тест 75.56 — caster AI range

Тег лога: `75.56-anodyne`. Канон: `docs/WAND_RANGE.md`.

```ini
[errata]
wandRange = on
```

Бой с магом/чародеем (наёмный ок). В логе:

```text
WandRange: APPLIED live cCmc ...
WandRange: applied bands: cCmcMagicUserCombo 5.0-10.0 cCmcLightningCloud 5.0-10.0
```

`cCmcIceWalk 0.0-5.0` только в `seen`. Лук/кинжал не в `applied`.
