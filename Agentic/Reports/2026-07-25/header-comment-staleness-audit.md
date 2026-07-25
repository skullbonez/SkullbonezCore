# Header Comment Staleness Audit

Date: 2026-07-25

Branch: `nightrunner-25th-JUL-26` (tip `d0e2c14f`)

Auditor: independent review pass, owner-requested

Scope: all 306 tracked headers (`.h`/`.hpp`/`.inl`) under `SkullbonezSource`,
plus all 552 tracked source-bearing files for `Related:` pointer resolution.

Status: **findings only — nothing in this audit has been fixed or committed.**

Line numbers are as of tip `d0e2c14f`. A repository-wide comment-alignment
format pass was in flight on this branch while the audit was written and has
already shifted some of them (A3 moved 337 → 326). **The search patterns, not
the line numbers, are authoritative**; re-derive positions at implementation
time. All findings below were re-verified after that format pass: the
`RunInput` count is unchanged at 14, and A2/A3/A4 all still match.

## Why This Audit Exists

The 2026-07-25 architecture review reported a "half-finished render graph"
finding: RenderGraph declared pass intent while the DX12 backend supposedly
kept hand-written barriers, giving two sources of truth for resource state.

That finding was **false**. `render-graph-completion` G0-G5 closed on
2026-07-20 and landed `Dx12RenderGraphExecutor`, which compiles declared pass
accesses into `ResourceBarrier` emission with a `DryRun` mode. The review was
misled by `RenderGraph.h`'s own header, which still said the backend "remains
the explicit owner of live transition and UAV barrier emission" and that pass
bodies use the callback model "without transferring barrier derivation to
RenderGraph." Both sentences were true when written and false after the
campaign. Corrected in `d0e2c14f` (comment-only).

The near-miss cost: a six-task campaign was nearly registered to build
something that already shipped. This audit asks whether other headers carry
the same class of falsified claim.

**Key insight for the reader:** the repository's comment regime is strong at
*presence* (every file has a learning header) and strong at *deletion* (see
Class C — retired vocabulary is clean). Its blind spot is **responsibility
movement**: when a campaign moves ownership without deleting a symbol, no
existing gate re-reads the prose that described the old owner.

## Method

1. **Pointer resolution.** Parsed every `Related:` block and tested each
   repository-relative path against the working tree.
2. **Retired vocabulary.** Searched header comments for types and subsystems
   that closed campaigns deleted (`PhysicsScene`, `PhysicsStandaloneWorld`,
   `GameModelCollection`, `GameModel`, render interfaces, SIMD kernels,
   audio/XAudio/stb_vorbis, OpenGL, DX11, `UIRenderContext`,
   `RuntimeRenderInputs`, `RuntimeRenderServices`, `InGameUIInputFrame`,
   `FlushUIDrawList`, `RenderModelPassInput`, `EntityId`).
3. **Ownership-rot language.** Searched header comments for the temporal
   hedges that produced the RenderGraph defect: `still owns`, `remains the
   owner`, `on this branch`, `currently`, `for now`, `temporarily`, `not
   yet`, `pending`, `transitional`, plus named-task references.
4. **Verification.** Every candidate in Class A was checked against live
   source before being reported. Candidates that survived scrutiny are listed
   in Class C as cleared, so this audit does not repeat the RenderGraph error
   in the opposite direction.

## Class A — Falsified Claims (verified, highest severity)

These headers assert an owner or behavior that live source contradicts. They
are capable of misleading a reviewer exactly as `RenderGraph.h` did.

### A1. `RunInput` is a phantom owner named in 14 comments across 9 files

`RunInput` **does not exist**: no file, no `class`/`struct` declaration, no
`RunInput::` qualification anywhere in the repository. Every occurrence is
prose attributing ownership to it. Verification:
`rg 'class RunInput|struct RunInput|RunInput::'` → zero rows;
`git ls-files | rg RunInput` → zero rows.

Almost certainly fallout from `source-blemish-remediation` B3, which renamed
ten `Run*` files that had zero `Run::` members. The files were renamed; the
comments describing them were not.

| File | Line | Claim |
|---|---:|---|
| `Runtime/Input/InputController.Bindings.h` | 4 | "Publishes the keyboard binding table consumed by RunInput." |
| `Runtime/Input/InputController.Bindings.h` | 9 | "while RunInput still owns what each action does" |
| `Runtime/Interaction/OperatorCommandApplier.h` | 104 | "Simulation-step reset stays in RunInput" |
| `Runtime/Interaction/OperatorCommandApplier.h` | 136 | "reporting still mirrors the UI packet that RunInput accepted" |
| `Runtime/Interaction/OperatorCommandApplier.h` | 142, 158 | "flags report accepted UI commands for RunInput action logging" |
| `Runtime/Interaction/OperatorCommandApplier.h` | 196 | "RunInput still owns …" |
| `Runtime/Editor/EditorTools.h` | 172 | "for RunInput action logging" |
| `Runtime/Editor/EditorTools.h` | 394 | "RunInput owns keybinding data" |
| `Runtime/Scene/SceneRuntimeCoordinator.h` | 99 | "for RunInput action logging" |
| `Runtime/Input/Input.cpp` | 63 | "RunInput and window/focus …" |
| `Runtime/Editor/EditorTools.cpp` | 8 | "before RunInput commits the object" |
| `Runtime/Interaction/OperatorCommandApplier.cpp` | 502 | "previous RunInput path still recorded …" |
| `Runtime/Interaction/OperatorCommandApplier.cpp` | 590 | "RunInput owns input-mode bookkeeping" |

