#!/bin/sh
# fb_term profiling: leaks (memcheck) + heap-over-time (massif).
# Run interactively - drive the emulator (open fish, run a few commands,
# Ctrl-D to exit cleanly so valgrind prints final report).
#
# Usage:
#   ./profile.sh leak     - memcheck pass, output vg-mem.log
#   ./profile.sh massif   - massif pass, output massif.out + ms_print
#   ./profile.sh both     - run both sequentially
#   ./profile.sh quick    - /usr/bin/time -v (peak RSS), no valgrind slowdown

set -e

cd "$(dirname "$0")"

build() {
    mkdir -p out
    echo ">> rebuild with -g -O0 for symbol info..."
    gcc -g -O0 -o out/fb_term fb_term.c -lm -lutil -Wall
}

leak() {
    build
    echo ">> memcheck. drive the emulator then Ctrl-D in fish to exit."
    valgrind \
        --leak-check=full \
        --show-leak-kinds=all \
        --track-origins=yes \
        --error-exitcode=0 \
        --log-file=vg-mem.log \
        ./out/fb_term --term
    echo ">> wrote vg-mem.log"
    echo "--- LEAK SUMMARY ---"
    grep -A8 "LEAK SUMMARY" vg-mem.log || tail -40 vg-mem.log
}

massif() {
    build
    echo ">> massif (--pages-as-heap=yes captures BSS/static incl 1.6MB cells[][])."
    echo ">> drive the emulator then Ctrl-D to exit."
    valgrind \
        --tool=massif \
        --pages-as-heap=yes \
        --massif-out-file=massif.out \
        ./out/fb_term --term
    echo ">> wrote massif.out"
    if command -v ms_print >/dev/null; then
        ms_print massif.out | head -60
        echo "..."
        echo "(full report: ms_print massif.out | less)"
    else
        echo ">> install valgrind-tools / ms_print not in PATH"
    fi
}

quick() {
    build
    echo ">> /usr/bin/time -v. drive then Ctrl-D."
    /usr/bin/time -v ./out/fb_term --term
}

case "${1:-both}" in
    leak)   leak ;;
    massif) massif ;;
    quick)  quick ;;
    both)   leak; massif ;;
    *)      echo "usage: $0 {leak|massif|quick|both}"; exit 1 ;;
esac
