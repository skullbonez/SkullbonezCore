# Small Findings Hardening Closure

Date: 2026-07-18

Branch: `nightrunner-17th-july`

H4 elapsed: approximately 20 minutes

## Closure Result

All five hardening tasks are complete:

- H0 ratified the complete 153-site cast census, startup ownership for the
  lock validator, pipeline-owned PSO identity, the exact 19-TU JSON cold
  boundary, and the two UI-tab cohesion rulings.
- H1 removed the final process singleton and gave startup/tests explicit,
  fixed-capacity lock-validator ownership.
- H2 replaced raw root-signature address identity with a nonzero monotonic
  pipeline-owner identity and ordered PSO teardown before signature release.
- H3 reconciled all 153 original cast/pointer sites to 88 ruled retained or
  centralized private boundaries, with zero unruled sites.
- H4 made JSON reachability an exact graph invariant, extracted the complete
  profiler histogram owner, and recorded why the memory tab remains cohesive.

`tools/validate_project_filters.py` now resolves local quote and angle includes,
builds reverse adjacency, and flood-fills from `nlohmann/json.hpp`. The computed
translation-unit set must equal the ratified 19-entry cold-boundary set: a new
hot-path leak and an accidentally disconnected cold owner both fail. A bounded
adversarial fixture proved exact equality, angle includes, an `A <-> B` include
cycle, one unexpected hot TU, and all 19 missing-boundary failures.

`UITabProfiler.cpp` is now a 945-line tree/timeline/worker coordinator.
`UITabProfilerHistogram.cpp` is a 1,235-line concrete implementation owner for
option caching/remapping, the bounded sample ring, panel input, and drawing.
All moved and retained function bodies were byte-equivalent to their pre-split
definitions after line-ending normalization. `UITabMemory.cpp` remains intact:
its overlay counters, replay policy controls, samples, and reserve-event rows
all mutate one `UIMemoryOverlayState` and share one scroll/layout transaction.

The independent closure review first found that forward DFS memoization could
misclassify a future cyclic include component. Reverse reachability replaced
that algorithm, the adversarial cycle control passed, and the repeat review
reported zero blocking and zero non-blocking findings.

No baseline, golden, screenshot, authored-data, coverage-floor, protected
render consumer interface, or `FRAME_COUNT = 2` change occurred.

## H4 Validation

- Focused `Profile|x64` build: passed in 9.975s.
- Histogram extraction equivalence probe: all private helpers and seven public
  API functions were byte-equivalent to `HEAD` after line-ending normalization.
- Adversarial JSON-fence fixture: passed exact, angle-include, cyclic-include,
  unexpected-TU, and missing-boundary controls.
- `tools\validate_fast.bat`: passed in 56.089s with zero build warnings/errors,
  exact 19/19 JSON reachability, and 291/291 doctest cases with 21,455/21,455
  assertions.
- `tools\validate_project_filters.bat`: passed in 2.651s with 0 errors and 740
  project/filter items across the three production projects.
- `tools\validate_full.bat`: passed in 138.830s. Every CPU and coverage lane,
  Automation replay/prediction smoke, DX12 renderer with zero InfoQueue errors
  and matching captures, and physics with the 44,401-line varied-scene CSV
  byte-exact all passed.

Comment audit:
`Agentic/Reports/2026-07-18/small-findings-h4-comment-audit.md` — **4/4
checked, 0 deferred, 0 unchecked**.
