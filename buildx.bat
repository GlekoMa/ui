@echo off
setlocal enabledelayedexpansion
set start=%time%

:: Check if build dir exist
set BuildDir=build
if not exist %BuildDir% (
    echo Error: No build directory exist. Need to run build_main.bat first.
    exit /b 1
)

:: Determine release/debug mode
if "%1"=="-r" (
    set IsRelease=1
    echo "Build in release mode"
) else (
    set IsRelease=0
)

:: --- Set compiler flags  ---
set CommonCompilerFlags=/nologo /W3 /WX /MP
set CompilerDebugFlags=/Od /Zi /RTC1
set CompilerReleaseFlags= /O2 /DNDEBUG
if %IsRelease%==1 (
    set CompilerFlags=%CommonCompilerFlags% %CompilerReleaseFlags%
) else (
    set CompilerFlags=%CommonCompilerFlags% %CompilerDebugFlags%
)

:: --- Set base compile command ---
set SourceFiles=
for %%f in (*.c) do (
    if not "%%f"=="main.c" (
        set SourceFiles=!SourceFiles! %%f
    )
)
set OutputDll=%BuildDir%\process.dll
set BaseCompileCommand=cl %CompilerFlags% %SourceFiles% /LD /Fe:%OutputDll% /Fo:%BuildDir%\ /Fd:%BuildDir%\ /D_CRT_SECURE_NO_WARNINGS

:: --- Generate compile_commands.json (for clangd analysis) ---
set "CompileCommands=%CD%\%BuildDir%\compile_commands.json"
echo [ > %CompileCommands%

set first=1
for %%f in (*.c) do (
    if !first!==1 (
        set first=0
    ) else (
        echo , >> %CompileCommands%
    )
    echo   { >> %CompileCommands%
    echo     "directory": "%CD:\=\\%", >> %CompileCommands%
    echo     "command": "%BaseCompileCommand:\=\\%", >> %CompileCommands%
    echo     "file": "%CD:\=\\%\\%%f" >> %CompileCommands%
    echo   } >> %CompileCommands%
)

echo ] >> %CompileCommands%

:: --- Set linker flags ---
set LinkerDebugFlags=
set LinkerReleaseFlags=/incremental:no /opt:icf /opt:ref
if %IsRelease%==1 (
    set LinkerFlags=%LinkerReleaseFlags%
) else (
    set LinkerFlags=%LinkerDebugFlags%
)

:: --- Compile ---
if exist "shader.hlsl" (
    fxc.exe /nologo /T vs_5_0 /E vs /O3 /WX /Zpc /Ges /Fh d3d11_vshader.h /Vn d3d11_vshader /Qstrip_reflect /Qstrip_debug /Qstrip_priv shader.hlsl
    fxc.exe /nologo /T ps_5_0 /E ps /O3 /WX /Zpc /Ges /Fh d3d11_pshader.h /Vn d3d11_pshader /Qstrip_reflect /Qstrip_debug /Qstrip_priv shader.hlsl
)
%BaseCompileCommand% /link %LinkerFlags%

:: --- Check success or failure ---
if exist "%OutputDll%" (
    echo Info: Process DLL built successfully
    Copy /Y %OutputDll% process.dll
) else (
    echo Error: Process DLL build failed
    exit /b 1
)

:: --- Calculate time taken ---
set end=%time%
set options="tokens=1-4 delims=:.,"
for /f %options% %%a in ("%start%") do set /a start_m=100%%b%%100&set /a start_s=100%%c%%100&set /a start_ms=100%%d%%100
for /f %options% %%a in ("%end%") do set /a end_m=100%%b%%100&set /a end_s=100%%c%%100&set /a end_ms=100%%d%%100
:: Convert all to milliseconds first
set /a start_total_ms=(start_m * 60 * 1000) + (start_s * 1000) + start_ms
set /a end_total_ms=(end_m * 60 * 1000) + (end_s * 1000) + end_ms
:: Calculate difference
set /a diff_ms=end_total_ms-start_total_ms
set /a diff_s=diff_ms/1000
set /a diff_ms_remain=diff_ms%%1000
echo Time taken: %diff_s%.%diff_ms_remain% seconds
