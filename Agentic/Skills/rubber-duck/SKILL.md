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
4. Report findings by severity:
   - `Blocking`: must be fixed or answered before continuing.
   - `Non-blocking`: worth addressing, but not fatal to the task.
   - `Missing evidence`: validation, tests, logs, screenshots, artifacts, or reproduction proof that are needed to trust the result.
5. If no material issues are found, say so clearly and name any residual risk.
6. End with the smallest useful next step, especially when the critique blocks progress.

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
