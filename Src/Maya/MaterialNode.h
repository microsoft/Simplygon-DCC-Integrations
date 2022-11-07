// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <maya/MGlobal.h>
#include <maya/MString.h>
#include <maya/MDagPath.h>
#include <maya/MFnDependencyNode.h>

class MeshNode;
class SimplygonCmd;
class WorkDirectoryHandler;
class StandardMaterial;

class ShadingPerChannelData
{
	public:
	std::map<std::basic_string<TCHAR>, Simplygon::spShadingTextureNode> TextureNodeLookup;
	std::map<std::basic_string<TCHAR>, Simplygon::spShadingColorNode> ColorNodeLookup;
	Simplygon::spShadingNode sgExitNode;

	ShadingPerChannelData() {}

	~ShadingPerChannelData()
	{
		TextureNodeLookup.clear();
		ColorNodeLookup.clear();
	}
};

class ShadingNetworkData
{
	public:
	std::map<std::basic_string<TCHAR>, ShadingPerChannelData*> ChannelToShadingNetworkMap;
	Simplygon::spMaterial sgMaterial;

	ShadingNetworkData() {}

	~ShadingNetworkData() { ChannelToShadingNetworkMap.clear(); }
};

class TextureShapeUVLinkage
{
	public:
	MObject Node;  // the node this applies to
	MString UVSet; // the uv set to use for this node
};

class TextureProperties
{
	public:
	TextureProperties()
	    : SRGB( true )
	    , HasTangentSpaceNormals( true )
	{
		this->ColorGain[ 0 ] = this->ColorGain[ 1 ] = this->ColorGain[ 2 ] = this->ColorGain[ 3 ] = 1.f;
		this->RepeatUV[ 0 ] = this->RepeatUV[ 1 ] = 1.f;
		this->OffsetUV[ 0 ] = this->OffsetUV[ 1 ] = 0.f;
	};

	TextureProperties( const TextureProperties& other )
	{
		this->TextureFileName = other.TextureFileName;
		this->OriginalTextureFileName = other.OriginalTextureFileName;
		this->TextureUVLinkage = other.TextureUVLinkage;
		this->HasTangentSpaceNormals = other.HasTangentSpaceNormals;

		this->ColorGain[ 0 ] = other.ColorGain[ 0 ];
		this->ColorGain[ 1 ] = other.ColorGain[ 1 ];
		this->ColorGain[ 2 ] = other.ColorGain[ 2 ];
		this->ColorGain[ 3 ] = other.ColorGain[ 3 ];
		this->RepeatUV[ 0 ] = other.RepeatUV[ 0 ];
		this->RepeatUV[ 1 ] = other.RepeatUV[ 1 ];
		this->OffsetUV[ 0 ] = other.OffsetUV[ 0 ];
		this->OffsetUV[ 1 ] = other.OffsetUV[ 1 ];
		this->SRGB = other.SRGB;
	}

	MString TextureFileName;
	MString OriginalTextureFileName;
	std::vector<TextureShapeUVLinkage> TextureUVLinkage;
	bool HasTangentSpaceNormals; // only used for normal textures
	real ColorGain[ 4 ];
	real RepeatUV[ 2 ];
	real OffsetUV[ 2 ];
	bool SRGB;
};

class MaterialTextureLayer : public TextureProperties
{
	public:
	int BlendType;
	real LayerAlpha;
	TextureProperties* AlphaTexture;

	public:
	MaterialTextureLayer()
	    : TextureProperties()
	    , BlendType( 0 )
	    , LayerAlpha( 1.f )
	    , AlphaTexture( nullptr )
	{
	}

	MaterialTextureLayer( const MaterialTextureLayer& other )
	{
		this->BlendType = other.BlendType;
		this->LayerAlpha = other.LayerAlpha;
		*this->AlphaTexture = *other.AlphaTexture;
	}
	~MaterialTextureLayer()
	{
		if( this->AlphaTexture )
			delete this->AlphaTexture;
	}
};

