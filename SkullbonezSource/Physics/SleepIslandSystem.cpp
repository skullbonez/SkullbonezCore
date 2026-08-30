/*
File: SkullbonezSource/Physics/SleepIslandSystem.cpp
Purpose:
  Owns persistent simulation-island contact topology and support diagnostics.

Summary:
  SimulationIslandSystem compares canonical active contact edges across fixed
  steps and publishes body-local topology activity. Support propagation remains
  diagnostic and never chooses island membership.

Invariants:
  - Physics-visible behavior must remain deterministic; byte-exact baselines
    are the validation contract.
  - Support propagation reads fixed-body state from hot arrays, not directly
    from legacy model storage.
  - Support-edge producers fail before the construction-reserved vector can
    grow during steady gameplay.

Related:
  - SkullbonezSource/Physics/SleepIslandSystem.h
  - Agentic/Reference/physics-overview.md
  - Agentic/Reference/engine-glossary.md
*/
#include "SleepIslandSystem.h"

#include "PhysicsBodyStore.h"
#include "PhysicsDiagnosticsView.h"
#include "PhysicsWorld.h"
#include "Ragdoll.h"

#include <algorithm>
#include <bit>

using namespace SkullbonezCore::Physics;

namespace
{
bool IsFixedBody( const PhysicsBodyHotFieldsConstView& hotFields, int bodyIndex )
{
    return bodyIndex < 0 || bodyIndex >= static_cast<int>( hotFields.fixed.size() ) ||
           hotFields.fixed[static_cast<std::size_t>( bodyIndex )] != 0u;
}

uint64_t HashJointDescriptor( const PointJointConstraint& joint )
{
    uint64_t hash = 1469598103934665603ull;
    const auto add = [&]( uint64_t value )
    {
        hash ^= value;
        hash *= 1099511628211ull;
    };

    add( std::bit_cast<uint32_t>( joint.localAnchorA.x ) );
    add( std::bit_cast<uint32_t>( joint.localAnchorA.y ) );
    add( std::bit_cast<uint32_t>( joint.localAnchorA.z ) );
    add( std::bit_cast<uint32_t>( joint.localAnchorB.x ) );
    add( std::bit_cast<uint32_t>( joint.localAnchorB.y ) );
    add( std::bit_cast<uint32_t>( joint.localAnchorB.z ) );
    add( std::bit_cast<uint32_t>( joint.slack ) );
    add( std::bit_cast<uint32_t>( joint.stiffness ) );
    add( std::bit_cast<uint32_t>( joint.damping ) );
    add( joint.groupId );
    add( joint.flags );
    return hash;
}
} // namespace

void SimulationIslandSystem::Reserve( std::size_t bodyCapacity, std::size_t contactEdgeCapacity,
                                      std::size_t pointJointCapacity )
{
    m_previousContactEdges.Reserve( contactEdgeCapacity );
    m_activeContactEdges.Reserve( contactEdgeCapacity );
    m_previousJointEdges.Reserve( pointJointCapacity );
    m_activeJointEdges.Reserve( pointJointCapacity );
    m_previousStaticContacts.Reserve( bodyCapacity );
    m_activeStaticContacts.Reserve( bodyCapacity );
    m_topologyChangedBodies.Reserve( bodyCapacity );
}

void SimulationIslandSystem::Clear()
{
    m_previousContactEdges.clear();
    m_activeContactEdges.clear();
    m_previousJointEdges.clear();
    m_activeJointEdges.clear();
    m_previousStaticContacts.clear();
    m_activeStaticContacts.clear();
    m_topologyChangedBodies.clear();
    m_needsSeed = true;
}

void SimulationIslandSystem::Invalidate()
{
    Clear();
}

