#!/bin/sh
# Compila e roda os testes no computador (Linux ou macOS)
cd "$(dirname "$0")"
g++ -std=c++17 -Wall -I../firmware/src -o test_protocolo test_protocolo.cpp \
    ../firmware/src/protocolo.cpp ../firmware/src/led_estado.cpp || exit 2
./test_protocolo
