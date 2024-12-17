// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "PCH.h"
#include "SimplygonMax.h"

using namespace Simplygon;

#include "NormalCalculator.h"

#define NORMALS_PER_BUCKET 1024

class VertexNormal
{
	public:
	Point3 normal;
	DWORD smoothingGroup;

	VertexNormal* next;

	VertexNormal()
	{
		this->next = nullptr;
		this->smoothingGroup = 0;
	}
};

class VertexNormalBucket
{
	public:
	VertexNormal normals[ NORMALS_PER_BUCKET ];
	VertexNormalBucket* next;

	VertexNormalBucket() { this->next = nullptr; }
};

class VertexNormalBucketAllocator
{
	public:
	VertexNormalBucket* first;
	uint numUsedNormals;

	VertexNormalBucketAllocator()
	{
		this->first = new VertexNormalBucket;
		this->first->next = nullptr;
		this->numUsedNormals = 0;
	}

	~VertexNormalBucketAllocator()
	{
		while( first != nullptr )
		{
			VertexNormalBucket* tmp = first->next;
			delete first;
			first = tmp;
		}
	}

	VertexNormal* AllocNormal()
	{
		// allocate a bucket, if needed
		if( this->numUsedNormals >= NORMALS_PER_BUCKET )
		{
			VertexNormalBucket* tmp = new VertexNormalBucket;
			tmp->next = first;
			first = tmp;
			numUsedNormals = 0;
		}

		// return a normal
		VertexNormal* tmp = &first->normals[ numUsedNormals ];
		++numUsedNormals;
		return tmp;
	}
};

class NormalRecord
{
	public:
	NormalRecord()
	{
		this->triangleId = 0;
		this->normal = Point3( 0, 0, 0 );
	}
	NormalRecord( uint tid, Point3& n )
	    : triangleId( tid )
	    , normal( n )
	{
	}
	uint GetTriangleID() { return this->triangleId; }
	Point3 GetNormal() { return this->normal; }

	private:
	uint triangleId;
	Point3 normal;
};

class NormalList
{
	public:
	VertexNormal* first;
	std::vector<NormalRecord> trianglesWithoutSmoothingGroups;

	NormalList() { this->first = nullptr; }

	~NormalList() { this->trianglesWithoutSmoothingGroups.clear(); }

	void AddNormal( Point3& normal, DWORD dwSmoothingGroup, VertexNormalBucketAllocator* allocator, uint tid )
	{
		// find an existing vertex normal for the smoothing group
		VertexNormal* tmp = first;
		while( tmp != nullptr )
		{
			if( dwSmoothingGroup & tmp->smoothingGroup )
			{
				// we have a match, add the normal and smoothing mask, and exit
				tmp->smoothingGroup |= dwSmoothingGroup;
				tmp->normal += normal;
				return;
			}
			tmp = tmp->next;
		}

		// an existing vertex was not found, add a new
		tmp = allocator->AllocNormal();
		tmp->normal = normal;
		tmp->smoothingGroup = dwSmoothingGroup;
		tmp->next = first;
		first = tmp;

		// if no match found, store triangle id and normal for later use
		this->trianglesWithoutSmoothingGroups.push_back( NormalRecord( tid, normal ) );
	}

	void NormalizeNormals()
	{
		VertexNormal* curr = first;

		// for each normal, search for overlapping smoothing
		// groups, and add into normal, then normalize
		while( curr != nullptr )
		{
			VertexNormal* other = curr->next;
			VertexNormal* prev = curr;

			// look for overlapping smoothing groups
			while( other != nullptr )
			{
				if( other->smoothingGroup & curr->smoothingGroup )
				{
					// they overlap, include other in curr
					curr->normal += other->normal;
					curr->smoothingGroup |= other->smoothingGroup;

					// unlink other from the list
					prev->next = other->next;
					other = other->next;
				}
				else
				{
					// no overlap, move on
					prev = other;
					other = other->next;
				}
			}

			// all overlaps have been added, normalize current normal
			curr->normal = ::Normalize( curr->normal );

			// move to the next normal
			curr = curr->next;
		}

		// done
	}