void SimulationIslandSystem::Rebuild( const PhysicsBodyStore& bodyStore,
                                      std::span<const PersistentContact> persistentContacts,
                                      std::span<const PointJointConstraint> pointJoints,
                                      std::span<const uint8_t> sleepState )
{
    const int modelCount = bodyStore.Count();
    const PhysicsBodyHotFieldsConstView hotFields = bodyStore.HotFields();
    m_activeContactEdges.clear();
    m_activeJointEdges.clear();
    m_activeStaticContacts.assign( static_cast<std::size_t>( modelCount ), 0u );
    m_topologyChangedBodies.assign( static_cast<std::size_t>( modelCount ), 0u );

    const auto appendContact = [&]( int bodyA, int bodyB )
    {
        if ( bodyA < 0 || bodyA >= modelCount )
        {
            return;
        }

        int dynamicA = bodyA;
        int dynamicB = bodyB;
        const bool fixedA = IsFixedBody( hotFields, dynamicA );
        const bool validB = dynamicB >= 0 && dynamicB < modelCount;
        const bool fixedB = !validB || IsFixedBody( hotFields, dynamicB );

        if ( fixedA && fixedB )
        {
            return;
        }

        if ( fixedA )
        {
            dynamicA = dynamicB;
            m_activeStaticContacts[static_cast<std::size_t>( dynamicA )] = 1u;
            return;
        }

        if ( fixedB )
        {
            m_activeStaticContacts[static_cast<std::size_t>( dynamicA )] = 1u;
            return;
        }

        if ( dynamicB < dynamicA )
        {
            std::swap( dynamicA, dynamicB );
        }

        const auto edge = std::make_pair( dynamicA, dynamicB );

        // Invariant: PersistentContact stores one manifold's rows contiguously.
        // Collapse that run before the bounded pair list sees it; the final
        // canonical sort still removes any duplicate introduced by restored
        // or dormant-edge input without an O(edges^2) hot-path search.
        if ( m_activeContactEdges.empty() || m_activeContactEdges.back() != edge )
        {
            m_activeContactEdges.push_back( edge );
        }
    };

    for ( const PersistentContact& contact : persistentContacts )
    {
        appendContact( contact.bodyA, contact.bodyB );
    }

    for ( const PointJointConstraint& joint : pointJoints )
    {
        int bodyA = joint.BodyAIndex( bodyStore );
        int bodyB = joint.BodyBIndex( bodyStore );

        if ( bodyA < 0 || bodyB < 0 || bodyA == bodyB || bodyA >= modelCount || bodyB >= modelCount ||
             IsFixedBody( hotFields, bodyA ) || IsFixedBody( hotFields, bodyB ) )
        {
            continue;
        }

        if ( bodyB < bodyA )
        {
            std::swap( bodyA, bodyB );
        }

        m_activeJointEdges.push_back(
            { joint.handle.index, joint.handle.generation, bodyA, bodyB, HashJointDescriptor( joint ) } );
    }

    std::sort( m_activeJointEdges.begin(), m_activeJointEdges.end() );

    for ( int bodyIndex = 0; bodyIndex < modelCount; ++bodyIndex )
    {
        if ( bodyIndex < static_cast<int>( m_previousStaticContacts.size() ) &&
             m_previousStaticContacts[static_cast<std::size_t>( bodyIndex )] != 0u &&
             bodyIndex < static_cast<int>( sleepState.size() ) && sleepState[bodyIndex] != 0u )
        {
            m_activeStaticContacts[static_cast<std::size_t>( bodyIndex )] = 1u;
        }
    }

    // Sleeping pairs do not enter the solver again. Their bodies cannot separate
    // until a wake, so the last exact edge remains topology while both endpoints
    // are asleep. A topology invalidation clears this history before compaction.
    for ( const auto& edge : m_previousContactEdges )
    {
        const int bodyA = edge.first;
        const int bodyB = edge.second;

        if ( bodyA >= 0 && bodyB >= 0 && bodyA < modelCount && bodyB < modelCount &&
             bodyA < static_cast<int>( sleepState.size() ) && bodyB < static_cast<int>( sleepState.size() ) &&
             sleepState[bodyA] != 0u && sleepState[bodyB] != 0u )
        {
            appendContact( bodyA, bodyB );
        }
    }

    std::sort( m_activeContactEdges.begin(), m_activeContactEdges.end() );
    m_activeContactEdges.erase( std::unique( m_activeContactEdges.begin(), m_activeContactEdges.end() ),
                                m_activeContactEdges.end() );

    if ( !m_needsSeed )
    {
        for ( int bodyIndex = 0; bodyIndex < modelCount; ++bodyIndex )
        {
            const bool hadStatic = bodyIndex < static_cast<int>( m_previousStaticContacts.size() ) &&
                                   m_previousStaticContacts[static_cast<std::size_t>( bodyIndex )] != 0u;
            const bool hasStatic = m_activeStaticContacts[static_cast<std::size_t>( bodyIndex )] != 0u;

            if ( hadStatic != hasStatic )
            {
                m_topologyChangedBodies[static_cast<std::size_t>( bodyIndex )] = 1u;
            }
        }

        std::size_t previousIndex = 0u;
        std::size_t activeIndex = 0u;
        const auto markEdge = [&]( const std::pair<int, int>& edge )
        {
            m_topologyChangedBodies[static_cast<std::size_t>( edge.first )] = 1u;
            m_topologyChangedBodies[static_cast<std::size_t>( edge.second )] = 1u;
        };

        while ( previousIndex < m_previousContactEdges.size() || activeIndex < m_activeContactEdges.size() )
        {
            if ( activeIndex >= m_activeContactEdges.size() ||
                 ( previousIndex < m_previousContactEdges.size() &&
                   m_previousContactEdges[previousIndex] < m_activeContactEdges[activeIndex] ) )
            {
                markEdge( m_previousContactEdges[previousIndex++] );
            }
            else if ( previousIndex >= m_previousContactEdges.size() ||
                      m_activeContactEdges[activeIndex] < m_previousContactEdges[previousIndex] )
            {
                markEdge( m_activeContactEdges[activeIndex++] );
            }
            else
            {
                ++previousIndex;
                ++activeIndex;
            }
        }

        previousIndex = 0u;
        activeIndex = 0u;

        while ( previousIndex < m_previousJointEdges.size() || activeIndex < m_activeJointEdges.size() )
        {
            if ( activeIndex >= m_activeJointEdges.size() ||
                 ( previousIndex < m_previousJointEdges.size() &&
                   m_previousJointEdges[previousIndex] < m_activeJointEdges[activeIndex] ) )
            {
                const SimulationIslandJointEdge& edge = m_previousJointEdges[previousIndex++];
                m_topologyChangedBodies[static_cast<std::size_t>( edge.bodyA )] = 1u;
                m_topologyChangedBodies[static_cast<std::size_t>( edge.bodyB )] = 1u;
            }
            else if ( previousIndex >= m_previousJointEdges.size() ||
                      m_activeJointEdges[activeIndex] < m_previousJointEdges[previousIndex] )
            {
                const SimulationIslandJointEdge& edge = m_activeJointEdges[activeIndex++];
                m_topologyChangedBodies[static_cast<std::size_t>( edge.bodyA )] = 1u;
                m_topologyChangedBodies[static_cast<std::size_t>( edge.bodyB )] = 1u;
            }
            else
            {
                ++previousIndex;
                ++activeIndex;
            }
        }
    }

    m_previousContactEdges.clear();

    for ( const auto& edge : m_activeContactEdges )
    {
        m_previousContactEdges.push_back( edge );
    }

    m_previousJointEdges.clear();

    for ( const SimulationIslandJointEdge& edge : m_activeJointEdges )
    {
        m_previousJointEdges.push_back( edge );
    }

    m_previousStaticContacts.clear();

    for ( uint8_t hasStaticContact : m_activeStaticContacts )
    {
        m_previousStaticContacts.push_back( hasStaticContact );
    }
    m_needsSeed = false;
}

