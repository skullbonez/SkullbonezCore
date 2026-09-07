#include "PhysicsComparison.h"
#include <algorithm>
#include <tuple>

using namespace SkullbonezCore::Runtime;
using SkullbonezCore::Physics::PhysicsSolverPersistentContactSample;

namespace
{
uint64_t ContactBodyId( const ReplaySolverFrameSample& frame, int modelRow )
{
    for ( const auto& body : frame.bodies )
    {
        if ( body.modelRow.value == modelRow )
        {
            return body.id.value;
        }
    }
    return 0;
}
auto ContactKey( const ReplaySolverFrameSample& frame, const PhysicsSolverPersistentContactSample& contact )
{
    // Keep directed endpoint identity: a feature ID may encode its reference
    // face. Reversing endpoints without remapping that feature is ambiguous.
    return std::tuple( ContactBodyId( frame, contact.bodyA ), contact.isTerrain ? 0 : ContactBodyId( frame, contact.bodyB ),
                       contact.isTerrain, contact.featureId );
}
bool SameVector( const SkullbonezCore::Math::Vector::Vector3& a, const SkullbonezCore::Math::Vector::Vector3& b )
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}
bool SameContact( const PhysicsSolverPersistentContactSample& a, const PhysicsSolverPersistentContactSample& b )
{
    return SameVector( a.normal, b.normal ) && SameVector( a.rA, b.rA ) && SameVector( a.rB, b.rB ) &&
           SameVector( a.tangent1, b.tangent1 ) && SameVector( a.tangent2, b.tangent2 ) && a.accN == b.accN &&
           a.accT1 == b.accT1 && a.accT2 == b.accT2 && a.penetration == b.penetration && a.normalMass == b.normalMass &&
           a.bias == b.bias && a.warmStarted == b.warmStarted;
}
} // namespace

void PhysicsComparison::BuildContactEvents( int tick )
{
    const bool observed = BuildObservedContactEvents( tick );
    const auto* a = m_recordings[0].Evidence( tick );
    const auto* b = m_recordings[1].Evidence( tick );
    if ( !a || !b )
    {
        if ( !observed && ( a || b ) )
        {
            m_events.push_back( { tick, 0, 0, 0, ComparisonFamily::Contact, ComparisonChange::NotRecorded } );
        }
        return;
    }
    const auto& contactsA = a->worldSnapshot.physics.persistentContacts;
    const auto& contactsB = b->worldSnapshot.physics.persistentContacts;
    std::vector<bool> matched( contactsB.size(), false );
    std::vector<bool> ambiguous( contactsB.size(), false );
    for ( std::size_t i = 0; i < contactsA.size(); ++i )
    {
        const auto& contact = contactsA[i];
        const auto key = ContactKey( *a, contact );
        const int countA = static_cast<int>( std::count_if( contactsA.begin(), contactsA.end(), [&]( const auto& other )
                                                            { return ContactKey( *a, other ) == key; } ) );
        int found = -1, countB = 0;
        for ( std::size_t j = 0; j < contactsB.size(); ++j )
        {
            if ( ContactKey( *b, contactsB[j] ) == key )
            {
                found = static_cast<int>( j );
                ++countB;
            }
        }
        ComparisonEvent event { tick,
                                std::get<0>( key ),
                                std::get<1>( key ),
                                contact.featureId,
                                contact.isTerrain ? ComparisonFamily::Terrain : ComparisonFamily::Contact,
                                ComparisonChange::OnlyA,
                                static_cast<int>( i ),
                                -1 };
        const auto exactA = std::count_if( contactsA.begin(), contactsA.end(), [&]( const auto& other )
                                           { return ContactKey( *a, other ) == key && SameContact( contact, other ); } );
        const auto exactB = std::count_if( contactsB.begin(), contactsB.end(), [&]( const auto& other )
                                           { return ContactKey( *b, other ) == key && SameContact( contact, other ); } );
        int exact = -1;
        if ( countA == countB && exactA == exactB && event.bodyA && ( contact.isTerrain || event.bodyB ) )
        {
            for ( std::size_t j = 0; j < contactsB.size(); ++j )
            {
                if ( !matched[j] && ContactKey( *b, contactsB[j] ) == key && SameContact( contact, contactsB[j] ) )
                {
                    exact = static_cast<int>( j );
                    break;
                }
            }
        }
        if ( exact >= 0 )
        {
            event.contactB = exact;
            matched[static_cast<std::size_t>( exact )] = true;
            event.change = ComparisonChange::Equal;
        }
        else if ( countA > 1 || countB > 1 || !event.bodyA || ( !contact.isTerrain && !event.bodyB ) )
        {
            event.change = ComparisonChange::Ambiguous;
            for ( std::size_t j = 0; j < contactsB.size(); ++j )
            {
                if ( ContactKey( *b, contactsB[j] ) == key )
                {
                    ambiguous[j] = true;
                }
            }
        }
        else if ( found >= 0 )
        {
            event.contactB = found;
            matched[static_cast<std::size_t>( found )] = true;
            event.change = SameContact( contact, contactsB[static_cast<std::size_t>( found )] ) ? ComparisonChange::Equal
                                                                                                : ComparisonChange::Changed;
        }
        m_events.push_back( event );
    }
    for ( std::size_t i = 0; i < contactsB.size(); ++i )
    {
        if ( matched[i] )
        {
            continue;
        }
        const auto& contact = contactsB[i];
        const auto key = ContactKey( *b, contact );
        m_events.push_back( { tick, std::get<0>( key ), std::get<1>( key ), contact.featureId,
                              contact.isTerrain ? ComparisonFamily::Terrain : ComparisonFamily::Contact,
                              ambiguous[i] ? ComparisonChange::Ambiguous : ComparisonChange::OnlyB, -1,
                              static_cast<int>( i ) } );
    }
}

void PhysicsComparison::BuildEvents( ComparisonLoadProgress* progress )
{
    m_events.clear();
    for ( int tick = 0; tick <= m_lastTick; ++tick )
    {
        if ( progress )
        {
            if ( progress->cancelled.load( std::memory_order_relaxed ) )
            {
                return;
            }
            progress->percent.store( 80 + tick * 20 / (std::max)( 1, m_lastTick ), std::memory_order_relaxed );
        }
        const auto* a = m_recordings[0].Frame( tick );
        const auto* b = m_recordings[1].Frame( tick );
        if ( a )
        {
            for ( const auto& body : a->bodies )
            {
                const auto difference = Difference( body.id.value, tick );
                if ( difference.change != ComparisonChange::Equal )
                {
                    m_events.push_back( { tick, body.id.value, 0, 0,
                                          difference.sleepChanged ? ComparisonFamily::Sleep : ComparisonFamily::Motion,
                                          difference.change } );
                }
            }
        }
        if ( b )
        {
            for ( const auto& body : b->bodies )
            {
                if ( !Body( 0, body.id.value, tick ) )
                {
                    m_events.push_back( { tick, body.id.value, 0, 0, ComparisonFamily::Motion, ComparisonChange::OnlyB } );
                }
            }
        }
        BuildContactEvents( tick );
    }
}
