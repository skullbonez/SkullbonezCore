#include "../ThirdPtySource/doctest/doctest.h"
#include "../SkullbonezSource/Runtime/Planning/PhysicsComparison.h"
#include "../SkullbonezSource/Runtime/Planning/PhysicsComparison.Archive.h"
#include "../SkullbonezSource/Runtime/Planning/PhysicsComparison.DiagnosticLines.h"
#include <windows.h>
#include <compressapi.h>
#include <algorithm>
#include <cmath>
#include <fstream>

using namespace SkullbonezCore::Runtime;
namespace
{
bool WriteDiagnosticArchive( const std::filesystem::path& path, const std::string& raw )
{
    COMPRESSOR_HANDLE compressor = nullptr;
    if ( !CreateCompressor( COMPRESS_ALGORITHM_XPRESS_HUFF, nullptr, &compressor ) )
    {
        return false;
    }
    std::vector<char> packed( 65536 );
    SIZE_T size = 0;
    const bool compressed = Compress( compressor, raw.data(), raw.size(), packed.data(), packed.size(), &size ) != 0;
    CloseCompressor( compressor );
    if ( !compressed )
    {
        return false;
    }
    std::ofstream stream( path, std::ios::binary );
    stream.write( "SKDIAG1\n", 8 );
    const std::array<uint32_t, 2> sizes { static_cast<uint32_t>( raw.size() ), static_cast<uint32_t>( size ) };
    stream.write( reinterpret_cast<const char*>( sizes.data() ), sizeof( sizes ) );
    stream.write( packed.data(), static_cast<std::streamsize>( size ) );
    const uint64_t end = 0;
    stream.write( reinterpret_cast<const char*>( &end ), sizeof( end ) );
    return static_cast<bool>( stream );
}
} // namespace

TEST_CASE( "Physics comparison archives preserve bytes and reject incomplete evidence" )
{
    const auto directory = std::filesystem::temp_directory_path() /
                           ( "solver-lab-archive-test-" + std::to_string( GetCurrentProcessId() ) );
    std::filesystem::create_directories( directory );
    const auto output = directory / "restored.ndjson";
    const std::vector<std::string> parts { "first.skdiag", "second.skdiag" };
    const std::string first = "{\"kind\":\"contact\",\"impulse\":0.001}\r\n";
    const std::string second = "{\"kind\":\"frame\",\"tick\":2}\n";
    REQUIRE( WriteDiagnosticArchive( directory / parts[0], first ) );
    REQUIRE( WriteDiagnosticArchive( directory / parts[1], second ) );
    ComparisonLoadProgress progress;
    REQUIRE( RestoreComparisonDiagnostics( directory, parts, output, progress ) );
    std::ifstream input( output, std::ios::binary );
    const std::string restored( ( std::istreambuf_iterator<char>( input ) ), std::istreambuf_iterator<char>() );
    CHECK( restored == first + second );
    input.close();

    SUBCASE( "Cancellation never leaves partial evidence" )
    {
        progress.cancelled.store( true );
        CHECK_FALSE( RestoreComparisonDiagnostics( directory, parts, output, progress ) );
        CHECK_FALSE( std::filesystem::exists( output ) );
    }
    SUBCASE( "Truncated terminator cannot publish" )
    {
        const auto path = directory / parts[1];
        std::filesystem::resize_file( path, std::filesystem::file_size( path ) - 1 );
        CHECK_FALSE( RestoreComparisonDiagnostics( directory, parts, output, progress ) );
        CHECK_FALSE( std::filesystem::exists( output ) );
    }
    SUBCASE( "Parent paths cannot escape the evidence directory" )
    {
        const std::vector<std::string> escaped { "../first.skdiag" };
        CHECK_FALSE( RestoreComparisonDiagnostics( directory, escaped, output, progress ) );
    }
    SUBCASE( "Oversized blocks are rejected before decoding" )
    {
        std::fstream archive( directory / parts[0], std::ios::binary | std::ios::in | std::ios::out );
        archive.seekp( 8 );
        const uint32_t oversized = 2 * 1024 * 1024;
        archive.write( reinterpret_cast<const char*>( &oversized ), sizeof( oversized ) );
        archive.close();
        CHECK_FALSE( RestoreComparisonDiagnostics( directory, parts, output, progress ) );
        CHECK_FALSE( std::filesystem::exists( output ) );
    }
    std::filesystem::remove_all( directory );
}

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

