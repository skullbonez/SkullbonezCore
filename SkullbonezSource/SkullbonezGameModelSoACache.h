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
class GameModelSoACache
{
  public:
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
