// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

class SimplygonCmd;

class StandardMaterialChannel
{
	public:
	MString TexturePath;
	MString UVSet;
	bool IsSRGB;

	StandardMaterialChannel()
	    : StandardMaterialChannel( true )
	{
	}

	StandardMaterialChannel( bool bIsSRGB )
	    : StandardMaterialChannel( "", "map1", bIsSRGB )
	{
	}

	StandardMaterialChannel( MString mTexturePath, MString mUVSet, bool bIsSRGB )
	{
		this->TexturePath = mTexturePath;
		this->UVSet = mUVSet;
		this->IsSRGB = bIsSRGB;
	}
};

class StandardMaterial
{
	private:
	spTextureTable sgTextureTable;
	std::vector<std::pair<std::string, std::string>> UvToTextureNodeMap;
	SimplygonCmd* cmd;

	public:
	MString NodeName;

	MObject ShaderObject;
	MString ShaderName;

	MObject ShaderGroupObject;
	MString ShaderGroupName;

	spMaterial sgMaterial;

	StandardMaterialChannel* AmbientChannel;
	StandardMaterialChannel* ColorChannel;
	StandardMaterialChannel* SpecularChannel;
	StandardMaterialChannel* NormalCameraChannel;
	StandardMaterialChannel* TransparencyChannel;
	StandardMaterialChannel* TranslucenceChannel;
	StandardMaterialChannel* TranslucenceDepthChannel;
	StandardMaterialChannel* TranslucenceFocusChannel;
	StandardMaterialChannel* IncandescenceChannel;

	MStatus CreatePhong( MDagPath mShape, MString mMeshName, MString mMaterialNameOverride, bool sentFromMeshLOD = false );
	MStatus
	ImportMaterialTextureFile( const char* cTextureType, StandardMaterialChannel* materialChannel, MString mMeshNameOverride, MString mMaterialNameOverride );
	MStatus ExtractMapping( MDagPath mShape );
	MStatus ImportMapping( MDagPath mShape );

	StandardMaterial( SimplygonCmd* cmd, spTextureTable sgTextureTable );
	~StandardMaterial();
};
