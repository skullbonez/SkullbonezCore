/*
File: SkullbonezSource/SkullbonezReplayRecorder.cpp
Purpose:
  Captures bounded replay presentation samples and deterministic state hashes.

Mental model:
  This recorder observes committed simulation state. It must not mutate bodies,
  physics caches, renderer resources, or UI state; capture enabled should only
  add bounded CPU memory use and optional hash-log writes.
*/
#include "SkullbonezReplayRecorder.h"

#include "SkullbonezCameraCollection.h"
#include "SkullbonezGameModelCollection.h"
#include "SkullbonezWorldEnvironment.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace SkullbonezCore::Basics;
using SkullbonezCore::Environment::CameraCollection;
using SkullbonezCore::Environment::WorldEnvironment;
using SkullbonezCore::GameObjects::GameModel;
using SkullbonezCore::GameObjects::GameModelCollection;
using SkullbonezCore::Math::Orientation::Quaternion;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Physics::PhysicsDebugContact;

namespace
{
constexpr int REPLAY_TICKS_PER_SECOND = 120;
constexpr int REPLAY_MIN_SECONDS = 1;
constexpr int REPLAY_MAX_SECONDS = 600;
constexpr uint64_t FNV64_OFFSET = 14695981039346656037ull;
constexpr uint64_t FNV64_PRIME = 1099511628211ull;

uint64_t HashByte( uint64_t hash, uint8_t value )
{
    hash ^= static_cast<uint64_t>( value );
    hash *= FNV64_PRIME;
    return hash;
}

uint64_t HashBytes( uint64_t hash, const void* bytes, std::size_t byteCount )
{
    const uint8_t* cursor = static_cast<const uint8_t*>( bytes );
    for ( std::size_t i = 0; i < byteCount; ++i )
    {
        hash = HashByte( hash, cursor[i] );
    }
    return hash;
}

uint64_t HashUint32( uint64_t hash, uint32_t value )
{
    return HashBytes( hash, &value, sizeof( value ) );
}

uint64_t HashInt( uint64_t hash, int value )
{
    const int32_t packed = static_cast<int32_t>( value );
    return HashBytes( hash, &packed, sizeof( packed ) );
}

uint64_t HashBool( uint64_t hash, bool value )
{
    return HashByte( hash, value ? static_cast<uint8_t>( 1 ) : static_cast<uint8_t>( 0 ) );
}

uint64_t HashFloat( uint64_t hash, float value )
{
    uint32_t packed = 0;
    std::memcpy( &packed, &value, sizeof( packed ) );
    return HashUint32( hash, packed );
}

uint64_t HashVector( uint64_t hash, const Vector3& value )
{
    hash = HashFloat( hash, value.x );
    hash = HashFloat( hash, value.y );
    hash = HashFloat( hash, value.z );
    return hash;
}

uint64_t HashOrientation( uint64_t hash, const float orientation[4] )
{
    hash = HashFloat( hash, orientation[0] );
    hash = HashFloat( hash, orientation[1] );
    hash = HashFloat( hash, orientation[2] );
    hash = HashFloat( hash, orientation[3] );
    return hash;
}

uint64_t HashWorld( uint64_t hash, const ReplayWorldPresentationSample& world )
{
    hash = HashFloat( hash, world.gravity );
    hash = HashFloat( hash, world.fluidHeight );
    hash = HashFloat( hash, world.fluidDensity );
    hash = HashBool( hash, world.waterHidden );
    hash = HashBool( hash, world.terrainHidden );
    hash = HashBool( hash, world.fixedStep );
    hash = HashBool( hash, world.scenePhysicsEnabled );
    hash = HashBool( hash, world.sceneTextEnabled );
    return hash;
}

ReplayBodyShapeKind ShapeKindForModel( const GameModel& model )
{
    if ( model.IsSphere() )
    {
        return ReplayBodyShapeKind::Sphere;
    }
    if ( model.IsBox() )
    {
        return ReplayBodyShapeKind::Box;
    }
    if ( model.IsConvexHull() )
    {
        return ReplayBodyShapeKind::ConvexHull;
    }
    return ReplayBodyShapeKind::Unknown;
}

uint16_t SaturatingUint16( std::size_t value )
{
    return value > 0xffffu ? 0xffffu : static_cast<uint16_t>( value );
}

void IncrementBodyContactSummary( int bodyIndex,
                                  float penetration,
                                  float normalImpulse,
                                  std::vector<uint16_t>& contactCounts,
                                  std::vector<float>& maxPenetrations,
                                  std::vector<float>& normalImpulseSums )
{
    if ( bodyIndex < 0 || bodyIndex >= static_cast<int>( contactCounts.size() ) )
    {
        return;
    }

    const std::size_t index = static_cast<std::size_t>( bodyIndex );
    if ( contactCounts[index] < 0xffffu )
    {
        ++contactCounts[index];
    }
    maxPenetrations[index] = (std::max)( maxPenetrations[index], penetration );
    normalImpulseSums[index] += normalImpulse;
}

uint64_t HashBodySample( uint64_t hash, const ReplayBodyPresentationSample& body )
{
    hash = HashUint32( hash, body.id.value );
    hash = HashInt( hash, body.modelIndex );
    hash = HashInt( hash, static_cast<int>( body.shapeKind ) );
    hash = HashVector( hash, body.position );
    hash = HashOrientation( hash, body.orientation );
    hash = HashVector( hash, body.linearVelocity );
    hash = HashVector( hash, body.angularVelocity );
    hash = HashFloat( hash, body.mass );
    hash = HashBool( hash, body.fixed );
    hash = HashBool( hash, body.sleeping );
    hash = HashBool( hash, body.sleepSupported );
    hash = HashBool( hash, body.sleepInhibited );
    hash = HashBool( hash, body.collisionContact );
    hash = HashInt( hash, body.sleepIslandVisualId );
    hash = HashInt( hash, static_cast<int>( body.contactCount ) );
    hash = HashFloat( hash, body.maxPenetration );
    hash = HashFloat( hash, body.normalImpulseSum );
    return hash;
}
} // namespace

