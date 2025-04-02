@echo off
setlocal enabledelayedexpansion

:: Determine release/debug mode
if "%1"=="-r" (
    set IsRelease=1
    echo "Build in release mode"
) else (
    set IsRelease=0
)

:: Set root directory name as exe name
for /f %%q in ("%~dp0.") do set ExeName=%%~nxq

:: Clean
tasklist | find "%ExeName%.exe" >nul && taskkill /F /IM %ExeName%.exe 2>nul

set BuildDir=build
if exist %BuildDir% (
    ping 127.0.0.1 -n 1 -w 200 > nul
    rmdir /s /q %BuildDir%
)
mkdir %BuildDir%

:: --- Set compiler flags  ---
set CommonCompilerFlags=/nologo /W3 /WX
set CompilerDebugFlags=/Od /Zi /RTC1
set CompilerReleaseFlags= /O2 /DNDEBUG
if %IsRelease%==1 (
    set CompilerFlags=%CommonCompilerFlags% %CompilerReleaseFlags%
) else (
    set CompilerFlags=%CommonCompilerFlags% %CompilerDebugFlags%
)

:: --- Set linker flags ---
set LinkerDebugFlags=
set LinkerReleaseFlags=/incremental:no /opt:icf /opt:ref
if %IsRelease%==1 (
    set LinkerFlags=%LinkerReleaseFlags%
) else (
    set LinkerFlags=%LinkerDebugFlags%
)

:: --- Compile ---
set SourceFiles=
for %%f in (*.c) do (
    if not "%%f"=="process.c" (
        set SourceFiles=!SourceFiles! %%f
    )
)
set OutputExe=%BuildDir%\%ExeName%.exe
set BaseCompileCommand=cl %CompilerFlags% %SourceFiles% /Fe:%OutputExe% /Fo:%BuildDir%\ /Fd:%BuildDir%\ /D_CRT_SECURE_NO_WARNINGS

if exist "resource.rc" (
    rc /nologo /fo %BuildDir%\resource.res resource.rc
    set SourceFiles=%SourceFiles% %BuildDir%\resource.res
)
if exist "main.manifest" (
    %BaseCompileCommand% /link %LinkerFlags% /MANIFEST:EMBED /MANIFESTINPUT:main.manifest
) else (
    %BaseCompileCommand% /link %LinkerFlags%
)

:: --- Check success or failure ---
if exist "%OutputExe%" (
    echo Info: Build successfully
) else (
    echo Error: Build failed
)

call buildx.bat
