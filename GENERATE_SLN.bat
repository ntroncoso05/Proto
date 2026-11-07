@echo off
cmake --preset vcpkg
cmake --build ./build --config Debug
pause