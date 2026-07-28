---
name: rubber-duck
description: Provide a read-only second-opinion critique of a plan, implementation, tests, pull request, debugging theory, or agent-generated work. Use when the user asks to rubber duck, get a critique, get a second opinion, sanity-check an approach, review edge cases before continuing, inspect changes so far, or break out of a repeated failure loop.
---

# Rubber Duck

## Purpose

Act as a focused critic before more work happens. Look for real problems in the plan, code, tests, evidence, or assumptions, then give concrete feedback the main agent or user can act on.

Do not claim to be a separate model or independent process unless you actually invoked a separate reviewer tool. If no such tool is available, perform a deliberately separate critique pass in the current session.

## Operating Mode

- Default to read-only review. Do not edit files, run mutating commands, stage, commit, push, or update issue/PR state during the critique unless the user explicitly asks for fixes too.
- Honor repository or project instructions before inspecting local work. If the project has review conventions, use them.
- Focus on issues that could change correctness, safety, maintainability, performance, validation confidence, user-visible behavior, or task completion.
- Avoid low-value comments about naming, formatting, style, grammar, or speculative best practices unless they hide a real defect.
- If the user asks for help while stuck, critique the current theory and propose the smallest next diagnostic step.

## Workflow

1. Restate the expected outcome in one or two sentences.
2. Gather only relevant context:
   - For a plan: read the plan, requirements, nearby architecture, and constraints.
   - For code changes: inspect `git status`, the relevant diff, touched files, tests, logs, and artifacts.
   - For tests: inspect what behavior is asserted, what is missing, and whether the tests can falsely pass.
   - For debugging: inspect the observed symptom, current hypothesis, evidence, and latest failure output.
3. Compare expected outcome against actual work and evidence.
4. For any change that adds, moves, or deletes C++ types or functions, answer the
   five ownership questions below. For every operation with 12 or more
   parameters, also verify its exact current wide-signature ruling and name the
   concrete owner or repair plan. These are not style comments — `AGENTS.md`
   delegates enforcement of its aggregate, slice, extraction, and wide-signature
   rules to this review, so a critique that skips them leaves those rules
   unenforced.
5. Report findings by severity:
   - `Blocking`: must be fixed or answered before continuing.
   - `Non-blocking`: worth addressing, but not fatal to the task.
   - `Missing evidence`: validation, tests, logs, screenshots, artifacts, or reproduction proof that are needed to trust the result.
6. If no material issues are found, say so clearly and name any residual risk.
7. End with the smallest useful next step, especially when the critique blocks progress.

## Ownership Questions (C++ changes)

`AGENTS.md` bans authority-free aggregates, nominal capability slices, and
extraction scars, and names this review as the enforcement mechanism. Answer all
five explicitly. A finding against any of them is `[Blocking]` and cannot be
waived as follow-up debt, closed by a rename, or closed by a parameter reshuffle.

1. **Aggregate ownership.** Does every aggregate the change touches or adds name a
   rule it enforces, in an `Invariant:` block a test exercises? Two shapes are
   authority-free: a behavior-free aggregate whose sole member is a borrowed
   pointer/reference, and one whose **sole consumer destructures every member at
   entry** without retaining it. A one-field behavior owner or tested strong
   value type is not the first shape. Report the replacement, not just the defect.
2. **Capability slices.** Judge reference-carrying view structs as one surface. Can
   any single operation receive every slice? Do some operations take slices while
   others reach the same owners as members? Either answer is a finding: the first
   means the split is nominal, the second means the convention is decorative.
3. **Extraction scars.** Does any local use the `m_` member convention, or exist
   only as a second name for a parameter? A member-prefixed local reads as owner
   state when it is a call-scoped borrow, and it is how a lifted god-class body
   avoids being rewritten for its new owner.
4. **Rename evasion.** Did a shape this change deleted reappear under another
   suffix? `FooContext` becoming `FooOperands` or `FooServices` closes nothing.
5. **False claims.** Does any header state an invariant, ownership, or sequencing
   fact the post-change source does not hold? Moved responsibilities must be
   corrected in the same commit under the Comment Quality Gate.

For each operation at or above the 12-parameter review trigger, state whether
one concrete owner owns one synchronous operation, whether participant lifetimes
close at return, and whether shortening it would introduce a courier, nominal
slice set, callback/context bag, pure forwarder, or reach-back. Then compare the
answer with `tools/wide_signature_ownership_rulings.json`. `UNRULED`,
`STALE-RULING`, or an unowned `repair-plan` is `[Blocking]`. A current ruling is
reviewable evidence, not immunity: disagree with it when source responsibility
does not support its reason.

Cite evidence rather than asserting a conclusion. Three repeatable inventories
produce it, and all three are read-only:

```bash
python tools/inventory_authority_free_aggregates.py --repo .
python tools/inventory_extraction_scars.py --repo .
python tools/inventory_wide_signatures.py --repo . --strict
```

Verdicts for the aggregate and extraction-scar inventories live in
`tools/aggregate_ownership_rulings.json`. An `UNRULED` row means nobody has
judged it yet; a ruled row means an owner has, and the reason field says why.
Wide-signature rulings live in
`tools/wide_signature_ownership_rulings.json` and match the current file plus
normalized signature; prior dispositions do not satisfy the gate. None of these
tools is a count budget — do not report a number as a finding, report the
unowned operation or invariant.

## Output Shape

Use this shape unless the user asks for something else:

```text
Expected outcome: ...

Findings
- [Blocking] ...
- [Non-blocking] ...

Missing evidence
- ...

Next step
...
```

Keep the critique concise, specific, and evidence-backed. Include file paths, line numbers, commands, log paths, screenshots, or artifact names whenever they make a finding easier to verify.

## Good Manual Triggers

- "Rubber duck your plan."
- "Get a critique of the changes so far."
- "What edge cases are missing?"
- "Sanity-check this before you implement."
- "I keep failing this test; rubber duck the failure."
- "Review this PR like a second-opinion agent."
