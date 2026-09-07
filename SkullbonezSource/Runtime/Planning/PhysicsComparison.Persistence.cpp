#include "PhysicsComparison.h"
#include "../Replay/ReplayV2Artifact.h"
#include "../../Core/Allocation/RuntimeAllocationTracker.h"
#include "../../../ThirdPtySource/nlohmann/json.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <windows.h>
#include <bcrypt.h>
#pragma comment( lib, "bcrypt.lib" )

using namespace SkullbonezCore::Runtime;
using Json = nlohmann::ordered_json;
namespace fs = std::filesystem;

namespace
{
bool MatchesFileHash( const fs::path& path, const std::string& expected )
{
    if ( expected.size() != 64 )
    {
        return false;
    }
    std::ifstream input( path, std::ios::binary );
    if ( !input )
    {
        return false;
    }
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::array<unsigned char, 1024> object {};
    std::array<unsigned char, 65536> buffer {};
    std::array<unsigned char, 32> digest {};
    DWORD size = 0, written = 0;
    NTSTATUS status = BCryptOpenAlgorithmProvider( &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0 );
    if ( status >= 0 )
    {
        status = BCryptGetProperty( algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>( &size ), sizeof( size ),
                                    &written, 0 );
    }
    if ( status >= 0 && size > object.size() )
    {
        status = -1;
    }
    if ( status >= 0 )
    {
        status = BCryptCreateHash( algorithm, &hash, object.data(), size, nullptr, 0, 0 );
    }
    while ( status >= 0 && input )
    {
        input.read( reinterpret_cast<char*>( buffer.data() ), buffer.size() );
        status = BCryptHashData( hash, buffer.data(), static_cast<ULONG>( input.gcount() ), 0 );
    }
    if ( status >= 0 && input.eof() )
    {
        status = BCryptFinishHash( hash, digest.data(), static_cast<ULONG>( digest.size() ), 0 );
    }
    else
    {
        status = -1;
    }
    if ( hash )
    {
        BCryptDestroyHash( hash );
    }
    if ( algorithm )
    {
        BCryptCloseAlgorithmProvider( algorithm, 0 );
    }
    if ( status < 0 )
    {
        return false;
    }
    constexpr char digits[] = "0123456789abcdef";
    for ( std::size_t i = 0; i < digest.size(); ++i )
    {
        if ( expected[2 * i] != digits[digest[i] >> 4] || expected[2 * i + 1] != digits[digest[i] & 15] )
        {
            return false;
        }
    }
    return true;
}
Json ReadJson( const fs::path& path )
{
    std::error_code error;
    const auto size = fs::file_size( path, error );
    if ( error || size > 2 * 1024 * 1024 )
    {
        return Json( Json::value_t::discarded );
    }
    std::ifstream input( path );
    return input ? Json::parse( input, nullptr, false ) : Json( Json::value_t::discarded );
}
bool ValidFields( const Json& json, std::initializer_list<const char*> keys, Json::value_t type )
{
    for ( const char* key : keys )
    {
        const auto field = json.find( key );
        if ( field == json.end() )
        {
            continue;
        }
        if ( type == Json::value_t::number_float )
        {
            if ( !field->is_number() || !std::isfinite( field->get<double>() ) || std::abs( field->get<double>() ) > 1.0e30 )
            {
                return false;
            }
        }
        else if ( type == Json::value_t::number_integer )
        {
            if ( !field->is_number_integer() || field->get<double>() < -1 || field->get<double>() > INT_MAX )
            {
                return false;
            }
        }
        else if ( field->type() != type )
        {
            return false;
        }
    }
    return true;
}
bool ValidFindingFields( const Json& json )
{
    if ( !ValidFields( json, { "format", "bundle", "note" }, Json::value_t::string ) ||
         !ValidFields( json, { "version", "tick", "event" }, Json::value_t::number_integer ) )
    {
        return false;
    }
    if ( json.contains( "selected" ) && !json["selected"].is_number_unsigned() )
    {
        return false;
    }
    if ( json.contains( "loop" ) )
    {
        const auto& loop = json["loop"];
        if ( !loop.is_array() || loop.size() != 3 )
        {
            return false;
        }
        for ( const auto& value : loop )
        {
            if ( !value.is_number_integer() || value.get<double>() < 0 || value.get<double>() > 14400 )
            {
                return false;
            }
        }
    }
    if ( !json.contains( "settings" ) || !json["settings"].is_object() )
    {
        return false;
    }
    const auto& settings = json["settings"];
    return ValidFields( settings, { "display" }, Json::value_t::number_integer ) &&
           ValidFields( settings,
                        { "showA", "stackedViews", "orbitSelected", "angularHeatmap", "occludedOutline", "followA",
                          "selectedOnly", "differencesOnly" },
                        Json::value_t::boolean ) &&
           ValidFields( settings,
                        { "outlineAlpha", "heatScale", "pixelGain", "positionThreshold", "angleThreshold",
                          "impulseThreshold", "speed" },
                        Json::value_t::number_float );
}
bool VerifyInputs( const Json& manifest, const fs::path& directory, ComparisonLoadProgress& progress )
{
    if ( !manifest.contains( "inputs" ) || !manifest["inputs"].is_object() || !manifest["inputs"].contains( "files" ) ||
         !manifest["inputs"]["files"].is_object() )
    {
        return false;
    }
    const auto& files = manifest["inputs"]["files"];
    if ( !files.contains( "scene.scene.json" ) )
    {
        return false;
    }
    for ( const auto& entry : files.items() )
    {
        if ( !entry.value().is_string() || progress.cancelled.load( std::memory_order_relaxed ) )
        {
            return false;
        }
        const fs::path relative( entry.key() );
        if ( relative.is_absolute() || relative.has_root_path() )
        {
            return false;
        }
        for ( const auto& component : relative )
        {
            if ( component == ".." )
            {
                return false;
            }
        }
        const auto expected = entry.value().get<std::string>();
        if ( !MatchesFileHash( directory / "inputs" / relative, expected ) )
        {
            return false;
        }
        // These scene asset readers use the viewer's normal runtime root.
        // Refuse an inspection with changed geometry/material assets.
        if ( entry.key().starts_with( "SkullbonezData/assets/" ) || entry.key().starts_with( "SkullbonezData/hulls/" ) ||
             entry.key().starts_with( "SkullbonezData/styles/" ) )
        {
            if ( !MatchesFileHash( fs::current_path() / relative, expected ) )
            {
                return false;
            }
        }
    }
    return true;
}
bool PreflightMemory( const Json& manifest, const fs::path& directory, ComparisonLoadProgress& progress, uint64_t& bytes,
                      std::size_t& events )
{
    // V3-V5 write complete body/contact records, not compressed deltas. Charge
    // four times artifact bytes for decoded fields, alignment and transient
    // reader storage, plus fixed frame headers and worst-case event rows.
    // Diagnostic text is streamed; contact/iteration rows reserve exact counts.
    bytes = 16ull * 1024 * 1024;
    events = 0;
    const uint64_t ticks = manifest["ticks"].get<uint64_t>();
    for ( int side = 0; side < 2; ++side )
    {
        const auto& metadata = manifest["sides"][side];
        if ( !metadata.is_object() || !metadata.contains( "path" ) || !metadata["path"].is_string() ||
             !metadata.contains( "version" ) || !metadata["version"].is_number_integer() )
        {
            return false;
        }
        const int version = metadata["version"].get<int>();
        const fs::path relative = metadata["path"].get<std::string>();
        if ( version < 3 || version > 5 || relative.has_parent_path() || relative.is_absolute() )
        {
            return false;
        }
        std::ifstream artifact( directory / relative, std::ios::binary );
        uint32_t encodedVersion = 0;
        artifact.seekg( 8 );
        artifact.read( reinterpret_cast<char*>( &encodedVersion ), sizeof( encodedVersion ) );
        if ( !artifact || encodedVersion != static_cast<uint32_t>( version ) )
        {
            return false;
        }
        std::error_code error;
        const uint64_t size = fs::file_size( directory / relative, error );
        if ( error || size > progress.availableBytes / 4 )
        {
            return false;
        }
        bytes += size * 4 + ticks * ( sizeof( ReplayPresentationSample ) + sizeof( ReplaySolverFrameSample ) +
                                      sizeof( ComparisonRecording::Observations ) );
        events += static_cast<std::size_t>( size / 64 );
        std::ifstream input( directory / ( side ? "B" : "A" ) / "physics.physicsdiag.ndjson" );
        std::string line;
        while ( std::getline( input, line ) )
        {
            if ( line.size() > 65536 || progress.cancelled.load( std::memory_order_relaxed ) )
            {
                return false;
            }
            if ( line.starts_with( "{\"kind\":\"contact\"" ) )
            {
                bytes += sizeof( ComparisonRecording::ContactSummary );
                ++events;
            }
            else if ( line.starts_with( "{\"kind\":\"solver_iteration_summary\"" ) )
            {
                bytes += sizeof( ComparisonRecording::IterationSummary );
                ++events;
            }
            if ( bytes + events * sizeof( ComparisonEvent ) > progress.availableBytes )
            {
                return false;
            }
        }
    }
    bytes += events * sizeof( ComparisonEvent );
    return bytes <= progress.availableBytes;
}
bool WriteJson( const fs::path& path, const Json& json )
{
    const fs::path temporary = path.string() + ".tmp";
    std::ofstream output( temporary, std::ios::binary | std::ios::trunc );
    output << json.dump( 2 ) << '\n';
    output.close();
    if ( !output )
    {
        return false;
    }
    std::error_code error;
    // Findings are explicit user saves; replacement never touches recordings.
    if ( fs::exists( path, error ) )
    {
        fs::remove( path, error );
    }
    if ( error )
    {
        return false;
    }
    fs::rename( temporary, path, error );
    return !error;
}
bool ValidFrames( ComparisonRecording& side, int ticks, uint64_t& bytes )
{
    if ( side.frames.size() != static_cast<std::size_t>( ticks ) )
    {
        return false;
    }
    for ( int tick = 0; tick < ticks; ++tick )
    {
        auto& frame = side.frames[static_cast<std::size_t>( tick )];
        if ( frame.sceneFrame != tick || !std::isfinite( frame.physicsDt ) ||
             ( tick && std::abs( frame.physicsDt - 1.0f / 120.0f ) > 1.0e-7f ) )
        {
            return false;
        }
        bytes += sizeof( ReplayPresentationSample ) + frame.bodies.capacity() * sizeof( ReplayBodyPresentationSample );
        if ( bytes > PhysicsComparison::MEMORY_BUDGET )
        {
            return false;
        }
        std::sort( frame.bodies.begin(), frame.bodies.end(),
                   []( const auto& a, const auto& b ) { return a.id.value < b.id.value; } );
        uint64_t previous = 0;
        for ( const auto& body : frame.bodies )
        {
            if ( !body.id.value || body.id.value == previous || !std::isfinite( body.position.x ) ||
                 !std::isfinite( body.position.y ) || !std::isfinite( body.position.z ) )
            {
                return false;
            }
            previous = body.id.value;
        }
    }
    return true;
}
Json VectorJson( const SkullbonezCore::Math::Vector::Vector3& value )
{
    return { value.x, value.y, value.z };
}
bool ReadVector( const Json& value, SkullbonezCore::Math::Vector::Vector3& out )
{
    if ( !value.is_array() || value.size() != 3 )
    {
        return false;
    }
    for ( const auto& v : value )
    {
        if ( !v.is_number() || !std::isfinite( v.get<float>() ) )
        {
            return false;
        }
    }
    out = { value[0].get<float>(), value[1].get<float>(), value[2].get<float>() };
    return true;
}
} // namespace

