/*
File: SkullbonezSource/Runtime/Automation/InteractionAutomationRecorder.cpp
Purpose:
  Captures and atomically publishes deterministic interaction manifests.

Summary:
  This implementation retains raw device snapshots rather than synthesizing
  sparse actions. One pending-turn commit delays publication until the next
  scene lifecycle boundary, while SHA-256 sidecar binding and manifest-last
  replacement make every visible artifact independently replayable.

Invariants:
  - JSON numbers never carry 64-bit key masks; masks use fixed-width hex text.
  - Pointer coordinates are normalized only when a client position exists.
  - Raw camera deltas remain unscaled and therefore independent of viewport size.
  - Serialization and reserve growth run only inside explicit Diagnostics work.

Related:
  - SkullbonezSource/Runtime/Automation/InteractionAutomationRecorder.h
  - SkullbonezSource/Core/Allocation/RuntimeReserveAllocator.h
  - SkullbonezSource/Runtime/Startup/StartupLaunchResolution.cpp
*/
#include "InteractionAutomationRecorder.h"

#include "../../Core/Allocation/RuntimeAllocationTracker.h"
#include "../../Core/Allocation/RuntimeReserveAllocator.h"
#include "../../Core/PlatformWin32.h"
#include "../../Core/SbDiagnosticStore.h"

#include <algorithm>
#include <bcrypt.h>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>

#pragma comment( lib, "bcrypt.lib" )
#pragma warning( push, 0 )
#include "../../../ThirdPtySource/nlohmann/json.hpp"
#pragma warning( pop )

namespace CoreAllocation = SkullbonezCore::Core::Allocation;
using Json = nlohmann::ordered_json;
using SkullbonezCore::Runtime::InteractionAutomationRecorder;
using SkullbonezCore::Runtime::InteractionRecordingBaseline;
using SkullbonezCore::Runtime::InteractionRecordingState;
using SkullbonezCore::Runtime::InteractionRecordingStatusView;
using SkullbonezCore::Runtime::RecordedInputFrame;

namespace
{
constexpr const char* INTERACTION_RECORDING_FORMAT = "skullbonez.interaction-recording";
constexpr int INTERACTION_RECORDING_VERSION = 1;
constexpr const char* INTERACTION_RECORDING_RESERVE_OWNER = "Runtime.Automation.InteractionRecordingFrames";
constexpr int INTERACTION_RECORDING_HARD_FRAMES = static_cast<int>( InteractionAutomationRecorder::FRAMES_PER_MINUTE ) *
                                                  InteractionAutomationRecorder::MAX_RECORDING_MINUTES;

CoreAllocation::RuntimeReserveOwnerHandle InteractionRecordingReserveOwner()
{
    static const CoreAllocation::RuntimeReserveOwnerHandle owner = CoreAllocation::RuntimeReserveAllocator::RegisterOwner( { INTERACTION_RECORDING_RESERVE_OWNER, CoreAllocation::RuntimeReserveSubsystem::Diagnostics,
                                                                                                                             CoreAllocation::RuntimeReservePhase::Diagnostics, 0, INTERACTION_RECORDING_HARD_FRAMES,
                                                                                                                             CoreAllocation::RUNTIME_RESERVE_REPLAY_GROWTH_LIMIT_UNBOUNDED, false,
                                                                                                                             "F8 interaction tape grows in one-minute chunks only while explicitly recording", false,
                                                                                                                             static_cast<int>( sizeof( RecordedInputFrame ) ) } );

    return owner;
}

std::string UtcRecordingDirectoryName()
{
    const std::time_t now = std::chrono::system_clock::to_time_t( std::chrono::system_clock::now() );
    std::tm utc = {};
    gmtime_s( &utc, &now );
    char value[32] = {};
    std::strftime( value, sizeof( value ), "%Y%m%dT%H%M%SZ", &utc );
    return value;
}

std::string HexWord( uint64_t value )
{
    std::ostringstream stream;
    stream << std::hex << std::setfill( '0' ) << std::setw( 16 ) << value;
    return stream.str();
}

bool Sha256File( const std::filesystem::path& path, std::string& outDigest )
{
    std::ifstream input( path, std::ios::binary );

    if ( !input )
    {
        return false;
    }

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objectBytes = 0;
    DWORD resultBytes = 0;
    std::vector<UCHAR> object;
    std::array<UCHAR, 32> digest = {};
    bool ok = BCryptOpenAlgorithmProvider( &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0 ) >= 0;

    if ( ok )
    {
        ok = BCryptGetProperty( algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>( &objectBytes ),
                                sizeof( objectBytes ), &resultBytes, 0 ) >= 0;
    }

    if ( ok )
    {
        object.resize( objectBytes );
        ok = BCryptCreateHash( algorithm, &hash, object.data(), objectBytes, nullptr, 0, 0 ) >= 0;
    }

    std::array<char, 64 * 1024> buffer = {};

    while ( ok && input )
    {
        input.read( buffer.data(), static_cast<std::streamsize>( buffer.size() ) );
        const std::streamsize count = input.gcount();

        if ( count > 0 )
        {
            ok = BCryptHashData( hash, reinterpret_cast<PUCHAR>( buffer.data() ), static_cast<ULONG>( count ), 0 ) >= 0;
        }
    }

    if ( ok )
    {
        ok = BCryptFinishHash( hash, digest.data(), static_cast<ULONG>( digest.size() ), 0 ) >= 0;
    }

    if ( hash )
    {
        BCryptDestroyHash( hash );
    }

    if ( algorithm )
    {
        BCryptCloseAlgorithmProvider( algorithm, 0 );
    }

    if ( !ok )
    {
        return false;
    }

    std::ostringstream stream;
    stream << std::hex << std::setfill( '0' );

    for ( const UCHAR byte : digest )
    {
        stream << std::setw( 2 ) << static_cast<unsigned int>( byte );
    }

    outDigest = stream.str();
    return true;
}
} // namespace

