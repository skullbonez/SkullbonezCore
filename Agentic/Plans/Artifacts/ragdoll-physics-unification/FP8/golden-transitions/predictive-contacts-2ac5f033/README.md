# Predictive-contact replay transition

Post-update mapped validation passes: `tools\validate_replay_visual_fidelity.bat`
exits 0 in 384.578 seconds, including the one native generation, 18 packet tests
with 82 assertions, exact visual/causal comparisons, artifact checks and all
negative controls. The final screenshot was inspected. `mapped-validation.json`
binds the preserved run artifacts; `mapped-validation.log` retains the gate's
key results. Independent transition review reports no material blocker.

The 5820 pre-FP8 producer reproduces every approved visual tick, final Physics
value and causal node/activation. The 2ac5 producer changes the striker's
interaction with the ragdoll before it reaches the wall. This transition accepts
that Physics change; it does not claim that ragdoll piles reliably sleep.

## First divergent interaction

The unchanged full recording first differs in the ragdoll poses at live tick
13. A separate 2-second Skarness prediction starts at source frame 61 in each
producer. Its complete 62-frame BODY/PRES prefix byte-matches the corresponding
full recording. Both owned processes exit 0.

The striker's predicted poses and velocities remain identical through prediction
frame 13. At frame 14, FP8 applies a normal impulse of 824.206421 against the
ragdoll head (stable IDs 1 and 204), across a 0.596154 m gap. The separated row
has zero tangent impulses and is not warm-started. The old producer first
contacts that head at frame 15 with 0.816796 m penetration. At frame 15 the new
head contact has 0.023388 m penetration. This is the intended early gap braking
and transition to touching contact, rather than an unexplained replay change.

`first-striker-divergence.json` binds the IDs, exact reported values, source
prefix hashes, normal shutdown and first differing striker frame. Solver
contact fields use model rows; the diagnostic projection resolves those rows
through the published stable-ID dictionary. Raw full state remains local under
`TestOutput/skarness/fp9-wall-contact-investigation-01/`.

## Accepted outcome

All 200 wall bricks remain affected and move. Toppled and sustained-toppled
counts change 185 to 192; settled bricks change 194 to 200. The first wall
activation changes frame 100 to 98. The downstream tree changes 201 to 200
nodes because the striker no longer hits fixed catcher wall 202 at frame 1857.
Ragdoll part IDs are 203 through 212; node 202 is not a ragdoll part.

Report shape, causal shape, complete durable-artifact round trip and all ten
injected-failure modes pass on the retained current report. Those controls were
first run against unapproved local candidates; the mapped gate must also pass
after the guarded writers install the two tracked goldens. FP8/FP9 remain open
until their remaining full and performance gates pass.

## Provenance and reproduction

The old executable's behavior matches the approved golden, but the ordinary
checker separately detects stale shader provenance. Previously committed UI
changes extended text vertices from RGB to RGBA and added preview opacity;
their source, DXIL and shader manifest change the shader-tree digest from
`f0ce4056c2850d3c652978119dcce51102caf7ef6611b63386639e0f23d78371` to
`589a92917228f108b86667e9cdda2175af082bbc439e8eb191eefa7050d2ff5a`.
FP8 does not edit shaders. The new golden records the actual current input tree.
The old producer's exact match of every raw prediction visual tick on that same
tree separates this provenance difference from the Physics behavior change.

`manifest.json` binds old and new golden hashes, source-parent commits and both
retained Automation executables. The two approval logs record exact guarded
writer commands and successful results. The complete new source changes land
atomically with this bundle; no Physics CSV baseline changes.

Only first-party game executables are retained here. `dependencies.txt` records
the dependency scans; obtain Windows and third-party runtime dependencies from
the pinned repository setup. For local reproduction, stage the exact archived
executable bytes beside the configured Automation runtime dependencies, retaining
a distinct `SKULLBONEZ_CORE-*.exe` name. Run the manifest's arguments from the
repository root. Do not copy DLL redistributables into this tracked bundle.
