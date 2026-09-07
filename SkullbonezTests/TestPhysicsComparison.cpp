#include "../ThirdPtySource/doctest/doctest.h"
#include "../SkullbonezSource/Runtime/Planning/PhysicsComparison.h"
#include <algorithm>
#include <cmath>

using namespace SkullbonezCore::Runtime;
namespace SkullbonezCore::Runtime
{
struct PhysicsComparisonTestAccess
{
    static void Populate( PhysicsComparison& comparison, int ticks )
    {
        comparison.m_lastTick = ticks;
        comparison.m_loopEnd = ticks;
        for ( auto& side : comparison.m_recordings )
        {
            for ( int tick = 1; tick <= ticks; ++tick )
            {
                ReplayPresentationSample sample;
                sample.sceneFrame = tick - 1;
                sample.physicsDt = 1.0f / 120;
                ReplayBodyPresentationSample body;
                body.id.value = 7;
                body.position = { 0, static_cast<float>( tick ), 0 };
                sample.bodies.push_back( body );
                side.frames.push_back( sample );
            }
        }
    }
    static auto& Recordings( PhysicsComparison& comparison )
    {
        return comparison.m_recordings;
    }
    static void Events( PhysicsComparison& comparison )
    {
        comparison.BuildEvents();
    }
};
} // namespace SkullbonezCore::Runtime
TEST_CASE( "Physics comparison detects small hops, rotations and sleep without a tolerance mutation" )
{
    ReplayBodyPresentationSample a, b;
    a.id.value = b.id.value = 17;
    CHECK( PhysicsComparison::Compare( &a, &b ).change == ComparisonChange::Equal );
    b.position.y = 0.0001f;
    CHECK( PhysicsComparison::Compare( &a, &b ).distance == doctest::Approx( 0.0001 ) );
    b.position = a.position;
    b.orientation[1] = std::sin( 0.025f );
    b.orientation[3] = std::cos( 0.025f );
    CHECK( PhysicsComparison::Compare( &a, &b ).angleDegrees == doctest::Approx( 2.86479 ).epsilon( 0.0001 ) );
    a = b;
    for ( auto& component : b.orientation )
    {
        component = -component;
    }
    CHECK( PhysicsComparison::Compare( &a, &b ).change == ComparisonChange::Equal );
    b.sleeping = true;
    CHECK( PhysicsComparison::Compare( &a, &b ).sleepChanged );
    CHECK( PhysicsComparison::Compare( &a, nullptr ).change == ComparisonChange::OnlyA );
    CHECK( PhysicsComparison::Compare( nullptr, nullptr ).change == ComparisonChange::NotRecorded );
}
TEST_CASE( "Physics comparison seeks exact ticks and repeats a range without changing evidence" )
{
    PhysicsComparison comparison;
    PhysicsComparisonTestAccess::Populate( comparison, 8 );
    CHECK( comparison.Tick() == 0 );
    CHECK( comparison.Recording( 0 ).Frame( 0 ) == nullptr );
    comparison.Seek( 4 );
    const auto* first = comparison.Body( 0, 7, 4 );
    REQUIRE( first );
    comparison.Seek( 8 );
    comparison.Seek( 4 );
    CHECK( comparison.Body( 0, 7, 4 ) == first );
    comparison.SetLoop( 2, 4, true );
    comparison.Play( 1 );
    comparison.Advance( 1.0 / 120 );
    CHECK( comparison.Tick() == 2 );
    comparison.Play( -1 );
    comparison.Advance( 1.0 / 120 );
    CHECK( comparison.Tick() == 4 );
    comparison.Step( -1 );
    CHECK( comparison.Tick() == 3 );
    CHECK( comparison.Direction() == 0 );
}
TEST_CASE( "Physics comparison contact matching uses stable ids and rejects ambiguous features" )
{
    PhysicsComparison comparison;
    PhysicsComparisonTestAccess::Populate( comparison, 1 );
    auto& recordings = PhysicsComparisonTestAccess::Recordings( comparison );
    for ( int side = 0; side < 2; ++side )
    {
        ReplaySolverFrameSample evidence;
        evidence.sceneFrame = 0;
        ReplaySolverBodySample a, b;
        a.id.value = 7;
        b.id.value = 9;
        a.modelRow.value = side ? 12 : 2;
        b.modelRow.value = side ? 3 : 8;
        evidence.bodies = { a, b };
        SkullbonezCore::Physics::PhysicsSolverPersistentContactSample contact;
        contact.bodyA = a.modelRow.value;
        contact.bodyB = b.modelRow.value;
        contact.featureId = 123;
        contact.accN = 2;
        evidence.worldSnapshot.physics.persistentContacts.push_back( contact );
        recordings[side].diagnostics.push_back( evidence );
    }
    PhysicsComparisonTestAccess::Events( comparison );
    REQUIRE( comparison.Events().size() == 1 );
    CHECK( comparison.Events()[0].change == ComparisonChange::Equal );
    auto& b = recordings[1].diagnostics[0].worldSnapshot.physics.persistentContacts;
    b[0].accN = 2.1f;
    PhysicsComparisonTestAccess::Events( comparison );
    CHECK( comparison.Events()[0].change == ComparisonChange::Changed );
    b[0].featureId = 124;
    PhysicsComparisonTestAccess::Events( comparison );
    REQUIRE( comparison.Events().size() == 2 );
    CHECK( comparison.Events()[0].change == ComparisonChange::OnlyA );
    CHECK( comparison.Events()[1].change == ComparisonChange::OnlyB );
    b[0].featureId = 123;
    b.push_back( b[0] );
    PhysicsComparisonTestAccess::Events( comparison );
    REQUIRE( comparison.Events().size() == 3 );
    for ( const auto& event : comparison.Events() )
    {
        CHECK( event.change == ComparisonChange::Ambiguous );
    }
    recordings[1].diagnostics.clear();
    PhysicsComparisonTestAccess::Events( comparison );
    REQUIRE( comparison.Events().size() == 1 );
    CHECK( comparison.Events()[0].change == ComparisonChange::NotRecorded );
}

