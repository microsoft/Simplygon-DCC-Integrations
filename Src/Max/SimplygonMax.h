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

class MorpherChannelSettings;

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
enum GlobalMorpherSettingsType
{
	UseLimits = 0,
	SpinnerMin,
	SpinnerMax,
	UseSelection,
	ValueIncrements,
	AutoLoadTargets,
	NumGlobalSettings
};

class GlobalMorpherSettings
{
	public:
	bool useLimits;
	float spinnerMin;
	float spinnerMax;
	bool useSelection;
	int valueIncrements;
	bool autoLoadTargets;

	GlobalMorpherSettings()
	{
		this->useLimits = true;
		this->spinnerMin = 0.f;
		this->spinnerMax = 100.f;
		this->useSelection = false;
		this->valueIncrements = 1;
		this->autoLoadTargets = false;
	}
};

class MorphTargetMetaData
{
	private:
	size_t originalMorphTargetIndex;
	std::basic_string<TCHAR> name;

	public:
	float weight;

	private:
	void InitUIParameters() { this->weight = 100.f; }

	public:
	MorphTargetMetaData( size_t originalMorphTargetIndex, std::basic_string<TCHAR> tMorphTargetName, float progressiveWeight )
	{
		this->InitUIParameters();
		this->originalMorphTargetIndex = originalMorphTargetIndex;
		this->name = tMorphTargetName;
		this->weight = progressiveWeight;
	}

	size_t GetIndex() const { return this->originalMorphTargetIndex; }
	std::basic_string<TCHAR> GetName() { return this->name; }
};

class MorphChannelMetaData
{
	public:
	float morphWeight;
	float tension;
	float minLimit;
	float maxLimit;
	bool useVertexSelection;
	bool useLimits;

	private:
	size_t originalChannelIndex;
	int channelIndex;

	public:
	std::vector<MorphTargetMetaData*> morphTargetMetaData;

	size_t GetOriginalIndex() const { return this->originalChannelIndex; }
	int GetIndex() const { return this->channelIndex; }

	private:
	void InitUIParameters()
	{
		this->morphWeight = 0.f;
		this->tension = 0.5f;
		this->minLimit = 0.f;
		this->maxLimit = 100.f;
		this->useVertexSelection = false;
		this->useLimits = false;
	}

	public:
	MorphChannelMetaData( size_t originalChannelIndex, int channelIndex )
	{
		this->originalChannelIndex = originalChannelIndex;
		this->channelIndex = channelIndex;
		this->InitUIParameters();
	}

	~MorphChannelMetaData()
	{
		for( MorphTargetMetaData* morphTargetMetaData : this->morphTargetMetaData )
		{
			delete morphTargetMetaData;
		}
		this->morphTargetMetaData.clear();
	}

	void AddProgressiveMorphTarget( size_t originalMorphTargetIndex, std::basic_string<TCHAR> tMorphTargetName, float progressiveWeight )
	{
		morphTargetMetaData.push_back( new MorphTargetMetaData( originalMorphTargetIndex, tMorphTargetName, progressiveWeight ) );
	}
};

class MorpherMetaData
{
	public:
	GlobalMorpherSettings globalSettings;

	std::vector<MorphChannelMetaData*> morphTargetMetaData;

	MorpherMetaData() {}

	~MorpherMetaData()
	{
		for( MorphChannelMetaData* morphChannelMetaData : this->morphTargetMetaData )
		{
			delete morphChannelMetaData;
		}
		this->morphTargetMetaData.clear();
	}
};

class GlobalMeshMap
{
	private:
	std::string sgId;
	std::basic_string<TCHAR> name;
	ULONG maxId;

	MorpherMetaData* morpherMetaData;

	public:
	MorpherMetaData* CreateMorpherMetaData()
	{
		if( this->morpherMetaData )
			delete this->morpherMetaData;

		this->morpherMetaData = new MorpherMetaData();

		return this->morpherMetaData;
	}

