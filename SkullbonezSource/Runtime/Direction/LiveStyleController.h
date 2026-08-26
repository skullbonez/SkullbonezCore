/*
File: SkullbonezSource/Runtime/Direction/LiveStyleController.h
Purpose:
  Owns the live style-harness control-folder state.

Summary:
  The live style controller watches a small opt-in folder for `live.style.json`
  and screenshot requests. It publishes parsed style and bounded capture-path
  values, then records the synchronous App receipt for each applied effect.

Glossary:
  Control folder: Directory containing live.style.json, capture.txt, and
    status.txt for the style harness.
  Style stamp: Timestamp/size fingerprint used to avoid rereading unchanged
    control files every frame.
  Pending capture: Screenshot path decoded from capture.txt and consumed after
    the current render pass.

Invariants:
  - File paths are fixed when the controller directory is configured.
  - Style polling is style-only; it must not reload scene physics or replace
    runtime-owned bodies.
  - Pending capture text is bounded and cleared after the controller consumes the save
    request, whether the screenshot succeeds or reports a recoverable failure.

Related:
  - SkullbonezSource/Runtime/Direction/LiveStyleController.cpp
  - SkullbonezSource/Runtime/App/InputFrameExecution.cpp
*/
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace SkullbonezCore
{
namespace Assets
{
class AssetSystem;
}
namespace Core
{
class SbDiagnosticStore;
}
namespace Runtime
{
class AuthoredScene;

inline constexpr std::size_t LIVE_STYLE_DIRECTORY_CAPACITY = 260u;
inline constexpr std::size_t LIVE_STYLE_CONTROL_PATH_CAPACITY = 300u;
inline constexpr std::size_t LIVE_STYLE_SCREENSHOT_PATH_CAPACITY = 512u;

// Invariant: capacity is proved before the first destination byte changes, so
// failed configuration and capture commands preserve the prior live state.
inline bool TryBuildLiveStylePath( const char* directory, const char* leaf, char* output,
                                   std::size_t outputCapacity ) noexcept
{
    if ( !directory || !leaf || !output || outputCapacity == 0u )
    {
        return false;
    }

    const std::size_t directoryLength = std::strlen( directory );
    const std::size_t leafLength = std::strlen( leaf );
    const bool hasSeparator = directoryLength > 0u &&
                              ( directory[directoryLength - 1u] == '\\' || directory[directoryLength - 1u] == '/' );
    const std::size_t separatorLength = directoryLength > 0u && !hasSeparator ? 1u : 0u;

    if ( directoryLength >= outputCapacity || leafLength >= outputCapacity - directoryLength ||
         separatorLength >= outputCapacity - directoryLength - leafLength )
    {
        return false;
    }

    std::memcpy( output, directory, directoryLength );
    std::size_t write = directoryLength;

    if ( separatorLength != 0u )
    {
        output[write++] = '\\';
    }

    std::memcpy( output + write, leaf, leafLength );
    output[write + leafLength] = '\0';
    return true;
}

struct LiveStyleControlPaths
{
    std::array<char, LIVE_STYLE_DIRECTORY_CAPACITY> directory = {};
    std::array<char, LIVE_STYLE_CONTROL_PATH_CAPACITY> style = {};
    std::array<char, LIVE_STYLE_CONTROL_PATH_CAPACITY> capture = {};
    std::array<char, LIVE_STYLE_CONTROL_PATH_CAPACITY> status = {};
    bool valid = false;
};

inline LiveStyleControlPaths ResolveLiveStyleControlPaths( const char* directory ) noexcept
{
    LiveStyleControlPaths result;

    if ( !directory || directory[0] == '\0' )
    {
        return result;
    }

    const std::size_t length = std::strlen( directory );

    if ( length >= result.directory.size() )
    {
        return result;
    }

    std::memcpy( result.directory.data(), directory, length + 1u );
    result.valid = TryBuildLiveStylePath( result.directory.data(), "live.style.json", result.style.data(),
                                          result.style.size() ) &&
                   TryBuildLiveStylePath( result.directory.data(), "capture.txt", result.capture.data(),
                                          result.capture.size() ) &&
                   TryBuildLiveStylePath( result.directory.data(), "status.txt", result.status.data(),
                                          result.status.size() );
    return result;
}

inline constexpr uint64_t ResolveLiveStyleRetainedStamp( uint64_t retainedStamp, uint64_t observedStamp,
                                                          bool operationSucceeded ) noexcept
{
    // A failed read/load remains eligible for retry even if metadata is unchanged.
    return operationSucceeded ? observedStamp : retainedStamp;
}

class LiveStyleController
{
  public:
    bool ConfigureDirectory( const char* path );
    void MarkReady();
    bool Poll( SkullbonezCore::Core::SbDiagnosticStore& resultDiagnostics, const Assets::AssetSystem& assets,
               AuthoredScene& outStyle );
    void MarkStyleApplied();
    bool HasPendingCapture() const;
    const char* PendingScreenshotPath() const;
    void MarkCaptureSaved();
    void MarkCaptureFailed( const char* message );

  private:
    void WriteStatus( const char* status, const char* detail ) const;

    bool m_enabled = false;                 // Polls a small control folder for live style JSON and screenshot requests.
    char m_directory[LIVE_STYLE_DIRECTORY_CAPACITY] = {};             // Folder containing live.style.json, capture.txt, and status.txt.
    char m_stylePath[LIVE_STYLE_CONTROL_PATH_CAPACITY] = {};           // Style descriptor applied without reloading the scene.
    char m_capturePath[LIVE_STYLE_CONTROL_PATH_CAPACITY] = {};         // Text command file used to request one screenshot.
    char m_statusPath[LIVE_STYLE_CONTROL_PATH_CAPACITY] = {};          // Latest harness status for scripts/humans.
    char m_pendingScreenshotPath[LIVE_STYLE_SCREENSHOT_PATH_CAPACITY] = {}; // Screenshot path requested by capture.txt.
    uint64_t m_styleStamp = 0;              // Last successfully loaded live.style.json write stamp.
    uint64_t m_captureStamp = 0;            // Last consumed capture.txt write stamp.
    int m_styleApplyCount = 0;              // Successful live style applications.
    int m_captureCount = 0;                 // Successful live screenshots.
    bool m_hasPendingScreenshot = false;    // Capture should run after render/UI this frame.
};
} // namespace Runtime
} // namespace SkullbonezCore
