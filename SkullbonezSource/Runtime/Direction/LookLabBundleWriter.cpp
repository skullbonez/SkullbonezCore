/*
File: SkullbonezSource/Runtime/Direction/LookLabBundleWriter.cpp
Purpose:
  Creates collision-safe Look Lab directories and exact derived receipts.

Summary:
  The writer validates timestamp and receipt text before reserving one bundle
  directory, then combines fixed transaction facts with Scene's flattened style
  listing for atomic receipt publication.

Glossary:
  Exclusive directory creation: create_directory succeeds only when this call
    owns a previously absent final bundle path.
  Partial success: Style remains reusable when later screenshot capture fails;
    the receipt reports both states independently.

Invariants:
  - Bundle creation never deletes, merges into, or overwrites an existing path.
  - Receipt output filenames are the fixed three bundle leaf names.
  - Calendar-invalid timestamps and newline-bearing metadata are rejected before
    filesystem or receipt publication.

Related:
  - SkullbonezSource/Runtime/Direction/LookLabBundleWriter.h
  - SkullbonezSource/Scene/StandaloneStyleWriter.cpp
  - SkullbonezSource/Core/AtomicTextFileWriter.cpp
*/
#include "LookLabBundleWriter.h"
#include "../../Core/AtomicTextFileWriter.h"
#include "../../Core/SbDiagnosticStore.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <sstream>

