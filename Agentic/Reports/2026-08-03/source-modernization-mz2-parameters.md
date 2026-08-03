# Source Modernization MZ2 — Parameter Names

Date: 2026-08-03
Branch: `nightrunner-3rd-AUG-26`
Plan phase: MZ2 of `Agentic/Plans/TODO/source-modernization-sweep.md`
Impact: Assets, Maths, Physics, Rendering, Runtime App/Camera, and World source

## Result

MZ2 retired all 82 semantic Hungarian-prefixed parameters across the 27 files
bounded by MZ0. Every declaration, definition, body reference, and parameter-
naming comment now uses the post-change identity. The ten A/B participant names,
two external ImGui declaration names, and seven first-party Win32
`wParam`/`lParam` slots remain exactly as classified.

Three MZ0 suggested spellings required collision-free refinements discovered by
source review or `/W4 /WX` compilation:

- `SpatialGrid` uses `requestedCellSize`, because `cellSize` hides its member and
  MSVC rejects that declaration.
- `WndProc` uses `messageId`, because `message` is already the local diagnostic
  text within the resize branch.
- `WinMain` uses `commandLineText`, because `commandLine` is the parsed
  `CommandLineView` local.

These names preserve the intended semantic meaning without member/local
shadowing. No member or unrelated local was renamed; notably `Camera.cpp`'s
local `vView`, `Window.cpp`'s local `hWnd`, and terrain/geometry `vPosition`
fields remain outside the tranche.

## Behavioral-Diff Proof

The complete identifier-level diff contains only census mappings and the three
collision-free refinements above. There is no changed type, literal, operator,
initializer value, call order, overload, branch, loop, storage, or signature
type. The `SpatialGrid` setter retains the same member comparison and assignment
after the parameter rename.

- Each of the 27 physical files was followed by an incremental Profile build;
  all passed. The `cellSize` shadow attempt failed once and was repaired before
  work continued.
- The final `tools\validate_build.bat Profile` passes with zero warnings and
  zero errors.
- `tools\validate_format.bat` passes for all 587 implementations and 327
  headers.
- The scoped old-identity scan is empty. The retained Win32 scan reports only
  the seven first-party ABI slots plus the two excluded external declaration
  parameters and their body references.
- `git diff --check` passes.
- The required touched-source comment audit is 27/27. All learning headers are
  complete, all parameter-naming comments describe post-change names, and the
  rename-only bodies introduce no new ownership, lifetime, invariant, unit, or
  hazard explanation need.

Byte-exact Physics, deep Physics, performance, and full validation remain the
binding MZ4 closure proofs after MZ3 records its two convention rulings.