bool PhysicsComparison::Load( const char* bundlePath, ComparisonLoadProgress* progress )
{
    using namespace SkullbonezCore::Core::Allocation;
    RuntimeAllocationScope loading( RuntimeAllocationPhase::Capture );
    m_error.clear();
    const fs::path path( bundlePath );
    const auto manifest = ReadJson( path );
    if ( !manifest.is_object() || !ValidFields( manifest, { "format", "status" }, Json::value_t::string ) ||
         !ValidFields( manifest, { "version", "ticks" }, Json::value_t::number_integer ) ||
         manifest.value( "format", "" ) != "skullbonez.physics-comparison" || manifest.value( "version", 0 ) != 1 ||
         manifest.value( "status", "" ) != "complete" || !manifest.contains( "sides" ) || !manifest["sides"].is_array() ||
         manifest["sides"].size() != 2 || !manifest.contains( "ticks" ) || !manifest["ticks"].is_number_integer() )
    {
        m_error = "Unsupported or incomplete comparison bundle";
        return false;
    }
    const int ticks = manifest["ticks"].get<int>();
    if ( ticks < 1 || ticks > 14400 )
    {
        m_error = "Capture interval exceeds supported capacity";
        return false;
    }
    ComparisonLoadProgress localProgress;
    auto& admission = progress ? *progress : localProgress;
    if ( !VerifyInputs( manifest, path.parent_path(), admission ) )
    {
        m_error = "Archived inputs or current scene assets do not match the comparison bundle";
        return false;
    }
    uint64_t charge = 0;
    std::size_t eventCapacity = 0;
    if ( !PreflightMemory( manifest, path.parent_path(), admission, charge, eventCapacity ) )
    {
        m_error = "Comparison exceeds available loading capacity, is cancelled, or has unsupported recording metadata";
        return false;
    }
    std::array<ComparisonRecording, 2> candidate;
    uint64_t bytes = 0;
    for ( int side = 0; side < 2; ++side )
    {
        const auto& metadata = manifest["sides"][side];
        if ( !metadata.is_object() ||
             !ValidFields( metadata, { "path", "sha256", "executable", "executableSha256", "diagnosticsSha256" },
                           Json::value_t::string ) ||
             !metadata.contains( "path" ) || !metadata["path"].is_string() )
        {
            m_error = "Recording path missing";
            return false;
        }
        const fs::path relative( metadata["path"].get<std::string>() );
        if ( relative.has_parent_path() || relative.is_absolute() )
        {
            m_error = "Recording must be a file within the comparison bundle";
            return false;
        }
        const fs::path recording = path.parent_path() / relative;
        std::error_code error;
        const uint64_t fileSize = fs::file_size( recording, error );
        if ( error || fileSize > MEMORY_BUDGET / 2 )
        {
            m_error = "Recording missing or too large";
            return false;
        }
        if ( !MatchesFileHash( recording, metadata.value( "sha256", "" ) ) )
        {
            m_error = "Recording hash does not match the comparison bundle";
            return false;
        }
        if ( !ReplayV2Artifact::LoadPresentation( recording.string().c_str(), candidate[side].frames ) ||
             !ValidFrames( candidate[side], ticks, bytes ) )
        {
            m_error = "Recording contains missing ticks, invalid identities or unsupported data";
            return false;
        }
        // Sparse diagnostics are optional. A failed optional decoder must not
        // masquerade as an empty, fully observed contact stream.
        if ( !ReplayV2Artifact::LoadSolverCheckpoints( recording.string().c_str(), candidate[side].diagnostics ) )
        {
            candidate[side].diagnostics.clear();
        }
        candidate[side].executable = metadata.value( "executable", "" );
        candidate[side].executableHash = metadata.value( "executableSha256", "" );
        if ( metadata.contains( "diagnosticsSha256" ) &&
             !MatchesFileHash( path.parent_path() / ( side ? "B" : "A" ) / "physics.physicsdiag.ndjson",
                               metadata["diagnosticsSha256"].get<std::string>() ) )
        {
            m_error = "Diagnostic hash does not match the comparison bundle";
            return false;
        }
        if ( !candidate[side].LoadObservations( ( path.parent_path() / ( side ? "B" : "A" ) / "physics.physicsdiag.ndjson" )
                                                    .string()
                                                    .c_str(),
                                                ticks, bytes, progress, side ) )
        {
            m_error = "Invalid or oversized recorded diagnostic stream";
            return false;
        }
    }
    Close();
    m_recordings = std::move( candidate );
    m_bundle = fs::absolute( path ).string();
    m_scene = ( path.parent_path() / "inputs" / "scene.scene.json" ).string();
    m_tick = 0;
    m_lastTick = ticks;
    m_loopStart = 0;
    m_loopEnd = ticks;
    m_fraction = 0;
    m_memoryCharge = charge;
    m_events.reserve( eventCapacity );
    BuildEvents( progress );
    if ( progress && progress->cancelled.load( std::memory_order_relaxed ) )
    {
        m_error = "Loading cancelled";
        return false;
    }
    return true;
}

