@echo off
setlocal enabledelayedexpansion

:: Create .gitignore
if not exist ".gitignore" (
    echo /build/ >.gitignore
    echo /.cache/ >>.gitignore
    echo source.zip >>.gitignore
)

:: Check if open_source.c exists
if exist "open_source.c" (
    echo Info: `open_source.c` exist. Archive HEAD repo to `source.zip`...
    git archive -o source.zip HEAD
    if errorlevel 1 (
        echo Error: No git repo
        del source.zip
        exit /b 1
    )
)

:: Determine release/debug mode
if "%1"=="-r" (
    set IsRelease=1
    echo "Build in release mode"
) else (
    set IsRelease=0
)

:: Set root directory name as exe name
for /f %%q in ("%~dp0.") do set ProjectName=%%~nxq
:: Replace '-' with '_' in ProjectName
set ProjectName=%ProjectName:-=_%

:: --- Kill previous process ---
tasklist | find "raddbg.exe" >nul && taskkill /F /IM raddbg.exe 2>nul
tasklist | find "%ProjectName%.exe" >nul && taskkill /F /IM %ProjectName%.exe 2>nul

:: --- Choose compiler ---
::where clang-cl >nul 2>&1
::if errorlevel 1 (
::    where cl >nul 2>&1
::    if errorlevel 1 (
::        echo No suitable compiler found. Please install Visual Studio Build Tools or clang-cl.
::        exit /b 1
::    ) else (
::        set "Compiler=cl"
::    )
::) else (
::    set "Compiler=clang-cl"
::)
:: Note: We use cl as default for speed
set Compiler=cl

:: --- Set compiler flags based on chosen compiler ---
if "%Compiler%"=="clang-cl" (
    set CommonCompilerFlags=/nologo /W3 -Wsign-conversion -Wno-unused-variable /WX
) else (
    set CommonCompilerFlags=/nologo /W3 /WX
)

set DebugFlags=/Od /Zi
set ReleaseFlags= /O2 /DNDEBUG

if %IsRelease%==1 (
    set CompilerFlags=%CommonCompilerFlags% %ReleaseFlags%
) else (
    set CompilerFlags=%CommonCompilerFlags% %DebugFlags%
)

:: --- Set and clean build directory (using ping as sleep) ---
set BuildDir=build
if exist %BuildDir% (
    ping 127.0.0.1 -n 1 -w 200 > nul
    rmdir /s /q %BuildDir%
)
mkdir %BuildDir%

:: --- Set source files ---
set SourceFiles=
for %%f in (*.c) do (
    set SourceFiles=!SourceFiles! %%f
)

:: --- Compile resource if exists ---
if exist "%ProjectName%.rc" (
    rc /nologo /fo %BuildDir%\%ProjectName%.res %ProjectName%.rc
    set SourceFiles=%SourceFiles% %BuildDir%\%ProjectName%.res
)

set OutputExe=%BuildDir%\%ProjectName%.exe

:: --- Set base compile command ---
set BaseCompileCommand=%Compiler% %CompilerFlags% %SourceFiles% /Fe:%OutputExe% /Fo:%BuildDir%\ /Fd:%BuildDir%\ /D_CRT_SECURE_NO_WARNINGS

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

:: --- Compile source using chosen compiler ---
if exist "shader.hlsl" (
    fxc.exe /nologo /T vs_5_0 /E vs /O3 /WX /Zpc /Ges /Fh d3d11_vshader.h /Vn d3d11_vshader /Qstrip_reflect /Qstrip_debug /Qstrip_priv shader.hlsl
    fxc.exe /nologo /T ps_5_0 /E ps /O3 /WX /Zpc /Ges /Fh d3d11_pshader.h /Vn d3d11_pshader /Qstrip_reflect /Qstrip_debug /Qstrip_priv shader.hlsl
)

if exist "%ProjectName%.manifest" (
    %BaseCompileCommand% /link /MANIFEST:EMBED /MANIFESTINPUT:%ProjectName%.manifest
) else (
    %BaseCompileCommand%
)

:: --- Check success or failure ---
if exist "%OutputExe%" (
    echo Info: Build successfully
) else (
    echo Error: Build failed
)
