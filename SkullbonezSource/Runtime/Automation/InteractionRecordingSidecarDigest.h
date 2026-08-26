/*
File: SkullbonezSource/Runtime/Automation/InteractionRecordingSidecarDigest.h
Purpose:
  Declares SHA-256 admission for immutable interaction-recording sidecars.

Summary:
  Startup and the Automation loader authenticate the exact path selected from
  their already-parsed manifest metadata through one byte-digest operation.

Invariants:
  - The caller supplies the exact resolved path it intends to consume.
  - A missing, partially read, or mismatched sidecar never authenticates.

Related:
  - SkullbonezSource/Runtime/Automation/InteractionRecordingSidecarDigest.cpp
  - SkullbonezSource/Runtime/App/StartupLaunchApplication.cpp
*/
#pragma once

#include <filesystem>
#include <string_view>

namespace SkullbonezCore::Runtime
{
bool InteractionRecordingSidecarDigestMatches( const std::filesystem::path& path,
                                                std::string_view expectedSha256 );
}