namespace SkullbonezCore::Runtime
{
namespace
{
constexpr const char* OWNER = "Runtime/Direction/LookLabBundleWriter";

} // namespace

bool IsValidLookLabLocalTimestamp( const char* timestamp ) noexcept
{
    if ( !timestamp || std::strlen( timestamp ) != 19 )
    {
        return false;
    }

    constexpr int separators[] = { 4, 7, 10, 13, 16 };
    constexpr char expected[] = { '-', '-', '_', '-', '-' };

    for ( int index = 0; index < 19; ++index )
    {
        bool separator = false;

        for ( size_t separatorIndex = 0; separatorIndex < sizeof( separators ) / sizeof( separators[0] );
              ++separatorIndex )
        {
            if ( index == separators[separatorIndex] )
            {
                separator = timestamp[index] == expected[separatorIndex];

                if ( !separator )
                {
                    return false;
                }

                break;
            }
        }

        if ( !separator && ( timestamp[index] < '0' || timestamp[index] > '9' ) )
        {
            return false;
        }
    }

    const auto pair = [timestamp]( int index ) { return ( timestamp[index] - '0' ) * 10 + timestamp[index + 1] - '0'; };
    const int year = ( timestamp[0] - '0' ) * 1000 + ( timestamp[1] - '0' ) * 100 +
                     ( timestamp[2] - '0' ) * 10 + timestamp[3] - '0';
    const int month = pair( 5 );
    const int day = pair( 8 );
    const int hour = pair( 11 );
    const int minute = pair( 14 );
    const int second = pair( 17 );
    if ( year < 1 || month < 1 || month > 12 || hour > 23 || minute > 59 || second > 59 )
    {
        return false;
    }

    constexpr int daysPerMonth[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    int maximumDay = daysPerMonth[month - 1];
    const bool leapYear = year % 400 == 0 || ( year % 4 == 0 && year % 100 != 0 );

    if ( month == 2 && leapYear )
    {
        maximumDay = 29;
    }

    return day >= 1 && day <= maximumDay;
}

namespace
{

template <size_t Capacity> bool TextValid( const std::array<char, Capacity>& text, bool allowEmpty = true )
{
    const char* terminator = static_cast<const char*>( std::memchr( text.data(), '\0', text.size() ) );

    if ( !terminator || ( !allowEmpty && text[0] == '\0' ) )
    {
        return false;
    }

    for ( const char* cursor = text.data(); cursor != terminator; ++cursor )
    {
        if ( *cursor == '\r' || *cursor == '\n' )
        {
            return false;
        }
    }

    return true;
}

template <size_t Capacity> bool CopyPath( const std::filesystem::path& path, std::array<char, Capacity>& output )
{
    const std::string text = path.generic_string();

    if ( text.size() >= output.size() )
    {
        return false;
    }

    std::memcpy( output.data(), text.c_str(), text.size() + 1 );
    return true;
}

const char* StatusName( LookLabArtifactStatus status )
{
    switch ( status )
    {
    case LookLabArtifactStatus::Pending:
        return "pending";
    case LookLabArtifactStatus::Saved:
        return "saved";
    case LookLabArtifactStatus::Failed:
        return "failed";
    case LookLabArtifactStatus::Cancelled:
        return "cancelled";
    default:
        return "invalid";
    }
}

std::string OffsetText( int minutes )
{
    const char sign = minutes < 0 ? '-' : '+';
    const int magnitude = minutes < 0 ? -minutes : minutes;
    char text[8] = {};
    std::snprintf( text, sizeof( text ), "%c%02d:%02d", sign, magnitude / 60, magnitude % 60 );
    return text;
}
} // namespace

Core::SbResult LookLabBundleWriter::ResolveBundlePaths( Core::SbDiagnosticStore& diagnostics, const char* lookLabRoot,
                                                        const char* localTimestamp, uint64_t seed,
                                                        LookLabBundlePaths& output )
{
    if ( !lookLabRoot || lookLabRoot[0] == '\0' || !IsValidLookLabLocalTimestamp( localTimestamp ) )
    {
        return diagnostics.Failure( OWNER, "Look Lab root or local timestamp is invalid." );
    }

    std::ostringstream directoryName;
    directoryName << localTimestamp << "_seed_" << std::hex << std::nouppercase << std::setw( 16 ) << std::setfill( '0' )
                  << seed;
    const std::filesystem::path root( lookLabRoot );
    const std::filesystem::path directory = root / directoryName.str();
    LookLabBundlePaths resolved;
    const bool pathsFit = CopyPath( directory, resolved.directory ) &&
                          CopyPath( directory / "look.style.json", resolved.style ) &&
                          CopyPath( directory / "look.txt", resolved.receipt ) &&
                          CopyPath( directory / "look.png", resolved.screenshot );

    if ( !pathsFit )
    {
        return diagnostics.Failure( OWNER, "Look Lab bundle path exceeds the bounded path capacity." );
    }

    output = resolved;
    return Core::SbResult::Success();
}

Core::SbResult LookLabBundleWriter::CreateBundleDirectory( Core::SbDiagnosticStore& diagnostics, const char* lookLabRoot,
                                                           const char* localTimestamp, uint64_t seed,
                                                           LookLabBundlePaths& output )
{
    LookLabBundlePaths resolved;
    Core::SbResult resolveResult = ResolveBundlePaths( diagnostics, lookLabRoot, localTimestamp, seed, resolved );

    if ( !resolveResult.Ok() )
    {
        return resolveResult;
    }

    const std::filesystem::path root( lookLabRoot );
    const std::filesystem::path directory( resolved.directory.data() );
    std::error_code filesystemError;
    std::filesystem::create_directories( root, filesystemError );

    if ( filesystemError )
    {
        return diagnostics.Failure( OWNER, "Failed to create Look Lab root '%s' (error=%d).", lookLabRoot,
                                    filesystemError.value() );
    }

    const bool created = std::filesystem::create_directory( directory, filesystemError );

    if ( filesystemError || !created )
    {
        return diagnostics.Failure( OWNER, "Look Lab bundle path collision or creation failure '%s' (error=%d).",
                                    directory.generic_string().c_str(), filesystemError.value() );
    }

    output = resolved;
    return Core::SbResult::Success();
}

Core::SbResult LookLabBundleWriter::BuildReceipt( Core::SbDiagnosticStore& diagnostics, const LookLabReceiptFacts& facts,
                                                  const Scene::StandaloneStyleSnapshot& snapshot,
                                                  const LookLabBundlePaths& paths, std::string& output )
{
    if ( !TextValid( facts.localTimestamp, false ) || !IsValidLookLabLocalTimestamp( facts.localTimestamp.data() ) ||
         facts.utcOffsetMinutes < -14 * 60 || facts.utcOffsetMinutes > 14 * 60 || !TextValid( facts.sourceScenePath ) ||
         !TextValid( facts.sourceSceneDisplayName ) || !TextValid( facts.styleDiagnostic ) ||
         !TextValid( facts.screenshotDiagnostic ) || !TextValid( paths.style, false ) ||
         !TextValid( paths.receipt, false ) || !TextValid( paths.screenshot, false ) || facts.generatorVersion == 0 ||
         facts.recipe >= LookLabRecipeFamily::Count || facts.styleStatus > LookLabArtifactStatus::Cancelled ||
         facts.screenshotStatus > LookLabArtifactStatus::Cancelled )
    {
        return diagnostics.Failure( OWNER, "Look Lab receipt facts contain invalid or unbounded text." );
    }

    std::string settings;
    Core::SbResult listingResult = Scene::StandaloneStyleWriter::BuildFlattenedListing( diagnostics, snapshot, settings );

    if ( !listingResult.Ok() )
    {
        return listingResult;
    }

    std::ostringstream receipt;
    receipt << "look_lab_receipt_version=1\n";
    receipt << "local_timestamp=" << facts.localTimestamp.data() << '\n';
    receipt << "utc_offset=" << OffsetText( facts.utcOffsetMinutes ) << '\n';
    receipt << "seed=" << std::hex << std::nouppercase << std::setw( 16 ) << std::setfill( '0' ) << facts.seed << std::dec
            << '\n';
    receipt << "generator_version=" << facts.generatorVersion << '\n';
    receipt << "recipe=" << LookLabRecipeFamilyName( facts.recipe ) << '\n';
    receipt << "source_scene_path=" << facts.sourceScenePath.data() << '\n';
    receipt << "source_scene_display_name=" << facts.sourceSceneDisplayName.data() << '\n';
    receipt << "style_file=look.style.json\n";
    receipt << "receipt_file=look.txt\n";
    receipt << "screenshot_file=look.png\n";
    receipt << "style_status=" << StatusName( facts.styleStatus ) << '\n';
    receipt << "style_diagnostic=" << facts.styleDiagnostic.data() << '\n';
    receipt << "screenshot_status=" << StatusName( facts.screenshotStatus ) << '\n';
    receipt << "screenshot_diagnostic=" << facts.screenshotDiagnostic.data() << '\n';
    receipt << "settings_begin\n" << settings << "settings_end\n";
    output = receipt.str();
    return Core::SbResult::Success();
}

Core::SbResult LookLabBundleWriter::SaveReceiptAtomic( Core::SbDiagnosticStore& diagnostics,
                                                       const LookLabReceiptFacts& facts,
                                                       const Scene::StandaloneStyleSnapshot& snapshot,
                                                       const LookLabBundlePaths& paths )
{
    std::string receipt;
    Core::SbResult result = BuildReceipt( diagnostics, facts, snapshot, paths, receipt );

    if ( !result.Ok() )
    {
        return result;
    }

    return Core::WriteTextFileAtomic( diagnostics, OWNER, paths.receipt.data(), receipt );
}
} // namespace SkullbonezCore::Runtime
