@echo off
rem DubStudio: run Catch2 tests via ctest. Usage: scripts\test.bat
setlocal
if not exist build\CMakeCache.txt (
  echo No build dir. Run scripts\build.bat first.
  exit /b 1
)
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
ctest --test-dir build --output-on-failure
endlocal
