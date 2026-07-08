/*
File: SkullbonezSource/Assets/AssetKeys.h
Purpose:
  Owns legacy hash keys used to address built-in textures, authored cameras,
  profiler markers, and other string-keyed runtime registries.

Mental model:
  These keys are stable data contracts. The compile-time hash keeps old tables
  compact, but the original strings still define the authored asset or camera
  identity humans edit in scene/config data.

Glossary:
  FNV-1a: Small deterministic hash used for legacy string-key lookup.
  Texture key: Hash that selects a fixed TextureCollection slot.
  Camera key: Hash that selects an authored CameraCollection slot.

Invariants:
  - HashStr must stay deterministic and constexpr because profiler markers and
    fixed texture/camera keys are evaluated at compile time.
  - The literal names below are serialized or authored contracts; changing one
    is a data migration, not a local renderer tweak.

Related:
  - SkullbonezSource/Core/Common.h includes this during the aliasing period.
  - SkullbonezSource/Assets/TextureCollection.h consumes texture keys.
  - SkullbonezSource/Runtime/CameraCollection.h consumes camera keys.
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include <cstdint>

// FNV-1a 32-bit compile-time hash for legacy string-key registries.
constexpr std::uint32_t HashStr( const char* s, std::uint32_t hash = 2166136261u )
{
    return ( *s == '\0' ) ? hash : HashStr( s + 1, ( hash ^ static_cast<std::uint32_t>( *s ) ) * 16777619u );
}

constexpr std::uint32_t TEXTURE_GROUND = HashStr( "Ground" );
constexpr std::uint32_t TEXTURE_BOUNDING_SPHERE = HashStr( "BoundingSphere" );
constexpr std::uint32_t TEXTURE_SKY_LEFT = HashStr( "SkyLeft" );
constexpr std::uint32_t TEXTURE_SKY_RIGHT = HashStr( "SkyRight" );
constexpr std::uint32_t TEXTURE_SKY_FRONT = HashStr( "SkyFront" );
constexpr std::uint32_t TEXTURE_SKY_BACK = HashStr( "SkyBack" );
constexpr std::uint32_t TEXTURE_SKY_UP = HashStr( "SkyUp" );
constexpr std::uint32_t TEXTURE_SKY_DOWN = HashStr( "SkyDown" );

constexpr std::uint32_t CAMERA_GAME_MODEL_1 = HashStr( "GameModel1" );
constexpr std::uint32_t CAMERA_GAME_MODEL_2 = HashStr( "GameModel2" );
constexpr std::uint32_t CAMERA_FREE = HashStr( "Free" );
