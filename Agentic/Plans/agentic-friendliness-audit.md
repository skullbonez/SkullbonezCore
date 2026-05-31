# Agentic Friendliness Audit â€” SkullbonezCore

**Date:** 2026-05-29  
**Auditor:** Agent (Opus 4.6)  
**Scope:** Evaluate how well the repository supports autonomous AI agent workflows; recommend concrete improvements.

---

## Executive Summary

SkullbonezCore has **unusually strong** agent documentation for a personal project â€” multiple skill files, a session-state handoff protocol, pre-commit hooks, and a 693-line build pipeline specification. However, it suffers from a critical architectural flaw: **all validation logic lives in Markdown prose that the agent must interpret and execute step-by-step**. There are zero runnable scripts. Every pipeline run requires the agent to copy-paste ~30 PowerShell blocks, manage state between them, and make judgment calls at ambiguous branch points.

This makes the pipeline:
- **Fragile** â€” one misinterpreted block or skipped step causes silent failure
- **Expensive** â€” ~40 tool calls per pipeline run, all token-consuming
- **Non-portable** â€” only one agent runtime can use it; no CI, no other agent frameworks
- **Slow** â€” sequential interpretation adds ~2 minutes of agent overhead per run

**Verdict: B+ documentation, D- automation.** The knowledge is there; it just isn't executable.

---

## Current State Assessment

### What Exists

| Asset | Location | Role |
|-------|----------|------|
| Agent guide | `agents.md` (root) | agent-specific operational reference |
| Agent instructions | `AGENTS.md` | an AI agent custom instructions |
| Build pipeline skill | `Agentic/Skills/skore-build-pipeline/skill.md` | 693-line step-by-step validation spec |
| Other skill files | `Agentic/Skills/skore-*/skill.md` | render-test, build, cdb-debug, launch, cpu-profiler. The old branch-and-snatch skill was deprecated by baseline perf comparison. |
| Session state | `Agentic/SessionState.md` | Cross-session handoff document |
| Pre-commit hooks | `.githooks/` + `.pre-commit-config.yaml` | clang-format, header consistency, brace check, line endings, whitespace |
| Test scenes | `SkullbonezData/scenes/*.scene` | 35 scenes + 3 suite files |
| Visual baselines | `TestOutput/baselines/` | PNG reference images per renderer |
| Physics baselines | `TestOutput/baselines/*.csv` | Deterministic physics regression CSVs |
| Perf archives | `TestOutput/NNN_<hash>/` | Historical perf JSON per commit |

### What's Missing

| Gap | Impact |
|-----|--------|
| **No runnable validation scripts** | Agent must interpret markdown; other tools can't validate |
| **No tiered validation** | Every change runs the full 12-step pipeline (~3 min) regardless of scope |
| **No universal agent contract** | `agents.md` is agent-specific; other agents (other agent runtimes) get `AGENTS.md` only |
| **No CI pipeline** | No GitHub Actions; validation only runs when an agent happens to be present |
| **No "danger zones" documentation** | Agent has no explicit "be extra careful here" signals |
| **No structured exit codes** | Validation steps use Write-Host; no machine-readable pass/fail |
| **No `CODEOWNERS`** | No file-to-risk mapping |
| **Instructions fragmented** | Agent must read 4+ files to understand the full contract |

---

## Recommendations

### 1. Create a Universal `AGENTS.md` (Top-Level Agent Contract)

**Priority:** ðŸ”´ Critical  
**Effort:** Low (1 hour)

The existing `agents.md` is good but agent-specific (references structured user-input tool, session state loading, etc.). Create a top-level `AGENTS.md` that is **agent-framework-agnostic** â€” the contract that ANY agent (any current or future AI coding agent) must follow.

**Proposed content:**

