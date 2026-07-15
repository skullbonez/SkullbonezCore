/*
File: SkullbonezSource/Core/FloatingPointContract.h
Purpose:
  Pins floating-point contraction off for every repository translation unit.

Mental model:
  A compiler may replace a multiply followed by an add with one fused
  instruction. The fused operation rounds once instead of twice, so it can
  change physics bits even when the source expression and `/fp:precise` remain
  unchanged. Each project force-includes this header before compiling a source
  file so the policy cannot depend on that file's ordinary includes.

Glossary:
  FP contraction: Combining adjacent floating-point operations into one fused
    instruction with different rounding behavior.
  Translation unit: One source file after all of its included headers have been
    expanded for compilation.
  Determinism envelope: Binary, toolchain settings, and gated content for which
    the repository has certified byte-exact physics output.

Invariants:
  - Every project force-includes this file in every configuration.
  - Contraction stays disabled before any AVX2, FMA, or SIMD policy change.

Related:
  - Agentic/Plans/MASTER-PLAN.md
  - Agentic/Reports/2026-07-15/fp-envelope-hardening-diagnosis.md
  - Agentic/Reference/physics-overview.md
*/
#pragma once

// Hazard: fp-envelope-hardening disables contraction before a future AVX2/FMA
// lane can make `a * b + c` round differently from separate operations. MSVC
// 19.51 rejects `/fp:contract-`, so all projects force-include this equivalent
// pragma instead.
#pragma fp_contract( off )
