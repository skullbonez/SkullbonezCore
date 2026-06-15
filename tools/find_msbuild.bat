@rem
@rem File: tools/find_msbuild.bat
@rem Purpose:
@rem   Documents and runs the find_msbuild.bat developer/validation helper script.
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
REM ===============================================================
REM  find_msbuild.bat - Locates MSBuild via vswhere and sets MSBUILD_EXE.
REM  Called by other validate_*.bat scripts. Do not run directly.
REM ===============================================================

for /f "usebackq tokens=*" %%i in (`"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do (
    set "MSBUILD_EXE=%%i"
    goto :found
)

echo ERROR: MSBuild not found. Install Visual Studio with C++ workload.
exit /b 99

:found
exit /b 0
