/*
File: SkullbonezTests/TestStringHash.cpp
Purpose:
  Verifies deterministic string hashes use byte values rather than signed chars.

Summary:
  Compile-time and runtime cases bind canonical FNV-1a identities for ASCII,
  one high-bit byte, and a complete two-byte UTF-8 code unit sequence.

Invariants:
  - Compiler default char signedness cannot change a hash identity.
  - Existing ASCII identities remain unchanged.

Related:
  - SkullbonezSource/Core/StringHash.h
*/
#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Core/StringHash.h"

static_assert( HashStr( "a" ) == 0xe40c292cu );
static_assert( HashStr( "\x80" ) == 0x850b939fu );
static_assert( HashStr( "\xc3\xa9" ) == 0x1e9de8c1u );

TEST_CASE( "StringHash mixes canonical unsigned UTF-8 bytes" )
{
    CHECK( HashStr( "a" ) == 0xe40c292cu );
    CHECK( HashStr( "\x80" ) == 0x850b939fu );
    CHECK( HashStr( "\xc3\xa9" ) == 0x1e9de8c1u );
}
