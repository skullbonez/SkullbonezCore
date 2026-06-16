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

Verify the tools:

```powershell
python --version
py --version
git --version
python -c "import PIL; print(PIL.__version__)"
winget --version
tools\orchestrator.bat doctor
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
CLI package with `python -m pip install --user openai-codex`. If the first real
`run-worker` or `run-verifier` invocation reports an authentication problem, run
`codex login` from the same Windows user account.

If perf analysis fails because `git` is missing, install Git with the `winget` command above. In the same shell, refresh PATH or open a new terminal.

If the format gate fails, run:

```powershell
tools\format_fix.bat
```

Then rerun the targeted validation command you originally intended to run.