Severity: **high**. Nine files teach a reader to look for an owner that
cannot be found, across the input/interaction boundary that
`InputRouter`/`InputController` actually own. A reader searching for
`RunInput` finds only more comments.

Remediation: replace each with the real owner (`InputRouter`,
`InputController`, or `InputFrameExecution` — determine per site; do not
bulk-substitute one name).

### A2. `RuntimeRenderResources.h` attributes backend teardown order to `Run`

`Runtime/Render/RuntimeRenderResources.h:9`:
> "RuntimeRenderer and the individual pass classes create, reset, and consume
> these resources **while Run still owns the broader backend teardown order**."

Live source contradicts this. `RuntimeRenderer::ReleaseBackendOwnedRuntimeResources`
(`RuntimeRenderer.cpp:2158`) owns the ordered release, and `RuntimeRenderer.h:34`
states the invariant: "Backend resource release begins only after a successful
GPU drain, then keeps consumer passes ahead of producer passes." `Run.cpp:374`
only *calls* it. Ownership moved during `runtime-renderer-decomposition`
RR0-RR5; the prose did not.

Severity: **high** — same shape as the RenderGraph defect (responsibility
moved, prose kept the old owner). This file also carries a broken pointer
(B6 below), so both its ownership claim and its cross-reference are wrong.

### A3. `SceneController.h` describes a completed temporary state as current

`Runtime/Scene/SceneController.h:337-338`:
> "Scene request submission stays owner-specific even while **Run temporarily
> executes the returned batch during lifecycle extraction C1**."

`TakePendingRequests` is consumed in `Runtime/Scene/SceneRequestExecution.cpp:68`,
not in `Run`. The named task (`owner-fanout-reduction` C1/lifecycle
extraction) is closed, and the "temporarily" condition it describes has
ended. A comment that cites a completed task code as live context is
guaranteed rot.

Severity: **medium-high**. Directly relevant to the registered
`invariant-ownership-governance-and-transaction-repair` GV2, whose census
reads this exact header.

### A4. `EditorOverlayTools.h` opens with a bare "Run still owns" claim

`Runtime/Editor/EditorOverlayTools.h:8`: "Run still owns runtime side
effects."

After `run-execute-deaccretion` X0-X2 and
`run-execute-frame-phase-decomposition` RX0-RX3, the binding decision is that
`Run` is "only process/frame composition." The sentence's intent is probably
"the caller applies effects, this helper does not" — but as written it
asserts a `Run` ownership the campaigns removed.

Severity: **medium**. Misleading rather than provably false; the "still"
marks it as written against a superseded state.

## Class B — Broken `Related:` Pointers (12, mechanically verified)

Every path below appears in a `Related:` block and does not resolve in the
working tree. Two are days old, from the in-flight UI campaign.

| # | File | Dead pointer | Likely cause |
|---|---|---|---|
| B1 | `Core/Profiler.h` | `Rendering/RenderProfilerPresentation.cpp` | UR4 (this campaign, 2026-07-25) |
| B2 | `Core/Profiler.cpp` | `Rendering/RenderProfilerPresentation.cpp` | UR4 (this campaign, 2026-07-25) |
| B3 | `Maths/OrbitalMechanics.h` | `Agentic/Plans/TODO/solar-system-trajectory-planner.md` | Plan deleted at closure (inventory rule 4) |
| B4 | `Maths/OrbitalMechanics.cpp` | same | same |
| B5 | `Physics/PhysicsRuntimeSettings.h` | `Agentic/Plans/TODO/physics-settings-snapshot.md` | same |
| B6 | `Rendering/RenderRasterBindingContract.h` | `Runtime/RunPasses.cpp` | Runtime package decomposition / B3 renames |
| B7 | `Runtime/Editor/LauncherLaser.cpp` | `Runtime/RunPasses.cpp` | same |
| B8 | `Runtime/Render/RuntimeRenderResources.h` | `Runtime/RunPasses.cpp` | same |
| B9 | `Runtime/Camera/AttachedCameraController.h` | `Agentic/Plans/In_Progress/authoritative-plan-01-run-composition-root.csv` | Folder banned by `Agentic/README.md` |
| B10 | `Runtime/Replay/ReplayPredictionArchive.h` | `Agentic/Plans/TODO/replay-visual-fidelity-mega-probe.md` | Plan deleted at closure |
| B11 | `Runtime/Scene/SceneEntityStore.h` | `SkullbonezSource/GameObjects/SceneController.cpp` | Pre-package-decomposition path |
| B12 | `UI/UISceneNavigationModel.h` | `Agentic/Plans/TODO/ui-runtime-separation.md` | Plan deleted at closure |