TEST_CASE( "Physics comparison buffered diagnostics preserve block boundaries and final lines" )
{
    const auto path = std::filesystem::temp_directory_path() /
                      ( "solver-lab-lines-" + std::to_string( GetCurrentProcessId() ) );
    const std::string first( 65530, 'x' );
    const std::string second = "a row crossing the block boundary\r";
    {
        std::ofstream output( path, std::ios::binary );
        output << first << '\n' << second << "\nfinal";
    }
    ComparisonDiagnosticLines input( path );
    std::string line;
    REQUIRE( input.Read( line ) );
    CHECK( line == first );
    REQUIRE( input.Read( line ) );
    CHECK( line == second );
    REQUIRE( input.Read( line ) );
    CHECK( line == "final" );
    CHECK_FALSE( input.Read( line ) );
    CHECK( input.Good() );
    REQUIRE( input.Rewind() );
    REQUIRE( input.Read( line ) );
    CHECK( line == first );
    const auto oversized = path.string() + ".oversized";
    {
        std::ofstream output( oversized );
        output << std::string( 65537, 'x' );
    }
    ComparisonDiagnosticLines invalid( oversized );
    CHECK_FALSE( invalid.Read( line ) );
    CHECK_FALSE( invalid.Good() );
}

TEST_CASE( "Physics comparison streaming JSON preserves contact values and rejects malformed normals" )
{
    PhysicsComparison comparison;
    PhysicsComparisonTestAccess::Populate( comparison, 1 );
    auto& recording = PhysicsComparisonTestAccess::Recordings( comparison )[0];
    recording.frames[0].bodies[0].modelRow.value = 0;
    const auto path = std::filesystem::temp_directory_path() /
                      ( "solver-lab-row-" + std::to_string( GetCurrentProcessId() ) );
    auto load = [&]( const std::string& normal )
    {
        recording.observations.clear();
        std::ofstream output( path, std::ios::binary );
        output << "{\"kind\":\"frame\",\"frame\":0}\n"
               << "{\"kind\":\"contact\",\"frame\":0,\"body_a\":0,\"body_b\":-1,\"warm_started\":1,"
                  "\"feature_id\":4294967295,\"normal\":"
               << normal
               << ",\"penetration\":-0.125,"
                  "\"normal_impulse\":2.5,\"tangent_impulse\":-0.75,\"pre_solve_normal_speed\":-1.25,"
                  "\"pre_solve_slip_speed\":0.5,\"slip_speed\":0.25,\"ignored\":{\"normal\":[9,9,9]}}\n";
        output.close();
        uint64_t bytes = 0;
        return recording.LoadObservations( path.string().c_str(), 1, bytes );
    };
    REQUIRE( load( "[0.25,0.5,-0.75]" ) );
    const auto* observation = recording.Observation( 1 );
    REQUIRE( observation );
    REQUIRE( observation->contacts.size() == 1 );
    const auto& contact = observation->contacts[0];
    CHECK( contact.bodyA == 7 );
    CHECK( contact.terrain );
    CHECK( contact.feature == UINT32_MAX );
    CHECK( contact.warmStarted );
    CHECK( contact.normal.x == 0.25f );
    CHECK( contact.normal.y == 0.5f );
    CHECK( contact.normal.z == -0.75f );
    CHECK( contact.penetration == -0.125f );
    CHECK( contact.normalImpulse == 2.5f );
    CHECK( contact.tangentImpulse == -0.75f );
    CHECK( contact.preNormalSpeed == -1.25f );
    CHECK( contact.preSlipSpeed == 0.5f );
    CHECK( contact.postSlipSpeed == 0.25f );
    for ( const char* normal : { "[1,true,2,3]", "[1,[],2,3]", "[1,2]", "[1,2,3,4]", "[1,2,1e100]", "[1,2,3" } )
    {
        CHECK_FALSE( load( normal ) );
    }
    std::filesystem::remove( path );
}
