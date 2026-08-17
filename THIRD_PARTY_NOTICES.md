# Third-Party Notices

This file supplements the license files retained with each dependency. It does
not replace their terms.

## Dear ImGui

- Project: <https://github.com/ocornut/imgui>
- Pinned revision: `v1.92.8-docking`, commit
  `b61e56346a92cfcaf1f43a545ca37b0b32239654`
- Copyright: 2014-2026 Omar Cornut
- License: MIT, reproduced verbatim in `ThirdParty/imgui/LICENSE.txt`

## Tracy Profiler

- Project: <https://github.com/wolfpld/tracy>
- Pinned revision: `v0.13.1`, commit
  `05cceee0df3b8d7c6fa87e9638af311dbabc63cb`
- Copyright: 2017-2025 Bartosz Taudul
- License: BSD 3-Clause, reproduced verbatim in `ThirdParty/tracy/LICENSE`

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