bool ReplayRecorder::Configure( const ReplayRecorderConfig& config )
{
    m_config = config;
    m_config.retentionSeconds = std::clamp( m_config.retentionSeconds, REPLAY_MIN_SECONDS, REPLAY_MAX_SECONDS );
    m_config.checkpointIntervalFrames = (std::max)( 1, m_config.checkpointIntervalFrames );
    m_config.enabled = m_config.enabled || !m_config.hashLogPath.empty();

    m_hashLog.close();
    m_samples.clear();
    m_checkpoints.clear();
    m_contactCountScratch.clear();
    m_maxPenetrationScratch.clear();
    m_normalImpulseSumScratch.clear();
    m_sampleHead = 0;
    m_sampleCount = 0;
    m_checkpointHead = 0;
    m_checkpointCount = 0;
    m_nextFrameIndex = 0;
    m_totalFramesCaptured = 0;
    m_totalFramesEvicted = 0;
    m_latestStateHash = 0;

    if ( !m_config.enabled )
    {
        return true;
    }

    m_samples.resize( SampleCapacityFromConfig() );
    m_checkpoints.resize( CheckpointCapacityFromConfig() );

    if ( !m_config.hashLogPath.empty() )
    {
        m_hashLog.open( m_config.hashLogPath, std::ios::out | std::ios::trunc );
        if ( !m_hashLog.is_open() )
        {
            fprintf( stderr, "[replay] Failed to open hash log: %s\n", m_config.hashLogPath.c_str() );
            m_config.hashLogPath.clear();
        }
    }

    return true;
}

