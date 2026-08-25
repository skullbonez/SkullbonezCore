/*
Purpose:
  Resolves causal-row transport/detail eligibility and advances the inspection transition.

Invariants:
  - Recorder statistics describe a contiguous half-open frame range.
  - Prediction matching is exact because publication may replace the frame bank.
  - Detail joins fail closed when the selected contact, identity, or source-frame
    stamp disagrees, while transport remains independently usable.
  - Contact presentation fails closed when either body pose or the complete
    bounded patch cannot be proven from the same solver frame.
  - Solver-detail publication fails closed before exposing partial copied spans;
    its scroll offset always clamps to the projected visible-row viewport.
  - The drawer visibility eases independently from evidence lifetime: evidence
    is cleared at every lifecycle edge while the empty shell may finish its
    bounded reverse animation.
  - Only the current generation may reveal detail or complete a return.
  - Forward and reverse transport round symmetrically and reach the exact target
    only at eased progress 1, independent of render cadence.
  - Planning publishes pause actions but never mutates Replay or camera owners directly.
*/
#include "ReplayCauseInspection.h"

#include "../../Core/Profiler.h"
#include "../Prediction/ReplayPredictionView.h"
#include "../Replay/ReplayOverlayLayout.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string_view>

namespace SkullbonezCore::Runtime
{
namespace
{
constexpr const char* REPLAY_FRAME_EXPIRED_FEEDBACK = "Replay frame expired";
constexpr const char* SOLVER_DETAIL_UNAVAILABLE_FEEDBACK = "Solver detail not available";
constexpr double REPLAY_CAUSE_TRANSITION_SECONDS = 1.5;

bool BodyPairMatches( int candidateA, int candidateB, int bodyA, int bodyB, bool terrain ) noexcept
{
    if ( terrain )
    {
        return candidateA == bodyA && candidateB < 0;
    }

    return ( candidateA == bodyA && candidateB == bodyB ) || ( candidateA == bodyB && candidateB == bodyA );
}

bool ContactPairMatches( const Physics::PhysicsSolverPersistentContactSample& contact, int bodyA, int bodyB,
                         bool terrain ) noexcept
{
    return contact.isTerrain == terrain && BodyPairMatches( contact.bodyA, contact.bodyB, bodyA, bodyB, terrain );
}

bool IsSolverDetailPipelineStage( Physics::PhysicsPipelineStage stage ) noexcept
{
    switch ( stage )
    {
    case Physics::PhysicsPipelineStage::ManifoldRow:
    case Physics::PhysicsPipelineStage::WarmStart:
    case Physics::PhysicsPipelineStage::SolverIteration:
    case Physics::PhysicsPipelineStage::VelocityWriteback:
    case Physics::PhysicsPipelineStage::PositionCorrection:
    case Physics::PhysicsPipelineStage::CacheStore:
        return true;
    default:
        return false;
    }
}

bool PipelineRecordMatches( const ReplayCauseSolverDetailResult& result,
                            const Physics::PhysicsPipelineRecord& record ) noexcept
{
    if ( !IsSolverDetailPipelineStage( record.stage ) )
    {
        return false;
    }

    if ( record.stage == Physics::PhysicsPipelineStage::VelocityWriteback )
    {
        return record.bodyA == result.bodyA || ( !result.terrain && record.bodyA == result.bodyB );
    }

    if ( !BodyPairMatches( record.bodyA, record.bodyB, result.bodyA, result.bodyB, result.terrain ) )
    {
        return false;
    }

    for ( std::size_t contactIndex = 0; contactIndex < result.SourceContactCount(); ++contactIndex )
    {
        const Physics::PhysicsSolverPersistentContactSample* contact = result.SourceContactAt( contactIndex );

        if ( contact && ContactPairMatches( *contact, result.bodyA, result.bodyB, result.terrain ) &&
             contact->featureId == record.featureId )
        {
            return true;
        }
    }

    return false;
}

bool PointInside( const UI::UIRect& rect, int x, int y ) noexcept
{
    const float pointX = static_cast<float>( x );
    const float pointY = static_cast<float>( y );
    return pointX >= rect.x && pointX <= rect.x + rect.w && pointY >= rect.y && pointY <= rect.y + rect.h;
}

class FixedTextWriter
{
  public:
    FixedTextWriter( char* destination, std::size_t capacity ) noexcept
        : m_destination( destination ), m_capacity( capacity )
    {
        if ( m_destination && m_capacity > 0u )
        {
            m_destination[0] = '\0';
        }
    }

    bool Append( std::string_view text ) noexcept
    {
        if ( !m_valid || !m_destination || text.size() >= m_capacity - m_size )
        {
            m_valid = false;
            return false;
        }

        std::memcpy( m_destination + m_size, text.data(), text.size() );
        m_size += text.size();
        m_destination[m_size] = '\0';
        return true;
    }

    bool Valid() const noexcept
    {
        return m_valid;
    }

