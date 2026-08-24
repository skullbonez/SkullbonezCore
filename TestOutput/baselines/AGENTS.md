# Baseline Instructions

Follow the root `../../AGENTS.md` contract first. This folder stores committed
visual, physics, query, and perf baselines.

## Local Rules

- Baseline changes are behavior changes unless the task is explicitly
  documentation-only elsewhere.
- Physics CSV and SkullScope query baselines must come from the final Debug
  executable, scene files, and config that will be committed, followed by
  the mapped Physics gate.
- An active Physics plan may update every golden it governs without a separate
  owner prompt only through the content-bound automated lane. Before writing,
  retain exact old/new executables and required DLLs in a new append-only
  transition bundle described by `../../Agentic/Plans/Artifacts/README.md`.
- Never refresh a golden merely to hide a failing gate or unexplained behavior.
- `tools\update_baselines.bat` is visual/perf only. Do not use it for physics
  CSV or SkullScope baselines.
- Visual baseline updates require the DX12 renderer gate and intentional review
  of the screenshot diff.
- Perf baseline updates require `tools\validate_perf.bat` and a note explaining
  why the new number is expected.
