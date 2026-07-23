# Run::Execute Frame-Phase Decomposition Closure

Date: 2026-07-22
Branch: `nightrunner-22nd-JUL-26`
Result: Complete - RX0-RX3, 4/4

## Outcome

`Run::Execute` is a 74-physical-line top-level frame sequencer. It retains the
exit latch, bounded Win32 message drain, steady-gameplay allocation scope,
four stack-view factories, named phase order, two restart edges, and final
application-result resolution. Business work remains with the existing input,
scene, replay, render, UI, automation, diagnostics, and platform owners.

The RX0 16-span census maps one-to-one to the extracted phase methods. No
`Run` data member, owner type, forwarding facade, context/service bag, callback
pack, stored host reference, or runtime allocation path was added.

## Final Frame Contract

The validated order is message drain; frame timing/view construction;
diagnostics begin; automation before input; input and atomic UI-surface
reconciliation; simulation and replay prediction; render preparation and model
publication; world render; operator UI; post-draw diagnostics; screenshot
policy; CPU-work completion; Present; profiler/performance bookkeeping; scene
advance.

The sensitive edges remain unchanged:

- at most 256 Win32 messages drain before frame work;
- automation precedes the single input turn and surface reconciliation;
- deterministic-presentation policy is fixed before physics;
- screenshot completion can restart before Present;
- scene advance can restart only after successful Present and bookkeeping;
- pipeline-sync, viewport, ImGui, and Present failures retain owned failure
  resolution and their existing timing/profiler cleanup.

`ImGuiEditorOwner` publishes value-only input intent and copied status.
`InteractionAutomationController` interprets its own replay command and builds
its value-only development-UI assertion view. `Run` sequences those owner calls
and retains only exit/failure and atomic surface-order authority.

## Independent Ownership Review

The mandatory read-only rubber-duck review covered `Run.h`, `RunFrame.cpp`,
`RunInput.cpp`, `RunRender.cpp`, sibling `Run` translation units, and the
RX0-to-RX2 diff. It found no blocking or non-blocking issue. The reviewer
confirmed the exact sensitive ordering above, stack-only views/results, no new
Run member or broad authority channel, value-only ImGui/automation seams, no
downward Replay include, and no expanded replay allocation privilege.

| Plan | Duck run | Reviewer/thread | Reason | Prompt chars | Response chars | Tokens | Elapsed | Verdict | Follow-up |
|---|---|---|---|---:|---:|---:|---:|---|---|
| `run-execute-frame-phase-decomposition` | `run-execute-frame-phase-decomposition-duck-01` | `/root/rx3_rubber_duck` | Final logical-Run ownership and ordering review | 1,507 | 1,014 | n/a | 6m 16s | No findings | None |

## Comment Audits

- RX1: `Run.h` and `RunFrame.cpp`, checked 2/2; zero deferred or unchecked.
- RX2: `ImGuiEditorOwner.h/.cpp`,
  `InteractionAutomationController.h/.cpp`, and `RunFrame.cpp`, checked 5/5;
  zero deferred or unchecked.
- RX3 changed documentation only; no additional source-bearing file required
  audit or repository validation after the final gate.

These were touched-file audits, so no repository-wide checklist plan was
required. No wording awaited owner review.

## Validation

The desktop shell could not open a separate visible console, so commands ran
in the app shell.

| Command | Time | Result |
|---|---:|---|
| RX1 Profile build | 8.41 s | PASS; zero warnings/errors |
| RX1 Automation build | 8.28 s | PASS; zero warnings/errors |
| RX1 proceed-policy doctest | 0.02 s | PASS; 1/1 case, 9 assertions |
| RX1 `tools\validate_full.bat` | 125.57 s | PASS; CPU/coverage and five runtime lanes |
| RX2 Profile build | 8.62 s | PASS; zero warnings/errors |
| RX2 Automation build | 10.35 s | PASS; zero warnings/errors |
| RX2 Release build | 33.34 s | PASS; zero warnings/errors |
| RX2 ImGui routing doctest | 0.02 s | PASS; 1/1 case, 29 assertions |
| RX2 `tools\validate_full.bat` | 129.65 s | PASS; final public/private owner surface |
| dependency-direction and Replay-boundary proofs | <1 s | PASS; all four exact commands returned no rows |
| RX1-RX2 escape-hatch diff proof | <1 s | PASS; no added callback, heap/growth, friend, or `void*` row |
| independent rubber-duck review | 6m 16s | PASS; no blocking or non-blocking findings |
| RX3 `tools\validate_full.bat` | 101.8 s | PASS; zero build warnings/errors, CPU/coverage and five runtime lanes, zero DX12 errors, accepted images, physics hash `0x953D97A226665242`, byte-exact 44,401-line CSV |

No behavioral baseline, golden, screenshot, replay artifact, physics CSV,
config, authored-data file, or runtime-reserve inventory changed.

## Closure

RX0-RX3 are complete. The active TODO plan is removed under MASTER inventory
rule 4; this report and commits `3d843b97`, `c8d5611d`, `bd4df11e`, and the RX3
closure commit are the durable record. The binding queue advances to
`runtime-renderer-decomposition` RR0.