```markdown
# Agent Instructions

Do not submit, force-push, rebase, or rewrite history.

## Before Editing

1. Read this file and `README.md`.
2. Identify impact area: GL, DX11, DX12, physics, scene system, tests.
3. State which validation command(s) you will run.

## After Editing

1. `tools\validate_fast.bat` â€” build + format check (always)
2. `tools\validate_renderers.bat` â€” if shader/render/backend changes
3. `tools\validate_physics.bat` â€” if physics/collision/solver changes
4. `tools\validate_perf.bat` â€” if performance-sensitive changes
5. `tools\validate_full.bat` â€” if broad or uncertain scope

## Rules

- Never claim success without command output.
- Never skip validation unless explicitly told.
- Kill processes by PID only â€” never by name.
- Zero warnings at /W4 â€” no exceptions.
- Zero DX12 validation errors â€” no exceptions.
- All three renderers must produce visually identical output.

## Danger Zones

Changes to these areas require extra care and full validation:

| Area | Risk | Required Validation |
|------|------|---------------------|
| DX12 resource barriers | GPU hang, corruption, race conditions | `validate_renderers` + DX12 InfoQueue clean |
| Renderer backend parity | Visual divergence across GL/DX11/DX12 | `validate_renderers` (cross-renderer pixel diff) |
| Per-frame allocations | Performance cliff, GC-style stalls | `validate_perf` + verify no heap alloc in hot path |
| Visual regression baselines | False passes hide real regressions | `validate_renderers` (always update baselines intentionally) |
| Matrix convention changes | Entire scene renders wrong | `validate_renderers` (all 3) |
| Physics determinism | Butterfly-effect regression across frames | `validate_physics` (byte-exact CSV diff) |
| Screenshot timing | Flaky baselines, non-deterministic captures | `validate_renderers` (verify frame count before capture) |
| Fixed-step behavior | Physics replay non-reproducible | `validate_physics` |
| GL/DX coordinate conventions | Y-flip, UV-flip, clip-space bugs | `validate_renderers` (cross-renderer parity) |
| Upload buffer / frame allocator | GPU race conditions (DX12) | `validate_renderers` + run 3+ back-to-back |
| Singleton lifecycle | Use-after-destroy, double-init | `validate_full` |
```

**Relationship to existing files:**
- `agents.md` â†’ rename to `Agentic/agent-guide.md` (agent-specific addendum)
- `AGENTS.md` â†’ keep as-is (All agents read this first)
- `AGENTS.md` â†’ new universal contract (top-level, all agents)

---

### 2. Create `tools\agent_validate.bat` â€” One-Command Full Sanity Loop

**Priority:** ðŸ”´ Critical  
**Effort:** Medium (2â€“3 hours)

Wrap the entire pipeline into a single script with structured exit codes. The agent should never need to "figure out" validation.

**Proposed implementation:**

```bat
@echo off
setlocal enabledelayedexpansion
REM â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
REM  agent_validate.bat â€” Full validation pipeline for SkullbonezCore
REM  Exit 0 = all pass. Non-zero = failure (code indicates which step).
REM â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•

set REPO=%~dp0..
set EXITCODE=0

REM â”€â”€ Step 1: Format Check â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
echo [1/6] Checking formatting...
call "%REPO%\tools\validate_format.bat"
if errorlevel 1 (echo FAIL: Formatting & exit /b 1)

REM â”€â”€ Step 2: Build Profile x64 â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
echo [2/6] Building Profile x64...
call "%REPO%\tools\validate_build.bat" Profile
if errorlevel 1 (echo FAIL: Build & exit /b 2)

REM â”€â”€ Step 3: GL Suite â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
echo [3/6] Running GL render suite...
"%REPO%\Profile\SKULLBONEZ_CORE.exe" --suite SkullbonezData/scenes/render_tests.suite
if errorlevel 1 (echo FAIL: GL suite & exit /b 3)

REM â”€â”€ Step 4: DX11 Suite â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
echo [4/6] Running DX11 render suite...
"%REPO%\Profile\SKULLBONEZ_CORE.exe" --renderer dx11 --suite SkullbonezData/scenes/render_tests.suite
if errorlevel 1 (echo FAIL: DX11 suite & exit /b 4)

REM â”€â”€ Step 5: DX12 Suite â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
echo [5/6] Running DX12 render suite...
"%REPO%\Profile\SKULLBONEZ_CORE.exe" --renderer dx12 --suite SkullbonezData/scenes/render_tests.suite
if errorlevel 1 (echo FAIL: DX12 suite & exit /b 5)

REM â”€â”€ Step 6: DX12 Validation Check â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
echo [6/6] Checking DX12 validation...
call "%REPO%\tools\check_dx12_validation.bat"
if errorlevel 1 (echo FAIL: DX12 validation errors & exit /b 6)

echo.
echo â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
echo   ALL VALIDATION PASSED
echo â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
exit /b 0
```

**Key design principles:**
- One command, deterministic outcome
- Structured exit codes (agent can map code â†’ failure step)
- Stdout is human-readable AND machine-parseable
- No agent interpretation required

---

### 3. Create Tiered Validation Scripts in `tools\`

**Priority:** ðŸŸ¡ High  
**Effort:** Medium (3â€“4 hours for all scripts)