void ReplayRecorder::ResetTimeline( const char* sceneLabel )
{
    if ( !m_config.enabled )
    {
        return;
    }

    m_sampleHead = 0;
    m_sampleCount = 0;
    m_checkpointHead = 0;
    m_checkpointCount = 0;
    m_nextFrameIndex = 0;
    m_latestStateHash = 0;
    WriteHashLogHeader( sceneLabel );
}

void ReplayRecorder::CaptureFrame( const ReplayCaptureInput& input )
{
    if ( !m_config.enabled || !input.models )
    {
        return;
    }

    ReplayPresentationSample& sample = AcquireSampleSlot();
    sample.frameIndex = m_nextFrameIndex++;
    sample.sceneFrame = input.sceneFrame;
    sample.physicsDt = input.physicsDt;
    sample.simulationSeconds = input.physicsDt > 0.0f
                                   ? static_cast<double>( sample.frameIndex ) * static_cast<double>( input.physicsDt )
                                   : input.simulationSeconds;
    sample.world.fixedStep = input.fixedStep;
    sample.world.scenePhysicsEnabled = input.scenePhysicsEnabled;
    sample.world.sceneTextEnabled = input.sceneTextEnabled;
    sample.world.waterHidden = input.waterHidden;
    sample.world.terrainHidden = input.terrainHidden;
    sample.contactCount = 0;
    sample.pipelineRecordCount = 0;
    sample.checkpointBoundary = ( sample.frameIndex == 0 ) ||
                                ( sample.frameIndex % static_cast<ReplayFrameIndex>( m_config.checkpointIntervalFrames ) == 0 );

    if ( input.world )
    {
        sample.world.gravity = input.world->GetGravity();
        sample.world.fluidHeight = input.world->GetFluidSurfaceHeight();
        sample.world.fluidDensity = input.world->GetFluidDensity();
    }

    if ( input.cameras )
    {
        sample.camera.eye = input.cameras->GetCameraTranslation();
        sample.camera.view = input.cameras->GetCameraView();
        sample.camera.up = input.cameras->GetCameraUp();
    }

    GameModelCollection& models = *input.models;
    std::vector<GameModel>& physicsModels = models.PhysicsModels();
    const std::size_t modelCount = physicsModels.size();
    sample.bodies.clear();
    sample.bodies.reserve( modelCount );

    m_contactCountScratch.assign( modelCount, 0 );
    m_maxPenetrationScratch.assign( modelCount, 0.0f );
    m_normalImpulseSumScratch.assign( modelCount, 0.0f );

    const std::vector<PhysicsDebugContact>& contacts = models.GetPhysicsDebugContacts();
    sample.contactCount = SaturatingUint16( contacts.size() );
    for ( const PhysicsDebugContact& contact : contacts )
    {
        IncrementBodyContactSummary( contact.bodyA,
                                     contact.penetration,
                                     contact.normalImpulse,
                                     m_contactCountScratch,
                                     m_maxPenetrationScratch,
                                     m_normalImpulseSumScratch );
        IncrementBodyContactSummary( contact.bodyB,
                                     contact.penetration,
                                     contact.normalImpulse,
                                     m_contactCountScratch,
                                     m_maxPenetrationScratch,
                                     m_normalImpulseSumScratch );
    }

    sample.pipelineRecordCount = SaturatingUint16( models.GetPhysicsPipelineTrace().size() );

    const std::vector<uint8_t>& sleepStates = models.GetSleepStates();
    const std::vector<uint8_t>& sleepSupportedStates = models.GetSleepSupportedStates();
    const std::vector<uint8_t>& sleepInhibitedStates = models.GetSleepInhibitedStates();
    const std::vector<uint8_t>& collisionContacts = models.GetCollisionVisualContacts();
    const std::vector<int>& sleepIslandIds = models.GetSleepIslandVisualIds();

    uint64_t hash = FNV64_OFFSET;
    hash = HashWorld( hash, sample.world );
    hash = HashInt( hash, static_cast<int>( modelCount ) );
    hash = HashInt( hash, static_cast<int>( sample.contactCount ) );
    hash = HashInt( hash, static_cast<int>( sample.pipelineRecordCount ) );

    for ( std::size_t i = 0; i < modelCount; ++i )
    {
        GameModel& model = physicsModels[i];
        ReplayBodyPresentationSample body;
        body.id.value = model.GetReplayBodyId();
        body.modelIndex = static_cast<int>( i );
        const char* modelName = model.GetName();
        if ( modelName && modelName[0] != '\0' )
        {
            strncpy_s( body.name, sizeof( body.name ), modelName, _TRUNCATE );
        }
        body.shapeKind = ShapeKindForModel( model );
        body.position = model.GetPosition();
        body.linearVelocity = model.GetVelocity();
        body.angularVelocity = model.GetAngularVelocity();
        const Quaternion& orientation = model.GetOrientation();
        orientation.GetComponents( body.orientation[0], body.orientation[1], body.orientation[2], body.orientation[3] );
        body.mass = model.GetMass();
        body.fixed = model.IsFixed();
        body.sleeping = i < sleepStates.size() && sleepStates[i] != 0;
        body.sleepSupported = i < sleepSupportedStates.size() && sleepSupportedStates[i] != 0;
        body.sleepInhibited = i < sleepInhibitedStates.size() && sleepInhibitedStates[i] != 0;
        body.collisionContact = i < collisionContacts.size() && collisionContacts[i] != 0;
        body.sleepIslandVisualId = i < sleepIslandIds.size() ? sleepIslandIds[i] : 0;
        body.contactCount = i < m_contactCountScratch.size() ? m_contactCountScratch[i] : 0;
        body.maxPenetration = i < m_maxPenetrationScratch.size() ? m_maxPenetrationScratch[i] : 0.0f;
        body.normalImpulseSum = i < m_normalImpulseSumScratch.size() ? m_normalImpulseSumScratch[i] : 0.0f;

        hash = HashBodySample( hash, body );
        sample.bodies.push_back( body );
    }

    sample.stateHash = hash;
    m_latestStateHash = hash;
    ++m_totalFramesCaptured;

    if ( sample.checkpointBoundary )
    {
        StoreCheckpointSummary( sample );
    }
    WriteHashLogRow( sample );
}

