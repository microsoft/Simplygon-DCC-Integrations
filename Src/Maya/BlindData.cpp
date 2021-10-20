// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "PCH.h"
#include "BlindData.h"

BlindDataSet::BlindDataSet()
{
	this->componentType = MFn::kInvalid;
	this->blindDataId = -1;
}

BlindDataSet::~BlindDataSet()
{
	// delete all allocated attribute sets
	while( !this->attributeSets.empty() )
	{
		BlindDataAttributeSet* p = this->attributeSets.back();
		this->attributeSets.pop_back();
		delete p;
	}
}

bool BlindDataSet::SetupBlindDataFromMesh( MFnMesh& mMesh, MFn::Type mComponentType, int blindDataId )
{
	this->componentType = mComponentType;
	this->blindDataId = blindDataId;

	MStringArray mLongNames;
	MStringArray mShortNames;
	MStringArray mFormatNames;

	if( !mMesh.getBlindDataAttrNames( this->blindDataId, mLongNames, mShortNames, mFormatNames ) )
	{
		return false;
	}

	// setup all attribute sets
	for( uint i = 0; i < mFormatNames.length(); ++i )
	{
		this->SetupAttributeSet( mMesh, mShortNames[ i ], mFormatNames[ i ] );
	}

	return true;
}
bool BlindDataSet::GetBlindDataFromMesh( MFnMesh& mMesh )
{
	for( size_t i = 0; i < this->attributeSets.size(); ++i )
	{
		if( !this->attributeSets[ i ]->GetBlindDataFromMesh( mMesh ) )
		{
			return false;
		}
	}
	return true;
}

bool BlindDataSet::ApplyBlindDataToMesh( MFnMesh& mMesh, std::map<rid, rid>& componentMap )
{
	for( size_t i = 0; i < this->attributeSets.size(); ++i )
	{
		if( !this->attributeSets[ i ]->ApplyBlindDataToMesh( mMesh, componentMap ) )
		{
			return false;
		}
	}
	return true;
}

BlindData::Component::Component()
{
	this->componentType = MFn::kInvalid;
}

BlindData::Component::~Component()
{
	while( !this->blindDataSets.empty() )
	{
		BlindDataSet* p = this->blindDataSets.back();
		this->blindDataSets.pop_back();
		delete p;
	}
}

bool BlindData::Component::SetupBlindDataFromMesh( MFnMesh& mMesh, MFn::Type mComponentType )
{
	this->componentType = mComponentType;

	MIntArray blindDataIds;
	mMesh.getBlindDataTypes( this->componentType, blindDataIds );

	for( uint i = 0; i < blindDataIds.length(); ++i )
	{
		const int blindId = blindDataIds[ i ];
		BlindDataSet* p = new BlindDataSet();
		blindDataSets.push_back( p );

		if( !p->SetupBlindDataFromMesh( mMesh, this->componentType, blindId ) )
		{
			return false;
		}
	}

	return true;
}

bool BlindData::Component::GetBlindDataFromMesh( MFnMesh& mMesh )
{
	for( size_t i = 0; i < this->blindDataSets.size(); ++i )
	{
		if( !this->blindDataSets[ i ]->GetBlindDataFromMesh( mMesh ) )
		{
			return false;
		}
	}

	return true;
}

bool BlindData::Component::ApplyBlindDataToMesh( MFnMesh& mMesh, std::map<rid, rid>& componentMap )
{
	for( size_t i = 0; i < this->blindDataSets.size(); ++i )
	{
		if( !this->blindDataSets[ i ]->ApplyBlindDataToMesh( mMesh, componentMap ) )
		{
			return false;
		}
	}

	return true;
}

bool BlindData::SetupBlindDataFromMesh( MFnMesh& mMesh )
{
	if( !this->vertexData.SetupBlindDataFromMesh( mMesh, MFn::kMeshVertComponent ) )
		return false;

	if( !this->triangleData.SetupBlindDataFromMesh( mMesh, MFn::kMeshPolygonComponent ) )
		return false;

	return this->GetBlindDataFromMesh( mMesh );
}

bool BlindData::GetBlindDataFromMesh( MFnMesh& mMesh )
{
	if( !this->vertexData.GetBlindDataFromMesh( mMesh ) )
		return false;

	if( !this->triangleData.GetBlindDataFromMesh( mMesh ) )
		return false;

	return true;
}

bool BlindData::ApplyBlindDataToMesh( MFnMesh& mMesh, std::map<rid, rid>& vertexMap, std::map<rid, rid>& triangleMap )
{
	if( !this->vertexData.ApplyBlindDataToMesh( mMesh, vertexMap ) )
		return false;

	if( !this->triangleData.ApplyBlindDataToMesh( mMesh, triangleMap ) )
		return false;

	return true;
}

