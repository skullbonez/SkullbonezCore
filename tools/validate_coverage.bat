@rem
@rem File: tools/validate_coverage.bat
@rem Purpose:
@rem   Build the Debug doctest runner, capture product-line coverage, and check
@rem   the versioned subsystem floors.
@rem
@rem Mental model:
@rem   OpenCppCoverage debugs one ordinary test process and exports the source
@rem   lines observed through its program database. The Python checker owns all
@rem   tier and exclusion policy so this wrapper stays mechanical.
@rem
@rem Glossary:
@rem   Cobertura: XML coverage interchange format consumed by check_coverage.py.
@rem   Report-only: U0 bring-up mode that measures ratified scopes without yet
@rem   failing on their future floors.
@rem
@rem Invariants:
@rem   - Coverage runs the Debug x64 SKULLBONEZ_TESTS executable from repo root.
@rem   - Test and third-party lines never enter the product denominator.
@rem   - The temporary topple-case exclusion is removed in campaign task U4,
@rem     after that fixture is moved away from its instrumentation-sensitive
@rem     floating-point selection boundary.
@rem   - Full child output stays in TestOutput/coverage to keep gate output bounded.
@rem
@rem Related:
@rem   - tools/check_coverage.py
@rem   - tools/coverage_floors.json
@rem   - Agentic/Plans/TODO/unit-test-coverage-campaign.md
@rem
@echo off
setlocal enabledelayedexpansion

for %%I in ("%~dp0..") do set "REPO=%%~fI"
set "CONFIG=Debug"
set "COVERAGE_DIR=%REPO%\TestOutput\coverage"
set "COVERAGE_XML=%COVERAGE_DIR%\coverage.xml"
set "COVERAGE_LOG=%COVERAGE_DIR%\test-output.txt"
set "COVERAGE_SUMMARY=%COVERAGE_DIR%\summary.md"
set "COVERAGE_TOOL_INFO=%COVERAGE_DIR%\tool-info.txt"

if /I "%~1"=="--self-test" goto :self_test
if not "%~1"=="" (
    echo ERROR: Unknown argument "%~1".
    echo Usage: tools\validate_coverage.bat [--self-test]
    exit /b 64
)

echo.
echo ========================================
echo   VALIDATE_COVERAGE - Debug Unit Coverage
echo ========================================
echo.

call :find_coverage_tool
if errorlevel 1 exit /b 99

call "%~dp0find_python.bat"
if errorlevel 1 exit /b 99

call "%~dp0find_msbuild.bat"
if errorlevel 1 exit /b 99

set "TOOLSET_ARG="
set "FALLBACK_TOOLSET=v143"
findstr /C:"<PlatformToolset>v145</PlatformToolset>" "%REPO%\SKULLBONEZ_TESTS.vcxproj" >nul
if not errorlevel 1 (
    call :find_toolset v145
    if errorlevel 1 (
        call :find_toolset %FALLBACK_TOOLSET%
        if errorlevel 1 (
            echo ERROR: Platform toolset v145 not found, and fallback %FALLBACK_TOOLSET% is not installed.
            exit /b 99
        )
        set "TOOLSET_ARG=/p:PlatformToolset=%FALLBACK_TOOLSET%"
        echo INFO: Platform toolset v145 not found; falling back to VS2022 toolset %FALLBACK_TOOLSET%.
    )
)

echo [1/4] Checking coverage policy self-tests...
"%PYTHON_EXE%" "%~dp0check_coverage.py" --self-test
if errorlevel 1 exit /b 1

echo [2/4] Building SKULLBONEZ_TESTS %CONFIG%^|x64 %TOOLSET_ARG%...
"%MSBUILD_EXE%" "%REPO%\SKULLBONEZ_TESTS.vcxproj" /p:Configuration=%CONFIG% /p:Platform=x64 %TOOLSET_ARG% /nologo /v:minimal /warnaserror
if errorlevel 1 (
    echo FAIL: SKULLBONEZ_TESTS Debug build failed.
    exit /b 2
)
if not exist "%REPO%\Debug\SKULLBONEZ_TESTS.exe" (
    echo FAIL: Debug\SKULLBONEZ_TESTS.exe was not produced.
    exit /b 3
)

