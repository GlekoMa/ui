@echo off
setlocal enabledelayedexpansion

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
set CommonCompilerFlags=/nologo /W3 /WX
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
    if not exist "%BuildDir%\_process.dll" (
        Copy /Y %OutputDll% %BuildDir%\_process.dll
    )
) else (
    echo Error: Process DLL build failed
    exit /b 1
)
