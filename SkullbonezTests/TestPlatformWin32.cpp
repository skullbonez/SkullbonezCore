/*
File: SkullbonezTests/TestPlatformWin32.cpp
Purpose:
  Verifies failure and ownership behavior in narrow Windows platform helpers.

Summary:
  Clipboard tests drive the production publication algorithm through detached
  operations, so failure coverage never opens or mutates the machine clipboard.

Invariants:
  - Failed publication never reports success.
  - Clipboard memory is released locally until publication succeeds.
  - After publication succeeds, Windows owns the memory even if close fails.

Related:
  - SkullbonezSource/Core/PlatformWin32.h
*/
#if defined( _WIN32 )

#include "../ThirdPtySource/doctest/doctest.h"
#include "../SkullbonezSource/Core/PlatformWin32.h"

#include <array>
#include <cstring>

namespace
{
enum class ClipboardFailure
{
    None,
    Open,
    Empty,
    Allocate,
    Lock,
    Publish,
    Close,
};

struct ClipboardOperations
{
    static constexpr char SENTINEL = '\x5a';

    ClipboardFailure failure = ClipboardFailure::None;
    std::array<char, 64> bytes;
    std::size_t allocationLength = 0u;
    int openCalls = 0;
    int closeCalls = 0;
    int releaseCalls = 0;
    int operationSequence = 0;
    int unlockSequence = 0;
    int publishSequence = 0;
    bool published = false;

    ClipboardOperations() { bytes.fill( SENTINEL ); }

    bool Open( SkullbonezCore::Core::Platform::NativeWindowHandle owner ) noexcept
    {
        ++openCalls;
        return owner != nullptr && failure != ClipboardFailure::Open;
    }

    bool Empty() const noexcept { return failure != ClipboardFailure::Empty; }

    void* Allocate( std::size_t length ) noexcept
    {
        allocationLength = length;
        return failure == ClipboardFailure::Allocate || length > bytes.size() ? nullptr : bytes.data();
    }

    void* Lock( void* memory ) const noexcept { return failure == ClipboardFailure::Lock ? nullptr : memory; }
    void Unlock( void* ) noexcept { unlockSequence = ++operationSequence; }

    void Release( void* ) noexcept { ++releaseCalls; }

    bool Publish( void* ) noexcept
    {
        publishSequence = ++operationSequence;

        if ( failure == ClipboardFailure::Publish )
        {
            return false;
        }

        published = true;
        return true;
    }

    bool Close() noexcept
    {
        ++closeCalls;
        return failure != ClipboardFailure::Close;
    }
};
} // namespace

TEST_CASE( "Platform clipboard reports every publication failure and preserves memory ownership" )
{
    constexpr const char* text = "clipboard payload";
    const auto owner = reinterpret_cast<SkullbonezCore::Core::Platform::NativeWindowHandle>( 1 );

    {
        ClipboardOperations operations;
        CHECK_FALSE(
            SkullbonezCore::Core::Platform::Detail::CopyTextToClipboardWithOperations( owner, nullptr, operations ) );
        CHECK( operations.openCalls == 0 );
    }

    {
        ClipboardOperations operations;
        CHECK_FALSE(
            SkullbonezCore::Core::Platform::Detail::CopyTextToClipboardWithOperations( nullptr, text, operations ) );
        CHECK( operations.openCalls == 0 );
    }

    struct FailureExpectation
    {
        ClipboardFailure failure;
        int closeCalls;
        int releaseCalls;
        bool published;
    };
    constexpr FailureExpectation failures[] = {
        { ClipboardFailure::Open, 0, 0, false },      { ClipboardFailure::Empty, 1, 0, false },
        { ClipboardFailure::Allocate, 1, 0, false },  { ClipboardFailure::Lock, 1, 1, false },
        { ClipboardFailure::Publish, 1, 1, false },   { ClipboardFailure::Close, 1, 0, true },
    };

    for ( const FailureExpectation& expectation : failures )
    {
        ClipboardOperations operations;
        operations.failure = expectation.failure;
        CHECK_FALSE(
            SkullbonezCore::Core::Platform::Detail::CopyTextToClipboardWithOperations( owner, text, operations ) );
        CHECK( operations.openCalls == 1 );
        CHECK( operations.closeCalls == expectation.closeCalls );
        CHECK( operations.releaseCalls == expectation.releaseCalls );
        CHECK( operations.published == expectation.published );
    }

    ClipboardOperations success;
    CHECK( SkullbonezCore::Core::Platform::Detail::CopyTextToClipboardWithOperations( owner, text, success ) );
    CHECK( success.closeCalls == 1 );
    CHECK( success.releaseCalls == 0 );
    CHECK( success.published );
    REQUIRE( std::strlen( text ) + 1u < success.bytes.size() );
    CHECK( success.allocationLength == std::strlen( text ) + 1u );
    REQUIRE( success.bytes[std::strlen( text )] == '\0' );
    CHECK( std::strcmp( success.bytes.data(), text ) == 0 );
    CHECK( success.bytes[std::strlen( text ) + 1u] == ClipboardOperations::SENTINEL );
    CHECK( success.unlockSequence > 0 );
    CHECK( success.unlockSequence < success.publishSequence );
}

#endif
