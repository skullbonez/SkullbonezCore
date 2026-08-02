/*
File: SkullbonezSource/Physics/Stages/PhysicsBroadphaseStage.cpp
Purpose:
  Implements deterministic broadphase candidate generation and retained output.

Summary:
  The stage maintains persistent integer-range membership, adds a one-step
  swept overlay for fast projectiles, canonicalizes solver-visible pair order,
  stamps cells reached by awake bodies, suppresses sleep-only work at emission,
  prunes fixed/joint pairs, and records bounded pipeline evidence. Its opt-in
  Debug stream freezes raw, augmented, and final pair boundaries for the active
  pair-dedup campaign without entering Profile or Release.

Glossary:
  Broadphase filter: Shape-aware cheap predicate applied while grid pairs form.
  Sleep-pruned pair: Pair of dormant bodies with no awake energy to create work.

Invariants:
  - P1 changes only pair work order after proving same-state raw and final work
    membership in both driver directions.
  - `remove_if` predicates preserve their diagnostic side effects in canonical
    solver-visible order.
  - Sleep-only pairs never enter the production candidate vector; Debug records
    the old geometric-admission evidence at the emission skip.
  - Count-only tracing batches admitted pair cardinality without loading body
    positions; full tracing preserves the canonical sorted payload order.
  - No hot-path list operation may exceed its scene-load reservation.
  - Pair-stream files use checked unbuffered I/O, explicit little-endian fields,
    per-pass magic/length/ordinal footers, and a checked global close boundary.

Related:
  - SkullbonezSource/Physics/Stages/PhysicsBroadphaseStage.h
  - SkullbonezSource/Physics/SolverBroadphaseStage.h
  - SkullbonezSource/Physics/PhysicsWorld.cpp
  - Agentic/Reference/engine-glossary.md
*/
#include "PhysicsBroadphaseStage.h"

#include "../../Core/FatalError.h"
#include "../../Core/Profiler.h"
#include "../../Core/SceneCapacity.h"
#include "../ColliderStore.h"
#include "../PhysicsBodyStore.h"
#include "../SolverBroadphaseStage.h"
#include "PhysicsStepDiagnostics.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using SkullbonezCore::Math::Vector::Vector3;
namespace Physics = SkullbonezCore::Physics;
namespace Vector = SkullbonezCore::Math::Vector;

namespace
{
constexpr size_t MAX_PIPELINE_TRACE_RECORDS = 4096;
constexpr float PHYSICS_FAST_SWEEP_MAX_RADIUS = 1.0f;
constexpr float PHYSICS_FAST_SWEEP_MIN_DISTANCE = 1.0f;
constexpr float PHYSICS_FAST_SWEEP_PAIR_SLOP = 1.0f;
constexpr float BROADPHASE_MIN_CELL_SIZE = 0.5f;
constexpr float DEFAULT_BROADPHASE_CELL = 24.0f;
bool IsSolverBodyFixed( const Physics::PhysicsBodyHotFieldsConstView& hotFields, int bodyIndex )
{
    return hotFields.fixed[static_cast<size_t>( bodyIndex )] != 0u;
}

Vector3 SolverBodyPosition( const Physics::PhysicsBodyHotFieldsConstView& hotFields, int bodyIndex )
{
    return Physics::PhysicsBodyPosition( hotFields, static_cast<size_t>( bodyIndex ) );
}

float SolverBodyRadius( std::span<const Physics::ColliderRecord> colliderRecords, int bodyIndex )
{
    return colliderRecords[static_cast<size_t>( bodyIndex )].boundingRadius;
}

// Invariant: conservative augmentation appends only normalized pairs not
// already emitted by the grid. The linear scan preserves first-seen order.
void AppendCandidatePairIfMissing( Physics::PhysicsCandidatePairList& candidatePairs,
                                   const Physics::PhysicsBodyStore& bodyStore, const Physics::ColliderStore& colliderStore,
                                   std::span<const uint8_t> sleepState, float dt, float contactSkin, int a, int b )
{
    const int modelCount = (std::min)( bodyStore.Count(), colliderStore.Count() );

    if ( a == b || a < 0 || b < 0 || a >= modelCount || b >= modelCount )
    {
        return;
    }

    if ( a > b )
    {
        std::swap( a, b );
    }

    if ( !Physics::BroadphaseCandidateCanTouch( bodyStore, colliderStore, sleepState, dt, contactSkin, a, b ) )
    {
        return;
    }

    for ( const std::pair<int, int>& pair : candidatePairs )
    {

        if ( pair.first == a && pair.second == b )
        {
            return;
        }
    }

    if ( !Physics::BroadphaseCandidateAppendHasCapacity( candidatePairs.size(), candidatePairs.capacity() ) )
    {

        // Lane F: growing here would violate the zero-allocation fixed-step
        // contract; dropping the conservative pair could miss a collision.
        SB_FATAL( "Physics/PhysicsBroadphaseStage",
                  "Candidate pair reserve exhausted: size=%zu capacity=%zu phase=steady_gameplay.", candidatePairs.size(),
                  candidatePairs.capacity() );
    }

    candidatePairs.emplace_back( a, b );
}

bool IsFastSmallSweepBody( const Physics::PhysicsBodyHotFieldsConstView& hotFields,
                           std::span<const Physics::ColliderRecord> colliderRecords, int bodyIndex, float dt )
{

    if ( IsSolverBodyFixed( hotFields, bodyIndex ) )
    {
        return false;
    }

    const float radius = SolverBodyRadius( colliderRecords, bodyIndex );

    if ( radius > PHYSICS_FAST_SWEEP_MAX_RADIUS )
    {
        return false;
    }

    const Vector3 displacement = Physics::PhysicsBodyLinearVelocity( hotFields, static_cast<size_t>( bodyIndex ) ) * dt;
    const float displacementSq = Vector::VectorMagSquared( displacement );
    const float minSweepDistance = (std::max)( radius * 2.0f, PHYSICS_FAST_SWEEP_MIN_DISTANCE );
    return displacementSq > minSweepDistance * minSweepDistance;
}

// Invariant: contactEpsilon is the raw config value, not the clamped
// broadphase contact skin. It controls only conservative pair admission.
bool SweptSegmentTouchesExpandedBody( const Physics::PhysicsBodyHotFieldsConstView& hotFields,
                                      std::span<const Physics::ColliderRecord> colliderRecords, int movingIndex,
                                      int targetIndex, float dt, float contactEpsilon )
{
    const Vector3 relativeStart = SolverBodyPosition( hotFields, movingIndex ) -
                                  SolverBodyPosition( hotFields, targetIndex );

    const Vector3 relativeDisplacement = ( Physics::PhysicsBodyLinearVelocity( hotFields,
                                                                               static_cast<size_t>( movingIndex ) ) -
                                           Physics::PhysicsBodyLinearVelocity( hotFields,
                                                                               static_cast<size_t>( targetIndex ) ) ) *
                                         dt;

    const float relativeLengthSq = Vector::VectorMagSquared( relativeDisplacement );

    if ( relativeLengthSq <= TOLERANCE * TOLERANCE )
    {
        return false;
    }

    float t = -( Dot( relativeStart, relativeDisplacement ) ) / relativeLengthSq;
    t = (std::max)( 0.0f, (std::min)( 1.0f, t ) );
    const Vector3 closestRelative = relativeStart + relativeDisplacement * t;
    const float expandedRadius = SolverBodyRadius( colliderRecords, movingIndex ) +
                                 SolverBodyRadius( colliderRecords, targetIndex ) + contactEpsilon +
                                 PHYSICS_FAST_SWEEP_PAIR_SLOP;

    return Vector::VectorMagSquared( closestRelative ) <= expandedRadius * expandedRadius;
}

bool AppendFastSmallSweepPairs( Physics::PhysicsCandidatePairList& candidatePairs,
                                const Physics::PhysicsBodyStore& bodyStore, const Physics::ColliderStore& colliderStore,
                                std::span<const uint8_t> sleepState, const Physics::PhysicsBodyHotFieldsConstView& hotFields,
                                std::span<const Physics::ColliderRecord> colliderRecords,
                                std::span<const int> awakeBodyIndices, float dt, float contactSkin, float contactEpsilon )
{
    const size_t pairCountBeforeSweep = candidatePairs.size();

    for ( int movingIndex : awakeBodyIndices )
    {

        if ( !IsFastSmallSweepBody( hotFields, colliderRecords, movingIndex, dt ) )
        {
            continue;
        }

        const int modelCount = (std::min)( bodyStore.Count(), colliderStore.Count() );

        for ( int targetIndex = 0; targetIndex < modelCount; ++targetIndex )
        {

            if ( movingIndex != targetIndex && SweptSegmentTouchesExpandedBody( hotFields, colliderRecords, movingIndex,
                                                                                targetIndex, dt, contactEpsilon ) )
            {
                AppendCandidatePairIfMissing( candidatePairs, bodyStore, colliderStore, sleepState, dt, contactSkin,
                                              movingIndex, targetIndex );
            }
        }
    }

    return candidatePairs.size() != pairCountBeforeSweep;
}

void CanonicalizeCandidatePairs( Physics::PhysicsCandidatePairList& candidatePairs )
{

    // Why: grid output is already canonical, but rare fast-sweep augmentation
    // appends pairs after it. Sorting once before pruning keeps the complete
    // solver-visible order independent of which conservative path found a pair.
    std::sort( candidatePairs.begin(), candidatePairs.end() );
}

bool IsFixedSolverCandidatePair( const Physics::PhysicsBodyHotFieldsConstView& hotFields, int modelCount,
                                 const std::pair<int, int>& pair )
{
    const int a = pair.first;
    const int b = pair.second;
    return a >= 0 && b >= 0 && a < modelCount && b < modelCount && IsSolverBodyFixed( hotFields, a ) &&
           IsSolverBodyFixed( hotFields, b );
}

struct FixedSolverCandidatePairPredicate
{
    Physics::PhysicsBodyHotFieldsConstView hotFields;
    int modelCount = 0;

