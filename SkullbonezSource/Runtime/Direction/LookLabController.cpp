/*
File: SkullbonezSource/Runtime/Direction/LookLabController.cpp
Purpose:
  Resolves, reports, snapshots, and clears live Look Lab candidates.

Summary:
  The controller combines pure generator output with the current scene's
  non-randomizable presentation facts and retains only the validated candidate
  plus fixed-capacity status facts. App sequences its detached snapshot into
  the SceneController-owned application boundary.

Glossary:
  Scene scale: Shadow coverage distance used to proportion generator-v1 fog
    distances without borrowing physics or scene topology.
  Quality carry: Exact copy of shadow allocation/filter/bias fields that a
    live presentation reroll is forbidden to change.

Invariants:
  - Candidate resolution performs no filesystem, shader, Capture, or simulation
    operation and reads no SceneSession random state.
  - The current candidate is published only after final validity succeeds.
  - Lifecycle clearing is generation-idempotent.

Related:
  - SkullbonezSource/Runtime/Direction/LookLabController.h
  - SkullbonezSource/Runtime/Scene/SceneController.h
  - SkullbonezSource/Scene/StandaloneStyleWriter.h
  - Agentic/Reports/2026-08-01/look-lab-random-style-authoring-ll0-census.md
*/
#include "LookLabController.h"

#include "../../Scene/StandaloneStyleWriter.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace SkullbonezCore::Runtime
{
namespace
{
constexpr float GENERATOR_FOG_REFERENCE_DISTANCE = 1500.0f;

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
    PublishStatus( LookLabStatusKind::Resolved, LookLabRecipeFamilyName( m_candidate->recipe ) );
    return true;
}

void LookLabController::MarkApplied()
{

    if ( m_candidate )
    {
        PublishStatus( LookLabStatusKind::Applied, LookLabRecipeFamilyName( m_candidate->recipe ) );
    }
}

void LookLabController::ClearForSceneTransition()
{
    m_candidate.reset();
    m_status.seed = 0;
    m_status.fingerprint = 0;
    m_status.generatorVersion = 0;
    m_status.recipe = LookLabRecipeFamily::GoldenRealism;
    PublishStatus( LookLabStatusKind::ClearedForSceneLoad, "scene transition cleared Look Lab" );
}

void LookLabController::ObserveSceneLifecycle( const SceneLifecyclePacket& packet )
{

    if ( !m_sceneClearObserver.ShouldApply( packet, SceneRuntimeLifecycleEvent::AfterSceneCleared ) )
    {
        return;
    }

    ClearForSceneTransition();
}

bool LookLabController::HasCandidate() const
{
    return m_candidate.has_value();
}

const LookLabCandidate* LookLabController::CurrentCandidate() const
{
    return m_candidate ? &*m_candidate : nullptr;
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
    strncpy_s( m_status.detail.data(), m_status.detail.size(), detail ? detail : "", _TRUNCATE );
}
} // namespace SkullbonezCore::Runtime
