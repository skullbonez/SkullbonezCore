/*
File: SkullbonezTests/TestApplicationExitState.cpp
Purpose:
  Verifies application-exit request, failure precedence, bounded diagnostics,
  and platform-neutral exit-code resolution.

Summary:
  Tests drive the state in the same orders the frame loop can observe: normal
  shutdown, subsystem failure, then an integer exit message. The final result
  must retain the most useful failure without requiring a real window loop.

Glossary:

  Precedence: Stable rule selecting the first owned failure over later exit data.

Invariants:
  - Tests use integer message codes and do not depend on Win32 types or calls.
  - A status-free frame phase reports failure only through RequestPhaseFailure;
    resolving message exit code zero must preserve that failure.
  - Every precedence ordering that could overwrite an owned failure has explicit
    coverage.

Related:
  - Agentic/Reference/engine-glossary.md
  - SkullbonezSource/Runtime/App/ApplicationExitState.h defines the tested contract.
  - SkullbonezSource/Core/SbResult.h defines success and recoverable failure values.
*/
#include "../SkullbonezSource/Runtime/App/ApplicationExitState.h"
#include "../SkullbonezSource/Core/SbDiagnosticStore.h"

#include "doctest/doctest.h"

#include <array>
#include <cstring>

using SkullbonezCore::Core::SbDiagnosticStore;
using SkullbonezCore::Core::SbResult;
using SkullbonezCore::Runtime::ApplicationExitState;

namespace
{
SbDiagnosticStore diagnostics;
}

TEST_CASE( "Application exit state starts idle and resolves zero normally" )
{
    const ApplicationExitState state( diagnostics );

    CHECK_FALSE( state.ExitRequested() );
    CHECK_FALSE( state.HasOwnedFailure() );
    CHECK( state.Resolve( 0 ).Ok() );
}


TEST_CASE( "Normal exit requests successful shutdown" )
{
    ApplicationExitState state( diagnostics );
    state.RequestNormalExit();

    CHECK( state.ExitRequested() );
    CHECK_FALSE( state.HasOwnedFailure() );
    CHECK( state.Resolve( 0 ).Ok() );
}


TEST_CASE( "Owned failure requests exit and retains owner diagnostics" )
{
    ApplicationExitState state( diagnostics );
    state.RequestOwnedFailure( diagnostics.Failure( "CaptureController", "readback failed at frame %d", 17 ) );

    const SkullbonezCore::Core::SbResult result = state.Resolve( 0 );
    CHECK( state.ExitRequested() );
    CHECK( state.HasOwnedFailure() );
    CHECK_FALSE( result.Ok() );
    CHECK_EQ( std::strcmp( result.ErrorOwner(), "CaptureController" ), 0 );
    CHECK_EQ( std::strcmp( result.ErrorMessage(), "readback failed at frame 17" ), 0 );
}


TEST_CASE( "Status-free frame phase failure latch cannot resolve as process exit zero" )
{
    ApplicationExitState state( diagnostics );
    const SbResult phaseFailure = diagnostics.Failure( "Runtime/Present", "swap-chain presentation failed" );

    state.RequestPhaseFailure( phaseFailure );
    const SbResult result = state.Resolve( 0 );

    CHECK( state.ExitRequested() );
    CHECK( state.HasOwnedFailure() );
    CHECK_FALSE( result.Ok() );
    CHECK_EQ( std::strcmp( result.ErrorOwner(), "Runtime/Present" ), 0 );
    CHECK_EQ( std::strcmp( result.ErrorMessage(), "swap-chain presentation failed" ), 0 );
}


TEST_CASE( "Normal exit cannot overwrite an owned failure" )
{
    ApplicationExitState state( diagnostics );
    state.RequestOwnedFailure( diagnostics.Failure( "SceneController", "scene load failed" ) );
    state.RequestNormalExit();

    const SkullbonezCore::Core::SbResult result = state.Resolve( 0 );
    CHECK_FALSE( result.Ok() );
    CHECK_EQ( std::strcmp( result.ErrorOwner(), "SceneController" ), 0 );
    CHECK_EQ( std::strcmp( result.ErrorMessage(), "scene load failed" ), 0 );
}


TEST_CASE( "First owned failure wins over later failures" )
{
    ApplicationExitState state( diagnostics );
    state.RequestOwnedFailure( diagnostics.Failure( "FirstOwner", "first failure" ) );
    state.RequestOwnedFailure( diagnostics.Failure( "SecondOwner", "second failure" ) );

    const SkullbonezCore::Core::SbResult result = state.Resolve( 0 );
    CHECK_FALSE( result.Ok() );
    CHECK_EQ( std::strcmp( result.ErrorOwner(), "FirstOwner" ), 0 );
    CHECK_EQ( std::strcmp( result.ErrorMessage(), "first failure" ), 0 );
}


