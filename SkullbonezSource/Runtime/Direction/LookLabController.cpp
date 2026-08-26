/*
File: SkullbonezSource/Runtime/Direction/LookLabController.cpp
Purpose:
  Owns live Look Lab candidates and their save transaction lifecycle.

Summary:
  The controller combines pure generator output with the current scene's
  non-randomizable presentation facts and retains only the validated candidate
  plus fixed-capacity status facts. During a save it retains the exact snapshot,
  artifact facts, and capture token until App returns Capture's completion.

Glossary:
  Scene scale: Shadow coverage distance used to proportion generator-v1 fog
    distances without borrowing physics or scene topology.
  Shadow policy carry: Exact copy of shadow allocation/filter/bias and
    cast/receive participation fields that a live presentation reroll is
    forbidden to change.
  Pending bundle: Style and first receipt are durable, while look.png still
    awaits the post-render Capture owner.

Invariants:
  - Candidate resolution performs no filesystem, shader, Capture, or simulation
    operation and reads no SceneSession random state.
  - The current candidate is published only after final validity succeeds.
  - A pending bundle blocks reroll and duplicate save so one accepted action
    produces one directory containing one internally consistent candidate.
  - Receipt metadata is fully validated before bundle publication receives a
    directory-creation request.
  - App explicitly clears before every scene replacement; clearing drops all
    scene-local candidate and pending-save state without polling a frame packet.

Related:
  - SkullbonezSource/Runtime/Direction/LookLabController.h
  - SkullbonezSource/Runtime/Scene/SceneController.h
  - SkullbonezSource/Scene/StandaloneStyleWriter.h
*/
#include "LookLabController.h"

