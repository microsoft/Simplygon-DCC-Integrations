// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "Triangulator.h"

using namespace Simplygon;
using namespace glm;

// A fixed size allocated array, with dynamic allocation fallback for
// allocations which are larger than fixedSize
template <class _Ty, uint fixedSize = 32> class FixedArray
{
	private:
	_Ty data[ fixedSize ];
	_Ty* ptr = nullptr;
	uint allocSize;

	FixedArray( const FixedArray& other ) = delete;
	FixedArray( FixedArray&& other ) = delete;

	public:
	FixedArray( uint _allocSize )
	    : allocSize( _allocSize )
	{
		if( allocSize > fixedSize )
		{
			this->ptr = new _Ty[ allocSize ];
		}
		else
		{
			memset( data, 0, sizeof( _Ty ) * allocSize );
		}
	}

	~FixedArray()
	{
		if( this->ptr )
		{
			delete[] this->ptr;
		}
	}

	operator _Ty*() { return this->ptr ? this->ptr : data; }
};

Triangulator::Triangulator( const vec3* vertexCoords, size_t vertexCount )
    : VertexCoords( vertexCoords )
    , VertexCount( vertexCount )
{
}

// generate a default, simple, convex polygon triangulation.
// used as a fallback for degenerate input polygons
inline void simpleConvexTriangulation( Triangulator::Triangle* destTriangleList, unsigned int srcPolygonCornerCount )
{
	// do the triangles, in order around the perimeter: 0-1-2, 0-2-3, 0-3-4 ...
	for( uint tIndex = 0; tIndex < srcPolygonCornerCount - 2; ++tIndex )
	{
		destTriangleList[ tIndex ] = { 0, tIndex + 1, tIndex + 2 };
	}
}

// check if point is inside triangle in 2D using barycentric coords
inline bool isPointInsideTriangle( const vec2& pt, const vec2& v0, const vec2& v1, const vec2& v2 )
{
	const vec2 a = v2 - v1;
	const vec2 b = v0 - v2;
	const vec2 c = v1 - v0;

	const vec2 ap = pt - v0;
	const vec2 bp = pt - v1;
	const vec2 cp = pt - v2;

	float s0 = a.x * bp.y - a.y * bp.x;
	float s1 = c.x * ap.y - c.y * ap.x;
	float s2 = b.x * cp.y - b.y * cp.x;

	return ( ( s0 >= 0 ) && ( s1 >= 0 ) && ( s2 >= 0 ) )     // normal
	       || ( ( s0 <= 0 ) && ( s1 <= 0 ) && ( s2 <= 0 ) ); // flipped
}

bool Triangulator::TriangulateConcavePolygon( Triangle* destTriangleList,
                                              const unsigned int* srcPolygonVertexIds,
                                              unsigned int srcPolygonCornerCount,
                                              bool enableConvexFallback ) const
{
	// we have 5 or more corners, use the general polygon triangulation
	FixedArray<vec3> srcPolygonVertexCoordsAllocation( srcPolygonCornerCount );
	vec3* srcPolygonVertexCoords = srcPolygonVertexCoordsAllocation;

	// collect vertex coords from the vertex list
	for( size_t cornerIndex = 0; cornerIndex < srcPolygonCornerCount; ++cornerIndex )
	{
		unsigned int vertexIndex = srcPolygonVertexIds[ cornerIndex ];
		if( vertexIndex >= this->VertexCount )
			throw std::out_of_range( "Invalid vertexIndex in polygon" );
		srcPolygonVertexCoords[ cornerIndex ] = this->VertexCoords[ vertexIndex ];
	} 

	return Triangulator::TriangulateConcavePolygon( destTriangleList, srcPolygonVertexCoords, srcPolygonCornerCount, enableConvexFallback );
}

