@rem
@rem File: tools/validate_dx12_arch_tests.bat
@rem Purpose:
@rem   Documents and runs the validate_dx12_arch_tests.bat developer/validation helper script.
@rem
@rem Mental model:
@rem   Tools are command-line guardrails around builds, validation, screenshots,
@rem   diagnostics, and artifact handling. They make the safe path repeatable and
@rem   keep output bounded for humans and agents.
@rem
@rem Glossary:
@rem   Validation gate: Repository script that proves a class of changes before
@rem   commit or PR.
@rem
@rem Invariants:
@rem   - Tool output should be bounded and readable because agents and humans use
@rem   it for decisions.
@rem
@rem Related:
@rem   - AGENTS.md
@rem   - Agentic/Reference/comment-style-guide.md
@rem
@rem
@echo off
setlocal enabledelayedexpansion
REM ===============================================================
REM  validate_dx12_arch_tests.bat - Build and run CPU-side DX12
REM  architecture unit tests.
REM
REM  These tests do not create a D3D12 device. They are fast checks for the
REM  renderer architecture rules that can be verified before GPU execution:
REM  descriptor table allocation policy and render-graph transition semantics.
REM  Use validate_dx12_renderer.bat for the real GPU crash/screenshot gate.
REM ===============================================================

set "REPO=%~dp0.."
set "PROJECT=%REPO%\Agentic\Tests\Dx12ArchUnitTests\Dx12ArchUnitTests.vcxproj"
set "EXE=%REPO%\Agentic\Tests\Dx12ArchUnitTests\x64\Debug\Dx12ArchUnitTests.exe"

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

echo Building DX12 architecture unit tests...
"%MSBUILD_EXE%" "%PROJECT%" /p:Configuration=Debug /p:Platform=x64 %TOOLSET_ARG% /nologo /v:minimal /warnaserror
if errorlevel 1 (
    echo FAIL: DX12 architecture unit test build failed.
    exit /b 1
)

echo Running DX12 architecture unit tests...
"%EXE%"
set "TEST_EXIT=!ERRORLEVEL!"
REM Hazard: Windows fatal exits can be signed negative NTSTATUS values. The
REM usual `if errorlevel 1` comparison misses them, so require exact zero.
if not "!TEST_EXIT!"=="0" (
    echo FAIL: DX12 architecture unit tests failed.
    exit /b 2
)

echo PASS: DX12 architecture unit tests passed.
exit /b 0

:find_toolset
set "CHECK_TOOLSET=%~1"
for %%B in ("%MSBUILD_EXE%") do set "MSBUILD_BIN=%%~dpB"
set "VC_TARGETS_ROOT=%MSBUILD_BIN%..\..\Microsoft\VC"
for /d %%D in ("%VC_TARGETS_ROOT%\v*") do (
    if exist "%%~fD\Platforms\x64\PlatformToolsets\%CHECK_TOOLSET%\Toolset.props" exit /b 0
)
exit /b 1
