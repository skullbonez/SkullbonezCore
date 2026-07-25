# Header Claim Staleness Remediation Closure

Date: 2026-07-25

Status: **Complete (HC0-HC2, 3/3)**

This campaign repaired misleading ownership prose, replaced expiring
`Related:` references with live source or permanent closure evidence, and
installed both judgment-based and mechanical defenses against the same drift.

## HC0 — Ownership Claims

HC0 corrected all 17 registered false-claim sites plus one additional phantom
`RunInput` claim found by the final word-boundary proof. The complete
old/new/source-proof table is retained in
`Agentic/Reports/2026-07-25/header-claim-staleness-hc0.md`.

## HC1 — Durable Related Paths

The 12 registered Class B rows now resolve as follows:

| Rows | Correct destination |
|---|---|
| B1-B2 | `SkullbonezSource/UI/UIProfilerOverlayPresenter.cpp` |
| B3-B4 | `Agentic/Reports/2026-07-24/solar-system-trajectory-planner-closure.md` |
| B5 | `Agentic/Reports/2026-07-20/physics-settings-snapshot-closure.md` |
| B6-B8 | `SkullbonezSource/Runtime/Render/RuntimeRenderPasses.cpp` |
| B9 | Deleted the obsolete `Plans/In_Progress` CSV row |
| B10 | `Agentic/Reports/2026-07-14/replay-visual-fidelity-mega-probe-closure.md` |
| B11 | `SkullbonezSource/Runtime/Scene/SceneController.cpp` |
| B12 | `Agentic/Reports/2026-07-23/ui-runtime-separation-closure.md` |

The new checker also found and repaired eight later-deleted ImGui/Tracy
campaign references and one deleted frame-view plan reference.

`tools/check_related_paths.py` now runs through `tools/validate_format.bat` and
therefore `tools/validate_fast.bat`. Its fixtures prove repository-root,
source-local, source-ancestor, and unique-source-name resolution; bare topics
are ignored; ambiguous source names and dead paths fail with their exact line.
The final direct sweep scanned 552 tracked source-bearing files and 1,459
repository-relative entries with zero findings.

## HC2 — Claim Verification

The repository comment-audit skill now verifies ownership, sequencing, and
behavior claims against post-change source. It preserves GV0's
aggregate-invariant review as procedure step 7 and adds claim verification as
step 8. Rot-marker words are review prompts rather than banned vocabulary, and
the RenderGraph near-miss is retained as the worked example.

`AGENTS.md` now requires real post-change owners, same-change prose correction
when responsibility moves, and resolving permanent `Related:` evidence.
Skill validation reports `Skill is valid!`.

## Closure Proofs

```text
python tools\check_related_paths.py --self-test
PASS: root/local/ancestor/unique targets resolve; ambiguous/dead targets fail

python tools\check_related_paths.py --repo .
PASS: 552 files, 1,459 repository-relative entries, zero findings

tools\validate_fast.bat
PASS: formatting, metadata, dependency graph, tests, Profile build, Debug build

rg -n '\bRunInput\b' SkullbonezSource
PASS: zero rows

rg -n 'lifecycle extraction C1|Run still owns' SkullbonezSource
PASS: zero rows

rg -n 'Related:' -A 6 SkullbonezSource | rg -n 'Agentic/Plans/TODO/'
PASS: zero rows
```

HC0 source changes are comment-only. HC1 source changes are confined to
`Related:` comment rows; the only behavioral addition is the validation
checker and its call-chain wiring.

## Independent Review

The whole-plan review found no new false ownership/sequencing claims, hidden
rot-marker defects, unresolved current `Related:` targets, or weakening of
GV0. Its initial blockers—honest checker terminology, current scan evidence,
fixture breadth, and stale master-ledger state—were corrected before closure.

