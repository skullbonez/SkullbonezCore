# Comment Style Audit Skill

Use this skill when asked to improve, audit, or refresh SkullbonezCore comments
against `Agentic/Reference/comment-style-guide.md`.

## Goal

Keep the code printable and learnable. A reader should be able to open a file
and understand its purpose, local vocabulary, invariants, and risky concepts
without already being a rendering or physics specialist.

## Scope

Default to touched files or the subsystem named by the user. Only audit the
whole repository when the user explicitly asks for a full pass.

Documentation/comment-only edits require no repository validation. If code
behavior changes accidentally, stop and switch to the validation map in
`AGENTS.md`.

## Procedure

1. Read `Agentic/Reference/comment-style-guide.md`.
2. Inspect the target files for a file learning header.
3. Ensure each header has:
   - `File`
   - `Purpose`
   - `Mental model`
   - `Glossary`
   - `Invariants` where behavior, lifetime, determinism, or GPU state matters
   - `Related` links where another file or reference doc helps
4. Replace acronym-only comments with concept comments.
5. Replace restatement comments with `Why:`, `Invariant:`, `Lifetime:`, or
   `Hazard:` comments.
6. Keep comments close to the concept they explain.
7. Preserve existing useful teaching comments. Do not rewrite good comments
   just to make them look new.
8. Confirm the diff is comment/documentation only before reporting completion.

## Checklist

- No unexplained local acronyms in comments.
- Rendering files explain RTV, DSV, SRV, UAV, PSO, root signatures, resource
  states, barriers, DRED, PIX, BLAS, TLAS, or SBT when those terms appear.
- Physics files explain broadphase, narrowphase, manifolds, contact rows, warm
  starting, sleep, and determinism when those terms appear.
- Scene/runtime files call out command-line and scene-file compatibility.
- UI files call out the request/command contract and draw/hitbox consistency.
- Tools call out bounded output and validation purpose.

## Report Format

Summarize:

- Files or subsystems audited.
- The comment/documentation changes made.
- Any terms that still need human-approved wording.
- Validation status, usually: `No validation run; comment-only changes.`
