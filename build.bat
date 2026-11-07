@echo off
REM ===========================================================================
REM  CopyMath - one-click build (CMake + MinGW-w64).
REM
REM  Requirements for building from source:
REM    * CMake  (on PATH, or set CMAKE=<path\to\cmake.exe>)
REM    * MinGW-w64 (g++.exe / mingw32-make.exe on PATH)
REM  A git-ignored ".build.local.bat" next to this script may pre-set CMAKE /
REM  CXX for convenience; it is never part of the release.
REM
REM  Keep this file CRLF + ASCII only.
REM ===========================================================================
setlocal enabledelayedexpansion
cd /d "%~dp0"

if exist "%~dp0.build.local.bat" call "%~dp0.build.local.bat"

if not defined CMAKE (
  where cmake >nul 2>nul && set "CMAKE=cmake"
)
if not defined CMAKE (
  echo [ERROR] CMake was not found.
  echo   Install CMake (https://cmake.org/download) and add it to PATH,
  echo   or set the CMAKE environment variable to its executable.
  exit /b 1
)

where g++.exe >nul 2>nul
if errorlevel 1 (
  echo [ERROR] g++.exe was not found in PATH.
  echo   Install MinGW-w64 and add its bin folder to PATH, then re-run.
  exit /b 1
)

echo [1/2] configure ...
"%CMAKE%" -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 goto :fail

echo [2/2] build ...
"%CMAKE%" --build build --parallel
if errorlevel 1 goto :fail

echo.
echo Build OK: bin\CopyMath.exe
echo Tests   : ctest --test-dir build --output-on-failure
exit /b 0

:fail
echo.
echo Build FAILED - see the errors above.
exit /b 1
