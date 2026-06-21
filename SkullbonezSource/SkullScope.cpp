/*
File: SkullbonezSource/SkullScope.cpp
Purpose:
  Defines compact physics diagnostics records emitted for SkullScope queries.

Mental model:
  This module is one piece of the engine contract. Read the glossary and
  invariants first, then follow ownership and call direction through the
  related files.

Glossary:
  SkullScope: Queryable physics diagnostics workflow backed by bounded trace
  output and local queries.
  CSV (Comma-Separated Values): Text table format used for byte-exact physics
  regression output.

Related:
  - SkullbonezSource/SkullScope.h
  - Agentic/Reference/comment-style-guide.md
*/
#include "SkullScope.h"

#ifdef _DEBUG

#include "GameModelCollection.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>


using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::GameObjects;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Vector;
namespace Vector = SkullbonezCore::Math::Vector;


namespace
{
std::string EscapeSkullScopeJson( const char* value )
{
    std::string escaped;
    if ( !value )
    {
        return escaped;
    }

    for ( const char* p = value; *p != '\0'; ++p )
    {
        switch ( *p )
        {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped += *p;
            break;
        }
    }
    return escaped;
}
} // namespace


// SkullScope writes append-only NDJSON rows for agent/query tooling.  It samples
// the same retained buffers used by physics visualization and sleep solving, so
// model-facing analysis can ask bounded questions without ingesting the legacy
// per-body CSV artifact.
void SkullScope::SetPath( const char* path )
{
    strcpy_s( m_physicsDiagnosticsPath, sizeof( m_physicsDiagnosticsPath ), path ? path : "" );
    m_physicsDiagnosticsFrame = 0;
    ResetPenetrationState();
}


void SkullScope::SetRunId( const char* runId )
{
    strcpy_s( m_physicsDiagnosticsRunId, sizeof( m_physicsDiagnosticsRunId ), runId ? runId : "" );
    ResetRunState();
}


void SkullScope::ResetRunState()
{
    m_physicsDiagnosticsFrame = 0;
    m_physicsDiagnosticsEventCounter = 0;
    m_physicsDiagnosticsTimeSeconds = 0.0;
    m_physicsDiagnosticsPrevEnergy = 0.0;
    m_physicsDiagnosticsHasPrevEnergy = false;
    ResetPenetrationState();
}


void SkullScope::ResetPenetrationState()
{
    m_physicsDiagnosticsPenetrationContact[0] = '\0';
    m_physicsDiagnosticsPenetrationFrames = 0;
    m_physicsDiagnosticsPenetrationGrowthFrames = 0;
    m_physicsDiagnosticsPenetrationWindowStart = 0.0;
    m_physicsDiagnosticsPrevPenetration = 0.0;
    m_physicsDiagnosticsPenetrationSustainedReported = false;
    m_physicsDiagnosticsPenetrationGrowingReported = false;
}


