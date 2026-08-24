---
name: parallel-bug-finder
description: >-
  Run a read-only, subsystem-sharded bug hunt across SkullbonezCore using one
  isolated worktree per active worker, the live subsystem taxonomy from
  Agentic/Bugs/master_bug_report.csv, evidence-backed finding packets, and
  deterministic coordinator consolidation. Use when the user asks to find,
  hunt, audit, discover, or inventory bugs in parallel, asks for one bug-search
  lane per subsystem, or wants the master bug report refreshed without changing
  production code.
---

# Parallel Bug Finder

Search every current subsystem for defensible bugs without mixing discovery and
repair. The coordinator derives the taxonomy from the canonical bug report,
fills useful agent slots with distinct subsystem lanes, validates structured
finding packets, and remains the only writer of the canonical CSV.

This is a discovery skill. Do not edit production source, tests, plans,
baselines, project files, or validation rules during the hunt. If the user also
wants fixes, finish and consolidate the hunt first, then hand accepted rows to
`Agentic/Skills/parallel-orchestrator/SKILL.md` as a separate implementation
wave.

## Load The Repository Contract

Before fan-out, read `AGENTS.md`, `README.md`, `Agentic/README.md`,
`Agentic/SessionState.md`, this skill, and
`references/finding-contract.md`. Read active `Agentic/Plans/TODO/` plans far
enough to identify ownership and avoid reporting planned work as a new bug.

Use `Agentic/Bugs/master_bug_report.csv` as the canonical report. Its current
distinct `subsystem` values are the scheduling taxonomy. Never freeze a copied
subsystem list in prompts, scripts, or this skill. If the report is missing or
its schema is invalid, stop before dispatch and report the governance defect.

When `.codegraph/` exists, check it and use CodeGraph before broad text search.
Confirm every important graph result against current source. Continue with
targeted `rg` and source reads when the index is unavailable or stale.

## Establish A Frozen Hunt Base

1. Require a clean coordinator worktree or identify every pre-existing change
   as user-owned and outside the hunt.
2. Record the full base commit and current branch.
3. Discover live agent capacity. The coordinator occupies one slot and owns
   consolidation; every other occupied slot maps one-to-one to one worker and
   one isolated worktree for the lane's complete lifetime.
4. Never let two active workers share a worktree, never give one active worker
   two worktrees, and never run two lanes for the same subsystem.
5. Treat an active implementation plan or bug fix as a subsystem lease. Exclude
   its complete lease set from the hunt unless the coordinator explicitly
   schedules a read-only audit against a separate frozen commit.

Generate a deterministic wave manifest:

```powershell
python Agentic/Skills/parallel-bug-finder/scripts/plan_subsystem_hunt.py `
  --repo . `
  --worker-slots <available-worker-slots> `
  --base-commit <full-commit> `
  --leased-subsystem "<exact CSV subsystem>" `
  --output TestOutput/bug-hunt/manifest.json
```

Repeat `--leased-subsystem` for every unavailable lease. The manifest covers
all remaining taxonomy values over as many waves as needed; available slots
limit simultaneous execution, not campaign scope.

## Create One Lane Per Subsystem

Create each worker branch and worktree from the exact frozen base. Use a stable
branch name such as `codex/bug-hunt-<subsystem-token>-<wave>`. A lane may read
shared files but owns only one primary subsystem. If evidence crosses an owner
boundary, record all secondary subsystems in the packet; do not silently widen
the lane into a second subsystem search.

Dispatch exactly this information:

```text
Authority: read-only parallel bug discovery
Base commit: <full hash>
Coordinator branch: <branch>
Worker branch/worktree: <branch and absolute path>
Primary subsystem: <exact current CSV value>
Known rows: <finding ids already assigned to this subsystem>
Active plan/fix exclusions: <owners and scopes that are not new findings>
Required output: one JSON packet matching references/finding-contract.md
Prohibited: source/test/plan/report edits; baseline changes; commits; pushes;
ledger lifecycle changes; fixes; every build, test, benchmark, executable/app
launch, or command that creates/mutates an intermediate, binary, PDB, cache,
database, report, trace, log, artifact, or other ignored/external output
```

Each lane inspects the subsystem's production entry points, ownership
boundaries, retained state, failure paths, public contracts, tests, fixtures,
and existing report rows. Search for concrete correctness failures: unsafe
lifetimes, bounds errors, races, stale state, invalid error handling,
determinism violations, resource leaks, contract mismatches, and missing
validation of externally reachable inputs. Do not report formatting debt,
subjective design preferences, frozen-count differences, or a hypothetical
failure with no reachable trigger.

Workers may run only source-read operations that have no filesystem or external
side effects, such as bounded CodeGraph reads, `git show`/`git grep`/
`git ls-files`, `rg`, and targeted file reads. They do not build, run tests,
launch an executable, generate a cache/report, or touch a mutable validation
resource during discovery. When runtime confirmation is needed, the packet
records the smallest proposed reproduction and the coordinator dispatches it
later through a separately leased bug-fix/reproduction lane. This prohibition
is what makes every subsystem hunt safe to overlap without hidden output-path
collisions; a worker may not waive it because its source worktree is clean.

## Require Evidence, Including For No-Finding Lanes

Every worker returns one packet even when it finds no bugs. The packet records
the exact base, primary and secondary subsystems, worktree, coverage, source-
read commands, and findings. Each finding must have one root cause, exact source
locations, a bounded reachable trigger, concrete impact, confidence, supporting
evidence, and a mechanical reproduction or focused-test proposal that was not
executed in the discovery lane.

Preserve each packet outside its worker worktree. At consolidation, the worker
tree must still be a registered, clean, branch-attached Git worktree at the
frozen base. Reviewed paths must exist at that commit, and each has explicit
ownership evidence. Agent identities, worktrees, and subsystem packets must be
unique within the wave.

Read `references/finding-contract.md` for the complete schema and review rubric.
Reject packets that invent finding IDs, mark rows fixed, omit meaningful
coverage, change the base commit, use an unknown subsystem, or bundle unrelated
root causes.

## Verify Each Wave Without Mutating The Report

After a wave completes, preserve its packets outside the worker worktrees and
run:

```powershell
python Agentic/Skills/parallel-bug-finder/scripts/consolidate_findings.py `
  --repo . `
  --base-commit <full-commit> `
  --manifest TestOutput/bug-hunt/manifest.json `
  --wave <wave-number> `
  --packet <packet-one.json> `
  --packet <packet-two.json> `
  --output TestOutput/bug-hunt/wave-<wave-number>-attestation.json
```