bool PhysicsComparison::SaveFinding( const char* path, const ReplayCameraSample& camera, const char* note )
{
    using namespace SkullbonezCore::Core::Allocation;
    RuntimeAllocationScope saving( RuntimeAllocationPhase::Capture );
    if ( !Active() )
    {
        return false;
    }
    const auto& s = m_settings;
    const Json json = { { "format", "skullbonez.physics-finding" },
                        { "version", 1 },
                        { "bundle", m_bundle },
                        { "builds", { m_recordings[0].executableHash, m_recordings[1].executableHash } },
                        { "tick", m_tick },
                        { "selected", m_selected },
                        { "event", m_selectedEvent },
                        { "loop", { m_loopStart, m_loopEnd, m_loop ? 1 : 0 } },
                        { "note", note ? note : "" },
                        { "camera",
                          { { "eye", VectorJson( camera.eye ) },
                            { "view", VectorJson( camera.view ) },
                            { "up", VectorJson( camera.up ) } } },
                        { "settings",
                          { { "display", static_cast<int>( s.display ) },
                            { "showA", s.showA },
                            { "stackedViews", s.stackedViews },
                            { "orbitSelected", s.orbitSelected },
                            { "angularHeatmap", s.angularHeatmap },
                            { "occludedOutline", s.occludedOutline },
                            { "followA", s.followA },
                            { "selectedOnly", s.selectedOnly },
                            { "differencesOnly", s.differencesOnly },
                            { "outlineAlpha", s.outlineAlpha },
                            { "heatScale", s.heatScale },
                            { "pixelGain", s.pixelGain },
                            { "positionThreshold", s.positionThreshold },
                            { "angleThreshold", s.angleThreshold },
                            { "impulseThreshold", s.impulseThreshold },
                            { "speed", s.speed } } } };
    return WriteJson( path, json );
}

