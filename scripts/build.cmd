@echo off
rem Полная сборка RuDub Studio: configure (CMakePresets, Ninja) + build + smoke-тест среды.
rem Использование: scripts\build.cmd [debug^|release]  (по умолчанию release)
setlocal

set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=release"

call "%~dp0env.cmd" || exit /b 1

echo [1/3] Конфигурация CMake (preset: %CONFIG%)...
cmake --preset %CONFIG% || exit /b 1

echo [2/3] Сборка Ninja...
cmake --build --preset %CONFIG% || exit /b 1

echo [3/3] Проверка среды (rudub_env_check)...
build\%CONFIG%\app\rudub_env_check.exe || exit /b 1

echo.
echo [OK] Сборка успешна: build\%CONFIG%\app\rudub.exe
endlocal
