# FP7 owner acceptance, 2026-09-07

The owner accepts the FP7 physics behavior and requests the baseline and PR
checks be prepared for integration. Further investigation may lead to a later
rollback; acceptance does not establish the cause of the observed wall-brick hop.

The accepted solver change is `c5505842699d412cdd7cf7ac75d5c571fb34637c`,
following FP6 `fa1acb095391fcf2d772fc79b618d2579e27d5a8`. Shared contact/joint
iterations, canonical contact-pair ordering, and independent component
convergence can change ordinary rigid-body trajectories as well as ragdolls.

An isolated 200-box comparison removed the ragdoll and supplied both producers
with the same ball pose, linear velocity and angular velocity after the new
ragdoll collision. Both captures contain 202 bodies and 2,400 ticks. Body states
match through tick 88; the first recorded difference is at tick 89. This proves
the downstream difference is not solely the changed incoming ball state. It
does not prove which solver change causes the later hop. These are cold scene
starts, not restored checkpoints with inherited contact caches.

Local investigation evidence remains at
`TestOutput/skarness/ab-post-ragdoll-wall-01/comparison.json` and its adjacent
`isolation-check.json` in the original workspace. The unfinished A/B viewer is
preserved separately and is not part of this solver acceptance PR.

The already committed core baseline has SHA-256
`50bca7c0f2c420832c4fd99b1812f4db48d88cfadb4d475622a3d3bd3465a1c1`.
Its prior value is
`03088b30b8826f88a6193e511b7f4205aff9324d06ad08456610aac0e13a3f6b`.
The immutable `golden-transitions/03088b30-to-50bca7c0/` bundle retains the
exact producers. The companion `shared-constraints-22ce605c/` bundle retains
the replay visual and causal transition. No tolerances are relaxed and no
new baseline refresh is required to express this acceptance.

Any later rollback must restore a consistent solver and golden set and rerun
the mapped checks. Keep the original commits and transition bundles intact.
