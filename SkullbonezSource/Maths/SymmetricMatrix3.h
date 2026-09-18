#pragma once
#include "Vector3.h"
#include <cmath>

namespace SkullbonezCore::Math::Transformation
{
// Six independent coefficients of a symmetric matrix. Off-diagonal order is
// xy, xz, yz, using actual matrix entries (not engineering shear components).
struct SymmetricMatrix3
{
    Vector::Vector3 diagonal = Vector::ZERO_VECTOR;
    Vector::Vector3 offDiagonal = Vector::ZERO_VECTOR;

    SymmetricMatrix3() = default;
    SymmetricMatrix3( const Vector::Vector3& diagonalValue ) : diagonal( diagonalValue )
    {
    }
    SymmetricMatrix3( const Vector::Vector3& diagonalValue, const Vector::Vector3& offDiagonalValue ) : diagonal( diagonalValue ), offDiagonal( offDiagonalValue )
    {
    }

    bool IsDiagonal() const
    {
        return offDiagonal.x == 0.0f && offDiagonal.y == 0.0f && offDiagonal.z == 0.0f;
    }

    Vector::Vector3 operator*( const Vector::Vector3& v ) const
    {
        // Preserve the diagonal operation order for existing primitive bodies.
        if ( IsDiagonal() )
        {
            return Vector::VectorMultiply( diagonal, v );
        }
        return { diagonal.x * v.x + offDiagonal.x * v.y + offDiagonal.y * v.z, offDiagonal.x * v.x + diagonal.y * v.y + offDiagonal.z * v.z, offDiagonal.y * v.x + offDiagonal.z * v.y + diagonal.z * v.z };
    }

    SymmetricMatrix3 operator*( float scalar ) const
    {
        return { diagonal * scalar, offDiagonal * scalar };
    }

    bool TryInversePositiveDefinite( SymmetricMatrix3& inverse ) const
    {
        // Double intermediates prevent the cubic determinant from overflowing
        // float for otherwise representable mass and scale combinations.
        const double a = diagonal.x, b = diagonal.y, c = diagonal.z;
        const double d = offDiagonal.x, e = offDiagonal.y, f = offDiagonal.z;
        const double minor = a * b - d * d;
        const double determinant = a * ( b * c - f * f ) - d * ( d * c - e * f ) + e * ( d * f - b * e );
        if ( !( a > 0 && minor > 0 && determinant > 0 ) || !std::isfinite( determinant ) )
        {
            return false;
        }
        const SymmetricMatrix3 result( { static_cast<float>( ( b * c - f * f ) / determinant ), static_cast<float>( ( a * c - e * e ) / determinant ), static_cast<float>( minor / determinant ) }, { static_cast<float>( ( e * f - d * c ) / determinant ), static_cast<float>( ( d * f - b * e ) / determinant ), static_cast<float>( ( d * e - a * f ) / determinant ) } );
        if ( !std::isfinite( result.diagonal.x ) || !std::isfinite( result.diagonal.y ) || !std::isfinite( result.diagonal.z ) || !std::isfinite( result.offDiagonal.x ) ||
             !std::isfinite( result.offDiagonal.y ) || !std::isfinite( result.offDiagonal.z ) )
        {
            return false;
        }
        inverse = result;
        return true;
    }
};
} // namespace SkullbonezCore::Math::Transformation
