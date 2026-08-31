@rem
@rem File: tools/validate_scene_parser_tests.bat
@rem Purpose:
@rem   Builds and runs CPU-only scene parser unit tests.
@rem
@rem Summary:
@rem   Parser tests exercise user-facing scene/style authoring syntax without
@rem   launching the renderer or updating screenshot baselines.
@rem
@rem Glossary:
@rem   CPU-only test: Test that exercises parser and data contracts without a
@rem   renderer launch.
@rem   Scene/style syntax: User-facing JSON fields accepted by authored
@rem   .scene.json and .style.json files.
@rem   Validation gate: Repository script that proves a class of changes before
@rem   commit or PR.
@rem
@rem Invariants:
@rem   - Scene/style syntax is repository compatibility surface.
@rem   - Unit-test failures should stop before PR-bound renderer validation.
@rem
@rem Related:
@rem   - AGENTS.md
@rem   - SkullbonezSource/Scene/AuthoredSceneParser.cpp
@rem
@echo off
setlocal enabledelayedexpansion
REM ===============================================================
REM  validate_scene_parser_tests.bat - Build and run CPU-side parser
REM  contract tests.
REM ===============================================================

set "REPO=%~dp0.."
set "PROJECT=%REPO%\Agentic\Tests\SceneParserUnitTests\SceneParserUnitTests.vcxproj"
set "EXE=%REPO%\Agentic\Tests\SceneParserUnitTests\x64\Debug\SceneParserUnitTests.exe"
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

echo Building scene parser unit tests...
"%MSBUILD_EXE%" "%PROJECT%" /p:Configuration=Debug /p:Platform=x64 %TOOLSET_ARG% /nologo /v:minimal /warnaserror
if errorlevel 1 (
    echo FAIL: scene parser unit test build failed.
    exit /b 1
)

echo Running scene parser unit tests...
pushd "%TEST_WORKDIR%"
"%EXE%"
set "TEST_EXIT=%ERRORLEVEL%"
popd
if not "%TEST_EXIT%"=="0" (
    echo FAIL: scene parser unit tests failed.
    exit /b 2
)

echo PASS: scene parser unit tests passed.
exit /b 0

:find_toolset
set "CHECK_TOOLSET=%~1"
for %%B in ("%MSBUILD_EXE%") do set "MSBUILD_BIN=%%~dpB"
set "VC_TARGETS_ROOT=%MSBUILD_BIN%..\..\Microsoft\VC"
for /d %%D in ("%VC_TARGETS_ROOT%\v*") do (
    if exist "%%~fD\Platforms\x64\PlatformToolsets\%CHECK_TOOLSET%\Toolset.props" exit /b 0
)
exit /b 1