bool PhysicsComparison::LoadFinding( const char* path, ReplayCameraSample& camera, ComparisonLoadProgress* progress )
{
    using namespace SkullbonezCore::Core::Allocation;
    RuntimeAllocationScope loading( RuntimeAllocationPhase::Capture );
    m_error = "Invalid comparison finding";
    const auto json = ReadJson( path );
    if ( !json.is_object() || !ValidFindingFields( json ) || json.value( "format", "" ) != "skullbonez.physics-finding" ||
         json.value( "version", 0 ) != 1 || !json.contains( "camera" ) || !json["camera"].is_object() ||
         !json.contains( "settings" ) || !json["settings"].is_object() || !json.contains( "bundle" ) ||
         !json["bundle"].is_string() )
    {
        return false;
    }
    ReplayCameraSample candidate;
    const auto& c = json["camera"];
    if ( !c.contains( "eye" ) || !c.contains( "view" ) || !c.contains( "up" ) || !ReadVector( c["eye"], candidate.eye ) ||
         !ReadVector( c["view"], candidate.view ) || !ReadVector( c["up"], candidate.up ) )
    {
        return false;
    }
    if ( !Load( json["bundle"].get<std::string>().c_str(), progress ) )
    {
        return false;
    }
    if ( !json.contains( "builds" ) || !json["builds"].is_array() || json["builds"].size() != 2 ||
         json["builds"][0] != m_recordings[0].executableHash || json["builds"][1] != m_recordings[1].executableHash )
    {
        m_error = "Finding build identities do not match the comparison bundle";
        return false;
    }
    const auto& s = json["settings"];
    m_settings.display = static_cast<ComparisonDisplay>( std::clamp( s.value( "display", 0 ), 0, 4 ) );
    m_settings.showA = s.value( "showA", false );
    m_settings.angularHeatmap = s.value( "angularHeatmap", false );
    m_settings.occludedOutline = s.value( "occludedOutline", false );
    m_settings.followA = s.value( "followA", false );
    m_settings.selectedOnly = s.value( "selectedOnly", true );
    m_settings.differencesOnly = s.value( "differencesOnly", true );
    m_settings.outlineAlpha = std::clamp( s.value( "outlineAlpha", 0.65f ), 0.0f, 1.0f );
    m_settings.heatScale = (std::max)( 0.000001f, s.value( "heatScale", 0.1f ) );
    m_settings.pixelGain = (std::max)( 0.0f, s.value( "pixelGain", 4.0f ) );
    m_settings.positionThreshold = (std::max)( 0.0f, s.value( "positionThreshold", 0.001f ) );
    m_settings.angleThreshold = (std::max)( 0.0f, s.value( "angleThreshold", 0.1f ) );
    m_settings.impulseThreshold = (std::max)( 0.0f, s.value( "impulseThreshold", 0.001f ) );
    m_settings.speed = std::clamp( s.value( "speed", 1.0f ), 0.05f, 8.0f );
    Seek( json.value( "tick", 0 ) );
    Select( json.value( "selected", uint64_t { 0 } ) );
    m_selectedEvent = json.value( "event", -1 );
    m_settings.stackedViews = s.value( "stackedViews", false );
    m_settings.orbitSelected = s.value( "orbitSelected", false );
    m_settings.followA = s.value( "followA", false );
    m_note = json.value( "note", "" );
    if ( json.contains( "loop" ) && json["loop"].is_array() && json["loop"].size() == 3 )
    {
        SetLoop( json["loop"][0].get<int>(), json["loop"][1].get<int>(), json["loop"][2].get<int>() != 0 );
    }
    camera = candidate;
    return true;
}

