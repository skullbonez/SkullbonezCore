#include "PhysicsComparison.h"
#include "../../../ThirdPtySource/nlohmann/json.hpp"
#include <fstream>
#include <algorithm>
#include <tuple>
#include <cmath>

using namespace SkullbonezCore::Runtime;
namespace
{
using Json = nlohmann::ordered_json;
uint64_t StableId( const ComparisonRecording& recording, int tick, int row )
{
    if ( const auto* frame = recording.Frame( tick ) )
    {
        for ( const auto& body : frame->bodies )
        {
            if ( body.modelRow.value == row )
            {
                return body.id.value;
            }
        }
    }
    return 0;
}
bool Number( const Json& json, const char* key, float& value )
{
    const auto found = json.find( key );
    if ( found == json.end() || !found->is_number() )
    {
        return false;
    }
    const double number = found->get<double>();
    if ( !std::isfinite( number ) || std::abs( number ) > 1.0e30 )
    {
        return false;
    }
    value = static_cast<float>( number );
    return true;
}
bool Integer( const Json& json, const char* key, int& value )
{
    float number = 0;
    if ( !Number( json, key, number ) || number < -1 || number > 10000000 || std::floor( number ) != number )
    {
        return false;
    }
    value = static_cast<int>( number );
    return true;
}
bool ReadContact( const Json& row, const ComparisonRecording& recording, int tick, ComparisonRecording::ContactSummary& out )
{
    int bodyA = -1, bodyB = -1, warm = 0;
    const auto feature = row.find( "feature_id" );
    if ( feature == row.end() || !feature->is_number_unsigned() || feature->get<uint64_t>() > UINT32_MAX )
    {
        return false;
    }
    if ( !Integer( row, "body_a", bodyA ) || !Integer( row, "body_b", bodyB ) || !Integer( row, "warm_started", warm ) )
    {
        return false;
    }
    out.bodyA = StableId( recording, tick, bodyA );
    out.bodyB = StableId( recording, tick, bodyB );
    out.terrain = bodyB < 0;
    out.feature = feature->get<uint32_t>();
    out.warmStarted = warm != 0;
    const auto normal = row.find( "normal" );
    if ( normal == row.end() || !normal->is_array() || normal->size() != 3 )
    {
        return false;
    }
    for ( const auto& v : *normal )
    {
        if ( !v.is_number() || !std::isfinite( v.get<double>() ) )
        {
            return false;
        }
    }
    out.normal = { ( *normal )[0].get<float>(), ( *normal )[1].get<float>(), ( *normal )[2].get<float>() };
    return Number( row, "penetration", out.penetration ) && Number( row, "normal_impulse", out.normalImpulse ) &&
           Number( row, "tangent_impulse", out.tangentImpulse ) &&
           Number( row, "pre_solve_normal_speed", out.preNormalSpeed ) &&
           Number( row, "pre_solve_slip_speed", out.preSlipSpeed ) && Number( row, "slip_speed", out.postSlipSpeed );
}
bool ReserveObservationRows( std::ifstream& input, std::vector<ComparisonRecording::Observations>& observations,
                             int tickOffset, ComparisonLoadProgress* progress )
{
    std::vector<std::array<std::size_t, 2>> counts( observations.size() );
    std::string line;
    while ( std::getline( input, line ) )
    {
        if ( line.size() > 65536 || ( progress && progress->cancelled.load( std::memory_order_relaxed ) ) )
        {
            return false;
        }
        const bool contact = line.starts_with( "{\"kind\":\"contact\"" );
        const bool iteration = line.starts_with( "{\"kind\":\"solver_iteration_summary\"" );
        if ( !contact && !iteration )
        {
            continue;
        }
        const auto row = Json::parse( line, nullptr, false );
        int frame = -1;
        if ( !row.is_object() || !Integer( row, "frame", frame ) || frame < -tickOffset ||
             static_cast<std::size_t>( frame + tickOffset ) >= counts.size() )
        {
            return false;
        }
        ++counts[static_cast<std::size_t>( frame + tickOffset )][contact ? 0 : 1];
    }
    for ( std::size_t tick = 0; tick < counts.size(); ++tick )
    {
        observations[tick].contacts.reserve( counts[tick][0] );
        observations[tick].iterations.reserve( counts[tick][1] );
    }
    input.clear();
    input.seekg( 0 );
    return static_cast<bool>( input );
}
auto Key( const ComparisonRecording::ContactSummary& c )
{
    return std::tuple( c.bodyA, c.bodyB, c.terrain, c.feature );
}
bool Same( const ComparisonRecording::ContactSummary& a, const ComparisonRecording::ContactSummary& b )
{
    return a.normal.x == b.normal.x && a.normal.y == b.normal.y && a.normal.z == b.normal.z &&
           a.penetration == b.penetration && a.normalImpulse == b.normalImpulse && a.tangentImpulse == b.tangentImpulse &&
           a.preNormalSpeed == b.preNormalSpeed && a.preSlipSpeed == b.preSlipSpeed && a.postSlipSpeed == b.postSlipSpeed &&
           a.warmStarted == b.warmStarted;
}
} // namespace
const ComparisonRecording::Observations* ComparisonRecording::Observation( int tick ) const noexcept
{
    return tick >= 0 && static_cast<std::size_t>( tick ) < observations.size() &&
                   observations[static_cast<std::size_t>( tick )].recorded
               ? &observations[static_cast<std::size_t>( tick )]
               : nullptr;
}
bool ComparisonRecording::LoadObservations( const char* path, int ticks, uint64_t& residentBytes,
                                            ComparisonLoadProgress* progress, int side )
{
    std::ifstream input( path );
    if ( !input )
    {
        return true; // Older producers may not publish this optional stream.
    }
    observations.resize( static_cast<std::size_t>( ticks ) + 1 );
    residentBytes += observations.capacity() * sizeof( Observations );
    // Exact reservations avoid geometric growth for the million-row wall trial.
    // Preflight charged every row; a changed stream cannot grow beyond this pass.
    if ( !ReserveObservationRows( input, observations, tickOffset, progress ) )
    {
        return false;
    }
    std::string line;
    while ( std::getline( input, line ) )
    {
        if ( progress && progress->cancelled.load( std::memory_order_relaxed ) )
        {
            return false;
        }
        if ( line.size() > 65536 )
        {
            return false;
        }
        const bool contact = line.starts_with( "{\"kind\":\"contact\"" );
        const bool iteration = line.starts_with( "{\"kind\":\"solver_iteration_summary\"" );
        const bool frame = line.starts_with( "{\"kind\":\"frame\"" );
        const bool stats = line.starts_with( "{\"kind\":\"solver_stats\"" );
        if ( !contact && !iteration && !frame && !stats )
        {
            continue;
        }
        const auto row = Json::parse( line, nullptr, false );
        int sceneFrame = -1;
        if ( !row.is_object() || !Integer( row, "frame", sceneFrame ) )
        {
            return false;
        }
        const int tick = sceneFrame + tickOffset;
        if ( progress && frame )
        {
            progress->percent.store( side * 40 + tick * 40 / (std::max)( 1, ticks ), std::memory_order_relaxed );
        }
        if ( tick < 0 || tick > ticks )
        {
            return false;
        }
        auto& observed = observations[static_cast<std::size_t>( tick )];
        if ( frame )
        {
            if ( observed.recorded )
            {
                return false;
            }
            observed.recorded = true;
        }
        if ( stats )
        {
            observed.iterationsRecorded = true;
        }
        if ( contact )
        {
            ContactSummary summary;
            if ( !ReadContact( row, *this, tick, summary ) )
            {
                return false;
            }
            if ( observed.contacts.size() == observed.contacts.capacity() )
            {
                return false;
            }
            observed.contacts.push_back( summary );
            residentBytes += sizeof( ContactSummary );
        }
        if ( iteration )
        {
            IterationSummary summary;
            if ( !Integer( row, "iteration", summary.iteration ) || !Integer( row, "dropped_iterations", summary.dropped ) ||
                 !Number( row, "stopping_impulse_delta_sq", summary.stoppingDeltaSquared ) ||
                 !Number( row, "normal_impulse_delta_sq", summary.normalDeltaSquared ) ||
                 !Number( row, "tangent_impulse_delta_sq", summary.tangentDeltaSquared ) )
            {
                return false;
            }
            if ( observed.iterations.size() == observed.iterations.capacity() )
            {
                return false;
            }
            observed.iterations.push_back( summary );
            residentBytes += sizeof( IterationSummary );
        }
        if ( residentBytes > PhysicsComparison::MEMORY_BUDGET )
        {
            return false;
        }
    }
    return input.eof();
}
bool PhysicsComparison::BuildObservedContactEvents( int tick )
{
    const auto* a = m_recordings[0].Observation( tick );
    const auto* b = m_recordings[1].Observation( tick );
    if ( !a && !b )
    {
        return false;
    }
    if ( !a || !b )
    {
        m_events.push_back( { tick, 0, 0, 0, ComparisonFamily::Contact, ComparisonChange::NotRecorded, -1, -1, true } );
        return true;
    }
    std::vector<uint8_t> matched( b->contacts.size(), 0 );
    for ( std::size_t i = 0; i < a->contacts.size(); ++i )
    {
        const auto& contact = a->contacts[i];
        const auto key = Key( contact );
        const auto duplicates = std::count_if( a->contacts.begin(), a->contacts.end(),
                                               [&]( const auto& other ) { return Key( other ) == key; } );
        int count = 0, found = -1;
        for ( std::size_t j = 0; j < b->contacts.size(); ++j )
        {
            if ( Key( b->contacts[j] ) == key )
            {
                ++count;
                found = static_cast<int>( j );
            }
        }
        ComparisonEvent event { tick,
                                contact.bodyA,
                                contact.bodyB,
                                contact.feature,
                                contact.terrain ? ComparisonFamily::Terrain : ComparisonFamily::Contact,
                                ComparisonChange::OnlyA,
                                static_cast<int>( i ),
                                -1,
                                true };
        // Repeated features do not establish correspondence by row order. An
        // equal multiset of recorded values can still prove A/A equality.
        const auto exactA = std::count_if( a->contacts.begin(), a->contacts.end(), [&]( const auto& other )
                                           { return Key( other ) == key && Same( contact, other ); } );
        const auto exactB = std::count_if( b->contacts.begin(), b->contacts.end(), [&]( const auto& other )
                                           { return Key( other ) == key && Same( contact, other ); } );
        int exact = -1;
        if ( duplicates == count && exactA == exactB && contact.bodyA && ( contact.terrain || contact.bodyB ) )
        {
            for ( std::size_t j = 0; j < b->contacts.size(); ++j )
            {
                if ( matched[j] != 1 && Key( b->contacts[j] ) == key && Same( contact, b->contacts[j] ) )
                {
                    exact = static_cast<int>( j );
                    break;
                }
            }
        }
        if ( exact >= 0 )
        {
            event.contactB = exact;
            matched[static_cast<std::size_t>( exact )] = 1;
            event.change = ComparisonChange::Equal;
        }
        else if ( duplicates > 1 || count > 1 || !contact.bodyA || ( !contact.terrain && !contact.bodyB ) )
        {
            event.change = ComparisonChange::Ambiguous;
            for ( std::size_t j = 0; j < b->contacts.size(); ++j )
            {
                if ( matched[j] != 1 && Key( b->contacts[j] ) == key )
                {
                    matched[j] = 2;
                }
            }
        }
        else if ( found >= 0 )
        {
            event.contactB = found;
            matched[static_cast<std::size_t>( found )] = 1;
            event.change = Same( contact, b->contacts[static_cast<std::size_t>( found )] ) ? ComparisonChange::Equal
                                                                                           : ComparisonChange::Changed;
        }
        m_events.push_back( event );
    }
    for ( std::size_t j = 0; j < b->contacts.size(); ++j )
    {
        if ( matched[j] != 1 )
        {
            const auto& contact = b->contacts[j];
            m_events.push_back( { tick, contact.bodyA, contact.bodyB, contact.feature,
                                  contact.terrain ? ComparisonFamily::Terrain : ComparisonFamily::Contact,
                                  matched[j] == 2 ? ComparisonChange::Ambiguous : ComparisonChange::OnlyB, -1,
                                  static_cast<int>( j ), true } );
        }
    }
    const auto count = (std::max)( a->iterations.size(), b->iterations.size() );
    for ( std::size_t i = 0; i < count; ++i )
    {
        ComparisonChange change = ComparisonChange::Equal;
        if ( !a->iterationsRecorded || !b->iterationsRecorded )
        {
            change = ComparisonChange::NotRecorded;
        }
        else if ( i >= a->iterations.size() )
        {
            change = ComparisonChange::OnlyB;
        }
        else if ( i >= b->iterations.size() )
        {
            change = ComparisonChange::OnlyA;
        }
        else
        {
            const auto& x = a->iterations[i];
            const auto& y = b->iterations[i];
            if ( x.iteration != y.iteration || x.dropped || y.dropped )
            {
                change = ComparisonChange::Ambiguous;
            }
            else if ( x.stoppingDeltaSquared != y.stoppingDeltaSquared || x.normalDeltaSquared != y.normalDeltaSquared ||
                      x.tangentDeltaSquared != y.tangentDeltaSquared )
            {
                change = ComparisonChange::Changed;
            }
        }
        m_events.push_back( { tick, 0, 0, static_cast<uint32_t>( i ), ComparisonFamily::SolverIteration, change,
                              i < a->iterations.size() ? static_cast<int>( i ) : -1,
                              i < b->iterations.size() ? static_cast<int>( i ) : -1, true } );
    }
    return true;
}