	bool HasMorpherMetaData() const { return this->morpherMetaData != nullptr; }
	bool HasMorphTargets() const { return this->morpherMetaData != nullptr ? this->morpherMetaData->morphTargetMetaData.size() > 0 : false; }

	MorpherMetaData* GetMorpherMetaData() const { return this->morpherMetaData; }

	GlobalMeshMap( std::string sSgId, std::basic_string<TCHAR> tName, ULONG uMaxId )
	    : sgId( sSgId )
	    , name( tName )
	    , maxId( uMaxId )
	    , morpherMetaData( nullptr )
	{
	}

	~GlobalMeshMap()
	{
		if( this->morpherMetaData )
		{
			delete this->morpherMetaData;
			this->morpherMetaData = nullptr;
		}
	}

	std::string GetSimplygonId() const { return this->sgId; }
	std::basic_string<TCHAR> GetName() const { return this->name; }
	ulong GetMaxId() const { return this->maxId; }
};
#pragma endregion

void FindAllUpStreamTextureNodes( Simplygon::spShadingNode sgShadingNode, std::map<std::basic_string<TCHAR>, Simplygon::spShadingTextureNode>& sgTextureNodes );

static void RegisterMorphScripts();
void GetActiveMorphChannels( ulong uniqueHandle, MorpherChannelSettings* morpherSettings );
void GetMorphChannelWeights( ulong uniqueHandle, std::vector<float>& mActiveMorphChannelWeights );
void GetActiveMorphTargetTension( ulong uniqueHandle, MorpherChannelSettings* morpherSettings );
void GetMorphChannelPoints( ulong uniqueHandle, std::vector<Point3>& mMorphChannelPoints, size_t channelIndex );
bool GetMorphChannelName( ulong uniqueHandle, size_t channelIndex, std::basic_string<TCHAR>& channelName );
void GetActiveMorphTargetProgressiveWeights( ulong uniqueHandle, size_t channelIndex, std::vector<float>& mMorphWeights );
void GetActiveMinLimits( ulong uniqueHandle, MorpherChannelSettings* morpherSettings );
void GetActiveMaxLimits( ulong uniqueHandle, MorpherChannelSettings* morpherSettings );
void GetActiveUseVertexSelections( ulong uniqueHandle, MorpherChannelSettings* morpherSettings );
void GetActiveUseLimits( ulong uniqueHandle, MorpherChannelSettings* morpherSettings );

namespace MaterialNodes {

class TextureData
{
	public:
	TextureData( Texmap* mTex )
	    : mBitmap( (BitmapTex*)mTex )
	    , mIsSRGB( false )
	    , mUseAlphaAsTransparency( false )
	    , mPremultipliedAlpha( true )
	    , mHasAlpha( false )
	    , mAlphaSource( 0 )
	{
	}
	~TextureData() {}

	BitmapTex* mBitmap;

	std::basic_string<TCHAR> mFilePath;
	std::basic_string<TCHAR> mTexturePathWithName;
	std::basic_string<TCHAR> mTextureName;
	std::basic_string<TCHAR> mTextureExtension;
	std::basic_string<TCHAR> mTextureNameWithExtension;

	bool mIsSRGB;
	bool mUseAlphaAsTransparency;
	bool mPremultipliedAlpha;
	bool mHasAlpha;
	int mAlphaSource;
};

class TextureSettingsOverride
{
	public:
	TextureSettingsOverride()
	    : mEnabledSRGBOverride( false )
	    , mSRGB( false )
	    , mEnabledAlphaSourceOverride( false )
	    , mAlphaSource( 0 )
	    , mEnabledPremultOverride( false )
	    , mPremultipliedAlpha( false )
	{
	}
	~TextureSettingsOverride() {}
	bool mEnabledSRGBOverride;
	bool mSRGB;
	bool mEnabledAlphaSourceOverride;
	int mAlphaSource;
	bool mEnabledPremultOverride;
	bool mPremultipliedAlpha;
};

class MaterialChannelData
{
	public:
	// ConstCharPtrToLPCTSTR
	// LPCTSTRToConstCharPtr

