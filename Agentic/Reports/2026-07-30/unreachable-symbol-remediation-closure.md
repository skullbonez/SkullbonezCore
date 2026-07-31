# Unreachable Symbol Remediation Closure

Date: 2026-07-30
Plan: `unreachable-symbol-remediation`
Status: COMPLETE - UR0-UR3, 4/4
Impact area: First-party C++ reachability, tests, build configurations, governance

## Outcome

The original 407-row provisional source scan was corrected before any bulk
deletion. The final scanner joins exact MSVC symbols and source declarations,
excludes internal linkage, preserves literal and variadic arity, follows
constructor/destructor and cross-TU inlining relays, and treats callback/template
adapters as conditional graph edges rather than unconditional roots.

Full validation exposed one last configuration defect in the proof itself:
Automation-only production consumers were absent from the Debug/Profile object
graph. The scanner now requires three distinct, current object roots:
Automation, Debug, and Profile. Its self-test contains a symbol whose only root
is compiled under `SKULLBONEZ_AUTOMATION_DIAGNOSTICS`.

The authoritative UR0 population is therefore 246 rows:

- 157 no-reference;
- 58 test-only;
- 26 own-TU-only;
- 5 own-TU-and-test-only;
- 245 exact compiler mappings; and
- one intentionally missing mapping with direct native test-build evidence.

Owner adjudication removed 181 ordinary out-of-line functions: 167 from the
corrected census, 12 first-order cascade leaves, and two final diagnostics
leaves. No compatibility alias, forwarding declaration, or commented-out body
replaced them. The permanent strict census now reports 79 rows, all with exact
`retain-owner` rulings and zero unruled, stale, or repair-plan entries.

Detailed checked decisions are in
`unreachable-symbol-remediation-ur0-census.md`.

## Configuration Correction

The first full gate proved that five deleted APIs were live only in Automation:

- `SceneWorld::Terrain() const`;
- `RuntimeTools::Laser() const`;
- `DevelopmentTools::TryParseImGuiEditorPanel`;
- `DemoDirectorPlayback::LoadShotList`; and
- `DemoDirectorPlayback::SetCurrentPhaseStyle`.

Those operations and the two parser dependencies reached by `LoadShotList`
(`LoadDemoShotList` and `TryParsePhaseAdvance`) were restored. The loader
validates into a temporary fixed-capacity shot list before publication, so a
malformed authoring document cannot partially overwrite active Director state.
`ImGuiEditorOwner::CopyStatus()` also became compiler-rooted by the Automation
graph and its now-stale retain ruling was removed.

An Automation clean rebuild removed historical orphan objects before the final
three-root proof. Automation then built with zero warnings/errors and its
replay/prediction plus development-UI smoke passed; Profile continued to reject
diagnostic interaction scripts.

## Test And API Migration

Tests that existed only to manufacture reachability for retired representation
or convenience APIs were removed. Behavior-bearing coverage moved to surviving
contracts:

- result-bearing authored-scene, style, convex-hull, and fixture loaders;
- explicit ColliderStore construction transactions;
- Input snapshots rather than retained-state convenience accessors;
- explicit RenderGraph pass ranges and single-transition DX12 execution;
- canonical matrix, quaternion, and Hohmann-transfer operations; and
- focused test fixture helpers for collider stores and result-bearing loads.

`SkullbonezTests/TestDemoDirector.cpp` was deleted with its loader-only retired
surface and removed from the project/filter files. Standalone Scene Parser,
Runtime Interaction Policy, UI Boundary, and DX12 Architecture suites all pass
through the complete CPU umbrella.

## Ownership And Comment Review

The touched-source checklist at
`../../Plans/unreachable-symbol-remediation-comment-checklist.md` was generated
from the final `git ls-files --modified --others --exclude-standard` inventory:
172 existing source-bearing files checked, one removed source file recorded,
and zero deferred.

Every checked source file has the required teaching header and any local
ownership, invariant, lifetime, or hazard note needed by the changed code. Stale
camera ownership comments and two tautological InputRouter assertions found by
independent review were removed or replaced with behavior-bearing checks.

Independent review also found that compiler callback adapters were being treated
as unconditional roots. The scanner now adds conditional graph edges and its
paired fixture proves the callback is rooted only when its binder is rooted.
The repeat review found no blocking ownership, scanner, test, or comment issue.

## Final Validation

- `python tools/inventory_unreachable_symbols.py --self-test`: PASS, including
  rooted/unrooted callback adapters and the Automation-only production root.
- Strict three-root repository census: PASS - 79/79 ruled, zero diagnostics
  (49 no-reference, 20 test-only, 10 own-TU-only).
- Fresh Automation, Profile, and Debug builds: PASS - zero warnings/errors.
- `tools\validate_fast.bat`: PASS - all 9 gates, including strict compiled
  reachability.
- `tools\validate_coverage.bat`: PASS - every subsystem floor and whole-product
  21,552/28,386 lines (75.92%).
- `tools\validate_all_cpu_tests.bat`: PASS - all six CPU lanes.
- `tools\validate_physics.bat`: PASS - the 44,401-line physics regression CSV
  remains byte-exact.
- `tools\validate_full.bat`: PASS - CPU/coverage, Automation policy and smoke,
  DX12 renderer validation, and byte-exact Physics.

No baseline, golden, shader, scene, schema, or committed runtime artifact
changed. A final documentation-only `validate_fast` run after ledger and TODO
cleanup is the commit precondition.
