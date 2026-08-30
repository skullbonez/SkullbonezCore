# SDT0 Reference Measurement

Date: 2026-08-30

This receipt records the Git-derived reference workload and the serial checker
behavior before source-design throughput changes. It deliberately stores counts
and a canonical identity digest rather than a source-coordinate list that could
become a policy allowlist.

## Reference Workload

- Pull request: 162
- Workflow run: `33244168782`, job `99078605186`
- Base: `7376c69ae6aa32c570dc1ee95fa96e958ba1de42`
- Head: `26baa61e8b92173db46200c9bd306a344b31364f`
- Selected C++ files: 79
- Distinct source/context identities: 623
- Canonical identity SHA-256:
  `d9e4b117d81e3ae8ca5f29376335f7c219bdc47cd94c4438d4cb4a7a0da5db77`
- Existing process count: 623 Tidy plus 2,492 Query, 3,115 total

The count was regenerated from the pull request's exact Git base/head with
`changed_sources`, `scan_repository`, and `compile_contexts` from
`tools/check_source_design.py`. Each digest row contains repository-relative
source, project, configuration, and the complete compiler-argument tuple.

## Serial Before Runs

Both clean runs used LLVM 22.1.3 locally, `SKORE_SIZE_DIFF_BASE` set to the
exact pull-request base above, and the unmodified checker at the reference head.
The process tree was sampled every 100 ms; peak committed bytes are the Python
coordinator plus its non-pre-existing Tidy or Query child.

| Run | Result | Files | Contexts | LLVM launches | Elapsed | Peak committed |
|---|---|---:|---:|---:|---:|---:|
| 1 | PASS | 79 | 623 | 3,115 | 3,274.505 s | 669,978,624 bytes |
| 2 | PASS | 79 | 623 | 3,115 | 3,275.521 s | 669,888,512 bytes |

The two elapsed values differ by 1.016 seconds. The second warm run therefore
does not reveal a material cache shortcut; repeated parser work owns the cost.

## Refreshed Environment

The latest successful mandatory CPU run remained `33244168782` when SDT0
started. Its log reports GitHub runner 2.336.0, `windows-2022` image
`20260824.284.2`, and a 58m58s source-design/retained-policy phase inside a
66m46s fast preflight. The complete hosted job took about 74 minutes.

The clean current branch at `d6b7c46ec09c1019bcb1e8eecc75b033747fa358`
selects 18 files and 136 contexts from merge base
`de6b19d68ca526316b5898e5536d6eb467df1a7b`, with canonical identity SHA-256
`aba90afcffa1fcda23013c81b621c58aae1df5faa1c95cf6eae6d10426978342`.
The pre-change self-test passes in 3.859 seconds. These refreshed measurements
confirm that the plan is current and that implementation should proceed.

No source selection, compile context, matcher, threshold, test, coverage floor,
Physics evidence, or golden baseline changed while collecting this receipt.
