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
  Owned failure: Lane R result carrying the subsystem owner and message that
    originally explained why the run must stop.
  Synthetic failure: Generic failure created only from a nonzero message code.
  Precedence: Stable rule selecting the first owned failure over later exit data.

Invariants:
  - Tests use integer message codes and do not depend on Win32 types or calls.
  - Every precedence ordering that could overwrite an owned failure has explicit
    coverage.

Related:
  - SkullbonezSource/Runtime/ApplicationExitState.h defines the tested contract.
  - SkullbonezSource/Core/SbResult.h defines success and Lane R failure values.
*/
#include "../SkullbonezSource/Runtime/ApplicationExitState.h"

#include "doctest/doctest.h"

#include <array>
#include <cstring>

using SkullbonezCore::Runtime::ApplicationExitState;
using SkullbonezCore::Core::SbResult;

TEST_CASE( "Application exit state starts idle and resolves zero normally" )
{
    const ApplicationExitState state;

    CHECK_FALSE( state.ExitRequested() );
    CHECK_FALSE( state.HasOwnedFailure() );
    CHECK( state.Resolve( 0 ).ok );
}


TEST_CASE( "Normal exit requests successful shutdown" )
{
    ApplicationExitState state;
    state.RequestNormalExit();

    CHECK( state.ExitRequested() );
    CHECK_FALSE( state.HasOwnedFailure() );
    CHECK( state.Resolve( 0 ).ok );
}


TEST_CASE( "Owned failure requests exit and retains owner diagnostics" )
{
    ApplicationExitState state;
    state.RequestOwnedFailure( SkullbonezCore::Core::SbResult::Failure( "CaptureController", "readback failed at frame %d", 17 ) );

    const SkullbonezCore::Core::SbResult result = state.Resolve( 0 );
    CHECK( state.ExitRequested() );
    CHECK( state.HasOwnedFailure() );
    CHECK_FALSE( result.ok );
    CHECK_EQ( std::strcmp( result.error.owner, "CaptureController" ), 0 );
    CHECK_EQ( std::strcmp( result.error.message, "readback failed at frame 17" ), 0 );
}


TEST_CASE( "Normal exit cannot overwrite an owned failure" )
{
    ApplicationExitState state;
    state.RequestOwnedFailure( SkullbonezCore::Core::SbResult::Failure( "SceneController", "scene load failed" ) );
    state.RequestNormalExit();

    const SkullbonezCore::Core::SbResult result = state.Resolve( 0 );
    CHECK_FALSE( result.ok );
    CHECK_EQ( std::strcmp( result.error.owner, "SceneController" ), 0 );
    CHECK_EQ( std::strcmp( result.error.message, "scene load failed" ), 0 );
}


TEST_CASE( "First owned failure wins over later failures" )
{
    ApplicationExitState state;
    state.RequestOwnedFailure( SkullbonezCore::Core::SbResult::Failure( "FirstOwner", "first failure" ) );
    state.RequestOwnedFailure( SkullbonezCore::Core::SbResult::Failure( "SecondOwner", "second failure" ) );

    const SkullbonezCore::Core::SbResult result = state.Resolve( 0 );
    CHECK_FALSE( result.ok );
    CHECK_EQ( std::strcmp( result.error.owner, "FirstOwner" ), 0 );
    CHECK_EQ( std::strcmp( result.error.message, "first failure" ), 0 );
}


TEST_CASE( "Success values do not request exit or consume failure precedence" )
{
    ApplicationExitState state;
    state.RequestOwnedFailure( SkullbonezCore::Core::SbResult::Success() );

    CHECK_FALSE( state.ExitRequested() );
    CHECK_FALSE( state.HasOwnedFailure() );

    state.RequestOwnedFailure( SkullbonezCore::Core::SbResult::Failure( "LaterOwner", "later real failure" ) );
    const SkullbonezCore::Core::SbResult result = state.Resolve( 0 );
    CHECK_FALSE( result.ok );
    CHECK_EQ( std::strcmp( result.error.owner, "LaterOwner" ), 0 );
}


