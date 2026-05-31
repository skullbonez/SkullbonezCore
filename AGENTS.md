# Agent Instructions

> Universal contract for any AI agent working on this repository.
> Framework-agnostic: applies to any current or future AI coding agent.

**Do not** submit, force-push, rebase, or rewrite git history.

---

## Before Editing

1. Read this file and `README.md`.
2. Identify your change's impact area: GL, DX11, DX12, physics, scene system, tests, documentation.
3. State which validation command you will run.
4. On a fresh machine or failed tool lookup, read `FIRST_TIME_SETUP.md`.

## After Editing

Run the appropriate validation script from the `tools\` directory:

| Change Type | Command | Runtime |
|-------------|---------|---------|
| Documentation only | `tools\validate_fast.bat` | ~30s |
| Small refactor, no render or physics changes | `tools\validate_fast.bat` | ~30s |
| Shader or render backend | `tools\validate_renderers.bat` | ~90s |
| Physics, collision, or solver | `tools\validate_physics.bat` | ~45s |
| Performance-sensitive hot path | `tools\validate_perf.bat` | ~1 min |
| Broad or uncertain scope | `tools\validate_full.bat` | ~3 min |
| Unsure what to run | `tools\agent_validate.bat` | ~3 min |

### File To Validation Mapping

| Files Changed | Required Script |
|---------------|-----------------|
| `SkullbonezRenderBackend*.cpp/h` | `validate_renderers` |
| `SkullbonezData/shaders/*` | `validate_renderers` |
| `SkullbonezRigidBody*` | `validate_physics` |
| `SkullbonezCollisionResponse*` | `validate_physics` |
| `SkullbonezImpulseSolver*` | `validate_physics` |
| `SkullbonezBoundingSphere*` | `validate_physics` |
| `SkullbonezDynamicsObject*` | `validate_physics` |
| `SkullbonezSpatialGrid*` | `validate_physics` + `validate_perf` |
| `SkullbonezGameModelCollection*` | `validate_renderers` + `validate_perf` |
| `SkullbonezCommon.h` | `validate_full` |
| `SkullbonezRun*` | `validate_full` |
| `SkullbonezWindow*` | `validate_full` |
| `SkullbonezInit*` | `validate_full` |
| Multiple areas or unsure | `validate_full` |
| `Agentic/*`, `*.md`, docs | `validate_fast` |
| `tools/*` | `validate_fast`, then run the changed script |

---

## Rules

- **Never claim success without command output.** Paste the validation output.
- **Never skip validation** unless the user explicitly says to.
- **Kill processes by PID only**; never use `taskkill /IM` or `Stop-Process -Name`.
- **Zero warnings** at `/W4`; no exceptions.
- **Zero DX12 validation errors**; no exceptions.
- **All three renderers** must produce visually identical output, with average pixel diff below 10.
- **Physics must be deterministic**; byte-exact CSV match against baselines.

---

## Danger Zones

Changes to these areas require extra care. Always run the specified validation:

| Area | Risk | Required Validation |
|------|------|---------------------|
| DX12 resource barriers | GPU hang, corruption, CPU/GPU race | `validate_renderers` + verify `dx12_validation.txt` = 0 |
| Renderer backend parity | Visual divergence GL vs DX11 vs DX12 | `validate_renderers` cross-renderer pixel diff |
| Per-frame heap allocations | Performance cliff, stall spikes | `validate_perf` + manual hot path review |
| Visual regression baselines | False passes hide real bugs | `validate_renderers` + intentional baseline update |
| Matrix conventions | Entire scene renders incorrectly | `validate_renderers` across all 3 backends |
| Physics determinism | Butterfly-effect divergence over frames | `validate_physics` byte-exact CSV diff |
| Screenshot timing | Flaky non-deterministic captures | `validate_renderers` |
| Fixed-step simulation behavior | Physics replay not reproducible | `validate_physics` |
| GL/DX coordinate conventions | Upside-down textures, clip-space bugs | `validate_renderers` cross-renderer parity |
| Upload buffer / frame allocator | DX12 CPU overwrites in-flight GPU data | `validate_renderers` + run 3 consecutive times |
| Singleton lifecycle | Use-after-destroy, double-init crash | `validate_full` |
| Broadphase spatial grid | Missed collisions, perf regression | `validate_physics` + `validate_perf` |

---

## Build

```bat
REM Quick build (Profile, for validation):
tools\validate_build.bat Profile

REM Debug build (for physics logging / CDB debugging):
tools\validate_build.bat Debug
```

- **Platform:** x64 only; do not change.
- **Configurations:** Debug, Profile, Release.
- **Toolset:** v143 (VS2022).
- **Warning level:** `/W4`, zero warnings required.

---

## Project Structure

| What | Path |
|------|------|
| Solution file | `SKULLBONEZ_CORE.sln` |
| Source code | `SkullbonezSource/` |
| Shaders | `SkullbonezData/shaders/` |
| Test scenes | `SkullbonezData/scenes/` |
| Suite files | `SkullbonezData/scenes/*.suite` |
| Visual baselines | `TestOutput/baselines/*.png` |
| Physics baselines | `TestOutput/baselines/*.csv` |
| Perf baselines | `TestOutput/baselines/*_perf.json` |
| Validation scripts | `tools/` |
| Agent handoff docs | `Agentic/` |

---

## Agentic Handoff

All agents should also read:

- `Agentic/README.md`, the index for handoff docs, skills, and plans.
- `Agentic/SessionState.md`, the session handoff state.
- `Agentic/Skills/skore-build-pipeline/skill.md`, the detailed pipeline with perf archiving.
