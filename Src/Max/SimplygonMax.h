// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef __SIMPLYGONMAX_H__
#define __SIMPLYGONMAX_H__

#include "SimplygonLoader.h"
#include "HelperFunctions.h"
#include "Common.h"
#include "CriticalSection.h"
#include "SimplygonInit.h"

class SimplygonMax;
class SimplygonMaxPerVertexData;
class SimplygonMaxMapData;

class MaterialInfoHandler;
class MaxMaterialMap;

class MaterialColorOverride;
class MaterialTextureOverride;
class MaterialTextureMapChannelOverride;

class ImportedTexture;
class MaterialInfo;

class Scene;
class MeshNode;

class IDxMaterial3;
class INamedSelectionSetManager;
class IGameModifier;

// Simplygon baked vertex colors
#define DEFAULT_VERTEXBAKED_AMBIENT_CHANNEL_SG  252
#define DEFAULT_VERTEXBAKED_DIFFUSE_CHANNEL_SG  253
#define DEFAULT_VERTEXBAKED_SPECUALR_CHANNEL_SG 254
#define DEFAULT_VERTEXBAKED_OPACITY_CHANNEL_SG  255

// max default map channels for vertex colors
#define DEFAULT_VERTEXBAKED_AMBIENT_CHANNEL_MAX  2
#define DEFAULT_VERTEXBAKED_DIFFUSE_CHANNEL_MAX  0 // the default vertex color channel in max
#define DEFAULT_VERTEXBAKED_SPECUALR_CHANNEL_MAX 3
#define DEFAULT_VERTEXBAKED_OPACITY_CHANNEL_MAX  -2 // should be MAX_ALPHA

#define sg_createnode_declare( varname ) int SimplygonMax::Create##varname( std::basic_string<TCHAR> name );

enum ErrorType
{
	Info = 0,
	Warning,
	Error
};

enum ExtractionType
{
	BATCH_PROCESSOR = 0,
	IMPORT_FROM_FILE,
	EXPORT_TO_FILE,
	PROCESS_FROM_FILE,
	NONE
};

enum NodeAttributeDataType
{
	nat_None,
	nat_Int,
	nat_Float,
	nat_Bool

};

enum NodeAttributeType
{
	UnknownAttr = 0,
	TileU = 1,
	TileV = 2,
	UVChannel = 3,
	TileUV = 4,
	OffsetU = 5,
	OffsetV = 6,
	OffsetUV = 7
};

#pragma region ATTRIBUTEDATA
class AttributeData
{
	public:
	void* Data;
	float FloatData;
	float IntData;
	bool BoolData;
	NodeAttributeDataType DataType;
	int NodeAttrType;
	int NodeId;
	AttributeData( int nodeId )
	{
		this->NodeId = nodeId;
		this->Data = nullptr;
		this->DataType = nat_None;
		this->NodeAttrType = UnknownAttr;
		this->FloatData = 0.0f;
		this->IntData = 0;
		this->BoolData = false;
	}

	AttributeData( int nodeId, int attributeType )
	    : AttributeData( nodeId )
	{
		this->NodeAttrType = attributeType;
	}

	~AttributeData()
	{
		if( this->Data != nullptr )
		{
			delete Data;
		}
	}
};
#pragma endregion

enum NodeProxyType
{
	ShadingTextureNode,
	ShadingInterpolateNode,
	ShadingAddNode,
	ShadingSubtractNode,
	ShadingMultiplyNode,
	ShadingDivideNode,
	ShadingClampNode,
	ShadingVertexColorNode,
	ShadingColorNode,
	ShadingSwizzlingNode,
	ShadingLayeredBlendNode,
	ShadingPowNode,
	ShadingStepNode,
	ShadingNormalize3Node,
	ShadingSqrtNode,
	ShadingDot3Node,
	ShadingCross3Node,
	ShadingCosNode,
	ShadingSinNode,
	ShadingMaxNode,
	ShadingMinNode,
	ShadingEqualNode,
	ShadingNotEqualNode,
	ShadingGreaterThanNode,
	ShadingLessThanNode,
	ShadingGeometryFieldNode,
	ShadingCustomNode
};

#pragma region FCOLOR
class fColor
{
	public:
	float r;
	float g;
	float b;
	float a;

