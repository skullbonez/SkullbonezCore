/*
File: SkullbonezSource/Rendering/RenderMaterial.h
Purpose:
  Defines backend-neutral render material data for object rendering.

Mental model:
  Render materials describe visual intent before a backend packs that intent
  into shader constants, instance streams, or the current object material table.

Glossary:
  Material kind: Small render-facing category that chooses the current object
  shader's visual branch, such as matte, metal, foliage, or shore.
  Material instance payload: Three float4 rows appended after each instance
  model matrix and consumed by lit_textured_instanced.hlsl.
  Material table: Fixed t4 texture storing default material response values by
  material kind for the current object shader.
  Legacy tint bridge: Compatibility path that maps old tint/colorOverride scene
  values into material0.rgb and material0.w.
  Backend-neutral: Data that belongs to engine rendering intent, not to a DX12
  descriptor, root parameter, shader register, or GPU buffer layout.

Invariants:
  - Render materials are separate from physics/contact material ids.
  - textureMode preserves old tint.a material-mode semantics inside material0.w
    so legacy scenes and generated objects keep their visual branch.
  - material2.w carries the material-kind row sampled from the fixed t4 table.
  - material3.x carries per-object alpha; pass alpha multiplies it in the lit shader.

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

    // Compatibility payload: object shaders receive color and material mode
    // through the material0 row. This keeps old scene tint/colorOverride
    // authoring stable while adding typed roughness, specular, and emissive
    // values through the expanded material rows.
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

// Stable scene-file spelling for a material category. These names
// are authoring surface, so keep old spellings valid when adding new categories.
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

// Bridge note: the old tint.a material mode still maps into the new CPU
// material kind. The negative textured sentinel and positive solid-material
// values are preserved so existing scenes keep rendering the same while the
// bridge is in place.
inline RenderMaterialKind RenderMaterialKindFromLegacyMode( float legacyMode )
{
    if ( legacyMode < -0.5f )
    {
        return RenderMaterialKind::Textured;
    }
    if ( legacyMode > 1.25f )
    {
        const int mode = static_cast<int>( legacyMode + 0.5f );
        if ( mode >= static_cast<int>( RenderMaterialKind::Textured ) &&
             mode <= static_cast<int>( RenderMaterialKind::Pine ) )
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

// Legacy payload contract: the current object instance stream still consumes a
// packed tint.a material value.
inline float RenderMaterialKindLegacyMode( RenderMaterialKind kind )
{
    if ( kind == RenderMaterialKind::Textured )
    {
        return -1.0f;
    }
    return static_cast<float>( static_cast<int>( kind ) );
}

inline uint32_t RenderMaterialKindIndex( RenderMaterialKind kind )
{
    const uint32_t index = static_cast<uint32_t>( kind );
    return index <= static_cast<uint32_t>( RenderMaterialKind::Pine ) ? index : 0u;
}

// Material mode value mirrored into material0.w. The value keeps
// the old tint.a shader decision tree alive while newer fields carry typed
// material response data in material1/material2.
inline float RenderMaterialLegacyInstanceMode( const RenderMaterial& material )
{
    if ( material.kind != RenderMaterialKind::Textured )
    {
        return RenderMaterialKindLegacyMode( material.kind );
    }
    return material.textureMode;
}

// Default shader response for a material kind. Scene/material
// authoring can override these fields later, but the generated t4 material table
// uses the same defaults so CPU payloads and shader table rows stay coherent.
inline void ApplyRenderMaterialDefaults( RenderMaterial& material )
{
    material.roughness = 0.72f;
    material.metallic = 0.0f;
    material.specular = 0.35f;
    material.transmission = 0.0f;
    material.stylization = 0.0f;
    material.emissiveColor[0] = 0.0f;
    material.emissiveColor[1] = 0.0f;
    material.emissiveColor[2] = 0.0f;
    material.emissiveStrength = 0.0f;
    material.flags = 0;

    switch ( material.kind )
    {
    case RenderMaterialKind::Metal:
        material.roughness = 0.22f;
        material.metallic = 1.0f;
        material.specular = 0.88f;
        break;
    case RenderMaterialKind::Emissive:
        material.roughness = 0.38f;
        material.specular = 0.22f;
        material.emissiveColor[0] = material.baseColor[0];
        material.emissiveColor[1] = material.baseColor[1];
        material.emissiveColor[2] = material.baseColor[2];
        material.emissiveStrength = 1.35f;
        break;
    case RenderMaterialKind::Glass:
        material.roughness = 0.08f;
        material.specular = 0.92f;
        material.transmission = 0.72f;
        break;
    case RenderMaterialKind::Toon:
        material.roughness = 0.80f;
        material.specular = 0.18f;
        material.stylization = 0.72f;
        break;
    case RenderMaterialKind::LowPoly:
        material.roughness = 0.64f;
        material.specular = 0.14f;
        material.stylization = 1.0f;
        break;
    case RenderMaterialKind::Shadow:
        material.roughness = 0.96f;
        material.specular = 0.06f;
        break;
    case RenderMaterialKind::Foliage:
    case RenderMaterialKind::Pine:
        material.roughness = 0.86f;
        material.specular = 0.10f;
        material.stylization = 0.82f;
        break;
    case RenderMaterialKind::Bark:
        material.roughness = 0.90f;
        material.specular = 0.08f;
        material.stylization = 0.50f;
        break;
    case RenderMaterialKind::Stone:
    case RenderMaterialKind::Ridge:
    case RenderMaterialKind::Shore:
        material.roughness = 0.94f;
        material.specular = 0.12f;
        material.stylization = 0.58f;
        break;
    default:
        break;
    }
}

// Instance payload consumed by lit_textured_instanced.hlsl:
//
// material0 = base rgb plus legacy material mode in w.
// material1 = roughness, metallic, specular, emissive strength.
// material2 = emissive rgb plus t4 material-table row in w.
// material3 = per-object alpha plus room for later material controls.
struct RenderMaterialInstancePayload
{
    float material0[4];
    float material1[4];
    float material2[4];
    float material3[4];
};

// Packs CPU render intent into the exact per-instance float layout declared by
// the object shader input layout. Keep this mapping in lockstep with
// lit_textured_instanced.hlsl and the CreateInstancedMesh attribute list.
inline RenderMaterialInstancePayload PackRenderMaterialInstancePayload( const RenderMaterial& material )
{
    RenderMaterialInstancePayload payload = {};
    payload.material0[0] = material.baseColor[0];
    payload.material0[1] = material.baseColor[1];
    payload.material0[2] = material.baseColor[2];
    payload.material0[3] = RenderMaterialLegacyInstanceMode( material );

    payload.material1[0] = material.roughness;
    payload.material1[1] = material.metallic;
    payload.material1[2] = material.specular;
    payload.material1[3] = material.emissiveStrength;

    payload.material2[0] = material.emissiveColor[0];
    payload.material2[1] = material.emissiveColor[1];
    payload.material2[2] = material.emissiveColor[2];
    payload.material2[3] = static_cast<float>( RenderMaterialKindIndex( material.kind ) );

    payload.material3[0] = material.baseColor[3];
    payload.material3[1] = material.transmission;
    payload.material3[2] = static_cast<float>( material.flags );
    payload.material3[3] = 0.0f;
    return payload;
}

// Render intent reconstructed from the current shader-facing object payload. Callers
// should prefer explicit RenderMaterial data when they have it, but this helper
// keeps scene-authored material data and generated objects on the same path.
inline RenderMaterial MakeRenderMaterialFromLegacyTint( float tintR, float tintG, float tintB, float legacyMode )
{
    RenderMaterial material;
    material.kind = RenderMaterialKindFromLegacyMode( legacyMode );
    material.baseColor[0] = tintR;
    material.baseColor[1] = tintG;
    material.baseColor[2] = tintB;
    material.baseColor[3] = 1.0f;
    ApplyRenderMaterialDefaults( material );
    material.textureMode = legacyMode;
    return material;
}

} // namespace Rendering
} // namespace SkullbonezCore
