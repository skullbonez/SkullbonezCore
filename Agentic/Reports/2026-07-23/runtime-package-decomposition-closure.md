# Runtime Package Decomposition Closure

Date: 2026-07-23
Branch: `nightrunner-23rd-JUL-26`
Plan: `Agentic/Plans/TODO/runtime-package-decomposition.md`
Status: COMPLETE — 5/5 phases

## Outcome

Runtime now exposes its physical ownership boundaries. Eighty formerly loose
files live in 12 named owner packages; `RuntimeFrameViews.h` is the only
source-bearing file left directly under `SkullbonezSource/Runtime/`.

- R1 reconciled all 81 original top-level assignments: 80 moves and one
  deliberate frame-view residue.
- R2 projected all Runtime includes onto the final layout and ratified a
  per-source allowlist plus 18 complementary proof commands.
- R3 performed the 80 exact renames, repaired includes, operational references,
  project/filter metadata, allocation-policy paths, and coverage paths without
  changing namespaces or behavior.
- R4 documented one Input ownership flow. `InputRouter` alone retains
  routing/context/pointer state; sampling, immutable bindings, stateless frame
  composition, execution, and mode/camera policy have distinct roles.
- R5 installed the standing `AGENTS.md` rule, remediated two proof defects found
  by the one independent review, and passed all mapped closure gates.

Accepted implementation commits before this closing commit:

| Slice | Commit |
|---|---|
| R1 | `2e5456d6` |
| R2 | `95bda675` |
| R3 | `d1dd0c88` |
| R4 | `ddf706b5` |

## Independent Review

The required rubber-duck review ran once at whole-plan closure as
`/root/runtime_r5_review` for approximately 13 minutes.

The review found two blocking governance defects, both remediated before
closure:

1. Twelve package proofs omitted the forbidden top-level
   `RuntimeFrameViews.h` alternative.
2. A literal quote in the recorded PowerShell regex was stripped by native
   argument serialization, making zero-row results false-zero-capable.

All 18 commands now use `\x22`, and the 12 affected complements also match
`../RuntimeFrameViews.h`. The corrected suite passes all 206 synthetic
complement cases, its Camera-to-Input positive control returns the two expected
rows, and every real forbidden-edge proof returns zero rows.

The final review found no remaining issue: 80 exact renames, no forwarding or
copied source, no bag/facade/context replacement, and no structural, project,
comment, or test-depth blocker. Its resolved-path graph contains 463
cross-package include rows / 135 directed pairs with zero violations or
unresolved paths.

## Replay And Input Boundaries

Replay files did not move. Seventeen Replay files contain 35 removed and 35
added lines, all mechanical include/header-comment paths. The staged classifier
found zero symbol-body, namespace, state, allocation, or behavior edits.

The Input review confirms:

- `Input.cpp/.h` owns callback-fed hardware latches and emits the device frame.
- `InputController.Bindings` owns the immutable binding table.
- `InputRouter` alone owns retained semantic edges, context, copied snapshots,
  and pointer-presentation state.
- App's input frame and interaction translation units retain no durable state.
- `InputController` is stateless policy; camera input is a derived frame value,
  not competing input authority.

## Static Closure Proofs

The 18 operational Runtime package proofs return zero forbidden rows in 0.5
seconds. The positive-control proof returns the two expected Camera-to-Input
includes. All five standing dependency proofs also return zero rows:

```powershell
rg -n '^#include[[:space:]]+.*(Assets|Gameplay|Physics|Rendering|Scene|World|Runtime|UI)/' SkullbonezSource/Core
rg -n '^#include[[:space:]]+.*(Gameplay|Runtime|UI)/' SkullbonezSource/Physics SkullbonezSource/Rendering
rg -n '^#include[[:space:]]+.*(Assets|Scene|World|Runtime|UI)/' SkullbonezSource/Gameplay
rg -n '^#include[[:space:]]+.*Runtime/' SkullbonezSource/UI
rg -n '^#include[[:space:]]+.*Runtime/Replay/' SkullbonezSource/Physics SkullbonezSource/Rendering SkullbonezSource/Scene SkullbonezSource/World SkullbonezSource/Core
```

No downward Replay include or new/expanded Replay growth privilege appeared.

## Validation

| Command | Time | Result |
|---|---:|---|
| Production project/filter check | 2.3 s | PASS; 749/749 items across three projects |
| Test project/filter check | 1.1 s | PASS; 102/102 items |
| Allocation-policy self-test and repository scan | 9.3 s | PASS; zero allowlist errors |
| `tools\validate_coverage.bat` | 31.6 s | PASS; all ten subsystem floors |
| `tools\validate_fast.bat` | 75.7 s | PASS; formatting, metadata, staged size, Profile/Debug builds and tests |
| `tools\validate_replay_visual_fidelity.bat` | 433.7 s | PASS; one engine, one generation, 2,401 ticks, 200 causal nodes, all false-pass controls |
| `tools\validate_full.bat` | 188.0 s | PASS; CPU umbrella plus all five runtime processes |
| `Profile\SKULLBONEZ_CORE.exe --platform-profiler-markers` | 4.2 s | PASS; exit 0 |

Key final output:

```text
Project filter summary: ... (0 errors, 749 project items, 749 filter items across 3 production projects)
PASS replay visual fidelity: ticks=2401 moved_wall_bricks=200 causal_nodes=200 presented_cascades=1
DX12 validation errors: 0
PASS: physics_regression_varied.csv (44401 lines, byte-exact match; output runs=2, baseline runs=1)
VALIDATE_FULL: DEFAULT GATE PASSED
```

The first replay-fidelity launch was terminated by the shell's 120-second outer
timeout. Its exact remaining Automation engine PID was stopped before retry.
The immediate retry then failed honestly because that stale process had locked
the executable. A second 300-second outer timeout was likewise cleaned up by
exact PID. The final unchanged-source run used a sufficient bound and passed in
433.7 seconds.

No baseline, golden, screenshot, authored-data, config, schema, shader, or
physics CSV artifact changed.

## Comment Audit

R3 audited all 245 touched source-bearing files with zero findings; the only
exemption was the trivial `tools/validate_automation.bat` wrapper. R4's
comment-only `InputController.h` edit passed its one-file audit. There are zero
deferred or unchecked files.