	fColor( float r, float g, float b, float a )
	{
		this->r = r;
		this->g = g;
		this->b = b;
		this->a = a;
	}
	~fColor()
	{
		this->r = 0.f;
		this->g = 0.f;
		this->b = 0.f;
		this->a = 0.f;
	}
};
#pragma endregion

enum MaxMaterialType
{
	StandardMax,
	DX11Shader,
	Custom
};

#pragma region SHADINGNETWORKPROXY
class ShadingNetworkProxy
{
	private:
	MaxSDK::AssetManagement::AssetUser MaxEffectFile;
	Mtl* MaxMaterial;
	std::basic_string<TCHAR> MaterialName; // name of the material

	bool IsMtlRefSet;
	bool IsTangentSpace;

	public:
	std::map<std::basic_string<TCHAR>, int> ShadingNodeToSGChannel; // list of output nodes to connect to corresponding sg channels
	std::map<std::basic_string<TCHAR>, std::basic_string<TCHAR>> SGChannelToShadingNode;
	ShadingNetworkProxy( std::basic_string<TCHAR> tMaterialName, MaxMaterialType materialType )
	{
		this->MaterialName = tMaterialName;
		this->IsMtlRefSet = false;
		this->IsTangentSpace = true;
		this->MaxMaterial = nullptr;
	}

	~ShadingNetworkProxy()
	{
		this->ShadingNodeToSGChannel.clear();
		this->SGChannelToShadingNode.clear();
	}

	std::basic_string<TCHAR> GetName() { return this->MaterialName; }
	bool GetUseTangentSpaceNormals() { return this->IsTangentSpace; }

	void SetMaxMaterialRef( Mtl* mMaxMaterial )
	{
		this->IsMtlRefSet = true;
		this->MaxMaterial = mMaxMaterial;
	}

	void SetDxMaterialFile( MaxSDK::AssetManagement::AssetUser mEffectFile ) { this->MaxEffectFile = mEffectFile; }

	void SetUseTangentSpaceNormals( bool bTangentSpace ) { this->IsTangentSpace = bTangentSpace; }

	MaxSDK::AssetManagement::AssetUser* GetDxMaterialFile() { return &this->MaxEffectFile; }

	Mtl* GetMaxMaterialRef() { return this->MaxMaterial; }
};

class ShadingNetworkProxyWriteBack
{
	private:
	std::basic_string<TCHAR> tEffectFilePath; // name of the material

	public:
	std::map<std::basic_string<TCHAR>, int> ShadingNodeToSGChannel; // list of output nodes to connect to corresponding sg channels
	std::map<std::basic_string<TCHAR>, std::basic_string<TCHAR>> SGChannelToShadingNode;
	ShadingNetworkProxyWriteBack( std::basic_string<TCHAR> tMaterialName ) { this->tEffectFilePath = tMaterialName; }

	~ShadingNetworkProxyWriteBack()
	{
		this->ShadingNodeToSGChannel.clear();
		this->SGChannelToShadingNode.clear();
	}

	std::basic_string<TCHAR> GetEffectFilePath() { return this->tEffectFilePath; }
};
#pragma endregion

class NodeProxy
{
	private:
	bool IsNodeInitialized;

	public:
	int MaterialId;
	int VertexColorChannel; // is only valid for VertexColorNode
	int UVOverride;         // is only for TextureNode
	bool uTilingOverride;
	bool vTilingOverride;
	float uTiling;
	float vTiling;
	bool uOffsetOverride;
	bool vOffsetOverride;
	float uOffset;
	float vOffset;
	bool isSRGB;
	bool isSRGBOverride;

	Simplygon::spShadingNode ShadingExitNode;
	NodeProxyType NodeType;
	std::basic_string<TCHAR> NodeName;
	std::map<std::basic_string<TCHAR>, AttributeData*> Attributes;

	std::vector<fColor*> Parameters;
	std::vector<bool> UseDefaultParameterInput;
	std::vector<int> ChildNodes;
	std::vector<int> ChannelSwizzleIndices;

	std::basic_string<TCHAR> GeometryFieldName;
	int GeometryFieldIndex;
	int GeometryFieldType;

