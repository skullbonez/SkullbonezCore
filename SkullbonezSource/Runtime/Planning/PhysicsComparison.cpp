#include "PhysicsComparison.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include "../../Core/Allocation/RuntimeAllocationTracker.h"

using namespace SkullbonezCore::Runtime;

const ReplayPresentationSample* ComparisonRecording::Frame( int tick ) const noexcept
{
    tick -= tickOffset;
    const auto found = std::lower_bound( frames.begin(), frames.end(), tick, []( const auto& frame, int value ) { return frame.sceneFrame < value; } );
    return found != frames.end() && found->sceneFrame == tick ? &*found : nullptr;
}

const ReplaySolverFrameSample* ComparisonRecording::Evidence( int tick ) const noexcept
{
    tick -= tickOffset;
    const auto found = std::lower_bound( diagnostics.begin(), diagnostics.end(), tick, []( const auto& frame, int value ) { return frame.sceneFrame < value; } );
    return found != diagnostics.end() && found->sceneFrame == tick ? &*found : nullptr;
}

const ReplayBodyPresentationSample* PhysicsComparison::Body( int side, uint64_t id, int tick ) const noexcept
{
    const auto* frame = Recording( side ).Frame( tick );
    if ( !frame )
    {
        return nullptr;
    }
    const auto found = std::lower_bound( frame->bodies.begin(), frame->bodies.end(), id, []( const auto& body, uint64_t value ) { return body.id.value < value; } );
    return found != frame->bodies.end() && found->id.value == id ? &*found : nullptr;
}

ComparisonBodyDifference PhysicsComparison::Compare( const ReplayBodyPresentationSample* a, const ReplayBodyPresentationSample* b ) noexcept
{
    ComparisonBodyDifference result;
    result.id = a ? a->id.value : b ? b->id.value : 0;
    if ( !a || !b )
    {
        result.change = a ? ComparisonChange::OnlyA : b ? ComparisonChange::OnlyB : ComparisonChange::NotRecorded;
        return result;
    }
    const auto delta = b->position - a->position;
    result.distance = std::sqrt( delta.x * delta.x + delta.y * delta.y + delta.z * delta.z );
    result.heightDelta = delta.y;
    result.verticalVelocityDelta = b->linearVelocity.y - a->linearVelocity.y;
    const auto velocityDelta = b->linearVelocity - a->linearVelocity;
    result.velocityDistance = std::sqrt( velocityDelta.x * velocityDelta.x + velocityDelta.y * velocityDelta.y + velocityDelta.z * velocityDelta.z );
    // q and -q describe the same rotation. Normalize the dot product so
    // binary32 normalization drift cannot invent a rotation difference.
    double dot = 0, aa = 0, bb = 0;
    for ( int i = 0; i < 4; ++i )
    {
        dot += static_cast<double>( a->orientation[i] ) * b->orientation[i];
        aa += static_cast<double>( a->orientation[i] ) * a->orientation[i];
        bb += static_cast<double>( b->orientation[i] ) * b->orientation[i];
    }
    const double cosine = aa > 0 && bb > 0 ? std::clamp( std::abs( dot ) / std::sqrt( aa * bb ), 0.0, 1.0 ) : 0;
    result.angleDegrees = cosine >= 1.0 - 1.0e-15 ? 0.0f : static_cast<float>( 2 * std::acos( cosine ) * 180 / 3.141592653589793 );
    result.sleepChanged = a->sleeping != b->sleeping;
    const bool velocityChanged = a->linearVelocity.x != b->linearVelocity.x || a->linearVelocity.y != b->linearVelocity.y || a->linearVelocity.z != b->linearVelocity.z ||
                                 a->angularVelocity.x != b->angularVelocity.x || a->angularVelocity.y != b->angularVelocity.y || a->angularVelocity.z != b->angularVelocity.z;
    result.change = result.distance > 0 || result.angleDegrees > 0 || result.sleepChanged || velocityChanged ? ComparisonChange::Changed : ComparisonChange::Equal;
    return result;
}

ComparisonBodyDifference PhysicsComparison::Difference( uint64_t id, int tick ) const noexcept
{
    return Compare( Body( 0, id, tick ), Body( 1, id, tick ) );
}

