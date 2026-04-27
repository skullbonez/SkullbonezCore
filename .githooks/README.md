# SkullbonezCore Pre-Commit Hooks

Enforces code quality standards automatically on every commit.

## Setup

```bash
pip install pre-commit
pre-commit install
```

## Hooks

| Hook | Purpose |
|------|---------|
| `trailing-whitespace` | Remove trailing spaces |
| `end-of-file-fixer` | Ensure files end with newline |
| `clang-format` | Format C++ code (in-place fix with `-i`) |
| `header-consistency` | Verify no ASCII art headers in source files |
| `no-braceless-multiline` | Catch multi-line `if/for/while` without braces |

## Running Manually

```bash
# Check all staged files
pre-commit run --all-files

# Check specific file
pre-commit run --files SkullbonezSource/SkullbonezRun.cpp

# Skip hooks temporarily
git commit --no-verify
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

**"pre-commit: command not found"**
- Ensure pip install worked: `python -m pip install pre-commit`