	NodeProxy( std::basic_string<TCHAR> tNodeName, NodeProxyType nodeType )
	{
		this->isSRGB = true;
		this->isSRGBOverride = false;
		this->UVOverride = -1;

		this->uTilingOverride = false;
		this->vTilingOverride = false;
		this->uOffsetOverride = false;
		this->vOffsetOverride = false;
		this->uTiling = 1.f;
		this->vTiling = 1.f;

		this->uOffset = 0.f;
		this->vOffset = 0.f;

		this->MaterialId = -1;
		this->NodeName = tNodeName;
		this->NodeType = nodeType;
		this->Parameters.reserve( 4 );
		this->UseDefaultParameterInput.reserve( 4 );
		this->ChildNodes.reserve( 4 );
		this->ChannelSwizzleIndices.reserve( 4 );
		this->VertexColorChannel = -1;
		this->GeometryFieldIndex = -1;
		this->GeometryFieldType = -1;

		for( uint index = 0; index < 4; ++index )
		{
			fColor* defaultParam = new fColor( 1.0, 1.0, 1.0, 1.0 );
			this->Parameters.push_back( defaultParam );
			this->UseDefaultParameterInput.push_back( false );
			this->ChildNodes.push_back( -1 );
			this->ChannelSwizzleIndices.push_back( index );
		}

		this->IsNodeInitialized = false;
	}

	~NodeProxy()
	{
		this->IsNodeInitialized = false;
		this->Parameters.clear();
		this->UseDefaultParameterInput.clear();
		this->Attributes.clear();
	}

	// setup the reference to Simplygon node
	void SetNode( Simplygon::spShadingNode sgExitNode )
	{
		if( !this->IsNodeInitialized )
		{
			this->ShadingExitNode = sgExitNode;
			this->ShadingExitNode->SetName( LPCTSTRToConstCharPtr( this->NodeName.c_str() ) );

			// setup default parameters
			for( uint index = 0; index < UseDefaultParameterInput.size(); ++index )
			{
				if( this->UseDefaultParameterInput[ index ] )
				{
					this->ShadingExitNode->SetDefaultParameter(
					    index, this->Parameters[ index ]->r, this->Parameters[ index ]->g, this->Parameters[ index ]->b, this->Parameters[ index ]->a );
				}
			}

			this->IsNodeInitialized = true;
		}
	}

	// setup child nodes with relation ship
	bool SetNodeInput( int inputChannel, int nodeIndex )
	{
		if( inputChannel < 4 )
		{
			this->ChildNodes[ inputChannel ] = nodeIndex;
			return true;
		}

		return false;
	}

	bool SetVertexColorChannel( int channel )
	{
		if( this->NodeType == ShadingVertexColorNode )
		{
			this->VertexColorChannel = channel;
			return true;
		}

		return false;
	}

	bool SetChannelSwizzle( int channel, int toChannel )
	{
		this->ChannelSwizzleIndices[ channel ] = toChannel;
		return true;
	}

	bool SetGeometryFieldName( std::basic_string<TCHAR> tGeometryFieldName )
	{
		this->GeometryFieldName = tGeometryFieldName;
		return true;
	}

	bool SetGeometryFieldIndex( int geometryFieldIndex )
	{
		this->GeometryFieldIndex = geometryFieldIndex;
		return true;
	}

	bool SetGeometryFieldType( int geometryFieldType )
	{
		this->GeometryFieldType = geometryFieldType;
		return true;
	}

	bool IsInitialized() { return this->IsNodeInitialized; }

	friend bool operator==( const NodeProxy& nodeA, const NodeProxy& nodeB );
};

static const TCHAR* CLEAR_MAT_PIPELINE[] = { _T("All"), _T("Nodes"), _T("SgToMax"), _T("MaxToSg") };

#pragma region SHADINGPIPELINECLEARINFO
class ShadingPipelineClearInfo
{
	private:
	bool ClearFlag;
	std::basic_string<TCHAR> PartToClear;

	public:
	ShadingPipelineClearInfo() { ClearFlag = false; }

	~ShadingPipelineClearInfo() { ClearFlag = false; }

	void SetClearFlag( bool flag ) { ClearFlag = flag; }

	bool IsClearFlagSet() { return ClearFlag; }

	void SetPartToClear( int index )
	{
		ClearFlag = true;
		PartToClear = std::basic_string<TCHAR>( CLEAR_MAT_PIPELINE[ index ] );
	}

	bool GetClearFlag() { return ClearFlag; }
	const std::basic_string<TCHAR> GetPartToClear() { return PartToClear; }
};
#pragma endregion

#pragma region GLOBAL_MESH_MAP
class GlobalMeshMap
{
	private:
	std::string sgId;
	std::basic_string<TCHAR> name;
	ULONG maxId;

