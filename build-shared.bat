@echo off
setlocal

set BUILD_DIR=build
set ZIP_NAME=rafx_release.zip
set SLANG_DIR=%BUILD_DIR%/_deps/slang-src/bin
set DXC_DIR=%BUILD_DIR%/_deps/dxc-src/bin/x64
set STAGE=dist

rem build
if not exist "%BUILD_DIR%" (mkdir %BUILD_DIR%)
cd %BUILD_DIR%
cmake .. -GNinja -DCMAKE_BUILD_TYPE=Release -DRAFX_D3D12_SUPPORT=ON -DBUILD_SHARED_LIBS=ON -DRAFX_STATIC_SLANG=ON
cmake --build . --config Release
if %errorlevel% neq 0 (exit /b %errorlevel%)
cd ..

if exist "%STAGE%" rd /s /q "%STAGE%"
mkdir "%STAGE%\include"
mkdir "%STAGE%\bin"

rem copy "%BUILD_DIR%\rafx.dll" "%STAGE%\bin\"
copy "%DXC_DIR%\dxcompiler.dll" "%STAGE%\bin\"
copy "%DXC_DIR%\dxil.dll" "%STAGE%\bin\"
rem copy "%SLANG_DIR%\slang.dll" "%STAGE%\bin\"
rem copy "%SLANG_DIR%\slang-compiler.dll" "%STAGE%\bin\"
copy "%BUILD_DIR%\rafx.lib" "%STAGE%\bin\"
copy "include\rafx.h" "%STAGE%\include\"
