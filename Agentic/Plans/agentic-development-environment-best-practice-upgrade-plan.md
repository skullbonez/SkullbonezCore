# Agentic Development Environment Best-Practice Upgrade Plan

Status: planning draft
Created: 2026-06-13
Scope: Agentic process, cross-agent instruction discovery, validation metadata, workflow evals, guardrails, cloud setup
Implementation status: Claude and Copilot pointer files created now; remaining work is planned here

## Goal

Upgrade SkullbonezCore from a strong Codex-oriented agent workspace into a
tool-agnostic, more enforceable, and more measurable agentic development
environment.

The repository already has a useful agent contract in `AGENTS.md`, compact
handoff state in `Agentic/SessionState.md`, task skills, queryable physics
diagnostics, and scoped validation gates. This plan keeps those strengths and
adds the missing pieces that current agent tooling practice points toward:

- path-scoped instructions,
- machine-readable validation and artifact metadata,
- agent-workflow evals,
- script or hook enforcement for critical rules,
- reproducible setup for local and cloud agents,
- a small cleanup for durable user-facing report storage.

## Already Done In This Pass

Created lightweight cross-agent pointer files:

- `CLAUDE.md`
- `.github/copilot-instructions.md`

These files intentionally point back to `AGENTS.md` instead of duplicating the
full rule set. `AGENTS.md` remains the single source of truth.

Validation: documentation-only; no repository validation required.

## Current Strengths To Preserve

- `AGENTS.md` gives clear repository rules, validation expectations, danger
  zones, commit expectations, and timing/reporting requirements.
- `README.md` separates human startup from AI-agent startup.
- `Agentic/README.md` keeps first-read context small and directs agents to load
  skills and long references on demand.
- `Agentic/SessionState.md` acts as compact shared memory instead of a long
  historical log.
- `tools/README.md` documents the validation harness and makes the expected PR
  gates discoverable.
- SkullScope gives agents bounded, queryable physics diagnostics and explicit
  model-read data-size accounting.
- `Agentic/Skills/orchestrator/SKILL.md` records the lightweight coordinator
  workflow for sequential plan execution through fresh worker agents and
  rubber-duck review agents.

## Workstream 1: Path-Scoped Agent Instructions

### Problem

The root `AGENTS.md` is strong, but it must cover many risk profiles at once:
DX12 rendering, shaders, physics determinism, tools,
docs, and agent orchestration. Agents editing one area should receive local
rules without rereading every unrelated danger zone.

### Proposed Changes

Add scoped instruction files where they reduce ambiguity:

```text
SkullbonezSource/AGENTS.md
SkullbonezData/shaders/AGENTS.md
tools/AGENTS.md
Agentic/AGENTS.md
TestOutput/baselines/AGENTS.md
```

Each file should be short and area-specific:

- ownership and risk notes,
- targeted validation gate,
- artifact or baseline handling rules,
- local do-not-touch guidance,
- pointers to relevant skills or reference docs.

Candidate examples:

- `SkullbonezData/shaders/AGENTS.md`: shader contract expectations,
  DX12 renderer gate, no silent baseline updates.
- `tools/AGENTS.md`: changed scripts require `tools\validate_fast.bat` plus the
  changed script's own focused check.
- `Agentic/AGENTS.md`: documentation/process changes require no validation
  unless helper scripts are edited; keep run state and report state separate.
- `TestOutput/baselines/AGENTS.md`: visual, perf, physics, and SkullScope
  baselines have different update rules.

### Validation

Documentation-only path instruction additions require no validation.

If helper scripts are touched while adding checks for path instructions, use:

```bat
tools\validate_fast.bat
```

## Workstream 2: Machine-Readable Agent Contract

### Problem

The validation map and artifact rules currently live in Markdown. Humans can
read it easily, but agents and helper scripts have to infer behavior from prose
and tables.

### Proposed Changes

Add a machine-readable contract file:

```text
Agentic/agent_contract.schema.json
Agentic/agent_contract.json
```

The contract should encode:

- impact areas,
- file globs,
- required PR-gate command,
- expected runtime,
- required artifacts,
- baseline update rules,
- danger-zone notes,
- whether documentation-only changes require validation,
- whether a change is allowed to update baselines,
- whether SkullScope query accounting is required.

Example entry shape:

```json
{
  "id": "physics",
  "globs": [
    "SkullbonezSource/SkullbonezRigidBody*",
    "SkullbonezSource/SkullbonezBoundingSphere*",
    "TestOutput/baselines/*.csv"
  ],
  "impact_area": ["physics", "determinism"],
  "pr_gate": "tools\\validate_physics.bat",
  "requires_skullscope_accounting": false,
  "baseline_policy": "physics-baselines-final-debug-only",
  "danger_zone": "Physics behavior must be deterministic and byte-exact against committed CSV baselines."
}
```