	public:
	GlobalMeshMap( std::string sSgId, std::basic_string<TCHAR> tName, ULONG uMaxId )
	    : sgId( sSgId )
	    , name( tName )
	    , maxId( uMaxId )
	{
	}

	~GlobalMeshMap() {}

	std::string GetSimplygonId() const { return this->sgId; }
	std::basic_string<TCHAR> GetName() const { return this->name; }
	ulong GetMaxId() const { return this->maxId; }
};
#pragma endregion

void FindAllUpStreamTextureNodes( Simplygon::spShadingNode sgShadingNode, std::map<std::basic_string<TCHAR>, Simplygon::spShadingTextureNode>& sgTextureNodes );

class SimplygonMax : public SimplygonEventRelay
{
	public:
	virtual void ProgressCallback( int progress );
	virtual void ErrorCallback( const TCHAR* cErrorMessage );

	void Callback( std::basic_string<TCHAR> tId, bool error, std::basic_string<TCHAR> tMessage, int progress );

	// Which texture coordinates to use: 0-UV 1-UW 2-VW
	SgValueMacro( unsigned int, TextureCoordinateRemapping );

	// If true, run the GUI with a debugger attached
	SgValueMacro( bool, RunDebugger );

	// If true, lock the selected vertices in the meshes
	SgValueMacro( bool, LockSelectedVertices );

	// If true, show a progress window. If false, run in caller thread
	SgValueMacro( bool, ShowProgress );

	// If true, add to undo queue
	SgValueMacro( bool, CanUndo );

	// If true, will use material colors
	SgValueMacro( bool, UseMaterialColors );

	// If false, will use not use non-conflicting texture names
	SgValueMacro( bool, UseNonConflictingTextureNames );

	// If false, will use old material path. Should be deprecated once new material system is fully in place
	SgValueMacro( bool, UseNewMaterialSystem );

	// if true, the plugin will generate a material for the LODs
	SgValueMacro( bool, GenerateMaterial );

	SgValueMacro( int, PipelineRunMode );

	SgValueMacro( std::basic_string<TCHAR>, DefaultPrefix );

	SgValueMacro( std::basic_string<TCHAR>, TextureOutputDirectory );

	SgValueMacro( std::basic_string<TCHAR>, SettingsObjectName );

	// Resets all values to default values
	void Reset();

	// initializes Simplygon SDK and sets up event handlers
	bool Initialize();

	// Reduces the currently selected geometries
	bool ProcessSelectedGeometries();

	// Reduces the scene from the given file
	bool ProcessSceneFromFile( std::basic_string<TCHAR> tImportFilePath, std::basic_string<TCHAR> tExportFilePath );

	// Import scenes
	bool ImportScenes();

	// Exports selected scene to file
	bool ExportSceneToFile( std::basic_string<TCHAR> tExportFilePath );

	// imports scene from file into Max
	bool ImportSceneFromFile( std::basic_string<TCHAR> tImportFilePath );

	// Overrides or adds a color or a texture for specified material name and channel
	bool MaterialColor( const TCHAR* tMaterialName, const TCHAR* tChannelname, float r, float g, float b, float a );
	bool MaterialTexture( const TCHAR* tMaterialName, const TCHAR* tChannelname, const TCHAR* tTextureFileName, const bool bSRGB );
	bool MaterialTextureMapChannel( const TCHAR* tMaterialName, const TCHAR* tChannelname, int mapChannel );

	// Overrides a mapping channel > 2 in Max to be treated as a vertex color
	bool SetIsVertexColorChannel( int maxChannel, BOOL isVertexColor );

	// Connects a node to another node
	bool SetInputNode( int nodeA, int nodeB, int index );
	bool SetVertexColorChannel( int nodeId, int vertexColorChannel );
	bool SetSwizzleChannel( int nodeId, int channel, int toChannel );

	bool SetGeometryFieldName( int nodeId, std::basic_string<TCHAR> geometryFieldName );
	bool SetGeometryFieldIndex( int nodeId, int geometryFieldIndex );
	bool SetGeometryFieldType( int nodeId, int geometryFieldType );

	TSTR GetUniqueNameForLOD( TSTR tLODName, size_t LODIndex );
	TSTR GetUniqueNameForProxy( int LODIndex );
	TSTR GetUniqueMaterialName( TSTR tNodeName );

