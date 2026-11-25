#!/usr/bin/env bash
set -euo pipefail

cxx="${CXX:-c++}"
out="${TMPDIR:-/tmp}/harddrive_sound_test.$$"
trap 'rm -f "$out"' EXIT

"$cxx" -std=c++17 -Wall -Wextra -Werror -pthread \
	-Isrc/include \
	-o "$out" \
	tests/harddrive_sound_test.cpp \
	src/harddrive_sound.cpp \
	src/harddrive_emu.cpp
"$out"
