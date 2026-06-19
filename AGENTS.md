# Agent Instructions

> Universal contract for any AI agent working on this repository.
> Framework-agnostic: applies to any current or future AI coding agent.

**Do not** force-push, rebase, or rewrite git history. Feature-branch commits
and normal pushes are allowed without asking. Agents may submit or merge PRs only
when explicitly requested by the user. Direct commits or pushes on `main` still
require explicit user confirmation.

---

## Agent Startup Contract

Before editing, all agents must read:

1. `AGENTS.md`.
2. `README.md`.
3. `Agentic/README.md`.
4. `Agentic/SessionState.md`.
5. Run `git status --short --branch` and treat any pre-existing dirty files as
   user-owned.

If this is a fresh machine or a required tool lookup fails, read
`FIRST_TIME_SETUP.md`. Load deeper skills, plans, audits, reports, or reference
files only when the current task calls for them.

---

## Plan Implementation Mode

When implementing work from `Agentic/Plans`, use orchestration as the default
mode. The orchestrator owns queue selection, branch/state policy,
worker/verifier handoff, required validation selection, report creation, and
plan archival. Reading, updating, or drafting a plan is normal documentation
work; turning a plan into repository changes should follow the orchestrator
workflow unless the user explicitly asks to bypass it.

---

## Before Editing

1. Follow the Agent Startup Contract above.
2. Identify your change's impact area: DX12, physics, scene system, tests, documentation.
3. State whether validation is required now. For normal implementation work, do not run repository validation scripts while iterating; name the targeted validation command to defer until PR-bound commit/PR prep. For documentation-only changes, state that no validation is required.
4. If unrelated dirty files are present, leave them alone. Do not overwrite,
   revert, stage, or format user-owned changes unless explicitly requested.

## After Editing