if not exist "%COVERAGE_DIR%" mkdir "%COVERAGE_DIR%"
if exist "%COVERAGE_XML%" del /q "%COVERAGE_XML%"
if exist "%COVERAGE_LOG%" del /q "%COVERAGE_LOG%"

echo [3/4] Capturing Cobertura product coverage...
echo       Full test output: %COVERAGE_LOG%
"%OPENCPPCOVERAGE_EXE%" --quiet --modules "%REPO%\Debug\SKULLBONEZ_TESTS.exe" --sources "%REPO%\SkullbonezSource" --working_dir "%REPO%" --export_type "cobertura:%COVERAGE_XML%" -- "%REPO%\Debug\SKULLBONEZ_TESTS.exe" "--test-case-exclude=Persistent contact solver: a box gains sleep support only after toppling from its edge" "--quiet" > "%COVERAGE_LOG%" 2>&1
if errorlevel 1 (
    echo FAIL: OpenCppCoverage or the Debug test process failed.
    findstr /C:"FATAL ERROR:" /C:"Status:" "%COVERAGE_LOG%"
    exit /b 4
)
if not exist "%COVERAGE_XML%" (
    echo FAIL: Cobertura XML was not produced: %COVERAGE_XML%
    exit /b 5
)

echo [4/4] Summarizing subsystem floors...
pushd "%REPO%"
"%PYTHON_EXE%" "%~dp0check_coverage.py" --xml "%COVERAGE_XML%" --config "%~dp0coverage_floors.json" --markdown-out "%COVERAGE_SUMMARY%"
set "CHECK_EXIT=!errorlevel!"
popd
if not "%CHECK_EXIT%"=="0" exit /b 6

echo.
echo OpenCppCoverage tool:
"%OPENCPPCOVERAGE_EXE%" --help > "%COVERAGE_TOOL_INFO%" 2>&1
findstr /B /C:"OpenCppCoverage Version:" "%COVERAGE_TOOL_INFO%"
echo Coverage XML: %COVERAGE_XML%
echo Coverage summary: %COVERAGE_SUMMARY%
echo ========================================
echo   VALIDATE_COVERAGE: ALL PASSED
echo ========================================
exit /b 0

:self_test
call "%~dp0find_python.bat"
if errorlevel 1 exit /b 99
"%PYTHON_EXE%" "%~dp0check_coverage.py" --self-test
exit /b %errorlevel%

:find_coverage_tool
if defined OPENCPPCOVERAGE_EXE if exist "%OPENCPPCOVERAGE_EXE%" exit /b 0
for %%E in (OpenCppCoverage.exe) do set "OPENCPPCOVERAGE_EXE=%%~$PATH:E"
if defined OPENCPPCOVERAGE_EXE if exist "%OPENCPPCOVERAGE_EXE%" exit /b 0
if exist "C:\Program Files\OpenCppCoverage\OpenCppCoverage.exe" (
    set "OPENCPPCOVERAGE_EXE=C:\Program Files\OpenCppCoverage\OpenCppCoverage.exe"
    exit /b 0
)
echo ERROR: OpenCppCoverage 0.9.9.0 or newer was not found.
echo Install: winget install --id OpenCppCoverage.OpenCppCoverage --exact --accept-package-agreements --accept-source-agreements
exit /b 1

:find_toolset
set "CHECK_TOOLSET=%~1"
for %%B in ("%MSBUILD_EXE%") do set "MSBUILD_BIN=%%~dpB"
set "VC_TARGETS_ROOT=%MSBUILD_BIN%..\..\Microsoft\VC"
for /d %%D in ("%VC_TARGETS_ROOT%\v*") do (
    if exist "%%~fD\Platforms\x64\PlatformToolsets\%CHECK_TOOLSET%\Toolset.props" exit /b 0
)
exit /b 1
