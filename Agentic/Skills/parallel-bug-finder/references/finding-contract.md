# Parallel Bug-Finding Contract

Use this contract for every subsystem lane. The coordinator rejects packets
that do not satisfy it.

## Worker Packet

Return one UTF-8 JSON object:

```json
{
  "schema_version": 1,
  "base_commit": "40 lowercase hexadecimal characters",
  "agent_id": "actual worker or task identity",
  "worktree": "absolute isolated worktree path",
  "subsystem": "exact value from the canonical CSV",
  "secondary_subsystems": [],
  "coverage": {
    "files_reviewed": ["repository/relative/path.cpp"],
    "ownership_evidence": {
      "repository/relative/path.cpp": "Why this owner belongs to the lane."
    },
    "entry_points": ["Namespace::Symbol"],
    "tests_reviewed": ["test name or path"],
    "commands": ["exact bounded source-read command with no output side effect"]
  },
  "findings": [
    {
      "root_cause_key": "stable-lower-kebab-description",
      "severity": "High",
      "title": "Concise failure statement",
      "locations": "Path.cpp:10-24; Path.h:31",
      "trigger": "Bounded reachable action or state sequence.",
      "impact": "Concrete incorrect behavior or safety consequence.",
      "confidence": "High",
      "evidence": ["Source fact, test result, or command observation."],
      "reproduction": "Smallest mechanical reproducer or focused-test shape."
    }
  ]
}
```

Use repository-relative forward-slash paths in coverage and location text.
`locations` is a semicolon-delimited list in the exact grammar
`path:start[-end]`; every token names a reviewed frozen-base blob and every
range is validated. Any tracked text-file extension is allowed. Do not add
prose, same-file shorthand, or partially specified ranges to this field.
`files_reviewed`, `entry_points`, and `commands` must be non-empty even when
`findings` is empty. `ownership_evidence` has exactly one non-empty explanation
for every reviewed file. Never invent a command or result.

Every recorded command must be source-read-only. Builds, tests, benchmarks,
executables, app launches, and commands that create or mutate intermediates,
binaries, PDBs, caches, databases, reports, traces, logs, artifacts, baselines,
or external state are forbidden in a discovery lane. `reproduction` is a
proposal for a later separately leased lane, never a command executed by this
worker. A clean Git worktree is not evidence that ignored outputs were safe.

Workers do not supply `finding_id`, `fixed`, or CSV rows. The coordinator owns
identity, duplicate disposition, and report mutation.

## Root-Cause And Ownership Rules

- One finding describes one violated invariant and one root cause. Split
  independent faults even when one test exposes both.
- Use the subsystem that owns the violated invariant as the primary subsystem.
  Record affected or collaborating owners in `secondary_subsystems`. If review
  changes the primary owner, replace the packet under the correct manifest lane
  and regenerate that wave attestation; never rewrite the subsystem or public
  ID after campaign consolidation.
- Keep `root_cause_key` stable, specific, lowercase, and hyphenated. It is a
  campaign deduplication key, not a public finding ID.
- Do not report work already explicitly owned by an active plan as newly
  discovered. Return it as an excluded observation in the handoff instead.
- Do not report a missing test by itself. Report the reachable behavior defect;
  the reproduction field explains the focused test that should pin it.

## Severity

- `High`: credible corruption, memory safety, data race, crash, security-like
  boundary break, or loss of core persisted/runtime state.
- `Medium`: reachable incorrect behavior, broken contract, deterministic drift,
  resource leak, or failure that materially impairs a supported workflow.
- `Low`: bounded correctness or diagnostics defect with limited operational
  consequence. Cosmetic preference and refactoring debt are not bugs.

## Confidence

- `High`: source path and trigger follow directly from a complete ownership/
  control-flow proof or already-existing frozen evidence; discovery lanes do
  not create new runtime evidence.
- `Medium-High`: source and reachability are strong, with one bounded runtime
  detail still inferred.
- `Medium`: plausible and source-grounded, but needs a focused reproducer before
  implementation. Do not submit confidence below Medium.

## Evidence Minimum

Every finding must answer all of these:

1. Which supported input, call path, frame sequence, or concurrent event reaches
   the defect?
2. Which exact invariant fails, and where is that invariant owned?
3. What externally observable or safety-relevant consequence follows?
4. Why is the row not a duplicate of an existing fixed or unresolved finding?
5. What smallest automated test or bounded command would reproduce it?

If any answer is speculative, omit the finding and record the investigation as
no finding for this campaign.

## Duplicate Dispositions

The consolidator blocks suspected candidate-versus-report and
candidate-versus-candidate duplicates. After source review, pass an optional
UTF-8 JSON disposition file:

```json
{
  "schema_version": 1,
  "base_commit": "40 lowercase hexadecimal characters",
  "decisions": {
    "distinct-root-cause-key": {
      "decision": "new",
      "reason": "Exact evidence showing why the apparent match is distinct."
    },
    "duplicate-root-cause-key": {
      "decision": "duplicate",
      "duplicate_of": "CORE-001",
      "reason": "Exact evidence showing the same root cause."
    }
  }
}
```

`duplicate_of` names an existing public finding ID or another candidate's
`root_cause_key`. Every decision must correspond to a candidate that the same
run classified for review. The script rejects unrelated overrides, self-links,
unknown targets, stale bases, and unsupported decisions.
