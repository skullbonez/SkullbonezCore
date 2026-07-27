# Governance Shape-To-Judgment Conversion — G0 Rule-Gap Census

Date: 2026-07-26
Plan: `../../Plans/TODO/governance-shape-to-judgment-conversion.md`
Branch: `nightrunner-26th-JUL-26`
Measurement tip: `693b3c03`
Tooling: `tools/inventory_authority_free_aggregates.py`,
`tools/inventory_extraction_scars.py` (both landed by G2 in the same change)

## Purpose

Map every current aggregate-shape and extraction-scar offender to the `AGENTS.md`
rule whose wording admits it, and record the specific wording change that would
have caught it. This is the evidence G1/G1b implement against.

## Method

Two read-only inventories over the 652 tracked `SkullbonezSource` translation
units, with comments, literals, raw strings, and preprocessor lines masked before
any pattern is applied. Every count below is reproducible:

```bash
python tools/inventory_authority_free_aggregates.py --repo .
python tools/inventory_extraction_scars.py --repo .
```

## Headline Measurements

| Measurement | Value |
|---|---:|
| Aggregate candidates matching the historical suffix families | 94 |
| Candidates stating a per-type `Invariant:` block | **0** |
| Candidates with a gating structural signal (single-member) | 8 |
| Multi-member candidates without a stated invariant (review context) | 69 |
| Extraction-scar findings | 89 |
| — member-prefixed locals | 86 |
| — pure reference aliases of a parameter | 3 |
| Files carrying at least one extraction scar | 12 |

The zero in row two is the most useful number in this census. Not one of the 94
aggregates the repository would classify as a candidate names the rule it
enforces, which is what the Invariant Ownership Rule already requires of a
legitimate aggregate. The rule was written; it was never the thing being checked.

## Rule Gap 1 — The ban is on the noun, so the noun multiplied

**Rule text that admits it** — Invariant Ownership Rule: "An aggregate that only
carries data to shorten a signature is an authority-free bag and remains banned."

