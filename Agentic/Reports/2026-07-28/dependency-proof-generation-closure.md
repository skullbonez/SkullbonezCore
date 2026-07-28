# Dependency Proof Generation — Closure

Date: 2026-07-28
Branch: `nightrunner-28th-JUL-26-takeover`
Plan: `Agentic/Plans/TODO/dependency-proof-generation.md`
Phase: DP2 — final phase

## Outcome

The existing dependency checker now owns one deterministic enforcement and
review surface from `tools/dependency_graph_rules.json`. DP2 closes the
remaining false-pass gaps without introducing another checker, a
package-specific enforcement branch, a frozen edge count, or a budget.

- A planted rule-data edit changes the rendered proof and makes the previous
  marked block stale.
- Missing, duplicate, and reversed markers fail closed; write mode preserves
  every byte outside the unique ordered marker pair.
- Rule-controlled pipes, backticks, angle brackets, ampersands, and line
  breaks are escaped through the complete renderer.
- Every Runtime allow rule rejects an end-to-end
  `Runtime/UnregisteredPackage/Fixture.h` edge.
- Macro and backslash-continuation operands are independently ignored, while
  quoted and angle operands are independently parsed through the documented
  local-first textual resolver.
- Project ownership has exact required-only, missing-required,
  required-plus-Core, and required-plus-Tests cases. End-to-end Git/XML
  discovery exercises every governed `.cpp`, `.h`, `.hpp`, and `.inl` suffix,
  both `ClCompile` and `ClInclude`, and excludes an untracked governed suffix
  plus a tracked non-governed suffix.

The generated `AGENTS.md` block and `tools/README.md` describe these exact
contracts and retain the scanner's bounded residual limits.

## Independent Review

The first read-only rubber-duck pass found two fixture false-pass risks:

1. The residual-parser fixture combined four directive forms into two
   identical findings, so one parsed/ignored swap could preserve the count.
2. The end-to-end project fixture governed only `.cpp`, so `ClInclude`
   discovery was not proved.

Both findings were repaired with isolated `0, 0, 1, 1` parser cases using
distinct targets and all-suffix project discovery. The review also identified
this report as the required permanent `Related:` target. Follow-up review
reinspected those repairs and returned `CLEAR` with zero blockers.

| Plan | Duck run | Reviewer/thread | Reason | Prompt chars | Response chars | Tokens | Elapsed | Verdict | Follow-up |
|---|---|---|---|---:|---:|---|---:|---|---|
| `Agentic/Plans/TODO/dependency-proof-generation.md` | `dependency-proof-generation-duck-01` | `/root/dependency_proof_duck_01` | Initial DP2 review | 1,297 | 2,008 | n/a | 4m 8s | Three blockers | Fixture separation, governed-header coverage, and permanent report |
| `Agentic/Plans/TODO/dependency-proof-generation.md` | `dependency-proof-generation-duck-02` | `/root/dependency_proof_duck_01` | Follow-up after fixture repairs | 638 | 891 | n/a | 1m 7s | Clear; zero blockers | None |

## Validation

Final-source checks pass:

| Command | Time | Result |
|---|---:|---|
| `python -m py_compile tools/check_dependency_graph.py` | — | PASS |
| `python tools/check_dependency_graph.py --self-test` | — | PASS — 27 include rules, 67 negative edge fixtures, one content rule with two negative fixtures, eight exact project-rule cases, and generated-proof fixtures |
| `python tools/check_dependency_graph.py --check-proof AGENTS.md` | — | PASS — generated block current |
| `tools\validate_dependency_graph.bat` | 3.23 s | PASS — generated block current and zero repository findings |
| `tools\validate_fast.bat` | 232.84 s | PASS — formatting, 787/787 project/filter rows, dependencies, ownership, tests, and zero-warning Profile/Debug builds |
| `tools\validate_full.bat` | 370.38 s | PASS — 438/438 doctests, 2,419,221 assertions, all CPU/runtime lanes, zero DX12 InfoQueue errors, accepted images, and byte-exact 44,401-line Physics output |
| scoped `git diff --check` | — | PASS |

The fast and full gates ran in `C:\sb-dp2-0dc10e15`, a detached validation
worktree containing only the DP2 diff. This kept the three protected Physics
files outside the gate. The first isolated setup omitted ignored ImGui/Tracy
files and stopped at their three existing `Related:` targets. After those
read-only dependencies were supplied, MSBuild rejected the system-temporary
path with `MSB8029` (zero warnings, five path errors, 45.07 seconds). Moving
the identical source to the non-temporary path resolved the environment-only
restriction; no implementation changed between those attempts and the passing
gates.

No baseline, golden, config, schema, performance artifact, or source layout was
refreshed.

## Comment Audit

Touched source/tool scope: `tools/check_dependency_graph.py`.

- Checked: 1/1
- Deferred: 0

The learning header and nearby fixture comments correctly describe one
rule-data owner, byte-preserving proof replacement, exact-file versus prefix
semantics, XML/tracked-path discovery, and the textual parser's bounded limits.
All three `Related:` paths resolve; `tools/check_related_paths.py` reports zero
findings across 571 source files and 1,519 repository paths.

No C++ source belongs to DP2, so the C++ ownership inventories were not required
for the review. The three pre-existing Physics warm-start files remain
protected, unstaged, and outside this plan.
