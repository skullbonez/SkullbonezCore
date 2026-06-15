/*
File: SkullbonezSource/SkullbonezRenderMaterial.h
Purpose:
  Defines backend-neutral render material data for object rendering.

Mental model:
  Render materials describe visual intent before a backend packs that intent
  into shader constants, instance streams, or future material tables.

Invariants:
  - Render materials are separate from physics/contact material ids.
  - The compatibility textureMode value preserves the existing tint.a shader
    bridge until the object instance payload is expanded.

Related:
  - Agentic/Reference/shader-inventory.md
  - Agentic/Plans/shader-architecture-cleanup-plan.md
*/
#pragma once

#include <cstdint>

namespace SkullbonezCore
{
namespace Rendering
{
enum class RenderMaterialKind : uint8_t
{
    Textured = 0,
    Matte = 1,
    Metal = 2,
    Emissive = 3,
    Glass = 4,
    Toon = 5,
    LowPoly = 6,
    Shadow = 7,
    Foliage = 8,
    Bark = 9,
    Stone = 10,
    Ridge = 11,
    Shore = 12,
    Pine = 13
};

struct RenderMaterial
{
    char name[32] = {};
    RenderMaterialKind kind = RenderMaterialKind::Textured;

    float baseColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    float emissiveColor[3] = { 0.0f, 0.0f, 0.0f };
    float emissiveStrength = 0.0f;

    float roughness = 0.72f;
    float metallic = 0.0f;
    float specular = 0.35f;
    float transmission = 0.0f;

    float stylization = 0.0f;
    float textureMode = 0.0f;
    uint32_t flags = 0;
};

inline const char* RenderMaterialKindName( RenderMaterialKind kind )
{
    switch ( kind )
    {
    case RenderMaterialKind::Textured:
        return "textured";
    case RenderMaterialKind::Matte:
        return "matte";
    case RenderMaterialKind::Metal:
        return "metal";
    case RenderMaterialKind::Emissive:
        return "emissive";
    case RenderMaterialKind::Glass:
        return "glass";
    case RenderMaterialKind::Toon:
        return "toon";
    case RenderMaterialKind::LowPoly:
        return "lowpoly";
    case RenderMaterialKind::Shadow:
        return "shadow";
    case RenderMaterialKind::Foliage:
        return "foliage";
    case RenderMaterialKind::Bark:
        return "bark";
    case RenderMaterialKind::Stone:
        return "stone";
    case RenderMaterialKind::Ridge:
        return "ridge";
    case RenderMaterialKind::Shore:
        return "shore";
    case RenderMaterialKind::Pine:
        return "pine";
    default:
        return "textured";
    }
}

inline RenderMaterialKind RenderMaterialKindFromLegacyMode( float legacyMode )
{
    if ( legacyMode < -0.5f )
    {
        return RenderMaterialKind::Textured;
    }
    if ( legacyMode > 1.25f )
    {
        const int mode = static_cast<int>( legacyMode + 0.5f );
        if ( mode >= static_cast<int>( RenderMaterialKind::Textured ) && mode <= static_cast<int>( RenderMaterialKind::Pine ) )
        {
            return static_cast<RenderMaterialKind>( mode );
        }
    }
    if ( legacyMode > 0.5f )
    {
        return RenderMaterialKind::Matte;
    }
    return RenderMaterialKind::Textured;
}

inline float RenderMaterialKindLegacyMode( RenderMaterialKind kind )
{
    if ( kind == RenderMaterialKind::Textured )
    {
        return -1.0f;
    }
    return static_cast<float>( static_cast<int>( kind ) );
}

inline RenderMaterial MakeRenderMaterialFromLegacyTint( float tintR, float tintG, float tintB, float legacyMode )
{
    RenderMaterial material;
    material.kind = RenderMaterialKindFromLegacyMode( legacyMode );
    material.baseColor[0] = tintR;
    material.baseColor[1] = tintG;
    material.baseColor[2] = tintB;
    material.baseColor[3] = 1.0f;
    material.textureMode = legacyMode;
    return material;
}

inline float RenderMaterialLegacyInstanceMode( const RenderMaterial& material )
{
    return material.textureMode;
}
} // namespace Rendering
} // namespace SkullbonezCore
