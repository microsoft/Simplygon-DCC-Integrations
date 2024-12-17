// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef __SIMPLYGONMAXPERVERTEXDATA_H__
#define __SIMPLYGONMAXPERVERTEXDATA_H__

// helper class that handles all per-vertex data in SimplygonMax.
class SimplygonMaxPerVertexSkinningBone
{
	public:
	Simplygon::rid Bone;        // the bone node id
	Simplygon::real BoneWeight; // its weighting in the vertex

	bool operator>( const SimplygonMaxPerVertexSkinningBone& other ) const;
	bool operator==( const SimplygonMaxPerVertexSkinningBone& other ) const;
};

#endif //__SIMPLYGONMAXPERVERTEXDATA_H__