Add a small helper:

```text
tools/agent_contract_check.py
tools/agent_contract_check.bat
```

Initial helper behavior:

1. Read `git diff --name-only` or an explicit file list.
2. Match paths against `Agentic/agent_contract.json`.
3. Print the selected impact areas and required PR gates.
4. Detect ambiguous or conflicting gates and recommend the strictest gate.
5. Exit nonzero only for malformed contract data or unknown command-line usage
   in the first version.

Later helper behavior:

- compare Markdown validation tables against the JSON contract,
- emit a concise validation recommendation for agents,
- inform orchestrator-skill prompts and validation handoffs.

### Validation

At PR gate:

```bat
tools\validate_fast.bat
tools\agent_contract_check.bat --self-test
```

## Workstream 3: Agent-Workflow Evals

### Problem

The engine has validation scripts for runtime behavior, DX12 renderer regression,
physics determinism, and performance. The agent workflow itself is not yet
evaluated. That leaves important process failures as instruction-only risks:

- wrong validation gate selected,
- raw physics logs ingested,
- elapsed time omitted,
- report commit contains extra files,
- direct `main` commit attempted,
- path-scoped rules ignored.

### Proposed Changes

Add eval fixtures and a lightweight scorer:

```text
Agentic/Evals/
  README.md
  cases/
    docs-only-change.json
    shader-change.json
    physics-baseline-change.json
    tools-change.json
    orchestrator-report-only-commit.json
  expected/
  results/
tools/agent_workflow_eval.py
tools/agent_workflow_eval.bat
```

Each case should include:

- task prompt,
- changed-file list or synthetic diff,
- expected impact area,
- expected validation command,
- forbidden actions,
- required final-response fields,
- expected artifact/report behavior.

Initial scoring can be static and deterministic:

- feed the case and changed files to `agent_contract_check`,
- verify expected gate selection,
- lint generated report metadata,
- check report-only commit file lists from a supplied fixture,
- check that SkullScope accounting fields are present when required.

Later scoring can replay real run folders under `Agentic/Runs` and produce a
summary of process adherence.

### Validation

At PR gate:

```bat
tools\validate_fast.bat
tools\agent_workflow_eval.bat --self-test
```

## Workstream 4: Enforce Critical Rules With Scripts

### Problem

Some important rules are still only written in prose. Prose is necessary, but
the highest-risk rules should also have local checks.

### Proposed Changes

Add or extend guardrail checks for:

- no direct commits or pushes on `main` without explicit user approval,
- final report-only commits contain only `Agentic/Reports/<date>/<item-id>/report.md`
  and referenced images,
- committed report images are actually referenced by report Markdown,
- `Agentic/Runs` raw state is not accidentally included in a report-only commit,
- no physics baseline update without a recorded final `validate_physics` gate,
- no use of broad process-kill commands in repository scripts,
- no oversized raw diagnostic files committed under `Agentic/Runs`,
- `SessionState.md` remains compact enough to be useful as startup context.

Candidate scripts:

```text
tools/check_agent_report_commit.py
tools/check_agent_guardrails.py
tools/check_session_state_size.py
```

Integrate cheap checks into:

```text
tools\validate_fast.bat
tools\agent_validate.bat
```

Avoid making early checks too aggressive. The first version should report clear
failures only for unambiguous violations.

### Validation

At PR gate:

```bat
tools\validate_fast.bat
```

Also run each changed helper's self-test command.

## Workstream 5: Cloud-Agent And Fresh-Machine Setup

### Problem

`FIRST_TIME_SETUP.md` and the `find_*` scripts help a local Windows machine,
but cloud or hosted agents need a single bootstrap path that prepares tools and
states what cannot be installed automatically.

### Proposed Changes

Add:

```text
Agentic/Reference/cloud-agent-setup.md
tools/bootstrap_agent_environment.ps1
tools/check_agent_environment.bat
```

The setup doc should cover:

- Windows x64 requirement,
- VS2022 Build Tools and v143 toolset,
- Python and Pillow,
- Git for Windows,
- LLVM/clang-format,
- NuGet/package restore expectations,
- GPU and DX12 availability expectations,
- what to do when render validation cannot run in a hosted environment,
- which validations are allowed locally versus expected in CI or PR gates.

The check script should:

- locate Git,
- locate Python,
- check Pillow import,
- locate MSBuild,
- locate clang-format,
- verify expected repo directories and solution file,
- print actionable missing-tool messages.