class MaterialTextures
{
	public:
	MString MappingChannelName;
	std::vector<MaterialTextureLayer> TextureLayers;
};

class MaterialColor
{
	public:
	real ColorValue[ 4 ];
	MaterialColor()
	{
		this->ColorValue[ 0 ] = 0.f;
		this->ColorValue[ 1 ] = 0.f;
		this->ColorValue[ 2 ] = 0.f;
		this->ColorValue[ 3 ] = 0.f;
	}
};

class MaterialTextureOverride
{
	public:
	MString MaterialName;
	MString TextureType;
	MString TextureName;
	int TextureLayer;
	int BlendType;
	bool HasTangentSpaceNormals;

	MaterialTextureOverride()
	{
		this->TextureLayer = 0;
		this->BlendType = 0;
		this->HasTangentSpaceNormals = true;
	}
};

class MaterialColorOverride
{
	public:
	MString MaterialName;
	MString ColorType;
	real ColorValue[ 4 ];

	MaterialColorOverride()
	{
		this->ColorValue[ 0 ] = 0.f;
		this->ColorValue[ 1 ] = 0.f;
		this->ColorValue[ 2 ] = 0.f;
		this->ColorValue[ 3 ] = 0.f;
	}
};

class TextureShapeUVLinkageOverride
{
	public:
	MString Node;        // the node this applies to
	MString UVSet;       // the uv set to use for this node
	MString TextureName; // the texture filename this applies to
};

class MaterialTextureMapChannelOverride
{
	public:
	MString MaterialName;
	MString MappingChannelName;
	MString NamedMappingChannel;
	int Layer;
	int MappingChannel;

	MaterialTextureMapChannelOverride()
	{
		this->Layer = 0;
		this->MappingChannel = 0;
	}
};

class MaterialHandler;

class MaterialNode
{
	public:
	MaterialNode( SimplygonCmd* cmd, MaterialHandler* materialHandler );
	~MaterialNode();

	spShadingNode FindUpstreamNode( spShadingNode sgNode, const char* cNodeName );

	// Setup from a named material
	MStatus SetupFromName( MString mMaterialName );

	void HandleMaterialOverride();

	bool MaterialChannelHasShadingNetwork( const char* cChannelName );
	void CreateAndAssignColorNode( const char* cChannelName, float v );
	void CreateAndAssignColorNode( const char* cChannelName, float r, float g, float b, float a );
	void CreateAndAssignColorNode( const char* cChannelName, const float colors[ 4 ] );

	void CreateSgMaterialChannel( const char* cChannelName, MeshNode* meshNode, const MaterialTextures& textures, bool& hasTextures, bool& sRGB );

	// Setup a simplygon material from this maya material and a mesh shape to apply it on
	std::string GetSimplygonMaterialForShape( MeshNode* meshNode );
	std::string GetSimplygonMaterialWithShadingNetwork( MString mMaterialName, MeshNode* meshNode );

	MaterialColorOverride* GetMaterialColorOverrideForChannel( std::string sMaterialName, std::string sChannelName );

	// accessors
	// spMaterial GetMaterial() { return this->mat; }
	const MString& GetShadingGroupName() { return this->Name; }
	const MString& GetShadingNodeName() { return this->ShadingNodeName; }
	MObject GetMaterialObject() { return this->MaterialObject; }
	// void HookMayaMaterialToShadingNode(MFnDependencyNode ShaderNodeFn);

	bool IsBasedOnSimplygonShadingNetwork;
	// std::vector<spTexture> Textures;

	ShadingNetworkData* shadingNetworkData;
	std::map<std::string, int> map_sgguid_to_sg; // map from sg to maya material id
	std::map<spShadingTextureNode, std::basic_string<TCHAR>> ShadingTextureNodeToPath;
	std::map<std::string, spShadingNode> ChannelToExitNodeMapping;

	spMaterial sgMaterial;

