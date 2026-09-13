@echo off
rem Инициализация окружения сборки RuDub Studio (Windows 11 x64).
rem Вызывать перед любыми ручными командами: call scripts\env.cmd

rem MSVC (VS "18" Build Tools): cl, link + bundled CMake/Ninja в PATH
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" amd64
if errorlevel 1 exit /b 1

rem Qt 6.5.3 LTS (bin — для запуска exe из консоли)
set "CMAKE_PREFIX_PATH=D:\Qt\6.5.3\msvc2019_64"
set "PATH=D:\Qt\6.5.3\msvc2019_64\bin;%PATH%"

rem vcpkg (пакеты ставятся по мере фаз: D:\dev\vcpkg\vcpkg install <pkg>:x64-windows)
set "VCPKG_ROOT=D:\dev\vcpkg"
