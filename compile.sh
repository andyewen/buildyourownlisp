#!/bin/bash
mkdir -p build/
cc -std=c11 -Wall -Werror -g parsing.c mpc.c -ledit -lm -o build/parsing
