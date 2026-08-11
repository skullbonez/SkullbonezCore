/*
File: DemoDirector.cpp
Purpose:
  Loads and saves Demo Director `.shot.json` files.

Summary:
  The director parser is a cold authoring boundary. It validates human-authored
  JSON into a temporary fixed-capacity record before publishing it, while the
  writer preserves the same stable schema.

Glossary:
  Authoring boundary: File I/O path used before or between demo takes, not a
    per-frame simulation loop.

Invariants:
  - Failed loads never modify the caller's existing shot list.
  - JSON phase count is capped by DemoShotList::MAX_PHASES.
  - Phase count must fit DemoShotList::MAX_PHASES before any file is opened.
  - Saved files use the same versioned field names accepted by the loader.

Related:
  - SkullbonezSource/Runtime/Direction/DemoDirector.h
  - SkullbonezSource/Runtime/Tools/RuntimeFileWriter.h
  - Agentic/Reference/engine-glossary.md
*/
#include "DemoDirector.h"
#include "../Tools/RuntimeFileWriter.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

#pragma warning( push, 0 )
#include "../../../ThirdPtySource/nlohmann/json.hpp"
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

bool FailField( const char* path, const std::string& context, const std::string& detail )
{
    LogShotListError( path, context + ": " + detail );
    return false;
}

const Json* FindMember( const Json& object, const char* key )
{
    const auto it = object.find( key );
    return it == object.end() ? nullptr : &*it;
}

bool ReadStringValue( const Json& value, const char* path, const std::string& context, std::string& out )
{
    if ( !value.is_string() )
    {
        return FailField( path, context, "must be a string" );
    }

    out = value.get<std::string>();
    return true;
}

bool ReadFloatValue( const Json& value, const char* path, const std::string& context, float& out )
{
    if ( !value.is_number() )
    {
        return FailField( path, context, "must be a number" );
    }

    out = value.get<float>();
    return true;
}

bool CopyTextField( char* destination, std::size_t destinationSize, const std::string& value, const char* path,
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

bool ReadVec3Value( const Json& value, const char* path, const std::string& context, Vector3& out )
{
    if ( !value.is_array() || value.size() != 3u )
    {
        return FailField( path, context, "must be an array of exactly 3 numbers" );
    }

    return ReadFloatValue( value[0], path, context + "[0]", out.x ) &&
           ReadFloatValue( value[1], path, context + "[1]", out.y ) &&
           ReadFloatValue( value[2], path, context + "[2]", out.z );
}

bool ReadRequiredStringMember( const Json& object, const char* key, const char* path, const std::string& context,
                               std::string& out )
{
    const Json* member = FindMember( object, key );
    return member ? ReadStringValue( *member, path, context + "." + key, out )
                  : FailField( path, context, std::string( "missing '" ) + key + "'" );
}

bool ReadRequiredVec3Member( const Json& object, const char* key, const char* path, const std::string& context,
                             Vector3& out )
{
    const Json* member = FindMember( object, key );
    return member ? ReadVec3Value( *member, path, context + "." + key, out )
                  : FailField( path, context, std::string( "missing '" ) + key + "'" );
}

bool ReadOptionalFloatMember( const Json& object, const char* key, const char* path, const std::string& context,
                              float& inOutValue )
{
    const Json* member = FindMember( object, key );
    return !member || ReadFloatValue( *member, path, context + "." + key, inOutValue );
}

bool ReadPhase( const Json& value, const char* path, int index, DemoPhase& outPhase )
{
    const std::string context = "phases[" + std::to_string( index ) + "]";

    if ( !value.is_object() )
    {
        return FailField( path, context, "must be an object" );
    }

    std::string name;

    if ( !ReadRequiredStringMember( value, "name", path, context, name ) ||
         !CopyTextField( outPhase.name, sizeof( outPhase.name ), name, path, context + ".name" ) )
    {
        return false;
    }

    const Json* camera = FindMember( value, "camera" );

    if ( !camera || !camera->is_object() )
    {
        return FailField( path, context, "missing object 'camera'" );
    }

    if ( !ReadRequiredVec3Member( *camera, "position", path, context + ".camera", outPhase.camera.eye ) ||
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

        if ( !ReadStringValue( *advance, path, context + ".advance", advanceText ) ||
             !TryParsePhaseAdvance( advanceText.c_str(), outPhase.advance ) )
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
    if ( !root.is_object() )
    {
        return FailField( path, "document root", "must be an object" );
    }

    std::string format;

    if ( !ReadRequiredStringMember( root, "format", path, "document root", format ) || format != kShotListFormat )
    {
        return FailField( path, "format", "expected 'skullbonez.shot.json'" );
    }

    const Json* version = FindMember( root, "version" );

    if ( !version || !version->is_number_integer() || version->get<int>() != kShotListVersion )
    {
        return FailField( path, "version", "expected integer 1" );
    }

    const Json* phases = FindMember( root, "phases" );

    if ( !phases || !phases->is_array() )
    {
        return FailField( path, "phases", "must be an array" );
    }

    if ( phases->size() > static_cast<std::size_t>( DemoShotList::MAX_PHASES ) )
    {
        return FailField( path, "phases", "too many phases for fixed shot-list storage" );
    }

    // Invariant: publish only after every field is valid so a bad authoring
    // document cannot partly overwrite the active Director sequence.
    DemoShotList parsed;

    if ( const Json* loop = FindMember( root, "loop" ) )
    {
        if ( loop->is_boolean() )
        {
            parsed.loop = loop->get<bool>();
        }
        else if ( loop->is_number_integer() )
        {
            parsed.loop = loop->get<int>() != 0;
        }
        else
        {
            return FailField( path, "loop", "must be a bool" );
        }
    }

    for ( std::size_t index = 0; index < phases->size(); ++index )
    {
        if ( !ReadPhase( ( *phases )[index], path, static_cast<int>( index ), parsed.phases[index] ) )
        {
            return false;
        }

        parsed.phaseCount = static_cast<int>( index ) + 1;
    }

    outShotList = parsed;
    return true;
}

Json Vec3Json( const Vector3& value )
{
    return Json::array( { value.x, value.y, value.z } );
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
