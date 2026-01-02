#!/bin/bash

if [ -z "$1" ]; then
    echo "Usage: $0 <example_name>"
    exit 1
fi

./build.sh

EXE="build/examples/$1"

if [ ! -f "$EXE" ]; then
    echo "Error: \"$EXE\" not found."
    exit 1
fi

./"$EXE"
