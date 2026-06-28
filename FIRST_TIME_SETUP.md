# First-Time Setup

This repository expects a Windows x64 development machine with Visual Studio
C++ tools, Python, and Pillow available to the validation scripts. Codex
orchestration also requires the official Codex CLI Python package so the
orchestrator can spawn worker and verifier agents through `codex exec`.

For agent workflow rules, read `AGENTS.md` first, then `Agentic/README.md`.

## Install Tools

Install Visual Studio with the Desktop development with C++ workload and LLVM tools. The validation scripts discover MSBuild and clang-format through `vswhere`, so Visual Studio 2022 Professional is not required as long as the C++ and LLVM components are installed.

Install Git:

```powershell
winget install --id Git.Git --exact --scope user --accept-package-agreements --accept-source-agreements
```

Install Python:

```powershell
winget install --id Python.Python.3.12 --exact --scope user --accept-package-agreements --accept-source-agreements
```

Refresh the current shell's PATH after installing Python:

```powershell
$env:PATH = [System.Environment]::GetEnvironmentVariable('PATH','User') + ';' + [System.Environment]::GetEnvironmentVariable('PATH','Machine')
```

Install the Python image dependency used by DX12 screenshot checks:

```powershell
python -m pip install Pillow
```

Install the Codex CLI package used by the roadmap orchestrator:

```powershell
python -m pip install --user openai-codex
```

Optional: install CodeGraph for local code-intelligence lookups. CodeGraph is
not required for builds, validation, or agent startup; it helps agents query
symbols, callers, callees, and impact before opening large source files.

```powershell
$installer = Join-Path $env:TEMP 'codegraph-install.ps1'
Invoke-WebRequest -Uri 'https://raw.githubusercontent.com/colbymchenry/codegraph/main/install.ps1' -OutFile $installer
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $installer
codegraph install --target codex --location global --yes
codegraph telemetry off
codegraph init .
```

The repository ignores the generated `.codegraph/` directory. After large source
changes, refresh the local index with:

```powershell
codegraph sync .
```

Use `codegraph index .` instead when a full rebuild is needed. If CodeGraph is
not installed or its index is stale, continue with the normal `rg` and targeted
file-read workflow.

Verify the tools:

```powershell
python --version
py --version
git --version
python -c "import PIL; print(PIL.__version__)"
winget --version
tools\orchestrator.bat doctor
```

If you installed optional CodeGraph, verify the local index from the repository
root:

```powershell
codegraph status .
```

## Validation Scripts

Validation scripts are formal pre-commit/PR gates, or explicit setup smoke
checks when requested. Do not run full validation during ordinary implementation
work. When verifying a fresh setup or PR-bound work whose scope is truly
uncertain, run from the repository root:

```powershell
tools\agent_validate.bat
```

This delegates to `tools\validate_full.bat`, which runs renderer validation,
physics determinism validation, and performance validation.

For targeted pre-commit/PR checks:

```powershell
tools\validate_fast.bat
tools\validate_dx12_renderer.bat
tools\validate_physics.bat
tools\validate_perf.bat
```

The scripts now discover local tool locations through helper scripts:

| Helper | Purpose |
|--------|---------|
| `tools\find_msbuild.bat` | Locates MSBuild through Visual Studio Installer `vswhere` |
| `tools\find_clang_format.bat` | Locates clang-format from the installed Visual Studio LLVM tools or PATH |
| `tools\find_git.bat` | Locates Git from PATH or standard Git for Windows install locations |
| `tools\find_python.bat` | Locates Python from the Codex bundled runtime, `py.exe`, or `python.exe` |

## Common First-Run Issues

If `clang-format` is reported missing, install the Visual Studio LLVM tools component. The scripts no longer assume the old fixed path `C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Tools\Llvm\x64\bin\clang-format.exe`.

If `py` or `python` opens the Microsoft Store or says Python was not found, install Python with the `winget` command above and refresh PATH in the current shell.

If Pillow is missing, renderer screenshot checks will fail with `ModuleNotFoundError: No module named 'PIL'`. Run `python -m pip install Pillow`.

If `tools\orchestrator.bat doctor` reports that Codex is missing, install the
CLI package with `python -m pip install --user openai-codex`. The doctor also
runs a small `codex exec` smoke test with the configured orchestrator sandbox.
If that smoke test reports an authentication problem, run `codex login` from
the same Windows user account and rerun the doctor command. On the current
Windows Codex CLI build, the orchestrator policy uses `danger-full-access`
because narrower CLI sandboxes fail shell spawn setup; verifier runs are still
checked for tracked worktree changes before and after execution.

If perf analysis fails because `git` is missing, install Git with the `winget` command above. In the same shell, refresh PATH or open a new terminal.

If the format gate fails, run:

```powershell
tools\format_fix.bat
```

Then rerun the targeted validation command you originally intended to run.