Do not run validation scripts automatically after every edit. Formal repository
validation runs only as a pre-commit/PR gate, or when the user explicitly asks
for it. When preparing PR-bound work, choose the smallest script from `tools\`
that matches the fix. The default broad PR gate is intentionally limited to two
runtime launches: one DX12 render suite and one core physics scene. Use deep,
perf, UI, and stress validation only when the change actually needs them:

| Change Type | Pre-Commit/PR Command | Runtime |
|-------------|---------|---------|
| Documentation only | No validation required | N/A |
| Small refactor, no render or physics changes | `tools\validate_fast.bat` | ~30s |
| Shader or render backend | `tools\validate_dx12_renderer.bat` | ~2 min |
| DX12 renderer validation tooling | `tools\validate_fast.bat`, then `tools\validate_dx12_renderer.bat` | ~2 min |
| Physics, collision, solver, or rigid body changes | `tools\validate_physics.bat` | 1 exe launch |
| Broad physics baseline, bullet sweep, or SkullScope diagnostics | `tools\validate_physics_deep.bat` | ~45s+ |
| Performance-sensitive hot path | `tools\validate_perf.bat` | ~1 min |
| Broad or uncertain scope | `tools\validate_full.bat` | 2 exe launches |
| Unsure what to run at the PR gate | `tools\agent_validate.bat` | 2 exe launches |

Profiling marker or platform-profiler changes must also run:

```bat
Profile\SKULLBONEZ_CORE.exe --platform-profiler-markers
```

### File To Validation Mapping

| Files Changed | Required Pre-Commit/PR Script |
|---------------|-----------------|
| `SkullbonezRenderBackend*.cpp/h` | `validate_dx12_renderer` |
| `SkullbonezData/shaders/*` | `validate_dx12_renderer` |
| `SkullbonezRigidBody*` | `validate_physics` |
| `SkullbonezGameModelCollection*` physics solver changes | `validate_physics` |
| `SkullbonezBoundingSphere*` | `validate_physics` |
| `SkullbonezDynamicsObject*` | `validate_physics` |
| `SkullbonezWorldEnvironment*` | `validate_physics` |
| `SkullbonezSpatialGrid*` | `validate_physics` + `validate_perf` |
| `SkullbonezGameModelCollection*` | `validate_dx12_renderer` + `validate_perf` |
| `SkullbonezConfig*`, `SkullbonezData/engine.cfg` physics defaults such as gravity, fluid, drag, friction, sleep, solver, or broadphase values | `validate_physics` |
| `TestOutput/baselines/physics_regression_solver.csv` | `validate_physics` |
| Other physics CSV baselines or `TestOutput/baselines/physics_query*.json` | `validate_physics_deep` |
| `SkullbonezCommon.h` | `validate_full` |
| `SkullbonezRun*` | `validate_full` |
| `SkullbonezWindow*` | `validate_full` |
| `SkullbonezInit*` | `validate_full` |
| Multiple areas or unsure | `validate_full` |
| `Agentic/*`, `*.md`, docs | No validation required when documentation-only |
| `tools/*` | `validate_fast`, then run the changed script |

---

## Rules

- **Repository validation scripts are PR/commit gates.** Do not run `tools\validate_*` merely as you go. During iteration, use targeted builds, launches, focused tests, or inspections only when they answer a specific question about the fix.
- **Renderer validation must fail fast.** `tools\validate_dx12_renderer.bat` builds `Profile` first and must stop before launching DX12 if compilation fails. Renderer launches in that script use PID-scoped timeouts, then `tools\check_dx12_baselines.py` handles image comparison artifacts.
- **DX12-only validation is the production safety net.** `tools\validate_dx12_renderer.bat` builds `Profile`, launches only DX12, checks `dx12_validation.txt`, and compares captures against committed DX12 baselines.
- **Never claim validation success without command output.** Paste the validation output when validation is required.
- **Never skip required pre-commit/PR validation** for code, tool, scene, shader, baseline, or runtime behavior changes unless the user explicitly says to.
- **Documentation-only changes require no validation.** Do not run `validate_fast` for prose-only edits.
- **Physics baseline refreshes require a final physics gate.** Regenerate CSV or SkullScope baselines only from the final Debug executable, scene files, and config that will be committed, then rerun the matching gate after the baseline files are updated: `tools\validate_physics.bat` for the core solver baseline, or `tools\validate_physics_deep.bat` for bullet sweep, shooting, known-issue, or SkullScope baselines. A copied physics artifact is not trustworthy until the gate compares it byte-exactly against the committed baseline.
- **`tools\update_baselines.bat` is visual/perf only.** Do not use it for physics CSV or SkullScope baselines unless the script explicitly grows that support; use the Debug physics artifacts generated by the validation commands and rerun the matching physics gate.
- **Time all user-requested work.** Record elapsed wall-clock time for every task from the start of work to the final response. Report the time taken in the final answer, and call out timings for substantial sub-runs such as builds, validation scripts, game launches, SkullScope trace generation, or long investigations.
- **Protect dirty worktrees.** Run `git status --short --branch` before edits and before commits. Treat pre-existing changes as user-owned. Never use `git reset --hard`, destructive `git clean`, checkout/discard commands, or broad formatter runs that touch unrelated files unless the user explicitly requested that operation.
- **Keep physics debug data cheap for model analysis.** Use SkullScope. Do not paste or ingest whole physics CSV, NDJSON, or SQLite diagnostic files unless the user explicitly requests raw logs. When available, prefer the queryable diagnostics workflow in `Agentic/Reference/physics-query-reference.md` and load `Agentic/Skills/skore-skullscope/skill.md` for the compact runbook: generate a deterministic trace with `--physics-diag`, run `tools\physics_query.bat <trace> summary` and `tools\physics_query.bat <trace> events`, or expand a pre-baked question with `tools\physics_query.bat <trace> questions <name>`, then ask focused frame/body/contact/island queries. The deterministic physics CSV remains the byte-exact validation artifact.
- **Report SkullScope query cost.** Whenever SkullScope is used, print the exact trace command, every `tools\physics_query.bat` query, and the data-size accounting in the final answer or handoff. Report raw artifact sizes separately from model-ingested data: trace NDJSON bytes and SQLite cache bytes are on-disk artifact sizes, while GPT-read size is only the bounded query output text actually exposed to the model. Include per-query output size and the total GPT-read characters/bytes. If output was truncated by the shell/app, mark it as truncated and rerun a narrower query before drawing conclusions.
- **Run builds, game launches, and validation/test scripts in a visible console window when available.** Use `cmd.exe` or PowerShell so the user can watch compile and test progress, and mirror output to a log when possible so the final response can quote the result. In headless or cloud contexts where a visible console is impossible, state that limitation, run through the available shell, mirror output to a log when practical, and quote the key result lines plus the log path.
- **Kill processes by PID only**; never use `taskkill /IM` or `Stop-Process -Name`.
- **Zero warnings** at `/W4`; no exceptions.
- **Zero DX12 validation errors**; no exceptions.
- **DX12 is the only runtime renderer.** OpenGL and DX11 final parity evidence has been archived; do not add new runtime dependencies on those backends.
- **Renderer regression** is measured with DX12 screenshots plus zero DX12 validation errors, not GL/DX11 parity.
- **Physics must be deterministic**; byte-exact CSV match against baselines.

---

## Reviews

When asked for a review, prioritize findings over summary. List bugs, behavior
regressions, missing tests, validation gaps, baseline mistakes, physics
determinism risks, DX12 validation risks, and hot-path allocation concerns first,
ordered by severity with file and line references. Keep summaries secondary.
If no issues are found, say so clearly and name any residual validation or test
risk.

---

## Commit Notes

When committing, write commit notes that are useful future handoff material, not a terse log line.

- Use a short, action-oriented subject. Conventional prefixes like `docs:`, `fix:`, or `feat:` are fine when they fit.
- Add a body for anything beyond a trivial single-file cleanup. Explain what changed, why it changed, and the important implementation details by area.
- Mention validation explicitly, including the command run and the meaningful result. Do not reduce this to "tests passed."
- Call out baseline, artifact, or session-state updates when they are part of the change.
- Avoid vague messages such as "Update files", "Fix stuff", "Delete old files", or "misc changes."

On feature branches, commit and push without asking when the work is ready. On
`main`, show the proposed commit notes and changed-file summary first, then
commit or push only after explicit confirmation.

---

## Danger Zones

Changes to these areas require extra care. Before committing PR-bound changes,
run the specified targeted validation:

| Area | Risk | Required Validation |
|------|------|---------------------|
| DX12 resource barriers | GPU hang, corruption, CPU/GPU race | `validate_dx12_renderer` + verify `dx12_validation.txt` = 0 |
| Renderer backend regression | Visual divergence from committed DX12 references | `validate_dx12_renderer` screenshot diff |
| DX12 renderer gate | Future visual regressions after parity removal | `validate_dx12_renderer` + verify `dx12_validation.txt` = 0 |
| Per-frame heap allocations | Performance cliff, stall spikes | `validate_perf` + manual hot path review |
| Visual regression baselines | False passes hide real bugs | `validate_dx12_renderer` + intentional baseline update |
| Physics regression baselines | Stale baselines hide real behavior changes | Update only from final Debug artifacts, then rerun `validate_physics` or `validate_physics_deep` to match the baseline set |
| Matrix conventions | Entire scene renders incorrectly | `validate_dx12_renderer` |
| Physics determinism | Butterfly-effect divergence over frames | `validate_physics` byte-exact CSV diff |
| Screenshot timing | Flaky non-deterministic captures | `validate_dx12_renderer` |
| Fixed-step simulation behavior | Physics replay not reproducible | `validate_physics` |
| Coordinate conventions | Upside-down textures, clip-space bugs | `validate_dx12_renderer` |
| Upload buffer / frame allocator | DX12 CPU overwrites in-flight GPU data | `validate_dx12_renderer` + run 3 consecutive times |
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
| Suite files | `SkullbonezData/scenes/*.suite.json` |
| Visual baselines | `TestOutput/baselines/*.png` |
| Physics baselines | `TestOutput/baselines/*.csv` |
| Perf baselines | `TestOutput/baselines/*_perf.json` |
| Validation scripts | `tools/` |
| Agent handoff docs | `Agentic/` |

---

## Agentic Handoff

The Agent Startup Contract names the required first-read files. Use
`Agentic/README.md` as the index for handoff docs, skills, plans, reports, and
reference material. Load skill files only when the current task needs them. Do
not load every skill at session start.