    bool operator()( const std::pair<int, int>& pair ) const
    {
        return IsFixedSolverCandidatePair( hotFields, modelCount, pair );
    }
};

bool IsPointJointCandidatePair( const Physics::PhysicsBodyStore& bodyStore,
                                std::span<const Physics::PointJointConstraint> pointJointConstraints,
                                const std::pair<int, int>& pair )
{
    int bodyA = pair.first;
    int bodyB = pair.second;

    if ( bodyA < 0 || bodyB < 0 || bodyA == bodyB )
    {
        return false;
    }

    if ( bodyA > bodyB )
    {
        std::swap( bodyA, bodyB );
    }

    for ( const Physics::PointJointConstraint& constraint : pointJointConstraints )
    {
        int jointA = constraint.BodyAIndex( bodyStore );
        int jointB = constraint.BodyBIndex( bodyStore );

        if ( jointA < 0 || jointB < 0 )
        {
            continue;
        }

        if ( jointA > jointB )
        {
            std::swap( jointA, jointB );
        }

        if ( jointA == bodyA && jointB == bodyB )
        {
            return true;
        }
    }

    return false;
}

struct PointJointCandidatePairPredicate
{
    const Physics::PhysicsBodyStore& bodyStore;
    std::span<const Physics::PointJointConstraint> pointJointConstraints;