  private:
    char* m_destination = nullptr;
    std::size_t m_capacity = 0u;
    std::size_t m_size = 0u;
    bool m_valid = true;
};

template <typename Integer> void FormatInteger( Integer value, char* destination, std::size_t capacity ) noexcept
{
    if ( !destination || capacity == 0u )
    {
        return;
    }

    const std::to_chars_result result = std::to_chars( destination, destination + capacity - 1u, value );
    destination[result.ec == std::errc {} ? static_cast<std::size_t>( result.ptr - destination ) : 0u] = '\0';
}

void FormatFloat( float value, char* destination, std::size_t capacity ) noexcept
{
    if ( !destination || capacity == 0u )
    {
        return;
    }

    const std::to_chars_result result = std::to_chars( destination, destination + capacity - 1u, value,
                                                       std::chars_format::general,
                                                       std::numeric_limits<float>::max_digits10 );
    destination[result.ec == std::errc {} ? static_cast<std::size_t>( result.ptr - destination ) : 0u] = '\0';
}

void FormatVector( const Math::Vector::Vector3& value, char* destination, std::size_t capacity ) noexcept
{
    char x[32] = {};
    char y[32] = {};
    char z[32] = {};
    FormatFloat( value.x, x, sizeof( x ) );
    FormatFloat( value.y, y, sizeof( y ) );
    FormatFloat( value.z, z, sizeof( z ) );
    FixedTextWriter writer( destination, capacity );
    (void)writer.Append( x );
    (void)writer.Append( ", " );
    (void)writer.Append( y );
    (void)writer.Append( ", " );
    (void)writer.Append( z );
}

void AddRawSection( ReplayCauseRawRecordProjection& projection, const char* label ) noexcept
{
    if ( projection.rowCount >= projection.rows.size() )
    {
        return;
    }

    ReplayCauseRawRecordRow& row = projection.rows[projection.rowCount++];
    row.kind = ReplayCauseRawRecordRowKind::Section;
    strcpy_s( row.label, sizeof( row.label ), label );
}

void AddRawValue( ReplayCauseRawRecordProjection& projection, const char* label, const char* value,
                  const char* unit = "" ) noexcept
{
    if ( projection.rowCount >= projection.rows.size() )
    {
        return;
    }

    ReplayCauseRawRecordRow& row = projection.rows[projection.rowCount++];
    row.kind = ReplayCauseRawRecordRowKind::Value;
    strcpy_s( row.label, sizeof( row.label ), label );
    strcpy_s( row.value, sizeof( row.value ), value );
    strcpy_s( row.unit, sizeof( row.unit ), unit );
}

template <typename Integer>
void AddRawInteger( ReplayCauseRawRecordProjection& projection, const char* label, Integer value,
                    const char* unit = "" ) noexcept
{
    char text[48] = {};
    FormatInteger( value, text, sizeof( text ) );
    AddRawValue( projection, label, text, unit );
}

void AddRawFloat( ReplayCauseRawRecordProjection& projection, const char* label, float value, const char* unit ) noexcept
{
    char text[48] = {};
    FormatFloat( value, text, sizeof( text ) );
    AddRawValue( projection, label, text, unit );
}

void AddRawVector( ReplayCauseRawRecordProjection& projection, const char* label, const Math::Vector::Vector3& value,
                   const char* unit ) noexcept
{
    char text[128] = {};
    FormatVector( value, text, sizeof( text ) );
    AddRawValue( projection, label, text, unit );
}
} // namespace

int ReplayCauseSolverDetailIterationCount( const ReplayCauseInspectionView& inspection, std::size_t contactRow ) noexcept
{
    if ( contactRow >= inspection.solverDetailContacts.size() )
    {
        return 0;
    }

    const uint32_t featureId = inspection.solverDetailContacts[contactRow].featureId;
    int count = 0;

    for ( const Physics::PhysicsPipelineRecord& record : inspection.solverDetailPipelineRecords )
    {
        if ( record.stage == Physics::PhysicsPipelineStage::SolverIteration && record.featureId == featureId )
        {
            ++count;
        }
    }

    return count;
}

ReplayCauseSolverPanelRowText BuildReplayCauseSolverPanelRowText( const ReplayCauseInspectionView& inspection,
                                                                  int rowIndex ) noexcept
{
    ReplayCauseSolverPanelRowText text;

    if ( rowIndex < 0 || static_cast<std::size_t>( rowIndex ) >= inspection.solverDetailContacts.size() )
    {
        return text;
    }

    const Physics::PhysicsSolverPersistentContactSample&
        contact = inspection.solverDetailContacts[static_cast<std::size_t>( rowIndex )];
    Math::Vector::Vector3 point = Math::Vector::ZERO_VECTOR;
    const bool hasPresentationPoint = static_cast<std::size_t>( rowIndex ) < inspection.contactPresentation.pointCount;

    if ( hasPresentationPoint )
    {
        point = inspection.contactPresentation.points[static_cast<std::size_t>( rowIndex )].point;
    }

    float previousNormalImpulse = 0.0f;
    bool hasPreviousNormalImpulse = false;

    for ( const Physics::PhysicsPipelineRecord& record : inspection.solverDetailPipelineRecords )
    {
        if ( record.featureId != contact.featureId )
        {
            continue;
        }

        if ( record.stage == Physics::PhysicsPipelineStage::ManifoldRow && !hasPresentationPoint )
        {
            point = record.point;
        }
        else if ( record.stage == Physics::PhysicsPipelineStage::WarmStart && !hasPreviousNormalImpulse )
        {
            previousNormalImpulse = record.scalarB;
            hasPreviousNormalImpulse = true;
        }
    }

    sprintf_s( text.headline, sizeof( text.headline ), "ROW %d  FEATURE %u  BODIES %d / %d  POINT (%.4f, %.4f, %.4f)",
               rowIndex, contact.featureId, contact.bodyA, contact.bodyB, point.x, point.y, point.z );
    sprintf_s( text.basis, sizeof( text.basis ), "n (%.4f %.4f %.4f)  t1 (%.4f %.4f %.4f)  t2 (%.4f %.4f %.4f)",
               contact.normal.x, contact.normal.y, contact.normal.z, contact.tangent1.x, contact.tangent1.y,
               contact.tangent1.z, contact.tangent2.x, contact.tangent2.y, contact.tangent2.z );
    sprintf_s( text.geometry, sizeof( text.geometry ), "rA (%.4f %.4f %.4f)  rB (%.4f %.4f %.4f)  penetration %.5f",
               contact.rA.x, contact.rA.y, contact.rA.z, contact.rB.x, contact.rB.y, contact.rB.z, contact.penetration );
    sprintf_s( text.masses, sizeof( text.masses ),
               "normalMass %.5f  tangentMass (%.5f, %.5f)  bias %.5f  frictionLimit %.5f", contact.normalMass,
               contact.tangentMass1, contact.tangentMass2, contact.bias, contact.frictionLimit );
    sprintf_s( text.impulses, sizeof( text.impulses ),
               "accN %.5f  accT1 %.5f  accT2 %.5f  warm-start %s  previous normal impulse %.5f", contact.accN, contact.accT1,
               contact.accT2, contact.warmStarted ? "YES" : "NO", previousNormalImpulse );
    return text;
}

ReplayCauseSummaryText BuildReplayCauseSummaryText( const ReplayCauseInspectionView& inspection, int rowIndex ) noexcept
{
    ReplayCauseSummaryText text;

    if ( rowIndex < 0 || static_cast<std::size_t>( rowIndex ) >= inspection.solverDetailContacts.size() )
    {
        return text;
    }

    const Physics::PhysicsSolverPersistentContactSample&
        contact = inspection.solverDetailContacts[static_cast<std::size_t>( rowIndex )];
    const float frictionMagnitude = std::sqrt( contact.accT1 * contact.accT1 + contact.accT2 * contact.accT2 );

    sprintf_s( text.normalImpulse, sizeof( text.normalImpulse ), "%.5f mass*u/s", contact.accN );
    sprintf_s( text.frictionImpulse, sizeof( text.frictionImpulse ), "%.5f mass*u/s", frictionMagnitude );
    sprintf_s( text.penetration, sizeof( text.penetration ), "%.5f u", contact.penetration );
    sprintf_s( text.effectiveMass, sizeof( text.effectiveMass ), "%.5f mass", contact.normalMass );
    sprintf_s( text.identity, sizeof( text.identity ), "ROW %d  FEATURE %u  BODIES %d / %d  %s", rowIndex, contact.featureId,
               contact.bodyA, contact.bodyB, contact.isTerrain ? "TERRAIN" : "OBJECT" );
    sprintf_s( text.dynamics, sizeof( text.dynamics ),
               "bias %.5f   friction limit %.5f   tangent mass %.5f / %.5f   manifold points %u", contact.bias,
               contact.frictionLimit, contact.tangentMass1, contact.tangentMass2,
               static_cast<unsigned>( contact.manifoldPointCount ) );
    sprintf_s( text.policy, sizeof( text.policy ), "warm %s   resting %s   tangent friction %s   coupled %s   sleep %s",
               contact.warmStarted ? "YES" : "NO", contact.supportsRestingPolicy ? "YES" : "NO",
               contact.allowsTangentFriction ? "YES" : "NO", contact.normalCoupledFriction ? "YES" : "NO",
               contact.inhibitsSleep ? "INHIBITED" : "ALLOWED" );
    return text;
}

ReplayCauseRawRecordProjection BuildReplayCauseRawRecordProjection( const ReplayCauseInspectionView& inspection,
                                                                    int rowIndex ) noexcept
{
    ReplayCauseRawRecordProjection projection;

    if ( rowIndex < 0 || static_cast<std::size_t>( rowIndex ) >= inspection.solverDetailContacts.size() )
    {
        return projection;
    }

    const Physics::PhysicsSolverPersistentContactSample&
        contact = inspection.solverDetailContacts[static_cast<std::size_t>( rowIndex )];

    Math::Vector::Vector3 point = Math::Vector::ZERO_VECTOR;
    const bool hasPresentationPoint = static_cast<std::size_t>( rowIndex ) < inspection.contactPresentation.pointCount;

    if ( hasPresentationPoint )
    {
        point = inspection.contactPresentation.points[static_cast<std::size_t>( rowIndex )].point;
    }
    else
    {
        for ( const Physics::PhysicsPipelineRecord& record : inspection.solverDetailPipelineRecords )
        {
            if ( record.stage == Physics::PhysicsPipelineStage::ManifoldRow && record.featureId == contact.featureId )
            {
                point = record.point;
                break;
            }
        }
    }

    // IDENTITY
    AddRawSection( projection, "IDENTITY" );
    AddRawInteger( projection, "Row Index", rowIndex );
    AddRawInteger( projection, "Feature ID", contact.featureId );
    AddRawInteger( projection, "Body A", contact.bodyA );
    AddRawInteger( projection, "Body B", contact.bodyB );
    AddRawInteger( projection, "Manifold Points", static_cast<unsigned>( contact.manifoldPointCount ) );
    AddRawInteger( projection, "Source Frame", static_cast<unsigned long long>( inspection.targetFrame ) );
    AddRawValue( projection, "Source Kind",
                 inspection.seekSource == ReplayCauseSeekSource::Prediction ? "PREDICTION" : "RECORDED" );

    if ( contact.key != 0 )
    {
        AddRawInteger( projection, "Persistent Key", contact.key );
    }

    // GEOMETRY
    AddRawSection( projection, "GEOMETRY" );
    AddRawVector( projection, "Contact Point", point, "u" );
    AddRawVector( projection, "Arm rA", contact.rA, "u" );
    AddRawVector( projection, "Arm rB", contact.rB, "u" );
    AddRawFloat( projection, "Penetration", contact.penetration, "u" );
    AddRawValue( projection, "Terrain Contact", contact.isTerrain ? "YES" : "NO" );

    if ( contact.isTerrain )
    {
        AddRawVector( projection, "Terrain Normal", contact.terrainNormal, "" );
    }

    // CONTACT BASIS
    AddRawSection( projection, "CONTACT BASIS" );
    AddRawVector( projection, "Normal n", contact.normal, "" );
    AddRawVector( projection, "Tangent t1", contact.tangent1, "" );
    AddRawVector( projection, "Tangent t2", contact.tangent2, "" );

    // SOLVER VALUES
    AddRawSection( projection, "SOLVER VALUES" );
    AddRawFloat( projection, "Normal Mass", contact.normalMass, "mass" );
    AddRawFloat( projection, "Tangent Mass 1", contact.tangentMass1, "mass" );
    AddRawFloat( projection, "Tangent Mass 2", contact.tangentMass2, "mass" );
    AddRawFloat( projection, "Bias Velocity", contact.bias, "u/s" );
    AddRawFloat( projection, "Friction Limit", contact.frictionLimit, "mass*u/s" );

    for ( const Physics::PhysicsPipelineRecord& record : inspection.solverDetailPipelineRecords )
    {
        if ( record.featureId == contact.featureId && record.stage == Physics::PhysicsPipelineStage::PositionCorrection )
        {
            AddRawFloat( projection, "Position Correction", record.scalarA, "u" );
            break;
        }
    }

    // ACCUMULATED IMPULSES
    AddRawSection( projection, "ACCUMULATED IMPULSES" );
    AddRawFloat( projection, "Normal Impulse accN", contact.accN, "mass*u/s" );
    AddRawFloat( projection, "Tangent Impulse accT1", contact.accT1, "mass*u/s" );
    AddRawFloat( projection, "Tangent Impulse accT2", contact.accT2, "mass*u/s" );
    const float frictionMagnitude = std::sqrt( contact.accT1 * contact.accT1 + contact.accT2 * contact.accT2 );
    AddRawFloat( projection, "Tangent Magnitude |T|", frictionMagnitude, "mass*u/s" );

    if ( contact.isTerrain && contact.terrainWarmStart != 0.0f )
    {
        AddRawFloat( projection, "Terrain Warm Start", contact.terrainWarmStart, "mass*u/s" );
    }

    // FLAGS & POLICY
    AddRawSection( projection, "FLAGS & POLICY" );
    AddRawValue( projection, "Warm Started", contact.warmStarted ? "YES" : "NO" );
    AddRawValue( projection, "Supports Resting Policy", contact.supportsRestingPolicy ? "YES" : "NO" );
    AddRawValue( projection, "Allows Tangent Friction", contact.allowsTangentFriction ? "YES" : "NO" );
    AddRawValue( projection, "Normal Coupled Friction", contact.normalCoupledFriction ? "YES" : "NO" );
    AddRawValue( projection, "Sleep Inhibition", contact.inhibitsSleep ? "INHIBITED" : "ALLOWED" );

    return projection;
}

bool SerializeReplayCauseRawRecord( const ReplayCauseRawRecordProjection& projection, char* destination,
                                    std::size_t destinationCapacity ) noexcept
{
    if ( !destination || destinationCapacity == 0u || projection.rowCount == 0u )
    {
        if ( destination && destinationCapacity > 0u )
        {
            destination[0] = '\0';
        }

        return false;
    }

    FixedTextWriter writer( destination, destinationCapacity );

    for ( std::size_t i = 0; i < projection.rowCount; ++i )
    {
        const ReplayCauseRawRecordRow& row = projection.rows[i];

        if ( row.kind == ReplayCauseRawRecordRowKind::Section )
        {
            if ( i > 0 )
            {
                (void)writer.Append( "\n" );
            }

            (void)writer.Append( "[" );
            (void)writer.Append( row.label );
            (void)writer.Append( "]\n" );
        }
        else
        {
            (void)writer.Append( row.label );
            (void)writer.Append( ": " );
            (void)writer.Append( row.value );

            if ( row.unit[0] != '\0' )
            {
                (void)writer.Append( " " );
                (void)writer.Append( row.unit );
            }

            (void)writer.Append( "\n" );
        }
    }

    return writer.Valid();
}

ReplayCauseIterationsProjection BuildReplayCauseIterationsProjection( const ReplayCauseInspectionView& inspection,
                                                                      int rowIndex ) noexcept
{
    ReplayCauseIterationsProjection projection;

    if ( inspection.solverDetailContacts.empty() || rowIndex < 0 ||
         static_cast<std::size_t>( rowIndex ) >= inspection.solverDetailContacts.size() )
    {
        sprintf_s( projection.summary, sizeof( projection.summary ), "No contact selected" );
        return projection;
    }

    const Physics::PhysicsSolverPersistentContactSample&
        contact = inspection.solverDetailContacts[static_cast<std::size_t>( rowIndex )];

    sprintf_s( projection.summary, sizeof( projection.summary ), "Feature %u  Body %d <-> %d  limit=%.3g", contact.featureId,
               contact.bodyA, contact.bodyB, contact.frictionLimit );

    for ( const Physics::PhysicsPipelineRecord& record : inspection.solverDetailPipelineRecords )
    {
        if ( record.stage == Physics::PhysicsPipelineStage::VelocityWriteback )
        {
            if ( record.bodyA != contact.bodyA && record.bodyA != contact.bodyB )
            {
                continue;
            }
        }
        else if ( record.featureId != contact.featureId )
        {
            continue;
        }

        if ( projection.rowCount >= REPLAY_CAUSE_ITERATIONS_ROW_CAPACITY )
        {
            break;
        }

        ReplayCauseIterationRow row;

        if ( record.stage == Physics::PhysicsPipelineStage::WarmStart )
        {
            row.kind = ReplayCauseIterationRowKind::WarmStart;
            sprintf_s( row.stage, sizeof( row.stage ), "Warm Start" );
            sprintf_s( row.accNormal, sizeof( row.accNormal ), "%.4g", record.scalarB );
            sprintf_s( row.frictionLimit, sizeof( row.frictionLimit ), "%.4g", record.scalarC );
            const bool isWarm = ( record.scalarA > 0.0f );
            sprintf_s( row.status, sizeof( row.status ), isWarm ? "ACTIVE" : "NONE" );
            sprintf_s( row.details, sizeof( row.details ), "accN=%.3g limit=%.3g", record.scalarB, record.scalarC );
            projection.rows[projection.rowCount++] = row;
        }
        else if ( record.stage == Physics::PhysicsPipelineStage::SolverIteration )
        {
            row.kind = ReplayCauseIterationRowKind::SolverIteration;
            row.iterationIndex = record.iteration;
            sprintf_s( row.stage, sizeof( row.stage ), "Iter %d", record.iteration );
            sprintf_s( row.deltaNormal, sizeof( row.deltaNormal ), "%.4g", record.scalarA );
            sprintf_s( row.accNormal, sizeof( row.accNormal ), "%.4g", record.scalarB );
            sprintf_s( row.tangentImpulse, sizeof( row.tangentImpulse ), "%.4g", record.scalarC );
            sprintf_s( row.frictionLimit, sizeof( row.frictionLimit ), "%.4g", contact.frictionLimit );
            const bool clamped = contact.frictionLimit > 0.0f && record.scalarC >= contact.frictionLimit - 1.0e-5f;
            sprintf_s( row.status, sizeof( row.status ), clamped ? "CLAMP" : "FREE" );
            projection.rows[projection.rowCount++] = row;
        }
        else if ( record.stage == Physics::PhysicsPipelineStage::PositionCorrection )
        {
            row.kind = ReplayCauseIterationRowKind::PositionCorrection;
            sprintf_s( row.stage, sizeof( row.stage ), "Pos Correct" );
            sprintf_s( row.deltaNormal, sizeof( row.deltaNormal ), "%.4g", record.scalarA );
            sprintf_s( row.status, sizeof( row.status ), "APPLIED" );
            sprintf_s( row.details, sizeof( row.details ), "Penetration repair" );
            projection.rows[projection.rowCount++] = row;
        }
        else if ( record.stage == Physics::PhysicsPipelineStage::CacheStore )
        {
            row.kind = ReplayCauseIterationRowKind::CacheStore;
            sprintf_s( row.stage, sizeof( row.stage ), "Cache Store" );
            sprintf_s( row.accNormal, sizeof( row.accNormal ), "%.4g", record.scalarA );
            sprintf_s( row.tangentImpulse, sizeof( row.tangentImpulse ), "%.4g",
                       std::hypot( record.scalarB, record.scalarC ) );
            sprintf_s( row.status, sizeof( row.status ), "SAVED" );
            sprintf_s( row.details, sizeof( row.details ), "Cached for next tick" );
            projection.rows[projection.rowCount++] = row;
        }
        else if ( record.stage == Physics::PhysicsPipelineStage::VelocityWriteback )
        {
            row.kind = ReplayCauseIterationRowKind::VelocityWriteback;
            sprintf_s( row.stage, sizeof( row.stage ), "Writeback" );
            sprintf_s( row.status, sizeof( row.status ), "COMMITTED" );
            sprintf_s( row.details, sizeof( row.details ), "Body %d pos=(%.2f,%.2f,%.2f) |v|=%.2f |w|=%.2f", record.bodyA,
                       record.point.x, record.point.y, record.point.z, record.scalarA, record.scalarB );
            projection.rows[projection.rowCount++] = row;
        }
    }

    return projection;
}

bool ShouldBeginReplayCauseAftermath( const ReplayCauseInspectionView& inspection, bool spaceDown ) noexcept
{
    return spaceDown && inspection.mode == ReplayCauseInspectionMode::DetailPaused;
}


bool ShouldBeginReplayCauseReturn( const ReplayCauseInspectionView& inspection, bool nonSelectionClick,
                                   bool scrubExit ) noexcept
{
    return inspection.mode != ReplayCauseInspectionMode::Inactive &&
           ( nonSelectionClick || scrubExit || inspection.mode == ReplayCauseInspectionMode::Returning );
}

ReplayCauseInspectorLayout BuildReplayCauseInspectorLayout( const ReplayCauseInspectionView& inspection,
                                                            const RunReplayCauseTreeState& causeTree, int screenWidth,
                                                            int screenHeight, float drawerProgress ) noexcept
{
    PROFILE_SCOPED( "Frame/Replay/CauseInspection/PanelLayout" );
    (void)screenHeight;
    ReplayCauseInspectorLayout layout;
    int maximumIterations = 0;

    for ( std::size_t row = 0; row < inspection.solverDetailContacts.size(); ++row )
    {
        maximumIterations = (std::max)( maximumIterations, ReplayCauseSolverDetailIterationCount( inspection, row ) );
    }

    const int iterationLines = ( maximumIterations + REPLAY_CAUSE_SOLVER_PANEL_ITERATIONS_PER_LINE - 1 ) /
                               REPLAY_CAUSE_SOLVER_PANEL_ITERATIONS_PER_LINE;
    layout.rowHeight = REPLAY_CAUSE_SOLVER_PANEL_BASE_ROW_HEIGHT +
                       static_cast<float>( iterationLines ) * REPLAY_CAUSE_SOLVER_PANEL_ITERATION_LINE_HEIGHT;

    const float targetDrawerWidth = ReplayOverlay::ReplayCauseWindowAttachedWidth( causeTree, screenWidth,
                                                                                   REPLAY_CAUSE_INSPECTOR_DRAWER_WIDTH,
                                                                                   REPLAY_CAUSE_INSPECTOR_DRAWER_MIN_WIDTH );
    layout.drawerProgress = std::clamp( drawerProgress, 0.0f, 1.0f );
    const float visibleDrawerWidth = targetDrawerWidth * layout.drawerProgress;
    layout.hierarchy = ReplayOverlay::ReplayCauseWindowRect( causeTree );
    layout.hierarchyTitle = ReplayOverlay::ReplayCauseWindowTitleRect( causeTree );
    layout.hierarchyScrollbar = { layout.hierarchy.x + layout.hierarchy.w - REPLAY_CAUSE_INSPECTOR_SCROLLBAR_WIDTH - 4.0f,
                                  layout.hierarchy.y + ReplayOverlay::REPLAY_CAUSE_WINDOW_TITLE_HEIGHT + 10.0f,
                                  REPLAY_CAUSE_INSPECTOR_SCROLLBAR_WIDTH,
                                  (std::max)( 0.0f, layout.hierarchy.h - ReplayOverlay::REPLAY_CAUSE_WINDOW_TITLE_HEIGHT -
                                                        22.0f ) };
    layout.resize = ReplayOverlay::ReplayCauseWindowResizeRect( causeTree );
    layout.targetDrawer = { layout.hierarchy.x - targetDrawerWidth, layout.hierarchy.y, targetDrawerWidth,
                            layout.hierarchy.h };
    layout.drawer = { layout.hierarchy.x - visibleDrawerWidth, layout.hierarchy.y, targetDrawerWidth, layout.hierarchy.h };
    layout.visibleDrawer = { layout.drawer.x, layout.drawer.y, visibleDrawerWidth, layout.drawer.h };
    layout.compound = { layout.drawer.x, layout.hierarchy.y, layout.hierarchy.w + visibleDrawerWidth, layout.hierarchy.h };
    layout.targetCompound = { layout.targetDrawer.x, layout.hierarchy.y, layout.hierarchy.w + targetDrawerWidth,
                              layout.hierarchy.h };
    layout.sharedSeam = { layout.hierarchy.x - REPLAY_CAUSE_INSPECTOR_SHARED_SEAM_WIDTH, layout.hierarchy.y,
                          REPLAY_CAUSE_INSPECTOR_SHARED_SEAM_WIDTH, layout.hierarchy.h };
    layout.drawerTitle = { layout.drawer.x, layout.drawer.y,
                           (std::max)( 0.0f, targetDrawerWidth - REPLAY_CAUSE_INSPECTOR_CLOSE_SIZE -
                                                 REPLAY_CAUSE_INSPECTOR_PADDING * 2.0f ),
                           ReplayOverlay::REPLAY_CAUSE_WINDOW_TITLE_HEIGHT };
    layout.drawerClose = { layout.drawer.x + targetDrawerWidth - REPLAY_CAUSE_INSPECTOR_PADDING -
                               REPLAY_CAUSE_INSPECTOR_CLOSE_SIZE,
                           layout.drawer.y + 8.0f, REPLAY_CAUSE_INSPECTOR_CLOSE_SIZE, REPLAY_CAUSE_INSPECTOR_CLOSE_SIZE };

    const float tabWidth = (std::max)( 0.0f, ( targetDrawerWidth - REPLAY_CAUSE_INSPECTOR_PADDING * 2.0f ) / 3.0f );

    for ( std::size_t tab = 0; tab < layout.tabs.size(); ++tab )
    {
        layout.tabs[tab] = { layout.drawer.x + REPLAY_CAUSE_INSPECTOR_PADDING + tabWidth * static_cast<float>( tab ),
                             layout.drawer.y + REPLAY_CAUSE_INSPECTOR_DRAWER_HEADER_HEIGHT, tabWidth,
                             REPLAY_CAUSE_INSPECTOR_TAB_HEIGHT };
    }

    layout.content = { layout.drawer.x + REPLAY_CAUSE_INSPECTOR_PADDING,
                       layout.drawer.y + REPLAY_CAUSE_INSPECTOR_DRAWER_HEADER_HEIGHT + REPLAY_CAUSE_INSPECTOR_TAB_HEIGHT +
                           REPLAY_CAUSE_INSPECTOR_PADDING,
                       (std::max)( 0.0f, targetDrawerWidth - REPLAY_CAUSE_INSPECTOR_PADDING * 2.0f ),
                       (std::max)( 0.0f, layout.drawer.h - REPLAY_CAUSE_INSPECTOR_DRAWER_HEADER_HEIGHT -
                                             REPLAY_CAUSE_INSPECTOR_TAB_HEIGHT - REPLAY_CAUSE_INSPECTOR_PADDING * 2.0f ) };
    layout.drawerScrollbar = { layout.content.x + layout.content.w - REPLAY_CAUSE_INSPECTOR_SCROLLBAR_WIDTH,
                               layout.content.y, REPLAY_CAUSE_INSPECTOR_SCROLLBAR_WIDTH, layout.content.h };

    const float rawCopyGap = 8.0f;
    layout.rawCopy = { layout.content.x, layout.content.y + layout.content.h - REPLAY_CAUSE_RAW_RECORD_COPY_HEIGHT,
                       layout.content.w, REPLAY_CAUSE_RAW_RECORD_COPY_HEIGHT };
    layout.rawTable = { layout.content.x, layout.content.y, layout.content.w,
                        (std::max)( 0.0f, layout.content.h - REPLAY_CAUSE_RAW_RECORD_COPY_HEIGHT - rawCopyGap ) };
    layout.rawVisibleRows = static_cast<int>( layout.rawTable.h / REPLAY_CAUSE_RAW_RECORD_ROW_HEIGHT );

    layout.iterationsTable = { layout.content.x, layout.content.y + 22.0f, layout.content.w,
                               (std::max)( 0.0f, layout.content.h - 22.0f ) };
    layout.iterationsVisibleRows = static_cast<int>( layout.iterationsTable.h / REPLAY_CAUSE_ITERATIONS_ROW_HEIGHT );

    const bool hasRows = inspection.solverDetailAvailability == ReplayCauseSolverDetailAvailability::Available &&
                         !inspection.solverDetailContacts.empty();

    if ( hasRows )
    {
        // Invariant: the fixed drawer footprint decides how many complete rows
        // fit; exact evidence never grows the joined surface vertically.
        layout.visibleRows = std::clamp( static_cast<int>( layout.content.h / layout.rowHeight ), 0,
                                         REPLAY_CAUSE_SOLVER_PANEL_VISIBLE_ROWS );
    }

    return layout;
}

bool ReplayCauseInspectorContainsPoint( const ReplayCauseInspectorLayout& layout, int x, int y ) noexcept
{
    return PointInside( layout.compound, x, y );
}

bool ReplayCauseInspectorDrawerTitleContainsPoint( const ReplayCauseInspectorLayout& layout, int x, int y ) noexcept
{
    return PointInside( layout.visibleDrawer, x, y ) && PointInside( layout.drawerTitle, x, y );
}

float EvaluateReplayCauseTransitionProgress( double elapsedSeconds ) noexcept
{
    const double u = std::clamp( elapsedSeconds / REPLAY_CAUSE_TRANSITION_SECONDS, 0.0, 1.0 );
    const double remaining = 1.0 - u;
    return static_cast<float>( 1.0 - remaining * remaining * remaining );
}

ReplayFrameIndex EvaluateReplayCauseTransitionFrame( ReplayFrameIndex sourceFrame, ReplayFrameIndex targetFrame,
                                                     float easedProgress ) noexcept
{
    const double progress = std::clamp( static_cast<double>( easedProgress ), 0.0, 1.0 );

    if ( progress >= 1.0 )
    {
        return targetFrame;
    }

    if ( sourceFrame <= targetFrame )
    {
        const ReplayFrameIndex distance = targetFrame - sourceFrame;
        return sourceFrame + static_cast<ReplayFrameIndex>( static_cast<double>( distance ) * progress );
    }

    const ReplayFrameIndex distance = sourceFrame - targetFrame;
    return sourceFrame - static_cast<ReplayFrameIndex>( static_cast<double>( distance ) * progress );
}

bool ReplayCauseSeekResult::CanTransport() const noexcept
{
    return availability == ReplayCauseSeekAvailability::Available;
}

const char* ReplayCauseSeekResult::Feedback() const noexcept
{
    return CanTransport() ? "" : REPLAY_FRAME_EXPIRED_FEEDBACK;
}

bool ReplayCauseSolverDetailResult::HasDetail() const noexcept
{
    return availability == ReplayCauseSolverDetailAvailability::Available;
}

std::size_t ReplayCauseSolverDetailResult::SourceContactCount() const noexcept
{
    return sourceContacts.size();
}

std::size_t ReplayCauseSolverDetailResult::SourcePipelineCount() const noexcept
{
    return sourcePipelineRecords.size();
}

const Physics::PhysicsSolverPersistentContactSample*
ReplayCauseSolverDetailResult::SourceContactAt( std::size_t index ) const noexcept
{
    return index < sourceContacts.size() ? &sourceContacts[index] : nullptr;
}

const Physics::PhysicsPipelineRecord* ReplayCauseSolverDetailResult::SourcePipelineAt( std::size_t index ) const noexcept
{
    return index < sourcePipelineRecords.size() ? &sourcePipelineRecords[index] : nullptr;
}

const Physics::PhysicsSolverPersistentContactSample*
ReplayCauseSolverDetailResult::ContactRowAt( std::size_t detailRow ) const noexcept
{
    if ( !HasDetail() )
    {
        return nullptr;
    }

    for ( std::size_t sourceIndex = 0; sourceIndex < SourceContactCount(); ++sourceIndex )
    {
        const Physics::PhysicsSolverPersistentContactSample* contact = SourceContactAt( sourceIndex );

        if ( !contact || !ContactPairMatches( *contact, bodyA, bodyB, terrain ) )
        {
            continue;
        }

        if ( detailRow == 0u )
        {
            return contact;
        }

        --detailRow;
    }

    return nullptr;
}

const Physics::PhysicsPipelineRecord*
ReplayCauseSolverDetailResult::PipelineRecordAt( std::size_t detailRecord ) const noexcept
{
    if ( !HasDetail() )
    {
        return nullptr;
    }

    for ( std::size_t sourceIndex = 0; sourceIndex < SourcePipelineCount(); ++sourceIndex )
    {
        const Physics::PhysicsPipelineRecord* record = SourcePipelineAt( sourceIndex );

        if ( !record || !PipelineRecordMatches( *this, *record ) )
        {
            continue;
        }

        if ( detailRecord == 0u )
        {
            return record;
        }

        --detailRecord;
    }

    return nullptr;
}

ReplayCauseSolverDetailResult EvaluateReplayCauseSolverDetail( const RunReplayCauseTreeRow& row,
                                                               const ReplayCauseSeekResult& seek,
                                                               const ReplayCauseSolverDetailSource& source ) noexcept
{
    PROFILE_SCOPED( "Frame/Replay/CauseInspection/SolverDetailLookup" );
    ReplayCauseSolverDetailResult result;
    result.frame = row.firstFrame;

    if ( !seek.CanTransport() )
    {
        result.availability = ReplayCauseSolverDetailAvailability::ReplayFrameExpired;
        return result;
    }

    const bool exactRowKind = row.kind == RunReplayCauseTreeRowKind::Manifold ||
                              row.kind == RunReplayCauseTreeRowKind::SolverRow;
    const bool predictionSource = row.prediction && seek.source == ReplayCauseSeekSource::Prediction;
    const bool recordedSource = !row.prediction && seek.source == ReplayCauseSeekSource::SolverHistory;

    // Hazard: the currently visible or nearest retained diagnostics can look
    // structurally identical. Only the explicit frame and bank stamps license
    // a join; a reused numeric offset in a replacement bank must fail closed.
    if ( seek.frame != row.firstFrame || !exactRowKind || ( !predictionSource && !recordedSource ) )
    {
        return result;
    }

    if ( predictionSource )
    {
        const ReplayPredictionEvidenceIdentity expected {
            row.sourceGeneration, ReplayPredictionDetailMode::High, row.sourceBankEpoch,
            row.firstFrame,       row.sourceTopologyVersion,        row.sourcePublicationVersion,
        };

        const ReplayPredictionCauseEvidenceQuery& query = source.prediction ? source.prediction->query
                                                                            : ReplayPredictionCauseEvidenceQuery {};

        if ( !source.prediction || !source.prediction->available || source.prediction->identity != expected ||
             query.identity != expected || query.contactIndex != row.contactIndex ||
             query.pipelineIndex != row.pipelineIndex || query.focusedBody != row.modelRow.value ||
             query.counterpartBody != row.counterpartModelRow.value || query.featureId != row.featureId ||
             query.terrain != row.terrain || !query.sourceHighDetail || !row.sourceHighDetail )
        {
            return result;
        }

        result.sourceContacts = source.prediction->ContactRows();
        result.sourcePipelineRecords = source.prediction->PipelineRows();
        result.bodyA = source.prediction->bodyA;
        result.bodyB = source.prediction->bodyB;
        result.terrain = source.prediction->terrain;
        result.selectedDetailContactRow = source.prediction->selectedContactRow;
        result.contactRowCount = source.prediction->contactCount;
        result.pipelineRecordCount = source.prediction->pipelineCount;
        result.availability = ReplayCauseSolverDetailAvailability::Available;
        return result;
    }
    else if ( source.frame != row.firstFrame )
    {
        return result;
    }

    result.sourceContacts = source.contacts;
    result.sourcePipelineRecords = source.pipelineRecords;

    if ( row.contactIndex < 0 || static_cast<std::size_t>( row.contactIndex ) >= result.SourceContactCount() )
    {
        return ReplayCauseSolverDetailResult { .frame = row.firstFrame };
    }

    const Physics::PhysicsSolverPersistentContactSample* anchorValue = result.SourceContactAt( static_cast<std::size_t>( row.contactIndex ) );

    if ( !anchorValue )
    {
        return ReplayCauseSolverDetailResult { .frame = row.firstFrame };
    }

    const Physics::PhysicsSolverPersistentContactSample& anchor = *anchorValue;
    const bool anchorTerrain = anchor.isTerrain || anchor.bodyB < 0;
    const bool focusedBodyMatches = row.modelRow.value == anchor.bodyA || row.modelRow.value == anchor.bodyB;
    const int anchorOtherBody = row.modelRow.value == anchor.bodyA ? anchor.bodyB : anchor.bodyA;

    if ( !focusedBodyMatches || row.featureId < 0 || static_cast<uint32_t>( row.featureId ) != anchor.featureId ||
         row.terrain != anchorTerrain ||
         ( anchorTerrain ? row.counterpartModelRow.value >= 0 : row.counterpartModelRow.value != anchorOtherBody ) )
    {
        return result;
    }

    result.bodyA = anchor.bodyA;
    result.bodyB = anchor.bodyB;
    result.terrain = anchorTerrain;

    if ( predictionSource )
    {
        if ( row.pipelineIndex < 0 || static_cast<std::size_t>( row.pipelineIndex ) >= result.SourcePipelineCount() )
        {
            return ReplayCauseSolverDetailResult { .frame = row.firstFrame };
        }

        const Physics::PhysicsPipelineRecord* sequenceAnchor = result.SourcePipelineAt( static_cast<std::size_t>( row.pipelineIndex ) );

        if ( !sequenceAnchor || !IsSolverDetailPipelineStage( sequenceAnchor->stage ) ||
             sequenceAnchor->featureId != anchor.featureId ||
             !BodyPairMatches( sequenceAnchor->bodyA, sequenceAnchor->bodyB, anchor.bodyA, anchor.bodyB, anchorTerrain ) )
        {
            return ReplayCauseSolverDetailResult { .frame = row.firstFrame };
        }
    }

    std::size_t detailContactIndex = 0;

    for ( std::size_t contactIndex = 0; contactIndex < result.SourceContactCount(); ++contactIndex )
    {
        const Physics::PhysicsSolverPersistentContactSample* contact = result.SourceContactAt( contactIndex );

        if ( contact && ContactPairMatches( *contact, result.bodyA, result.bodyB, result.terrain ) )
        {
            if ( contactIndex == static_cast<std::size_t>( row.contactIndex ) ||
                 ( row.featureId >= 0 && static_cast<uint32_t>( row.featureId ) == contact->featureId ) )
            {
                result.selectedDetailContactRow = static_cast<int>( detailContactIndex );
            }

            ++detailContactIndex;
            ++result.contactRowCount;
        }
    }

    if ( result.selectedDetailContactRow < 0 && result.contactRowCount > 0 )
    {
        result.selectedDetailContactRow = 0;
    }

    if ( result.contactRowCount == 0u )
    {
        return ReplayCauseSolverDetailResult { .frame = row.firstFrame };
    }

    for ( std::size_t recordIndex = 0; recordIndex < result.SourcePipelineCount(); ++recordIndex )
    {
        const Physics::PhysicsPipelineRecord* record = result.SourcePipelineAt( recordIndex );

        if ( record && PipelineRecordMatches( result, *record ) )
        {
            ++result.pipelineRecordCount;
        }
    }

    result.availability = ReplayCauseSolverDetailAvailability::Available;
    return result;
}

Rendering::ContactManifoldPresentation BuildReplayCauseContactPresentation( const ReplayCauseSolverDetailResult& detail,
                                                                            const ReplaySolverFrameSample& sample ) noexcept
{
    PROFILE_SCOPED( "Frame/Replay/CauseInspection/ManifoldPresentation" );
    Rendering::ContactManifoldPresentation presentation;

    if ( !detail.HasDetail() || sample.frameIndex != detail.frame || detail.contactRowCount == 0u )
    {
        return presentation;
    }

    auto findBody = [&]( int modelRow ) -> const ReplaySolverBodySample*
    {
        const auto found = std::find_if( sample.bodies.begin(), sample.bodies.end(),
                                         [&]( const ReplaySolverBodySample& body )
                                         { return body.modelRow.value == modelRow; } );
        return found == sample.bodies.end() ? nullptr : &*found;
    };

    auto publishBody = [&]( int modelRow, std::size_t presentationIndex ) -> bool
    {
        const ReplaySolverBodySample* body = findBody( modelRow );

        if ( !body )
        {
            return false;
        }

        Rendering::ContactBodyPosePresentation& pose = presentation.bodies[presentationIndex];
        pose.position = body->position;
        pose.orientation = Math::Orientation::Quaternion( body->orientation[0], body->orientation[1], body->orientation[2],
                                                          body->orientation[3] );
        pose.valid = true;
        ++presentation.bodyCount;
        return true;
    };

    if ( !publishBody( detail.bodyA, 0u ) || ( !detail.terrain && !publishBody( detail.bodyB, 1u ) ) )
    {
        return Rendering::ContactManifoldPresentation {};
    }

    // Invariant: Rendering receives a truthful bounded prefix. The explicit
    // truncation bit prevents eight presented points from claiming a larger
    // retained patch was complete.
    const std::size_t presentedContactCount = (std::min)( detail.contactRowCount,
                                                          Rendering::CONTACT_MANIFOLD_PRESENTATION_POINT_CAPACITY );
    presentation.truncated = detail.contactRowCount > presentedContactCount;

    for ( std::size_t contactIndex = 0; contactIndex < presentedContactCount; ++contactIndex )
    {
        const Physics::PhysicsSolverPersistentContactSample* contact = detail.ContactRowAt( contactIndex );

        if ( !contact )
        {
            return Rendering::ContactManifoldPresentation {};
        }

        const Physics::PhysicsPipelineRecord* manifoldRecord = nullptr;

        // Prefer the original narrowphase point when that bounded record still
        // exists. A missing record permits only the surviving solver row's own
        // pose-plus-arm point; discarded candidates are never reconstructed.
        for ( std::size_t pipelineIndex = 0; pipelineIndex < detail.pipelineRecordCount; ++pipelineIndex )
        {
            const Physics::PhysicsPipelineRecord* candidate = detail.PipelineRecordAt( pipelineIndex );

            if ( candidate && candidate->stage == Physics::PhysicsPipelineStage::ManifoldRow &&
                 candidate->featureId == contact->featureId )
            {
                manifoldRecord = candidate;
                break;
            }
        }

        Rendering::ContactPointPresentation& point = presentation.points[contactIndex];
        const ReplaySolverBodySample* contactBodyA = findBody( contact->bodyA );

        if ( !manifoldRecord && !contactBodyA )
        {
            return Rendering::ContactManifoldPresentation {};
        }

        point.point = manifoldRecord ? manifoldRecord->point : contactBodyA->position + contact->rA;
        point.normal = manifoldRecord ? manifoldRecord->normal
                                      : ( contact->isTerrain ? contact->terrainNormal : contact->normal );
        point.tangent1 = contact->tangent1;
        point.tangent2 = contact->tangent2;
        point.penetration = manifoldRecord ? manifoldRecord->scalarA : contact->penetration;
        point.exactSourcePoint = manifoldRecord != nullptr;
        ++presentation.pointCount;
    }

    return presentation;
}

Rendering::ContactManifoldPresentation BuildReplayCauseContactPresentation( const ReplayCauseSolverDetailResult& detail,
                                                                            const RunReplayPredictionFrame& frame ) noexcept
{
    PROFILE_SCOPED( "Frame/Replay/CauseInspection/PredictionManifoldPresentation" );
    Rendering::ContactManifoldPresentation presentation;

    if ( !detail.HasDetail() || frame.frameIndex != detail.frame || detail.contactRowCount == 0u )
    {
        return presentation;
    }

    const auto findBody = [&]( int modelRow ) -> const RunReplayPredictionBodySample*
    {
        const auto found = std::find_if( frame.bodies.begin(), frame.bodies.end(),
                                         [&]( const RunReplayPredictionBodySample& body )
                                         { return body.modelRow.value == modelRow; } );
        return found == frame.bodies.end() ? nullptr : &*found;
    };
    const auto publishBody = [&]( int modelRow, std::size_t presentationIndex )
    {
        const RunReplayPredictionBodySample* body = findBody( modelRow );

        if ( !body )
        {
            return false;
        }

        Rendering::ContactBodyPosePresentation& pose = presentation.bodies[presentationIndex];
        pose.position = body->position;
        pose.orientation = body->orientation;
        pose.valid = true;
        ++presentation.bodyCount;
        return true;
    };

    if ( !publishBody( detail.bodyA, 0u ) || ( !detail.terrain && !publishBody( detail.bodyB, 1u ) ) )
    {
        return {};
    }

    const std::size_t presentedContactCount = (std::min)( detail.contactRowCount,
                                                          Rendering::CONTACT_MANIFOLD_PRESENTATION_POINT_CAPACITY );
    presentation.truncated = detail.contactRowCount > presentedContactCount;

    for ( std::size_t contactIndex = 0; contactIndex < presentedContactCount; ++contactIndex )
    {
        const Physics::PhysicsSolverPersistentContactSample* contact = detail.ContactRowAt( contactIndex );

        if ( !contact )
        {
            return {};
        }

        const Physics::PhysicsPipelineRecord* manifoldRecord = nullptr;

        for ( std::size_t pipelineIndex = 0; pipelineIndex < detail.pipelineRecordCount; ++pipelineIndex )
        {
            const Physics::PhysicsPipelineRecord* candidate = detail.PipelineRecordAt( pipelineIndex );

            if ( candidate && candidate->stage == Physics::PhysicsPipelineStage::ManifoldRow &&
                 candidate->featureId == contact->featureId )
            {
                manifoldRecord = candidate;
                break;
            }
        }

        const RunReplayPredictionBodySample* contactBodyA = findBody( contact->bodyA );

        if ( !manifoldRecord && !contactBodyA )
        {
            return {};
        }

        Rendering::ContactPointPresentation& point = presentation.points[contactIndex];
        point.point = manifoldRecord ? manifoldRecord->point : contactBodyA->position + contact->rA;
        point.normal = manifoldRecord ? manifoldRecord->normal
                                      : ( contact->isTerrain ? contact->terrainNormal : contact->normal );
        point.tangent1 = contact->tangent1;
        point.tangent2 = contact->tangent2;
        point.penetration = manifoldRecord ? manifoldRecord->scalarA : contact->penetration;
        point.exactSourcePoint = manifoldRecord != nullptr;
        ++presentation.pointCount;
    }

    return presentation;
}

ReplayCauseSeekResult EvaluateReplayCauseSeek( const RunReplayCauseTreeRow& row, const ReplayRecorderStats& solverStats,
                                               std::span<const RunReplayPredictionFrame> predictionFrames ) noexcept
{
    ReplayCauseSeekResult result;
    result.frame = row.firstFrame;
    result.source = row.prediction ? ReplayCauseSeekSource::Prediction : ReplayCauseSeekSource::SolverHistory;

    if ( row.prediction )
    {
        const auto match = std::find_if( predictionFrames.begin(), predictionFrames.end(),
                                         [&]( const auto& frame ) { return frame.frameIndex == row.firstFrame; } );

        if ( match != predictionFrames.end() )
        {
            result.availability = ReplayCauseSeekAvailability::Available;
        }

        return result;
    }

    const ReplayFrameIndex retainedCount = static_cast<ReplayFrameIndex>( solverStats.sampleCount );
    const ReplayFrameIndex oldestFrame = solverStats.nextFrameIndex > retainedCount
                                             ? solverStats.nextFrameIndex - retainedCount
                                             : 0;

    if ( solverStats.sampleCount > 0 && row.firstFrame >= oldestFrame && row.firstFrame < solverStats.nextFrameIndex )
    {
        result.availability = ReplayCauseSeekAvailability::Available;
    }

    return result;
}

bool ReplayCauseInspection::Select( int rowIndex, const ReplayCauseSeekResult& seek, ReplayFrameIndex presentedFrame,
                                    bool simulationAlreadyPaused, double nowSeconds ) noexcept
{
    if ( rowIndex < 0 || !seek.CanTransport() )
    {
        return false;
    }

    // Invariant: zero is reserved for "no request" at the App boundary. A
    // wrap restarts at one so even a process-lifetime session keeps that seam.
    ++m_state.generation;

    if ( m_state.generation == 0 )
    {
        m_state.generation = 1;
    }

    if ( m_state.mode == ReplayCauseInspectionMode::Inactive )
    {
        m_state.ownsPause = !simulationAlreadyPaused;
    }
    else
    {
        // Direct retargeting from aftermath reacquires only the pause that this
        // inspection itself caused. A pre-existing operator pause stays external.
        m_state.ownsPause = m_state.ownsPause || !simulationAlreadyPaused;
    }

    m_state.mode = ReplayCauseInspectionMode::Transporting;
    m_state.sourceFrame = presentedFrame;
    m_state.targetFrame = seek.frame;
    m_state.presentedFrame = presentedFrame;
    m_state.seekSource = seek.source;
    m_state.selectedRow = rowIndex;
    ClearFocusedSurface();
    m_state.activeTab = ReplayCauseInspectorTab::Summary;
    m_state.transportPending = false;
    m_state.easedProgress = 0.0f;
    m_startedAtSeconds = nowSeconds;
    m_lastAdvanceSeconds = nowSeconds;
    SetDrawerTarget( true, nowSeconds );
    m_pendingFrame = presentedFrame;
    return true;
}

void ReplayCauseInspection::Advance( double nowSeconds ) noexcept
{
    m_lastAdvanceSeconds = nowSeconds;
    AdvanceDrawer( nowSeconds );

    if ( m_state.mode != ReplayCauseInspectionMode::Transporting )
    {
        return;
    }

    // Invariant: total elapsed time, not prior progress, owns the curve. A
    // backwards host clock clamps to the source rather than reversing a turn.
    m_state.easedProgress = EvaluateReplayCauseTransitionProgress( nowSeconds - m_startedAtSeconds );
    const ReplayFrameIndex requested = EvaluateReplayCauseTransitionFrame( m_state.sourceFrame, m_state.targetFrame,
                                                                           m_state.easedProgress );

    if ( requested != m_state.presentedFrame && ( !m_state.transportInFlight || requested != m_inFlightFrame ) )
    {
        // Coalescing replaces an obsolete not-yet-issued intermediate frame.
        m_pendingFrame = requested;
        m_state.transportPending = true;
    }

    if ( m_state.easedProgress >= 1.0f && m_state.presentedFrame == m_state.targetFrame && !m_state.transportInFlight &&
         !m_state.transportPending )
    {
        m_state.mode = ReplayCauseInspectionMode::DetailPaused;
        m_state.detailVisible = true;
    }
}

bool ReplayCauseInspection::TakeTransportRequest( ReplayCauseTransportRequest& outRequest ) noexcept
{
    if ( !m_state.transportPending || m_state.transportInFlight || m_state.mode != ReplayCauseInspectionMode::Transporting )
    {
        return false;
    }

    outRequest.generation = m_state.generation;
    outRequest.sourceFrame = m_state.sourceFrame;
    outRequest.targetFrame = m_pendingFrame;
    outRequest.source = m_state.seekSource;
    m_state.transportPending = false;
    m_state.transportInFlight = true;
    m_inFlightGeneration = outRequest.generation;
    m_inFlightFrame = outRequest.targetFrame;
    m_state.transportFrame = outRequest.targetFrame;
    return true;
}

void ReplayCauseInspection::PublishSolverDetail( uint64_t generation, const ReplayCauseSolverDetailResult& detail,
                                                 const Rendering::ContactManifoldPresentation& contactPresentation ) noexcept
{
    if ( generation == 0u || generation != m_state.generation || detail.frame != m_state.targetFrame )
    {
        return;
    }

    // Lifetime: copy every bounded row before the restore retires its source
    // sample. The published spans point only into this Planning owner.
    m_state.solverDetailAvailability = detail.availability;
    m_state.solverDetailFeedback = detail.Feedback();
    m_state.solverDetailContactRowCount = 0u;
    m_state.solverDetailPipelineRecordCount = 0u;
    m_state.selectedDetailContactRow = -1;
    m_state.solverDetailContacts = {};
    m_state.solverDetailPipelineRecords = {};
    m_state.solverDetailFirstRow = 0;
    m_state.contactPresentation = {};

    if ( !detail.HasDetail() || detail.contactRowCount > m_solverDetailContacts.size() ||
         detail.pipelineRecordCount > m_solverDetailPipelineRecords.size() )
    {
        return;
    }

    for ( std::size_t row = 0; row < detail.contactRowCount; ++row )
    {
        const Physics::PhysicsSolverPersistentContactSample* contact = detail.ContactRowAt( row );

        if ( !contact )
        {
            m_state.solverDetailAvailability = ReplayCauseSolverDetailAvailability::SolverDetailNotAvailable;
            m_state.solverDetailFeedback = SOLVER_DETAIL_UNAVAILABLE_FEEDBACK;
            return;
        }

        m_solverDetailContacts[row] = *contact;
    }

    for ( std::size_t recordIndex = 0; recordIndex < detail.pipelineRecordCount; ++recordIndex )
    {
        const Physics::PhysicsPipelineRecord* record = detail.PipelineRecordAt( recordIndex );

        if ( !record )
        {
            m_state.solverDetailAvailability = ReplayCauseSolverDetailAvailability::SolverDetailNotAvailable;
            m_state.solverDetailFeedback = SOLVER_DETAIL_UNAVAILABLE_FEEDBACK;
            return;
        }

        m_solverDetailPipelineRecords[recordIndex] = *record;
    }

    m_state.solverDetailContactRowCount = detail.contactRowCount;
    m_state.solverDetailPipelineRecordCount = detail.pipelineRecordCount;
    m_state.selectedDetailContactRow = detail.selectedDetailContactRow;
    m_state.solverDetailContacts = std::span<const Physics::PhysicsSolverPersistentContactSample>( m_solverDetailContacts
                                                                                                       .data(),
                                                                                                   detail.contactRowCount );
    m_state
        .solverDetailPipelineRecords = std::span<const Physics::PhysicsPipelineRecord>( m_solverDetailPipelineRecords.data(),
                                                                                        detail.pipelineRecordCount );
    m_state.contactPresentation = contactPresentation;
}

void ReplayCauseInspection::CompleteTransport( uint64_t generation, bool succeeded ) noexcept
{
    if ( generation == 0 || generation != m_inFlightGeneration )
    {
        return;
    }

    m_state.transportInFlight = false;
    m_inFlightGeneration = 0;

    // Hazard: a newer row may have been selected while this restore was in
    // flight. Its pending generation owns the next mutation; the old completion
    // must neither expose detail nor unwind the inspection pause.
    if ( generation != m_state.generation )
    {
        return;
    }

    if ( !succeeded )
    {
        m_state.mode = ReplayCauseInspectionMode::Returning;
        ClearFocusedSurface();
        SetDrawerTarget( false, m_lastAdvanceSeconds );
        return;
    }

    m_state.presentedFrame = m_inFlightFrame;

    if ( m_state.easedProgress >= 1.0f && m_state.presentedFrame == m_state.targetFrame && !m_state.transportPending )
    {
        m_state.mode = ReplayCauseInspectionMode::DetailPaused;
        m_state.detailVisible = true;
    }
}

bool ReplayCauseInspection::BeginAftermath( bool& outReleasePause ) noexcept
{
    outReleasePause = false;

    if ( m_state.mode != ReplayCauseInspectionMode::DetailPaused )
    {
        return false;
    }

    m_state.mode = ReplayCauseInspectionMode::AftermathFollow;
    ClearFocusedSurface();
    SetDrawerTarget( false, m_lastAdvanceSeconds );
    outReleasePause = m_state.ownsPause;
    m_state.ownsPause = false;
    return true;
}

ReplayCauseExitAction ReplayCauseInspection::BeginReturn() noexcept
{
    ReplayCauseExitAction action;

    if ( m_state.mode == ReplayCauseInspectionMode::Inactive || m_state.returnIssued )
    {
        return action;
    }

    action.apply = true;
    action.releasePause = m_state.ownsPause;
    m_state.ownsPause = false;
    ClearFocusedSurface();
    SetDrawerTarget( false, m_lastAdvanceSeconds );
    m_state.transportPending = false;
    m_state.mode = ReplayCauseInspectionMode::Returning;
    m_state.returnIssued = true;

    // Invalidate a synchronous completion that arrives after cancellation.
    ++m_state.generation;

    if ( m_state.generation == 0 )
    {
        m_state.generation = 1;
    }

    return action;
}

void ReplayCauseInspection::CompleteReturn() noexcept
{
    if ( m_state.mode == ReplayCauseInspectionMode::Returning && !m_state.transportInFlight )
    {
        Reset();
    }
}

void ReplayCauseInspection::RestoreInteractionRecordingBaseline( const ReplayCauseInspectionRecordingState& baseline,
                                                                 double nowSeconds ) noexcept
{
    // Why: Select regenerated the selected row and detached detail evidence
    // from the loaded artifact. Restore only scalar transition/presentation
    // state here so no serialized span or stale owner address can cross runs.
    m_state.mode = baseline.mode;
    m_state.activeTab = baseline.activeTab;
    m_state.selectedRow = baseline.selectedRow;
    m_state.selectedDetailContactRow = baseline.selectedDetailContactRow;
    m_state.solverDetailFirstRow = (std::max)( 0, baseline.solverDetailFirstRow );
    m_state.rawRecordFirstRow = (std::max)( 0, baseline.rawRecordFirstRow );
    m_state.iterationsFirstRow = (std::max)( 0, baseline.iterationsFirstRow );
    m_state.sourceFrame = baseline.sourceFrame;
    m_state.targetFrame = baseline.targetFrame;
    m_state.presentedFrame = baseline.presentedFrame;
    m_state.detailVisible = baseline.detailVisible;
    m_state.ownsPause = baseline.ownsPause;
    m_state.returnIssued = baseline.returnIssued;
    m_state.easedProgress = std::clamp( baseline.easedProgress, 0.0f, 1.0f );
    m_state.drawerProgress = std::clamp( baseline.drawerProgress, 0.0f, 1.0f );

    const double transitionUnit = 1.0 - std::cbrt( 1.0 - static_cast<double>( m_state.easedProgress ) );
    m_startedAtSeconds = nowSeconds - transitionUnit * REPLAY_CAUSE_TRANSITION_SECONDS;
    m_lastAdvanceSeconds = nowSeconds;

    m_drawerTargetOpen = baseline.mode != ReplayCauseInspectionMode::Returning && baseline.detailVisible;
    m_drawerStartProgress = m_drawerTargetOpen ? 0.0f : 1.0f;
    const float drawerCurveProgress = m_drawerTargetOpen ? m_state.drawerProgress : 1.0f - m_state.drawerProgress;
    const double drawerUnit = 1.0 - std::cbrt( 1.0 - static_cast<double>( drawerCurveProgress ) );
    m_drawerStartedAtSeconds = nowSeconds - drawerUnit * REPLAY_CAUSE_INSPECTOR_DRAWER_SECONDS;

    // A captured in-flight restore cannot survive process restart. Reissue its
    // deterministic current curve position through the normal transport owner.
    m_state.transportInFlight = false;
    m_state.transportPending = baseline.mode == ReplayCauseInspectionMode::Transporting &&
                               ( baseline.transportPending || baseline.transportInFlight );
    m_pendingFrame = EvaluateReplayCauseTransitionFrame( m_state.sourceFrame, m_state.targetFrame, m_state.easedProgress );
    m_inFlightFrame = 0;
    m_inFlightGeneration = 0;
}

bool ReplayCauseInspection::TickSolverDetailPanelInput( const RunReplayCauseTreeState& causeTree, int mouseX, int mouseY,
                                                        bool hasClientPosition, bool pointerBlocked, bool leftPressed,
                                                        int wheelDelta, int screenWidth, int screenHeight,
                                                        ReplayCauseInspectorCommand* outCommand ) noexcept
{
    if ( !m_state.detailVisible || !hasClientPosition || pointerBlocked || screenWidth <= 0 || screenHeight <= 0 )
    {
        return false;
    }

    PROFILE_SCOPED( "Frame/Replay/CauseInspection/PanelInput" );
    const ReplayCauseInspectorLayout layout = BuildReplayCauseInspectorLayout( m_state, causeTree, screenWidth, screenHeight,
                                                                               m_state.drawerProgress );

    if ( !ReplayCauseInspectorContainsPoint( layout, mouseX, mouseY ) ||
         !PointInside( layout.visibleDrawer, mouseX, mouseY ) )
    {
        return false;
    }

    const int contactRow = ( m_state.selectedDetailContactRow >= 0 &&
                             static_cast<std::size_t>( m_state.selectedDetailContactRow ) <
                                 m_state.solverDetailContacts.size() )
                               ? m_state.selectedDetailContactRow
                               : 0;

    if ( leftPressed )
    {
        if ( PointInside( layout.drawerClose, mouseX, mouseY ) )
        {
            (void)BeginReturn();
            return true;
        }

        for ( std::size_t tab = 0; tab < layout.tabs.size(); ++tab )
        {
            if ( PointInside( layout.tabs[tab], mouseX, mouseY ) )
            {
                m_state.activeTab = static_cast<ReplayCauseInspectorTab>( tab );
                m_state.solverDetailFirstRow = 0;
                m_state.rawRecordFirstRow = 0;
                m_state.iterationsFirstRow = 0;
                return true;
            }
        }

        if ( m_state.activeTab == ReplayCauseInspectorTab::RawRecord && PointInside( layout.rawCopy, mouseX, mouseY ) )
        {
            if ( outCommand )
            {
                outCommand->kind = ReplayCauseInspectorCommandKind::CopyRecord;
                const ReplayCauseRawRecordProjection projection = BuildReplayCauseRawRecordProjection( m_state, contactRow );
                SerializeReplayCauseRawRecord( projection, outCommand->text, sizeof( outCommand->text ) );
            }

            return true;
        }
    }

    if ( wheelDelta != 0 )
    {
        if ( m_state.activeTab == ReplayCauseInspectorTab::Iterations && layout.iterationsVisibleRows > 0 )
        {
            const ReplayCauseIterationsProjection projection = BuildReplayCauseIterationsProjection( m_state, contactRow );
            const int direction = wheelDelta > 0 ? -1 : 1;
            const int wheelSteps = (std::max)( 1, std::abs( wheelDelta ) / 120 ) * 3;
            const int maximumFirstRow = (std::max)( 0,
                                                    static_cast<int>( projection.rowCount ) - layout.iterationsVisibleRows );
            m_state.iterationsFirstRow = std::clamp( m_state.iterationsFirstRow + direction * wheelSteps, 0,
                                                     maximumFirstRow );
        }
        else if ( m_state.activeTab == ReplayCauseInspectorTab::RawRecord && layout.rawVisibleRows > 0 )
        {
            const ReplayCauseRawRecordProjection projection = BuildReplayCauseRawRecordProjection( m_state, contactRow );
            const int direction = wheelDelta > 0 ? -1 : 1;
            const int wheelSteps = (std::max)( 1, std::abs( wheelDelta ) / 120 ) * 3;
            const int maximumFirstRow = (std::max)( 0, static_cast<int>( projection.rowCount ) - layout.rawVisibleRows );
            m_state.rawRecordFirstRow = std::clamp( m_state.rawRecordFirstRow + direction * wheelSteps, 0, maximumFirstRow );
        }
        else if ( layout.visibleRows > 0 )
        {
            const int direction = wheelDelta > 0 ? -1 : 1;
            const int wheelSteps = (std::max)( 1, std::abs( wheelDelta ) / 120 );
            const int maximumFirstRow = (std::max)( 0, static_cast<int>( m_state.solverDetailContacts.size() ) -
                                                           layout.visibleRows );
            m_state.solverDetailFirstRow = std::clamp( m_state.solverDetailFirstRow + direction * wheelSteps, 0,
                                                       maximumFirstRow );
        }
    }

    return true;
}

void ReplayCauseInspection::Reset() noexcept
{
    m_state = ReplayCauseInspectionView {};
    m_startedAtSeconds = 0.0;
    m_lastAdvanceSeconds = 0.0;
    m_drawerStartedAtSeconds = 0.0;
    m_drawerStartProgress = 0.0f;
    m_drawerTargetOpen = false;
    m_pendingFrame = 0;
    m_inFlightFrame = 0;
    m_inFlightGeneration = 0;
}

void ReplayCauseInspection::ClearFocusedSurface() noexcept
{
    // Lifetime: fixed backing arrays remain allocated, but no stale row is
    // reachable once its synchronous spans and paired Rendering packet clear.
    m_state.solverDetailAvailability = ReplayCauseSolverDetailAvailability::SolverDetailNotAvailable;
    m_state.solverDetailContactRowCount = 0u;
    m_state.solverDetailPipelineRecordCount = 0u;
    m_state.selectedDetailContactRow = -1;
    m_state.solverDetailContacts = {};
    m_state.solverDetailPipelineRecords = {};
    m_state.solverDetailFeedback = SOLVER_DETAIL_UNAVAILABLE_FEEDBACK;
    m_state.solverDetailFirstRow = 0;
    m_state.rawRecordFirstRow = 0;
    m_state.iterationsFirstRow = 0;
    m_state.contactPresentation = {};
}

void ReplayCauseInspection::SetDrawerTarget( bool open, double nowSeconds ) noexcept
{
    if ( m_drawerTargetOpen == open && ( open || m_state.drawerProgress <= 0.0f ) )
    {
        return;
    }

    m_drawerTargetOpen = open;
    m_drawerStartProgress = m_state.drawerProgress;
    m_drawerStartedAtSeconds = nowSeconds;
    m_state.detailVisible = true;
}

void ReplayCauseInspection::AdvanceDrawer( double nowSeconds ) noexcept
{
    if ( !m_state.detailVisible )
    {
        return;
    }

    const double elapsed = (std::max)( 0.0, nowSeconds - m_drawerStartedAtSeconds );
    const float eased = EvaluateReplayCauseTransitionProgress( elapsed * REPLAY_CAUSE_TRANSITION_SECONDS /
                                                               REPLAY_CAUSE_INSPECTOR_DRAWER_SECONDS );
    const float target = m_drawerTargetOpen ? 1.0f : 0.0f;
    m_state.drawerProgress = m_drawerStartProgress + ( target - m_drawerStartProgress ) * eased;

    if ( eased >= 1.0f )
    {
        m_state.drawerProgress = target;

        if ( !m_drawerTargetOpen )
        {
            m_state.detailVisible = false;
        }
    }
}

ReplayCauseInspectionView ReplayCauseInspection::View() const noexcept
{
    return m_state;
}
} // namespace SkullbonezCore::Runtime
