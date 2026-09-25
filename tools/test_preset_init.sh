#!/bin/sh
# Regression: startup/reload must never overwrite user [customAnchor].
set -eu
cd "$(dirname "$0")/.."
bin=$(mktemp)
trap 'rm -f "$bin"' EXIT
g++ -std=c++11 -Itools/tcomp -Itools/tcomp/shim -I. \
    '-D__try=try' '-D__except(x)=catch(...)' \
    tools/test_preset_init.cpp -o "$bin"
"$bin"
echo 'Preset Init: PASS (user anchors survive startup/reinit; explicit Balanced still writes)'
