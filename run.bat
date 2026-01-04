@echo off
if "%~1"=="" (
    echo Usage: %~nx0 ^<example_name^>
    exit /b 1
)
call build
set "EXE=build\examples\%~1.exe"
if not exist "%EXE%" (
    echo Error: "%EXE%" not found.
    exit /b 1
)
cd
"%EXE%"
