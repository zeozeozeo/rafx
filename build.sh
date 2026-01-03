#!/bin/bash

if [ ! -d "build" ]; then
    mkdir build
    cd build
    cmake .. -GNinja -DCMAKE_BUILD_TYPE=Debug -DRAFX_BUILD_EXAMPLES=ON -DRAFX_USE_WAYLAND=ON
    cd ..
fi

cmake --build build --config Debug

if [ $? -ne 0 ]; then
    exit $?
fi
