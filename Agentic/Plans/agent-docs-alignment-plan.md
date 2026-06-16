# Agent Documentation Alignment Plan

Status: planning draft
Created: 2026-06-16
Scope: agent documentation, Codex/Claude/Copilot instruction discovery, handoff state, validation guidance
Implementation status: plan only, no repository behavior changes in this pass

## Goal

Tighten the repository's agent documentation so Codex, Claude Code, and GitHub
Copilot receive the same core operating contract while each tool also gets the
instruction shape it handles best.

The repo already has a strong root contract in `AGENTS.md`, a Claude pointer in
`CLAUDE.md`, a Copilot pointer in `.github/copilot-instructions.md`, and
on-demand deeper material under `Agentic/`. This plan keeps `AGENTS.md` as the
source of truth, removes drift between startup instructions, trims stale shared
state, and adds provider-specific path-scoped guidance where it will improve
agent behavior.

Validation: documentation-only; no repository validation script is required.

## Current State

- `AGENTS.md` is the durable universal contract. It covers git policy,
  validation gates, danger zones, build commands, commit notes, and handoff
  expectations.
- `README.md`, `Agentic/README.md`, and `.github/copilot-instructions.md`
  each describe a startup read order, but the lists are not identical.
- `CLAUDE.md` contains `@AGENTS.md`, which is a good minimal Claude Code import
  strategy.
- `.github/copilot-instructions.md` mostly points to `AGENTS.md`. That is
  useful for cloud-agent style work, but too thin for Copilot code review and
  path-specific assistance.
- `Agentic/SessionState.md` says it should stay short, but it currently carries
  substantial completed history and can drift from the real `git status`.
- `Agentic/Skills/*` already include both `SKILL.md` and `skill.md`, which
  keeps Codex-style and repo-local skill discovery friendly.

## Design Principles

1. Keep `AGENTS.md` authoritative and vendor-neutral.
2. Avoid duplicating long rules across provider files.
3. Give Copilot enough self-contained context for review and inline help.
4. Move area-specific warnings closer to the files they govern.
5. Keep volatile state short enough that agents actually read and obey it.
6. Make headless/cloud fallbacks explicit where local-only rules cannot always
   be enforced.

## Workstream 1: Consolidate Startup Instructions

### Problem

The required first-read set is scattered:

- `AGENTS.md` says to read `AGENTS.md` and `README.md` before editing, then
  later says all agents should also read `Agentic/README.md` and
  `Agentic/SessionState.md`.
- `README.md` tells AI agents to read `AGENTS.md`, `Agentic/SessionState.md`,
  and only the task-relevant skill.
- `.github/copilot-instructions.md` lists `AGENTS.md`, `README.md`,
  `Agentic/README.md`, and `Agentic/SessionState.md`.

### Proposed Changes

Add a single "Agent Startup Contract" section near the top of `AGENTS.md`:

```text
1. Read AGENTS.md.
2. Read README.md.
3. Read Agentic/README.md.
4. Read Agentic/SessionState.md.
5. If this is a fresh machine or a required tool lookup fails, read FIRST_TIME_SETUP.md.
6. Load deeper skills, plans, audits, or references only when the current task calls for them.
```

Then simplify `README.md`, `Agentic/README.md`, and
`.github/copilot-instructions.md` so they all point back to that canonical
startup section instead of maintaining separate variants.

### Acceptance Criteria

- Only one file owns the canonical startup sequence.
- Other docs either quote that sequence exactly or point to it.
- The sequence still keeps deeper `Agentic/` material on demand.

## Workstream 2: Refresh And Slim Session State

### Problem

`Agentic/SessionState.md` is meant to be short shared memory, but it currently
contains many completed or historical work items. It also has a field for
expected uncommitted changes, which can become stale quickly.

### Proposed Changes

Keep `Agentic/SessionState.md` focused on:

- current branch and worktree,
- active user-facing objective,
- known blockers,
- expected dirty files or explicit "check git status" instruction,
- next required validation at PR/commit gate,
- a short list of active risks.

Move completed history to the relevant `Agentic/Plans/Done`,
`Agentic/Reports`, or audit files.

### Acceptance Criteria

- `SessionState.md` is short enough to scan in under a minute.
- It does not claim the worktree is clean unless that was just verified.
- Historical milestones live outside session state.

## Workstream 3: Strengthen Copilot Instructions

### Problem

GitHub Copilot's repository instruction file is currently a pointer. That can
work when an agent explicitly reads `AGENTS.md`, but Copilot code review and
some inline/chat contexts benefit from concise, self-contained instructions in
`.github/copilot-instructions.md`.

### Proposed Changes

Expand `.github/copilot-instructions.md` into a compact summary with:

- project identity: Windows x64 C++17 DX12-first graphics and physics engine,
- production renderer rule: DX12 only, no new GL/DX11 runtime dependencies,
- validation policy: formal validation is a pre-commit/PR gate, not an
  after-every-edit loop,
- documentation-only rule: no validation required,
- process rule: kill launched processes by PID only,
- git rule: no force-push, rebase, history rewrite, or direct main commit/push
  without explicit user confirmation,
- review focus: bugs, regressions, missing tests, physics determinism, DX12
  validation errors, baseline integrity, and hot-path allocations.

