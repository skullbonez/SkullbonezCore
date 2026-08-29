@rem
@rem File: tools/validate_coverage.bat
@rem Purpose:
@rem   Build the Debug doctest runner, capture product-line coverage, and check
@rem   the versioned subsystem floors.
@rem
@rem Summary:
@rem   Microsoft.CodeCoverage.Console statically instruments one ordinary test
@rem   process and exports the source lines observed through its program database.
@rem   The Python checker owns all tier and exclusion policy so this wrapper
@rem   stays mechanical.
@rem
@rem Glossary:
@rem   Cobertura: XML coverage interchange format consumed by check_coverage.py.
@rem   Report-only: Measurement mode that reports ratified scopes without
@rem   enforcing minimum coverage floors.
@rem
@rem Invariants:
@rem   - Coverage runs the Debug x64 SKULLBONEZ_TESTS executable from repo root.
@rem   - Test and third-party lines never enter the product denominator.
@rem   - Every doctest case participates; instrumentation-sensitive fixtures
@rem     must be authored away from floating-point selection boundaries.
@rem   - Child probes still run, but the collector observes only their parent;
@rem     the children deliberately terminate to prove fatal boundaries.
@rem   - Full child output stays in TestOutput/coverage to keep gate output bounded.
@rem
@rem Related:
@rem   - tools/check_coverage.py
@rem   - tools/coverage_floors.json
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
set "COVERAGE_TOOL_LOG=%COVERAGE_DIR%\collector.log"
set "DOCTEST_XML=%COVERAGE_DIR%\doctest.xml"
set "COVERAGE_CHILD_EXE=%REPO%\Debug\SKULLBONEZ_TESTS_COVERAGE_CHILD.exe"

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

echo [2/4] Building ordinary child probes and coverage parent %CONFIG%^|x64 %TOOLSET_ARG%...
"%MSBUILD_EXE%" "%REPO%\SKULLBONEZ_TESTS.vcxproj" /p:Configuration=%CONFIG% /p:Platform=x64 /p:SKULLBONEZ_COVERAGE_CHILD_BUILD=1 %TOOLSET_ARG% /nologo /v:minimal /warnaserror
if errorlevel 1 (
    echo FAIL: Ordinary child-probe build failed.
    exit /b 2
)
if not exist "%COVERAGE_CHILD_EXE%" (
    echo FAIL: The ordinary child-probe executable was not produced.
    exit /b 3
)
"%MSBUILD_EXE%" "%REPO%\SKULLBONEZ_TESTS.vcxproj" /p:Configuration=%CONFIG% /p:Platform=x64 /p:SKULLBONEZ_COVERAGE_BUILD=1 %TOOLSET_ARG% /nologo /v:minimal /warnaserror
if errorlevel 1 (
    echo FAIL: SKULLBONEZ_TESTS Debug build failed.
    exit /b 2
)
if not exist "%REPO%\Debug\SKULLBONEZ_TESTS.exe" (
    echo FAIL: Debug\SKULLBONEZ_TESTS.exe was not produced.
    exit /b 3
)
set "SKULLBONEZ_TEST_CHILD_EXE=%COVERAGE_CHILD_EXE%"

if not exist "%COVERAGE_DIR%" mkdir "%COVERAGE_DIR%"
if exist "%COVERAGE_XML%" del /q "%COVERAGE_XML%"
if exist "%COVERAGE_LOG%" del /q "%COVERAGE_LOG%"
if exist "%COVERAGE_TOOL_LOG%" del /q "%COVERAGE_TOOL_LOG%"
if exist "%DOCTEST_XML%" del /q "%DOCTEST_XML%"

echo [3/4] Capturing Cobertura product coverage...
echo       Full test output: %COVERAGE_LOG%
"%MICROSOFT_CODE_COVERAGE_EXE%" collect --settings "%~dp0native_coverage.settings" --include-files "%REPO%\Debug\SKULLBONEZ_TESTS.exe" --output "%COVERAGE_XML%" --output-format cobertura --log-file "%COVERAGE_TOOL_LOG%" --log-level Info -- "%REPO%\Debug\SKULLBONEZ_TESTS.exe" "--reporters=xml" "--out=%DOCTEST_XML%" > "%COVERAGE_LOG%" 2>&1
set "COLLECT_EXIT=!errorlevel!"
if not exist "%DOCTEST_XML%" (
    echo FAIL: The parent doctest process did not publish a completion report.
    echo Collector log: %COVERAGE_TOOL_LOG%
    exit /b 4
)
"%PYTHON_EXE%" -c "import sys, xml.etree.ElementTree as ET; root=ET.parse(sys.argv[1]).getroot(); assertions=root.find('OverallResultsAsserts'); cases=root.find('OverallResultsTestCases'); sys.exit(0 if assertions is not None and cases is not None and assertions.get('failures') == '0' and cases.get('failures') == '0' and int(cases.get('successes', '0')) > 0 else 1)" "%DOCTEST_XML%"
if errorlevel 1 (
    echo FAIL: The parent doctest process reported a test failure or incomplete run.
    exit /b 4
)
if not exist "%COVERAGE_XML%" (
    echo FAIL: Cobertura XML was not produced: %COVERAGE_XML%
    exit /b 5
)
if not "%COLLECT_EXIT%"=="0" echo INFO: Collector exit %COLLECT_EXIT% reflects deliberate nonzero fatal-child probes; parent doctest and Cobertura reports are complete.

echo [4/4] Summarizing subsystem floors...
pushd "%REPO%"
"%PYTHON_EXE%" "%~dp0check_coverage.py" --xml "%COVERAGE_XML%" --config "%~dp0coverage_floors.json" --markdown-out "%COVERAGE_SUMMARY%"
set "CHECK_EXIT=!errorlevel!"
popd
if not "%CHECK_EXIT%"=="0" exit /b 6

echo.
echo Microsoft native coverage tool:
"%MICROSOFT_CODE_COVERAGE_EXE%" --version > "%COVERAGE_TOOL_INFO%" 2>&1
type "%COVERAGE_TOOL_INFO%"
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
if defined MICROSOFT_CODE_COVERAGE_EXE if exist "%MICROSOFT_CODE_COVERAGE_EXE%" exit /b 0
for %%E in (Microsoft.CodeCoverage.Console.exe) do set "MICROSOFT_CODE_COVERAGE_EXE=%%~$PATH:E"
if defined MICROSOFT_CODE_COVERAGE_EXE if exist "%MICROSOFT_CODE_COVERAGE_EXE%" exit /b 0
set "VSWHERE=C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -find Common7\IDE\Extensions\Microsoft\CodeCoverage.Console\Microsoft.CodeCoverage.Console.exe`) do set "MICROSOFT_CODE_COVERAGE_EXE=%%i"
)
if defined MICROSOFT_CODE_COVERAGE_EXE if exist "%MICROSOFT_CODE_COVERAGE_EXE%" exit /b 0
echo ERROR: Microsoft.CodeCoverage.Console was not found in the active Visual Studio installation.
echo Install the Visual Studio native code coverage tools component.
exit /b 1

:find_toolset
set "CHECK_TOOLSET=%~1"
for %%B in ("%MSBUILD_EXE%") do set "MSBUILD_BIN=%%~dpB"
set "VC_TARGETS_ROOT=%MSBUILD_BIN%..\..\Microsoft\VC"
for /d %%D in ("%VC_TARGETS_ROOT%\v*") do (
    if exist "%%~fD\Platforms\x64\PlatformToolsets\%CHECK_TOOLSET%\Toolset.props" exit /b 0
)
exit /b 1