void ReplayRecorder::FlushHashLog()
{
    if ( m_hashLog.is_open() )
    {
        m_hashLog.flush();
    }
}

bool ReplayRecorder::IsEnabled() const
{
    return m_config.enabled;
}

ReplayRecorderStats ReplayRecorder::GetStats() const
{
    ReplayRecorderStats stats;
    stats.enabled = m_config.enabled;
    stats.totalFramesCaptured = m_totalFramesCaptured;
    stats.totalFramesEvicted = m_totalFramesEvicted;
    stats.nextFrameIndex = m_nextFrameIndex;
    stats.sampleCapacity = m_samples.size();
    stats.sampleCount = m_sampleCount;
    stats.checkpointCapacity = m_checkpoints.size();
    stats.checkpointCount = m_checkpointCount;
    stats.latestStateHash = m_latestStateHash;
    return stats;
}

const ReplayPresentationSample* ReplayRecorder::LatestSample() const
{
    if ( m_sampleCount == 0 || m_samples.empty() )
    {
        return nullptr;
    }

    const std::size_t index = ( m_sampleHead + m_sampleCount - 1 ) % m_samples.size();
    return &m_samples[index];
}


const ReplayPresentationSample* ReplayRecorder::SampleAtNormalized( float normalized ) const
{
    if ( m_sampleCount == 0 || m_samples.empty() )
    {
        return nullptr;
    }

    const float t = std::clamp( normalized, 0.0f, 1.0f );
    const std::size_t maxOffset = m_sampleCount - 1;
    const std::size_t offset = static_cast<std::size_t>( static_cast<float>( maxOffset ) * t + 0.5f );
    const std::size_t index = ( m_sampleHead + (std::min)( offset, maxOffset ) ) % m_samples.size();
    return &m_samples[index];
}


