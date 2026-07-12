# Editor Undo/Redo Closure

Date: 2026-07-12
Plan: `editor-undo-redo`
Result: complete (5/5)

## Delivered

- Added an editor-owned, inline 64-entry inverse-command history with bounded
  16-object transform commands, redo-branch truncation, overflow eviction, and
  explicit invalidation for committed edits whose inverse is deferred.
- Coalesced translate, rotate, and supported primitive scale gestures on release.
  Place/delete support covers standalone sphere and box entities using stable
  `PhysicsSceneObjectId`; recreated bodies receive fresh handles and resolve the
  current scene terrain instead of retaining borrowed pointers.
- Added editor-only Ctrl+Z, Ctrl+Y, Ctrl+Shift+Z, and Delete routing, including
  the missing Editor keyboard-context publication discovered by the interaction
  probe.
- Clear history on scene load/reset and editor exit, expose undo/redo depth in
  the editor tab, and retain documented deferrals for compound assets, convex
  hull recipes, and live-object parameter commands.

## Acceptance Evidence

`SkullbonezData/interaction/editor_undo_redo.json` drives the normal input path
through place, gizmo transform, delete, three undos, three redos, and a new edit
after undo. The final report is `ok=true` and matches two exact fingerprints at
every inverse/forward boundary:

- placed: `0x36AAADEF4C215C61`
- transformed: `0xE77C60349615EE09`

Each fingerprint covers stable scene id, display name, render material, body
pose/velocity/inertia and authored physics parameters, terrain presence,
primitive shape, and collider material. Absence is separately asserted after
delete, undo-place, and redo-delete. The final new placement changes undo depth
from 2 to 3 while redo depth changes from 1 to 0.

Unit coverage proves push, undo, redo, branch truncation, overflow, clear, and
full invalidation after a mutation with no inverse.

## Review

The plan-end rubber-duck review initially found three blockers: terrain binding
was omitted from recreation, deferred mutations could retain stale redo, and
cursor-only assertions could falsely pass while transform application silently
failed. The final implementation resolves all three. Focused re-review cleared
the plan with no remaining blocker.

Comment-style audit covered all 27/27 touched source-bearing files. New history files
have complete learning headers; dense recreation, preflight, rollback,
non-undoable invalidation, modifier injection, and fatal application invariants
have local teaching comments. No files were deferred.

## Validation

- `tools\validate_interaction_clicks.bat` — passed all six scenarios in
  23.424s; the final strengthened editor-only launch passed in 3.130s.
- `tools\validate_tests.bat` — passed 167/167 cases and 3,919 assertions.
- `tools\validate_fast.bat` — passed in 46.008s after project-filter metadata
  was extended for the two new editor source files.
- `python tools\check_allocation_policy.py --repo .` — passed; 320 files
  scanned and zero allowlist errors.
- `tools\validate_full.bat` — final source passed in 98.412s: all CPU lanes,
  zero-warning Profile/Debug builds, zero DX12 validation errors, visual maxima
  33/61/0, and 44,401-line physics output byte-exact.
