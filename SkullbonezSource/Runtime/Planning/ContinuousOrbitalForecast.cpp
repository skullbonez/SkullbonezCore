/*
File: ContinuousOrbitalForecast.cpp
Purpose:
  Implements the Planning-owned continuous orbital forecast lifecycle.

Summary:
  Start admits one authored scene contract and live seed. Worker callbacks copy
  only configured members plus distinct configured contact pairs into bounded
  values before updating the mutex-protected stability analyzer. The render
  turn separately snapshots the detached logical ring into an inactive
  presentation bank, then publishes only a complete chronological packet.

Invariants:
  - Contract mutation happens only after the producer worker has joined.
  - A configured body appears exactly once in every analyzer publication.
  - Contact duplication cannot overflow the authored-pair stack capacity.
  - Presentation storage allocates only during owner construction; Publish
    mutates the inactive bank and preserves the last coherent packet on failure.
  - Every published member head comes from the same newest absolute tick, and
    no range connects the newest retained row back to the logical oldest row.

Related:
  - ContinuousOrbitalForecast.h
  - SkullbonezSource/Physics/PhysicsDiagnosticsView.h
  - SkullbonezSource/Physics/PhysicsBodyStore.h
*/
#include "ContinuousOrbitalForecast.h"

#include "../../Physics/PhysicsDiagnosticsView.h"
#include "../../Physics/PhysicsEngine.h"
#include "../../Physics/PhysicsTimestep.h"
#include "../../Core/SceneCapacity.h"
#include "../Scene/SceneEntityStore.h"

#include <algorithm>
#include <limits>
#include <new>