**Why it did not catch these.** The test is stated as an intent ("only carries
data to shorten a signature") with no decidable form, and the surrounding
God-Object Closure Rule enumerates *names* (`*Internal`, `*Context`, `*Services`,
`*Bindings`). A reviewer checking names passes anything named plausibly; a
reviewer checking intent has no artifact to point at. Three of the eight
behavior-free single borrowed-member couriers do not even shorten a signature,
so they fail the rule's
own stated test and survived anyway.

| Aggregate | Site | Members | Ruling |
|---|---|---:|---|
| `TornadoUICommandContext` | `Runtime/Interaction/OperatorCommandApplier.h:78` | 1 (`SceneWorld&`) | remove |
| `PhysicsSleepPolicyUICommandContext` | `Runtime/Interaction/OperatorCommandApplier.h:87` | 1 (`SceneWorld&`) | remove |
| `SceneRuntimeCreateContext` | `Runtime/Scene/SceneRuntimeCreate.h:35` | 1 (`SceneController&`) | remove |
| `SceneAuthoredCameraContext` | `Runtime/Scene/SceneAuthoredSetup.h:64` | 1 | remove |
| `SceneGeneratedCameraContext` | `Runtime/Scene/SceneGeneratedSetup.h:68` | 1 | remove |
| `AssetContext` | `Assets/AssetSystem.h:190` | 1 (`const AssetSystem*`) | remove |
| `ShadowGraphInputs` | `Runtime/Render/RuntimeRenderer.h:327` | 1 | retain-prior (PB0) |
| `ReflectionGraphInputs` | `Runtime/Render/RuntimeRenderer.h:331` | 1 | retain-prior (PB0) |

The first two are distinct types wrapping the **same** single reference,
differing only by name and comment — the clearest possible demonstration that the
naming ban does no work. `SceneRuntimeCreateContext` is passed **by value** into
`CreateSceneFromUI( SceneRuntimeCreateContext, const char* )`, a signature
strictly worse than `CreateSceneFromUI( SceneController&, const char* )`.

**Wording change (G1).** Add decidable sub-tests to the Invariant Ownership Rule:
a behavior-free aggregate whose sole member borrows another owner is
authority-free; an aggregate whose sole
consumer destructures every member at entry owns nothing; two aggregates with
identical member lists are one or none. State that renaming never legitimises a
shape. Landed as the "The Test Is Ownership, Not Spelling" subsection.

## Rule Gap 2 — The ban is on members, so authority moved to locals

**Rule text that admits it** — God-Object Closure Rule: "`Run::*` forwarding
wrappers or nominal owner types that merely relay business operations while
authority remains in `Run`."

**Why it did not catch these.** Every listed failure is a *type* or a *member*.
A local variable is neither, so an extracted function can keep its pre-extraction
body verbatim by rebinding parameters to the old member names and no rule applies.
`concrete-parameter-bag-elimination` PB5 deleted
`PersistentContactSolverContext` and its own census note records that the consumer
"immediately aliases nearly every field" — the bag went, the aliases stayed.

| File | Findings |
|---|---:|
| `Runtime/App/InputFrameExecution.cpp` | 25 |
| `Physics/Diagnostics/SkullScope.cpp` | 16 |
| `Runtime/Capture/RuntimeStressController.cpp` | 14 |
| `Physics/PersistentContactSolver.cpp` | 11 |
| `Runtime/App/InputRouter.Interactions.cpp` | 6 |
| `Runtime/Scene/SceneController.Load.cpp` | 3 |
| `Physics/SleepIslandSystem.cpp` | 3 |
| `Physics/PhysicsDiagnosticsSink.cpp` | 3 |
| `Maths/GeometricMath.cpp` | 2 |
| `Runtime/Scene/SceneRequestExecution.cpp` | 1 |
| `Runtime/App/Window.cpp`, `Runtime/Input/Input.cpp` | 1 each |
| `Core/WorkerPool.h`, `Runtime/Render/RuntimeRenderPasses.cpp`, `Runtime/Prediction/ReplayPredictionArchive.cpp` | 1 each (alias) |

The originating architecture review found this by hand in four physics files and
estimated 33 aliases. The measured figure is 89 across 12 files — a 2.7x
under-count, which is itself evidence for why the rule needed an instrument
rather than attentive reading. `extraction-scar-remediation` is corrected to the
measured scope.

**Wording change (G1).** New Extraction Scar Rule naming both shapes, plus a new
God-Object Closure Rule bullet for a body preserved by parameter rebinding.
Landed.

## Rule Gap 3 — The ban is on the whole surface, so the surface was sliced

**Rule text that admits it** — God-Object Closure Rule: "mutable multi-domain
state or queues collected in `Run` or a replacement ... broad bag."

**Why it did not catch this.** The rule targets one broad bag. Four narrow bags
that together reconstitute the surface satisfy every word of it.
`Runtime/RuntimeFrameViews.h:24` states the invariant "No capability slice spans
the complete frame surface" and the same file declares four views totalling 23
references — `Run`'s member list. `Run::RunInputPhase` (`Runtime/App/Run.h:192`)
and `Run::RenderOperatorUiPhase` (`:208`) each receive all four.

Two pieces of corroborating evidence found during this census:

1. **The convention is not load-bearing.** `Run::TickPhysics`
   (`Runtime/App/RunFrame.cpp:877`) does the heaviest frame work and uses no view,
   reaching `m_simulation`, `m_replayRuntime`, `m_inputRouter`,
   `m_diagnosticsRuntime`, `m_runtimeTools`, `m_interaction`, `m_sceneController`,
   and `m_config` as members. No rule distinguishes which form an operation uses.
2. **The views feed pre-extraction member names.** The two external view consumers
   named by `RuntimeFrameViews.h:30-31` are also the two largest extraction-scar
   sites: `InputFrameExecution.cpp` (25) and `RuntimeStressController.cpp` (14)
   destructure the views straight back into `m_UI`, `m_applicationExit`,
   `m_assets`, `m_camera`, `m_config`, `m_sceneController`, `m_renderer`,
   `m_renderBackendView`, `m_workerPool`, and more. The slices exist to supply the
   old member names to lifted bodies.

`RuntimeRenderBackendView` (`Runtime/Render/RuntimeRenderHost.h:150`) is the same
gap in one type: eleven nullable concrete `Rendering::Dx12*` pointers with a
`RequireBackbufferCapture()` that Lane-F terminates on null. PB3's review already
named it "the service bag" when rejecting it as a composer parameter
(`concrete-parameter-bag-elimination-pb3-render-ui.md:44-48`) and it survived as a
`Run` member and a `RuntimeFramePresentationView` field.

**Wording change (G1).** New Capability Slice Ownership Rule: judge a slice set as
one surface; an operation receiving every slice makes the split nominal; a
convention bypassed by some operations on its own path is decorative; a view may
not state an invariant it does not hold. Landed.

## Rule Gap 4 — Frozen counts were banned with no replacement instrument

**Rule text that admits it** — Governance Review Model: "These policies are
enforced by code review, owning plans, focused behavioral tests ... and the
targeted validation gates below."

**Why it did not catch anything.** Banning frozen counts was correct and removed
the wrong instrument, but nothing replaced it, so the shape rules were enforced
only when a human noticed. `tools/inventory_wide_signatures.py` already proved the
repository knows how to build a repeatable inventory that reports without
ratcheting; no equivalent existed for aggregates or scars.

**Wording change (G1).** Governance Review Model now names three inventories as
the instrument and states the shared contract: output is a current measurement
requiring rulings, an unruled finding fails the gate, a ruled one passes, and no
row is ever an allowance. Landed with the table.

## Rule Gap 5 — The delegated reviews were never told the rules

**Rule text that admits it** — Governance Review Model delegates to "code review";
the orchestrator skill routes that to `$rubber-duck`.

**Why it did not catch anything.** Verified 2026-07-26: neither
`Agentic/Skills/rubber-duck/SKILL.md` nor
`Agentic/Skills/carmack-test/SKILL.md` contained the words `bag`, `Context`, or
`aggregate` anywhere. `rubber-duck` defined Purpose, Operating Mode, Workflow, and
Output Shape with no aggregate-ownership criterion; `carmack-test` had a Hard
Checks list with no row for it. A reviewer caught these shapes only by having read
`AGENTS.md` in the same session.

This is the gap that explains the other four. A rule delegated to a review whose
instructions omit it is unenforced in practice, however well it is written.

**Wording change (G1b).** Five ownership questions added to `rubber-duck` with
`[Blocking]` severity and the inventory commands; matching Hard Checks rows and a
`3 / 5` encapsulation verdict cap added to `carmack-test`; the orchestrator now
requires inventory output in the end-of-plan review prompt;
`code-style-guide.md` and `skullbonez-core-class-structure.md` carry the tests at
the point of authoring. Landed.

## Retain Rulings Carried Forward

Every PB0 `Explicit Retain Ruling`
(`concrete-parameter-bag-elimination-pb0-census.md:82-99`) is carried forward
unchanged and is out of scope for redecision: `SceneDefaultsSaveView`,
`RuntimeRenderer::FrameEntryContext`, the ten `*GraphInputs` and six non-UI
`*PassInputs`, `RenderResourceContext`, `PrimitiveRenderContext`,
`ReplayWorkspaceFrameInput`, `ReplayWorldPointerInput`,
`EditorPointerPreviewInput`, `EditorPointerSelectionInput`,
`EditorGizmoDragPointerInput`, `LauncherPointerInput`, `ReplayPathPickInput`,
`RuntimePickRequest`, `ReplayStartupLoadInput`, `ReplayOverlayStateView`.

G4 independent review reopened two of those rulings. `ShadowGraphInputs` and
`ReflectionGraphInputs` each carry one borrowed pass input and their sole
consumer forwards that member unchanged, so the PB0 "ABI thunk" explanation
does not name an invariant. Both are now `remove` rows owned by
`ceremonial-aggregate-elimination` CA3.

One new `retain` ruling was made during this census: `Core/WorkerPool.h:228`
(`IndexFunctionT& indexFn = fn;`) binds a forwarding reference to an lvalue
reference so the chunk lambda can capture it. That is a language requirement, not
a preserved spelling.

## Plan-To-Row Coverage

Every `repair` and `remove` row is owned by exactly one registered plan, as
required by G4:

| Rows | Owning plan |
|---|---|
| 10 `remove` aggregates | `ceremonial-aggregate-elimination` CA1/CA2/CA3 |
| 88 `repair` scars | `extraction-scar-remediation` ES0 |
| Frame views (4) | `runtime-frame-view-retirement` |
| `RuntimeRenderBackendView` | `render-backend-service-bag-removal` |
| `OperatorCommandApplier` operation family | `operator-command-invariant-ownership` |
| `TestCoverageFloorContracts.cpp` | `coverage-gate-test-reorganization` |

No row is unattributed.

## Deliberate Non-Mechanisation

The "sole consumer destructures at entry" test is **not** gated. Deciding it
requires distinguishing a construction from a same-named local, which is not
decidable lexically without a compiler database — the same limitation
`inventory_wide_signatures.py` records for call resolution. Lexical construction
and consumer counts are reported as review context and explicitly excluded from
the gate; the self-test pins that exclusion. Gating on an unreliable proxy would
reproduce exactly the frozen-metric failure this plan exists to replace.

## Residual Risk

- The 69 multi-member candidates without a stated invariant are review context,
  not findings. Some are certainly legitimate domain values (`ParsedArgs` at 81
  members is a parsed CLI record). `ceremonial-aggregate-elimination` CA0 must
  rule them; until then they are unowned by design, not by omission.
- G4 removed the former comment-only exemption: an `Invariant:` sentence is
  review evidence but cannot make a behavior-free borrowed-member courier leave
  the gating set. Planted fixtures pin that rule, suffix-independent discovery,
  `class` discovery, behavior-owner exclusion, and strong-scalar exclusion.
