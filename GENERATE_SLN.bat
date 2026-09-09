@echo off

REM Set command
set CMAKE_CMD=cmake -G "Visual Studio 18 2026" -A x64 -B ./build .

cmake --preset vcpkg

REM Run command
echo %CMAKE_CMD%
%CMAKE_CMD%

pause