The script requires the complete packet set for the selected manifest wave,
validates Git worktree/base/cleanliness and reviewed-file existence, rejects
conflicting root-cause keys and shared public-ID prefixes, derives stable next
IDs for preview, and records hashes of the verified packets and worktrees. A
wave result has `authoritative_for_report: false`; it is an attestation, not a
CSV update. Preserve it with the packets, then the verified worker worktrees may
be removed safely.

Do not edit `Agentic/Bugs/master_bug_report.csv` between manifest creation and
final campaign consolidation. The manifest binds its exact frozen-base blob by
SHA-256, so any intervening report change fails closed.

## Consolidate The Whole Campaign

After every non-leased subsystem has a wave attestation, pass every packet and
every attestation to one campaign-wide run:

```powershell
python Agentic/Skills/parallel-bug-finder/scripts/consolidate_findings.py `
  --repo . `
  --base-commit <full-commit> `
  --manifest TestOutput/bug-hunt/manifest.json `
  --all-waves `
  --attestation TestOutput/bug-hunt/wave-1-attestation.json `
  --attestation TestOutput/bug-hunt/wave-2-attestation.json `
  --packet <every-preserved-packet.json> `
  --output TestOutput/bug-hunt/campaign-candidates.json
```

This run verifies every packet hash against its original clean-worktree
attestation, requires all manifest waves, and performs duplicate detection and
ID allocation across the whole campaign. This is the only output eligible to
become report rows.

For suspected duplicates, review the named source and evidence. If the
candidate is genuinely distinct, or is a duplicate that should be dropped,
record the decision and reason in the dispositions format from
`references/finding-contract.md`, then rerun the same `--all-waves` command with
`--dispositions <file>`. Do not assign an ID manually around an unresolved
duplicate warning.

The coordinator must inspect every proposed row against source and the existing
fixed and unresolved rows. The packet's primary subsystem must own the violated
invariant, with affected owners retained as secondary evidence. If review
changes that owner, create a corrected packet for the owning manifest lane,
replace that lane's wave attestation, and rerun `--all-waves`; never hand-edit
the subsystem or ID after deterministic allocation. Use `apply_patch` to update
the canonical CSV only when the final output says `scope: all-waves`,
`authoritative_for_report: true`, and has no `review_required` rows. Preserve
column order, quote fields correctly, set new `fixed` values to `No`, and never
renumber existing findings.

Discard duplicate, speculative, already-owned, or non-reproducible candidates
instead of weakening their wording to make them fit.

## Complete Every Wave

Refill freed slots with the next distinct subsystems from the manifest until
each non-leased subsystem has returned a valid packet. A failed lane may be
retried in a fresh worktree at the same base; it does not authorize skipping
that subsystem. If a subsystem remains leased, record the exact owner and a
future checkpoint rather than claiming full coverage.

Remove a worker worktree only after its packet, command evidence, and successful
wave attestation are preserved. No worker branch needs a commit because the
hunt is read-only.

## Validate And Hand Off

Run these focused checks after any skill or report change:

```powershell
python Agentic/Skills/parallel-bug-finder/scripts/plan_subsystem_hunt.py --self-test
python Agentic/Skills/parallel-bug-finder/scripts/consolidate_findings.py --self-test
python -m py_compile Agentic/Skills/parallel-bug-finder/scripts/plan_subsystem_hunt.py
python -m py_compile Agentic/Skills/parallel-bug-finder/scripts/consolidate_findings.py
python <skill-creator>/scripts/quick_validate.py Agentic/Skills/parallel-bug-finder
git diff --check
```

Report the frozen base, waves completed, exact subsystem coverage, leased or
failed lanes, packet paths, candidates accepted/rejected as duplicates, report
rows added, commands run, and final worktree status. Do not say the repository
is bug-free; report only the bounded coverage and evidence obtained.