Severity: **medium**. Non-misleading individually, but this is a systemic
pattern: **inventory rule 4 deletes completed plans while source headers keep
pointing at them.** B3/B5/B10/B12 are all this. Any header citing a `TODO/`
plan is a future broken pointer by construction — the rule guarantees it.

Recommendation: headers should cite closure **reports** under
`Agentic/Reports/<date>/` (permanent) rather than `Agentic/Plans/TODO/`
(deleted by policy). Consider adding the `Related:` resolution check to
`validate_fast` — it is cheap, fully mechanical, and would have caught all 12.

## Class C — Checked And Cleared

Recorded so this audit is falsifiable and so the next reviewer does not
re-derive it.

- **Retired vocabulary: clean.** Zero header comments reference
  `PhysicsScene`, `PhysicsStandaloneWorld`, `GameModelCollection`,
  `GameModel`, retired render interfaces, `UIRenderContext`,
  `RuntimeRenderInputs`, `RuntimeRenderServices`, `InGameUIInputFrame`,
  `FlushUIDrawList`, `RenderModelPassInput`, `EntityId`, or audio subsystems.
  The comment-audit regime demonstrably works for deletions.
- `Run.h:19` names DX11/OpenGL but explicitly labels them "Retired runtime
  renderers… source backends have been removed." Accurate.
- `MathsCommon.h:33` "SSE/SIMD intrinsics are enabled in Release/Profile by
  default" refers to compiler settings, not the SIMD kernels S7 deleted.
  Accurate.
- `Core/Config.h:139` "No runtime renderer currently consumes these values"
  is explicitly scoped as the "compatibility owner for the retired projected
  blob-shadow path." Accurate.
- `UI/OperatorEditorExchange.h:13` "currently Legacy or ImGui" matches the
  standing UI ruling. Accurate.
- ~50 further `currently`/`pending`/`temporarily` hits describe **runtime
  state** ("rows currently owned by live resources", "pending query ring
  depth", "operator temporarily owns free-fly framing"). These are correct
  uses and must not be swept mechanically — a bulk edit of this vocabulary
  would damage good comments.

## Root Cause And Recommended Process Fix

Three closure gates already exist and none catches this class:

1. The comment audit checks that touched files **have** required sections and
   teach local vocabulary. It does not ask whether existing prose is **still
   true** after the change.
2. The dependency validator checks include edges, not prose.
3. Independent reviews read code for ownership defects; they treat headers as
   documentation of intent, not as claims to be falsified.

The gap is narrow and specific: **a campaign that moves responsibility
without deleting a symbol leaves every comment describing the old owner
intact, and nothing re-reads them.** Deletions are caught (Class C proves
it); movements are not (Class A proves it).

Recommended amendment — fold into
`invariant-ownership-governance-and-transaction-repair` **GV0**, which is
already editing `Agentic/Skills/comment-style-audit/skill.md`:

- Add a **claim-verification step**: for each touched file, the audit must
  identify sentences asserting ownership, sequencing, or subsystem behavior,
  and confirm each against the post-change source. Any claim the change
  falsified is corrected in the same commit.
- Add the rot-marker list (`still owns`, `remains the owner`, `currently`,
  `for now`, `temporarily`, `on this branch`, `not yet`, plus any task code
  such as "C1"/"UR3") as a prompt for that step. Presence is not a defect —
  it is a signal to verify. Class C shows why this must be judgment, not a
  banned-word gate.
- Add the rule that headers cite permanent closure **reports**, never
  `Agentic/Plans/TODO/` paths that inventory rule 4 will delete.
- Consider a mechanical `Related:` resolution check in `validate_fast`. This
  is the one fully automatable part; it would have caught all 12 Class B rows
  with no judgment required.

## Remediation Summary

| Class | Count | Severity | Suggested owner |
|---|---:|---|---|
| A — falsified ownership/behavior claims | 4 findings, 17 comment sites | High | GV0/GV1 (A3 sits inside GV2's census scope) |
| B — broken `Related:` pointers | 12 | Medium | Mechanical; batch with any touching commit |
| C — cleared | ~55 reviewed | None | No action; do not bulk-edit |

No fixes were applied. Class A remediation needs per-site judgment about the
real owner and should not be bulk-substituted. Class B is mechanical and safe
to batch. Both should carry the `AGENTS.md` comment-only documentation rule:
prove the diff is comments-only, and no repository validation is required.
