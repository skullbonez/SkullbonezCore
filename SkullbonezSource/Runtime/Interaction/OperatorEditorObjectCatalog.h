/*
File: SkullbonezSource/Runtime/Interaction/OperatorEditorObjectCatalog.h
Purpose:
  Defines the stable object identifiers shared by editor commands and views.

Summary:
  Interaction owns the object vocabulary consumed by placement, automation,
  and product presenters. Labels are immutable presentation values; placement
  policy remains with Runtime/Editor.

Invariants:
  - Identifier order is serialized command behavior and must remain stable.
  - The label table has exactly one entry for every identifier.

Related:
  - SkullbonezSource/Runtime/Interaction/OperatorUiCommands.h
  - SkullbonezSource/Runtime/Editor/EditorTools.h
*/
#pragma once

#include <algorithm>

namespace SkullbonezCore::UI::EditorTab
{
constexpr int OBJECT_BOX = 0;
constexpr int OBJECT_BALL = 1;
constexpr int OBJECT_SPHERE = 2;
constexpr int OBJECT_HULL_WEDGE = 3;
constexpr int OBJECT_HULL_TRI_PRISM = 4;
constexpr int OBJECT_HULL_TAPERED_BLOCK = 5;
constexpr int OBJECT_HULL_PYRAMID = 6;
constexpr int OBJECT_HULL_HEX_PRISM = 7;
constexpr int OBJECT_HULL_DIAMOND = 8;
constexpr int OBJECT_ROCK_SLAB = 9;
constexpr int OBJECT_ROCK_LUMP = 10;
constexpr int OBJECT_ROCK_SHARD = 11;
constexpr int OBJECT_ROCK_CHIPPED = 12;
constexpr int OBJECT_ROOT_SMALL = 13;
constexpr int OBJECT_ROOT_LARGE = 14;
constexpr int OBJECT_TREE_SMALL = 15;
constexpr int OBJECT_TREE_BIG = 16;
constexpr int OBJECT_TREE_CEDAR = 17;
constexpr int OBJECT_TREE_SMALL_SLOPE = 18;
constexpr int OBJECT_TREE_BIG_SLOPE = 19;
constexpr int OBJECT_TREE_CEDAR_SLOPE = 20;
constexpr int OBJECT_TREE_SMALL_SLEEP = 21;
constexpr int OBJECT_TREE_BIG_SLEEP = 22;
constexpr int OBJECT_TREE_CEDAR_SLEEP = 23;
constexpr int OBJECT_TREE_SMALL_ROOTED = 24;
constexpr int OBJECT_TREE_BIG_ROOTED = 25;
constexpr int OBJECT_TREE_CEDAR_ROOTED = 26;
constexpr int OBJECT_TREE_PINE_SHEDDING = 27;
constexpr int OBJECT_RAGDOLL = 28;
constexpr int OBJECT_RAGDOLL_SLEEP = 29;
constexpr int OBJECT_BRICK_HOUSE_SLEEP = 30;
constexpr int OBJECT_BRICK_HOUSE_HIGH_SLEEP = 31;
constexpr int OBJECT_CUTE_HOUSE_SLEEP = 32;
constexpr int OBJECT_CUTE_HOUSE_HIGH_SLEEP = 33;
constexpr int OBJECT_TRIPLE_DECKER_SLEEP = 34;
constexpr int OBJECT_TRIPLE_DECKER_HIGH_SLEEP = 35;
constexpr int OBJECT_BRICK_WALL_200_SLEEP = 36;
constexpr int OBJECT_TYPE_COUNT = 37;

inline constexpr const char* OBJECT_LABELS[OBJECT_TYPE_COUNT] = {
        "Box", "Ball", "Sphere", "Hull wedge", "Hull tri prism", "Hull tapered", "Hull pyramid", "Hull hex prism",
        "Hull diamond", "Rock slab", "Rock lump", "Rock shard", "Rock chipped", "Root small", "Root large",
        "Tree small", "Tree pine", "Tree cedar", "Tree small slope", "Tree pine slope", "Tree cedar slope",
        "Tree small sleep", "Tree pine sleep", "Tree cedar sleep", "Tree small rooted", "Tree pine rooted",
        "Tree cedar rooted", "Pine shedding", "Ragdoll", "Ragdoll sleep", "Brick house low", "Brick house high",
        "Cute house low", "Cute house high", "Triple decker low", "Triple decker high", "Brick wall 200",
};

inline const char* ObjectLabel( int objectType )
{
    return OBJECT_LABELS[std::clamp( objectType, 0, OBJECT_TYPE_COUNT - 1 )];
}
} // namespace SkullbonezCore::UI::EditorTab
