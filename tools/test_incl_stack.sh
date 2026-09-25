#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
bin=$(mktemp)
log=$(mktemp)
trap 'rm -f "$bin" "$log"' EXIT
g++ -std=c++11 -ffunction-sections -fdata-sections -Itools/tcomp \
  -Itools/tcomp/shim -I. -Isrc '-D__try=try' '-D__except(x)=catch(...)' \
  tools/test_incl_stack.cpp src/pawnai/PawnPersona.cpp -Wl,--gc-sections -o "$bin"
"$bin" "$log"
echo 'InclStack discovery/change: PASS'