std::span<const std::pair<int, int>> SimulationIslandSystem::ActiveContactEdges() const
{
    return m_activeContactEdges;
}

std::span<const SimulationIslandJointEdge> SimulationIslandSystem::ActiveJointEdges() const
{
    return m_activeJointEdges;
}

std::span<const uint8_t> SimulationIslandSystem::TopologyChangedBodies() const
{
    return m_topologyChangedBodies;
}

uint64_t SimulationIslandSystem::CollectDynamicMemoryBytes() const
{
    return m_previousContactEdges.committed_bytes() + m_activeContactEdges.committed_bytes() +
           m_previousJointEdges.committed_bytes() + m_activeJointEdges.committed_bytes() +
           m_previousStaticContacts.committed_bytes() + m_activeStaticContacts.committed_bytes() +
           m_topologyChangedBodies.committed_bytes();
}

void SleepSupportPropagationSystem::PropagateSupport( SleepSupportPropagationContext& context,
                                                      const PhysicsBodyHotFieldsConstView& hotFields )
{
    // Concept: support propagates upward through a stack.
    //
    // If object B is resting on object A, and A is fixed/asleep/supported, then
    // B can be considered supported too. Repeating that rule lets a whole tower
    // become one stable sleep island instead of requiring every object to touch
    // terrain directly.
    // Lifetime: the three rows below borrow controller-owned spans only for
    // this bounded propagation pass; the system retains no sleep state.
    auto& sleepState = context.sleepState;
    auto& sleepSupportEdges = context.sleepSupportEdges;
    auto& sleepSupportedThisFrame = context.sleepSupportedThisFrame;

    const int modelCount = static_cast<int>( hotFields.fixed.size() );

    if ( modelCount <= 0 || sleepSupportEdges.empty() )
    {
        return;
    }

    for ( int pass = 0; pass < modelCount; ++pass )
    {
        // This is a bounded fixed-point solve over support edges. At most
        // modelCount passes are needed because each successful pass marks at
        // least one additional body supported; early exit keeps the normal case
        // cheap.
        bool changed = false;

        for ( const auto& edge : sleepSupportEdges )
        {
            const int supporter = edge.first;
            const int supported = edge.second;

            if ( supporter < 0 || supporter >= modelCount || supported < 0 || supported >= modelCount )
            {
                continue;
            }

            bool supporterHasSupport = sleepSupportedThisFrame[supporter] != 0;

            if ( !supporterHasSupport && hotFields.fixed[static_cast<std::size_t>( supporter )] != 0u )
            {
                supporterHasSupport = true;
            }

            if ( !supporterHasSupport && supporter < static_cast<int>( sleepState.size() ) && sleepState[supporter] != 0 )
            {
                supporterHasSupport = true;
            }

            if ( supporterHasSupport && sleepSupportedThisFrame[supported] == 0 )
            {
                sleepSupportedThisFrame[supported] = 1;
                changed = true;
            }
        }

        if ( !changed )
        {
            break;
        }
    }
}
