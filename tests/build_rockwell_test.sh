#!/bin/sh
set -eu

ca65 --cpu 6502 tests/rockwell_test.s -o build/rockwell_test.o
ld65 -C tests/wireless_test.cfg -o build/rockwell_test.bin \
    build/rockwell_test.o
