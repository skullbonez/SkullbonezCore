# Original and Modified path reveal

The requested result is a stationary faded-blue Original path with a high-detail
red Modified path revealing alongside it after each edited-vector release.
Showing the widget alone must preserve the current screen and allocate nothing.

The comparison-only sampler split 24,000 records between two recomputed paths
and ignored the reveal cursor. In the 200-box scene this allowed only 56 segments
per body across 2,400 simulation ticks. It has been removed. App snapshots the
exact displayed compact records and interpolated heads once, changing only RGB
and alpha. Modified uses the normal retained drawing and provisional heads.
Every changed-vector release clears the superseded Modified publication; otherwise
normal same-target refresh policy retains a completed reveal cursor.

Original uses the existing renderer per-lane record region and Modified uses
the separate range region. Each keeps the normal 24,000 ordinary/3,000 priority
capacity. The snapshot is allocated only on a changed vector, shares the existing
Replay-phase prediction registration and 960 MiB cap, and is released with the
experiment. No registration, cap, phase permission or GPU allocation is added.
Memory reporting includes the optional snapshot. Rendering receives generic
compact/expanded input modes; product state remains in App.

The all-body space view also needed a fair horizon-based sampling quota. Its
normal retained renderer previously let early bodies consume the whole arena.
The quota is fixed before reveal and does not affect selected-causal-tree detail.
The capacity fixture explicitly requests all-body presentation and uses motion
above the normal minimum visible-segment distance. Normal publication exposes
at most 240 paths; the 3,000-body fixture checks that visible set, exact Original
geometry, and all 3,000 ghosts. It no longer requires the comparison to generate
additional paths that were absent from the original screen.

## Critique and evidence

This is a separate critique pass in the same session, not an independent agent.
The review checked capture-before-clear ordering, optional allocation lifetime,
stable Original stream identity, independent GPU offsets and upload caches,
normal-path tint reset, repeated-release behaviour, accept/cancel cleanup and
the dependency direction. The review found and fixed the repeated-release cursor
inheritance, all-body budget starvation, and stale borrowed Original spans at
experiment teardown.

Ownership review: (1) the optional Original aggregate retains both owned lanes
and one immutable stream identity, with its invariant exercised by the native
hash/re-grab assertions; it is not a parameter wrapper. (2) No capability slices
or retained sibling borrows are introduced. (3) No member-prefixed locals or
parameter aliases are added. (4) The coarse sampler is deleted rather than
renamed into another helper. (5) Renderer input/offset comments and reserve
lifetime/cap comments match their production callers. The initial focused
compiler-backed source-design check passed 16 files/144 contexts with no findings;
the final fast gate rechecks the completed source. No build inventory changes
or downward Replay includes are introduced.

The dense native regression at `TestOutput/skarness/pr169-paced-reveal-02/`
passed: target 1 remained selected/published/submitted; Original retained all
15,916 displayed records with an identical geometry hash; Modified completed
15,250 red records and revealed progressively over 227 observations. Original
geometry and stream identity remained unchanged through the second edit.
Checkbox-only activation allocated no snapshot and preserved drawing. Strict
allocation guards passed and DX12 reported zero errors. Normal, held, partial
and completed screenshots were captured; partial and completed views were
inspected directly. The first test run exposed the repeated-release defect and
is retained as failing evidence, not a passing result.

The first offline candidate command used the main checkout's checker, which
resolved the report's relative artifact path against an older main-checkout
artifact. Re-running the producing checkout's checker resolves the correct
artifact and passes. The mistaken command is preserved separately in
`TestOutput/reveal-wrong-checkout-controls.log`; it required no source change
or new engine run and is excluded from passing evidence.

Final selected preflight passed in 117.625 seconds, using
`SKORE_SIZE_DIFF_BASE=cd39282c0d5bad586bc60e0ad4a60dfdac810f86` and
`tools/validate_fast.bat --preflight-only`. The current UI gate separately ran
all 1,044 unit tests successfully (one existing skip), avoiding duplicate unit
execution. The earlier broad preflight reported a formatting mismatch and was
stopped after that failure; it is not counted as passing validation. The final
formatter and source-design/retained-policy lanes are green.

The final dense reveal run in the UI gate reproduced the same 15,916 Original
and 15,250 Modified records over 216 reveal observations. The 3,000-body case
passed exact Original geometry, both sets of 240 displayed paths, all-body
ghost coverage, allocation guards and zero DX12 errors.

Other final gate results are recorded in
`TestOutput/reveal-native-closure-results.json` and
`TestOutput/reveal-engine-closure-results.json`. Final results:

- Full UI gate: PASS, 812.672 seconds, including current Profile,
  Debug and Automation builds, 1,044 unit tests, all native UI cases and captured
  appearance checks.
- DX12 renderer gate: PASS, 46.438 seconds, unchanged references
  and zero validation errors.
- One-minute graphics stress: PASS, 70.812 seconds including
  cleanup; the runner verified and stopped its owned PID after graceful timeout.
- Staged physics determinism gate: PASS, 34.266 seconds, with
  a clean-process worker matrix and the current staged source fingerprint.
- Canonical replay gate: FAIL at the known first causal identity (expected 91,
  actual 70), 328.828 seconds. The current artifact exactly
  matches the unchanged reviewed candidate baselines; all negative and
  determinism controls pass. Canonical baselines remain unchanged.

The physics and replay gates used the isolated validation checkout under
`TestOutput/velocity-widget-index`, synchronized to the exact staged source,
to exclude the user-owned untracked scene from deterministic input inventory.
No test result from the earlier failing experiments is counted as a pass.

The earlier authored-sleep change still differs from the canonical 200-box
replay/causal baseline. Its controlled before/after explanation and candidate
approval request remain in `2026-09-12-pr169-adversarial.md`. This follow-up
does not replace any canonical baseline or claim that pending approval is resolved.