	MaterialChannelData( std::basic_string<TCHAR> tMaterialName,
	                     std::basic_string<TCHAR> tChannelName,
	                     const long maxChannelId,
	                     StdMat2* maxStdMaterial,
	                     spMaterial sgMaterial,
	                     std::vector<MaterialTextureOverride>* materialTextureOverrides,
	                     TimeValue time,
	                     const bool isPbr )
	    : mMaterialName( tMaterialName )
	    , mChannelName( tChannelName )
	    , mMaxChannelId( maxChannelId )
	    , mMaxStdMaterial( maxStdMaterial )
	    , mSGMaterial( sgMaterial )
	    , mMaterialTextureOverrides( materialTextureOverrides )
	    , mTime( time )
	    , mIsMatPBR( isPbr )
	{
	}
	~MaterialChannelData() {}

	const bool IsPBR() { return mIsMatPBR; }
	const bool IsSTD() { return !mIsMatPBR; }

	std::basic_string<TCHAR> mMaterialName;
	std::basic_string<TCHAR> mChannelName;

	const long mMaxChannelId;

	StdMat2* mMaxStdMaterial;
	spMaterial mSGMaterial;

	std::vector<MaterialTextureOverride>* mMaterialTextureOverrides;
	TimeValue mTime;

	private:
	const bool mIsMatPBR;
};

struct ColorCorrectionData
{
	// rewire RGBA
	int mRewireMode;
	int mRewireR;
	int mRewireG;
	int mRewireB;
	int mRewireA;

	float mHueShift;
	float mSaturation;
	AColor mHueTint;
	float mHueTintStrength;
	int mLightnessMode;
	float mContrast;
	float mBrightness;
	int mExposureMode;
	bool mEnableR;
	bool mEnableG;
	bool mEnableB;

	// Lightness Gain
	float mGainRGB;
	float mGainR;
	float mGainG;
	float mGainB;

	// Lightness Gamma
	float mGammaRGB;
	float mGammaR;
	float mGammaG;
	float mGammaB;

	// Lightness pivot
	float mPivotRGB;
	float mPivotR;
	float mPivotG;
	float mPivotB;

	// Lightness lift
	float mLiftRGB;
	float mLiftR;
	float mLiftG;
	float mLiftB;

