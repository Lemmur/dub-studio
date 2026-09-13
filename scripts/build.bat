@echo off
rem DubStudio: build (configure on first run). Usage: scripts\build.bat [target]
setlocal
if not exist build\CMakeCache.txt call scripts\configure.bat
if errorlevel 1 exit /b 1
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cmake --build build %*
if errorlevel 1 exit /b 1
rem Копируем Qt DLL рядом с DubStudio.exe, чтобы приложение запускалось без установки PATH.
if exist build\app\DubStudio.exe (
  set "PATH=C:\Qt\6.5.3\msvc2019_64\bin;%PATH%"
  windeployqt --no-translations --no-system-d3d-compiler --no-opengl-sw build\app\DubStudio.exe
)
endlocal
