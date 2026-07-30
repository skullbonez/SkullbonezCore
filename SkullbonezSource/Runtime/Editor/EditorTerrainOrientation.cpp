/*
File: SkullbonezSource/Runtime/Editor/EditorTerrainOrientation.cpp
Purpose:
  Converts a terrain normal into the editor's deterministic placement orientation.

Summary:
  The editor aligns an object's local up axis to a normalized terrain normal.
  Parallel and antiparallel poles are handled explicitly before the general
  cross-product axis is normalized.

Glossary:
  Alignment axis: Cross product of world up and the requested terrain normal.
  Unit-domain pole: Either endpoint accepted by inverse cosine.

Invariants:
  - The general axis path runs only when its magnitude is safely non-zero.
  - The inverse-cosine argument always uses the shared Maths domain spelling.
  - A fully inverted normal rotates by pi about deterministic world X.

Related:
  - SkullbonezSource/Runtime/Editor/EditorTerrainOrientation.h
  - SkullbonezSource/Runtime/Editor/EditorPlacementAssets.cpp
*/
#include "EditorTerrainOrientation.h"

using SkullbonezCore::Math::Orientation::IDENTITY_QUATERNION;
using SkullbonezCore::Math::Orientation::Quaternion;
using SkullbonezCore::Math::Vector::CrossProduct;
using SkullbonezCore::Math::Vector::Dot;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Math::Vector::VectorMag;

Quaternion SkullbonezCore::Runtime::EditorTerrainOrientationFromNormal( Vector3 terrainNormal )
{
    const float normalMag = VectorMag( terrainNormal );

    if ( normalMag <= TOLERANCE )
    {
        return IDENTITY_QUATERNION;
    }

    terrainNormal /= normalMag;

    const Vector3 up( 0.0f, 1.0f, 0.0f );
    const float dot = Math::ClampUnit( Dot( up, terrainNormal ) );
    Quaternion orientation = IDENTITY_QUATERNION;

    if ( dot > 0.9995f )
    {
        return orientation;
    }

    if ( dot < -0.9995f )
    {
        orientation.RotateAboutAxis( Vector3( 1.0f, 0.0f, 0.0f ), _PI );
        return orientation;
    }

    Vector3 axis = CrossProduct( up, terrainNormal );
    const float axisMag = VectorMag( axis );

    if ( axisMag <= TOLERANCE )
    {
        return orientation;
    }

    axis /= axisMag;
    orientation.RotateAboutAxis( axis, acosf( dot ) );
    return orientation;
}
