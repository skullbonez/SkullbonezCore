# Look Lab Random Style Authoring - LL1 Generator

Date: 2026-08-01
Branch: `nightrunner-1st-AUG-26`
Plan: `Agentic/Plans/TODO/look-lab-random-style-authoring.md`
Phase: LL1 - complete

## Decision

Look Lab generator version 1 is a pure Runtime Direction operation. It accepts
only a 64-bit seed plus an explicit generator version and returns a detached
`LookLabCandidate`. It borrows no window, renderer, scene, filesystem, clock, or
shared random source. The candidate contains presentation intent; LL2 and LL3
will resolve retained scene/quality values at the Scene boundary before exact
serialization or live application.

The byte-reproducibility contract is not `memcmp` over a C++ object. Candidate
padding and library layout are outside the contract. Instead,
`EncodeLookLabCandidateCanonical` writes every typed field in an explicit
little-endian order and `FingerprintLookLabCandidate` hashes those bytes with
FNV-1a. Seed `0x0123456789abcdef`, generator version 1, pins fingerprint
`0xc3b6fad6b7b4defa` in both Debug and Profile. LL2 corrected the
canonical textured-material bridge from numeric kind zero to the parser's
established `-1` legacy sentinel before standalone serialization was admitted;
the deterministic pin moved with that source contract and no random draw or
recipe changed.

## Generator Contract

The private SplitMix64 stream implements the LL0 constants and wraparound
sequence exactly. Recipe choice is the first draw. Palette, feature, scalar,
and material variation then consume a fixed documented draw order. No
`std::uniform_*`, `rand`, gameplay seed, physics stream, renderer sampling
state, or locale conversion participates.

Palette roles begin as Q12 Oklab lightness/opponent coordinates. The Oklab
inverse matrices are quantized to integer coefficients; integer cubic and
matrix operations produce Q12 linear RGB. Every emitted float is an exact
multiple of 1/4096. This removes optimization-mode rounding ambiguity while
keeping sun, horizon, zenith, fog, terrain, accent, water, and object roles
correlated in a perceptual space.

The fourteen typed recipes are:

| Index | Recipe | Primary material |
|---:|---|---|
| 0 | `golden_realism` | textured |
| 1 | `low_poly_storybook` | matte |
| 2 | `painterly_poster` | metal |
| 3 | `neon_cyberpunk` | emissive |
| 4 | `tron_graphic` | glass |
| 5 | `atmospheric_storm` | toon |
| 6 | `studio_high_key` | lowpoly |
| 7 | `industrial_low_key` | shadow |
| 8 | `desert_warm` | foliage |
| 9 | `nordic_cool` | bark |
| 10 | `ocean_terrestrial` | stone |
| 11 | `alien_world` | ridge |
| 12 | `deep_space_dreamscape` | shore |
| 13 | `abstract_chromatic` | pine |

Each candidate has three stable broad material roles in `balls`, `boxes`,
`hulls` order. Secondary recipe-relative kinds make all fourteen supported
material branches reachable without a scene-name dependency. Names, flags,
alpha, roughness, metallic, specular, transmission, stylization, emissive
color/strength, and compatibility mode are fully resolved.

## Validity And Retained Policy

`ValidateLookLabCandidate` rejects:

- unsupported generator, recipe, sky, terrain, object, water, or material
  modes;
- non-finite or out-of-contract cinematic/material values;
- deep-space atmosphere/cloud/fog/shaft combinations and other disabled-feature
  payload contradictions;
- inverted or under-separated fog, invisible terrain/water/accent roles, and a
  black-frame luminance envelope;
- unordered or changed material targets, unterminated names/targets, nonzero
  runtime material flags, or mismatched compatibility modes; and
- changes to shadow map size, PCF radius, depth/slope bias, max distance, or the
  basin mask. These fields remain placeholders in the detached value and never
  grant quality or scene-coordinate authority.

The generator deliberately leaves live scene mutation, retained-value
resolution, serialization, and save/capture state to later owners. No source
outside Runtime Direction, tests, and project metadata changed in LL1.

## Deterministic Coverage

The fixed seed interval 0 through 4095 produces 4096 unique candidate
fingerprints with zero validation failures. The matrix covers:

- all 14 recipe families;
- all 21 named sky branches (`0..13`, `15..21`);
- all 16 terrain branches;
- all 8 supported top-level object branches;
- all 5 water branches; and
- all 14 typed material kinds.

Negative controls plant an out-of-range exposure, unsupported version,
non-finite gamma, incompatible deep-space/cloud pair, and black-frame palette.
Each reaches its intended rejection. A seeded C-library RNG witness proves a
generation call consumes no process-global random value.

## Evidence

- Profile x64 `SKULLBONEZ_TESTS` focused build: pass with warnings as errors.
- Debug x64 `SKULLBONEZ_TESTS` focused build: pass with warnings as errors.
- Profile focused Look Lab tests: 4 cases, 4,115 assertions, all pass.
- Debug focused Look Lab tests: 4 cases, 4,115 assertions, all pass with the
  same pinned fingerprint.
- Project-filter ownership: 794/794 production items, zero errors.
- Build-configuration consistency: self-test and direct repository scan pass;
  1,670 compile rows, 126 current reviewed divergence pairs, zero dropped
  inheritance, and zero blocking diagnostics. The generator's Core/test
  exception and executable-role macro differences have exact current rulings.
- Strict compiled-symbol reachability: pass after current Automation, Debug,
  and Profile object builds.
- `tools\validate_full.bat`: pass in 589.6 seconds from final source.
- Touched-source comment audit: 4/4 files inspected, zero deferred
  (`LookLabGenerator.h`, `LookLabGenerator.cpp`, `TestLookLabGenerator.cpp`,
  and the one-line project-filter taxonomy extension in
  `validate_project_filters.py`).
- No style, scene, configuration, screenshot, physics, Replay, DX12, or other
  tracked baseline changed.
