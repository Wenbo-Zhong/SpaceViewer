@echo off
setlocal
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
if not exist win mkdir win
gcc -c src\platform\win\empty_manifest.c -o win\default-manifest.o
if errorlevel 1 exit /b 1
windres -O coff -i src\platform\win\app.rc -o src\platform\win\app.res.o
if errorlevel 1 exit /b 1
g++ -std=c++17 -O2 -s -B win\ -Isrc -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0601 -DWINVER=0x0601 -DNOMINMAX -mwindows -static -static-libgcc -static-libstdc++ -o SpaceViewer.exe src\main.cpp src\util.cpp src\layout.cpp src\scan.cpp src\ui.cpp src\export.cpp src\selftest.cpp src\motion.cpp src\opencl_fill.cpp src\platform\win\window.cpp src\platform\win\app.res.o -luser32 -lgdi32 -lshell32 -lole32 -luuid -loleaut32 -lcomdlg32 -lwinpthread
set ERR=%ERRORLEVEL%
del /q src\platform\win\app.res.o >nul 2>nul
if not %ERR%==0 exit /b %ERR%
echo Built SpaceViewer.exe
exit /b 0
