#pragma once

#include <cstdint>
#include <vector>
#include "SkullbonezMatrix4.h"
#include "SkullbonezVector3.h"

namespace SkullbonezCore
{
namespace GameObjects
{
class GameModelCollection;
}

namespace Physics
{
enum PhysicsDebugFlags : uint32_t
{
    PHYSICS_DEBUG_NONE = 0u,
    PHYSICS_DEBUG_AXES = 1u << 0,
    PHYSICS_DEBUG_CONTACTS = 1u << 1,
    PHYSICS_DEBUG_SLEEP = 1u << 2,
    PHYSICS_DEBUG_ALL = PHYSICS_DEBUG_AXES | PHYSICS_DEBUG_CONTACTS | PHYSICS_DEBUG_SLEEP,
};

struct PhysicsDebugContact
{
    int bodyA = -1;
    int bodyB = -1;
    Math::Vector::Vector3 point = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 normal = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 tangent1 = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 tangent2 = Math::Vector::ZERO_VECTOR;
    float penetration = 0.0f;
    float normalImpulse = 0.0f;
};

class PhysicsDebugVisualizer
{
  private:
    uint32_t m_flags = PHYSICS_DEBUG_NONE;
    std::vector<float> m_lineData;

    void EmitLine( const Math::Vector::Vector3& a, const Math::Vector::Vector3& b, float r, float g, float bl );
    void EmitCross( const Math::Vector::Vector3& p, float size, float r, float g, float bl );
    void EmitArrow( const Math::Vector::Vector3& a, const Math::Vector::Vector3& b, float r, float g, float bl );
    void EmitRingXZ( const Math::Vector::Vector3& center, float radius, float yOffset, float r, float g, float bl );
    void EmitObjectAxes( GameObjects::GameModelCollection& models );
    void EmitContacts( GameObjects::GameModelCollection& models );
    void EmitSleepState( GameObjects::GameModelCollection& models );

  public:
    void SetFlags( uint32_t flags )
    {
        m_flags = flags & PHYSICS_DEBUG_ALL;
    }
    uint32_t GetFlags() const
    {
        return m_flags;
    }
    bool IsEnabled() const
    {
        return m_flags != PHYSICS_DEBUG_NONE;
    }
    void Render( GameObjects::GameModelCollection& models, const Math::Transformation::Matrix4& viewProj );
};
} // namespace Physics
} // namespace SkullbonezCore