	float mPrinterLights;
};

enum MultiplyNodeAlphaFrom
{
	AlphaFirstSource = 0,
	AlphaSecondSource,
	AlphaBlendSource
};

enum eMaxBlendMode
{
	eNormal = 0,
	eAverage,
	eAddition,
	eSubtract,
	eDarken,
	eMultiply,
	eColor_Burn,
	eLinear_Burn,
	eLighten,
	eScreen,
	eColor_Dodge,
	eLinear_Dodge,
	eSpotlight,
	eSpotlight_Blend,
	eOverlay,
	eSoft_Light,
	eHard_Light,
	ePin_Light,
	eHard_Mix,
	eDifference,
	eExclusion,
	eHue,
	eSaturation,
	eColor,
	eValue
};

enum eMaxColorCorrectionSwizzle
{
	Red = 0,
	Green,
	Blue,
	Alpha,
	invRed,
	invGreen,
	invBlue,
	invAlpha,
	Monochrome,
	One,
	Zero
};

enum eMaxRewireMode
{
	reWireNormal,
	reWireMonochrome,
	reWireInvert,
	reWireCustom
};

const bool GetData( IParamBlock2* mParamBlock, const ParamID paramId, const TimeValue time, std::vector<AColor>* outValue );

const bool GetData( IParamBlock2* mParamBlock, const ParamID paramId, const TimeValue time, std::vector<int>* outValue );

const bool GetData( IParamBlock2* mParamBlock, const ParamID paramId, const TimeValue time, std::vector<float>* outValue );

const bool GetData( IParamBlock2* mParamBlock, const ParamID paramId, const TimeValue time, std::vector<std::basic_string<TCHAR>>* outValue );

const bool GetData( IParamBlock2* mParamBlock, const ParamID paramId, const TimeValue time, std::vector<Color>* outValue );

const bool GetData( IParamBlock2* mParamBlock, const ParamID paramId, const TimeValue time, std::vector<eMaxBlendMode>* outValue );

template <typename T> bool GetTexMapProperty( Texmap* mTexMap, std::basic_string<TCHAR> tPropertyName, TimeValue time, T* outValue )
{
	const int numParamBlocks = mTexMap->NumParamBlocks();
	for( int paramBlockIndex = 0; paramBlockIndex < numParamBlocks; ++paramBlockIndex )
	{
		IParamBlock2* paramBlock = mTexMap->GetParamBlock( paramBlockIndex );
		if( paramBlock )
		{
			return GetStdMaterialProperty<T>( paramBlock, tPropertyName, time, outValue );
		}
	}

	return false;
}

template <typename T> bool GetStdMaterialProperty( IParamBlock2* mParamBlock, std::basic_string<TCHAR> tPropertyName, TimeValue time, T* outValue )
{
	if( !mParamBlock )
		return false;
	else if( !outValue )
		return false;

	const int numParams = mParamBlock->NumParams();
	for( int paramIndex = 0; paramIndex < numParams; ++paramIndex )
	{
		const ParamID paramId = (ParamID)mParamBlock->IDtoIndex( (ParamID)paramIndex );

		if( paramId != -1 )
		{
			const ParamDef& paramDef = mParamBlock->GetParamDef( paramId );
			if( paramDef.int_name == nullptr )
				continue;

			const bool bNameIsEqual = ( std::basic_string<TCHAR>( paramDef.int_name ).compare( tPropertyName ) == 0 );
			if( bNameIsEqual )
			{
				// expand if needed.
				if( typeid( T ) == typeid( AColor ) )
				{
					*(AColor*)outValue = mParamBlock->GetAColor( paramId, time );
					return true;
				}
				else if( typeid( T ) == typeid( Color ) )
				{
					*(Color*)outValue = mParamBlock->GetColor( paramId, time );
					return true;
				}
				else if( typeid( T ) == typeid( int ) )
				{
					*(int*)outValue = mParamBlock->GetInt( paramId, time );
					return true;
				}
				else if( typeid( T ) == typeid( float ) )
				{
					*(float*)outValue = mParamBlock->GetFloat( paramId, time );
					return true;
				}
				else if( typeid( T ) == typeid( std::basic_string<TCHAR> ) )
				{
					*(std::basic_string<TCHAR>*)outValue = mParamBlock->GetStr( paramId, time );
					return true;
				}
			}
		}
	}

	return false;
}

template <typename T> bool GetTexMapProperties( Texmap* mTexMap, std::basic_string<TCHAR> tPropertyName, TimeValue time, std::vector<T>* outValue )
{
	const int numParamBlocks = mTexMap->NumParamBlocks();
	for( int paramBlockIndex = 0; paramBlockIndex < numParamBlocks; ++paramBlockIndex )
	{
		IParamBlock2* paramBlock = mTexMap->GetParamBlock( paramBlockIndex );

		if( paramBlock )
		{
			return GetStdMaterialProperties<T>( paramBlock, tPropertyName, time, outValue );
		}
	}

	return true;
}

template <typename T>
bool GetStdMaterialProperties( IParamBlock2* mParamBlock, std::basic_string<TCHAR> tPropertyName, TimeValue time, std::vector<T>* outValue )
{
	if( !mParamBlock )
		return false;
	else if( !outValue )
		return false;

	const int numParams = mParamBlock->NumParams();
	for( int paramIndex = 0; paramIndex < numParams; ++paramIndex )
	{
		const ParamID paramId = (ParamID)mParamBlock->IDtoIndex( (ParamID)paramIndex );

		if( paramId != -1 )
		{
			const ParamDef& paramDef = mParamBlock->GetParamDef( paramId );
			if( paramDef.int_name == nullptr )
				continue;

			const bool bNameIsEqual = ( std::basic_string<TCHAR>( paramDef.int_name ).compare( tPropertyName ) == 0 );
			if( bNameIsEqual )
			{
				// expand if needed.
				if( typeid( T ) == typeid( Color ) )
				{
					return GetData( mParamBlock, paramId, time, reinterpret_cast<std::vector<Color>*>( outValue ) );
				}
				else if( typeid( T ) == typeid( AColor ) )
				{
					return GetData( mParamBlock, paramId, time, reinterpret_cast<std::vector<AColor>*>( outValue ) );
				}
				else if( typeid( T ) == typeid( int ) )
				{
					return GetData( mParamBlock, paramId, time, reinterpret_cast<std::vector<int>*>( outValue ) );
				}
				else if( typeid( T ) == typeid( float ) )
				{
					return GetData( mParamBlock, paramId, time, reinterpret_cast<std::vector<float>*>( outValue ) );
				}
				else if( typeid( T ) == typeid( eMaxBlendMode ) )
				{
					return GetData( mParamBlock, paramId, time, reinterpret_cast<std::vector<eMaxBlendMode>*>( outValue ) );
				}
				else if( typeid( T ) == typeid( std::basic_string<TCHAR> ) )
				{
					return GetData( mParamBlock, paramId, time, reinterpret_cast<std::vector<std::basic_string<TCHAR>>*>( outValue ) );
				}
			}
		}
	}

	return false;
}

spShadingNode GetColorCorrectionLightSettings( ColorCorrectionData& sgColorCorrectionData,
                                               spShadingNode sgInputColor,
                                               spShadingColorNode sgGainRGBNode,
                                               spShadingColorNode sgGammaRGBNode,
                                               spShadingColorNode sgPivotRGBNode,
                                               spShadingColorNode sgLiftRGBNode );

spShadingNode GetShadingNode( MaterialNodes::TextureData& textureData, std::basic_string<TCHAR> tMaxMappingChannel, TimeValue time );

spShadingNode SetUpMultiplyShadingNode( spShadingNode sgInputNodes[ 2 ],
                                        MaterialNodes::MultiplyNodeAlphaFrom alphaFrom,
                                        std::basic_string<TCHAR>& tMaterialName,
                                        TimeValue time );

spShadingNode RunMultiplyNode( Texmap* mTexMap, MaterialNodes::MaterialChannelData& mChannelData );

spShadingNode SetUpBitmapShadingNode( std::basic_string<TCHAR>& tMaterialName,
                                      std::basic_string<TCHAR>& tMaxMappingChannel,
                                      MaterialNodes::TextureData& rTextureData,
                                      TimeValue time );

spShadingNode
RunBitmapNode( Texmap* mTexMap, MaterialNodes::MaterialChannelData& mChannelData, MaterialNodes::TextureSettingsOverride* textureSettingsOverride = nullptr );

spShadingNode SetUpTintShadingNode(
    spShadingNode& sgInputNode, std::basic_string<TCHAR>& tMaterialName, Color& redChannel, Color& greenChannel, Color& blueChannel, TimeValue time );

spShadingNode RunTintNode( Texmap* mTexMap, MaterialNodes::MaterialChannelData& mChannelData );

spShadingNode SetUpCompositeShadingNode( std::vector<spShadingNode>& TextureNodes,
                                         std::vector<spShadingNode>& MaskNodes,
                                         std::vector<ETextureBlendType>& TextureBlendTypes,
                                         std::vector<float>& Opacity,
                                         std::basic_string<TCHAR>& tMaterialName,
                                         TimeValue time );

spShadingNode RunCompositeNode( Texmap* mTexMap, MaterialNodes::MaterialChannelData& mChannelData );

spShadingNode
SetUpColorCorrectionShadingNode( spShadingNode sgInputNode, ColorCorrectionData& ColorCorrectionData, std::basic_string<TCHAR>& tMaterialName, TimeValue time );

spShadingNode ReWireColorCorrectionNode( spShadingNode& sgInputNode,
                                         eMaxColorCorrectionSwizzle red,
                                         eMaxColorCorrectionSwizzle green,
                                         eMaxColorCorrectionSwizzle blue,
                                         eMaxColorCorrectionSwizzle alpha );

spShadingNode RunColorCorrectionNode( Texmap* mTexMap, MaterialNodes::MaterialChannelData& mChannelData );

void PopulateTextureNode( spShadingTextureNode sgTextureNode,
                          BitmapTex* mBitmapTex,
                          std::basic_string<TCHAR>& tMaxMappingChannel,
                          std::basic_string<TCHAR>& tTextureName,
                          TimeValue& time,
                          const bool isSRGB );

spShadingTextureNode CreateTextureNode(
    BitmapTex* mBitmapTex, std::basic_string<TCHAR>& tMaxMappingChannel, std::basic_string<TCHAR>& tTextureName, TimeValue& time, const bool isSRGB );
};

