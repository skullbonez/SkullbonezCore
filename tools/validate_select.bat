@rem
@rem File: tools/validate_select.bat
@rem Purpose:
@rem   Runs one or more explicitly selected repository validation owners.
@rem
@rem Mental model:
@rem   This is a developer convenience router. Each selected target delegates to
@rem   its owning script; mandatory broad ordering remains in validate_full.
@rem
@rem Glossary:
@rem   CPU umbrella: Five first-party CPU/coverage gates run fail-fast through
@rem   validate_all_cpu_tests.bat.
@rem   Validation gate: Repository script that proves a class of changes before
@rem   commit or PR.
@rem
@rem Invariants:
@rem   - Every advertised selector maps to one concrete owning script.
@rem   - Successful broad targets suppress the redundant ready-build footer.
@rem   - The fast selector builds Debug before delegating because validate_fast
@rem   owns a two-configuration compiled-symbol reachability gate.
@rem
@rem Related:
@rem   - AGENTS.md
@rem   - tools/README.md
@rem   - tools/validate_all_cpu_tests.bat
@rem
@rem
@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "ROOT=%~dp0"
if "%~1"=="" (
    call :print_help
    exit /b 1
)

set "FAILED=0"
set "READY_BUILDS_HANDLED=0"
set "PREVIOUS_SKIP_READY_BUILDS=%SKULLBONEZ_SKIP_READY_BUILDS%"
set "SKULLBONEZ_SKIP_READY_BUILDS=1"

for %%A in (%*) do (
    set "ARG=%%~A"
    set "KNOWN=1"
    if /I "!ARG!"=="fast" (
        call "%ROOT%validate_build.bat" Debug
        if errorlevel 1 (
            set "FAILED=1"
        ) else (
            set "SKULLBONEZ_ASSUME_DEBUG_BUILT=1"
            call "%ROOT%validate_fast.bat"
            if errorlevel 1 set "FAILED=1"
            if not errorlevel 1 set "READY_BUILDS_HANDLED=1"
        )
    ) else if /I "!ARG!"=="all-cpu-tests" (
        call "%ROOT%validate_all_cpu_tests.bat"
        if errorlevel 1 set "FAILED=1"
    ) else if /I "!ARG!"=="tests" (
        call "%ROOT%validate_tests.bat"
        if errorlevel 1 set "FAILED=1"
    ) else if /I "!ARG!"=="renderers" (
        echo INFO: "renderers" is a retired compatibility target; running dx12-renderer.
        call "%ROOT%validate_dx12_renderer.bat"
        if errorlevel 1 set "FAILED=1"
    ) else if /I "!ARG!"=="dx12-renderer" (
        call "%ROOT%validate_dx12_renderer.bat"
        if errorlevel 1 set "FAILED=1"
    ) else if /I "!ARG!"=="dx12" (
        call "%ROOT%validate_dx12_renderer.bat"
        if errorlevel 1 set "FAILED=1"
    ) else if /I "!ARG!"=="concepts" (
        call "%ROOT%validate_concepts.bat" smoke
        if errorlevel 1 set "FAILED=1"
    ) else if /I "!ARG!"=="concept-smoke" (
        call "%ROOT%validate_concepts.bat" smoke
        if errorlevel 1 set "FAILED=1"
    ) else if /I "!ARG!"=="concept-core" (
        call "%ROOT%validate_concepts.bat" core
        if errorlevel 1 set "FAILED=1"
    ) else if /I "!ARG!"=="concept-full" (
        call "%ROOT%validate_concepts.bat" full
        if errorlevel 1 set "FAILED=1"
    ) else if /I "!ARG!"=="shaders" (
        call "%ROOT%validate_shaders.bat"
        if errorlevel 1 set "FAILED=1"
    ) else if /I "!ARG!"=="project-filters" (
        call "%ROOT%validate_project_filters.bat"
        if errorlevel 1 set "FAILED=1"
    ) else if /I "!ARG!"=="vcxproj-filters" (
        call "%ROOT%validate_project_filters.bat"
        if errorlevel 1 set "FAILED=1"
    ) else if /I "!ARG!"=="runtime-interaction-policy" (
        call "%ROOT%validate_runtime_interaction_policy.bat"
        if errorlevel 1 set "FAILED=1"
    ) else if /I "!ARG!"=="replay-visual-fidelity" (
        call "%ROOT%validate_replay_visual_fidelity.bat"
        if errorlevel 1 set "FAILED=1"
    ) else if /I "!ARG!"=="automation" (
        call "%ROOT%validate_automation.bat"
        if errorlevel 1 set "FAILED=1"
    ) else if /I "!ARG!"=="scene-parser-tests" (
        call "%ROOT%validate_scene_parser_tests.bat"
        if errorlevel 1 set "FAILED=1"
    ) else if /I "!ARG!"=="dx12-arch-tests" (
        call "%ROOT%validate_dx12_arch_tests.bat"
        if errorlevel 1 set "FAILED=1"
    ) else if /I "!ARG!"=="ui" (
        call "%ROOT%validate_ui.bat"
        if errorlevel 1 set "FAILED=1"
    ) else if /I "!ARG!"=="ui-stress" (
        call "%ROOT%validate_ui_stress.bat"
        if errorlevel 1 set "FAILED=1"
    ) else if /I "!ARG!"=="demo-stress" (
        call "%ROOT%validate_demo_stress.bat"
        if errorlevel 1 set "FAILED=1"
    ) else if /I "!ARG!"=="physics" (
        call "%ROOT%validate_physics.bat"
        if errorlevel 1 set "FAILED=1"
    ) else if /I "!ARG!"=="physics-deep" (
        call "%ROOT%validate_physics_deep.bat"
        if errorlevel 1 set "FAILED=1"
    ) else if /I "!ARG!"=="physics-query" (
        call "%ROOT%validate_physics_query.bat"
        if errorlevel 1 set "FAILED=1"
    ) else if /I "!ARG!"=="perf" (
        call "%ROOT%validate_perf.bat"
        if errorlevel 1 set "FAILED=1"
    ) else if /I "!ARG!"=="full" (
        call "%ROOT%validate_full.bat"
        if errorlevel 1 set "FAILED=1"
        if not errorlevel 1 set "READY_BUILDS_HANDLED=1"
    ) else if /I "!ARG!"=="deep" (
        call "%ROOT%validate_deep.bat"
        if errorlevel 1 set "FAILED=1"
        if not errorlevel 1 set "READY_BUILDS_HANDLED=1"
    ) else if /I "!ARG!"=="agent" (
        call "%ROOT%agent_validate.bat"
        if errorlevel 1 set "FAILED=1"
        if not errorlevel 1 set "READY_BUILDS_HANDLED=1"
    ) else if /I "!ARG!"=="format" (
        call "%ROOT%validate_format.bat"
        if errorlevel 1 set "FAILED=1"
    ) else if /I "!ARG!"=="build-debug" (
        call "%ROOT%validate_build.bat" Debug
        if errorlevel 1 set "FAILED=1"
    ) else if /I "!ARG!"=="build-profile" (
        call "%ROOT%validate_build.bat" Profile
        if errorlevel 1 set "FAILED=1"
    ) else if /I "!ARG!"=="build-automation" (
        call "%ROOT%validate_build.bat" Automation
        if errorlevel 1 set "FAILED=1"
    ) else if /I "!ARG!"=="build-release" (
        call "%ROOT%validate_build.bat" Release
        if errorlevel 1 set "FAILED=1"
    ) else (
        set "KNOWN=0"
        echo Unknown validation target: !ARG!
        call :print_help
        set "FAILED=1"
    )
)