void PhysicsComparison::Seek( int tick ) noexcept
{
    m_tick = std::clamp( tick, 0, m_lastTick );
    m_fraction = 0;
}
void PhysicsComparison::Step( int direction ) noexcept
{
    Play( 0 );
    Seek( m_tick + ( direction < 0 ? -1 : 1 ) );
}
void PhysicsComparison::Play( int direction ) noexcept
{
    m_direction = direction < 0 ? -1 : direction > 0 ? 1 : 0;
}
void PhysicsComparison::SetLoop( int first, int last, bool enabled ) noexcept
{
    m_loopStart = std::clamp( first, 0, m_lastTick );
    m_loopEnd = std::clamp( last, m_loopStart, m_lastTick );
    m_loop = enabled;
}
void PhysicsComparison::Advance( double seconds ) noexcept
{
    if ( !Active() || !m_direction || !std::isfinite( seconds ) || seconds <= 0 )
    {
        return;
    }
    m_fraction += (std::min)( seconds, 1.0 ) * 120.0 * std::clamp( m_settings.speed, 0.05f, 8.0f );
    const int steps = static_cast<int>( m_fraction );
    m_fraction -= steps;
    const int first = m_loop ? m_loopStart : 0;
    const int last = m_loop ? m_loopEnd : m_lastTick;
    const int next = m_tick + steps * m_direction;
    if ( m_loop )
    {
        const int count = last - first + 1;
        m_tick = first + ( ( next - first ) % count + count ) % count;
    }
    else
    {
        m_tick = std::clamp( next, first, last );
        if ( next < first || next > last )
        {
            Play( 0 );
        }
    }
}
void PhysicsComparison::Select( uint64_t id ) noexcept
{
    m_selected = id;
    m_selectedEvent = -1;
    m_settings.orbitSelected = id != 0;
    m_settings.followA = id != 0;
}
bool PhysicsComparison::SelectEvent( std::size_t index ) noexcept
{
    if ( index >= m_events.size() )
    {
        return false;
    }
    ++m_eventSelectionRevision;
    m_selectedEvent = static_cast<int>( index );
    m_selected = m_events[index].bodyA;
    m_settings.orbitSelected = m_selected != 0;
    m_settings.followA = m_selected != 0;
    Play( 0 );
    Seek( m_events[index].tick );
    return true;
}
bool PhysicsComparison::VisibleEvent( const ComparisonEvent& event ) const noexcept
{
    if ( m_settings.selectedOnly && m_selected && event.bodyA != m_selected && event.bodyB != m_selected )
    {
        return false;
    }
    if ( m_settings.differencesOnly && event.change == ComparisonChange::Equal )
    {
        return false;
    }
    if ( event.family == ComparisonFamily::Motion && event.change == ComparisonChange::Changed )
    {
        const auto difference = Difference( event.bodyA, event.tick );
        return difference.distance >= m_settings.positionThreshold || difference.angleDegrees >= m_settings.angleThreshold || difference.sleepChanged ||
               difference.velocityDistance >= m_settings.positionThreshold;
    }
    return true;
}
bool PhysicsComparison::NextDifference() noexcept
{
    for ( std::size_t i = 0; i < m_events.size(); ++i )
    {
        if ( m_events[i].tick > m_tick && m_events[i].change != ComparisonChange::Equal && m_events[i].change != ComparisonChange::NotRecorded && VisibleEvent( m_events[i] ) )
        {
            return SelectEvent( i );
        }
    }
    return false;
}
void PhysicsComparison::Close() noexcept
{
    m_velocityExperiment = false;
    m_recordings = {};
    m_events = std::vector<ComparisonEvent> {};
    m_memoryCharge = 0;
    m_direction = 0;
    m_selected = 0;
    m_selectedEvent = -1;
}
bool PhysicsComparison::SetDisplay( const char* name ) noexcept
{
    const char* names[] = { "split", "overlay", "toggle", "heatmap", "pixels" };
    for ( int i = 0; i < 5; ++i )
    {
        if ( std::strcmp( name, names[i] ) == 0 )
        {
            m_settings.display = static_cast<ComparisonDisplay>( i );
            return true;
        }
    }
    return false;
}
bool PhysicsComparison::SetSetting( const char* name, double value ) noexcept
{
    if ( !std::isfinite( value ) )
    {
        return false;
    }
    auto boolean = [&]( const char* key, bool& field )
    {
        if ( std::strcmp( key, name ) != 0 )
        {
            return false;
        }
        field = value != 0;
        return true;
    };
    if ( boolean( "showA", m_settings.showA ) || boolean( "angularHeatmap", m_settings.angularHeatmap ) || boolean( "occludedOutline", m_settings.occludedOutline ) ||
         boolean( "followA", m_settings.followA ) || boolean( "stackedViews", m_settings.stackedViews ) || boolean( "orbitSelected", m_settings.orbitSelected ) ||
         boolean( "selectedOnly", m_settings.selectedOnly ) || boolean( "differencesOnly", m_settings.differencesOnly ) )
    {
        return true;
    }
    if ( value < 0 || value > 1000000 )
    {
        return false;
    }
    auto number = [&]( const char* key, float& field )
    {
        if ( std::strcmp( key, name ) != 0 )
        {
            return false;
        }
        field = static_cast<float>( value );
        return true;
    };
    if ( std::strcmp( name, "speed" ) == 0 )
    {
        if ( value < 0.05 || value > 8 )
        {
            return false;
        }
        m_settings.speed = static_cast<float>( value );
        return true;
    }
    if ( std::strcmp( name, "outlineAlpha" ) == 0 )
    {
        if ( value > 1 )
        {
            return false;
        }
        m_settings.outlineAlpha = static_cast<float>( value );
        return true;
    }
    if ( std::strcmp( name, "heatScale" ) == 0 && value <= 0 )
    {
        return false;
    }
    return number( "heatScale", m_settings.heatScale ) || number( "pixelGain", m_settings.pixelGain ) || number( "positionThreshold", m_settings.positionThreshold ) ||
           number( "angleThreshold", m_settings.angleThreshold ) || number( "impulseThreshold", m_settings.impulseThreshold );
}