void SkullScope::EmitFrame( GameModelCollection& collection, float dt )
{
    if ( m_physicsDiagnosticsPath[0] == '\0' || m_physicsDiagnosticsRunId[0] == '\0' )
    {
        return;
    }

    auto& m_gameModels = collection.m_gameModels;
    auto& physicsWorld = collection.m_physicsWorld;
    auto& m_persistentContacts = physicsWorld.m_persistentContacts;
    auto& m_persistentContactSolverStats = physicsWorld.m_persistentContactSolverStats;
    auto& m_sleepIslandParent = physicsWorld.m_sleepIslandParent;
    auto& m_sleepSupportedThisFrame = physicsWorld.m_sleepSupportedThisFrame;
    auto& m_sleepInhibitedThisFrame = physicsWorld.m_sleepInhibitedThisFrame;
    auto& m_sleepState = physicsWorld.m_sleepState;
    auto& m_sleepCounter = physicsWorld.m_sleepCounter;
    auto& m_sleepIslandEligible = physicsWorld.m_sleepIslandEligible;
    auto& m_sleepIslandCanSleep = physicsWorld.m_sleepIslandCanSleep;
    auto& m_spatialGrid = physicsWorld.m_spatialGrid;
    auto& m_candidatePairs = physicsWorld.m_candidatePairs;
    auto& m_collisionCellKeys = physicsWorld.m_collisionCellKeys;
    auto& m_sleepSupportEdges = physicsWorld.m_sleepSupportEdges;
    auto& m_sleepIslandVisualId = physicsWorld.m_sleepIslandVisualId;
    auto& m_physicsPipelineTrace = physicsWorld.m_physicsPipelineTrace;
    auto& m_terrainContactManifolds = physicsWorld.m_terrainContactManifolds;

    // Frame rows summarize the whole physics island graph, not just individual
    // bodies.  The query layer uses these aggregate maxima/counts to decide which
    // body/contact/island details are worth expanding in a follow-up query.
    const int frame = m_physicsDiagnosticsFrame;
    const int modelCount = static_cast<int>( m_gameModels.size() );
    int awakeCount = 0;
    int sleepingCount = 0;
    int supportedCount = 0;
    int inhibitedCount = 0;
    int maxSpeedBody = -1;
    int maxOmegaBody = -1;
    double totalLinearEnergy = 0.0;
    double totalAngularEnergy = 0.0;
    double maxSpeed = 0.0;
    double maxOmega = 0.0;
    double maxPenetration = 0.0;
    char maxPenetrationContact[64] = "";
    int maxPenetrationBodyA = -1;
    int maxPenetrationBodyB = -1;
    uint32_t maxPenetrationFeatureId = 0;

    struct DiagnosticsIslandStats
    {
        int root = -1;
        int islandId = 0;
        int bodyCount = 0;
        int awakeCount = 0;
        int sleepingCount = 0;
        int supportedCount = 0;
        int inhibitedCount = 0;
        int eligible = 0;
        int canSleep = 0;
        double maxSpeed = 0.0;
        double maxOmega = 0.0;
        double totalEnergy = 0.0;
    };

    for ( const auto& c : m_persistentContacts )
    {
        if ( c.penetration > maxPenetration )
        {
            maxPenetration = c.penetration;
            sprintf_s( maxPenetrationContact,
                       sizeof( maxPenetrationContact ),
                       "%d:%d:%u",
                       c.bodyA,
                       c.bodyB,
                       c.featureId );
            maxPenetrationBodyA = c.bodyA;
            maxPenetrationBodyB = c.bodyB;
            maxPenetrationFeatureId = c.featureId;
        }
    }

    std::vector<int> islandRoots;
    islandRoots.reserve( modelCount );
    std::vector<int> bodyIslandIds( modelCount, 0 );
    std::vector<DiagnosticsIslandStats> islandStats( modelCount );

    // Sleep islands are rebuilt every physics step in GameModelCollection.  The
    // diagnostics copy only the root/id mapping and aggregate counts; it avoids
    // serializing the entire union-find unless a specific query asks for detail.
    auto findIslandRoot = [&]( int index ) -> int
    {
        int root = index;
        while ( root >= 0 && root < static_cast<int>( m_sleepIslandParent.size() ) &&
                m_sleepIslandParent[root] != root )
        {
            root = m_sleepIslandParent[root];
        }
        if ( root < 0 || root >= modelCount )
        {
            root = index;
        }
        return root;
    };

    for ( int i = 0; i < modelCount; ++i )
    {
        GameModel& model = m_gameModels[i];
        const Vector3& vel = model.GetVelocity();
        const Vector3& omega = model.GetAngularVelocity();
        const Vector3& inertia = model.GetRotationalInertia();
        const double speedSq = static_cast<double>( vel.x ) * vel.x + static_cast<double>( vel.y ) * vel.y +
                               static_cast<double>( vel.z ) * vel.z;
        const double omegaSq = static_cast<double>( omega.x ) * omega.x + static_cast<double>( omega.y ) * omega.y +
                               static_cast<double>( omega.z ) * omega.z;
        const double speed = sqrt( speedSq );
        const double omegaMag = sqrt( omegaSq );
        const double mass = model.GetMass();
        const double linearEnergy = 0.5 * mass * speedSq;
        const double angularEnergy = 0.5 * ( static_cast<double>( inertia.x ) * omega.x * omega.x +
                                             static_cast<double>( inertia.y ) * omega.y * omega.y +
                                             static_cast<double>( inertia.z ) * omega.z * omega.z );

        totalLinearEnergy += linearEnergy;
        totalAngularEnergy += angularEnergy;

        if ( speed > maxSpeed )
        {
            maxSpeed = speed;
            maxSpeedBody = i;
        }
        if ( omegaMag > maxOmega )
        {
            maxOmega = omegaMag;
            maxOmegaBody = i;
        }

        const int sleeping = ( i < static_cast<int>( m_sleepState.size() ) ) ? m_sleepState[i] : 0;
        const int sleepSupported =
            ( i < static_cast<int>( m_sleepSupportedThisFrame.size() ) ) ? m_sleepSupportedThisFrame[i] : 0;
        const int sleepInhibited =
            ( i < static_cast<int>( m_sleepInhibitedThisFrame.size() ) ) ? m_sleepInhibitedThisFrame[i] : 0;
        if ( sleeping )
        {
            ++sleepingCount;
        }
        else
        {
            ++awakeCount;
        }
        if ( sleepSupported )
        {
            ++supportedCount;
        }
        if ( sleepInhibited )
        {
            ++inhibitedCount;
        }

        const int root = ( i < static_cast<int>( m_sleepIslandParent.size() ) ) ? findIslandRoot( i ) : i;
        const int islandId = root + 1;
        bodyIslandIds[i] = islandId;
        DiagnosticsIslandStats& island = islandStats[root];
        if ( island.root < 0 )
        {
            island.root = root;
            island.islandId = islandId;
            island.eligible =
                ( root < static_cast<int>( m_sleepIslandEligible.size() ) ) ? m_sleepIslandEligible[root] : 0;
            island.canSleep =
                ( root < static_cast<int>( m_sleepIslandCanSleep.size() ) ) ? m_sleepIslandCanSleep[root] : 0;
            bool seen = false;
            for ( int existingRoot : islandRoots )
            {
                if ( existingRoot == root )
                {
                    seen = true;
                    break;
                }
            }
            if ( !seen )
            {
                islandRoots.push_back( root );
            }
        }
        ++island.bodyCount;
        if ( sleeping )
        {
            ++island.sleepingCount;
        }
        else
        {
            ++island.awakeCount;
        }
        if ( sleepSupported )
        {
            ++island.supportedCount;
        }
        if ( sleepInhibited )
        {
            ++island.inhibitedCount;
        }
        if ( speed > island.maxSpeed )
        {
            island.maxSpeed = speed;
        }
        if ( omegaMag > island.maxOmega )
        {
            island.maxOmega = omegaMag;
        }
        island.totalEnergy += linearEnergy + angularEnergy;
    }

    const double totalEnergy = totalLinearEnergy + totalAngularEnergy;
    Log().Writef( m_physicsDiagnosticsPath,
                  "{\"kind\":\"frame\",\"run\":\"%s\",\"frame\":%d,\"time_seconds\":%.6f,\"dt\":%.6f,\"body_count\":%d,"
                  "\"awake_count\":%d,\"sleeping_count\":%d,\"supported_count\":%d,\"inhibited_count\":%d,\"contact_"
                  "count\":%zu,\"island_count\":%zu,\"total_energy\":%.6f,\"linear_energy\":%.6f,\"angular_energy\":%."
                  "6f,\"max_speed\":%.6f,\"max_speed_body\":%d,\"max_omega\":%.6f,\"max_omega_body\":%d,\"max_"
                  "penetration\":%.6f,\"max_penetration_contact\":\"%s\"}\n",
                  m_physicsDiagnosticsRunId,
                  frame,
                  m_physicsDiagnosticsTimeSeconds,
                  dt,
                  modelCount,
                  awakeCount,
                  sleepingCount,
                  supportedCount,
                  inhibitedCount,
                  m_persistentContacts.size(),
                  islandRoots.size(),
                  totalEnergy,
                  totalLinearEnergy,
                  totalAngularEnergy,
                  maxSpeed,
                  maxSpeedBody,
                  maxOmega,
                  maxOmegaBody,
                  maxPenetration,
                  maxPenetrationContact );

    Log().Writef( m_physicsDiagnosticsPath,
                  "{\"kind\":\"solver_stats\",\"run\":\"%s\",\"frame\":%d,\"row_count\":%d,\"cache_previous_rows\":%d,"
                  "\"cache_hits\":%d,\"cache_misses\":%d,\"warm_started_rows\":%d,\"position_correction_rows\":%d,"
                  "\"position_correction_total\":%.6f,\"position_correction_max\":%.6f,\"solver_iterations\":%d}\n",
                  m_physicsDiagnosticsRunId,
                  frame,
                  m_persistentContactSolverStats.rowCount,
                  m_persistentContactSolverStats.cachePreviousRows,
                  m_persistentContactSolverStats.cacheHits,
                  m_persistentContactSolverStats.cacheMisses,
                  m_persistentContactSolverStats.warmStartedRows,
                  m_persistentContactSolverStats.positionCorrectionRows,
                  m_persistentContactSolverStats.positionCorrectionTotal,
                  m_persistentContactSolverStats.positionCorrectionMax,
                  m_persistentContactSolverStats.solverIterations );

    {
        const int stageCount = static_cast<int>( Physics::PhysicsPipelineStage::Count );
        int stageCounts[static_cast<int>( Physics::PhysicsPipelineStage::Count )] = {};
        for ( const Physics::PhysicsPipelineRecord& record : m_physicsPipelineTrace )
        {
            const int stageIndex = static_cast<int>( record.stage );
            if ( stageIndex >= 0 && stageIndex < stageCount )
            {
                ++stageCounts[stageIndex];
            }
        }

        Log().Writef( m_physicsDiagnosticsPath,
                      "{\"kind\":\"pipeline_stages\",\"run\":\"%s\",\"frame\":%d,\"record_count\":%zu",
                      m_physicsDiagnosticsRunId,
                      frame,
                      m_physicsPipelineTrace.size() );
        for ( int i = 0; i < stageCount; ++i )
        {
            Log().Writef( m_physicsDiagnosticsPath,
                          ",\"%s\":%d",
                          Physics::PhysicsPipelineStageName( static_cast<Physics::PhysicsPipelineStage>( i ) ),
                          stageCounts[i] );
        }
        Log().Writef( m_physicsDiagnosticsPath, "}\n" );
    }

    if ( m_physicsDiagnosticsHasPrevEnergy )
    {
        const double deltaEnergy = totalEnergy - m_physicsDiagnosticsPrevEnergy;
        const double spikeThreshold = (std::max)( 10000.0, fabs( m_physicsDiagnosticsPrevEnergy ) * 0.50 );
        if ( deltaEnergy > spikeThreshold )
        {
            const int eventId = ++m_physicsDiagnosticsEventCounter;
            Log().Writef( m_physicsDiagnosticsPath,
                          "{\"kind\":\"event\",\"run\":\"%s\",\"event_id\":\"E%d\",\"frame\":%d,\"type\":\"energy_"
                          "spike\",\"severity\":\"medium\",\"body_a\":%d,\"body_b\":-1,\"island_id\":-1,\"summary\":"
                          "\"Total kinetic energy increased "
                          "sharply.\",\"data\":{\"previous_total_energy\":%.6f,\"total_energy\":%.6f,\"delta_energy\":%"
                          ".6f,\"followups\":[\"energy --frames %d:%d\",\"frame %d\",\"body %d --frames %d:%d\"]}}\n",
                          m_physicsDiagnosticsRunId,
                          eventId,
                          frame,
                          maxSpeedBody,
                          m_physicsDiagnosticsPrevEnergy,
                          totalEnergy,
                          deltaEnergy,
                          (std::max)( 0, frame - 30 ),
                          frame + 30,
                          frame,
                          maxSpeedBody,
                          (std::max)( 0, frame - 30 ),
                          frame + 30 );
        }
    }
    m_physicsDiagnosticsPrevEnergy = totalEnergy;
    m_physicsDiagnosticsHasPrevEnergy = true;

    int activeCellCount = m_spatialGrid.GetActiveCellCount();
    int maxCellOccupancy = 0;
    if ( activeCellCount > 0 )
    {
        std::vector<SpatialGrid::ActiveCell> activeCells( activeCellCount );
        m_spatialGrid.GetActiveCells( activeCells.data(), activeCellCount );
        for ( const SpatialGrid::ActiveCell& cell : activeCells )
        {
            if ( cell.objectCount > maxCellOccupancy )
            {
                maxCellOccupancy = cell.objectCount;
            }
        }
    }

    std::vector<std::pair<int, int>> contactPairs;
    contactPairs.reserve( m_persistentContacts.size() );
    for ( const auto& c : m_persistentContacts )
    {
        if ( c.isTerrain )
        {
            continue;
        }

        int a = c.bodyA;
        int b = c.bodyB;
        if ( a > b )
        {
            std::swap( a, b );
        }
        bool seen = false;
        for ( const auto& pair : contactPairs )
        {
            if ( pair.first == a && pair.second == b )
            {
                seen = true;
                break;
            }
        }
        if ( !seen )
        {
            contactPairs.emplace_back( a, b );
        }
    }

    int rejectedPairs = static_cast<int>( m_candidatePairs.size() ) - static_cast<int>( contactPairs.size() );
    if ( rejectedPairs < 0 )
    {
        rejectedPairs = 0;
    }
    Log().Writef( m_physicsDiagnosticsPath,
                  "{\"kind\":\"broadphase\",\"run\":\"%s\",\"frame\":%d,\"candidate_pairs\":%zu,\"contact_pairs\":%zu,"
                  "\"rejected_pairs\":%d,\"active_cells\":%d,\"max_cell_occupancy\":%d,\"collision_cell_count\":%zu}\n",
                  m_physicsDiagnosticsRunId,
                  frame,
                  m_candidatePairs.size(),
                  contactPairs.size(),
                  rejectedPairs,
                  activeCellCount,
                  maxCellOccupancy,
                  m_collisionCellKeys.size() );

    if ( m_candidatePairs.size() > (std::max)( 128, modelCount * 8 ) || maxCellOccupancy > 32 )
    {
        const int eventId = ++m_physicsDiagnosticsEventCounter;
        Log().Writef( m_physicsDiagnosticsPath,
                      "{\"kind\":\"event\",\"run\":\"%s\",\"event_id\":\"E%d\",\"frame\":%d,\"type\":\"broadphase_"
                      "spike\",\"severity\":\"medium\",\"body_a\":-1,\"body_b\":-1,\"island_id\":-1,\"summary\":"
                      "\"Broadphase candidate work is unusually high for this "
                      "frame.\",\"data\":{\"candidate_pairs\":%zu,\"active_cells\":%d,\"max_cell_occupancy\":%d,"
                      "\"followups\":[\"broadphase --frames %d:%d\",\"frame %d\"]}}\n",
                      m_physicsDiagnosticsRunId,
                      eventId,
                      frame,
                      m_candidatePairs.size(),
                      activeCellCount,
                      maxCellOccupancy,
                      (std::max)( 0, frame - 30 ),
                      frame + 30,
                      frame );
    }

    constexpr double penetrationSustainedThreshold = 0.05;
    constexpr double penetrationGrowthTrackThreshold = 0.02;
    constexpr double penetrationGrowthEpsilon = 0.001;
    constexpr double penetrationGrowthMinDelta = 0.02;
    constexpr int penetrationSustainFrames = 12;
    constexpr int penetrationGrowthWindow = 8;

    const int penetrationIslandId =
        ( maxPenetrationBodyA >= 0 && maxPenetrationBodyA < static_cast<int>( bodyIslandIds.size() ) )
            ? bodyIslandIds[maxPenetrationBodyA]
            : -1;
    const int penetrationWindowStartFrame = (std::max)( 0, frame - penetrationGrowthWindow );
    const int penetrationContextStartFrame = (std::max)( 0, frame - 30 );
    const bool hasPenetrationContact = maxPenetrationContact[0] != '\0';

    if ( hasPenetrationContact && maxPenetration >= penetrationGrowthTrackThreshold )
    {
        if ( strcmp( m_physicsDiagnosticsPenetrationContact, maxPenetrationContact ) != 0 )
        {
            strcpy_s( m_physicsDiagnosticsPenetrationContact,
                      sizeof( m_physicsDiagnosticsPenetrationContact ),
                      maxPenetrationContact );
            m_physicsDiagnosticsPenetrationFrames = 0;
            m_physicsDiagnosticsPenetrationGrowthFrames = 0;
            m_physicsDiagnosticsPenetrationWindowStart = maxPenetration;
            m_physicsDiagnosticsPrevPenetration = maxPenetration;
            m_physicsDiagnosticsPenetrationSustainedReported = false;
            m_physicsDiagnosticsPenetrationGrowingReported = false;
        }

        if ( maxPenetration >= penetrationSustainedThreshold )
        {
            ++m_physicsDiagnosticsPenetrationFrames;
        }
        else
        {
            m_physicsDiagnosticsPenetrationFrames = 0;
            m_physicsDiagnosticsPenetrationSustainedReported = false;
        }

        if ( maxPenetration > m_physicsDiagnosticsPrevPenetration + penetrationGrowthEpsilon )
        {
            if ( m_physicsDiagnosticsPenetrationGrowthFrames == 0 )
            {
                m_physicsDiagnosticsPenetrationWindowStart = m_physicsDiagnosticsPrevPenetration;
            }
            ++m_physicsDiagnosticsPenetrationGrowthFrames;
        }
        else if ( maxPenetration < m_physicsDiagnosticsPrevPenetration - penetrationGrowthEpsilon )
        {
            m_physicsDiagnosticsPenetrationGrowthFrames = 0;
            m_physicsDiagnosticsPenetrationWindowStart = maxPenetration;
            m_physicsDiagnosticsPenetrationGrowingReported = false;
        }

        const double penetrationGrowthDelta = maxPenetration - m_physicsDiagnosticsPenetrationWindowStart;

        if ( !m_physicsDiagnosticsPenetrationSustainedReported &&
             m_physicsDiagnosticsPenetrationFrames >= penetrationSustainFrames )
        {
            const int eventId = ++m_physicsDiagnosticsEventCounter;
            Log().Writef( m_physicsDiagnosticsPath,
                          "{\"kind\":\"event\",\"run\":\"%s\",\"event_id\":\"E%d\",\"frame\":%d,\"type\":\"penetration_"
                          "sustained\",\"severity\":\"medium\",\"body_a\":%d,\"body_b\":%d,\"island_id\":%d,"
                          "\"summary\":\"Contact penetration stayed above the diagnostic threshold for multiple "
                          "frames.\",\"data\":{\"max_penetration\":%.6f,\"threshold\":%.6f,\"frames_over_threshold\":%"
                          "d,\"required_frames\":%d,\"contact\":\"%s\",\"feature_id\":%u,\"followups\":[\"contacts "
                          "--frame %d --top penetration\",\"event E%d --window 30\",\"body %d --frames %d:%d\",\"body "
                          "%d --frames %d:%d\",\"frame %d\"]}}\n",
                          m_physicsDiagnosticsRunId,
                          eventId,
                          frame,
                          maxPenetrationBodyA,
                          maxPenetrationBodyB,
                          penetrationIslandId,
                          maxPenetration,
                          penetrationSustainedThreshold,
                          m_physicsDiagnosticsPenetrationFrames,
                          penetrationSustainFrames,
                          maxPenetrationContact,
                          maxPenetrationFeatureId,
                          frame,
                          eventId,
                          maxPenetrationBodyA,
                          penetrationContextStartFrame,
                          frame + 30,
                          maxPenetrationBodyB,
                          penetrationContextStartFrame,
                          frame + 30,
                          frame );
            m_physicsDiagnosticsPenetrationSustainedReported = true;
        }

        if ( !m_physicsDiagnosticsPenetrationGrowingReported &&
             m_physicsDiagnosticsPenetrationGrowthFrames >= penetrationGrowthWindow &&
             penetrationGrowthDelta >= penetrationGrowthMinDelta )
        {
            const int eventId = ++m_physicsDiagnosticsEventCounter;
            Log().Writef(
                m_physicsDiagnosticsPath,
                "{\"kind\":\"event\",\"run\":\"%s\",\"event_id\":\"E%d\",\"frame\":%d,\"type\":\"penetration_growing\","
                "\"severity\":\"high\",\"body_a\":%d,\"body_b\":%d,\"island_id\":%d,\"summary\":\"Contact penetration "
                "kept increasing across the diagnostic "
                "window.\",\"data\":{\"start_penetration\":%.6f,\"current_penetration\":%.6f,\"delta_penetration\":%."
                "6f,\"window_start_frame\":%d,\"growth_frames\":%d,\"required_growth_frames\":%d,\"min_delta\":%.6f,"
                "\"contact\":\"%s\",\"feature_id\":%u,\"followups\":[\"contacts --frame %d --top penetration\",\"event "
                "E%d --window 30\",\"body %d --frames %d:%d\",\"body %d --frames %d:%d\",\"frame %d\"]}}\n",
                m_physicsDiagnosticsRunId,
                eventId,
                frame,
                maxPenetrationBodyA,
                maxPenetrationBodyB,
                penetrationIslandId,
                m_physicsDiagnosticsPenetrationWindowStart,
                maxPenetration,
                penetrationGrowthDelta,
                penetrationWindowStartFrame,
                m_physicsDiagnosticsPenetrationGrowthFrames,
                penetrationGrowthWindow,
                penetrationGrowthMinDelta,
                maxPenetrationContact,
                maxPenetrationFeatureId,
                frame,
                eventId,
                maxPenetrationBodyA,
                penetrationContextStartFrame,
                frame + 30,
                maxPenetrationBodyB,
                penetrationContextStartFrame,
                frame + 30,
                frame );
            m_physicsDiagnosticsPenetrationGrowingReported = true;
        }

        m_physicsDiagnosticsPrevPenetration = maxPenetration;
    }
    else
    {
        ResetPenetrationState();
    }

    for ( const auto& c : m_persistentContacts )
    {
        if ( c.bodyA < 0 || c.bodyA >= modelCount || ( !c.isTerrain && ( c.bodyB < 0 || c.bodyB >= modelCount ) ) )
        {
            continue;
        }

        GameModel& a = m_gameModels[c.bodyA];
        const Vector3 velA = a.GetVelocity() + Vector::CrossProduct( a.GetAngularVelocity(), c.rA );
        const Vector3 velB = c.isTerrain ? ZERO_VECTOR
                                         : m_gameModels[c.bodyB].GetVelocity() +
                                               Vector::CrossProduct( m_gameModels[c.bodyB].GetAngularVelocity(), c.rB );
        const Vector3 relVel = velB - velA;
        const float normalSpeed = relVel * c.normal;
        const Vector3 tangentVel = relVel - c.normal * normalSpeed;
        const float slipSpeed = Vector::VectorMag( tangentVel );
        const double tangentImpulse =
            sqrt( static_cast<double>( c.accT1 ) * c.accT1 + static_cast<double>( c.accT2 ) * c.accT2 );
        const char* shapeA = a.GetShapeName();
        const char* shapeB = c.isTerrain ? "terrain" : m_gameModels[c.bodyB].GetShapeName();
        char contactType[32] = "";
        sprintf_s( contactType, sizeof( contactType ), "%s/%s", shapeA, shapeB );
        const int supportsSleep =
            c.isTerrain ? ( c.bodyA < static_cast<int>( m_sleepSupportedThisFrame.size() ) &&
                            m_sleepSupportedThisFrame[c.bodyA] )
                        : ( ( c.normal.y > 0.25f && c.bodyB < static_cast<int>( m_sleepSupportedThisFrame.size() ) &&
                              m_sleepSupportedThisFrame[c.bodyB] ) ||
                            ( c.normal.y < -0.25f && c.bodyA < static_cast<int>( m_sleepSupportedThisFrame.size() ) &&
                              m_sleepSupportedThisFrame[c.bodyA] ) );
        const Vector3 diagnosticNormal = c.isTerrain ? c.terrainNormal : c.normal;

        Log().Writef( m_physicsDiagnosticsPath,
                      "{\"kind\":\"contact\",\"run\":\"%s\",\"frame\":%d,\"contact_id\":\"%d:%d:%u\",\"body_a\":%d,"
                      "\"body_b\":%d,\"contact_type\":\"%s\",\"feature_id\":%u,\"point_count\":%u,\"normal\":[%.6f,%."
                      "6f,%.6f],\"penetration\":%.6f,\"normal_impulse\":%.6f,\"tangent_impulse\":%.6f,\"slip_speed\":%."
                      "6f,\"rolling_residual\":%.6f,\"warm_started\":%d,\"supports_sleep\":%d}\n",
                      m_physicsDiagnosticsRunId,
                      frame,
                      c.bodyA,
                      c.bodyB,
                      c.featureId,
                      c.bodyA,
                      c.bodyB,
                      contactType,
                      c.featureId,
                      static_cast<unsigned>( c.manifoldPointCount ),
                      diagnosticNormal.x,
                      diagnosticNormal.y,
                      diagnosticNormal.z,
                      c.penetration,
                      c.accN,
                      tangentImpulse,
                      slipSpeed,
                      slipSpeed,
                      c.warmStarted ? 1 : 0,
                      supportsSleep ? 1 : 0 );
    }

    for ( const auto& edge : m_sleepSupportEdges )
    {
        Log().Writef( m_physicsDiagnosticsPath,
                      "{\"kind\":\"support_edge\",\"run\":\"%s\",\"frame\":%d,\"supporter\":%d,\"supported\":%d,"
                      "\"source\":\"object_contact\"}\n",
                      m_physicsDiagnosticsRunId,
                      frame,
                      edge.first,
                      edge.second );
    }

    for ( const auto& manifold : m_terrainContactManifolds )
    {
        if ( manifold.supportsRestingPolicy )
        {
            Log().Writef( m_physicsDiagnosticsPath,
                          "{\"kind\":\"support_edge\",\"run\":\"%s\",\"frame\":%d,\"supporter\":-1,\"supported\":%d,"
                          "\"source\":\"terrain\"}\n",
                          m_physicsDiagnosticsRunId,
                          frame,
                          manifold.bodyA );
        }
    }

    for ( int root : islandRoots )
    {
        if ( root < 0 || root >= static_cast<int>( islandStats.size() ) || islandStats[root].root < 0 )
        {
            continue;
        }
        const DiagnosticsIslandStats& island = islandStats[root];
        Log().Writef( m_physicsDiagnosticsPath,
                      "{\"kind\":\"island\",\"run\":\"%s\",\"frame\":%d,\"island_id\":%d,\"body_count\":%d,\"awake_"
                      "count\":%d,\"sleeping_count\":%d,\"supported_count\":%d,\"inhibited_count\":%d,\"eligible\":%d,"
                      "\"can_sleep\":%d,\"max_speed\":%.6f,\"max_omega\":%.6f,\"total_energy\":%.6f}\n",
                      m_physicsDiagnosticsRunId,
                      frame,
                      island.islandId,
                      island.bodyCount,
                      island.awakeCount,
                      island.sleepingCount,
                      island.supportedCount,
                      island.inhibitedCount,
                      island.eligible,
                      island.canSleep,
                      island.maxSpeed,
                      island.maxOmega,
                      island.totalEnergy );
    }

    for ( int i = 0; i < modelCount; ++i )
    {
        Log().Writef( m_physicsDiagnosticsPath,
                      "{\"kind\":\"island_member\",\"run\":\"%s\",\"frame\":%d,\"island_id\":%d,\"body_id\":%d}\n",
                      m_physicsDiagnosticsRunId,
                      frame,
                      bodyIslandIds[i],
                      i );
    }

    for ( int i = 0; i < modelCount; ++i )
    {
        GameModel& model = m_gameModels[i];
        const char* shapeType = model.GetShapeName();
        std::string escapedName = EscapeSkullScopeJson( model.GetName() );
        const Vector3& pos = model.GetPosition();
        const Vector3& vel = model.GetVelocity();
        const Vector3& omega = model.GetAngularVelocity();
        const Vector3& inertia = model.GetRotationalInertia();
        float qx = 0.0f;
        float qy = 0.0f;
        float qz = 0.0f;
        float qw = 1.0f;
        model.GetOrientation().GetComponents( qx, qy, qz, qw );

        const double speedSq = static_cast<double>( vel.x ) * vel.x + static_cast<double>( vel.y ) * vel.y +
                               static_cast<double>( vel.z ) * vel.z;
        const double omegaSq = static_cast<double>( omega.x ) * omega.x + static_cast<double>( omega.y ) * omega.y +
                               static_cast<double>( omega.z ) * omega.z;
        const double speed = sqrt( speedSq );
        const double omegaMag = sqrt( omegaSq );
        const double mass = model.GetMass();
        const double linearEnergy = 0.5 * mass * speedSq;
        const double angularEnergy = 0.5 * ( static_cast<double>( inertia.x ) * omega.x * omega.x +
                                             static_cast<double>( inertia.y ) * omega.y * omega.y +
                                             static_cast<double>( inertia.z ) * omega.z * omega.z );
        const int sleeping = ( i < static_cast<int>( m_sleepState.size() ) ) ? m_sleepState[i] : 0;
        const int sleepSupported =
            ( i < static_cast<int>( m_sleepSupportedThisFrame.size() ) ) ? m_sleepSupportedThisFrame[i] : 0;
        const int sleepInhibited =
            ( i < static_cast<int>( m_sleepInhibitedThisFrame.size() ) ) ? m_sleepInhibitedThisFrame[i] : 0;
        const int sleepCounter = ( i < static_cast<int>( m_sleepCounter.size() ) ) ? m_sleepCounter[i] : 0;
        const int islandId = bodyIslandIds[i];
        const int visualIslandId =
            ( i < static_cast<int>( m_sleepIslandVisualId.size() ) ) ? m_sleepIslandVisualId[i] : 0;

        float radius = 0.0f;
        Vector3 halfExtents = ZERO_VECTOR;
        uint16_t hullVertices = 0;
        uint16_t hullFaces = 0;
        uint16_t hullEdges = 0;
        std::string escapedHullName;
        std::visit(
            [&]( const auto& shape )
            {
                using ShapeT = std::decay_t<decltype( shape )>;
                if constexpr ( std::is_same_v<ShapeT, BoundingSphere> )
                {
                    radius = shape.GetRadius();
                }
                else if constexpr ( std::is_same_v<ShapeT, BoundingBox> )
                {
                    halfExtents = shape.GetHalfExtents();
                }
                else
                {
                    radius = shape.GetBoundingRadius();
                    hullVertices = shape.GetVertexCount();
                    hullFaces = shape.GetFaceCount();
                    hullEdges = shape.GetEdgeCount();
                    escapedHullName = EscapeSkullScopeJson( shape.GetName() );
                }
            },
            model.GetCollisionShape() );

        Log().Writef(
            m_physicsDiagnosticsPath,
            "{\"kind\":\"body\",\"run\":\"%s\",\"frame\":%d,\"body_id\":%d,\"name\":\"%s\",\"shape\":\"%s\",\"pos\":[%."
            "6f,%.6f,%.6f],\"vel\":[%.6f,%.6f,%.6f],\"omega\":[%.6f,%.6f,%.6f],\"q\":[%.6f,%.6f,%.6f,%.6f],\"speed\":%."
            "6f,\"omega_mag\":%.6f,\"mass\":%.6f,\"inv_mass\":%.6f,\"inertia\":[%.6f,%.6f,%.6f],\"radius\":%.6f,\"half_"
            "extents\":[%.6f,%.6f,%.6f],\"hull_name\":\"%s\",\"hull_vertices\":%u,\"hull_faces\":%u,\"hull_edges\":%u,"
            "\"linear_energy\":%.6f,\"angular_energy\":%.6f,\"sleeping\":%d,\"sleep_supported\":%d,\"sleep_inhibited\":"
            "%d,\"sleep_counter\":%d,\"island_id\":%d}\n",
            m_physicsDiagnosticsRunId,
            frame,
            i,
            escapedName.c_str(),
            shapeType,
            pos.x,
            pos.y,
            pos.z,
            vel.x,
            vel.y,
            vel.z,
            omega.x,
            omega.y,
            omega.z,
            qx,
            qy,
            qz,
            qw,
            speed,
            omegaMag,
            mass,
            model.GetInvertedMass(),
            inertia.x,
            inertia.y,
            inertia.z,
            radius,
            halfExtents.x,
            halfExtents.y,
            halfExtents.z,
            escapedHullName.c_str(),
            static_cast<unsigned>( hullVertices ),
            static_cast<unsigned>( hullFaces ),
            static_cast<unsigned>( hullEdges ),
            linearEnergy,
            angularEnergy,
            sleeping,
            sleepSupported,
            sleepInhibited,
            sleepCounter,
            islandId );

        if ( sleeping && visualIslandId == 0 )
        {
            const int eventId = ++m_physicsDiagnosticsEventCounter;
            Log().Writef( m_physicsDiagnosticsPath,
                          "{\"kind\":\"event\",\"run\":\"%s\",\"event_id\":\"E%d\",\"frame\":%d,\"type\":\"unsupported_"
                          "sleep\",\"severity\":\"high\",\"body_a\":%d,\"body_b\":-1,\"island_id\":%d,\"summary\":"
                          "\"Body is sleeping without an assigned sleep island "
                          "id.\",\"data\":{\"body_id\":%d,\"sleep_supported\":%d,\"sleep_inhibited\":%d,\"followups\":["
                          "\"body %d --frames %d:%d\",\"contacts --frame %d --body %d\",\"frame %d\"]}}\n",
                          m_physicsDiagnosticsRunId,
                          eventId,
                          frame,
                          i,
                          islandId,
                          i,
                          sleepSupported,
                          sleepInhibited,
                          i,
                          (std::max)( 0, frame - 30 ),
                          frame + 30,
                          frame,
                          i,
                          frame );
        }
    }

    ++m_physicsDiagnosticsFrame;
    m_physicsDiagnosticsTimeSeconds += dt;
}

#endif