#include "../../Core/SbDiagnosticStore.h"
#include "../../Scene/StandaloneStyleWriter.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace SkullbonezCore::Runtime
{
namespace
{
constexpr float GENERATOR_FOG_REFERENCE_DISTANCE = 1500.0f;
constexpr const char* OWNER = "Runtime/Direction/LookLabController";

class FilesystemLookLabBundlePublication final : public LookLabBundlePublication
{
  public:
    Core::SbResult CreateBundleDirectory( Core::SbDiagnosticStore& diagnostics, const LookLabSaveRequest& request,
                                          uint64_t seed, LookLabBundlePaths& output ) override
    {
        return LookLabBundleWriter::CreateBundleDirectory( diagnostics, request.lookLabRoot, request.localTimestamp, seed,
                                                           output );
    }

    Core::SbResult SaveStyleAtomic( Core::SbDiagnosticStore& diagnostics, const Scene::StandaloneStyleSnapshot& snapshot,
                                    const LookLabBundlePaths& paths ) override
    {
        return Scene::StandaloneStyleWriter::SaveAtomic( diagnostics, snapshot, paths.style.data() );
    }

    Core::SbResult SaveReceiptAtomic( Core::SbDiagnosticStore& diagnostics, const LookLabReceiptFacts& facts,
                                      const Scene::StandaloneStyleSnapshot& snapshot,
                                      const LookLabBundlePaths& paths ) override
    {
        return LookLabBundleWriter::SaveReceiptAtomic( diagnostics, facts, snapshot, paths );
    }
};

LookLabBundlePublication& DefaultBundlePublication()
{
    static FilesystemLookLabBundlePublication publication;
    return publication;
}

template <std::size_t Capacity> void CopyBounded( std::array<char, Capacity>& output, const char* value )
{
    strncpy_s( output.data(), output.size(), value ? value : "", _TRUNCATE );
}

template <std::size_t Capacity> bool RequestTextFits( const char* value, bool allowEmpty = true )
{
    if ( !value || ( !allowEmpty && value[0] == '\0' ) )
    {
        return false;
    }

    const std::size_t length = std::strlen( value );

    if ( length >= Capacity )
    {
        return false;
    }

    return std::strchr( value, '\r' ) == nullptr && std::strchr( value, '\n' ) == nullptr;
}

uint64_t MixAuthoringSeed( uint64_t value )
{
    value += 0x9e3779b97f4a7c15ull;
    value = ( value ^ ( value >> 30u ) ) * 0xbf58476d1ce4e5b9ull;
    value = ( value ^ ( value >> 27u ) ) * 0x94d049bb133111ebull;
    return value ^ ( value >> 31u );
}

void ResolveRetainedPresentationValues( LookLabCandidate& candidate, const Core::CinematicRenderConfig& activePresentation )
{
    Core::CinematicRenderConfig& resolved = candidate.cinematic;

    // Invariant: basin coordinates are presentation geometry in scene units.
    // Changing them would move water/terrain features relative to authored bodies.
    resolved.basinCenterX = activePresentation.basinCenterX;
    resolved.basinCenterZ = activePresentation.basinCenterZ;
    resolved.basinRadiusX = activePresentation.basinRadiusX;
    resolved.basinRadiusZ = activePresentation.basinRadiusZ;
    resolved.basinFeather = activePresentation.basinFeather;

    // Why: water mode zero means this scene deliberately has no water surface.
    // A look reroll may restyle existing water but may not add scene content.
    if ( activePresentation.waterMode == Core::CinematicStyleMode::Water::Off )
    {
        resolved.waterMode = Core::CinematicStyleMode::Water::Off;
    }

    // Invariant: generator-v1 distances are normalized around the renderer's
    // default shadow coverage. Scaling by the retained coverage keeps fog useful
    // in small and large scenes without touching simulation units or transforms.
    const float sceneScale = std::clamp( activePresentation.shadow.maxDistance / GENERATOR_FOG_REFERENCE_DISTANCE, 0.25f,
                                         4.0f );

    resolved.fogStart = std::clamp( resolved.fogStart * sceneScale, 0.0f, 10000.0f );
    const float minimumFogGap = std::max( 32.0f, 0.15f * resolved.fogStart );
    resolved.fogEnd = std::clamp( resolved.fogEnd * sceneScale, resolved.fogStart + minimumFogGap, 20000.0f );

    // Quality carry: visibility remains randomized, but allocation, filtering,
    // geometry participation, bias, and coverage stay with the active owner.
    resolved.shadow.terrainCasts = activePresentation.shadow.terrainCasts;
    resolved.shadow.objectsCast = activePresentation.shadow.objectsCast;
    resolved.shadow.terrainReceives = activePresentation.shadow.terrainReceives;
    resolved.shadow.objectsReceive = activePresentation.shadow.objectsReceive;
    resolved.shadow.mapSize = activePresentation.shadow.mapSize;
    resolved.shadow.pcfRadius = activePresentation.shadow.pcfRadius;
    resolved.shadow.depthBias = activePresentation.shadow.depthBias;
    resolved.shadow.slopeBias = activePresentation.shadow.slopeBias;
    resolved.shadow.maxDistance = activePresentation.shadow.maxDistance;
}
} // namespace

Core::SbResult ValidateLookLabSaveRequest( Core::SbDiagnosticStore& diagnostics, const LookLabSaveRequest& request,
                                           uint64_t seed )
{
    if ( !request.lookLabRoot || request.lookLabRoot[0] == '\0' ||
         !RequestTextFits<LookLabReceiptFacts::LOCAL_TIMESTAMP_CAPACITY>( request.localTimestamp, false ) ||
         !IsValidLookLabLocalTimestamp( request.localTimestamp ) ||
         !RequestTextFits<LookLabReceiptFacts::SOURCE_SCENE_PATH_CAPACITY>( request.sourceScenePath ) ||
         !RequestTextFits<LookLabReceiptFacts::SOURCE_SCENE_DISPLAY_NAME_CAPACITY>( request.sourceSceneDisplayName ) )
    {
        return diagnostics.Failure( OWNER, "save request contains invalid or unbounded receipt metadata" );
    }

    if ( request.utcOffsetMinutes < -14 * 60 || request.utcOffsetMinutes > 14 * 60 )
    {
        return diagnostics.Failure( OWNER, "save request UTC offset must be within -14:00..+14:00" );
    }

    LookLabBundlePaths preflightPaths;
    return LookLabBundleWriter::ResolveBundlePaths( diagnostics, request.lookLabRoot, request.localTimestamp, seed,
                                                     preflightPaths );
}

LookLabController::LookLabController() : m_publication( &DefaultBundlePublication() )
{
}

LookLabController::LookLabController( LookLabBundlePublication& publication ) : m_publication( &publication )
{
}

LookLabCandidate ResolveLookLabCandidateForScene( uint64_t seed, const Core::CinematicRenderConfig& activePresentation )
{
    LookLabCandidate candidate = GenerateLookLabCandidate( seed );
    ResolveRetainedPresentationValues( candidate, activePresentation );
    return candidate;
}

LookLabCandidateIssue ValidateResolvedLookLabCandidate( const LookLabCandidate& candidate,
                                                        const Core::CinematicRenderConfig& activePresentation )
{
    const Core::CinematicRenderConfig& resolved = candidate.cinematic;
    const float retainedFloats[] = { activePresentation.shadow.depthBias,   activePresentation.shadow.slopeBias,
                                     activePresentation.shadow.maxDistance, activePresentation.basinCenterX,
                                     activePresentation.basinCenterZ,       activePresentation.basinRadiusX,
                                     activePresentation.basinRadiusZ,       activePresentation.basinFeather };

    for ( float value : retainedFloats )
    {
        if ( !std::isfinite( value ) )
        {
            return LookLabCandidateIssue::NonFiniteValue;
        }
    }

    if ( activePresentation.shadow.mapSize < 256 || activePresentation.shadow.mapSize > 8192 ||
         activePresentation.shadow.pcfRadius < 0 || activePresentation.shadow.pcfRadius > 3 ||
         activePresentation.shadow.depthBias < 0.0f || activePresentation.shadow.depthBias > 0.05f ||
         activePresentation.shadow.slopeBias < 0.0f || activePresentation.shadow.slopeBias > 0.05f ||
         activePresentation.shadow.maxDistance < 128.0f || activePresentation.shadow.maxDistance > 10000.0f )
    {
        return LookLabCandidateIssue::ValueOutOfRange;
    }

    if ( resolved.shadow.terrainCasts != activePresentation.shadow.terrainCasts ||
         resolved.shadow.objectsCast != activePresentation.shadow.objectsCast ||
         resolved.shadow.terrainReceives != activePresentation.shadow.terrainReceives ||
         resolved.shadow.objectsReceive != activePresentation.shadow.objectsReceive ||
         resolved.shadow.mapSize != activePresentation.shadow.mapSize ||
         resolved.shadow.pcfRadius != activePresentation.shadow.pcfRadius ||
         resolved.shadow.depthBias != activePresentation.shadow.depthBias ||
         resolved.shadow.slopeBias != activePresentation.shadow.slopeBias ||
         resolved.shadow.maxDistance != activePresentation.shadow.maxDistance ||
         resolved.basinCenterX != activePresentation.basinCenterX ||
         resolved.basinCenterZ != activePresentation.basinCenterZ ||
         resolved.basinRadiusX != activePresentation.basinRadiusX ||
         resolved.basinRadiusZ != activePresentation.basinRadiusZ ||
         resolved.basinFeather != activePresentation.basinFeather ||
         ( activePresentation.waterMode == Core::CinematicStyleMode::Water::Off &&
           resolved.waterMode != Core::CinematicStyleMode::Water::Off ) )
    {
        return LookLabCandidateIssue::IncompatibleFeatures;
    }

    // The pure validator intentionally pins generator-v1's retained placeholders.
    // Normalize only those fields to prove the randomized contract, while the
    // exact comparisons above separately prove scene resolution.
    LookLabCandidate normalized = candidate;
    const Core::CinematicRenderConfig generatorDefaults;
    normalized.cinematic.shadow.terrainCasts = generatorDefaults.shadow.terrainCasts;
    normalized.cinematic.shadow.objectsCast = generatorDefaults.shadow.objectsCast;
    normalized.cinematic.shadow.terrainReceives = generatorDefaults.shadow.terrainReceives;
    normalized.cinematic.shadow.objectsReceive = generatorDefaults.shadow.objectsReceive;
    normalized.cinematic.shadow.mapSize = generatorDefaults.shadow.mapSize;
    normalized.cinematic.shadow.pcfRadius = generatorDefaults.shadow.pcfRadius;
    normalized.cinematic.shadow.depthBias = generatorDefaults.shadow.depthBias;
    normalized.cinematic.shadow.slopeBias = generatorDefaults.shadow.slopeBias;
    normalized.cinematic.shadow.maxDistance = generatorDefaults.shadow.maxDistance;
    normalized.cinematic.basinCenterX = generatorDefaults.basinCenterX;
    normalized.cinematic.basinCenterZ = generatorDefaults.basinCenterZ;
    normalized.cinematic.basinRadiusX = generatorDefaults.basinRadiusX;
    normalized.cinematic.basinRadiusZ = generatorDefaults.basinRadiusZ;
    normalized.cinematic.basinFeather = generatorDefaults.basinFeather;
    return ValidateLookLabCandidate( normalized );
}

Scene::StandaloneStyleSnapshot BuildLookLabStyleSnapshot( const LookLabCandidate& candidate )
{
    Scene::StandaloneStyleSnapshot snapshot;
    snapshot.cinematic = candidate.cinematic;
    snapshot.materialRules.reserve( candidate.materialRules.size() );

    for ( const LookLabMaterialRule& candidateRule : candidate.materialRules )
    {
        Scene::StandaloneStyleMaterialRule rule;
        strncpy_s( rule.target.data(), rule.target.size(), candidateRule.target.data(), _TRUNCATE );
        rule.material = candidateRule.material;
        snapshot.materialRules.push_back( rule );
    }

    return snapshot;
}

bool LookLabController::ResolveSeed( uint64_t seed, const Core::CinematicRenderConfig& activePresentation )
{
    if ( m_pendingSave )
    {
        PublishStatus( LookLabStatusKind::Rejected, "save pending; reroll ignored" );
        return false;
    }

    LookLabCandidate candidate = ResolveLookLabCandidateForScene( seed, activePresentation );

    if ( ValidateResolvedLookLabCandidate( candidate, activePresentation ) != LookLabCandidateIssue::None )
    {
        PublishStatus( LookLabStatusKind::Rejected, "candidate failed final validation" );
        return false;
    }

    m_candidate = std::move( candidate );
    m_status.seed = m_candidate->seed;
    m_status.generatorVersion = m_candidate->generatorVersion;
    m_status.recipe = m_candidate->recipe;
    m_status.fingerprint = FingerprintLookLabCandidate( *m_candidate );
    PublishBundlePath( "" );
    PublishStatus( LookLabStatusKind::Resolved, LookLabRecipeFamilyName( m_candidate->recipe ) );
    return true;
}

uint64_t LookLabController::NextAuthoringSeed()
{
    const uint64_t clockBits = static_cast<uint64_t>( std::chrono::high_resolution_clock::now().time_since_epoch().count() );
    const uint64_t sequence = ++m_authoringSequence;
    const uint64_t current = m_candidate ? m_candidate->seed : 0;
    uint64_t seed = MixAuthoringSeed( clockBits ^ ( sequence * 0x9e3779b97f4a7c15ull ) ^ current );

    // Invariant: even a coarse or frozen host clock cannot repeat the currently
    // visible candidate because the private authoring sequence still advances.
    if ( m_candidate && seed == m_candidate->seed )
    {
        seed = m_candidate->seed + 1u;
    }

    return seed;
}

void LookLabController::MarkApplied()
{
    if ( m_candidate )
    {
        PublishStatus( LookLabStatusKind::Applied, LookLabRecipeFamilyName( m_candidate->recipe ) );
    }
}

LookLabSaveStartResult LookLabController::BeginSave( Core::SbDiagnosticStore& diagnostics,
                                                     const LookLabSaveRequest& request )
{
    LookLabSaveStartResult output;

    if ( !m_candidate )
    {
        output.status = diagnostics.Failure( OWNER, "save requires a resolved Look Lab candidate" );
        PublishStatus( LookLabStatusKind::Rejected, output.status.ErrorMessage() );
        return output;
    }

    if ( m_pendingSave )
    {
        output.status = diagnostics.Failure( OWNER, "one Look Lab bundle is already pending capture" );
        PublishStatus( LookLabStatusKind::Rejected, output.status.ErrorMessage() );
        return output;
    }

    Core::SbResult requestResult = ValidateLookLabSaveRequest( diagnostics, request, m_candidate->seed );

    if ( !requestResult.Ok() )
    {
        PublishStatus( LookLabStatusKind::Rejected, requestResult.ErrorMessage() );
        output.status = std::move( requestResult );
        return output;
    }

    PendingSave pending;
    PublishBundlePath( "" );
    pending.snapshot = BuildLookLabStyleSnapshot( *m_candidate );
    pending.facts.seed = m_candidate->seed;
    pending.facts.generatorVersion = m_candidate->generatorVersion;
    pending.facts.recipe = m_candidate->recipe;
    pending.facts.utcOffsetMinutes = request.utcOffsetMinutes;
    CopyBounded( pending.facts.localTimestamp, request.localTimestamp );
    CopyBounded( pending.facts.sourceScenePath, request.sourceScenePath );
    CopyBounded( pending.facts.sourceSceneDisplayName, request.sourceSceneDisplayName );

    Core::SbResult directoryResult = m_publication->CreateBundleDirectory( diagnostics, request, m_candidate->seed,
                                                                           pending.paths );

    if ( !directoryResult.Ok() )
    {
        PublishStatus( LookLabStatusKind::Rejected, directoryResult.ErrorMessage() );
        output.status = std::move( directoryResult );
        return output;
    }

    PublishBundlePath( pending.paths.directory.data() );
    Core::SbResult styleResult = m_publication->SaveStyleAtomic( diagnostics, pending.snapshot, pending.paths );

    if ( !styleResult.Ok() )
    {
        pending.facts.styleStatus = LookLabArtifactStatus::Failed;
        pending.facts.screenshotStatus = LookLabArtifactStatus::Cancelled;
        CopyBounded( pending.facts.styleDiagnostic, styleResult.ErrorMessage() );
        CopyBounded( pending.facts.screenshotDiagnostic, "capture not requested because style publication failed" );
        const Core::SbResult receiptResult = m_publication->SaveReceiptAtomic( diagnostics, pending.facts, pending.snapshot,
                                                                               pending.paths );

        PublishStatus( LookLabStatusKind::BundlePartialFailure,
                       receiptResult.Ok() ? styleResult.ErrorMessage() : receiptResult.ErrorMessage() );

        output.status = receiptResult.Ok() ? std::move( styleResult ) : receiptResult;
        return output;
    }

    pending.facts.styleStatus = LookLabArtifactStatus::Saved;
    pending.facts.screenshotStatus = LookLabArtifactStatus::Pending;
    Core::SbResult receiptResult = m_publication->SaveReceiptAtomic( diagnostics, pending.facts, pending.snapshot,
                                                                     pending.paths );

    if ( !receiptResult.Ok() )
    {
        PublishStatus( LookLabStatusKind::BundlePartialFailure, receiptResult.ErrorMessage() );
        output.status = std::move( receiptResult );
        return output;
    }

    pending.token = m_nextSaveToken++;

    if ( m_nextSaveToken == 0 )
    {
        m_nextSaveToken = 1;
    }

    output.captureRequested = true;
    output.captureToken = pending.token;
    output.screenshotPath = pending.paths.screenshot;
    m_pendingSave = std::move( pending );
    PublishStatus( LookLabStatusKind::BundlePending, "style saved; screenshot pending" );
    return output;
}

Core::SbResult LookLabController::CompleteSaveCapture( Core::SbDiagnosticStore& diagnostics, uint64_t token,
                                                       const Core::SbResult& captureResult )
{
    if ( !m_pendingSave || token == 0 || m_pendingSave->token != token )
    {
        return diagnostics.Failure( OWNER, "capture completion token does not match the pending Look Lab bundle" );
    }

    PendingSave pending = std::move( *m_pendingSave );
    m_pendingSave.reset();
    pending.facts.screenshotStatus = captureResult.Ok() ? LookLabArtifactStatus::Saved : LookLabArtifactStatus::Failed;

    if ( !captureResult.Ok() )
    {
        CopyBounded( pending.facts.screenshotDiagnostic, captureResult.ErrorMessage() );
    }

    Core::SbResult receiptResult = m_publication->SaveReceiptAtomic( diagnostics, pending.facts, pending.snapshot,
                                                                     pending.paths );

    if ( !receiptResult.Ok() )
    {
        PublishStatus( LookLabStatusKind::BundlePartialFailure, receiptResult.ErrorMessage() );
        return receiptResult;
    }

    PublishStatus( captureResult.Ok() ? LookLabStatusKind::BundleSaved : LookLabStatusKind::BundlePartialFailure,
                   captureResult.Ok() ? "Look Lab bundle saved" : captureResult.ErrorMessage() );

    return captureResult;
}

Core::SbResult LookLabController::CancelPendingSave( Core::SbDiagnosticStore& diagnostics, const char* reason )
{
    if ( !m_pendingSave )
    {
        return Core::SbResult::Success();
    }

    PendingSave pending = std::move( *m_pendingSave );
    m_pendingSave.reset();
    pending.facts.screenshotStatus = LookLabArtifactStatus::Cancelled;
    CopyBounded( pending.facts.screenshotDiagnostic, reason ? reason : "Look Lab save cancelled" );
    Core::SbResult receiptResult = m_publication->SaveReceiptAtomic( diagnostics, pending.facts, pending.snapshot,
                                                                     pending.paths );

    PublishStatus( receiptResult.Ok() ? LookLabStatusKind::BundleCancelled : LookLabStatusKind::BundlePartialFailure,
                   receiptResult.Ok() ? pending.facts.screenshotDiagnostic.data() : receiptResult.ErrorMessage() );

    return receiptResult;
}

void LookLabController::ClearForSceneTransition()
{
    m_candidate.reset();
    m_status.seed = 0;
    m_status.fingerprint = 0;
    m_status.generatorVersion = 0;
    m_status.recipe = LookLabRecipeFamily::GoldenRealism;
    PublishBundlePath( "" );
    PublishStatus( LookLabStatusKind::ClearedForSceneLoad, "scene transition cleared Look Lab" );
}

bool LookLabController::HasCandidate() const
{
    return m_candidate.has_value();
}

bool LookLabController::HasPendingSave() const
{
    return m_pendingSave.has_value();
}

uint64_t LookLabController::PendingSaveToken() const
{
    return m_pendingSave ? m_pendingSave->token : 0;
}

Scene::StandaloneStyleSnapshot LookLabController::BuildCurrentSnapshot() const
{
    return m_candidate ? BuildLookLabStyleSnapshot( *m_candidate ) : Scene::StandaloneStyleSnapshot {};
}

LookLabStatusView LookLabController::Status() const
{
    return m_status;
}

void LookLabController::PublishStatus( LookLabStatusKind kind, const char* detail )
{
    m_status.kind = kind;
    m_status.hasCandidate = m_candidate.has_value();
    m_status.savePending = m_pendingSave.has_value();
    strncpy_s( m_status.detail.data(), m_status.detail.size(), detail ? detail : "", _TRUNCATE );
}

void LookLabController::PublishBundlePath( const char* path )
{
    CopyBounded( m_status.bundleDirectory, path );
}
} // namespace SkullbonezCore::Runtime
