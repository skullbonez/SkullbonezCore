/*
File: DemoDirector.cpp
Purpose:
  Loads and saves Demo Director `.shot.json` files.

Summary:
  The director parser is a cold authoring boundary. It converts human-authored
  JSON into fixed-capacity runtime records, logs the first invalid field it
  finds, and leaves playback state untouched on failure.

Glossary:
  Authoring boundary: File I/O path used before or between demo takes, not a
    per-frame simulation loop.
  Round-trip: Loading a shot list, saving it, and loading the saved copy with
    the same phase data.
  Lane R: Repository error-handling lane for recoverable runtime/file input:
    return bool status and log a path-rich reason instead of throwing.

Invariants:
  - Failed loads never modify the caller's existing shot list.
  - JSON phase count is capped by DemoShotList::MAX_PHASES.
  - Saved files use the same schema accepted by the loader.

Related:
  - SkullbonezSource/Runtime/DemoDirector.h
  - SkullbonezSource/Runtime/RuntimeFileWriter.h
  - SkullbonezTests/TestDemoDirector.cpp
*/
#include "DemoDirector.h"
#include "RuntimeFileWriter.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

#pragma warning( push, 0 )
#include "../../ThirdPtySource/nlohmann/json.hpp"
#pragma warning( pop )

using SkullbonezCore::Math::Vector::Vector3;

