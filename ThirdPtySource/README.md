# Third-Party Source

Vendor source kept here is checked in only when the dependency is small,
single-file or otherwise build-system-light enough that NuGet/vcpkg plumbing
would add more moving parts than the dependency itself.

## doctest

- Version: 2.4.12
- Source: https://github.com/doctest/doctest/releases/tag/v2.4.12
- Header: https://raw.githubusercontent.com/doctest/doctest/v2.4.12/doctest/doctest.h
- License: MIT

## Box3D Deterministic Math

`SkullbonezSource/Maths/DeterministicMath.cpp` adapts the deterministic
`b3ComputeCosSin` and `b3Atan2` algorithms from Box3D commit
`30c67b5e6d0a3a66f0f506c69ce9e9e0587e3b7c`. The engine owns a different
bounded range-reduction policy and API; the retained upstream MIT terms are in
`box3d_math_LICENSE.txt`.

License SHA-256:
`DA5E31A26BF3CFD5BA5C96D6823E480128C81E76C107EF9D3EE5D94789184B90`.
