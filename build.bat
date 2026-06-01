@echo off
setlocal
echo Building HookNt project with CMake...

echo Generating build files...
cmake -S . -B build -A x64

if %errorlevel% neq 0 (
    echo CMake generation failed!
    exit /b 1
)

echo Building project...
cmake --build build --config Release

if %errorlevel% neq 0 (
    echo Build failed!
    exit /b 1
)

echo Build completed successfully!
echo.
echo Executables are in: build\bin\Release\
echo.
