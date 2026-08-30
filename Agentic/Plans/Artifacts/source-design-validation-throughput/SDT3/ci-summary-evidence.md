# SDT3 CI Summary Evidence

Date: 2026-08-30

`validate_fast.bat` retains its existing self-test group followed by its live
scan group. Both now call `run_retained_policy_group.ps1`, which launches the
same three direct checkers concurrently, redirects each child once, waits for
exact exit values, and removes its validated per-run temporary directory.

## Single-Invocation Evidence

- Source design appears once in the group command inventory.
- Self-test mode adds only `--self-test`; live mode uses the checker's bounded
  automatic default.
- Successful source-design output is emitted into the existing caller stream,
  so the mandatory workflow's existing `Tee-Object` writes the bounded summary
  into `mandatory-cpu-lane.log` without another scan.
- Failed child output is emitted from the captured invocation rather than
  replaying the command.
- Live output documents the non-executed serial diagnostic command:
  `python tools\check_source_design.py --repo . --jobs 1`.

## Local Integration Checks

The self-test group returned exit 0 and exposed the source-design PASS line plus
its complete one-line summary. The build-configuration and deterministic-math
self-tests also returned zero without a replay.

A live group run exercised the current branch-wide policy-failure path. It ran
19 sources and 138 contexts with 138 Tidy plus 138 Query processes, four
configured and observed workers, zero infrastructure errors, and 94.557 seconds
elapsed. It returned the expected group exit 8 after emitting the 22 existing
source findings once.

With a temporary `GITHUB_STEP_SUMMARY` target, that same live invocation wrote a
Markdown table containing source/context counts, both process counts,
configured and observed workers, discovery/Tidy/Query/dead-code/total seconds,
findings, and infrastructure errors. The generated table was inspected and the
temporary file was deleted.

The repository plain-language scan now passes after replacing two retired
phrases in an unrelated inactive WNF plan with direct `unrelated-value record`
wording. This wording-only prerequisite changes neither that plan's meaning nor
its activation state.

No workflow split, second source-design scan, header-context pruning, matcher,
threshold, test, coverage, Physics evidence, or golden baseline changed.