class SimplygonMax : public SimplygonEventRelay
{
	public:
	virtual void ProgressCallback( int progress );
	virtual void ErrorCallback( const TCHAR* tErrorMessage );

	void Callback( std::basic_string<TCHAR> tId, bool bIsError, std::basic_string<TCHAR> tMessage, int progress );

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

	// If true, the plugin will generate a material for the LODs
	SgValueMacro( bool, GenerateMaterial );

	// Sets the pipeline runmode enum (int)
	SgValueMacro( int, PipelineRunMode );

	// If true, allow fallback to scene mapping during import.
	// This is intended to allow import of Simplygon scenes into other sessions of Max,
	// that does not include in-memory mapping.
	SgValueMacro( bool, AllowUnsafeImport );

	// Sets the LOD prefix
	SgValueMacro( std::basic_string<TCHAR>, DefaultPrefix );

	// Sets the output texture directory
	SgValueMacro( std::basic_string<TCHAR>, TextureOutputDirectory );

	// Sets the name of the settings object to use
	SgValueMacro( std::basic_string<TCHAR>, SettingsObjectName );

	// Resets all values to default values
	void Reset();

	// Initializes Simplygon SDK and sets up event handlers
	bool Initialize();

	// Reduces the currently selected geometries
	bool ProcessSelectedGeometries();