ReplayPresentationSample& ReplayRecorder::AcquireSampleSlot()
{
    if ( m_sampleCount < m_samples.size() )
    {
        const std::size_t index = ( m_sampleHead + m_sampleCount ) % m_samples.size();
        ++m_sampleCount;
        return m_samples[index];
    }

    ReplayPresentationSample& sample = m_samples[m_sampleHead];
    m_sampleHead = ( m_sampleHead + 1 ) % m_samples.size();
    ++m_totalFramesEvicted;
    return sample;
}

void ReplayRecorder::StoreCheckpointSummary( const ReplayPresentationSample& sample )
{
    if ( m_checkpoints.empty() )
    {
        return;
    }

    std::size_t index = 0;
    if ( m_checkpointCount < m_checkpoints.size() )
    {
        index = ( m_checkpointHead + m_checkpointCount ) % m_checkpoints.size();
        ++m_checkpointCount;
    }
    else
    {
        index = m_checkpointHead;
        m_checkpointHead = ( m_checkpointHead + 1 ) % m_checkpoints.size();
    }

    ReplayCheckpointSummary& checkpoint = m_checkpoints[index];
    checkpoint.frameIndex = sample.frameIndex;
    checkpoint.simulationSeconds = sample.simulationSeconds;
    checkpoint.stateHash = sample.stateHash;
    checkpoint.bodyCount = static_cast<uint32_t>( (std::min)( sample.bodies.size(), static_cast<std::size_t>( 0xffffffffu ) ) );
    checkpoint.contactCount = sample.contactCount;
    checkpoint.pipelineRecordCount = sample.pipelineRecordCount;
}

void ReplayRecorder::WriteHashLogHeader( const char* sceneLabel )
{
    if ( !m_hashLog.is_open() )
    {
        return;
    }

    m_hashLog << "# replay_scene scene=\"" << ( sceneLabel && sceneLabel[0] != '\0' ? sceneLabel : "generated" )
              << "\" retention_seconds=" << m_config.retentionSeconds
              << " retention_frames=" << m_samples.size()
              << " checkpoint_interval_frames=" << m_config.checkpointIntervalFrames
              << "\n";
    m_hashLog << "frame,scene_frame,simulation_seconds,body_count,contact_count,pipeline_record_count,checkpoint,state_hash\n";
}

void ReplayRecorder::WriteHashLogRow( const ReplayPresentationSample& sample )
{
    if ( !m_hashLog.is_open() )
    {
        return;
    }

    char line[256] = {};
    sprintf_s( line,
               sizeof( line ),
               "%llu,%d,%.6f,%llu,%u,%u,%u,0x%016llX\n",
               static_cast<unsigned long long>( sample.frameIndex ),
               sample.sceneFrame,
               sample.simulationSeconds,
               static_cast<unsigned long long>( sample.bodies.size() ),
               static_cast<unsigned>( sample.contactCount ),
               static_cast<unsigned>( sample.pipelineRecordCount ),
               sample.checkpointBoundary ? 1u : 0u,
               static_cast<unsigned long long>( sample.stateHash ) );
    m_hashLog << line;
}

std::size_t ReplayRecorder::SampleCapacityFromConfig() const
{
    const int seconds = std::clamp( m_config.retentionSeconds, REPLAY_MIN_SECONDS, REPLAY_MAX_SECONDS );
    return static_cast<std::size_t>( seconds ) * static_cast<std::size_t>( REPLAY_TICKS_PER_SECOND );
}

std::size_t ReplayRecorder::CheckpointCapacityFromConfig() const
{
    const std::size_t sampleCapacity = SampleCapacityFromConfig();
    const std::size_t interval = static_cast<std::size_t>( (std::max)( 1, m_config.checkpointIntervalFrames ) );
    return (std::max)( static_cast<std::size_t>( 2 ), sampleCapacity / interval + 2 );
}
