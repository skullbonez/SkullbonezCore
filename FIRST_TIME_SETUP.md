# First-Time Setup

This repository expects a Windows x64 development machine with Visual Studio
C++ tools, Python, and Pillow available to the validation scripts.

For agent workflow rules, read `AGENTS.md` first, then `Agentic/README.md`.

## Initialize Pinned Dependencies

After cloning, initialize the exact Dear ImGui and Tracy revisions recorded by
the repository. This command checks out the committed gitlinks; it does not
resolve a moving latest version:

```powershell
git submodule update --init --recursive
git submodule status
```

The expected pins, licenses, compiled source list, and update procedure are in
`ThirdPtySource/README.md` and `THIRD_PARTY_NOTICES.md`.

## Install Tools

Install Visual Studio with the Desktop development with C++ workload, LLVM
tools, and the C++ AddressSanitizer component. The validation scripts discover
MSBuild, clang-format, clang-tidy, and clang-query from the installed tools, so
Visual Studio Professional is not required as long as those C++ components are
installed.

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

Enable the repository-native commit hook. This makes physics-golden approval
and deterministic runtime comparison mandatory for affected staged commits;
it does not require an extra Python package:

```powershell
git config --local core.hooksPath .githooks
```

Install OpenCppCoverage for the Debug unit-coverage lane. Its installer is
machine-scoped, so omit `--scope user`:

```powershell
winget install --id OpenCppCoverage.OpenCppCoverage --exact --accept-package-agreements --accept-source-agreements
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
& 'C:\Program Files\OpenCppCoverage\OpenCppCoverage.exe' --help
```

If you installed optional CodeGraph, verify the local index from the repository
root:

```powershell
codegraph status .
```

## Validation Scripts

Validation scripts are formal pre-commit/PR gates, or explicit setup smoke
checks when requested. Do not run full validation during setup, ordinary
implementation, or routine PR preparation. Use the cumulative focused gates in
`AGENTS.md`. Only when an entire implementation plan is complete, run:

```powershell
tools\agent_validate.bat --plan-completion
```

This delegates once to `tools\validate_full.bat --plan-completion`. After the
required Debug build, deterministic physics is the first runtime oracle; the
remaining CPU, automation, and DX12 lanes follow. Performance validation
remains a separate targeted gate.

For targeted pre-commit/PR checks:

```powershell
tools\validate_fast.bat
tools\validate_dx12_renderer.bat
tools\validate_physics.bat
tools\validate_perf.bat
tools\validate_native_diagnostics.bat
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

If native diagnostics reports a missing `clang_rt.asan_dynamic-x86_64.dll`,
add the C++ AddressSanitizer component through Visual Studio Installer. The
diagnostic lane writes only under `TestOutput\validation\native_diagnostics`.

If `py` or `python` opens the Microsoft Store or says Python was not found, install Python with the `winget` command above and refresh PATH in the current shell.

If Pillow is missing, renderer screenshot checks will fail with `ModuleNotFoundError: No module named 'PIL'`. Run `python -m pip install Pillow`.

If `tools\validate_coverage.bat` reports that OpenCppCoverage is missing, run
the winget command above. The script also honors an `OPENCPPCOVERAGE_EXE`
environment variable pointing to a non-standard installation.

If perf analysis fails because `git` is missing, install Git with the `winget` command above. In the same shell, refresh PATH or open a new terminal.

If the format gate fails, run:

```powershell
tools\format_fix.bat
```

Then rerun the targeted validation command you originally intended to run.
