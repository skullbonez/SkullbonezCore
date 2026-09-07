// Independent dynamic-body components own stopping decisions. Static anchors
// never merge otherwise unrelated constraints into one convergence group.
#pragma once
#include "PhysicsStageCapacity.h"
#include "PhysicsHandles.h"
#include <span>

namespace SkullbonezCore::Physics
{
class PhysicsBodyStore;
class PointJointBlock;
struct SolverBodyState;
struct PersistentContact;

class ConstraintIslandConvergence
{
    float m_maxImpulseDeltaSq = 0.0f;
    int m_minimumSweeps = 1;
    int m_maximumSweeps = 1;
    int m_completedSweeps = 0;
    bool m_active = false;
    bool m_included = false;

  public:
    void Exclude()
    {
        m_active = false;
        m_included = false;
    }
    bool Included() const
    {
        return m_included;
    }
    void IncludeConstraint( bool joint, int contactIterations );
    void BeginSweep()
    {
        m_maxImpulseDeltaSq = 0.0f;
    }
    void ObserveImpulseDelta( float squaredDelta );
    bool FinishSweep();
    bool Active() const
    {
        return m_active;
    }
};

class ConstraintIslandSchedule
{
    PhysicsBodyRowList<int> m_parents { "ConstraintIslandSchedule.parents", PhysicsCapacityReason::SceneBodies };
    PhysicsBodyRowList<ConstraintIslandConvergence> m_convergence { "ConstraintIslandSchedule.convergence",
                                                                    PhysicsCapacityReason::SceneBodies };
    int FindRoot( int body ) const;
    void Join( int bodyA, int bodyB, const PhysicsBodyStore& bodies );

  public:
    void Reserve( std::size_t bodyCapacity );
    void Clear();
    uint64_t CapacityBytes() const;
    void Prepare( const PhysicsBodyStore& bodyStore, std::span<const SolverBodyState> bodies,
                  std::span<const PersistentContact> contacts, std::span<const PointJointBlock> joints,
                  int contactIterations );
    int RootForPair( int bodyA, int bodyB ) const;
    bool Active( int bodyA, int bodyB ) const;
    bool Included( int body ) const;
    void RestrictToReleasedComponents( std::span<const int> released );
    void BeginSweep();
    void Observe( int bodyA, int bodyB, float squaredDelta );
    bool FinishSweep();
};
} // namespace SkullbonezCore::Physics
