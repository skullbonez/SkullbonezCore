@rem
@rem File: tools/validate_runtime_interaction_policy.bat
@rem Purpose:
@rem   Builds and runs CPU-only runtime interaction controller policy tests.
@rem
@rem Mental model:
@rem   Runtime interaction policy is input/physics glue. Fast CPU tests catch
@rem   impossible pointer capture, camera-look, and physics-step regressions
@rem   before a full renderer or physics validation launch.
@rem
@rem Glossary:
@rem   CPU-only test: Test that exercises runtime policy without a renderer launch.
@rem   Validation gate: Repository script that proves a class of changes before
@rem   commit or PR.
@rem
@rem Invariants:
@rem   - Tool pointer capture must be exclusive.
@rem   - Manipulator drag policy must not depend on holding the step key.
@rem
@rem Related:
@rem   - AGENTS.md
@rem   - Agentic/Plans/TODO/interaction-state-machine.md
@rem   - Agentic/Tests/RuntimeInteractionPolicyTests/RuntimeInteractionPolicyTests.cpp
@rem
@echo off
setlocal enabledelayedexpansion
REM ===============================================================
REM  validate_runtime_interaction_policy.bat - Build and run
REM  CPU-side runtime interaction controller policy tests.
REM ===============================================================

set "REPO=%~dp0.."
set "PROJECT=%REPO%\Agentic\Tests\RuntimeInteractionPolicyTests\RuntimeInteractionPolicyTests.vcxproj"
set "DEBUG_EXE=%REPO%\Agentic\Tests\RuntimeInteractionPolicyTests\x64\Debug\RuntimeInteractionPolicyTests.exe"
set "RELEASE_EXE=%REPO%\Agentic\Tests\RuntimeInteractionPolicyTests\x64\Release\RuntimeInteractionPolicyTests.exe"

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

echo Building runtime interaction policy tests (Debug)...
"%MSBUILD_EXE%" "%PROJECT%" /p:Configuration=Debug /p:Platform=x64 %TOOLSET_ARG% /nologo /v:minimal /warnaserror
if errorlevel 1 (
    echo FAIL: runtime interaction policy Debug test build failed.
    exit /b 1
)

echo Running runtime interaction policy tests (Debug)...
pushd "%REPO%"
"%DEBUG_EXE%"
set "TEST_EXIT=%ERRORLEVEL%"
popd
if not "%TEST_EXIT%"=="0" (
    echo FAIL: runtime interaction policy Debug tests failed.
    exit /b 2
)

echo Building runtime interaction policy tests (Release)...
"%MSBUILD_EXE%" "%PROJECT%" /p:Configuration=Release /p:Platform=x64 %TOOLSET_ARG% /nologo /v:minimal /warnaserror
if errorlevel 1 (
    echo FAIL: runtime interaction policy Release test build failed.
    exit /b 3
)

echo Running runtime interaction policy tests (Release)...
pushd "%REPO%"
"%RELEASE_EXE%"
set "TEST_EXIT=%ERRORLEVEL%"
popd
if not "%TEST_EXIT%"=="0" (
    echo FAIL: runtime interaction policy Release tests failed.
    exit /b 4
)

echo PASS: runtime interaction policy tests passed.
exit /b 0

:find_toolset
set "CHECK_TOOLSET=%~1"
for %%B in ("%MSBUILD_EXE%") do set "MSBUILD_BIN=%%~dpB"
set "VC_TARGETS_ROOT=%MSBUILD_BIN%..\..\Microsoft\VC"
for /d %%D in ("%VC_TARGETS_ROOT%\v*") do (
    if exist "%%~fD\Platforms\x64\PlatformToolsets\%CHECK_TOOLSET%\Toolset.props" exit /b 0
)
exit /b 1
