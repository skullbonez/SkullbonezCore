# SkullbonezCore Pre-Commit Hooks

Enforces code quality standards automatically on every commit. The native hook
always protects the deterministic physics golden and runs the physics runtime
gate for staged simulation-affecting inputs. A successful run is cached against
the exact relevant Git-index blobs, so retrying the same commit is immediate.

## Setup

```powershell
git config --local core.hooksPath .githooks
```

The Python `pre-commit` package is optional. When installed, the native hook
delegates to it so the formatting hooks below run as well:

```powershell
python -m pip install pre-commit
```

## Hooks

| Hook | Purpose |
|------|---------|
| `physics-baseline-approval` | Reject a changed golden or approval record without an exact owner approval receipt |
| `physics-runtime-commit-gate` | Run byte-exact physics validation for the exact staged simulation inputs |
| `trim-trailing-whitespace` | Remove trailing spaces |
| `fix-crlf` | Normalize touched C++ files to the repository line endings |
| `clang-format` | Format C++ code (in-place fix with `-i`) |
| `header-consistency` | Verify no ASCII art headers in source files |
| `no-braceless-multiline` | Catch multi-line `if/for/while` without braces |

## Running Manually

```bash
# Check all staged files when the optional package is installed
python -m pre_commit run --all-files

# Check specific file
python -m pre_commit run --files SkullbonezSource/SkullbonezRun.cpp

# Inspect the owner-approved golden without launching the engine
python tools/check_physics_baseline_guard.py --repo .
```

## Configuration

- `.pre-commit-config.yaml` — Hook definitions
- `.githooks/check-headers.py` — Header format validator
- `.githooks/check-braces.py` — Brace rule enforcer
- `.clang-format` — Code formatting rules

## Common Issues

**"clang-format not found"**
- Install LLVM: `choco install llvm` or add VS2022 LLVM tools to PATH

**Hooks fail on staged files**
- Run `pre-commit run --all-files` to auto-fix format issues
- Re-stage: `git add .`
- Retry commit

**"owner approval is required"**
- Do not copy the generated CSV over the golden directly. The repository owner
  reviews the generated result and runs this command in an interactive terminal:
  `python tools/check_physics_baseline_guard.py --repo . --approve-output Debug/physics_regression_varied.csv`

**"pre-commit module is not installed"**
- Physics enforcement still runs through the native hook. Install the optional
  package with `python -m pip install pre-commit` to enable formatting hooks too.