TEST_CASE( "Owned failure arriving after normal exit still determines the result" )
{
    ApplicationExitState state;
    state.RequestNormalExit();
    state.RequestOwnedFailure( SkullbonezCore::Core::SbResult::Failure( "Runtime/Window", "resize failed" ) );

    const SkullbonezCore::Core::SbResult result = state.Resolve( 0 );
    CHECK_FALSE( result.ok );
    CHECK_EQ( std::strcmp( result.error.owner, "Runtime/Window" ), 0 );
    CHECK_EQ( std::strcmp( result.error.message, "resize failed" ), 0 );
}


TEST_CASE( "Nonzero message exit code resolves as a synthetic failure" )
{
    const ApplicationExitState state;
    const SkullbonezCore::Core::SbResult result = state.Resolve( 23 );

    CHECK_FALSE( result.ok );
    CHECK_EQ( std::strcmp( result.error.owner, "Runtime/ApplicationExit" ), 0 );
    CHECK( std::strstr( result.error.message, "23" ) != nullptr );
}


TEST_CASE( "Negative message exit code also resolves as failure" )
{
    const ApplicationExitState state;
    const SkullbonezCore::Core::SbResult result = state.Resolve( -9 );

    CHECK_FALSE( result.ok );
    CHECK( std::strstr( result.error.message, "-9" ) != nullptr );
}


TEST_CASE( "Owned failure outranks a nonzero message exit code" )
{
    ApplicationExitState state;
    state.RequestOwnedFailure( SkullbonezCore::Core::SbResult::Failure( "Runtime/Capture", "specific capture failure" ) );

    const SkullbonezCore::Core::SbResult result = state.Resolve( 91 );
    CHECK_FALSE( result.ok );
    CHECK_EQ( std::strcmp( result.error.owner, "Runtime/Capture" ), 0 );
    CHECK_EQ( std::strcmp( result.error.message, "specific capture failure" ), 0 );
    CHECK( std::strstr( result.error.message, "91" ) == nullptr );
}


TEST_CASE( "Failure owner is copied into bounded state storage" )
{
    ApplicationExitState state;
    std::array<char, ApplicationExitState::FAILURE_OWNER_CAPACITY + 32> owner = {};
    owner.fill( 'o' );
    owner.back() = '\0';
    state.RequestOwnedFailure( SkullbonezCore::Core::SbResult::Failure( owner.data(), "bounded owner" ) );
    owner.fill( 'x' );

    const SkullbonezCore::Core::SbResult result = state.Resolve( 0 );
    CHECK_FALSE( result.ok );
    CHECK_EQ( std::strlen( result.error.owner ), ApplicationExitState::FAILURE_OWNER_CAPACITY - 1 );
    CHECK_EQ( result.error.owner[0], 'o' );
    CHECK_EQ( result.error.owner[ApplicationExitState::FAILURE_OWNER_CAPACITY - 2], 'o' );
}


TEST_CASE( "Failure message is copied and bounded independently" )
{
    ApplicationExitState state;
    std::array<char, ApplicationExitState::FAILURE_MESSAGE_CAPACITY + 32> message = {};
    message.fill( 'm' );
    message.back() = '\0';
    SkullbonezCore::Core::SbResult failure = SkullbonezCore::Core::SbResult::Failure( "BoundedMessageOwner", "%s", message.data() );
    state.RequestOwnedFailure( failure );
    message.fill( 'x' );
    std::memset( failure.error.message, 'x', sizeof( failure.error.message ) );

    const SkullbonezCore::Core::SbResult result = state.Resolve( 0 );
    CHECK_FALSE( result.ok );
    CHECK_EQ( std::strlen( result.error.message ), ApplicationExitState::FAILURE_MESSAGE_CAPACITY - 1 );
    CHECK_EQ( result.error.message[0], 'm' );
    CHECK_EQ( result.error.message[ApplicationExitState::FAILURE_MESSAGE_CAPACITY - 2], 'm' );
}


TEST_CASE( "Copied exit state owns an independent diagnostic buffer" )
{
    ApplicationExitState copied;
    {
        ApplicationExitState original;
        original.RequestOwnedFailure( SkullbonezCore::Core::SbResult::Failure( "ReplayProbe", "copied failure" ) );
        copied = original;
    }

    const SkullbonezCore::Core::SbResult result = copied.Resolve( 0 );
    CHECK_FALSE( result.ok );
    CHECK_EQ( std::strcmp( result.error.owner, "ReplayProbe" ), 0 );
    CHECK_EQ( std::strcmp( result.error.message, "copied failure" ), 0 );
}
