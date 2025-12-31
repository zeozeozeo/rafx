@echo off
if not exist "build/" (
    mkdir build
    cd build
    cmake .. -GNinja -DCMAKE_BUILD_TYPE=Debug -DRAFX_BUILD_EXAMPLES=ON
    cd ..
)

cmake --build build --config Debug
if %errorlevel% neq 0 (
    exit /b %errorlevel%
)