TEST_CASE( "Success values do not request exit or consume failure precedence" )
{
    ApplicationExitState state( diagnostics );
    state.RequestOwnedFailure( SkullbonezCore::Core::SbResult::Success() );

    CHECK_FALSE( state.ExitRequested() );
    CHECK_FALSE( state.HasOwnedFailure() );

    state.RequestOwnedFailure( diagnostics.Failure( "LaterOwner", "later real failure" ) );
    const SkullbonezCore::Core::SbResult result = state.Resolve( 0 );
    CHECK_FALSE( result.Ok() );
    CHECK_EQ( std::strcmp( result.ErrorOwner(), "LaterOwner" ), 0 );
}


TEST_CASE( "Owned failure arriving after normal exit still determines the result" )
{
    ApplicationExitState state( diagnostics );
    state.RequestNormalExit();
    state.RequestOwnedFailure( diagnostics.Failure( "Runtime/Window", "resize failed" ) );

    const SkullbonezCore::Core::SbResult result = state.Resolve( 0 );
    CHECK_FALSE( result.Ok() );
    CHECK_EQ( std::strcmp( result.ErrorOwner(), "Runtime/Window" ), 0 );
    CHECK_EQ( std::strcmp( result.ErrorMessage(), "resize failed" ), 0 );
}


TEST_CASE( "Nonzero message exit code resolves as a synthetic failure" )
{
    const ApplicationExitState state( diagnostics );
    const SkullbonezCore::Core::SbResult result = state.Resolve( 23 );

    CHECK_FALSE( result.Ok() );
    CHECK_EQ( std::strcmp( result.ErrorOwner(), "Runtime/ApplicationExit" ), 0 );
    CHECK( std::strstr( result.ErrorMessage(), "23" ) != nullptr );
}


TEST_CASE( "Negative message exit code also resolves as failure" )
{
    const ApplicationExitState state( diagnostics );
    const SkullbonezCore::Core::SbResult result = state.Resolve( -9 );

    CHECK_FALSE( result.Ok() );
    CHECK( std::strstr( result.ErrorMessage(), "-9" ) != nullptr );
}


TEST_CASE( "Owned failure outranks a nonzero message exit code" )
{
    ApplicationExitState state( diagnostics );
    state.RequestOwnedFailure( diagnostics.Failure( "Runtime/Capture", "specific capture failure" ) );

    const SkullbonezCore::Core::SbResult result = state.Resolve( 91 );
    CHECK_FALSE( result.Ok() );
    CHECK_EQ( std::strcmp( result.ErrorOwner(), "Runtime/Capture" ), 0 );
    CHECK_EQ( std::strcmp( result.ErrorMessage(), "specific capture failure" ), 0 );
    CHECK( std::strstr( result.ErrorMessage(), "91" ) == nullptr );
}


TEST_CASE( "Failure owner is copied into bounded state storage" )
{
    ApplicationExitState state( diagnostics );
    std::array<char, SkullbonezCore::Core::SbDiagnosticStore::OWNER_CAPACITY> owner = {};
    owner.fill( 'o' );
    owner.back() = '\0';
    state.RequestOwnedFailure( diagnostics.Failure( owner.data(), "bounded owner" ) );
    owner.fill( 'x' );

    const SkullbonezCore::Core::SbResult result = state.Resolve( 0 );
    CHECK_FALSE( result.Ok() );
    CHECK_EQ( std::strlen( result.ErrorOwner() ), SkullbonezCore::Core::SbDiagnosticStore::OWNER_CAPACITY - 1 );
    CHECK_EQ( result.ErrorOwner()[0], 'o' );
    CHECK_EQ( result.ErrorOwner()[SkullbonezCore::Core::SbDiagnosticStore::OWNER_CAPACITY - 2], 'o' );
}


TEST_CASE( "Failure message is copied and bounded independently" )
{
    ApplicationExitState state( diagnostics );
    std::array<char, SkullbonezCore::Core::SbDiagnosticStore::MESSAGE_CAPACITY + 32> message = {};
    message.fill( 'm' );
    message.back() = '\0';
    SkullbonezCore::Core::SbResult failure = diagnostics.Failure( "BoundedMessageOwner", "%s", message.data() );
    state.RequestOwnedFailure( failure );
    message.fill( 'x' );

    const SkullbonezCore::Core::SbResult result = state.Resolve( 0 );
    CHECK_FALSE( result.Ok() );
    CHECK_EQ( std::strlen( result.ErrorMessage() ), SkullbonezCore::Core::SbDiagnosticStore::MESSAGE_CAPACITY - 1 );
    CHECK_EQ( result.ErrorMessage()[0], 'm' );
    CHECK_EQ( result.ErrorMessage()[SkullbonezCore::Core::SbDiagnosticStore::MESSAGE_CAPACITY - 2], 'm' );
}


TEST_CASE( "Resolved exit failure keeps its diagnostic lease after the exit state is destroyed" )
{
    SkullbonezCore::Core::SbResult result;
    {
        ApplicationExitState original( diagnostics );
        original.RequestOwnedFailure( diagnostics.Failure( "ReplayProbe", "copied failure" ) );
        result = original.Resolve( 0 );
    }

    CHECK_FALSE( result.Ok() );
    CHECK_EQ( std::strcmp( result.ErrorOwner(), "ReplayProbe" ), 0 );
    CHECK_EQ( std::strcmp( result.ErrorMessage(), "copied failure" ), 0 );
}
