# Third-Party Notices

This file supplements the license files retained with each dependency. It does
not replace their terms.

## Box3D Deterministic Math

- Project: <https://github.com/erincatto/box3d>
- Adapted revision: commit `30c67b5e6d0a3a66f0f506c69ce9e9e0587e3b7c`
- Adapted source: `src/math_functions.c` deterministic `b3ComputeCosSin` and
  `b3Atan2` algorithms; SkullbonezCore owns its range reduction and API
- Copyright: 2026 Erin Catto
- License: MIT, reproduced verbatim in
  `ThirdPtySource/box3d_math_LICENSE.txt`

The existing checked-in single-file dependencies under `ThirdPtySource` retain
their own license files and inventory in `ThirdPtySource/README.md`.

## SMAA

SMAA 1x shader and lookup tables by Jorge Jimenez and coauthors, MIT license.
Source: https://github.com/iryoku/smaa at revision 71c806a838bdd7d517df19192a20f0c61b3ca29d.
The source (comments normalized to UTF-8 and source line endings to LF), lookup headers and license are in `ThirdPtySource/SMAA/`.

## Solver2D sample scenes

- Project: <https://github.com/erincatto/solver2d>
- Adapted revision: `1e0492d81f68c7831cfa549699dd98bcb8454060`
- Adapted sources: `samples/collection/sample_contact.cpp`,
  `sample_joints.cpp`, and `sample_far.cpp`
- Local adaptation: `tools/generate_catto_solver_scenes.py`, generated
  `SkullbonezData/scenes/catto_*` scenes, and `SkullbonezData/hulls/catto_*` hulls
- Copyright: 2024 Erin Catto
- License: MIT, reproduced verbatim in `ThirdPtySource/solver2d_samples_LICENSE.txt`
- Adaptation details: `SkullbonezData/scenes/CATTO_SOLVER_SCENES.md`
