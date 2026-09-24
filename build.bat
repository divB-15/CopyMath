@echo off
REM 用 MinGW 编译 CopyMath（纯 Win32 API + GDI+，无 Qt / 无第三方 GUI 库）
set CC=D:\mingw64\bin\g++.exe
set RC=D:\mingw64\bin\windres.exe
if not exist "%CC%" (
    echo 未找到 MinGW g++，请修改本文件中的 CC 路径
    pause
    exit /b 1
)
if not exist "copymath.ico" (
    echo 未找到 copymath.ico，无法嵌入图标
    pause
    exit /b 1
)

REM 先把图标编译成目标文件（windres），再一起链接
"%RC%" app.rc -O coff -o app_res.o
if errorlevel 1 (
    echo 图标资源编译失败
    pause
    exit /b 1
)

"%CC%" -std=c++17 -O2 -Wall -mwindows -static-libgcc -static-libstdc++ ^
    main.cpp render.cpp config.cpp clipboard.cpp util.cpp app_res.o ^
    -o copymath.exe -lgdiplus -lshell32 -lshlwapi -lole32
if %errorlevel%==0 (
    echo 编译成功：copymath.exe（已嵌入图标）
) else (
    echo 编译失败，请根据上方报错修改源码
)
pause
