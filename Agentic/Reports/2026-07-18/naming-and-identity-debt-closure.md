# Naming And Identity Debt Closure

Date: 2026-07-18
Plan: `naming-and-identity-debt`
Branch: `nightrunner-17th-july`
Result: complete, 5/5 tasks

## Outcome

Production naming now describes the live domains:

- `TestScene` / `TestSceneParser*` became `AuthoredScene` /
  `AuthoredSceneParser*` across production source and consumers.
- `GameModelRenderer` became `Rendering::RenderInstanceRenderer`, and all
  production `GameModel` vocabulary was retired while the external config key
  and legacy camera hashes stayed unchanged.
- `RuntimeTuning.*` became `OperatorCommandApplier.*`; its stateless
  command-application boundary and per-domain delegation/keep rulings are
  recorded in `naming-n3-command-seam-rulings.md`.
- `RunDemoDirector.*` became `DemoDirectorPlayback.*`, and
  `RunUiTextPass.cpp` became `UiTextPass.cpp`. These were filename/include/path
  moves only; their existing symbols and behavior did not change.

The implementation commits before closure are `cd423112a` (N0), `6a79cbd7a`
(N1), `a9ac26d49` (N2), and `2561efb3a` (N3). N4 is carried by the closure
commit that adds this report.

## Complete Prefix Ruling Reconciliation

The N0 git-index inventory ruled all 62 tracked `Run*` / `Runtime*` source
files. N3 moved two files and N4 moved three files. The final git-index
inventory contains exactly 57 retained matching files, which reconciles
`62 = 57 retained + 5 moved`. The independent reviewer confirmed that no
original inventory stem is absent from the ruling report and found no silent
rename or ungoverned prefix debt.

## Independent Review

The plan-mandated read-only reviewer found no behavior, ownership, census,
artifact, interface, or frame-count defect. It independently normalized the
filename vocabulary and confirmed that every changed C++ file is otherwise
byte-equivalent to its pre-N4 source.

The review did find one documentation blocker: the first N4 comment audit
omitted the substantial changed `tools/validate_project_filters.py` script.
The checklist was corrected before closure and now covers 14/14 source-bearing
files. The reviewer recorded only path/configuration integration as residual
risk, which the formal gates below close.

## Validation

- Focused `Profile|x64` build: passed with zero warnings/errors in 15.050 s.
- `tools\validate_full.bat`: passed in 165.249 s. All 291 doctests and 21,455
  assertions passed, every coverage/CPU lane passed, Automation passed, DX12
  reported zero validation errors with accepted captures, and the 44,401-line
  physics regression remained byte-exact.
- `tools\validate_replay_visual_fidelity.bat`: the task's single invocation
  passed in 431.062 s with 2,401 ticks, 200 moved wall bricks, 187 toppled
  bricks, 199 causal nodes, one presented cascade, 62 saved/loaded ticks, and
  every negative/determinism control detecting divergence.
- Direct `tools\validate_coverage.bat`: passed in 20.394 s; all ten ratified
  subsystem floors passed after the instrumentation path rename.
- Direct `tools\validate_project_filters.bat`: passed in 1.598 s with 738/738
  project/filter items and zero errors.

Logs are under `TestOutput/agent_logs/n4_*.log`.

## Preserved Contracts

- `SkullbonezData/` and `TestOutput/baselines/` have no plan diff.
- No baseline, golden, screenshot, schema, authored-data, or coverage-floor
  value was refreshed.
- The seven excluded renderer consumer interfaces are unchanged.
- `Dx12FrameOwner::FRAME_COUNT` remains 2.
- N4 touched no source logic and introduced no new owner, bag, bridge, alias,
  callback, inheritance, allocation, or exception path.

Comment evidence: `naming-n4-comment-audit.md`, 14 checked, 0 deferred,
0 unchecked.
