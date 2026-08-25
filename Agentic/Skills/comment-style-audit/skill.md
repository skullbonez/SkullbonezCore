---
name: comment-style-audit
description: Audit SkullbonezCore source comments for accurate ownership, invariants, hazards, lifetimes, units, and caller contracts.
---

# Comment Style Audit

Use this skill when asked to improve or audit SkullbonezCore comments against
`Agentic/Reference/comment-style-guide.md`.

## Scope

Default to files touched by the current change or the subsystem named by the
user. Audit the whole repository only when explicitly requested. For a
subsystem or repository pass, inventory tracked source with `git ls-files` and
record any intentionally deferred file; ignored directory names can still
contain tracked source.

Comment-only edits require no repository validation. If behavior changes, stop
and use the validation map in `AGENTS.md`.

## Procedure

1. Read `Agentic/Reference/comment-style-guide.md`.
2. List the exact source-bearing files in scope.
3. Check each comment against the post-change source and call path. Correct
   false ownership, sequencing, lifetime, unit, capacity, and behavior claims
   in the same change.
4. Keep an optional file preamble brief. It may state purpose and a few
   non-obvious ownership rules, but it must not repeat the filename or create empty
   documentation sections.
5. Move detailed explanations beside the declarations and operations they
   constrain. Use `Concept:`, `Why:`, `Invariant:`, `Lifetime:`, `Hazard:`, or
   `Units:` only when the label helps.
6. Use `Runtime allocation policy:` for a permitted allocation phase, hard cap,
   and storage owner. Do not shorten that label.
7. Preserve `CATTO REF` and `ENGINE-SPECIFIC` where they distinguish a published
   Physics algorithm from a local policy.
8. Distinguish recoverable `SbResult` failures, fatal internal invariants, and
   bounded test probes.
9. Replace acronym-only and syntax-restatement comments with the concrete reason
   or delete them when the code is already clear.
10. Keep shared engine vocabulary in
    `Agentic/Reference/engine-glossary.md`; explain a one-off term at its first
    dense use instead of copying a glossary into the file.
11. Remove decorative banners, stale task codes, branch-status prose, vague
    TODOs, and history that does not explain a current compatibility rule.
12. Preserve useful comments. Do not rewrite accurate direct prose merely to
    make the file look newly audited.

## Review questions

- Does each ownership claim name the owner that exists after the change?
- Are invalidation points and borrowed-reference lifetimes explicit?
- Are threading, ordering, units, hard caps, and deterministic behavior visible
  where a caller or maintainer needs them?
- Do public APIs explain caller-visible preconditions and results?
- Do Physics and Rendering comments identify byte-exact or GPU-state-sensitive
  behavior?
- Does the source use direct C++ and systems-programming language?
- Is the diff still comments/documentation only?

Report blocking inaccuracies with file and line evidence. If the review is
clean, say so and name any validation not run.
