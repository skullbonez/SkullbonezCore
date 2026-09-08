# Solver Lab

Open the Scene tab and use its third dropdown, **Solver Lab**. Selecting an
entry opens the completed A/B recording. **Exit Solver Lab** returns to the
normal scene interface, where either comparison can be selected again.

| Entry | A | B | Scene |
| --- | --- | --- | --- |
| Ragdoll & Wall | FP6 | FP7 | Original ball, ragdoll and 200-box wall, 212 bodies |
| Wall Only - Post-Ragdoll Velocity | FP6 | FP7 | No ragdoll, 202 bodies; identical ball state taken just after the FP7 ragdoll collision |

Both comparisons contain 2,400 recorded ticks at 120 Hz (20 seconds). These
historical captures start at physics tick 1; tick zero remains explicitly not
recorded. Sparse solver checkpoints and the complete producer diagnostic streams
are preserved. Diagnostics that the producers never recorded remain unavailable.

The four `.skreplay` files are byte-for-byte copies of the original captures.
Each manifest preserves their hashes, producer executable identities, original
input settings and archived input hashes. The inputs include the required scene,
assets, hulls and styles. Viewing does not require the historical executables.

The original diagnostic text is losslessly compressed into `.skdiag` parts of
at most 48 MiB. Each part starts with `SKDIAG1\n`, followed by little-endian
32-bit uncompressed/compressed byte counts and independent Windows
XPRESS-Huffman blocks; a zero/zero pair terminates the part. Raw blocks are at
most 1 MiB. Loading reconstructs the text in the user's temporary directory
under `SkullbonezSolverLab`, verifies the original SHA-256, then uses the existing
comparison reader. Corruption, truncation, cancellation and insufficient space
prevent publication. The cache can be deleted when the viewer is closed and is
rebuilt on demand; first use needs about 4 GB of free space for both pairs.

`tools/package_solver_lab.py` creates these packages from a completed comparison.
Verbose Skarness transport logs, local query databases and command reports are
not distributed. They do not supply the viewer's motion or contact evidence.
Original local captures remain under `TestOutput/skarness/`.

Sources: `ab-fp6-current-wall-06` and `ab-post-ragdoll-wall-01`, captured before
Solver Lab packaging. Neither solver, recording, baseline nor test tolerance
was changed to produce this library.
