#include "PhysicsComparison.Binary.h"
#include "PhysicsComparison.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <utility>

using namespace SkullbonezCore::Runtime;

namespace
{
// Invariant: the disk format contains little-endian uint32/int32 and IEEE
// binary32 words, never compiler-padded C++ records. Both producers spell it out.
static_assert( std::endian::native == std::endian::little );
static_assert( sizeof( float ) == 4 && std::numeric_limits<float>::is_iec559 );

bool ValidFrameIndex( const ComparisonObservationIndex& entry, int ticks, int offset )
{
    const auto tick = int64_t { entry.frame } + offset;
    return tick >= 0 && tick <= ticks;
}

class ObservationBodyRows
{
    std::vector<std::pair<int, uint64_t>> m_rows;

  public:
    void Prepare( const ReplayPresentationSample* frame )
    {
        m_rows.clear();
        if ( frame )
        {
            m_rows.reserve( frame->bodies.size() );
            for ( const auto& body : frame->bodies )
            {
                m_rows.emplace_back( body.modelRow.value, body.id.value );
            }
            // The JSON path takes the first matching row in identity order.
            // Pair ordering preserves that rule even for ambiguous model rows.
            std::sort( m_rows.begin(), m_rows.end() );
        }
    }
    uint64_t Find( int row ) const
    {
        const auto found = std::lower_bound( m_rows.begin(), m_rows.end(), std::pair<int, uint64_t> { row, 0 } );
        return found != m_rows.end() && found->first == row ? found->second : 0;
    }
};

bool ReadContactWords( const uint32_t* words, ComparisonRecording::ContactSummary& contact, const ObservationBodyRows& rows )
{
    const int bodyA = std::bit_cast<int32_t>( words[0] ), bodyB = std::bit_cast<int32_t>( words[1] );
    if ( bodyA < -1 || bodyA > 10000000 || bodyB < -1 || bodyB > 10000000 || words[3] > 1 )
    {
        return false;
    }
    std::array<float, 9> values;
    for ( std::size_t i = 0; i < values.size(); ++i )
    {
        values[i] = std::bit_cast<float>( words[4 + i] );
        if ( !std::isfinite( values[i] ) || std::abs( values[i] ) > 1.000001e30f )
        {
            return false;
        }
    }
    contact.bodyA = rows.Find( bodyA );
    contact.bodyB = rows.Find( bodyB );
    contact.feature = words[2];
    contact.warmStarted = words[3] != 0;
    contact.terrain = bodyB < 0;
    contact.normal = { values[0], values[1], values[2] };
    contact.penetration = values[3];
    contact.normalImpulse = values[4];
    contact.tangentImpulse = values[5];
    contact.preNormalSpeed = values[6];
    contact.preSlipSpeed = values[7];
    contact.postSlipSpeed = values[8];
    return true;
}

bool ReadIterationWords( const uint32_t* words, ComparisonRecording::IterationSummary& iteration )
{
    iteration.iteration = std::bit_cast<int32_t>( words[0] );
    iteration.dropped = std::bit_cast<int32_t>( words[1] );
    iteration.stoppingDeltaSquared = std::bit_cast<float>( words[2] );
    iteration.normalDeltaSquared = std::bit_cast<float>( words[3] );
    iteration.tangentDeltaSquared = std::bit_cast<float>( words[4] );
    for ( int i = 2; i < 5; ++i )
    {
        const float value = std::bit_cast<float>( words[i] );
        if ( !std::isfinite( value ) || std::abs( value ) > 1.000001e30f )
        {
            return false;
        }
    }
    return iteration.iteration >= -1 && iteration.iteration <= 10000000 && iteration.dropped >= -1 &&
           iteration.dropped <= 10000000;
}
} // namespace

ComparisonObservationBinary::ComparisonObservationBinary( const std::filesystem::path& path )
    : m_input( path, std::ios::binary )
{
    if ( m_input )
    {
        m_input.seekg( 0, std::ios::end );
        const auto size = m_input.tellg();
        m_size = size >= 0 ? static_cast<uint64_t>( size ) : 0;
        m_input.seekg( 0 );
    }
}

