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
   five ownership questions below. Treat every changed operation with 12 or more
   parameters and every changed function at 400 body lines or brace depth 6 as
   blocking compiler-backed source-design findings. Name the concrete ownership
   defect and require a real responsibility repair; there is no permission
   ledger or historical exception.
   For project-file or build-inventory changes, also inspect the fifth
   inventory: every shared file/setting divergence needs an exact current
   fingerprint ruling, and dropped list inheritance is always blocking.
   For changes that add, move, or delete C++ functions, inspect production use
   directly and use the source-design link fixture as the mechanical evidence.
   Distinguish dynamic invocation and deliberate test seams from dead surface;
   do not preserve obsolete code through a test-only call.
   For source-comment or shared-glossary changes, inspect the comment guide and
   the post-change source directly. Reject false ownership, sequencing,
   lifetime, unit, capacity, or behavior claims and oversized template preambles.
5. Report findings by severity:
   - `Blocking`: must be fixed or answered before continuing.
   - `Non-blocking`: worth addressing, but not fatal to the task.
   - `Missing evidence`: validation, tests, logs, screenshots, artifacts, or reproduction proof that are needed to trust the result.
6. If no material issues are found, say so clearly and name any residual risk.
7. End with the smallest useful next step, especially when the critique blocks progress.

## Ownership Questions (C++ changes)

`AGENTS.md` bans data-only bags with no enforced invariant, nominal capability
slices, and incomplete extractions, and names this review as the enforcement
mechanism. Answer all five explicitly. A finding against any of them is `[Blocking]` and cannot be
waived as follow-up debt, closed by a rename, or closed by a parameter reshuffle.

1. **Aggregate ownership.** Does every aggregate the change touches or adds name a
   rule it enforces, in an `Invariant:` block a test exercises? Two shapes
   enforce no rule: a behavior-free aggregate whose sole member is a borrowed
   pointer/reference, and one whose **sole consumer destructures every member at
   entry** without retaining it. A one-field behavior owner or tested strong
   value type is not the first shape. Report the replacement, not just the defect.
2. **Capability slices.** Judge reference-carrying view structs as one surface. Can
   any single operation receive every slice? Do some operations take slices while
   others reach the same owners as members? Either answer is a finding: the first
   means the split is nominal, the second means the convention is decorative.
3. **Incomplete extractions.** Does any local use the `m_` member convention, or exist
   only as a second name for a parameter? A member-prefixed local reads as owner
   state when it is a call-scoped borrow, and it is how a lifted god-class body
   avoids being rewritten for its new owner.
4. **Rename evasion.** Did a shape this change deleted reappear under another
   suffix? `FooContext` becoming `FooOperands` or `FooServices` closes nothing.
5. **False claims.** Does any header state an invariant, ownership, or sequencing
   fact the post-change source does not hold? Moved responsibilities must be
   corrected in the same commit under the Comment Quality Gate.

For each edited operation at or above 12 parameters, state whether
one concrete owner owns one synchronous operation, whether participant lifetimes
close at return, and whether shortening it would introduce a parameter wrapper, nominal
slice set, callback pack, broad frame context, pure forwarder, or reach-back. A compiler
finding from `tools/check_source_design.py` is `[Blocking]`; do not ask for or
accept a per-site exception.

For every edited function at 400 lines or nesting beyond five, state the concrete
owner and whether all parsing, arbitration, mutation, publication, or lifecycle
phases are one cohesive operation. A compiler finding is `[Blocking]`. A split into a helper
called once immediately is not decomposition when the same authority and phase
order remain in the caller; follow that helper before accepting the change.

Cite evidence rather than asserting a conclusion. Two focused reports
produce it, and both are read-only:

```bash
python tools/check_build_config_consistency.py --repo .
python tools/check_source_design.py --repo .
```

Source-design findings have no permission ledger. Repair the changed function,
parameter struct, or local code directly.
Build-configuration rulings live in `tools/build_config_rulings.json` and match
the complete current cross-project variant set for one file/setting pair.
Review touched glossary definitions directly against
`Agentic/Reference/engine-glossary.md`; consolidate duplicates instead of
recording a per-term exception.
Prior dispositions do not satisfy any gate. None of these tools is a count
budget — do not report a number as a finding, report the unowned operation or
invariant, unreachable seam, or build-contract divergence.

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
