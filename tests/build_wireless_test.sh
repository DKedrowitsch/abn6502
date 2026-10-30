#!/bin/sh
set -eu

ca65 --cpu 65c02 tests/wireless_test.s -o build/wireless_test.o
ld65 -C tests/wireless_test.cfg -o build/wireless_test.bin \
    build/wireless_test.o
