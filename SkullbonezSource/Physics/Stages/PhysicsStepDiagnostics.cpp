/*
File: SkullbonezSource/Physics/Stages/PhysicsStepDiagnostics.cpp
Purpose:
  Implements fixed-step diagnostics, bounded traces, and collision visuals.

Summary:
  This file mechanically re-homes the former PhysicsWorld diagnostic storage
  and helpers. Output ordering, caps, and Debug/Profile conditional behavior
  remain unchanged.

Glossary:
  Suppression: Debug automation switch that temporarily disables diagnostics.
  Frame-active visual: Overlay accumulation that spans more than one tick.
  Trace cap: Fixed 4,096-record maximum retained from PhysicsWorld.

Invariants:
  - BeginStep clears one-tick outputs before any stage emits.
  - Collision-time events remain bounded inside PhysicsDiagnosticsSink.
  - No store or diagnostics view is retained after a synchronous call.

Related:
  - SkullbonezSource/Physics/Stages/PhysicsStepDiagnostics.h
  - SkullbonezSource/Physics/PhysicsWorld.cpp
*/
#include "PhysicsStepDiagnostics.h"

#include "../../Runtime/Scene/SceneCapacity.h"
#include "../ColliderStore.h"
#include "../PhysicsBodyStore.h"
#include "../PhysicsDiagnosticsModel.h"
#include "../PhysicsWorld.h"

#include <algorithm>

using namespace SkullbonezCore::Physics;

namespace
{
template <typename T> uint64_t VectorCapacityBytes( const std::vector<T>& values )
{
    return static_cast<uint64_t>( values.capacity() ) * static_cast<uint64_t>( sizeof( T ) );
}
} // namespace

PhysicsStepDiagnostics::PhysicsStepDiagnostics()
{
    m_collisionVisualContacts.reserve( SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS );
    m_physicsDebugContacts.reserve( SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS * 4 );
    m_physicsPipelineTrace.reserve( MAX_PIPELINE_TRACE_RECORDS );
}

void PhysicsStepDiagnostics::Clear()
{
    m_collisionVisualContacts.clear();
    m_collisionVisualFrameActive = false;
    m_physicsDebugContacts.clear();
    m_physicsPipelineTrace.clear();
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
    m_physicsPipelineTrace.clear();
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

void PhysicsStepDiagnostics::RecordPipelineStage( const PhysicsPipelineRecord& record )
{
    if ( m_physicsPipelineTrace.size() < MAX_PIPELINE_TRACE_RECORDS )
    {
        m_physicsPipelineTrace.push_back( record );
    }
}

bool PhysicsStepDiagnostics::CanRecordPipelineStage() const
{
    return m_physicsPipelineTrace.size() < MAX_PIPELINE_TRACE_RECORDS;
}

int PhysicsStepDiagnostics::RemainingPipelineRecordCapacity() const
{
    return (std::max)( 0,
                       static_cast<int>( MAX_PIPELINE_TRACE_RECORDS ) -
                           static_cast<int>( m_physicsPipelineTrace.size() ) );
}

void PhysicsStepDiagnostics::EmitCollisionTime( bool diagnosticsSuppressed,
                                                const char* type,
                                                int bodyA,
                                                int bodyB,
                                                float collisionTime,
                                                float availableTime )
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

bool PhysicsStepDiagnostics::ShouldEmitCollisionTimeDiagnostics( bool diagnosticsSuppressed ) const
{
#ifdef _DEBUG
    return !diagnosticsSuppressed && m_sink.IsCollisionTimeLogEnabled();
#else
    (void)diagnosticsSuppressed;
    return false;
#endif
}

void PhysicsStepDiagnostics::EmitStepDiagnostics( bool diagnosticsSuppressed,
                                                  const PhysicsDiagnosticsView& diagnosticsView,
                                                  const PhysicsBodyStore& bodyStore,
                                                  const ColliderStore& colliderStore,
                                                  float deltaSeconds,
                                                  const char* const* diagnosticNames,
                                                  int diagnosticNameCount,
                                                  const PhysicsDiagnosticsCsvWriter& diagnosticsCsvWriter )
{
#ifdef _DEBUG
    if ( !diagnosticsSuppressed )
    {
        const bool regressionLogEnabled = m_sink.IsRegressionLogEnabled();
        const bool frameLogEnabled = m_sink.IsFrameLogEnabled();
        if ( regressionLogEnabled || frameLogEnabled )
        {
            const PhysicsDiagnosticsNameView names{ diagnosticNames, diagnosticNameCount };
            const PhysicsDiagnosticsFrameInput frame{ diagnosticsView,
                                                      bodyStore,
                                                      colliderStore,
                                                      names,
                                                      diagnosticsCsvWriter,
                                                      deltaSeconds };
            if ( regressionLogEnabled )
            {
                m_sink.EmitRegressionLog( frame );
            }
            if ( frameLogEnabled )
            {
                m_sink.EmitFrame( frame );
            }
        }
        m_sink.FlushCollisionTimes( diagnosticNames, diagnosticNameCount, diagnosticsCsvWriter );
        m_sink.IncrementCollisionTimeFrameIfEnabled();
    }
#else
    (void)diagnosticsSuppressed;
    (void)diagnosticsView;
    (void)bodyStore;
    (void)colliderStore;
    (void)deltaSeconds;
    (void)diagnosticNames;
    (void)diagnosticNameCount;
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

void PhysicsStepDiagnostics::CaptureReplayState( Runtime::ReplaySolverWorldSnapshot& snapshot ) const
{
    snapshot.collisionVisualContacts = m_collisionVisualContacts;
    snapshot.debugContacts = m_physicsDebugContacts;
    snapshot.pipelineTrace = m_physicsPipelineTrace;
    snapshot.collisionVisualFrameActive = m_collisionVisualFrameActive;
}

void PhysicsStepDiagnostics::RestoreReplayState( const Runtime::ReplaySolverWorldSnapshot& snapshot )
{
    m_collisionVisualContacts = snapshot.collisionVisualContacts;
    m_physicsDebugContacts = snapshot.debugContacts;
    m_physicsPipelineTrace = snapshot.pipelineTrace;
    m_collisionVisualFrameActive = snapshot.collisionVisualFrameActive;
}

const std::vector<uint8_t>& PhysicsStepDiagnostics::GetCollisionVisualContacts() const
{
    return m_collisionVisualContacts;
}
std::vector<PhysicsDebugContact>& PhysicsStepDiagnostics::MutableDebugContacts()
{
    return m_physicsDebugContacts;
}
const std::vector<PhysicsDebugContact>& PhysicsStepDiagnostics::GetDebugContacts() const
{
    return m_physicsDebugContacts;
}
std::vector<PhysicsPipelineRecord>& PhysicsStepDiagnostics::MutablePipelineTrace()
{
    return m_physicsPipelineTrace;
}
const std::vector<PhysicsPipelineRecord>& PhysicsStepDiagnostics::GetPipelineTrace() const
{
    return m_physicsPipelineTrace;
}

uint64_t PhysicsStepDiagnostics::CollectDynamicMemoryBytes() const
{
    return VectorCapacityBytes( m_collisionVisualContacts ) + VectorCapacityBytes( m_physicsDebugContacts ) +
           VectorCapacityBytes( m_physicsPipelineTrace );
}

uint64_t PhysicsStepDiagnostics::CollectDebugMemoryBytes() const
{
    return CollectDynamicMemoryBytes();
}