namespace SkullbonezCore::Runtime
{
Core::SbResult InteractionAutomationRecorder::Arm( Core::SbDiagnosticStore& diagnostics, const char* requestedManifestPath,
                                                   int maximumMinutes )
{
    Reset();

    if ( maximumMinutes < 1 || maximumMinutes > MAX_RECORDING_MINUTES )
    {
        Fail( "interaction recording maximum must be between 1 and 60 minutes" );
        return diagnostics.Failure( "InteractionRecorder", "%s", m_failure.c_str() );
    }

    m_maximumMinutes = maximumMinutes;

    if ( !PreparePaths( requestedManifestPath ) )
    {
        Fail( "failed to prepare interaction recording output paths" );
        return diagnostics.Failure( "InteractionRecorder", "%s", m_failure.c_str() );
    }

    if ( !EnsureFrameCapacity( diagnostics, FRAMES_PER_MINUTE ) )
    {
        return diagnostics.Failure( "InteractionRecorder", "%s", m_failure.c_str() );
    }

    m_state = InteractionRecordingState::Armed;
    std::printf( "[recorder] Armed; baseline will be captured at the next clean frame boundary: %s\n",
                 m_manifestPath.c_str() );
    return Core::SbResult::Success();
}

Core::SbResult InteractionAutomationRecorder::BeginAtBoundary( Core::SbDiagnosticStore& diagnostics, int sourceWidth,
                                                               int sourceHeight, uint64_t sceneGeneration,
                                                               const InteractionRecordingBaseline& baseline,
                                                               bool replaySidecarWritten )
{
    if ( m_state != InteractionRecordingState::Armed )
    {
        return Core::SbResult::Success();
    }

    if ( sourceWidth <= 0 || sourceHeight <= 0 || !std::filesystem::is_regular_file( m_scenePath ) )
    {
        Fail( "scene snapshot or source viewport was unavailable at recording start" );
        return diagnostics.Failure( "InteractionRecorder", "%s", m_failure.c_str() );
    }

    m_sourceWidth = sourceWidth;
    m_sourceHeight = sourceHeight;
    m_sceneGeneration = sceneGeneration;
    m_baseline = baseline;
    m_replaySidecarWritten = replaySidecarWritten;
    m_state = InteractionRecordingState::Recording;
    std::printf( "[recorder] Recording started. scene=%s max_minutes=%d capacity=%zu\n", m_scenePath.c_str(),
                 m_maximumMinutes, m_frames.capacity() );
    return Core::SbResult::Success();
}

Core::SbResult InteractionAutomationRecorder::AdvanceBoundary( Core::SbDiagnosticStore& diagnostics,
                                                               uint64_t sceneGeneration )
{
    if ( m_state != InteractionRecordingState::Recording )
    {
        return Core::SbResult::Success();
    }

    if ( sceneGeneration != m_sceneGeneration )
    {
        // Hazard: the pending input may have requested the scene change. It is
        // deliberately discarded so the saved prefix remains single-scene.
        m_hasPendingTurn = false;
        return StopAndSave( diagnostics, "scene_transition", false );
    }

    const std::size_t maximumFrames = static_cast<std::size_t>( m_maximumMinutes ) * FRAMES_PER_MINUTE;

    if ( m_hasPendingTurn && m_frames.size() >= maximumFrames )
    {
        m_hasPendingTurn = false;
        return StopAndSave( diagnostics, "frame_capacity_limit", false );
    }

    const Core::SbResult commit = CommitPendingTurn( diagnostics );

    if ( !commit.Ok() )
    {
        return commit;
    }

    if ( m_elapsedSeconds >= static_cast<double>( m_maximumMinutes ) * 60.0 )
    {
        return StopAndSave( diagnostics, "duration_limit", false );
    }

    return Core::SbResult::Success();
}

void InteractionAutomationRecorder::CapturePendingTurn( double deltaSeconds, int sourceWidth, int sourceHeight,
                                                         const InteractionAutomationInputSample& frame,
                                                         const char* semanticAnchor )
{
    if ( m_state != InteractionRecordingState::Recording )
    {
        return;
    }

    m_pendingTurn = {};
    m_pendingTurn.turn = static_cast<uint64_t>( m_frames.size() );
    m_pendingTurn.deltaSeconds = std::clamp( deltaSeconds, 0.0, 0.05 );
    m_pendingTurn.keyWords = frame.keyWords;

    // F8 is recorder control, never replay input. Clearing the bit also covers
    // a slow release that spans the clean-boundary transition.
    const std::size_t f8Word = static_cast<std::size_t>( VK_F8 ) / 64u;
    m_pendingTurn.keyWords[f8Word] &= ~( uint64_t { 1 } << ( static_cast<unsigned int>( VK_F8 ) & 63u ) );
    m_pendingTurn.hasPointer = frame.hasClientPosition && sourceWidth > 0 && sourceHeight > 0;

    if ( m_pendingTurn.hasPointer )
    {
        const int sourceMaxX = (std::max)( 0, sourceWidth - 1 );
        const int sourceMaxY = (std::max)( 0, sourceHeight - 1 );
        m_pendingTurn.normalizedX = sourceMaxX > 0
                                        ? std::clamp( static_cast<float>( frame.clientX ) / static_cast<float>( sourceMaxX ),
                                                      0.0f, 1.0f )
                                        : 0.0f;
        m_pendingTurn.normalizedY = sourceMaxY > 0
                                        ? std::clamp( static_cast<float>( frame.clientY ) / static_cast<float>( sourceMaxY ),
                                                      0.0f, 1.0f )
                                        : 0.0f;
    }

    m_pendingTurn.rawMouseX = frame.rawMouseX;
    m_pendingTurn.rawMouseY = frame.rawMouseY;
    m_pendingTurn.wheelDelta = frame.wheelDelta;
    m_pendingTurn.appFocused = frame.appFocused;
    m_pendingTurn.leftDown = frame.leftDown;
    m_pendingTurn.rightDown = frame.rightDown;
    m_pendingTurn.middleDown = frame.middleDown;

    // An anchor refines a real pointer sample; it cannot create one. This keeps
    // failed cursor capture from becoming a deterministic but fabricated hit.
    if ( m_pendingTurn.hasPointer && semanticAnchor && semanticAnchor[0] != '\0' )
    {
        strncpy_s( m_pendingTurn.semanticAnchor, sizeof( m_pendingTurn.semanticAnchor ), semanticAnchor, _TRUNCATE );
    }

    m_hasPendingTurn = true;
}

Core::SbResult InteractionAutomationRecorder::StopAndSave( Core::SbDiagnosticStore& diagnostics, const char* reason,
                                                           bool commitPendingTurn )
{
    if ( m_state != InteractionRecordingState::Recording && m_state != InteractionRecordingState::Armed )
    {
        return m_state == InteractionRecordingState::Failed
                   ? diagnostics.Failure( "InteractionRecorder", "%s", m_failure.c_str() )
                   : Core::SbResult::Success();
    }

    if ( m_state == InteractionRecordingState::Armed )
    {
        Fail( "recording stopped before its baseline could be captured" );
        return diagnostics.Failure( "InteractionRecorder", "%s", m_failure.c_str() );
    }

    if ( commitPendingTurn )
    {
        const Core::SbResult commit = CommitPendingTurn( diagnostics );

        if ( !commit.Ok() )
        {
            return commit;
        }
    }
    else
    {
        m_hasPendingTurn = false;
    }

    m_stopReason = reason && reason[0] != '\0' ? reason : "operator";
    m_state = InteractionRecordingState::Saving;
    return SaveManifestAtomically( diagnostics );
}

void InteractionAutomationRecorder::Reset()
{
    m_state = InteractionRecordingState::Idle;
    m_baseline = {};
    std::vector<RecordedInputFrame>().swap( m_frames );
    m_pendingTurn = {};
    m_manifestPath.clear();
    m_scenePath.clear();
    m_replayPath.clear();
    m_stopReason.clear();
    m_failure.clear();
    m_sceneGeneration = 0u;
    m_elapsedSeconds = 0.0;
    m_sourceWidth = 0;
    m_sourceHeight = 0;
    m_maximumMinutes = 1;
    m_hasPendingTurn = false;
    m_replaySidecarWritten = false;
}

Core::SbResult InteractionAutomationRecorder::Abort( Core::SbDiagnosticStore& diagnostics, const char* message )
{
    Fail( message );
    return diagnostics.Failure( "InteractionRecorder", "%s", m_failure.c_str() );
}

InteractionRecordingStatusView InteractionAutomationRecorder::Status() const noexcept
{
    return { m_state,          m_elapsedSeconds,    m_maximumMinutes,
             m_frames.size(),  m_frames.capacity(), m_stopReason.c_str(),
             m_failure.c_str() };
}

bool InteractionAutomationRecorder::PreparePaths( const char* requestedManifestPath )
{
    std::error_code error;
    std::filesystem::path manifest;

    if ( requestedManifestPath && requestedManifestPath[0] != '\0' )
    {
        manifest = requestedManifestPath;
    }
    else
    {
        std::filesystem::path root = std::filesystem::path( "TestOutput" ) / "recordings" / UtcRecordingDirectoryName();
        int sequence = 0;

        while ( std::filesystem::exists( root, error ) && sequence < 1000 )
        {
            ++sequence;
            root = std::filesystem::path( "TestOutput" ) / "recordings" /
                   ( UtcRecordingDirectoryName() + "-" + std::to_string( sequence ) );
        }

        manifest = root / "interaction.json";
    }

    if ( manifest.filename().empty() )
    {
        return false;
    }

    // Invariant: a complete manifest is immutable evidence. Refusing to reuse
    // its path prevents a later capture from temporarily invalidating the
    // digests of an already published reproduction.
    if ( std::filesystem::exists( manifest, error ) || error )
    {
        return false;
    }

    const std::filesystem::path parent = manifest.has_parent_path() ? manifest.parent_path() : std::filesystem::path( "." );
    std::filesystem::create_directories( parent, error );

    if ( error )
    {
        return false;
    }

    m_manifestPath = manifest.lexically_normal().string();
    m_scenePath = ( parent / "scene.scene.json" ).lexically_normal().string();
    m_replayPath = ( parent / "replay.skreplay" ).lexically_normal().string();
    return true;
}

bool InteractionAutomationRecorder::EnsureFrameCapacity( Core::SbDiagnosticStore& diagnostics, std::size_t requiredCapacity )
{
    if ( requiredCapacity <= m_frames.capacity() )
    {
        return true;
    }

    const std::size_t maximumFrames = static_cast<std::size_t>( m_maximumMinutes ) * FRAMES_PER_MINUTE;
    const std::size_t requested = (std::min)( maximumFrames,
                                              ( ( requiredCapacity + FRAMES_PER_MINUTE - 1u ) / FRAMES_PER_MINUTE ) *
                                                  FRAMES_PER_MINUTE );

    if ( requested < requiredCapacity || requested > static_cast<std::size_t>( ( std::numeric_limits<int>::max )() ) )
    {
        Fail( "interaction recording reached its configured frame capacity" );
        return false;
    }

    const CoreAllocation::RuntimeReserveOwnerHandle owner = InteractionRecordingReserveOwner();
    const CoreAllocation::RuntimeReserveGrowthRequest request = { INTERACTION_RECORDING_RESERVE_OWNER,
                                                                  "RecordedInputFrameTape",
                                                                  CoreAllocation::RuntimeReservePhase::Diagnostics,
                                                                  static_cast<int>( m_frames.size() ),
                                                                  static_cast<int>( m_frames.capacity() ),
                                                                  static_cast<int>( requested ),
                                                                  static_cast<int>( sizeof( RecordedInputFrame ) ) };
    const CoreAllocation::RuntimeReserveGrowthResult
        growth = CoreAllocation::RuntimeReserveAllocator::RequestGrowth( owner, request );

    if ( !growth.granted )
    {
        Fail( "Diagnostics reserve denied interaction recording frame growth" );
        return false;
    }

    CoreAllocation::RuntimeAllocationScope diagnosticsScope( CoreAllocation::RuntimeAllocationPhase::Diagnostics );
    CoreAllocation::RuntimeReserveOwnerScope ownerScope( owner );
    m_frames.reserve( requested );

    if ( m_frames.capacity() < requiredCapacity )
    {
        Fail( "interaction recording frame allocation did not provide the requested capacity" );
        return false;
    }

    (void)diagnostics;
    return true;
}

Core::SbResult InteractionAutomationRecorder::CommitPendingTurn( Core::SbDiagnosticStore& diagnostics )
{
    if ( !m_hasPendingTurn )
    {
        return Core::SbResult::Success();
    }

    if ( !EnsureFrameCapacity( diagnostics, m_frames.size() + 1u ) )
    {
        return diagnostics.Failure( "InteractionRecorder", "%s", m_failure.c_str() );
    }

    m_pendingTurn.turn = static_cast<uint64_t>( m_frames.size() );
    const double durationLimit = static_cast<double>( m_maximumMinutes ) * 60.0;
    m_pendingTurn.deltaSeconds = (std::min)( m_pendingTurn.deltaSeconds,
                                             (std::max)( 0.0, durationLimit - m_elapsedSeconds ) );
    m_elapsedSeconds += m_pendingTurn.deltaSeconds;
    m_frames.push_back( m_pendingTurn );
    m_pendingTurn = {};
    m_hasPendingTurn = false;
    return Core::SbResult::Success();
}

Core::SbResult InteractionAutomationRecorder::SaveManifestAtomically( Core::SbDiagnosticStore& diagnostics )
{
    CoreAllocation::RuntimeAllocationScope diagnosticsScope( CoreAllocation::RuntimeAllocationPhase::Diagnostics );
    std::string sceneDigest;

    if ( !Sha256File( m_scenePath, sceneDigest ) )
    {
        Fail( "failed to hash the recorded scene snapshot" );
        return diagnostics.Failure( "InteractionRecorder", "%s", m_failure.c_str() );
    }

    std::string replayDigest;

    if ( m_replaySidecarWritten && !Sha256File( m_replayPath, replayDigest ) )
    {
        Fail( "failed to hash the recorded replay sidecar" );
        return diagnostics.Failure( "InteractionRecorder", "%s", m_failure.c_str() );
    }

    Json root;
    root["format"] = INTERACTION_RECORDING_FORMAT;
    root["version"] = INTERACTION_RECORDING_VERSION;
    root["complete"] = true;
    root["stopReason"] = m_stopReason;
    root["scene"] = { { "path", std::filesystem::path( m_scenePath ).filename().string() }, { "sha256", sceneDigest } };

    if ( m_replaySidecarWritten )
    {
        root["replay"] = { { "path", std::filesystem::path( m_replayPath ).filename().string() },
                           { "sha256", replayDigest } };
    }

    root["sourceViewport"] = { { "width", m_sourceWidth }, { "height", m_sourceHeight } };
    root["durationSeconds"] = m_elapsedSeconds;
    root["turnCount"] = m_frames.size();
    Json causeInspection = Json::object();
    causeInspection["mode"] = m_baseline.replayCauseInspectionMode;
    causeInspection["selectedRow"] = m_baseline.replayCauseSelectedRow;
    causeInspection["activeTab"] = m_baseline.replayCauseActiveTab;
    causeInspection["selectedDetailContactRow"] = m_baseline.replayCauseSelectedDetailContactRow;
    causeInspection["solverDetailFirstRow"] = m_baseline.replayCauseSolverDetailFirstRow;
    causeInspection["rawRecordFirstRow"] = m_baseline.replayCauseRawRecordFirstRow;
    causeInspection["iterationsFirstRow"] = m_baseline.replayCauseIterationsFirstRow;
    causeInspection["sourceFrame"] = m_baseline.replayCauseSourceFrame;
    causeInspection["targetFrame"] = m_baseline.replayCauseTargetFrame;
    causeInspection["presentedFrame"] = m_baseline.replayCausePresentedFrame;
    causeInspection["detailVisible"] = m_baseline.replayCauseDetailVisible;
    causeInspection["ownsPause"] = m_baseline.replayCauseOwnsPause;
    causeInspection["transportPending"] = m_baseline.replayCauseTransportPending;
    causeInspection["transportInFlight"] = m_baseline.replayCauseTransportInFlight;
    causeInspection["returnIssued"] = m_baseline.replayCauseReturnIssued;
    causeInspection["easedProgress"] = m_baseline.replayCauseEasedProgress;
    causeInspection["drawerProgress"] = m_baseline.replayCauseDrawerProgress;

    Json replayBaseline = { { "active", m_baseline.replayActive },
                            { "scrubPaused", m_baseline.replayScrubPaused },
                            { "liveAdvanceHeld", m_baseline.replayLiveAdvanceHeld },
                            { "predictionEnabled", m_baseline.replayPredictionEnabled },
                            { "track", m_baseline.replayTrack },
                            { "presentationTrackPosition", m_baseline.replayPresentationTrackPosition },
                            { "solverTrackPosition", m_baseline.replaySolverTrackPosition },
                            { "pathTarget", m_baseline.replayPathTargetName },
                            { "causeInspection", std::move( causeInspection ) } };
    root["baseline"] = { { "camera", { { "mode", m_baseline.cameraMode } } },
                         { "interaction", { { "worldOwner", m_baseline.worldInteractionOwner } } },
                         { "tools",
                           { { "editorMode", m_baseline.editorModeEnabled },
                             { "placementMode", m_baseline.editorPlacementModeEnabled },
                             { "placeStatic", m_baseline.editorPlaceStatic },
                             { "terrainAlign", m_baseline.editorTerrainAlign },
                             { "objectType", m_baseline.editorObjectType },
                             { "selection", m_baseline.editorSelectionName } } },
                         { "ui",
                           { { "visible", m_baseline.uiVisible },
                             { "minimized", m_baseline.uiMinimized },
                             { "activeTab", m_baseline.activeUiTab },
                             { "developmentSurface", m_baseline.developmentUiSurface } } },
                         { "replay", std::move( replayBaseline ) } };
    Json frames = Json::array();

    for ( const RecordedInputFrame& frame : m_frames )
    {
        Json words = Json::array();

        for ( const uint64_t word : frame.keyWords )
        {
            words.push_back( HexWord( word ) );
        }

        Json entry = { { "turn", frame.turn },
                       { "deltaSeconds", frame.deltaSeconds },
                       { "keys", std::move( words ) },
                       { "focused", frame.appFocused },
                       { "left", frame.leftDown },
                       { "right", frame.rightDown },
                       { "middle", frame.middleDown },
                       { "wheel", frame.wheelDelta },
                       { "rawMouse", { frame.rawMouseX, frame.rawMouseY } } };

        if ( frame.hasPointer )
        {
            entry["pointer"] = { frame.normalizedX, frame.normalizedY };
        }

        if ( frame.semanticAnchor[0] != '\0' )
        {
            entry["semanticAnchor"] = frame.semanticAnchor;
        }

        frames.push_back( std::move( entry ) );
    }

    root["frames"] = std::move( frames );
    const std::filesystem::path temporary = m_manifestPath + ".partial";
    std::ofstream output( temporary, std::ios::binary | std::ios::trunc );

    if ( !output )
    {
        Fail( "failed to open the interaction manifest partial file" );
        return diagnostics.Failure( "InteractionRecorder", "%s", m_failure.c_str() );
    }

    output << root.dump( 2 ) << '\n';
    output.flush();
    const bool writeOk = output.good();
    output.close();

    if ( !writeOk || !MoveFileExA( temporary.string().c_str(), m_manifestPath.c_str(),
                                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH ) )
    {
        std::error_code ignored;
        std::filesystem::remove( temporary, ignored );
        Fail( "failed to atomically publish the interaction manifest" );
        return diagnostics.Failure( "InteractionRecorder", "%s", m_failure.c_str() );
    }

    m_state = InteractionRecordingState::Saved;
    std::printf( "[recorder] Saved %zu turns (%.3fs) to %s reason=%s\n", m_frames.size(), m_elapsedSeconds,
                 m_manifestPath.c_str(), m_stopReason.c_str() );
    return Core::SbResult::Success();
}

void InteractionAutomationRecorder::Fail( const char* message )
{
    m_state = InteractionRecordingState::Failed;
    m_failure = message ? message : "interaction recording failed";
    std::fprintf( stderr, "[recorder] FAILED: %s\n", m_failure.c_str() );
}
} // namespace SkullbonezCore::Runtime
