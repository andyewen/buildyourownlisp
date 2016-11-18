#!/bin/bash
cc -std=c11 -Wall parsing.c mpc.c -ledit -lm -o build/parsing
