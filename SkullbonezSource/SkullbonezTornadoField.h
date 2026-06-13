#pragma once


// --- Includes ---
#include <vector>
#include "SkullbonezMatrix4.h"
#include "SkullbonezVector3.h"


namespace SkullbonezCore
{
namespace Physics
{
struct TornadoFieldConfig
{
    bool enabled = false;
    bool visualizeVelocityField = false;
    Math::Vector::Vector3 center = Math::Vector::Vector3( 620.0f, 25.0f, 615.0f );
    float radius = 210.0f;
    float height = 140.0f;
    float inwardAcceleration = 120.0f;
    float swirlAcceleration = 170.0f;
    float liftAcceleration = 78.0f;
};

class TornadoField
{
  public:
    TornadoField();

    void SetConfig( const TornadoFieldConfig& config );
    const TornadoFieldConfig& GetConfig() const
    {
        return m_config;
    }

    Math::Vector::Vector3 SampleAcceleration( const Math::Vector::Vector3& position ) const;
    void RenderVectors( const Math::Transformation::Matrix4& viewProj );

  private:
    TornadoFieldConfig m_config;
    std::vector<float> m_lineData;
};
} // namespace Physics
} // namespace SkullbonezCore