    bool operator()( const std::pair<int, int>& pair ) const
    {
        return IsPointJointCandidatePair( bodyStore, pointJointConstraints, pair );
    }
};

void TryRecordSleepPrunedCandidatePair( Physics::PhysicsPipelineTraceRecorder& physicsPipelineTrace,
                                        const Physics::PhysicsBodyHotFieldsConstView& hotFields,
                                        const std::pair<int, int>& pair )
{

    if ( !physicsPipelineTrace.CanRecord() )
    {
        return;
    }

    const int a = pair.first;
    const int b = pair.second;
    Physics::PhysicsPipelineRecord record;
    record.stage = Physics::PhysicsPipelineStage::SleepPrunedPair;
    record.bodyA = a;
    record.bodyB = b;
    record.point = ( Physics::PhysicsBodyPosition( hotFields, static_cast<size_t>( a ) ) +
                     Physics::PhysicsBodyPosition( hotFields, static_cast<size_t>( b ) ) ) *
                   0.5f;

    record.scalarA = 1.0f;
    physicsPipelineTrace.Record( record );
}

bool TryRecordBroadphaseCandidatePair( Physics::PhysicsPipelineTraceRecorder& physicsPipelineTrace,
                                       const Physics::PhysicsBodyHotFieldsConstView& hotFields, int modelCount,
                                       const std::pair<int, int>& pair, size_t candidateCount )
{

    if ( !physicsPipelineTrace.CanRecord() )
    {
        return false;
    }

    if ( pair.first < 0 || pair.second < 0 || pair.first >= modelCount || pair.second >= modelCount )
    {
        return true;
    }

    Physics::PhysicsPipelineRecord record;
    record.stage = Physics::PhysicsPipelineStage::BroadphaseCandidate;
    record.bodyA = pair.first;
    record.bodyB = pair.second;
    record.point = ( Physics::PhysicsBodyPosition( hotFields, static_cast<size_t>( pair.first ) ) +
                     Physics::PhysicsBodyPosition( hotFields, static_cast<size_t>( pair.second ) ) ) *
                   0.5f;

    const Vector3 delta = Physics::PhysicsBodyPosition( hotFields, static_cast<size_t>( pair.second ) ) -
                          Physics::PhysicsBodyPosition( hotFields, static_cast<size_t>( pair.first ) );

    const float deltaMag = Vector::VectorMag( delta );
    record.normal = deltaMag > TOLERANCE ? delta / deltaMag : Vector3( 0.0f, 1.0f, 0.0f );
    record.scalarA = static_cast<float>( candidateCount );
    physicsPipelineTrace.Record( record );
    return true;
}

#if defined( _DEBUG )
constexpr char PAIR_STREAM_ORACLE_FILE_MAGIC[8] = { 'S', 'K', 'O', 'R', 'E', 'B', 'D', '0' };
constexpr char PAIR_STREAM_ORACLE_TRAILER_MAGIC[8] = { 'S', 'K', 'O', 'R', 'E', 'E', 'N', 'D' };
constexpr char PAIR_STREAM_ORACLE_RECORD_MAGIC[8] = { 'S', 'K', 'O', 'R', 'E', 'P', 'A', 'S' };
constexpr char PAIR_STREAM_ORACLE_RECORD_END_MAGIC[8] = { 'S', 'K', 'O', 'R', 'E', 'P', 'E', 'N' };
constexpr uint32_t PAIR_STREAM_ORACLE_VERSION = 2u;

void WritePairStreamOracleBytes( std::FILE* file, const void* bytes, std::size_t byteCount, const char* field )
{

    if ( !file || std::fwrite( bytes, 1u, byteCount, file ) != byteCount )
    {
        SB_FATAL( "Physics/PairStreamOracle", "Pair-stream oracle write failed: field=%s bytes=%zu.", field, byteCount );
    }
}

void WritePairStreamOracleScalar( std::FILE* file, uint32_t value, const char* field )
{
    const uint8_t bytes[4] = {
        static_cast<uint8_t>( value ),
        static_cast<uint8_t>( value >> 8u ),
        static_cast<uint8_t>( value >> 16u ),
        static_cast<uint8_t>( value >> 24u ),
    };

    WritePairStreamOracleBytes( file, bytes, sizeof( bytes ), field );
}

void WritePairStreamOracleScalar( std::FILE* file, uint64_t value, const char* field )
{
    const uint8_t bytes[8] = {
        static_cast<uint8_t>( value ),        static_cast<uint8_t>( value >> 8u ),  static_cast<uint8_t>( value >> 16u ),
        static_cast<uint8_t>( value >> 24u ), static_cast<uint8_t>( value >> 32u ), static_cast<uint8_t>( value >> 40u ),
        static_cast<uint8_t>( value >> 48u ), static_cast<uint8_t>( value >> 56u ),
    };

    WritePairStreamOracleBytes( file, bytes, sizeof( bytes ), field );
}

void WritePairStreamOraclePairs( std::FILE* file, const Physics::PhysicsCandidatePairList& pairs, const char* field )
{
    constexpr std::size_t PAIRS_PER_WRITE = 512u;
    uint8_t encodedPairs[PAIRS_PER_WRITE * sizeof( int32_t ) * 2u] = {};

    for ( std::size_t firstPair = 0; firstPair < pairs.size(); firstPair += PAIRS_PER_WRITE )
    {
        const std::size_t pairCount = (std::min)( PAIRS_PER_WRITE, pairs.size() - firstPair );

        for ( std::size_t pairOffset = 0; pairOffset < pairCount; ++pairOffset )
        {
            const std::pair<int, int>& pair = pairs[firstPair + pairOffset];
            const uint32_t first = static_cast<uint32_t>( pair.first );
            const uint32_t second = static_cast<uint32_t>( pair.second );
            const std::size_t firstByte = pairOffset * sizeof( int32_t ) * 2u;
            encodedPairs[firstByte] = static_cast<uint8_t>( first );
            encodedPairs[firstByte + 1u] = static_cast<uint8_t>( first >> 8u );
            encodedPairs[firstByte + 2u] = static_cast<uint8_t>( first >> 16u );
            encodedPairs[firstByte + 3u] = static_cast<uint8_t>( first >> 24u );
            encodedPairs[firstByte + 4u] = static_cast<uint8_t>( second );
            encodedPairs[firstByte + 5u] = static_cast<uint8_t>( second >> 8u );
            encodedPairs[firstByte + 6u] = static_cast<uint8_t>( second >> 16u );
            encodedPairs[firstByte + 7u] = static_cast<uint8_t>( second >> 24u );
        }

        WritePairStreamOracleBytes( file, encodedPairs, pairCount * sizeof( int32_t ) * 2u, field );
    }
}

void CopyPairsWithoutGrowth( const Physics::PhysicsCandidatePairList& source,
                             Physics::PhysicsCandidatePairList& destination )
{
    destination.clear();

    if ( source.size() > destination.capacity() )
    {
        SB_FATAL( "Physics/BroadphaseOracle",
                  "Oracle normalization capacity exhausted: size=%zu capacity=%zu phase=diagnostic.", source.size(),
                  destination.capacity() );
    }

    for ( const std::pair<int, int>& pair : source )
    {
        destination.emplace_back( pair );
    }
}

void RequireSamePairMembership( const Physics::PhysicsCandidatePairList& driverPairs,
                                Physics::PhysicsCandidatePairList& shadowPairs,
                                Physics::PhysicsCandidatePairList& normalizedDriverPairs, const char* boundary,
                                const char* driverName, uint64_t tick )
{
    CopyPairsWithoutGrowth( driverPairs, normalizedDriverPairs );
    std::sort( normalizedDriverPairs.begin(), normalizedDriverPairs.end() );
    std::sort( shadowPairs.begin(), shadowPairs.end() );

    if ( normalizedDriverPairs.size() != shadowPairs.size() ||
         !std::equal( normalizedDriverPairs.begin(), normalizedDriverPairs.end(), shadowPairs.begin() ) )
    {
        SB_FATAL( "Physics/P1PairOracle",
                  "P1 same-state pair membership mismatch: boundary=%s driver=%s tick=%llu "
                  "driver_count=%zu shadow_count=%zu.",
                  boundary, driverName, static_cast<unsigned long long>( tick ), normalizedDriverPairs.size(),
                  shadowPairs.size() );
    }
}
#endif

template <typename T> uint64_t ListCapacityBytes( const T& values )
{
    return static_cast<uint64_t>( values.capacity() ) * static_cast<uint64_t>( sizeof( typename T::value_type ) );
}
} // namespace

