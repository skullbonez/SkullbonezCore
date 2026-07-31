/*
File: SkullbonezSource/Physics/Stages/PhysicsStepDiagnostics.cpp
Purpose:
  Implements fixed-step diagnostics, bounded traces, and collision visuals.

Summary:
  This file implements collision visuals, diagnostic emission, and a pipeline
  recorder whose saturated count remains live while payload retention follows
  the active Replay, SkullScope, or presentation consumer.

Glossary:
  Suppression: Debug automation switch that temporarily disables diagnostics.
  Frame-active visual: Overlay accumulation that spans more than one tick.
  Trace cap: Fixed 4,096-record maximum retained from PhysicsWorld.
  Count-only trace: Saturated event census that retains no payload rows.

Invariants:
  - BeginStep clears one-tick outputs before any stage emits.
  - Pipeline count saturation is identical with payload retention on or off.
  - Replay capture fails loud if its step did not retain every counted payload.
  - Collision-time events remain bounded inside PhysicsDiagnosticsSink.
  - No store or diagnostics view is retained after a synchronous call.

Related:
  - SkullbonezSource/Physics/Stages/PhysicsStepDiagnostics.h
  - SkullbonezSource/Physics/PhysicsWorld.cpp
*/
#include "PhysicsStepDiagnostics.h"

#include "../../Core/FatalError.h"
#include "../../Core/SceneCapacity.h"
#include "../ColliderStore.h"
#include "../PhysicsBodyStore.h"
#include "../PhysicsDiagnosticsModel.h"
#include "../PhysicsStageCapacity.h"
#include "../PhysicsWorld.h"

#include <algorithm>

using namespace SkullbonezCore::Physics;

namespace
{
template <typename T> uint64_t ListCapacityBytes( const T& values )
{
    return static_cast<uint64_t>( values.capacity() ) * static_cast<uint64_t>( sizeof( typename T::value_type ) );
}
} // namespace

void PhysicsPipelineTraceRecorder::Reserve()
{
    m_records.Reserve( PHYSICS_MAX_PIPELINE_TRACE_RECORDS );
}

void PhysicsPipelineTraceRecorder::Record( const PhysicsPipelineRecord& record )
{

    if ( !CanRecord() )
    {
        return;
    }

    ++m_recordCount;

    if ( m_retainFullRecords )
    {
        m_records.push_back( record );
    }
}

void PhysicsPipelineTraceRecorder::RecordEvents( std::size_t eventCount )
{

    if ( m_retainFullRecords )
    {
        SB_FATAL( "Physics/PhysicsStepDiagnostics",
                  "Count-only pipeline event batches cannot be recorded while full payload retention is active." );
    }

    // Invariant: this is algebraically identical to eventCount successful
    // count-only Record attempts, including saturation, but does not require a
    // row value. Rejecting full mode keeps Records().size() == Count().
    const std::size_t remaining = static_cast<std::size_t>( RemainingRecordCapacity() );
    m_recordCount += static_cast<uint32_t>( (std::min)( eventCount, remaining ) );
}

void PhysicsPipelineTraceRecorder::RestoreFullRecords( std::span<const PhysicsPipelineRecord> records )
{

    if ( records.size() > PHYSICS_MAX_PIPELINE_TRACE_RECORDS )
    {
        SB_FATAL( "Physics/PhysicsStepDiagnostics", "Replay pipeline trace exceeds the fixed record ceiling." );
    }

    m_records.Reserve( records.size() );
    m_records.clear();

    for ( const PhysicsPipelineRecord& record : records )
    {
        m_records.push_back( record );
    }

    m_recordCount = static_cast<uint32_t>( records.size() );
    m_retainFullRecords = true;
}

bool PhysicsPipelineTraceRecorder::RetainsFullRecords() const
{
    return m_retainFullRecords;
}

bool PhysicsPipelineTraceRecorder::CanRecord() const
{
    return m_recordCount < PHYSICS_MAX_PIPELINE_TRACE_RECORDS;
}

uint32_t PhysicsPipelineTraceRecorder::Count() const
{
    return m_recordCount;
}

int PhysicsPipelineTraceRecorder::RemainingRecordCapacity() const
{
    return static_cast<int>( PHYSICS_MAX_PIPELINE_TRACE_RECORDS - m_recordCount );
}

std::span<const PhysicsPipelineRecord> PhysicsPipelineTraceRecorder::Records() const
{
    return m_records;
}

PhysicsStepDiagnostics::PhysicsStepDiagnostics() = default;

