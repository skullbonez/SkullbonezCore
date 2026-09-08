#include "PhysicsComparison.h"
#include "PhysicsComparison.DiagnosticLines.h"
#include "../../../ThirdPtySource/nlohmann/json.hpp"
#include <fstream>
#include <algorithm>
#include <tuple>
#include <cmath>
#include <filesystem>
#include <string_view>

using namespace SkullbonezCore::Runtime;
namespace
{
using Json = nlohmann::ordered_json;

class DiagnosticRowReader final : public nlohmann::json_sax<Json>
{
    // Invariant: only top-level numeric fields and a three-number normal are
    // retained. SAX validates the entire JSON row without building millions of
    // transient maps, strings and value nodes in the process allocation hook.
    static constexpr const char* FIELDS[] = { "frame",
                                              "body_a",
                                              "body_b",
                                              "warm_started",
                                              "feature_id",
                                              "penetration",
                                              "normal_impulse",
                                              "tangent_impulse",
                                              "pre_solve_normal_speed",
                                              "pre_solve_slip_speed",
                                              "slip_speed",
                                              "iteration",
                                              "dropped_iterations",
                                              "stopping_impulse_delta_sq",
                                              "normal_impulse_delta_sq",
                                              "tangent_impulse_delta_sq" };
    std::array<double, 19> m_values {};
    uint32_t m_present = 0, m_unsigned = 0;
    int m_depth = 0, m_field = -1, m_normalCount = 0;
    bool m_rootObject = false, m_normalArray = false;

    static int Field( const std::string_view name )
    {
        for ( int i = 0; i < 16; ++i )
        {
            if ( name == FIELDS[i] )
            {
                return i;
            }
        }
        return name == "normal" ? 16 : -1;
    }

    bool Numeric( double value, bool isUnsigned )
    {
        int field = m_depth == 1 ? m_field : -1;
        if ( m_normalArray && m_depth == 2 )
        {
            field = m_normalCount < 3 ? 16 + m_normalCount : -1;
            ++m_normalCount;
        }
        if ( field >= 0 && field < 19 && std::isfinite( value ) && std::abs( value ) <= 1.0e30 )
        {
            m_values[field] = value;
            m_present |= 1u << field;
            if ( isUnsigned )
            {
                m_unsigned |= 1u << field;
            }
        }
        return true;
    }

