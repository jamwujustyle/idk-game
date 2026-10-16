#!/usr/bin/env bash
set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
compiler="${CXX:-}"

if [[ -z "$compiler" ]]; then
    for candidate in g++ clang++ "$project_root/.tools/bin/g++" "$project_root/.tools/bin/clang++"; do
        if command -v "$candidate" >/dev/null 2>&1; then
            compiler="$candidate"
            break
        fi
    done
fi

if [[ -z "$compiler" && -d "$project_root/.tools" ]]; then
    while IFS= read -r candidate; do
        if [[ -x "$candidate" ]]; then
            compiler="$candidate"
            break
        fi
    done < <(find "$project_root/.tools" \( -name g++ -o -name clang++ -o -name 'g++-[0-9]*' \) -type f | sort)
fi

if [[ -z "$compiler" ]]; then
    printf '%s\n' 'No C++17 compiler found. Install g++ or clang++, or set CXX to the compiler executable.' >&2
    exit 2
fi

test_build="$(mktemp -d "${TMPDIR:-/tmp}/ronin-core-test.XXXXXX")"
trap 'rm -rf "$test_build"' EXIT

"$compiler" -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror \
    -I"$project_root/Source/Ronin/Public" \
    "$project_root/Source/Ronin/Private/CombatCore.cpp" \
    "$project_root/Tests/combat_core_test.cpp" \
    -o "$test_build/combat-core-tests"
"$test_build/combat-core-tests"