namespace SkullbonezCore
{
namespace Runtime
{
namespace
{
using Json = nlohmann::ordered_json;

constexpr const char* kShotListFormat = "skullbonez.shot.json";
constexpr int kShotListVersion = 1;

void LogShotListError( const char* path, const std::string& detail )
{
    printf( "[demo-director] %s: %s\n", path && path[0] ? path : "<null-path>", detail.c_str() );
}

std::string JsonTypeName( const Json& value )
{
    if ( value.is_null() )
    {
        return "null";
    }
    if ( value.is_boolean() )
    {
        return "bool";
    }
    if ( value.is_number() )
    {
        return "number";
    }
    if ( value.is_string() )
    {
        return "string";
    }
    if ( value.is_array() )
    {
        return "array";
    }
    if ( value.is_object() )
    {
        return "object";
    }
    return "value";
}

bool FailField( const char* path, const std::string& context, const std::string& detail )
{
    std::ostringstream message;
    message << context << ": " << detail;
    LogShotListError( path, message.str() );
    return false;
}

const Json* FindMember( const Json& object, const char* key )
{
    const auto it = object.find( key );
    return it == object.end() ? nullptr : &*it;
}

bool RequireObject( const Json& value, const char* path, const std::string& context )
{
    if ( value.is_object() )
    {
        return true;
    }
    std::ostringstream detail;
    detail << "must be an object, got " << JsonTypeName( value );
    return FailField( path, context, detail.str() );
}

bool RequireArray( const Json& value, const char* path, const std::string& context )
{
    if ( value.is_array() )
    {
        return true;
    }
    std::ostringstream detail;
    detail << "must be an array, got " << JsonTypeName( value );
    return FailField( path, context, detail.str() );
}

bool ReadStringValue( const Json& value, const char* path, const std::string& context, std::string& out )
{
    if ( !value.is_string() )
    {
        std::ostringstream detail;
        detail << "must be a string, got " << JsonTypeName( value );
        return FailField( path, context, detail.str() );
    }
    out = value.get<std::string>();
    return true;
}

bool ReadFloatValue( const Json& value, const char* path, const std::string& context, float& out )
{
    if ( !value.is_number() )
    {
        std::ostringstream detail;
        detail << "must be a number, got " << JsonTypeName( value );
        return FailField( path, context, detail.str() );
    }
    out = value.get<float>();
    return true;
}

bool ReadBoolValue( const Json& value, const char* path, const std::string& context, bool& out )
{
    if ( value.is_boolean() )
    {
        out = value.get<bool>();
        return true;
    }
    if ( value.is_number_integer() )
    {
        out = value.get<int>() != 0;
        return true;
    }
    return FailField( path, context, "must be a bool" );
}

bool CopyTextField( char* destination,
                    std::size_t destinationSize,
                    const std::string& value,
                    const char* path,
                    const std::string& context )
{
    if ( value.size() >= destinationSize )
    {
        std::ostringstream detail;
        detail << "string is too long for fixed field (" << value.size() << " >= " << destinationSize << ")";
        return FailField( path, context, detail.str() );
    }
    strcpy_s( destination, destinationSize, value.c_str() );
    return true;
}

bool ReadOptionalFloatMember( const Json& object,
                              const char* key,
                              const char* path,
                              const std::string& context,
                              float& inOutValue )
{
    if ( const Json* member = FindMember( object, key ) )
    {
        return ReadFloatValue( *member, path, context + "." + key, inOutValue );
    }
    return true;
}

Json Vec3Json( const Vector3& value )
{
    return Json::array( { value.x, value.y, value.z } );
}

bool ReadVec3Value( const Json& value, const char* path, const std::string& context, Vector3& out )
{
    if ( !RequireArray( value, path, context ) )
    {
        return false;
    }
    if ( value.size() != 3u )
    {
        return FailField( path, context, "must have exactly 3 numbers" );
    }

    return ReadFloatValue( value[0], path, context + "[0]", out.x ) &&
           ReadFloatValue( value[1], path, context + "[1]", out.y ) &&
           ReadFloatValue( value[2], path, context + "[2]", out.z );
}

bool ReadRequiredStringMember( const Json& object,
                               const char* key,
                               const char* path,
                               const std::string& context,
                               std::string& out )
{
    const Json* member = FindMember( object, key );
    if ( !member )
    {
        return FailField( path, context, std::string( "missing '" ) + key + "'" );
    }
    return ReadStringValue( *member, path, context + "." + key, out );
}

bool ReadRequiredVec3Member( const Json& object,
                             const char* key,
                             const char* path,
                             const std::string& context,
                             Vector3& out )
{
    const Json* member = FindMember( object, key );
    if ( !member )
    {
        return FailField( path, context, std::string( "missing '" ) + key + "'" );
    }
    return ReadVec3Value( *member, path, context + "." + key, out );
}

bool ReadPhase( const Json& value, const char* path, int index, DemoPhase& outPhase )
{
    std::ostringstream contextStream;
    contextStream << "phases[" << index << "]";
    const std::string context = contextStream.str();

    if ( !RequireObject( value, path, context ) )
    {
        return false;
    }

    std::string name;
    if ( !ReadRequiredStringMember( value, "name", path, context, name ) ||
         !CopyTextField( outPhase.name, sizeof( outPhase.name ), name, path, context + ".name" ) )
    {
        return false;
    }

    const Json* camera = FindMember( value, "camera" );
    if ( !camera )
    {
        return FailField( path, context, "missing 'camera'" );
    }
    if ( !RequireObject( *camera, path, context + ".camera" ) ||
         !ReadRequiredVec3Member( *camera, "position", path, context + ".camera", outPhase.camera.eye ) ||
         !ReadRequiredVec3Member( *camera, "view", path, context + ".camera", outPhase.camera.view ) ||
         !ReadRequiredVec3Member( *camera, "up", path, context + ".camera", outPhase.camera.up ) )
    {
        return false;
    }

    if ( const Json* stylePath = FindMember( value, "stylePath" ) )
    {
        std::string style;
        if ( !ReadStringValue( *stylePath, path, context + ".stylePath", style ) ||
             !CopyTextField( outPhase.stylePath, sizeof( outPhase.stylePath ), style, path, context + ".stylePath" ) )
        {
            return false;
        }
    }

    if ( const Json* advance = FindMember( value, "advance" ) )
    {
        std::string advanceText;
        if ( !ReadStringValue( *advance, path, context + ".advance", advanceText ) )
        {
            return false;
        }
        if ( !TryParsePhaseAdvance( advanceText.c_str(), outPhase.advance ) )
        {
            return FailField( path, context + ".advance", "unknown advance rule" );
        }
    }

    return ReadOptionalFloatMember( value, "timerSeconds", path, context, outPhase.timerSeconds ) &&
           ReadOptionalFloatMember( value, "revealThreshold", path, context, outPhase.revealThreshold ) &&
           ReadOptionalFloatMember( value, "blendInSeconds", path, context, outPhase.blendInSeconds ) &&
           ReadOptionalFloatMember( value, "revealRate", path, context, outPhase.revealRate );
}

bool ReadRoot( const Json& root, const char* path, DemoShotList& outShotList )
{
    if ( !RequireObject( root, path, "document root" ) )
    {
        return false;
    }

    std::string format;
    if ( !ReadRequiredStringMember( root, "format", path, "document root", format ) )
    {
        return false;
    }
    if ( format != kShotListFormat )
    {
        return FailField( path, "format", "expected 'skullbonez.shot.json'" );
    }

    const Json* versionMember = FindMember( root, "version" );
    if ( !versionMember )
    {
        return FailField( path, "version", "missing version" );
    }
    float version = 0.0f;
    if ( !ReadFloatValue( *versionMember, path, "version", version ) )
    {
        return false;
    }
    if ( version != static_cast<float>( kShotListVersion ) )
    {
        return FailField( path, "version", "expected 1" );
    }

    // Invariant: fill a temporary record first so a failed load leaves the
    // caller's currently active shot list untouched.
    DemoShotList parsed;
    if ( const Json* loop = FindMember( root, "loop" ) )
    {
        if ( !ReadBoolValue( *loop, path, "loop", parsed.loop ) )
        {
            return false;
        }
    }

    const Json* phases = FindMember( root, "phases" );
    if ( !phases )
    {
        return FailField( path, "document root", "missing 'phases'" );
    }
    if ( !RequireArray( *phases, path, "phases" ) )
    {
        return false;
    }
    if ( phases->size() > static_cast<std::size_t>( DemoShotList::MAX_PHASES ) )
    {
        return FailField( path, "phases", "too many phases for fixed shot-list storage" );
    }

    for ( std::size_t i = 0; i < phases->size(); ++i )
    {
        const int phaseIndex = static_cast<int>( i );
        if ( !ReadPhase( ( *phases )[i], path, phaseIndex, parsed.phases[static_cast<std::size_t>( phaseIndex )] ) )
        {
            return false;
        }
        parsed.phaseCount = phaseIndex + 1;
    }

    outShotList = parsed;
    return true;
}

Json PhaseJson( const DemoPhase& phase )
{
    Json value;
    value["name"] = phase.name;
    value["camera"] = {
        { "position", Vec3Json( phase.camera.eye ) },
        { "view", Vec3Json( phase.camera.view ) },
        { "up", Vec3Json( phase.camera.up ) },
    };
    value["stylePath"] = phase.stylePath;
    value["advance"] = PhaseAdvanceName( phase.advance );
    value["timerSeconds"] = phase.timerSeconds;
    value["revealThreshold"] = phase.revealThreshold;
    value["blendInSeconds"] = phase.blendInSeconds;
    value["revealRate"] = phase.revealRate;
    return value;
}
} // namespace


const char* PhaseAdvanceName( PhaseAdvance advance )
{
    switch ( advance )
    {
    case PhaseAdvance::Manual:
        return "Manual";
    case PhaseAdvance::Timer:
        return "Timer";
    case PhaseAdvance::RevealAtLeast:
        return "RevealAtLeast";
    }

    return "Manual";
}

bool TryParsePhaseAdvance( const char* text, PhaseAdvance& outAdvance )
{
    if ( !text )
    {
        return false;
    }
    if ( strcmp( text, "Manual" ) == 0 || strcmp( text, "manual" ) == 0 )
    {
        outAdvance = PhaseAdvance::Manual;
        return true;
    }
    if ( strcmp( text, "Timer" ) == 0 || strcmp( text, "timer" ) == 0 )
    {
        outAdvance = PhaseAdvance::Timer;
        return true;
    }
    if ( strcmp( text, "RevealAtLeast" ) == 0 || strcmp( text, "revealAtLeast" ) == 0 ||
         strcmp( text, "reveal_at_least" ) == 0 )
    {
        outAdvance = PhaseAdvance::RevealAtLeast;
        return true;
    }
    return false;
}

bool LoadDemoShotList( const char* path, DemoShotList& outShotList )
{
    if ( !path || path[0] == '\0' )
    {
        LogShotListError( path, "missing shot-list path" );
        return false;
    }

    std::ifstream input( path );
    if ( !input.is_open() )
    {
        LogShotListError( path, "failed to open shot-list file" );
        return false;
    }

    Json root = Json::parse( input, nullptr, false );
    if ( root.is_discarded() )
    {
        LogShotListError( path, "invalid JSON" );
        return false;
    }
    return ReadRoot( root, path, outShotList );
}

bool SaveDemoShotList( const char* path, const DemoShotList& shotList )
{
    if ( !path || path[0] == '\0' )
    {
        LogShotListError( path, "missing shot-list path" );
        return false;
    }
    if ( shotList.phaseCount < 0 || shotList.phaseCount > DemoShotList::MAX_PHASES )
    {
        LogShotListError( path, "phaseCount is outside DemoShotList::MAX_PHASES" );
        return false;
    }

    std::ofstream output;
    if ( !RuntimeFileWriter::OpenTextFile( path, output ) )
    {
        LogShotListError( path, "failed to open output file" );
        return false;
    }

    Json root;
    root["format"] = kShotListFormat;
    root["version"] = kShotListVersion;
    root["loop"] = shotList.loop;
    root["phases"] = Json::array();
    for ( int i = 0; i < shotList.phaseCount; ++i )
    {
        root["phases"].push_back( PhaseJson( shotList.phases[static_cast<std::size_t>( i )] ) );
    }

    output << root.dump( 2 ) << '\n';
    return output.good();
}

} // namespace Runtime
} // namespace SkullbonezCore