| Script | When to Use | What It Does | Est. Runtime |
|--------|-------------|--------------|--------------|
| `validate_fast.bat` | Every change | Format check + build Profile x64 | ~30s |
| `validate_renderers.bat` | Shader/render/backend changes | Build + run GL/DX11/DX12 suite + cross-parity | ~90s |
| `validate_physics.bat` | Physics/collision/solver changes | Build Debug + run regression scenes + CSV diff | ~60s |
| `validate_perf.bat` | Performance-sensitive changes | Build Profile + run perf scene + generate JSON | ~45s |
| `validate_full.bat` | Broad or uncertain scope | All of the above + baseline update | ~3 min |

**Tiering rules for agents:**

```
Changed files â†’ Required validation:
  SkullbonezRenderBackend*.cpp    â†’ validate_renderers
  SkullbonezData/shaders/*        â†’ validate_renderers
  SkullbonezRigidBody*            â†’ validate_physics
  SkullbonezCollisionResponse*    â†’ validate_physics
  SkullbonezImpulseSolver*        â†’ validate_physics
  SkullbonezSpatialGrid*          â†’ validate_physics + validate_perf
  SkullbonezGameModelCollection*  â†’ validate_renderers + validate_perf
  SkullbonezCommon.h              â†’ validate_full
  SkullbonezRun*                  â†’ validate_full
  SkullbonezWindow*               â†’ validate_full
  Multiple areas                  â†’ validate_full
  Documentation only              â†’ validate_fast
```

This is **dramatically** better than the current approach where the agent asks the user "which tests should the pipeline run?" via the available user-input mechanism â€” the file-to-validation mapping should be deterministic and scripted.

---

### 4. Document Danger Zones Explicitly

**Priority:** ðŸŸ¡ High  
**Effort:** Low (30 minutes)

The existing `agents.md` has a "Key Gotchas" section but it's buried and informational rather than prescriptive. Elevate this to a first-class section in `AGENTS.md` with explicit validation requirements per danger zone (shown in Recommendation 1 above).

Additionally, consider adding inline file-level markers:

```cpp
// âš ï¸ DANGER ZONE: DX12 resource barriers â€” changes here require validate_renderers
//    and manual inspection of dx12_validation.txt. Frame allocator races are subtle.
```

---

### 5. Additional Improvements (Lower Priority)

#### 5a. Add GitHub Actions CI

**Priority:** ðŸŸ¢ Medium  
**Effort:** High (requires self-hosted runner with GPU)

Even a minimal workflow that runs `validate_fast.bat` on PR would catch agent mistakes before merge. The full tri-renderer validation requires GPU hardware (self-hosted runner), but build+format can run anywhere.

```yaml
# .github/workflows/validate.yml
on: [pull_request]
jobs:
  build:
    runs-on: self-hosted  # Windows, GPU
    steps:
      - uses: actions/checkout@v4
      - run: tools\validate_full.bat
```

#### 5b. Consolidate Agent Instructions

**Priority:** ðŸŸ¢ Medium  
**Effort:** Low

Current instruction surface:
1. `AGENTS.md` (proposed universal contract)
2. `AGENTS.md` (agent-specific, auto-loaded)
3. `agents.md` (current Agent guide â€” rename to `Agentic/agent-guide.md`)
4. `Agentic/SessionState.md` (handoff state)
5. 7 skill files in `Agentic/Skills/`

Proposed hierarchy:
```
AGENTS.md                          â† Universal contract (any agent)
â”œâ”€â”€ tools\validate_*.bat           â† Executable validation (any agent)
â”œâ”€â”€ AGENTS.md â† Agent auto-load (extends AGENTS.md)
â””â”€â”€ Agentic/                       â† agent-specific extensions
    â”œâ”€â”€ SessionState.md
    â”œâ”€â”€ Skills/*.md
    â””â”€â”€ agent-guide.md          â† agent-specific operational details
```

Rule: If an instruction depends on a particular agent runtime, it belongs in `Agentic/`, not at the top level.

#### 5c. Add Machine-Readable Validation Output

**Priority:** ðŸŸ¢ Medium  
**Effort:** Medium

Have validation scripts write a `validation_result.json`:

```json
{
  "timestamp": "2026-05-29T12:34:56Z",
  "commit": "abc1234",
  "steps": {
    "format": { "status": "pass", "duration_ms": 2100 },
    "build": { "status": "pass", "duration_ms": 18500, "warnings": 0 },
    "gl_suite": { "status": "pass", "duration_ms": 22000 },
    "dx11_suite": { "status": "pass", "duration_ms": 24000 },
    "dx12_suite": { "status": "pass", "duration_ms": 26000 },
    "dx12_validation": { "status": "pass", "errors": 0 }
  },
  "overall": "pass"
}
```

Agents can parse this without interpreting human text.

#### 5d. Add `CODEOWNERS`-Style Risk Map

**Priority:** ðŸŸ¢ Low  
**Effort:** Low

```
# .github/RISK_MAP (custom, not GitHub's CODEOWNERS)
# Format: glob  risk_level  required_validation

SkullbonezSource/SkullbonezRenderBackendDX12*  HIGH    validate_renderers
SkullbonezSource/SkullbonezRenderBackendDX11*  HIGH    validate_renderers
SkullbonezSource/SkullbonezRenderBackendGL*    HIGH    validate_renderers
SkullbonezData/shaders/*                       HIGH    validate_renderers
SkullbonezSource/SkullbonezImpulseSolver*      HIGH    validate_physics
SkullbonezSource/SkullbonezCollisionResponse*  HIGH    validate_physics
SkullbonezSource/SkullbonezSpatialGrid*        HIGH    validate_physics validate_perf
SkullbonezSource/SkullbonezRun*                HIGH    validate_full
SkullbonezSource/SkullbonezCommon.h            HIGH    validate_full
SkullbonezSource/SkullbonezWindow*             MEDIUM  validate_renderers
SkullbonezSource/SkullbonezTerrain*            MEDIUM  validate_renderers
SkullbonezSource/SkullbonezText*               LOW     validate_fast
Agentic/*                                      NONE    validate_fast
```

---

## Scoring

| Dimension | Current | After Recommendations | Notes |
|-----------|---------|----------------------|-------|
| **Discoverability** â€” Can an agent find the rules? | 7/10 | 9/10 | `AGENTS.md` at root is universally discoverable |
| **Executability** â€” Can an agent run validation without interpretation? | 2/10 | 9/10 | Scripts replace markdown copy-paste |
| **Tiering** â€” Can the agent validate proportionally to risk? | 3/10 | 9/10 | Five targeted scripts vs one monolithic pipeline |
| **Safety** â€” Does the agent know where the landmines are? | 5/10 | 8/10 | Explicit danger zones with required validation |
| **Portability** â€” Can other agents use this? | 3/10 | 8/10 | Universal contract + bat scripts = any agent works |
| **CI integration** â€” Is validation enforced automatically? | 0/10 | 7/10 | GitHub Actions (requires self-hosted GPU runner) |

**Overall: 3.3/10 â†’ 8.3/10** with all recommendations implemented.

---

## Implementation Order

| Step | What | Effort | Unblocks |
|------|------|--------|----------|
| 1 | Create `tools\validate_fast.bat` (format + build) | 30 min | Everything else |
| 2 | Create `tools\validate_renderers.bat` (tri-renderer suite) | 1 hour | Renderer changes |
| 3 | Create `tools\validate_physics.bat` (regression CSVs) | 1 hour | Physics changes |
| 4 | Create `tools\validate_perf.bat` (perf JSON) | 45 min | Perf changes |
| 5 | Create `tools\validate_full.bat` (orchestrates all) | 30 min | Broad changes |
| 6 | Create `AGENTS.md` (universal contract) | 1 hour | All other agents |
| 7 | Rename `agents.md` â†’ `Agentic/agent-guide.md` | 5 min | Clarity |
| 8 | Add danger zones to `AGENTS.md` | 30 min | Agent paranoia |
| 9 | Add `validation_result.json` output to scripts | 1 hour | Machine-readable results |
| 10 | Add GitHub Actions workflow | 2 hours | CI enforcement |

**Total estimated effort: ~9 hours** for a transformative improvement in agent reliability.

---

## Conclusion

The SkullbonezCore repo is **documentation-rich but automation-poor**. The pipeline knowledge exists â€” it's just trapped in Markdown that only one specific agent framework can interpret, and even then it does so unreliably (40+ tool calls, sequential interpretation, judgment-call branch points).

The single highest-value change is **extracting the validation logic into runnable `.bat` scripts**. This:
1. Makes validation agent-framework-agnostic
2. Eliminates interpretation errors
3. Enables CI integration
4. Reduces pipeline runs from ~40 tool calls to 1
5. Gives structured exit codes instead of prose output

The agent shouldn't need to think. It should run one command and read the exit code.
