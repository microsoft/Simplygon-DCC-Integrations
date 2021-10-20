// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "PCH.h"
#include "MaxMaterialMap.h"

void MaxMaterialMap::SetupFromMaterial( Mtl* mMaxMaterial )
{
	this->sgMaterialName = mMaxMaterial->GetName();
	this->mMaxMaterialHandle = Animatable::GetHandleByAnim( mMaxMaterial );

	if( mMaxMaterial != nullptr && mMaxMaterial->ClassID() == Class_ID( MULTI_CLASS_ID, 0 ) )
	{
		this->NumSubMaterials = mMaxMaterial->NumSubMtls();
	}
	else
	{
		this->NumSubMaterials = 0;
	}
}

int MaxMaterialMap::GetMaxMaterialId( int sgMaterialId )
{
	std::map<int, int>::const_iterator& materialIterator = this->SGToMaxMapping.find( sgMaterialId );
	if( materialIterator != this->SGToMaxMapping.end() )
	{
		return materialIterator->second;
	}
	else
	{
		return 0;
	}
}

int MaxMaterialMap::GetSimplygonMaterialId( int maxMaterialId ) const
{
	if( this->NumSubMaterials == 0 )
	{
		maxMaterialId = 0;
	}

	std::map<int, int>::const_iterator& materialIterator = this->MaxToSGMapping.find( maxMaterialId );
	if( materialIterator != MaxToSGMapping.end() )
	{
		return materialIterator->second;
	}
	else
	{
		return 0;
	}
}

MaxMaterialMap::MaxMaterialMap()
{
	this->NumSubMaterials = 0;
	this->NumActiveMaterials = 0;
	this->sgMaterialId = "";
	this->mMaxMaterialHandle = Animatable::kInvalidAnimHandle;
}
