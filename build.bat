@echo off
chcp 65001 >nul
setlocal
cd /d "%~dp0"

where cmake >nul 2>nul
if errorlevel 1 (
    echo 未找到 CMake。请先安装 Visual Studio 或 CMake。
    echo 也可以直接用下方 cl 命令编译。
    goto :cl
)

if not exist build mkdir build
cmake -S . -B build -G "Ninja" 2>nul
if errorlevel 1 cmake -S . -B build
if errorlevel 1 goto :cl
cmake --build build --config Release
if errorlevel 1 goto :cl

if exist build\Release\DesktopLyrics.exe (
    copy /Y build\Release\DesktopLyrics.exe . >nul
    copy /Y sample.lrc . >nul
    echo.
    echo 编译成功: DesktopLyrics.exe
    goto :eof
)
if exist build\DesktopLyrics.exe (
    copy /Y build\DesktopLyrics.exe . >nul
    echo.
    echo 编译成功: DesktopLyrics.exe
    goto :eof
)

:cl
echo.
echo 尝试使用 MSVC cl 编译...
where cl >nul 2>nul
if errorlevel 1 (
    echo 未找到 cl.exe。请在 "x64 Native Tools Command Prompt for VS" 中运行本脚本。
    exit /b 1
)

cl /nologo /EHsc /utf-8 /std:c++20 /DUNICODE /D_UNICODE /DNOMINMAX /O2 ^
    /Fe:DesktopLyrics.exe src\DesktopLyrics.cpp ^
    /link gdiplus.lib dwmapi.lib comctl32.lib ole32.lib shell32.lib comdlg32.lib user32.lib gdi32.lib wininet.lib crypt32.lib WindowsApp.lib runtimeobject.lib /SUBSYSTEM:WINDOWS
if errorlevel 1 (
    echo 编译失败。
    exit /b 1
)
echo.
echo 编译成功: DesktopLyrics.exe
endlocal