bool ComparisonObservationBinary::ReadIndex()
{
    if ( m_size < 44 || m_size > PhysicsComparison::MEMORY_BUDGET )
    {
        return false;
    }
    std::array<char, 8> magic {};
    std::array<unsigned char, 32> hash {};
    uint32_t count = 0;
    m_input.read( magic.data(), magic.size() );
    m_input.read( reinterpret_cast<char*>( hash.data() ), hash.size() );
    m_input.read( reinterpret_cast<char*>( &count ), sizeof( count ) );
    if ( !m_input || magic != std::array<char, 8> { 'S', 'K', 'O', 'B', 'S', '1', '\r', '\n' } || count > 14402 )
    {
        return false;
    }
    constexpr char hex[] = "0123456789abcdef";
    for ( const auto byte : hash )
    {
        m_sourceHash += hex[byte >> 4];
        m_sourceHash += hex[byte & 15];
    }
    m_index.resize( count );
    uint64_t expectedSize = 44 + uint64_t { count } * 16;
    int previous = -2;
    for ( auto& entry : m_index )
    {
        std::array<uint32_t, 4> words {};
        m_input.read( reinterpret_cast<char*>( words.data() ), sizeof( words ) );
        entry = { std::bit_cast<int32_t>( words[0] ), words[1], words[2], words[3] };
        if ( !m_input || entry.frame <= previous || entry.frame > 10000000 || entry.flags > 3 )
        {
            return false;
        }
        previous = entry.frame;
        m_contacts += entry.contacts;
        m_iterations += entry.iterations;
        m_maxWords = (std::max)( m_maxWords, entry.Words() );
        expectedSize += entry.Words() * 4;
        if ( expectedSize > m_size )
        {
            return false;
        }
    }
    return expectedSize == m_size;
}

bool ComparisonObservationBinary::ReadFrame( const ComparisonObservationIndex& entry, std::vector<uint32_t>& words )
{
    if ( entry.Words() > m_maxWords )
    {
        return false;
    }
    words.resize( static_cast<std::size_t>( entry.Words() ) );
    if ( !words.empty() )
    {
        m_input.read( reinterpret_cast<char*>( words.data() ), static_cast<std::streamsize>( words.size() * 4 ) );
    }
    return static_cast<bool>( m_input );
}

bool ComparisonRecording::LoadBinaryObservations( const char* path, int ticks, uint64_t& residentBytes,
                                                  ComparisonLoadProgress* progress, int side )
{
    if ( ticks < 0 || ticks > 14400 || residentBytes > PhysicsComparison::MEMORY_BUDGET )
    {
        return false;
    }
    ComparisonObservationBinary input( path );
    if ( !input.ReadIndex() )
    {
        return false;
    }
    const uint64_t charge = ( static_cast<uint64_t>( ticks ) + 1 ) * sizeof( Observations ) +
                            input.Contacts() * sizeof( ContactSummary ) + input.Iterations() * sizeof( IterationSummary );
    if ( residentBytes + charge + input.ScratchBytes() > PhysicsComparison::MEMORY_BUDGET )
    {
        return false;
    }
    for ( const auto& entry : input.Index() )
    {
        if ( !ValidFrameIndex( entry, ticks, tickOffset ) )
        {
            return false;
        }
    }
    residentBytes += charge;
    observations.resize( static_cast<std::size_t>( ticks ) + 1 );
    std::vector<uint32_t> words;
    std::size_t maxWords = 0;
    for ( const auto& entry : input.Index() )
    {
        maxWords = (std::max)( maxWords, static_cast<std::size_t>( entry.Words() ) );
    }
    words.reserve( maxWords );
    ObservationBodyRows rows;
    if ( progress )
    {
        progress->Begin( side ? "B: reading binary contact evidence" : "A: reading binary contact evidence", side ? 60 : 30,
                         25 );
    }
    std::size_t complete = 0;
    for ( const auto& entry : input.Index() )
    {
        if ( ( progress && progress->cancelled.load( std::memory_order_relaxed ) ) || !input.ReadFrame( entry, words ) )
        {
            return false;
        }
        const int tick = entry.frame + tickOffset;
        auto& observed = observations[static_cast<std::size_t>( tick )];
        observed.recorded = ( entry.flags & 1 ) != 0;
        observed.iterationsRecorded = ( entry.flags & 2 ) != 0;
        observed.contacts.resize( entry.contacts );
        observed.iterations.resize( entry.iterations );
        rows.Prepare( Frame( tick ) );
        for ( std::size_t i = 0; i < observed.contacts.size(); ++i )
        {
            if ( !ReadContactWords( words.data() + i * 13, observed.contacts[i], rows ) )
            {
                return false;
            }
        }
        for ( std::size_t i = 0; i < observed.iterations.size(); ++i )
        {
            if ( !ReadIterationWords( words.data() + observed.contacts.size() * 13 + i * 5, observed.iterations[i] ) )
            {
                return false;
            }
        }
        if ( progress )
        {
            progress->Update( ++complete, input.Index().size() );
        }
    }
    return true;
}
