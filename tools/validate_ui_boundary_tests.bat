@rem
@rem File: tools/validate_ui_boundary_tests.bat
@rem Purpose:
@rem   Builds and runs the renderer-free production UI link probe.
@rem
@rem Summary:
@rem   A successful Release link proves SKULLBONEZ_UI owns a backend-neutral
@rem   component implementation. The executable fingerprints only reusable
@rem   component contracts without launching or linking the product engine.
@rem
@rem Glossary:
@rem   Link probe: Executable whose dependency set is itself an architecture test.
@rem   Release-only: Configuration without profiling implementation references.
@rem
@rem Invariants:
@rem   - The test project references only SKULLBONEZ_UI.
@rem   - The harness compiles the Core diagnostic infrastructure floor directly
@rem     because the UI library may return recoverable results while no production
@rem     Core library exists for this standalone link probe.
@rem   - No graphics device or native window is created.
@rem
@rem Related:
@rem   - Agentic/Tests/UiBoundaryUnitTests/UiBoundaryUnitTests.vcxproj
@rem   - SKULLBONEZ_UI.vcxproj
@rem
@echo off
setlocal enabledelayedexpansion

set "REPO=%~dp0.."
set "PROJECT=%REPO%\Agentic\Tests\UiBoundaryUnitTests\UiBoundaryUnitTests.vcxproj"
set "EXE=%REPO%\Agentic\Tests\UiBoundaryUnitTests\x64\Release\UiBoundaryUnitTests.exe"
set "TEST_WORKDIR=%REPO%"
if defined SKULLBONEZ_TEST_WORKDIR if "%SKULLBONEZ_PARALLEL_VALIDATION%"=="1" set "TEST_WORKDIR=%SKULLBONEZ_TEST_WORKDIR%"
if defined SKULLBONEZ_TEST_WORKDIR if not "%SKULLBONEZ_PARALLEL_VALIDATION%"=="1" exit /b 99

call "%~dp0find_msbuild.bat"
if errorlevel 1 exit /b 99

set "TOOLSET_ARG="
set "FALLBACK_TOOLSET=v143"

if not "%SKULLBONEZ_PLATFORM_TOOLSET%"=="" (
    call :find_toolset "%SKULLBONEZ_PLATFORM_TOOLSET%"
    if errorlevel 1 (
        echo ERROR: Requested platform toolset %SKULLBONEZ_PLATFORM_TOOLSET% not found.
        exit /b 99
    )
    set "TOOLSET_ARG=/p:PlatformToolset=%SKULLBONEZ_PLATFORM_TOOLSET%"
) else (
    findstr /C:"<PlatformToolset>v145</PlatformToolset>" "%PROJECT%" >nul
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
)

echo Building renderer-free UI boundary tests...
"%MSBUILD_EXE%" "%PROJECT%" /p:Configuration=Release /p:Platform=x64 %TOOLSET_ARG% /nologo /v:minimal /warnaserror
if errorlevel 1 (
    echo FAIL: UI boundary test build failed.
    exit /b 1
)

echo Running renderer-free UI boundary tests...
pushd "%TEST_WORKDIR%"
"%EXE%"
set "TEST_EXIT=%ERRORLEVEL%"
popd
if not "%TEST_EXIT%"=="0" (
    echo FAIL: UI boundary tests failed.
    exit /b 2
)

echo PASS: UI boundary tests passed.
exit /b 0

:find_toolset
set "CHECK_TOOLSET=%~1"
for %%B in ("%MSBUILD_EXE%") do set "MSBUILD_BIN=%%~dpB"
set "VC_TARGETS_ROOT=%MSBUILD_BIN%..\..\Microsoft\VC"
for /d %%D in ("%VC_TARGETS_ROOT%\v*") do (
    if exist "%%~fD\Platforms\x64\PlatformToolsets\%CHECK_TOOLSET%\Toolset.props" exit /b 0
)
exit /b 1
