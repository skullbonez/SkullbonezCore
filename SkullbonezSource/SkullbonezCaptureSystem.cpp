/*
File: SkullbonezSource/SkullbonezCaptureSystem.cpp
Purpose:
  Captures frame output to screenshots for scenes, validation, and look-dev.

Mental model:
  Renderer-facing code translates engine concepts into backend resources, draw
  calls, shader bindings, and validation artifacts.

Glossary:
  Descriptor: Small binding record that tells a renderer how to interpret a
  resource.
  Back buffer: Swap-chain image that will be presented to the window.

Related:
  - SkullbonezSource/SkullbonezCaptureSystem.h
  - Agentic/Reference/comment-style-guide.md
*/
#include "SkullbonezCaptureSystem.h"

#include "SkullbonezIRenderBackend.h"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <vector>

namespace SkullbonezCore
{
namespace Basics
{
namespace
{
struct FileCloser
{
    void operator()( FILE* file ) const
    {
        if ( file )
        {
            fclose( file );
        }
    }
};

using FileHandle = std::unique_ptr<FILE, FileCloser>;

void WriteExact( FILE* file, const void* data, size_t size, const char* path )
{
    if ( size == 0 )
    {
        return;
    }

    const size_t written = fwrite( data, 1, size, file );
    if ( written != size )
    {
        char msg[512];
        sprintf_s( msg, sizeof( msg ), "Failed to write screenshot file: %s  (CaptureSystem::SaveBackbufferBmp)", path );
        throw std::runtime_error( msg );
    }
}
} // namespace

void CaptureSystem::SaveBackbufferBmp( Rendering::IRenderBackend& backend, const char* path )
{
    if ( !backend.GetCapabilities().supportsBackbufferCapture )
    {
        char msg[512];
        sprintf_s( msg, sizeof( msg ), "Renderer does not support backbuffer capture for file: %s  (CaptureSystem::SaveBackbufferBmp)", path );
        throw std::runtime_error( msg );
    }

    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels = backend.CaptureBackbuffer( width, height );
    if ( width <= 0 || height <= 0 )
    {
        char msg[512];
        sprintf_s( msg, sizeof( msg ), "Invalid screenshot dimensions for file: %s  (CaptureSystem::SaveBackbufferBmp)", path );
        throw std::runtime_error( msg );
    }

    const int rowStride = ( width * 3 + 3 ) & ~3;
    const int imageSize = rowStride * height;

    unsigned char fileHeader[14] = {};
    const int fileSize = 14 + 40 + imageSize;
    fileHeader[0] = 'B';
    fileHeader[1] = 'M';
    fileHeader[2] = static_cast<unsigned char>( fileSize );
    fileHeader[3] = static_cast<unsigned char>( fileSize >> 8 );
    fileHeader[4] = static_cast<unsigned char>( fileSize >> 16 );
    fileHeader[5] = static_cast<unsigned char>( fileSize >> 24 );
    fileHeader[10] = 54; // Pixel data offset.

    unsigned char infoHeader[40] = {};
    infoHeader[0] = 40; // Header size.
    infoHeader[4] = static_cast<unsigned char>( width );
    infoHeader[5] = static_cast<unsigned char>( width >> 8 );
    infoHeader[6] = static_cast<unsigned char>( width >> 16 );
    infoHeader[7] = static_cast<unsigned char>( width >> 24 );
    infoHeader[8] = static_cast<unsigned char>( height );
    infoHeader[9] = static_cast<unsigned char>( height >> 8 );
    infoHeader[10] = static_cast<unsigned char>( height >> 16 );
    infoHeader[11] = static_cast<unsigned char>( height >> 24 );
    infoHeader[12] = 1;  // Color planes.
    infoHeader[14] = 24; // Bits per pixel.
    infoHeader[20] = static_cast<unsigned char>( imageSize );
    infoHeader[21] = static_cast<unsigned char>( imageSize >> 8 );
    infoHeader[22] = static_cast<unsigned char>( imageSize >> 16 );
    infoHeader[23] = static_cast<unsigned char>( imageSize >> 24 );

    FILE* rawFile = nullptr;
    const errno_t err = fopen_s( &rawFile, path, "wb" );
    if ( err != 0 || !rawFile )
    {
        char msg[512];
        sprintf_s( msg, sizeof( msg ), "Failed to open screenshot file: %s  (CaptureSystem::SaveBackbufferBmp)", path );
        throw std::runtime_error( msg );
    }
    FileHandle file( rawFile );

    WriteExact( file.get(), fileHeader, sizeof( fileHeader ), path );
    WriteExact( file.get(), infoHeader, sizeof( infoHeader ), path );
    WriteExact( file.get(), pixels.data(), static_cast<size_t>( imageSize ), path );
}
} // namespace Basics
} // namespace SkullbonezCore
