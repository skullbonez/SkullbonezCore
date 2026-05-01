#pragma once


// Dispatcher header - includes either OpenGL or DirectX implementation
// based on SKULLBONEZ_USE_DIRECTX11 define

#ifdef SKULLBONEZ_USE_DIRECTX11
    #include "SkullbonezTextureCollectionDX.h"
#else
    #include "SkullbonezTextureCollectionGL.h"
#endif
