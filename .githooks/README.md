# SkullbonezCore Pre-Commit Hooks

Enforces code quality and history evidence automatically on every commit. The
native `commit-msg` hook rejects subject-only, short, or placeholder commit
notes. Every commit must preserve substantive `Why:`, `Ownership:`, `What:`,
`Validation:`, `Baselines/Artifacts:`, and `Review:` sections in that order.
The native pre-commit hook also protects the deterministic physics golden and
runs the physics runtime gate for staged simulation-affecting inputs. A
successful run is cached against the exact relevant Git-index blobs, so
retrying the same commit is immediate.

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
| `commit-msg` | Reject empty or non-substantive commit notes and require all six evidence sections |
| `physics-baseline-transition` | Reject a changed Physics golden or retained bundle without exact content-bound transition evidence |
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

# Inspect the accepted golden and immutable transition bundles without launching the engine
python tools/check_physics_baseline_guard.py --repo .

# Check detailed commit-note positive and negative controls
python tools/check_commit_message.py --self-test
```

## Configuration

- `.githooks/commit-msg` — Mandatory detailed commit-note gate
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

**"physics baseline transition receipt is missing"**
- Do not copy the generated CSV over the golden directly. For an active Physics
  plan, retain the exact old/new launch bundles and invoke the noninteractive,
  content-bound writer:
  `python tools/check_physics_baseline_guard.py --repo . --automated-override-output Debug/physics_regression_varied.csv --candidate-sha256 <sha256> --artifact-manifest <manifest.json>`
- A per-transition owner phrase is not required. Missing or mismatched candidate,
  manifest, executable, DLL, or golden hashes fail closed.

**"pre-commit module is not installed"**
- Physics enforcement still runs through the native hook. Install the optional
  package with `python -m pip install pre-commit` to enable formatting hooks too.