	Mtl* GetExistingMappedMaterial( std::string sMaterialId );

	Mtl* GetExistingMaterial( std::basic_string<TCHAR> tMaterialName );

	// creates a texture node and returns it's id
	sg_createnode_declare( ShadingTextureNode );
	sg_createnode_declare( ShadingInterpolateNode );
	sg_createnode_declare( ShadingVertexColorNode );
	sg_createnode_declare( ShadingClampNode );
	sg_createnode_declare( ShadingAddNode );
	sg_createnode_declare( ShadingSubtractNode );
	sg_createnode_declare( ShadingDivideNode );
	sg_createnode_declare( ShadingMultiplyNode );
	sg_createnode_declare( ShadingColorNode );
	sg_createnode_declare( ShadingSwizzlingNode );
	sg_createnode_declare( ShadingLayeredBlendNode );
	sg_createnode_declare( ShadingPowNode );
	sg_createnode_declare( ShadingStepNode );
	sg_createnode_declare( ShadingNormalize3Node );
	sg_createnode_declare( ShadingSqrtNode );
	sg_createnode_declare( ShadingDot3Node );
	sg_createnode_declare( ShadingCross3Node );
	sg_createnode_declare( ShadingCosNode );
	sg_createnode_declare( ShadingSinNode );
	sg_createnode_declare( ShadingMaxNode );
	sg_createnode_declare( ShadingMinNode );
	sg_createnode_declare( ShadingEqualNode );
	sg_createnode_declare( ShadingNotEqualNode );
	sg_createnode_declare( ShadingGreaterThanNode );
	sg_createnode_declare( ShadingLessThanNode );
	sg_createnode_declare( ShadingGeometryFieldNode );

	public:
	SimplygonMax();
	virtual ~SimplygonMax();

	ExtractionType extractionType;

	protected:
	Interface* MaxInterface;
	TimeValue CurrentTime;

	uint MaxNumBonesPerVertex;

	size_t SelectedMeshCount;
	std::vector<MeshNode*> SelectedMeshNodes;

	// the bones mapping
	std::map<INode*, std::string> MaxBoneToSgBone;
	std::map<std::string, INode*> SgBoneToMaxBone;
	std::map<std::string, int> SgBoneIdToIndex;

	// node mapping
	std::map<INode*, std::string> MaxSgNodeMap;
	std::map<std::string, INode*> SgMaxNodeMap;

	// vertex color channel override
	std::vector<int> MaxVertexColorOverrides;

	std::map<Mtl*, int> GlobalMaxToSgMaterialMap;
	std::map<std::string, Mtl*> GlobalSgToMaxMaterialMap;

	std::vector<MaxMaterialMap*> GlobalExportedMaterialMap;
	std::vector<MaterialTextureOverride> MaterialTextureOverrides;
	std::vector<MaterialTextureMapChannelOverride> MaterialChannelOverrides;
	std::vector<MaterialColorOverride> MaterialColorOverrides;

	HANDLE UILock;

	// collection of node proxy global table
	std::vector<NodeProxy*> nodeTable; // list of nodes present in the node network
	std::vector<ShadingNetworkProxy*> materialProxyTable;
	std::vector<ShadingNetworkProxyWriteBack*> materialProxyWritebackTable;

	std::map<std::basic_string<TCHAR>, Mtl*> UsedShaderReferences;

	std::map<std::basic_string<TCHAR>, std::basic_string<TCHAR>> sgChannelToMaxMatParam;

	int GetMaterialId( ShadingNetworkProxy* proxyMaterial );

	// gets the attribute data from node proxy table
	AttributeData* GetNodeAttribute( std::basic_string<TCHAR> tAttributeName, ShadingNetworkProxy* materialProxy );

	ShadingPipelineClearInfo ShadingNetworkClearInfo;

	MaterialInfoHandler* materialInfoHandler;

	public:
	// create proxy shading network which is used to setup the sgMaterial shading network
	int CreateProxyShadingNetworkMaterial( std::basic_string<TCHAR> tMaterialName, MaxMaterialType materialType );
	int CreateProxyShadingNetworkWritebackMaterial( std::basic_string<TCHAR> tMaterialName, MaxMaterialType materialType );

	// returns a spShading node object from the proxy node table
	Simplygon::spShadingNode GetSpShadingNodeFromTable( std::basic_string<TCHAR> tTextureName, ShadingNetworkProxy* materialProxy );