// Capture is an explicit cold import into Solver Lab's existing capped owner.
// Only published motion is copied; missing contact/iteration evidence stays absent.
bool PhysicsComparison::LoadPredictionFrames( std::span<const RunReplayPredictionFrame> blue, std::span<const RunReplayPredictionFrame> red )
{
    using namespace SkullbonezCore::Core::Allocation;
    if ( blue.empty() || blue.size() != red.size() )
    {
        return false;
    }
    uint64_t bytes = sizeof( PhysicsComparison );
    for ( std::size_t tick = 0; tick < blue.size(); ++tick )
    {
        if ( blue[tick].frameIndex != red[tick].frameIndex ||
             std::abs( ( blue[tick].simulationSeconds - blue.front().simulationSeconds ) - ( red[tick].simulationSeconds - red.front().simulationSeconds ) ) > 1.0e-6 )
        {
            return false;
        }
        // Reserve the worst case of one difference per body on either side.
        bytes += 2 * sizeof( ReplayPresentationSample ) + ( blue[tick].bodies.size() + red[tick].bodies.size() ) * ( sizeof( ReplayBodyPresentationSample ) + sizeof( ComparisonEvent ) );
        if ( bytes > MEMORY_BUDGET )
        {
            return false;
        }
    }
    RuntimeAllocationScope loading( RuntimeAllocationPhase::Capture );
    Close();
    m_error.clear();
    m_scene.clear();
    m_bundle.clear();
    m_velocityExperiment = true;
    m_note = "Original and modified velocity. Predicted poses, linear velocity and sleep; solver details and angular velocity samples were not captured.";
    m_settings = {};
    m_memoryCharge = bytes;
    m_lastTick = static_cast<int>( blue.size() - 1 );
    m_tick = 0;
    m_loop = false;
    m_loopStart = 0;
    m_loopEnd = m_lastTick;
    m_fraction = 0;
    std::size_t eventCapacity = 0;
    for ( int side = 0; side < 2; ++side )
    {
        const auto source = side == 0 ? blue : red;
        auto& recording = m_recordings[side];
        recording.tickOffset = 0;
        recording.executable = side == 0 ? "Original" : "Modified";
        recording.frames.reserve( source.size() );
        for ( const auto& frame : source )
        {
            ReplayPresentationSample sample;
            sample.frameIndex = static_cast<ReplayFrameIndex>( recording.frames.size() );
            sample.sceneFrame = static_cast<int>( sample.frameIndex );
            sample.simulationSeconds = frame.simulationSeconds - source.front().simulationSeconds;
            sample.physicsDt = 1.0f / 120.0f;
            sample.bodies.reserve( frame.bodies.size() );
            for ( const auto& body : frame.bodies )
            {
                ReplayBodyPresentationSample value;
                value.id = body.id;
                value.modelRow = body.modelRow;
                value.position = body.position;
                value.linearVelocity = body.linearVelocity;
                body.orientation.GetComponents( value.orientation[0], value.orientation[1], value.orientation[2], value.orientation[3] );
                value.sleeping = body.sleeping;
                sample.bodies.push_back( value );
            }
            std::sort( sample.bodies.begin(), sample.bodies.end(), []( const auto& a, const auto& b ) { return a.id.value < b.id.value; } );
            eventCapacity += sample.bodies.size();
            recording.frames.push_back( std::move( sample ) );
        }
    }
    m_events.reserve( eventCapacity );
    BuildEvents();
    return true;
}