namespace SkullbonezCore::Runtime
{
namespace
{
constexpr float RIBBON_WIDTH = 1.25f;
constexpr float RIBBON_ALPHA = 0.9f;
constexpr float RIBBON_EDGE_FEATHER = 1.0f;
constexpr float RIBBON_EMPHASIS = 0.15f;
constexpr float HEAD_MARKER_HALF_EXTENT = 2.0f;

void WriteRibbonRecord( float* destination, const Math::Vector::Vector3& start, const Math::Vector::Vector3& end,
                        const ContinuousOrbitalPresentationMember& member ) noexcept
{
    const float packed[ContinuousOrbitalPresentation::FLOATS_PER_RIBBON_RECORD] = { start.x,         start.y,       start.z,      end.x,
          end.y,           end.z,         RIBBON_WIDTH, member.colorR,
          member.colorG,   member.colorB, RIBBON_ALPHA, RIBBON_EDGE_FEATHER,
          RIBBON_EMPHASIS, start.x,       start.y,      start.z,
          end.x,           end.y,         end.z };

    std::copy( std::begin( packed ), std::end( packed ), destination );
}

void AppendColoredLine( std::array<float, ContinuousOrbitalPresentation::LINE_FLOAT_CAPACITY>& lines,
                        std::size_t& floatCount, const Math::Vector::Vector3& start, const Math::Vector::Vector3& end,
                        const ContinuousOrbitalPresentationMember& member ) noexcept
{
    const float packed[12] = { start.x, start.y, start.z, member.colorR, member.colorG, member.colorB,
                               end.x,   end.y,   end.z,   member.colorR, member.colorG, member.colorB };
    std::copy( std::begin( packed ), std::end( packed ), lines.begin() + floatCount );
    floatCount += std::size( packed );
}
} // namespace

ContinuousOrbitalPresentation::ContinuousOrbitalPresentation()
{
    // Runtime allocation policy: both publication banks and the largest
    // source-row scratch buffer are acquired before the feature can start.
    // Publish performs no heap growth while the worker advances the ring.
    constexpr std::size_t recordFloatCapacity = RIBBON_RECORD_CAPACITY * FLOATS_PER_RIBBON_RECORD;
    m_records[0] = std::unique_ptr<float[]>( new ( std::nothrow ) float[recordFloatCapacity] );
    m_records[1] = std::unique_ptr<float[]>( new ( std::nothrow ) float[recordFloatCapacity] );
    m_rowPositions = std::unique_ptr<Math::Vector::Vector3[]>( new ( std::nothrow ) Math::Vector::Vector3[Scene::Capacity::MAX_SCENE_OBJECTS] );
    m_storageReady = m_records[0] && m_records[1] && m_rowPositions;
}

bool ContinuousOrbitalPresentation::Begin( std::span<const ContinuousOrbitalPresentationMember> members,
                                           std::size_t sourceBodyCount ) noexcept
{
    Reset();

    if ( !m_storageReady || members.empty() || members.size() > m_members.size() || sourceBodyCount == 0u ||
         sourceBodyCount > static_cast<std::size_t>( Scene::Capacity::MAX_SCENE_OBJECTS ) )
    {
        return false;
    }

    for ( std::size_t index = 0u; index < members.size(); ++index )
    {
        if ( members[index].identity == 0u || members[index].bodyRow >= sourceBodyCount )
        {
            return false;
        }

        for ( std::size_t previous = 0u; previous < index; ++previous )
        {
            if ( members[previous].identity == members[index].identity ||
                 members[previous].bodyRow == members[index].bodyRow )
            {
                return false;
            }
        }

        m_members[index] = members[index];
    }

    m_memberCount = members.size();
    m_sourceBodyCount = sourceBodyCount;
    m_configured = true;
    return true;
}

void ContinuousOrbitalPresentation::Reset() noexcept
{
    m_banks = {};
    m_memberCount = 0u;
    m_sourceBodyCount = 0u;
    m_publishedBank = 0u;
    m_revision = 0u;
    m_configured = false;
}

bool ContinuousOrbitalPresentation::Publish( const ContinuousPredictionSampleRingSnapshot& samples ) noexcept
{
    if ( !m_configured || samples.Cancelled() || samples.Failed() || samples.RowCount() < 2u ||
         samples.BodyCount() != m_sourceBodyCount )
    {
        return false;
    }

    const std::size_t logicalSegmentCount = samples.RowCount() - 1u;
    const std::size_t weightedSegmentCount = logicalSegmentCount * m_memberCount;
    std::size_t stride = (std::max)( std::size_t { 1u },
                                     ( weightedSegmentCount + RIBBON_RECORD_CAPACITY - 1u ) / RIBBON_RECORD_CAPACITY );
    std::size_t sampledSegmentsPerBody = ( logicalSegmentCount + stride - 1u ) / stride;

    while ( sampledSegmentsPerBody * m_memberCount > RIBBON_RECORD_CAPACITY )
    {
        ++stride;
        sampledSegmentsPerBody = ( logicalSegmentCount + stride - 1u ) / stride;
    }

    // Lifetime: Packet borrows the published bank synchronously through the
    // render frame. Build the other bank so a torn ring read cannot mutate the
    // values still visible to RuntimeRenderer.
    const std::size_t nextBank = 1u - m_publishedBank;
    BankState& bank = m_banks[nextBank];
    bank = {};
    auto& ranges = m_ranges[nextBank];
    auto& lines = m_lines[nextBank];
    float* records = m_records[nextBank].get();
    const std::uint64_t nextRevision = m_revision == ( std::numeric_limits<std::uint64_t>::max )() ? 1u : m_revision + 1u;

    for ( std::size_t memberIndex = 0u; memberIndex < m_memberCount; ++memberIndex )
    {
        Rendering::RetainedGeometryRangeToken& range = ranges[memberIndex];
        range = {};
        range.identity = m_members[memberIndex].identity;
        range.drawOrder = memberIndex;
        range.firstRecord = static_cast<std::uint32_t>( memberIndex * sampledSegmentsPerBody );
        range.recordCapacity = static_cast<std::uint32_t>( sampledSegmentsPerBody );
        range.sourceVersion = static_cast<std::uint32_t>( nextRevision );
        range.cacheSlot = static_cast<std::uint32_t>( memberIndex );
        range.lane = Rendering::RetainedGeometryLane::Ordinary;
    }

    const std::span<Math::Vector::Vector3> rowPositions( m_rowPositions.get(), m_sourceBodyCount );
    std::uint64_t rowTick = 0u;

    if ( !samples.TryReadRow( 0u, rowPositions, rowTick ) || rowTick != samples.OldestAbsoluteTick() )
    {
        return false;
    }

    std::array<Math::Vector::Vector3, Scene::ORBITAL_STABILITY_MEMBER_CAPACITY> previousPositions = {};

    for ( std::size_t memberIndex = 0u; memberIndex < m_memberCount; ++memberIndex )
    {
        previousPositions[memberIndex] = rowPositions[m_members[memberIndex].bodyRow];
    }

    std::size_t previousLogicalRow = 0u;

    // Invariant: iteration follows logical oldest-to-newest rows. Physical ring
    // wrap is hidden by TryReadRow, so this loop can never emit a seam chord.
    while ( previousLogicalRow < logicalSegmentCount )
    {
        const std::size_t currentLogicalRow = (std::min)( previousLogicalRow + stride, logicalSegmentCount );
        const std::uint64_t expectedTick = samples.OldestAbsoluteTick() + currentLogicalRow;

        if ( expectedTick < samples.OldestAbsoluteTick() ||
             !samples.TryReadRow( currentLogicalRow, rowPositions, rowTick ) || rowTick != expectedTick )
        {
            return false;
        }

        for ( std::size_t memberIndex = 0u; memberIndex < m_memberCount; ++memberIndex )
        {
            Rendering::RetainedGeometryRangeToken& range = ranges[memberIndex];
            const std::size_t recordIndex = static_cast<std::size_t>( range.firstRecord ) + range.recordCount;
            float* record = records + recordIndex * FLOATS_PER_RIBBON_RECORD;
            const Math::Vector::Vector3 current = rowPositions[m_members[memberIndex].bodyRow];
            WriteRibbonRecord( record, previousPositions[memberIndex], current, m_members[memberIndex] );

            if ( range.recordCount > 0u )
            {
                float* previousRecord = record - FLOATS_PER_RIBBON_RECORD;
                record[13] = previousRecord[0];
                record[14] = previousRecord[1];
                record[15] = previousRecord[2];
                previousRecord[16] = current.x;
                previousRecord[17] = current.y;
                previousRecord[18] = current.z;
            }

            ++range.recordCount;
            previousPositions[memberIndex] = current;
        }

        previousLogicalRow = currentLogicalRow;
    }

    if ( rowTick != samples.NewestAbsoluteTick() )
    {
        return false;
    }

    for ( std::size_t memberIndex = 0u; memberIndex < m_memberCount; ++memberIndex )
    {
        const Math::Vector::Vector3 head = previousPositions[memberIndex];
        AppendColoredLine( lines, bank.lineFloatCount, head + Math::Vector::Vector3( -HEAD_MARKER_HALF_EXTENT, 0.0f, 0.0f ),
                           head + Math::Vector::Vector3( HEAD_MARKER_HALF_EXTENT, 0.0f, 0.0f ), m_members[memberIndex] );
        AppendColoredLine( lines, bank.lineFloatCount, head + Math::Vector::Vector3( 0.0f, -HEAD_MARKER_HALF_EXTENT, 0.0f ),
                           head + Math::Vector::Vector3( 0.0f, HEAD_MARKER_HALF_EXTENT, 0.0f ), m_members[memberIndex] );
        AppendColoredLine( lines, bank.lineFloatCount, head + Math::Vector::Vector3( 0.0f, 0.0f, -HEAD_MARKER_HALF_EXTENT ),
                           head + Math::Vector::Vector3( 0.0f, 0.0f, HEAD_MARKER_HALF_EXTENT ), m_members[memberIndex] );
    }

    bank.ribbonSegmentCount = sampledSegmentsPerBody * m_memberCount;
    bank.ribbonFloatCount = bank.ribbonSegmentCount * FLOATS_PER_RIBBON_RECORD;
    bank.rangeCount = m_memberCount;
    bank.headMarkerCount = m_memberCount;
    bank.oldestAbsoluteTick = samples.OldestAbsoluteTick();
    bank.newestAbsoluteTick = samples.NewestAbsoluteTick();
    bank.sourceRowCount = samples.RowCount();
    bank.coherent = true;
    m_revision = nextRevision;
    m_publishedBank = nextBank;
    return true;
}

Rendering::RetainedGeometryPacket ContinuousOrbitalPresentation::Packet() const noexcept
{
    const BankState& bank = m_banks[m_publishedBank];

    if ( !m_configured || !bank.coherent )
    {
        return {};
    }

    Rendering::RetainedGeometryPacket packet;
    packet.compactRibbonRecords = std::span<const float>( m_records[m_publishedBank].get(), bank.ribbonFloatCount );
    packet.ribbonRanges = std::span<const Rendering::RetainedGeometryRangeToken>( m_ranges[m_publishedBank].data(),
                                                                                  bank.rangeCount );
    packet.coloredLineVertices = std::span<const float>( m_lines[m_publishedBank].data(), bank.lineFloatCount );
    packet.stream = { STREAM_IDENTITY, m_revision };
    packet.sourceSequence = bank.newestAbsoluteTick;
    packet.pointMarkerCount = static_cast<std::uint32_t>( bank.headMarkerCount );
    return packet;
}

ContinuousOrbitalPresentationView ContinuousOrbitalPresentation::View() const noexcept
{
    const BankState& bank = m_banks[m_publishedBank];
    ContinuousOrbitalPresentationView view;
    view.oldestAbsoluteTick = bank.oldestAbsoluteTick;
    view.newestAbsoluteTick = bank.newestAbsoluteTick;
    view.revision = m_revision;
    view.sourceRowCount = bank.sourceRowCount;
    view.ribbonSegmentCount = bank.ribbonSegmentCount;
    view.headMarkerCount = bank.headMarkerCount;
    view.retainedBytes = 2u * RIBBON_RECORD_CAPACITY * FLOATS_PER_RIBBON_RECORD * sizeof( float ) +
                         2u * LINE_FLOAT_CAPACITY * sizeof( float ) +
                         2u * m_ranges[0].size() * sizeof( Rendering::RetainedGeometryRangeToken ) +
                         static_cast<std::size_t>( Scene::Capacity::MAX_SCENE_OBJECTS ) * sizeof( Math::Vector::Vector3 );
    view.coherent = bank.coherent;
    view.wrapped = bank.coherent && bank.oldestAbsoluteTick > 0u;
    return view;
}

ContinuousOrbitalForecast::ContinuousOrbitalForecast( Core::Profiler* profiler ) noexcept : m_producer( profiler )
{
}

ContinuousOrbitalForecast::~ContinuousOrbitalForecast()
{
    Stop();
}

bool ContinuousOrbitalForecast::Start( const Physics::PhysicsEngine& liveEngine,
                                       const Gameplay::TornadoGameplay& liveTornado, const Core::EngineConfig& config,
                                       const Physics::PhysicsWorldForces& worldForces, Threading::WorkerPool& workerPool,
                                       const Scene::OrbitalStabilityContract& contract, const SceneEntityStore& entities )
{
    Stop();
    m_contract = contract;
    m_failed = false;
    m_available = contract.enabled && worldForces.mutualGravity.enabled && contract.memberCount > 0u &&
                  contract.memberCount <= Scene::ORBITAL_STABILITY_MEMBER_CAPACITY;

    std::array<ContinuousOrbitalBodySample, Scene::ORBITAL_STABILITY_MEMBER_CAPACITY> seedBodies = {};
    std::array<ContinuousOrbitalPresentationMember, Scene::ORBITAL_STABILITY_MEMBER_CAPACITY> presentationMembers = {};
    std::size_t seedBodyCount = 0u;
    const Physics::PhysicsBodyStore& liveBodies = Physics::PhysicsEngine::ReadBodies( liveEngine );

    bool presentationMembersValid = m_available;

    for ( std::size_t memberIndex = 0u; presentationMembersValid && memberIndex < contract.memberCount; ++memberIndex )
    {
        const Physics::PhysicsSceneObjectId id = contract.members[memberIndex].sceneObjectId;
        const Physics::PhysicsBodyHandle handle = liveBodies.HandleForSceneObjectId( id );
        const int bodyRow = liveBodies.ModelIndexForHandle( handle );
        const int entityRow = entities.FindBySceneObjectId( id );
        presentationMembersValid = bodyRow >= 0 && entityRow >= 0;

        if ( presentationMembersValid )
        {
            const Rendering::RenderMaterial& material = entities.At( entityRow ).renderMaterial;
            presentationMembers[memberIndex] = { static_cast<std::size_t>( bodyRow ), id.value, material.baseColor[0],
                                                 material.baseColor[1], material.baseColor[2] };
        }
    }

    if ( !m_available || !presentationMembersValid || !CaptureConfiguredBodies( liveBodies, seedBodies, seedBodyCount ) ||
         !m_stability.Begin( contract, static_cast<double>( worldForces.mutualGravity.gravitationalConstant ),
                             static_cast<double>( worldForces.mutualGravity.softeningLength ),
                             std::span<const ContinuousOrbitalBodySample>( seedBodies.data(), seedBodyCount ) ) ||
         !m_presentation.Begin( std::span<const ContinuousOrbitalPresentationMember>( presentationMembers.data(),
                                                                                      contract.memberCount ),
                                static_cast<std::size_t>( liveBodies.Count() ) ) ||
         !m_producer.Begin( liveEngine, liveTornado, config, worldForces, workerPool,
                            ContinuousPredictionWindowRowCapacity(), this ) )
    {
        m_producer.Stop();
        m_stability.Reset();
        m_presentation.Reset();
        m_failed = true;
        return false;
    }

    return true;
}

bool ContinuousOrbitalForecast::Reset( const Physics::PhysicsEngine& liveEngine,
                                       const Gameplay::TornadoGameplay& liveTornado, const Core::EngineConfig& config,
                                       const Physics::PhysicsWorldForces& worldForces, Threading::WorkerPool& workerPool,
                                       const Scene::OrbitalStabilityContract& contract, const SceneEntityStore& entities )
{
    return Start( liveEngine, liveTornado, config, worldForces, workerPool, contract, entities );
}

void ContinuousOrbitalForecast::Stop() noexcept
{
    m_producer.Stop();
    m_stability.Reset();
    m_presentation.Reset();
    m_contract = {};
    m_available = false;
    m_failed = false;
}

bool ContinuousOrbitalForecast::AdvanceFrame( const std::chrono::steady_clock::time_point& frameBudgetStart ) noexcept
{
    return m_producer.AdvanceFrame( frameBudgetStart );
}

Rendering::RetainedGeometryPacket ContinuousOrbitalForecast::PreparePresentation() noexcept
{
    const ContinuousPredictionProducerView producer = m_producer.View();

    if ( producer.active && !producer.failed )
    {
        (void)m_presentation.Publish( producer.samples );
    }

    return m_presentation.Packet();
}

ContinuousOrbitalForecastView ContinuousOrbitalForecast::View() const noexcept
{
    const ContinuousPredictionProducerView producer = m_producer.View();
    ContinuousOrbitalForecastView view;
    view.stability = m_stability.View();
    view.presentation = m_presentation.View();
    view.newestAbsoluteTick = producer.newestAbsoluteTick;
    view.simulatedSeconds = producer.simulatedSeconds;
    view.simulatedSecondsPerRealSecond = producer.measuredTicksPerMillisecond * 1000.0 *
                                         static_cast<double>( PHYSICS_FIXED_DT );
    view.rollingWindowAgeSeconds = producer.samples.RowCount() > 1u
                                       ? static_cast<double>( producer.samples.NewestAbsoluteTick() -
                                                              producer.samples.OldestAbsoluteTick() ) *
                                             static_cast<double>( PHYSICS_FIXED_DT )
                                       : 0.0;
    view.retainedBytes = producer.retainedBytes;
    view.available = m_available;
    view.active = producer.active;
    view.workerInFlight = producer.workerInFlight;
    view.failed = m_failed || producer.failed;
    return view;
}

void ContinuousOrbitalForecast::ObserveCompleteContinuousPredictionTick( const Physics::PhysicsBodyStore& bodies, std::span<const Physics::PersistentContact> contacts,
                                                                         std::uint64_t absoluteTick ) noexcept
{
    std::array<ContinuousOrbitalBodySample, Scene::ORBITAL_STABILITY_MEMBER_CAPACITY> bodySamples = {};
    std::array<ContinuousOrbitalContactSample, CONTACT_CAPACITY> contactSamples = {};
    std::size_t bodySampleCount = 0u;
    std::size_t contactSampleCount = 0u;

    if ( !CaptureConfiguredBodies( bodies, bodySamples, bodySampleCount ) )
    {
        ObserveInvalidContinuousPredictionPublication( absoluteTick );
        return;
    }

    for ( const Physics::PersistentContact& contact : contacts )
    {
        if ( contact.bodyA < 0 || contact.bodyB < 0 )
        {
            continue;
        }

        const Physics::PhysicsBodyRecord* bodyA = bodies.RecordForHandle( bodies.HandleForModelIndex( contact.bodyA ) );
        const Physics::PhysicsBodyRecord* bodyB = bodies.RecordForHandle( bodies.HandleForModelIndex( contact.bodyB ) );

        if ( !bodyA || !bodyB || !IsConfiguredMember( bodyA->sceneObjectId ) ||
             !IsConfiguredMember( bodyB->sceneObjectId ) || bodyA->sceneObjectId == bodyB->sceneObjectId )
        {
            continue;
        }

        ContinuousOrbitalContactSample candidate { bodyA->sceneObjectId, bodyB->sceneObjectId };

        if ( candidate.bodyB.value < candidate.bodyA.value )
        {
            std::swap( candidate.bodyA, candidate.bodyB );
        }

        const bool duplicate = std::find_if( contactSamples.begin(), contactSamples.begin() + contactSampleCount,
                                             [&]( const ContinuousOrbitalContactSample& existing )
                                             {
                                                 return existing.bodyA == candidate.bodyA &&
                                                        existing.bodyB == candidate.bodyB;
                                             } ) != contactSamples.begin() + contactSampleCount;

        if ( !duplicate && contactSampleCount < contactSamples.size() )
        {
            contactSamples[contactSampleCount++] = candidate;
        }
    }

    (void)m_stability.ObserveTick( { std::span<const ContinuousOrbitalBodySample>( bodySamples.data(), bodySampleCount ),
                                     std::span<const ContinuousOrbitalContactSample>( contactSamples.data(), contactSampleCount ), absoluteTick, true,
                                     true } );
}

void ContinuousOrbitalForecast::ObserveInvalidContinuousPredictionPublication( std::uint64_t absoluteTick ) noexcept
{
    (void)m_stability.ObserveTick( { {}, {}, absoluteTick, true, false } );
}

bool ContinuousOrbitalForecast::CaptureConfiguredBodies( const Physics::PhysicsBodyStore& bodies,
                                                         std::array<ContinuousOrbitalBodySample, Scene::ORBITAL_STABILITY_MEMBER_CAPACITY>& out,
                                                         std::size_t& outCount ) const noexcept
{
    outCount = 0u;

    if ( m_contract.memberCount == 0u || m_contract.memberCount > out.size() )
    {
        return false;
    }

    const Physics::PhysicsBodyHotFieldsConstView hot = bodies.HotFields();

    for ( std::size_t memberIndex = 0u; memberIndex < m_contract.memberCount; ++memberIndex )
    {
        const Physics::PhysicsSceneObjectId id = m_contract.members[memberIndex].sceneObjectId;
        const Physics::PhysicsBodyHandle handle = bodies.HandleForSceneObjectId( id );
        const Physics::PhysicsBodyRecord* record = bodies.RecordForHandle( handle );
        const int modelIndex = bodies.ModelIndexForHandle( handle );

        if ( !record || modelIndex < 0 || static_cast<std::size_t>( modelIndex ) >= hot.positionX.size() ||
             outCount >= out.size() )
        {
            return false;
        }

        const std::size_t row = static_cast<std::size_t>( modelIndex );
        out[outCount++] = { id,
                            Physics::PhysicsBodyPosition( hot, row ),
                            Physics::PhysicsBodyOrientation( hot, row ),
                            Physics::PhysicsBodyLinearVelocity( hot, row ),
                            Physics::PhysicsBodyAngularVelocity( hot, row ),
                            static_cast<double>( record->mass ) };
    }

    return outCount == m_contract.memberCount;
}

bool ContinuousOrbitalForecast::IsConfiguredMember( Physics::PhysicsSceneObjectId id ) const noexcept
{
    if ( m_contract.memberCount > m_contract.members.size() )
    {
        return false;
    }

    for ( std::size_t index = 0u; index < m_contract.memberCount; ++index )
    {
        if ( m_contract.members[index].sceneObjectId == id )
        {
            return true;
        }
    }

    return false;
}
} // namespace SkullbonezCore::Runtime
