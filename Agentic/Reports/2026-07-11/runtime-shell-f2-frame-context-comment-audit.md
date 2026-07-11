# Runtime Shell F2 Frame-Context Comment Audit

Scope: `SkullbonezSource/Runtime/RunFrame.cpp` after deleting all local
multi-domain `*Context` bags.

Checklist: 1 checked, 0 deferred, 0 unchecked.

- [x] `SkullbonezSource/Runtime/RunFrame.cpp`

Audit notes:

- The late UI pass documents that its explicit borrows are synchronous and are
  intentionally not retained in a substitute frame context.
- Contact-audio and replay post-step functions preserve their hot-path,
  allocation-phase, diagnostic, and identity comments beside the relevant work.
- Visualization sequencing retains the broadphase gate, linger, and end-of-frame
  invariants without a mutable owner bag.
