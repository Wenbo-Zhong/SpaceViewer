@echo off
setlocal
cd /d "%~dp0"
rem Build a single SpaceViewer.exe with MinGW-w64 g++ on Windows 7 SP1 or newer.
rem Use a MinGW that itself runs on Windows 7 (MSVCRT toolchain, GCC 9 through 13).
where g++ >nul 2>nul
if errorlevel 1 (
  echo g++ was not found. Install MinGW-w64 and add its bin directory to PATH.
  exit /b 1
)
where windres >nul 2>nul
if errorlevel 1 (
  echo windres was not found. It ships with MinGW-w64.
  exit /b 1
)
where cmake >nul 2>nul
if errorlevel 1 (
  echo cmake was not found. Install a CMake version that runs on this system.
  exit /b 1
)
cmake -S . -B build/win7 -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DCMAKE_RC_COMPILER=windres
if errorlevel 1 exit /b 1
cmake --build build/win7 --config Release
if errorlevel 1 exit /b 1
copy /y build\win7\SpaceViewer.exe SpaceViewer.exe >nul
if errorlevel 1 exit /b 1
echo Built SpaceViewer.exe
exit /b 0
