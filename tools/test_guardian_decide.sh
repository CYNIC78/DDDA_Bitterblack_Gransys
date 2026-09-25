#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
bin=$(mktemp)
trap 'rm -f "$bin"' EXIT
g++ -std=c++11 -ffunction-sections -fdata-sections -Itools/tcomp -Itools/tcomp/shim -I. -Isrc '-D__try=try' '-D__except(x)=catch(...)' tools/test_guardian_decide.cpp -Wl,--gc-sections -o "$bin"
"$bin"
