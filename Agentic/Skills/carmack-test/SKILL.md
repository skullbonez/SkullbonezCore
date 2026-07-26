---
name: carmack-test
description: Run a hard-nosed Carmack-inspired engine quality evaluation. Use when the user asks whether SkullbonezCore or another game engine is robust, performant, encapsulated, deterministic, commercially credible, suitable as a physics engine, or whether an elite systems programmer would rate, buy, or use it.
---

# Carmack Test

Use this skill to produce an evidence-backed engine-quality critique with the
standards of a demanding systems programmer: simple architecture, tight data
ownership, predictable performance, deterministic simulation, minimal hidden
state, direct debugging paths, and validation that catches real regressions.

## Voice And Boundary

- Do not impersonate John Carmack or claim actual endorsement. Frame the result
  as a "Carmack-style verdict" or "Carmack-test verdict."
- Do not write "Would he use it?" or "Would he buy it?" Write "Would a
  Carmack-style systems programmer use it?" or "Would this pass a
  Carmack-style buy/use bar?"
- Be blunt but technical. Prefer specific defects over rhetorical criticism.
- Findings come first, ordered by severity, with file and line references when
  local source is available.
- Separate fact from inference. Say "the evidence shows" for sourced findings
  and "I infer" for judgment calls.
- Do not reward complexity. A clever abstraction only scores well if it reduces
  coupling, failure modes, or runtime cost.

## Startup

For SkullbonezCore, follow the repository startup contract before evaluating:

1. Read `AGENTS.md`, `README.md`, `Agentic/README.md`, and
   `Agentic/SessionState.md`.
2. Run `git status --short --branch` and treat dirty files as user-owned.
3. Determine the evaluation scope: whole engine, current branch, PR, plan,
   subsystem, or single diff. If unclear, default to the current checkout.
   For a whole-engine verdict, explicitly inventory the required coverage areas
   in `Evidence Ledger`; do not issue a strong verdict from a narrow sample.
4. State the impact area: runtime ownership, physics, render/DX12, scene/data,
   tooling, tests, documentation, or mixed.
5. Documentation-only reports require no repository validation. Do not run
   `tools\validate_*` unless the user asks for validation or the task becomes
   PR-bound implementation work.

## Evidence Gathering

Build the critique from concrete evidence, not vibes:

- Use `git diff`, `git log`, and `git status` when evaluating a branch or PR.
- Use `rg --files` and targeted reads to inspect architecture rather than
  scanning the whole repository blindly.
- Inspect the owning headers and call sites for any subsystem being judged.
- Prefer committed validation logs, baselines, reports, and plans before asking
  for expensive new runs.
- For physics diagnostics, use SkullScope/query workflows instead of loading
  large raw CSV, NDJSON, or SQLite artifacts into context.
- For performance claims, check whether evidence is machine-comparable, warm,
  repeatable, and targeted to the changed path.

## Evidence Ledger

Before scoring, create a compact evidence ledger. Each row must cite files,
logs, baselines, reports, or explicitly say `insufficient evidence`.

For a whole-engine verdict, cover these rows:

| Area | Required Evidence |
|------|-------------------|
| Runtime ownership | Composition root, owner types, boundary checks, call sites |
| Encapsulation | Dependency direction, friends, globals, handles, read-only views |
| Physics | Step boundary, solver determinism, collision coverage, diagnostics |
| DX12/render | Render graph, resource lifetime, InfoQueue/screenshot evidence |
| Performance | Hot paths, allocations, perf logs, benchmark comparability |
| Diagnostics | Debug commands, SkullScope/query flow, crash/invariant visibility |
| Validation | Required gates, baselines, architecture tests, stale evidence risk |
| Data/assets | Scene/assets/config ownership and runtime registration |
| Maintainability | Code-reading cost, comments where needed, plan/report honesty |

If the user asks for a focused subsystem review, mark unrelated rows
`out of scope`; do not let out-of-scope rows affect the subsystem score.

## Rubric

Score each area from 0 to 5:

| Score | Meaning |
|-------|---------|
| 0 | Unusable or unreviewable; core evidence is missing. |
| 1 | Fragile prototype; likely to fail outside narrow demos. |
| 2 | Useful toy or learning engine; major architecture debt remains. |
| 3 | Credible indie-engine foundation with clear risks. |
| 4 | Strong subsystem or engine core; remaining problems are bounded. |
| 5 | Shipping-quality, simple, fast, deterministic, and well-instrumented. |

Rate at least these categories:

- Runtime ownership and composition-root discipline
- Encapsulation and dependency direction
- Physics correctness, determinism, and data boundaries
- Performance model and allocation behavior
- Render graph/resource lifetime/DX12 validation
- Debuggability and observability
- Test, baseline, and validation integrity
- Data-driven assets/scenes/configuration
- Maintainability and code-reading cost

Every scorecard row must include a citation or `insufficient evidence`. Do not
fill score rows from intuition alone.

## Hard Checks

Treat these as serious negative findings unless evidence proves they are
contained:

- A physics step depends on broad game-object or renderer ownership.
- Runtime state is reachable through globals, singletons, friends, or callback
  chains without clear lifetime rules.
- Hot paths allocate, format strings, walk maps, or cross subsystem boundaries
  per frame without measurement.
- Validation is stale, optional, machine-specific, or not wired to the changed
  behavior.
- Physics determinism is asserted without byte-exact baselines or targeted
  replay evidence.
- Render resource ownership is split between declarative graphs and hidden
  backend side effects.
- Error handling logs and continues after state corruption or device/physics
  invariant failure.