	Point3 GetNormal( DWORD dwSmoothingGroup, uint tid )
	{
		// find a normal that overlaps smoothing group
		VertexNormal* curr = first;
		while( curr != nullptr )
		{
			if( curr->smoothingGroup & dwSmoothingGroup )
			{
				return curr->normal;
			}
			curr = curr->next;
		}

		// if not part of any smoothing group, return original normal stored in list
		for( uint t = 0; t < this->trianglesWithoutSmoothingGroups.size(); ++t )
		{
			if( this->trianglesWithoutSmoothingGroups[ t ].GetTriangleID() == tid )
			{
				return this->trianglesWithoutSmoothingGroups[ t ].GetNormal();
			}
		}

		// failed to find a normal, return first (this should never happen)
		return first != nullptr ? first->normal : Point3( 0, 0, 0 );
	}
};

inline Point3 TriangleNormal( Point3 triangleCoords[ 3 ] )
{
	Point3 vec1 = triangleCoords[ 1 ] - triangleCoords[ 0 ];
	Point3 vec2 = triangleCoords[ 2 ] - triangleCoords[ 0 ];
	Point3 normal = vec1 ^ vec2;
	normal.Normalize();
	return normal;
}

// Compute the face and vertex normals
// Normals are defined as 3 normals per face (triangle) in the mesh
void ComputeVertexNormals( spGeometryData sgMeshData )
{
	const uint triangleCount = sgMeshData->GetTriangleCount();
	const uint vertexCount = sgMeshData->GetVertexCount();

	spRidArray sgVertexIds = sgMeshData->GetVertexIds();
	spRealArray sgCoords = sgMeshData->GetCoords();
	spRealArray sgNormals = sgMeshData->GetNormals();

	spUnsignedIntArray sgShadingGroupIds = spUnsignedIntArray::SafeCast( sgMeshData->GetUserTriangleField( "ShadingGroupIds" ) );

	Point3 v0, v1, v2;
	VertexNormalBucketAllocator allocator;
	NormalList* vnorms = new NormalList[ vertexCount ];
	Point3* fnorms = new Point3[ triangleCount ];

	// Compute face and vertex surface normals
	for( uint tid = 0; tid < triangleCount; ++tid )
	{
		const uint tx3 = tid * 3;

		// check for degenerate triangles
		if( sgVertexIds->GetItem( tx3 + 0 ) < 0 )
		{
			continue;
		}

		// Calculate the surface normal
		rid ids[ 3 ];
		Point3 coords[ 3 ];
		for( uint c = 0; c < 3; ++c )
		{
			// get vertex id of corner
			ids[ c ] = sgVertexIds->GetItem( tx3 + c );

			// get coordinate of corner
			spRealData sgVertex = sgCoords->GetTuple( ids[ c ] );
			coords[ c ].Set( sgVertex[ 0 ], sgVertex[ 1 ], sgVertex[ 2 ] );
		}

		// calculate the triangle normal
		fnorms[ tid ] = TriangleNormal( coords );

		// add normal to all 3 vertices
		for( uint c = 0; c < 3; ++c )
		{
			vnorms[ ids[ c ] ].AddNormal( fnorms[ tid ], sgShadingGroupIds->GetItem( tid ), &allocator, tid );
		}
	}

	for( uint vid = 0; vid < vertexCount; ++vid )
	{
		vnorms[ vid ].NormalizeNormals();
	}

	for( uint tid = 0; tid < triangleCount; ++tid )
	{
		const uint tx3 = tid * 3;

		// set degenerate tris to default value, point along x
		if( sgVertexIds->GetItem( tx3 + 0 ) < 0 )
		{
			real normal[ 3 ] = { 1, 0, 0 };
			sgNormals->SetTuple( tx3 + 0, normal );
			sgNormals->SetTuple( tx3 + 1, normal );
			sgNormals->SetTuple( tx3 + 2, normal );
			continue;
		}

		for( uint q = 0; q < 3; ++q )
		{
			int vid = sgVertexIds->GetItem( tx3 + q );
			Point3 normal = vnorms[ vid ].GetNormal( sgShadingGroupIds->GetItem( tid ), tid );
			sgNormals->SetTuple( tx3 + q, normal );
		}
	}

	delete[] vnorms;
	delete[] fnorms;
}