	// return a shading network proxy by name
	ShadingNetworkProxy* GetProxyShadingNetworkMaterial( std::basic_string<TCHAR> tMaterialName );

	// return write-back shading network proxy
	ShadingNetworkProxyWriteBack* GetProxyShadingNetworkWritebackMaterial();

	// maps dx-texture to sg texture node
	void SetupSGDXTexture( IDxMaterial3* mDXMaterial, int bitmapIndex, Simplygon::spShadingTextureNode sgTextureNode );

	// map sg texture node to dx-texture
	void SetupMaxDXTexture( Simplygon::spScene sgProcessedScene,
	                        Simplygon::spMaterial sgMaterial,
	                        const char* cChannelName,
	                        Mtl* mMaxmaterial,
	                        IDxMaterial3* mDXMaterial,
	                        std::basic_string<TCHAR> tParameterName,
	                        Simplygon::spShadingTextureNode sgTextureNode,
	                        std::basic_string<TCHAR> tNodeName,
	                        std::basic_string<TCHAR> tMeshNameOverride,
	                        std::basic_string<TCHAR> tMaterialNameOverride );

	// validate material shading network
	void ValidateMaterialShadingNetwork( Simplygon::spMaterial sgMaterial, ShadingNetworkProxy* materialProxy );

	// connect node proxy to sg channel
	bool ConnectRootNodeToChannel( int nodeId, int materialIndex, std::basic_string<TCHAR> tChannelName );

	// set default parameter in node proxy
	bool SetDefaultParameter( int nodeId, int paramId, float r, float g, float b, float a );

	// add extra attribute to the node proxy
	bool AddNodeAttribute( int nodeId, std::basic_string<TCHAR> tAttributeName, int attibuteType );

	// override uv channel for a texture node
	bool SetUV( int nodeId, int set );
	bool SetSRGB( int nodeId, bool isSRGB );

	bool SetUseTangentSpaceNormals( std::basic_string<TCHAR> tMaterialName, bool bTangentSpace );

	// override tiling
	bool SetUVTiling( int nodeId, float u, float v );
	bool SetUTiling( int nodeId, float u );
	bool SetVTiling( int nodeId, float v );

	bool SetUVOffset( int nodeId, float u, float v );
	bool SetUOffset( int nodeId, float u );
	bool SetVOffset( int nodeId, float v );

	// remap sg channel after after casting to a shading network proxy node
	bool ConnectSgChannelToMaterialNode( std::basic_string<TCHAR> tChannellName, std::basic_string<TCHAR> tMaxMaterialParamName );

	// create sg material node given the type
	Simplygon::spShadingNode CreateSGMaterialNode( NodeProxyType nodeType );

	void GetSpShadingNodesFromTable( NodeProxyType nodeType,
	                                 std::basic_string<TCHAR> tChannel,
	                                 ShadingNetworkProxy* materialProxy,
	                                 std::map<int, NodeProxy*>& nodeProxies );

	void GetNodeProxyFromTable( int nodeIndex, NodeProxyType nodeType, NodeProxy* nodeProxy, std::map<int, NodeProxy*>& nodeProxies );

	// clear all shading network info
	void ClearShadingNetworkInfo( bool reset = false );

	// set flag to clear shading network info
	void SetShadingNetworkClearInfo( bool clearFlag, int flagIndex );

	// get a node proxy from the node table
	NodeProxy* GetNodeFromTable( std::basic_string<TCHAR> tNodeName, ShadingNetworkProxy* materialProxy, int* outIndex = nullptr );

	// find upstream texture and color nodes
	Simplygon::spShadingTextureNode FindUpstreamTextureNode( Simplygon::spShadingNode sgNode );
	Simplygon::spShadingColorNode FindUpstreamColorNode( Simplygon::spShadingNode sgNode );

	// take a node proxy in the node table and setup sg network for it
	Simplygon::spShadingNode CreateSgNodeNetwork( int second, int materialId );

	// use material network proxy to initialize Simplygon materials
	void InitializeNodesInNodeTable();

	void SetupMaterialWithCustomShadingNetwork( Simplygon::spMaterial sgMaterial, ShadingNetworkProxy* materialProxy );

	void LogMessageToScriptEditor( std::basic_string<TCHAR> tMessage );

	double GetLODSwitchCameraDistance( int pixelSize );
	double GetLODSwitchPixelSize( double distance );