- The code has "manager" objects that own unrelated policy, storage, execution,
  and presentation state.
- An aggregate carries data for one operation without enforcing a rule. A
  **single-member** aggregate is authority-free by definition — it cannot shorten
  a signature, so it exists only to add a name. So is one whose sole consumer
  destructures every member at entry. Two aggregates with identical member lists
  are one aggregate or none.
- Reference-carrying view or slice structs partition an owner's member list, and
  some operation receives every slice. Judge the slice set as one surface: if one
  call reaches all of it, the split is nominal. A convention followed by some
  operations on a path and bypassed by others is decorative.
- A local uses the `m_` member convention, or exists only as a second name for a
  parameter. This is how a function body lifted out of a god class avoids being
  rewritten for its new owner, and it lies about lifetime: `m_x` reads as owner
  state when it is a borrow that expires at the next `return`.
- A deleted banned shape reappeared under a different suffix, or a header states
  an invariant, ownership, or sequencing fact the current source does not hold.
- A test file is named for a coverage gate, a metric, or a plan rather than the
  subsystem whose behavior it pins.
- Plans are ticked without matching source changes, validation evidence, and
  independent review.

Evidence for the aggregate and local findings above comes from read-only
inventories rather than impression:

```bash
python tools/inventory_authority_free_aggregates.py --repo .
python tools/inventory_extraction_scars.py --repo .
```

Verdicts live in `tools/aggregate_ownership_rulings.json`. `UNRULED` means nobody
has judged the row yet. None of these is a count budget: report the unowned
invariant, never the number.

Verdict caps:

- Any unresolved hard-check defect caps the whole-engine verdict at `Not yet`.
- Missing whole-engine ledger rows cap the verdict at `Insufficient evidence`.
- No byte-exact or replay-backed physics determinism evidence caps physics at
  `2 / 5`.
- No targeted performance evidence caps performance at `2 / 5`.
- No DX12 validation or screenshot evidence caps render/resource lifetime at
  `2 / 5`.
- A serious ownership leak from physics/render into runtime policy caps
  encapsulation at `2 / 5`.
- An unruled authority-free aggregate, nominal capability slice, or extraction
  scar caps encapsulation at `3 / 5`. A rename or a parameter reshuffle does not
  lift the cap; only an owned invariant or a deletion does.

## Positive Evidence

Give credit only when the repository shows it:

- Narrow ownership types, stable handles, explicit contexts, and read-only views
  that reduce friendship and direct mutation.
- Deterministic physics gates with byte-exact baselines.
- DX12 InfoQueue validation with screenshot comparisons.
- Architecture tests that fail on boundary regressions.
- Simple fixed-step paths and reproducible launch commands.
- Data-driven scenes/assets with registered asset names.
- Diagnostics that answer focused questions with small query outputs.
- Reports and plans that state residual risk instead of pretending completion.

## Physics Engine Suitability Criteria

When judging whether the engine is viable as a physics engine, check:

- Public integration boundary: can physics be used without dragging in runtime,
  renderer, editor, or scene UI ownership?
- Determinism: byte-exact baselines, fixed-step replay, and cross-run evidence.
- Solver quality: contacts, friction, restitution, stacking, joints/constraints,
  sleep/island behavior, and stability under stress.
- Collision coverage: primitive, convex, hull, terrain, broadphase, ray/query,
  and known missing CCD or tunneling behavior.
- Scaling model: island cost, broadphase cost, persistent contact reuse,
  allocation behavior, and perf counters.
- Debuggability: focused trace/query workflow, body/contact/island inspection,
  reproduction commands, and small model-ingested outputs.
- Portability: dependency footprint and whether the API is engine-agnostic
  enough to embed in another title.

## Output Format

Use this structure unless the user asks for a different format:

```markdown
**Carmack-Test Verdict**
Would a Carmack-style systems programmer use it? <No / Not yet / For this subsystem / Yes with conditions>
Would it pass as a standalone physics engine? <answer>
Would it pass a serious buy/rate bar? <answer with confidence and conditions>

**Evidence Ledger**
| Area | Coverage | Evidence |

**Worst Things**
1. <severity> <finding> - <file:line evidence or explicit evidence gap>

**Best Things**
1. <finding> - <file:line evidence or validation/report evidence>

**Scorecard**
| Area | Score / 5 | Evidence | Reason |

**Physics Engine Suitability**
<direct assessment of determinism, solver quality, collision coverage, data
ownership, debug tooling, and missing proof>

**Robustness And Encapsulation**
<direct assessment of lifetimes, ownership, boundaries, validation, and failure
behavior>

**Performance Judgment**
<direct assessment of hot paths, allocation model, benchmark quality, and
remaining unknowns>

**Required Fixes Before A Strong Yes**
1. <fix> - <expected evidence and smallest validation/report needed>

**Validation Gaps**
- <missing command, missing baseline, missing test, or evidence limitation>
```

Minimum acceptable sample shape for a strong finding:

```markdown
1. Blocking: Physics still depends on runtime-owned scene state during step.
   Evidence: `SkullbonezSource/...:123`; no isolated physics API found.
   Verdict impact: caps physics-engine suitability at `2 / 5` until the step
   boundary is proved engine-agnostic with deterministic validation.
```

## When Asked For Plans

If the user asks to turn findings into plans:

- Create one Markdown plan per top issue under `Agentic/Plans/`.
- Include explicit checklists for implementation, validation, evidence capture,
  and independent review.
- Name the smallest repository validation command required by `AGENTS.md`.
- State when no validation is required because the work is documentation-only.
- Do not mark a checkbox complete unless the work, evidence, and review exist.