// this template class implements blind data sets for specific blind data types
template <class arraytype> class BlindDataAttributeSetImp : public BlindDataAttributeSet
{
	protected:
	arraytype data;

	arraytype remappedData;
	MIntArray remappedComponentIds;

	bool RemapData( std::map<rid, rid>& componentMap )
	{
		remappedData.clear();
		remappedComponentIds.clear();

		for( uint i = 0; i < this->componentIds.length(); ++i )
		{
			// find the original component id, get the remapped id
			const int originalId = this->componentIds[ i ];

			const std::map<rid, rid>::const_iterator& componentMapIterator = componentMap.find( originalId );
			if( componentMapIterator == componentMap.end() )
			{
				// not found, skip this data
				continue;
			}

			const int remappedId = componentMapIterator->second;

			// we have a value, apped the component id and value to the arrays
			remappedComponentIds.append( remappedId );
			remappedData.append( this->data[ i ] );
		}

		return true;
	}
};

// function that retrieves/applies int blind data
class BlindDataAttributeSetInt : public BlindDataAttributeSetImp<MIntArray>
{
	public:
	bool GetBlindDataFromMesh( MFnMesh& mMesh ) override
	{
		return mMesh.getIntBlindData( this->componentType, this->blindDataId, this->shortName, this->componentIds, this->data );
	}

	bool ApplyBlindDataToMesh( MFnMesh& mMesh, std::map<rid, rid>& componentMap ) override
	{
		this->RemapData( componentMap );
		return mMesh.setIntBlindData( this->remappedComponentIds, this->componentType, this->blindDataId, this->shortName, this->remappedData );
	}
};

// function that retrieves/applies float blind data
class BlindDataAttributeSetFloat : public BlindDataAttributeSetImp<MFloatArray>
{
	public:
	bool GetBlindDataFromMesh( MFnMesh& mMesh ) override
	{
		return mMesh.getFloatBlindData( this->componentType, this->blindDataId, this->shortName, this->componentIds, this->data );
	}

	bool ApplyBlindDataToMesh( MFnMesh& mMesh, std::map<rid, rid>& componentMap ) override
	{
		this->RemapData( componentMap );
		return mMesh.setFloatBlindData( this->remappedComponentIds, this->componentType, this->blindDataId, this->shortName, this->remappedData );
	}
};

// function that retrieves/applies double blind data
class BlindDataAttributeSetDouble : public BlindDataAttributeSetImp<MDoubleArray>
{
	public:
	bool GetBlindDataFromMesh( MFnMesh& mMesh ) override
	{
		return mMesh.getDoubleBlindData( this->componentType, this->blindDataId, this->shortName, this->componentIds, this->data );
	}

	bool ApplyBlindDataToMesh( MFnMesh& mMesh, std::map<rid, rid>& componentMap ) override
	{
		this->RemapData( componentMap );
		return mMesh.setDoubleBlindData( this->remappedComponentIds, this->componentType, this->blindDataId, this->shortName, this->remappedData );
	}
};

// function that retrieves/applies MString blind data
class BlindDataAttributeSetString : public BlindDataAttributeSetImp<MStringArray>
{
	public:
	bool GetBlindDataFromMesh( MFnMesh& mMesh ) override
	{
		return mMesh.getStringBlindData( this->componentType, this->blindDataId, this->shortName, this->componentIds, this->data );
	}

	bool ApplyBlindDataToMesh( MFnMesh& mMesh, std::map<rid, rid>& componentMap ) override
	{
		this->RemapData( componentMap );
		return mMesh.setStringBlindData( this->remappedComponentIds, this->componentType, this->blindDataId, this->shortName, this->remappedData );
	}
};

// function that retrieves/applies binary blind data
class BlindDataAttributeSetBinary : public BlindDataAttributeSetImp<MStringArray>
{
	public:
	bool GetBlindDataFromMesh( MFnMesh& mMesh ) override
	{
		return mMesh.getBinaryBlindData( this->componentType, this->blindDataId, this->shortName, this->componentIds, this->data );
	}

	bool ApplyBlindDataToMesh( MFnMesh& mMesh, std::map<rid, rid>& componentMap ) override
	{
		this->RemapData( componentMap );
		return mMesh.setBinaryBlindData( this->remappedComponentIds, this->componentType, this->blindDataId, this->shortName, this->remappedData );
	}
};

void BlindDataSet::SetupAttributeSet( MFnMesh& mMesh, MString mShortName, MString mFormatName )
{
	BlindDataAttributeSet* attributeSet = nullptr;

	if( mFormatName == "int" )
		attributeSet = new BlindDataAttributeSetInt();
	else if( mFormatName == "float" )
		attributeSet = new BlindDataAttributeSetFloat();
	else if( mFormatName == "double" )
		attributeSet = new BlindDataAttributeSetDouble();
	else if( mFormatName == "string" )
		attributeSet = new BlindDataAttributeSetString();
	else if( mFormatName == "binary" )
		attributeSet = new BlindDataAttributeSetBinary();

	// if we have created an attribute set, add it into the list of attributes
	if( attributeSet != nullptr )
	{
		this->attributeSets.push_back( attributeSet );

		attributeSet->blindDataId = this->blindDataId;
		attributeSet->componentType = this->componentType;
		attributeSet->shortName = mShortName;
		attributeSet->formatName = mFormatName;
	}
}
