#pragma once


// Dispatcher header - includes either OpenGL or DirectX implementation
// based on SKULLBONEZ_USE_DIRECTX11 define

#ifdef SKULLBONEZ_USE_DIRECTX11
    #include "SkullbonezShaderDX.h"
#else
    #include "SkullbonezShaderGL.h"
#endif
