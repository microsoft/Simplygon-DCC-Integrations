// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "PCH.h"
#include "SimplygonMaxPerVertexData.h"

using namespace Simplygon;
using namespace std;

bool SimplygonMaxPerVertexSkinningBone::operator>( const SimplygonMaxPerVertexSkinningBone& other ) const
{
	if( this->BoneWeight > other.BoneWeight )
		return true;
	if( this->BoneWeight < other.BoneWeight )
		return false;

	if( this->Bone > other.Bone )
		return true;
	else
		return false;
}

bool SimplygonMaxPerVertexSkinningBone::operator==( const SimplygonMaxPerVertexSkinningBone& other ) const
{
	if( this->BoneWeight == other.BoneWeight )
	{
		if( this->Bone == other.Bone )
			return true;
	}
	return false;
}
