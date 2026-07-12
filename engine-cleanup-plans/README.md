# Engine Cleanup Plans

Date: 2026-07-10 (reconciled)
Status: In progress
Owner: architecture cleanup

Start with [`Agentic/Plans/MASTER-PLAN.md`](../Agentic/Plans/MASTER-PLAN.md),
the authoritative inventory of every live plan and its checked-phase count.
The campaign protocol is [`00-EXECUTION-GUIDE.md`](00-EXECUTION-GUIDE.md).

The current audit scope is 406 tracked engine/shader source-bearing files and
172,036 lines. The campaign targets substantive risks: unsafe failure paths,
missing mandatory test execution, god-object ownership, replay size, physics/
scene authority, and concrete renderer ownership. Documentation volume or
spelling changes are not architecture evidence.

## Quality Rules

1. Acceptance is behavioral or structural and measurable: an owner/state
   surface moves, a type/method is deleted, a fault is caught, or a named test
   proves the contract.
2. Status uses checked counts, not subjective percentages.
3. Each phase names its owner, dependency, deletion/behavior proof, and exact
   validation gate.
4. No plan adds regex/frozen-count architecture budgets as a substitute for
   design and behavioral tests.
5. Completed plans/checklists are deleted; git history is the archive.
6. Live plans stay in `Agentic/Plans/TODO/`; do not recreate historical status
   folders.

## Current Campaign File

The [aggregate closure report](../Agentic/Reports/2026-07-12/engine-cleanup-aggregate-closure.md) maps external-review findings to their
owning TODO plans and carries the remaining 15.6 comment-reference cleanup.

Owner decisions recorded in
[`HANDOFF-2026-07-09-OWNER-DECISIONS.md`](HANDOFF-2026-07-09-OWNER-DECISIONS.md)
remain binding unless the owner changes direction.

## Validation

These files are documentation. Editing them requires no repository validation.
Implementation plans name their required source/test/runtime gates. Always
verify plan links, inventory counts, `git diff --check`, and that the diff is
Markdown-only before reporting plan work complete.