PhysicsComparisonLoadJob::~PhysicsComparisonLoadJob()
{
    Cancel();
    if ( m_worker.joinable() )
    {
        m_worker.join();
    }
}
bool PhysicsComparisonLoadJob::Start( const char* path, bool finding, uint64_t retainedBytes )
{
    using namespace SkullbonezCore::Core::Allocation;
    RuntimeAllocationScope loading( RuntimeAllocationPhase::Capture );
    if ( m_pending )
    {
        return false;
    }
    m_candidate = PhysicsComparison {};
    m_error.clear();
    m_finding = finding;
    m_success = false;
    m_progress.availableBytes = retainedBytes < PhysicsComparison::MEMORY_BUDGET
                                    ? PhysicsComparison::MEMORY_BUDGET - retainedBytes
                                    : 0;
    m_progress.percent.store( 0 );
    m_progress.cancelled.store( false );
    m_ready.store( false );
    m_pending = true;
    m_worker = std::thread(
        [this, path = std::string( path )]
        {
            m_success = m_finding ? m_candidate.LoadFinding( path.c_str(), m_camera, &m_progress )
                                  : m_candidate.Load( path.c_str(), &m_progress );
            m_ready.store( true, std::memory_order_release );
        } );
    return true;
}
bool PhysicsComparisonLoadJob::Take( PhysicsComparison& destination, ReplayCameraSample& camera )
{
    if ( !m_pending || !Ready() )
    {
        return false;
    }
    m_worker.join();
    m_pending = false;
    if ( !m_success )
    {
        m_error = m_progress.cancelled.load() ? "Loading cancelled" : m_candidate.Error();
        if ( m_error.empty() )
        {
            m_error = "Comparison could not be loaded";
        }
        m_candidate = PhysicsComparison {};
        return false;
    }
    destination = std::move( m_candidate );
    camera = m_camera;
    return true;
}
