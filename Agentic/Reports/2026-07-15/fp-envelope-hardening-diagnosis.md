# FP Envelope Hardening Diagnosis

Date: 2026-07-15  
Plan: `fp-envelope-hardening` T1-T2  
Historical change: `ff6e780e` (`vector3-inline-hot-math`)

## Verdict

The old persistent-contact fixture is a configuration-specific floating-point
knife edge. The historical Profile result changed because moving Vector3
operations into the header changed the optimizer's inlining and packed-SSE
code shape inside the manifold translation unit. It was not an FP-contraction
event: neither historical object contains AVX or fused multiply-add
instructions.

This result does not justify solver hysteresis in the current lane. Per the
2026-07-15 owner ruling, manifold tolerance/hysteresis remains deferred until
the future SIMD lane requires an intentional physics and baseline decision.

## Reproduction

Two detached worktrees were built with the same installed compiler:

- pre-inline: `ff6e780e^` (`6b20e5745`), original `0.70f` rotation and `1.5f`
  contact height;
- post-inline: `ff6e780e`, with only those two old fixture values restored in
  scratch. The committed post-inline fixture correction was not altered on the
  working branch.

Toolchain: MSVC 19.51.36248 for x64, tool directory 14.51.36231. All project
configurations used their committed `/fp:precise` settings.

| Configuration | Pre-inline result | Post-inline result | Classification |
|---|---|---|---|
| Debug | No manifold (`BuildObjectContactManifold == false`) | No manifold | Existing configuration difference; no inline flip |
| Profile | Pass, 2-point face | Fail, 4-point face | Reproduced historical flip |
| Release | Fail, 4-point face | Fail, 4-point face | Existing configuration difference; no inline flip |
| Profile-WPO | Fail, 4-point face | Fail, 4-point face | Existing configuration difference; no inline flip |

The focused Profile command built `SKULLBONEZ_TESTS.vcxproj` and ran:

```text
SKULLBONEZ_TESTS.exe --test-case="Persistent contact solver: a box gains sleep support only after toppling from its edge" --no-skip
```

Pre-inline passed 10/10 assertions. Post-inline failed the second assertion:
`edgeManifold.pointCount` was 4, not at most 2. Test-TU-only diagnostics,
which did not alter `ObjectContactManifold.obj`, recorded:

```text
pre:  point_count=2 normal=(0,1,0) first_penetration=0.818591475 feature=36480
post: point_count=4 normal=(0,1,0) first_penetration=0.909059882 feature=36480
```

## Disassembly Evidence

`dumpbin /disasm:nobytes` was run on each clean Profile
`ObjectContactManifold.obj`. Across the object, the pre-inline state contains
197 calls to Vector3 arithmetic operators, zero packed floating-point
arithmetic instructions, zero AVX instructions, and zero FMA instructions.
The post-inline state contains zero Vector3 arithmetic-operator calls, 40
packed SSE arithmetic instructions, zero AVX instructions, and zero FMA
instructions.

The normalized SAT-axis path shows the boundary change directly:

```text
pre  AcceptSatAxis+008B  call Vector3::operator/(float)
pre  AcceptSatAxis+009D  call Vector3::operator*(Vector3)  ; dot product
post AcceptSatAxis+00BB  divss xmm1,xmm0
post AcceptSatAxis+00CA  mulps xmm8,xmm0
```

The source arithmetic bodies were carried across, but the optimizer no longer
had call boundaries or temporary return objects constraining instruction
selection. That is an inlining/vectorization and configuration-envelope change,
not intermediate x87 precision and not FP contraction. It explains why only
the Profile knife edge crossed in this historical commit while the other
configurations already disagreed with the old fixture.

## Contraction Guard Choice

The owner-approved `/fp:contract-` spelling is not supported by the installed
MSVC 19.51 compiler: a `/W4 /WX` probe emitted D9002, “ignoring unknown option
`/fp:contract-`.” A `/W4 /WX` probe of `#pragma fp_contract(off)` compiled
successfully. The implementation therefore uses the plan-approved equivalent:
all four projects unconditionally force-include
`SkullbonezSource/Core/FloatingPointContract.h` in every configuration.

The guard is prospective. Today's default SSE code has no FMA to suppress;
the unchanged physics gate is the required proof that adding the pragma does
not change current certified output. A future AVX2/FMA or SIMD change must keep
the guard or explicitly reopen the determinism envelope with owner approval.

An explicit effectiveness probe compiled the same no-inline `a * b + c`
function twice with `/O2 /arch:AVX2 /fp:precise /fp:contract`. Without the
pragma, MSVC emitted one fused instruction:

```text
vfmadd213ss xmm0,xmm1,xmm2
```

With `#pragma fp_contract(off)`, it emitted two separately rounded operations:

```text
vmulss xmm1,xmm0,xmm1
vaddss xmm0,xmm1,xmm2
```

This closes the compiler-effectiveness gap: the forced header does not merely
parse; it suppresses an otherwise eligible FMA under the future instruction
envelope that motivated the guard.