Do not silently install large tools in the first version. Prefer detection and
clear remediation.

### Validation

At PR gate:

```bat
tools\validate_fast.bat
tools\check_agent_environment.bat
```

## Workstream 6: Report Directory Scaffolding

### Problem

The lightweight orchestrator skill expects durable user-facing summaries for
completed plan work, but report storage should stay separate from raw run state
and temporary artifacts.

### Proposed Changes

Add:

```text
Agentic/Reports/README.md
Agentic/Reports/.gitkeep
```

The README should explain:

- reports are durable, user-facing summaries,
- run state and raw artifacts belong under `Agentic/Runs`,
- report commits should contain only report Markdown and referenced PNG/JPG
  images,
- bulky logs, prompts, raw traces, and unreferenced images do not belong in
  report-only commits.

### Validation

Documentation-only; no validation required.

## Workstream 7: Documentation De-Duplication And Drift Checks

### Problem

Validation expectations appear in several places:

- `AGENTS.md`,
- `README.md`,
- `Agentic/README.md`,
- `Agentic/SessionState.md`,
- `tools/README.md`,
- future path-scoped instruction files,
- future machine-readable contract.

That is useful for discoverability, but it creates drift risk.

### Proposed Changes

Add a small drift checker after `Agentic/agent_contract.json` exists:

```text
tools/check_agent_docs_drift.py
tools/check_agent_docs_drift.bat
```

Initial drift checks:

- every command named in the JSON contract exists,
- every validation script named in Markdown exists,
- `tools/README.md` quick-reference commands match the contract's known gates,
- `AGENTS.md` and `README.md` agree that documentation-only changes require no
  validation,
- `CLAUDE.md` remains a pointer instead of a divergent duplicate.

### Validation

At PR gate:

```bat
tools\validate_fast.bat
tools\check_agent_docs_drift.bat
```

## Recommended Rollout

### Phase 1: Low-Risk Documentation Structure

- Add path-scoped instruction files.
- Add `Agentic/Reports/README.md` and `.gitkeep`.
- Keep every new instruction short and defer to `AGENTS.md` for global rules.

Validation: documentation-only, no validation required.

### Phase 2: Contract Data

- Add `Agentic/agent_contract.schema.json`.
- Add `Agentic/agent_contract.json`.
- Encode the existing validation tables without changing behavior.
- Add a read-only recommendation helper.

Validation:

```bat
tools\validate_fast.bat
tools\agent_contract_check.bat --self-test
```

### Phase 3: Workflow Evals

- Add initial eval cases for docs, shader, physics, tools, and report-only
  commit behavior.
- Score gate selection and required reporting fields.
- Keep this separate from engine behavior validation.

Validation:

```bat
tools\validate_fast.bat
tools\agent_workflow_eval.bat --self-test
```

### Phase 4: Guardrails

- Add report-only commit checks.
- Add session-state size checks.
- Add raw artifact and process-kill checks where detection is reliable.
- Wire cheap checks into fast validation.

Validation:

```bat
tools\validate_fast.bat
```

### Phase 5: Cloud-Agent Setup

- Add environment detection.
- Add hosted-agent setup docs.
- Document which gates require local GPU/runtime support.

Validation:

```bat
tools\validate_fast.bat
tools\check_agent_environment.bat
```

### Phase 6: Drift Checks

- Add docs/contract drift detection.
- Make it part of fast validation only after false positives are addressed.

Validation:

```bat
tools\validate_fast.bat
tools\check_agent_docs_drift.bat
```

## Risks

- Too many instruction files can increase maintenance burden. Keep scoped files
  short and anchored to `AGENTS.md`.
- A machine-readable contract can drift from Markdown if no drift check exists.
- Guardrails that fail on edge cases will be bypassed. Start with clear,
  unambiguous checks.
- Cloud setup cannot guarantee GPU availability. Be explicit about which
  renderer gates require a local Windows graphics environment.
- Workflow evals can become busywork if they do not reflect real agent failure
  modes. Seed them from actual incidents in `Agentic/Runs`.

## Acceptance Criteria

- Claude and Copilot discover the same repository rules as Codex through small
  pointer files.
- Agents editing high-risk subtrees see local scoped instructions.
- A machine-readable contract can recommend the correct PR gate from changed
  files.
- Agent workflow evals cover at least docs-only, shader, physics baseline,
  tools, and report-only commit cases.
- Critical report and artifact rules have script-backed checks.
- Fresh machines and hosted agents can run one environment check and get
  actionable setup output.
- `Agentic/Reports` exists and clearly documents how reports differ from run
  state.
- Validation guidance remains consistent across Markdown docs and the
  machine-readable contract.
