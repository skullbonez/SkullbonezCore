@rem
@rem File: tools/validate_tests.bat
@rem Purpose:
@rem   Build and run the SKULLBONEZ_TESTS unit-test executable.
@rem
@rem Mental model:
@rem   Unit tests are the cheapest validation layer: they compile small
@rem   contracts into a console runner and avoid launching the full runtime.
@rem
@rem Glossary:
@rem   Test project: The Visual Studio project that owns doctest-based unit
@rem   tests and emits SKULLBONEZ_TESTS.exe.
@rem   Validation gate: Repository script that proves a class of changes before
@rem   commit or PR.
@rem
@rem Invariants:
@rem   - The test project filter check runs in partial-project mode because the
@rem     test executable intentionally compiles only the files under test.
@rem   - Tests run from the repository root so future file-relative fixtures use
@rem     the same working directory as the engine validation scripts.
@rem
@rem Related:
@rem   - fable_plans/01-unit-test-pyramid-plan.md
@rem   - Agentic/Reference/comment-style-guide.md
@rem
@echo off
setlocal enabledelayedexpansion
REM ===============================================================
REM  validate_tests.bat - Build and run doctest unit tests.
REM ===============================================================

set "REPO=%~dp0.."
set "CONFIG=Profile"

pushd "%REPO%"

echo.
echo ========================================
echo   VALIDATE_TESTS - Unit Test Harness
echo ========================================
echo.

call "%~dp0find_python.bat"
if errorlevel 1 (
    popd
    exit /b 99
)

echo [1/3] Checking SKULLBONEZ_TESTS project filters...
"%PYTHON_EXE%" "%~dp0validate_project_filters.py" --repo "%REPO%" --project "%REPO%\SKULLBONEZ_TESTS.vcxproj" --filters "%REPO%\SKULLBONEZ_TESTS.vcxproj.filters" --partial-project --json-out "%REPO%\TestOutput\validation\project_filters\tests_summary.json"
if errorlevel 1 (
    echo FAIL: Test project filter validation failed.
    popd
    exit /b 1
)

call "%~dp0find_msbuild.bat"
if errorlevel 1 (
    popd
    exit /b 99
)

set "TOOLSET_ARG="
set "FALLBACK_TOOLSET=v143"

if not "%SKULLBONEZ_PLATFORM_TOOLSET%"=="" (
    call :find_toolset "%SKULLBONEZ_PLATFORM_TOOLSET%"
    if errorlevel 1 (
        echo ERROR: Requested platform toolset %SKULLBONEZ_PLATFORM_TOOLSET% not found.
        popd
        exit /b 99
    )
    set "TOOLSET_ARG=/p:PlatformToolset=%SKULLBONEZ_PLATFORM_TOOLSET%"
) else (
    findstr /C:"<PlatformToolset>v145</PlatformToolset>" "%REPO%\SKULLBONEZ_TESTS.vcxproj" >nul
    if not errorlevel 1 (
        call :find_toolset v145
        if errorlevel 1 (
            call :find_toolset %FALLBACK_TOOLSET%
            if errorlevel 1 (
                echo ERROR: Platform toolset v145 not found, and fallback %FALLBACK_TOOLSET% is not installed.
                popd
                exit /b 99
            )
            set "TOOLSET_ARG=/p:PlatformToolset=%FALLBACK_TOOLSET%"
            echo INFO: Platform toolset v145 not found; falling back to VS2022 toolset %FALLBACK_TOOLSET%.
        )
    )
)

echo [2/3] Building SKULLBONEZ_TESTS %CONFIG%^|x64 %TOOLSET_ARG%...
"%MSBUILD_EXE%" "%REPO%\SKULLBONEZ_TESTS.vcxproj" /p:Configuration=%CONFIG% /p:Platform=x64 %TOOLSET_ARG% /nologo /v:normal /clp:Summary;PerformanceSummary /warnaserror
if errorlevel 1 (
    echo FAIL: SKULLBONEZ_TESTS build failed.
    popd
    exit /b 2
)

if not exist "%REPO%\Profile\SKULLBONEZ_TESTS.exe" (
    echo FAIL: Profile\SKULLBONEZ_TESTS.exe was not produced.
    popd
    exit /b 3
)

echo [3/3] Running SKULLBONEZ_TESTS...
"%REPO%\Profile\SKULLBONEZ_TESTS.exe" --duration=true
if errorlevel 1 (
    echo FAIL: Unit tests failed.
    popd
    exit /b 4
)

echo.
echo ========================================
echo   VALIDATE_TESTS: ALL PASSED
echo ========================================
popd
exit /b 0

:find_toolset
set "CHECK_TOOLSET=%~1"
for %%B in ("%MSBUILD_EXE%") do set "MSBUILD_BIN=%%~dpB"
set "VC_TARGETS_ROOT=%MSBUILD_BIN%..\..\Microsoft\VC"
for /d %%D in ("%VC_TARGETS_ROOT%\v*") do (
    if exist "%%~fD\Platforms\x64\PlatformToolsets\%CHECK_TOOLSET%\Toolset.props" exit /b 0
)
exit /b 1
