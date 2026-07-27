/*
File: SkullbonezSource/Core/ByteView.h
Purpose:
  Provides the engine's typed, allocation-free view of immutable byte ranges.

Summary:
  ByteView carries a pointer and byte count without erasing the boundary to
  void. ObjectBytes exposes the representation of trivially copyable values

  for deterministic hashes and cold binary formats.

Glossary:
  Object representation: The contiguous bytes that encode a C++ object value.
  Byte view: A non-owning span whose extent is measured in uint8_t elements.

Invariants:
  - A view never owns or extends the lifetime of its source.
  - ObjectBytes accepts only trivially copyable values.
  - Callers consume the view synchronously; they must not retain it.

Related:
  - Agentic/Reports/2026-07-18/small-findings-h0-rulings-census.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <type_traits>

namespace SkullbonezCore::Core
{
using ByteView = std::span<const uint8_t>;

template <typename T> ByteView ObjectBytes( const T& value ) noexcept
{
    static_assert( std::is_trivially_copyable_v<T>, "ObjectBytes requires a trivially copyable value." );

    // Why: C++ permits inspection of any object representation through an
    // unsigned-character type. This is the single typed boundary used by
    // deterministic hashes and binary writers instead of public void pointers.
    return { reinterpret_cast<const uint8_t*>( std::addressof( value ) ), sizeof( T ) };
}

template <typename T> ByteView ObjectBytes( std::span<const T> values ) noexcept
{
    static_assert( std::is_trivially_copyable_v<T>, "ObjectBytes requires trivially copyable elements." );

    if ( values.empty() )
    {
        return {};
    }

    return { ObjectBytes( values.front() ).data(), values.size_bytes() };
}
} // namespace SkullbonezCore::Core