void PhysicsStepDiagnostics::ReserveSceneCapacity( std::size_t bodyCapacity )
{
    m_collisionVisualContacts.Reserve( bodyCapacity );
    m_physicsDebugContacts.Reserve( PhysicsContactRowCapacity( bodyCapacity ) );
    m_pipelineTrace.Reserve();
}

void PhysicsStepDiagnostics::Clear()
{
    m_collisionVisualContacts.clear();
    m_collisionVisualFrameActive = false;
    m_physicsDebugContacts.clear();
    m_pipelineTrace.Clear();
    m_sink.SetDiagnosticNames( {} );
}


void PhysicsStepDiagnostics::SetDiagnosticNames( std::span<const char* const> diagnosticNames )
{
    m_sink.SetDiagnosticNames( diagnosticNames );
}

void PhysicsStepDiagnostics::BeginStep( int modelCount )
{

    if ( static_cast<int>( m_collisionVisualContacts.size() ) != modelCount )
    {
        m_collisionVisualContacts.assign( modelCount, 0 );
    }

    if ( !m_collisionVisualFrameActive )
    {
        m_collisionVisualContacts.assign( modelCount, 0 );
    }

    m_physicsDebugContacts.clear();
    bool retainPipelineRecords = m_pipelineTraceFullRecordConsumerActive;
#ifdef _DEBUG
    retainPipelineRecords = retainPipelineRecords || m_sink.IsFrameLogEnabled();
#endif
    m_pipelineTrace.BeginStep( retainPipelineRecords );
    m_sink.BeginCollisionTimeFrame();
}

void PhysicsStepDiagnostics::BeginCollisionVisualFrame( int modelCount )
{
    m_collisionVisualContacts.assign( modelCount, 0 );
    m_collisionVisualFrameActive = true;
}

void PhysicsStepDiagnostics::EndCollisionVisualFrame()
{
    m_collisionVisualFrameActive = false;
}

void PhysicsStepDiagnostics::MarkCollisionVisualContact( int index )
{

    if ( index >= 0 && index < static_cast<int>( m_collisionVisualContacts.size() ) )
    {
        m_collisionVisualContacts[index] = 1;
    }
}

void PhysicsStepDiagnostics::SetPipelineTraceFullRecordConsumerActive( bool active )
{
    m_pipelineTraceFullRecordConsumerActive = active;
}

void PhysicsStepDiagnostics::RecordPipelineStage( const PhysicsPipelineRecord& record )
{
    m_pipelineTrace.Record( record );
}

void PhysicsStepDiagnostics::RecordPipelineEvents( std::size_t eventCount )
{
    m_pipelineTrace.RecordEvents( eventCount );
}

bool PhysicsStepDiagnostics::RetainsFullPipelineRecords() const
{
    return m_pipelineTrace.RetainsFullRecords();
}


int PhysicsStepDiagnostics::RemainingPipelineRecordCapacity() const
{
    return m_pipelineTrace.RemainingRecordCapacity();
}

void PhysicsStepDiagnostics::EmitCollisionTime( bool diagnosticsSuppressed, const char* type, int bodyA, int bodyB,
                                                float collisionTime, float availableTime )
{
#ifdef _DEBUG

    if ( diagnosticsSuppressed )
    {
        return;
    }
#else
    (void)diagnosticsSuppressed;
#endif
    m_sink.QueueCollisionTime( type, bodyA, bodyB, collisionTime, availableTime );
}

bool PhysicsStepDiagnostics::ShouldEmitStepDiagnostics( bool diagnosticsSuppressed ) const
{
#ifdef _DEBUG
    return !diagnosticsSuppressed && ( m_sink.IsRegressionLogEnabled() || m_sink.IsFrameLogEnabled() );
#else
    (void)diagnosticsSuppressed;
    return false;
#endif
}

void PhysicsStepDiagnostics::EmitStepDiagnostics( bool diagnosticsSuppressed, const PhysicsDiagnosticsView& diagnosticsView,
                                                  const PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore,
                                                  float deltaSeconds,
                                                  const PhysicsDiagnosticsCsvWriter& diagnosticsCsvWriter )
{
#ifdef _DEBUG

    if ( !diagnosticsSuppressed )
    {
        const bool regressionLogEnabled = m_sink.IsRegressionLogEnabled();
        const bool frameLogEnabled = m_sink.IsFrameLogEnabled();

        if ( regressionLogEnabled || frameLogEnabled )
        {
            const PhysicsDiagnosticsNameView names = m_sink.RegisteredNames();
            const PhysicsDiagnosticsFrameInput frame { diagnosticsView,      bodyStore,   colliderStore, names,
                                                       diagnosticsCsvWriter, deltaSeconds };

            if ( regressionLogEnabled )
            {
                m_sink.EmitRegressionLog( frame );
            }

            if ( frameLogEnabled )
            {
                m_sink.EmitFrame( frame );
            }
        }

        m_sink.FlushCollisionTimes( diagnosticsCsvWriter );
        m_sink.IncrementCollisionTimeFrameIfEnabled();
    }
#else
    (void)diagnosticsSuppressed;
    (void)diagnosticsView;
    (void)bodyStore;
    (void)colliderStore;
    (void)deltaSeconds;
    (void)diagnosticsCsvWriter;
#endif
}

