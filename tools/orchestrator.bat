@rem
@rem File: tools/orchestrator.bat
@rem Purpose:
@rem   Windows launcher for the roadmap orchestrator state-machine helper.
@rem
@rem Mental model:
@rem   The Python script owns mechanical queue/state checks. Codex owns
@rem   implementation reasoning only after this tool renders a prompt or invokes
@rem   codex exec.
@rem
@rem Invariants:
@rem   - Use Python from find_python.bat.
@rem   - Do not mutate queue state unless the Python command explicitly asks.
@rem
@echo off
setlocal

set "REPO=%~dp0.."
pushd "%REPO%"

call "%~dp0find_python.bat"
if errorlevel 1 (
    popd
    exit /b 99
)

"%PYTHON_EXE%" "%~dp0orchestrator.py" --repo "%REPO%" %*
set "RESULT=%ERRORLEVEL%"

popd
exit /b %RESULT%