	// Reduces the scene from the given file
	bool ProcessSceneFromFile( std::basic_string<TCHAR> tImportFilePath, std::basic_string<TCHAR> tExportFilePath );

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

	static void RegisterMorphScripts();
	void GetActiveMorphChannels( ulong uniqueHandle, MorpherChannelSettings* morpherSettings );
	void GetMorphChannelWeights( ulong uniqueHandle, std::vector<float>& mActiveMorphChannelWeights );
	void GetMorphChannelPoints( ulong uniqueHandle, std::vector<Point3>& mMorphChannelPoints, size_t channelIndex );
	bool GetMorphChannelName( ulong uniqueHandle, size_t channelIndex, std::basic_string<TCHAR>& name );
	void GetActiveMorphTargetProgressiveWeights( ulong uniqueHandle, size_t channelIndex, std::vector<float>& mActiveProgressiveWeights );
	void GetActiveMorphTargetTension( ulong uniqueHandle, MorpherChannelSettings* morpherSettings );
	void SetMorphChannelTension( ulong uniqueHandle, size_t channelIndex, float tension );
	void SetMorphTarget( ulong uniqueHandle, ulong uniqueTargetHandle, size_t channelIndex );
	void SetMorphChannelWeight( ulong uniqueHandle, size_t channelIndex, float weight );
	void AddProgressiveMorphTarget( ulong uniqueHandle, ulong uniqueTargetHandle, size_t channelIndex );
	void SetProgressiveMorphTargetWeight( ulong uniqueHandle, size_t channelIndex, size_t progressiveIndex, float weight );
	void GetActiveUseVertexSelections( ulong uniqueHandle, MorpherChannelSettings* morpherSettings );
	void SetChannelUseVertexSelection( ulong uniqueHandle, size_t channelIndex, bool bUseVertexSelection );
	void GetActiveMinLimits( ulong uniqueHandle, MorpherChannelSettings* morpherSettings );
	void GetActiveMaxLimits( ulong uniqueHandle, MorpherChannelSettings* morpherSettings );
	void SetChannelMinLimit( ulong uniqueHandle, size_t channelIndex, float minLimit );
	void SetChannelMaxLimit( ulong uniqueHandle, size_t channelIndex, float maxLimit );
	void GetActiveUseLimits( ulong uniqueHandle, MorpherChannelSettings* morpherSettings );
	void SetChannelUseLimits( ulong uniqueHandle, size_t channelIndex, bool bUseLimits );

