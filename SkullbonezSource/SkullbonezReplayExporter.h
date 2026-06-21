/*
File: SkullbonezSource/SkullbonezReplayExporter.h
Purpose:
  Serializes retained replay samples into a replay artifact file.
*/
#pragma once

#include "SkullbonezReplayRecorder.h"

namespace SkullbonezCore
{
namespace Basics
{
class ReplayExporter
{
  public:
    static bool Save( const ReplayRecorder& recorder, const char* path );
};
} // namespace Basics
} // namespace SkullbonezCore
