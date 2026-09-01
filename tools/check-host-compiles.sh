#!/usr/bin/env bash
# Syntax and type check for the Windows-only host sources, on any platform.
#
# Compiles host/visual4k-host/src, and the driver diagnostics, against the
# hand-written stubs in
# tools/winstub/. Read tools/winstub/README.md for what this does and does not
# prove -- in short, it catches errors in our code, never a misremembered API.
set -uo pipefail

cd "$(dirname "$0")/.."
mkdir -p build/hostcheck

CXX=${CXX:-g++}
SOURCES=(
    host/visual4k-host/src/TapTable.cpp
    host/visual4k-host/src/CursorDecoder.cpp
    host/visual4k-host/src/Renderer.cpp
    host/visual4k-host/src/Duplicator.cpp
    host/visual4k-host/src/main.cpp
    driver/Visual4kDisplay/Diagnostics.cpp
)

status=0
for src in "${SOURCES[@]}"; do
    printf '%-46s' "$(basename "$src")"
    if out=$("$CXX" -std=c++17 -fsyntax-only -Wall -Wextra \
                 -I tools/winstub -I host/visual4k-host/src \
                 -D_DEBUG=0 "$src" 2>&1); then
        echo "ok"
    else
        echo "FAILED"
        echo "$out" | sed 's/^/    /'
        status=1
    fi
done

if [ $status -eq 0 ]; then
    echo
    echo "All host sources parse and type-check."
    echo "This is not a substitute for a real Windows SDK build."
fi
exit $status