Keep it under roughly two pages and avoid long duplicated tables.

### Acceptance Criteria

- Copilot can produce a useful review without needing to infer the whole
  `AGENTS.md` file.
- The file remains short enough that it will not compete with task context.
- The root source of truth is still named clearly.

## Workstream 4: Add Path-Scoped Instructions

### Problem

The root `AGENTS.md` carries several distinct risk profiles at once. Agents
editing shaders, physics, validation tools, and docs need different high-signal
reminders.

### Proposed Changes

Add concise path-scoped files where they reduce ambiguity.

For Codex and other agents that read local `AGENTS.md` files:

```text
SkullbonezSource/AGENTS.md
SkullbonezData/shaders/AGENTS.md
tools/AGENTS.md
Agentic/AGENTS.md
TestOutput/baselines/AGENTS.md
```

For Copilot:

```text
.github/instructions/dx12.instructions.md
.github/instructions/physics.instructions.md
.github/instructions/tools.instructions.md
.github/instructions/docs.instructions.md
.github/instructions/baselines.instructions.md
```

Each Copilot file should use `applyTo` frontmatter for the relevant paths.

For Claude Code, optionally add `.claude/rules/` equivalents if Claude-specific
path scoping is desired. Keep `CLAUDE.md` as the root import unless a real
Claude-only rule appears.

### Acceptance Criteria

- Root instructions get shorter or stay stable.
- Area-specific files contain only local risk and validation guidance.
- No provider file contradicts `AGENTS.md`.

## Workstream 5: Clarify Environment-Dependent Rules

### Problem

Some rules assume a local interactive machine. They are right for this repo but
need an explicit fallback for headless or cloud agents.

### Proposed Changes

Update `AGENTS.md` with fallback wording for:

- visible console windows: use them when available; otherwise mirror command
  output to a log and state that the run was headless,
- timing: choose one rule, either all user-requested work or only substantial
  work, and make `SessionState.md` match,
- validation output: define whether final answers should paste full logs or
  quote key result lines plus a log path.

### Acceptance Criteria

- Local agents still use visible consoles for builds, launches, and validation.
- Cloud/headless agents have a compliant path instead of silently violating the
  instruction.
- Timing expectations are consistent across docs.

## Workstream 6: Add Dirty-Worktree And Destructive-Git Guardrails

### Problem

The repo already forbids force-push, rebase, and history rewrite. It should also
explicitly tell agents how to behave in a dirty worktree.

### Proposed Changes

Add to `AGENTS.md`:

- run `git status --short --branch` before edits and before commits,
- treat pre-existing changes as user-owned,
- do not revert, overwrite, or stage unrelated user changes,
- do not use `git reset --hard`, destructive `git clean`, or discard commands
  unless the user explicitly requests that operation,
- when committing on a feature branch, summarize changed-file scope first if
  unrelated dirty files are present.

### Acceptance Criteria

- Agents can work safely in a shared dirty worktree.
- Commit automation does not accidentally capture unrelated user work.

## Workstream 7: Add Review-Specific Guidance

### Problem

Code review behavior differs from implementation behavior. Copilot review,
Codex review, and Claude review should all prioritize the same repository
risks.

### Proposed Changes

Add a short review section to `AGENTS.md` or a small
`Agentic/Reference/code-review-guidance.md` referenced by provider files:

- findings first, ordered by severity,
- include file and line references,
- focus on behavioral bugs, regressions, missing tests, validation gaps,
  baseline mistakes, physics determinism, DX12 validation errors, and
  performance hot paths,
- keep summaries secondary,
- say clearly when no issues are found and name residual risk.

For Copilot, mirror the short version into `.github/copilot-instructions.md`
because Copilot code review should not depend on following external links.

### Acceptance Criteria

- Review output becomes more consistent across agents.
- Copilot receives review-specific instructions without reading all handoff
  material.

## Suggested Implementation Order

1. Update `AGENTS.md` startup, dirty-worktree, timing, and headless fallback
   wording.
2. Align `README.md`, `Agentic/README.md`, and
   `.github/copilot-instructions.md` with the canonical startup contract.
3. Trim `Agentic/SessionState.md` and move historical material to completed
   plans or reports.
4. Expand `.github/copilot-instructions.md` into a self-contained summary.
5. Add path-scoped Copilot instructions.
6. Add path-scoped `AGENTS.md` files only where they remove meaningful
   ambiguity.
7. Add optional Claude path-scoped rules only after observing a Claude-specific
   gap.

## Validation And Review

This is documentation-only work. No repository validation script is required.

Recommended manual checks:

- Review all changed docs for contradictory startup or validation instructions.
- Confirm `git status --short --branch` matches any dirty-worktree statement in
  `Agentic/SessionState.md`.
- Ask each agent surface a small smoke prompt, such as "summarize the repo
  validation policy," and verify that the answer matches `AGENTS.md`.

## Source Guidance

The plan is based on the repository's current docs plus public provider
guidance:

- OpenAI Codex: `AGENTS.md` for repo guidance, nested instruction files, and
  skills for reusable workflows.
- Anthropic Claude Code: `CLAUDE.md` memory files, imports, and concise
  project memory.
- GitHub Copilot: repository custom instructions, path-specific instruction
  files, and focused code-review instructions.