	protected:
	MStatus InternalSetup();
	MStatus InternalSetupConnectNetworkNodes();

	MObject GetConnectedNamedPlug( const MFnDependencyNode& mDependencyNode, MString mPlugName );

	MString Name;
	MString ShadingNodeName;
	MObject MaterialObject;

	// material data
	std::vector<MaterialTextures> UserTextures;

	// Multi-layered material texture
	MaterialTextures AmbientTextures;
	MaterialTextures ColorTextures;
	MaterialTextures SpecularColorTextures;
	MaterialTextures TransparencyTextures;
	MaterialTextures TranslucenceTextures;
	MaterialTextures TranslucenceDepthTextures;
	MaterialTextures TranslucenceFocusTextures;
	MaterialTextures IncandescenceTextures;
	MaterialTextures NormalCameraTextures;
	MaterialTextures ReflectedColorTextures;

	MaterialColor AmbientValue;
	MaterialColor ColorValue;
	MaterialColor SpecularValue;
	MaterialColor TransparencyValue;
	MaterialColor TranslucenceValue;
	MaterialColor TranslucenceDepthValue;
	MaterialColor TranslucenceFocusValue;
	MaterialColor IncandescenceValue;
	MaterialColor ReflectedColorValue;

	MaterialHandler* materialHandler;
	SimplygonCmd* cmd;

	bool SetMaterialColor( MaterialColor& materialColor, real r, real g, real b, real a = 1.f );
	bool GetFileTexture( MObject mNode, MaterialTextureLayer& textureLayer );
	void PopulateLayeredTextureProperties( const MFnDependencyNode& mDependencyNode, const MPlug& mMultiLayeredChildPlug, TextureProperties* textureLayer );
	void PopulateTextureProperties( const MFnDependencyNode& mDependencyNode, const MPlug& mMultiLayeredChildPlug, TextureProperties* textureLayer );
	bool GetFileTexture( MObject mNode, MaterialTextures& textures );
	void AddTextureToSimplygonScene( std::basic_string<TCHAR> tTextureFileName );
	void SetMaterialTextureForMeshNode( std::string sgMaterialChannel, MeshNode* meshNode, const MaterialTextures& textures, bool& hasTextures, bool& SRGB );
	friend class MaterialHandler;

	public:
	// Maya Blend options
	static const int MAYA_BLEND_NONE = 0;
	static const int MAYA_BLEND_OVER = 1;
	static const int MAYA_BLEND_IN = 2;
	static const int MAYA_BLEND_OUT = 3;
	static const int MAYA_BLEND_ADD = 4;
	static const int MAYA_BLEND_SUBTRACT = 5;
	static const int MAYA_BLEND_MULTIPLY = 6;
	static const int MAYA_BLEND_DIFFERENCE = 7;
	static const int MAYA_BLEND_LIGHTEN = 8;
	static const int MAYA_BLEND_DARKEN = 9;
	static const int MAYA_BLEND_SATURATE = 10;
	static const int MAYA_BLEND_DESATURATE = 11;
	static const int MAYA_BLEND_ILLUMINATE = 12;
};

class ImportedTexture
{
	public:
	MString OriginalPath;
	std::string ImportedPath;
};

class MaterialHandler
{
	private:
	std::map<std::basic_string<TCHAR>, ShadingNetworkData*> ChannelToShadingNetworkDataMap;

	// the list of all materials
	std::vector<MaterialNode*> MaterialNodes;

	// the simplygon material table
	spMaterialTable sgMaterialTable;

	// the simplygon texture table
	spTextureTable sgTextureTable;

	// imported textures
	std::vector<ImportedTexture> ImportedTextures;

	// added overrides
	std::vector<MaterialColorOverride> MaterialColorOverrides;
	std::vector<MaterialTextureOverride> MaterialTextureOverrides;
	std::vector<TextureShapeUVLinkageOverride> TextureShapeUVLinkageOverrides;
	std::vector<MaterialTextureMapChannelOverride> MaterialTextureMapChannelOverrides;

