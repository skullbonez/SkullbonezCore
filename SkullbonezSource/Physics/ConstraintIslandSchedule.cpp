#include "ConstraintIslandSchedule.h"
#include "PhysicsBodyStore.h"
#include "PhysicsDiagnosticsView.h"
#include "PointJointBlock.h"
#include <tuple>

using namespace SkullbonezCore::Physics;

void ConstraintIslandConvergence::IncludeConstraint( bool joint, int contactIterations )
{
    m_active = true;
    m_included = true;
    m_minimumSweeps = joint ? POINT_JOINT_SOLVER_ITERATIONS : m_minimumSweeps;
    m_maximumSweeps = (std::max)( contactIterations, m_minimumSweeps );
}
void ConstraintIslandConvergence::ObserveImpulseDelta( float squaredDelta )
{
    m_maxImpulseDeltaSq = (std::max)( m_maxImpulseDeltaSq, squaredDelta );
}
bool ConstraintIslandConvergence::FinishSweep()
{
    if ( m_active )
    {
        ++m_completedSweeps;
        // Units: both scalar contact and vector joint deltas are impulses.
        // A maximum, never a sum over rows, controls this component only.
        m_active = m_completedSweeps < m_maximumSweeps &&
                   ( m_completedSweeps < m_minimumSweeps || m_maxImpulseDeltaSq >= 1.0e-6f );
    }
    return m_active;
}
void ConstraintIslandSchedule::Reserve( std::size_t bodyCapacity )
{
    m_parents.Reserve( bodyCapacity );
    m_convergence.Reserve( bodyCapacity );
}
void ConstraintIslandSchedule::Clear()
{
    m_parents.clear();
    m_convergence.clear();
}
uint64_t ConstraintIslandSchedule::CapacityBytes() const
{
    return m_parents.capacity() * sizeof( int ) + m_convergence.capacity() * sizeof( ConstraintIslandConvergence );
}
int ConstraintIslandSchedule::FindRoot( int body ) const
{
    if ( body < 0 || static_cast<std::size_t>( body ) >= m_parents.size() )
    {
        return -1;
    }
    int root = m_parents[static_cast<std::size_t>( body )];
    while ( root >= 0 && m_parents[static_cast<std::size_t>( root )] != root )
    {
        root = m_parents[static_cast<std::size_t>( root )];
    }
    return root;
}
void ConstraintIslandSchedule::Join( int bodyA, int bodyB, const PhysicsBodyStore& bodies )
{
    const int rootA = FindRoot( bodyA );
    const int rootB = FindRoot( bodyB );
    if ( rootA < 0 || rootB < 0 || rootA == rootB )
    {
        return;
    }
    const auto records = bodies.Records();
    const auto key = [&]( int row )
    {
        const auto& record = records[static_cast<std::size_t>( row )];
        return std::tuple( record.sceneObjectId.value, record.handle.index, record.handle.generation );
    };
    const bool aFirst = key( rootA ) < key( rootB );
    m_parents[static_cast<std::size_t>( aFirst ? rootB : rootA )] = aFirst ? rootA : rootB;
}
void ConstraintIslandSchedule::Prepare( const PhysicsBodyStore& bodyStore, std::span<const SolverBodyState> bodies,
                                        std::span<const PersistentContact> contacts, std::span<const PointJointBlock> joints,
                                        int contactIterations )
{
    m_parents.ResetDefault( bodies.size() );
    m_convergence.ResetDefault( bodies.size() );
    for ( std::size_t i = 0; i < bodies.size(); ++i )
    {
        m_parents[i] = bodies[i].invMass > 0.0f ? static_cast<int>( i ) : -1;
    }
    for ( const auto& contact : contacts )
    {
        Join( contact.bodyA, contact.bodyB, bodyStore );
    }
    for ( const auto& joint : joints )
    {
        if ( joint.Active() )
        {
            Join( joint.BodyA(), joint.BodyB(), bodyStore );
        }
    }
    for ( std::size_t i = 0; i < bodies.size(); ++i )
    {
        m_parents[i] = FindRoot( static_cast<int>( i ) );
    }
    for ( const auto& contact : contacts )
    {
        const int root = RootForPair( contact.bodyA, contact.bodyB );
        if ( root >= 0 )
        {
            m_convergence[static_cast<std::size_t>( root )].IncludeConstraint( false, contactIterations );
        }
    }
    for ( const auto& joint : joints )
    {
        const int root = RootForPair( joint.BodyA(), joint.BodyB() );
        if ( joint.Active() && root >= 0 )
        {
            m_convergence[static_cast<std::size_t>( root )].IncludeConstraint( true, contactIterations );
        }
    }
}
int ConstraintIslandSchedule::RootForPair( int bodyA, int bodyB ) const
{
    const int rootA = FindRoot( bodyA );
    return rootA >= 0 ? rootA : FindRoot( bodyB );
}
bool ConstraintIslandSchedule::Active( int bodyA, int bodyB ) const
{
    const int root = RootForPair( bodyA, bodyB );
    return root >= 0 && m_convergence[static_cast<std::size_t>( root )].Active();
}
void ConstraintIslandSchedule::BeginSweep()
{
    for ( auto& island : m_convergence )
    {
        island.BeginSweep();
    }
}
void ConstraintIslandSchedule::Observe( int bodyA, int bodyB, float squaredDelta )
{
    const int root = RootForPair( bodyA, bodyB );
    if ( root >= 0 )
    {
        m_convergence[static_cast<std::size_t>( root )].ObserveImpulseDelta( squaredDelta );
    }
}
bool ConstraintIslandSchedule::FinishSweep()
{
    bool active = false;
    for ( auto& island : m_convergence )
    {
        active = island.FinishSweep() || active;
    }
    return active;
}

bool ConstraintIslandSchedule::Included( int body ) const
{
    const int root = FindRoot( body );
    return root >= 0 && m_convergence[static_cast<std::size_t>( root )].Included();
}
void ConstraintIslandSchedule::RestrictToReleasedComponents( std::span<const int> released )
{
    for ( std::size_t i = 0; i < m_convergence.size(); ++i )
    {
        const bool affected = std::any_of( released.begin(), released.end(),
                                           [&]( int body ) { return FindRoot( body ) == static_cast<int>( i ); } );
        if ( !affected )
        {
            m_convergence[i].Exclude();
        }
    }
}