namespace SkullbonezCore
{
namespace Physics
{
PhysicsBroadphaseStage::PhysicsBroadphaseStage() : m_spatialGrid( DEFAULT_BROADPHASE_CELL )
{
#if defined( _DEBUG )
    char oracleDriver[16] = {};
    size_t oracleDriverLength = 0;
    getenv_s( &oracleDriverLength, oracleDriver, sizeof( oracleDriver ), "SKORE_P1_PAIR_DRIVER" );

    if ( oracleDriverLength > sizeof( oracleDriver ) )
    {
        SB_FATAL( "Physics/P1PairOracle", "SKORE_P1_PAIR_DRIVER value exceeds the fixed diagnostic buffer." );
    }

    if ( oracleDriverLength > 0 )
    {
        m_pairOracleLegacyDrives = std::strcmp( oracleDriver, "legacy" ) == 0;
        m_pairOracleEnabled = m_pairOracleLegacyDrives || std::strcmp( oracleDriver, "canonical" ) == 0;

        if ( !m_pairOracleEnabled )
        {
            SB_FATAL( "Physics/P1PairOracle", "Unknown SKORE_P1_PAIR_DRIVER value '%s'; expected legacy or canonical.",
                      oracleDriver );
        }

        std::fprintf( stderr, "P1_PAIR_ORACLE enabled driver=%s boundaries=raw,final\n",
                      m_pairOracleLegacyDrives ? "legacy" : "canonical" );

        std::fflush( stderr );
    }

    char pairStreamOraclePath[512] = {};
    size_t pairStreamOraclePathLength = 0;
    const errno_t pairStreamOraclePathRead = getenv_s( &pairStreamOraclePathLength, pairStreamOraclePath,
                                                       sizeof( pairStreamOraclePath ), "SKORE_PAIR_STREAM_ORACLE" );

    if ( pairStreamOraclePathRead != 0 || pairStreamOraclePathLength > sizeof( pairStreamOraclePath ) )
    {
        SB_FATAL( "Physics/PairStreamOracle", "SKORE_PAIR_STREAM_ORACLE path exceeds the fixed diagnostic buffer." );
    }

    if ( pairStreamOraclePathLength > 0 )
    {

        // Lifetime: one process can construct live, prediction, and replay
        // worlds. Give every owner a deterministic suffix so their records can
        // never interleave; the run receipt selects the stream with the expected
        // body/pass facts instead of assuming construction order is semantic.
        static std::atomic_uint32_t nextPairStreamOracleInstance { 0u };
        const uint32_t pairStreamOracleInstance = nextPairStreamOracleInstance.fetch_add( 1u, std::memory_order_relaxed );
        char resolvedPairStreamOraclePath[560] = {};
        sprintf_s( resolvedPairStreamOraclePath, "%s.%u.bin", pairStreamOraclePath, pairStreamOracleInstance );
        m_pairStreamOracleEnabled = true;

        if ( fopen_s( &m_pairStreamOracleFile, resolvedPairStreamOraclePath, "wb" ) != 0 || !m_pairStreamOracleFile )
        {
            SB_FATAL( "Physics/PairStreamOracle", "Unable to open pair-stream oracle path '%s'.",
                      resolvedPairStreamOraclePath );
        }

        // Hazard: stdio may otherwise allocate its buffer on the first gameplay
        // write. Make the cold-opened diagnostic stream unbuffered.

        if ( std::setvbuf( m_pairStreamOracleFile, nullptr, _IONBF, 0 ) != 0 )
        {
            SB_FATAL( "Physics/PairStreamOracle", "Unable to disable stdio buffering: path='%s' phase=open.",
                      resolvedPairStreamOraclePath );
        }

        WritePairStreamOracleBytes( m_pairStreamOracleFile, PAIR_STREAM_ORACLE_FILE_MAGIC,
                                    sizeof( PAIR_STREAM_ORACLE_FILE_MAGIC ), "file_magic" );

        WritePairStreamOracleScalar( m_pairStreamOracleFile, PAIR_STREAM_ORACLE_VERSION, "version" );
        const uint32_t encodedPairBytes = sizeof( int32_t ) * 2u;
        WritePairStreamOracleScalar( m_pairStreamOracleFile, encodedPairBytes, "encoded_pair_bytes" );

        std::fprintf( stderr, "PAIR_STREAM_ORACLE enabled path=%s format=%u\n", resolvedPairStreamOraclePath,
                      PAIR_STREAM_ORACLE_VERSION );

        std::fflush( stderr );
    }

    if ( m_pairStreamOracleEnabled && m_pairOracleEnabled )
    {
        SB_FATAL( "Physics/PairStreamOracle", "SKORE_PAIR_STREAM_ORACLE cannot run with SKORE_P1_PAIR_DRIVER." );
    }
#endif
}


#if defined( _DEBUG )
PhysicsBroadphaseStage::~PhysicsBroadphaseStage()
{

    if ( m_pairStreamOracleFile )
    {

        if ( m_pairStreamOracleRecordStartOffset != 0u )
        {
            SB_FATAL( "Physics/PairStreamOracle", "Pair-stream oracle closed with an incomplete pass record." );
        }

        const __int64 contentBytes = _ftelli64( m_pairStreamOracleFile );

        if ( contentBytes < 0 )
        {
            SB_FATAL( "Physics/PairStreamOracle", "Pair-stream oracle size query failed." );
        }

        WritePairStreamOracleBytes( m_pairStreamOracleFile, PAIR_STREAM_ORACLE_TRAILER_MAGIC,
                                    sizeof( PAIR_STREAM_ORACLE_TRAILER_MAGIC ), "trailer_magic" );

        WritePairStreamOracleScalar( m_pairStreamOracleFile, m_pairStreamOraclePassCount, "trailer_pass_count" );
        const uint64_t contentByteCount = static_cast<uint64_t>( contentBytes );
        WritePairStreamOracleScalar( m_pairStreamOracleFile, contentByteCount, "trailer_content_bytes" );
        const int closeResult = std::fclose( m_pairStreamOracleFile );
        m_pairStreamOracleFile = nullptr;

        if ( closeResult != 0 )
        {
            SB_FATAL( "Physics/PairStreamOracle", "Pair-stream oracle finalization failed: phase=close." );
        }
    }
}
#endif

void PhysicsBroadphaseStage::ReserveSceneCapacity( std::size_t bodyCapacity )
{
    m_spatialGrid.ReserveSceneCapacity( bodyCapacity );
    const std::size_t pairCapacity = PhysicsCandidatePairCapacity( bodyCapacity );
    m_candidatePairs.Reserve( pairCapacity );
    m_collisionCellKeys.Reserve( pairCapacity );
#if defined( _DEBUG )
    m_sleepPrunedPairs.Reserve( pairCapacity );
    m_pairOracleShadowPairs.Reserve( pairCapacity );
    m_pairOracleNormalizedDriverPairs.Reserve( pairCapacity );

    if ( m_pairStreamOracleEnabled )
    {
        m_pairStreamOracleBodyCapacity = (std::max)( m_pairStreamOracleBodyCapacity, bodyCapacity );
    }
#endif
}


#if defined( _DEBUG )
void PhysicsBroadphaseStage::WritePairStreamOraclePrePruneRecord( int modelCount, uint64_t totalGeometryInvocations )
{

    if ( !m_pairStreamOracleEnabled )
    {
        return;
    }

    const uint64_t bodyCount = static_cast<uint64_t>( modelCount );
    const uint64_t pairIdentities = bodyCount > 1u ? bodyCount * ( bodyCount - 1u ) / 2u : 0u;
    const uint64_t clearedWords = ( pairIdentities + 63u ) / 64u;
    const uint64_t explicitMemsetBytes = clearedWords * sizeof( uint64_t );
    const uint64_t pairSeenCommittedBytes = static_cast<uint64_t>( m_spatialGrid.GetPairDedupWordCapacity() ) *
                                            sizeof( uint64_t );

    const uint32_t gridCandidateCount = static_cast<uint32_t>( m_pairOracleNormalizedDriverPairs.size() );
    const uint32_t augmentedCandidateCount = static_cast<uint32_t>( m_candidatePairs.size() );
    const uint32_t rawSleepPrunedCount = static_cast<uint32_t>( m_sleepPrunedPairs.size() );

    const __int64 recordStart = _ftelli64( m_pairStreamOracleFile );

    if ( recordStart < 0 )
    {
        SB_FATAL( "Physics/PairStreamOracle", "Pair-stream oracle record offset query failed." );
    }

    m_pairStreamOracleRecordStartOffset = static_cast<uint64_t>( recordStart );

    // Version 2 is an explicitly little-endian stream. Each pass has its own
    // magic, length, and ordinal footer, so a parser detects truncation or count
    // drift before accepting any byte-exact equivalence result.
    WritePairStreamOracleBytes( m_pairStreamOracleFile, PAIR_STREAM_ORACLE_RECORD_MAGIC,
                                sizeof( PAIR_STREAM_ORACLE_RECORD_MAGIC ), "record_magic" );

    WritePairStreamOracleScalar( m_pairStreamOracleFile, m_pairStreamOraclePassCount, "pass_ordinal" );
    WritePairStreamOracleScalar( m_pairStreamOracleFile, bodyCount, "body_count" );
    const uint64_t reservedBodyCapacity = static_cast<uint64_t>( m_pairStreamOracleBodyCapacity );
    WritePairStreamOracleScalar( m_pairStreamOracleFile, reservedBodyCapacity, "reserved_body_capacity" );
    WritePairStreamOracleScalar( m_pairStreamOracleFile, clearedWords, "cleared_words" );
    WritePairStreamOracleScalar( m_pairStreamOracleFile, explicitMemsetBytes, "explicit_memset_bytes" );
    WritePairStreamOracleScalar( m_pairStreamOracleFile, pairSeenCommittedBytes, "pair_seen_committed_bytes" );
    WritePairStreamOracleScalar( m_pairStreamOracleFile, m_pairStreamOracleGridGeometryInvocations,
                                 "grid_geometry_invocations" );

    WritePairStreamOracleScalar( m_pairStreamOracleFile, totalGeometryInvocations, "total_geometry_invocations" );
    WritePairStreamOracleScalar( m_pairStreamOracleFile, gridCandidateCount, "grid_candidate_count" );
    WritePairStreamOracleScalar( m_pairStreamOracleFile, augmentedCandidateCount, "augmented_candidate_count" );
    WritePairStreamOracleScalar( m_pairStreamOracleFile, rawSleepPrunedCount, "raw_sleep_pruned_count" );
    WritePairStreamOraclePairs( m_pairStreamOracleFile, m_pairOracleNormalizedDriverPairs, "grid_candidate_pairs" );
    WritePairStreamOraclePairs( m_pairStreamOracleFile, m_candidatePairs, "augmented_candidate_pairs" );
    WritePairStreamOraclePairs( m_pairStreamOracleFile, m_sleepPrunedPairs, "raw_sleep_pruned_pairs" );
}

void PhysicsBroadphaseStage::WritePairStreamOraclePostPruneRecord()
{

    if ( !m_pairStreamOracleEnabled )
    {
        return;
    }

    const uint32_t finalCandidateCount = static_cast<uint32_t>( m_candidatePairs.size() );
    const uint32_t finalSleepPrunedCount = static_cast<uint32_t>( m_sleepPrunedPairs.size() );
    WritePairStreamOracleScalar( m_pairStreamOracleFile, finalCandidateCount, "final_candidate_count" );
    WritePairStreamOracleScalar( m_pairStreamOracleFile, finalSleepPrunedCount, "final_sleep_pruned_count" );
    WritePairStreamOraclePairs( m_pairStreamOracleFile, m_candidatePairs, "final_candidate_pairs" );
    WritePairStreamOraclePairs( m_pairStreamOracleFile, m_sleepPrunedPairs, "final_sleep_pruned_pairs" );

    const __int64 recordContentEnd = _ftelli64( m_pairStreamOracleFile );

    if ( recordContentEnd < 0 || static_cast<uint64_t>( recordContentEnd ) < m_pairStreamOracleRecordStartOffset )
    {
        SB_FATAL( "Physics/PairStreamOracle", "Pair-stream oracle record size query failed." );
    }

    const uint64_t recordContentBytes = static_cast<uint64_t>( recordContentEnd ) - m_pairStreamOracleRecordStartOffset;
    WritePairStreamOracleBytes( m_pairStreamOracleFile, PAIR_STREAM_ORACLE_RECORD_END_MAGIC,
                                sizeof( PAIR_STREAM_ORACLE_RECORD_END_MAGIC ), "record_end_magic" );

    WritePairStreamOracleScalar( m_pairStreamOracleFile, m_pairStreamOraclePassCount, "record_end_ordinal" );
    WritePairStreamOracleScalar( m_pairStreamOracleFile, recordContentBytes, "record_content_bytes" );
    m_pairStreamOracleRecordStartOffset = 0u;
    ++m_pairStreamOraclePassCount;
}
#endif


void PhysicsBroadphaseStage::ApplyRuntimeSettings( const BroadphaseSettings& settings )
{
    const float configuredCell = (std::max)( BROADPHASE_MIN_CELL_SIZE, settings.cellSize );

    if ( configuredCell != m_spatialGrid.GetCellSize() )
    {
        m_gridMembershipSeeded = false;
    }

    m_spatialGrid.SetCellSize( configuredCell );
}


void PhysicsBroadphaseStage::Clear()
{
    m_candidatePairs.clear();
    m_collisionCellKeys.clear();
    m_spatialGrid.Clear();
    m_gridMembershipSeeded = false;
    m_gridMembershipBodyCount = 0;
    m_largestBroadphaseRadius = 0.0f;
    m_largestBroadphaseRadiusValid = false;
#if defined( _DEBUG )
    m_sleepPrunedPairs.clear();
    m_pairOracleShadowPairs.clear();
    m_pairOracleNormalizedDriverPairs.clear();
    m_pairOracleTickCount = 0;
#endif
}


void PhysicsBroadphaseStage::InvalidateBodyTopology()
{

    // Cold authored mutations may preserve body count while replacing a dense
    // row. The next Run refreshes every range in-place; retaining the fixed grid
    // avoids an O(table capacity) clear for each body in a replay restore batch.
    m_candidatePairs.clear();
    m_collisionCellKeys.clear();
    m_gridMembershipSeeded = false;
    m_gridMembershipBodyCount = 0;
    m_largestBroadphaseRadius = 0.0f;
    m_largestBroadphaseRadiusValid = false;
#if defined( _DEBUG )
    m_sleepPrunedPairs.clear();
#endif
}


void PhysicsBroadphaseStage::ResetTransientAfterReplayRestore()
{

    // Invariant: replay restores collision-cell diagnostic keys from the
    // snapshot, while candidate pairs and grid buckets are rebuilt next tick.
    m_candidatePairs.clear();
    m_spatialGrid.Clear();
    m_gridMembershipSeeded = false;
    m_gridMembershipBodyCount = 0;
    m_largestBroadphaseRadius = 0.0f;
    m_largestBroadphaseRadiusValid = false;
#if defined( _DEBUG )
    m_sleepPrunedPairs.clear();
    m_pairOracleShadowPairs.clear();
    m_pairOracleNormalizedDriverPairs.clear();
#endif
}


std::span<const std::pair<int, int>> PhysicsBroadphaseStage::Run( const PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore, const BroadphaseSettings& broadphaseSettings,
                                                                  std::span<const PointJointConstraint> pointJointConstraints, std::span<const uint8_t> sleepState,
                                                                  std::span<const int> awakeBodyIndices, PhysicsStepDiagnostics& stepDiagnostics, float dt, float contactSkin,
                                                                  float contactEpsilon, Core::Profiler* profiler )
{
    PROFILE_BEGIN( profiler, "Frame/Physics/Broadphase" );
    const std::span<const PhysicsBodyRecord> bodyRecords = bodyStore.Records();
    const PhysicsBodyHotFieldsConstView hotFields = bodyStore.HotFields();
    const std::span<const ColliderRecord> colliderRecords = colliderStore.Records();
    const int modelCount = (std::min)( { bodyStore.Count(), static_cast<int>( bodyRecords.size() ),
                                         static_cast<int>( colliderRecords.size() ) } );

    auto& physicsPipelineTrace = stepDiagnostics.MutablePipelineTraceRecorder();

    {

        // Invariant: Broadphase is the inclusive owner marker. Every direct
        // child below is mutually exclusive so reports can sum children once
        // without adding a nested interval a second time.
        PROFILE_SCOPED( profiler, "Frame/Physics/Broadphase/GridSetup" );

        if ( !m_largestBroadphaseRadiusValid )
        {

            // Cold topology boundary: collider radii do not change during a
            // fixed step, so the scene-wide maximum is not an all-body hot pass.
            m_largestBroadphaseRadius = 0.0f;

            for ( int bodyIndex = 0; bodyIndex < modelCount; ++bodyIndex )
            {
                const float radius = SolverBodyRadius( colliderRecords, bodyIndex );

                if ( std::isfinite( radius ) && radius > m_largestBroadphaseRadius )
                {
                    m_largestBroadphaseRadius = radius;
                }
            }

            m_largestBroadphaseRadiusValid = true;
        }

        // Why: a fixed 24m cell made the 200-brick wall share huge buckets.
        // Deterministic scene inputs choose a cell no larger than the config cap.
        const float configuredCell = (std::max)( BROADPHASE_MIN_CELL_SIZE, broadphaseSettings.cellSize );
        const float sceneCell = (std::max)( BROADPHASE_MIN_CELL_SIZE, ( m_largestBroadphaseRadius + contactSkin ) * 2.0f );

        const float selectedCellSize = (std::min)( configuredCell, sceneCell );

        if ( selectedCellSize != m_spatialGrid.GetCellSize() )
        {
            m_gridMembershipSeeded = false;
        }

        m_spatialGrid.SetCellSize( selectedCellSize );
        m_spatialGrid.BeginFrame( modelCount );
        m_collisionCellKeys.clear();
    }
    {
        PROFILE_SCOPED( profiler, "Frame/Physics/Broadphase/GridMaintain" );
        const bool fullSeed = !m_gridMembershipSeeded || m_gridMembershipBodyCount != modelCount;
        auto maintainBody = [&]( int bodyIndex, bool isAwakeSource )
        {
            const float radius = SolverBodyRadius( colliderRecords, bodyIndex ) + contactSkin;

            const Vector3 displacement = PhysicsBodyLinearVelocity( hotFields, static_cast<size_t>( bodyIndex ) ) * dt;

            const float displacementSq = Vector::VectorMagSquared( displacement );

            if ( isAwakeSource && displacementSq > radius * radius )
            {
                m_spatialGrid.InsertSwept( bodyIndex, SolverBodyPosition( hotFields, bodyIndex ), displacement, radius );
            }
            else
            {
                m_spatialGrid.Insert( bodyIndex, SolverBodyPosition( hotFields, bodyIndex ), radius );
            }

            if ( isAwakeSource )
            {
                m_spatialGrid.MarkPairSourceCells( bodyIndex );
            }
        };

        if ( fullSeed )
        {

            // Cold boundary: seed every persistent membership once, but stamp
            // only awake dynamic bodies as this frame's pair-work sources.

            for ( int bodyIndex = 0; bodyIndex < modelCount; ++bodyIndex )
            {
                const bool isAwakeSource = !IsSolverBodyFixed( hotFields, bodyIndex ) &&
                                           sleepState[static_cast<size_t>( bodyIndex )] == 0u;

                maintainBody( bodyIndex, isAwakeSource );
            }

            m_gridMembershipSeeded = true;
            m_gridMembershipBodyCount = modelCount;
        }
        else
        {

            // P3 invariant: sleepers keep their last persistent range. Only
            // awake bodies can move, sweep, or source new narrowphase work.

            for ( int bodyIndex : awakeBodyIndices )
            {
                maintainBody( bodyIndex, true );
            }
        }
    }
    {
        PROFILE_SCOPED( profiler, "Frame/Physics/Broadphase/CandidatePairs" );
#if defined( _DEBUG )
        m_sleepPrunedPairs.clear();

        if ( m_pairStreamOracleEnabled )
        {
            ResetBroadphaseCandidateGeometryInvocationCount();
        }

        if ( m_pairOracleEnabled )
        {

            if ( m_pairOracleLegacyDrives )
            {
                m_spatialGrid.GetFilteredCandidatePairsLegacyForOracle( m_candidatePairs, bodyStore, colliderStore,
                                                                        sleepState, dt, contactSkin );

                m_spatialGrid.GetFilteredCandidatePairs( m_pairOracleShadowPairs, bodyStore, colliderStore, sleepState, dt,
                                                         contactSkin, m_sleepPrunedPairs, false );
            }
            else
            {
                m_spatialGrid.GetFilteredCandidatePairs( m_candidatePairs, bodyStore, colliderStore, sleepState, dt,
                                                         contactSkin, m_sleepPrunedPairs, false );

                m_spatialGrid.GetFilteredCandidatePairsLegacyForOracle( m_pairOracleShadowPairs, bodyStore, colliderStore,
                                                                        sleepState, dt, contactSkin );
            }

            RequireSamePairMembership( m_candidatePairs, m_pairOracleShadowPairs, m_pairOracleNormalizedDriverPairs, "raw",
                                       m_pairOracleLegacyDrives ? "legacy" : "canonical", m_pairOracleTickCount );
        }
        else
#endif
        {
#if defined( _DEBUG )

            // Debug walks the full retained grid to preserve one bounded
            // SleepPrunedPair breadcrumb per old sleep-only pair.
            m_spatialGrid.GetFilteredCandidatePairs( m_candidatePairs, bodyStore, colliderStore, sleepState, dt, contactSkin,
                                                     m_sleepPrunedPairs, false );
#else

            // Production visits only cells reached by an awake body this step;
            // sleep-only cells retain membership but emit no candidate work.
            m_spatialGrid.GetFilteredCandidatePairs( m_candidatePairs, bodyStore, colliderStore, sleepState, dt, contactSkin,
                                                     true );
#endif
        }

#if defined( _DEBUG )

        if ( m_pairStreamOracleEnabled )
        {

            // The pair-stream and P1 same-state oracles are mutually exclusive,
            // so the already-reserved normalization scratch can snapshot raw
            // grid candidates without registering another Debug list owner.
            CopyPairsWithoutGrowth( m_candidatePairs, m_pairOracleNormalizedDriverPairs );
            m_pairStreamOracleGridGeometryInvocations = BroadphaseCandidateGeometryInvocationCount();
        }
#endif
    }

    bool fastSmallSweepAppendedPairs = false;
    {
        PROFILE_SCOPED( profiler, "Frame/Physics/Broadphase/FastSmallSweepAugment" );
        fastSmallSweepAppendedPairs = AppendFastSmallSweepPairs( m_candidatePairs, bodyStore, colliderStore, sleepState,
                                                                 hotFields, colliderRecords, awakeBodyIndices, dt,
                                                                 contactSkin, contactEpsilon );

#if defined( _DEBUG )

        if ( m_pairOracleEnabled )
        {
            AppendFastSmallSweepPairs( m_pairOracleShadowPairs, bodyStore, colliderStore, sleepState, hotFields,
                                       colliderRecords, awakeBodyIndices, dt, contactSkin, contactEpsilon );
        }
#endif
    }

#if defined( _DEBUG )

    if ( ( !m_pairOracleEnabled || !m_pairOracleLegacyDrives ) && fastSmallSweepAppendedPairs )
#endif
#if !defined( _DEBUG )

        if ( fastSmallSweepAppendedPairs )
#endif
        {
            CanonicalizeCandidatePairs( m_candidatePairs );
        }

#if defined( _DEBUG )

    if ( m_pairStreamOracleEnabled )
    {
        WritePairStreamOraclePrePruneRecord( modelCount, BroadphaseCandidateGeometryInvocationCount() );
    }
#endif

    {
        PROFILE_SCOPED( profiler, "Frame/Physics/Broadphase/PruneFixedPairs" );
        m_candidatePairs.erase( std::remove_if( m_candidatePairs.begin(), m_candidatePairs.end(),
                                                FixedSolverCandidatePairPredicate { hotFields, modelCount } ),
                                m_candidatePairs.end() );

#if defined( _DEBUG )
        m_sleepPrunedPairs.erase( std::remove_if( m_sleepPrunedPairs.begin(), m_sleepPrunedPairs.end(),
                                                  FixedSolverCandidatePairPredicate { hotFields, modelCount } ),
                                  m_sleepPrunedPairs.end() );

        if ( m_pairOracleEnabled )
        {
            m_pairOracleShadowPairs.erase( std::remove_if( m_pairOracleShadowPairs.begin(), m_pairOracleShadowPairs.end(),
                                                           FixedSolverCandidatePairPredicate { hotFields, modelCount } ),
                                           m_pairOracleShadowPairs.end() );
        }
#endif
    }

    if ( !pointJointConstraints.empty() )
    {
        PROFILE_SCOPED( profiler, "Frame/Physics/Broadphase/PruneJointPairs" );
        m_candidatePairs.erase( std::remove_if( m_candidatePairs.begin(), m_candidatePairs.end(),
                                                PointJointCandidatePairPredicate { bodyStore, pointJointConstraints } ),
                                m_candidatePairs.end() );

#if defined( _DEBUG )

        if ( m_pairOracleEnabled )
        {
            m_pairOracleShadowPairs.erase( std::remove_if( m_pairOracleShadowPairs.begin(), m_pairOracleShadowPairs.end(),
                                                           PointJointCandidatePairPredicate { bodyStore,
                                                                                              pointJointConstraints } ),
                                           m_pairOracleShadowPairs.end() );
        }
#endif

#if defined( _DEBUG )
        m_sleepPrunedPairs.erase( std::remove_if( m_sleepPrunedPairs.begin(), m_sleepPrunedPairs.end(),
                                                  PointJointCandidatePairPredicate { bodyStore, pointJointConstraints } ),
                                  m_sleepPrunedPairs.end() );
#endif
    }

#if defined( _DEBUG )

    if ( m_pairOracleEnabled )
    {
        RequireSamePairMembership( m_candidatePairs, m_pairOracleShadowPairs, m_pairOracleNormalizedDriverPairs, "final",
                                   m_pairOracleLegacyDrives ? "legacy" : "canonical", m_pairOracleTickCount );

        ++m_pairOracleTickCount;

        if ( ( m_pairOracleTickCount % 120u ) == 0u )
        {
            std::fprintf( stderr, "P1_PAIR_ORACLE pass driver=%s ticks=%llu boundaries=raw,final\n",
                          m_pairOracleLegacyDrives ? "legacy" : "canonical",
                          static_cast<unsigned long long>( m_pairOracleTickCount ) );

            std::fflush( stderr );
        }
    }

    if ( m_pairStreamOracleEnabled )
    {
        WritePairStreamOraclePostPruneRecord();
    }
#endif

    {
        PROFILE_SCOPED( profiler, "Frame/Physics/Broadphase/RecordCandidates" );

        // Why: every retained candidate already passed the pair-validity gate,
        // so count-only mode can batch the canonical event cardinality without
        // loading either body's position or comparing capacity per pair.

        if ( !physicsPipelineTrace.RetainsFullRecords() )
        {
            std::size_t pipelineEventCount = m_candidatePairs.size();
#if defined( _DEBUG )
            pipelineEventCount += m_sleepPrunedPairs.size();
#endif
            physicsPipelineTrace.RecordEvents( pipelineEventCount );
        }
        else
        {
#if defined( _DEBUG )

            // Compatibility invariant: P2 recorded the canonical geometrically
            // admitted stream before removing sleep-only pairs. Reconstruct that
            // Debug trace by merging the two retained sorted lists; this does not
            // restore dormant solver work to the production candidate vector.
            std::sort( m_sleepPrunedPairs.begin(), m_sleepPrunedPairs.end() );
            const size_t diagnosticCandidateCount = m_candidatePairs.size() + m_sleepPrunedPairs.size();
            auto visible = m_candidatePairs.begin();
            auto pruned = m_sleepPrunedPairs.begin();

            while ( visible != m_candidatePairs.end() || pruned != m_sleepPrunedPairs.end() )
            {
                const bool takePruned = visible == m_candidatePairs.end() ||
                                        ( pruned != m_sleepPrunedPairs.end() && *pruned < *visible );

                const std::pair<int, int>& pair = takePruned ? *pruned++ : *visible++;

                if ( !TryRecordBroadphaseCandidatePair( physicsPipelineTrace, hotFields, modelCount, pair,
                                                        diagnosticCandidateCount ) )
                {
                    break;
                }
            }
#else

            for ( const auto& pair : m_candidatePairs )
            {

                if ( !TryRecordBroadphaseCandidatePair( physicsPipelineTrace, hotFields, modelCount, pair,
                                                        m_candidatePairs.size() ) )
                {
                    break;
                }
            }
#endif
        }
    }
#if defined( _DEBUG )
    {
        PROFILE_SCOPED( profiler, "Frame/Physics/Broadphase/RecordSleepPrunedPairs" );

        // The production path never walks sleep-only cells. Debug retains the
        // old diagnostic evidence at the earlier emission skip instead of
        // paying for a solver-visible list followed by a prune pass.

        if ( !physicsPipelineTrace.RetainsFullRecords() )
        {
            physicsPipelineTrace.RecordEvents( m_sleepPrunedPairs.size() );
        }
        else
        {

            for ( const std::pair<int, int>& pair : m_sleepPrunedPairs )
            {
                TryRecordSleepPrunedCandidatePair( physicsPipelineTrace, hotFields, pair );
            }
        }
    }
#endif
    PROFILE_END( profiler, "Frame/Physics/Broadphase" );
    return m_candidatePairs;
}


const Math::CollisionDetection::SpatialGrid& PhysicsBroadphaseStage::GetSpatialGrid() const
{
    return m_spatialGrid;
}


float PhysicsBroadphaseStage::GetCellSize() const
{
    return m_spatialGrid.GetCellSize();
}


std::span<const std::pair<int, int>> PhysicsBroadphaseStage::GetCandidatePairs() const
{
    return m_candidatePairs;
}


std::span<const int64_t> PhysicsBroadphaseStage::GetCollisionCellKeys() const
{
    return m_collisionCellKeys;
}


std::span<const int64_t> PhysicsBroadphaseStage::CollisionCellKeysForReplay() const
{
    return m_collisionCellKeys;
}


PhysicsCollisionCellKeyList& PhysicsBroadphaseStage::CollisionCellKeysForReplay()
{
    return m_collisionCellKeys;
}


void PhysicsBroadphaseStage::AppendCollisionCellKey( int64_t collisionCellKey )
{

    if ( m_collisionCellKeys.size() >= m_collisionCellKeys.capacity() )
    {
        assert( false && "Physics collision-cell key capacity exceeded" );

        // Invariant: collision-cell diagnostics share the fixed candidate-pair
        // event budget; overflow would lose deterministic evidence.
        SB_FATAL( "Physics/PhysicsWorld", "Physics collision-cell key capacity exceeded" );
    }

    m_collisionCellKeys.push_back( collisionCellKey );
}


uint64_t PhysicsBroadphaseStage::CollectDynamicMemoryBytes() const
{

    // Invariant: this is the owning contribution used by PhysicsWorld's total.
    // SpatialGrid's inline control/topology is already inside sizeof(PhysicsWorld);
    // its registered backing must be added here exactly once.
    uint64_t bytes = m_spatialGrid.CollectDynamicMemoryBytes() + ListCapacityBytes( m_candidatePairs ) +
                     ListCapacityBytes( m_collisionCellKeys );

#if defined( _DEBUG )
    bytes += ListCapacityBytes( m_sleepPrunedPairs ) + ListCapacityBytes( m_pairOracleShadowPairs ) +
             ListCapacityBytes( m_pairOracleNormalizedDriverPairs );
#endif
    return bytes;
}


uint64_t PhysicsBroadphaseStage::CollectDebugAndBroadphaseMemoryBytes() const
{

    // Historical diagnostic subset: include the grid's inline bytes plus the
    // same owning dynamic contribution, but do not add this subset to totals.
    return static_cast<uint64_t>( sizeof( m_spatialGrid ) ) + CollectDynamicMemoryBytes();
}
} // namespace Physics
} // namespace SkullbonezCore
