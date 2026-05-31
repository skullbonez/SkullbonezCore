# Agentic Friendliness â€” Implementation Plan

> **Purpose:** Step-by-step checklist for improving agentic usability of this repository. Implements tiered validation scripts, a universal `AGENTS.md` agent contract, and fixes stale documentation. Designed so even a simple model can follow along without ambiguity.
>
> **Check off tasks** by changing `[ ]` to `[x]` as you complete them.

> **Implementation note (2026-05-30):** Completed through Phase 6 verification. The generated batch files use ASCII banners for reliable `cmd.exe` parsing, and `validate_perf.bat` forces UTF-8 for Python output so `perf_compare.py` can print its status markers on Windows. Phase 7 remains unchecked because committing requires explicit user permission.

---

## Prerequisites

Before starting, verify these tools are available:

```pwsh
# Run these checks â€” all must succeed
Test-Path "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"  # â†’ True
py --version                                                                       # â†’ Python 3.x
Test-Path "G:\skore3\SKULLBONEZ_CORE.sln"                                         # â†’ True
```

**Working directory for all tasks:** `G:\skore3`

---

## Phase 1: Create `tools\` Directory and Helper Scripts

### Task 1.1 â€” Create the `tools\` directory

- [x] Create folder: `G:\skore3\tools\`

```pwsh
New-Item -ItemType Directory -Path "G:\skore3\tools" -Force
```

---

### Task 1.2 â€” Create `tools\find_msbuild.bat`

A shared helper that locates MSBuild. Other scripts call this.

- [x] Create file: `G:\skore3\tools\find_msbuild.bat`

**Full file contents:**

```bat
@echo off
REM â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
REM  find_msbuild.bat â€” Locates MSBuild via vswhere and sets MSBUILD_EXE
REM  Called by other validate_*.bat scripts. Do not run directly.
REM â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
for /f "usebackq tokens=*" %%i in (`"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do (
    set "MSBUILD_EXE=%%i"
    goto :found
)
echo ERROR: MSBuild not found. Install Visual Studio with C++ workload.
exit /b 99

:found
```

---

### Task 1.3 â€” Create `tools\validate_format.bat`

Checks clang-format compliance. Exits 0 if all files pass, 1 if any need formatting.

- [x] Create file: `G:\skore3\tools\validate_format.bat`

**Full file contents:**

```bat
@echo off
setlocal enabledelayedexpansion
REM â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
REM  validate_format.bat â€” Check all C++ files are correctly formatted
REM  Exit 0 = pass, Exit 1 = formatting violations found
REM â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•

set "REPO=%~dp0.."
set "CLANG_FMT=C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Tools\Llvm\x64\bin\clang-format.exe"
set BAD_COUNT=0

if not exist "%CLANG_FMT%" (
    echo ERROR: clang-format not found at expected path.
    echo        Expected: %CLANG_FMT%
    echo        Install VS2022 with C++ LLVM tools.
    exit /b 99
)

echo Checking formatting...

for %%f in ("%REPO%\SkullbonezSource\*.cpp" "%REPO%\SkullbonezSource\*.h") do (
    "%CLANG_FMT%" --dry-run -Werror "%%f" >nul 2>&1
    if errorlevel 1 (
        echo   FAIL: %%~nxf
        set /a BAD_COUNT+=1
    )
)

if %BAD_COUNT% GTR 0 (
    echo FAIL: %BAD_COUNT% file(s) need formatting.
    echo       Run: tools\format_fix.bat
    exit /b 1
)

echo PASS: All source files correctly formatted.
exit /b 0
```

---

### Task 1.4 â€” Create `tools\format_fix.bat`

Auto-fixes formatting violations in-place.

- [x] Create file: `G:\skore3\tools\format_fix.bat`

**Full file contents:**

```bat
@echo off
setlocal
REM â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
REM  format_fix.bat â€” Auto-format all C++ source files in-place
REM â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•

set "REPO=%~dp0.."
set "CLANG_FMT=C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Tools\Llvm\x64\bin\clang-format.exe"

if not exist "%CLANG_FMT%" (
    echo ERROR: clang-format not found. Install VS2022 with C++ LLVM tools.
    exit /b 99
)

REM Run the parameter collapse script first (matches pipeline Step 1)
py "%REPO%\Agentic\Skills\collapse_params.py"

set COUNT=0
for %%f in ("%REPO%\SkullbonezSource\*.cpp" "%REPO%\SkullbonezSource\*.h") do (
    "%CLANG_FMT%" -i "%%f"
    set /a COUNT+=1
)

echo Formatted %COUNT% files.
exit /b 0
```

---

### Task 1.5 â€” Create `tools\validate_build.bat`

Builds a specified configuration. Exits 0 on success (0 errors, 0 warnings), 1 on failure.

- [x] Create file: `G:\skore3\tools\validate_build.bat`

**Full file contents:**

```bat
@echo off
setlocal enabledelayedexpansion
REM â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
REM  validate_build.bat â€” Build SkullbonezCore solution
REM  Usage: validate_build.bat [Configuration]
REM    Configuration = Debug | Release | Profile (default: Profile)
REM  Exit 0 = build succeeded (0 errors, 0 warnings)
REM  Exit 1 = build failed
REM â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•

set "REPO=%~dp0.."
set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Profile"

call "%~dp0find_msbuild.bat"
if errorlevel 1 exit /b 99

echo Building %CONFIG%^|x64...
"%MSBUILD_EXE%" "%REPO%\SKULLBONEZ_CORE.sln" /p:Configuration=%CONFIG% /p:Platform=x64 /nologo /v:minimal /warnaserror
if errorlevel 1 (
    echo FAIL: Build %CONFIG% failed.
    exit /b 1
)

echo PASS: Build %CONFIG%^|x64 succeeded.
exit /b 0
```

---

### Task 1.6 â€” Create `tools\check_dx12_validation.bat`

Checks that `dx12_validation.txt` exists and reports 0 errors.

- [x] Create file: `G:\skore3\tools\check_dx12_validation.bat`

**Full file contents:**

```bat
@echo off
setlocal
REM â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
REM  check_dx12_validation.bat â€” Verify DX12 InfoQueue clean
REM  Exit 0 = no validation errors, Exit 1 = errors present or file missing
REM â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•

set "REPO=%~dp0.."
set "VAL_FILE=%REPO%\dx12_validation.txt"

if not exist "%VAL_FILE%" (
    echo FAIL: dx12_validation.txt not found.
    echo       DX12 suite may not have run or crashed before writing validation output.
    exit /b 1
)

REM Read the last line â€” it should be "0" (error count)
for /f "usebackq delims=" %%a in ("%VAL_FILE%") do set "LAST_LINE=%%a"

if "%LAST_LINE%"=="0" (
    echo PASS: DX12 InfoQueue reported 0 validation errors.
    exit /b 0
) else (
    echo FAIL: DX12 InfoQueue reported %LAST_LINE% validation error(s):
    type "%VAL_FILE%"
    exit /b 1
)
```

---

### Task 1.7 â€” Create `tools\validate_fast.bat`

**The quick sanity check.** Format + Build only. ~30 seconds.

- [x] Create file: `G:\skore3\tools\validate_fast.bat`

**Full file contents:**

```bat
@echo off
setlocal
REM â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
REM  validate_fast.bat â€” Quick sanity check: format + build
REM  Use for: documentation changes, small refactors, non-rendering edits
REM  Runtime: ~30 seconds
REM  Exit 0 = pass, Non-zero = failure
REM â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•

set "REPO=%~dp0.."
echo.
echo â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
echo   VALIDATE_FAST â€” Format + Build
echo â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
echo.

REM â”€â”€ Step 1: Format Check â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
echo [1/2] Checking formatting...
call "%~dp0validate_format.bat"
if errorlevel 1 (
    echo.
    echo To auto-fix: tools\format_fix.bat
    exit /b 1
)

REM â”€â”€ Step 2: Build Profile x64 â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
echo [2/2] Building Profile x64...
call "%~dp0validate_build.bat" Profile
if errorlevel 1 exit /b 2

echo.
echo â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
echo   VALIDATE_FAST: ALL PASSED
echo â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
exit /b 0
```

---

### Task 1.8 â€” Create `tools\validate_renderers.bat`

**Tri-renderer validation.** Build + run GL/DX11/DX12 suites + stdout/stderr check + DX12 validation check + cross-renderer parity. ~90 seconds.

- [x] Create file: `G:\skore3\tools\validate_renderers.bat`

**Full file contents:**

```bat
@echo off
setlocal enabledelayedexpansion
REM â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
REM  validate_renderers.bat â€” Full tri-renderer visual validation
REM  Use for: shader changes, render backend changes, texture changes
REM  Runtime: ~90 seconds
REM  Steps: format, build, clean, GL suite, DX11 suite, DX12 suite,
REM         stdout/stderr check, DX12 InfoQueue, cross-renderer parity
REM  Exit 0 = all renderers pass, Non-zero = failure (code = step)
REM â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•

set "REPO=%~dp0.."
pushd "%REPO%"
echo.
echo â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
echo   VALIDATE_RENDERERS â€” Tri-Renderer Suite
echo â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
echo.

REM â”€â”€ Step 1: Format Check â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
echo [1/7] Checking formatting...
call "%~dp0validate_format.bat"
if errorlevel 1 exit /b 1

REM â”€â”€ Step 2: Build Profile â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
echo [2/7] Building Profile x64...
call "%~dp0validate_build.bat" Profile
if errorlevel 1 exit /b 2

REM â”€â”€ Step 3: Clean old artifacts â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
echo [3/7] Cleaning old artifacts...
del /q "%REPO%\Profile\*screenshot.bmp" 2>nul
del /q "%REPO%\Profile\*legacy_smoke.bmp" 2>nul
del /q "%REPO%\Profile\*_stdout.txt" 2>nul
del /q "%REPO%\Profile\*_stderr.txt" 2>nul
del /q "%REPO%\dx12_validation.txt" 2>nul

REM â”€â”€ Step 4: Run GL Suite â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
echo [4/7] Running GL suite...
"%REPO%\Profile\SKULLBONEZ_CORE.exe" --vsync off --suite SkullbonezData/scenes/render_tests.suite >"%REPO%\Profile\gl_stdout.txt" 2>"%REPO%\Profile\gl_stderr.txt"
if errorlevel 1 (
    echo FAIL: GL suite exited with error.
    exit /b 3
)
REM Rename GL artifacts
if exist "%REPO%\Profile\screenshot.bmp"   rename "%REPO%\Profile\screenshot.bmp" gl_screenshot.bmp
if exist "%REPO%\Profile\legacy_smoke.bmp"  rename "%REPO%\Profile\legacy_smoke.bmp" gl_legacy_smoke.bmp

REM â”€â”€ Step 5: Run DX11 Suite â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
echo [5/7] Running DX11 suite...
"%REPO%\Profile\SKULLBONEZ_CORE.exe" --renderer dx11 --vsync off --suite SkullbonezData/scenes/render_tests.suite >"%REPO%\Profile\dx11_stdout.txt" 2>"%REPO%\Profile\dx11_stderr.txt"
if errorlevel 1 (
    echo FAIL: DX11 suite exited with error.
    exit /b 4
)
if exist "%REPO%\Profile\screenshot.bmp"   rename "%REPO%\Profile\screenshot.bmp" dx11_screenshot.bmp
if exist "%REPO%\Profile\legacy_smoke.bmp"  rename "%REPO%\Profile\legacy_smoke.bmp" dx11_legacy_smoke.bmp

REM â”€â”€ Step 6: Run DX12 Suite â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
echo [6/7] Running DX12 suite...
"%REPO%\Profile\SKULLBONEZ_CORE.exe" --renderer dx12 --vsync off --suite SkullbonezData/scenes/render_tests.suite >"%REPO%\Profile\dx12_stdout.txt" 2>"%REPO%\Profile\dx12_stderr.txt"
if errorlevel 1 (
    echo FAIL: DX12 suite exited with error.
    exit /b 5
)
if exist "%REPO%\Profile\screenshot.bmp"   rename "%REPO%\Profile\screenshot.bmp" dx12_screenshot.bmp
if exist "%REPO%\Profile\legacy_smoke.bmp"  rename "%REPO%\Profile\legacy_smoke.bmp" dx12_legacy_smoke.bmp

REM â”€â”€ Verify all artifacts produced â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
set MISSING=0
for %%f in (gl_screenshot.bmp gl_legacy_smoke.bmp dx11_screenshot.bmp dx11_legacy_smoke.bmp dx12_screenshot.bmp dx12_legacy_smoke.bmp) do (
    if not exist "%REPO%\Profile\%%f" (
        echo   MISSING: %%f
        set /a MISSING+=1
    )
)
if %MISSING% GTR 0 (
    echo FAIL: %MISSING% expected artifact(s) missing.
    exit /b 6
)

REM â”€â”€ Stdout/Stderr Error Check (matches pipeline Step 3.5) â”€â”€â”€â”€â”€
echo [7/7] Checking stdout/stderr for errors...
set "STDOUT_CLEAN=1"
for %%r in (gl dx11 dx12) do (
    findstr /I /C:"error" /C:"warning" /C:"failed" "%REPO%\Profile\%%r_stdout.txt" >nul 2>&1
    if not errorlevel 1 (
        echo   FAIL [%%r]: Unexpected error/warning in stdout:
        findstr /I /C:"error" /C:"warning" /C:"failed" "%REPO%\Profile\%%r_stdout.txt"
        set "STDOUT_CLEAN=0"
    )
    findstr /I /C:"error" /C:"warning" /C:"failed" "%REPO%\Profile\%%r_stderr.txt" >nul 2>&1
    if not errorlevel 1 (
        echo   FAIL [%%r]: Unexpected error/warning in stderr:
        findstr /I /C:"error" /C:"warning" /C:"failed" "%REPO%\Profile\%%r_stderr.txt"
        set "STDOUT_CLEAN=0"
    )
)
if "%STDOUT_CLEAN%"=="0" (
    echo FAIL: One or more renderers produced error/warning output.
    exit /b 7
)

REM â”€â”€ DX12 Validation Check â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
call "%~dp0check_dx12_validation.bat"
if errorlevel 1 exit /b 8

REM â”€â”€ Cross-Renderer Parity (Python) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
echo.
echo Checking cross-renderer parity...
set "SKORE_REPO=%REPO%"
py "%~dp0check_parity.py"
if errorlevel 1 (
    echo FAIL: Cross-renderer parity check failed.
    exit /b 9
)

echo.
echo â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
echo   VALIDATE_RENDERERS: ALL PASSED
echo â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
popd
exit /b 0
```

> **Note:** The parity check calls `tools\check_parity.py` (created in Task 1.12). Stdout/stderr validation matches what the pipeline skill does in Step 3.5.

---

### Task 1.9 â€” Create `tools\validate_physics.bat`

**Physics regression validation.** Builds Debug + runs deterministic scenes + byte-exact CSV diff. ~60 seconds.

- [x] Create file: `G:\skore3\tools\validate_physics.bat`

**Full file contents:**

```bat
@echo off
setlocal enabledelayedexpansion
REM â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
REM  validate_physics.bat â€” Physics determinism regression test
REM  Use for: physics, collision, solver, rigid body changes
REM  Runtime: ~60 seconds
REM  Exit 0 = physics output matches baselines exactly
REM  Exit 1 = build failure, Exit 2 = regression detected
REM â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•

set "REPO=%~dp0.."
pushd "%REPO%"
echo.
echo â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
echo   VALIDATE_PHYSICS â€” Determinism Check
echo â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
echo.

REM â”€â”€ Step 1: Build Debug (required for physics logging) â”€â”€â”€â”€â”€â”€â”€â”€
echo [1/3] Building Debug x64...
call "%~dp0validate_build.bat" Debug
if errorlevel 1 exit /b 1

REM â”€â”€ Step 2: Run regression scenes â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
echo [2/3] Running physics regression scenes...
del /q "%REPO%\Debug\physics_regression_*.csv" 2>nul

echo   Running physics_regression_solver...
"%REPO%\Debug\SKULLBONEZ_CORE.exe" --vsync off --fixed-step --scene SkullbonezData/scenes/physics_regression_solver.scene
if errorlevel 1 (
    echo FAIL: physics_regression_solver crashed or errored.
    exit /b 2
)

REM â”€â”€ Step 3: Compare against baselines â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
echo [3/3] Comparing output against baselines...
set "SKORE_REPO=%REPO%"
py "%~dp0check_physics_regression.py"
if errorlevel 1 (
    echo FAIL: Physics regression detected. Output differs from baselines.
    echo       Baseline: TestOutput\baselines\physics_regression_solver.csv
    echo       Actual:   Debug\physics_regression_solver.csv
    exit /b 2
)

echo.
echo â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
echo   VALIDATE_PHYSICS: ALL PASSED
echo â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
popd
exit /b 0
```

---

### Task 1.10 â€” Create `tools\check_physics_regression.py`

The Python script that does byte-exact CSV comparison for physics regression.

- [x] Create file: `G:\skore3\tools\check_physics_regression.py`

**Full file contents:**

```python
"""
check_physics_regression.py â€” Compare physics CSV output against committed baselines.

Physics scene uses fixed_step + seed 42, so output is EXACTLY deterministic.
Any single differing byte is a real regression.

Exit 0 = all match, Exit 1 = regression detected or files missing.
"""
import os
import sys

REPO = os.environ.get("SKORE_REPO", os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BASELINE_DIR = os.path.join(REPO, "TestOutput", "baselines")

TESTS = [
    (os.path.join(REPO, "Debug", "physics_regression_solver.csv"), "physics_regression_solver.csv"),
]


def main():
    all_pass = True

    for output_path, baseline_name in TESTS:
        baseline_path = os.path.join(BASELINE_DIR, baseline_name)

        if not os.path.exists(output_path):
            print(f"  FAIL: {os.path.basename(output_path)} not produced")
            all_pass = False
            continue

        if not os.path.exists(baseline_path):
            # No baseline yet â€” create it (first run)
            import shutil
            shutil.copy(output_path, baseline_path)
            with open(output_path) as f:
                lines = f.readlines()
            print(f"  BASELINE CREATED: {baseline_name} ({len(lines)} lines)")
            continue

        with open(output_path) as f:
            current = f.readlines()
        with open(baseline_path) as f:
            baseline = f.readlines()

        if current == baseline:
            print(f"  PASS: {baseline_name} ({len(current)} lines, exact match)")
        else:
            all_pass = False
            if len(current) != len(baseline):
                print(f"  FAIL: {baseline_name} row count {len(current)} vs baseline {len(baseline)}")
            else:
                diffs = [
                    (i + 1, b.rstrip(), c.rstrip())
                    for i, (b, c) in enumerate(zip(baseline, current))
                    if b != c
                ]
                print(f"  FAIL: {baseline_name} â€” {len(diffs)} lines differ (first at line {diffs[0][0]}):")
                for lineno, b, c in diffs[:5]:
                    print(f"    line {lineno}:")
                    print(f"      baseline: {b}")
                    print(f"      current:  {c}")

    return 0 if all_pass else 1


if __name__ == "__main__":
    sys.exit(main())
```

---

### Task 1.11 â€” Create `tools\validate_perf.bat`

**Performance regression check.** Builds Profile + runs `perf_test.scene` for GL, DX11, and DX12 with vsync off + fixed-step for frame-controlled 1000-frame passes, generates JSON with 1970 analyzed timing samples after first-pass warmup, and compares each renderer. ~1 minute.

- [x] Create file: `G:\skore3\tools\validate_perf.bat`

**Current implementation summary:**

```bat
call "%~dp0validate_build.bat" Profile
call :RunPerf gl "" 2
call :RunPerf dx11 "--renderer dx11" 3
call :RunPerf dx12 "--renderer dx12" 4
for %%r in (gl dx11 dx12) do (
    py "%REPO%\Agentic\Skills\skore-render-test\analyze_perf.py" --renderer %%r --csv "%REPO%\Profile\%%r_perf_log.csv" --out-dir "%REPO%\Profile"
    py "%REPO%\Agentic\Skills\skore-render-test\perf_compare.py" --current "%REPO%\Profile\%%r_perf.json" --previous "%REPO%\TestOutput\baselines\%%r_perf.json"
)
```

---

### Task 1.12 â€” Create `tools\check_parity.py`

Cross-renderer pixel parity comparison script.

- [x] Create file: `G:\skore3\tools\check_parity.py`

**Full file contents:**

```python
"""
check_parity.py â€” Cross-renderer visual parity check.

Compares GL vs DX11 and GL vs DX12 screenshots. Reports average pixel
difference per pair. Fails if any pair exceeds threshold (avg_diff > 10.0).

Expects screenshots at: {REPO}/Profile/{renderer}_{scene}.bmp
Exit 0 = parity acceptable, Exit 1 = parity violation
"""
import os
import sys

try:
    from PIL import Image
except ImportError:
    print("ERROR: Pillow not installed. Run: py -m pip install Pillow")
    sys.exit(99)

REPO = os.environ.get("SKORE_REPO", os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PROFILE = os.path.join(REPO, "Profile")
THRESHOLD = 10.0  # Average per-pixel-per-channel difference threshold

PAIRS = [
    ("gl_screenshot.bmp", "dx11_screenshot.bmp", "water_ball_test GL vs DX11"),
    ("gl_screenshot.bmp", "dx12_screenshot.bmp", "water_ball_test GL vs DX12"),
    ("gl_legacy_smoke.bmp", "dx11_legacy_smoke.bmp", "legacy_smoke GL vs DX11"),
    ("gl_legacy_smoke.bmp", "dx12_legacy_smoke.bmp", "legacy_smoke GL vs DX12"),
]


def main():
    all_pass = True

    for a_file, b_file, name in PAIRS:
        a_path = os.path.join(PROFILE, a_file)
        b_path = os.path.join(PROFILE, b_file)

        if not os.path.exists(a_path) or not os.path.exists(b_path):
            print(f"  {name}: MISSING ({a_file if not os.path.exists(a_path) else b_file})")
            all_pass = False
            continue

        a_img = Image.open(a_path).convert("RGB")
        b_img = Image.open(b_path).convert("RGB")

        if a_img.size != b_img.size:
            print(f"  {name}: SIZE MISMATCH {a_img.size} vs {b_img.size}")
            all_pass = False
            continue

        pixel_count = a_img.size[0] * a_img.size[1] * 3
        total_diff = sum(abs(pa - pb) for pa, pb in zip(a_img.tobytes(), b_img.tobytes()))
        avg_diff = total_diff / pixel_count

        status = "PASS" if avg_diff <= THRESHOLD else "FAIL"
        if avg_diff > THRESHOLD:
            all_pass = False
        print(f"  {name}: avg_diff={avg_diff:.4f} [{status}]")

    if all_pass:
        print("PASS: Cross-renderer parity acceptable.")
    else:
        print("FAIL: Cross-renderer parity violated.")

    return 0 if all_pass else 1


if __name__ == "__main__":
    sys.exit(main())
```

---

### Task 1.13 â€” Create `tools\validate_full.bat`

**The full validation pipeline.** Combines all other scripts. ~3 minutes.

- [x] Create file: `G:\skore3\tools\validate_full.bat`

**Full file contents:**

```bat
@echo off
setlocal
REM â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
REM  validate_full.bat â€” Complete validation pipeline
REM  Use for: broad changes, uncertain scope, pre-merge verification
REM  Runtime: ~3 minutes
REM  Exit 0 = all pass, Non-zero = failure
REM â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•

set "REPO=%~dp0.."
echo.
echo â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
echo   VALIDATE_FULL â€” Complete Validation Pipeline
echo â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
echo.

REM â”€â”€ Renderers (includes format + build + tri-renderer + parity) â”€â”€
echo === Phase 1: Renderer Validation ===
call "%~dp0validate_renderers.bat"
if errorlevel 1 (
    echo.
    echo VALIDATE_FULL: FAILED at renderer validation.
    exit /b 1
)

REM â”€â”€ Physics (builds Debug + regression CSVs) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
echo.
echo === Phase 2: Physics Validation ===
call "%~dp0validate_physics.bat"
if errorlevel 1 (
    echo.
    echo VALIDATE_FULL: FAILED at physics validation.
    exit /b 2
)

REM â”€â”€ Perf (rebuilds Profile and re-runs perf_test.scene for GL/DX11/DX12.
REM   The rebuild is a no-op since renderers already built Profile, but perf
REM   analysis needs standalone CSVs to compare against baseline JSONs.) â”€â”€
echo.
echo === Phase 3: Performance Validation ===
call "%~dp0validate_perf.bat"
if errorlevel 1 (
    echo.
    echo VALIDATE_FULL: FAILED at performance validation.
    exit /b 3
)

echo.
echo â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
echo   VALIDATE_FULL: ALL PHASES PASSED
echo â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
exit /b 0
```

---

### Task 1.14 â€” Create `tools\agent_validate.bat` (Alias for validate_full)

One-command entry point that any agent can run with no arguments.

- [x] Create file: `G:\skore3\tools\agent_validate.bat`

**Full file contents:**

```bat
@echo off
REM â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
REM  agent_validate.bat â€” THE one command an agent must run.
REM  Delegates to validate_full.bat. Exists so agents can run a
REM  single predictable command without thinking.
REM
REM  Usage:  tools\agent_validate.bat
REM  Exit 0 = all validation passed
REM  Non-zero = failure (see output for details)
REM â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
call "%~dp0validate_full.bat"
exit /b %errorlevel%
```

---

## Phase 2: Create Universal `AGENTS.md`

### Task 2.1 â€” Create `AGENTS.md` at repo root

- [x] Create file: `G:\skore3\AGENTS.md`

**Full file contents:**

```markdown
# Agent Instructions

> Universal contract for any AI agent working on this repository.
> Framework-agnostic: applies to Agent, other agent runtimes, and future tools.

**Do not** submit, force-push, rebase, or rewrite git history.

---

## Before Editing

1. Read this file and `README.md`.
2. Identify your change's impact area(s): GL, DX11, DX12, physics, scene system, tests, documentation.
3. State which validation command you will run (see table below).

## After Editing

Run the appropriate validation script from the `tools\` directory:

| Change Type | Command | Runtime |
|-------------|---------|---------|
| Documentation only | `tools\validate_fast.bat` | ~30s |
| Small refactor (no render/physics) | `tools\validate_fast.bat` | ~30s |
| Shader or render backend | `tools\validate_renderers.bat` | ~60s |
| Physics, collision, or solver | `tools\validate_physics.bat` | ~45s |
| Performance-sensitive hot path | `tools\validate_perf.bat` | ~1 min |
| Broad or uncertain scope | `tools\validate_full.bat` | ~3 min |
| **Don't know? Use this:** | **`tools\agent_validate.bat`** | ~3 min |

### File â†’ Validation Mapping

| Files Changed | Required Script |
|---------------|-----------------|
| `SkullbonezRenderBackend*.cpp/h` | `validate_renderers` |
| `SkullbonezData/shaders/*` | `validate_renderers` |
| `SkullbonezRigidBody*` | `validate_physics` |
| `SkullbonezCollisionResponse*` | `validate_physics` |
| `SkullbonezImpulseSolver*` | `validate_physics` |
| `SkullbonezBoundingSphere*` | `validate_physics` |
| `SkullbonezDynamicsObject*` | `validate_physics` |
| `SkullbonezSpatialGrid*` | `validate_physics` + `validate_perf` |
| `SkullbonezGameModelCollection*` | `validate_renderers` + `validate_perf` |
| `SkullbonezCommon.h` | `validate_full` |
| `SkullbonezRun*` | `validate_full` |
| `SkullbonezWindow*` | `validate_full` |
| `SkullbonezInit*` | `validate_full` |
| Multiple areas or unsure | `validate_full` |
| `Agentic/*`, `*.md`, docs | `validate_fast` |
| `tools/*` | `validate_fast` (then run the changed script) |

---

## Rules

- **Never claim success without command output.** Paste the validation output.
- **Never skip validation** unless the user explicitly says to.
- **Kill processes by PID only** â€” never `taskkill /IM` or `Stop-Process -Name`. Multiple agents may run simultaneously.
- **Zero warnings** at `/W4` â€” no exceptions.
- **Zero DX12 validation errors** â€” no exceptions.
- **All three renderers** must produce visually identical output (avg pixel diff < 10).
- **Physics must be deterministic** â€” byte-exact CSV match against baselines.

---

## Danger Zones

Changes to these areas require extra care. Always run the specified validation:

| Area | Risk | Required Validation |
|------|------|---------------------|
| DX12 resource barriers | GPU hang, corruption, CPU/GPU race | `validate_renderers` + verify `dx12_validation.txt` = 0 |
| Renderer backend parity | Visual divergence GL vs DX11 vs DX12 | `validate_renderers` (cross-renderer pixel diff) |
| Per-frame heap allocations | Performance cliff, GC-style stalls | `validate_perf` + manual review of hot path |
| Visual regression baselines | False passes hide real bugs | `validate_renderers` + intentional baseline update |
| Matrix conventions (row/col major) | Entire scene renders incorrectly | `validate_renderers` (all 3 backends) |
| Physics determinism (solver, contacts) | Butterfly-effect divergence over frames | `validate_physics` (byte-exact CSV diff) |
| Screenshot timing / frame counting | Flaky non-deterministic captures | `validate_renderers` (verify frames before capture) |
| Fixed-step simulation behavior | Physics replay not reproducible | `validate_physics` |
| GL/DX coordinate conventions (Y-flip, UV) | Upside-down textures, clip-space bugs | `validate_renderers` (cross-renderer parity) |
| Upload buffer / frame allocator (DX12) | GPU race: CPU overwrites in-flight data | `validate_renderers` + run 3Ã— consecutive |
| Singleton lifecycle (Window, SkyBox, etc.) | Use-after-destroy, double-init crash | `validate_full` |
| Broadphase spatial grid | Missed collisions, perf regression | `validate_physics` + `validate_perf` |

---

## Build

```bat
REM Quick build (Profile, for validation):
tools\validate_build.bat Profile

REM Debug build (for physics logging / CDB debugging):
tools\validate_build.bat Debug
```

- **Platform:** x64 only â€” do not change
- **Configurations:** Debug, Profile, Release
- **Toolset:** v143 (VS2022)
- **Warning level:** /W4, zero warnings required

---

## Project Structure (Key Paths)

| What | Path |
|------|------|
| Solution file | `SKULLBONEZ_CORE.sln` |
| Source code | `SkullbonezSource/` |
| Shaders (GLSL + HLSL) | `SkullbonezData/shaders/` |
| Test scenes | `SkullbonezData/scenes/` |
| Suite files | `SkullbonezData/scenes/*.suite` |
| Visual baselines | `TestOutput/baselines/*.png` |
| Physics baselines | `TestOutput/baselines/*.csv` |
| Perf baselines | `TestOutput/baselines/*_perf.json` |
| Validation scripts | `tools/` |
| agent-specific docs | `Agentic/` |

---

## For Agent Agents Specifically

If you are an AI agent, also read:
- `AGENTS.md` â€” loaded automatically
- `Agentic/SessionState.md` â€” session handoff state
- `Agentic/Skills/skore-build-pipeline/skill.md` â€” detailed pipeline with perf archiving

These extend this contract with agent-specific tooling (skills, session state, the available user-input mechanism).
```

---

### Task 2.2 â€” Move existing `agents.md` to `Agentic\agent-guide.md` and create universal `AGENTS.md`

The existing `agents.md` at repo root is agent-specific. Move it so it doesn't conflict with the universal `AGENTS.md`.

> **Windows note:** The filesystem is case-insensitive, so `agents.md` and `AGENTS.md` are the same file. You must use a two-step rename through an intermediate path. Git tracks case changes correctly even though Windows doesn't distinguish them.

- [x] Move to agent-specific location, then create the new universal contract:

```pwsh
# Step 1: Move the agent-specific file
git mv "G:\skore3\agents.md" "G:\skore3\Agentic\agent-guide.md"

# Step 2: Create the new universal AGENTS.md (Task 2.1 content)
# Git will track this as a new file at the root since the old agents.md was moved away.
```

> After the `git mv`, the root `agents.md` no longer exists. Create `AGENTS.md` (Task 2.1 content) as a brand new file. Git handles the case correctly in the index.

---

### Task 2.3 â€” Update `AGENTS.md` (reference `AGENTS.md` + fix stale info)

The old tool-specific instruction file had stale references (Win32 platform, old member naming convention) and needed to reference the new universal contract.

- [x] Edit file: `G:\skore3\AGENTS.md`

**Change 1 â€” Add this block at line 1 (before existing content):**

```markdown
> **Universal agent contract:** Read `AGENTS.md` at the repo root first. This file extends it with agent-specific workflows.

```

**Change 2 â€” Build section:** Replace `Win32` references with `x64`:

Find:
```bat
msbuild SKULLBONEZ_CORE.sln /p:Configuration=Debug /p:Platform=Win32
msbuild SKULLBONEZ_CORE.sln /p:Configuration=Release /p:Platform=Win32
```

Replace with:
```bat
msbuild SKULLBONEZ_CORE.sln /p:Configuration=Debug /p:Platform=x64
msbuild SKULLBONEZ_CORE.sln /p:Configuration=Release /p:Platform=x64
```

**Change 3 â€” Target line:**

Find:
```
- Target: **Win32 (x86)** â€” do not change to x64
```

Replace with:
```
- Target: **x64** â€” do not change to Win32
```

**Change 4 â€” Member naming convention** (the `m_` rename phase is complete):

Find:
```
- Members: camelCase with intent prefixes â€” `is` (bools), `f` (floats in ctors), `p` (pointers), `s` (static/struct members), `c` (class-instance members)
```

Replace with:
```
- Members: `m_` prefix, camelCase â€” `m_position`, `m_isGrounded`, `m_pTexture`
```

**Change 5 â€” Output paths** (now x64, output goes to config-named folders):

Find:
```
- Output: `Debug\SKULLBONEZ_CORE.exe` or `Release\SKULLBONEZ_CORE.exe`
```

Replace with:
```
- Output: `Debug\SKULLBONEZ_CORE.exe`, `Release\SKULLBONEZ_CORE.exe`, or `Profile\SKULLBONEZ_CORE.exe`
```

**Change 6 â€” Remove "no automated tests"** (validation scripts now exist):

Find:
```
There are no automated tests.
```

Replace with:
```
Automated validation: run `tools\validate_full.bat` (see `AGENTS.md` for the tiered validation table).
```

**Change 7 - Deprecated: `skore-branch-and-snatch`:**

This old recommendation is no longer current. The branch-and-snatch workflow was removed in favor of committed perf baselines and `perf_compare.py`.

**Change 8 â€” Add `Profile` configuration reference:**

Find:
```
- Toolset: v143 (VS2019+)
```

Replace with:
```
- Configurations: Debug, Profile, Release
- Toolset: v143 (VS2022)
```

---

## Phase 3: Fix `collapse_params.py` Hardcoded Path

The existing `Agentic\Skills\collapse_params.py` has a hardcoded path (`G:\SkullbonezCoreOriginal\SkullbonezSource`). The `format_fix.bat` script calls it, so it needs to work with dynamic paths.

### Task 3.1 â€” Update `collapse_params.py` to use repo-relative path

- [x] Edit file: `G:\skore3\Agentic\Skills\collapse_params.py`

**Find line 3:**
```python
SOURCE_DIR = r"G:\SkullbonezCoreOriginal\SkullbonezSource"
```

**Replace with:**
```python
SOURCE_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), "SkullbonezSource")
```

This resolves to `{repo_root}\SkullbonezSource` regardless of where the repo is cloned.

---

## Phase 4: Add `tools\README.md`

### Task 4.1 â€” Create documentation for the tools directory

- [x] Create file: `G:\skore3\tools\README.md`

**Full file contents:**

```markdown
# Validation Tools

Scripts for validating SkullbonezCore changes. Run from the repo root or from within this directory.

## Quick Reference

| Script | Use When | Runtime |
|--------|----------|---------|
| `agent_validate.bat` | Don't know what to run (runs everything) | ~3 min |
| `validate_fast.bat` | Docs, small refactors, non-render edits | ~30s |
| `validate_renderers.bat` | Shader, texture, render backend changes | ~60s |
| `validate_physics.bat` | Physics, collision, solver, rigid body | ~45s |
| `validate_perf.bat` | Performance-sensitive, hot-path changes | ~1 min |
| `validate_full.bat` | Broad changes, pre-merge, uncertain scope | ~3 min |

## Utility Scripts

| Script | Purpose |
|--------|---------|
| `validate_format.bat` | Check clang-format compliance (no auto-fix) |
| `format_fix.bat` | Auto-fix formatting in-place |
| `validate_build.bat <Config>` | Build a specific configuration (Debug/Profile/Release) |
| `find_msbuild.bat` | Locate MSBuild (called by other scripts) |
| `check_dx12_validation.bat` | Verify DX12 InfoQueue clean |
| `check_parity.py` | Cross-renderer pixel comparison |
| `check_physics_regression.py` | Byte-exact physics CSV diff |

## Exit Codes

All scripts follow this convention:
- `0` = Pass
- `1â€“98` = Failure (code indicates which step failed)
- `99` = Tool not found (MSBuild, clang-format, Python, Pillow)

## Prerequisites

- Visual Studio 2022 with C++ and LLVM tools
- Python 3.x with Pillow (`py -m pip install Pillow`)
- Built exe in `Profile\` (for render/perf tests) or `Debug\` (for physics tests)
```

---

## Phase 5: Update `.gitignore`

### Task 5.1 â€” Ensure tools directory is tracked

- [x] Verify `tools/` is NOT in `.gitignore`

```pwsh
Select-String -Path "G:\skore3\.gitignore" -Pattern "tools"
```

If it appears, remove the line. The `tools\` directory must be committed.

---

## Phase 6: Verification

### Task 6.1 â€” Verify all files were created

Run this check to confirm every file exists:

- [x] Run verification

```pwsh
$files = @(
    "G:\skore3\tools\find_msbuild.bat",
    "G:\skore3\tools\validate_format.bat",
    "G:\skore3\tools\format_fix.bat",
    "G:\skore3\tools\validate_build.bat",
    "G:\skore3\tools\check_dx12_validation.bat",
    "G:\skore3\tools\validate_fast.bat",
    "G:\skore3\tools\validate_renderers.bat",
    "G:\skore3\tools\validate_physics.bat",
    "G:\skore3\tools\check_physics_regression.py",
    "G:\skore3\tools\validate_perf.bat",
    "G:\skore3\tools\check_parity.py",
    "G:\skore3\tools\validate_full.bat",
    "G:\skore3\tools\agent_validate.bat",
    "G:\skore3\tools\README.md",
    "G:\skore3\AGENTS.md",
    "G:\skore3\Agentic\agent-guide.md"
)
$missing = $files | Where-Object { -not (Test-Path $_) }
if ($missing) { $missing | ForEach-Object { Write-Host "MISSING: $_" } }
else { Write-Host "ALL FILES PRESENT" }
```

### Task 6.2 â€” Run `validate_fast.bat` to confirm it works

- [x] Run the fast validation to confirm scripts are functional

```pwsh
cd G:\skore3
& tools\validate_fast.bat
```

Expected output:
```
â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
  VALIDATE_FAST â€” Format + Build
â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•

[1/2] Checking formatting...
PASS: All source files correctly formatted.
[2/2] Building Profile x64...
PASS: Build Profile|x64 succeeded.

â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
  VALIDATE_FAST: ALL PASSED
â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
```

### Task 6.3 â€” Run `validate_renderers.bat` to confirm tri-renderer works

- [x] Run renderer validation (requires GPU, ~60s)

```pwsh
cd G:\skore3
& tools\validate_renderers.bat
```

### Task 6.4 â€” Run `validate_physics.bat` to confirm regression test works

- [x] Run physics validation (~45s)

```pwsh
cd G:\skore3
& tools\validate_physics.bat
```

---

## Phase 7: Commit

### Task 7.1 â€” Stage all new files

- [ ] Stage changes

```pwsh
cd G:\skore3
git add tools/
git add AGENTS.md
git add Agentic/agent-guide.md
git add AGENTS.md
git add Agentic/Skills/collapse_params.py
git add Agentic/Plans/agentic-friendliness-implementation.md
```

### Task 7.2 â€” Confirm with user before committing

Ask: "All validation scripts created and tested. Ready to commit?"

### Task 7.3 â€” Commit

- [ ] Commit with descriptive message

```pwsh
git commit -m "feat: add tools/ validation scripts and universal AGENTS.md

- Created tools/ directory with tiered validation scripts:
  validate_fast.bat, validate_renderers.bat, validate_physics.bat,
  validate_perf.bat, validate_full.bat, agent_validate.bat
- Created AGENTS.md as universal agent contract (framework-agnostic)
- Moved agents.md to Agentic/agent-guide.md (agent-specific)
- Added helper scripts: find_msbuild.bat, check_dx12_validation.bat,
  format_fix.bat, check_parity.py, check_physics_regression.py
- Fixed hardcoded path in collapse_params.py
- Fixed stale Win32/x86 and naming convention references in AGENTS.md
- Added tools/README.md with usage guide

Co-authored-by: AI Agent <agent@example.invalid>"
```

---

## Summary of Created Files

| # | File | Purpose |
|---|------|---------|
| 1 | `tools\find_msbuild.bat` | Shared helper: locates MSBuild |
| 2 | `tools\validate_format.bat` | Check clang-format compliance |
| 3 | `tools\format_fix.bat` | Auto-fix formatting |
| 4 | `tools\validate_build.bat` | Build specified configuration |
| 5 | `tools\check_dx12_validation.bat` | Verify DX12 InfoQueue clean |
| 6 | `tools\validate_fast.bat` | Quick: format + build (~30s) |
| 7 | `tools\validate_renderers.bat` | Tri-renderer suite + stdout/stderr + parity (~60s) |
| 8 | `tools\validate_physics.bat` | Physics determinism regression (~45s) |
| 9 | `tools\check_physics_regression.py` | Physics CSV diff logic |
| 10 | `tools\validate_perf.bat` | Tri-renderer performance regression (~1 min) |
| 11 | `tools\check_parity.py` | Cross-renderer pixel comparison |
| 12 | `tools\validate_full.bat` | All validations combined (~3 min) |
| 13 | `tools\agent_validate.bat` | THE one command (delegates to full) |
| 14 | `tools\README.md` | Documentation for tools/ |
| 15 | `AGENTS.md` | Universal agent contract |
| 16 | `Agentic\agent-guide.md` | Renamed from `agents.md` (agent-specific) |
