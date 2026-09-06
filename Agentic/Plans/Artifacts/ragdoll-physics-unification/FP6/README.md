# FP6 explicit point-joint spring

The Physics-owned model is a mass-normalized implicit spring. Authored controls
are `frequencyHz` (cycles/second) and `dampingRatio` (dimensionless). Defaults
are 40 Hz and 1, with eight sweeps. The named coefficient owner is
`Physics/PointJointSettings.h`. Position recovery is bounded at 28 m/s;
relative physical velocity is not capped. The former per-body 70 m/s and
18 rad/s clamps and positional postcorrection are removed. The separate neck
angular limiter is unchanged and is not attributed to point-joint damping.

For inverse effective mass K (1/kg), omega=2*pi*f (1/s), step h (s), and zeta,
let gamma=1/[h*omega*(2*zeta+h*omega)] (dimensionless) and
biasRate=omega/(2*zeta+h*omega) (1/s). The block solves
(K+gamma*K)*deltaLambda=Cdot+bias-gamma*K*lambda. The implementation reuses
K's factorization with massScale=1/(1+gamma) and impulseScale=gamma/(1+gamma).
Zero frequency disables the row and clears its cache. Infinite frequency tends
to the hard constraint coefficients; that algebraic limit is not a guarantee
that eight sweeps converge an arbitrarily stiff network. Finite iteration
convergence still depends on topology and mass ratios.

## Model selection and envelope

`model-selection.txt` preserves isolated probes at 60/120/240 Hz and 4/8/12
sweeps. Four sweeps were rejected for the 40 Hz model: the 60 Hz ten-link chain
was unstable. Eight and twelve sweeps settle the default chain near 15.53 mm
at every tested rate. These probes retained the prior safety clamps; final
candidate measurements and regressions exercise their removal. A 20 Hz model
had about 62 mm sag. The tested hard four-sweep alternative had 602 mm final
sag and 1812 J settled kinetic energy and was rejected.

The supported regression envelope for the chosen default is 60-240 Hz with
8-12 sweeps. Production uses the fixed eight-sweep default. The optional sweep
count is a bounded test/convergence control, not an authored gameplay setting.
Changing h midstream requires the caller to invalidate cached impulses, as with
other solver-policy edits; fixed-step comparisons start from identical cold state.

## Behavior and cost

Ten links, unit masses, gravity 9.81 m/s^2, 240 steps at 120 Hz. Settled
statistics use the last 120 steps. Two alternating rounds each retain ten
100-chain timing samples. The same host/compiler/support library is used.
`measure.py` reproduces the comparison; `measurements/*.txt` retain every
measurement and test outcome, omitting repetitive capacity announcements.
Unfiltered logs and builds stay in TestOutput/ragdoll-physics-unification/FP6.

| Metric | FP5 | FP6 |
|---|---:|---:|
| Final bottom sag, m | 0.00711250305 | 0.0155305862 |
| Peak bottom sag, m | 0.00711250305 | 0.0239934921 |
| Settled jitter RMS, m/step | 0.00172971522 | 0.00000321172629 |
| Settled kinetic energy, J | 0.188763669 | 0.000000364717111 |
| Peak kinetic energy, J | 0.5966953 | 0.279893427 |
| Median microseconds/joint/iteration | 0.287047448 | 0.2342654425 |
| Iterations | 4 | 8 |
| Derived microseconds/joint/step | 1.148189792 | 1.87412354 |

The tradeoff is deliberate: finite physical stretch and about 63% more total
joint solve time buy much lower resting motion. Per-iteration speed alone
would hide the doubled sweep count. The default is provisional and localized
in PointJointSettings.h; FP7 will address contact/joint convergence together.

## Compatibility

Scene schema v5 writes physical controls. v1-v4 retain explicit legacy parsing:
f=clamp(stiffness,0,1)/0.22*40 and zeta=clamp(damping,0,1)/0.35. This is a named
retuning that preserves control ordering/defaults, not trajectory equivalence.
The Python migrator rounds every operation to binary32 to match C++; non-default
exact-value regressions cover this rule. All active committed scene files were
upgraded; immutable artifacts and intentional historical fixtures are retained.

Nested solver snapshot v8 gives the two parameter floats their physical meaning.
v3-v7 retain historical raw bytes for inspection and hashing. Explicit Physics
import compares migrated descriptors and clears the incompatible old impulse.
App refuses older authoritative continuation before mutating live state. C++
persisted v6/v7 tests and Python v1-v8/future-v9 tests cover these boundaries.

## Verification scope

Focused cases cover load, off-axis response, anisotropic rotation, free angular
motion, high common velocity, impact decay, ten-second rest, zero frequency,
legacy/current parsing and writer round trips, and retained-cache identity.
Signed corrective impulse work is published per iteration; it excludes warm
start and other forces. Implicit integration dissipates some energy even at
zero damping ratio; tests distinguish that from positive damping and avoid the
separately documented positional recovery cap.

The general data migration check also reports the pre-existing
`test_scaled_normals_box.hull`. That version-2 fixture deliberately stores
non-unit baked normals for TestPhysicsStageState; normalizing it would erase
the regression. Its bytes are preserved. All active scene migrations and
migration self-tests pass.


## Bounded prediction archive coverage

The first 200-box capture completed 2401 prediction frames but could not freeze
its archive: detailed evidence exhausted its existing 320 MiB bank at frame
1295, before a causal event at frame 1587. FP5 happened to retain detail past
its last causal event; the codec incorrectly required that for every capture.
RVPD schema v7 now records an exclusive evidence coverage boundary and retains
frame zero plus event records inside it. Complete lightweight trajectories and
later causal nodes remain available. Schemas v4/v6 retain their full-coverage
interpretation. A restored archive preserves the boundary independently of its
sparse records, so save/load/save remains exact. Every contact node must still
refer to the complete timeline; coverage does not permit nonexistent frames.

The fix changes no evidence cap or growth owner. Focused tests cover partial
coverage, full v6 import, missing in-prefix events, invalid coverage boundaries,
out-of-timeline nodes, timeline holes, and atomic rejection. Debug archive tests
pass 3 cases/120 assertions; four affected source files pass 23 compiler-backed
contexts. Review by agent 01a0779b-e345-7222-93f5-148a1da70b97 has no remaining
blocking or non-blocking findings. The failed first capture is retained under
TestOutput and is not baseline evidence.


The corrected first capture passed 2401 frames, 201 causal nodes, all 200 wall
bricks moved/settled, durable artifact equality, and in-process archive checks.
The content-bound transition is `golden-transitions/joint-softness-9499cda6/`.
The old/new visual hashes are `0a84ed99...` / `9499cda6...`; causal hashes are
`1f752b99...` / `128d2bd0...`. Its exact old/new Automation producers retain the
matching scene inputs and dependency scans. The independent full native repeat
is the acceptance check for these replacements. Final focused Debug coverage
passes 63 cases and 32,678 assertions, including the archive regressions.

The independent full native replay gate passes, including all typed, causal,
artifact, and determinism negative controls (TestOutput/fp6-replay-repeat.log).
The staged baseline guard confirms exact producer/golden binding.