	void SetEnableEdgeSets( bool val );

	MaterialInfoHandler* GetMaterialInfoHandler();
	WorkDirectoryHandler* GetWorkDirectoryHandler();
	Scene* GetSceneHandler();

	void SetCopyTextures( bool bCopy );
	void SetLinkMeshes( bool bLink );
	void SetLinkMaterials( bool bLink );
	void ClearGlobalMapping();

	void SetMeshFormatString( std::basic_string<TCHAR> tMeshFormatString );
	void SetInitialLODIndex( int initialIndex );

	std::map<std::basic_string<TCHAR>, std::basic_string<TCHAR>> LoadedTexturePathToID;

	private:
	typedef std::pair<unsigned long, std::vector<int>> SelectionSetEdgePair;
	std::map<std::basic_string<TCHAR>, SelectionSetEdgePair> SelectionSetEdgesMap;
	std::map<std::basic_string<TCHAR>, std::set<unsigned long>> SelectionSetObjectsMap;
	std::set<std::basic_string<TCHAR>> SelectionSetsActiveInPipeline;

	std::map<Simplygon::spShadingTextureNode, std::basic_string<TCHAR>> ShadingTextureNodeToPath;

	bool EdgeSetsEnabled;

	bool mapMaterials;
	bool mapMeshes;
	bool copyTextures;

	std::basic_string<TCHAR> inputSceneFile;
	std::basic_string<TCHAR> outputSceneFile;

	std::basic_string<TCHAR> meshFormatString;
	int initialLodIndex;

	MaxMaterialMap* AddMaterial( Mtl* mMaterial, Simplygon::spGeometryData sgMeshData );
	MaxMaterialMap* GetGlobalMaterialMap( Mtl* mMaterial );
	MaxMaterialMap* GetGlobalMaterialMap( std::string sgMaterialId );

	WorkDirectoryHandler* workDirectoryHandler;
	Scene* SceneHandler;

	std::vector<ImportedTexture> ImportedTextures;
	std::map<std::string, int> ImportedUvNameToMaxIndex;
	std::map<int, std::string> ImportedMaxIndexToUv;

	std::map<std::string, GlobalMeshMap> GlobalGuidToMaxNodeMap;

	HANDLE SpawnThreadHandle;
	DWORD SpawnThreadID;
	int SpawnError;
	DWORD SpawnThreadExitValue;
	TCHAR* tLogMessage;
	int logProgress;
	CriticalSection threadLock;

	bool ProcessLODMeshes();
	bool ExtractScene();

	bool
	CreateSceneGraph( INode* mMaxNode, Simplygon::spSceneNode sgNode, std::vector<std::pair<INode*, spSceneMesh>>& mMaxSgMeshList, Simplygon::spScene sgScene );
	bool HasSelectedChildren( INode* mMaxNode );
	bool IsMesh( INode* mMaxNode );
	bool IsCamera( INode* mMaxNode );
	Simplygon::spSceneNode AddCamera( INode* mMaxNode );
	void MakeCameraTargetRelative( INode* mMaxNode, Simplygon::spSceneNode sgNode );

	bool ExtractMapping( size_t meshIndex, Mesh& mMaxMesh );
	bool ExtractGeometry( size_t meshIndex );
	bool ExtractAllGeometries();

	void FindSelectedEdges();
	void FindSelectedObjects();

	void PopulateActiveSets();
	bool NodeExistsInActiveSets( INode* mMaxNode );
	std::set<std::basic_string<TCHAR>> GetSetsForNode( INode* mMaxNode );

	int AddBone( INode* mMaxNode );
	Simplygon::spSceneBone ReplaceNodeWithBone( Simplygon::spSceneNode sgNode );
	INode* GetMaxBoneById( std::string sBoneId );

	bool WritebackMapping( size_t lodIndex, Mesh& mMaxMesh, Simplygon::spSceneMesh sgMesh );
	void WriteSGTexCoordsToMaxChannel( Simplygon::spRealArray sgTexCoords, Mesh& mMaxMesh, int maxChannel, uint cornerCount, uint triangleCount );
	void WriteSGVertexColorsToMaxChannel( Simplygon::spRealArray sgVertexColors, Mesh& mMaxMesh, int maxChannel, uint cornerCount, uint triangleCount );
	void SetupVertexColorData(
	    Mesh& mMaxMesh, int mappingChannel, UVWMapper& mUVWMapper, uint TriangleCount, uint VertexCount, Simplygon::spRealArray sgVertexColors );
	bool
	WritebackGeometry( Simplygon::spScene sgProcessedScene, size_t lodIndex, spSceneMesh sgMesh, std::map<std::string, INode*>& meshNodesThatNeedsParents );