  public:
    bool Parse( const std::string& line )
    {
        return Json::sax_parse( line, this ) && m_rootObject;
    }
    bool Number( const char* key, float& value ) const
    {
        const int field = Field( key );
        if ( field < 0 || !( m_present & ( 1u << field ) ) )
        {
            return false;
        }
        value = static_cast<float>( m_values[field] );
        return true;
    }
    bool Feature( uint32_t& value ) const
    {
        if ( !( m_unsigned & ( 1u << 4 ) ) || m_values[4] > UINT32_MAX )
        {
            return false;
        }
        value = static_cast<uint32_t>( m_values[4] );
        return true;
    }
    bool Normal( SkullbonezCore::Math::Vector::Vector3& value ) const
    {
        if ( ( m_present & ( 7u << 16 ) ) != ( 7u << 16 ) || m_normalCount != 3 )
        {
            return false;
        }
        value = { static_cast<float>( m_values[16] ), static_cast<float>( m_values[17] ),
                  static_cast<float>( m_values[18] ) };
        return true;
    }
    bool SkipScalar()
    {
        if ( m_normalArray && m_depth == 2 )
        {
            m_normalCount = 4;
        }
        return true;
    }
    bool null() override
    {
        return SkipScalar();
    }
    bool boolean( bool ) override
    {
        return SkipScalar();
    }
    bool string( string_t& ) override
    {
        return SkipScalar();
    }
    bool binary( binary_t& ) override
    {
        return true;
    }
    bool number_integer( number_integer_t value ) override
    {
        return Numeric( static_cast<double>( value ), false );
    }
    bool number_unsigned( number_unsigned_t value ) override
    {
        return Numeric( static_cast<double>( value ), true );
    }
    bool number_float( number_float_t value, const string_t& ) override
    {
        return Numeric( value, false );
    }
    bool start_object( std::size_t ) override
    {
        SkipScalar();
        m_rootObject |= m_depth == 0;
        ++m_depth;
        return true;
    }
    bool end_object() override
    {
        --m_depth;
        return true;
    }
    bool key( string_t& key ) override
    {
        if ( m_depth == 1 )
        {
            m_field = Field( key );
            if ( m_field >= 0 )
            {
                const uint32_t mask = m_field == 16 ? 7u << 16 : 1u << m_field;
                m_present &= ~mask;
                m_unsigned &= ~mask;
                if ( m_field == 16 )
                {
                    m_normalCount = 0;
                }
            }
        }
        return true;
    }
    bool start_array( std::size_t ) override
    {
        SkipScalar();
        if ( m_depth == 1 )
        {
            m_normalArray = m_field == 16;
        }
        ++m_depth;
        return true;
    }
    bool end_array() override
    {
        if ( m_depth == 2 )
        {
            m_normalArray = false;
        }
        --m_depth;
        return true;
    }
    bool parse_error( std::size_t, const std::string&, const nlohmann::detail::exception& ) override
    {
        return false;
    }
};

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
bool Integer( const DiagnosticRowReader& json, const char* key, int& value )
{
    float number = 0;
    if ( !json.Number( key, number ) || number < -1 || number > 10000000 || std::floor( number ) != number )
    {
        return false;
    }
    value = static_cast<int>( number );
    return true;
}
bool ReadContact( const DiagnosticRowReader& row, const ComparisonRecording& recording, int tick,
                  ComparisonRecording::ContactSummary& out )
{
    int bodyA = -1, bodyB = -1, warm = 0;
    if ( !row.Feature( out.feature ) || !row.Normal( out.normal ) )
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
    out.warmStarted = warm != 0;
    return row.Number( "penetration", out.penetration ) && row.Number( "normal_impulse", out.normalImpulse ) &&
           row.Number( "tangent_impulse", out.tangentImpulse ) &&
           row.Number( "pre_solve_normal_speed", out.preNormalSpeed ) &&
           row.Number( "pre_solve_slip_speed", out.preSlipSpeed ) && row.Number( "slip_speed", out.postSlipSpeed );
}
bool ReserveObservationRows( ComparisonDiagnosticLines& input, std::vector<ComparisonRecording::Observations>& observations,
                             int tickOffset, ComparisonLoadProgress* progress )
{
    std::vector<std::array<std::size_t, 2>> counts( observations.size() );
    std::string line;
    while ( input.Read( line ) )
    {
        if ( progress )
        {
            progress->Update( input.Position(), input.Size() );
        }
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
        DiagnosticRowReader row;
        int frame = -1;
        if ( !row.Parse( line ) || !Integer( row, "frame", frame ) || frame < -tickOffset ||
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
    return input.Good() && input.Rewind();
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
    if ( std::filesystem::path( path ).extension() == ".skobs" )
    {
        return LoadBinaryObservations( path, ticks, residentBytes, progress, side );
    }
    ComparisonDiagnosticLines input( path );
    if ( !input.Open() )
    {
        return input.Good(); // Older producers may not publish this optional stream.
    }
    observations.resize( static_cast<std::size_t>( ticks ) + 1 );
    residentBytes += observations.capacity() * sizeof( Observations );
    if ( progress )
    {
        progress->Begin( side ? "B: reserving contact rows" : "A: reserving contact rows", side ? 60 : 30, 10 );
    }
    // Exact reservations avoid geometric growth for the million-row wall trial.
    // Preflight charged every row; a changed stream cannot grow beyond this pass.
    if ( !ReserveObservationRows( input, observations, tickOffset, progress ) )
    {
        return false;
    }
    if ( progress )
    {
        progress->Begin( side ? "B: reading contact evidence" : "A: reading contact evidence", side ? 70 : 40, 15 );
    }
    std::string line;
    while ( input.Read( line ) )
    {
        if ( progress )
        {
            progress->Update( input.Position(), input.Size() );
        }
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
        DiagnosticRowReader row;
        int sceneFrame = -1;
        if ( !row.Parse( line ) || !Integer( row, "frame", sceneFrame ) )
        {
            return false;
        }
        const int tick = sceneFrame + tickOffset;

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
                 !row.Number( "stopping_impulse_delta_sq", summary.stoppingDeltaSquared ) ||
                 !row.Number( "normal_impulse_delta_sq", summary.normalDeltaSquared ) ||
                 !row.Number( "tangent_impulse_delta_sq", summary.tangentDeltaSquared ) )
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
    return input.Good();
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
