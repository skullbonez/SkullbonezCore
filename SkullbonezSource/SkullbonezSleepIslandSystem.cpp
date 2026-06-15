#include "SkullbonezSleepIslandSystem.h"

#include "SkullbonezGameModelCollection.h"
#include "SkullbonezPhysicsWorld.h"

using namespace SkullbonezCore::GameObjects;
using namespace SkullbonezCore::Physics;


void SleepIslandSystem::PropagateSupport( PhysicsWorld& world, GameModelCollection& collection )
{
    auto& m_gameModels = collection.m_gameModels;
    auto& m_sleepState = world.m_sleepState;
    auto& m_sleepSupportEdges = world.m_sleepSupportEdges;
    auto& m_sleepSupportedThisFrame = world.m_sleepSupportedThisFrame;

    const int modelCount = static_cast<int>( m_gameModels.size() );
    if ( modelCount <= 0 || m_sleepSupportEdges.empty() )
    {
        return;
    }

    for ( int pass = 0; pass < modelCount; ++pass )
    {
        bool changed = false;
        for ( const auto& edge : m_sleepSupportEdges )
        {
            const int supporter = edge.first;
            const int supported = edge.second;
            if ( supporter < 0 || supporter >= modelCount || supported < 0 || supported >= modelCount )
            {
                continue;
            }

            bool supporterHasSupport = m_sleepSupportedThisFrame[supporter] != 0;
            if ( !supporterHasSupport && m_gameModels[supporter].IsFixed() )
            {
                supporterHasSupport = true;
            }
            if ( !supporterHasSupport &&
                 supporter < static_cast<int>( m_sleepState.size() ) &&
                 m_sleepState[supporter] != 0 )
            {
                supporterHasSupport = true;
            }

            if ( supporterHasSupport && m_sleepSupportedThisFrame[supported] == 0 )
            {
                m_sleepSupportedThisFrame[supported] = 1;
                changed = true;
            }
        }

        if ( !changed )
        {
            break;
        }
    }
}
