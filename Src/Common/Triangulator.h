// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtx/normal.hpp>
#include <stdexcept>

namespace Simplygon {

// Triangulator - Triangulation generation class.
//
// The Triangulator is used to generate triangles from simply-connected polygons (polygons without holes).
// The methods will try to do the best to generate triangles from the polygons, and the number of triangles
// output will always equal (the number of corners input - 2), even for concave and degenerate input polygons.
// Note that degenerate input polygons will cause degenerate output triangles, but the triangulation will
// keep the topology/connectivity of the input polygon. If the triangulation detects degenerate outputs,
// the methods will return false.

class Triangulator
{
	// Public section
	public:
	// A triangle with corner indices, used for the output triangle list
	struct Triangle
	{
		unsigned int c[ 3 ];
	};

	using uint = glm::uint;
	using vec3 = glm::vec3;

	// Constructs a Triangulator, and requires a list of vertex coordinates.
	// Note: The vertex coordinate list is assumed not to change during the lifetime of
	// the Triangulator object.
	Triangulator( const vec3* vertexCoords, size_t vertexCount );

	// Triangulate any quad, convex or concave, planar or non-planar.
	// The code will try to triangulate along the shortest diagonal, unless that causes
	// a fold-over (caused by a concave or non-planar quad).
	//
	// Notes on triangle creation:
	// The code will always create 2 triangles, where the first corner is shared
	// between the two triangles, and the last edge of the first triangle will be the first
	// edge of the second triangle. (Note however, that the first corner of the two
	// triangles will not necessarily be the first corner of the quad. It will be either
	// the first or the second corner of the quad.)
	//
	// Also note, that the output triangles will reference the input quad's local corner indices
	// [0->3] and NOT the vertex ids. This is because when generating the triangles, the corners
	// can be used to reference eg texture coordinates or vertex normals.
	//
	// Parameters:
	//	destTriangleList - an array of Triangle, minimum length 2, which will receive the
	//                     corner ids of the two triangles
	//	srcQuadVertexIds - a list of length 4, with the vertex index of each corner in the quad.
	//	srcQuadVertexCoords - a list of length 4, with the vertex coords of each corner in the quad.
	//
	// Returns: true if the method could create valid triangles. false if the triangles generated
	// are degenerate.
	//
	bool TriangulateQuad( Triangle* destTriangleList, const unsigned int* srcQuadVertexIds ) const;
	static bool TriangulateQuad( Triangle* destTriangleList, const vec3* srcQuadVertexCoords );

	// Triangulate any simply-connected polygon, convex or concave, planar or non-planar.
	// If the polygon is a triangle, it will be returned directly. If the polygon is a quad,
	// the TriangulateQuad() method will be used to generate the triangles.
	// Note that the number of output triangles will always equal the number of input corners minus 2.
	//
	// Parameters:
	//	destTriangleList - an array of Triangle, minimum length 2, which will receive
	//					   the corner ids of the two triangles
	//	srcPolygonVertexIds - a list of length (srcPolygonCornerCount), with the vertex
	//					      index of each corner in the quad.
	//	srcPolygonVertexCoords - a list of length (srcPolygonCornerCount), with the vertex
	//                           coords of each corner in the quad.
	//  srcPolygonCornerCount - the size of the polygon, and the length of the
	//                          srcPolygonVertexIds/srcPolygonVertexCoords list
	//
	//  enableConvexFallback - a toggle that enables convex triangulation as a fallback if 
	//                         there were issues with the triangulation, issues are usually 
	//                         caused by overlapping geometry. If false, the best available
	//                         concave triangulation is returned. The default value is true.
	// 
	// Returns: true if the method could create valid triangles. false if the triangles generated
	// are degenerate, and the method used a fallback fan of triangles to triangulate the poly
	// (corners 0-1-2, 0-2-3, 0-3-4 ... )
	//
	bool TriangulatePolygon( Triangle* destTriangleList,
	                         const unsigned int* srcPolygonVertexIds,
	                         unsigned int srcPolygonCornerCount,
	                         bool enableConvexFallback = true ) const;
	static bool
	TriangulatePolygon( Triangle* destTriangleList, const vec3* srcPolygonVertexCoords, unsigned int srcPolygonCornerCount, bool enableConvexFallback = true );

	// Internal section
	private:
	const vec3* VertexCoords;
	const size_t VertexCount;

