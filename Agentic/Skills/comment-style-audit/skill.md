---
name: comment-style-audit
description: Audit SkullbonezCore source comments against the repository comment-style guide. Use when source-bearing files are edited, when a user asks to inspect comment quality, or when checking learning headers, invariants, hazards, lifetime notes, and comment-standard compliance.
---

# Comment Style Audit Skill

Use this skill when asked to improve, audit, or refresh SkullbonezCore comments
against `Agentic/Reference/comment-style-guide.md`.

## Goal

Keep the code printable and learnable. A reader should be able to open a file
and understand its purpose, local vocabulary, invariants, and risky concepts
without already knowing this engine's rendering or physics architecture.

## Scope

Default to touched files or the subsystem named by the user. Only audit the
whole repository when the user explicitly asks for a full pass.

Documentation/comment-only edits require no repository validation. If code
behavior changes accidentally, stop and switch to the validation map in
`AGENTS.md`.

For subsystem or repository-wide work, completeness is mandatory. Build the file
inventory with `git ls-files`, not `rg`, because tracked source can live under
ignored directory names such as `Physics/Debug`. Create or update a checklist
plan under `Agentic/Plans/` before editing, and tick each source-bearing file
only after it has been inspected against this skill and the guide.

## Procedure

1. Read `Agentic/Reference/comment-style-guide.md`.
2. Identify the target scope:
   - Touched-file pass: list the exact source-bearing files from `git diff` and
     `git status`.
   - Subsystem or full pass: generate the tracked inventory with `git ls-files`
     and reconcile it against a checklist plan under `Agentic/Plans/`.
3. Inspect the target files for a file learning header.
4. Ensure each header has:
   - `File`
   - `Purpose`
   - `Summary`, with ownership, a decision, or a flow the filename does not
     already reveal
   - `Glossary` when the file defines single-file local vocabulary
   - `Invariants` where behavior, lifetime, determinism, or GPU state matters
   - `Related` links where another file or reference doc helps
5. Apply the glossary split rule:
   - A term defined in exactly one tracked `.cpp`, `.h`, `.hpp`, `.inl`, or
     `.hlsl` file stays in that file's `Glossary:` block.
   - A term defined in more than one tracked source file belongs in
     `Agentic/Reference/engine-glossary.md`; remove source copies and cite that
     reference from each affected file's `Related:` block.
   - Run `python tools/inventory_glossary_terms.py --repo . --strict` when the
     pass changes glossary ownership. Every multi-file finding needs an exact
     current ruling; a ruled row is migration evidence, not permission to keep
     the copies indefinitely.
   - Treat counts as current measurements, never thresholds or budgets.
6. Replace non-assumed acronym-only comments with concept comments.
7. Replace restatement comments with `Why:`, `Invariant:`, `Lifetime:`, or
   `Hazard:` comments.
8. For any type that aggregates unrelated-owner data or orchestrates
   multi-owner sequencing, require a header `Invariant:` block that names the
   rule the type enforces and identify the focused test that exercises it.
   Absence of either artifact is an audit failure; a data-only aggregate that
   merely shortens a signature remains a banned bag.
9. Verify behavioral claims in every touched file:
   - Identify sentences that assert ownership, sequencing, or subsystem
     behavior and confirm each against the post-change source and call path.
   - Correct in the same commit every claim falsified by a responsibility move.
   - Treat `still owns`, `remains the owner`, `currently`, `for now`,
     `temporarily`, `on this branch`, `not yet`, and embedded task codes such
     as `C1` or `UR3` as prompts to verify, not banned words. The repository
     audit found roughly 55 correct uses that describe runtime state.
   - Require repository-relative `Related:` entries to resolve. Cite permanent
     closure reports, never deletion-bound `Agentic/Plans/TODO/` paths.
10. Keep comments close to the concept they explain.
11. Preserve existing useful teaching comments. Do not rewrite good comments
   just to make them look new.
12. Tick each checklist item only after the file was inspected. Leave deferred
   files unchecked and record the reason beside the item.
13. Rerun the scoped `git ls-files` inventory before reporting completion and
    confirm every tracked source file in scope appears in the checklist exactly
    once.
14. Confirm the diff is comment/documentation only before reporting completion.

## Why Claim Verification Exists

`RenderGraph.h` once retained a pre-completion claim that DX12 still owned live
barrier derivation after `Dx12RenderGraphExecutor` had taken that responsibility.
An architecture review trusted the stale header and nearly registered a
six-task campaign to rebuild shipped work. Verify responsibility claims against
post-change source so ownership moves cannot create the same false finding.

## Checklist

- No unexplained local, ambiguous, or behavior-sensitive acronyms in comments.
- Every `Summary:` adds ownership, decision, or flow information beyond the
  filename; tautological summaries fail the audit.
- File glossaries contain only exact single-file terms. Multi-file definitions
  live in `Agentic/Reference/engine-glossary.md`, source headers cite it from
  `Related:`, and the strict glossary inventory has no unruled current finding.
- Never add glossary entries that merely define assumed baseline technology
  names such as HLSL, DirectX, Direct3D, DX12/D3D12, DXR, C++, CPU, GPU,
  shader, texture, compiler, or linker.
- Rendering files explain RTV, DSV, SRV, UAV, PSO, root signatures, resource
  states, barriers, DRED, PIX, BLAS, TLAS, or SBT when those terms appear.
- Physics files explain broadphase, narrowphase, manifolds, contact rows, warm
  starting, sleep, and determinism when those terms appear.
- Scene/runtime files call out command-line and scene-file compatibility.
- UI files call out the request/command contract and draw/hitbox consistency.
- Tools call out bounded output and validation purpose.
- Aggregate/transaction types name their enforced invariant and focused test;
  data-only parameter bags fail the audit.
- Ownership, sequencing, and behavior claims match post-change source; every
  rot marker was reviewed as a prompt rather than rejected mechanically.
- Repository-relative `Related:` entries resolve and permanent history points
  to closure reports rather than live `TODO/` plans.
- No source file in the selected scope is silently skipped. The checklist has no
  unchecked items unless each remaining item has a written deferral reason.

## Report Format

Summarize:

- Files or subsystems audited.
- Checklist path, checked count, deferred count, and unchecked files if any.
- The comment/documentation changes made.
- The ownership, sequencing, and behavior claims verified or corrected.
- Any terms that still need human-approved wording.
- Validation status, usually: `No validation run; comment-only changes.`