	public:
	SimplygonMax();
	virtual ~SimplygonMax();

	ExtractionType extractionType;

	protected:
	Interface* MaxInterface;
	TimeValue CurrentTime;

	_locale_t MaxScriptLocale;

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

	std::basic_string<TCHAR> SetupMaxMappingChannel( const char* cMaterialName, const char* cChannelName, Texmap* mTexMap );

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
	MaxMaterialMap* GetGlobalMaterialMapUnsafe( Mtl* mMaterial );

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

	void WriteMaterialMappingAttribute();
	void ReadMaterialMappingAttribute( Simplygon::spScene sgScene );

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

	bool MaterialChannelHasShadingNetwork( Simplygon::spMaterial sgMaterial, const char* cChannelName );

	public:
	spShadingNode CreateSgMaterialPBRChannel( Texmap* mTexMap, long maxChannelId, const char* cMaterialName, const char* cChannelName );

	void CreateAndLinkTexture( MaterialNodes::TextureData& rTextureData );

	void CreateSgMaterialSTDChannel( long maxChannelID, StdMat2* mMaxStdMaterial, Simplygon::spMaterial sgMaterial, bool* hasTextures );

	spShadingNode CreateSgMaterial( Texmap* mTexMap,
	                                MaterialNodes::MaterialChannelData& mMaterialChannel,
	                                MaterialNodes::TextureSettingsOverride* textureSettingsOverride = nullptr );

	private:
	void ApplyChannelSpecificModifiers( long maxChannelId, StdMat2* mMaxStdMaterial, std::basic_string<TCHAR> tMaterialName, Color* outColor, float* outAlpha );

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
	void RequiredCleanUp();
	void CleanUpGlobalMaterialMappingData();

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
	void LogMaterialNodeMessage( Texmap* mTexMap,
	                             std::basic_string<TCHAR> tMaterialName,
	                             std::basic_string<TCHAR> tChannelName,
	                             bool partialFail = false,
	                             std::basic_string<TCHAR> tExtendedInformation = L"" );
};

extern SimplygonMax* SimplygonMaxInstance;

void GlobalLogMaterialNodeMessage( Texmap* mTexMap,
                                   std::basic_string<TCHAR> tMaterialName,
                                   std::basic_string<TCHAR> tChannelName,
                                   const bool partialFail = false,
                                   std::basic_string<TCHAR> tExtendedInformation = L"" );

#endif //__SIMPLYGONMAXPLUGIN_H__
