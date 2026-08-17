//
// File: SkullbonezTests/TestFixedSeed.h
// Purpose:
//   Provide one deterministic pseudo-random generator for bounded property tests.
//
// Summary:
//   Property cases exercise many ordinary inputs without becoming fuzz tests.
//   Each case supplies an explicit non-zero seed, and this xorshift32 generator
//   maps the resulting integer stream into closed floating-point ranges.
//
// Glossary:
//   Fixed seed: Literal starting state printed in the test name so a failure is
//     exactly reproducible.
//   Bounded property: Invariant checked over a small, deterministic sample set.
//
// Invariants:
//   - A seed produces the same stream on every supported build.
//   - Tests name their seed; this helper never reads time or process state.
//   - Callers choose ranges that avoid undefined or fatal product inputs.
//
// Related:
//   - tools/validate_coverage.bat
//
#pragma once

#include <cstdint>

namespace SkullbonezTests
{
class FixedSeed
{
  public:
    explicit FixedSeed( uint32_t seed ) : m_state( seed )
    {
    }

    uint32_t NextU32()
    {
        // Concept: xorshift32 is intentionally small and fully specified by
        // integer operations, making the generated corpus independent of the
        // standard library's implementation-specific random engines.
        uint32_t value = m_state;
        value ^= value << 13;
        value ^= value >> 17;
        value ^= value << 5;
        m_state = value;
        return value;
    }

    float Float( float minimum, float maximum )
    {
        constexpr float kInverseU24 = 1.0f / 16777215.0f;
        const float unit = static_cast<float>( NextU32() >> 8 ) * kInverseU24;
        return minimum + ( maximum - minimum ) * unit;
    }

  private:
    uint32_t m_state;
};
} // namespace SkullbonezTests
