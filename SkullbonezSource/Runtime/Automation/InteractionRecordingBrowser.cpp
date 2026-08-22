/*
File: SkullbonezSource/Runtime/Automation/InteractionRecordingBrowser.cpp
Purpose:
  Discovers complete interaction manifests and launches deterministic playback.

Summary:
  Refresh builds one newest-first detached catalog for the Scene tab. Launch
  validates the selected catalog entry, starts Automation without a shell, and
  places both the final report and incremental turn trace beside the manifest.

Glossary:
  Recordings root: The canonical TestOutput/recordings directory that bounds selectable evidence.

Invariants:
  - Catalog order is descending recording directory name with a path tie-break.
  - A catalog entry must remain beneath the canonical recordings root at launch.
  - Process handles are released immediately; the child owns playback lifetime.

Related:
  - SkullbonezSource/Runtime/Automation/InteractionRecordingBrowser.h
  - SkullbonezSource/UI/UISceneNavigationModel.h
  - Agentic/Reference/engine-glossary.md
  - Agentic/Reference/runtime-reference.md
*/
#include "InteractionRecordingBrowser.h"

#include "../../Core/Log.h"
#include "../../Core/PlatformWin32.h"
#include "../../Core/SbDiagnosticStore.h"
#include "../../UI/UISceneNavigationModel.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace
{
namespace fs = std::filesystem;

bool IsPathInside( const fs::path& root, const fs::path& candidate )
{
    // Invariant: launch authority is limited to a descendant of the canonical
    // recordings root even if a stale UI catalog entry is replaced on disk.
    auto rootPart = root.begin();
    auto candidatePart = candidate.begin();

    for ( ; rootPart != root.end() && candidatePart != candidate.end(); ++rootPart, ++candidatePart )
    {
        if ( *rootPart != *candidatePart )
        {
            return false;
        }
    }

    return rootPart == root.end();
}

void AppendQuotedWindowsArgument( std::wstring& commandLine, const std::wstring& argument )
{
    if ( !commandLine.empty() )
    {
        commandLine.push_back( L' ' );
    }

    // Hazard: CreateProcess receives one mutable command-line string. Double
    // trailing backslashes before the closing quote so paths cannot escape it.
    commandLine.push_back( L'"' );
    std::size_t backslashes = 0u;

    for ( const wchar_t value : argument )
    {
        if ( value == L'\\' )
        {
            ++backslashes;
            continue;
        }

        if ( value == L'"' )
        {
            commandLine.append( backslashes * 2u + 1u, L'\\' );
            commandLine.push_back( L'"' );
            backslashes = 0u;
            continue;
        }

        commandLine.append( backslashes, L'\\' );
        backslashes = 0u;
        commandLine.push_back( value );
    }

    commandLine.append( backslashes * 2u, L'\\' );
    commandLine.push_back( L'"' );
}
} // namespace

void SkullbonezCore::UI::SceneNavigationModel::RefreshInteractionRecordings()
{
    recordings.paths.clear();
    recordings.names.clear();
    recordings.namePtrs.clear();
    recordings.selectedIndex = 0;

    const fs::path root = fs::path( "TestOutput" ) / "recordings";
    std::error_code error;

    if ( !fs::is_directory( root, error ) || error )
    {
        return;
    }

    std::vector<fs::path> manifests;
    fs::directory_iterator iterator( root, error );
    const fs::directory_iterator end;

    while ( !error && iterator != end )
    {
        std::error_code entryError;

        if ( iterator->is_directory( entryError ) && !entryError )
        {
            const fs::path manifest = iterator->path() / "interaction.json";

            if ( fs::is_regular_file( manifest, entryError ) && !entryError )
            {
                manifests.push_back( manifest );
            }
        }

        iterator.increment( error );
    }

    if ( error )
    {
        Core::Log().WriteEventf( "interaction_recording_browser_refresh_failed message=\"%s\"", error.message().c_str() );
        manifests.clear();
    }

    std::sort( manifests.begin(), manifests.end(),
               []( const fs::path& left, const fs::path& right )
               {
                   const std::string leftName = left.parent_path().filename().string();
                   const std::string rightName = right.parent_path().filename().string();
                   return leftName != rightName ? leftName > rightName : left.generic_string() > right.generic_string();
               } );

    recordings.paths.reserve( manifests.size() );
    recordings.names.reserve( manifests.size() );
    recordings.namePtrs.reserve( manifests.size() );

    for ( const fs::path& manifest : manifests )
    {
        recordings.paths.push_back( manifest.generic_string() );
        recordings.names.push_back( manifest.parent_path().filename().string() );
    }

    for ( const std::string& name : recordings.names )
    {
        recordings.namePtrs.push_back( name.c_str() );
    }
}

SkullbonezCore::Core::SbResult
SkullbonezCore::Runtime::LaunchInteractionRecording( Core::SbDiagnosticStore& diagnostics,
                                                     const UI::InteractionRecordingBrowserState& recordings, int index )
{
    if ( index < 0 || index >= static_cast<int>( recordings.paths.size() ) )
    {
        return diagnostics.Failure( "InteractionPlaybackLauncher", "selected recording index is unavailable" );
    }

    std::error_code error;
    const fs::path root = fs::weakly_canonical( fs::path( "TestOutput" ) / "recordings", error );
    const fs::path manifest = fs::weakly_canonical( recordings.paths[static_cast<std::size_t>( index )], error );

    if ( error || !IsPathInside( root, manifest ) || manifest.filename() != "interaction.json" ||
         !fs::is_regular_file( manifest, error ) || error )
    {
        return diagnostics.Failure( "InteractionPlaybackLauncher", "selected recording manifest is invalid" );
    }

    const fs::path executable = fs::absolute( fs::path( "Automation" ) / "SKULLBONEZ_CORE.exe", error );

    if ( error || !fs::is_regular_file( executable, error ) || error )
    {
        return diagnostics.Failure( "InteractionPlaybackLauncher",
                                    "Automation\\SKULLBONEZ_CORE.exe is missing; build Automation|x64 first" );
    }

    const fs::path report = manifest.parent_path() / "playback-report.json";
    const fs::path trace = manifest.parent_path() / "playback-trace.jsonl";
    std::wstring commandLine;
    AppendQuotedWindowsArgument( commandLine, executable.wstring() );
    commandLine.append( L" --interaction-script" );
    AppendQuotedWindowsArgument( commandLine, manifest.wstring() );
    commandLine.append( L" --interaction-report" );
    AppendQuotedWindowsArgument( commandLine, report.wstring() );
    commandLine.append( L" --interaction-trace" );
    AppendQuotedWindowsArgument( commandLine, trace.wstring() );

    std::vector<wchar_t> mutableCommandLine( commandLine.begin(), commandLine.end() );
    mutableCommandLine.push_back( L'\0' );
    STARTUPINFOW startup = {};
    startup.cb = sizeof( startup );
    PROCESS_INFORMATION process = {};

    if ( !CreateProcessW( executable.c_str(), mutableCommandLine.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                          &startup, &process ) )
    {
        return diagnostics.Failure( "InteractionPlaybackLauncher", "CreateProcessW failed with Win32 error %lu",
                                    static_cast<unsigned long>( GetLastError() ) );
    }

    CloseHandle( process.hThread );
    CloseHandle( process.hProcess );
    return Core::SbResult::Success();
}
