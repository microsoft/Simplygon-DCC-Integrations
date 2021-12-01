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

// note: this function's purpose is to allow manual population of the material map,
// currently used for "AllowUnsafeImport".
void MaxMaterialMap::AddSubMaterialMapping( int first, int second )
{
	this->MaxToSGMapping.insert( std::pair<int, int>( first, second ) );
	this->SGToMaxMapping.insert( std::pair<int, int>( second, first ) );

	// as material index starts at zero, add one to reach numSubMaterials.
	// numSubMaterials should be equal or larger than numActiveMaterials.
	// sub material indices can be sparse so we should store largest found material index +1.
	this->NumSubMaterials = ( first + 1 ) > this->NumSubMaterials ? ( first + 1 ) : this->NumSubMaterials;
	this->NumActiveMaterials = this->MaxToSGMapping.size();
}

// note: this function's purpose is to allow manual population of the material map,
// currently used for "AllowUnsafeImport".
MaxMaterialMap::MaxMaterialMap( ULONG uniqueHandle, std::basic_string<TCHAR> materialName, std::string materialId )
    : MaxMaterialMap()
{
	this->mMaxMaterialHandle = uniqueHandle;
	this->sgMaterialName = materialName;
	this->sgMaterialId = materialId;
}

MaxMaterialMap::MaxMaterialMap()
{
	this->NumSubMaterials = 0;
	this->NumActiveMaterials = 0;
	this->sgMaterialId = "";
	this->mMaxMaterialHandle = Animatable::kInvalidAnimHandle;
}