	bool ImportProcessedScenes();

	TSTR GetNonCollidingMeshName( TSTR tNodeName );
	void CreateSgMaterialChannel( long maxChannelID, StdMat2* mMaxStdMaterial, Simplygon::spMaterial sgMaterial, bool* hasTextures );

	bool MaterialChannelHasShadingNetwork( Simplygon::spMaterial sgMaterial, const char* cChannelName );

	void CreateShadingNetworkForStdMaterial( long maxChannelID,
	                                         StdMat2* mMaxStdMaterial,
	                                         spMaterial sgMaterial,
	                                         std::basic_string<TCHAR> tMaxMappingChannel,
	                                         std::basic_string<TCHAR> tMappedChannelName,
	                                         std::basic_string<TCHAR> tFilePath,
	                                         float blendAmount,
	                                         bool bIsSRGB,
	                                         BitmapTex* mBitmapTex,
	                                         bool* hasTextures );

	std::pair<std::string, int> AddMaxMaterialToSgScene( Mtl* mMaxMaterial );

	Mtl* CreateMaterial( spScene sgProcessedScene,
	                     spSceneMesh sgProcessedMesh,
	                     size_t lodIndex,
	                     std::basic_string<TCHAR> sgMeshName,
	                     std::basic_string<TCHAR> sgMaterialName,
	                     uint globalMaterialIndex );

	bool ImportMaterialTexture( spScene sgProcessedScene,
	                            spMaterial sgMaterial,
	                            const TCHAR* tNodeName,
	                            const TCHAR* tChannelName,
	                            int maxChannelID,
	                            BitmapTex** mBitmapTex,
	                            std::basic_string<TCHAR> tMeshNameOverride,
	                            std::basic_string<TCHAR> tMaterialNameOverride );

	PBBitmap* ImportMaterialTexture( spScene sgProcessedScene,
	                                 spMaterial sgMaterial,
	                                 const TCHAR* tNodeName,
	                                 const TCHAR* tChannelName,
	                                 std::basic_string<TCHAR> tMeshNameOverride,
	                                 std::basic_string<TCHAR> tMaterialNameOverride );

	Mtl* SetupMaxStdMaterial( spScene sgProcessedScene, std::basic_string<TCHAR> tMeshName, Simplygon::spMaterial sgMaterial, TSTR tNodeName, TCHAR* tLODName );

#if MAX_VERSION_MAJOR >= 23
	Mtl*
	SetupPhysicalMaterial( spScene sgProcessedScene, std::basic_string<TCHAR> tMeshName, Simplygon::spMaterial sgMaterial, TSTR tNodeName, TCHAR* tLODName );
#endif

	bool RunSimplygonProcess();
	bool ProcessScene();
	void AddLogString( const TCHAR* tMessage );

	static DWORD WINAPI StaticProcessingThread( LPVOID lpParameter );
	static INT_PTR CALLBACK AppDialogProc( HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam );

	void CleanUp();

	Matrix3 GetRelativeTransformation( INode* mMaxNode );

	void AddEdgeCollapse( INode* mMaxNode, spGeometryData sgMeshData );
	void AddToObjectSelectionSet( INode* mMaxNode );

	private:
	void ThreadLock(){};
	void ThreadUnlock(){};
	void Modified(){};

	std::vector<MaterialInfo> CachedMaterialInfos;

	friend class SimplygonObserver;

	public:
	Simplygon::spPipeline sgPipeline;

	bool UseSettingsPipelineForProcessing( const INT64 pipelineId );

	std::basic_string<TCHAR> ImportTexture( std::basic_string<TCHAR> tFilePath );

	void LogToWindow( std::basic_string<TCHAR> tMessage, ErrorType errorType = Info, bool sleep = false );
	void LogMaterialNodeMessage( Texmap* mTexMap, std::basic_string<TCHAR> tMaterialName, std::basic_string<TCHAR> tChannelName );

};

extern SimplygonMax* SimplygonMaxInstance;

#endif //__SIMPLYGONMAXPLUGIN_H__
