#!/usr/bin/env bash
# Configures, builds and tests the given CMake presets (default: the three
# Linux CI presets) and prints a one-line result per preset.
set -u
presets=("$@")
[ ${#presets[@]} -eq 0 ] && presets=(ci-default ci-clang ci-asan)
status=0
for p in "${presets[@]}"; do
    echo "== $p"
    if ! cmake --preset "$p" > "/tmp/adari-$p-configure.log" 2>&1; then
        tail -20 "/tmp/adari-$p-configure.log"; status=1; continue
    fi
    if ! cmake --build --preset "$p" > "/tmp/adari-$p-build.log" 2>&1; then
        grep -E "error|warning:" "/tmp/adari-$p-build.log" | head -30; status=1; continue
    fi
    if ! ctest --preset "$p" > "/tmp/adari-$p-test.log" 2>&1; then
        grep -vE "^\s*$" "/tmp/adari-$p-test.log" | tail -40; status=1; continue
    fi
    grep -E "tests passed" "/tmp/adari-$p-test.log"
done
exit $status
