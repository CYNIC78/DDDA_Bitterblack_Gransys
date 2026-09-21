#!/usr/bin/env bash
# AudioRedirect: static isolation + syntax check без MSVC (84.95).
#
# g++ резервирует __try/__except лексически (макросы не спасают), поэтому
# перед компиляцией над ВРЕМЕННОЙ копией делаем sed-преобразование:
#   __try  -> if (true)
#   __except (EXCEPTION_EXECUTE_HANDLER) -> else
# (все использования в модуле имеют ровно эту форму — проверено по коду).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$(mktemp -d /tmp/audioredirect.XXXXXX)"
trap 'rm -rf "$TMP"' EXIT

python3 - "$ROOT" <<'PY'
from pathlib import Path
import sys
root = Path(sys.argv[1])
ar = (root / 'src/audio/AudioRedirect.cpp').read_text(encoding='utf-8')
tag = (root / 'src/BuildTag.h').read_text(encoding='utf-8')
assert '84.' in tag
# 84.95: recovery pipeline (голос каждой смены состояния — FIX_RULES 4б)
assert 'InvalidateFound' in ar
assert 'INVALIDATE' in ar
assert 'RECOVERY' in ar
assert 'WaitForFound' in ar
assert 'MaybeProbe(true)' in ar
# 84.94: guard-skip (фикс краша сейва) — НЕ должен уйти
assert ar.count('PAGE_GUARD)') >= 2
# 84.95: потокобезопасность реестра
assert 'CRITICAL_SECTION g_stateCrit' in ar
assert 'static PatchRec g_tablePatch[128]' in ar
# 84.93-регрессия (лупы ванильные) — не должна вернуться:
assert 'TableInRegion' not in ar          # skip всех найденных таблиц
assert 'g_loopVictims' not in ar          # общий known-список между потоками
assert 'static bool  g_strikeCoherence = false' in ar  # гейт OFF по умолчанию
# 84.96: voice-hunt (обрыв пост-сеи на ванильной отметке)
assert 'g_struckOnce' not in ar           # latch pass0 «один раз на ключ» убран
assert 'LocalStrikePass' in ar            # целевая добивка на #2/#3 борста
assert 'g_trackVictims' in ar             # реестр известных копий
assert 'rec @' in ar                      # record dump для раскладки голоса (84.97: адрес вместо inA)
assert 'TABLE [' in ar                    # snapshot таблицы в момент патча
assert 'InvalidateFoundLocked' in ar      # WATCH-дрейф → проактивный INVALIDATE
# 84.97: voice-window (копия голоса рождается после pass0)
assert 'VoiceWindowThread' in ar          # короткоживущий 6-с поток
assert 'g_windowAlive' in ar              # CAS: одно окно на всё приложение
assert 'ScanReverseAllFast' in ar         # прогон вниз с верха (новые аллокации)
assert 'HotPass' in ar                    # горячие ±512KB
assert 'MAPPED-HIT' in ar                 # mapped-телеметрия без записи
assert 'voice_window.txt' in ar           # отдельный отчёт окна
# 84.96a: MSVC — windows.h определяет макросы min/max: std::max(/std::min(
# разворачиваются в мусор (C2589 «недопустимая лексема после ::»).
assert 'std::max(' not in ar and 'std::min(' not in ar
print('AudioRedirect static isolation: PASS')
PY

SRC="$ROOT/src/audio/AudioRedirect.cpp"
CONV="$TMP/audio_seh.cpp"
sed -e 's/__try/if (true)/' -e 's/__except (EXCEPTION_EXECUTE_HANDLER)/else/' \
  "$SRC" > "$CONV"
printf '#define DDDA_AUDIO_PORTABLE_FIXTURE\n#include "%s"\n' "$CONV" \
  > "$TMP/audio_t_gen.cpp"

# -Wno-sign-compare/-Wno-comment: наследие 84.69-84.94 (DWORD vs int в
# ProbeOgg; хвостовой '\\' в комментарии строки 42 — задокументировано в
# ModPaths.h). Остальные предупреждения — Werror.
g++ -std=c++11 -Wall -Wextra -Werror -Wno-sign-compare -Wno-comment \
  -D_M_X64=1 -DAUDIO_RUNTIME_EXPERIMENTAL=1 \
  -I"$ROOT/tools/tcomp" -I"$ROOT/tools/tcomp/shim" -I"$ROOT" -I"$ROOT/src" \
  "$TMP/audio_t_gen.cpp" -c -o "$TMP/audio_t.o"
echo "AudioRedirect syntax check passed."
