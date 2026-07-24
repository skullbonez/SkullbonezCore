/*
File: SkullbonezSource/Assets/AssetKeys.h
Purpose:
  Owns legacy hash keys used to address built-in textures, authored cameras,
  profiler markers, and other string-keyed runtime registries.

Summary:
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
  - SkullbonezSource/Core/StringHash.h owns the shared hash function.
  - SkullbonezSource/Assets/TextureCollection.h consumes texture keys.
  - SkullbonezSource/Runtime/Camera/CameraCollection.h consumes camera keys.
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include "../Core/StringHash.h"

#include <cstdint>

constexpr std::uint32_t TEXTURE_GROUND = HashStr( "Ground" );
constexpr std::uint32_t TEXTURE_BOUNDING_SPHERE = HashStr( "BoundingSphere" );
constexpr std::uint32_t TEXTURE_SKY_LEFT = HashStr( "SkyLeft" );
constexpr std::uint32_t TEXTURE_SKY_RIGHT = HashStr( "SkyRight" );
constexpr std::uint32_t TEXTURE_SKY_FRONT = HashStr( "SkyFront" );
constexpr std::uint32_t TEXTURE_SKY_BACK = HashStr( "SkyBack" );
constexpr std::uint32_t TEXTURE_SKY_UP = HashStr( "SkyUp" );
constexpr std::uint32_t TEXTURE_SKY_DOWN = HashStr( "SkyDown" );

// Invariant: these numeric values retain the two legacy generated-object camera
// hashes exactly. The author-facing identities cannot change without migrating
// committed scene data, so the naming cleanup changes only source vocabulary.
constexpr std::uint32_t CAMERA_SCENE_OBJECT_1 = 0x76EECD4Fu;
constexpr std::uint32_t CAMERA_SCENE_OBJECT_2 = 0x77EECEE2u;
constexpr std::uint32_t CAMERA_FREE = HashStr( "Free" );