	SimplygonCmd* cmd;

	public:
	MaterialHandler( SimplygonCmd* _cmd );
	~MaterialHandler();

	std::map<std::string, StandardMaterial*> MaterialIdToStandardMaterial;
	std::map<std::string, MaterialNode*> MaterialIdToMaterialNode;
	std::map<std::string, int> MaterialIdToMaterialIndex;

	// setup the material handler
	void Setup( spMaterialTable sgMaterialTable, spTextureTable sgTextureTable );

	// adds a material to the handler, and extracts any data it can find
	// in the scene. if the material already exists, no new material is added,
	// the existing material is returned
	MaterialNode* AddMaterial( MString mMaterialName );

	// retrieves a material
	MaterialNode* GetMaterial( const MString& mMaterialName );

	spTextureTable GetTextureTable();
	spMaterialTable GetMaterialTable();

	void AddMaterialWithShadingNetworks( std::basic_string<TCHAR> tMaterialName );
	ShadingNetworkData* GetMaterialWithShadingNetworks( std::basic_string<TCHAR> tMaterialName );
	void
	SetupMaterialChannelNetworkFromXML( std::basic_string<TCHAR> tMaterialName, std::basic_string<TCHAR> tChannelName, std::basic_string<TCHAR> tXMLString );
	bool HasMaterialWithXMLNetworks( std::basic_string<TCHAR> tMaterialName );

	// gets a simplygon material id from the handler, that is setup to match
	// the shape that is the parameter
	std::string GetSimplygonMaterialForShape( MString mMaterialName, MeshNode* meshNode );

	// gets the maya material that a simplygon material id points at
	MaterialNode* GetMaterialFromSimplygonMaterialId( std::string sMaterialId );

	// imports a texture into the wdh folder, and returns the imported name
	std::string ImportTexture( const MString& mFilePath );

	// adds an override into the handler
	void AddMaterialColorOverride( MString mMaterialName, MString mColorType, float r, float g, float b, float a );
	void AddMaterialTextureOverride(
	    MString mMaterialName, MString mTextureType, MString mTextureName, int Layer = 0, int BlendType = 0, bool isTangentSpace = true );
	void AddMaterialTextureChannelOverride( MString mMaterialName, MString mTextureType, int Layer, int Channel );
	void AddMaterialTextureNamedChannelOverride( MString mMaterialName, MString mTextureType, int Layer, MString mChannel );
	void AddTextureShapeUVLinkageOverride( MString mNode, MString mUVSet, MString mTextureName );
	void AddBoneLockOverride( MString mBoneName, bool Lock );
	void AddBoneRemoveOverride( MString mBoneName, bool removeBonesWithThisNameAsStartingSubName, bool removeBonesWithThisNameAsEndingSubName );

	rid FindUVSetIndex( MObject mShapeObj, std::vector<MString>& mUVSets, const TextureProperties& textureLayer );

	std::vector<MaterialColorOverride>& GetMaterialColorOverrides() { return MaterialColorOverrides; }
	std::vector<MaterialTextureOverride>& GetMaterialTextureOverrides() { return MaterialTextureOverrides; }
	std::vector<MaterialTextureMapChannelOverride>& GetMaterialChannelOverrides() { return MaterialTextureMapChannelOverrides; }
	std::vector<TextureShapeUVLinkageOverride>& GetTextureShapeUVLinkageOverrides() { return TextureShapeUVLinkageOverrides; }

	void FindAllUpStreamTextureNodes( spShadingNode sgNode, std::map<std::basic_string<TCHAR>, spShadingTextureNode>& sgNodes );
	void FindAllUpStreamColorNodes( spShadingNode sgNode, std::map<std::basic_string<TCHAR>, spShadingColorNode>& sgNodes );
	void FindAllUpStreamVertexColorNodes( spShadingNode sgNode, std::map<std::basic_string<TCHAR>, spShadingVertexColorNode>& sgNodes );
};