	bool TriangulateConcavePolygon( Triangle* destTriangleList,
	                                const unsigned int* srcPolygonVertexIds,
	                                unsigned int srcPolygonCornerCount,
	                                bool enableConvexFallback = true ) const;
	static bool TriangulateConcavePolygon( Triangle* destTriangleList,
	                                       const vec3* srcPolygonVertexCoords,
	                                       unsigned int srcPolygonCornerCount,
	                                       bool enableConvexFallback = true );

	static bool isvalid( float val )
	{
		int t = fpclassify( val );
		return ( t != FP_NAN ) && ( t != FP_INFINITE );
	}
};

inline bool Triangulator::TriangulateQuad( Triangle* destTriangleList, const unsigned int* srcQuadVertexIds ) const
{
	// collect vertex coords from the vertex list
	vec3 srcVertexCoords[ 4 ] = {};
	for( size_t cornerIndex = 0; cornerIndex < 4; ++cornerIndex )
	{
		unsigned int vertexIndex = srcQuadVertexIds[ cornerIndex ];
		if( vertexIndex >= this->VertexCount )
			throw std::out_of_range( "Invalid vertexIndex in quad" );
		srcVertexCoords[ cornerIndex ] = this->VertexCoords[ vertexIndex ];
	}

	// call the static method
	return Triangulator::TriangulateQuad( destTriangleList, srcVertexCoords );
}

inline bool Triangulator::TriangulateQuad( Triangle* destTriangleList, const vec3* srcVertexCoords )
{
	// The diagonal cut will either be corner 0->2 or 1->3, so
	// the triangles will either be:
	//
	//   0-1-2 and 0-2-3  (diagonal 0->2)
	// or
	//   1-2-3 and 1-3-0  (diagonal 1->3)
	//
	static constexpr Triangle triangleCoords[ 2 ][ 2 ] = {
	    { { 0, 1, 2 }, { 0, 2, 3 } }, // diagonal 0->2
	    { { 1, 2, 3 }, { 1, 3, 0 } }  // diagonal 1->3
	};

	// get the squared diagonal lengths of 0->2 and 1->3
	const vec3 delta0 = srcVertexCoords[ 2 ] - srcVertexCoords[ 0 ];
	const float d0LengthSquared = dot( delta0, delta0 );
	const vec3 delta1 = srcVertexCoords[ 3 ] - srcVertexCoords[ 1 ];
	const float d1LengthSquared = dot( delta1, delta1 );

	// first try the shortest diagonal, then the other diagonal if
	// the shorter one causes foldover, or the triangle normals are degenerate
	// in the first diagonal
	unsigned int diagonalIndex = ( d0LengthSquared <= d1LengthSquared ) ? 0 : 1;

	// get and classify the normals of the two triangles
	const vec3 tri0Normal = triangleNormal( srcVertexCoords[ triangleCoords[ diagonalIndex ][ 0 ].c[ 0 ] ],
	                                        srcVertexCoords[ triangleCoords[ diagonalIndex ][ 0 ].c[ 1 ] ],
	                                        srcVertexCoords[ triangleCoords[ diagonalIndex ][ 0 ].c[ 2 ] ] );
	const vec3 tri1Normal = triangleNormal( srcVertexCoords[ triangleCoords[ diagonalIndex ][ 1 ].c[ 0 ] ],
	                                        srcVertexCoords[ triangleCoords[ diagonalIndex ][ 1 ].c[ 1 ] ],
	                                        srcVertexCoords[ triangleCoords[ diagonalIndex ][ 1 ].c[ 2 ] ] );
	const bool diagonalValid = isvalid( tri0Normal.x ) && isvalid( tri1Normal.x );
	const bool diagonalLowAngle = dot( tri0Normal, tri1Normal ) >= 0;

	// if we have valid normals and a low angle in out preferred diagonal, early out
	if( diagonalValid && diagonalLowAngle )
	{
		destTriangleList[ 0 ] = triangleCoords[ diagonalIndex ][ 0 ];
		destTriangleList[ 1 ] = triangleCoords[ diagonalIndex ][ 1 ];
		return true;
	}

	// prefered diagonal does not check out, try the other diagonal
	const unsigned int otherDiagonalIndex = 1 ^ diagonalIndex;

	// get and classify the normals of the two triangles of the other diagonal
	const vec3 otherTri0Normal = triangleNormal( srcVertexCoords[ triangleCoords[ otherDiagonalIndex ][ 0 ].c[ 0 ] ],
	                                             srcVertexCoords[ triangleCoords[ otherDiagonalIndex ][ 0 ].c[ 1 ] ],
	                                             srcVertexCoords[ triangleCoords[ otherDiagonalIndex ][ 0 ].c[ 2 ] ] );
	const vec3 otherTri1Normal = triangleNormal( srcVertexCoords[ triangleCoords[ otherDiagonalIndex ][ 1 ].c[ 0 ] ],
	                                             srcVertexCoords[ triangleCoords[ otherDiagonalIndex ][ 1 ].c[ 1 ] ],
	                                             srcVertexCoords[ triangleCoords[ otherDiagonalIndex ][ 1 ].c[ 2 ] ] );
	const bool otherDiagonalValid = isvalid( otherTri0Normal.x ) && isvalid( otherTri1Normal.x );
	const bool otherDiagonalLowAngle = dot( otherTri0Normal, otherTri1Normal ) >= 0;

	// if we have valid normals and a low angle, take the other diagonal instead
	if( otherDiagonalValid && otherDiagonalLowAngle )
	{
		destTriangleList[ 0 ] = triangleCoords[ otherDiagonalIndex ][ 0 ];
		destTriangleList[ 1 ] = triangleCoords[ otherDiagonalIndex ][ 1 ];
		return true;
	}

	// neither diagonal fills all criterias, take one that has valid values, if possible

	// try the preferred diagonal, it has the shortest diagonal
	if( diagonalValid )
	{
		// diagonal has valid values
		destTriangleList[ 0 ] = triangleCoords[ diagonalIndex ][ 0 ];
		destTriangleList[ 1 ] = triangleCoords[ diagonalIndex ][ 1 ];
		return true;
	}

	// try the other diagonal
	if( otherDiagonalValid )
	{
		// other diagonal has valid values
		destTriangleList[ 0 ] = triangleCoords[ otherDiagonalIndex ][ 0 ];
		destTriangleList[ 1 ] = triangleCoords[ otherDiagonalIndex ][ 1 ];
		return true;
	}

	// no valid triangulation possible, output diagonal 0, and return false to mark that the triangulation as degenerate
	destTriangleList[ 0 ] = triangleCoords[ 0 ][ 0 ];
	destTriangleList[ 1 ] = triangleCoords[ 0 ][ 1 ];
	return false;
}

inline bool Triangulator::TriangulatePolygon( Triangle* destTriangleList,
                                              const unsigned int* srcPolygonVertexIds,
                                              unsigned int srcPolygonCornerCount,
                                              bool enableConvexFallback ) const
{
	// check for special cases, with less than 5 corners

	// use the quad triangulator for 4 corners
	if( srcPolygonCornerCount == 4 )
	{
		return TriangulateQuad( destTriangleList, srcPolygonVertexIds );
	}

	// just return the triangle if 3 corners
	if( srcPolygonCornerCount == 3 )
	{
		destTriangleList[ 0 ] = { 0, 1, 2 };
		return true;
	}

	// no output for fewer than 3 corners
	if( srcPolygonCornerCount < 3 )
	{
		return false;
	}

	return Triangulator::TriangulateConcavePolygon( destTriangleList, srcPolygonVertexIds, srcPolygonCornerCount, enableConvexFallback );
}

inline bool
Triangulator::TriangulatePolygon( Triangle* destTriangleList, const vec3* srcPolygonVertexCoords, unsigned int srcPolygonCornerCount, bool enableConvexFallback )
{
	// check for special cases, with less than 5 corners

	// use the quad triangulator for 4 corners
	if( srcPolygonCornerCount == 4 )
	{
		return TriangulateQuad( destTriangleList, srcPolygonVertexCoords );
	}

	// just return the triangle if 3 corners
	if( srcPolygonCornerCount == 3 )
	{
		destTriangleList[ 0 ] = { 0, 1, 2 };
		return true;
	}

	// no output for fewer than 3 corners
	if( srcPolygonCornerCount < 3 )
	{
		return false;
	}

	// we have 5 or more corners, use the general polygon triangulation
	return TriangulateConcavePolygon( destTriangleList, srcPolygonVertexCoords, srcPolygonCornerCount, enableConvexFallback );
}

};
