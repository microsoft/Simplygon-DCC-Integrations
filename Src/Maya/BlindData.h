// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _BLINDDATA_H_
#define _BLINDDATA_H_

class BlindDataAttributeSet
{
	public:
	BlindDataAttributeSet()
	{
		this->blindDataId = 0;
		this->componentType = MFn::kInvalid;
	};
	virtual ~BlindDataAttributeSet(){};

	int blindDataId;  // the blind data id of this set
	MString shortName;  // attribute name of set, short name
	MString formatName; // name of the format of the values

	MFn::Type componentType; // the component type of this set
	MIntArray componentIds;        // the component ids of this set

	// retrieves the blind data values from the mesh
	virtual bool GetBlindDataFromMesh( MFnMesh& mMesh ) = 0;

	// applies the blind data values to a specified mesh, using the
	// remapping supplied, which maps from the original component id
	// to the component id on the mesh
	virtual bool ApplyBlindDataToMesh( MFnMesh& mMesh, std::map<rid, rid>& componentMap ) = 0;
};

// a set of blind data for a specific blind data id, on a specific component type
class BlindDataSet
{
	private:
	MFn::Type componentType; // the type of component this set is for
	int blindDataId;        // the id of this blind data

	// the attributes of this blind data set
	std::vector<BlindDataAttributeSet*> attributeSets;

	// allocates and adds an attribute set for a named attribute format
	void SetupAttributeSet( MFnMesh& mMesh, MString mShortName, MString mFormatName );

	public:
	BlindDataSet();
	~BlindDataSet();

	// sets up all attributes
	bool SetupBlindDataFromMesh( MFnMesh& mMesh, MFn::Type mComponentType, int blindDataId );

	// gets the blind data from the mesh
	bool GetBlindDataFromMesh( MFnMesh& mMesh );

	// applies the blind data set on a mesh, and uses the component_mapping
	// to apply the data to the components of the mesh.
	bool ApplyBlindDataToMesh( MFnMesh& mMesh, std::map<rid, rid>& componentMap );
};

// handler of all blind data for all component types on a mesh
class BlindData
{
	private:
	class Component
	{
		public:
		Component();
		~Component();

		MFn::Type componentType; // the component type
		std::vector<BlindDataSet*> blindDataSets;

		bool SetupBlindDataFromMesh( MFnMesh& mMesh, MFn::Type mComponentType );
		bool GetBlindDataFromMesh( MFnMesh& mMesh );
		bool ApplyBlindDataToMesh( MFnMesh& mMesh, std::map<rid, rid>& componentMap );
	};

	Component vertexData;
	Component triangleData;

	bool GetBlindDataFromMesh( MFnMesh& mMesh );

	public:
	bool SetupBlindDataFromMesh( MFnMesh& mMesh );
	bool ApplyBlindDataToMesh( MFnMesh& mMesh, std::map<rid, rid>& vertexMap, std::map<rid, rid>& triangleMap );
};

#endif _BLINDDATA_H_