TEST_CASE( "Physics comparison selection enables orbit and layouts remain independent of transport" )
{
    PhysicsComparison comparison;
    comparison.Select( 42 );
    CHECK( comparison.Settings().orbitSelected );
    CHECK( comparison.Settings().followA );
    CHECK( comparison.SetSetting( "stackedViews", 1 ) );
    CHECK( comparison.Settings().stackedViews );
    CHECK( comparison.Selected() == 42 );
    CHECK( comparison.Tick() == 0 );
    comparison.Select( 0 );
    CHECK_FALSE( comparison.Settings().orbitSelected );
    CHECK_FALSE( comparison.Settings().followA );
}

TEST_CASE( "Physics comparison repeated contacts preserve A/A equality without assuming row correspondence" )
{
    PhysicsComparison comparison;
    PhysicsComparisonTestAccess::Populate( comparison, 1 );
    auto& recordings = PhysicsComparisonTestAccess::Recordings( comparison );
    for ( auto& recording : recordings )
    {
        recording.observations.resize( 2 );
        auto& observation = recording.observations[1];
        observation.recorded = true;
        ComparisonRecording::ContactSummary contact;
        contact.bodyA = 7;
        contact.bodyB = 9;
        contact.feature = 123;
        contact.normalImpulse = 2;
        observation.contacts.push_back( contact );
        contact.normalImpulse = 3;
        observation.contacts.push_back( contact );
        observation.contacts.push_back( contact );
    }
    std::reverse( recordings[1].observations[1].contacts.begin(), recordings[1].observations[1].contacts.end() );
    PhysicsComparisonTestAccess::Events( comparison );
    REQUIRE( comparison.Events().size() == 3 );
    for ( const auto& event : comparison.Events() )
    {
        CHECK( event.change == ComparisonChange::Equal );
    }
    recordings[1].observations[1].contacts[0].normalImpulse = 4;
    PhysicsComparisonTestAccess::Events( comparison );
    CHECK( std::any_of( comparison.Events().begin(), comparison.Events().end(),
                        []( const auto& event ) { return event.change == ComparisonChange::Ambiguous; } ) );
}
TEST_CASE( "Physics comparison invalid finding exposes a useful loading error" )
{
    PhysicsComparison comparison;
    ReplayCameraSample camera;
    CHECK_FALSE( comparison.LoadFinding( "TestOutput/nonexistent-physics-finding.json", camera ) );
    CHECK_FALSE( comparison.Error().empty() );
}

TEST_CASE( "Physics comparison supplements equal summaries with recorded manifold differences" )
{
    PhysicsComparison comparison;
    PhysicsComparisonTestAccess::Populate( comparison, 1 );
    auto& recordings = PhysicsComparisonTestAccess::Recordings( comparison );
    for ( auto& side : recordings )
    {
        side.observations.resize( 2 );
        side.observations[1].recorded = true;
        ReplaySolverFrameSample evidence;
        evidence.sceneFrame = 0;
        ReplaySolverBodySample body;
        body.id.value = 7;
        body.modelRow.value = 0;
        evidence.bodies.push_back( body );
        SkullbonezCore::Physics::PhysicsSolverPersistentContactSample contact;
        contact.bodyA = 0;
        contact.isTerrain = true;
        evidence.worldSnapshot.physics.persistentContacts.push_back( contact );
        side.diagnostics.push_back( evidence );
    }
    recordings[1].diagnostics[0].worldSnapshot.physics.persistentContacts[0].rA.y = 0.01f;
    PhysicsComparisonTestAccess::Events( comparison );
    REQUIRE( comparison.Events().size() == 1 );
    CHECK( comparison.Events()[0].change == ComparisonChange::Changed );
    CHECK_FALSE( comparison.Events()[0].summary );
    REQUIRE( comparison.SelectEvent( 0 ) );
    const auto revision = comparison.EventSelectionRevision();
    REQUIRE( comparison.SelectEvent( 0 ) );
    CHECK( comparison.EventSelectionRevision() > revision );
}
