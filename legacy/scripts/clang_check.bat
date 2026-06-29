@echo off
setlocal enabledelayedexpansion

REM clang_check.bat — Run Clang-Tidy static analysis on all .c files.
REM Usage: clang_check.bat <project_path> [target_path]

if "%~1"=="" (
    echo Usage: clang_check.bat ^<project_path^> [target_path]
    exit /b 1
)

set "PROJECT_ROOT=%~1"
if "%~2"=="" (
    set "TARGET_PATH=%~1"
) else (
    set "TARGET_PATH=%~2"
)

set "BUILD_DIR=%PROJECT_ROOT%\build"
if not exist "%BUILD_DIR%\compile_commands.json" (
    echo ERROR: Build directory not found or compile_commands.json missing. Run 'idf.py build' first.
    exit /b 1
)

echo ========================================
echo Clang-Tidy: EcoTiter Firmware
echo Project:    %PROJECT_ROOT%
echo Checking:   %TARGET_PATH%
echo ========================================
echo.

set "CLANG_TIDY=C:\Espressif\tools\esp-clang\esp-20.1.1_20250829\esp-clang\bin\clang-tidy.exe"
set "OUTPUT_FILE=%PROJECT_ROOT%\clang-tidy-output.txt"
if exist "%OUTPUT_FILE%" del "%OUTPUT_FILE%"

set /a count=0

REM Iterate over all .c and .h files, excluding 'managed_components' and 'build' directories
for /f "delims=" %%f in ('dir /s /b /a:-d "%TARGET_PATH%\*.*" ^| findstr /v /i "managed_components build"') do (
    set "ext=%%~xf"
    if "!ext!"==".c" (
        set /a count+=1
        echo [!count!] %%~nxf
        "!CLANG_TIDY!" -p "%BUILD_DIR%" "%%f" >> "%OUTPUT_FILE%" 2>&1
    )
)

echo.
echo ========================================
echo Done! Checked !count! files.
echo Report saved to: %OUTPUT_FILE%
echo ========================================
echo.

REM Count warnings and errors
for /f %%a in ('findstr /i /c:"warning:" "%OUTPUT_FILE%" ^| find /c /v ""') do set warnings=%%a
for /f %%a in ('findstr /i /c:"error:" "%OUTPUT_FILE%" ^| find /c /v ""') do set errors=%%a

echo Warnings: !warnings!
echo Errors:   !errors!
echo.

if !errors! gtr 0 (
    echo --- ERRORS ---
    findstr /i "error:" "%OUTPUT_FILE%"
    echo.
)
if !warnings! gtr 0 (
    echo --- WARNINGS ---
    findstr /i "warning:" "%OUTPUT_FILE%"
    echo.
)

pause
