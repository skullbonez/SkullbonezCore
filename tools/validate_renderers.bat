@rem
@rem File: tools/validate_renderers.bat
@rem Purpose:
@rem   Compatibility alias for the retired tri-renderer validation command.
@rem
@rem Summary:
@rem   DX12 is now the only runtime renderer. Old habits and older plans may
@rem   still mention validate_renderers.bat, so this wrapper keeps those commands
@rem   productive while routing them to the DX12 screenshot and InfoQueue gate.
@rem
@rem Glossary:
@rem   InfoQueue: DX12 debug-message stream checked by validation; zero errors
@rem   are allowed.
@rem   Validation gate: Repository script that proves a class of changes before
@rem   commit or PR.
@rem
@rem Invariants:
@rem   - This wrapper must not launch retired GL or DX11 code paths.
@rem
@rem Related:
@rem   - AGENTS.md
@rem
@rem
@echo off
setlocal
echo.
echo ========================================
echo   VALIDATE_RENDERERS - Retired Alias
echo ========================================
echo.
echo OpenGL and DX11 renderer validation has been retired.
echo Running the DX12-only renderer gate instead:
echo   tools\validate_dx12_renderer.bat
echo.
call "%~dp0validate_dx12_renderer.bat" %*
exit /b %ERRORLEVEL%
