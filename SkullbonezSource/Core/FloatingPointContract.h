/*
File: SkullbonezSource/Core/FloatingPointContract.h
Purpose:
  Pins floating-point contraction off for every repository translation unit.

Summary:
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
  - Contraction stays disabled before any vector-width or optimizer policy change.

Related:
  - Agentic/Plans/MASTER-PLAN.md
  - Agentic/Reference/physics-overview.md
*/
#pragma once

// Hazard: fp-envelope-hardening disables contraction before vector-width or
// optimizer policy can make `a * b + c` round differently. MSVC
// 19.51 rejects `/fp:contract-`, so all Visual Studio projects force-include
// this equivalent pragma. Portable targets must pair their marker with the
// compiler's enforced `-ffp-contract=off` option.
#if defined( _MSC_VER )
#pragma fp_contract( off )
#elif defined( SKULLBONEZ_FFP_CONTRACT_OFF ) && defined( __clang__ )
#pragma STDC FP_CONTRACT OFF
#elif defined( SKULLBONEZ_FFP_CONTRACT_OFF ) && defined( __GNUC__ )
// Why: GCC does not implement STDC FP_CONTRACT and warns on the pragma. The
// CMake flag probe and this paired marker make -ffp-contract=off the GCC owner.
#else
#error "Portable builds require supported Clang/GCC plus the verified -ffp-contract=off marker."
#endif
