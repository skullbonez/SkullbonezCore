@rem
@rem File: tools/validate_build_all.bat
@rem Purpose:
@rem   Builds every ordinary development configuration in one explicit command.
@rem
@rem Summary:
@rem   Automation, Debug, and Profile remain useful together for release
@rem   preparation and explicit whole-build requests. Ordinary fast validation
@rem   builds only the configuration it consumes.
@rem
@rem Glossary:
@rem   Object root: Per-configuration output directory holding *.obj evidence.
@rem   Development configuration: One of Automation, Debug, or Profile.
@rem
@rem Invariants:
@rem   - Automation, Debug, and Profile are all built in a fixed order.
@rem   - Release is built only on explicit request because no gate reads it.
@rem   - The first failing configuration stops the run and returns non-zero.
@rem
@rem Related:
@rem   - tools/validate_build.bat
@rem   - tools/validate_fast.bat
@rem
@rem
@echo off
setlocal
REM ===============================================================
REM  validate_build_all.bat - Build every gate-required configuration.
REM  Usage: validate_build_all.bat [--with-release]
REM  Exit 0 = all builds succeeded, non-zero = first failure's code.
REM ===============================================================

set "WITH_RELEASE=0"
if /I "%~1"=="--with-release" set "WITH_RELEASE=1"
if not "%~1"=="" if "%WITH_RELEASE%"=="0" (
    echo ERROR: Unknown argument "%~1".
    echo Usage: tools\validate_build_all.bat [--with-release]
    exit /b 64
)

REM Invariant: honours the same opt-out as validate_ready_builds.bat so a parent
REM gate that already built these configurations does not rebuild them. The
REM caller stays responsible for having built every gate-required root.
if /I "%SKULLBONEZ_SKIP_READY_BUILDS%"=="1" exit /b 0

echo.
echo ========================================
echo   VALIDATE_BUILD_ALL - Automation + Debug + Profile
echo ========================================
echo.

REM Why: Profile is built last so the IDE's default launch target is the most
REM recently linked binary, matching validate_ready_builds.bat's contract.
for %%C in (Automation Debug Profile) do (
    echo [build] %%C^|x64...
    call "%~dp0validate_build.bat" %%C
    if errorlevel 1 (
        echo FAIL: %%C build failed; skipping remaining configurations.
        exit /b 1
    )
)

if "%WITH_RELEASE%"=="1" (
    echo [build] Release^|x64...
    call "%~dp0validate_build.bat" Release
    if errorlevel 1 (
        echo FAIL: Release build failed.
        exit /b 1
    )
)

echo.
echo PASS: All gate-required configurations built.
exit /b 0