bool Triangulator::TriangulateConcavePolygon( Triangle* destTriangleList,
                                              const vec3* srcPolygonVertexCoords,
                                              unsigned int srcPolygonCornerCount,
                                              bool enableConvexFallback )
{
	// find a tangent space embedding of the polygon
	vec3 tangent;
	vec3 bitangent;
	vec3 normal;

	// look for a valid normal using the corners of the polygon
	bool normalWasFound = false;
	uint prevCorner = srcPolygonCornerCount - 1;
	for( uint cIndex = 0; cIndex < srcPolygonCornerCount; ++cIndex )
	{
		uint nextCorner = ( cIndex + 1 ) % srcPolygonCornerCount;

		// try calculating the normal of the polygon corner
		tangent = normalize( srcPolygonVertexCoords[ prevCorner ] - srcPolygonVertexCoords[ cIndex ] );
		bitangent = normalize( srcPolygonVertexCoords[ nextCorner ] - srcPolygonVertexCoords[ cIndex ] );
		normal = cross( tangent, bitangent );
		if( dot( normal, normal ) > FLT_EPSILON )
		{
			normal = normalize( normal );
			normalWasFound = true;
			break;
		}

		prevCorner = cIndex;
	}

	// if no normal was found, the polygon vertices are all either the same point, or along a straight line
	// in either case, the polygon is degenerate, so just triangulate as a convex polygon in
	// order to keep the topology/connectivity of the mesh
	if( !normalWasFound )
	{
		simpleConvexTriangulation( destTriangleList, srcPolygonCornerCount );
		return false;
	}

	// orthogonalize the tangent base
	bitangent = normalize( cross( normal, tangent ) );

	// project polygon to 2D plane, and move corners so that the first corner is in the origin
	FixedArray<vec2> allocPolyCoords2D( srcPolygonCornerCount );
	vec2* polyCoords2D = allocPolyCoords2D;
	FixedArray<uint> allocPolyCornerIndices( srcPolygonCornerCount );
	uint* polyCornerIndices = allocPolyCornerIndices;

	vec3 origin = srcPolygonVertexCoords[ 0 ];
	for( uint cIndex = 0; cIndex < srcPolygonCornerCount; ++cIndex )
	{
		vec3 tmp = srcPolygonVertexCoords[ cIndex ] - origin;
		polyCoords2D[ cIndex ] = { dot( tmp, tangent ), dot( tmp, bitangent ) };
		polyCornerIndices[ cIndex ] = cIndex;
	}

	// calculate total winding of the polygon in 2D
	float winding = 0;
	for( uint cIndex = 0; cIndex < srcPolygonCornerCount; ++cIndex )
	{
		uint nextCorner = ( cIndex + 1 ) % srcPolygonCornerCount;
		float xdiff = polyCoords2D[ nextCorner ].x - polyCoords2D[ cIndex ].x;
		float ysum = polyCoords2D[ nextCorner ].y + polyCoords2D[ cIndex ].y;
		winding += xdiff * ysum;
	}

	// ear clip polygon on the 2D plane
	uint polyCornerIndicesSize = srcPolygonCornerCount;
	uint foundTriangleCount = 0;
	while( polyCornerIndicesSize >= 3 )
	{
		// find a candidate triangle
		bool foundATriangle = false;
		uint prevIndex = polyCornerIndicesSize - 1;
		for( uint cIndex = 0; cIndex < polyCornerIndicesSize; ++cIndex )
		{
			const uint nextIndex = ( cIndex + 1 ) % polyCornerIndicesSize;

			// get the current 3 corner indices
			const unsigned int c0 = polyCornerIndices[ prevIndex ];
			const unsigned int c1 = polyCornerIndices[ cIndex ];
			const unsigned int c2 = polyCornerIndices[ nextIndex ];

			const vec2 coord0 = polyCoords2D[ c0 ];
			const vec2 coord1 = polyCoords2D[ c1 ];
			const vec2 coord2 = polyCoords2D[ c2 ];

			// calculate the winding of the triangle
			float xdiff = coord1.x - coord0.x;
			float ysum = coord1.y + coord0.y;
			float triangleWinding = xdiff * ysum;

			xdiff = coord2.x - coord1.x;
			ysum = coord2.y + coord1.y;
			triangleWinding += xdiff * ysum;

			xdiff = coord0.x - coord2.x;
			ysum = coord0.y + coord2.y;
			triangleWinding += xdiff * ysum;

			// make sure the triangle winding is the same as the whole poly, else we are on the outside of the poly
			if( ( ( winding < 0 ) && ( triangleWinding >= 0 ) ) || ( ( winding > 0 ) && ( triangleWinding <= 0 ) ) )
			{
				prevIndex = cIndex;
				continue;
			}

			// we have the same winding. now check if any of the other corners in the remaining polygon
			// is inside the triangle.
			bool foundPointInside = false;
			for( uint pIndex = 0; pIndex < polyCornerIndicesSize; ++pIndex )
			{
				if( pIndex == prevIndex || pIndex == cIndex || pIndex == nextIndex )
					continue;

				const unsigned int p = polyCornerIndices[ pIndex ];
				if( isPointInsideTriangle( polyCoords2D[ p ], coord0, coord1, coord2 ) )
				{
					foundPointInside = true;
					break;
				}
			}

			// if we found a point on the inside, we can't use this triangle
			if( foundPointInside )
			{
				prevIndex = cIndex;
				continue;
			}

			// the triangle checks out, use it
			destTriangleList[ foundTriangleCount ].c[ 0 ] = c0;
			destTriangleList[ foundTriangleCount ].c[ 1 ] = c1;
			destTriangleList[ foundTriangleCount ].c[ 2 ] = c2;
			++foundTriangleCount;

			// remove the corner at cIndex, and move up all the remainging values in the list
			memcpy( &polyCornerIndices[ cIndex ], &polyCornerIndices[ cIndex + 1 ], sizeof( uint ) * ( polyCornerIndicesSize - cIndex - 1 ) );
			--polyCornerIndicesSize;

			foundATriangle = true;
			break;
		}

		// if no triangle was found, only degenerate triangles are left.
		// bail out of the generation
		if( !foundATriangle )
			break;
	}

	// if there are more than 3 corners left in the list, there were issues with the polygon, probably overlapping.
	// Since we really wont have a correct triangulation anyway, fall back to convex triangulation, just
	// to keep connectivity and topology
	if( polyCornerIndicesSize >= 3 )
	{
		if( enableConvexFallback )
		{
			simpleConvexTriangulation( destTriangleList, srcPolygonCornerCount );
		}
		return false;
	}

	return true;
}
