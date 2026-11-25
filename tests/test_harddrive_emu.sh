#!/usr/bin/env bash
set -euo pipefail

cxx="${CXX:-c++}"
out="${TMPDIR:-/tmp}/harddrive_emu_test.$$"
trap 'rm -f "$out"' EXIT

"$cxx" -std=c++17 -Wall -Wextra -Werror -pthread \
	-Isrc/include \
	-o "$out" \
	tests/harddrive_emu_test.cpp \
	src/harddrive_emu.cpp
"$out"
