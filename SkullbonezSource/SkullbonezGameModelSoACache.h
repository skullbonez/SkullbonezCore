/*
File: SkullbonezSource/SkullbonezGameModelSoACache.h
Purpose:
  Caches model state in structure-of-arrays form for render and physics hot paths.

Mental model:
  Runtime code connects authored scene data, input, simulation, render
  backends, and validation-oriented launch modes. Follow who owns state and
  when that state changes.

Glossary:
  SoA (Structure of Arrays): Cache layout that stores each field in its own
  contiguous array for faster iteration.
  Validation gate: Repository script that proves a class of changes before
  commit or PR.

Related:
  - SkullbonezSource/SkullbonezGameModelSoACache.cpp
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "SkullbonezCommon.h"
#include "SkullbonezGameModel.h"
#include "SkullbonezMatrix4.h"
#include "SkullbonezVector3.h"

namespace SkullbonezCore
{
namespace GameObjects
{
// Borrowed views over GameModelSoACache arrays. They make physics/render call
// sites explicit about the fields they read, but they are not independent
// physics or render storage. GameModelCollection still owns the authoritative
// GameModel vector and invalidates this cache after mutations.
struct GameModelBodyStream
{
    const Math::Vector::Vector3* positions = nullptr;
    const float* boundingRadii = nullptr;
    const uint8_t* isBox = nullptr;
    const uint8_t* isFixed = nullptr;
    int count = 0;

    bool Empty() const
    {
        return count <= 0;
    }
};

struct GameModelRenderStream
{
    const uint8_t* isBox = nullptr;
    const uint8_t* isFixed = nullptr;
    const Math::Transformation::Matrix4* modelMatrices = nullptr;
    int count = 0;

    bool Empty() const
    {
        return count <= 0;
    }
};

class GameModelSoACache
{
  public:
    // Concept: this cache mirrors selected GameModel fields in hot-loop order.
    //
    // GameModel is convenient for ownership, but tight render/physics loops
    // repeatedly need "all positions", "all radii", or "all fixed flags".
    // Structure-of-arrays storage keeps those fields contiguous and cheap to
    // scan. GameModel remains authoritative; this cache is disposable derived
    // data and must be invalidated after mutations.
    std::array<Math::Vector::Vector3, MAX_GAME_MODELS> positions;
    std::array<float, MAX_GAME_MODELS> boundingRadii;
    std::array<uint8_t, MAX_GAME_MODELS> isBox;
    std::array<uint8_t, MAX_GAME_MODELS> isFixed;
    std::array<Math::Transformation::Matrix4, MAX_GAME_MODELS> modelMatrices;
    int activeCount = 0;
    bool bodyDataValid = false;
    bool modelMatricesValid = false;

    void Clear();
    void Invalidate();
    void RefreshBodyData( std::vector<GameModel>& models );
    void EnsureModelMatrices( std::vector<GameModel>& models );
};
} // namespace GameObjects
} // namespace SkullbonezCore