if "!FAILED!"=="1" (
    echo.
    echo VALIDATE_SELECT: one or more validations failed.
    exit /b 1
)

set "SKULLBONEZ_SKIP_READY_BUILDS=%PREVIOUS_SKIP_READY_BUILDS%"
if "!READY_BUILDS_HANDLED!"=="1" (
    echo [ready] Requested validation already built Profile and Debug.
) else (
    call "%ROOT%validate_ready_builds.bat"
    if errorlevel 1 exit /b 2
)

echo.
echo VALIDATE_SELECT: all requested validations passed.
exit /b 0

:print_help
echo.
echo Validate one or more targets from this workspace:
echo   tools\validate_select.bat fast
echo   tools\validate_select.bat all-cpu-tests
echo   tools\validate_select.bat tests
echo   tools\validate_select.bat dx12-renderer
echo   tools\validate_select.bat dx12
echo   tools\validate_select.bat concepts
echo   tools\validate_select.bat concept-smoke
echo   tools\validate_select.bat concept-core
echo   tools\validate_select.bat concept-full
echo   tools\validate_select.bat shaders
echo   tools\validate_select.bat project-filters
echo   tools\validate_select.bat runtime-interaction-policy
echo   tools\validate_select.bat replay-visual-fidelity
echo   tools\validate_select.bat automation
echo   tools\validate_select.bat scene-parser-tests
echo   tools\validate_select.bat dx12-arch-tests
echo   tools\validate_select.bat ui
echo   tools\validate_select.bat ui-stress
echo   tools\validate_select.bat demo-stress
echo   tools\validate_select.bat physics
echo   tools\validate_select.bat physics-deep
echo   tools\validate_select.bat physics-query
echo   tools\validate_select.bat perf
echo   tools\validate_select.bat full
echo   tools\validate_select.bat deep
echo.
echo   tools\validate_select.bat format
echo   tools\validate_select.bat build-debug
echo   tools\validate_select.bat build-profile
echo   tools\validate_select.bat build-automation
echo   tools\validate_select.bat build-release
echo.
echo You can pass several targets in one command:
echo   tools\validate_select.bat format dx12-renderer physics
echo.
echo Legacy alias:
echo   tools\validate_select.bat renderers
exit /b 0
