# Mega PR terminal evidence - 2026-09-18

PR #175 integrates the existing Split Future workspace, PR #174 and its hull ancestry, the preserved Catto worktree changes, Interactive Grass and Physics Tool Window. The PR remains unmerged. The original Catto worktree and unrelated audit files are preserved. The user's running Profile process 44840 was not terminated; its original locked executable remains at Profile/SKULLBONEZ_CORE.running-44840.exe.

## Feature acceptance

Interactive Grass is enabled by default only in the generated Demo scene. Validation fixtures explicitly opt in. Two fixed live/history owners use 22,820,400 bytes under a 24 MiB ceiling, four views per owner, 65,536 world cells, 64 probes and 131,072 root tests per sample. Exact seeded roots follow physical terrain. Swept sphere/box contact, sleeping contact, overlap identity, finite recovery, water/slope exclusion and capacity overflow are tested. Replay v6 retains shape, terrain and explicit sweep-continuity evidence; missing history, teleports and recording gaps never invent deformation. Grass casts shadows but does not receive object/terrain shadows; nonzero cinematic visual terrain relief is ineligible.

Physics has four dock sections: Simulation, Visualisation, Body and Statistics. Live edits join prediction and end recording before mutation; historical comparison remains read-only. Solver-policy v10 retains all 28 effective settings and saved noncheckpoint continuation verifies hashes. Exact tick, defaults/gravity restore, mass/full-inertia undo/redo/authored save and one-shot centered/off-center impulses pass. Diagnostics use 16 selected contacts, 64 convergence samples, 8,192 historical contacts, 73,728 line segments and 32 numeric labels. The Body scroll limit follows populated rows, including after resize/selection changes, and its compact screenshot contains readable contact values.

Independent grass and Physics reviews found no remaining feature correctness blockers. Later reviews verified the scroll repair, v6 readers, guarded comparison-fixture override and the retained performance measurements. No new downward Replay dependency or steady-gameplay allocation privilege was introduced by these two feature plans.

## Validation

The once-run terminal umbrella initially passed Debug/Automation builds, core byte-exact Physics, full preflight (145 sources / 1,299 compiler contexts), all mandatory CPU lanes and DX12 renderer, then stopped at a stale Python replay-v5 allowlist. Its failed log is preserved; it is not represented as a successful umbrella invocation. Remaining gates were recovered individually after fixes.

- Full Automation/Skarness suite: PASS, 877.92 seconds, including the complete registered grass and Physics acceptance/edge suites and the existing long-horizon prediction matrix.
- Profile tests after closure fixes: PASS, 1,115 tests / 3,822,546 assertions; one existing skipped test.
- Deep Physics: PASS, 140.24 seconds. Core Physics numerical CSV remains unchanged.
- DX12 renderer: PASS; UI stress PASS, 28.84 seconds; graphics stress PASS, 70.88 seconds.
- Replay artifact: PASS, 84.60 seconds. Current v6 uses 112-byte dictionary rows and 76-byte pose rows; authentic v3 migration strips v6 evidence and rebuilds offsets; future v7 and corruption are rejected.
- Final replay visual fidelity: PASS, 370.58 seconds, including every durable-artifact, causal, geometry and determinism negative control.
- Formatting: PASS, 156 changed source visits. Dependency graph and plain language pass. Focused final source-design checks pass for the UI scroll, v6 comparison and Automation fixture changes.
- Final performance gate: PASS, 127.10 seconds, including allocation guards, structural path checks, scale/joint/gravity matrix, absolute budgets and both unchanged-threshold baseline comparisons.
- Native UI gate: all required cases PASS across the preserved initial run and focused recovery. The final cases include 184 Physics toggle/slider checks (144.03 seconds), all 39 editor catalog choices and quick-object variants (151.85 seconds), 27 Options/Keys checks, theme pixels/migration (26.25 seconds), and preference restarts (8.15 seconds). Header, diagnostics, memory, floating windows, dock navigation, side panels, transitions and compact tools pass. Final DX12 check and Profile/Debug ready builds pass. Stale version-6 expectations were updated to version 7; the version-4 fixture omits later fields so it tests actual legacy migration. No preference parser rule was weakened.

The informational replay frame-spike workload failed predictionFullHorizonComplete at its fixed script deadline. It is non-blocking by validate_full's existing contract. The state-based 120-second horizon tests pass. No frame-time conclusion or relaxed assertion is claimed for that diagnostic.

## Explained reference transitions

The replay transition changes only the previously requested Split Future box-corner light submission and provenance. Every one of 2,401 physical/body/ghost/trajectory/topology/header/marker sample hashes, final physical metric and causal topology/timeline matches the predecessor. Eight line fields change over 2,300 ticks with exact vertex/byte accounting. The LF shader-source repair changes four manifest hash fields and no DXIL byte. A clean-checkout hash comparison proves the source matches Git bytes. Both retained producers and hashes are in golden-transitions/replay-v6-split-future-20260918.

The performance transition retains exact old/new producers and three alternating captures of each renderer. Whole median-frame runs are selected; all matched CPU timing comparisons pass unchanged thresholds. Fixed grass and inherited hull/contact diagnostic stores explain the memory increase; restart equals end. Reserved capacity is not equated with resident working set. Planted CPU, memory and absolute-budget regressions are rejected. The exact candidate evidence and independent review decision are in golden-transitions/performance-mega-20260918.

The native comparison fixture override exists only with SKULLBONEZ_SKARNESS and an enabled session. Header entry, cancellation, native library switching and reentry still use ordinary LoadComparison and asynchronous integrity validation. Tests require distinct scene hashes/body counts, exact selected fixture paths and successful load before screenshots. Immutable shipped archives are unchanged.

## Plan closure and delivery state

INTERACTIVE_GRASS IG0-IG6 is complete at 7/7 and PHYSICS_WINDOW PW0-PW7 is complete at 8/8 after local implementation, independent review and mapped validation. Their completed checklists are removed under the repository lifecycle. Acceptance moves 158/167 to 160/167, then removal of their 15 completed phases leaves the live portfolio at 145/152. CONVEX_HULL CH7 owner acceptance and PHYSICS_AB remain outside this requested queue. Additional solver algorithms and adjustable Physics frequency remain deferred as specified by the Physics window scope. The overall delivery goal remains active until final-head hosted CI passes.

The initial hosted run passed all CPU lanes, including its 39-minute preflight. Both hosted scene replicas stopped before simulation because the grass source was baked with CRLF but checked out as LF. The pinned rebake corrects that manifest-only provenance mismatch. Final-head hosted checks remain required before handing off the ready PR.

Logs and raw native traces are under TestOutput/mega-20260917 and TestOutput/skarness. The existing live work ledger belongs to another session; the supported isolated ledger could not obtain verified model pricing. No token/cost telemetry or completion ledger was fabricated.