#ifdef _DEBUG
void PhysicsStepDiagnostics::SetPhysicsRegressionLogPath( const char* path )
{
    m_sink.SetPhysicsRegressionLogPath( path );
}
void PhysicsStepDiagnostics::SetPhysicsCollisionTimeLogPath( const char* path )
{
    m_sink.SetPhysicsCollisionTimeLogPath( path );
}
void PhysicsStepDiagnostics::SetPhysicsDiagnosticsPath( const char* path )
{
    m_sink.SetPhysicsDiagnosticsPath( path );
}
void PhysicsStepDiagnostics::SetPhysicsDiagnosticsRunId( const char* runId )
{
    m_sink.SetPhysicsDiagnosticsRunId( runId );
}
#endif

void PhysicsStepDiagnostics::CaptureReplayState( PhysicsSolverSnapshot& snapshot ) const
{
    snapshot.collisionVisualContacts.clear();

    for ( uint8_t value : m_collisionVisualContacts )
    {
        snapshot.collisionVisualContacts.push_back( value );
    }

    snapshot.debugContacts.clear();

    for ( const PhysicsDebugContact& contact : m_physicsDebugContacts )
    {
        snapshot.debugContacts.push_back( contact );
    }

    if ( m_pipelineTrace.Count() != m_pipelineTrace.Records().size() )
    {
        SB_FATAL( "Physics/PhysicsStepDiagnostics",
                  "Replay capture requires full pipeline records for every counted event." );
    }

    snapshot.pipelineTrace.clear();

    for ( const PhysicsPipelineRecord& record : m_pipelineTrace.Records() )
    {
        snapshot.pipelineTrace.push_back( record );
    }

    snapshot.collisionVisualFrameActive = m_collisionVisualFrameActive;
}

void PhysicsStepDiagnostics::RestoreReplayState( const PhysicsSolverSnapshot& snapshot )
{
    m_collisionVisualContacts.Reserve( snapshot.collisionVisualContacts.size() );
    m_collisionVisualContacts.clear();

    for ( uint8_t value : snapshot.collisionVisualContacts )
    {
        m_collisionVisualContacts.push_back( value );
    }

    m_physicsDebugContacts.Reserve( snapshot.debugContacts.size() );
    m_physicsDebugContacts.clear();

    for ( const PhysicsDebugContact& contact : snapshot.debugContacts )
    {
        m_physicsDebugContacts.push_back( contact );
    }

    m_pipelineTrace.RestoreFullRecords( snapshot.pipelineTrace );

    m_collisionVisualFrameActive = snapshot.collisionVisualFrameActive;
}

std::span<const uint8_t> PhysicsStepDiagnostics::GetCollisionVisualContacts() const
{
    return m_collisionVisualContacts;
}
PhysicsContactRowList<PhysicsDebugContact>& PhysicsStepDiagnostics::MutableDebugContacts()
{
    return m_physicsDebugContacts;
}
std::span<const PhysicsDebugContact> PhysicsStepDiagnostics::GetDebugContacts() const
{
    return m_physicsDebugContacts;
}
PhysicsPipelineTraceRecorder& PhysicsStepDiagnostics::MutablePipelineTraceRecorder()
{
    return m_pipelineTrace;
}
uint32_t PhysicsStepDiagnostics::GetPipelineRecordCount() const
{
    return m_pipelineTrace.Count();
}
std::span<const PhysicsPipelineRecord> PhysicsStepDiagnostics::GetPipelineTrace() const
{
    return m_pipelineTrace.Records();
}

uint64_t PhysicsStepDiagnostics::CollectDynamicMemoryBytes() const
{
    return ListCapacityBytes( m_collisionVisualContacts ) + ListCapacityBytes( m_physicsDebugContacts ) +
           m_pipelineTrace.CollectDynamicMemoryBytes();
}

uint64_t PhysicsStepDiagnostics::CollectDebugMemoryBytes() const
{
    return CollectDynamicMemoryBytes();
}
