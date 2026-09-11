// App switches complete prediction owners; Planning owns the user's choice.
#include "ReplayRuntime.h"
#include "../Planning/PhysicsComparisonPanel.h"
#include "../../Physics/PhysicsBodyStore.h"
#include "../../Physics/PhysicsEngine.h"

namespace SkullbonezCore::Runtime
{
namespace
{
std::array<float, 4> SeedOrientation( const Math::Orientation::Quaternion& orientation )
{
    std::array<float, 4> result;
    orientation.GetComponents( result[0], result[1], result[2], result[3] );
    return result;
}

bool PredictionSeedMatches( const Physics::PhysicsEngine& physics, const ReplayPredictionIsolatedSimulation& seed )
{
    const auto& store = Physics::PhysicsEngine::ReadBodies( physics );
    const auto hot = store.HotFields();
    if ( seed.predictionBodies.size() != static_cast<std::size_t>( store.Count() ) )
    {
        return false;
    }
    for ( const auto& body : seed.predictionBodies )
    {
        const auto handle = store.HandleForSceneObjectId( body.id, body.modelRow.value );
        const auto row = store.ModelIndexForHandle( handle );
        if ( row < 0 || Math::Vector::VectorMagSquared( Physics::PhysicsBodyPosition( hot, row ) - body.position ) != 0.0f ||
             Math::Vector::VectorMagSquared( Physics::PhysicsBodyLinearVelocity( hot, row ) - body.linearVelocity ) != 0.0f ||
             Math::Vector::VectorMagSquared( Physics::PhysicsBodyAngularVelocity( hot, row ) - body.angularVelocity ) != 0.0f ||
             SeedOrientation( Physics::PhysicsBodyOrientation( hot, row ) ) != SeedOrientation( body.orientation ) )
        {
            return false;
        }
    }
    return true;
}
} // namespace

bool ReplayRuntime::VelocityComparisonActive() const noexcept
{
    return m_planningOwner.VelocityDivergence().active;
}

bool ReplayRuntime::BeginVelocityDivergence( Physics::PhysicsEngine& physics )
{
    m_scrubberOwner.SetLiveAdvanceHeld( true );
    if ( m_planningOwner.VelocityDivergence().active )
    {
        return true;
    }
    CancelUncommittedTripPlan( physics );
    if ( !Prediction().ReadyForDeterministicReveal() || Prediction().State().build.dirty || Prediction().State().build.pendingLatestRestart ||
         Prediction().State().simulation.predictionBodies.empty() )
    {
        return false;
    }

    if ( !PredictionSeedMatches( physics, Prediction().State().simulation ) )
    {
        // A prediction completed while live time advanced is not the edit's
        // stock state. Freeze time and rebuild it before allowing a mutation.
        Prediction().MarkDirty();
        return false;
    }

    // Invariant: the stock owner is never scheduled again during comparison.
    // The second owner uses the same registered, capped prediction allocator.
    const auto settings = Prediction().PresentationView();
    auto replacement = Prediction().CreateAdditionalOwner( m_resultDiagnostics );
    if ( !replacement )
    {
        return false;
    }
    ReplayPrediction& modified = *replacement;
    modified.ApplyDetailModeCommand( { settings.diagnostics.detailMode } );
    modified.SetHorizonSeconds( settings.controls.horizonSeconds );
    modified.SetRevealRatePreservingCursor( settings.controls.revealSecondsPerSecond );
    // Allocation is approved on entry; simulation waits for an edited release.
    modified.SetGenerationPermitted( false );
    modified.SetEnabled( true );
    m_bluePrediction = std::move( m_prediction );
    m_prediction = std::move( replacement );
    m_planningOwner.VelocityDivergence() = { true, false };
    m_scrubberOwner.SetLiveAdvanceHeld( true );
    m_planningOwner.CauseInspection().Reset();
    return true;
}

namespace
{
bool RestorePredictionSeed( Physics::PhysicsEngine& physics, const ReplayPredictionIsolatedSimulation& seed )
{
    const auto& store = Physics::PhysicsEngine::ReadBodies( physics );
    const auto count = Physics::MakePhysicsBodyCountFromNonNegativeInt( store.Count() );
    if ( seed.predictionBodies.size() != static_cast<std::size_t>( store.Count() ) || !physics.CanRestoreReplaySolverSnapshot( seed.predictionWorld.physics, count ) )
    {
        return false;
    }
    // Validate every identity before the first write, including unchanged bodies.
    for ( const auto& body : seed.predictionBodies )
    {
        const auto* record = store.RecordForHandle( store.HandleForModelIndex( body.modelRow.value ) );
        if ( !record || record->sceneObjectId != body.id )
        {
            return false;
        }
    }
    for ( const auto& body : seed.predictionBodies )
    {
        const Physics::PhysicsBodyRestoreState restore { store.HandleForModelIndex( body.modelRow.value ),
                                                         body.id,
                                                         body.fixed,
                                                         body.position,
                                                         body.orientation,
                                                         body.linearVelocity,
                                                         body.angularVelocity,
                                                         body.mass,
                                                         body.inverseMass,
                                                         body.rotationalInertia,
                                                         body.inverseRotationalInertia };
        if ( !physics.RestoreReplayBodyState( restore ) )
        {
            return false;
        }
    }
    return physics.RestoreReplaySolverSnapshot( seed.predictionWorld.physics, count );
}
} // namespace

bool ReplayRuntime::AcceptVelocityDivergence( Physics::PhysicsEngine& physics, bool acceptRed )
{
    const auto choice = m_planningOwner.VelocityDivergence();
    if ( !choice.active || ( acceptRed && !choice.redReady ) )
    {
        return false;
    }
    if ( !acceptRed )
    {
        if ( !RestorePredictionSeed( physics, m_bluePrediction->State().simulation ) )
        {
            return false;
        }
        m_prediction.swap( m_bluePrediction );
    }
    m_bluePrediction.reset();
    m_planningOwner.VelocityDivergence() = {};
    m_authoring.ResetVelocityEdit();
    (void)m_authoring.TakePredictionRequest();
    m_planningOwner.CauseInspection().Reset();
    m_scrubberOwner.SetLiveAdvanceHeld( true );
    m_scrubberOwner.SetTrackPosition( RunReplayTrack::Solver, SolverPresentTrackPosition() );
    return true;
}

bool ReplayRuntime::OpenVelocityComparison( PhysicsComparison& comparison )
{
    if ( !m_bluePrediction || !m_planningOwner.VelocityDivergence().redReady )
    {
        return false;
    }
    m_planningOwner.VelocityDivergence().playing = false;
    return comparison.LoadPredictionFrames( m_bluePrediction->ActiveFrames(), Prediction().ActiveFrames() );
}

bool ReplayRuntime::PlaybackPaused() const noexcept
{
    const auto& divergence = m_planningOwner.VelocityDivergence();
    return divergence.active ? !divergence.playing : m_scrubberOwner.LiveAdvanceHeld();
}

void ReplayRuntime::TickVelocityDivergencePlayback( RuntimeInteractionController& interaction, double now, ReplayWorkspaceOutput& output )
{
    auto& divergence = m_planningOwner.VelocityDivergence();
    if ( !divergence.active || !divergence.playing || !divergence.redReady )
    {
        return;
    }
    const auto frames = Prediction().ActiveFrames();
    if ( frames.size() < 2u )
    {
        return;
    }
    const double elapsed = (std::max)( 0.0, now - divergence.playbackTime );
    divergence.playbackTime = now;
    const double duration = frames.back().simulationSeconds - frames.front().simulationSeconds;
    const float present = SolverPresentTrackPosition();
    const float position = (std::max)( present, m_scrubberOwner.TrackPosition( RunReplayTrack::Solver ) );
    const float next = duration > 0.0 ? (std::min)( 1.0f, position + static_cast<float>( elapsed / duration ) * ( 1.0f - present ) ) : 1.0f;
    (void)SetTransportCursor( next, interaction, now, output );
    divergence.playing = next < 1.0f;
}

void ReplayRuntime::ClearVelocityDivergence()
{
    if ( m_bluePrediction )
    {
        // Scene replacement also ends an edit that has never been released.
        // Restore the stock production policy before reusing the red owner.
        Prediction().SetGenerationPermitted( m_bluePrediction->GenerationPermitted() );
    }
    m_bluePrediction.reset();
    m_planningOwner.VelocityDivergence() = {};
}
} // namespace SkullbonezCore::Runtime
