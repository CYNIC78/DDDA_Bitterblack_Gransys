#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
bin=$(mktemp)
trap 'rm -f "$bin"' EXIT
g++ -std=c++11 -Wall -Wextra -pedantic tools/test_nexus_policy.cpp -o "$bin"
"$bin"
echo 'Nexus policy: PASS'
